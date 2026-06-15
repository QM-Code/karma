#include "karma/platform/network/protocol.h"

#include <algorithm>
#include <cstring>
#include <limits>

namespace karma::net {
namespace {

void appendLittle(std::vector<std::byte>& out, uint64_t value, std::size_t size) {
  for (std::size_t i = 0; i < size; ++i) {
    out.push_back(static_cast<std::byte>((value >> (i * 8u)) & 0xFFu));
  }
}

bool readLittle(std::span<const std::byte> bytes,
                std::size_t& offset,
                std::size_t size,
                uint64_t& value) {
  if (offset > bytes.size() || size > bytes.size() - offset) {
    return false;
  }
  value = 0;
  for (std::size_t i = 0; i < size; ++i) {
    value |= static_cast<uint64_t>(std::to_integer<uint8_t>(bytes[offset + i])) << (i * 8u);
  }
  offset += size;
  return true;
}

}  // namespace

void BinaryWriter::writeUInt8(uint8_t value) {
  bytes_.push_back(static_cast<std::byte>(value));
}

void BinaryWriter::writeUInt16(uint16_t value) {
  appendLittle(bytes_, value, 2);
}

void BinaryWriter::writeUInt32(uint32_t value) {
  appendLittle(bytes_, value, 4);
}

void BinaryWriter::writeUInt64(uint64_t value) {
  appendLittle(bytes_, value, 8);
}

void BinaryWriter::writeFloat32(float value) {
  uint32_t bits = 0;
  static_assert(sizeof(bits) == sizeof(value));
  std::memcpy(&bits, &value, sizeof(bits));
  writeUInt32(bits);
}

void BinaryWriter::writeBytes(std::span<const std::byte> bytes) {
  bytes_.insert(bytes_.end(), bytes.begin(), bytes.end());
}

void BinaryWriter::writeString(const std::string& value) {
  const auto size = static_cast<uint32_t>(
      std::min<std::size_t>(value.size(), std::numeric_limits<uint32_t>::max()));
  writeUInt32(size);
  const auto* data = reinterpret_cast<const std::byte*>(value.data());
  writeBytes(std::span<const std::byte>(data, size));
}

bool BinaryReader::readUInt8(uint8_t& value) {
  if (offset_ >= bytes_.size()) {
    return false;
  }
  value = std::to_integer<uint8_t>(bytes_[offset_++]);
  return true;
}

bool BinaryReader::readUInt16(uint16_t& value) {
  uint64_t raw = 0;
  if (!readLittle(bytes_, offset_, 2, raw)) {
    return false;
  }
  value = static_cast<uint16_t>(raw);
  return true;
}

bool BinaryReader::readUInt32(uint32_t& value) {
  uint64_t raw = 0;
  if (!readLittle(bytes_, offset_, 4, raw)) {
    return false;
  }
  value = static_cast<uint32_t>(raw);
  return true;
}

bool BinaryReader::readUInt64(uint64_t& value) {
  uint64_t raw = 0;
  if (!readLittle(bytes_, offset_, 8, raw)) {
    return false;
  }
  value = raw;
  return true;
}

bool BinaryReader::readFloat32(float& value) {
  uint32_t bits = 0;
  if (!readUInt32(bits)) {
    return false;
  }
  static_assert(sizeof(bits) == sizeof(value));
  std::memcpy(&value, &bits, sizeof(value));
  return true;
}

bool BinaryReader::readBytes(uint32_t size, std::vector<std::byte>& value) {
  if (offset_ > bytes_.size() || size > bytes_.size() - offset_) {
    return false;
  }
  value.assign(bytes_.begin() + static_cast<std::ptrdiff_t>(offset_),
               bytes_.begin() + static_cast<std::ptrdiff_t>(offset_ + size));
  offset_ += size;
  return true;
}

bool BinaryReader::readString(std::string& value) {
  uint32_t size = 0;
  if (!readUInt32(size)) {
    return false;
  }
  if (offset_ > bytes_.size() || size > bytes_.size() - offset_) {
    return false;
  }
  const auto* data = reinterpret_cast<const char*>(bytes_.data() + offset_);
  value.assign(data, data + size);
  offset_ += size;
  return true;
}

std::span<const std::byte> BinaryReader::remainingBytes() const {
  return bytes_.subspan(offset_);
}

bool BinaryReader::readRaw(std::byte* out, std::size_t size) {
  if (offset_ > bytes_.size() || size > bytes_.size() - offset_) {
    return false;
  }
  std::memcpy(out, bytes_.data() + offset_, size);
  offset_ += size;
  return true;
}

std::vector<std::byte> encodePacket(PacketHeader header,
                                    std::span<const std::byte> payload) {
  header.magic = kPacketMagic;
  header.version = kProtocolVersion;
  header.header_size = kPacketHeaderSize;
  header.payload_length = static_cast<uint32_t>(
      std::min<std::size_t>(payload.size(), std::numeric_limits<uint32_t>::max()));

  BinaryWriter writer;
  writer.writeUInt32(header.magic);
  writer.writeUInt16(header.version);
  writer.writeUInt16(header.header_size);
  writer.writeUInt32(header.app_id);
  writer.writeUInt16(static_cast<uint16_t>(header.message_type));
  writer.writeUInt16(header.flags);
  writer.writeUInt32(header.tick);
  writer.writeUInt32(header.sequence);
  writer.writeUInt32(header.payload_length);
  writer.writeBytes(payload.first(header.payload_length));
  return writer.takeBytes();
}

DecodeResult decodePacket(std::span<const std::byte> bytes,
                          uint32_t expected_app_id,
                          uint16_t expected_version) {
  if (bytes.size() < kPacketHeaderSize) {
    return {.status = DecodeStatus::TooSmall};
  }

  BinaryReader reader(bytes);
  PacketHeader header{};
  uint16_t message_type = 0;
  if (!reader.readUInt32(header.magic) ||
      !reader.readUInt16(header.version) ||
      !reader.readUInt16(header.header_size) ||
      !reader.readUInt32(header.app_id) ||
      !reader.readUInt16(message_type) ||
      !reader.readUInt16(header.flags) ||
      !reader.readUInt32(header.tick) ||
      !reader.readUInt32(header.sequence) ||
      !reader.readUInt32(header.payload_length)) {
    return {.status = DecodeStatus::TooSmall};
  }
  header.message_type = static_cast<MessageType>(message_type);

  if (header.magic != kPacketMagic) {
    return {.status = DecodeStatus::BadMagic};
  }
  if (header.version != expected_version) {
    return {.status = DecodeStatus::UnsupportedVersion};
  }
  if (header.header_size != kPacketHeaderSize) {
    return {.status = DecodeStatus::HeaderSizeMismatch};
  }
  if (expected_app_id != 0 && header.app_id != expected_app_id) {
    return {.status = DecodeStatus::AppIdMismatch};
  }
  if (header.payload_length > bytes.size()) {
    return {.status = DecodeStatus::PayloadTooLarge};
  }
  if (bytes.size() != static_cast<std::size_t>(header.header_size) + header.payload_length) {
    return {.status = DecodeStatus::PayloadLengthMismatch};
  }

  std::vector<std::byte> payload;
  if (!reader.readBytes(header.payload_length, payload)) {
    return {.status = DecodeStatus::PayloadLengthMismatch};
  }
  return {
      .status = DecodeStatus::Ok,
      .packet = Packet{.header = header, .payload = std::move(payload)},
  };
}

}  // namespace karma::net
