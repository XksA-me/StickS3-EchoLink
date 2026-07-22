#pragma once

#include <stdint.h>

namespace lifetime {

void begin();
void onEnter();
void update(bool foreground);
void draw(bool force = false);

void handleAShortPress();
void handleALongPress();
void handleBShortPress();
void handleBLongPress();

bool takeAlert();
bool peekOutgoingNudge(uint8_t* type);
void markOutgoingNudgeSent();
void receiveNudge(uint8_t type, uint32_t senderId);

}  // namespace lifetime
