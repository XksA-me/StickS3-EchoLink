#include <Arduino.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <M5Unified.h>
#include <Preferences.h>
#include <WiFi.h>
#include <WebServer.h>

#include <PubSubClient.h>
#include <esp_err.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <lwip/sockets.h>

#include "Config.h"
#include "LifeTimeApp.h"
#include "VoiceProtocol.h"

namespace {

constexpr size_t kMaxAudioBytes = ECHOLINK_SAMPLE_RATE * ECHOLINK_MAX_RECORD_SECONDS;
constexpr size_t kMaxChunks =
    (kMaxAudioBytes + ECHOLINK_AUDIO_CHUNK_BYTES - 1) / ECHOLINK_AUDIO_CHUNK_BYTES;
constexpr size_t kMicrophoneBufferCount = 3;
constexpr uint8_t kMaxUnreadVoices = 5;
constexpr uint8_t kMaxOutgoingVoices = 5;
constexpr size_t kMaxTransportFrameBytes =
    sizeof(echolink::PacketHeader) + ECHOLINK_AUDIO_CHUNK_BYTES;
constexpr uint8_t kIncomingFrameQueueDepth = 48;
constexpr uint32_t kMqttConnectTimeoutSeconds = 2;
constexpr uint32_t kMqttRetryIntervalMs = 1000;
constexpr uint8_t kMqttMaxAttempts = 3;
constexpr uint32_t kMqttRetryPauseMs = 15000;
constexpr uint32_t kDisplayDimMs = ECHOLINK_DISPLAY_DIM_SECONDS * 1000UL;
constexpr uint32_t kDisplayIdleMs = ECHOLINK_DISPLAY_SLEEP_SECONDS * 1000UL;
constexpr uint32_t kAPlayOrPttThresholdMs = 350;
constexpr uint32_t kDiagnosticIntervalMs = 30000;

enum class UiState : uint8_t {
  Ready,
  Recording,
  Sending,
  Receiving,
  Playing,
  Setup,
  Error,
};

enum class TransportMode : uint8_t {
  Local = 0,
  WiFi = 1,
};

enum class AppMode : uint8_t {
  EchoLink = 0,
  LifeTime = 1,
  Launcher = 2,
};

enum class OutgoingPhase : uint8_t {
  Start,
  Audio,
  End,
};

struct NetworkSettings {
  String ssid;
  String password;
  String mqttHost;
  uint16_t mqttPort = 1883;
  String mqttUser;
  String mqttPassword;
  String mqttTopic = "echolink/v1/voice";
};

struct WiFiProfile {
  String ssid;
  String password;
};

constexpr uint8_t kMaxWiFiProfiles = 5;
constexpr uint8_t kMaxScannedWiFiNetworks = 16;

int8_t* txAudio = nullptr;
int8_t* rxAudio = nullptr;
int8_t* voiceQueueData[kMaxUnreadVoices] = {};
size_t voiceQueueBytes[kMaxUnreadVoices] = {};
bool voiceQueueUnread[kMaxUnreadVoices] = {};
uint8_t voiceQueueCount = 0;
uint8_t unreadVoiceCount = 0;
struct OutgoingVoice {
  int8_t* data = nullptr;
  size_t bytes = 0;
  uint16_t sessionId = 0;
};
struct TransportFrame {
  uint16_t length = 0;
  uint8_t data[kMaxTransportFrameBytes] = {};
};
struct MqttPublishRequest {
  uint32_t generation = 0;
  uint32_t id = 0;
  TransportFrame frame;
};
struct MqttPublishResult {
  uint32_t generation = 0;
  uint32_t id = 0;
  bool success = false;
};
OutgoingVoice outgoingVoices[kMaxOutgoingVoices];
uint8_t outgoingVoiceHead = 0;
uint8_t outgoingVoiceCount = 0;
OutgoingPhase outgoingPhase = OutgoingPhase::Start;
size_t outgoingOffset = 0;
uint16_t outgoingSequence = 0;
bool mqttPublishPending = false;
uint32_t pendingMqttPublishId = 0;
uint32_t nextMqttPublishId = 0;
bool* rxChunkSeen = nullptr;
int16_t microphoneSamples[kMicrophoneBufferCount][ECHOLINK_AUDIO_CHUNK_BYTES];
size_t txAudioBytes = 0;
size_t rxExpectedBytes = 0;
size_t rxReceivedBytes = 0;
uint16_t rxSessionId = 0;
uint32_t rxSenderId = 0;
uint32_t deviceId = 0;
uint8_t volume = 160;
UiState uiState = UiState::Ready;
bool microphoneEnabled = false;
bool speakerEnabled = false;
bool recordingStopRequested = false;
size_t microphoneWriteIndex = 0;
size_t microphoneBlocksQueued = 0;
bool voiceAlertPending = false;
UiState lastDrawnState = UiState::Error;
uint8_t lastDrawnVolume = 0;
uint32_t lastUiAnimation = 0;
uint8_t uiAnimationFrame = 0;
char lastDrawnMessage[32] = "";
uint32_t droppedPackets = 0;
uint32_t droppedMessages = 0;
volatile uint32_t txFramesDelivered = 0;
volatile uint32_t txFramesFailed = 0;
char lastResult[32] = "Waiting for voice";
char errorMessage[32] = "";
TransportMode activeTransport = TransportMode::Local;
NetworkSettings networkSettings;
WiFiProfile wifiProfiles[kMaxWiFiProfiles];
uint8_t wifiProfileCount = 0;
uint8_t activeWiFiProfile = 0;
WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);
QueueHandle_t incomingFrameQueue = nullptr;
QueueHandle_t mqttPublishQueue = nullptr;
QueueHandle_t mqttPublishResultQueue = nullptr;
TaskHandle_t mqttWorkerHandle = nullptr;
volatile bool mqttWorkerEnabled = false;
volatile bool mqttReady = false;
volatile uint32_t mqttTransportGeneration = 0;
volatile int mqttWorkerState = MQTT_DISCONNECTED;
volatile uint8_t mqttWorkerAttempts = 0;
bool wifiPowerSaveDisabled = false;
uint32_t lastMqttConnectAttempt = 0;
volatile uint32_t mqttRetryPausedUntil = 0;
Preferences preferences;
WebServer configServer(80);
DNSServer dnsServer;
bool espNowActive = false;
bool configServerStarted = false;
bool portalActive = false;
bool mdnsActive = false;
bool restartTransportRequested = false;
uint32_t configWindowUntil = 0;
uint32_t configSaveUntil = 0;
bool bPortalHoldHandled = false;
wl_status_t lastWiFiStatus = WL_NO_SHIELD;
int lastMqttState = 99;
uint32_t lastNetworkStatusLog = 0;
struct ScannedWiFiNetwork {
  String ssid;
  int32_t rssi = 0;
};
ScannedWiFiNetwork scannedWiFiNetworks[kMaxScannedWiFiNetworks];
uint8_t scannedWiFiNetworkCount = 0;
bool wifiScanInProgress = false;
uint32_t lastWiFiScanStart = 0;
char setupInfo[32] = "";
uint8_t setupInfoPage = 0;
uint32_t lastSetupInfoChange = 0;
bool displaySleeping = false;
bool displayDimmed = false;
uint32_t lastUserActivity = 0;
bool aPlayPending = false;
uint32_t aPressedAt = 0;
uint32_t lastDiagnosticHeartbeat = 0;
AppMode activeApp = AppMode::EchoLink;
AppMode launcherSelection = AppMode::EchoLink;
uint32_t appChordStartedAt = 0;
bool appChordHandled = false;

void fail(const char* message);
String deviceSuffix();
void serviceIncomingTransportFrames(uint8_t budget = 8);

const char* stateText(UiState state) {
  switch (state) {
    case UiState::Ready: return "READY";
    case UiState::Recording: return "RECORDING";
    case UiState::Sending: return "SENDING";
    case UiState::Receiving: return "RECEIVING";
    case UiState::Playing: return "PLAYING";
    case UiState::Setup: return "PORTAL";
    case UiState::Error: return "ERROR";
  }
  return "UNKNOWN";
}

const char* transportText() {
  return activeTransport == TransportMode::Local ? "LOCAL / ESP-NOW" : "WIFI / MQTT";
}

constexpr uint16_t kUiBackground = 0x1104;
constexpr uint16_t kUiPanel = 0x21A7;
constexpr uint16_t kUiMuted = 0x8C51;
constexpr uint16_t kUiTeal = 0x2ED7;
constexpr uint16_t kUiMint = 0xA7F5;
constexpr uint16_t kUiCoral = 0xFB90;
constexpr uint16_t kUiYellow = 0xFDE6;
constexpr uint16_t kUiSky = 0x4DDF;

uint16_t stateColor(UiState state) {
  switch (state) {
    case UiState::Recording: return kUiCoral;
    case UiState::Sending: return kUiYellow;
    case UiState::Receiving: return kUiSky;
    case UiState::Playing: return kUiMint;
    case UiState::Setup: return kUiSky;
    case UiState::Error: return kUiCoral;
    case UiState::Ready: return kUiTeal;
  }
  return kUiTeal;
}

const char* stateCaption(UiState state) {
  switch (state) {
    case UiState::Recording: return "RECORDING";
    case UiState::Sending: return "SENDING";
    case UiState::Receiving: return "INCOMING";
    case UiState::Playing: return "PLAYING";
    case UiState::Setup: return "SETUP";
    case UiState::Error: return "CHECK LINK";
    case UiState::Ready: return "READY";
  }
  return "READY";
}

bool isAnimatedState(UiState state) {
  return state == UiState::Recording || state == UiState::Sending ||
         state == UiState::Receiving || state == UiState::Playing;
}

void drawMicrophoneGlyph(int16_t x, int16_t y, uint16_t color) {
  M5.Display.fillRoundRect(x - 7, y - 17, 14, 25, 7, color);
  M5.Display.fillRect(x - 2, y + 6, 4, 10, color);
  M5.Display.drawArc(x, y + 3, 14, 19, 40, 140, color);
  M5.Display.fillRoundRect(x - 11, y + 15, 22, 3, 1, color);
}

