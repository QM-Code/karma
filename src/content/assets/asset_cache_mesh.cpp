#include "asset_cache_serializers.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <exception>
#include <limits>
#include <optional>
#include <string>
#include <vector>

#include "karma/assets.h"

namespace karma::assets::detail {

namespace {

constexpr std::array<char, 8> kMagic{'K', 'A', 'S', 'S', 'E', 'T', '0', '2'};
constexpr uint32_t kKindMesh = 2u;
constexpr uint32_t kChunkMesh = 0x4853454du;      // MESH

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

bool readCount(const std::vector<uint8_t>& bytes, std::size_t& offset, std::size_t& out) {
  uint64_t count = 0u;
  if (!readU64(bytes, offset, count) ||
      count > static_cast<uint64_t>(std::numeric_limits<std::size_t>::max())) {
    return false;
  }
  out = static_cast<std::size_t>(count);
  return true;
}

void appendString(std::vector<uint8_t>& out, const std::string& value) {
  appendU64(out, static_cast<uint64_t>(value.size()));
  const auto* begin = reinterpret_cast<const uint8_t*>(value.data());
  out.insert(out.end(), begin, begin + value.size());
}

bool readString(const std::vector<uint8_t>& bytes, std::size_t& offset, std::string& out) {
  std::size_t size = 0u;
  if (!readCount(bytes, offset, size) || size > bytes.size() - offset) {
    return false;
  }
  out.assign(reinterpret_cast<const char*>(bytes.data() + offset), size);
  offset += size;
  return true;
}

void appendVec2(std::vector<uint8_t>& out, const glm::vec2& value) {
  appendF32(out, value.x);
  appendF32(out, value.y);
}

void appendVec3(std::vector<uint8_t>& out, const glm::vec3& value) {
  appendF32(out, value.x);
  appendF32(out, value.y);
  appendF32(out, value.z);
}

void appendVec4(std::vector<uint8_t>& out, const glm::vec4& value) {
  appendF32(out, value.x);
  appendF32(out, value.y);
  appendF32(out, value.z);
  appendF32(out, value.w);
}

void appendUvec4(std::vector<uint8_t>& out, const glm::uvec4& value) {
  appendU32(out, value.x);
  appendU32(out, value.y);
  appendU32(out, value.z);
  appendU32(out, value.w);
}

bool readVec2Binary(const std::vector<uint8_t>& bytes, std::size_t& offset, glm::vec2& out) {
  return readF32(bytes, offset, out.x) &&
         readF32(bytes, offset, out.y);
}

bool readVec3Binary(const std::vector<uint8_t>& bytes, std::size_t& offset, glm::vec3& out) {
  return readF32(bytes, offset, out.x) &&
         readF32(bytes, offset, out.y) &&
         readF32(bytes, offset, out.z);
}

bool readVec4Binary(const std::vector<uint8_t>& bytes, std::size_t& offset, glm::vec4& out) {
  return readF32(bytes, offset, out.x) &&
         readF32(bytes, offset, out.y) &&
         readF32(bytes, offset, out.z) &&
         readF32(bytes, offset, out.w);
}

bool readUvec4Binary(const std::vector<uint8_t>& bytes,
                     std::size_t& offset,
                     glm::uvec4& out) {
  return readU32(bytes, offset, out.x) &&
         readU32(bytes, offset, out.y) &&
         readU32(bytes, offset, out.z) &&
         readU32(bytes, offset, out.w);
}

template <typename T, typename Writer>
void appendBinaryVector(std::vector<uint8_t>& out,
                        const std::vector<T>& values,
                        Writer writer) {
  appendU64(out, static_cast<uint64_t>(values.size()));
  for (const T& value : values) {
    writer(out, value);
  }
}

template <typename T, typename Reader>
bool readBinaryVector(const std::vector<uint8_t>& bytes,
                      std::size_t& offset,
                      std::vector<T>& out,
                      std::size_t encoded_element_size,
                      Reader reader) {
  std::size_t count = 0u;
  if (!readCount(bytes, offset, count) || encoded_element_size == 0u ||
      count > (bytes.size() - offset) / encoded_element_size) {
    return false;
  }
  std::vector<T> parsed;
  parsed.reserve(count);
  for (std::size_t i = 0u; i < count; ++i) {
    T value{};
    if (!reader(bytes, offset, value)) {
      return false;
    }
    parsed.push_back(std::move(value));
  }
  out = std::move(parsed);
  return true;
}

std::vector<uint8_t> serializeMeshPayload(const world::MeshData& mesh) {
  std::vector<uint8_t> payload;
  appendBinaryVector(payload, mesh.vertices, appendVec3);
  appendBinaryVector(payload, mesh.normals, appendVec3);
  appendBinaryVector(payload, mesh.uvs, appendVec2);
  appendBinaryVector(payload, mesh.uvs1, appendVec2);
  appendBinaryVector(payload, mesh.tangents, appendVec4);
  appendBinaryVector(payload, mesh.joint_indices, appendUvec4);
  appendBinaryVector(payload, mesh.joint_weights, appendVec4);
  appendBinaryVector(payload, mesh.indices, [](std::vector<uint8_t>& out, uint32_t value) {
    appendU32(out, value);
  });

  appendU64(payload, static_cast<uint64_t>(mesh.morph_targets.size()));
  for (const auto& target : mesh.morph_targets) {
    appendBinaryVector(payload, target.position_deltas, appendVec3);
    appendBinaryVector(payload, target.normal_deltas, appendVec3);
    appendBinaryVector(payload, target.tangent_deltas, appendVec3);
  }

  appendU64(payload, static_cast<uint64_t>(mesh.submeshes.size()));
  for (const auto& submesh : mesh.submeshes) {
    appendU32(payload, submesh.index_offset);
    appendU32(payload, submesh.index_count);
    appendU32(payload, submesh.material_slot);
  }

  appendU64(payload, static_cast<uint64_t>(mesh.material_slots.size()));
  for (const auto& slot : mesh.material_slots) {
    appendString(payload, slot.name);
    appendString(payload, slot.default_material_key);
  }

  return payload;
}

bool parseMeshPayload(const std::vector<uint8_t>& payload, world::MeshData& mesh) {
  std::size_t offset = 0u;
  world::MeshData parsed{};
  if (!readBinaryVector(payload, offset, parsed.vertices, 12u, readVec3Binary) ||
      !readBinaryVector(payload, offset, parsed.normals, 12u, readVec3Binary) ||
      !readBinaryVector(payload, offset, parsed.uvs, 8u, readVec2Binary) ||
      !readBinaryVector(payload, offset, parsed.uvs1, 8u, readVec2Binary) ||
      !readBinaryVector(payload, offset, parsed.tangents, 16u, readVec4Binary) ||
      !readBinaryVector(payload, offset, parsed.joint_indices, 16u, readUvec4Binary) ||
      !readBinaryVector(payload, offset, parsed.joint_weights, 16u, readVec4Binary) ||
      !readBinaryVector(payload, offset, parsed.indices, 4u,
                        [](const std::vector<uint8_t>& bytes,
                           std::size_t& read_offset,
                           uint32_t& value) {
        return readU32(bytes, read_offset, value);
      })) {
    return false;
  }

  std::size_t morph_target_count = 0u;
  if (!readCount(payload, offset, morph_target_count) ||
      morph_target_count > (payload.size() - offset) / 24u) {
    return false;
  }
  parsed.morph_targets.reserve(morph_target_count);
  for (std::size_t i = 0u; i < morph_target_count; ++i) {
    world::MeshData::MorphTarget target{};
    if (!readBinaryVector(payload, offset, target.position_deltas, 12u, readVec3Binary) ||
        !readBinaryVector(payload, offset, target.normal_deltas, 12u, readVec3Binary) ||
        !readBinaryVector(payload, offset, target.tangent_deltas, 12u, readVec3Binary)) {
      return false;
    }
    parsed.morph_targets.push_back(std::move(target));
  }

  std::size_t submesh_count = 0u;
  if (!readCount(payload, offset, submesh_count) ||
      submesh_count > (payload.size() - offset) / 12u) {
    return false;
  }
  parsed.submeshes.reserve(submesh_count);
  for (std::size_t i = 0u; i < submesh_count; ++i) {
    world::MeshSubmesh submesh{};
    if (!readU32(payload, offset, submesh.index_offset) ||
        !readU32(payload, offset, submesh.index_count) ||
        !readU32(payload, offset, submesh.material_slot)) {
      return false;
    }
    parsed.submeshes.push_back(submesh);
  }

  std::size_t material_slot_count = 0u;
  if (!readCount(payload, offset, material_slot_count) ||
      material_slot_count > (payload.size() - offset) / 16u) {
    return false;
  }
  parsed.material_slots.reserve(material_slot_count);
  for (std::size_t i = 0u; i < material_slot_count; ++i) {
    world::MeshMaterialSlot slot{};
    if (!readString(payload, offset, slot.name) ||
        !readString(payload, offset, slot.default_material_key)) {
      return false;
    }
    parsed.material_slots.push_back(std::move(slot));
  }

  if (offset != payload.size()) {
    return false;
  }
  mesh = std::move(parsed);
  return true;
}

std::vector<uint8_t> serializeMeshBlob(const world::MeshData& mesh) {
  std::vector<uint8_t> out;
  out.insert(out.end(), kMagic.begin(), kMagic.end());
  appendU32(out, AssetCache::kSchemaVersion);
  appendU32(out, kKindMesh);
  appendChunk(out, kChunkMesh, serializeMeshPayload(mesh));
  return out;
}

std::optional<world::MeshData> deserializeMeshBlob(const std::vector<uint8_t>& bytes,
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
      schema != AssetCache::kSchemaVersion || kind != kKindMesh) {
    if (diagnostic != nullptr) {
      *diagnostic = "cache blob version or kind mismatch";
    }
    return std::nullopt;
  }

