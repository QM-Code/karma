#include "gltf_document.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>

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

std::vector<std::uint8_t> readFileBytes(const std::filesystem::path& path) {
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    return {};
  }
  return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

int base64Value(char c) {
  if (c >= 'A' && c <= 'Z') return c - 'A';
  if (c >= 'a' && c <= 'z') return c - 'a' + 26;
  if (c >= '0' && c <= '9') return c - '0' + 52;
  if (c == '+') return 62;
  if (c == '/') return 63;
  return -1;
}

std::vector<std::uint8_t> decodeBase64(std::string_view text) {
  std::vector<std::uint8_t> out;
  int value = 0;
  int bits = -8;
  for (char c : text) {
    if (std::isspace(static_cast<unsigned char>(c))) {
      continue;
    }
    if (c == '=') {
      break;
    }
    const int decoded = base64Value(c);
    if (decoded < 0) {
      return {};
    }
    value = (value << 6) | decoded;
    bits += 6;
    if (bits >= 0) {
      out.push_back(static_cast<std::uint8_t>((value >> bits) & 0xFF));
      bits -= 8;
    }
  }
  return out;
}

bool startsWith(std::string_view text, std::string_view prefix) {
  return text.size() >= prefix.size() &&
         std::equal(prefix.begin(), prefix.end(), text.begin());
}

std::vector<std::uint8_t> decodeDataUri(std::string_view uri) {
  constexpr std::string_view kPrefix = "data:";
  if (!startsWith(uri, kPrefix)) {
    return {};
  }
  const size_t comma = uri.find(',');
  if (comma == std::string_view::npos) {
    return {};
  }
  const std::string_view metadata = uri.substr(0, comma);
  const std::string_view payload = uri.substr(comma + 1);
  if (metadata.find(";base64") != std::string_view::npos) {
    return decodeBase64(payload);
  }
  std::vector<std::uint8_t> out;
  out.reserve(payload.size());
  for (size_t i = 0; i < payload.size(); ++i) {
    if (payload[i] == '%' && i + 2 < payload.size()) {
      const auto hex = [](char h) -> int {
        if (h >= '0' && h <= '9') return h - '0';
        if (h >= 'a' && h <= 'f') return h - 'a' + 10;
        if (h >= 'A' && h <= 'F') return h - 'A' + 10;
        return -1;
      };
      const int hi = hex(payload[i + 1]);
      const int lo = hex(payload[i + 2]);
      if (hi >= 0 && lo >= 0) {
        out.push_back(static_cast<std::uint8_t>((hi << 4) | lo));
        i += 2;
        continue;
      }
    }
    out.push_back(static_cast<std::uint8_t>(payload[i]));
  }
  return out;
}

const std::vector<std::uint8_t>* gltfBuffer(const GltfDocument& doc, uint32_t index) {
  if (!doc.buffers.empty()) {
    if (index >= doc.buffers.size()) {
      return nullptr;
    }
    return &doc.buffers[index];
  }
  if (index == 0u && !doc.bin.empty()) {
    return &doc.bin;
  }
  return nullptr;
}

const std::vector<std::uint8_t>* accessorBuffer(const GltfDocument& doc,
                                                const Json& view) {
  const uint32_t buffer_index = view.value("buffer", 0u);
  return gltfBuffer(doc, buffer_index);
}

size_t componentTypeSize(int component_type) {
  switch (component_type) {
    case 5120:
    case 5121:
      return 1;
    case 5122:
    case 5123:
      return 2;
    case 5125:
    case 5126:
      return 4;
    default:
      return 0;
  }
}

const Json* getBufferView(const GltfDocument& doc, uint32_t buffer_view_index) {
  if (!doc.json.contains("bufferViews") ||
      !doc.json["bufferViews"].is_array() ||
      buffer_view_index >= doc.json["bufferViews"].size()) {
    return nullptr;
  }
  return &doc.json["bufferViews"][buffer_view_index];
}

