#include "asset_texture_internal.h"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#if defined(KARMA_ENABLE_KTX2)
#include <ktx.h>
#if __has_include(<vkformat_enum.h>)
#include <vkformat_enum.h>
#endif
#if !defined(KARMA_KTX_SOFTWARE_TAG)
#define KARMA_KTX_SOFTWARE_TAG "system"
#endif
#endif

#include "karma/assets.h"

#include <spdlog/spdlog.h>

namespace karma::assets::detail {

uint32_t fullMipLevelCount(int width, int height);
Rgba8Image downsampleAlphaWeighted(const Rgba8Image& source);

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
  return "texture-ktx2-uastc-v6";
#else
  return "texture-rgba8-v6";
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

uint32_t envUint(const char* value, uint32_t fallback) {
  if (value == nullptr || value[0] == '\0') {
    return fallback;
  }
  errno = 0;
  char* end = nullptr;
  const unsigned long parsed = std::strtoul(value, &end, 10);
  if (errno != 0 || end == value || *end != '\0' || parsed == 0ul) {
    return fallback;
  }
  return static_cast<uint32_t>(std::min<unsigned long>(parsed, 1024ul));
}

bool textureImportDiagnosticsEnabled() {
  static const bool enabled =
      envFlagOn(std::getenv("KARMA_TEXTURE_IMPORT_DIAG")) ||
      envFlagOn(std::getenv("KARMA_ENGINE_STARTUP_DIAG"));
  return enabled;
}

double elapsedMs(std::chrono::steady_clock::time_point start,
                 std::chrono::steady_clock::time_point end) {
  return std::chrono::duration<double, std::milli>(end - start).count();
}

uint32_t ktxEncodeThreadCount() {
  uint32_t fallback = std::thread::hardware_concurrency();
  if (fallback == 0u) {
    fallback = 16u;
  }
  fallback = std::clamp(fallback, 1u, 16u);
  return envUint(std::getenv("KARMA_TEXTURE_KTX2_THREADS"), fallback);
}

#if defined(KARMA_ENABLE_KTX2)
uint32_t ktxUastcLevel() {
  return std::min(envUint(std::getenv("KARMA_TEXTURE_KTX2_UASTC_LEVEL"),
                          static_cast<uint32_t>(KTX_PACK_UASTC_LEVEL_FASTEST)),
                  static_cast<uint32_t>(KTX_PACK_UASTC_MAX_LEVEL));
}
#endif

bool keepKtx2Fallback() {
  static const bool enabled = envFlagOn(std::getenv("KARMA_TEXTURE_KTX2_KEEP_FALLBACK"));
  return enabled;
}

bool runtimeBc7Enabled(const TextureAsset& texture) {
  const char* override_value = std::getenv("KARMA_TEXTURE_BC7");
  if (override_value != nullptr && override_value[0] != '\0') {
    return !envFlagOff(override_value);
  }
  return texture.fallback_rgba8.empty();
}

rendering::TextureFormat preparedUploadFormatForTexture(
    const TextureAsset& texture,
    TextureRuntimeCapabilities capabilities) {
  if (texture.payload_format == TextureAsset::PayloadFormat::PreparedUpload) {
    return texture.desc.format;
  }
  if (texture.payload_format == TextureAsset::PayloadFormat::KTX2_BASIS_UASTC) {
    const bool supports_bc7 = runtimeBc7Enabled(texture) &&
                              (texture.desc.srgb ? capabilities.bc7_srgb
                                                 : capabilities.bc7_unorm);
    if (supports_bc7) {
      return texture.desc.srgb ? rendering::TextureFormat::BC7_RGBA_UNORM_SRGB
                               : rendering::TextureFormat::BC7_RGBA_UNORM;
    }
  }
  return rendering::TextureFormat::RGBA8;
}

std::string textureProfileVersion() {
#if defined(KARMA_ENABLE_KTX2)
  return "ktx2_basis_uastc_explicit_mips:fallback=" +
         std::string(keepKtx2Fallback() ? "1" : "0") +
         ":uastc_level=" + std::to_string(ktxUastcLevel()) +
         ":normal_rgb=1";
#else
  return "rgba8";
#endif
}

struct Ktx2EncodeResult {
  std::vector<uint8_t> bytes;
  uint32_t mip_levels = 1u;
};

void logKtx2EncodeDiag(const char* stage,
                       const Rgba8Image& image,
                       bool srgb,
                       bool generate_mips,
                       bool normal_map,
                       uint32_t mip_levels,
                       uint32_t level,
                       uint32_t threads,
                       int result,
                       std::size_t bytes,
                       std::chrono::steady_clock::time_point start) {
  if (!textureImportDiagnosticsEnabled()) {
    return;
  }
  spdlog::info(
      "Texture import diag: area=ktx2_encode stage={} width={} height={} srgb={} "
      "generate_mips={} normal={} mip_levels={} level={} threads={} result={} bytes={} ms={:.2f}",
      stage ? stage : "unknown",
      image.width,
      image.height,
      srgb,
      generate_mips,
      normal_map,
      mip_levels,
      level,
      threads,
      result,
      bytes,
      elapsedMs(start, std::chrono::steady_clock::now()));
}

std::optional<Ktx2EncodeResult> encodeKtx2Uastc(const Rgba8Image& image,
                                                bool srgb,
                                                bool generate_mips,
                                                bool normal_map) {
#if defined(KARMA_ENABLE_KTX2)
  const auto encode_start = std::chrono::steady_clock::now();
  const uint32_t mip_levels =
      generate_mips ? fullMipLevelCount(image.width, image.height) : 1u;
  const uint32_t threads = ktxEncodeThreadCount();
  if (!image.valid()) {
    logKtx2EncodeDiag("invalid_image",
                      image,
                      srgb,
                      generate_mips,
                      normal_map,
                      mip_levels,
                      0u,
                      threads,
                      -1,
                      0u,
                      encode_start);
    return std::nullopt;
  }

  ktxTextureCreateInfo create_info{};
  create_info.glInternalformat = 0u;
  create_info.vkFormat = srgb ? kKtxVkFormatRgba8Srgb : kKtxVkFormatRgba8Unorm;
  create_info.baseWidth = static_cast<ktx_uint32_t>(image.width);
  create_info.baseHeight = static_cast<ktx_uint32_t>(image.height);
  create_info.baseDepth = 1u;
  create_info.numDimensions = 2u;
  create_info.numLevels = mip_levels;
  create_info.numLayers = 1u;
  create_info.numFaces = 1u;
  create_info.isArray = KTX_FALSE;
  create_info.generateMipmaps = KTX_FALSE;

  ktxTexture2* raw_texture = nullptr;
  const KTX_error_code create_result =
      ktxTexture2_Create(&create_info, KTX_TEXTURE_CREATE_ALLOC_STORAGE, &raw_texture);
  if (create_result != KTX_SUCCESS || raw_texture == nullptr) {
    logKtx2EncodeDiag("create_failed",
                      image,
                      srgb,
                      generate_mips,
                      normal_map,
                      mip_levels,
                      0u,
                      threads,
                      static_cast<int>(create_result),
                      0u,
                      encode_start);
    return std::nullopt;
  }
  std::unique_ptr<ktxTexture2, KtxTexture2Deleter> texture(raw_texture);

  auto set_image = [&](uint32_t level, const Rgba8Image& mip) {
    return ktxTexture_SetImageFromMemory(ktxTexture(texture.get()),
                                         level,
                                         0u,
                                         0u,
                                         mip.pixels.data(),
                                         mip.pixels.size());
  };

  KTX_error_code set_result = set_image(0u, image);
  if (set_result != KTX_SUCCESS) {
    logKtx2EncodeDiag("set_image_failed",
                      image,
                      srgb,
                      generate_mips,
                      normal_map,
                      mip_levels,
                      0u,
                      threads,
                      static_cast<int>(set_result),
                      0u,
                      encode_start);
    return std::nullopt;
  }

  if (mip_levels > 1u) {
    Rgba8Image current = image;
    for (uint32_t level = 1u; level < mip_levels; ++level) {
      current = downsampleAlphaWeighted(current);
      if (!current.valid()) {
        logKtx2EncodeDiag("mip_generation_failed",
                          image,
                          srgb,
                          generate_mips,
                          normal_map,
                          mip_levels,
                          level,
                          threads,
                          -1,
                          0u,
                          encode_start);
        return std::nullopt;
      }
      set_result = set_image(level, current);
      if (set_result != KTX_SUCCESS) {
        logKtx2EncodeDiag("set_image_failed",
                          image,
                          srgb,
                          generate_mips,
                          normal_map,
                          mip_levels,
                          level,
                          threads,
                          static_cast<int>(set_result),
                          0u,
                          encode_start);
        return std::nullopt;
      }
    }
  }

  ktxBasisParams params{};
  params.structSize = sizeof(params);
  params.uastc = KTX_TRUE;
  params.compressionLevel = KTX_ETC1S_DEFAULT_COMPRESSION_LEVEL;
  params.threadCount = threads;
  // libktx normal-map mode repacks XY into R/A on transcode. The renderer's
  // material shader currently samples normal maps as full RGB XYZ, so keep
  // normals in the ordinary RGBA path until the shader has an explicit RA
  // decode path.
  params.normalMap = KTX_FALSE;
  params.uastcFlags = static_cast<ktx_pack_uastc_flags>(ktxUastcLevel());
  const KTX_error_code compress_result = ktxTexture2_CompressBasisEx(texture.get(), &params);
  if (compress_result != KTX_SUCCESS) {
    logKtx2EncodeDiag("compress_failed",
                      image,
                      srgb,
                      generate_mips,
                      normal_map,
                      mip_levels,
                      0u,
                      threads,
                      static_cast<int>(compress_result),
                      0u,
                      encode_start);
    return std::nullopt;
  }

  const KTX_error_code deflate_result = ktxTexture2_DeflateZstd(texture.get(), 5u);
  if (deflate_result != KTX_SUCCESS && textureImportDiagnosticsEnabled()) {
    logKtx2EncodeDiag("deflate_failed",
                      image,
                      srgb,
                      generate_mips,
                      normal_map,
                      mip_levels,
                      0u,
                      threads,
                      static_cast<int>(deflate_result),
                      0u,
                      encode_start);
  }

  ktx_uint8_t* out_bytes = nullptr;
  ktx_size_t out_size = 0u;
  const KTX_error_code write_result =
      ktxTexture2_WriteToMemory(texture.get(), &out_bytes, &out_size);
  if (write_result != KTX_SUCCESS || out_bytes == nullptr || out_size == 0u) {
    if (out_bytes != nullptr) {
      std::free(out_bytes);
    }
    logKtx2EncodeDiag("write_failed",
                      image,
                      srgb,
                      generate_mips,
                      normal_map,
                      mip_levels,
                      0u,
                      threads,
                      static_cast<int>(write_result),
                      static_cast<std::size_t>(out_size),
                      encode_start);
    return std::nullopt;
  }

  Ktx2EncodeResult result{};
  result.bytes.assign(out_bytes, out_bytes + out_size);
  result.mip_levels = std::max(1u, static_cast<uint32_t>(texture->numLevels));
  std::free(out_bytes);
  logKtx2EncodeDiag("success",
                    image,
                    srgb,
                    generate_mips,
                    normal_map,
                    result.mip_levels,
                    0u,
                    threads,
                    static_cast<int>(KTX_SUCCESS),
                    result.bytes.size(),
                    encode_start);
  return result;
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
  prepared.desc.format = rendering::TextureFormat::RGBA8;
  prepared.desc.mip_levels = 1u;
  prepared.upload.format = rendering::TextureFormat::RGBA8;
  prepared.upload.bytes.assign(bytes->begin(),
                               bytes->begin() + static_cast<std::ptrdiff_t>(expected));
  prepared.upload.subresources.push_back(rendering::TextureUploadSubresource{
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

std::optional<PreparedTextureUpload> prepareCachedUpload(const TextureAsset& texture) {
  if (texture.payload_format != TextureAsset::PayloadFormat::PreparedUpload ||
      texture.desc.width <= 0 ||
      texture.desc.height <= 0 ||
      texture.bytes.empty() ||
      texture.subresources.empty()) {
    return std::nullopt;
  }

  for (const rendering::TextureUploadSubresource& subresource : texture.subresources) {
    if (subresource.width <= 0 ||
        subresource.height <= 0 ||
        subresource.size == 0u ||
        subresource.offset > texture.bytes.size() ||
        subresource.size > texture.bytes.size() - subresource.offset) {
      return std::nullopt;
    }
  }

  PreparedTextureUpload prepared{};
  prepared.desc = texture.desc;
  prepared.upload.format = texture.desc.format;
  prepared.upload.subresources = texture.subresources;
  prepared.upload.bytes = texture.bytes;
  return prepared.valid() ? std::optional<PreparedTextureUpload>{std::move(prepared)}
                          : std::nullopt;
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
  prepared.desc.format = bc7 ? (texture.desc.srgb ? rendering::TextureFormat::BC7_RGBA_UNORM_SRGB
                                                  : rendering::TextureFormat::BC7_RGBA_UNORM)
                             : rendering::TextureFormat::RGBA8;
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
    prepared.upload.subresources.push_back(rendering::TextureUploadSubresource{
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

void bleedTransparentRgb(Rgba8Image& image) {
  if (!image.valid()) {
    return;
  }

  constexpr uint8_t kVisibleAlphaThreshold = 8u;
  const int width = image.width;
  const int height = image.height;
  const size_t pixel_count = static_cast<size_t>(width) * static_cast<size_t>(height);

  std::vector<uint8_t> filled(pixel_count, 0u);
  std::vector<size_t> queue;
  queue.reserve(pixel_count);
  for (size_t index = 0; index < pixel_count; ++index) {
    if (image.pixels[index * 4u + 3u] <= kVisibleAlphaThreshold) {
      continue;
    }
    filled[index] = 1u;
    queue.push_back(index);
  }
  if (queue.empty()) {
    return;
  }

  for (size_t head = 0; head < queue.size(); ++head) {
    const size_t index = queue[head];
    const int x = static_cast<int>(index % static_cast<size_t>(width));
    const int y = static_cast<int>(index / static_cast<size_t>(width));
    const size_t source_offset = index * 4u;

    for (int oy = -1; oy <= 1; ++oy) {
      for (int ox = -1; ox <= 1; ++ox) {
        if (ox == 0 && oy == 0) {
          continue;
        }
        const int nx = x + ox;
        const int ny = y + oy;
        if (nx < 0 || ny < 0 || nx >= width || ny >= height) {
          continue;
        }
        const size_t neighbor = static_cast<size_t>(ny) * static_cast<size_t>(width) +
                                static_cast<size_t>(nx);
        if (filled[neighbor] != 0u) {
          continue;
        }

        const size_t neighbor_offset = neighbor * 4u;
        image.pixels[neighbor_offset + 0u] = image.pixels[source_offset + 0u];
        image.pixels[neighbor_offset + 1u] = image.pixels[source_offset + 1u];
        image.pixels[neighbor_offset + 2u] = image.pixels[source_offset + 2u];
        filled[neighbor] = 1u;
        queue.push_back(neighbor);
      }
    }
  }
}

uint32_t fullMipLevelCount(int width, int height) {
  uint32_t levels = 1u;
  int current_width = std::max(1, width);
  int current_height = std::max(1, height);
  while (current_width > 1 || current_height > 1) {
    current_width = std::max(1, current_width / 2);
    current_height = std::max(1, current_height / 2);
    ++levels;
  }
  return levels;
}

float alphaCoverage(const Rgba8Image& image, float cutoff) {
  if (!image.valid()) {
    return 0.0f;
  }
  const float threshold = std::clamp(cutoff, 0.0f, 1.0f) * 255.0f;
  const size_t pixel_count = static_cast<size_t>(image.width) * static_cast<size_t>(image.height);
  if (pixel_count == 0u) {
    return 0.0f;
  }
  size_t covered = 0u;
  for (size_t i = 0; i < pixel_count; ++i) {
    if (static_cast<float>(image.pixels[i * 4u + 3u]) >= threshold) {
      ++covered;
    }
  }
  return static_cast<float>(covered) / static_cast<float>(pixel_count);
}

float scaledAlphaCoverage(const Rgba8Image& image, float scale, float cutoff) {
  if (!image.valid()) {
    return 0.0f;
  }
  const float threshold = std::clamp(cutoff, 0.0f, 1.0f);
  const size_t pixel_count = static_cast<size_t>(image.width) * static_cast<size_t>(image.height);
  if (pixel_count == 0u) {
    return 0.0f;
  }
  size_t covered = 0u;
  for (size_t i = 0; i < pixel_count; ++i) {
    const float alpha =
        std::clamp((static_cast<float>(image.pixels[i * 4u + 3u]) / 255.0f) * scale,
                   0.0f,
                   1.0f);
    if (alpha >= threshold) {
      ++covered;
    }
  }
  return static_cast<float>(covered) / static_cast<float>(pixel_count);
}

void scaleAlphaToCoverage(Rgba8Image& image, float target_coverage, float cutoff) {
  if (!image.valid() || target_coverage <= 0.0f) {
    return;
  }

  float low = 0.0f;
  float high = 1.0f;
  for (int i = 0; i < 8 && scaledAlphaCoverage(image, high, cutoff) < target_coverage; ++i) {
    high *= 2.0f;
  }
  for (int i = 0; i < 16; ++i) {
    const float mid = (low + high) * 0.5f;
    if (scaledAlphaCoverage(image, mid, cutoff) < target_coverage) {
      low = mid;
    } else {
      high = mid;
    }
  }

  const size_t pixel_count = static_cast<size_t>(image.width) * static_cast<size_t>(image.height);
  for (size_t i = 0; i < pixel_count; ++i) {
    const size_t alpha_offset = i * 4u + 3u;
    const float scaled =
        std::clamp((static_cast<float>(image.pixels[alpha_offset]) / 255.0f) * high,
                   0.0f,
                   1.0f);
    image.pixels[alpha_offset] =
        static_cast<uint8_t>(std::clamp(std::round(scaled * 255.0f), 0.0f, 255.0f));
  }
}

Rgba8Image downsampleAlphaWeighted(const Rgba8Image& source) {
  Rgba8Image out{};
  if (!source.valid()) {
    return out;
  }

  out.width = std::max(1, source.width / 2);
  out.height = std::max(1, source.height / 2);
  out.pixels.resize(static_cast<size_t>(out.width) * static_cast<size_t>(out.height) * 4u);

  for (int y = 0; y < out.height; ++y) {
    for (int x = 0; x < out.width; ++x) {
      uint32_t alpha_sum = 0u;
      uint32_t count = 0u;
      float weighted_r = 0.0f;
      float weighted_g = 0.0f;
      float weighted_b = 0.0f;
      uint32_t fallback_r = 0u;
      uint32_t fallback_g = 0u;
      uint32_t fallback_b = 0u;

      for (int oy = 0; oy < 2; ++oy) {
        for (int ox = 0; ox < 2; ++ox) {
          const int sx = std::min(source.width - 1, x * 2 + ox);
          const int sy = std::min(source.height - 1, y * 2 + oy);
          const size_t source_offset =
              (static_cast<size_t>(sy) * static_cast<size_t>(source.width) +
               static_cast<size_t>(sx)) * 4u;
          const uint32_t alpha = source.pixels[source_offset + 3u];
          alpha_sum += alpha;
          weighted_r += static_cast<float>(source.pixels[source_offset + 0u]) *
                        static_cast<float>(alpha);
          weighted_g += static_cast<float>(source.pixels[source_offset + 1u]) *
                        static_cast<float>(alpha);
          weighted_b += static_cast<float>(source.pixels[source_offset + 2u]) *
                        static_cast<float>(alpha);
          fallback_r += source.pixels[source_offset + 0u];
          fallback_g += source.pixels[source_offset + 1u];
          fallback_b += source.pixels[source_offset + 2u];
          ++count;
        }
      }

      const size_t out_offset =
          (static_cast<size_t>(y) * static_cast<size_t>(out.width) +
           static_cast<size_t>(x)) * 4u;
      if (alpha_sum > 0u) {
        out.pixels[out_offset + 0u] =
            static_cast<uint8_t>(std::clamp(weighted_r / static_cast<float>(alpha_sum),
                                           0.0f,
                                           255.0f));
        out.pixels[out_offset + 1u] =
            static_cast<uint8_t>(std::clamp(weighted_g / static_cast<float>(alpha_sum),
                                           0.0f,
                                           255.0f));
        out.pixels[out_offset + 2u] =
            static_cast<uint8_t>(std::clamp(weighted_b / static_cast<float>(alpha_sum),
                                           0.0f,
                                           255.0f));
      } else {
        out.pixels[out_offset + 0u] = static_cast<uint8_t>(fallback_r / count);
        out.pixels[out_offset + 1u] = static_cast<uint8_t>(fallback_g / count);
        out.pixels[out_offset + 2u] = static_cast<uint8_t>(fallback_b / count);
      }
      out.pixels[out_offset + 3u] = static_cast<uint8_t>(alpha_sum / count);
    }
  }

  bleedTransparentRgb(out);
  return out;
}

void appendRgba8Mip(TextureAsset& texture, const Rgba8Image& mip, uint32_t level) {
  const size_t offset = texture.bytes.size();
  texture.bytes.insert(texture.bytes.end(), mip.pixels.begin(), mip.pixels.end());
  texture.subresources.push_back(rendering::TextureUploadSubresource{
      .mip_level = level,
      .array_layer = 0u,
      .width = mip.width,
      .height = mip.height,
      .offset = offset,
      .size = mip.pixels.size(),
      .row_stride = static_cast<std::size_t>(mip.width) * 4u,
  });
}

void buildAlphaAwareMipChain(TextureAsset& texture, Rgba8Image base_image, float alpha_coverage_cutoff) {
  texture.desc.generate_mips = false;
  texture.desc.mip_levels = fullMipLevelCount(base_image.width, base_image.height);
  texture.payload_format = TextureAsset::PayloadFormat::RGBA8;
  texture.bytes.clear();
  texture.subresources.clear();
  texture.subresources.reserve(texture.desc.mip_levels);

  bleedTransparentRgb(base_image);
  appendRgba8Mip(texture, base_image, 0u);
  Rgba8Image current = std::move(base_image);
  for (uint32_t level = 1u; level < texture.desc.mip_levels; ++level) {
    const float target_coverage = alphaCoverage(current, alpha_coverage_cutoff);
    current = downsampleAlphaWeighted(current);
    scaleAlphaToCoverage(current, target_coverage, alpha_coverage_cutoff);
    bleedTransparentRgb(current);
    appendRgba8Mip(texture, current, level);
  }
}

TextureAsset makeTextureAssetFromImage(Rgba8Image image,
                                       bool srgb,
                                       bool generate_mips,
                                       TextureAsset::Semantic semantic,
                                       bool prefer_compressed,
                                       bool alpha_aware_mips,
                                       float alpha_coverage_cutoff) {
  TextureAsset texture{};
  texture.desc.width = image.width;
  texture.desc.height = image.height;
  texture.desc.format = rendering::TextureFormat::RGBA8;
  texture.desc.srgb = srgb;
  texture.desc.generate_mips = generate_mips;
  texture.semantic = semantic;

  if (generate_mips && alpha_aware_mips) {
    buildAlphaAwareMipChain(texture, std::move(image), alpha_coverage_cutoff);
    texture.content_hash = textureContentHash(texture);
    return texture;
  }

  if (prefer_compressed) {
    auto ktx2 = encodeKtx2Uastc(image,
                                srgb,
                                generate_mips,
                                semantic == TextureAsset::Semantic::Normal);
    if (ktx2.has_value()) {
      texture.payload_format = TextureAsset::PayloadFormat::KTX2_BASIS_UASTC;
      texture.desc.mip_levels = std::max(1u, ktx2->mip_levels);
      texture.bytes = std::move(ktx2->bytes);
      if (keepKtx2Fallback()) {
        texture.fallback_rgba8 = std::move(image.pixels);
      }
      texture.content_hash = textureContentHash(texture);
      return texture;
    }
  }

  texture.payload_format = TextureAsset::PayloadFormat::RGBA8;
  texture.bytes = std::move(image.pixels);
  texture.subresources.push_back(rendering::TextureUploadSubresource{
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

std::string importedTextureAlias(rendering::ImportedMaterialTextureSemantic semantic,
                                 std::string_view fallback) {
  using Semantic = rendering::ImportedMaterialTextureSemantic;
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

TextureAsset::Semantic importedTextureSemantic(rendering::ImportedMaterialTextureSemantic semantic,
                                               bool srgb) {
  using Semantic = rendering::ImportedMaterialTextureSemantic;
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
    const rendering::ImportedMaterialTexture& texture) {
  if (texture.embedded) {
    if (texture.source_bytes.empty()) {
      return std::nullopt;
    }
    if (texture.compressed) {
      return loadRgba8ImageFromMemory(texture.source_bytes.data(),
                                      texture.source_bytes.size(),
                                      Rgba8ImageLoadOptions{.flip_y = true});
    }
    if (texture.width > static_cast<uint32_t>(std::numeric_limits<int>::max()) ||
        texture.height > static_cast<uint32_t>(std::numeric_limits<int>::max())) {
      return std::nullopt;
    }
    Rgba8Image image{};
    image.width = static_cast<int>(texture.width);
    image.height = static_cast<int>(texture.height);
    std::size_t expected = 0u;
    if (!rendering::tryTextureDataSize(image.width, image.height, 4u, expected) ||
        texture.source_bytes.size() < expected) {
      return std::nullopt;
    }
    // Uncompressed importer payloads are already canonical renderer-order RGBA8.
    image.pixels.assign(texture.source_bytes.begin(),
                        texture.source_bytes.begin() + static_cast<std::ptrdiff_t>(expected));
    return image.valid() ? std::optional<Rgba8Image>{std::move(image)} : std::nullopt;
  }
  return loadRgba8Image(texture.resolved_path, Rgba8ImageLoadOptions{.flip_y = true});
}

std::string importedTextureSourceHash(const rendering::ImportedMaterialTexture& texture) {
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

}  // namespace karma::assets::detail

namespace karma::assets {

std::optional<PreparedTextureUpload> prepareTextureUpload(
    const TextureAsset& texture,
    TextureRuntimeCapabilities capabilities) {
  if (texture.desc.width <= 0 || texture.desc.height <= 0) {
    return std::nullopt;
  }

  if (texture.payload_format == TextureAsset::PayloadFormat::PreparedUpload) {
    return detail::prepareCachedUpload(texture);
  }

  if (texture.payload_format == TextureAsset::PayloadFormat::KTX2_BASIS_UASTC) {
#if defined(KARMA_ENABLE_KTX2)
    const bool supports_bc7 = detail::runtimeBc7Enabled(texture) &&
                              (texture.desc.srgb ? capabilities.bc7_srgb
                                                 : capabilities.bc7_unorm);
    if (supports_bc7) {
      if (auto prepared = detail::transcodeKtx2Upload(texture, true)) {
        return prepared;
      }
    }
    if (auto prepared = detail::prepareRgba8Upload(texture)) {
      return prepared;
    }
    if (auto prepared = detail::transcodeKtx2Upload(texture, false)) {
      return prepared;
    }
#endif
    return detail::prepareRgba8Upload(texture);
  }

  if (texture.payload_format == TextureAsset::PayloadFormat::RGBA8) {
    if (!texture.subresources.empty() && !texture.bytes.empty()) {
      PreparedTextureUpload prepared{};
      prepared.desc = texture.desc;
      prepared.desc.format = rendering::TextureFormat::RGBA8;
      prepared.upload.format = rendering::TextureFormat::RGBA8;
      prepared.upload.subresources = texture.subresources;
      prepared.upload.bytes = texture.bytes;
      return prepared.valid() ? std::optional<PreparedTextureUpload>{std::move(prepared)}
                              : std::nullopt;
    }
    return detail::prepareRgba8Upload(texture);
  }

  return detail::prepareRgba8Upload(texture);
}

std::string preparedTextureUploadCacheKey(
    const TextureAsset& texture,
    TextureRuntimeCapabilities capabilities) {
  if (texture.desc.width <= 0 || texture.desc.height <= 0) {
    return {};
  }
  const rendering::TextureFormat target_format =
      detail::preparedUploadFormatForTexture(texture, capabilities);
  std::ostringstream key;
  key << "prepared-texture-upload-v2"
      << "|content=" << detail::textureContentHash(texture)
      << "|payload=" << static_cast<uint32_t>(texture.payload_format)
      << "|semantic=" << static_cast<uint32_t>(texture.semantic)
      << "|width=" << texture.desc.width
      << "|height=" << texture.desc.height
      << "|srgb=" << (texture.desc.srgb ? 1 : 0)
      << "|mips=" << texture.desc.mip_levels
      << "|generate_mips=" << (texture.desc.generate_mips ? 1 : 0)
      << "|fallback=" << (texture.fallback_rgba8.empty() ? 0 : 1)
      << "|target=" << static_cast<uint32_t>(target_format)
      << "|bc7_unorm=" << (capabilities.bc7_unorm ? 1 : 0)
      << "|bc7_srgb=" << (capabilities.bc7_srgb ? 1 : 0);
  return "prepared-texture-" + hashString(key.str());
}

}  // namespace karma::assets