  while (offset < bytes.size()) {
    uint32_t chunk_id = 0u;
    uint64_t chunk_size = 0u;
    if (!readU32(bytes, offset, chunk_id) || !readU64(bytes, offset, chunk_size) ||
        chunk_size > bytes.size() - offset) {
      if (diagnostic != nullptr) {
        *diagnostic = "cache blob chunk is truncated";
      }
      return std::nullopt;
    }
    if (chunk_id == kChunkMesh) {
      try {
        const std::vector<uint8_t> payload(
            bytes.begin() + static_cast<std::ptrdiff_t>(offset),
            bytes.begin() + static_cast<std::ptrdiff_t>(offset + chunk_size));
        world::MeshData mesh{};
        if (!parseMeshPayload(payload, mesh)) {
          if (diagnostic != nullptr) {
            *diagnostic = "cache mesh payload failed validation";
          }
          return std::nullopt;
        }
        return mesh;
      } catch (const std::exception& e) {
        if (diagnostic != nullptr) {
          *diagnostic = e.what();
        }
        return std::nullopt;
      }
    }
    offset += static_cast<std::size_t>(chunk_size);
  }
  if (diagnostic != nullptr) {
    *diagnostic = "cache blob is missing mesh payload";
  }
  return std::nullopt;
}

}  // namespace

std::vector<uint8_t> serializeMesh(const world::MeshData& mesh) {
  return serializeMeshBlob(mesh);
}

std::optional<world::MeshData> deserializeMesh(const std::vector<uint8_t>& bytes,
                                                  std::string* diagnostic) {
  return deserializeMeshBlob(bytes, diagnostic);
}

}  // namespace karma::assets::detail