bool readSparseIndices(const GltfDocument& doc,
                       const Json& indices_desc,
                       size_t count,
                       std::vector<uint32_t>& out) {
  out.clear();
  const int component_type = indices_desc.value("componentType", 0);
  if (component_type != 5121 && component_type != 5123 && component_type != 5125) {
    return false;
  }
  const size_t component_size = componentTypeSize(component_type);
  const uint32_t buffer_view_index = indices_desc.value("bufferView", 0u);
  const Json* view = getBufferView(doc, buffer_view_index);
  if (view == nullptr) {
    return false;
  }
  const std::vector<std::uint8_t>* buffer = accessorBuffer(doc, *view);
  if (buffer == nullptr) {
    return false;
  }
  const size_t base_offset = view->value("byteOffset", 0u) +
                             indices_desc.value("byteOffset", 0u);
  const size_t stride = view->value("byteStride", static_cast<unsigned int>(component_size));
  if (stride < component_size) {
    return false;
  }
  out.resize(count);
  for (size_t i = 0; i < count; ++i) {
    const size_t element_offset = base_offset + i * stride;
    if (element_offset + component_size > buffer->size()) {
      out.clear();
      return false;
    }
    if (component_type == 5121) {
      out[i] = static_cast<uint32_t>((*buffer)[element_offset]);
    } else if (component_type == 5123) {
      std::uint16_t value = 0;
      std::memcpy(&value, buffer->data() + element_offset, sizeof(value));
      out[i] = static_cast<uint32_t>(value);
    } else {
      std::uint32_t value = 0;
      std::memcpy(&value, buffer->data() + element_offset, sizeof(value));
      out[i] = value;
    }
  }
  return true;
}

bool readSparseFloatValues(const GltfDocument& doc,
                           const Json& values_desc,
                           size_t component_count,
                           size_t count,
                           std::vector<float>& out) {
  out.clear();
  const uint32_t buffer_view_index = values_desc.value("bufferView", 0u);
  const Json* view = getBufferView(doc, buffer_view_index);
  if (view == nullptr) {
    return false;
  }
  const std::vector<std::uint8_t>* buffer = accessorBuffer(doc, *view);
  if (buffer == nullptr) {
    return false;
  }
  const size_t component_size = sizeof(float);
  const size_t base_offset = view->value("byteOffset", 0u) +
                             values_desc.value("byteOffset", 0u);
  const size_t stride = view->value(
      "byteStride", static_cast<unsigned int>(component_count * component_size));
  if (stride < component_count * component_size) {
    return false;
  }
  out.resize(count * component_count);
  for (size_t i = 0; i < count; ++i) {
    const size_t element_offset = base_offset + i * stride;
    if (element_offset + component_count * component_size > buffer->size()) {
      out.clear();
      return false;
    }
    for (size_t c = 0; c < component_count; ++c) {
      float value = 0.0f;
      std::memcpy(&value, buffer->data() + element_offset + c * component_size, sizeof(value));
      out[i * component_count + c] = value;
    }
  }
  return true;
}

bool readSparseIndexValues(const GltfDocument& doc,
                           const Json& values_desc,
                           int component_type,
                           size_t count,
                           std::vector<uint32_t>& out) {
  out.clear();
  const size_t component_size = componentTypeSize(component_type);
  if (component_size == 0) {
    return false;
  }
  const uint32_t buffer_view_index = values_desc.value("bufferView", 0u);
  const Json* view = getBufferView(doc, buffer_view_index);
  if (view == nullptr) {
    return false;
  }
  const std::vector<std::uint8_t>* buffer = accessorBuffer(doc, *view);
  if (buffer == nullptr) {
    return false;
  }
  const size_t base_offset = view->value("byteOffset", 0u) +
                             values_desc.value("byteOffset", 0u);
  const size_t stride = view->value("byteStride", static_cast<unsigned int>(component_size));
  if (stride < component_size) {
    return false;
  }
  out.resize(count);
  for (size_t i = 0; i < count; ++i) {
    const size_t element_offset = base_offset + i * stride;
    if (element_offset + component_size > buffer->size()) {
      out.clear();
      return false;
    }
    if (component_type == 5121) {
      out[i] = static_cast<uint32_t>((*buffer)[element_offset]);
    } else if (component_type == 5123) {
      std::uint16_t value = 0;
      std::memcpy(&value, buffer->data() + element_offset, sizeof(value));
      out[i] = static_cast<uint32_t>(value);
    } else {
      std::uint32_t value = 0;
      std::memcpy(&value, buffer->data() + element_offset, sizeof(value));
      out[i] = value;
    }
  }
  return true;
}

