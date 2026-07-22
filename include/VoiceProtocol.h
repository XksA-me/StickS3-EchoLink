#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace echolink {

constexpr uint16_t kProtocolMagic = 0xEC71;
constexpr uint8_t kProtocolVersion = 2;

enum class PacketType : uint8_t {
  VoiceStart = 1,
  Audio = 2,
  VoiceEnd = 3,
  Nudge = 4,
};

struct __attribute__((packed)) PacketHeader {
  uint16_t magic;
  uint8_t version;
  uint8_t type;
  uint32_t groupId;
  uint32_t senderId;
  uint16_t sessionId;
  uint16_t sequence;
  uint32_t totalBytes;
  uint16_t payloadBytes;
};

static_assert(sizeof(PacketHeader) == 22, "Unexpected voice protocol header size");

inline PacketHeader readHeader(const uint8_t* data) {
  PacketHeader header{};
  memcpy(&header, data, sizeof(header));
  return header;
}

inline bool isValidPacket(const uint8_t* data, size_t length, uint32_t groupId,
                          uint32_t localSenderId) {
  if (data == nullptr || length < sizeof(PacketHeader)) {
    return false;
  }
  const PacketHeader header = readHeader(data);
  return header.magic == kProtocolMagic && header.version == kProtocolVersion &&
         header.groupId == groupId && header.senderId != localSenderId &&
         header.payloadBytes + sizeof(PacketHeader) == length;
}

inline size_t encodePacket(uint8_t* frame, size_t frameCapacity, PacketType type,
                           uint32_t groupId, uint32_t senderId, uint16_t sessionId,
                           uint16_t sequence, uint32_t totalBytes, const int8_t* payload,
                           uint16_t payloadBytes) {
  const size_t frameBytes = sizeof(PacketHeader) + payloadBytes;
  if (frame == nullptr || frameBytes > frameCapacity || (payloadBytes > 0 && payload == nullptr)) {
    return 0;
  }
  const PacketHeader header{
      kProtocolMagic,
      kProtocolVersion,
      static_cast<uint8_t>(type),
      groupId,
      senderId,
      sessionId,
      sequence,
      totalBytes,
      payloadBytes,
  };
  memcpy(frame, &header, sizeof(header));
  if (payloadBytes > 0) {
    memcpy(frame + sizeof(header), payload, payloadBytes);
  }
  return frameBytes;
}

}  // namespace echolink
