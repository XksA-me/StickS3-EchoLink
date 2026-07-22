#pragma once

// Both devices must use the same group ID. Change this before using the
// ESP-NOW mode outside a private test area.
#define ECHOLINK_GROUP_ID 0x4D3A91C7UL

// ESP-NOW uses the current Wi-Fi channel. Keep both devices on this channel.
#define ECHOLINK_ESPNOW_CHANNEL 1

#define ECHOLINK_SAMPLE_RATE 16000
#define ECHOLINK_MAX_RECORD_SECONDS 8
#define ECHOLINK_AUDIO_CHUNK_BYTES 200

// Display power profile. Brightness uses M5GFX's 0-255 scale.
#define ECHOLINK_DISPLAY_BRIGHTNESS 50
#define ECHOLINK_DISPLAY_DIM_BRIGHTNESS 8
#define ECHOLINK_DISPLAY_DIM_SECONDS 15
#define ECHOLINK_DISPLAY_SLEEP_SECONDS 30
