#include <algorithm>
#include <array>
#include <cstring>
#include <iostream>
#include <vector>

#include "Config.h"
#include "VoiceProtocol.h"

namespace {

constexpr size_t kAudioBytes = 600;
constexpr size_t kMaxFrameBytes = sizeof(echolink::PacketHeader) + ECHOLINK_AUDIO_CHUNK_BYTES;

struct SimReceiver {
  explicit SimReceiver(uint32_t senderId) : senderId(senderId) {}

  bool accept(const std::vector<uint8_t>& frame) {
    if (!echolink::isValidPacket(frame.data(), frame.size(), ECHOLINK_GROUP_ID, senderId)) {
      return false;
    }
    const auto header = echolink::readHeader(frame.data());
    const auto type = static_cast<echolink::PacketType>(header.type);

    if (type == echolink::PacketType::VoiceStart) {
      expectedBytes = header.totalBytes;
      sourceId = header.senderId;
      sessionId = header.sessionId;
      audio.assign(expectedBytes, 0);
      seen.assign((expectedBytes + ECHOLINK_AUDIO_CHUNK_BYTES - 1) / ECHOLINK_AUDIO_CHUNK_BYTES,
                  false);
      receivedBytes = 0;
      complete = false;
      return true;
    }

    if (header.senderId != sourceId || header.sessionId != sessionId || expectedBytes == 0) {
      return false;
    }
    if (type == echolink::PacketType::Audio) {
      const size_t offset = header.sequence * ECHOLINK_AUDIO_CHUNK_BYTES;
      if (header.sequence >= seen.size() || offset >= expectedBytes ||
          header.payloadBytes > ECHOLINK_AUDIO_CHUNK_BYTES) {
        return false;
      }
      const size_t bytes = std::min(static_cast<size_t>(header.payloadBytes), expectedBytes - offset);
      if (!seen[header.sequence]) {
        memcpy(audio.data() + offset, frame.data() + sizeof(echolink::PacketHeader), bytes);
        seen[header.sequence] = true;
        receivedBytes += bytes;
      }
      return true;
    }
    if (type == echolink::PacketType::VoiceEnd) {
      complete = true;
      return true;
    }
    return false;
  }

  size_t missingBytes() const { return expectedBytes - receivedBytes; }

  uint32_t senderId;
  uint32_t sourceId = 0;
  uint16_t sessionId = 0;
  size_t expectedBytes = 0;
  size_t receivedBytes = 0;
  bool complete = false;
  std::vector<int8_t> audio;
  std::vector<bool> seen;
};

std::vector<uint8_t> makePacket(echolink::PacketType type, uint32_t group, uint32_t sender,
                                uint16_t session, uint16_t sequence, uint32_t total,
                                const int8_t* payload = nullptr, uint16_t payloadBytes = 0) {
  std::array<uint8_t, kMaxFrameBytes> raw{};
  const size_t bytes = echolink::encodePacket(raw.data(), raw.size(), type, group, sender, session,
                                               sequence, total, payload, payloadBytes);
  return {raw.begin(), raw.begin() + bytes};
}

bool check(bool condition, const char* label) {
  std::cout << (condition ? "PASS" : "FAIL") << ": " << label << '\n';
  return condition;
}

}  // namespace

