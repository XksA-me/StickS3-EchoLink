#!/usr/bin/env sh
set -eu

mkdir -p .host-build
c++ -std=c++17 -Wall -Wextra -Werror -Iinclude host/voice_protocol_sim.cpp \
  -o .host-build/voice_protocol_sim
.host-build/voice_protocol_sim
python3 host/recorder_sim.py