bool readSparseUnsignedValues(const GltfDocument& doc,
                              const Json& values_desc,
                              int component_type,
                              size_t component_count,
                              size_t count,
                              std::vector<uint32_t>& out) {
  out.clear();
  const size_t component_size = componentTypeSize(component_type);
  if (component_size == 0) {
    return false;
  }
  const uint32_t buffer_view_index = values_desc.value("bufferView", 0u);
  const Json* view = getBufferView(doc, buffer_view_index);
  if (view == nullptr) {
    return false;
  }
  const std::vector<std::uint8_t>* buffer = accessorBuffer(doc, *view);
  if (buffer == nullptr) {
    return false;
  }
  const size_t base_offset = view->value("byteOffset", 0u) +
                             values_desc.value("byteOffset", 0u);
  const size_t packed_size = component_count * component_size;
  const size_t stride = view->value("byteStride", static_cast<unsigned int>(packed_size));
  if (stride < packed_size) {
    return false;
  }
  out.resize(count * component_count);
  for (size_t i = 0; i < count; ++i) {
    const size_t element_offset = base_offset + i * stride;
    if (element_offset + packed_size > buffer->size()) {
      out.clear();
      return false;
    }
    for (size_t c = 0; c < component_count; ++c) {
      const size_t component_offset = element_offset + c * component_size;
      if (component_type == 5121) {
        out[i * component_count + c] = static_cast<uint32_t>((*buffer)[component_offset]);
      } else if (component_type == 5123) {
        std::uint16_t value = 0;
        std::memcpy(&value, buffer->data() + component_offset, sizeof(value));
        out[i * component_count + c] = static_cast<uint32_t>(value);
      } else {
        std::uint32_t value = 0;
        std::memcpy(&value, buffer->data() + component_offset, sizeof(value));
        out[i * component_count + c] = value;
      }
    }
  }
  return true;
}

bool readSparseWeightValues(const GltfDocument& doc,
                            const Json& values_desc,
                            int component_type,
                            bool normalized,
                            size_t component_count,
                            size_t count,
                            std::vector<float>& out) {
  out.clear();
  const size_t component_size = componentTypeSize(component_type);
  if (component_size == 0) {
    return false;
  }
  const uint32_t buffer_view_index = values_desc.value("bufferView", 0u);
  const Json* view = getBufferView(doc, buffer_view_index);
  if (view == nullptr) {
    return false;
  }
  const std::vector<std::uint8_t>* buffer = accessorBuffer(doc, *view);
  if (buffer == nullptr) {
    return false;
  }
  const size_t base_offset = view->value("byteOffset", 0u) +
                             values_desc.value("byteOffset", 0u);
  const size_t packed_size = component_count * component_size;
  const size_t stride = view->value("byteStride", static_cast<unsigned int>(packed_size));
  if (stride < packed_size) {
    return false;
  }
  out.resize(count * component_count);
  for (size_t i = 0; i < count; ++i) {
    const size_t element_offset = base_offset + i * stride;
    if (element_offset + packed_size > buffer->size()) {
      out.clear();
      return false;
    }
    for (size_t c = 0; c < component_count; ++c) {
      const size_t component_offset = element_offset + c * component_size;
      if (component_type == 5126) {
        float value = 0.0f;
        std::memcpy(&value, buffer->data() + component_offset, sizeof(value));
        out[i * component_count + c] = value;
      } else if (component_type == 5121) {
        const auto value = static_cast<float>((*buffer)[component_offset]);
        out[i * component_count + c] = normalized ? value / 255.0f : value;
      } else {
        std::uint16_t value = 0;
        std::memcpy(&value, buffer->data() + component_offset, sizeof(value));
        out[i * component_count + c] =
            normalized ? static_cast<float>(value) / 65535.0f : static_cast<float>(value);
      }
    }
  }
  return true;
}

}  // namespace