void drawWaveform(int16_t centerX, int16_t centerY, uint16_t color) {
  static constexpr uint8_t kHeights[][7] = {
      {10, 18, 28, 38, 28, 18, 10},
      {16, 30, 20, 42, 20, 30, 16},
      {24, 14, 36, 22, 36, 14, 24},
      {12, 26, 42, 18, 42, 26, 12},
  };
  const auto& heights = kHeights[uiAnimationFrame % 4];
  for (int i = 0; i < 7; ++i) {
    const int16_t x = centerX - 27 + i * 9;
    const int16_t h = heights[i];
    M5.Display.fillRoundRect(x, centerY - h / 2, 5, h, 2, color);
  }
}

void drawVolumeMark() {
  const uint8_t bars = volume < 110 ? 1 : (volume < 200 ? 2 : 3);
  for (uint8_t i = 0; i < 3; ++i) {
    const uint16_t color = i < bars ? kUiMint : kUiMuted;
    M5.Display.fillRoundRect(92 + i * 8, 207 - i * 3, 4, 8 + i * 3, 2, color);
  }
}

void drawUiFrame() {
  const uint16_t accent = stateColor(uiState);
  M5.Display.startWrite();
  M5.Display.fillScreen(kUiBackground);
  M5.Display.fillRect(0, 0, M5.Display.width(), 4, accent);
  M5.Display.fillRoundRect(8, 12, 119, 28, 5, kUiPanel);
  M5.Display.setTextDatum(textdatum_t::middle_left);
  M5.Display.setTextColor(TFT_WHITE, kUiPanel);
  M5.Display.setTextSize(1);
  M5.Display.drawString("EchoLink", 16, 26);
  M5.Display.fillRoundRect(82, 18, 37, 15, 4, accent);
  M5.Display.setTextDatum(textdatum_t::middle_center);
  M5.Display.setTextColor(kUiBackground, accent);
  M5.Display.drawString(activeTransport == TransportMode::Local ? "LOCAL" : "WIFI", 100, 26);

  M5.Display.fillRoundRect(8, 194, 119, 34, 5, kUiPanel);
  M5.Display.setTextDatum(textdatum_t::middle_left);
  M5.Display.setTextColor(kUiMuted, kUiPanel);
  M5.Display.drawString("B", 18, 211);
  M5.Display.setTextColor(TFT_WHITE, kUiPanel);
  M5.Display.drawString("VOLUME", 31, 211);
  drawVolumeMark();
  M5.Display.endWrite();
}

void drawUiDynamic() {
  const uint16_t accent = stateColor(uiState);
  const int16_t centerX = M5.Display.width() / 2;
  constexpr int16_t centerY = 112;
  const char* message = uiState == UiState::Error ? errorMessage : lastResult;
  char detail[32];

  M5.Display.startWrite();
  M5.Display.fillRect(8, 48, 119, 138, kUiBackground);
  M5.Display.fillCircle(centerX, centerY, 43, kUiPanel);
  M5.Display.drawCircle(centerX, centerY, 43, accent);
  M5.Display.drawCircle(centerX, centerY, 42, accent);

  if (uiState == UiState::Ready) {
    drawMicrophoneGlyph(centerX, centerY - 2, kUiTeal);
  } else if (uiState == UiState::Error) {
    M5.Display.fillCircle(centerX, centerY, 27, kUiCoral);
    M5.Display.setTextDatum(textdatum_t::middle_center);
    M5.Display.setTextColor(kUiBackground, kUiCoral);
    M5.Display.setTextSize(3);
    M5.Display.drawString("!", centerX, centerY - 1);
  } else {
    M5.Display.fillCircle(centerX, centerY, 29, accent);
    drawWaveform(centerX, centerY, kUiBackground);
  }

  M5.Display.setTextDatum(textdatum_t::middle_center);
  M5.Display.setTextColor(accent, kUiBackground);
  M5.Display.setTextSize(1);
  M5.Display.drawString(uiState == UiState::Setup ? lastResult : stateCaption(uiState), centerX, 164);
  M5.Display.setTextColor(TFT_WHITE, kUiBackground);
  if (uiState == UiState::Recording) {
    snprintf(detail, sizeof(detail), "%0.1f s", static_cast<float>(txAudioBytes) / ECHOLINK_SAMPLE_RATE);
  } else if (uiState == UiState::Receiving) {
    snprintf(detail, sizeof(detail), "%u%%", rxExpectedBytes == 0 ? 0 :
             static_cast<unsigned>((rxReceivedBytes * 100) / rxExpectedBytes));
  } else if (uiState == UiState::Sending) {
    snprintf(detail, sizeof(detail), "sending voice");
  } else if (uiState == UiState::Setup) {
    snprintf(detail, sizeof(detail), "%s", setupInfo);
  } else {
    snprintf(detail, sizeof(detail), "%s", message);
  }
  M5.Display.drawString(detail, centerX, 180);
  M5.Display.endWrite();
}

void drawUi(bool force = false) {
  if (activeApp != AppMode::EchoLink || displaySleeping) {
    return;
  }
  const char* message = uiState == UiState::Error ? errorMessage : lastResult;
  const bool stateChanged = uiState != lastDrawnState;
  const bool messageChanged = strcmp(message, lastDrawnMessage) != 0;
  const bool volumeChanged = volume != lastDrawnVolume;
  const bool animate = isAnimatedState(uiState) && millis() - lastUiAnimation >= 100;
  if (!force && !stateChanged && !messageChanged && !volumeChanged && !animate) {
    return;
  }
  if (force || stateChanged || volumeChanged) {
    drawUiFrame();
  }
  if (animate) {
    ++uiAnimationFrame;
    lastUiAnimation = millis();
  }
  drawUiDynamic();
  lastDrawnState = uiState;
  lastDrawnVolume = volume;
  snprintf(lastDrawnMessage, sizeof(lastDrawnMessage), "%s", message);
}

void drawLauncher() {
  M5.Display.startWrite();
  M5.Display.fillScreen(kUiBackground);
  M5.Display.fillRect(0, 0, M5.Display.width(), 4, kUiMint);
  M5.Display.setTextDatum(textdatum_t::top_left);
  M5.Display.setTextColor(TFT_WHITE, kUiBackground);
  M5.Display.setTextSize(1);
  M5.Display.drawString("APPS", 10, 14);
  M5.Display.setTextColor(kUiMuted, kUiBackground);
  M5.Display.setTextDatum(textdatum_t::top_right);
  M5.Display.drawString("A OPEN", 125, 14);

  const AppMode apps[] = {AppMode::EchoLink, AppMode::LifeTime};
  const char* names[] = {"EchoLink", "LifeTime"};
  const char* details[] = {"VOICE LINK", "CLOCK & TIMER"};
  const uint16_t accents[] = {kUiTeal, kUiYellow};
  for (uint8_t i = 0; i < 2; ++i) {
    const int16_t y = 48 + i * 72;
    const bool selected = launcherSelection == apps[i];
    const uint16_t panelColor = selected ? 0x3269 : kUiPanel;
    M5.Display.fillRoundRect(8, y, 119, 58, 6, panelColor);
    M5.Display.fillRoundRect(15, y + 13, 30, 30, 6, accents[i]);
    M5.Display.setTextDatum(textdatum_t::middle_center);
    M5.Display.setTextColor(kUiBackground, accents[i]);
    M5.Display.setTextSize(2);
    M5.Display.drawString(i == 0 ? "E" : "L", 30, y + 28);
    M5.Display.setTextDatum(textdatum_t::middle_left);
    M5.Display.setTextColor(TFT_WHITE, panelColor);
    M5.Display.setTextSize(1);
    M5.Display.drawString(names[i], 54, y + 22);
    M5.Display.setTextColor(selected ? kUiMint : kUiMuted, panelColor);
    M5.Display.drawString(details[i], 54, y + 39);
  }

  M5.Display.fillRoundRect(8, 204, 119, 24, 5, kUiPanel);
  M5.Display.setTextDatum(textdatum_t::middle_center);
  M5.Display.setTextColor(kUiMuted, kUiPanel);
  M5.Display.setTextSize(1);
  M5.Display.drawString("B SELECT", M5.Display.width() / 2, 216);
  M5.Display.endWrite();
}

void enterLauncher() {
  if (activeApp == AppMode::Launcher || portalActive || uiState == UiState::Recording ||
      uiState == UiState::Playing) {
    return;
  }
  launcherSelection = activeApp;
  activeApp = AppMode::Launcher;
  aPlayPending = false;
  drawLauncher();
}

void openSelectedApp() {
  activeApp = launcherSelection;
  preferences.putUChar("app", static_cast<uint8_t>(activeApp));
  if (activeApp == AppMode::LifeTime) {
    lifetime::onEnter();
  } else {
    lastDrawnState = UiState::Error;
    drawUi(true);
  }
}

bool handleAppSwitcherGesture() {
  const bool bothPressed = M5.BtnA.isPressed() && M5.BtnB.isPressed();
  if (bothPressed) {
    if (appChordStartedAt == 0) {
      appChordStartedAt = millis();
    }
    if (!appChordHandled && millis() - appChordStartedAt >= 1200) {
      appChordHandled = true;
      enterLauncher();
    }
    return true;
  }
  if (!M5.BtnA.isPressed() && !M5.BtnB.isPressed()) {
    appChordStartedAt = 0;
    appChordHandled = false;
  }
  return false;
}

void wakeDisplay() {
  lastUserActivity = millis();
  if (displayDimmed || displaySleeping) {
    M5.Display.setBrightness(ECHOLINK_DISPLAY_BRIGHTNESS);
    displayDimmed = false;
  }
  if (displaySleeping) {
    M5.Display.wakeup();
    displaySleeping = false;
    if (activeApp == AppMode::LifeTime) {
      lifetime::draw(true);
    } else if (activeApp == AppMode::Launcher) {
      drawLauncher();
    } else {
      drawUi(true);
    }
  }
}

