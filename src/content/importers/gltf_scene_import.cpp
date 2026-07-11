#include "gltf_scene_import_internal.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <assimp/GltfMaterial.h>
#include <assimp/Importer.hpp>
#include <assimp/config.h>
#include <assimp/material.h>
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#include <meshoptimizer.h>
#include <spdlog/spdlog.h>

#include "karma/assets.h"
#include "karma/core.h"
#include "../assets/asset_texture_internal.h"
#include "gltf_document.h"
#include "gltf_scene_animation_import.h"
#include "gltf_scene_mesh_import.h"
#include "gltf_scene_skinning.h"
#include "karma/components.h"
#include "karma/world.h"

namespace karma::world {

namespace {

std::string normalizedTextureStem(std::string_view name) {
  const size_t separator = name.find_last_of("/\\");
  if (separator != std::string_view::npos) {
    name.remove_prefix(separator + 1u);
  }
  if (const size_t extension = name.find_last_of('.');
      extension != std::string_view::npos) {
    name = name.substr(0u, extension);
  }

  std::string normalized;
  normalized.reserve(name.size());
  bool pending_separator = false;
  for (const char c : name) {
    const unsigned char value = static_cast<unsigned char>(c);
    if (std::isalnum(value) != 0) {
      if (pending_separator && !normalized.empty()) {
        normalized.push_back('_');
      }
      normalized.push_back(static_cast<char>(std::tolower(value)));
      pending_separator = false;
    } else {
      pending_separator = true;
    }
  }
  return normalized;
}

std::string compactTextureStem(std::string_view normalized) {
  std::string compact;
  compact.reserve(normalized.size());
  for (const char c : normalized) {
    if (c != '_') {
      compact.push_back(c);
    }
  }
  return compact;
}

bool endsWith(std::string_view value, std::string_view suffix) {
  return value.size() >= suffix.size() &&
         value.substr(value.size() - suffix.size()) == suffix;
}

int packedMetallicRoughnessNameScore(std::string_view name) {
  const std::string normalized = normalizedTextureStem(name);
  if (normalized.empty()) {
    return 0;
  }

  int score = 0;
  size_t token_begin = 0u;
  while (token_begin < normalized.size()) {
    const size_t token_end = normalized.find('_', token_begin);
    const std::string_view token(
        normalized.data() + token_begin,
        (token_end == std::string::npos ? normalized.size() : token_end) - token_begin);
    if (token == "orm" || token == "arm") {
      score = std::max(score, 4);
    } else if (token == "rm") {
      score = std::max(score, 3);
    }
    if (token_end == std::string::npos) {
      break;
    }
    token_begin = token_end + 1u;
  }

  const std::string compact = compactTextureStem(normalized);
  if (endsWith(compact, "ambientocclusionroughnessmetallic") ||
      endsWith(compact, "occlusionroughnessmetallic")) {
    score = std::max(score, 6);
  } else if (endsWith(compact, "metallicroughness") ||
             endsWith(compact, "metalnessroughness")) {
    score = std::max(score, 5);
  }
  return score;
}

std::string textureFamilyKey(std::string_view name) {
  std::string normalized = normalizedTextureStem(name);
  if (normalized.empty()) {
    return {};
  }

  constexpr std::string_view kSemanticTokens[] = {
      "basecolor", "basecolour", "diffusecolor", "albedo", "diffuse",
      "normalmap", "normal", "normals", "nrm", "roughness", "rough",
      "metallic", "metalness", "metal", "specular", "shininessexponent",
      "shininess", "occlusion", "ambientocclusion", "ambient", "orm", "arm",
      "rm", "ao",
  };
  while (!normalized.empty()) {
    const size_t separator = normalized.find_last_of('_');
    const std::string_view token =
        separator == std::string::npos
            ? std::string_view(normalized)
            : std::string_view(normalized).substr(separator + 1u);
    bool has_semantic_suffix = false;
    for (const std::string_view semantic : kSemanticTokens) {
      if (semantic == token) {
        has_semantic_suffix = true;
        break;
      }
    }
    if (!has_semantic_suffix) {
      break;
    }
    normalized.resize(separator == std::string::npos ? 0u : separator);
  }

  std::string compact = compactTextureStem(normalized);
  constexpr std::string_view kCompoundSuffixes[] = {
      "ambientocclusionroughnessmetallic", "occlusionroughnessmetallic",
      "metallicroughness", "metalnessroughness", "shininessexponent",
      "diffusecolor", "basecolour", "basecolor", "normalmap",
  };
  for (const std::string_view suffix : kCompoundSuffixes) {
    if (compact.size() > suffix.size() && endsWith(compact, suffix)) {
      compact.resize(compact.size() - suffix.size());
      break;
    }
  }
  return compact;
}

}  // namespace

namespace detail {

bool canonicalizeAssimpEmbeddedTexture(std::span<const uint8_t> bgra,
                                       uint32_t width,
                                       uint32_t height,
                                       std::vector<uint8_t>& rgba) {
  rgba.clear();
  if (width == 0u || height == 0u ||
      width > static_cast<uint32_t>(std::numeric_limits<int>::max()) ||
      height > static_cast<uint32_t>(std::numeric_limits<int>::max())) {
    return false;
  }
  const uint64_t byte_count = static_cast<uint64_t>(width) * height * 4u;
  if (byte_count > static_cast<uint64_t>(std::numeric_limits<size_t>::max()) ||
      bgra.size() < static_cast<size_t>(byte_count)) {
    return false;
  }

  rgba.resize(static_cast<size_t>(byte_count));
  const size_t row_bytes = static_cast<size_t>(width) * 4u;
  for (uint32_t output_y = 0u; output_y < height; ++output_y) {
    const uint32_t source_y = height - output_y - 1u;
    const uint8_t* source_row = bgra.data() + static_cast<size_t>(source_y) * row_bytes;
    uint8_t* output_row = rgba.data() + static_cast<size_t>(output_y) * row_bytes;
    for (uint32_t x = 0u; x < width; ++x) {
      const size_t pixel = static_cast<size_t>(x) * 4u;
      output_row[pixel + 0u] = source_row[pixel + 2u];
      output_row[pixel + 1u] = source_row[pixel + 1u];
      output_row[pixel + 2u] = source_row[pixel + 0u];
      output_row[pixel + 3u] = source_row[pixel + 3u];
    }
  }
  return true;
}

bool isPackedMetallicRoughnessTextureName(std::string_view name) {
  return packedMetallicRoughnessNameScore(name) > 0;
}

}  // namespace detail

namespace {

bool envFlagEnabled(const char* value) {
  if (value == nullptr || value[0] == '\0') {
    return false;
  }
  return std::strcmp(value, "0") != 0 &&
         std::strcmp(value, "false") != 0 &&
         std::strcmp(value, "FALSE") != 0 &&
         std::strcmp(value, "off") != 0 &&
         std::strcmp(value, "OFF") != 0;
}

bool startupDiagnosticsEnabled() {
  static const bool enabled = envFlagEnabled(std::getenv("KARMA_ENGINE_STARTUP_DIAG"));
  return enabled;
}

constexpr std::uint32_t kGlbMagic = 0x46546C67u;
constexpr std::uint32_t kGlbJsonChunk = 0x4E4F534Au;
constexpr std::uint32_t kGlbBinChunk = 0x004E4942u;

size_t align4(size_t value) {
  return (value + 3u) & ~size_t{3u};
}

void writeU32LE(std::ostream& stream, std::uint32_t value) {
  const unsigned char bytes[] = {
      static_cast<unsigned char>(value & 0xFFu),
      static_cast<unsigned char>((value >> 8u) & 0xFFu),
      static_cast<unsigned char>((value >> 16u) & 0xFFu),
      static_cast<unsigned char>((value >> 24u) & 0xFFu),
  };
  stream.write(reinterpret_cast<const char*>(bytes), sizeof(bytes));
}

void removeJsonString(Json& array, std::string_view value) {
  if (!array.is_array()) {
    return;
  }
  for (auto it = array.begin(); it != array.end();) {
    if (it->is_string() && it->get<std::string>() == value) {
      it = array.erase(it);
    } else {
      ++it;
    }
  }
}

const std::vector<std::uint8_t>* documentBuffer(const GltfDocument& doc,
                                                std::uint32_t index) {
  if (index < doc.buffers.size()) {
    return &doc.buffers[index];
  }
  if (index == 0u && !doc.bin.empty()) {
    return &doc.bin;
  }
  return nullptr;
}

std::string sanitizedStem(const std::filesystem::path& path) {
  std::string stem = path.stem().string();
  for (char& c : stem) {
    const unsigned char value = static_cast<unsigned char>(c);
    if (std::isalnum(value) == 0 && c != '_' && c != '-') {
      c = '_';
    }
  }
  return stem.empty() ? "scene" : stem;
}

std::filesystem::path meshoptDecodedTempPath(const std::filesystem::path& source) {
  std::error_code ec;
  std::filesystem::path temp_root = std::filesystem::temp_directory_path(ec);
  if (ec || temp_root.empty()) {
    temp_root = std::filesystem::current_path(ec);
  }
  const std::filesystem::path dir = temp_root / "karma_gltf_meshopt_decode";
  std::filesystem::create_directories(dir, ec);

  std::string key = source.lexically_normal().string();
  if (const auto size = std::filesystem::file_size(source, ec); !ec) {
    key += ":" + std::to_string(size);
  }
  if (const auto stamp = std::filesystem::last_write_time(source, ec); !ec) {
    key += ":" + std::to_string(stamp.time_since_epoch().count());
  }
  return dir / (sanitizedStem(source) + "_" +
                std::to_string(std::hash<std::string>{}(key)) + ".glb");
}

bool applyMeshoptFilter(std::string_view filter,
                        std::vector<std::uint8_t>& data,
                        size_t count,
                        size_t stride) {
  if (filter.empty()) {
    return true;
  }
  if (filter == "OCTAHEDRAL") {
    meshopt_decodeFilterOct(data.data(), count, stride);
    return true;
  }
  if (filter == "QUATERNION") {
    meshopt_decodeFilterQuat(data.data(), count, stride);
    return true;
  }
  if (filter == "EXPONENTIAL") {
    meshopt_decodeFilterExp(data.data(), count, stride);
    return true;
  }
  if (filter == "COLOR") {
    meshopt_decodeFilterColor(data.data(), count, stride);
    return true;
  }
  return false;
}

bool decodeMeshoptBufferView(const GltfDocument& doc,
                             const Json& view,
                             const Json& extension,
                             std::vector<std::uint8_t>& out) {
  const std::uint32_t source_buffer_index = extension.value("buffer", 0u);
  const std::vector<std::uint8_t>* source_buffer =
      documentBuffer(doc, source_buffer_index);
  if (source_buffer == nullptr) {
    return false;
  }

  const size_t source_offset = extension.value("byteOffset", size_t{0});
  const size_t source_length = extension.value("byteLength", size_t{0});
  if (source_offset > source_buffer->size() ||
      source_length > source_buffer->size() - source_offset) {
    return false;
  }

  const size_t count = extension.value("count", size_t{0});
  const size_t stride =
      extension.value("byteStride", view.value("byteStride", size_t{0}));
  size_t decoded_size = view.value("byteLength", size_t{0});
  if (decoded_size == 0 && count > 0 && stride > 0 &&
      count <= std::numeric_limits<size_t>::max() / stride) {
    decoded_size = count * stride;
  }
  if (count == 0 || stride == 0 || decoded_size == 0 ||
      count > std::numeric_limits<size_t>::max() / stride ||
      count * stride > decoded_size) {
    return false;
  }

  out.assign(decoded_size, 0u);
  const unsigned char* encoded =
      reinterpret_cast<const unsigned char*>(source_buffer->data() + source_offset);
  const std::string mode = extension.value("mode", std::string{});
  int result = -1;
  if (mode == "ATTRIBUTES") {
    result = meshopt_decodeVertexBuffer(out.data(),
                                        count,
                                        stride,
                                        encoded,
                                        source_length);
    if (result == 0 &&
        !applyMeshoptFilter(extension.value("filter", std::string{}),
                            out,
                            count,
                            stride)) {
      return false;
    }
  } else if (mode == "TRIANGLES") {
    result = meshopt_decodeIndexBuffer(out.data(),
                                       count,
                                       stride,
                                       encoded,
                                       source_length);
  } else if (mode == "INDICES") {
    result = meshopt_decodeIndexSequence(out.data(),
                                         count,
                                         stride,
                                         encoded,
                                         source_length);
  } else {
    return false;
  }
  return result == 0;
}

bool copyPlainBufferView(const GltfDocument& doc,
                         const Json& view,
                         std::vector<std::uint8_t>& out) {
  const std::uint32_t buffer_index = view.value("buffer", 0u);
  const std::vector<std::uint8_t>* source_buffer =
      documentBuffer(doc, buffer_index);
  if (source_buffer == nullptr) {
    return false;
  }

  const size_t source_offset = view.value("byteOffset", size_t{0});
  const size_t source_length = view.value("byteLength", size_t{0});
  if (source_offset > source_buffer->size() ||
      source_length > source_buffer->size() - source_offset) {
    return false;
  }

  out.assign(source_buffer->begin() + static_cast<std::ptrdiff_t>(source_offset),
             source_buffer->begin() +
                 static_cast<std::ptrdiff_t>(source_offset + source_length));
  return true;
}

bool writeGlb(const std::filesystem::path& path,
              const Json& json,
              const std::vector<std::uint8_t>& bin) {
  std::string json_text = json.dump();
  const size_t json_padded_size = align4(json_text.size());
  const size_t bin_padded_size = align4(bin.size());
  const size_t total_size =
      12u + 8u + json_padded_size + 8u + bin_padded_size;
  if (total_size > std::numeric_limits<std::uint32_t>::max()) {
    return false;
  }

  std::ofstream output(path, std::ios::binary);
  if (!output) {
    return false;
  }
  writeU32LE(output, kGlbMagic);
  writeU32LE(output, 2u);
  writeU32LE(output, static_cast<std::uint32_t>(total_size));
  writeU32LE(output, static_cast<std::uint32_t>(json_padded_size));
  writeU32LE(output, kGlbJsonChunk);
  output.write(json_text.data(), static_cast<std::streamsize>(json_text.size()));
  for (size_t i = json_text.size(); i < json_padded_size; ++i) {
    output.put(' ');
  }
  writeU32LE(output, static_cast<std::uint32_t>(bin_padded_size));
  writeU32LE(output, kGlbBinChunk);
  if (!bin.empty()) {
    output.write(reinterpret_cast<const char*>(bin.data()),
                 static_cast<std::streamsize>(bin.size()));
  }
  for (size_t i = bin.size(); i < bin_padded_size; ++i) {
    output.put('\0');
  }
  return output.good();
}

std::optional<std::filesystem::path> writeMeshoptDecodedGltf(
    const std::filesystem::path& source_path) {
  GltfDocument doc = loadGltfDocument(source_path);
  if (!doc.valid() || doc.bin.empty() ||
      !doc.json.contains("bufferViews") ||
      !doc.json["bufferViews"].is_array()) {
    return std::nullopt;
  }

  Json decoded_json = doc.json;
  Json& buffer_views = decoded_json["bufferViews"];
  std::vector<std::uint8_t> decoded_bin;
  bool decoded_any = false;

  for (Json& view : buffer_views) {
    if (!view.is_object()) {
      return std::nullopt;
    }

    const Json* meshopt_extension = nullptr;
    if (view.contains("extensions") && view["extensions"].is_object() &&
        view["extensions"].contains("EXT_meshopt_compression") &&
        view["extensions"]["EXT_meshopt_compression"].is_object()) {
      meshopt_extension = &view["extensions"]["EXT_meshopt_compression"];
    }

    std::vector<std::uint8_t> view_bytes;
    if (meshopt_extension != nullptr) {
      if (!decodeMeshoptBufferView(doc, view, *meshopt_extension, view_bytes)) {
        spdlog::warn("Failed to decode EXT_meshopt_compression bufferView in {}",
                     source_path.string());
        return std::nullopt;
      }
      view["extensions"].erase("EXT_meshopt_compression");
      if (view["extensions"].empty()) {
        view.erase("extensions");
      }
      decoded_any = true;
    } else if (!copyPlainBufferView(doc, view, view_bytes)) {
      return std::nullopt;
    }

    decoded_bin.resize(align4(decoded_bin.size()), 0u);
    view["buffer"] = 0u;
    view["byteOffset"] = decoded_bin.size();
    view["byteLength"] = view_bytes.size();
    decoded_bin.insert(decoded_bin.end(), view_bytes.begin(), view_bytes.end());
  }

  if (!decoded_any) {
    return std::nullopt;
  }

  decoded_json["buffers"] =
      Json::array({Json{{"byteLength", decoded_bin.size()}}});
  if (decoded_json.contains("extensionsRequired")) {
    removeJsonString(decoded_json["extensionsRequired"],
                     "EXT_meshopt_compression");
    if (decoded_json["extensionsRequired"].empty()) {
      decoded_json.erase("extensionsRequired");
    }
  }
  if (decoded_json.contains("extensionsUsed")) {
    removeJsonString(decoded_json["extensionsUsed"], "EXT_meshopt_compression");
    if (decoded_json["extensionsUsed"].empty()) {
      decoded_json.erase("extensionsUsed");
    }
  }

  const std::filesystem::path decoded_path = meshoptDecodedTempPath(source_path);
  if (!writeGlb(decoded_path, decoded_json, decoded_bin)) {
    return std::nullopt;
  }
  return decoded_path;
}

void logGltfStartupDiag(const std::filesystem::path& path,
                        const char* stage,
                        core::SteadyClock::time_point start,
                        core::SteadyClock::time_point end) {
  if (!startupDiagnosticsEnabled()) {
    return;
  }
  spdlog::info("Engine startup diag: area=gltf_scene_load path='{}' stage={} ms={:.2f}",
               path.string(),
               stage ? stage : "unknown",
               core::elapsedMilliseconds(start, end));
}

void logGltfStartupDiag(const std::filesystem::path& path,
                        const char* stage,
                        core::SteadyClock::time_point start,
                        core::SteadyClock::time_point end,
                        std::size_t count) {
  if (!startupDiagnosticsEnabled()) {
    return;
  }
  spdlog::info(
      "Engine startup diag: area=gltf_scene_load path='{}' stage={} ms={:.2f} count={}",
      path.string(),
      stage ? stage : "unknown",
      core::elapsedMilliseconds(start, end),
      count);
}

math::Vec3 toVec3(const aiVector3D& v) {
  return {v.x, v.y, v.z};
}

math::Quat toQuat(const aiQuaternion& q) {
  return {q.x, q.y, q.z, q.w};
}

std::string safeName(std::string_view base, std::string_view fallback) {
  return base.empty() ? std::string(fallback) : std::string(base);
}

world::MeshData buildMeshData(const aiMesh& mesh) {
  world::MeshData out{};
  out.vertices.reserve(mesh.mNumVertices);
  out.normals.reserve(mesh.mNumVertices);
  out.uvs.reserve(mesh.mNumVertices);
  out.uvs1.reserve(mesh.mNumVertices);
  out.tangents.reserve(mesh.mNumVertices);

  for (unsigned int v = 0; v < mesh.mNumVertices; ++v) {
    const auto& pos = mesh.mVertices[v];
    out.vertices.emplace_back(pos.x, pos.y, pos.z);

    if (mesh.HasNormals()) {
      const auto& n = mesh.mNormals[v];
      out.normals.emplace_back(n.x, n.y, n.z);
    } else {
      out.normals.emplace_back(0.0f, 1.0f, 0.0f);
    }

    if (mesh.HasTextureCoords(0)) {
      const auto& uv = mesh.mTextureCoords[0][v];
      out.uvs.emplace_back(uv.x, uv.y);
    } else {
      out.uvs.emplace_back(0.0f, 0.0f);
    }

    if (mesh.HasTextureCoords(1)) {
      const auto& uv = mesh.mTextureCoords[1][v];
      out.uvs1.emplace_back(uv.x, uv.y);
    } else {
      out.uvs1.emplace_back(out.uvs.back());
    }

    if (mesh.HasTangentsAndBitangents()) {
      const auto& t = mesh.mTangents[v];
      const auto& b = mesh.mBitangents[v];
      const auto& n = mesh.mNormals[v];
      const glm::vec3 tangent{t.x, t.y, t.z};
      const glm::vec3 bitangent{b.x, b.y, b.z};
      const glm::vec3 normal{n.x, n.y, n.z};
      const float sign =
          glm::dot(glm::cross(normal, tangent), bitangent) < 0.0f ? -1.0f : 1.0f;
      out.tangents.emplace_back(tangent.x, tangent.y, tangent.z, sign);
    } else {
      out.tangents.emplace_back(1.0f, 0.0f, 0.0f, 1.0f);
    }
  }

  for (unsigned int f = 0; f < mesh.mNumFaces; ++f) {
    const aiFace& face = mesh.mFaces[f];
    if (face.mNumIndices != 3) {
      continue;
    }
    out.indices.push_back(face.mIndices[0]);
    out.indices.push_back(face.mIndices[1]);
    out.indices.push_back(face.mIndices[2]);
  }

  return out;
}

rendering::MaterialDesc buildMaterialDesc(const aiMaterial& material) {
  rendering::MaterialDesc desc{};
  desc.base_color = {1.0f, 1.0f, 1.0f, 1.0f};

  aiColor4D base_color(1.0f, 1.0f, 1.0f, 1.0f);
  if (material.Get(AI_MATKEY_BASE_COLOR, base_color) == AI_SUCCESS) {
    desc.base_color = {base_color.r, base_color.g, base_color.b, base_color.a};
  } else {
    aiColor3D diffuse(1.0f, 1.0f, 1.0f);
    if (material.Get(AI_MATKEY_COLOR_DIFFUSE, diffuse) == AI_SUCCESS) {
      desc.base_color = {diffuse.r, diffuse.g, diffuse.b, 1.0f};
    }
  }

  float opacity = desc.base_color.a;
  if (material.Get(AI_MATKEY_OPACITY, opacity) == AI_SUCCESS) {
    desc.base_color.a = opacity;
  }

  int two_sided = 0;
  if (material.Get(AI_MATKEY_TWOSIDED, two_sided) == AI_SUCCESS) {
    desc.double_sided = two_sided != 0;
  }

  if (float specular_factor = desc.specular_factor;
      material.Get(AI_MATKEY_SPECULAR_FACTOR, specular_factor) == AI_SUCCESS) {
    desc.specular_factor = std::clamp(specular_factor, 0.0f, 1.0f);
  }
  if (aiColor3D specular_color(1.0f, 1.0f, 1.0f);
      material.Get(AI_MATKEY_COLOR_SPECULAR, specular_color) == AI_SUCCESS) {
    desc.specular_color = {
        std::clamp(specular_color.r, 0.0f, 1.0f),
        std::clamp(specular_color.g, 0.0f, 1.0f),
        std::clamp(specular_color.b, 0.0f, 1.0f), 1.0f};
  }

  if (float alpha_cutoff = desc.alpha_cutoff;
      material.Get(AI_MATKEY_GLTF_ALPHACUTOFF, alpha_cutoff) == AI_SUCCESS) {
    desc.alpha_cutoff = alpha_cutoff;
  }

  aiString alpha_mode;
  const bool has_gltf_alpha_mode =
      material.Get(AI_MATKEY_GLTF_ALPHAMODE, alpha_mode) == AI_SUCCESS;
  const std::string alpha_mode_value = has_gltf_alpha_mode ? alpha_mode.C_Str() : "";
  if (alpha_mode_value == "MASK" || alpha_mode_value == "mask") {
    desc.alpha_mode = rendering::MaterialDesc::AlphaMode::Masked;
    desc.transparent = false;
  } else if (alpha_mode_value == "BLEND" || alpha_mode_value == "blend") {
    desc.alpha_mode = rendering::MaterialDesc::AlphaMode::Blend;
    desc.transparent = true;
    desc.depth_write = false;
  } else {
    desc.transparent = desc.base_color.a < 0.999f;
    if (desc.transparent) {
      desc.alpha_mode = rendering::MaterialDesc::AlphaMode::Blend;
      desc.depth_write = false;
    }
  }
  return desc;
}

rendering::MaterialDesc buildMaterialDesc(const aiScene& scene, const aiMesh& mesh) {
  if (mesh.mMaterialIndex >= scene.mNumMaterials ||
      scene.mMaterials[mesh.mMaterialIndex] == nullptr) {
    return {};
  }
  return buildMaterialDesc(*scene.mMaterials[mesh.mMaterialIndex]);
}

enum ImportedTextureCoordSlot : size_t {
  kTexCoordBaseColor = 0,
  kTexCoordNormal = 1,
  kTexCoordMetallicRoughness = 2,
  kTexCoordOcclusion = 3,
  kTexCoordEmissive = 4,
  kTexCoordClearcoat = 5,
  kTexCoordClearcoatRoughness = 6,
  kTexCoordClearcoatNormal = 7,
  kTexCoordSheenColor = 8,
  kTexCoordSheenRoughness = 9,
  kTexCoordTransmission = 10,
  kTexCoordThickness = 11,
  kTexCoordSpecular = 12,
  kTexCoordSpecularColor = 13,
};

int embeddedTextureIndex(const std::string& raw_key) {
  if (raw_key.size() < 2 || raw_key[0] != '*') {
    return -1;
  }
  char* end = nullptr;
  const long parsed = std::strtol(raw_key.c_str() + 1, &end, 10);
  if (end == nullptr || *end != '\0' || parsed < 0 ||
      parsed > static_cast<long>(std::numeric_limits<int>::max())) {
    return -1;
  }
  return static_cast<int>(parsed);
}

void setImportedTextureCoordTransform(rendering::ImportedMaterialData& data,
                                      const aiMaterial& material,
                                      unsigned int texture_type,
                                      unsigned int texture_index,
                                      unsigned int uv_index,
                                      size_t slot) {
  if (slot >= rendering::kImportedMaterialTextureCoordSlotCount) {
    return;
  }

  const auto type = static_cast<aiTextureType>(texture_type);
  aiUVTransform transform;
  transform.mTranslation = aiVector2D(0.0f, 0.0f);
  transform.mScaling = aiVector2D(1.0f, 1.0f);
  transform.mRotation = 0.0f;
  material.Get(AI_MATKEY_UVTRANSFORM(type, texture_index), transform);

  const float sx = transform.mScaling.x;
  const float sy = transform.mScaling.y;
  const float c = std::cos(transform.mRotation);
  const float s = std::sin(transform.mRotation);
  const float tx = transform.mTranslation.x;
  const float ty = transform.mTranslation.y;

  data.texcoord_row0[slot] =
      glm::vec4(c * sx, -s * sy, -0.5f * c + 0.5f * s + 0.5f + tx,
                uv_index > 0u ? 1.0f : 0.0f);
  data.texcoord_row1[slot] =
      glm::vec4(s * sx, c * sy, -0.5f * s - 0.5f * c + 0.5f + ty, 0.0f);
}

std::filesystem::path resolveImportedTexturePath(
    const std::filesystem::path& asset_path,
    const std::string& raw_key) {
  const std::filesystem::path raw_path = raw_key;
  std::error_code ec;
  if (raw_path.is_absolute() && std::filesystem::exists(raw_path, ec)) {
    return raw_path;
  }

  const std::filesystem::path base_dir = asset_path.parent_path();
  const std::filesystem::path direct = base_dir / raw_path;
  if (std::filesystem::exists(direct, ec)) {
    return direct;
  }

  const std::filesystem::path filename = raw_path.filename();
  if (!filename.empty()) {
    const std::filesystem::path texture_dir = base_dir / "textures" / filename;
    if (std::filesystem::exists(texture_dir, ec)) {
      return texture_dir;
    }

    const std::filesystem::path sibling = base_dir / filename;
    if (std::filesystem::exists(sibling, ec)) {
      return sibling;
    }

    const std::filesystem::path fbx_media_dir =
        base_dir / (asset_path.stem().string() + ".fbm") / filename;
    if (std::filesystem::exists(fbx_media_dir, ec)) {
      return fbx_media_dir;
    }
  }

  return direct;
}

bool populateImportedTextureSource(rendering::ImportedMaterialTexture& texture,
                                   const aiScene& scene,
                                   const std::filesystem::path& asset_path,
                                   const std::string& raw_key) {
  texture.raw_name = raw_key;
  texture.embedded = !raw_key.empty() && raw_key[0] == '*';
  if (!texture.embedded) {
    texture.resolved_path = resolveImportedTexturePath(asset_path, raw_key);
    texture.source_key = texture.resolved_path.string();
    return true;
  }

  const int texture_idx = embeddedTextureIndex(raw_key);
  if (texture_idx < 0 || texture_idx >= static_cast<int>(scene.mNumTextures) ||
      scene.mTextures[texture_idx] == nullptr) {
    return false;
  }

  texture.source_key = asset_path.string() + ":" + raw_key;
  const aiTexture& embedded_texture = *scene.mTextures[texture_idx];
  if (embedded_texture.mHeight == 0) {
    texture.compressed = true;
    texture.source_bytes.resize(static_cast<size_t>(embedded_texture.mWidth));
    if (!texture.source_bytes.empty()) {
      std::memcpy(texture.source_bytes.data(),
                  embedded_texture.pcData,
                  texture.source_bytes.size());
    }
    return true;
  }

  texture.compressed = false;
  texture.width = embedded_texture.mWidth;
  texture.height = embedded_texture.mHeight;
  const uint64_t byte_count = static_cast<uint64_t>(texture.width) *
                              static_cast<uint64_t>(texture.height) * 4u;
  if (byte_count > static_cast<uint64_t>(std::numeric_limits<size_t>::max())) {
    return false;
  }
  const auto* raw_bytes = reinterpret_cast<const uint8_t*>(embedded_texture.pcData);
  return detail::canonicalizeAssimpEmbeddedTexture(
      std::span<const uint8_t>(raw_bytes, static_cast<size_t>(byte_count)),
      texture.width,
      texture.height,
      texture.source_bytes);
}

int base64Value(char value) {
  if (value >= 'A' && value <= 'Z') return value - 'A';
  if (value >= 'a' && value <= 'z') return value - 'a' + 26;
  if (value >= '0' && value <= '9') return value - '0' + 52;
  if (value == '+') return 62;
  if (value == '/') return 63;
  return -1;
}

std::vector<uint8_t> decodeBase64(std::string_view encoded) {
  std::vector<uint8_t> bytes;
  int value = 0;
  int bits = -8;
  for (const char character : encoded) {
    if (std::isspace(static_cast<unsigned char>(character)) != 0) continue;
    if (character == '=') break;
    const int digit = base64Value(character);
    if (digit < 0) return {};
    value = (value << 6) | digit;
    bits += 6;
    if (bits >= 0) {
      bytes.push_back(static_cast<uint8_t>((value >> bits) & 0xff));
      bits -= 8;
    }
  }
  return bytes;
}

std::vector<uint8_t> decodeDataUri(std::string_view uri) {
  const size_t comma = uri.find(',');
  if (!uri.starts_with("data:") || comma == std::string_view::npos) return {};
  const std::string_view metadata = uri.substr(0u, comma);
  const std::string_view payload = uri.substr(comma + 1u);
  if (metadata.find(";base64") != std::string_view::npos) {
    return decodeBase64(payload);
  }
  std::vector<uint8_t> bytes;
  bytes.reserve(payload.size());
  const auto hex = [](char value) {
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    if (value >= 'A' && value <= 'F') return value - 'A' + 10;
    return -1;
  };
  for (size_t index = 0u; index < payload.size(); ++index) {
    if (payload[index] == '%' && index + 2u < payload.size()) {
      const int high = hex(payload[index + 1u]);
      const int low = hex(payload[index + 2u]);
      if (high >= 0 && low >= 0) {
        bytes.push_back(static_cast<uint8_t>((high << 4) | low));
        index += 2u;
        continue;
      }
    }
    bytes.push_back(static_cast<uint8_t>(payload[index]));
  }
  return bytes;
}

std::string decodeUriPath(std::string_view uri) {
  const std::vector<uint8_t> decoded = decodeDataUri(
      std::string("data:," + std::string(uri)));
  return std::string(decoded.begin(), decoded.end());
}

void setGltfTextureCoordTransform(rendering::ImportedMaterialData& data,
                                  const Json& texture_info,
                                  size_t slot) {
  if (slot >= rendering::kImportedMaterialTextureCoordSlotCount) return;
  uint32_t uv_index = texture_info.value("texCoord", 0u);
  glm::vec2 offset{0.0f};
  glm::vec2 scale{1.0f};
  float rotation = 0.0f;
  if (const auto extensions = texture_info.find("extensions");
      extensions != texture_info.end() && extensions->is_object()) {
    if (const auto transform = extensions->find("KHR_texture_transform");
        transform != extensions->end() && transform->is_object()) {
      const auto read_vec2 = [](const Json& object, const char* key, glm::vec2& out) {
        const auto value = object.find(key);
        if (value == object.end() || !value->is_array() || value->size() != 2u ||
            !(*value)[0].is_number() || !(*value)[1].is_number()) {
          return;
        }
        out = {(*value)[0].get<float>(), (*value)[1].get<float>()};
      };
      read_vec2(*transform, "offset", offset);
      read_vec2(*transform, "scale", scale);
      if (const auto value = transform->find("rotation");
          value != transform->end() && value->is_number()) {
        rotation = value->get<float>();
      }
      uv_index = transform->value("texCoord", uv_index);
    }
  }
  const float cosine = std::cos(rotation);
  const float sine = std::sin(rotation);
  // Imported glTF mesh UVs are V-flipped. Conjugating the authored glTF
  // transform by that flip keeps KHR_texture_transform in the same image
  // orientation seen by the renderer.
  data.texcoord_row0[slot] =
      {cosine * scale.x, sine * scale.y, offset.x - sine * scale.y,
       uv_index > 0u ? 1.0f : 0.0f};
  data.texcoord_row1[slot] =
      {-sine * scale.x, cosine * scale.y,
       1.0f - offset.y - cosine * scale.y, 0.0f};
}

bool appendGltfMaterialTexture(
    rendering::ImportedMaterialData& data,
    const GltfDocument& document,
    const Json& texture_info,
    rendering::ImportedMaterialTextureSemantic semantic,
    bool srgb,
    const char* label,
    size_t texcoord_slot) {
  if (!document.valid() || !texture_info.is_object() ||
      !texture_info.contains("index") || !texture_info["index"].is_number_unsigned() ||
      !document.json.contains("textures") || !document.json["textures"].is_array() ||
      !document.json.contains("images") || !document.json["images"].is_array()) {
    return false;
  }
  const uint32_t texture_index = texture_info["index"].get<uint32_t>();
  if (texture_index >= document.json["textures"].size()) return false;
  const Json& texture_json = document.json["textures"][texture_index];
  uint32_t image_index = texture_json.value("source", std::numeric_limits<uint32_t>::max());
  if (const auto extensions = texture_json.find("extensions");
      extensions != texture_json.end() && extensions->is_object()) {
    if (const auto basis = extensions->find("KHR_texture_basisu");
        basis != extensions->end() && basis->is_object()) {
      image_index = basis->value("source", image_index);
    }
  }
  if (image_index >= document.json["images"].size()) return false;
  const Json& image = document.json["images"][image_index];

  rendering::ImportedMaterialTexture imported{};
  imported.semantic = semantic;
  imported.srgb = srgb;
  imported.label = label != nullptr ? label : "gltfTexture";
  imported.raw_name = image.value("name", std::string{});
  const std::string uri = image.value("uri", std::string{});
  if (!uri.empty()) {
    if (uri.starts_with("data:")) {
      imported.source_bytes = decodeDataUri(uri);
      imported.embedded = true;
      imported.compressed = true;
      imported.source_key = document.source_path.string() + "#image=" +
                            std::to_string(image_index);
    } else {
      const std::string decoded_uri = decodeUriPath(uri);
      imported.resolved_path =
          (document.source_path.parent_path() / decoded_uri).lexically_normal();
      imported.source_key = imported.resolved_path.string();
      imported.raw_name = uri;
    }
  } else if (image.contains("bufferView") && image["bufferView"].is_number_unsigned() &&
             document.json.contains("bufferViews") &&
             document.json["bufferViews"].is_array()) {
    const uint32_t view_index = image["bufferView"].get<uint32_t>();
    if (view_index >= document.json["bufferViews"].size()) return false;
    const Json& view = document.json["bufferViews"][view_index];
    const std::vector<uint8_t>* buffer =
        documentBuffer(document, view.value("buffer", 0u));
    if (buffer == nullptr) return false;
    const size_t offset = view.value("byteOffset", size_t{0});
    const size_t length = view.value("byteLength", size_t{0});
    if (offset > buffer->size() || length > buffer->size() - offset) return false;
    imported.source_bytes.assign(
        buffer->begin() + static_cast<std::ptrdiff_t>(offset),
        buffer->begin() + static_cast<std::ptrdiff_t>(offset + length));
    imported.embedded = true;
    imported.compressed = true;
    imported.source_key = document.source_path.string() + "#image=" +
                          std::to_string(image_index);
  } else {
    return false;
  }
  if (imported.source_key.empty() ||
      (imported.embedded && imported.source_bytes.empty())) {
    return false;
  }
  setGltfTextureCoordTransform(data, texture_info, texcoord_slot);
  data.textures.push_back(std::move(imported));
  return true;
}

void applyGltfSpecularMaterial(rendering::ImportedMaterialData& data,
                               const GltfDocument& document,
                               uint32_t material_index) {
  data.material.normal_map_convention =
      rendering::MaterialDesc::NormalMapConvention::OpenGL;
  if (!document.valid() || !document.json.contains("materials") ||
      !document.json["materials"].is_array() ||
      material_index >= document.json["materials"].size()) {
    return;
  }
  const Json& material = document.json["materials"][material_index];
  const auto extensions = material.find("extensions");
  if (extensions == material.end() || !extensions->is_object()) return;
  const auto specular = extensions->find("KHR_materials_specular");
  if (specular == extensions->end() || !specular->is_object()) return;

  const auto finite_unit = [](float value, float fallback) {
    return std::isfinite(value) ? std::clamp(value, 0.0f, 1.0f) : fallback;
  };
  if (const auto factor = specular->find("specularFactor");
      factor != specular->end() && factor->is_number()) {
    data.material.specular_factor =
        finite_unit(factor->get<float>(), data.material.specular_factor);
  }
  if (const auto color = specular->find("specularColorFactor");
      color != specular->end() && color->is_array() && color->size() == 3u &&
      (*color)[0].is_number() && (*color)[1].is_number() && (*color)[2].is_number()) {
    data.material.specular_color = {
        finite_unit((*color)[0].get<float>(), 1.0f),
        finite_unit((*color)[1].get<float>(), 1.0f),
        finite_unit((*color)[2].get<float>(), 1.0f), 1.0f};
  }
  if (const auto texture = specular->find("specularTexture");
      texture != specular->end()) {
    appendGltfMaterialTexture(
        data, document, *texture,
        rendering::ImportedMaterialTextureSemantic::Specular,
        false, "specular", kTexCoordSpecular);
  }
  if (const auto texture = specular->find("specularColorTexture");
      texture != specular->end()) {
    appendGltfMaterialTexture(
        data, document, *texture,
        rendering::ImportedMaterialTextureSemantic::SpecularColor,
        true, "specularColor", kTexCoordSpecularColor);
  }
}

bool appendImportedTexture(rendering::ImportedMaterialData& data,
                           const aiScene& scene,
                           const aiMaterial& material,
                           const std::filesystem::path& asset_path,
                           aiTextureType type,
                           unsigned int texture_index,
                           rendering::ImportedMaterialTextureSemantic semantic,
                           bool srgb,
                           const char* label,
                           size_t texcoord_slot) {
  aiString tex_path;
  aiTextureMapping mapping = aiTextureMapping_UV;
  unsigned int uv_index = 0;
  float blend = 1.0f;
  aiTextureOp op = aiTextureOp_Multiply;
  aiTextureMapMode mapmode[2] = {aiTextureMapMode_Wrap, aiTextureMapMode_Wrap};
  if (material.GetTexture(type, texture_index, &tex_path, &mapping, &uv_index, &blend, &op,
                          mapmode) != AI_SUCCESS ||
      tex_path.length == 0) {
    return false;
  }

  const std::string raw_key = tex_path.C_Str();
  rendering::ImportedMaterialTexture texture{};
  texture.semantic = semantic;
  texture.label = label ? label : "importedTexture";
  texture.srgb = srgb;
  if (!populateImportedTextureSource(texture, scene, asset_path, raw_key)) {
    return false;
  }

  setImportedTextureCoordTransform(data, material, type, texture_index, uv_index, texcoord_slot);
  data.textures.push_back(std::move(texture));
  return true;
}

std::optional<std::string> importedTextureRawKey(const aiMaterial& material,
                                                 aiTextureType type,
                                                 unsigned int texture_index = 0u) {
  aiString path;
  if (material.GetTexture(type, texture_index, &path) != AI_SUCCESS ||
      path.length == 0u) {
    return std::nullopt;
  }
  return std::string(path.C_Str());
}

std::string importedTextureReferenceName(const aiScene& scene,
                                         const std::string& raw_key) {
  const int embedded_index = embeddedTextureIndex(raw_key);
  if (embedded_index >= 0 && embedded_index < static_cast<int>(scene.mNumTextures) &&
      scene.mTextures[embedded_index] != nullptr &&
      scene.mTextures[embedded_index]->mFilename.length > 0u) {
    return scene.mTextures[embedded_index]->mFilename.C_Str();
  }
  return raw_key;
}

std::unordered_set<std::string> importedMaterialTextureFamilies(
    const aiMaterial& material) {
  std::unordered_set<std::string> families;
  for (unsigned int value = static_cast<unsigned int>(aiTextureType_DIFFUSE);
       value <= static_cast<unsigned int>(aiTextureType_UNKNOWN);
       ++value) {
    const auto type = static_cast<aiTextureType>(value);
    const unsigned int texture_count = material.GetTextureCount(type);
    for (unsigned int texture_index = 0u; texture_index < texture_count;
         ++texture_index) {
      const std::optional<std::string> raw_key =
          importedTextureRawKey(material, type, texture_index);
      if (!raw_key.has_value()) {
        continue;
      }
      std::string family = textureFamilyKey(*raw_key);
      if (!family.empty()) {
        families.insert(std::move(family));
      }
    }
  }
  return families;
}

std::optional<unsigned int> findEmbeddedPackedMetallicRoughnessTexture(
    const aiScene& scene,
    const aiMaterial& material) {
  const std::unordered_set<std::string> material_families =
      importedMaterialTextureFamilies(material);
  std::optional<unsigned int> best_match;
  int best_score = 0;
  std::optional<unsigned int> sole_candidate;
  size_t candidate_count = 0u;

  for (unsigned int texture_index = 0u; texture_index < scene.mNumTextures;
       ++texture_index) {
    const aiTexture* texture = scene.mTextures[texture_index];
    if (texture == nullptr || texture->mFilename.length == 0u) {
      continue;
    }
    const std::string_view name = texture->mFilename.C_Str();
    const int name_score = packedMetallicRoughnessNameScore(name);
    if (name_score == 0) {
      continue;
    }

    sole_candidate = texture_index;
    candidate_count += 1u;
    const std::string family = textureFamilyKey(name);
    if (family.empty() || !material_families.contains(family)) {
      continue;
    }
    if (!best_match.has_value() || name_score > best_score) {
      best_match = texture_index;
      best_score = name_score;
    }
  }

  if (best_match.has_value()) {
    return best_match;
  }
  if (scene.mNumMaterials == 1u && candidate_count == 1u) {
    return sole_candidate;
  }
  return std::nullopt;
}

void copyRecoveredMetallicRoughnessTransform(
    rendering::ImportedMaterialData& data,
    const aiMaterial& material,
    std::string_view recovered_name) {
  const std::string recovered_family = textureFamilyKey(recovered_name);
  if (recovered_family.empty()) {
    return;
  }

  constexpr aiTextureType kTransformSources[] = {
      aiTextureType_DIFFUSE_ROUGHNESS,
      aiTextureType_METALNESS,
      aiTextureType_SPECULAR,
      aiTextureType_SHININESS,
      aiTextureType_BASE_COLOR,
      aiTextureType_DIFFUSE,
  };
  for (const aiTextureType type : kTransformSources) {
    const unsigned int texture_count = material.GetTextureCount(type);
    for (unsigned int texture_index = 0u; texture_index < texture_count;
         ++texture_index) {
      aiString path;
      unsigned int uv_index = 0u;
      if (material.GetTexture(type, texture_index, &path, nullptr, &uv_index) !=
              AI_SUCCESS ||
          textureFamilyKey(path.C_Str()) != recovered_family) {
        continue;
      }
      setImportedTextureCoordTransform(data,
                                       material,
                                       type,
                                       texture_index,
                                       uv_index,
                                       kTexCoordMetallicRoughness);
      return;
    }
  }
}

bool appendRecoveredMetallicRoughnessTexture(
    rendering::ImportedMaterialData& data,
    const aiScene& scene,
    const aiMaterial& material,
    const std::filesystem::path& asset_path) {
  const std::optional<unsigned int> texture_index =
      findEmbeddedPackedMetallicRoughnessTexture(scene, material);
  if (!texture_index.has_value()) {
    return false;
  }

  const aiTexture& embedded_texture = *scene.mTextures[*texture_index];
  const std::string raw_key = "*" + std::to_string(*texture_index);
  rendering::ImportedMaterialTexture texture{};
  texture.semantic =
      rendering::ImportedMaterialTextureSemantic::MetallicRoughness;
  texture.label = "metallicRoughness";
  texture.srgb = false;
  if (!populateImportedTextureSource(texture, scene, asset_path, raw_key)) {
    return false;
  }
  const std::string recovered_name = embedded_texture.mFilename.C_Str();
  texture.raw_name = recovered_name.empty() ? raw_key : recovered_name;
  copyRecoveredMetallicRoughnessTransform(data, material, texture.raw_name);
  data.textures.push_back(std::move(texture));

  if (startupDiagnosticsEnabled()) {
    aiString material_name;
    material.Get(AI_MATKEY_NAME, material_name);
    spdlog::info("Recovered packed metallic-roughness texture '{}' for material '{}'",
                 recovered_name,
                 material_name.C_Str());
  }
  return true;
}

bool appendPackedMetallicRoughnessTexture(
    rendering::ImportedMaterialData& data,
    const aiScene& scene,
    const aiMaterial& material,
    const std::filesystem::path& asset_path) {
  using Semantic = rendering::ImportedMaterialTextureSemantic;
  const std::optional<std::string> metallic =
      importedTextureRawKey(material, aiTextureType_METALNESS);
  const std::optional<std::string> roughness =
      importedTextureRawKey(material, aiTextureType_DIFFUSE_ROUGHNESS);

  if (metallic.has_value() && roughness.has_value() && *metallic == *roughness &&
      appendImportedTexture(data,
                            scene,
                            material,
                            asset_path,
                            aiTextureType_METALNESS,
                            0u,
                            Semantic::MetallicRoughness,
                            false,
                            "metallicRoughness",
                            kTexCoordMetallicRoughness)) {
    return true;
  }

  if (metallic.has_value() &&
      detail::isPackedMetallicRoughnessTextureName(
          importedTextureReferenceName(scene, *metallic)) &&
      appendImportedTexture(data,
                            scene,
                            material,
                            asset_path,
                            aiTextureType_METALNESS,
                            0u,
                            Semantic::MetallicRoughness,
                            false,
                            "metallicRoughness",
                            kTexCoordMetallicRoughness)) {
    return true;
  }

  if (roughness.has_value() &&
      detail::isPackedMetallicRoughnessTextureName(
          importedTextureReferenceName(scene, *roughness)) &&
      appendImportedTexture(data,
                            scene,
                            material,
                            asset_path,
                            aiTextureType_DIFFUSE_ROUGHNESS,
                            0u,
                            Semantic::MetallicRoughness,
                            false,
                            "metallicRoughness",
                            kTexCoordMetallicRoughness)) {
    return true;
  }

  return appendRecoveredMetallicRoughnessTexture(data, scene, material, asset_path);
}

struct AlphaTextureStats {
  uint64_t sample_count = 0u;
  uint64_t near_zero_count = 0u;
  uint64_t mid_alpha_count = 0u;
  uint64_t near_opaque_count = 0u;
  uint8_t min_alpha = 255u;
  uint8_t max_alpha = 0u;
  double mean_alpha = 255.0;
};

std::optional<AlphaTextureStats> alphaTextureStats(
    const rendering::ImportedMaterialData& data) {
  const rendering::ImportedMaterialTexture* base_color_texture = nullptr;
  for (const auto& texture : data.textures) {
    if (texture.semantic == rendering::ImportedMaterialTextureSemantic::BaseColor) {
      base_color_texture = &texture;
      break;
    }
  }
  if (base_color_texture == nullptr) {
    return std::nullopt;
  }

  std::optional<assets::Rgba8Image> image =
      assets::detail::decodeImportedTexture(*base_color_texture);
  if (!image.has_value() || !image->valid()) {
    return std::nullopt;
  }

  AlphaTextureStats stats{};
  uint64_t alpha_sum = 0u;
  stats.sample_count = image->pixels.size() / 4u;
  for (size_t i = 3u; i < image->pixels.size(); i += 4u) {
    const uint8_t alpha = image->pixels[i];
    stats.min_alpha = std::min(stats.min_alpha, alpha);
    stats.max_alpha = std::max(stats.max_alpha, alpha);
    alpha_sum += alpha;
    if (alpha < 8u) {
      stats.near_zero_count += 1u;
    } else if (alpha < 240u) {
      stats.mid_alpha_count += 1u;
    } else {
      stats.near_opaque_count += 1u;
    }
  }
  if (stats.sample_count == 0u) {
    return std::nullopt;
  }
  stats.mean_alpha = static_cast<double>(alpha_sum) /
                     static_cast<double>(stats.sample_count);
  return stats;
}

void applyAlphaModePolicy(rendering::ImportedMaterialData& data,
                          const GltfSceneLoadOptions& options) {
  if (options.alpha_mode_policy != GltfSceneLoadOptions::AlphaModePolicy::AutoCutout ||
      data.material.alpha_mode != rendering::MaterialDesc::AlphaMode::Blend ||
      data.material.base_color.a < 0.999f) {
    return;
  }

  const std::optional<AlphaTextureStats> stats = alphaTextureStats(data);
  if (!stats.has_value()) {
    return;
  }

  const double sample_count = static_cast<double>(stats->sample_count);
  const double near_zero_fraction =
      static_cast<double>(stats->near_zero_count) / sample_count;
  const double near_opaque_fraction =
      static_cast<double>(stats->near_opaque_count) / sample_count;
  const double mid_alpha_fraction =
      static_cast<double>(stats->mid_alpha_count) / sample_count;

  if (stats->min_alpha >= 180u &&
      stats->mean_alpha >= 248.0 &&
      mid_alpha_fraction <= 0.10) {
    data.material.alpha_mode = rendering::MaterialDesc::AlphaMode::Opaque;
    data.material.transparent = false;
    data.material.depth_write = true;
    return;
  }

  if (stats->min_alpha <= 8u &&
      stats->max_alpha >= 240u &&
      near_zero_fraction >= 0.05 &&
      near_opaque_fraction >= 0.02) {
    data.material.alpha_mode = rendering::MaterialDesc::AlphaMode::Masked;
    data.material.transparent = false;
    data.material.depth_write = true;
    data.material.alpha_cutoff = 0.5f;
    data.material.alpha_dither = mid_alpha_fraction > 0.02;
  }
}

bool isBlackEmissiveTexture(const rendering::ImportedMaterialTexture& texture) {
  std::optional<assets::Rgba8Image> image =
      assets::detail::decodeImportedTexture(texture);
  if (!image.has_value() || !image->valid()) {
    return false;
  }

  for (size_t i = 0u; i + 2u < image->pixels.size(); i += 4u) {
    if (image->pixels[i] > 1u ||
        image->pixels[i + 1u] > 1u ||
        image->pixels[i + 2u] > 1u) {
      return false;
    }
  }
  return true;
}

void pruneBlackEmissiveTextures(rendering::ImportedMaterialData& data) {
  bool removed_emissive = false;
  data.textures.erase(
      std::remove_if(data.textures.begin(),
                     data.textures.end(),
                     [&removed_emissive](const rendering::ImportedMaterialTexture& texture) {
                       if (texture.semantic !=
                           rendering::ImportedMaterialTextureSemantic::Emissive) {
                         return false;
                       }
                       if (!isBlackEmissiveTexture(texture)) {
                         return false;
                       }
                       removed_emissive = true;
                       return true;
                     }),
      data.textures.end());
  if (removed_emissive) {
    data.material.emissive_color = {0.0f, 0.0f, 0.0f, 1.0f};
    data.material.emissive_strength = 0.0f;
  }
}

rendering::ImportedMaterialData buildImportedMaterialData(const aiScene& scene,
                                                         const aiMaterial& material,
                                                         const std::filesystem::path& asset_path) {
  rendering::ImportedMaterialData data{};
  data.material = buildMaterialDesc(material);

  aiColor3D emissive(0.0f, 0.0f, 0.0f);
  if (material.Get(AI_MATKEY_COLOR_EMISSIVE, emissive) == AI_SUCCESS) {
    data.material.emissive_color = {emissive.r, emissive.g, emissive.b, 1.0f};
  }
  if (float emissive_strength = 1.0f;
      material.Get(AI_MATKEY_EMISSIVE_INTENSITY, emissive_strength) == AI_SUCCESS) {
    data.material.emissive_strength = emissive_strength;
  }
  if (float metallic = 1.0f;
      material.Get(AI_MATKEY_METALLIC_FACTOR, metallic) == AI_SUCCESS) {
    data.material.metallic = metallic;
  }
  if (float roughness = 1.0f;
      material.Get(AI_MATKEY_ROUGHNESS_FACTOR, roughness) == AI_SUCCESS) {
    data.material.roughness = roughness;
  }
  if (float normal_scale = 1.0f;
      material.Get(AI_MATKEY_TEXBLEND_NORMALS(0), normal_scale) == AI_SUCCESS) {
    data.material.normal_scale = normal_scale;
  }
  float occlusion_strength = 1.0f;
  if (material.Get(AI_MATKEY_TEXBLEND(aiTextureType_AMBIENT_OCCLUSION, 0),
                   occlusion_strength) == AI_SUCCESS ||
      material.Get(AI_MATKEY_TEXBLEND_LIGHTMAP(0), occlusion_strength) == AI_SUCCESS) {
    data.material.occlusion_strength = occlusion_strength;
  }
  if (float clearcoat = 0.0f;
      material.Get(AI_MATKEY_CLEARCOAT_FACTOR, clearcoat) == AI_SUCCESS) {
    data.material.clearcoat = clearcoat;
  }
  if (float clearcoat_roughness = 0.0f;
      material.Get(AI_MATKEY_CLEARCOAT_ROUGHNESS_FACTOR, clearcoat_roughness) == AI_SUCCESS) {
    data.material.clearcoat_roughness = clearcoat_roughness;
  }
  if (aiColor3D sheen_color(0.0f, 0.0f, 0.0f);
      material.Get(AI_MATKEY_SHEEN_COLOR_FACTOR, sheen_color) == AI_SUCCESS) {
    data.material.sheen_color = {sheen_color.r, sheen_color.g, sheen_color.b, 1.0f};
  }
  if (float sheen_roughness = 0.0f;
      material.Get(AI_MATKEY_SHEEN_ROUGHNESS_FACTOR, sheen_roughness) == AI_SUCCESS) {
    data.material.sheen_roughness = sheen_roughness;
  }
  if (float anisotropy = 0.0f;
      material.Get(AI_MATKEY_ANISOTROPY_FACTOR, anisotropy) == AI_SUCCESS) {
    data.material.anisotropy = anisotropy;
  }
  if (float transmission = 0.0f;
      material.Get(AI_MATKEY_TRANSMISSION_FACTOR, transmission) == AI_SUCCESS) {
    data.material.transmission = transmission;
    if (transmission > 0.001f) {
      data.material.transparent = true;
      data.material.alpha_mode = rendering::MaterialDesc::AlphaMode::Blend;
    }
  }
  if (float ior = 1.5f; material.Get(AI_MATKEY_REFRACTI, ior) == AI_SUCCESS) {
    data.material.ior = ior;
  }
  if (float thickness = 0.0f;
      material.Get(AI_MATKEY_VOLUME_THICKNESS_FACTOR, thickness) == AI_SUCCESS) {
    data.material.thickness = thickness;
  }
  if (float attenuation_distance = std::numeric_limits<float>::infinity();
      material.Get(AI_MATKEY_VOLUME_ATTENUATION_DISTANCE, attenuation_distance) == AI_SUCCESS) {
    data.material.attenuation_distance = attenuation_distance;
  }
  if (aiColor3D attenuation_color(1.0f, 1.0f, 1.0f);
      material.Get(AI_MATKEY_VOLUME_ATTENUATION_COLOR, attenuation_color) == AI_SUCCESS) {
    data.material.attenuation_color =
        {attenuation_color.r, attenuation_color.g, attenuation_color.b, 1.0f};
  }

  using Semantic = rendering::ImportedMaterialTextureSemantic;
  if (!appendImportedTexture(data, scene, material, asset_path, aiTextureType_BASE_COLOR, 0,
                             Semantic::BaseColor, true, "baseColor", kTexCoordBaseColor)) {
    appendImportedTexture(data, scene, material, asset_path, aiTextureType_DIFFUSE, 0,
                          Semantic::BaseColor, true, "baseColor", kTexCoordBaseColor);
  }
  appendImportedTexture(data, scene, material, asset_path, aiTextureType_NORMALS, 0,
                        Semantic::Normal, false, "normal", kTexCoordNormal);
  appendPackedMetallicRoughnessTexture(data, scene, material, asset_path);
  if (!appendImportedTexture(data, scene, material, asset_path, aiTextureType_AMBIENT_OCCLUSION, 0,
                             Semantic::Occlusion, false, "occlusion", kTexCoordOcclusion)) {
    appendImportedTexture(data, scene, material, asset_path, aiTextureType_LIGHTMAP, 0,
                          Semantic::Occlusion, false, "occlusion", kTexCoordOcclusion);
  }
  appendImportedTexture(data, scene, material, asset_path, aiTextureType_EMISSIVE, 0,
                        Semantic::Emissive, true, "emissive", kTexCoordEmissive);
  appendImportedTexture(data, scene, material, asset_path, aiTextureType_CLEARCOAT, 0,
                        Semantic::Clearcoat, false, "clearcoat", kTexCoordClearcoat);
  appendImportedTexture(data, scene, material, asset_path, aiTextureType_CLEARCOAT, 1,
                        Semantic::ClearcoatRoughness, false, "clearcoatRoughness",
                        kTexCoordClearcoatRoughness);
  appendImportedTexture(data, scene, material, asset_path, aiTextureType_CLEARCOAT, 2,
                        Semantic::ClearcoatNormal, false, "clearcoatNormal",
                        kTexCoordClearcoatNormal);
  appendImportedTexture(data, scene, material, asset_path, aiTextureType_SHEEN, 0,
                        Semantic::SheenColor, true, "sheenColor", kTexCoordSheenColor);
  appendImportedTexture(data, scene, material, asset_path, aiTextureType_SHEEN, 1,
                        Semantic::SheenRoughness, false, "sheenRoughness",
                        kTexCoordSheenRoughness);
  appendImportedTexture(data, scene, material, asset_path, aiTextureType_TRANSMISSION, 0,
                        Semantic::Transmission, false, "transmission", kTexCoordTransmission);
  appendImportedTexture(data, scene, material, asset_path, aiTextureType_TRANSMISSION, 1,
                        Semantic::Thickness, false, "thickness", kTexCoordThickness);
  pruneBlackEmissiveTextures(data);
  return data;
}

std::string materialName(const aiMaterial& material) {
  aiString name;
  if (material.Get(AI_MATKEY_NAME, name) == AI_SUCCESS && name.length > 0) {
    return name.C_Str();
  }
  return {};
}

bool materialOverrideMatches(const GltfSceneLoadOptions::MaterialOverride& override,
                             uint32_t material_index,
                             std::string_view material_name) {
  if (override.all_materials) {
    return true;
  }
  const bool index_matches =
      override.material_index != kInvalidGltfSceneMaterial &&
      override.material_index == material_index;
  const bool name_matches =
      !override.material_name.empty() && override.material_name == material_name;
  return index_matches || name_matches;
}

void forceDiffuseOnly(rendering::ImportedMaterialData& data, bool keep_normal_maps) {
  auto& material = data.material;
  material.metallic = 0.0f;
  material.roughness = 1.0f;
  if (!keep_normal_maps) {
    material.normal_scale = 0.0f;
  }
  material.occlusion_strength = 1.0f;
  material.emissive_color = {0.0f, 0.0f, 0.0f, 1.0f};
  material.emissive_strength = 0.0f;
  material.clearcoat = 0.0f;
  material.clearcoat_roughness = 1.0f;
  material.sheen_color = {0.0f, 0.0f, 0.0f, 1.0f};
  material.sheen_roughness = 1.0f;
  material.anisotropy = 0.0f;
  material.transmission = 0.0f;
  material.ior = 1.5f;
  material.thickness = 0.0f;
  material.attenuation_distance = std::numeric_limits<float>::infinity();
  material.attenuation_color = {1.0f, 1.0f, 1.0f, 1.0f};
  material.analytic_sphere_normals = false;
  material.unlit = false;

  data.textures.erase(
      std::remove_if(data.textures.begin(),
                     data.textures.end(),
                     [keep_normal_maps](const rendering::ImportedMaterialTexture& texture) {
                       if (texture.semantic ==
                           rendering::ImportedMaterialTextureSemantic::BaseColor) {
                         return false;
                       }
                       return !keep_normal_maps ||
                              texture.semantic !=
                                  rendering::ImportedMaterialTextureSemantic::Normal;
                     }),
      data.textures.end());
}

void disableMetallicRoughness(rendering::ImportedMaterialData& data) {
  data.material.metallic = 0.0f;
  data.material.roughness = 1.0f;
  data.textures.erase(
      std::remove_if(data.textures.begin(),
                     data.textures.end(),
                     [](const rendering::ImportedMaterialTexture& texture) {
                       return texture.semantic ==
                              rendering::ImportedMaterialTextureSemantic::MetallicRoughness;
                     }),
      data.textures.end());
}

void applyMaterialOverrides(rendering::ImportedMaterialData& data,
                            uint32_t material_index,
                            std::string_view material_name,
                            const GltfSceneLoadOptions& options) {
  for (const GltfSceneLoadOptions::MaterialOverride& override : options.material_overrides) {
    if (!materialOverrideMatches(override, material_index, material_name)) {
      continue;
    }

    if (override.has_normal_scale) {
      data.material.normal_scale = std::clamp(override.normal_scale, 0.0f, 8.0f);
    }
    if (override.has_diffuse_only && override.diffuse_only) {
      forceDiffuseOnly(data, override.keep_normal_maps);
    }
    if (override.has_disable_metallic_roughness &&
        override.disable_metallic_roughness) {
      disableMetallicRoughness(data);
    }
  }
}

bool materialCastsShadows(uint32_t material_index,
                          std::string_view material_name,
                          const GltfSceneLoadOptions& options) {
  bool casts_shadows = true;
  for (const GltfSceneLoadOptions::MaterialOverride& override : options.material_overrides) {
    if (override.has_casts_shadows &&
        materialOverrideMatches(override, material_index, material_name)) {
      casts_shadows = override.casts_shadows;
    }
  }
  return casts_shadows;
}

float estimateLightRange(const aiLight& light) {
  if (light.mAttenuationLinear > 1e-5f) {
    return std::max(1.0f, 1.0f / light.mAttenuationLinear);
  }
  if (light.mAttenuationQuadratic > 1e-5f &&
      (light.mAttenuationConstant > 1e-5f || light.mAttenuationLinear > 1e-5f ||
       std::abs(light.mAttenuationQuadratic - 1.0f) > 1e-5f)) {
    return std::max(1.0f, std::sqrt(1.0f / light.mAttenuationQuadratic));
  }
  return 10.0f;
}

components::LightComponent buildLightComponent(const aiLight& light) {
  constexpr float kRadiansToDegrees = 57.29577951308232f;
  constexpr float kDirectionalIntensityScale = 1.0f / 700.0f;
  constexpr float kLocalLightIntensityScale = 1.0f / 50.0f;
  constexpr float kLocalLightCutoffIntensity = 0.05f;
  components::LightComponent out{};
  float intensity_scale = kLocalLightIntensityScale;

  switch (light.mType) {
    case aiLightSource_DIRECTIONAL:
      out.type = components::LightComponent::Type::Directional;
      intensity_scale = kDirectionalIntensityScale;
      break;
    case aiLightSource_SPOT:
      out.type = components::LightComponent::Type::Spot;
      out.casts_shadows = true;
      out.inner_cone_degrees = light.mAngleInnerCone * kRadiansToDegrees;
      out.outer_cone_degrees = light.mAngleOuterCone * kRadiansToDegrees;
      break;
    case aiLightSource_POINT:
    default:
      out.type = components::LightComponent::Type::Point;
      out.casts_shadows = true;
      break;
  }

  math::Color diffuse{
      light.mColorDiffuse.r,
      light.mColorDiffuse.g,
      light.mColorDiffuse.b,
      1.0f};
  const float max_channel =
      std::max(diffuse.r, std::max(diffuse.g, std::max(diffuse.b, 0.0f)));
  if (max_channel > 1e-5f) {
    out.color = {diffuse.r / max_channel, diffuse.g / max_channel, diffuse.b / max_channel, 1.0f};
    out.intensity = max_channel * intensity_scale;
  } else {
    out.color = {1.0f, 1.0f, 1.0f, 1.0f};
    out.intensity = 1.0f;
  }

  if (out.type == components::LightComponent::Type::Point ||
      out.type == components::LightComponent::Type::Spot) {
    const bool gltf_default_quadratic =
        light.mAttenuationQuadratic > 1e-5f &&
        light.mAttenuationConstant <= 1e-5f &&
        light.mAttenuationLinear <= 1e-5f &&
        std::abs(light.mAttenuationQuadratic - 1.0f) <= 1e-5f;
    if (gltf_default_quadratic) {
      out.range = std::clamp(std::sqrt(std::max(out.intensity, 0.0f) / kLocalLightCutoffIntensity),
                             4.0f,
                             40.0f);
    } else {
      out.range = estimateLightRange(light);
    }
  }

  return out;
}

void decomposeTransform(const aiMatrix4x4& transform,
                        math::Vec3& position,
                        math::Quat& rotation,
                        math::Vec3& scale) {
  aiVector3D ai_scale;
  aiQuaternion ai_rotation;
  aiVector3D ai_position;
  transform.Decompose(ai_scale, ai_rotation, ai_position);
  position = toVec3(ai_position);
  rotation = toQuat(ai_rotation);
  scale = toVec3(ai_scale);
}

uint32_t loadNodePrefab(const aiScene& scene,
                        const aiNode& node,
                        const aiMatrix4x4& parent_world,
                        const std::unordered_map<std::string, const aiLight*>& lights_by_name,
                        const GltfSceneLoadOptions& options,
                        GltfScenePrefab& prefab,
                        std::unordered_map<std::string, uint32_t>& node_indices_by_name) {
  const aiMatrix4x4 world_transform = parent_world * node.mTransformation;

  GltfScenePrefabNode prefab_node{};
  prefab_node.name = node.mName.C_Str();
  decomposeTransform(node.mTransformation,
                     prefab_node.local_position,
                     prefab_node.local_rotation,
                     prefab_node.local_scale);
  decomposeTransform(world_transform,
                     prefab_node.world_position,
                     prefab_node.world_rotation,
                     prefab_node.world_scale);

  if (options.import_meshes) {
    prefab_node.primitives.reserve(node.mNumMeshes);
    for (unsigned int mesh_index = 0; mesh_index < node.mNumMeshes; ++mesh_index) {
      const unsigned int scene_mesh_index = node.mMeshes[mesh_index];
      if (scene_mesh_index >= scene.mNumMeshes || scene.mMeshes[scene_mesh_index] == nullptr) {
        continue;
      }
      const aiMesh& mesh = *scene.mMeshes[scene_mesh_index];
      GltfScenePrefabPrimitive primitive{};
      primitive.name = safeName(mesh.mName.C_Str(), "Primitive");
      primitive.mesh = buildMeshData(mesh);
      primitive.source_material_index =
          mesh.mMaterialIndex < scene.mNumMaterials ? mesh.mMaterialIndex : kInvalidGltfSceneMaterial;
      const std::string primitive_material_name =
          primitive.source_material_index < scene.mNumMaterials &&
                  scene.mMaterials[primitive.source_material_index] != nullptr
              ? materialName(*scene.mMaterials[primitive.source_material_index])
              : std::string{};
      primitive.casts_shadows =
          materialCastsShadows(primitive.source_material_index, primitive_material_name, options);
      if (primitive.source_material_index < prefab.imported_materials.size() &&
          prefab.imported_materials[primitive.source_material_index]) {
        primitive.material = prefab.imported_materials[primitive.source_material_index]->material;
      } else {
        primitive.material = buildMaterialDesc(scene, mesh);
      }
      primitive.source_mesh_index = scene_mesh_index;
      prefab_node.primitives.push_back(std::move(primitive));
    }
  }

  if (options.import_lights) {
    const auto light_it = lights_by_name.find(node.mName.C_Str());
    if (light_it != lights_by_name.end() && light_it->second != nullptr) {
      prefab_node.has_light = true;
      prefab_node.light = buildLightComponent(*light_it->second);
    }
  }

  const uint32_t node_index = static_cast<uint32_t>(prefab.nodes.size());
  prefab.nodes.push_back(std::move(prefab_node));
  node_indices_by_name.try_emplace(node.mName.C_Str(), node_index);

  prefab.nodes[node_index].children.reserve(node.mNumChildren);
  for (unsigned int child_index = 0; child_index < node.mNumChildren; ++child_index) {
    const aiNode* child = node.mChildren[child_index];
    if (child == nullptr) {
      continue;
    }
    const uint32_t imported_child =
        loadNodePrefab(scene,
                       *child,
                       world_transform,
                       lights_by_name,
                       options,
                       prefab,
                       node_indices_by_name);
    if (imported_child != kInvalidGltfSceneNode) {
      prefab.nodes[node_index].children.push_back(imported_child);
    }
  }

  return node_index;
}

std::string nodeDisplayName(const GltfScenePrefabNode& node, uint32_t index) {
  if (!node.name.empty()) {
    return node.name;
  }
  return "glTF Node " + std::to_string(index);
}

std::string primitiveDisplayName(const GltfScenePrefabNode& node,
                                 uint32_t node_index,
                                 const GltfScenePrefabPrimitive& primitive,
                                 size_t primitive_index) {
  if (!primitive.name.empty()) {
    return primitive.name;
  }
  return nodeDisplayName(node, node_index) + " Primitive " + std::to_string(primitive_index);
}

std::string sanitizeAssetKeySegment(std::string_view value) {
  std::string out;
  out.reserve(value.size());
  bool last_was_separator = false;
  for (const unsigned char ch : value) {
    if (std::isalnum(ch)) {
      out.push_back(static_cast<char>(std::tolower(ch)));
      last_was_separator = false;
    } else if (!last_was_separator) {
      out.push_back('_');
      last_was_separator = true;
    }
  }
  while (!out.empty() && out.front() == '_') {
    out.erase(out.begin());
  }
  while (!out.empty() && out.back() == '_') {
    out.pop_back();
  }
  return out.empty() ? std::string("scene") : out;
}

uint64_t stableHash(std::string_view value) {
  uint64_t hash = 14695981039346656037ull;
  for (const unsigned char ch : value) {
    hash ^= static_cast<uint64_t>(ch);
    hash *= 1099511628211ull;
  }
  return hash;
}

std::string hex64(uint64_t value) {
  constexpr char kDigits[] = "0123456789abcdef";
  std::string out(16u, '0');
  for (int i = 15; i >= 0; --i) {
    out[static_cast<size_t>(i)] = kDigits[value & 0x0full];
    value >>= 4u;
  }
  return out;
}

std::string defaultAssetKeyPrefix(const GltfScenePrefab& prefab) {
  const std::string raw_stem =
      prefab.source_path.empty() ? std::string("imported_gltf") : prefab.source_path.stem().string();
  const std::string stem = sanitizeAssetKeySegment(raw_stem);
  std::string hash_source = prefab.source_path.lexically_normal().generic_string();
  if (hash_source.empty()) {
    hash_source = stem;
  }
  return "gltf/" + stem + "/" + hex64(stableHash(hash_source));
}

std::string normalizeAssetKeyPrefix(std::string prefix) {
  while (prefix.size() > 1u && prefix.back() == '/') {
    prefix.pop_back();
  }
  return prefix;
}

std::string prefabResourceKey(std::string_view asset_key_prefix,
                              uint32_t node_index,
                              size_t primitive_index,
                              std::string_view suffix) {
  std::string key(asset_key_prefix);
  key.append("/nodes/");
  key.append(std::to_string(node_index));
  key.append("/primitives/");
  key.append(std::to_string(primitive_index));
  key.push_back('/');
  key.append(suffix);
  return key;
}

std::string materialResourceKey(std::string_view asset_key_prefix, uint32_t material_index) {
  std::string key(asset_key_prefix);
  key.append("/materials/");
  key.append(std::to_string(material_index));
  return key;
}

std::string fallbackPrimitiveMaterialKey(std::string_view asset_key_prefix,
                                         uint32_t node_index,
                                         size_t primitive_index) {
  return prefabResourceKey(asset_key_prefix, node_index, primitive_index, "material");
}

std::string primitiveMaterialKey(std::string_view asset_key_prefix,
                                 const GltfScenePrefab& prefab,
                                 uint32_t node_index,
                                 size_t primitive_index,
                                 const GltfScenePrefabPrimitive& primitive) {
  if (primitive.source_material_index != kInvalidGltfSceneMaterial) {
    return materialResourceKey(asset_key_prefix, primitive.source_material_index);
  }
  return fallbackPrimitiveMaterialKey(asset_key_prefix, node_index, primitive_index);
}

void registerPrimitiveMaterial(const GltfScenePrefab& prefab,
                               uint32_t node_index,
                               size_t primitive_index,
                               const GltfScenePrefabPrimitive& primitive,
                               const std::string& material_key,
                               assets::AssetRegistry& assets,
                               std::unordered_set<std::string>& registered_materials) {
  if (material_key.empty() || !registered_materials.insert(material_key).second) {
    return;
  }

  if (primitive.source_material_index != kInvalidGltfSceneMaterial &&
      !prefab.source_path.empty()) {
    std::shared_ptr<const rendering::ImportedMaterialData> imported_material;
    if (primitive.source_material_index < prefab.imported_materials.size()) {
      imported_material = prefab.imported_materials[primitive.source_material_index];
    }
    rendering::MaterialAssetDesc material{};
    material.surface = primitive.material;
    material.material_asset_path = prefab.source_path;
    material.material_asset_index = primitive.source_material_index;
    material.imported_material = std::move(imported_material);
    assets.registerImportedMaterialTextures(material_key, material);
    assets.registerMaterialAsset(material_key, std::move(material));
  } else {
    (void)node_index;
    (void)primitive_index;
    rendering::MaterialAssetDesc material{};
    material.surface = primitive.material;
    assets.registerMaterialAsset(material_key, std::move(material));
  }
}

void appendPrimitiveMesh(world::MeshData& out,
                         const world::MeshData& primitive,
                         uint32_t material_slot) {
  const uint32_t base_vertex = static_cast<uint32_t>(out.vertices.size());
  out.vertices.insert(out.vertices.end(), primitive.vertices.begin(), primitive.vertices.end());
  out.normals.insert(out.normals.end(), primitive.normals.begin(), primitive.normals.end());
  out.uvs.insert(out.uvs.end(), primitive.uvs.begin(), primitive.uvs.end());
  out.uvs1.insert(out.uvs1.end(), primitive.uvs1.begin(), primitive.uvs1.end());
  out.tangents.insert(out.tangents.end(), primitive.tangents.begin(), primitive.tangents.end());

  const uint32_t index_offset = static_cast<uint32_t>(out.indices.size());
  for (const uint32_t index : primitive.indices) {
    out.indices.push_back(base_vertex + index);
  }
  const uint32_t index_count = static_cast<uint32_t>(out.indices.size()) - index_offset;
  if (index_count > 0) {
    out.submeshes.push_back(world::MeshSubmesh{
        .index_offset = index_offset,
        .index_count = index_count,
        .material_slot = material_slot,
    });
  }
}

world::MeshData buildCombinedNodeMesh(const GltfScenePrefab& prefab,
                                         std::string_view asset_key_prefix,
                                         uint32_t node_index,
                                         const GltfScenePrefabNode& node,
                                         const std::vector<size_t>& primitive_indices,
                                         assets::AssetRegistry& assets,
                                         std::unordered_set<std::string>& registered_materials) {
  world::MeshData combined{};
  std::unordered_map<std::string, uint32_t> slots_by_material_key;
  slots_by_material_key.reserve(primitive_indices.size());

  for (const size_t primitive_index : primitive_indices) {
    const auto& primitive = node.primitives[primitive_index];
    const std::string material_key =
        primitiveMaterialKey(asset_key_prefix, prefab, node_index, primitive_index, primitive);
    registerPrimitiveMaterial(prefab,
                              node_index,
                              primitive_index,
                              primitive,
                              material_key,
                              assets,
                              registered_materials);

    auto slot_it = slots_by_material_key.find(material_key);
    if (slot_it == slots_by_material_key.end()) {
      const uint32_t slot = static_cast<uint32_t>(combined.material_slots.size());
      std::string slot_name =
          primitive.name.empty() ? ("Slot " + std::to_string(slot)) : primitive.name;
      combined.material_slots.push_back(world::MeshMaterialSlot{
          .name = std::move(slot_name),
          .default_material_key = material_key,
      });
      slot_it = slots_by_material_key.emplace(material_key, slot).first;
    }
    appendPrimitiveMesh(combined, primitive.mesh, slot_it->second);
  }

  return combined;
}
}  // namespace

GltfScenePrefab loadGltfScenePrefab(const std::filesystem::path& path,
                                  const GltfSceneLoadOptions& options) {
  const auto total_start = core::SteadyClock::now();
  GltfScenePrefab prefab{};
  prefab.source_path = path;

  Assimp::Importer importer;
  // FBX pivot/helper nodes are an interchange-format implementation detail,
  // not a useful runtime hierarchy. Evaluate them into ordinary node-local
  // transforms so skeleton topology, rest poses, animation channels, and
  // inverse bind matrices all use the same node space.
  importer.SetPropertyBool(AI_CONFIG_IMPORT_FBX_PRESERVE_PIVOTS, false);
  constexpr unsigned int kAssimpPostprocess =
      aiProcess_Triangulate | aiProcess_GenNormals |
      aiProcess_CalcTangentSpace;
  std::filesystem::path load_path = path;
  auto stage_start = total_start;
  const aiScene* scene = importer.ReadFile(load_path.string(),
                                           kAssimpPostprocess);
  logGltfStartupDiag(path, "assimp read file", stage_start, core::SteadyClock::now());
  std::string original_assimp_error;
  if (scene == nullptr || scene->mRootNode == nullptr) {
    if (const char* error = importer.GetErrorString();
        error != nullptr && error[0] != '\0') {
      original_assimp_error = error;
    }

    stage_start = core::SteadyClock::now();
    const std::optional<std::filesystem::path> decoded_path =
        writeMeshoptDecodedGltf(path);
    logGltfStartupDiag(path,
                       "meshopt decode fallback",
                       stage_start,
                       core::SteadyClock::now());
    if (decoded_path.has_value()) {
      load_path = *decoded_path;
      stage_start = core::SteadyClock::now();
      scene = importer.ReadFile(load_path.string(), kAssimpPostprocess);
      logGltfStartupDiag(path,
                         "assimp read meshopt-decoded file",
                         stage_start,
                         core::SteadyClock::now());
      if (scene == nullptr || scene->mRootNode == nullptr) {
        if (const char* error = importer.GetErrorString();
            error != nullptr && error[0] != '\0') {
          spdlog::warn("Assimp failed to import meshopt-decoded glTF scene {}: {}",
                       load_path.string(),
                       error);
        }
      }
    } else if (!original_assimp_error.empty()) {
      spdlog::warn("Assimp failed to import glTF scene {}: {}",
                   path.string(),
                   original_assimp_error);
    }
  }
  if (scene == nullptr || scene->mRootNode == nullptr) {
    logGltfStartupDiag(path, "total failed", total_start, core::SteadyClock::now());
    return prefab;
  }

  // Material extensions are read from the source document because Assimp maps
  // both KHR_materials_specular textures onto one legacy texture slot. Keeping
  // the original document also preserves external-image paths when a temporary
  // meshopt-decoded GLB was used for geometry import.
  const GltfDocument source_gltf = loadGltfDocument(path);

  stage_start = core::SteadyClock::now();
  prefab.imported_materials.reserve(scene->mNumMaterials);
  for (unsigned int i = 0; i < scene->mNumMaterials; ++i) {
    if (scene->mMaterials[i] == nullptr) {
      prefab.imported_materials.push_back({});
      continue;
    }
    rendering::ImportedMaterialData material =
        buildImportedMaterialData(*scene, *scene->mMaterials[i], load_path);
    applyGltfSpecularMaterial(material, source_gltf, i);
    applyAlphaModePolicy(material, options);
    applyMaterialOverrides(material,
                           i,
                           materialName(*scene->mMaterials[i]),
                           options);
    prefab.imported_materials.push_back(
        std::make_shared<rendering::ImportedMaterialData>(std::move(material)));
  }
  logGltfStartupDiag(path,
                     "imported material metadata",
                     stage_start,
                     core::SteadyClock::now(),
                     prefab.imported_materials.size());

  stage_start = core::SteadyClock::now();
  std::unordered_map<std::string, const aiLight*> lights_by_name;
  lights_by_name.reserve(scene->mNumLights);
  for (unsigned int i = 0; i < scene->mNumLights; ++i) {
    const aiLight* light = scene->mLights[i];
    if (light == nullptr) {
      continue;
    }
    lights_by_name[light->mName.C_Str()] = light;
  }
  logGltfStartupDiag(path,
                     "light map",
                     stage_start,
                     core::SteadyClock::now(),
                     lights_by_name.size());

  stage_start = core::SteadyClock::now();
  std::unordered_map<std::string, uint32_t> node_indices_by_name;
  node_indices_by_name.reserve(128);
  prefab.root_node = loadNodePrefab(*scene,
                                    *scene->mRootNode,
                                    aiMatrix4x4{},
                                    lights_by_name,
                                    options,
                                    prefab,
                                    node_indices_by_name);
  logGltfStartupDiag(path,
                     "node prefab",
                     stage_start,
                     core::SteadyClock::now(),
                     prefab.nodes.size());
  stage_start = core::SteadyClock::now();
  const GltfDocument gltf = loadGltfDocument(load_path);
  logGltfStartupDiag(path, "gltf document", stage_start, core::SteadyClock::now());
  stage_start = core::SteadyClock::now();
  populateGltfMeshData(gltf, node_indices_by_name, prefab);
  logGltfStartupDiag(path, "mesh metadata", stage_start, core::SteadyClock::now());
  stage_start = core::SteadyClock::now();
  populateGltfSkins(gltf, node_indices_by_name, prefab);
  logGltfStartupDiag(path,
                     "skin metadata",
                     stage_start,
                     core::SteadyClock::now(),
                     prefab.skins.size());
  stage_start = core::SteadyClock::now();
  populatePrimitiveSkinning(*scene, node_indices_by_name, prefab);
  logGltfStartupDiag(path, "primitive skinning", stage_start, core::SteadyClock::now());
  stage_start = core::SteadyClock::now();
  prefab.animations = loadGltfAnimationClips(gltf, node_indices_by_name, prefab);
  logGltfStartupDiag(path,
                     "gltf animations",
                     stage_start,
                     core::SteadyClock::now(),
                     prefab.animations.size());
  if (prefab.animations.empty()) {
    stage_start = core::SteadyClock::now();
    prefab.animations = loadAnimationClips(*scene, node_indices_by_name, &prefab);
    logGltfStartupDiag(path,
                       "assimp fallback animations",
                       stage_start,
                       core::SteadyClock::now(),
                       prefab.animations.size());
  }
  logGltfStartupDiag(path,
                     "total",
                     total_start,
                     core::SteadyClock::now(),
                     prefab.nodes.size());
  return prefab;
}

GltfSceneImportResult instantiateGltfScenePrefab(
    world::World& world,
    world::Scene& scene,
    assets::AssetRegistry& assets,
    const GltfScenePrefab& prefab,
    const GltfScenePrefabInstantiateOptions& options) {
  GltfSceneImportResult result{};
  if (!prefab.valid()) {
    return result;
  }
  const std::string asset_key_prefix =
      normalizeAssetKeyPrefix(options.asset_key_prefix.empty()
                                  ? defaultAssetKeyPrefix(prefab)
                                  : options.asset_key_prefix);
  if (!assets::AssetRegistry::isValidAssetKey(asset_key_prefix)) {
    return result;
  }

  result.node_entities_by_index.resize(prefab.nodes.size());
  result.morph_entities_by_node_index.resize(prefab.nodes.size());

  struct PendingDeformation {
    world::Entity entity{};
    const GltfScenePrefabPrimitive* primitive = nullptr;
  };
  std::vector<PendingDeformation> pending_deformations;
  world::Entity skin_render_transform_entity{};
  std::unordered_set<std::string> registered_materials;

  auto attach_pending_deformations = [&]() {
    for (const PendingDeformation& pending : pending_deformations) {
      if (!world.isAlive(pending.entity) || pending.primitive == nullptr) {
        continue;
      }
      std::vector<world::Entity> joint_entities;
      if (pending.primitive->skinned()) {
        joint_entities.reserve(pending.primitive->joint_node_indices.size());
        for (const uint32_t joint_node_index : pending.primitive->joint_node_indices) {
          if (joint_node_index < result.node_entities_by_index.size()) {
            joint_entities.push_back(result.node_entities_by_index[joint_node_index]);
          } else {
            joint_entities.push_back({});
          }
        }
      }

      std::vector<float> morph_weights = pending.primitive->morph_weights;
      morph_weights.resize(pending.primitive->mesh.morph_targets.size(), 0.0f);
      world.add(pending.entity, components::DeformableMeshComponent{
                                    .bind_mesh = pending.primitive->mesh,
                                    .cpu_deformed_mesh = pending.primitive->mesh,
                                    .vertex_influences = pending.primitive->vertex_influences,
                                    .joint_entities = std::move(joint_entities),
                                    .inverse_bind_matrices =
                                        pending.primitive->inverse_bind_matrices,
                                    .base_morph_weights = morph_weights,
                                    .morph_weights = morph_weights,
                                    .render_transform_entity = skin_render_transform_entity,
                                    .skin_index = pending.primitive->skin_index,
                                    .path = components::DeformationPath::Gpu,
                                    .diagnostic = "Waiting for first deformation update",
                                    .morph_weights_dirty = true,
                                    .override_render_transform = pending.primitive->skinned(),
                                    .enabled = true});
    }
  };

  std::function<std::pair<world::Entity, world::NodeId>(uint32_t, world::NodeId)> instantiate_node;
  instantiate_node = [&](uint32_t prefab_node_index,
                         world::NodeId parent_node) -> std::pair<world::Entity, world::NodeId> {
    const auto& prefab_node = prefab.nodes[prefab_node_index];
    const world::Entity entity = world.createEntity();
    world.setName(entity, nodeDisplayName(prefab_node, prefab_node_index));
    world.add(entity, components::TransformComponent{
                           prefab_node.local_position,
                           prefab_node.local_rotation,
                           prefab_node.local_scale});
    if (prefab_node.has_light) {
      world.add(entity, prefab_node.light);
    }

    const world::NodeId node_id = scene.createNode(entity);
    if (parent_node != world::Node::kInvalidId) {
      scene.reparent(node_id, parent_node);
    }
    result.entities.push_back(entity);
    if (prefab_node_index < result.node_entities_by_index.size()) {
      result.node_entities_by_index[prefab_node_index] = entity;
    }

    std::vector<size_t> combined_primitive_indices;
    combined_primitive_indices.reserve(prefab_node.primitives.size());
    for (size_t primitive_index = 0; primitive_index < prefab_node.primitives.size(); ++primitive_index) {
      const auto& primitive = prefab_node.primitives[primitive_index];
      if (!primitive.skinned() && !primitive.morphable()) {
        combined_primitive_indices.push_back(primitive_index);
        continue;
      }

      const world::Entity primitive_entity = world.createEntity();
      world.setName(primitive_entity,
                    primitiveDisplayName(prefab_node, prefab_node_index, primitive, primitive_index));
      world.add(primitive_entity, components::TransformComponent{
                                     {},
                                     {},
                                     {1.0f, 1.0f, 1.0f}});

      const std::string mesh_key =
          prefabResourceKey(asset_key_prefix, prefab_node_index, primitive_index, "mesh");
      const std::string material_key =
          primitiveMaterialKey(asset_key_prefix,
                               prefab,
                               prefab_node_index,
                               primitive_index,
                               primitive);
      registerPrimitiveMaterial(prefab,
                                prefab_node_index,
                                primitive_index,
                                primitive,
                                material_key,
                                assets,
                                registered_materials);
      world::MeshData mesh_asset = primitive.mesh;
      mesh_asset.material_slots = {world::MeshMaterialSlot{
          .name = primitive.name.empty() ? std::string("Slot 0") : primitive.name,
          .default_material_key = material_key,
      }};
      if (mesh_asset.submeshes.empty() && !mesh_asset.indices.empty()) {
        mesh_asset.submeshes.push_back(world::MeshSubmesh{
            .index_offset = 0,
            .index_count = static_cast<uint32_t>(mesh_asset.indices.size()),
            .material_slot = 0,
        });
      } else {
        for (auto& submesh : mesh_asset.submeshes) {
          submesh.material_slot = 0;
        }
      }
      if (!assets.registerMeshAsset(mesh_key, mesh_asset)) {
        continue;
      }
      world.add(primitive_entity, components::MeshComponent{
                                     .mesh_asset_key = mesh_key,
                                     .visible = true,
                                     .shadow_visible = primitive.casts_shadows});
      if (primitive.morphable()) {
        if (prefab_node_index < result.morph_entities_by_node_index.size()) {
          result.morph_entities_by_node_index[prefab_node_index].push_back(primitive_entity);
        }
      }
      if (primitive.skinned() || primitive.morphable()) {
        pending_deformations.push_back(
            PendingDeformation{.entity = primitive_entity, .primitive = &primitive});
      }

      const world::NodeId primitive_node = scene.createNode(primitive_entity);
      scene.reparent(primitive_node, node_id);
      result.entities.push_back(primitive_entity);
    }

    if (!combined_primitive_indices.empty()) {
      bool combined_casts_shadows = false;
      for (const size_t primitive_index : combined_primitive_indices) {
        combined_casts_shadows =
            combined_casts_shadows || prefab_node.primitives[primitive_index].casts_shadows;
      }

      const world::Entity mesh_entity = world.createEntity();
      world.setName(mesh_entity, nodeDisplayName(prefab_node, prefab_node_index) + " Mesh");
      world.add(mesh_entity, components::TransformComponent{
                               {},
                               {},
                               {1.0f, 1.0f, 1.0f}});
      const std::string mesh_key =
          prefabResourceKey(asset_key_prefix, prefab_node_index, 0, "mesh");
      world::MeshData combined_mesh = buildCombinedNodeMesh(prefab,
                                                               asset_key_prefix,
                                                               prefab_node_index,
                                                               prefab_node,
                                                               combined_primitive_indices,
                                                               assets,
                                                               registered_materials);
      if (!assets.registerMeshAsset(mesh_key, std::move(combined_mesh))) {
        return {entity, node_id};
      }
      world.add(mesh_entity, components::MeshComponent{
                                 .mesh_asset_key = mesh_key,
                                 .visible = true,
                                 .shadow_visible = combined_casts_shadows});

      const world::NodeId mesh_node = scene.createNode(mesh_entity);
      scene.reparent(mesh_node, node_id);
      result.entities.push_back(mesh_entity);
    }

    for (const uint32_t child_index : prefab_node.children) {
      instantiate_node(child_index, node_id);
    }

    return {entity, node_id};
  };

  if (options.create_synthetic_root) {
    const world::Entity root_entity = world.createEntity();
    const std::string root_name =
        prefab.source_path.stem().empty() ? std::string("Imported glTF") : prefab.source_path.stem().string();
    world.setName(root_entity, root_name);
    world.add(root_entity, components::TransformComponent{});
    const world::NodeId root_node = scene.createNode(root_entity);
    result.entities.push_back(root_entity);
    skin_render_transform_entity = root_entity;
    instantiate_node(prefab.root_node, root_node);
    result.root_entity = root_entity;
    result.root_node = root_node;
    attach_pending_deformations();
    if (!prefab.animations.empty()) {
      world.add(root_entity, components::AnimatorComponent{
                                 .clips = prefab.animations,
                                 .node_entities_by_index = result.node_entities_by_index,
                                 .morph_entities_by_node_index =
                                     result.morph_entities_by_node_index,
                                 .skeletons = prefab.skeletons,
                                 .skins = prefab.skins,
                                 .humanoid_rigs = prefab.humanoid_rigs,
                                 .current_clip_index = 0,
                                 .time_seconds = 0.0f,
                                 .speed = 1.0f,
                                 .loop = true,
                                 .playing = options.autoplay_animations});
    }
    updateWorldTransforms(world, scene);
    return result;
  }

  const auto [root_entity, root_node] = instantiate_node(prefab.root_node, world::Node::kInvalidId);
  result.root_entity = root_entity;
  result.root_node = root_node;
  skin_render_transform_entity = root_entity;
  attach_pending_deformations();
  if (!prefab.animations.empty()) {
    world.add(root_entity, components::AnimatorComponent{
                               .clips = prefab.animations,
                               .node_entities_by_index = result.node_entities_by_index,
                               .morph_entities_by_node_index =
                                   result.morph_entities_by_node_index,
                               .skeletons = prefab.skeletons,
                               .skins = prefab.skins,
                               .humanoid_rigs = prefab.humanoid_rigs,
                               .current_clip_index = 0,
                               .time_seconds = 0.0f,
                               .speed = 1.0f,
                               .loop = true,
                               .playing = options.autoplay_animations});
  }
  updateWorldTransforms(world, scene);
  return result;
}

GltfSceneImportResult instantiateGltfSceneAsset(
    world::World& world,
    world::Scene& scene,
    assets::AssetRegistry& assets,
    const assets::GltfSceneAsset& asset,
    const GltfSceneInstantiateOptions& options) {
  GltfSceneImportResult result{};
  if (!asset.valid()) {
    return result;
  }

  result.node_entities_by_index.resize(asset.nodes.size());
  result.morph_entities_by_node_index.resize(asset.nodes.size());

  struct PendingDeformation {
    world::Entity entity{};
    std::string mesh_key;
    const assets::GltfSceneAssetPrimitive* primitive = nullptr;
  };
  std::vector<PendingDeformation> pending_deformations;
  world::Entity skin_render_transform_entity{};

  auto vertex_influences_from_mesh = [](const world::MeshData& mesh) {
    std::vector<components::VertexSkinInfluence> influences;
    const std::size_t count = std::min(mesh.joint_indices.size(), mesh.joint_weights.size());
    influences.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
      influences.push_back(components::VertexSkinInfluence{
          .joints = mesh.joint_indices[index],
          .weights = mesh.joint_weights[index],
      });
    }
    return influences;
  };