GltfDocument loadGltfDocument(const std::filesystem::path& path) {
  GltfDocument doc{};
  doc.source_path = path;
  std::vector<std::uint8_t> bytes = readFileBytes(path);
  if (bytes.empty()) {
    return doc;
  }

  constexpr std::uint32_t kGlbMagic = 0x46546C67u;
  constexpr std::uint32_t kJsonChunk = 0x4E4F534Au;
  constexpr std::uint32_t kBinChunk = 0x004E4942u;
  if (bytes.size() >= 12 && readU32LE(bytes, 0) == kGlbMagic && readU32LE(bytes, 4) == 2u) {
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
    if (!doc.bin.empty()) {
      doc.buffers.push_back(doc.bin);
    }
    return doc;
  }

  const std::string json_text(reinterpret_cast<const char*>(bytes.data()), bytes.size());
  doc.json = Json::parse(json_text, nullptr, false);
  if (doc.json.is_discarded()) {
    doc.json = Json{};
    return doc;
  }

  if (doc.json.contains("buffers") && doc.json["buffers"].is_array()) {
    doc.buffers.reserve(doc.json["buffers"].size());
    for (const Json& buffer : doc.json["buffers"]) {
      std::vector<std::uint8_t> data;
      const std::string uri = buffer.value("uri", std::string{});
      if (!uri.empty()) {
        if (startsWith(uri, "data:")) {
          data = decodeDataUri(uri);
        } else {
          data = readFileBytes(path.parent_path() / uri);
        }
      }
      doc.buffers.push_back(std::move(data));
    }
    if (!doc.buffers.empty()) {
      doc.bin = doc.buffers.front();
    }
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
  if (accessor.value("componentType", 0) != 5126) {
    return false;
  }
  const size_t component_count = gltfComponentCount(accessor.value("type", std::string{}));
  if (component_count == 0 ||
      (expected_components != 0 && component_count != expected_components)) {
    return false;
  }
  const size_t count = accessor.value("count", 0u);

  out.assign(count * component_count, 0.0f);
  if (accessor.contains("bufferView")) {
    const uint32_t buffer_view_index = accessor.value("bufferView", 0u);
    const Json* view = getBufferView(doc, buffer_view_index);
    if (view == nullptr) {
      return false;
    }
    const std::vector<std::uint8_t>* buffer = accessorBuffer(doc, *view);
    if (buffer == nullptr) {
      return false;
    }
    const size_t view_offset = view->value("byteOffset", 0u);
    const size_t accessor_offset = accessor.value("byteOffset", 0u);
    const size_t stride =
        view->value("byteStride", static_cast<unsigned int>(component_count * sizeof(float)));
    const size_t base_offset = view_offset + accessor_offset;
    if (stride < component_count * sizeof(float)) {
      return false;
    }

    for (size_t i = 0; i < count; ++i) {
      const size_t element_offset = base_offset + i * stride;
      if (element_offset + component_count * sizeof(float) > buffer->size()) {
        out.clear();
        return false;
      }
      for (size_t c = 0; c < component_count; ++c) {
        float value = 0.0f;
        std::memcpy(&value, buffer->data() + element_offset + c * sizeof(float), sizeof(float));
        out[i * component_count + c] = value;
      }
    }
  }

  if (accessor.contains("sparse")) {
    const Json& sparse = accessor["sparse"];
    const size_t sparse_count = sparse.value("count", 0u);
    std::vector<uint32_t> sparse_indices;
    std::vector<float> sparse_values;
    if (!sparse.contains("indices") || !sparse.contains("values") ||
        !readSparseIndices(doc, sparse["indices"], sparse_count, sparse_indices) ||
        !readSparseFloatValues(doc, sparse["values"], component_count, sparse_count,
                               sparse_values)) {
      out.clear();
      return false;
    }
    for (size_t i = 0; i < sparse_count; ++i) {
      const uint32_t dst = sparse_indices[i];
      if (dst >= count) {
        out.clear();
        return false;
      }
      for (size_t c = 0; c < component_count; ++c) {
        out[static_cast<size_t>(dst) * component_count + c] =
            sparse_values[i * component_count + c];
      }
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
      accessor.value("type", std::string{}) != "VEC4") {
    return false;
  }

  const size_t count = accessor.value("count", 0u);
  const size_t component_size =
      component_type == 5121 ? sizeof(std::uint8_t)
                             : component_type == 5123 ? sizeof(std::uint16_t)
                                                      : sizeof(std::uint32_t);
  const size_t component_count = 4;

  out.assign(count, glm::uvec4{0u});
  if (accessor.contains("bufferView")) {
    const uint32_t buffer_view_index = accessor.value("bufferView", 0u);
    const Json* view = getBufferView(doc, buffer_view_index);
    if (view == nullptr) {
      return false;
    }
    const std::vector<std::uint8_t>* buffer = accessorBuffer(doc, *view);
    if (buffer == nullptr) {
      return false;
    }
    const size_t view_offset = view->value("byteOffset", 0u);
    const size_t accessor_offset = accessor.value("byteOffset", 0u);
    const size_t stride =
        view->value("byteStride", static_cast<unsigned int>(component_count * component_size));
    const size_t base_offset = view_offset + accessor_offset;
    if (stride < component_count * component_size) {
      return false;
    }

    for (size_t i = 0; i < count; ++i) {
      const size_t element_offset = base_offset + i * stride;
      if (element_offset + component_count * component_size > buffer->size()) {
        out.clear();
        return false;
      }
      glm::uvec4 joints{0u};
      for (size_t c = 0; c < component_count; ++c) {
        const size_t component_offset = element_offset + c * component_size;
        if (component_type == 5121) {
          joints[static_cast<glm::length_t>(c)] =
              static_cast<uint32_t>((*buffer)[component_offset]);
        } else if (component_type == 5123) {
          std::uint16_t value = 0;
          std::memcpy(&value, buffer->data() + component_offset, sizeof(value));
          joints[static_cast<glm::length_t>(c)] = static_cast<uint32_t>(value);
        } else {
          std::uint32_t value = 0;
          std::memcpy(&value, buffer->data() + component_offset, sizeof(value));
          joints[static_cast<glm::length_t>(c)] = value;
        }
      }
      out[i] = joints;
    }
  }

  if (accessor.contains("sparse")) {
    const Json& sparse = accessor["sparse"];
    const size_t sparse_count = sparse.value("count", 0u);
    std::vector<uint32_t> sparse_indices;
    std::vector<uint32_t> sparse_values;
    if (!sparse.contains("indices") || !sparse.contains("values") ||
        !readSparseIndices(doc, sparse["indices"], sparse_count, sparse_indices) ||
        !readSparseUnsignedValues(doc, sparse["values"], component_type, component_count,
                                  sparse_count, sparse_values)) {
      out.clear();
      return false;
    }
    for (size_t i = 0; i < sparse_count; ++i) {
      const uint32_t dst = sparse_indices[i];
      if (dst >= count) {
        out.clear();
        return false;
      }
      glm::uvec4 joints{0u};
      for (size_t c = 0; c < component_count; ++c) {
        joints[static_cast<glm::length_t>(c)] = sparse_values[i * component_count + c];
      }
      out[dst] = joints;
    }
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
      accessor.value("type", std::string{}) != "VEC4") {
    return false;
  }

  const size_t count = accessor.value("count", 0u);
  const size_t component_size =
      component_type == 5126 ? sizeof(float)
                             : component_type == 5121 ? sizeof(std::uint8_t)
                                                      : sizeof(std::uint16_t);
  const size_t component_count = 4;
  const bool normalized = accessor.value("normalized", false);
  out.assign(count, glm::vec4{0.0f});
  if (accessor.contains("bufferView")) {
    const uint32_t buffer_view_index = accessor.value("bufferView", 0u);
    const Json* view = getBufferView(doc, buffer_view_index);
    if (view == nullptr) {
      return false;
    }
    const std::vector<std::uint8_t>* buffer = accessorBuffer(doc, *view);
    if (buffer == nullptr) {
      return false;
    }
    const size_t view_offset = view->value("byteOffset", 0u);
    const size_t accessor_offset = accessor.value("byteOffset", 0u);
    const size_t stride =
        view->value("byteStride", static_cast<unsigned int>(component_count * component_size));
    const size_t base_offset = view_offset + accessor_offset;
    if (stride < component_count * component_size) {
      return false;
    }

    for (size_t i = 0; i < count; ++i) {
      const size_t element_offset = base_offset + i * stride;
      if (element_offset + component_count * component_size > buffer->size()) {
        out.clear();
        return false;
      }
      glm::vec4 weights{0.0f};
      for (size_t c = 0; c < component_count; ++c) {
        const size_t component_offset = element_offset + c * component_size;
        if (component_type == 5126) {
          float value = 0.0f;
          std::memcpy(&value, buffer->data() + component_offset, sizeof(value));
          weights[static_cast<glm::length_t>(c)] = value;
        } else if (component_type == 5121) {
          const auto value = static_cast<float>((*buffer)[component_offset]);
          weights[static_cast<glm::length_t>(c)] = normalized ? value / 255.0f : value;
        } else {
          std::uint16_t value = 0;
          std::memcpy(&value, buffer->data() + component_offset, sizeof(value));
          weights[static_cast<glm::length_t>(c)] =
              normalized ? static_cast<float>(value) / 65535.0f : static_cast<float>(value);
        }
      }
      out[i] = weights;
    }
  }

  if (accessor.contains("sparse")) {
    const Json& sparse = accessor["sparse"];
    const size_t sparse_count = sparse.value("count", 0u);
    std::vector<uint32_t> sparse_indices;
    std::vector<float> sparse_values;
    if (!sparse.contains("indices") || !sparse.contains("values") ||
        !readSparseIndices(doc, sparse["indices"], sparse_count, sparse_indices) ||
        !readSparseWeightValues(doc, sparse["values"], component_type, normalized,
                                component_count, sparse_count, sparse_values)) {
      out.clear();
      return false;
    }
    for (size_t i = 0; i < sparse_count; ++i) {
      const uint32_t dst = sparse_indices[i];
      if (dst >= count) {
        out.clear();
        return false;
      }
      glm::vec4 weights{0.0f};
      for (size_t c = 0; c < component_count; ++c) {
        weights[static_cast<glm::length_t>(c)] = sparse_values[i * component_count + c];
      }
      out[dst] = weights;
    }
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
      accessor.value("type", std::string{}) != "SCALAR") {
    return false;
  }

  const size_t count = accessor.value("count", 0u);
  const size_t component_size =
      component_type == 5121 ? sizeof(std::uint8_t)
                             : component_type == 5123 ? sizeof(std::uint16_t)
                                                      : sizeof(std::uint32_t);

  out.assign(count, 0u);
  if (accessor.contains("bufferView")) {
    const uint32_t buffer_view_index = accessor.value("bufferView", 0u);
    const Json* view = getBufferView(doc, buffer_view_index);
    if (view == nullptr) {
      return false;
    }
    const std::vector<std::uint8_t>* buffer = accessorBuffer(doc, *view);
    if (buffer == nullptr) {
      return false;
    }
    const size_t view_offset = view->value("byteOffset", 0u);
    const size_t accessor_offset = accessor.value("byteOffset", 0u);
    const size_t stride = view->value("byteStride", static_cast<unsigned int>(component_size));
    const size_t base_offset = view_offset + accessor_offset;
    if (stride < component_size) {
      return false;
    }

    for (size_t i = 0; i < count; ++i) {
      const size_t element_offset = base_offset + i * stride;
      if (element_offset + component_size > buffer->size()) {
        out.clear();
        return false;
      }
      if (component_type == 5121) {
        out[i] = static_cast<uint32_t>((*buffer)[element_offset]);
      } else if (component_type == 5123) {
        std::uint16_t value = 0;
        std::memcpy(&value, buffer->data() + element_offset, sizeof(value));
        out[i] = static_cast<uint32_t>(value);
      } else {
        std::uint32_t value = 0;
        std::memcpy(&value, buffer->data() + element_offset, sizeof(value));
        out[i] = value;
      }
    }
  }

  if (accessor.contains("sparse")) {
    const Json& sparse = accessor["sparse"];
    const size_t sparse_count = sparse.value("count", 0u);
    std::vector<uint32_t> sparse_indices;
    std::vector<uint32_t> sparse_values;
    if (!sparse.contains("indices") || !sparse.contains("values") ||
        !readSparseIndices(doc, sparse["indices"], sparse_count, sparse_indices) ||
        !readSparseIndexValues(doc, sparse["values"], component_type, sparse_count,
                               sparse_values)) {
      out.clear();
      return false;
    }
    for (size_t i = 0; i < sparse_count; ++i) {
      const uint32_t dst = sparse_indices[i];
      if (dst >= count) {
        out.clear();
        return false;
      }
      out[dst] = sparse_values[i];
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
