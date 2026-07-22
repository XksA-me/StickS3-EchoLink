#include "LifeTimeApp.h"

#include <Arduino.h>
#include <M5Unified.h>
#include <Preferences.h>

#include <math.h>

#include "LifeTimeConfig.h"
#include "generated/woodblock_soft_cc0.h"

namespace lifetime {
namespace {

constexpr uint32_t kBatteryRefreshMs = 60000;
constexpr uint32_t kPulseRefreshMs = 1000;
constexpr uint32_t kHourglassFrameMs = 450;
constexpr uint32_t kMeritEffectMs = 1400;

constexpr uint16_t kBackground = 0x0862;
constexpr uint16_t kSurface = 0x18E4;
constexpr uint16_t kSurfaceRaised = 0x29A6;
constexpr uint16_t kMuted = 0x8C71;
constexpr uint16_t kWhite = 0xFFFF;
constexpr uint16_t kMint = 0x67F3;
constexpr uint16_t kCyan = 0x4DDF;
constexpr uint16_t kYellow = 0xFDE6;
constexpr uint16_t kCoral = 0xFB8F;
constexpr uint16_t kOrange = 0xFD20;
constexpr uint16_t kPink = 0xF81F;
constexpr uint16_t kBlue = 0x4D9F;
constexpr uint16_t kPurple = 0xA81F;
constexpr uint16_t kGlass = 0xB67B;
constexpr uint16_t kFrame = 0xB30D;
constexpr uint16_t kOceanDeep = 0x02B8;
constexpr uint16_t kOceanMid = 0x04FF;
constexpr uint16_t kOceanLight = 0x7DFF;

enum class Page : uint8_t {
  Battery,
  Hourglass,
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
  uint32_t reverseMs = 0;
  uint32_t totalMs = 0;
  uint32_t lastTickMs = 0;
  bool running = false;
  bool completed = false;
};

enum class HourglassMode : uint8_t {
  Countdown,
  Rewind,
};

struct Point {
  int16_t x;
  int16_t y;
};

Face oppositeFace(Face face);

Preferences preferences;
M5Canvas canvas(&M5.Display);
bool canvasReady = false;
Page page = Page::Battery;
TimerState hourglassTimer;
uint32_t countValue = 0;
int batteryLevel = -1;
int batteryMillivolts = 0;
bool charging = false;
bool wifiPowerContext = false;
bool monitorPowerContext = false;
bool imuAvailable = false;
bool uiDirty = true;
bool alertPending = false;
Face stableFace = Face::Unknown;
Face hourglassFace = Face::Unknown;
Face hourglassAnchorFace = Face::Unknown;
Face candidateFace = Face::Unknown;
uint32_t candidateSinceMs = 0;
uint32_t lastBatteryReadMs = 0;
uint32_t lastImuReadMs = 0;
uint32_t lastUiDrawMs = 0;
uint32_t animationFrame = 0;
uint32_t dropCounter = 0;
uint32_t fireworkUntil = 0;
HourglassMode hourglassMode = HourglassMode::Countdown;
float swayOffsetPx = 0.0f;
float swayKickPx = 0.0f;
float swayEnergy = 0.0f;
float lastAccelX = 0.0f;
bool haveAccelSample = false;
float turnAngleDeg = 0.0f;
uint32_t lastGyroMs = 0;
bool turnFlipLatched = false;
uint32_t meritUntil = 0;

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
    case Page::Battery: return "BATTERY";
    case Page::Hourglass: return "HOURGLASS";
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
    case Face::Unknown: return "SET A DIRECTION";
  }
  return "SET A DIRECTION";
}

void markUiDirty() {
  uiDirty = true;
}

