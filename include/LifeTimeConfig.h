#pragma once

#include <Arduino.h>

static constexpr char LIFETIME_NTP_SERVER_1[] = "pool.ntp.org";
static constexpr char LIFETIME_NTP_SERVER_2[] = "time.nist.gov";
static constexpr char LIFETIME_TIMEZONE[] = "CST-8";

static constexpr uint32_t LIFETIME_ORIENTATION_STABLE_MS = 1000;
static constexpr uint32_t LIFETIME_IMU_SAMPLE_MS = 50;

// Screen up, screen down, right, left, top, bottom.
static constexpr uint32_t LIFETIME_FACE_DURATION_SECONDS[6] = {
    60, 120, 300, 600, 900, 1800};
