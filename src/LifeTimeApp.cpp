#include "LifeTimeApp.h"

#include <Arduino.h>
#include <M5Unified.h>
#include <Preferences.h>
#include <WiFi.h>

#include <math.h>
#include <time.h>

#include "LifeTimeConfig.h"

namespace lifetime {
namespace {

constexpr uint32_t kBatteryRefreshMs = 5000;
constexpr uint32_t kAnimatedRefreshMs = 120;
constexpr uint32_t kStaticRefreshMs = 1000;
constexpr uint8_t kPresetCount = 7;
constexpr uint32_t kPresetMinutes[kPresetCount] = {1, 3, 5, 10, 15, 30, 60};

constexpr uint16_t kBackground = 0x0862;
constexpr uint16_t kSurface = 0x18E4;
constexpr uint16_t kSurfaceRaised = 0x29A6;
constexpr uint16_t kMuted = 0x8C71;
constexpr uint16_t kWhite = 0xFFFF;
constexpr uint16_t kMint = 0x67F3;
constexpr uint16_t kCyan = 0x4DDF;
constexpr uint16_t kYellow = 0xFDE6;
constexpr uint16_t kCoral = 0xFB8F;
constexpr uint16_t kGlass = 0xB67B;
constexpr uint16_t kFrame = 0xB30D;

enum class Page : uint8_t {
  Now,
  Focus,
  Flow,
  Together,
};

enum class Face : int8_t {
  Unknown = -1,
  ScreenUp = 0,
  ScreenDown,
  Right,
  Left,
  Top,
  Bottom,
};

struct TimerState {
  uint32_t remainingMs = 0;
  uint32_t lastTickMs = 0;
  bool running = false;
  bool completed = false;
};

Preferences preferences;
M5Canvas canvas(&M5.Display);
bool canvasReady = false;
Page page = Page::Now;
TimerState focusTimer;
TimerState flowTimer;
uint32_t countValue = 0;
uint8_t presetIndex = 0;
int batteryLevel = -1;
bool charging = false;
bool imuAvailable = false;
bool ntpConfigured = false;
bool uiDirty = true;
bool alertPending = false;
Face stableFace = Face::Unknown;
Face flowFace = Face::Unknown;
Face candidateFace = Face::Unknown;
uint32_t candidateSinceMs = 0;
bool flowTriggeredForFace = false;
uint32_t lastBatteryReadMs = 0;
uint32_t lastImuReadMs = 0;
uint32_t lastUiDrawMs = 0;
uint32_t animationFrame = 0;

bool outgoingNudgePending = false;
uint8_t outgoingNudgeType = 1;
uint32_t nudgeCooldownUntil = 0;
uint32_t lastNudgeSentAt = 0;
uint32_t lastNudgeReceivedAt = 0;
uint32_t lastNudgeSender = 0;
uint32_t nudgeSentCount = 0;
uint32_t nudgeReceivedCount = 0;
uint8_t motionPeakCount = 0;
uint32_t lastMotionPeakAt = 0;

const char* pageTitle() {
  switch (page) {
    case Page::Now: return "NOW";
    case Page::Focus: return "FOCUS";
    case Page::Flow: return "FLOW";
    case Page::Together: return "TOGETHER";
  }
  return "LIFETIME";
}

const char* faceTitle(Face face) {
  switch (face) {
    case Face::ScreenUp: return "SCREEN UP";
    case Face::ScreenDown: return "SCREEN DOWN";
    case Face::Right: return "RIGHT SIDE";
    case Face::Left: return "LEFT SIDE";
    case Face::Top: return "TOP EDGE";
    case Face::Bottom: return "BOTTOM EDGE";
    case Face::Unknown: return "CHOOSE A SIDE";
  }
  return "CHOOSE A SIDE";
}

void markUiDirty() {
  uiDirty = true;
}

uint32_t presetDurationMs() {
  return kPresetMinutes[presetIndex] * 60UL * 1000UL;
}

uint32_t faceDurationMs(Face face) {
  const int index = static_cast<int>(face);
  return index < 0 || index >= 6 ? 0 : LIFETIME_FACE_DURATION_SECONDS[index] * 1000UL;
}

void formatDuration(uint32_t remainingMs, char* buffer, size_t bufferSize) {
  const uint32_t totalSeconds = (remainingMs + 999) / 1000;
  const uint32_t hours = totalSeconds / 3600;
  const uint32_t minutes = (totalSeconds % 3600) / 60;
  const uint32_t seconds = totalSeconds % 60;
  if (hours > 0) {
    snprintf(buffer, bufferSize, "%02lu:%02lu:%02lu", static_cast<unsigned long>(hours),
             static_cast<unsigned long>(minutes), static_cast<unsigned long>(seconds));
  } else {
    snprintf(buffer, bufferSize, "%02lu:%02lu", static_cast<unsigned long>(minutes),
             static_cast<unsigned long>(seconds));
  }
}

bool getLocalTimeSafe(struct tm* localTime) {
  const time_t now = time(nullptr);
  return localTime != nullptr && now >= 1700000000 && localtime_r(&now, localTime) != nullptr;
}

void drawCentered(const String& text, int16_t y, uint8_t size, uint16_t color = kWhite,
                  uint16_t background = kBackground) {
  canvas.setTextDatum(textdatum_t::middle_center);
  canvas.setTextColor(color, background);
  canvas.setTextSize(size);
  canvas.drawString(text, canvas.width() / 2, y);
}

void drawBattery() {
  constexpr int16_t x = 101;
  constexpr int16_t y = 12;
  canvas.drawRoundRect(x, y, 25, 12, 3, kMuted);
  canvas.fillRect(x + 25, y + 4, 2, 4, kMuted);
  if (batteryLevel >= 0) {
    const int16_t fill = static_cast<int16_t>(19 * batteryLevel / 100);
    canvas.fillRoundRect(x + 3, y + 3, fill, 6, 2,
                         batteryLevel <= 15 ? kCoral : kMint);
  }
  if (charging) {
    canvas.setTextDatum(textdatum_t::middle_right);
    canvas.setTextColor(kYellow, kBackground);
    canvas.setTextSize(1);
    canvas.drawString("+", x - 5, y + 6);
  }
}

void drawHeader(uint16_t accent) {
  canvas.fillScreen(kBackground);
  canvas.fillRoundRect(8, 10, 4, 16, 2, accent);
  canvas.setTextDatum(textdatum_t::middle_left);
  canvas.setTextColor(kWhite, kBackground);
  canvas.setTextSize(1);
  canvas.drawString(pageTitle(), 18, 18);
  drawBattery();
  for (uint8_t i = 0; i < 4; ++i) {
    const bool active = static_cast<uint8_t>(page) == i;
    canvas.fillCircle(51 + i * 11, 18, active ? 3 : 2, active ? accent : kSurfaceRaised);
  }
}

void drawFooter(const char* left, const char* right, uint16_t accent) {
  canvas.drawFastHLine(10, 207, 115, kSurfaceRaised);
  canvas.setTextSize(1);
  canvas.setTextDatum(textdatum_t::middle_left);
  canvas.setTextColor(accent, kBackground);
  canvas.drawString(left, 10, 222);
  canvas.setTextDatum(textdatum_t::middle_right);
  canvas.setTextColor(kMuted, kBackground);
  canvas.drawString(right, 125, 222);
}

void drawNowPage() {
  drawHeader(kMint);
  struct tm localTime;
  char timeText[16] = "--:--:--";
  char dateText[24] = "WAITING FOR TIME";
  if (getLocalTimeSafe(&localTime)) {
    snprintf(timeText, sizeof(timeText), "%02d:%02d:%02d", localTime.tm_hour,
             localTime.tm_min, localTime.tm_sec);
    static constexpr const char* kMonths[] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN",
                                               "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};
    snprintf(dateText, sizeof(dateText), "%s %02d  %04d", kMonths[localTime.tm_mon],
             localTime.tm_mday, localTime.tm_year + 1900);
  }
  drawCentered(timeText, 67, 2);
  drawCentered(dateText, 91, 1, kMuted);

  canvas.drawFastHLine(14, 111, 107, kSurfaceRaised);
  canvas.setTextDatum(textdatum_t::middle_left);
  canvas.setTextColor(kMuted, kBackground);
  canvas.drawString("MOMENTS", 16, 137);
  canvas.setTextColor(kWhite, kBackground);
  canvas.setTextSize(countValue > 999999 ? 2 : 3);
  canvas.setTextDatum(textdatum_t::middle_right);
  canvas.drawString(String(countValue), 119, 137);
  canvas.setTextSize(1);

  char focusText[28];
  if (focusTimer.running) {
    char remaining[16];
    formatDuration(focusTimer.remainingMs, remaining, sizeof(remaining));
    snprintf(focusText, sizeof(focusText), "FOCUS  %s", remaining);
  } else {
    snprintf(focusText, sizeof(focusText), "%s", WiFi.status() == WL_CONNECTED ? "TIME SYNCED"
                                                                                : "TIME OFFLINE");
  }
  drawCentered(focusText, 181, 1, focusTimer.running ? kYellow : kMuted);
  drawFooter("A NEXT", "B +1", kMint);
}

void drawFocusPage() {
  drawHeader(kYellow);
  const uint32_t totalMs = presetDurationMs();
  const float fraction = totalMs == 0 ? 0.0f : min(1.0f, static_cast<float>(focusTimer.remainingMs) /
                                                             static_cast<float>(totalMs));
  constexpr int16_t cx = 67;
  constexpr int16_t cy = 105;
  canvas.drawArc(cx, cy, 49, 43, 0, 360, kSurfaceRaised);
  const int endAngle = static_cast<int>(360.0f * fraction);
  if (endAngle > 1) {
    canvas.drawArc(cx, cy, 49, 43, 0, endAngle, focusTimer.completed ? kCoral : kYellow);
  }
  char duration[16];
  formatDuration(focusTimer.remainingMs, duration, sizeof(duration));
  drawCentered(duration, cy - 3, 3);
  const char* state = focusTimer.running ? "IN FOCUS"
                      : focusTimer.completed ? "COMPLETE"
                      : "READY";
  drawCentered(state, cy + 27, 1, focusTimer.completed ? kCoral : kMuted);
  char preset[24];
  snprintf(preset, sizeof(preset), "%lu MIN PRESET",
           static_cast<unsigned long>(kPresetMinutes[presetIndex]));
  drawCentered(preset, 174, 1, kMuted);
  drawFooter("A NEXT", focusTimer.running ? "A HOLD PAUSE" : "B PRESET", kYellow);
}

void drawHourglass(uint32_t remainingMs, uint32_t totalMs) {
  constexpr int16_t cx = 67;
  constexpr int16_t top = 45;
  constexpr int16_t neck = 105;
  constexpr int16_t bottom = 164;
  constexpr int16_t half = 34;

  canvas.fillRoundRect(cx - half - 5, top - 5, half * 2 + 10, 8, 4, kFrame);
  canvas.fillRoundRect(cx - half - 5, bottom - 3, half * 2 + 10, 8, 4, kFrame);
  canvas.drawLine(cx - half, top + 3, cx - 4, neck - 3, kGlass);
  canvas.drawLine(cx - half + 2, top + 3, cx - 2, neck - 2, kGlass);
  canvas.drawLine(cx + half, top + 3, cx + 4, neck - 3, kGlass);
  canvas.drawLine(cx + half - 2, top + 3, cx + 2, neck - 2, kGlass);
  canvas.drawLine(cx - 4, neck + 3, cx - half, bottom - 3, kGlass);
  canvas.drawLine(cx - 2, neck + 2, cx - half + 2, bottom - 3, kGlass);
  canvas.drawLine(cx + 4, neck + 3, cx + half, bottom - 3, kGlass);
  canvas.drawLine(cx + 2, neck + 2, cx + half - 2, bottom - 3, kGlass);

  const float fraction = totalMs == 0 ? 0.0f : min(1.0f, static_cast<float>(remainingMs) /
                                                             static_cast<float>(totalMs));
  const int16_t topSandY = neck - 6 - static_cast<int16_t>(48.0f * fraction);
  const int16_t topSandHalf = static_cast<int16_t>(3 + 28.0f * fraction);
  if (fraction > 0.01f) {
    canvas.fillTriangle(cx - topSandHalf, topSandY, cx + topSandHalf, topSandY, cx, neck - 5,
                        kYellow);
  }
  const float bottomFraction = 1.0f - fraction;
  const int16_t bottomSandY = bottom - 7 - static_cast<int16_t>(45.0f * bottomFraction);
  if (bottomFraction > 0.01f) {
    canvas.fillTriangle(cx, bottomSandY, cx - 29, bottom - 7, cx + 29, bottom - 7, kYellow);
  }
  if (flowTimer.running && remainingMs > 0) {
    for (uint8_t i = 0; i < 5; ++i) {
      const int16_t y = neck + 1 + ((animationFrame * 4 + i * 9) % 42);
      const int16_t x = cx + static_cast<int16_t>((i % 3) - 1);
      if (y < bottomSandY) {
        canvas.fillCircle(x, y, i % 2 == 0 ? 1 : 0, kYellow);
      }
    }
  }
}

void drawFlowPage() {
  drawHeader(kCyan);
  drawHourglass(flowTimer.remainingMs, faceDurationMs(flowFace));
  char duration[16];
  formatDuration(flowTimer.remainingMs, duration, sizeof(duration));
  drawCentered(duration, 182, 2);
  const Face shownFace = flowFace == Face::Unknown ? stableFace : flowFace;
  drawCentered(imuAvailable ? faceTitle(shownFace) : "IMU NOT FOUND", 199, 1,
               imuAvailable ? kCyan : kCoral);
  drawFooter("A NEXT", flowTimer.running ? "A HOLD PAUSE" : "TURN TO START", kCyan);
}

void drawDeviceGlyph(int16_t x, int16_t y, uint16_t accent, bool active) {
  canvas.fillRoundRect(x - 13, y - 24, 26, 48, 6, active ? accent : kSurfaceRaised);
  canvas.fillRoundRect(x - 9, y - 18, 18, 29, 3, kBackground);
  canvas.fillCircle(x, y + 18, 2, active ? kBackground : kMuted);
}

void drawTogetherPage() {
  drawHeader(kCoral);
  const uint32_t now = millis();
  const bool receivedPulse = now - lastNudgeReceivedAt < 1800;
  const bool sentPulse = now - lastNudgeSentAt < 1200;
  const bool pulse = receivedPulse || sentPulse || outgoingNudgePending;
  const int16_t radius = 14 + static_cast<int16_t>((animationFrame * 4) % 28);
  if (pulse) {
    canvas.drawCircle(67, 96, radius, receivedPulse ? kCoral : kMint);
    if (radius > 20) {
      canvas.drawCircle(67, 96, radius - 11, kSurfaceRaised);
    }
  }
  drawDeviceGlyph(35, 96, kMint, sentPulse || outgoingNudgePending);
  drawDeviceGlyph(99, 96, kCoral, receivedPulse);
  canvas.drawFastHLine(49, 96, 36, pulse ? kWhite : kMuted);
  canvas.fillTriangle(82, 92, 88, 96, 82, 100, pulse ? kWhite : kMuted);

  const char* status = outgoingNudgePending ? "WAITING FOR LINK"
                       : receivedPulse ? "PING FROM YOUR PEER"
                       : sentPulse ? "PING SENT"
                       : "DOUBLE SHAKE TO PING";
  drawCentered(status, 147, 1, receivedPulse ? kCoral : (sentPulse ? kMint : kWhite));
  if (lastNudgeSender != 0 && receivedPulse) {
    char peer[20];
    snprintf(peer, sizeof(peer), "PEER %04lx", static_cast<unsigned long>(lastNudgeSender & 0xFFFF));
    drawCentered(peer, 165, 1, kMuted);
  }
  char totals[32];
  snprintf(totals, sizeof(totals), "SENT %lu   FELT %lu",
           static_cast<unsigned long>(nudgeSentCount),
           static_cast<unsigned long>(nudgeReceivedCount));
  drawCentered(totals, 187, 1, kMuted);
  drawFooter("A NEXT", "B SEND", kCoral);
}

void updateBattery() {
  const uint32_t now = millis();
  if (lastBatteryReadMs != 0 && now - lastBatteryReadMs < kBatteryRefreshMs) {
    return;
  }
  lastBatteryReadMs = now;
  const int nextBatteryLevel = M5.Power.getBatteryLevel();
  const bool nextCharging =
      M5.Power.isCharging() == m5::Power_Class::is_charging_t::is_charging;
  if (nextBatteryLevel != batteryLevel || nextCharging != charging) {
    batteryLevel = nextBatteryLevel;
    charging = nextCharging;
    markUiDirty();
  }
}

void ensureNtp() {
  if (!ntpConfigured && WiFi.status() == WL_CONNECTED) {
    configTzTime(LIFETIME_TIMEZONE, LIFETIME_NTP_SERVER_1, LIFETIME_NTP_SERVER_2);
    ntpConfigured = true;
  }
}

void resetTimer(TimerState& timer, uint32_t durationMs) {
  timer.running = false;
  timer.completed = false;
  timer.remainingMs = durationMs;
  timer.lastTickMs = millis();
  markUiDirty();
}

void finishTimer(TimerState& timer, const char* name) {
  timer.running = false;
  timer.remainingMs = 0;
  timer.completed = true;
  alertPending = true;
  Serial.printf("%s complete\n", name);
  markUiDirty();
}

void updateTimer(TimerState& timer, const char* name) {
  if (!timer.running) {
    return;
  }
  const uint32_t beforeSeconds = (timer.remainingMs + 999) / 1000;
  const uint32_t now = millis();
  const uint32_t elapsed = now - timer.lastTickMs;
  timer.lastTickMs = now;
  if (elapsed >= timer.remainingMs) {
    finishTimer(timer, name);
    return;
  }
  timer.remainingMs -= elapsed;
  if ((timer.remainingMs + 999) / 1000 != beforeSeconds) {
    markUiDirty();
  }
}

void startOrPauseTimer(TimerState& timer, uint32_t durationMs, const char* name) {
  if (timer.running) {
    timer.running = false;
    markUiDirty();
    return;
  }
  if (timer.remainingMs == 0 || timer.completed) {
    timer.remainingMs = durationMs;
    timer.completed = false;
  }
  timer.running = true;
  timer.lastTickMs = millis();
  Serial.printf("%s started\n", name);
  markUiDirty();
}

Face classifyFace(float x, float y, float z) {
  const float ax = fabsf(x);
  const float ay = fabsf(y);
  const float az = fabsf(z);
  if (max(ax, max(ay, az)) < 0.72f) {
    return Face::Unknown;
  }
  if (az >= ax && az >= ay) {
    return z >= 0.0f ? Face::ScreenUp : Face::ScreenDown;
  }
  if (ax >= ay) {
    return x >= 0.0f ? Face::Right : Face::Left;
  }
  return y >= 0.0f ? Face::Top : Face::Bottom;
}

void requestNudge() {
  const uint32_t now = millis();
  if (outgoingNudgePending || static_cast<int32_t>(now - nudgeCooldownUntil) < 0) {
    return;
  }
  outgoingNudgePending = true;
  outgoingNudgeType = 1;
  nudgeCooldownUntil = now + 1500;
  markUiDirty();
}

void detectNudgeGesture(float x, float y, float z) {
  if (page != Page::Together || outgoingNudgePending) {
    return;
  }
  const float magnitude = sqrtf(x * x + y * y + z * z);
  const float motion = fabsf(magnitude - 1.0f);
  const uint32_t now = millis();
  if (motion < 0.72f || now - lastMotionPeakAt < 160) {
    return;
  }
  if (motionPeakCount == 0 || now - lastMotionPeakAt > 900) {
    motionPeakCount = 1;
  } else {
    motionPeakCount = 0;
    requestNudge();
  }
  lastMotionPeakAt = now;
}

void updateOrientation(bool foreground) {
  const uint32_t now = millis();
  if (!foreground || !imuAvailable || now - lastImuReadMs < LIFETIME_IMU_SAMPLE_MS) {
    return;
  }
  lastImuReadMs = now;
  if (!M5.Imu.update()) {
    return;
  }
  float x = 0.0f;
  float y = 0.0f;
  float z = 0.0f;
  if (!M5.Imu.getAccel(&x, &y, &z)) {
    return;
  }
  if (foreground) {
    detectNudgeGesture(x, y, z);
  }

  const Face detected = classifyFace(x, y, z);
  if (detected != candidateFace) {
    candidateFace = detected;
    candidateSinceMs = now;
    if (detected == Face::Unknown) {
      flowTriggeredForFace = false;
    }
    markUiDirty();
    return;
  }
  if (detected == Face::Unknown || now - candidateSinceMs < LIFETIME_ORIENTATION_STABLE_MS) {
    return;
  }
  if (stableFace != detected) {
    stableFace = detected;
    flowTriggeredForFace = false;
    markUiDirty();
  }
  if (foreground && page == Page::Flow && !flowTimer.running && !flowTimer.completed &&
      !flowTriggeredForFace) {
    const uint32_t durationMs = faceDurationMs(stableFace);
    if (durationMs > 0) {
      flowFace = stableFace;
      flowTimer.remainingMs = durationMs;
      flowTimer.running = true;
      flowTimer.lastTickMs = now;
      flowTriggeredForFace = true;
      Serial.printf("Flow %s: %lu seconds\n", faceTitle(stableFace),
                    static_cast<unsigned long>(durationMs / 1000));
      markUiDirty();
    }
  }
}

bool animatedNow(uint32_t now) {
  return (page == Page::Flow && flowTimer.running) ||
         (page == Page::Together && (outgoingNudgePending || now - lastNudgeSentAt < 1200 ||
                                     now - lastNudgeReceivedAt < 1800));
}

}  // namespace

void begin() {
  preferences.begin("lifetime", false);
  countValue = preferences.getULong("count", 0);
  presetIndex = min<uint8_t>(preferences.getUChar("preset", 0), kPresetCount - 1);
  nudgeSentCount = preferences.getULong("nudges_tx", 0);
  nudgeReceivedCount = preferences.getULong("nudges_rx", 0);
  resetTimer(focusTimer, presetDurationMs());
  imuAvailable = M5.Imu.isEnabled();
  canvas.setColorDepth(16);
  canvasReady = canvas.createSprite(M5.Display.width(), M5.Display.height()) != nullptr;
  if (canvasReady) {
    canvas.setTextWrap(false);
  }
  updateBattery();
  Serial.printf("LifeTime ready; IMU=%d canvas=%d\n", imuAvailable, canvasReady);
}

void onEnter() {
  markUiDirty();
  draw(true);
}

void update(bool foreground) {
  ensureNtp();
  updateBattery();
  updateOrientation(foreground);
  updateTimer(focusTimer, "Focus");
  updateTimer(flowTimer, "Flow");
}

void draw(bool force) {
  if (!canvasReady) {
    return;
  }
  const uint32_t now = millis();
  const bool animated = animatedNow(now);
  const uint32_t refreshMs = animated ? kAnimatedRefreshMs : kStaticRefreshMs;
  if (!force && now - lastUiDrawMs < refreshMs) {
    return;
  }
  if (!force && !uiDirty && !animated && page != Page::Now) {
    return;
  }
  ++animationFrame;
  switch (page) {
    case Page::Now: drawNowPage(); break;
    case Page::Focus: drawFocusPage(); break;
    case Page::Flow: drawFlowPage(); break;
    case Page::Together: drawTogetherPage(); break;
  }
  M5.Display.startWrite();
  canvas.pushSprite(0, 0);
  M5.Display.endWrite();
  lastUiDrawMs = now;
  uiDirty = false;
}

void handleAShortPress() {
  page = static_cast<Page>((static_cast<uint8_t>(page) + 1) % 4);
  if (page == Page::Flow) {
    flowTriggeredForFace = false;
  }
  motionPeakCount = 0;
  markUiDirty();
  draw(true);
}

void handleALongPress() {
  if (page == Page::Focus) {
    startOrPauseTimer(focusTimer, presetDurationMs(), "Focus");
  } else if (page == Page::Flow && flowTimer.remainingMs > 0 && !flowTimer.completed) {
    startOrPauseTimer(flowTimer, flowTimer.remainingMs, "Flow");
  }
}

void handleBShortPress() {
  if (page == Page::Now) {
    ++countValue;
    preferences.putULong("count", countValue);
  } else if (page == Page::Focus && !focusTimer.running) {
    presetIndex = (presetIndex + 1) % kPresetCount;
    preferences.putUChar("preset", presetIndex);
    resetTimer(focusTimer, presetDurationMs());
  } else if (page == Page::Together) {
    requestNudge();
  }
  markUiDirty();
}

void handleBLongPress() {
  if (page == Page::Now) {
    countValue = 0;
    preferences.putULong("count", countValue);
  } else if (page == Page::Focus) {
    resetTimer(focusTimer, presetDurationMs());
  } else if (page == Page::Flow) {
    resetTimer(flowTimer, faceDurationMs(stableFace));
    flowFace = stableFace;
    flowTriggeredForFace = false;
  } else if (page == Page::Together) {
    nudgeSentCount = 0;
    nudgeReceivedCount = 0;
    preferences.putULong("nudges_tx", 0);
    preferences.putULong("nudges_rx", 0);
  }
  markUiDirty();
}

bool takeAlert() {
  if (!alertPending) {
    return false;
  }
  alertPending = false;
  return true;
}

bool peekOutgoingNudge(uint8_t* type) {
  if (!outgoingNudgePending || type == nullptr) {
    return false;
  }
  *type = outgoingNudgeType;
  return true;
}

void markOutgoingNudgeSent() {
  if (!outgoingNudgePending) {
    return;
  }
  outgoingNudgePending = false;
  lastNudgeSentAt = millis();
  ++nudgeSentCount;
  preferences.putULong("nudges_tx", nudgeSentCount);
  markUiDirty();
}

void receiveNudge(uint8_t type, uint32_t senderId) {
  (void)type;
  lastNudgeSender = senderId;
  lastNudgeReceivedAt = millis();
  ++nudgeReceivedCount;
  preferences.putULong("nudges_rx", nudgeReceivedCount);
  alertPending = true;
  markUiDirty();
  Serial.printf("LifeTime nudge from %08lx\n", static_cast<unsigned long>(senderId));
}

}  // namespace lifetime
