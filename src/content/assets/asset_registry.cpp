#include "karma/content/assets/asset_registry.h"

#include <cstddef>
#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <utility>

#include <nlohmann/json.hpp>

#if defined(KARMA_ENABLE_KTX2)
#include <ktx.h>
#if __has_include(<vkformat_enum.h>)
#include <vkformat_enum.h>
#endif
#if !defined(KARMA_KTX_SOFTWARE_TAG)
#define KARMA_KTX_SOFTWARE_TAG "system"
#endif
#endif

#include "karma/content/assets/asset_cache.h"
#include "karma/content/image/image.h"
#include "karma/features/visual/particles/effect_asset.h"

#include "asset_source_import.h"
#include "../importers/gltf_scene_import_internal.h"
#include "../importers/mesh_import_internal.h"
#include "material_registry_backing.h"
#include "post_process_profile_registry_backing.h"

namespace karma::content {

namespace {

std::string lowercase(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return value;
}

bool hasKnownSourceExtension(std::string_view key) {
  const std::filesystem::path path{std::string(key)};
  const std::string ext = lowercase(path.extension().string());
  if (ext.empty()) {
    return false;
  }
  static constexpr std::array<std::string_view, 20> kSourceExtensions{
      ".glb", ".gltf", ".obj", ".fbx", ".dae", ".png", ".jpg", ".jpeg", ".tga",
      ".bmp", ".hdr", ".exr", ".ktx", ".dds", ".mat", ".json", ".kpeffect",
      ".wav", ".ogg", ".mp3"};
  return std::find(kSourceExtensions.begin(), kSourceExtensions.end(), ext) !=
         kSourceExtensions.end();
}

bool hasDotDotSegment(std::string_view key) {
  std::size_t segment_start = 0;
  while (segment_start <= key.size()) {
    const std::size_t slash = key.find('/', segment_start);
    const std::size_t segment_end = slash == std::string_view::npos ? key.size() : slash;
    if (key.substr(segment_start, segment_end - segment_start) == "..") {
      return true;
    }
    if (slash == std::string_view::npos) {
      break;
    }
    segment_start = slash + 1;
  }
  return false;
}

std::string childKey(const std::string& parent,
                     std::string_view kind,
                     std::size_t index) {
  return parent + "/" + std::string(kind) + "/" + std::to_string(index);
}

void assignSingleMaterialSlot(geometry::MeshData& mesh,
                              std::string slot_name,
                              std::string material_key) {
  mesh.material_slots = {geometry::MeshMaterialSlot{
      .name = std::move(slot_name),
      .default_material_key = std::move(material_key),
  }};
  if (mesh.submeshes.empty() && !mesh.indices.empty()) {
    mesh.submeshes.push_back(geometry::MeshSubmesh{
        .index_offset = 0u,
        .index_count = static_cast<uint32_t>(mesh.indices.size()),
        .material_slot = 0u,
    });
    return;
  }
  for (geometry::MeshSubmesh& submesh : mesh.submeshes) {
    submesh.material_slot = 0u;
  }
}

geometry::MeshData combineMeshes(std::vector<geometry::MeshData> meshes) {
  geometry::MeshData combined{};
  for (geometry::MeshData& mesh : meshes) {
    if (mesh.vertices.empty() || mesh.indices.empty()) {
      continue;
    }

    const uint32_t vertex_base = static_cast<uint32_t>(combined.vertices.size());
    const uint32_t index_base = static_cast<uint32_t>(combined.indices.size());
    combined.vertices.insert(combined.vertices.end(), mesh.vertices.begin(), mesh.vertices.end());
    combined.normals.insert(combined.normals.end(), mesh.normals.begin(), mesh.normals.end());
    combined.uvs.insert(combined.uvs.end(), mesh.uvs.begin(), mesh.uvs.end());
    combined.uvs1.insert(combined.uvs1.end(), mesh.uvs1.begin(), mesh.uvs1.end());
    combined.tangents.insert(combined.tangents.end(), mesh.tangents.begin(), mesh.tangents.end());
    combined.joint_indices.insert(combined.joint_indices.end(),
                                  mesh.joint_indices.begin(),
                                  mesh.joint_indices.end());
    combined.joint_weights.insert(combined.joint_weights.end(),
                                  mesh.joint_weights.begin(),
                                  mesh.joint_weights.end());

    for (uint32_t index : mesh.indices) {
      combined.indices.push_back(vertex_base + index);
    }

    const uint32_t material_slot_base =
        static_cast<uint32_t>(combined.material_slots.size());
    combined.material_slots.insert(combined.material_slots.end(),
                                   mesh.material_slots.begin(),
                                   mesh.material_slots.end());
    if (!mesh.submeshes.empty()) {
      for (const geometry::MeshSubmesh& submesh : mesh.submeshes) {
        combined.submeshes.push_back(geometry::MeshSubmesh{
            .index_offset = index_base + submesh.index_offset,
            .index_count = submesh.index_count,
            .material_slot = material_slot_base + submesh.material_slot,
        });
      }
    } else {
      combined.submeshes.push_back(geometry::MeshSubmesh{
          .index_offset = index_base,
          .index_count = static_cast<uint32_t>(mesh.indices.size()),
          .material_slot = material_slot_base,
      });
    }
  }
  return combined;
}

template <typename T>
const T* findInMap(const std::unordered_map<std::string, T>& map,
                   std::string_view key) {
  const auto it = map.find(std::string(key));
  return it != map.end() ? &it->second : nullptr;
}

template <typename T>
void appendRaw(std::vector<uint8_t>& out, const T& value) {
  const auto* bytes = reinterpret_cast<const uint8_t*>(&value);
  out.insert(out.end(), bytes, bytes + sizeof(T));
}

template <typename T>
void appendVectorRaw(std::vector<uint8_t>& out, const std::vector<T>& values) {
  const uint64_t size = static_cast<uint64_t>(values.size());
  appendRaw(out, size);
  if (!values.empty()) {
    const auto* bytes = reinterpret_cast<const uint8_t*>(values.data());
    out.insert(out.end(), bytes, bytes + sizeof(T) * values.size());
  }
}

std::string meshContentHash(const geometry::MeshData& mesh) {
  std::vector<uint8_t> bytes;
  appendVectorRaw(bytes, mesh.vertices);
  appendVectorRaw(bytes, mesh.normals);
  appendVectorRaw(bytes, mesh.uvs);
  appendVectorRaw(bytes, mesh.uvs1);
  appendVectorRaw(bytes, mesh.tangents);
  appendVectorRaw(bytes, mesh.indices);
  appendVectorRaw(bytes, mesh.joint_indices);
  appendVectorRaw(bytes, mesh.joint_weights);
  appendVectorRaw(bytes, mesh.submeshes);
  return hashBytes(bytes.data(), bytes.size());
}

std::string textureContentHash(const TextureAsset& texture) {
  if (!texture.content_hash.empty()) {
    return texture.content_hash;
  }
  if (!texture.bytes.empty()) {
    return hashBytes(texture.bytes.data(), texture.bytes.size());
  }
  if (!texture.fallback_rgba8.empty()) {
    return hashBytes(texture.fallback_rgba8.data(), texture.fallback_rgba8.size());
  }
  return hashString("empty-texture");
}

#if defined(KARMA_ENABLE_KTX2)
struct KtxTexture2Deleter {
  void operator()(ktxTexture2* texture) const {
    if (texture != nullptr) {
      ktxTexture2_Destroy(texture);
    }
  }
};

#if defined(VK_FORMAT_R8G8B8A8_UNORM)
constexpr ktx_uint32_t kKtxVkFormatRgba8Unorm = VK_FORMAT_R8G8B8A8_UNORM;
#else
constexpr ktx_uint32_t kKtxVkFormatRgba8Unorm = 37u;
#endif

#if defined(VK_FORMAT_R8G8B8A8_SRGB)
constexpr ktx_uint32_t kKtxVkFormatRgba8Srgb = VK_FORMAT_R8G8B8A8_SRGB;
#else
constexpr ktx_uint32_t kKtxVkFormatRgba8Srgb = 43u;
#endif
#endif

std::string_view textureImporterVersion() {
#if defined(KARMA_ENABLE_KTX2)
  return "texture-ktx2-uastc-v2";
#else
  return "texture-rgba8-v3";
#endif
}

std::string_view textureDependencyVersion() {
#if defined(KARMA_ENABLE_KTX2)
  return "libktx:" KARMA_KTX_SOFTWARE_TAG;
#else
  return "no-ktx2";
#endif
}

bool envFlagOff(const char* value) {
  if (value == nullptr || value[0] == '\0') {
    return false;
  }
  const std::string_view text(value);
  return text == "0" || text == "false" || text == "FALSE" ||
         text == "off" || text == "OFF";
}

bool envFlagOn(const char* value) {
  if (value == nullptr || value[0] == '\0') {
    return false;
  }
  return !envFlagOff(value);
}

bool runtimeBc7Enabled() {
  return envFlagOn(std::getenv("KARMA_TEXTURE_BC7"));
}

std::optional<std::vector<uint8_t>> encodeKtx2Uastc(const Rgba8Image& image,
                                                    bool srgb,
                                                    bool generate_mips,
                                                    bool normal_map) {
#if defined(KARMA_ENABLE_KTX2)
  if (!image.valid()) {
    return std::nullopt;
  }

  ktxTextureCreateInfo create_info{};
  create_info.glInternalformat = 0u;
  create_info.vkFormat = srgb ? kKtxVkFormatRgba8Srgb : kKtxVkFormatRgba8Unorm;
  create_info.baseWidth = static_cast<ktx_uint32_t>(image.width);
  create_info.baseHeight = static_cast<ktx_uint32_t>(image.height);
  create_info.baseDepth = 1u;
  create_info.numDimensions = 2u;
  create_info.numLevels = 1u;
  create_info.numLayers = 1u;
  create_info.numFaces = 1u;
  create_info.isArray = KTX_FALSE;
  create_info.generateMipmaps = generate_mips ? KTX_TRUE : KTX_FALSE;

  ktxTexture2* raw_texture = nullptr;
  if (ktxTexture2_Create(&create_info,
                         KTX_TEXTURE_CREATE_ALLOC_STORAGE,
                         &raw_texture) != KTX_SUCCESS ||
      raw_texture == nullptr) {
    return std::nullopt;
  }
  std::unique_ptr<ktxTexture2, KtxTexture2Deleter> texture(raw_texture);

  if (ktxTexture_SetImageFromMemory(ktxTexture(texture.get()),
                                    0u,
                                    0u,
                                    0u,
                                    image.pixels.data(),
                                    image.pixels.size()) != KTX_SUCCESS) {
    return std::nullopt;
  }

  ktxBasisParams params{};
  params.structSize = sizeof(params);
  params.uastc = KTX_TRUE;
  params.compressionLevel = KTX_ETC1S_DEFAULT_COMPRESSION_LEVEL;
  params.threadCount = std::max(1u, std::thread::hardware_concurrency());
  params.normalMap = normal_map && !srgb ? KTX_TRUE : KTX_FALSE;
  params.uastcFlags = KTX_PACK_UASTC_LEVEL_DEFAULT;
  if (ktxTexture2_CompressBasisEx(texture.get(), &params) != KTX_SUCCESS) {
    return std::nullopt;
  }

  (void)ktxTexture2_DeflateZstd(texture.get(), 5u);

  ktx_uint8_t* out_bytes = nullptr;
  ktx_size_t out_size = 0u;
  if (ktxTexture2_WriteToMemory(texture.get(), &out_bytes, &out_size) != KTX_SUCCESS ||
      out_bytes == nullptr || out_size == 0u) {
    if (out_bytes != nullptr) {
      std::free(out_bytes);
    }
    return std::nullopt;
  }

  std::vector<uint8_t> bytes(out_bytes, out_bytes + out_size);
  std::free(out_bytes);
  return bytes;
#else
  (void)image;
  (void)srgb;
  (void)generate_mips;
  (void)normal_map;
  return std::nullopt;
#endif
}

std::optional<PreparedTextureUpload> prepareRgba8Upload(const TextureAsset& texture) {
  const std::vector<uint8_t>* bytes = nullptr;
  if (texture.payload_format == TextureAsset::PayloadFormat::RGBA8 && !texture.bytes.empty()) {
    bytes = &texture.bytes;
  } else if (!texture.fallback_rgba8.empty()) {
    bytes = &texture.fallback_rgba8;
  }
  if (bytes == nullptr ||
      texture.desc.width <= 0 ||
      texture.desc.height <= 0) {
    return std::nullopt;
  }
  const std::size_t expected =
      static_cast<std::size_t>(texture.desc.width) *
      static_cast<std::size_t>(texture.desc.height) * 4u;
  if (bytes->size() < expected) {
    return std::nullopt;
  }

  PreparedTextureUpload prepared{};
  prepared.desc = texture.desc;
  prepared.desc.format = renderer::TextureFormat::RGBA8;
  prepared.desc.mip_levels = 1u;
  prepared.upload.format = renderer::TextureFormat::RGBA8;
  prepared.upload.bytes.assign(bytes->begin(),
                               bytes->begin() + static_cast<std::ptrdiff_t>(expected));
  prepared.upload.subresources.push_back(renderer::TextureUploadSubresource{
      .mip_level = 0u,
      .array_layer = 0u,
      .width = prepared.desc.width,
      .height = prepared.desc.height,
      .offset = 0u,
      .size = expected,
      .row_stride = static_cast<std::size_t>(prepared.desc.width) * 4u,
  });
  return prepared;
}

#if defined(KARMA_ENABLE_KTX2)
std::size_t bc7RowStride(int width) {
  const std::size_t blocks_x = (static_cast<std::size_t>(std::max(width, 1)) + 3u) / 4u;
  return blocks_x * 16u;
}

std::optional<PreparedTextureUpload> transcodeKtx2Upload(const TextureAsset& texture,
                                                         bool bc7) {
  if (texture.payload_format != TextureAsset::PayloadFormat::KTX2_BASIS_UASTC ||
      texture.bytes.empty()) {
    return std::nullopt;
  }

  ktxTexture2* raw_texture = nullptr;
  if (ktxTexture2_CreateFromMemory(texture.bytes.data(),
                                   texture.bytes.size(),
                                   KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT,
                                   &raw_texture) != KTX_SUCCESS ||
      raw_texture == nullptr) {
    return std::nullopt;
  }
  std::unique_ptr<ktxTexture2, KtxTexture2Deleter> ktx(raw_texture);

  const ktx_transcode_fmt_e target = bc7 ? KTX_TTF_BC7_RGBA : KTX_TTF_RGBA32;
  if (ktxTexture2_NeedsTranscoding(ktx.get()) &&
      ktxTexture2_TranscodeBasis(ktx.get(), target, 0u) != KTX_SUCCESS) {
    return std::nullopt;
  }

  ktxTexture* base_texture = ktxTexture(ktx.get());
  ktx_uint8_t* data = ktxTexture_GetData(base_texture);
  const ktx_size_t data_size = ktxTexture_GetDataSize(base_texture);
  if (data == nullptr || data_size == 0u) {
    return std::nullopt;
  }

  PreparedTextureUpload prepared{};
  prepared.desc = texture.desc;
  prepared.desc.width = static_cast<int>(ktx->baseWidth);
  prepared.desc.height = static_cast<int>(ktx->baseHeight);
  prepared.desc.generate_mips = false;
  prepared.desc.mip_levels = std::max(1u, static_cast<uint32_t>(ktx->numLevels));
  prepared.desc.format = bc7 ? (texture.desc.srgb ? renderer::TextureFormat::BC7_RGBA_UNORM_SRGB
                                                  : renderer::TextureFormat::BC7_RGBA_UNORM)
                             : renderer::TextureFormat::RGBA8;
  prepared.upload.format = prepared.desc.format;
  prepared.upload.bytes.assign(data, data + data_size);
  prepared.upload.subresources.reserve(prepared.desc.mip_levels);
  for (uint32_t level = 0u; level < prepared.desc.mip_levels; ++level) {
    ktx_size_t offset = 0u;
    if (ktxTexture_GetImageOffset(base_texture, level, 0u, 0u, &offset) != KTX_SUCCESS ||
        offset >= data_size) {
      return std::nullopt;
    }
    const int width =
        std::max(1, static_cast<int>(prepared.desc.width) >> static_cast<int>(level));
    const int height =
        std::max(1, static_cast<int>(prepared.desc.height) >> static_cast<int>(level));
    const std::size_t image_size = static_cast<std::size_t>(
        ktxTexture_GetImageSize(base_texture, level));
    if (image_size == 0u || image_size > prepared.upload.bytes.size() - offset) {
      return std::nullopt;
    }
    prepared.upload.subresources.push_back(renderer::TextureUploadSubresource{
        .mip_level = level,
        .array_layer = 0u,
        .width = width,
        .height = height,
        .offset = static_cast<std::size_t>(offset),
        .size = image_size,
        .row_stride = bc7 ? bc7RowStride(width)
                          : static_cast<std::size_t>(ktxTexture_GetRowPitch(base_texture, level)),
    });
  }
  return prepared;
}
#endif

TextureAsset makeTextureAssetFromImage(Rgba8Image image,
                                       bool srgb,
                                       bool generate_mips,
                                       TextureAsset::Semantic semantic,
                                       bool prefer_compressed) {
  TextureAsset texture{};
  texture.desc.width = image.width;
  texture.desc.height = image.height;
  texture.desc.format = renderer::TextureFormat::RGBA8;
  texture.desc.srgb = srgb;
  texture.desc.generate_mips = generate_mips;
  texture.semantic = semantic;

  if (prefer_compressed) {
    auto ktx2 = encodeKtx2Uastc(image,
                                srgb,
                                generate_mips,
                                semantic == TextureAsset::Semantic::Normal);
    if (ktx2.has_value()) {
      texture.payload_format = TextureAsset::PayloadFormat::KTX2_BASIS_UASTC;
      texture.bytes = std::move(*ktx2);
      texture.fallback_rgba8 = std::move(image.pixels);
      texture.content_hash = textureContentHash(texture);
      return texture;
    }
  }

  texture.payload_format = TextureAsset::PayloadFormat::RGBA8;
  texture.bytes = std::move(image.pixels);
  texture.subresources.push_back(renderer::TextureUploadSubresource{
      .mip_level = 0u,
      .array_layer = 0u,
      .width = texture.desc.width,
      .height = texture.desc.height,
      .offset = 0u,
      .size = texture.bytes.size(),
      .row_stride = static_cast<std::size_t>(texture.desc.width) * 4u,
  });
  texture.content_hash = textureContentHash(texture);
  return texture;
}

std::string sanitizeTextureKeySegment(std::string_view value) {
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
  return out.empty() ? std::string("texture") : out;
}

std::string importedTextureAlias(renderer::ImportedMaterialTextureSemantic semantic,
                                 std::string_view fallback) {
  using Semantic = renderer::ImportedMaterialTextureSemantic;
  switch (semantic) {
    case Semantic::BaseColor:
      return "base_color";
    case Semantic::Normal:
      return "normal";
    case Semantic::MetallicRoughness:
      return "metallic_roughness";
    case Semantic::Occlusion:
      return "occlusion";
    case Semantic::Emissive:
      return "emissive";
    case Semantic::Clearcoat:
      return "clearcoat";
    case Semantic::ClearcoatRoughness:
      return "clearcoat_roughness";
    case Semantic::ClearcoatNormal:
      return "clearcoat_normal";
    case Semantic::SheenColor:
      return "sheen_color";
    case Semantic::SheenRoughness:
      return "sheen_roughness";
    case Semantic::Transmission:
      return "transmission";
    case Semantic::Thickness:
      return "thickness";
  }
  return sanitizeTextureKeySegment(fallback);
}

TextureAsset::Semantic importedTextureSemantic(renderer::ImportedMaterialTextureSemantic semantic,
                                               bool srgb) {
  using Semantic = renderer::ImportedMaterialTextureSemantic;
  if (semantic == Semantic::Normal ||
      semantic == Semantic::ClearcoatNormal) {
    return TextureAsset::Semantic::Normal;
  }
  if (srgb) {
    return TextureAsset::Semantic::Color;
  }
  if (semantic == Semantic::BaseColor || semantic == Semantic::Emissive ||
      semantic == Semantic::SheenColor) {
    return TextureAsset::Semantic::Linear;
  }
  return TextureAsset::Semantic::Data;
}

std::optional<Rgba8Image> decodeImportedTexture(
    const renderer::ImportedMaterialTexture& texture) {
  if (texture.embedded) {
    if (texture.source_bytes.empty()) {
      return std::nullopt;
    }
    if (texture.compressed) {
      return loadRgba8ImageFromMemory(texture.source_bytes.data(),
                                      texture.source_bytes.size(),
                                      Rgba8ImageLoadOptions{.flip_y = true});
    }
    Rgba8Image image{};
    image.width = static_cast<int>(texture.width);
    image.height = static_cast<int>(texture.height);
    const std::size_t expected =
        static_cast<std::size_t>(image.width) * static_cast<std::size_t>(image.height) * 4u;
    if (image.width <= 0 || image.height <= 0 || texture.source_bytes.size() < expected) {
      return std::nullopt;
    }
    image.pixels.assign(texture.source_bytes.begin(),
                        texture.source_bytes.begin() + static_cast<std::ptrdiff_t>(expected));
    return image.valid() ? std::optional<Rgba8Image>{std::move(image)} : std::nullopt;
  }
  return loadRgba8Image(texture.resolved_path, Rgba8ImageLoadOptions{.flip_y = true});
}

std::string importedTextureSourceHash(const renderer::ImportedMaterialTexture& texture) {
  if (texture.embedded && !texture.source_bytes.empty()) {
    return hashBytes(texture.source_bytes.data(), texture.source_bytes.size());
  }
  if (!texture.resolved_path.empty()) {
    if (auto file_hash = hashFile(texture.resolved_path)) {
      return *file_hash;
    }
  }
  return hashString(texture.source_key);
}

}  // namespace

std::optional<PreparedTextureUpload> prepareTextureUpload(
    const TextureAsset& texture,
    TextureRuntimeCapabilities capabilities) {
  if (texture.desc.width <= 0 || texture.desc.height <= 0) {
    return std::nullopt;
  }

  if (texture.payload_format == TextureAsset::PayloadFormat::KTX2_BASIS_UASTC) {
#if defined(KARMA_ENABLE_KTX2)
    const bool supports_bc7 = runtimeBc7Enabled() &&
                              (texture.desc.srgb ? capabilities.bc7_srgb
                                                 : capabilities.bc7_unorm);
    if (supports_bc7) {
      if (auto prepared = transcodeKtx2Upload(texture, true)) {
        return prepared;
      }
    }
    if (auto prepared = prepareRgba8Upload(texture)) {
      return prepared;
    }
    if (auto prepared = transcodeKtx2Upload(texture, false)) {
      return prepared;
    }
#endif
    return prepareRgba8Upload(texture);
  }

  if (texture.payload_format == TextureAsset::PayloadFormat::RGBA8) {
    if (!texture.subresources.empty() && !texture.bytes.empty()) {
      PreparedTextureUpload prepared{};
      prepared.desc = texture.desc;
      prepared.desc.format = renderer::TextureFormat::RGBA8;
      prepared.upload.format = renderer::TextureFormat::RGBA8;
      prepared.upload.subresources = texture.subresources;
      prepared.upload.bytes = texture.bytes;
      return prepared.valid() ? std::optional<PreparedTextureUpload>{std::move(prepared)}
                              : std::nullopt;
    }
    return prepareRgba8Upload(texture);
  }

  return prepareRgba8Upload(texture);
}

struct AssetRegistry::Impl {
  std::unordered_map<std::string, std::shared_ptr<geometry::MeshData>> meshes;
  std::unordered_map<std::string, std::shared_ptr<TextureAsset>> textures;
  std::unordered_map<std::string, std::weak_ptr<geometry::MeshData>> mesh_payloads_by_hash;
  std::unordered_map<std::string, std::weak_ptr<TextureAsset>> texture_payloads_by_hash;
  std::unordered_map<std::string, std::string> imported_texture_keys_by_source_hash;
  std::unordered_map<std::string, particles::ParticleEffectAsset> particle_effects;
  std::unordered_map<std::string, AudioClipAsset> audio_clips;
  std::unordered_map<std::string, EnvironmentMapAsset> environment_maps;
  std::unordered_map<std::string, animation::AnimationClip> animation_clips;
  std::unordered_map<std::string, animation::Skeleton> skeletons;
  std::unordered_map<std::string, animation::Skin> skins;
  std::unordered_map<std::string, GltfSceneAsset> gltf_scenes;
  renderer::MaterialLibrary materials;
  renderer::PostProcessProfileLibrary post_process_profiles;
  uint64_t version = 0;
  uint64_t mesh_version = 0;
  uint64_t texture_version = 0;
};

AssetRegistry::AssetRegistry() : impl_(std::make_unique<Impl>()) {}

AssetRegistry::~AssetRegistry() = default;

AssetRegistry::AssetRegistry(AssetRegistry&&) noexcept = default;

AssetRegistry& AssetRegistry::operator=(AssetRegistry&&) noexcept = default;

void AssetRegistry::bumpVersion() {
  impl_->version += 1;
}

void AssetRegistry::bumpMeshVersion() {
  impl_->mesh_version += 1;
  bumpVersion();
}

void AssetRegistry::bumpTextureVersion() {
  impl_->texture_version += 1;
  bumpVersion();
}

uint64_t AssetRegistry::version() const {
  return impl_->version;
}

uint64_t AssetRegistry::meshVersion() const {
  return impl_->mesh_version;
}

uint64_t AssetRegistry::textureVersion() const {
  return impl_->texture_version;
}

bool AssetRegistry::isValidAssetKey(std::string_view key) {
  return assetKeyValidationError(key).empty();
}

std::string AssetRegistry::assetKeyValidationError(std::string_view key) {
  if (key.empty()) {
    return "asset key must not be empty";
  }
  if (key.front() == '/' || std::filesystem::path{std::string(key)}.is_absolute()) {
    return "asset key must be a logical identifier, not an absolute path";
  }
  if (key.find('\\') != std::string_view::npos) {
    return "asset key must use '/' namespace separators, not backslashes";
  }
  if (key.find(':') != std::string_view::npos) {
    return "asset key must not contain drive or URI separators";
  }
  if (hasDotDotSegment(key)) {
    return "asset key must not contain '..' path segments";
  }
  if (hasKnownSourceExtension(key)) {
    return "asset key must not be a source file path";
  }
  return {};
}

void AssetRegistry::clear() {
  impl_->meshes.clear();
  impl_->textures.clear();
  impl_->mesh_payloads_by_hash.clear();
  impl_->texture_payloads_by_hash.clear();
  impl_->imported_texture_keys_by_source_hash.clear();
  impl_->particle_effects.clear();
  impl_->audio_clips.clear();
  impl_->environment_maps.clear();
  impl_->animation_clips.clear();
  impl_->skeletons.clear();
  impl_->skins.clear();
  impl_->gltf_scenes.clear();
  impl_->materials.clear();
  impl_->post_process_profiles = renderer::PostProcessProfileLibrary{};
  bumpMeshVersion();
  bumpTextureVersion();
}

bool AssetRegistry::registerMeshAsset(const std::string& key, geometry::MeshData mesh) {
  if (!isValidAssetKey(key)) {
    return false;
  }
  const std::string hash = meshContentHash(mesh);
  std::shared_ptr<geometry::MeshData> payload;
  if (auto existing = impl_->mesh_payloads_by_hash[hash].lock()) {
    payload = std::move(existing);
  } else {
    payload = std::make_shared<geometry::MeshData>(std::move(mesh));
    impl_->mesh_payloads_by_hash[hash] = payload;
  }
  impl_->meshes[key] = std::move(payload);
  bumpMeshVersion();
  return true;
}

namespace detail {

bool importMeshAsset(AssetRegistry& assets,
                     const std::string& key,
                     const std::filesystem::path& path) {
  if (!AssetRegistry::isValidAssetKey(key)) {
    return false;
  }
  std::vector<geometry::MeshData> imported = importMeshes(path);
  geometry::MeshData combined = combineMeshes(std::move(imported));
  if (combined.vertices.empty() || combined.indices.empty()) {
    return false;
  }
  return assets.registerMeshAsset(key, std::move(combined));
}

bool importTextureAsset(AssetRegistry& assets,
                        const std::string& key,
                        const std::filesystem::path& path,
                        const TextureImportOptions& options) {
  if (!AssetRegistry::isValidAssetKey(key)) {
    return false;
  }
  AssetCache cache;
  const nlohmann::json cache_options{
      {"srgb", options.srgb},
      {"generate_mips", options.generate_mips},
      {"semantic", static_cast<uint32_t>(options.semantic)},
      {"prefer_compressed", options.prefer_compressed},
      {"texture_profile", "ktx2_basis_uastc_zstd_rgba8_fallback"},
  };
  const std::string cache_key =
      cache.makeSourceKey(path,
                          textureImporterVersion(),
                          cache_options,
                          {},
                          textureDependencyVersion());
  if (auto cached = cache.readTexture(cache_key)) {
    return assets.registerTextureAsset(key, std::move(*cached));
  }

  std::optional<Rgba8Image> image = loadRgba8Image(path);
  if (!image.has_value() || !image->valid()) {
    return false;
  }
  TextureAsset texture = makeTextureAssetFromImage(std::move(*image),
                                                   options.srgb,
                                                   options.generate_mips,
                                                   options.semantic,
                                                   options.prefer_compressed);
  std::string diagnostic;
  (void)cache.writeTexture(cache_key, texture, &diagnostic);
  return assets.registerTextureAsset(key, std::move(texture));
}

bool importParticleEffect(AssetRegistry& assets,
                          const std::string& key,
                          const std::filesystem::path& path) {
  if (!AssetRegistry::isValidAssetKey(key)) {
    return false;
  }
  particles::ParticleEffectAsset effect{};
  if (!particles::loadParticleEffectAsset(path, effect)) {
    return false;
  }
  return assets.registerParticleEffect(key, std::move(effect));
}

GltfSceneAsset importGltfSceneAsset(AssetRegistry& assets,
                                    const std::string& key,
                                    const std::filesystem::path& path,
                                    const scene::GltfSceneLoadOptions& options) {
  if (!AssetRegistry::isValidAssetKey(key)) {
    return {};
  }
  scene::GltfScenePrefab prefab = scene::loadGltfScenePrefab(path, options);
  if (!prefab.valid()) {
    return {};
  }

  GltfSceneAsset asset{};
  asset.source_path = prefab.source_path;
  asset.scene_key = key + "/scene";
  asset.root_node = prefab.root_node;
  asset.nodes.reserve(prefab.nodes.size());

  std::size_t mesh_index = 0;
  std::size_t material_index = 0;
  for (const scene::GltfScenePrefabNode& node : prefab.nodes) {
    GltfSceneAssetNode asset_node{};
    asset_node.name = node.name;
    asset_node.local_position = node.local_position;
    asset_node.local_rotation = node.local_rotation;
    asset_node.local_scale = node.local_scale;
    asset_node.world_position = node.world_position;
    asset_node.world_rotation = node.world_rotation;
    asset_node.world_scale = node.world_scale;
    asset_node.has_light = node.has_light;
    asset_node.light = node.light;
    asset_node.children = node.children;
    asset_node.primitives.reserve(node.primitives.size());

    for (const scene::GltfScenePrefabPrimitive& primitive : node.primitives) {
      const std::string mesh_key = childKey(key, "meshes", mesh_index++);
      const std::string material_key = childKey(key, "materials", material_index++);
      renderer::MaterialAssetDesc material{};
      material.surface = primitive.material;
      material.material_asset_path = prefab.source_path;
      material.material_asset_index = primitive.source_material_index;
      if (primitive.source_material_index < prefab.imported_materials.size()) {
        material.imported_material = prefab.imported_materials[primitive.source_material_index];
      }
      std::vector<std::string> texture_keys =
          assets.registerImportedMaterialTextures(material_key, material);
      if (!assets.registerMaterialAsset(material_key, std::move(material))) {
        return {};
      }
      asset.texture_asset_keys.insert(asset.texture_asset_keys.end(),
                                      texture_keys.begin(),
                                      texture_keys.end());
      asset.material_keys.push_back(material_key);

      geometry::MeshData mesh = primitive.mesh;
      assignSingleMaterialSlot(mesh,
                               primitive.name.empty() ? std::string("Slot 0") : primitive.name,
                               material_key);
      if (!assets.registerMeshAsset(mesh_key, std::move(mesh))) {
        return {};
      }
      asset.mesh_asset_keys.push_back(mesh_key);

      asset_node.primitives.push_back(GltfSceneAssetPrimitive{
          .name = primitive.name,
          .mesh_key = mesh_key,
          .material_key = material_key,
          .skin_index = primitive.skin_index,
          .morph_weights = primitive.morph_weights,
          .joint_node_indices = primitive.joint_node_indices,
          .inverse_bind_matrices = primitive.inverse_bind_matrices,
      });
    }
    asset.nodes.push_back(std::move(asset_node));
  }

  for (std::size_t i = 0; i < prefab.animations.size(); ++i) {
    const std::string clip_key = childKey(key, "animation_clips", i);
    if (!assets.registerAnimationClip(clip_key, prefab.animations[i])) {
      return {};
    }
    asset.animation_clip_keys.push_back(clip_key);
  }
  for (std::size_t i = 0; i < prefab.skeletons.size(); ++i) {
    const std::string skeleton_key = childKey(key, "skeletons", i);
    if (!assets.registerSkeleton(skeleton_key, prefab.skeletons[i])) {
      return {};
    }
    asset.skeleton_keys.push_back(skeleton_key);
  }
  for (std::size_t i = 0; i < prefab.skins.size(); ++i) {
    const std::string skin_key = childKey(key, "skins", i);
    if (!assets.registerSkin(skin_key, prefab.skins[i])) {
      return {};
    }
    asset.skin_keys.push_back(skin_key);
  }

  if (!assets.registerGltfSceneAsset(key, asset)) {
    return {};
  }
  return asset;
}

}  // namespace detail

bool AssetRegistry::unregisterMeshAsset(const std::string& key) {
  if (impl_->meshes.erase(key) == 0) {
    return false;
  }
  bumpMeshVersion();
  return true;
}

const geometry::MeshData* AssetRegistry::findMeshAsset(std::string_view key) const {
  const auto it = impl_->meshes.find(std::string(key));
  return it != impl_->meshes.end() && it->second ? it->second.get() : nullptr;
}

bool AssetRegistry::registerTextureAsset(const std::string& key, TextureAsset texture) {
  if (!isValidAssetKey(key)) {
    return false;
  }
  texture.content_hash = textureContentHash(texture);
  std::shared_ptr<TextureAsset> payload;
  if (auto existing = impl_->texture_payloads_by_hash[texture.content_hash].lock()) {
    payload = std::move(existing);
  } else {
    payload = std::make_shared<TextureAsset>(std::move(texture));
    impl_->texture_payloads_by_hash[payload->content_hash] = payload;
  }
  impl_->textures[key] = std::move(payload);
  bumpTextureVersion();
  return true;
}

bool AssetRegistry::unregisterTextureAsset(const std::string& key) {
  if (impl_->textures.erase(key) == 0) {
    return false;
  }
  bumpTextureVersion();
  return true;
}

const TextureAsset* AssetRegistry::findTextureAsset(std::string_view key) const {
  const auto it = impl_->textures.find(std::string(key));
  return it != impl_->textures.end() && it->second ? it->second.get() : nullptr;
}

std::vector<std::string> AssetRegistry::registerImportedMaterialTextures(
    const std::string& material_key,
    renderer::MaterialAssetDesc& material) {
  (void)material_key;
  std::vector<std::string> keys;
  if (material.imported_material == nullptr ||
      material.imported_material->textures.empty()) {
    return keys;
  }

  keys.reserve(material.imported_material->textures.size());
  for (const renderer::ImportedMaterialTexture& imported :
       material.imported_material->textures) {
    if (imported.source_key.empty()) {
      continue;
    }
    const std::string source_hash = importedTextureSourceHash(imported);
    std::string texture_key;
    if (const auto existing = impl_->imported_texture_keys_by_source_hash.find(source_hash);
        existing != impl_->imported_texture_keys_by_source_hash.end()) {
      texture_key = existing->second;
    } else {
      std::optional<Rgba8Image> image = decodeImportedTexture(imported);
      if (!image.has_value() || !image->valid()) {
        continue;
      }

      const std::string label = sanitizeTextureKeySegment(
          imported.label.empty() ? imported.raw_name : imported.label);
      texture_key = "gltf/textures/" + source_hash + "/" + label;
      if (!AssetRegistry::isValidAssetKey(texture_key)) {
        texture_key = "gltf/textures/" + source_hash;
      }

      TextureAsset texture = makeTextureAssetFromImage(
          std::move(*image),
          imported.srgb,
          true,
          importedTextureSemantic(imported.semantic, imported.srgb),
          true);
      if (!registerTextureAsset(texture_key, std::move(texture))) {
        continue;
      }
      impl_->imported_texture_keys_by_source_hash[source_hash] = texture_key;
    }

    const std::string alias = importedTextureAlias(imported.semantic, imported.label);
    material.textures[alias] = texture_key;
    keys.push_back(texture_key);
  }
  return keys;
}

bool AssetRegistry::registerMaterialAsset(const std::string& key,
                                          renderer::MaterialAssetDesc material) {
  if (!isValidAssetKey(key)) {
    return false;
  }
  impl_->materials.registerMaterialAsset(key, std::move(material));
  bumpVersion();
  return true;
}

bool AssetRegistry::registerMaterialAsset(const std::string& key,
                                          renderer::MaterialDesc surface) {
  renderer::MaterialAssetDesc material{};
  material.surface = std::move(surface);
  return registerMaterialAsset(key, std::move(material));
}

bool AssetRegistry::registerMaterialVariant(const std::string& key,
                                            renderer::MaterialVariantDesc material) {
  if (!isValidAssetKey(key) || !isValidAssetKey(material.base_material_key)) {
    return false;
  }
  impl_->materials.registerMaterialVariant(key, std::move(material));
  bumpVersion();
  return true;
}

bool AssetRegistry::registerMaterialVariant(
    const std::string& key,
    const std::string& base_material_key,
    std::unordered_map<std::string, renderer::MaterialParameterValue> params,
    std::unordered_map<std::string, std::string> textures) {
  renderer::MaterialVariantDesc material{};
  material.base_material_key = base_material_key;
  material.params = std::move(params);
  material.textures = std::move(textures);
  return registerMaterialVariant(key, std::move(material));
}

bool AssetRegistry::unregisterMaterial(const std::string& key) {
  const bool removed = impl_->materials.unregisterMaterial(key);
  if (removed) {
    bumpVersion();
  }
  return removed;
}

const renderer::MaterialAssetDesc* AssetRegistry::findMaterialAsset(
    std::string_view key) const {
  return impl_->materials.findAsset(std::string(key));
}

const renderer::MaterialVariantDesc* AssetRegistry::findMaterialVariant(
    std::string_view key) const {
  return impl_->materials.findVariant(std::string(key));
}

std::optional<renderer::ResolvedMaterialDesc> AssetRegistry::resolveMaterial(
    std::string_view key) const {
  return impl_->materials.resolve(std::string(key));
}

bool AssetRegistry::registerPostProcessProfile(const std::string& key,
                                               renderer::PostProcessSettings profile) {
  if (!isValidAssetKey(key)) {
    return false;
  }
  impl_->post_process_profiles.registerProfile(key, profile);
  bumpVersion();
  return true;
}

bool AssetRegistry::unregisterPostProcessProfile(const std::string& key) {
  const bool removed = impl_->post_process_profiles.unregisterProfile(key);
  if (removed) {
    bumpVersion();
  }
  return removed;
}

const renderer::PostProcessSettings* AssetRegistry::findPostProcessProfile(
    std::string_view key) const {
  return impl_->post_process_profiles.find(key);
}

const renderer::PostProcessSettings& AssetRegistry::resolvePostProcessProfile(
    std::string_view key) const {
  return impl_->post_process_profiles.resolve(key);
}

bool AssetRegistry::registerParticleEffect(const std::string& key,
                                           particles::ParticleEffectAsset effect) {
  if (!isValidAssetKey(key)) {
    return false;
  }
  impl_->particle_effects[key] = std::move(effect);
  bumpVersion();
  return true;
}

bool AssetRegistry::unregisterParticleEffect(const std::string& key) {
  if (impl_->particle_effects.erase(key) == 0) {
    return false;
  }
  bumpVersion();
  return true;
}

const particles::ParticleEffectAsset* AssetRegistry::findParticleEffect(
    std::string_view key) const {
  return findInMap(impl_->particle_effects, key);
}

bool AssetRegistry::registerAudioClip(const std::string& key, AudioClipAsset clip) {
  if (!isValidAssetKey(key)) {
    return false;
  }
  impl_->audio_clips[key] = std::move(clip);
  bumpVersion();
  return true;
}

bool AssetRegistry::unregisterAudioClip(const std::string& key) {
  if (impl_->audio_clips.erase(key) == 0) {
    return false;
  }
  bumpVersion();
  return true;
}

const AudioClipAsset* AssetRegistry::findAudioClip(std::string_view key) const {
  return findInMap(impl_->audio_clips, key);
}

bool AssetRegistry::registerEnvironmentMap(const std::string& key,
                                           EnvironmentMapAsset environment) {
  if (!isValidAssetKey(key)) {
    return false;
  }
  impl_->environment_maps[key] = std::move(environment);
  bumpVersion();
  return true;
}

bool AssetRegistry::unregisterEnvironmentMap(const std::string& key) {
  if (impl_->environment_maps.erase(key) == 0) {
    return false;
  }
  bumpVersion();
  return true;
}

const EnvironmentMapAsset* AssetRegistry::findEnvironmentMap(std::string_view key) const {
  return findInMap(impl_->environment_maps, key);
}

bool AssetRegistry::registerAnimationClip(const std::string& key,
                                          animation::AnimationClip clip) {
  if (!isValidAssetKey(key)) {
    return false;
  }
  impl_->animation_clips[key] = std::move(clip);
  bumpVersion();
  return true;
}

bool AssetRegistry::unregisterAnimationClip(const std::string& key) {
  if (impl_->animation_clips.erase(key) == 0) {
    return false;
  }
  bumpVersion();
  return true;
}

const animation::AnimationClip* AssetRegistry::findAnimationClip(std::string_view key) const {
  return findInMap(impl_->animation_clips, key);
}

bool AssetRegistry::registerSkeleton(const std::string& key, animation::Skeleton skeleton) {
  if (!isValidAssetKey(key)) {
    return false;
  }
  impl_->skeletons[key] = std::move(skeleton);
  bumpVersion();
  return true;
}

bool AssetRegistry::unregisterSkeleton(const std::string& key) {
  if (impl_->skeletons.erase(key) == 0) {
    return false;
  }
  bumpVersion();
  return true;
}

const animation::Skeleton* AssetRegistry::findSkeleton(std::string_view key) const {
  return findInMap(impl_->skeletons, key);
}

bool AssetRegistry::registerSkin(const std::string& key, animation::Skin skin) {
  if (!isValidAssetKey(key)) {
    return false;
  }
  impl_->skins[key] = std::move(skin);
  bumpVersion();
  return true;
}

bool AssetRegistry::unregisterSkin(const std::string& key) {
  if (impl_->skins.erase(key) == 0) {
    return false;
  }
  bumpVersion();
  return true;
}

const animation::Skin* AssetRegistry::findSkin(std::string_view key) const {
  return findInMap(impl_->skins, key);
}

bool AssetRegistry::registerGltfSceneAsset(const std::string& key, GltfSceneAsset scene) {
  if (!isValidAssetKey(key)) {
    return false;
  }
  impl_->gltf_scenes[key] = std::move(scene);
  bumpVersion();
  return true;
}

bool AssetRegistry::unregisterGltfSceneAsset(const std::string& key) {
  if (impl_->gltf_scenes.erase(key) == 0) {
    return false;
  }
  bumpVersion();
  return true;
}

const GltfSceneAsset* AssetRegistry::findGltfSceneAsset(std::string_view key) const {
  return findInMap(impl_->gltf_scenes, key);
}

}  // namespace karma::content