int main() {
  constexpr uint32_t senderA = 0xA0010001;
  constexpr uint32_t senderB = 0xB0020002;
  constexpr uint16_t session = 42;
  std::array<int8_t, kAudioBytes> voice{};
  for (size_t i = 0; i < voice.size(); ++i) {
    voice[i] = static_cast<int8_t>((i * 17) & 0x7F);
  }

  SimReceiver receiver(senderB);
  bool ok = true;
  ok &= check(receiver.accept(makePacket(echolink::PacketType::VoiceStart, ECHOLINK_GROUP_ID,
                                         senderA, session, 0, voice.size())),
              "receiver accepts voice start");

  // Deliberately deliver chunks out of order. ESP-NOW delivery cannot be assumed ordered.
  for (const uint16_t sequence : {2, 0, 1}) {
    const size_t offset = sequence * ECHOLINK_AUDIO_CHUNK_BYTES;
    ok &= check(receiver.accept(makePacket(echolink::PacketType::Audio, ECHOLINK_GROUP_ID, senderA,
                                           session, sequence, voice.size(), voice.data() + offset,
                                           ECHOLINK_AUDIO_CHUNK_BYTES)),
                "receiver accepts audio chunk");
  }
  ok &= check(receiver.accept(makePacket(echolink::PacketType::VoiceEnd, ECHOLINK_GROUP_ID,
                                         senderA, session, 0, voice.size())),
              "receiver accepts voice end");
  ok &= check(receiver.complete && receiver.missingBytes() == 0 &&
                  std::equal(receiver.audio.begin(), receiver.audio.end(), voice.begin()),
              "out-of-order voice is reconstructed exactly");

  SimReceiver isolated(senderB);
  ok &= check(!isolated.accept(makePacket(echolink::PacketType::VoiceStart, 0xDEADBEEFUL,
                                          senderA, session, 0, voice.size())),
              "different group is rejected");
  ok &= check(!isolated.accept(makePacket(echolink::PacketType::VoiceStart, ECHOLINK_GROUP_ID,
                                          senderB, session, 0, voice.size())),
              "device ignores its own broadcast");

  SimReceiver lossy(senderB);
  lossy.accept(makePacket(echolink::PacketType::VoiceStart, ECHOLINK_GROUP_ID, senderA, session,
                          0, voice.size()));
  lossy.accept(makePacket(echolink::PacketType::Audio, ECHOLINK_GROUP_ID, senderA, session, 0,
                          voice.size(), voice.data(), ECHOLINK_AUDIO_CHUNK_BYTES));
  lossy.accept(makePacket(echolink::PacketType::Audio, ECHOLINK_GROUP_ID, senderA, session, 2,
                          voice.size(), voice.data() + 2 * ECHOLINK_AUDIO_CHUNK_BYTES,
                          ECHOLINK_AUDIO_CHUNK_BYTES));
  lossy.accept(makePacket(echolink::PacketType::VoiceEnd, ECHOLINK_GROUP_ID, senderA, session,
                          0, voice.size()));
  ok &= check(lossy.complete && lossy.missingBytes() == ECHOLINK_AUDIO_CHUNK_BYTES,
              "lost audio chunk is detected");

  constexpr uint32_t longVoiceBytes = ECHOLINK_SAMPLE_RATE * ECHOLINK_MAX_RECORD_SECONDS;
  const auto longVoice = makePacket(echolink::PacketType::VoiceStart, ECHOLINK_GROUP_ID, senderA,
                                    session, 0, longVoiceBytes);
  ok &= check(echolink::readHeader(longVoice.data()).totalBytes == longVoiceBytes,
              "eight-second voice length is preserved without overflow");

  const int8_t nudgeType = 1;
  const auto nudge = makePacket(echolink::PacketType::Nudge, ECHOLINK_GROUP_ID, senderA,
                                session, 0, 0, &nudgeType, 1);
  const auto nudgeHeader = echolink::readHeader(nudge.data());
  ok &= check(echolink::isValidPacket(nudge.data(), nudge.size(), ECHOLINK_GROUP_ID, senderB) &&
                  static_cast<echolink::PacketType>(nudgeHeader.type) ==
                      echolink::PacketType::Nudge &&
                  nudgeHeader.payloadBytes == 1 &&
                  nudge[sizeof(echolink::PacketHeader)] == static_cast<uint8_t>(nudgeType),
              "two-device nudge packet is encoded and accepted");
  ok &= check(!echolink::isValidPacket(nudge.data(), nudge.size(), ECHOLINK_GROUP_ID, senderA),
              "device ignores its own nudge broadcast");

  return ok ? 0 : 1;
}
