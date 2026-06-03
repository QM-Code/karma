#include "gltf_document.h"

#include <cstring>
#include <fstream>
#include <iterator>

namespace karma::scene {

namespace {

std::uint32_t readU32LE(const std::vector<std::uint8_t>& bytes, size_t offset) {
  if (offset + 4 > bytes.size()) {
    return 0;
  }
  return static_cast<std::uint32_t>(bytes[offset]) |
         (static_cast<std::uint32_t>(bytes[offset + 1]) << 8u) |
         (static_cast<std::uint32_t>(bytes[offset + 2]) << 16u) |
         (static_cast<std::uint32_t>(bytes[offset + 3]) << 24u);
}

size_t gltfComponentCount(std::string_view type) {
  if (type == "SCALAR") return 1;
  if (type == "VEC2") return 2;
  if (type == "VEC3") return 3;
  if (type == "VEC4") return 4;
  if (type == "MAT4") return 16;
  return 0;
}

}  // namespace

GltfDocument loadGltfDocument(const std::filesystem::path& path) {
  GltfDocument doc{};
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    return doc;
  }
  std::vector<std::uint8_t> bytes{std::istreambuf_iterator<char>(file),
                                  std::istreambuf_iterator<char>()};
  if (bytes.size() < 12) {
    return doc;
  }

  constexpr std::uint32_t kGlbMagic = 0x46546C67u;
  constexpr std::uint32_t kJsonChunk = 0x4E4F534Au;
  constexpr std::uint32_t kBinChunk = 0x004E4942u;
  if (readU32LE(bytes, 0) != kGlbMagic || readU32LE(bytes, 4) != 2u) {
    return doc;
  }

  const std::uint32_t total_length = readU32LE(bytes, 8);
  if (total_length > bytes.size()) {
    return doc;
  }

  size_t offset = 12;
  while (offset + 8 <= total_length) {
    const std::uint32_t chunk_length = readU32LE(bytes, offset);
    const std::uint32_t chunk_type = readU32LE(bytes, offset + 4);
    offset += 8;
    if (offset + chunk_length > bytes.size()) {
      return {};
    }
    if (chunk_type == kJsonChunk) {
      const std::string json_text(reinterpret_cast<const char*>(bytes.data() + offset),
                                  chunk_length);
      doc.json = Json::parse(json_text, nullptr, false);
      if (doc.json.is_discarded()) {
        doc.json = Json{};
      }
    } else if (chunk_type == kBinChunk) {
      doc.bin.assign(bytes.begin() + static_cast<std::ptrdiff_t>(offset),
                     bytes.begin() + static_cast<std::ptrdiff_t>(offset + chunk_length));
    }
    offset += chunk_length;
  }
  return doc;
}

bool readFloatAccessor(const GltfDocument& doc,
                       uint32_t accessor_index,
                       size_t expected_components,
                       std::vector<float>& out,
                       size_t* out_count) {
  out.clear();
  if (!doc.valid() ||
      !doc.json.contains("accessors") ||
      !doc.json["accessors"].is_array() ||
      accessor_index >= doc.json["accessors"].size()) {
    return false;
  }

  const Json& accessor = doc.json["accessors"][accessor_index];
  if (accessor.value("componentType", 0) != 5126 ||
      !accessor.contains("bufferView")) {
    return false;
  }
  const size_t component_count = gltfComponentCount(accessor.value("type", std::string{}));
  if (component_count == 0 ||
      (expected_components != 0 && component_count != expected_components)) {
    return false;
  }
  const size_t count = accessor.value("count", 0u);
  const uint32_t buffer_view_index = accessor.value("bufferView", 0u);
  if (!doc.json.contains("bufferViews") ||
      !doc.json["bufferViews"].is_array() ||
      buffer_view_index >= doc.json["bufferViews"].size()) {
    return false;
  }

  const Json& view = doc.json["bufferViews"][buffer_view_index];
  const size_t view_offset = view.value("byteOffset", 0u);
  const size_t accessor_offset = accessor.value("byteOffset", 0u);
  const size_t stride =
      view.value("byteStride", static_cast<unsigned int>(component_count * sizeof(float)));
  const size_t base_offset = view_offset + accessor_offset;
  if (stride < component_count * sizeof(float)) {
    return false;
  }

  out.resize(count * component_count);
  for (size_t i = 0; i < count; ++i) {
    const size_t element_offset = base_offset + i * stride;
    if (element_offset + component_count * sizeof(float) > doc.bin.size()) {
      out.clear();
      return false;
    }
    for (size_t c = 0; c < component_count; ++c) {
      float value = 0.0f;
      std::memcpy(&value, doc.bin.data() + element_offset + c * sizeof(float), sizeof(float));
      out[i * component_count + c] = value;
    }
  }
  if (out_count != nullptr) {
    *out_count = count;
  }
  return true;
}

