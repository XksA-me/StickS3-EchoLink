#pragma once

#include <Arduino.h>

static constexpr uint32_t LIFETIME_ORIENTATION_STABLE_MS = 1000;
// Running sand needs a faster sample; idle pages use the slower interval to
// reduce IMU wakeups and CPU work.
static constexpr uint32_t LIFETIME_IMU_SAMPLE_MS = 180;
static constexpr uint32_t LIFETIME_HOURGLASS_ACTIVE_IMU_SAMPLE_MS = 100;
static constexpr uint32_t LIFETIME_HOURGLASS_IDLE_IMU_SAMPLE_MS = 320;

// Approximate full-charge runtime used by the battery page. These are UI
// estimates, not fuel-gauge measurements; tune them after measuring hardware.
static constexpr uint32_t LIFETIME_LOCAL_RUNTIME_MINUTES = 240;
static constexpr uint32_t LIFETIME_WIFI_RUNTIME_MINUTES = 180;
static constexpr uint32_t LIFETIME_MONITOR_RUNTIME_MINUTES = 360;

// Screen up, screen down, right, left, top, bottom.
static constexpr uint32_t LIFETIME_FACE_DURATION_SECONDS[6] = {
    120, 120, 300, 600, 900, 1800};