void maintainDisplayPower() {
  if (displaySleeping || portalActive || uiState == UiState::Recording ||
      uiState == UiState::Sending || uiState == UiState::Playing) {
    return;
  }
  const uint32_t idleMs = millis() - lastUserActivity;
  if (idleMs >= kDisplayIdleMs) {
    M5.Display.sleep();
    displaySleeping = true;
  } else if (!displayDimmed && idleMs >= kDisplayDimMs) {
    M5.Display.setBrightness(ECHOLINK_DISPLAY_DIM_BRIGHTNESS);
    displayDimmed = true;
  }
}

void stopAudio() {
  if (microphoneEnabled) {
    while (M5.Mic.isRecording()) {
      delay(1);
    }
    M5.Mic.end();
    microphoneEnabled = false;
  }
  if (speakerEnabled) {
    M5.Speaker.end();
    speakerEnabled = false;
  }
}

void useMicrophone() {
  if (microphoneEnabled) {
    return;
  }
  // The StickS3 microphone and speaker share audio clocks. Stop the speaker
  // even when our local state was reset, matching M5Unified's mic example.
  M5.Speaker.end();
  speakerEnabled = false;
  M5.Mic.begin();
  microphoneEnabled = true;
}

void useSpeaker() {
  if (speakerEnabled) {
    return;
  }
  if (microphoneEnabled) {
    while (M5.Mic.isRecording()) {
      delay(1);
    }
    M5.Mic.end();
    microphoneEnabled = false;
  }
  M5.Speaker.begin();
  M5.Speaker.setVolume(volume);
  speakerEnabled = true;
}

void chirp(uint16_t first, uint16_t second = 0) {
  useSpeaker();
  M5.Speaker.tone(first, 55, 0, true);
  delay(70);
  if (second != 0) {
    M5.Speaker.tone(second, 70, 0, true);
    delay(85);
  }
}

bool validPacket(const uint8_t* data, size_t length) {
  return echolink::isValidPacket(data, length, ECHOLINK_GROUP_ID, deviceId);
}

void refreshUnreadVoiceCount() {
  unreadVoiceCount = 0;
  for (uint8_t i = 0; i < voiceQueueCount; ++i) {
    if (voiceQueueUnread[i]) {
      ++unreadVoiceCount;
    }
  }
}

void removeVoiceQueueItem(uint8_t index) {
  if (index >= voiceQueueCount) {
    return;
  }
  for (uint8_t i = index; i + 1 < voiceQueueCount; ++i) {
    memcpy(voiceQueueData[i], voiceQueueData[i + 1], voiceQueueBytes[i + 1]);
    voiceQueueBytes[i] = voiceQueueBytes[i + 1];
    voiceQueueUnread[i] = voiceQueueUnread[i + 1];
  }
  --voiceQueueCount;
  refreshUnreadVoiceCount();
}

void enqueueVoice(const int8_t* data, size_t bytes) {
  if (data == nullptr || bytes == 0 || bytes > kMaxAudioBytes) {
    return;
  }
  if (voiceQueueCount >= kMaxUnreadVoices) {
    uint8_t removeIndex = 0;
    for (uint8_t i = 0; i < voiceQueueCount; ++i) {
      if (!voiceQueueUnread[i]) {
        removeIndex = i;
        break;
      }
    }
    removeVoiceQueueItem(removeIndex);
  }
  memcpy(voiceQueueData[voiceQueueCount], data, bytes);
  voiceQueueBytes[voiceQueueCount] = bytes;
  voiceQueueUnread[voiceQueueCount] = true;
  ++voiceQueueCount;
  refreshUnreadVoiceCount();
}

int8_t* nextUnreadVoice(size_t* bytes, uint8_t* index) {
  for (uint8_t i = 0; i < voiceQueueCount; ++i) {
    if (voiceQueueUnread[i]) {
      voiceQueueUnread[i] = false;
      refreshUnreadVoiceCount();
      if (bytes != nullptr) {
        *bytes = voiceQueueBytes[i];
      }
      if (index != nullptr) {
        *index = i;
      }
      return voiceQueueData[i];
    }
  }
  return nullptr;
}

void resetReceiveBuffer() {
  rxExpectedBytes = 0;
  rxReceivedBytes = 0;
  rxSessionId = 0;
  rxSenderId = 0;
  memset(rxChunkSeen, 0, kMaxChunks * sizeof(bool));
}

void acceptIncomingPacket(const uint8_t* data, size_t length) {
  if (!validPacket(data, length)) {
    return;
  }
  const auto header = echolink::readHeader(data);
  const auto type = static_cast<echolink::PacketType>(header.type);
  const auto* payload = reinterpret_cast<const int8_t*>(data + sizeof(echolink::PacketHeader));

  if (type == echolink::PacketType::Nudge) {
    if (header.payloadBytes == 1) {
      lifetime::receiveNudge(static_cast<uint8_t>(payload[0]), header.senderId);
    }
    return;
  }

  if (type == echolink::PacketType::VoiceStart) {
    if (header.totalBytes == 0 || header.totalBytes > kMaxAudioBytes) {
      return;
    }
    resetReceiveBuffer();
    rxExpectedBytes = header.totalBytes;
    rxSessionId = header.sessionId;
    rxSenderId = header.senderId;
    if (uiState == UiState::Ready || uiState == UiState::Receiving) {
      uiState = UiState::Receiving;
    }
    snprintf(lastResult, sizeof(lastResult), "RX: %u bytes", static_cast<unsigned>(rxExpectedBytes));
    return;
  }

  if (header.senderId != rxSenderId || header.sessionId != rxSessionId || rxExpectedBytes == 0) {
    return;
  }

  if (type == echolink::PacketType::Audio) {
    const size_t chunkIndex = header.sequence;
    const size_t offset = chunkIndex * ECHOLINK_AUDIO_CHUNK_BYTES;
    if (chunkIndex >= kMaxChunks || offset >= rxExpectedBytes ||
        header.payloadBytes > ECHOLINK_AUDIO_CHUNK_BYTES) {
      ++droppedPackets;
      return;
    }
    const size_t copyBytes = min(static_cast<size_t>(header.payloadBytes), rxExpectedBytes - offset);
    if (!rxChunkSeen[chunkIndex]) {
      memcpy(rxAudio + offset, payload, copyBytes);
      rxChunkSeen[chunkIndex] = true;
      rxReceivedBytes += copyBytes;
    }
    return;
  }

  if (type == echolink::PacketType::VoiceEnd) {
    if (rxReceivedBytes == rxExpectedBytes) {
      enqueueVoice(rxAudio, rxExpectedBytes);
      voiceAlertPending = true;
      snprintf(lastResult, sizeof(lastResult), "NEW MSG %u", unreadVoiceCount);
      resetReceiveBuffer();
      if (uiState == UiState::Receiving) {
        uiState = UiState::Ready;
      }
    } else {
      ++droppedMessages;
      snprintf(lastResult, sizeof(lastResult), "RX %u/%u",
               static_cast<unsigned>(rxReceivedBytes), static_cast<unsigned>(rxExpectedBytes));
      resetReceiveBuffer();
      if (uiState == UiState::Receiving) {
        uiState = UiState::Ready;
      }
    }
  }
}

