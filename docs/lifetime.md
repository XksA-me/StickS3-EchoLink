# LifeTime

LifeTime is the local utility app inside the single EchoLink firmware. Press `B` to move between `BATTERY`, `HOURGLASS`, and `TOGETHER`; press `A` to perform the current page action.

## Wooden fish

On `BATTERY`, each short `A` press increments the persistent merit counter, plays a short woodblock recording, and shows a floating `功德 +1` effect. The effect is intentionally temporary, so the battery page remains quiet while idle.

The embedded sample is a mono 16 kHz signed 8-bit conversion of [Woodblock-soft.wav by hollandm](https://freesound.org/people/hollandm/sounds/692828/). The source page describes one soft wooden-stick hit and marks it **Creative Commons 0 (CC0 1.0)**. The converted raw sample is stored at `assets/audio/woodblock-soft-cc0.raw`; the generated C header is `include/generated/woodblock_soft_cc0.h`.

## Hourglass

The on-screen hourglass has one fixed upright direction. The IMU uses the first stable physical side as the starting side, then treats its opposite side as the inverted position:

| Physical position | Mode | Meaning |
| --- | --- | --- |
| Initial side | `TIME FLOW` | Countdown from the configured duration |
| Opposite side | `TIME REWIND` | Count up from zero while the sand returns upward |

Hold the device on a stable side for one second to select the starting side. Press `A` to start or pause. Turning the device over to the opposite side while running preserves the current fraction and changes the direction of time flow. The display itself stays upright and is never rotated.

While the timer is running, the screen commits at most one frame every 450 ms. The IMU sample period remains 100 ms so short left/right movement can be smoothed into a low-cost sand sway. When the device is still and the timer is paused, the page does not redraw automatically.

The sand uses a three-stop ocean palette (`deep`, `mid`, `light`) and small dots, diamonds, stars, and hearts. The heart shape is deliberately smaller than the other particles to keep the sand readable on the 135 x 240 display.

The default duration is `LIFETIME_FACE_DURATION_SECONDS[0]` in [LifeTimeConfig.h](../include/LifeTimeConfig.h). The remaining five legacy duration entries are retained for configuration compatibility but are no longer mapped to screen rotation.

## Power notes

LifeTime only samples the IMU while it is the foreground app. The battery page reads the fuel-gauge values once per minute. The Together animation and the merit effect are event-driven and expire automatically. A single power-key press can still put the whole product into the display-off monitor mode described in [power-management.md](power-management.md).