uint32_t faceDurationMs(Face face) {
  (void)face;
  return LIFETIME_FACE_DURATION_SECONDS[0] * 1000UL;
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

void drawCentered(const String& text, int16_t y, uint8_t size, uint16_t color = kWhite,
                  uint16_t background = kBackground) {
  canvas.setTextDatum(textdatum_t::middle_center);
  canvas.setTextColor(color, background);
  canvas.setTextSize(size);
  canvas.drawString(text, canvas.width() / 2, y);
}

void drawHeader(uint16_t accent) {
  canvas.setFont(&fonts::Font0);
  canvas.fillScreen(kBackground);
  canvas.fillRoundRect(8, 10, 4, 16, 2, accent);
  canvas.setTextDatum(textdatum_t::middle_left);
  canvas.setTextColor(kWhite, kBackground);
  canvas.setTextSize(1);
  canvas.drawString(pageTitle(), 18, 18);
  for (uint8_t i = 0; i < 3; ++i) {
    const bool active = static_cast<uint8_t>(page) == i;
    canvas.fillCircle(98 + i * 11, 18, active ? 3 : 2, active ? accent : kSurfaceRaised);
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

void drawWoodenFish(int16_t cx, int16_t cy) {
  canvas.fillEllipse(cx, cy, 22, 14, kCoral);
  canvas.fillTriangle(cx - 21, cy - 3, cx - 29, cy - 13, cx - 12, cy - 10, kCoral);
  canvas.fillCircle(cx + 10, cy - 2, 2, kBackground);
  canvas.drawArc(cx - 1, cy + 1, 15, 8, 15, 165, kYellow);
  canvas.drawLine(cx + 17, cy - 16, cx + 27, cy - 27, kYellow);
  canvas.fillCircle(cx + 28, cy - 28, 3, kYellow);
}

void drawMeritEffect() {
  if (meritUntil == 0) {
    return;
  }
  const uint32_t now = millis();
  if (static_cast<int32_t>(meritUntil - now) <= 0) {
    return;
  }
  const uint32_t age = kMeritEffectMs - (meritUntil - now);
  const int16_t y = 158 - static_cast<int16_t>(age / 28);
  const uint16_t color = age < 450 ? kYellow : age < 950 ? kPink : kMint;
  canvas.setFont(&fonts::efontCN_16_b);
  canvas.setTextDatum(textdatum_t::middle_center);
  canvas.setTextColor(color, kBackground);
  canvas.setTextSize(1);
  canvas.drawString("功德 +1", 66, y);
  canvas.setFont(&fonts::Font0);
}

void drawBatteryPage() {
  drawHeader(kMint);
  constexpr int16_t x = 18;
  constexpr int16_t y = 45;
  constexpr int16_t width = 99;
  constexpr int16_t height = 54;
  canvas.drawRoundRect(x, y, width, height, 8, kMuted);
  canvas.fillRoundRect(x + width, y + 17, 5, 20, 2, kMuted);
  const int clampedLevel = batteryLevel < 0 ? 0 : min(100, batteryLevel);
  const int16_t fillWidth = static_cast<int16_t>((width - 8) * clampedLevel / 100);
  if (fillWidth > 0) {
    canvas.fillRoundRect(x + 4, y + 4, fillWidth, height - 8, 5,
                         clampedLevel <= 15 ? kCoral : kMint);
  }
  char percentage[12];
  snprintf(percentage, sizeof(percentage), batteryLevel < 0 ? "--%%" : "%d%%", batteryLevel);
  drawCentered(percentage, 72, 3, kWhite, fillWidth > 50 ? kMint : kBackground);

  char batteryDetail[32];
  if (charging) {
    snprintf(batteryDetail, sizeof(batteryDetail), "CHARGING  %.2f V",
             static_cast<float>(batteryMillivolts) / 1000.0f);
  } else {
    const uint32_t fullMinutes = monitorPowerContext ? LIFETIME_MONITOR_RUNTIME_MINUTES
                                 : wifiPowerContext ? LIFETIME_WIFI_RUNTIME_MINUTES
                                                    : LIFETIME_LOCAL_RUNTIME_MINUTES;
    const uint32_t estimatedMinutes = batteryLevel < 0 ? 0 : fullMinutes * batteryLevel / 100;
    snprintf(batteryDetail, sizeof(batteryDetail), "EST ~%luH %02luM  %.2fV",
             static_cast<unsigned long>(estimatedMinutes / 60),
             static_cast<unsigned long>(estimatedMinutes % 60),
             static_cast<float>(batteryMillivolts) / 1000.0f);
  }
  drawCentered(batteryDetail, 113, 1, charging ? kYellow : kMuted);

  canvas.drawFastHLine(14, 131, 107, kSurfaceRaised);
  drawWoodenFish(43, 166);
  canvas.setTextDatum(textdatum_t::middle_right);
  canvas.setTextColor(kWhite, kBackground);
  canvas.setTextSize(countValue > 999999 ? 2 : 3);
  canvas.drawString(String(countValue), 119, 166);
  drawMeritEffect();
  drawFooter("A HIT", "B NEXT", kMint);
}

uint32_t shapeSeed(uint32_t value) {
  value ^= value >> 16;
  value *= 0x7feb352dUL;
  value ^= value >> 15;
  value *= 0x846ca68bUL;
  return value ^ (value >> 16);
}

uint16_t shapeColor(uint32_t seed) {
  static constexpr uint16_t colors[] = {kYellow, kOrange, kPink, kBlue, kPurple, kMint};
  return colors[seed % (sizeof(colors) / sizeof(colors[0]))];
}

uint16_t blend565(uint16_t first, uint16_t second, float amount) {
  amount = constrain(amount, 0.0f, 1.0f);
  const uint8_t r1 = (first >> 11) & 0x1F;
  const uint8_t g1 = (first >> 5) & 0x3F;
  const uint8_t b1 = first & 0x1F;
  const uint8_t r2 = (second >> 11) & 0x1F;
  const uint8_t g2 = (second >> 5) & 0x3F;
  const uint8_t b2 = second & 0x1F;
  return (static_cast<uint16_t>(r1 + (r2 - r1) * amount) << 11) |
         (static_cast<uint16_t>(g1 + (g2 - g1) * amount) << 5) |
         static_cast<uint16_t>(b1 + (b2 - b1) * amount);
}

void drawShape(int16_t x, int16_t y, uint8_t shape, uint16_t color) {
  switch (shape % 4) {
    case 0:
      canvas.fillCircle(x, y, 2, color);
      break;
    case 1:
      canvas.fillTriangle(x, y - 4, x - 4, y + 2, x + 4, y + 2, color);
      canvas.fillTriangle(x, y + 4, x - 4, y - 2, x + 4, y - 2, color);
      break;
    case 2:
      canvas.drawLine(x, y - 5, x, y + 5, color);
      canvas.drawLine(x - 5, y, x + 5, y, color);
      canvas.drawLine(x - 3, y - 3, x + 3, y + 3, color);
      canvas.drawLine(x - 3, y + 3, x + 3, y - 3, color);
      break;
    default:
      // The heart is intentionally small so the flowing motion stays light.
      canvas.fillCircle(x - 2, y - 1, 2, color);
      canvas.fillCircle(x + 2, y - 1, 2, color);
      canvas.fillTriangle(x - 5, y, x + 5, y, x, y + 6, color);
      break;
  }
}

void drawFireworks() {
  const int16_t cx = 67;
  const int16_t cy = 103;
  for (uint8_t i = 0; i < 8; ++i) {
    const float angle = static_cast<float>(i) * 45.0f * DEG_TO_RAD;
    const int16_t x = cx + static_cast<int16_t>(cosf(angle) * 29.0f);
    const int16_t y = cy + static_cast<int16_t>(sinf(angle) * 29.0f);
    canvas.drawLine(cx, cy, x, y, shapeColor(shapeSeed(dropCounter + i)));
    canvas.fillCircle(x, y, 2, shapeColor(shapeSeed(dropCounter + i + 17)));
  }
}

int16_t chamberHalf(int16_t y, bool topChamber) {
  constexpr int16_t top = 38;
  constexpr int16_t neck = 103;
  constexpr int16_t bottom = 168;
  constexpr int16_t neckHalf = 5;
  constexpr int16_t outerHalf = 42;
  const int16_t start = topChamber ? top : neck;
  const int16_t end = topChamber ? neck : bottom;
  const float progress = constrain(static_cast<float>(y - start) / (end - start), 0.0f, 1.0f);
  return topChamber ? static_cast<int16_t>(outerHalf - (outerHalf - neckHalf) * progress)
                    : static_cast<int16_t>(neckHalf + (outerHalf - neckHalf) * progress);
}

void drawSandChamber(bool topChamber, float fill, float sway, uint32_t seed) {
  constexpr int16_t cx = 67;
  constexpr int16_t neck = 103;
  constexpr int16_t top = 38;
  constexpr int16_t bottom = 168;
  fill = constrain(fill, 0.0f, 1.0f);
  if (fill <= 0.01f) {
    return;
  }
  const int16_t chamberHeight = topChamber ? neck - top : bottom - neck;
  const int16_t filledHeight = max<int16_t>(2, static_cast<int16_t>(chamberHeight * fill));
  const int16_t firstY = topChamber ? neck - filledHeight : neck + 4;
  const int16_t lastY = topChamber ? neck - 4 : neck + filledHeight;
  for (int16_t y = firstY; y <= lastY; y += 3) {
    const float chamberProgress = topChamber
                                      ? static_cast<float>(neck - y) / chamberHeight
                                      : static_cast<float>(y - neck) / chamberHeight;
    const int16_t half = chamberHalf(y, topChamber);
    const float wave = sinf(static_cast<float>(y) * 0.19f + animationFrame * 0.24f + seed) *
                       (1.0f + sway * 3.5f);
    const float counterWave = sinf(static_cast<float>(y) * 0.07f - animationFrame * 0.16f +
                                   seed * 0.37f) * (0.7f + sway * 2.0f);
    const int16_t requestedOffset = static_cast<int16_t>(sway * 8.0f + wave + counterWave);
    const int16_t offset = constrain(requestedOffset, -max<int16_t>(0, half - 4),
                                     max<int16_t>(0, half - 4));
    const int16_t safeHalf = max<int16_t>(3, half - abs(offset));
    const int16_t left = cx - safeHalf + offset;
    const int16_t width = max<int16_t>(2, safeHalf * 2 - 2);
    const float gradient = topChamber ? 1.0f - chamberProgress : chamberProgress;
    const uint16_t color = gradient < 0.5f
                               ? blend565(kOceanDeep, kOceanMid, gradient * 2.0f)
                               : blend565(kOceanMid, kOceanLight, (gradient - 0.5f) * 2.0f);
    canvas.fillRect(left, y, width, 3, color);
    if ((y + seed) % 9 == 0) {
      canvas.drawFastHLine(left + 3, y, max<int16_t>(2, width - 8), kOceanLight);
    }
  }

  // A separate bright crest makes the fill read as moving water instead of
  // a stack of flat rectangles.
  const int16_t surfaceY = topChamber ? firstY : lastY;
  const int16_t surfaceHalf = chamberHalf(surfaceY, topChamber);
  const int16_t amplitude = 1 + static_cast<int16_t>(sway * 3.0f);
  const int16_t surfaceOffset = static_cast<int16_t>(sway * 5.0f);
  const int16_t safeSurfaceHalf = max<int16_t>(3, surfaceHalf - abs(surfaceOffset));
  for (int16_t x = -safeSurfaceHalf + 2; x <= safeSurfaceHalf - 2; x += 2) {
    const float phase = static_cast<float>(x) * 0.22f + animationFrame * 0.55f + seed;
    const int16_t crest = constrain(
        surfaceY + static_cast<int16_t>(sinf(phase) * amplitude),
        topChamber ? top : neck + 1, topChamber ? neck - 1 : bottom);
    const int16_t crestX = 67 + x + surfaceOffset;
    canvas.drawPixel(crestX, crest, kOceanLight);
    if ((x + seed) % 6 == 0) {
      canvas.drawPixel(crestX + 1, crest + (topChamber ? 1 : -1), kOceanMid);
    }
  }
}

float hourglassProgress() {
  if (hourglassTimer.totalMs == 0) {
    return 0.0f;
  }
  return hourglassMode == HourglassMode::Countdown
             ? min(1.0f, static_cast<float>(hourglassTimer.remainingMs) /
                              static_cast<float>(hourglassTimer.totalMs))
             : min(1.0f, static_cast<float>(hourglassTimer.reverseMs) /
                              static_cast<float>(hourglassTimer.totalMs));
}

void drawHourglass() {
  constexpr int16_t cx = 67;
  constexpr int16_t top = 31;
  constexpr int16_t neck = 103;
  constexpr int16_t bottom = 175;
  constexpr int16_t half = 49;

  for (int8_t thickness = -2; thickness <= 2; ++thickness) {
    canvas.drawFastHLine(cx - half, top + thickness, half * 2, kFrame);
    canvas.drawFastHLine(cx - half, bottom + thickness, half * 2, kFrame);
  }
  canvas.drawLine(cx - half + 2, top + 4, cx - 5, neck - 4, kGlass);
  canvas.drawLine(cx + half - 2, top + 4, cx + 5, neck - 4, kGlass);
  canvas.drawLine(cx - 5, neck + 4, cx - half + 2, bottom - 4, kGlass);
  canvas.drawLine(cx + 5, neck + 4, cx + half - 2, bottom - 4, kGlass);

  const float progress = hourglassProgress();
  const float topFill = progress;
  const float bottomFill = 1.0f - progress;
  drawSandChamber(true, topFill, swayEnergy, 3);
  drawSandChamber(false, bottomFill, swayEnergy, 17);

  if (hourglassTimer.running && hourglassTimer.totalMs > 0) {
    const bool flowingUp = hourglassMode == HourglassMode::Rewind;
    const int16_t streamStart = flowingUp ? neck + 16 : neck - 16;
    const int16_t streamEnd = flowingUp ? neck + 4 : neck - 4;
    const int16_t streamX = cx + static_cast<int16_t>(swayOffsetPx * 0.45f) +
                            static_cast<int16_t>(sinf(animationFrame * 0.7f) * swayEnergy * 5.0f);
    canvas.drawLine(streamX, streamStart, streamX, streamEnd, kOceanLight);
    const uint32_t seed = shapeSeed(dropCounter * 53UL + animationFrame);
    const int16_t shapeY = flowingUp ? neck - 21 : neck + 21;
    drawShape(streamX + static_cast<int16_t>(seed % 5) - 2, shapeY,
              static_cast<uint8_t>(seed >> 16), kOceanLight);
  }
  if (bottomFill > 0.01f) {
    const uint8_t shapeCount = min<uint8_t>(8, 1 + static_cast<uint8_t>(bottomFill * 8.0f));
    for (uint8_t i = 0; i < shapeCount; ++i) {
      const uint32_t seed = shapeSeed(dropCounter * 37UL + i * 101UL + 11UL);
      const int16_t x = cx - 34 + static_cast<int16_t>(seed % 69);
      const int16_t y = bottom - 10 - static_cast<int16_t>((seed >> 8) % 22);
      drawShape(x + static_cast<int16_t>(swayOffsetPx * 0.25f), y,
                static_cast<uint8_t>(seed >> 16), shapeColor(seed));
    }
  }
  if (fireworkUntil != 0 && static_cast<int32_t>(fireworkUntil - millis()) > 0) {
    drawFireworks();
  }
}

void drawHourglassPage() {
  drawHeader(kCyan);
  drawHourglass();
  char duration[16];
  const uint32_t shownMs = hourglassMode == HourglassMode::Countdown
                               ? hourglassTimer.remainingMs
                               : hourglassTimer.reverseMs;
  formatDuration(shownMs, duration, sizeof(duration));
  drawCentered(hourglassMode == HourglassMode::Rewind ? String("+") + duration : duration, 187, 2);
  const char* status = !imuAvailable ? "IMU NOT FOUND"
                       : hourglassAnchorFace == Face::Unknown ? "HOLD INITIAL SIDE"
                       : hourglassTimer.completed ? (hourglassMode == HourglassMode::Rewind
                                                         ? "REWIND DONE" : "COMPLETE")
                       : hourglassTimer.running ? (hourglassMode == HourglassMode::Rewind
                                                       ? "TIME REWIND" : "TIME FLOW")
                       : (hourglassMode == HourglassMode::Rewind ? "PRESS A TO REWIND"
                                                                  : "PRESS A TO START");
  drawCentered(status, 202, 1, !imuAvailable ? kCoral
                                               : hourglassMode == HourglassMode::Rewind ? kOceanLight
                                                                                         : kCyan);
  drawFooter("A START/PAUSE", "B NEXT", kCyan);
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
  const int16_t radius = 14 + static_cast<int16_t>((animationFrame * 5) % 28);
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
  drawFooter("A PING", "B NEXT", kCoral);
}

void updateBattery() {
  const uint32_t now = millis();
  if (lastBatteryReadMs != 0 && now - lastBatteryReadMs < kBatteryRefreshMs) {
    return;
  }
  lastBatteryReadMs = now;
  const int nextLevel = M5.Power.getBatteryLevel();
  const int nextMillivolts = M5.Power.getBatteryVoltage();
  const bool nextCharging =
      M5.Power.isCharging() == m5::Power_Class::is_charging_t::is_charging;
  if (nextLevel != batteryLevel || nextMillivolts != batteryMillivolts ||
      nextCharging != charging) {
    batteryLevel = nextLevel;
    batteryMillivolts = nextMillivolts;
    charging = nextCharging;
    markUiDirty();
  }
}

void resetTimer(TimerState& timer, uint32_t durationMs) {
  timer.running = false;
  timer.completed = false;
  timer.remainingMs = durationMs;
  timer.reverseMs = 0;
  timer.totalMs = durationMs;
  timer.lastTickMs = millis();
  markUiDirty();
}

void startHourglass(Face face, uint32_t now) {
  const uint32_t durationMs = faceDurationMs(face);
  if (durationMs == 0) {
    return;
  }
  hourglassFace = face;
  if (hourglassAnchorFace == Face::Unknown) {
    hourglassAnchorFace = face;
  }
  hourglassMode = face == oppositeFace(hourglassAnchorFace) ? HourglassMode::Rewind
                                                            : HourglassMode::Countdown;
  hourglassTimer.totalMs = durationMs;
  hourglassTimer.remainingMs = durationMs;
  hourglassTimer.reverseMs = 0;
  hourglassTimer.running = true;
  hourglassTimer.completed = false;
  hourglassTimer.lastTickMs = now;
  dropCounter = 0;
  fireworkUntil = 0;
  Serial.printf("Hourglass %s: %lu seconds\n", faceTitle(face),
                static_cast<unsigned long>(durationMs / 1000));
  markUiDirty();
}

void updateTimer() {
  const uint32_t now = millis();
  if (fireworkUntil != 0 && static_cast<int32_t>(now - fireworkUntil) >= 0) {
    fireworkUntil = 0;
    markUiDirty();
  }
  if (!hourglassTimer.running) {
    return;
  }
  const uint32_t elapsed = now - hourglassTimer.lastTickMs;
  hourglassTimer.lastTickMs = now;
  bool reachedEnd = false;
  if (hourglassMode == HourglassMode::Countdown) {
    if (elapsed >= hourglassTimer.remainingMs) {
      hourglassTimer.remainingMs = 0;
      reachedEnd = true;
    } else {
      hourglassTimer.remainingMs -= elapsed;
    }
  } else {
    const uint32_t available = hourglassTimer.totalMs - hourglassTimer.reverseMs;
    if (elapsed >= available) {
      hourglassTimer.reverseMs = hourglassTimer.totalMs;
      reachedEnd = true;
    } else {
      hourglassTimer.reverseMs += elapsed;
    }
  }
  if (reachedEnd) {
    hourglassTimer.running = false;
    hourglassTimer.completed = true;
    alertPending = true;
    fireworkUntil = now + 2500;
    markUiDirty();
    return;
  }
  const uint32_t shownMs = hourglassMode == HourglassMode::Countdown
                               ? hourglassTimer.remainingMs : hourglassTimer.reverseMs;
  if ((shownMs / 1000) != ((shownMs - min<uint32_t>(elapsed, shownMs)) / 1000)) {
    ++dropCounter;
    if (dropCounter % 12 == 0) {
      fireworkUntil = now + 1800;
    }
    markUiDirty();
  }
}

void toggleHourglassDirection(uint32_t now) {
  if (hourglassTimer.totalMs == 0) {
    return;
  }
  const uint32_t currentProgress = hourglassMode == HourglassMode::Countdown
                                       ? hourglassTimer.totalMs - hourglassTimer.remainingMs
                                       : hourglassTimer.reverseMs;
  const bool rewind = hourglassMode == HourglassMode::Countdown;
  hourglassMode = rewind ? HourglassMode::Rewind : HourglassMode::Countdown;
  if (rewind) {
    hourglassTimer.reverseMs = currentProgress >= hourglassTimer.totalMs ? 0 : currentProgress;
  } else {
    hourglassTimer.remainingMs = hourglassTimer.totalMs > currentProgress
                                     ? hourglassTimer.totalMs - currentProgress : 0;
  }
  hourglassTimer.completed = false;
  hourglassTimer.lastTickMs = now;
  turnAngleDeg = 0.0f;
  Serial.printf("Hourglass mode=%s progress=%lu ms\n",
                rewind ? "TIME REWIND" : "TIME FLOW",
                static_cast<unsigned long>(currentProgress));
  markUiDirty();
}

void detectInPlaneFlip(float z, uint32_t now) {
  float gx = 0.0f;
  float gy = 0.0f;
  float gz = 0.0f;
  if (!M5.Imu.getGyro(&gx, &gy, &gz)) {
    return;
  }
  if (lastGyroMs == 0) {
    lastGyroMs = now;
    return;
  }
  const float elapsed = min(0.5f, static_cast<float>(now - lastGyroMs) / 1000.0f);
  lastGyroMs = now;
  // When the screen remains face-up/down, Z gyro measures a 180-degree
  // in-plane turn. This supports turning the visible UI upside down without
  // relying on an accelerometer face change.
  if (fabsf(z) < 0.72f) {
    turnAngleDeg = 0.0f;
    turnFlipLatched = false;
    return;
  }
  const float turnRate = fabsf(gz);
  if (turnRate > 45.0f) {
    turnAngleDeg += turnRate * elapsed;
    if (!turnFlipLatched && turnAngleDeg >= 135.0f && hourglassTimer.totalMs > 0) {
      turnFlipLatched = true;
      toggleHourglassDirection(now);
    }
  } else if (turnRate < 15.0f) {
    turnAngleDeg = 0.0f;
    turnFlipLatched = false;
  }
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

Face oppositeFace(Face face) {
  switch (face) {
    case Face::ScreenUp: return Face::ScreenDown;
    case Face::ScreenDown: return Face::ScreenUp;
    case Face::Right: return Face::Left;
    case Face::Left: return Face::Right;
    case Face::Top: return Face::Bottom;
    case Face::Bottom: return Face::Top;
    case Face::Unknown: return Face::Unknown;
  }
  return Face::Unknown;
}

void requestNudge() {
  const uint32_t now = millis();
  if (outgoingNudgePending || static_cast<int32_t>(now - nudgeCooldownUntil) < 0) {
    return;
  }
  outgoingNudgePending = true;
  outgoingNudgeType = 1;
  nudgeCooldownUntil = now + 1500;
  M5.Speaker.begin();
  M5.Speaker.setVolume(210);
  M5.Speaker.tone(520, 80, 0, true);
  delay(100);
  M5.Speaker.end();
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
  const uint32_t sampleInterval = page == Page::Hourglass
                                      ? (hourglassTimer.running
                                             ? LIFETIME_HOURGLASS_ACTIVE_IMU_SAMPLE_MS
                                             : LIFETIME_HOURGLASS_IDLE_IMU_SAMPLE_MS)
                                      : LIFETIME_IMU_SAMPLE_MS;
  if (!foreground || page == Page::Battery || !imuAvailable ||
      now - lastImuReadMs < sampleInterval) {
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
  if (page == Page::Hourglass) {
    const float deltaX = haveAccelSample ? x - lastAccelX : 0.0f;
    swayKickPx = constrain(swayKickPx + deltaX * 22.0f, -14.0f, 14.0f) * 0.82f;
    swayOffsetPx = constrain(x * 8.0f + swayKickPx, -16.0f, 16.0f);
    swayEnergy = constrain(swayEnergy * 0.78f + fabsf(deltaX) * 2.4f, 0.0f, 1.0f);
    if (fabsf(swayOffsetPx) > 0.4f || swayEnergy > 0.03f) {
      markUiDirty();
    }
  }
  lastAccelX = x;
  haveAccelSample = true;
  if (page == Page::Hourglass) {
    detectInPlaneFlip(z, now);
  }
  detectNudgeGesture(x, y, z);
  if (page != Page::Hourglass) {
    return;
  }

  const Face detected = classifyFace(x, y, z);
  if (detected != candidateFace) {
    candidateFace = detected;
    candidateSinceMs = now;
    return;
  }
  if (detected == Face::Unknown || now - candidateSinceMs < LIFETIME_ORIENTATION_STABLE_MS) {
    return;
  }
  if (stableFace != detected) {
    const bool wasRunning = hourglassTimer.running;
    const uint32_t totalMs = hourglassTimer.totalMs;
    const uint32_t currentProgress = totalMs == 0
                                        ? 0
                                        : hourglassMode == HourglassMode::Countdown
                                              ? totalMs - hourglassTimer.remainingMs
                                              : hourglassTimer.reverseMs;
    stableFace = detected;
    hourglassFace = detected;
    if (hourglassAnchorFace == Face::Unknown) {
      hourglassAnchorFace = detected;
    }
    hourglassMode = detected == oppositeFace(hourglassAnchorFace)
                        ? HourglassMode::Rewind : HourglassMode::Countdown;
    if (totalMs == 0) {
      resetTimer(hourglassTimer, 0);
    } else if (hourglassMode == HourglassMode::Rewind) {
      hourglassTimer.reverseMs = currentProgress;
      hourglassTimer.completed = hourglassTimer.reverseMs >= totalMs;
    } else {
      hourglassTimer.remainingMs = totalMs > currentProgress ? totalMs - currentProgress : 0;
      hourglassTimer.completed = hourglassTimer.remainingMs == 0;
    }
    hourglassTimer.running = wasRunning && !hourglassTimer.completed;
    hourglassTimer.lastTickMs = now;
    Serial.printf("Hourglass face=%s mode=%s\n", faceTitle(detected),
                  hourglassMode == HourglassMode::Rewind ? "TIME REWIND" : "TIME FLOW");
    dropCounter = 0;
    fireworkUntil = 0;
    markUiDirty();
  }
}

bool animatedNow(uint32_t now) {
  return (page == Page::Together &&
          (outgoingNudgePending || now - lastNudgeSentAt < 1200 ||
           now - lastNudgeReceivedAt < 1800)) ||
         (page == Page::Hourglass &&
          (hourglassTimer.running || swayEnergy > 0.03f)) ||
         (page == Page::Battery && meritUntil != 0);
}

}  // namespace

void begin() {
  preferences.begin("lifetime", false);
  countValue = preferences.getULong("count", 0);
  nudgeSentCount = preferences.getULong("nudges_tx", 0);
  nudgeReceivedCount = preferences.getULong("nudges_rx", 0);
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
  if (!foreground) {
    return;
  }
  updateBattery();
  updateOrientation(true);
  updateTimer();
  if (meritUntil != 0 && static_cast<int32_t>(millis() - meritUntil) >= 0) {
    meritUntil = 0;
    markUiDirty();
  }
}

void draw(bool force) {
  if (!canvasReady) {
    return;
  }
  const uint32_t now = millis();
  const bool animated = animatedNow(now);
  if (!force && !uiDirty && !animated) {
    return;
  }
  const uint32_t frameInterval = page == Page::Hourglass ? kHourglassFrameMs :
                                 page == Page::Battery ? 140 : kPulseRefreshMs;
  if (!force && animated && now - lastUiDrawMs < frameInterval) {
    return;
  }
  ++animationFrame;
  switch (page) {
    case Page::Battery: drawBatteryPage(); break;
    case Page::Hourglass: drawHourglassPage(); break;
    case Page::Together: drawTogetherPage(); break;
  }
  M5.Display.startWrite();
  canvas.pushSprite(0, 0);
  M5.Display.endWrite();
  lastUiDrawMs = now;
  uiDirty = false;
}

void resetRuntime() {
  resetTimer(hourglassTimer, 0);
  hourglassMode = HourglassMode::Countdown;
  stableFace = Face::Unknown;
  hourglassFace = Face::Unknown;
  hourglassAnchorFace = Face::Unknown;
  candidateFace = Face::Unknown;
  candidateSinceMs = 0;
  outgoingNudgePending = false;
  motionPeakCount = 0;
  dropCounter = 0;
  fireworkUntil = 0;
  alertPending = false;
  swayOffsetPx = 0.0f;
  swayKickPx = 0.0f;
  swayEnergy = 0.0f;
  haveAccelSample = false;
  turnAngleDeg = 0.0f;
  lastGyroMs = 0;
  turnFlipLatched = false;
  meritUntil = 0;
}

void setPowerContext(bool wifi, bool monitorOnly) {
  if (wifiPowerContext == wifi && monitorPowerContext == monitorOnly) {
    return;
  }
  wifiPowerContext = wifi;
  monitorPowerContext = monitorOnly;
  markUiDirty();
}

void handleAShortPress() {
  if (page == Page::Battery) {
    ++countValue;
    preferences.putULong("count", countValue);
    meritUntil = millis() + kMeritEffectMs;
    M5.Speaker.begin();
    M5.Speaker.setVolume(235);
    M5.Speaker.playRaw(assets_audio_woodblock_soft_cc0_raw,
                       assets_audio_woodblock_soft_cc0_raw_len, 16000, false, 1, 0, false);
    while (M5.Speaker.isPlaying()) {
      delay(2);
    }
    M5.Speaker.end();
  } else if (page == Page::Hourglass) {
    const Face face = hourglassFace == Face::Unknown ? stableFace : hourglassFace;
    const uint32_t durationMs = faceDurationMs(face);
    if (durationMs == 0) {
      markUiDirty();
      draw(true);
      return;
    }
    if (hourglassTimer.running) {
      hourglassTimer.running = false;
    } else {
      if ((hourglassMode == HourglassMode::Countdown && hourglassTimer.remainingMs == 0) ||
          (hourglassMode == HourglassMode::Rewind && hourglassTimer.reverseMs >= durationMs) ||
          hourglassTimer.completed) {
        hourglassTimer.totalMs = durationMs;
        hourglassTimer.remainingMs = durationMs;
        hourglassTimer.reverseMs = 0;
        hourglassTimer.completed = false;
        dropCounter = 0;
        fireworkUntil = 0;
      }
      hourglassTimer.running = true;
      hourglassTimer.lastTickMs = millis();
    }
    M5.Speaker.begin();
    M5.Speaker.setVolume(190);
    M5.Speaker.tone(hourglassTimer.running ? 660 : 420, 75, 0, true);
    delay(95);
    M5.Speaker.end();
  } else {
    requestNudge();
  }
  motionPeakCount = 0;
  markUiDirty();
  draw(true);
}

void handleALongPress() {
  // LifeTime uses a short A press for the current action. Long A is reserved
  // by the global input state machine for EchoLink PTT.
}

void handleBShortPress() {
  page = static_cast<Page>((static_cast<uint8_t>(page) + 1) % 3);
  if (page == Page::Hourglass) {
    hourglassMode = HourglassMode::Countdown;
    stableFace = Face::Unknown;
    hourglassFace = Face::Unknown;
    hourglassAnchorFace = Face::Unknown;
    candidateFace = Face::Unknown;
    candidateSinceMs = 0;
    swayOffsetPx = 0.0f;
    swayKickPx = 0.0f;
    swayEnergy = 0.0f;
    haveAccelSample = false;
    turnAngleDeg = 0.0f;
    lastGyroMs = 0;
    turnFlipLatched = false;
  }
  markUiDirty();
  draw(true);
}

void handleBLongPress() {
  handleBShortPress();
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