  auto attach_pending_deformations = [&]() {
    for (const PendingDeformation& pending : pending_deformations) {
      if (!world.isAlive(pending.entity) || pending.primitive == nullptr) {
        continue;
      }
      const world::MeshData* mesh = assets.findMeshAsset(pending.mesh_key);
      if (mesh == nullptr) {
        continue;
      }

      std::vector<world::Entity> joint_entities;
      joint_entities.reserve(pending.primitive->joint_node_indices.size());
      for (const uint32_t joint_node_index : pending.primitive->joint_node_indices) {
        if (joint_node_index < result.node_entities_by_index.size()) {
          joint_entities.push_back(result.node_entities_by_index[joint_node_index]);
        } else {
          joint_entities.push_back({});
        }
      }

      std::vector<float> morph_weights = pending.primitive->morph_weights;
      morph_weights.resize(mesh->morph_targets.size(), 0.0f);
      world.add(pending.entity, components::DeformableMeshComponent{
                                    .bind_mesh = *mesh,
                                    .cpu_deformed_mesh = *mesh,
                                    .vertex_influences = vertex_influences_from_mesh(*mesh),
                                    .joint_entities = std::move(joint_entities),
                                    .inverse_bind_matrices =
                                        pending.primitive->inverse_bind_matrices,
                                    .base_morph_weights = morph_weights,
                                    .morph_weights = morph_weights,
                                    .render_transform_entity = skin_render_transform_entity,
                                    .skin_index = pending.primitive->skin_index,
                                    .path = components::DeformationPath::Gpu,
                                    .diagnostic = "Waiting for first deformation update",
                                    .morph_weights_dirty = true,
                                    .override_render_transform =
                                        !pending.primitive->joint_node_indices.empty(),
                                    .enabled = true});
    }
  };