constexpr uint8_t kEspNowBroadcastAddress[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

bool queueIncomingTransportFrame(const uint8_t* data, size_t length) {
  if (incomingFrameQueue == nullptr || data == nullptr || length > kMaxTransportFrameBytes) {
    return false;
  }
  TransportFrame frame;
  frame.length = static_cast<uint16_t>(length);
  memcpy(frame.data, data, length);
  return xQueueSend(incomingFrameQueue, &frame, 0) == pdTRUE;
}

void serviceIncomingTransportFrames(uint8_t budget) {
  if (incomingFrameQueue == nullptr) {
    return;
  }
  TransportFrame frame;
  while (budget-- > 0 && xQueueReceive(incomingFrameQueue, &frame, 0) == pdTRUE) {
    acceptIncomingPacket(frame.data, frame.length);
  }
}

void onEspNowReceive(const uint8_t* senderMac, const uint8_t* data, int length) {
  (void)senderMac;
  if (!queueIncomingTransportFrame(data, static_cast<size_t>(length))) {
    ++droppedPackets;
  }
}

void onEspNowSend(const uint8_t* receiverMac, esp_now_send_status_t status) {
  (void)receiverMac;
  if (status == ESP_NOW_SEND_SUCCESS) {
    ++txFramesDelivered;
  } else {
    ++txFramesFailed;
  }
}

bool sendEspNowFrame(const uint8_t* data, size_t length) {
  const esp_err_t result = esp_now_send(kEspNowBroadcastAddress, data, length);
  if (result != ESP_OK) {
    snprintf(errorMessage, sizeof(errorMessage), "TX err 0x%04x",
             static_cast<unsigned>(result));
    Serial.printf("ESP-NOW send failed: %s (0x%04x)\n", esp_err_to_name(result),
                  static_cast<unsigned>(result));
    return false;
  }
  return true;
}

bool initEspNowTransport() {
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(false, false);
  delay(100);
  if (esp_wifi_set_channel(ECHOLINK_ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE) != ESP_OK) {
    return false;
  }
  if (esp_now_init() != ESP_OK) {
    return false;
  }
  esp_now_register_recv_cb(onEspNowReceive);
  esp_now_register_send_cb(onEspNowSend);

  esp_now_peer_info_t peerInfo = {};
  memset(peerInfo.peer_addr, 0xFF, sizeof(peerInfo.peer_addr));
  peerInfo.channel = ECHOLINK_ESPNOW_CHANNEL;
  peerInfo.encrypt = false;
  if (esp_now_add_peer(&peerInfo) != ESP_OK && !esp_now_is_peer_exist(peerInfo.peer_addr)) {
    return false;
  }
  espNowActive = true;
  return true;
}

void onMqttMessage(char* topic, uint8_t* payload, unsigned int length) {
  (void)topic;
  if (!queueIncomingTransportFrame(payload, length)) {
    ++droppedPackets;
  }
}

bool sendMqttFrame(const uint8_t* data, size_t length) {
  if (!mqttReady || mqttPublishQueue == nullptr || mqttPublishPending || data == nullptr ||
      length > kMaxTransportFrameBytes) {
    snprintf(errorMessage, sizeof(errorMessage), "MQTT offline");
    return false;
  }
  MqttPublishRequest request;
  request.generation = mqttTransportGeneration;
  request.id = ++nextMqttPublishId;
  request.frame.length = static_cast<uint16_t>(length);
  memcpy(request.frame.data, data, length);
  if (xQueueSend(mqttPublishQueue, &request, 0) != pdTRUE) {
    snprintf(errorMessage, sizeof(errorMessage), "MQTT queue busy");
    return false;
  }
  mqttPublishPending = true;
  pendingMqttPublishId = request.id;
  return true;
}

bool hasNetworkSettings() {
  return networkSettings.ssid.length() > 0 && networkSettings.mqttHost.length() > 0;
}

String profileKey(const char* prefix, uint8_t index) {
  return String(prefix) + String(index);
}

void selectWiFiProfile(uint8_t index) {
  if (wifiProfileCount == 0) {
    activeWiFiProfile = 0;
    networkSettings.ssid = "";
    networkSettings.password = "";
    return;
  }
  activeWiFiProfile = min<uint8_t>(index, wifiProfileCount - 1);
  networkSettings.ssid = wifiProfiles[activeWiFiProfile].ssid;
  networkSettings.password = wifiProfiles[activeWiFiProfile].password;
}

void saveWiFiProfiles() {
  preferences.putUChar("wifi_count", wifiProfileCount);
  preferences.putUChar("wifi_active", activeWiFiProfile);
  for (uint8_t i = 0; i < kMaxWiFiProfiles; ++i) {
    preferences.putString(profileKey("ws", i).c_str(),
                          i < wifiProfileCount ? wifiProfiles[i].ssid : "");
    preferences.putString(profileKey("wp", i).c_str(),
                          i < wifiProfileCount ? wifiProfiles[i].password : "");
  }
  // Keep the original keys so existing builds can still read the selected network.
  preferences.putString("ssid", networkSettings.ssid);
  preferences.putString("wifi_pass", networkSettings.password);
}

void loadNetworkSettings() {
  wifiProfileCount = min<uint8_t>(preferences.getUChar("wifi_count", 0), kMaxWiFiProfiles);
  for (uint8_t i = 0; i < wifiProfileCount;) {
    wifiProfiles[i].ssid = preferences.getString(profileKey("ws", i).c_str(), "");
    wifiProfiles[i].password = preferences.getString(profileKey("wp", i).c_str(), "");
    if (wifiProfiles[i].ssid.length() == 0) {
      for (uint8_t j = i; j + 1 < wifiProfileCount; ++j) {
        wifiProfiles[j] = wifiProfiles[j + 1];
      }
      --wifiProfileCount;
    } else {
      ++i;
    }
  }
  if (wifiProfileCount == 0) {
    const String legacySsid = preferences.getString("ssid", "");
    if (legacySsid.length() > 0) {
      wifiProfiles[0].ssid = legacySsid;
      wifiProfiles[0].password = preferences.getString("wifi_pass", "");
      wifiProfileCount = 1;
    }
  }
  selectWiFiProfile(preferences.getUChar("wifi_active", 0));
  networkSettings.mqttHost = preferences.getString("mqtt_host", "");
  networkSettings.mqttPort = preferences.getUShort("mqtt_port", 1883);
  networkSettings.mqttUser = preferences.getString("mqtt_user", "");
  networkSettings.mqttPassword = preferences.getString("mqtt_pass", "");
  networkSettings.mqttTopic = preferences.getString("mqtt_topic", "echolink/v1/voice");
  if (networkSettings.mqttTopic.length() == 0) {
    networkSettings.mqttTopic = "echolink/v1/voice";
  }
  activeTransport = preferences.getUChar("mode", 0) == 1 ? TransportMode::WiFi : TransportMode::Local;
}

String htmlEscape(const String& value) {
  String escaped;
  escaped.reserve(value.length() + 8);
  for (size_t i = 0; i < value.length(); ++i) {
    switch (value[i]) {
      case '&': escaped += F("&amp;"); break;
      case '<': escaped += F("&lt;"); break;
      case '>': escaped += F("&gt;"); break;
      case '\"': escaped += F("&quot;"); break;
      case '\'': escaped += F("&#39;"); break;
      default: escaped += value[i]; break;
    }
  }
  return escaped;
}

String deviceSuffix() {
  char suffix[9];
  snprintf(suffix, sizeof(suffix), "%08lx", static_cast<unsigned long>(deviceId));
  return String(suffix);
}

bool configWindowOpen() {
  return portalActive && static_cast<int32_t>(millis() - configWindowUntil) < 0;
}

void setSetupStatus(const char* status, const char* info) {
  snprintf(lastResult, sizeof(lastResult), "%s", status);
  snprintf(setupInfo, sizeof(setupInfo), "%s", info);
  lastSetupInfoChange = millis();
  drawUi(true);
}

void collectWiFiScanResults(int scanCount) {
  scannedWiFiNetworkCount = 0;
  for (int i = 0; i < scanCount && scannedWiFiNetworkCount < kMaxScannedWiFiNetworks; ++i) {
    const String ssid = WiFi.SSID(i);
    if (ssid.length() == 0) {
      continue;
    }
    bool duplicate = false;
    for (uint8_t j = 0; j < scannedWiFiNetworkCount; ++j) {
      if (scannedWiFiNetworks[j].ssid == ssid) {
        duplicate = true;
        break;
      }
    }
    if (!duplicate) {
      scannedWiFiNetworks[scannedWiFiNetworkCount].ssid = ssid;
      scannedWiFiNetworks[scannedWiFiNetworkCount].rssi = WiFi.RSSI(i);
      ++scannedWiFiNetworkCount;
    }
  }
  WiFi.scanDelete();
}

void startWiFiScan() {
  if (!portalActive || wifiScanInProgress) {
    return;
  }
  WiFi.scanDelete();
  scannedWiFiNetworkCount = 0;
  lastWiFiScanStart = millis();
  const int result = WiFi.scanNetworks(true, true);
  if (result >= 0) {
    collectWiFiScanResults(result);
    char info[32];
    snprintf(info, sizeof(info), "%u NETWORKS FOUND", scannedWiFiNetworkCount);
    setSetupStatus("PORTAL READY", info);
    return;
  }
  wifiScanInProgress = result == WIFI_SCAN_RUNNING;
  if (wifiScanInProgress) {
    setSetupStatus("SCANNING WIFI", "PLEASE WAIT");
  } else {
    setSetupStatus("SCAN RETRY", "OPEN PAGE TO REFRESH");
  }
}

void maintainWiFiScan() {
  if (!portalActive) {
    return;
  }
  if (!wifiScanInProgress) {
    if (scannedWiFiNetworkCount == 0 && millis() - lastWiFiScanStart > 3000) {
      startWiFiScan();
    }
    return;
  }
  const int result = WiFi.scanComplete();
  if (result == WIFI_SCAN_RUNNING) {
    return;
  }
  wifiScanInProgress = false;
  if (result >= 0) {
    collectWiFiScanResults(result);
    char info[32];
    snprintf(info, sizeof(info), "%u NETWORKS FOUND", scannedWiFiNetworkCount);
    setSetupStatus("PORTAL READY", info);
  } else {
    WiFi.scanDelete();
    setSetupStatus("SCAN RETRY", "OPEN PAGE TO REFRESH");
  }
}

void rotateSetupInfo() {
  if (!portalActive || wifiScanInProgress || strcmp(lastResult, "PORTAL READY") != 0 ||
      millis() - lastSetupInfoChange < 2500) {
    return;
  }
  const String suffix = deviceSuffix().substring(4);
  ++setupInfoPage;
  if (setupInfoPage % 3 == 0) {
    snprintf(setupInfo, sizeof(setupInfo), "JOIN EchoLink-%s", suffix.c_str());
  } else if (setupInfoPage % 3 == 1) {
    snprintf(setupInfo, sizeof(setupInfo), "PASS echo%s", suffix.c_str());
  } else {
    snprintf(setupInfo, sizeof(setupInfo), "OPEN 192.168.4.1");
  }
  lastSetupInfoChange = millis();
  drawUi(true);
}

String configPage() {
  const char* selectedLocal = activeTransport == TransportMode::Local ? " selected" : "";
  const char* selectedWiFi = activeTransport == TransportMode::WiFi ? " selected" : "";
  String page = F("<!doctype html><html><head><meta charset=utf-8>"
                  "<meta name=viewport content='width=device-width,initial-scale=1'>"
                  "<style>body{margin:0;background:#111923;color:#f5f7fa;font:16px system-ui;padding:20px}"
                  "main{max-width:430px;margin:auto}h1{margin:0 0 4px}p{color:#aeb8c7;line-height:1.45}label{display:block;margin-top:14px;color:#a7f3d0}"
                  "input,select{box-sizing:border-box;width:100%;margin-top:5px;padding:11px;border:1px solid #355064;"
                  "border-radius:6px;background:#1c2b38;color:#fff;font:inherit}.hint{font-size:13px;margin:6px 0 0}.scan{color:#a7f3d0;text-decoration:none}button{width:100%;margin-top:22px;"
                  "padding:12px;border:0;border-radius:6px;background:#2dd4bf;color:#07131b;font-weight:700;font-size:16px}</style>"
                  "</head><body><main><h1>EchoLink</h1><p>Device ");
  page += deviceSuffix();
  page += F("</p><form method=post action=/save><label>Transport<select name=mode><option value=local");
  page += selectedLocal;
  page += F(">Local ESP-NOW</option><option value=wifi");
  page += selectedWiFi;
  page += F(">Wi-Fi MQTT</option></select></label><label>使用已保存的 Wi-Fi / Saved Wi-Fi<select name=wifi_profile>");
  for (uint8_t i = 0; i < wifiProfileCount; ++i) {
    page += F("<option value='");
    page += String(i);
    if (i == activeWiFiProfile) {
      page += F("' selected>");
    } else {
      page += F("'>");
    }
    page += htmlEscape(wifiProfiles[i].ssid);
    page += F("</option>");
  }
  page += F("<option value='new'>添加新网络 / Add network</option></select></label>"
            "<p class=hint>SSID 就是平时看到的 Wi-Fi 名称。StickS3 仅支持 2.4GHz。</p>"
            "<label>附近的 2.4GHz Wi-Fi / Nearby networks<select name=scanned_ssid>"
            "<option value=''>请选择，或在下方手动输入</option>");
  for (uint8_t i = 0; i < scannedWiFiNetworkCount; ++i) {
    page += F("<option value='");
    page += htmlEscape(scannedWiFiNetworks[i].ssid);
    page += F("'>");
    page += htmlEscape(scannedWiFiNetworks[i].ssid);
    page += F(" (");
    page += String(scannedWiFiNetworks[i].rssi);
    page += F(" dBm)</option>");
  }
  page += F("</select></label><p class=hint>");
  if (wifiScanInProgress) {
    page += F("正在扫描附近网络，请稍候后刷新页面。 / Scanning, refresh shortly.");
  } else if (scannedWiFiNetworkCount == 0) {
    page += F("未找到网络。确认 2.4GHz 已开启，然后刷新扫描。 / No networks found.");
  } else {
    page += F("<a class=scan href=/scan>Refresh network list</a>");
  }
  page += F("</p><label>隐藏网络名称 / Manual SSID<input name=ssid maxlength=32 "
            "placeholder='仅在上方找不到时填写'></label>"
            "<label>Wi-Fi 密码 / Password<input type=password name=wifi_pass maxlength=63 "
            "placeholder='已保存网络留空；新网络请输入'></label>"
            "<label>MQTT host<input name=mqtt_host value='");
  page += htmlEscape(networkSettings.mqttHost);
  page += F("'></label><label>MQTT port<input type=number name=mqtt_port value='");
  page += String(networkSettings.mqttPort);
  page += F("'></label><label>MQTT user<input name=mqtt_user value='");
  page += htmlEscape(networkSettings.mqttUser);
  page += F("'></label><label>MQTT password<input type=password name=mqtt_pass placeholder='unchanged when blank'></label>"
            "<label>Topic<input name=mqtt_topic value='");
  page += htmlEscape(networkSettings.mqttTopic);
  page += F("'></label><button>Save and connect</button></form></main></body></html>");
  return page;
}

void ensureConfigServer() {
  if (configServerStarted) {
    return;
  }
  configServer.on("/", HTTP_GET, []() {
    if (!configWindowOpen()) {
      configServer.send(403, "text/plain", "Enable setup on the device first.");
      return;
    }
    configServer.sendHeader("Cache-Control", "no-store, max-age=0");
    configServer.sendHeader("Pragma", "no-cache");
    configServer.send(200, "text/html; charset=utf-8", configPage());
  });
  configServer.on("/scan", HTTP_GET, []() {
    if (!configWindowOpen()) {
      configServer.send(403, "text/plain", "Enable setup on the device first.");
      return;
    }
    startWiFiScan();
    configServer.sendHeader("Cache-Control", "no-store, max-age=0");
    configServer.sendHeader("Location", "/");
    configServer.send(302, "text/plain", "");
  });
  configServer.on("/save", HTTP_POST, []() {
    if (!configWindowOpen()) {
      configServer.send(403, "text/plain", "Setup window closed.");
      return;
    }
    String ssid = configServer.arg("ssid");
    if (ssid.length() == 0) {
      ssid = configServer.arg("scanned_ssid");
    }
    const String profileArg = configServer.arg("wifi_profile");
    const String host = configServer.arg("mqtt_host");
    const bool wantsWiFi = configServer.arg("mode") == "wifi";
    int selectedProfile = profileArg == "new" ? -1 : profileArg.toInt();
    if (selectedProfile < 0 && ssid.length() > 0) {
      for (uint8_t i = 0; i < wifiProfileCount; ++i) {
        if (wifiProfiles[i].ssid == ssid) {
          selectedProfile = i;
          break;
        }
      }
      if (selectedProfile < 0) {
        if (wifiProfileCount >= kMaxWiFiProfiles) {
          configServer.send(400, "text/plain", "Five Wi-Fi networks are already saved.");
          return;
        }
        selectedProfile = wifiProfileCount++;
        wifiProfiles[selectedProfile].ssid = ssid;
        wifiProfiles[selectedProfile].password = "";
      }
    }
    if (wantsWiFi &&
        (selectedProfile < 0 || selectedProfile >= wifiProfileCount || host.length() == 0)) {
      configServer.send(400, "text/plain", "Select or add a Wi-Fi network and enter the MQTT host.");
      return;
    }
    if (host.length() > 0) {
      networkSettings.mqttHost = host;
    }
    networkSettings.mqttPort = static_cast<uint16_t>(configServer.arg("mqtt_port").toInt());
    if (networkSettings.mqttPort == 0) {
      networkSettings.mqttPort = 1883;
    }
    networkSettings.mqttUser = configServer.arg("mqtt_user");
    networkSettings.mqttTopic = configServer.arg("mqtt_topic");
    if (networkSettings.mqttTopic.length() == 0) {
      networkSettings.mqttTopic = "echolink/v1/voice";
    }
    const String wifiPassword = configServer.arg("wifi_pass");
    const String mqttPassword = configServer.arg("mqtt_pass");
    if (selectedProfile >= 0 && selectedProfile < wifiProfileCount) {
      if (ssid.length() > 0) {
        wifiProfiles[selectedProfile].ssid = ssid;
      }
      if (wifiPassword.length() > 0) {
        wifiProfiles[selectedProfile].password = wifiPassword;
      }
      selectWiFiProfile(selectedProfile);
    }
    if (mqttPassword.length() > 0) {
      networkSettings.mqttPassword = mqttPassword;
    }
    activeTransport = wantsWiFi ? TransportMode::WiFi : TransportMode::Local;
    saveWiFiProfiles();
    preferences.putString("mqtt_host", networkSettings.mqttHost);
    preferences.putUShort("mqtt_port", networkSettings.mqttPort);
    preferences.putString("mqtt_user", networkSettings.mqttUser);
    preferences.putString("mqtt_pass", networkSettings.mqttPassword);
    preferences.putString("mqtt_topic", networkSettings.mqttTopic);
    preferences.putUChar("mode", static_cast<uint8_t>(activeTransport));
    configServer.sendHeader("Cache-Control", "no-store, max-age=0");
    configServer.send(200, "text/html; charset=utf-8",
                      "<meta charset=utf-8><meta name=viewport content='width=device-width'>"
                      "<p>Saved. EchoLink is connecting.</p>");
    setSetupStatus("SAVED", "CONNECTING WIFI");
    configSaveUntil = millis() + 1200;
    restartTransportRequested = true;
  });
  configServer.onNotFound([]() {
    if (configWindowOpen()) {
      configServer.sendHeader("Location", "/");
      configServer.send(302, "text/plain", "");
    } else {
      configServer.send(404, "text/plain", "Not found.");
    }
  });
  configServer.begin();
  configServerStarted = true;
}

void stopConfigPortal() {
  if (!portalActive) {
    return;
  }
  dnsServer.stop();
  WiFi.scanDelete();
  wifiScanInProgress = false;
  WiFi.softAPdisconnect(true);
  portalActive = false;
  configWindowUntil = 0;
}

void startConfigPortal() {
  if (espNowActive) {
    esp_now_deinit();
    espNowActive = false;
  }
  mqttWorkerEnabled = false;
  mqttReady = false;
  ++mqttTransportGeneration;
  mqttPublishPending = false;
  WiFi.mode(WIFI_AP_STA);
  WiFi.disconnect(false, false);
  const String apName = "EchoLink-" + deviceSuffix().substring(4);
  const String apPassword = "echo" + deviceSuffix().substring(4);
  WiFi.softAP(apName.c_str(), apPassword.c_str());
  dnsServer.start(53, "*", WiFi.softAPIP());
  ensureConfigServer();
  portalActive = true;
  configWindowUntil = millis() + 300000;
  uiState = UiState::Setup;
  setupInfoPage = 0;
  setSetupStatus("PORTAL READY", ("JOIN " + apName).c_str());
  startWiFiScan();
  Serial.printf("Setup AP %s password %s at %s\n", apName.c_str(), apPassword.c_str(),
                WiFi.softAPIP().toString().c_str());
}

bool initMqttTransport() {
  if (!hasNetworkSettings()) {
    startConfigPortal();
    return true;
  }
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(networkSettings.ssid.c_str(), networkSettings.password.c_str());
  lastMqttConnectAttempt = 0;
  mqttRetryPausedUntil = 0;
  lastWiFiStatus = WL_NO_SHIELD;
  lastMqttState = 99;
  mqttWorkerEnabled = true;
  snprintf(lastResult, sizeof(lastResult), "WiFi connecting");
  Serial.printf("WiFi connecting to '%s'; MQTT %s:%u topic %s\n",
                networkSettings.ssid.c_str(), networkSettings.mqttHost.c_str(),
                networkSettings.mqttPort, networkSettings.mqttTopic.c_str());
  return true;
}

const char* wifiStatusMessage(wl_status_t status) {
  switch (status) {
    case WL_NO_SSID_AVAIL: return "WiFi SSID missing";
    case WL_CONNECT_FAILED: return "WiFi auth failed";
    case WL_CONNECTION_LOST: return "WiFi link lost";
    case WL_DISCONNECTED: return "WiFi connecting";
    case WL_IDLE_STATUS: return "WiFi connecting";
    default: return "WiFi unavailable";
  }
}

const char* mqttStatusMessage(int state) {
  switch (state) {
    case MQTT_CONNECTION_TIMEOUT: return "MQTT timeout";
    case MQTT_CONNECTION_LOST: return "MQTT link lost";
    case MQTT_CONNECT_FAILED: return "MQTT unreachable";
    case MQTT_DISCONNECTED: return "MQTT connecting";
    case MQTT_CONNECT_BAD_PROTOCOL: return "MQTT protocol error";
    case MQTT_CONNECT_BAD_CLIENT_ID: return "MQTT client rejected";
    case MQTT_CONNECT_UNAVAILABLE: return "MQTT unavailable";
    case MQTT_CONNECT_BAD_CREDENTIALS: return "MQTT bad login";
    case MQTT_CONNECT_UNAUTHORIZED: return "MQTT unauthorized";
    default: return "MQTT connecting";
  }
}

void disableWiFiPowerSaveAfterLinkLoss() {
  if (wifiPowerSaveDisabled) {
    return;
  }
  if (esp_wifi_set_ps(WIFI_PS_NONE) == ESP_OK) {
    wifiPowerSaveDisabled = true;
    Serial.println("WiFi modem sleep disabled after MQTT link loss");
  }
}

void mqttWorkerTask(void*) {
  uint32_t configuredGeneration = UINT32_MAX;
  bool connectedDuringGeneration = false;
  for (;;) {
    const uint32_t generation = mqttTransportGeneration;
    if (!mqttWorkerEnabled) {
      if (mqttClient.connected()) {
        mqttClient.disconnect();
      }
      wifiClient.stop();
      mqttReady = false;
      mqttWorkerState = MQTT_DISCONNECTED;
      configuredGeneration = UINT32_MAX;
      connectedDuringGeneration = false;

      MqttPublishRequest staleRequest;
      if (mqttPublishQueue != nullptr &&
          xQueueReceive(mqttPublishQueue, &staleRequest, 0) == pdTRUE) {
        MqttPublishResult result;
        result.generation = staleRequest.generation;
        result.id = staleRequest.id;
        result.success = false;
        xQueueSend(mqttPublishResultQueue, &result, 0);
      }
      vTaskDelay(pdMS_TO_TICKS(100));
      continue;
    }

    if (configuredGeneration != generation) {
      if (mqttClient.connected()) {
        mqttClient.disconnect();
      }
      wifiClient.stop();
      mqttClient.setServer(networkSettings.mqttHost.c_str(), networkSettings.mqttPort);
      mqttClient.setCallback(onMqttMessage);
      mqttClient.setKeepAlive(30);
      mqttClient.setSocketTimeout(kMqttConnectTimeoutSeconds);
      // Includes MQTT framing in addition to the largest 222-byte protocol frame.
      mqttClient.setBufferSize(384);
      configuredGeneration = generation;
      mqttReady = false;
      mqttWorkerAttempts = 0;
      mqttRetryPausedUntil = 0;
      lastMqttConnectAttempt = 0;
      connectedDuringGeneration = false;
    }

    if (WiFi.status() != WL_CONNECTED) {
      mqttReady = false;
      mqttWorkerState = MQTT_DISCONNECTED;
      vTaskDelay(pdMS_TO_TICKS(100));
      continue;
    }

    if (!mqttClient.connected()) {
      mqttReady = false;
      if (connectedDuringGeneration) {
        disableWiFiPowerSaveAfterLinkLoss();
      }
      if (static_cast<int32_t>(millis() - mqttRetryPausedUntil) < 0 ||
          millis() - lastMqttConnectAttempt < kMqttRetryIntervalMs) {
        vTaskDelay(pdMS_TO_TICKS(100));
        continue;
      }
      lastMqttConnectAttempt = millis();
      wifiClient.stop();
      char clientId[32];
      snprintf(clientId, sizeof(clientId), "echolink-%08lx",
               static_cast<unsigned long>(deviceId));
      if (mqttClient.connect(clientId, networkSettings.mqttUser.c_str(),
                             networkSettings.mqttPassword.c_str())) {
        wifiClient.setNoDelay(true);
        const int keepAliveEnabled = 1;
        const int keepIdleSeconds = 10;
        const int keepIntervalSeconds = 5;
        const int keepProbeCount = 3;
        wifiClient.setSocketOption(SOL_SOCKET, SO_KEEPALIVE, &keepAliveEnabled,
                                   sizeof(keepAliveEnabled));
        wifiClient.setSocketOption(IPPROTO_TCP, TCP_KEEPIDLE, &keepIdleSeconds,
                                   sizeof(keepIdleSeconds));
        wifiClient.setSocketOption(IPPROTO_TCP, TCP_KEEPINTVL, &keepIntervalSeconds,
                                   sizeof(keepIntervalSeconds));
        wifiClient.setSocketOption(IPPROTO_TCP, TCP_KEEPCNT, &keepProbeCount,
                                   sizeof(keepProbeCount));
        mqttWorkerAttempts = 0;
        mqttRetryPausedUntil = 0;
        mqttClient.subscribe(networkSettings.mqttTopic.c_str());
        mqttWorkerState = MQTT_CONNECTED;
        mqttReady = true;
        connectedDuringGeneration = true;
        Serial.printf("MQTT connected as %s\n", clientId);
      } else {
        mqttWorkerState = mqttClient.state();
        ++mqttWorkerAttempts;
        Serial.printf("MQTT connect failed, state=%d attempt=%u/%u\n", mqttWorkerState,
                      mqttWorkerAttempts, kMqttMaxAttempts);
        if (mqttWorkerAttempts >= kMqttMaxAttempts) {
          mqttWorkerAttempts = 0;
          mqttRetryPausedUntil = millis() + kMqttRetryPauseMs;
        }
      }
      vTaskDelay(pdMS_TO_TICKS(20));
      continue;
    }

    if (!mqttClient.loop()) {
      mqttWorkerState = mqttClient.state();
      mqttReady = false;
      disableWiFiPowerSaveAfterLinkLoss();
      Serial.printf("MQTT loop disconnected, state=%d\n", mqttWorkerState);
      vTaskDelay(pdMS_TO_TICKS(20));
      continue;
    }

    MqttPublishRequest request;
    const bool hadPublishRequest = xQueueReceive(mqttPublishQueue, &request, 0) == pdTRUE;
    if (hadPublishRequest) {
      MqttPublishResult result;
      result.generation = request.generation;
      result.id = request.id;
      result.success = false;
      if (request.generation == generation && mqttWorkerEnabled && mqttClient.connected()) {
        result.success = mqttClient.publish(networkSettings.mqttTopic.c_str(), request.frame.data,
                                            request.frame.length, false);
      }
      if (!result.success) {
        mqttWorkerState = mqttClient.state();
        mqttReady = false;
        mqttClient.disconnect();
        wifiClient.stop();
      }
      xQueueSend(mqttPublishResultQueue, &result, 0);
    }
    vTaskDelay(pdMS_TO_TICKS(hadPublishRequest ? 2 : 10));
  }
}

bool sendTransportFrame(const uint8_t* data, size_t length) {
  return activeTransport == TransportMode::Local ? sendEspNowFrame(data, length)
                                                  : sendMqttFrame(data, length);
}

bool initTransport() {
  if (espNowActive) {
    esp_now_deinit();
    espNowActive = false;
  }
  mqttWorkerEnabled = false;
  mqttReady = false;
  ++mqttTransportGeneration;
  mqttPublishPending = false;
  outgoingPhase = OutgoingPhase::Start;
  outgoingOffset = 0;
  outgoingSequence = 0;
  if (mdnsActive) {
    MDNS.end();
    mdnsActive = false;
  }
  if (activeTransport == TransportMode::Local) {
    return initEspNowTransport();
  }
  return initMqttTransport();
}

void maintainTransport() {
  ensureConfigServer();
  configServer.handleClient();
  if (restartTransportRequested &&
      (configSaveUntil == 0 || static_cast<int32_t>(millis() - configSaveUntil) >= 0)) {
    restartTransportRequested = false;
    configSaveUntil = 0;
    stopConfigPortal();
    uiState = UiState::Ready;
    if (!initTransport()) {
      fail("Transport init failed");
    } else {
      drawUi(true);
    }
    return;
  }
  if (portalActive) {
    dnsServer.processNextRequest();
    maintainWiFiScan();
    rotateSetupInfo();
    if (static_cast<int32_t>(millis() - configWindowUntil) >= 0) {
      stopConfigPortal();
      restartTransportRequested = true;
    }
    return;
  }
  if (activeTransport != TransportMode::WiFi) {
    return;
  }
  const wl_status_t wifiStatus = WiFi.status();
  if (wifiStatus != lastWiFiStatus || millis() - lastNetworkStatusLog > 10000) {
    lastWiFiStatus = wifiStatus;
    lastNetworkStatusLog = millis();
    Serial.printf("WiFi status=%d IP=%s RSSI=%d\n", static_cast<int>(wifiStatus),
                  WiFi.localIP().toString().c_str(), WiFi.RSSI());
  }
  if (wifiStatus != WL_CONNECTED) {
    snprintf(lastResult, sizeof(lastResult), "%s", wifiStatusMessage(wifiStatus));
    return;
  }
  if (!mdnsActive) {
    const String hostname = "echolink-" + deviceSuffix();
    if (MDNS.begin(hostname.c_str())) {
      MDNS.addService("http", "tcp", 80);
      mdnsActive = true;
    }
  }
  const int workerState = mqttWorkerState;
  if (mqttReady && lastMqttState != MQTT_CONNECTED) {
    lastMqttState = MQTT_CONNECTED;
    if (outgoingVoiceCount > 0) {
      snprintf(lastResult, sizeof(lastResult), "MQTT READY Q:%u", outgoingVoiceCount);
    } else {
      snprintf(lastResult, sizeof(lastResult), "WiFi link ready");
    }
    drawUi(true);
  } else if (!mqttReady && workerState != lastMqttState) {
    lastMqttState = workerState;
    if (static_cast<int32_t>(millis() - mqttRetryPausedUntil) < 0) {
      snprintf(lastResult, sizeof(lastResult), "MQTT RETRY 3X");
    } else if (outgoingVoiceCount > 0) {
      snprintf(lastResult, sizeof(lastResult), "QUEUED %u / WAIT", outgoingVoiceCount);
    } else {
      snprintf(lastResult, sizeof(lastResult), "%s", mqttStatusMessage(workerState));
    }
    drawUi(true);
  }
}

bool sendPacket(echolink::PacketType type, uint16_t sessionId, uint16_t sequence, uint32_t totalBytes,
                const int8_t* payload = nullptr, uint16_t payloadBytes = 0) {
  uint8_t frame[sizeof(echolink::PacketHeader) + ECHOLINK_AUDIO_CHUNK_BYTES];
  const size_t frameBytes = echolink::encodePacket(
      frame, sizeof(frame), type, ECHOLINK_GROUP_ID, deviceId, sessionId, sequence, totalBytes,
      payload, payloadBytes);
  return frameBytes > 0 && sendTransportFrame(frame, frameBytes);
}

void resetOutgoingProgress() {
  outgoingPhase = OutgoingPhase::Start;
  outgoingOffset = 0;
  outgoingSequence = 0;
}

void queueRecordedVoice() {
  if (txAudioBytes == 0) {
    stopAudio();
    uiState = UiState::Ready;
    return;
  }
  if (outgoingVoiceCount >= kMaxOutgoingVoices) {
    snprintf(lastResult, sizeof(lastResult), "TX QUEUE FULL");
    txAudioBytes = 0;
    recordingStopRequested = false;
    stopAudio();
    uiState = UiState::Ready;
    return;
  }
  const uint8_t index = (outgoingVoiceHead + outgoingVoiceCount) % kMaxOutgoingVoices;
  memcpy(outgoingVoices[index].data, txAudio, txAudioBytes);
  outgoingVoices[index].bytes = txAudioBytes;
  outgoingVoices[index].sessionId = static_cast<uint16_t>(esp_random());
  ++outgoingVoiceCount;
  snprintf(lastResult, sizeof(lastResult), "QUEUED %u", outgoingVoiceCount);
  Serial.printf("Voice queued: %u bytes, queue=%u\n", static_cast<unsigned>(txAudioBytes),
                outgoingVoiceCount);
  txAudioBytes = 0;
  recordingStopRequested = false;
  stopAudio();
  uiState = UiState::Ready;
}

bool outgoingTransportReady() {
  if (activeTransport == TransportMode::Local) {
    return espNowActive;
  }
  return WiFi.status() == WL_CONNECTED && mqttReady;
}

void completeOutgoingVoice() {
  const auto& voice = outgoingVoices[outgoingVoiceHead];
  Serial.printf("Voice sent: session=%u bytes=%u\n", voice.sessionId,
                static_cast<unsigned>(voice.bytes));
  outgoingVoiceHead = (outgoingVoiceHead + 1) % kMaxOutgoingVoices;
  --outgoingVoiceCount;
  resetOutgoingProgress();
  if (unreadVoiceCount == 0) {
    if (outgoingVoiceCount > 0) {
      snprintf(lastResult, sizeof(lastResult), "TX SENT Q:%u", outgoingVoiceCount);
    } else {
      snprintf(lastResult, sizeof(lastResult), "TX SENT");
    }
  }
}

bool pollMqttPublishResult(bool* success) {
  if (mqttPublishResultQueue == nullptr) {
    return false;
  }
  MqttPublishResult result;
  while (xQueueReceive(mqttPublishResultQueue, &result, 0) == pdTRUE) {
    if (mqttPublishPending && result.generation == mqttTransportGeneration &&
        result.id == pendingMqttPublishId) {
      mqttPublishPending = false;
      *success = result.success;
      return true;
    }
  }
  return false;
}

bool sendCurrentOutgoingFrame(const OutgoingVoice& voice) {
  if (outgoingPhase == OutgoingPhase::Start) {
    return sendPacket(echolink::PacketType::VoiceStart, voice.sessionId, 0, voice.bytes);
  }
  if (outgoingPhase == OutgoingPhase::Audio) {
    const uint16_t bytes = static_cast<uint16_t>(
        min(static_cast<size_t>(ECHOLINK_AUDIO_CHUNK_BYTES), voice.bytes - outgoingOffset));
    return sendPacket(echolink::PacketType::Audio, voice.sessionId, outgoingSequence, voice.bytes,
                      voice.data + outgoingOffset, bytes);
  }
  return sendPacket(echolink::PacketType::VoiceEnd, voice.sessionId, 0, voice.bytes);
}

bool advanceOutgoingProgress(const OutgoingVoice& voice) {
  if (outgoingPhase == OutgoingPhase::Start) {
    outgoingPhase = OutgoingPhase::Audio;
    return false;
  }
  if (outgoingPhase == OutgoingPhase::Audio) {
    const size_t bytes = min(static_cast<size_t>(ECHOLINK_AUDIO_CHUNK_BYTES),
                             voice.bytes - outgoingOffset);
    outgoingOffset += bytes;
    ++outgoingSequence;
    if (outgoingOffset >= voice.bytes) {
      outgoingPhase = OutgoingPhase::End;
    }
    return false;
  }
  completeOutgoingVoice();
  return true;
}

void serviceOutgoingVoices() {
  if (outgoingVoiceCount == 0) {
    bool ignored = false;
    pollMqttPublishResult(&ignored);
    return;
  }
  if (uiState == UiState::Recording || uiState == UiState::Receiving ||
      uiState == UiState::Playing || uiState == UiState::Setup) {
    return;
  }

  auto& voice = outgoingVoices[outgoingVoiceHead];
  if (activeTransport == TransportMode::WiFi && mqttPublishPending) {
    bool success = false;
    if (!pollMqttPublishResult(&success)) {
      return;
    }
    if (!success) {
      ++droppedPackets;
      resetOutgoingProgress();
      Serial.println("MQTT TX failed; message queued for retry");
      return;
    }
    if (advanceOutgoingProgress(voice)) {
      return;
    }
  }

  if (!outgoingTransportReady()) {
    if (outgoingPhase != OutgoingPhase::Start) {
      resetOutgoingProgress();
      Serial.println("TX paused; transport lost, message will restart");
    }
    return;
  }

  if (!sendCurrentOutgoingFrame(voice)) {
    ++droppedPackets;
    resetOutgoingProgress();
    Serial.println("TX frame failed; message queued for retry");
    return;
  }
  // MQTT completion arrives asynchronously from the network task. ESP-NOW
  // reports queue acceptance synchronously, so its progress can advance now.
  if (activeTransport == TransportMode::Local) {
    advanceOutgoingProgress(voice);
  }
}

void serviceOutgoingNudge() {
  uint8_t type = 0;
  if (outgoingVoiceCount != 0 || mqttPublishPending || !lifetime::peekOutgoingNudge(&type) ||
      uiState == UiState::Recording || uiState == UiState::Receiving ||
      uiState == UiState::Playing || uiState == UiState::Setup || !outgoingTransportReady()) {
    return;
  }
  if (sendPacket(echolink::PacketType::Nudge, static_cast<uint16_t>(esp_random()), 0, 0,
                 reinterpret_cast<const int8_t*>(&type), 1)) {
    lifetime::markOutgoingNudgeSent();
    Serial.println("LifeTime nudge sent");
  }
}

void serviceBackgroundTransmits() {
  serviceOutgoingVoices();
  serviceOutgoingNudge();
}

uint32_t cooperativeLoopDelayMs() {
  if (uiState == UiState::Recording) {
    return 2;
  }
  if (outgoingVoiceCount > 0 || mqttPublishPending) {
    return 5;
  }
  return displaySleeping ? 40 : 20;
}

void startRecording() {
  if (uiState != UiState::Ready) {
    return;
  }
  if (outgoingVoiceCount >= kMaxOutgoingVoices) {
    snprintf(lastResult, sizeof(lastResult), "TX QUEUE FULL");
    drawUi(true);
    return;
  }
  useMicrophone();
  txAudioBytes = 0;
  recordingStopRequested = false;
  microphoneWriteIndex = 0;
  microphoneBlocksQueued = 0;
  snprintf(lastResult, sizeof(lastResult), "PTT active");
  Serial.println("PTT recording started");
  uiState = UiState::Recording;
}

void recordAudioBlock() {
  if (!microphoneEnabled) {
    fail("Mic unavailable");
    return;
  }

  if (recordingStopRequested || txAudioBytes + ECHOLINK_AUDIO_CHUNK_BYTES > kMaxAudioBytes) {
    return;
  }

  // M5Unified records asynchronously. Three buffers let the microphone task
  // fill the oldest block before this loop reads it, as in its mic example.
  if (!M5.Mic.record(microphoneSamples[microphoneWriteIndex], ECHOLINK_AUDIO_CHUNK_BYTES,
                     ECHOLINK_SAMPLE_RATE)) {
    fail("Mic record failed");
    return;
  }

  if (microphoneBlocksQueued >= kMicrophoneBufferCount - 1) {
    const size_t completedIndex = (microphoneWriteIndex + 1) % kMicrophoneBufferCount;
    for (size_t i = 0; i < ECHOLINK_AUDIO_CHUNK_BYTES; ++i) {
      txAudio[txAudioBytes + i] = static_cast<int8_t>(microphoneSamples[completedIndex][i] >> 8);
    }
    txAudioBytes += ECHOLINK_AUDIO_CHUNK_BYTES;
  }
  ++microphoneBlocksQueued;
  microphoneWriteIndex = (microphoneWriteIndex + 1) % kMicrophoneBufferCount;
}

void playNewVoiceAlert() {
  if (!voiceAlertPending || uiState == UiState::Recording || uiState == UiState::Sending ||
      uiState == UiState::Playing) {
    return;
  }
  voiceAlertPending = false;
  chirp(720);
  chirp(720);
  chirp(720);
  stopAudio();
  snprintf(lastResult, sizeof(lastResult), "NEW MSG %u", unreadVoiceCount);
  uiState = UiState::Ready;
  wakeDisplay();
  drawUi(true);
}

void playNextUnreadVoice() {
  if (uiState != UiState::Ready || unreadVoiceCount == 0) {
    return;
  }
  size_t bytes = 0;
  int8_t* data = nextUnreadVoice(&bytes, nullptr);
  if (data == nullptr || bytes == 0) {
    return;
  }
  uiState = UiState::Playing;
  drawUi(true);
  useSpeaker();
  M5.Speaker.playRaw(data, bytes, ECHOLINK_SAMPLE_RATE, false, 1, 0, false);
  while (M5.Speaker.isPlaying()) {
    M5.update();
    serviceIncomingTransportFrames();
    delay(5);
  }
  stopAudio();
  if (unreadVoiceCount > 0) {
    snprintf(lastResult, sizeof(lastResult), "NEW MSG %u", unreadVoiceCount);
  } else {
    snprintf(lastResult, sizeof(lastResult), "NO NEW MSG");
  }
  uiState = UiState::Ready;
}

void cycleVolume() {
  if (volume < 110) {
    volume = 160;
  } else if (volume < 200) {
    volume = 220;
  } else {
    volume = 80;
  }
  if (speakerEnabled) {
    M5.Speaker.setVolume(volume);
  }
  chirp(560 + volume * 2);
  stopAudio();
}

void toggleTransport() {
  if (uiState == UiState::Recording || uiState == UiState::Sending || uiState == UiState::Playing) {
    return;
  }
  activeTransport = activeTransport == TransportMode::Local ? TransportMode::WiFi
                                                             : TransportMode::Local;
  preferences.putUChar("mode", static_cast<uint8_t>(activeTransport));
  snprintf(lastResult, sizeof(lastResult), "%s selected",
           activeTransport == TransportMode::Local ? "Local" : "WiFi");
  uiState = UiState::Ready;
  restartTransportRequested = true;
  drawUi(true);
}

void handleSerialCommand() {
  if (Serial.available() == 0) {
    return;
  }
  const char command = static_cast<char>(Serial.read());
  TransportMode requested = activeTransport;
  if (command == 'W' || command == 'w') {
    requested = TransportMode::WiFi;
  } else if (command == 'L' || command == 'l') {
    requested = TransportMode::Local;
  } else {
    return;
  }
  if (requested == activeTransport) {
    Serial.printf("Transport already %s\n", transportText());
    return;
  }
  activeTransport = requested;
  preferences.putUChar("mode", static_cast<uint8_t>(activeTransport));
  snprintf(lastResult, sizeof(lastResult), "%s selected",
           activeTransport == TransportMode::Local ? "Local" : "WiFi");
  uiState = UiState::Ready;
  restartTransportRequested = true;
  drawUi(true);
  Serial.printf("Transport changed to %s\n", transportText());
}

void fail(const char* message) {
  Serial.println(message);
  snprintf(errorMessage, sizeof(errorMessage), "%s", message);
  uiState = UiState::Error;
  drawUi(true);
}

}  // namespace

