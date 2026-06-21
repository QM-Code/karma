#include "asset_cache_serializers.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <limits>
#include <optional>
#include <string>
#include <vector>

#include "karma/assets.h"

namespace karma::assets::detail {

namespace {

constexpr std::array<char, 8> kMagic{'K', 'A', 'S', 'S', 'E', 'T', '0', '2'};
constexpr uint32_t kKindTexture = 1u;
constexpr uint32_t kChunkDesc = 0x54444553u;      // TDES
constexpr uint32_t kChunkSubresources = 0x53554252u;  // SUBR
constexpr uint32_t kChunkBytes = 0x44415441u;     // DATA
constexpr uint32_t kChunkFallback = 0x46414c4cu;  // FALL

void appendU32(std::vector<uint8_t>& out, uint32_t value) {
  out.push_back(static_cast<uint8_t>(value & 0xffu));
  out.push_back(static_cast<uint8_t>((value >> 8u) & 0xffu));
  out.push_back(static_cast<uint8_t>((value >> 16u) & 0xffu));
  out.push_back(static_cast<uint8_t>((value >> 24u) & 0xffu));
}

void appendU64(std::vector<uint8_t>& out, uint64_t value) {
  for (uint32_t i = 0u; i < 8u; ++i) {
    out.push_back(static_cast<uint8_t>((value >> (i * 8u)) & 0xffu));
  }
}

void appendF32(std::vector<uint8_t>& out, float value) {
  static_assert(sizeof(float) == sizeof(uint32_t));
  uint32_t bits = 0u;
  std::memcpy(&bits, &value, sizeof(bits));
  appendU32(out, bits);
}

bool readU32(const std::vector<uint8_t>& bytes, std::size_t& offset, uint32_t& out) {
  if (offset + 4u > bytes.size()) {
    return false;
  }
  out = static_cast<uint32_t>(bytes[offset]) |
        (static_cast<uint32_t>(bytes[offset + 1u]) << 8u) |
        (static_cast<uint32_t>(bytes[offset + 2u]) << 16u) |
        (static_cast<uint32_t>(bytes[offset + 3u]) << 24u);
  offset += 4u;
  return true;
}

bool readU64(const std::vector<uint8_t>& bytes, std::size_t& offset, uint64_t& out) {
  if (offset + 8u > bytes.size()) {
    return false;
  }
  out = 0u;
  for (uint32_t i = 0u; i < 8u; ++i) {
    out |= static_cast<uint64_t>(bytes[offset + i]) << (i * 8u);
  }
  offset += 8u;
  return true;
}

bool readF32(const std::vector<uint8_t>& bytes, std::size_t& offset, float& out) {
  uint32_t bits = 0u;
  if (!readU32(bytes, offset, bits)) {
    return false;
  }
  std::memcpy(&out, &bits, sizeof(out));
  return true;
}

void appendChunk(std::vector<uint8_t>& out, uint32_t id, const std::vector<uint8_t>& payload) {
  appendU32(out, id);
  appendU64(out, static_cast<uint64_t>(payload.size()));
  out.insert(out.end(), payload.begin(), payload.end());
}

std::vector<uint8_t> serializeTextureBlob(const TextureAsset& texture) {
  std::vector<uint8_t> out;
  out.insert(out.end(), kMagic.begin(), kMagic.end());
  appendU32(out, AssetCache::kSchemaVersion);
  appendU32(out, kKindTexture);

  std::vector<uint8_t> desc;
  appendU32(desc, static_cast<uint32_t>(texture.desc.width));
  appendU32(desc, static_cast<uint32_t>(texture.desc.height));
  appendU32(desc, static_cast<uint32_t>(texture.desc.format));
  appendU32(desc, texture.desc.srgb ? 1u : 0u);
  appendU32(desc, texture.desc.generate_mips ? 1u : 0u);
  appendU32(desc, texture.desc.mip_levels);
  appendU32(desc, static_cast<uint32_t>(texture.payload_format));
  appendU32(desc, static_cast<uint32_t>(texture.semantic));
  appendU32(desc, static_cast<uint32_t>(texture.subresources.size()));
  appendChunk(out, kChunkDesc, desc);

  std::vector<uint8_t> subresources;
  for (const auto& subresource : texture.subresources) {
    appendU32(subresources, subresource.mip_level);
    appendU32(subresources, subresource.array_layer);
    appendU32(subresources, static_cast<uint32_t>(subresource.width));
    appendU32(subresources, static_cast<uint32_t>(subresource.height));
    appendU64(subresources, static_cast<uint64_t>(subresource.offset));
    appendU64(subresources, static_cast<uint64_t>(subresource.size));
    appendU64(subresources, static_cast<uint64_t>(subresource.row_stride));
  }
  appendChunk(out, kChunkSubresources, subresources);
  appendChunk(out, kChunkBytes, texture.bytes);
  appendChunk(out, kChunkFallback, texture.fallback_rgba8);
  return out;
}

bool parseTextureDesc(const std::vector<uint8_t>& payload, TextureAsset& texture) {
  std::size_t offset = 0u;
  uint32_t width = 0u;
  uint32_t height = 0u;
  uint32_t format = 0u;
  uint32_t srgb = 0u;
  uint32_t generate_mips = 0u;
  uint32_t mip_levels = 0u;
  uint32_t payload_format = 0u;
  uint32_t semantic = 0u;
  uint32_t subresource_count = 0u;
  if (!readU32(payload, offset, width) ||
      !readU32(payload, offset, height) ||
      !readU32(payload, offset, format) ||
      !readU32(payload, offset, srgb) ||
      !readU32(payload, offset, generate_mips) ||
      !readU32(payload, offset, mip_levels) ||
      !readU32(payload, offset, payload_format) ||
      !readU32(payload, offset, semantic) ||
      !readU32(payload, offset, subresource_count)) {
    return false;
  }
  if (width > static_cast<uint32_t>(std::numeric_limits<int>::max()) ||
      height > static_cast<uint32_t>(std::numeric_limits<int>::max())) {
    return false;
  }
  texture.desc.width = static_cast<int>(width);
  texture.desc.height = static_cast<int>(height);
  texture.desc.format = static_cast<rendering::TextureFormat>(format);
  texture.desc.srgb = srgb != 0u;
  texture.desc.generate_mips = generate_mips != 0u;
  texture.desc.mip_levels = std::max(1u, mip_levels);
  texture.payload_format = static_cast<TextureAsset::PayloadFormat>(payload_format);
  texture.semantic = static_cast<TextureAsset::Semantic>(semantic);
  texture.subresources.reserve(subresource_count);
  return true;
}

bool parseSubresources(const std::vector<uint8_t>& payload, TextureAsset& texture) {
  std::size_t offset = 0u;
  while (offset < payload.size()) {
    uint32_t mip = 0u;
    uint32_t layer = 0u;
    uint32_t width = 0u;
    uint32_t height = 0u;
    uint64_t byte_offset = 0u;
    uint64_t size = 0u;
    uint64_t row_stride = 0u;
    if (!readU32(payload, offset, mip) ||
        !readU32(payload, offset, layer) ||
        !readU32(payload, offset, width) ||
        !readU32(payload, offset, height) ||
        !readU64(payload, offset, byte_offset) ||
        !readU64(payload, offset, size) ||
        !readU64(payload, offset, row_stride)) {
      return false;
    }
    texture.subresources.push_back(rendering::TextureUploadSubresource{
        .mip_level = mip,
        .array_layer = layer,
        .width = static_cast<int>(width),
        .height = static_cast<int>(height),
        .offset = static_cast<std::size_t>(byte_offset),
        .size = static_cast<std::size_t>(size),
        .row_stride = static_cast<std::size_t>(row_stride),
    });
  }
  return true;
}

std::optional<TextureAsset> deserializeTextureBlob(const std::vector<uint8_t>& bytes,
                                                   std::string* diagnostic) {
  if (bytes.size() < kMagic.size() + 8u ||
      !std::equal(kMagic.begin(), kMagic.end(), bytes.begin())) {
    if (diagnostic != nullptr) {
      *diagnostic = "cache blob magic mismatch";
    }
    return std::nullopt;
  }

  std::size_t offset = kMagic.size();
  uint32_t schema = 0u;
  uint32_t kind = 0u;
  if (!readU32(bytes, offset, schema) || !readU32(bytes, offset, kind) ||
      schema != AssetCache::kSchemaVersion || kind != kKindTexture) {
    if (diagnostic != nullptr) {
      *diagnostic = "cache blob version or kind mismatch";
    }
    return std::nullopt;
  }

  TextureAsset texture{};
  bool saw_desc = false;
  bool saw_bytes = false;
  while (offset < bytes.size()) {
    uint32_t chunk_id = 0u;
    uint64_t chunk_size = 0u;
    if (!readU32(bytes, offset, chunk_id) || !readU64(bytes, offset, chunk_size) ||
        chunk_size > bytes.size() - offset) {
      return std::nullopt;
    }
    std::vector<uint8_t> payload(bytes.begin() + static_cast<std::ptrdiff_t>(offset),
                                 bytes.begin() + static_cast<std::ptrdiff_t>(offset + chunk_size));
    offset += static_cast<std::size_t>(chunk_size);
    switch (chunk_id) {
      case kChunkDesc:
        saw_desc = parseTextureDesc(payload, texture);
        break;
      case kChunkSubresources:
        if (!parseSubresources(payload, texture)) {
          return std::nullopt;
        }
        break;
      case kChunkBytes:
        texture.bytes = std::move(payload);
        saw_bytes = true;
        break;
      case kChunkFallback:
        texture.fallback_rgba8 = std::move(payload);
        break;
      default:
        break;
    }
  }
  if (!saw_desc || !saw_bytes) {
    return std::nullopt;
  }
  texture.content_hash = hashBytes(texture.bytes.data(), texture.bytes.size());
  return texture;
}

}  // namespace

std::vector<uint8_t> serializeTexture(const TextureAsset& texture) {
  return serializeTextureBlob(texture);
}

std::optional<TextureAsset> deserializeTexture(const std::vector<uint8_t>& bytes,
                                               std::string* diagnostic) {
  return deserializeTextureBlob(bytes, diagnostic);
}

}  // namespace karma::assets::detail