bool readVec4JointAccessor(const GltfDocument& doc,
                           uint32_t accessor_index,
                           std::vector<glm::uvec4>& out) {
  out.clear();
  if (!doc.valid() ||
      !doc.json.contains("accessors") ||
      !doc.json["accessors"].is_array() ||
      accessor_index >= doc.json["accessors"].size()) {
    return false;
  }

  const Json& accessor = doc.json["accessors"][accessor_index];
  const int component_type = accessor.value("componentType", 0);
  if ((component_type != 5121 && component_type != 5123 && component_type != 5125) ||
      accessor.value("type", std::string{}) != "VEC4" ||
      !accessor.contains("bufferView")) {
    return false;
  }

  const size_t count = accessor.value("count", 0u);
  const uint32_t buffer_view_index = accessor.value("bufferView", 0u);
  if (!doc.json.contains("bufferViews") ||
      !doc.json["bufferViews"].is_array() ||
      buffer_view_index >= doc.json["bufferViews"].size()) {
    return false;
  }

  const Json& view = doc.json["bufferViews"][buffer_view_index];
  const size_t component_size =
      component_type == 5121 ? sizeof(std::uint8_t)
                             : component_type == 5123 ? sizeof(std::uint16_t)
                                                      : sizeof(std::uint32_t);
  const size_t component_count = 4;
  const size_t view_offset = view.value("byteOffset", 0u);
  const size_t accessor_offset = accessor.value("byteOffset", 0u);
  const size_t stride =
      view.value("byteStride", static_cast<unsigned int>(component_count * component_size));
  const size_t base_offset = view_offset + accessor_offset;
  if (stride < component_count * component_size) {
    return false;
  }

  out.resize(count);
  for (size_t i = 0; i < count; ++i) {
    const size_t element_offset = base_offset + i * stride;
    if (element_offset + component_count * component_size > doc.bin.size()) {
      out.clear();
      return false;
    }
    glm::uvec4 joints{0u};
    for (size_t c = 0; c < component_count; ++c) {
      const size_t component_offset = element_offset + c * component_size;
      if (component_type == 5121) {
        joints[static_cast<glm::length_t>(c)] =
            static_cast<uint32_t>(doc.bin[component_offset]);
      } else if (component_type == 5123) {
        std::uint16_t value = 0;
        std::memcpy(&value, doc.bin.data() + component_offset, sizeof(value));
        joints[static_cast<glm::length_t>(c)] = static_cast<uint32_t>(value);
      } else {
        std::uint32_t value = 0;
        std::memcpy(&value, doc.bin.data() + component_offset, sizeof(value));
        joints[static_cast<glm::length_t>(c)] = value;
      }
    }
    out[i] = joints;
  }
  return true;
}

bool readVec4WeightAccessor(const GltfDocument& doc,
                            uint32_t accessor_index,
                            std::vector<glm::vec4>& out) {
  out.clear();
  if (!doc.valid() ||
      !doc.json.contains("accessors") ||
      !doc.json["accessors"].is_array() ||
      accessor_index >= doc.json["accessors"].size()) {
    return false;
  }

  const Json& accessor = doc.json["accessors"][accessor_index];
  const int component_type = accessor.value("componentType", 0);
  if ((component_type != 5126 && component_type != 5121 && component_type != 5123) ||
      accessor.value("type", std::string{}) != "VEC4" ||
      !accessor.contains("bufferView")) {
    return false;
  }

  const size_t count = accessor.value("count", 0u);
  const uint32_t buffer_view_index = accessor.value("bufferView", 0u);
  if (!doc.json.contains("bufferViews") ||
      !doc.json["bufferViews"].is_array() ||
      buffer_view_index >= doc.json["bufferViews"].size()) {
    return false;
  }

  const Json& view = doc.json["bufferViews"][buffer_view_index];
  const size_t component_size =
      component_type == 5126 ? sizeof(float)
                             : component_type == 5121 ? sizeof(std::uint8_t)
                                                      : sizeof(std::uint16_t);
  const size_t component_count = 4;
  const size_t view_offset = view.value("byteOffset", 0u);
  const size_t accessor_offset = accessor.value("byteOffset", 0u);
  const size_t stride =
      view.value("byteStride", static_cast<unsigned int>(component_count * component_size));
  const size_t base_offset = view_offset + accessor_offset;
  if (stride < component_count * component_size) {
    return false;
  }

  const bool normalized = accessor.value("normalized", false);
  out.resize(count);
  for (size_t i = 0; i < count; ++i) {
    const size_t element_offset = base_offset + i * stride;
    if (element_offset + component_count * component_size > doc.bin.size()) {
      out.clear();
      return false;
    }
    glm::vec4 weights{0.0f};
    for (size_t c = 0; c < component_count; ++c) {
      const size_t component_offset = element_offset + c * component_size;
      if (component_type == 5126) {
        float value = 0.0f;
        std::memcpy(&value, doc.bin.data() + component_offset, sizeof(value));
        weights[static_cast<glm::length_t>(c)] = value;
      } else if (component_type == 5121) {
        const auto value = static_cast<float>(doc.bin[component_offset]);
        weights[static_cast<glm::length_t>(c)] = normalized ? value / 255.0f : value;
      } else {
        std::uint16_t value = 0;
        std::memcpy(&value, doc.bin.data() + component_offset, sizeof(value));
        weights[static_cast<glm::length_t>(c)] =
            normalized ? static_cast<float>(value) / 65535.0f : static_cast<float>(value);
      }
    }
    out[i] = weights;
  }
  return true;
}