void setup() {
  Serial.begin(115200);
  auto cfg = M5.config();
  cfg.internal_mic = true;
  cfg.internal_spk = true;
  cfg.internal_imu = true;
  M5.begin(cfg);
  M5.Display.setRotation(0);
  M5.Display.setTextDatum(textdatum_t::top_left);
  M5.Display.setBrightness(ECHOLINK_DISPLAY_BRIGHTNESS);

  deviceId = static_cast<uint32_t>(ESP.getEfuseMac());
  preferences.begin("echolink", false);
  loadNetworkSettings();
  activeApp = preferences.getUChar("app", 0) == 1 ? AppMode::LifeTime : AppMode::EchoLink;
  launcherSelection = activeApp;
  M5.BtnA.setHoldThresh(900);
  M5.BtnB.setHoldThresh(900);
  lifetime::begin();
  txAudio = static_cast<int8_t*>(ps_malloc(kMaxAudioBytes));
  rxAudio = static_cast<int8_t*>(ps_malloc(kMaxAudioBytes));
  rxChunkSeen = static_cast<bool*>(ps_malloc(kMaxChunks * sizeof(bool)));
  for (uint8_t i = 0; i < kMaxUnreadVoices; ++i) {
    voiceQueueData[i] = static_cast<int8_t*>(ps_malloc(kMaxAudioBytes));
  }
  for (uint8_t i = 0; i < kMaxOutgoingVoices; ++i) {
    outgoingVoices[i].data = static_cast<int8_t*>(ps_malloc(kMaxAudioBytes));
  }
  bool queueMemoryOk = true;
  for (uint8_t i = 0; i < kMaxUnreadVoices; ++i) {
    queueMemoryOk = queueMemoryOk && voiceQueueData[i] != nullptr;
  }
  for (uint8_t i = 0; i < kMaxOutgoingVoices; ++i) {
    queueMemoryOk = queueMemoryOk && outgoingVoices[i].data != nullptr;
  }
  if (txAudio == nullptr || rxAudio == nullptr || rxChunkSeen == nullptr || !queueMemoryOk) {
    fail("PSRAM alloc failed");
    return;
  }
  incomingFrameQueue = xQueueCreate(kIncomingFrameQueueDepth, sizeof(TransportFrame));
  mqttPublishQueue = xQueueCreate(1, sizeof(MqttPublishRequest));
  mqttPublishResultQueue = xQueueCreate(4, sizeof(MqttPublishResult));
  if (incomingFrameQueue == nullptr || mqttPublishQueue == nullptr ||
      mqttPublishResultQueue == nullptr) {
    fail("Transport queue failed");
    return;
  }
  if (xTaskCreatePinnedToCore(mqttWorkerTask, "echolink-mqtt", 6144, nullptr, 1,
                              &mqttWorkerHandle, 0) != pdPASS) {
    fail("MQTT task failed");
    return;
  }
  lastUserActivity = millis();
  resetReceiveBuffer();
  stopAudio();

  if (!initTransport()) {
    fail("Radio init failed");
    return;
  }
  if (activeApp == AppMode::LifeTime) {
    lifetime::onEnter();
  } else {
    drawUi(true);
  }
  Serial.printf("EchoLink device %08lx ready in %s mode\n", static_cast<unsigned long>(deviceId),
                transportText());
}

