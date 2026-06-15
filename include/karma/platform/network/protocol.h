#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace karma::net {

/// \ingroup karma_platform
/// Wire protocol version for Karma networking v1 packets.
inline constexpr uint16_t kProtocolVersion = 1;
/// \ingroup karma_platform
/// Fixed packet header size in bytes.
inline constexpr uint16_t kPacketHeaderSize = 28;
/// \ingroup karma_platform
/// Packet magic for quick malformed datagram rejection.
inline constexpr uint32_t kPacketMagic = 0x4B4E4554u;  // KNET

/// \ingroup karma_platform
/// Built-in network message types understood by the v1 session layer.
enum class MessageType : uint16_t {
  HandshakeRequest = 1,
  HandshakeAccept = 2,
  Disconnect = 3,
  Ping = 4,
  Pong = 5,
  CustomReliable = 6,
  CustomUnreliable = 7,
  EntitySpawn = 8,
  EntityDespawn = 9,
  ComponentSnapshot = 10,
  ComponentDelta = 11,
  AuthorityTransfer = 12,
  InputCommand = 13
};

/// \ingroup karma_platform
/// Packet flags stored in `PacketHeader::flags`.
enum PacketFlag : uint16_t {
  PacketFlagNone = 0,
  PacketFlagReliable = 1u << 0u,
  PacketFlagFragment = 1u << 1u,
  PacketFlagAck = 1u << 2u
};

/// \ingroup karma_platform
/// Versioned binary packet header.
struct PacketHeader {
  uint32_t magic = kPacketMagic;
  uint16_t version = kProtocolVersion;
  uint16_t header_size = kPacketHeaderSize;
  uint32_t app_id = 0;
  MessageType message_type = MessageType::CustomReliable;
  uint16_t flags = PacketFlagNone;
  uint32_t tick = 0;
  uint32_t sequence = 0;
  uint32_t payload_length = 0;
};

/// \ingroup karma_platform
/// Decoded protocol packet.
struct Packet {
  PacketHeader header{};
  std::vector<std::byte> payload;
};

/// \ingroup karma_platform
/// Packet decode status.
enum class DecodeStatus {
  Ok,
  TooSmall,
  BadMagic,
  UnsupportedVersion,
  HeaderSizeMismatch,
  AppIdMismatch,
  PayloadLengthMismatch,
  PayloadTooLarge
};

/// \ingroup karma_platform
/// Result for packet decode attempts.
struct DecodeResult {
  DecodeStatus status = DecodeStatus::TooSmall;
  Packet packet{};

  bool ok() const { return status == DecodeStatus::Ok; }
};

/// \ingroup karma_platform
/// Binary writer used by protocol and component replication payloads.
class BinaryWriter {
 public:
  void writeUInt8(uint8_t value);
  void writeUInt16(uint16_t value);
  void writeUInt32(uint32_t value);
  void writeUInt64(uint64_t value);
  void writeFloat32(float value);
  void writeBytes(std::span<const std::byte> bytes);
  void writeString(const std::string& value);

  const std::vector<std::byte>& bytes() const { return bytes_; }
  std::vector<std::byte> takeBytes() { return std::move(bytes_); }

 private:
  std::vector<std::byte> bytes_;
};

/// \ingroup karma_platform
/// Bounds-checked binary reader used by protocol and replication payloads.
class BinaryReader {
 public:
  explicit BinaryReader(std::span<const std::byte> bytes) : bytes_(bytes) {}

  bool readUInt8(uint8_t& value);
  bool readUInt16(uint16_t& value);
  bool readUInt32(uint32_t& value);
  bool readUInt64(uint64_t& value);
  bool readFloat32(float& value);
  bool readBytes(uint32_t size, std::vector<std::byte>& value);
  bool readString(std::string& value);

  std::span<const std::byte> remainingBytes() const;
  std::size_t remaining() const { return bytes_.size() - offset_; }
  bool exhausted() const { return offset_ == bytes_.size(); }

 private:
  bool readRaw(std::byte* out, std::size_t size);

  std::span<const std::byte> bytes_;
  std::size_t offset_ = 0;
};

/// \ingroup karma_platform
/// Encodes `header` and `payload` into one wire packet.
std::vector<std::byte> encodePacket(PacketHeader header,
                                    std::span<const std::byte> payload);

/// \ingroup karma_platform
/// Decodes a wire packet. Set `expected_app_id` to `0` to skip app id matching.
DecodeResult decodePacket(std::span<const std::byte> bytes,
                          uint32_t expected_app_id = 0,
                          uint16_t expected_version = kProtocolVersion);

}  // namespace karma::net