  std::function<std::pair<world::Entity, world::NodeId>(uint32_t, world::NodeId)> instantiate_node;
  instantiate_node = [&](uint32_t node_index,
                         world::NodeId parent_node) -> std::pair<world::Entity, world::NodeId> {
    const assets::GltfSceneAssetNode& asset_node = asset.nodes[node_index];
    const world::Entity entity = world.createEntity();
    world.setName(entity, asset_node.name.empty() ? ("Node " + std::to_string(node_index))
                                                  : asset_node.name);
    world.add(entity, components::TransformComponent{
                           asset_node.local_position,
                           asset_node.local_rotation,
                           asset_node.local_scale});
    if (asset_node.has_light) {
      world.add(entity, asset_node.light);
    }

    const world::NodeId scene_node = scene.createNode(entity);
    if (parent_node != world::Node::kInvalidId) {
      scene.reparent(scene_node, parent_node);
    }
    result.entities.push_back(entity);
    result.node_entities_by_index[node_index] = entity;

    for (std::size_t primitive_index = 0; primitive_index < asset_node.primitives.size();
         ++primitive_index) {
      const assets::GltfSceneAssetPrimitive& primitive = asset_node.primitives[primitive_index];
      if (primitive.mesh_key.empty()) {
        continue;
      }

      const world::Entity primitive_entity = world.createEntity();
      const std::string primitive_name =
          primitive.name.empty()
              ? (asset_node.name.empty() ? "Primitive " + std::to_string(primitive_index)
                                         : asset_node.name + " Mesh")
              : primitive.name;
      world.setName(primitive_entity, primitive_name);
      world.add(primitive_entity, components::TransformComponent{
                                      {},
                                      {},
                                      {1.0f, 1.0f, 1.0f}});
      world.add(primitive_entity, components::MeshComponent{
                                      .mesh_asset_key = primitive.mesh_key,
                                      .visible = true,
                                      .shadow_visible = primitive.casts_shadows,
                                  });

      const world::MeshData* mesh = assets.findMeshAsset(primitive.mesh_key);
      const bool morphable = mesh != nullptr && !mesh->morph_targets.empty();
      const bool skinned =
          mesh != nullptr &&
          !primitive.joint_node_indices.empty() &&
          mesh->joint_indices.size() == mesh->vertices.size() &&
          mesh->joint_weights.size() == mesh->vertices.size();
      if (morphable && node_index < result.morph_entities_by_node_index.size()) {
        result.morph_entities_by_node_index[node_index].push_back(primitive_entity);
      }
      if (morphable || skinned) {
        pending_deformations.push_back(PendingDeformation{
            .entity = primitive_entity,
            .mesh_key = primitive.mesh_key,
            .primitive = &primitive,
        });
      }

      const world::NodeId primitive_node = scene.createNode(primitive_entity);
      scene.reparent(primitive_node, scene_node);
      result.entities.push_back(primitive_entity);
    }

    for (const uint32_t child_index : asset_node.children) {
      if (child_index < asset.nodes.size()) {
        instantiate_node(child_index, scene_node);
      }
    }

    return {entity, scene_node};
  };

