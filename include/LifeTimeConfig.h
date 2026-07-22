#pragma once

#include <Arduino.h>

static constexpr uint32_t LIFETIME_ORIENTATION_STABLE_MS = 1000;
static constexpr uint32_t LIFETIME_IMU_SAMPLE_MS = 100;

// Approximate full-charge runtime used by the battery page. These are UI
// estimates, not fuel-gauge measurements; tune them after measuring hardware.
static constexpr uint32_t LIFETIME_LOCAL_RUNTIME_MINUTES = 240;
static constexpr uint32_t LIFETIME_WIFI_RUNTIME_MINUTES = 180;
static constexpr uint32_t LIFETIME_MONITOR_RUNTIME_MINUTES = 360;

// Screen up, screen down, right, left, top, bottom.
static constexpr uint32_t LIFETIME_FACE_DURATION_SECONDS[6] = {
    60, 120, 300, 600, 900, 1800};