void loop() {
  M5.update();
  handleSerialCommand();
  if (M5.BtnA.wasPressed() || M5.BtnB.wasPressed()) {
    wakeDisplay();
  }
  const bool appSwitchGestureActive = handleAppSwitcherGesture();
  maintainTransport();
  serviceIncomingTransportFrames();
  lifetime::update(activeApp == AppMode::LifeTime);
  if (millis() - lastDiagnosticHeartbeat >= kDiagnosticIntervalMs) {
    lastDiagnosticHeartbeat = millis();
    Serial.printf("HEARTBEAT app=%s mode=%s state=%s wifi=%d mqtt=%d txq=%u unread=%u psram=%u\n",
                  activeApp == AppMode::LifeTime ? "LIFETIME"
                  : activeApp == AppMode::Launcher ? "APPS" : "ECHOLINK",
                  activeTransport == TransportMode::Local ? "LOCAL" : "WIFI", stateText(uiState),
                  static_cast<int>(WiFi.status()), mqttReady, outgoingVoiceCount,
                  unreadVoiceCount, static_cast<unsigned>(ESP.getFreePsram()));
  }

  playNewVoiceAlert();
  if (uiState == UiState::Ready && !voiceAlertPending && lifetime::takeAlert()) {
    chirp(880);
    chirp(1320);
    stopAudio();
    wakeDisplay();
  }

  if (appSwitchGestureActive) {
    serviceBackgroundTransmits();
    maintainDisplayPower();
    delay(cooperativeLoopDelayMs());
    return;
  }

  if (activeApp == AppMode::Launcher) {
    if (M5.BtnB.wasClicked()) {
      launcherSelection = launcherSelection == AppMode::EchoLink ? AppMode::LifeTime
                                                                  : AppMode::EchoLink;
      drawLauncher();
    } else if (M5.BtnA.wasClicked()) {
      openSelectedApp();
    }
    serviceBackgroundTransmits();
    maintainDisplayPower();
    delay(cooperativeLoopDelayMs());
    return;
  }

  if (activeApp == AppMode::LifeTime) {
    if (M5.BtnA.wasReleasedAfterHold()) {
      lifetime::handleALongPress();
    } else if (M5.BtnA.wasClicked()) {
      lifetime::handleAShortPress();
    }
    if (M5.BtnB.wasReleasedAfterHold()) {
      lifetime::handleBLongPress();
    } else if (M5.BtnB.wasClicked()) {
      lifetime::handleBShortPress();
    }
    serviceBackgroundTransmits();
    if (!displaySleeping) {
      lifetime::draw();
    }
    maintainDisplayPower();
    delay(cooperativeLoopDelayMs());
    return;
  }

  if (!bPortalHoldHandled && M5.BtnB.pressedFor(2500)) {
    bPortalHoldHandled = true;
    startConfigPortal();
    drawUi(true);
  }
  if (M5.BtnB.wasReleasedAfterHold()) {
    bPortalHoldHandled = false;
  } else if (!bPortalHoldHandled && M5.BtnB.wasDoubleClicked()) {
    toggleTransport();
  } else if (!bPortalHoldHandled && M5.BtnB.wasSingleClicked()) {
    cycleVolume();
  }

  if (uiState == UiState::Error || uiState == UiState::Setup) {
    drawUi();
    delay(20);
    return;
  }

  if (uiState == UiState::Ready && M5.BtnA.wasPressed()) {
    if (unreadVoiceCount > 0) {
      aPlayPending = true;
      aPressedAt = millis();
    } else {
      startRecording();
    }
  }

  if (aPlayPending && M5.BtnA.isPressed() &&
      millis() - aPressedAt >= kAPlayOrPttThresholdMs) {
    aPlayPending = false;
    startRecording();
  }

  if (aPlayPending && M5.BtnA.wasReleased()) {
    aPlayPending = false;
    if (millis() - aPressedAt < kAPlayOrPttThresholdMs) {
      playNextUnreadVoice();
    }
  }

  if (uiState == UiState::Recording) {
    if (!M5.BtnA.isPressed() || txAudioBytes >= kMaxAudioBytes) {
      recordingStopRequested = true;
    }
    recordAudioBlock();
    if (recordingStopRequested) {
      Serial.printf("PTT recording complete: %u bytes\n", static_cast<unsigned>(txAudioBytes));
      queueRecordedVoice();
    }
  }

  serviceBackgroundTransmits();

  drawUi();
  maintainDisplayPower();
  // Recording and queued frames get short scheduling gaps. Idle mode can
  // sleep longer because radio callbacks and MQTT run independently.
  delay(cooperativeLoopDelayMs());
}