  auto collect_animation_clips = [&]() {
    std::vector<world::AnimationClip> clips;
    clips.reserve(asset.animation_clip_keys.size());
    for (const std::string& key : asset.animation_clip_keys) {
      if (const world::AnimationClip* clip = assets.findAnimationClip(key)) {
        clips.push_back(*clip);
      }
    }
    return clips;
  };
  auto collect_skeletons = [&]() {
    std::vector<world::Skeleton> skeletons;
    skeletons.reserve(asset.skeleton_keys.size());
    for (const std::string& key : asset.skeleton_keys) {
      if (const world::Skeleton* skeleton = assets.findSkeleton(key)) {
        skeletons.push_back(*skeleton);
      }
    }
    return skeletons;
  };
  auto collect_skins = [&]() {
    std::vector<world::Skin> skins;
    skins.reserve(asset.skin_keys.size());
    for (const std::string& key : asset.skin_keys) {
      if (const world::Skin* skin = assets.findSkin(key)) {
        skins.push_back(*skin);
      }
    }
    return skins;
  };
  auto collect_humanoid_rigs = [&]() {
    std::vector<world::HumanoidRig> rigs;
    rigs.reserve(asset.humanoid_rig_keys.size());
    for (const std::string& key : asset.humanoid_rig_keys) {
      if (const world::HumanoidRig* rig = assets.findHumanoidRig(key)) {
        rigs.push_back(*rig);
      }
    }
    return rigs;
  };
  auto attach_animator = [&](world::Entity root_entity) {
    std::vector<world::AnimationClip> clips = collect_animation_clips();
    if (clips.empty()) {
      return;
    }
    world.add(root_entity, components::AnimatorComponent{
                               .clips = std::move(clips),
                               .node_entities_by_index = result.node_entities_by_index,
                               .morph_entities_by_node_index =
                                   result.morph_entities_by_node_index,
                               .skeletons = collect_skeletons(),
                               .skins = collect_skins(),
                               .humanoid_rigs = collect_humanoid_rigs(),
                               .current_clip_index = 0,
                               .time_seconds = 0.0f,
                               .speed = 1.0f,
                               .loop = true,
                               .playing = options.autoplay_animations});
  };