bool readIndexAccessor(const GltfDocument& doc,
                       uint32_t accessor_index,
                       std::vector<uint32_t>& out) {
  out.clear();
  if (!doc.valid() ||
      !doc.json.contains("accessors") ||
      !doc.json["accessors"].is_array() ||
      accessor_index >= doc.json["accessors"].size()) {
    return false;
  }

  const Json& accessor = doc.json["accessors"][accessor_index];
  const int component_type = accessor.value("componentType", 0);
  if ((component_type != 5121 && component_type != 5123 && component_type != 5125) ||
      accessor.value("type", std::string{}) != "SCALAR" ||
      !accessor.contains("bufferView")) {
    return false;
  }

  const size_t count = accessor.value("count", 0u);
  const uint32_t buffer_view_index = accessor.value("bufferView", 0u);
  if (!doc.json.contains("bufferViews") ||
      !doc.json["bufferViews"].is_array() ||
      buffer_view_index >= doc.json["bufferViews"].size()) {
    return false;
  }

  const Json& view = doc.json["bufferViews"][buffer_view_index];
  const size_t component_size =
      component_type == 5121 ? sizeof(std::uint8_t)
                             : component_type == 5123 ? sizeof(std::uint16_t)
                                                      : sizeof(std::uint32_t);
  const size_t view_offset = view.value("byteOffset", 0u);
  const size_t accessor_offset = accessor.value("byteOffset", 0u);
  const size_t stride = view.value("byteStride", static_cast<unsigned int>(component_size));
  const size_t base_offset = view_offset + accessor_offset;
  if (stride < component_size) {
    return false;
  }

  out.resize(count);
  for (size_t i = 0; i < count; ++i) {
    const size_t element_offset = base_offset + i * stride;
    if (element_offset + component_size > doc.bin.size()) {
      out.clear();
      return false;
    }
    if (component_type == 5121) {
      out[i] = static_cast<uint32_t>(doc.bin[element_offset]);
    } else if (component_type == 5123) {
      std::uint16_t value = 0;
      std::memcpy(&value, doc.bin.data() + element_offset, sizeof(value));
      out[i] = static_cast<uint32_t>(value);
    } else {
      std::uint32_t value = 0;
      std::memcpy(&value, doc.bin.data() + element_offset, sizeof(value));
      out[i] = value;
    }
  }
  return true;
}

std::string gltfNodeName(const GltfDocument& doc, uint32_t node_index) {
  if (!doc.valid() ||
      !doc.json.contains("nodes") ||
      !doc.json["nodes"].is_array() ||
      node_index >= doc.json["nodes"].size()) {
    return {};
  }
  return doc.json["nodes"][node_index].value("name", std::string{});
}

std::unordered_map<uint32_t, uint32_t> buildGltfNodeToPrefabIndex(
    const GltfDocument& doc,
    const std::unordered_map<std::string, uint32_t>& node_indices_by_name) {
  std::unordered_map<uint32_t, uint32_t> out;
  if (!doc.valid() || !doc.json.contains("nodes") || !doc.json["nodes"].is_array()) {
    return out;
  }
  for (size_t node_index = 0; node_index < doc.json["nodes"].size(); ++node_index) {
    const std::string name = gltfNodeName(doc, static_cast<uint32_t>(node_index));
    if (name.empty()) {
      continue;
    }
    const auto it = node_indices_by_name.find(name);
    if (it != node_indices_by_name.end()) {
      out.emplace(static_cast<uint32_t>(node_index), it->second);
    }
  }
  return out;
}

}  // namespace karma::scene