  if (options.create_synthetic_root) {
    const world::Entity root_entity = world.createEntity();
    const std::string root_name =
        asset.source_path.stem().empty() ? std::string("Cached glTF")
                                         : asset.source_path.stem().string();
    world.setName(root_entity, root_name);
    world.add(root_entity, components::TransformComponent{});
    const world::NodeId root_node = scene.createNode(root_entity);
    result.entities.push_back(root_entity);
    skin_render_transform_entity = root_entity;
    instantiate_node(asset.root_node, root_node);
    result.root_entity = root_entity;
    result.root_node = root_node;
    attach_pending_deformations();
    attach_animator(root_entity);
    updateWorldTransforms(world, scene);
    return result;
  }

  const auto [root_entity, root_node] =
      instantiate_node(asset.root_node, world::Node::kInvalidId);
  result.root_entity = root_entity;
  result.root_node = root_node;
  skin_render_transform_entity = root_entity;
  attach_pending_deformations();
  attach_animator(root_entity);
  updateWorldTransforms(world, scene);
  return result;
}

GltfSceneImportResult importGltfScene(world::World& world,
                                    world::Scene& scene,
                                    assets::AssetRegistry& assets,
                                    const std::filesystem::path& path,
                                    const GltfSceneImportOptions& options) {
  const GltfScenePrefab prefab = loadGltfScenePrefab(path, options.load);
  return instantiateGltfScenePrefab(world, scene, assets, prefab, options.instantiate);
}

}  // namespace karma::world
