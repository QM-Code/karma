#include "karma/visual.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <initializer_list>
#include <limits>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include <glm/gtc/matrix_transform.hpp>

#include "karma/assets.h"
#include "karma/rendering.h"
#include "karma/components.h"
#include "karma/components.h"
#include "karma/components.h"
#include "karma/components.h"

namespace karma::visual::terrain {
namespace {

float clamp01(float value) {
  return std::clamp(value, 0.0f, 1.0f);
}

uint8_t toByte(float value) {
  return static_cast<uint8_t>(std::lround(clamp01(value) * 255.0f));
}

template <typename T>
void hashCombine(std::size_t& seed, const T& value) {
  seed ^= std::hash<T>{}(value) + 0x9e3779b97f4a7c15ull + (seed << 6u) + (seed >> 2u);
}

uint32_t hashGrid(int32_t x, int32_t z, uint32_t seed) {
  uint32_t h = seed ^ 0x9E3779B9u;
  h ^= static_cast<uint32_t>(x) + 0x85EBCA6Bu + (h << 6u) + (h >> 2u);
  h ^= static_cast<uint32_t>(z) + 0xC2B2AE35u + (h << 6u) + (h >> 2u);
  h ^= h >> 16u;
  h *= 0x7FEB352Du;
  h ^= h >> 15u;
  h *= 0x846CA68Bu;
  h ^= h >> 16u;
  return h;
}

float hash01(int32_t x, int32_t z, uint32_t seed) {
  return static_cast<float>(hashGrid(x, z, seed) & 0x00FFFFFFu) /
         static_cast<float>(0x01000000u);
}

float smooth(float t) {
  return t * t * (3.0f - 2.0f * t);
}

float valueNoise(float x, float z, float frequency, uint32_t seed) {
  const float sx = x * frequency;
  const float sz = z * frequency;
  const int32_t ix = static_cast<int32_t>(std::floor(sx));
  const int32_t iz = static_cast<int32_t>(std::floor(sz));
  const float fx = smooth(sx - static_cast<float>(ix));
  const float fz = smooth(sz - static_cast<float>(iz));
  const float a = hash01(ix, iz, seed);
  const float b = hash01(ix + 1, iz, seed);
  const float c = hash01(ix, iz + 1, seed);
  const float d = hash01(ix + 1, iz + 1, seed);
  const float x0 = a + (b - a) * fx;
  const float x1 = c + (d - c) * fx;
  return x0 + (x1 - x0) * fz;
}

float proceduralHeight(float world_x, float world_z, float tile_size) {
  const float scale = 1.0f / std::max(tile_size, 1.0f);
  float amplitude = 0.55f;
  float frequency = scale * 0.85f;
  float height = 0.0f;
  float amplitude_sum = 0.0f;
  for (uint32_t octave = 0; octave < 5u; ++octave) {
    height += (valueNoise(world_x, world_z, frequency, 1319u + octave * 977u) * 2.0f -
               1.0f) *
              amplitude;
    amplitude_sum += amplitude;
    amplitude *= 0.52f;
    frequency *= 2.05f;
  }
  const float ridge = std::sin(world_x * scale * 0.73f + world_z * scale * 0.21f) * 0.08f;
  return clamp01(0.5f + height / std::max(amplitude_sum * 2.0f, 0.001f) + ridge);
}

glm::vec3 toTerrainGlm(const math::Vec3& value) {
  return {value.x, value.y, value.z};
}

float authoredTileSize(const components::TerrainComponent& terrain) {
  return terrain.source == components::TerrainSourceType::SingleImage
             ? terrain.terrain_size
             : terrain.tile_size;
}

uint64_t terrainInstanceKey(uint64_t entity_key, TileCoord coord) {
  uint64_t h = entity_key ^ 0xCBF29CE484222325ull;
  h ^= static_cast<uint32_t>(coord.x);
  h *= 0x100000001B3ull;
  h ^= static_cast<uint32_t>(coord.z);
  h *= 0x100000001B3ull;
  return h == rendering::kInvalidInstance ? h - 1u : h;
}

bool terrainDescEquals(const rendering::TerrainDesc& a, const rendering::TerrainDesc& b) {
  return a.tile_size == b.tile_size &&
         a.tile_resolution == b.tile_resolution &&
         a.origin_tile_x == b.origin_tile_x &&
         a.origin_tile_z == b.origin_tile_z &&
         a.height_scale == b.height_scale &&
         a.height_offset == b.height_offset &&
         a.base_patch_size == b.base_patch_size &&
         a.max_tessellation_factor == b.max_tessellation_factor &&
         a.target_tessellated_edge_size == b.target_tessellated_edge_size &&
         a.cpu_fallback_enabled == b.cpu_fallback_enabled;
}

bool terrainColliderRequested(const world::World& world, world::Entity entity) {
  return world.has<components::ColliderComponent>(entity);
}

components::ColliderComponent terrainColliderMarker(const world::World& world,
                                                    world::Entity entity) {
  if (world.has<components::ColliderComponent>(entity)) {
    return world.get<components::ColliderComponent>(entity);
  }
  return {};
}

std::size_t terrainColliderSignature(const components::TerrainComponent& terrain,
                                     const components::ColliderComponent& marker) {
  std::size_t seed = 0u;
  hashCombine(seed, static_cast<uint8_t>(terrain.source));
  hashCombine(seed, terrain.tile_directory.string());
  hashCombine(seed, terrain.height_pattern);
  hashCombine(seed, terrain.color_pattern);
  hashCombine(seed, terrain.control_pattern);
  hashCombine(seed, terrain.height_image.string());
  hashCombine(seed, terrain.heatmap_image.string());
  hashCombine(seed, terrain.color_image.string());
  hashCombine(seed, terrain.control_image.string());
  hashCombine(seed, static_cast<uint8_t>(terrain.height_format));
  hashCombine(seed, terrain.raw_width);
  hashCombine(seed, terrain.raw_height);
  hashCombine(seed, terrain.raw_little_endian);
  hashCombine(seed, terrain.flip_y);
  hashCombine(seed, terrain.height_value_min);
  hashCombine(seed, terrain.height_value_max);
  hashCombine(seed, terrain.tile_index_base);
  hashCombine(seed, terrain.terrain_size);
  hashCombine(seed, terrain.tile_size);
  hashCombine(seed, terrain.tile_resolution);
  hashCombine(seed, terrain.origin_tile_x);
  hashCombine(seed, terrain.origin_tile_z);
  hashCombine(seed, terrain.height_scale);
  hashCombine(seed, terrain.height_offset);
  hashCombine(seed, marker.is_trigger);
  hashCombine(seed, marker.debug_draw);
  return seed;
}

std::optional<rendering::TerrainTileData> loadTerrainColliderTile(
    const components::TerrainComponent& terrain) {
  const TileCoord origin{.x = terrain.origin_tile_x, .z = terrain.origin_tile_z};
  switch (terrain.source) {
    case components::TerrainSourceType::Procedural:
      return generateProceduralTerrainTile(terrain, origin);
    case components::TerrainSourceType::ImageTileDirectory:
      return loadImageTerrainTile(terrain, origin);
    case components::TerrainSourceType::SingleImage:
      return loadSingleImageTerrainTile(terrain);
  }
  return std::nullopt;
}

components::ColliderComponent terrainTileToHeightFieldCollider(
    const components::TerrainComponent& terrain,
    const rendering::TerrainTileData& tile,
    const components::ColliderComponent& marker) {
  const rendering::TerrainDesc desc = terrainDescFromComponent(terrain);
  const float tile_size = std::max(desc.tile_size, 0.001f);
  const float sample_step =
      tile.resolution > 1u ? tile_size / static_cast<float>(tile.resolution - 1u) : tile_size;
  components::HeightFieldColliderShape shape{};
  shape.samples = tile.heights;
  shape.sample_count = tile.resolution;
  shape.offset = {
      static_cast<float>(tile.coord.x - terrain.origin_tile_x) * tile_size,
      desc.height_offset,
      static_cast<float>(tile.coord.z - terrain.origin_tile_z) * tile_size,
  };
  shape.scale = {sample_step, desc.height_scale, sample_step};
  shape.block_size = 2u;
  shape.bits_per_sample = 8u;
  return components::ColliderComponent::heightField(
      std::move(shape), marker.is_trigger, marker.debug_draw);
}

uint8_t sampleImageByteBilinear(const assets::Rgba8Image& image,
                                float u,
                                float v,
                                uint32_t channel) {
  if (!image.valid()) {
    return 0u;
  }
  const float x = clamp01(u) * static_cast<float>(image.width - 1);
  const float y = clamp01(v) * static_cast<float>(image.height - 1);
  const int x0 = static_cast<int>(std::floor(x));
  const int y0 = static_cast<int>(std::floor(y));
  const int x1 = std::min(x0 + 1, image.width - 1);
  const int y1 = std::min(y0 + 1, image.height - 1);
  const float fx = x - static_cast<float>(x0);
  const float fy = y - static_cast<float>(y0);
  auto at = [&](int px, int py) {
    const std::size_t index =
        (static_cast<std::size_t>(py) * static_cast<std::size_t>(image.width) +
         static_cast<std::size_t>(px)) *
            4u +
        channel;
    return static_cast<float>(image.pixels[index]);
  };
  const float a = at(x0, y0);
  const float b = at(x1, y0);
  const float c = at(x0, y1);
  const float d = at(x1, y1);
  const float x_top = a + (b - a) * fx;
  const float x_bottom = c + (d - c) * fx;
  return static_cast<uint8_t>(std::lround(x_top + (x_bottom - x_top) * fy));
}

bool isHeaderlessScalarTerrainPath(const std::filesystem::path& path) {
  std::string extension = path.extension().string();
  std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return extension == ".raw" || extension == ".r16" || extension == ".r16u" ||
         extension == ".r32";
}

std::string lowerCopy(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return value;
}

std::string lowerExtension(const std::filesystem::path& path) {
  return lowerCopy(path.extension().string());
}

bool containsAny(std::string_view value, std::initializer_list<std::string_view> needles) {
  for (std::string_view needle : needles) {
    if (!needle.empty() && value.find(needle) != std::string_view::npos) {
      return true;
    }
  }
  return false;
}

bool isUnsupportedGaeaBitmapExtension(std::string_view extension) {
  return extension == ".exr" || extension == ".tif" || extension == ".tiff";
}

bool isSupportedGaeaScalarExtension(std::string_view extension) {
  return extension == ".raw" || extension == ".r16" || extension == ".r16u" ||
         extension == ".r32" || extension == ".png" || extension == ".tga" ||
         extension == ".jpg" || extension == ".jpeg" || extension == ".bmp" ||
         extension == ".pgm" || extension == ".pnm" || extension == ".ppm" ||
         extension == ".psd" || extension == ".hdr";
}

bool isSupportedGaeaRgbaExtension(std::string_view extension) {
  return extension == ".png" || extension == ".tga" || extension == ".jpg" ||
         extension == ".jpeg" || extension == ".bmp" || extension == ".psd" ||
         extension == ".hdr";
}

int extensionHeightScore(std::string_view extension) {
  if (extension == ".r32") {
    return 40;
  }
  if (extension == ".raw" || extension == ".r16" || extension == ".r16u") {
    return 34;
  }
  if (extension == ".png") {
    return 28;
  }
  if (extension == ".tga") {
    return 18;
  }
  if (extension == ".pgm" || extension == ".pnm" || extension == ".ppm") {
    return 16;
  }
  if (extension == ".hdr") {
    return 12;
  }
  if (extension == ".bmp" || extension == ".jpg" || extension == ".jpeg" ||
      extension == ".psd") {
    return 8;
  }
  return 0;
}

components::TerrainHeightFormat terrainHeightFormatForPath(
    const std::filesystem::path& path) {
  const std::string extension = lowerExtension(path);
  if (extension == ".raw" || extension == ".r16" || extension == ".r16u") {
    return components::TerrainHeightFormat::Raw16Unsigned;
  }
  if (extension == ".r32") {
    return components::TerrainHeightFormat::R32Float;
  }
  return components::TerrainHeightFormat::ImageFile;
}

struct GaeaOutputFile {
  std::filesystem::path path;
  std::string stem_lower;
  std::string extension;
};

std::vector<GaeaOutputFile> listGaeaOutputFiles(const std::filesystem::path& directory) {
  std::vector<GaeaOutputFile> files;
  std::error_code ec;
  for (const auto& entry : std::filesystem::directory_iterator(directory, ec)) {
    if (ec) {
      break;
    }
    std::error_code status_ec;
    if (!entry.is_regular_file(status_ec) || status_ec) {
      continue;
    }
    GaeaOutputFile file{};
    file.path = entry.path().lexically_normal();
    file.stem_lower = lowerCopy(file.path.stem().string());
    file.extension = lowerExtension(file.path);
    files.push_back(std::move(file));
  }
  std::sort(files.begin(), files.end(), [](const GaeaOutputFile& a,
                                           const GaeaOutputFile& b) {
    return a.path.generic_string() < b.path.generic_string();
  });
  return files;
}

int scoreGaeaHeightFile(const GaeaOutputFile& file, bool allow_unsupported) {
  if (!isSupportedGaeaScalarExtension(file.extension) &&
      !(allow_unsupported && isUnsupportedGaeaBitmapExtension(file.extension))) {
    return 0;
  }
  if (containsAny(file.stem_lower,
                  {"color", "albedo", "diffuse", "normal", "rough", "splat",
                   "control", "weight", "mask", "flow", "wear", "deposit",
                   "slope", "curvature"})) {
    return 0;
  }

  int score = extensionHeightScore(file.extension);
  if (containsAny(file.stem_lower, {"heightmap", "height_map"})) {
    score += 90;
  }
  if (containsAny(file.stem_lower, {"height", "terrain", "shape", "displace"})) {
    score += 75;
  }
  if (containsAny(file.stem_lower, {"final", "output", "main"})) {
    score += 12;
  }
  return score;
}

int scoreGaeaColorFile(const GaeaOutputFile& file) {
  if (!isSupportedGaeaRgbaExtension(file.extension)) {
    return 0;
  }
  if (containsAny(file.stem_lower,
                  {"height", "normal", "rough", "splat", "control", "weight",
                   "flow", "wear", "deposit", "slope", "curvature"})) {
    return 0;
  }
  int score = 0;
  if (containsAny(file.stem_lower, {"color", "colour", "colormap", "satmap"})) {
    score += 80;
  }
  if (containsAny(file.stem_lower, {"albedo", "diffuse", "texture"})) {
    score += 60;
  }
  if (file.extension == ".png" || file.extension == ".tga") {
    score += 12;
  }
  return score;
}

int scoreGaeaControlFile(const GaeaOutputFile& file) {
  if (!isSupportedGaeaRgbaExtension(file.extension)) {
    return 0;
  }
  if (containsAny(file.stem_lower,
                  {"height", "color", "colour", "albedo", "diffuse", "normal",
                   "rough", "flow", "wear", "deposit", "slope", "curvature"})) {
    return 0;
  }
  int score = 0;
  if (containsAny(file.stem_lower, {"splat", "control", "weight"})) {
    score += 80;
  }
  if (containsAny(file.stem_lower, {"mask", "layer"})) {
    score += 35;
  }
  if (file.extension == ".png" || file.extension == ".tga") {
    score += 12;
  }
  return score;
}

std::optional<GaeaOutputFile> bestGaeaFile(
    const std::vector<GaeaOutputFile>& files,
    const std::function<int(const GaeaOutputFile&)>& score_file) {
  const GaeaOutputFile* best = nullptr;
  int best_score = 0;
  for (const GaeaOutputFile& file : files) {
    const int score = score_file(file);
    if (score > best_score) {
      best = &file;
      best_score = score;
    }
  }
  if (best == nullptr) {
    return std::nullopt;
  }
  return *best;
}

std::filesystem::path resolveGaeaPath(const std::filesystem::path& directory,
                                      const std::filesystem::path& path) {
  if (path.empty()) {
    return {};
  }
  return path.is_relative() ? (directory / path).lexically_normal() : path.lexically_normal();
}

bool sameNormalizedPath(const std::filesystem::path& a, const std::filesystem::path& b) {
  return a.lexically_normal().generic_string() == b.lexically_normal().generic_string();
}

bool pathInUse(const std::filesystem::path& path,
               std::initializer_list<std::filesystem::path> used_paths) {
  for (const auto& used : used_paths) {
    if (!used.empty() && sameNormalizedPath(path, used)) {
      return true;
    }
  }
  return false;
}

bool gaeaFail(std::string* diagnostic, std::string message) {
  if (diagnostic != nullptr) {
    *diagnostic = std::move(message);
  }
  return false;
}

bool validateGaeaHeightPath(const std::filesystem::path& path,
                            const components::TerrainComponent& terrain,
                            std::string* diagnostic) {
  const std::string extension = lowerExtension(path);
  if (isUnsupportedGaeaBitmapExtension(extension)) {
    return gaeaFail(diagnostic,
                    "Gaea terrain height '" + path.string() +
                        "' uses EXR/TIFF, which the built-in terrain loader does not decode");
  }
  if (!isSupportedGaeaScalarExtension(extension)) {
    return gaeaFail(diagnostic,
                    "Gaea terrain height '" + path.string() +
                        "' has an unsupported extension");
  }
  if ((terrain.height_format == components::TerrainHeightFormat::Raw16Unsigned ||
       terrain.height_format == components::TerrainHeightFormat::R32Float) &&
      (terrain.raw_width == 0u || terrain.raw_height == 0u)) {
    return gaeaFail(diagnostic,
                    "Gaea RAW/R32 terrain heights require raw_width and raw_height");
  }
  return true;
}

bool validateGaeaRgbaPath(const std::filesystem::path& path,
                          std::string_view role,
                          std::string* diagnostic) {
  const std::string extension = lowerExtension(path);
  if (isUnsupportedGaeaBitmapExtension(extension)) {
    return gaeaFail(diagnostic,
                    "Gaea terrain " + std::string(role) + " '" + path.string() +
                        "' uses EXR/TIFF, which the built-in RGBA loader does not decode");
  }
  if (!isSupportedGaeaRgbaExtension(extension)) {
    return gaeaFail(diagnostic,
                    "Gaea terrain " + std::string(role) + " '" + path.string() +
                        "' has an unsupported extension");
  }
  return true;
}

std::optional<std::string> inferGaeaTilePattern(const std::filesystem::path& path) {
  std::string stem = path.stem().string();
  const std::string extension = path.extension().string();
  struct Run {
    std::size_t begin = 0u;
    std::size_t end = 0u;
  };
  std::vector<Run> runs;
  for (std::size_t i = 0u; i < stem.size();) {
    if (!std::isdigit(static_cast<unsigned char>(stem[i]))) {
      ++i;
      continue;
    }
    std::size_t begin = i;
    if (begin > 0u && stem[begin - 1u] == '-') {
      --begin;
    }
    while (i < stem.size() && std::isdigit(static_cast<unsigned char>(stem[i]))) {
      ++i;
    }
    runs.push_back(Run{begin, i});
  }
  if (runs.size() < 2u) {
    return std::nullopt;
  }
  const Run z = runs[runs.size() - 1u];
  const Run x = runs[runs.size() - 2u];
  stem.replace(z.begin, z.end - z.begin, "{z}");
  stem.replace(x.begin, x.end - x.begin, "{x}");
  return stem + extension;
}

std::optional<components::TerrainDataMapKind> detectGaeaDataMapKind(
    const GaeaOutputFile& file,
    std::string& out_name) {
  if (containsAny(file.stem_lower, {"flow"})) {
    out_name = "flow";
    return components::TerrainDataMapKind::Flow;
  }
  if (containsAny(file.stem_lower, {"wear"})) {
    out_name = "wear";
    return components::TerrainDataMapKind::Wear;
  }
  if (containsAny(file.stem_lower, {"deposit", "deposition"})) {
    out_name = "deposit";
    return components::TerrainDataMapKind::Deposit;
  }
  if (containsAny(file.stem_lower, {"slope"})) {
    out_name = "slope";
    return components::TerrainDataMapKind::Slope;
  }
  if (containsAny(file.stem_lower, {"curvature", "curve"})) {
    out_name = "curvature";
    return components::TerrainDataMapKind::Curvature;
  }
  return std::nullopt;
}

float sampleScalarImageBilinear(const assets::ScalarImage& image,
                                float u,
                                float v) {
  if (!image.valid()) {
    return 0.0f;
  }
  const float x = clamp01(u) * static_cast<float>(image.width - 1);
  const float y = clamp01(v) * static_cast<float>(image.height - 1);
  const int x0 = static_cast<int>(std::floor(x));
  const int y0 = static_cast<int>(std::floor(y));
  const int x1 = std::min(x0 + 1, image.width - 1);
  const int y1 = std::min(y0 + 1, image.height - 1);
  const float fx = x - static_cast<float>(x0);
  const float fy = y - static_cast<float>(y0);
  auto at = [&](int px, int py) {
    const std::size_t index =
        static_cast<std::size_t>(py) * static_cast<std::size_t>(image.width) +
        static_cast<std::size_t>(px);
    return image.values[index];
  };
  const float a = at(x0, y0);
  const float b = at(x1, y0);
  const float c = at(x0, y1);
  const float d = at(x1, y1);
  const float x_top = a + (b - a) * fx;
  const float x_bottom = c + (d - c) * fx;
  return clamp01(x_top + (x_bottom - x_top) * fy);
}

assets::ScalarImageFormat scalarFormatFromTerrain(
    components::TerrainHeightFormat format) {
  switch (format) {
    case components::TerrainHeightFormat::Auto:
      return assets::ScalarImageFormat::Auto;
    case components::TerrainHeightFormat::ImageFile:
      return assets::ScalarImageFormat::ImageFile;
    case components::TerrainHeightFormat::Raw16Unsigned:
      return assets::ScalarImageFormat::Raw16Unsigned;
    case components::TerrainHeightFormat::R32Float:
      return assets::ScalarImageFormat::R32Float;
  }
  return assets::ScalarImageFormat::Auto;
}

assets::ScalarImageLoadOptions scalarLoadOptions(
    const components::TerrainComponent& terrain,
    components::TerrainHeightFormat format,
    uint32_t raw_width,
    uint32_t raw_height) {
  return assets::ScalarImageLoadOptions{
      .format = scalarFormatFromTerrain(format),
      .raw_width = raw_width,
      .raw_height = raw_height,
      .little_endian = terrain.raw_little_endian,
      .flip_y = terrain.flip_y,
      .value_min = terrain.height_value_min,
      .value_max = terrain.height_value_max,
  };
}

std::optional<assets::ScalarImage> loadTerrainScalarImage(
    const std::filesystem::path& path,
    const components::TerrainComponent& terrain) {
  return assets::loadScalarImage(path,
                                  scalarLoadOptions(terrain,
                                                    terrain.height_format,
                                                    terrain.raw_width,
                                                    terrain.raw_height));
}

std::optional<assets::ScalarImage> loadTerrainDataMapImage(
    const std::filesystem::path& path,
    const components::TerrainComponent& terrain,
    const components::TerrainDataMapBinding& binding) {
  if ((binding.format == components::TerrainHeightFormat::Auto ||
       binding.format == components::TerrainHeightFormat::ImageFile) &&
      binding.channel > 0u &&
      binding.channel < 4u &&
      !isHeaderlessScalarTerrainPath(path)) {
    std::optional<assets::Rgba8Image> rgba = assets::loadRgba8Image(path);
    if (rgba && rgba->valid()) {
      assets::ScalarImage image{};
      image.width = rgba->width;
      image.height = rgba->height;
      image.values.resize(static_cast<std::size_t>(image.width) *
                          static_cast<std::size_t>(image.height));
      for (int y = 0; y < image.height; ++y) {
        for (int x = 0; x < image.width; ++x) {
          const std::size_t sample =
              static_cast<std::size_t>(y) * static_cast<std::size_t>(image.width) +
              static_cast<std::size_t>(x);
          image.values[sample] =
              static_cast<float>(
                  rgba->pixels[sample * 4u + static_cast<std::size_t>(binding.channel)]) /
              255.0f;
        }
      }
      return image.valid() ? std::optional<assets::ScalarImage>{std::move(image)}
                           : std::nullopt;
    }
  }
  const uint32_t raw_width = binding.raw_width != 0u ? binding.raw_width : terrain.raw_width;
  const uint32_t raw_height = binding.raw_height != 0u ? binding.raw_height : terrain.raw_height;
  return assets::loadScalarImage(path,
                                  scalarLoadOptions(terrain,
                                                    binding.format,
                                                    raw_width,
                                                    raw_height));
}

rendering::TerrainTextureData textureDataFromImage(assets::Rgba8Image image) {
  rendering::TerrainTextureData texture{};
  if (!image.valid()) {
    return texture;
  }
  texture.width = static_cast<uint32_t>(image.width);
  texture.height = static_cast<uint32_t>(image.height);
  texture.rgba8 = std::move(image.pixels);
  return texture;
}

rendering::TerrainTextureData solidTextureData(uint8_t r,
                                              uint8_t g,
                                              uint8_t b,
                                              uint8_t a = 255u) {
  rendering::TerrainTextureData texture{};
  texture.width = 1u;
  texture.height = 1u;
  texture.rgba8 = {r, g, b, a};
  return texture;
}

std::optional<rendering::TerrainTextureData> loadTerrainTexture(
    const std::filesystem::path& path) {
  if (path.empty()) {
    return std::nullopt;
  }
  if (auto image = assets::loadRgba8Image(path); image && image->valid()) {
    return textureDataFromImage(std::move(*image));
  }
  return std::nullopt;
}

std::optional<std::filesystem::path> findResolvedTexture(
    const rendering::ResolvedMaterialDesc& material,
    std::initializer_list<std::string_view> aliases) {
  for (std::string_view alias : aliases) {
    const auto it = material.textures.find(std::string(alias));
    if (it != material.textures.end() && !it->second.empty()) {
      return it->second;
    }
  }
  return std::nullopt;
}

std::vector<uint8_t> scalarImageToGrayscaleRgba8(const assets::ScalarImage& image) {
  if (!image.valid()) {
    return {255u, 255u, 255u, 255u};
  }
  std::vector<uint8_t> rgba(static_cast<std::size_t>(image.width) *
                                static_cast<std::size_t>(image.height) * 4u,
                            255u);
  for (int y = 0; y < image.height; ++y) {
    for (int x = 0; x < image.width; ++x) {
      const std::size_t sample =
          static_cast<std::size_t>(y) * static_cast<std::size_t>(image.width) +
          static_cast<std::size_t>(x);
      const uint8_t value = toByte(image.values[sample]);
      const std::size_t pixel = sample * 4u;
      rgba[pixel + 0u] = value;
      rgba[pixel + 1u] = value;
      rgba[pixel + 2u] = value;
      rgba[pixel + 3u] = 255u;
    }
  }
  return rgba;
}

std::vector<uint8_t> solidWhiteRgba8() {
  return {255u, 255u, 255u, 255u};
}

bool hasMaterialLayers(const components::TerrainComponent& terrain) {
  return std::any_of(terrain.material_layers.begin(),
                     terrain.material_layers.end(),
                     [](const components::TerrainMaterialLayer& layer) {
                       return layer.enabled &&
                              (!layer.material_key.empty() ||
                               !layer.albedo_image.empty());
                     });
}

void loadTerrainControlMap(const std::filesystem::path& path,
                           rendering::TerrainTileData& tile) {
  if (path.empty() || !std::filesystem::exists(path)) {
    return;
  }
  std::optional<assets::Rgba8Image> control = assets::loadRgba8Image(path);
  if (!control || !control->valid()) {
    return;
  }
  tile.control_width = static_cast<uint32_t>(control->width);
  tile.control_height = static_cast<uint32_t>(control->height);
  tile.control_rgba8 = std::move(control->pixels);
}

void loadTerrainDataMaps(const components::TerrainComponent& terrain,
                         TileCoord coord,
                         rendering::TerrainTileData& tile) {
  for (const auto& binding : terrain.data_maps) {
    if (!binding.enabled) {
      continue;
    }
    std::filesystem::path path;
    if (!binding.pattern.empty() && !terrain.tile_directory.empty()) {
      path = terrain.tile_directory /
             formatTerrainTilePattern(binding.pattern, coord, terrain.tile_index_base);
    } else {
      path = binding.image;
    }
    if (path.empty() || !std::filesystem::exists(path)) {
      continue;
    }
    std::optional<assets::ScalarImage> image =
        loadTerrainDataMapImage(path, terrain, binding);
    if (!image || !image->valid()) {
      continue;
    }
    rendering::TerrainDataMapTileData data{};
    data.name = !binding.name.empty() ? binding.name : path.stem().string();
    data.width = static_cast<uint32_t>(image->width);
    data.height = static_cast<uint32_t>(image->height);
    data.values = std::move(image->values);
    if (data.valid()) {
      tile.data_maps.push_back(std::move(data));
    }
  }
}

}  // namespace

std::size_t TileCoordHash::operator()(const TileCoord& coord) const noexcept {
  uint64_t h = 0x9E3779B97F4A7C15ull;
  h ^= static_cast<uint32_t>(coord.x) + 0x9E3779B97F4A7C15ull + (h << 6u) + (h >> 2u);
  h ^= static_cast<uint32_t>(coord.z) + 0x9E3779B97F4A7C15ull + (h << 6u) + (h >> 2u);
  return static_cast<std::size_t>(h);
}

int terrainTileRadius(float view_distance, float tile_size) {
  if (tile_size <= 0.0f || view_distance <= 0.0f) {
    return 0;
  }
  return static_cast<int>(std::ceil(view_distance / tile_size));
}

std::vector<TileCoord> terrainChunkCoordsAround(TileCoord center,
                                                float view_distance,
                                                float tile_size) {
  const int radius = terrainTileRadius(view_distance, tile_size);
  std::vector<TileCoord> coords;
  const int diameter = radius * 2 + 1;
  coords.reserve(static_cast<std::size_t>(diameter) * static_cast<std::size_t>(diameter));
  for (int z = center.z - radius; z <= center.z + radius; ++z) {
    for (int x = center.x - radius; x <= center.x + radius; ++x) {
      coords.push_back(TileCoord{.x = x, .z = z});
    }
  }
  return coords;
}

TileCoord terrainTileCoordForWorldPosition(
    const glm::vec3& world_position,
    const glm::vec3& terrain_origin,
    const components::TerrainComponent& terrain) {
  const float tile_size = std::max(terrain.tile_size, 0.001f);
  return TileCoord{
      .x = terrain.origin_tile_x +
           static_cast<int32_t>(std::floor((world_position.x - terrain_origin.x) / tile_size)),
      .z = terrain.origin_tile_z +
           static_cast<int32_t>(std::floor((world_position.z - terrain_origin.z) / tile_size)),
  };
}

rendering::TerrainDesc terrainDescFromComponent(const components::TerrainComponent& terrain) {
  rendering::TerrainDesc desc{};
  desc.tile_size = std::max(authoredTileSize(terrain), 0.001f);
  desc.tile_resolution = std::max(terrain.tile_resolution, 2u);
  desc.origin_tile_x = terrain.origin_tile_x;
  desc.origin_tile_z = terrain.origin_tile_z;
  desc.height_scale = terrain.height_scale;
  desc.height_offset = terrain.height_offset;
  desc.base_patch_size = std::max(terrain.base_patch_size, 1u);
  desc.max_tessellation_factor = std::clamp(terrain.tessellation_factor, 1.0f, 64.0f);
  desc.target_tessellated_edge_size =
      std::max(terrain.target_tessellated_edge_size, 1.0f);
  desc.cpu_fallback_enabled = terrain.cpu_fallback_enabled;
  return desc;
}

std::string formatTerrainTilePattern(std::string pattern,
                                     TileCoord coord,
                                     int32_t index_base) {
  const int32_t x = coord.x + index_base;
  const int32_t z = coord.z + index_base;
  auto replace_all = [&](std::string_view key, const std::string& value) {
    std::size_t pos = 0u;
    while ((pos = pattern.find(key, pos)) != std::string::npos) {
      pattern.replace(pos, key.size(), value);
      pos += value.size();
    }
  };
  replace_all("{x}", std::to_string(x));
  replace_all("{z}", std::to_string(z));
  replace_all("{y}", std::to_string(z));
  replace_all("{tile_x}", std::to_string(x));
  replace_all("{tile_z}", std::to_string(z));
  replace_all("{tile_y}", std::to_string(z));
  replace_all("{X}", std::to_string(x));
  replace_all("{Z}", std::to_string(z));
  replace_all("{Y}", std::to_string(z));
  return pattern;
}

std::vector<float> convertHeightImageToNormalizedHeights(
    const assets::Rgba8Image& image,
    uint32_t output_resolution) {
  output_resolution = std::max(output_resolution, 2u);
  std::vector<float> heights(static_cast<std::size_t>(output_resolution) *
                                 static_cast<std::size_t>(output_resolution),
                             0.0f);
  if (!image.valid()) {
    return heights;
  }
  const float inv = 1.0f / static_cast<float>(output_resolution - 1u);
  for (uint32_t z = 0u; z < output_resolution; ++z) {
    for (uint32_t x = 0u; x < output_resolution; ++x) {
      const float u = static_cast<float>(x) * inv;
      const float v = static_cast<float>(z) * inv;
      heights[static_cast<std::size_t>(z) * output_resolution + x] =
          static_cast<float>(sampleImageByteBilinear(image, u, v, 0u)) / 255.0f;
    }
  }
  return heights;
}

std::vector<float> convertScalarImageToNormalizedHeights(
    const assets::ScalarImage& image,
    uint32_t output_resolution) {
  output_resolution = std::max(output_resolution, 2u);
  std::vector<float> heights(static_cast<std::size_t>(output_resolution) *
                                 static_cast<std::size_t>(output_resolution),
                             0.0f);
  if (!image.valid()) {
    return heights;
  }
  const float inv = 1.0f / static_cast<float>(output_resolution - 1u);
  for (uint32_t z = 0u; z < output_resolution; ++z) {
    for (uint32_t x = 0u; x < output_resolution; ++x) {
      const float u = static_cast<float>(x) * inv;
      const float v = static_cast<float>(z) * inv;
      heights[static_cast<std::size_t>(z) * output_resolution + x] =
          sampleScalarImageBilinear(image, u, v);
    }
  }
  return heights;
}

rendering::TerrainTileData generateProceduralTerrainTile(
    const components::TerrainComponent& terrain,
    TileCoord coord) {
  const uint32_t resolution = std::max(terrain.tile_resolution, 2u);
  const float tile_size = std::max(terrain.tile_size, 0.001f);
  rendering::TerrainTileData tile{};
  tile.coord = coord;
  tile.resolution = resolution;
  tile.color_width = resolution;
  tile.color_height = resolution;
  tile.heights.resize(static_cast<std::size_t>(resolution) *
                      static_cast<std::size_t>(resolution));
  tile.color_rgba8.resize(tile.heights.size() * 4u);

  const float inv = 1.0f / static_cast<float>(resolution - 1u);
  const float origin_x =
      static_cast<float>(coord.x - terrain.origin_tile_x) * tile_size;
  const float origin_z =
      static_cast<float>(coord.z - terrain.origin_tile_z) * tile_size;
  for (uint32_t z = 0u; z < resolution; ++z) {
    for (uint32_t x = 0u; x < resolution; ++x) {
      const float u = static_cast<float>(x) * inv;
      const float v = static_cast<float>(z) * inv;
      const float world_x = origin_x + u * tile_size;
      const float world_z = origin_z + v * tile_size;
      const float height = proceduralHeight(world_x, world_z, tile_size);
      const std::size_t sample = static_cast<std::size_t>(z) * resolution + x;
      tile.heights[sample] = height;

      const float cool = clamp01((height - 0.25f) * 1.9f);
      const float rock = clamp01((height - 0.58f) * 2.3f);
      const float snow = clamp01((height - 0.78f) * 4.0f);
      const float r = (0.16f + cool * 0.22f + rock * 0.23f) * (1.0f - snow) +
                      snow * 0.86f;
      const float g = (0.29f + cool * 0.34f + rock * 0.10f) * (1.0f - snow) +
                      snow * 0.88f;
      const float b = (0.18f + cool * 0.12f + rock * 0.09f) * (1.0f - snow) +
                      snow * 0.90f;
      const std::size_t pixel = sample * 4u;
      tile.color_rgba8[pixel + 0u] = toByte(r);
      tile.color_rgba8[pixel + 1u] = toByte(g);
      tile.color_rgba8[pixel + 2u] = toByte(b);
      tile.color_rgba8[pixel + 3u] = 255u;
    }
  }
  return tile;
}

std::optional<rendering::TerrainMaterialLayerData> loadTerrainMaterialLayer(
    const components::TerrainMaterialLayer& layer,
    uint32_t layer_index) {
  if (!layer.enabled || layer_index >= 4u || layer.albedo_image.empty()) {
    return std::nullopt;
  }
  auto albedo = loadTerrainTexture(layer.albedo_image);
  if (!albedo) {
    return std::nullopt;
  }

  rendering::TerrainMaterialLayerData data{};
  data.layer = layer_index;
  data.name = layer.name;
  data.uv_scale = std::max(layer.uv_scale, 0.001f);
  data.enabled = layer.enabled;
  data.albedo = std::move(*albedo);

  if (auto normal = loadTerrainTexture(layer.normal_image)) {
    data.normal = std::move(*normal);
  }
  if (auto roughness = loadTerrainTexture(layer.roughness_image)) {
    data.roughness = std::move(*roughness);
  }

  return data.valid() ? std::optional<rendering::TerrainMaterialLayerData>{std::move(data)}
                      : std::nullopt;
}

std::optional<rendering::TerrainMaterialLayerData> loadTerrainMaterialLayer(
    const components::TerrainMaterialLayer& layer,
    uint32_t layer_index,
    const assets::AssetRegistry* assets) {
  if (!layer.enabled || layer_index >= 4u) {
    return std::nullopt;
  }

  if (!layer.material_key.empty()) {
    std::optional<rendering::ResolvedMaterialDesc> material;
    if (assets != nullptr) {
      material = assets->resolveMaterial(layer.material_key);
    }
    if (material.has_value()) {
      rendering::TerrainMaterialLayerData data{};
      data.layer = layer_index;
      data.name = !layer.name.empty() ? layer.name : layer.material_key;
      data.uv_scale = std::max(layer.uv_scale, 0.001f);
      data.enabled = layer.enabled;

      if (auto path = findResolvedTexture(
              *material, {"base_color", "baseColor", "albedo", "diffuse"})) {
        if (auto texture = loadTerrainTexture(*path)) {
          data.albedo = std::move(*texture);
        }
      }
      if (!data.albedo.valid()) {
        const rendering::Color color = material->surface.base_color;
        data.albedo = solidTextureData(
            toByte(color.r), toByte(color.g), toByte(color.b), toByte(color.a));
      }

      if (auto path = findResolvedTexture(
              *material, {"normal", "normal_map", "normalMap"})) {
        if (auto texture = loadTerrainTexture(*path)) {
          data.normal = std::move(*texture);
        }
      }
      if (auto path = findResolvedTexture(*material, {"roughness", "roughness_map"})) {
        if (auto texture = loadTerrainTexture(*path)) {
          data.roughness = std::move(*texture);
        }
      }
      if (!data.roughness.valid()) {
        const uint8_t roughness = toByte(material->surface.roughness);
        data.roughness = solidTextureData(roughness, roughness, roughness, 255u);
      }

      if (data.valid()) {
        return std::optional<rendering::TerrainMaterialLayerData>{std::move(data)};
      }
    }
  }

  return loadTerrainMaterialLayer(layer, layer_index);
}

std::optional<components::TerrainComponent> importGaeaTerrainDirectory(
    const GaeaTerrainImportDesc& desc,
    std::string* diagnostic) {
  if (diagnostic != nullptr) {
    diagnostic->clear();
  }
  if (desc.directory.empty()) {
    gaeaFail(diagnostic, "Gaea terrain import requires a directory");
    return std::nullopt;
  }

  const std::filesystem::path directory = desc.directory.lexically_normal();
  std::error_code ec;
  if (!std::filesystem::exists(directory, ec) || ec ||
      !std::filesystem::is_directory(directory, ec) || ec) {
    gaeaFail(diagnostic, "Gaea terrain import directory does not exist: " +
                             directory.string());
    return std::nullopt;
  }
  if (desc.height_value_max <= desc.height_value_min) {
    gaeaFail(diagnostic, "Gaea terrain height_value_max must exceed height_value_min");
    return std::nullopt;
  }
  if (desc.tile_resolution < 2u ||
      desc.base_patch_size == 0u ||
      desc.tessellation_factor < 1.0f ||
      desc.target_tessellated_edge_size <= 0.0f ||
      desc.view_distance < 0.0f) {
    gaeaFail(diagnostic, "Gaea terrain import descriptor has invalid terrain settings");
    return std::nullopt;
  }
  if (desc.material_layers.size() > 4u) {
    gaeaFail(diagnostic, "Gaea terrain supports at most four material layers");
    return std::nullopt;
  }

  const std::vector<GaeaOutputFile> files = listGaeaOutputFiles(directory);
  if (files.empty()) {
    gaeaFail(diagnostic, "Gaea terrain import directory contains no files: " +
                             directory.string());
    return std::nullopt;
  }

  components::TerrainComponent terrain{};
  terrain.source = desc.tiled ? components::TerrainSourceType::ImageTileDirectory
                              : components::TerrainSourceType::SingleImage;
  terrain.tile_directory = desc.tiled ? directory : std::filesystem::path{};
  terrain.raw_width = desc.raw_width;
  terrain.raw_height = desc.raw_height;
  terrain.raw_little_endian = desc.raw_little_endian;
  terrain.flip_y = desc.flip_y;
  terrain.height_value_min = desc.height_value_min;
  terrain.height_value_max = desc.height_value_max;
  terrain.tile_index_base = desc.tile_index_base;
  terrain.material_layers = desc.material_layers;
  terrain.terrain_size = desc.terrain_size;
  terrain.tile_size = desc.tile_size;
  terrain.tile_resolution = desc.tile_resolution;
  terrain.origin_tile_x = desc.origin_tile_x;
  terrain.origin_tile_z = desc.origin_tile_z;
  terrain.height_scale = desc.height_scale;
  terrain.height_offset = desc.height_offset;
  terrain.view_distance = desc.view_distance;
  terrain.base_patch_size = desc.base_patch_size;
  terrain.tessellation_factor = desc.tessellation_factor;
  terrain.target_tessellated_edge_size = desc.target_tessellated_edge_size;
  terrain.layer = desc.layer;
  terrain.visible = desc.visible;
  terrain.cpu_fallback_enabled = desc.cpu_fallback_enabled;

  std::filesystem::path selected_height;
  std::filesystem::path selected_color;
  std::filesystem::path selected_control;

  if (desc.tiled) {
    terrain.height_pattern = desc.height_pattern;
    terrain.color_pattern = desc.color_pattern;
    terrain.control_pattern = desc.control_pattern;

    if (terrain.height_pattern.empty()) {
      const auto height = bestGaeaFile(files, [](const GaeaOutputFile& file) {
        return scoreGaeaHeightFile(file, false);
      });
      if (!height) {
        const auto unsupported_height = bestGaeaFile(files, [](const GaeaOutputFile& file) {
          return scoreGaeaHeightFile(file, true);
        });
        if (unsupported_height &&
            isUnsupportedGaeaBitmapExtension(unsupported_height->extension)) {
          gaeaFail(diagnostic,
                   "Gaea terrain height appears to be EXR/TIFF; export .r32, .raw/.r16, "
                   "or 16-bit PNG for the built-in loader");
        } else {
          gaeaFail(diagnostic, "could not find a Gaea height tile in: " +
                                   directory.string());
        }
        return std::nullopt;
      }
      selected_height = height->path;
      auto pattern = inferGaeaTilePattern(height->path.filename());
      if (!pattern) {
        gaeaFail(diagnostic,
                 "could not infer Gaea height tile pattern from: " +
                     height->path.filename().string());
        return std::nullopt;
      }
      terrain.height_pattern = *pattern;
    }

    terrain.height_format = terrainHeightFormatForPath(terrain.height_pattern);
    if (!validateGaeaHeightPath(terrain.height_pattern, terrain, diagnostic)) {
      return std::nullopt;
    }

    if (terrain.color_pattern.empty()) {
      if (const auto color = bestGaeaFile(files, scoreGaeaColorFile)) {
        if (auto pattern = inferGaeaTilePattern(color->path.filename())) {
          selected_color = color->path;
          terrain.color_pattern = *pattern;
        }
      }
    } else if (!validateGaeaRgbaPath(terrain.color_pattern, "color pattern", diagnostic)) {
      return std::nullopt;
    }

    if (terrain.control_pattern.empty()) {
      if (const auto control = bestGaeaFile(files, scoreGaeaControlFile)) {
        if (auto pattern = inferGaeaTilePattern(control->path.filename())) {
          selected_control = control->path;
          terrain.control_pattern = *pattern;
        }
      }
    } else if (!validateGaeaRgbaPath(terrain.control_pattern, "control pattern", diagnostic)) {
      return std::nullopt;
    }
  } else {
    selected_height = resolveGaeaPath(directory, desc.height_image);
    if (selected_height.empty()) {
      const auto height = bestGaeaFile(files, [](const GaeaOutputFile& file) {
        return scoreGaeaHeightFile(file, false);
      });
      if (!height) {
        const auto unsupported_height = bestGaeaFile(files, [](const GaeaOutputFile& file) {
          return scoreGaeaHeightFile(file, true);
        });
        if (unsupported_height &&
            isUnsupportedGaeaBitmapExtension(unsupported_height->extension)) {
          gaeaFail(diagnostic,
                   "Gaea terrain height appears to be EXR/TIFF; export .r32, .raw/.r16, "
                   "or 16-bit PNG for the built-in loader");
        } else {
          gaeaFail(diagnostic, "could not find a Gaea height image in: " +
                                   directory.string());
        }
        return std::nullopt;
      }
      selected_height = height->path;
    }
    terrain.height_image = selected_height;
    terrain.height_format = terrainHeightFormatForPath(selected_height);
    if (!validateGaeaHeightPath(selected_height, terrain, diagnostic)) {
      return std::nullopt;
    }

    selected_color = resolveGaeaPath(directory, desc.color_image);
    if (!selected_color.empty()) {
      if (!validateGaeaRgbaPath(selected_color, "color", diagnostic)) {
        return std::nullopt;
      }
      terrain.color_image = selected_color;
    } else if (const auto color = bestGaeaFile(files, scoreGaeaColorFile)) {
      selected_color = color->path;
      terrain.color_image = selected_color;
    }

    selected_control = resolveGaeaPath(directory, desc.control_image);
    if (!selected_control.empty()) {
      if (!validateGaeaRgbaPath(selected_control, "control", diagnostic)) {
        return std::nullopt;
      }
      terrain.control_image = selected_control;
    } else if (const auto control = bestGaeaFile(files, scoreGaeaControlFile)) {
      selected_control = control->path;
      terrain.control_image = selected_control;
    }
  }

  std::vector<components::TerrainDataMapKind> added_kinds;
  for (const GaeaOutputFile& file : files) {
    if (pathInUse(file.path, {selected_height, selected_color, selected_control})) {
      continue;
    }
    if (!isSupportedGaeaScalarExtension(file.extension)) {
      continue;
    }
    if (isHeaderlessScalarTerrainPath(file.path) &&
        (desc.raw_width == 0u || desc.raw_height == 0u)) {
      continue;
    }

    std::string map_name;
    auto kind = detectGaeaDataMapKind(file, map_name);
    if (!kind) {
      continue;
    }
    if (std::find(added_kinds.begin(), added_kinds.end(), *kind) != added_kinds.end()) {
      continue;
    }

    components::TerrainDataMapBinding binding{};
    binding.name = map_name;
    binding.kind = *kind;
    binding.format = terrainHeightFormatForPath(file.path);
    if (isHeaderlessScalarTerrainPath(file.path)) {
      binding.raw_width = desc.raw_width;
      binding.raw_height = desc.raw_height;
    }
    if (desc.tiled) {
      auto pattern = inferGaeaTilePattern(file.path.filename());
      if (!pattern) {
        continue;
      }
      binding.pattern = *pattern;
    } else {
      binding.image = file.path;
    }
    terrain.data_maps.push_back(std::move(binding));
    added_kinds.push_back(*kind);
  }

  return terrain;
}

std::optional<rendering::TerrainTileData> loadSingleImageTerrainTile(
    const components::TerrainComponent& terrain) {
  const std::filesystem::path height_path =
      !terrain.height_image.empty() ? terrain.height_image : terrain.heatmap_image;
  if (height_path.empty()) {
    return std::nullopt;
  }

  std::optional<assets::ScalarImage> height = loadTerrainScalarImage(height_path, terrain);
  if (!height || !height->valid()) {
    return std::nullopt;
  }

  std::optional<assets::Rgba8Image> color;
  if (!terrain.color_image.empty()) {
    color = assets::loadRgba8Image(terrain.color_image);
  } else if (!terrain.heatmap_image.empty() && terrain.heatmap_image != height_path) {
    color = assets::loadRgba8Image(terrain.heatmap_image);
  }
  if ((!color || !color->valid()) && !hasMaterialLayers(terrain) &&
      !isHeaderlessScalarTerrainPath(height_path)) {
    color = assets::loadRgba8Image(height_path);
  }

  rendering::TerrainTileData tile{};
  tile.coord = TileCoord{.x = terrain.origin_tile_x, .z = terrain.origin_tile_z};
  tile.resolution = std::max(terrain.tile_resolution, 2u);
  tile.heights = convertScalarImageToNormalizedHeights(*height, tile.resolution);
  if (color && color->valid()) {
    tile.color_width = static_cast<uint32_t>(color->width);
    tile.color_height = static_cast<uint32_t>(color->height);
    tile.color_rgba8 = std::move(color->pixels);
  } else {
    if (hasMaterialLayers(terrain)) {
      tile.color_width = 1u;
      tile.color_height = 1u;
      tile.color_rgba8 = solidWhiteRgba8();
    } else {
      tile.color_width = static_cast<uint32_t>(height->width);
      tile.color_height = static_cast<uint32_t>(height->height);
      tile.color_rgba8 = scalarImageToGrayscaleRgba8(*height);
    }
  }
  if (hasMaterialLayers(terrain)) {
    loadTerrainControlMap(terrain.control_image, tile);
  }
  loadTerrainDataMaps(terrain, tile.coord, tile);
  return tile.valid() ? std::optional<rendering::TerrainTileData>{std::move(tile)}
                      : std::nullopt;
}

std::optional<rendering::TerrainTileData> loadImageTerrainTile(
    const components::TerrainComponent& terrain,
    TileCoord coord) {
  if (terrain.tile_directory.empty()) {
    return std::nullopt;
  }
  const auto height_path =
      terrain.tile_directory /
      formatTerrainTilePattern(terrain.height_pattern, coord, terrain.tile_index_base);
  const auto color_path =
      terrain.tile_directory /
      formatTerrainTilePattern(terrain.color_pattern, coord, terrain.tile_index_base);
  std::optional<assets::ScalarImage> height = loadTerrainScalarImage(height_path, terrain);
  if (!height || !height->valid()) {
    return std::nullopt;
  }
  std::optional<assets::Rgba8Image> color;
  if (!terrain.color_pattern.empty() && std::filesystem::exists(color_path)) {
    color = assets::loadRgba8Image(color_path);
  }

  rendering::TerrainTileData tile{};
  tile.coord = coord;
  tile.resolution = std::max(terrain.tile_resolution, 2u);
  tile.heights = convertScalarImageToNormalizedHeights(*height, tile.resolution);
  if (color && color->valid()) {
    tile.color_width = static_cast<uint32_t>(color->width);
    tile.color_height = static_cast<uint32_t>(color->height);
    tile.color_rgba8 = std::move(color->pixels);
  } else {
    if (hasMaterialLayers(terrain)) {
      tile.color_width = 1u;
      tile.color_height = 1u;
      tile.color_rgba8 = solidWhiteRgba8();
    } else {
      tile.color_width = static_cast<uint32_t>(height->width);
      tile.color_height = static_cast<uint32_t>(height->height);
      tile.color_rgba8 = scalarImageToGrayscaleRgba8(*height);
    }
  }
  if (hasMaterialLayers(terrain) && !terrain.control_pattern.empty()) {
    const auto control_path =
        terrain.tile_directory /
        formatTerrainTilePattern(terrain.control_pattern, coord, terrain.tile_index_base);
    loadTerrainControlMap(control_path, tile);
  }
  loadTerrainDataMaps(terrain, coord, tile);
  return tile.valid() ? std::optional<rendering::TerrainTileData>{std::move(tile)}
                      : std::nullopt;
}

TerrainSystem::TerrainSystem(rendering::GraphicsDevice* device,
                             const assets::AssetRegistry* assets)
    : device_(device), assets_(assets) {
  worker_ = std::thread([this] { workerLoop(); });
}

TerrainSystem::~TerrainSystem() {
  stopWorker();
  for (auto& [key, state] : states_) {
    (void)key;
    destroyState(state);
  }
}

void TerrainSystem::syncTerrainColliders(world::World& world) {
  std::unordered_set<uint64_t> seen;
  world.forEach<components::TerrainComponent>(
      [&](const world::Entity entity) {
    if (!terrainColliderRequested(world, entity)) {
      return true;
    }

    const uint64_t key = entityKey(entity);
    seen.insert(key);
    const auto& terrain = world.get<components::TerrainComponent>(entity);
    const components::ColliderComponent marker = terrainColliderMarker(world, entity);
    const std::size_t signature = terrainColliderSignature(terrain, marker);
    const auto signature_it = generated_collider_signatures_.find(key);
    if (signature_it != generated_collider_signatures_.end() &&
        signature_it->second == signature &&
        world.has<components::ColliderComponent>(entity) &&
        world.get<components::ColliderComponent>(entity).type ==
            components::ColliderShapeType::HeightField) {
      return true;
    }

    std::optional<rendering::TerrainTileData> tile = loadTerrainColliderTile(terrain);
    if (!tile || !tile->valid()) {
      if (signature_it != generated_collider_signatures_.end() &&
          world.has<components::ColliderComponent>(entity) &&
          world.get<components::ColliderComponent>(entity).type ==
              components::ColliderShapeType::HeightField) {
        world.remove<components::ColliderComponent>(entity);
      }
      generated_collider_signatures_.erase(key);
      return true;
    }

    world.add(entity, terrainTileToHeightFieldCollider(terrain, *tile, marker));
    generated_collider_signatures_[key] = signature;
    return true;
  });

  for (auto it = generated_collider_signatures_.begin();
       it != generated_collider_signatures_.end();) {
    if (seen.contains(it->first)) {
      ++it;
      continue;
    }
    const world::Entity entity = entityFromKey(it->first);
    if (world.isAlive(entity) &&
        world.has<components::ColliderComponent>(entity) &&
        world.get<components::ColliderComponent>(entity).type ==
            components::ColliderShapeType::HeightField) {
      world.remove<components::ColliderComponent>(entity);
    }
    it = generated_collider_signatures_.erase(it);
  }
}

TerrainSystem::TerrainState& TerrainSystem::ensureState(
    uint64_t key,
    const components::TerrainComponent& terrain) {
  TerrainState& state = states_[key];
  const rendering::TerrainDesc desc = terrainDescFromComponent(terrain);
  const TerrainSourceSettings source_settings{
      .source = terrain.source,
      .tile_directory = terrain.tile_directory,
      .height_pattern = terrain.height_pattern,
      .color_pattern = terrain.color_pattern,
      .control_pattern = terrain.control_pattern,
      .height_image = terrain.height_image,
      .heatmap_image = terrain.heatmap_image,
      .color_image = terrain.color_image,
      .control_image = terrain.control_image,
      .height_format = terrain.height_format,
      .raw_width = terrain.raw_width,
      .raw_height = terrain.raw_height,
      .raw_little_endian = terrain.raw_little_endian,
      .flip_y = terrain.flip_y,
      .height_value_min = terrain.height_value_min,
      .height_value_max = terrain.height_value_max,
      .tile_index_base = terrain.tile_index_base,
      .asset_registry_version = assets_ != nullptr ? assets_->version() : 0u,
      .material_layers = terrain.material_layers,
      .data_maps = terrain.data_maps,
  };
  if (state.terrain == rendering::kInvalidTerrain ||
      !terrainDescEquals(state.desc, desc) ||
      !(state.source_settings == source_settings)) {
    destroyState(state);
    state.desc = desc;
    state.source_settings = source_settings;
    state.terrain = device_ != nullptr ? device_->createTerrain(desc)
                                       : rendering::kInvalidTerrain;
    if (device_ != nullptr && state.terrain != rendering::kInvalidTerrain) {
      device_->clearTerrainMaterialLayers(state.terrain);
      const uint32_t max_layers =
          static_cast<uint32_t>(std::min<std::size_t>(terrain.material_layers.size(), 4u));
      for (uint32_t layer = 0u; layer < max_layers; ++layer) {
        if (auto data = loadTerrainMaterialLayer(
                terrain.material_layers[layer], layer, assets_);
            data.has_value()) {
          device_->uploadTerrainMaterialLayer(state.terrain, *data);
        }
      }
    }
    state.generation += 1u;
    state.desired.clear();
    state.loaded.clear();
    state.queued.clear();
  }
  return state;
}

void TerrainSystem::destroyState(TerrainState& state) {
  if (device_ != nullptr && state.terrain != rendering::kInvalidTerrain) {
    device_->destroyTerrain(state.terrain);
  }
  state.terrain = rendering::kInvalidTerrain;
  state.desired.clear();
  state.loaded.clear();
  state.queued.clear();
}

void TerrainSystem::cleanupStaleStates(world::World& world) {
  for (auto it = states_.begin(); it != states_.end();) {
    const world::Entity entity = entityFromKey(it->first);
    if (!world.isAlive(entity) || !world.has<components::TerrainComponent>(entity)) {
      destroyState(it->second);
      it = states_.erase(it);
      continue;
    }
    ++it;
  }
}

void TerrainSystem::queueTile(uint64_t key,
                              TerrainState& state,
                              const components::TerrainComponent& terrain,
                              TileCoord coord) {
  if (state.loaded.contains(coord) || state.queued.contains(coord)) {
    return;
  }
  state.queued.insert(coord);
  {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    requests_.push_back(TileRequest{
        .entity_key = key,
        .generation = state.generation,
        .terrain = terrain,
        .coord = coord,
    });
  }
  queue_cv_.notify_one();
}

void TerrainSystem::drainCompleted() {
  std::vector<CompletedTile> completed;
  {
    std::lock_guard<std::mutex> lock(completed_mutex_);
    completed.swap(completed_);
  }
  for (CompletedTile& tile : completed) {
    auto state_it = states_.find(tile.entity_key);
    if (state_it == states_.end()) {
      continue;
    }
    TerrainState& state = state_it->second;
    state.queued.erase(tile.coord);
    if (state.generation != tile.generation ||
        state.terrain == rendering::kInvalidTerrain ||
        !tile.data.has_value() ||
        !tile.data->valid() ||
        !state.desired.contains(tile.coord)) {
      continue;
    }
    if (device_ != nullptr) {
      device_->uploadTerrainTile(state.terrain, *tile.data);
      state.loaded.insert(tile.coord);
    }
  }
}

void TerrainSystem::update(world::World& world, float, float interpolation_alpha) {
  syncTerrainColliders(world);
  if (device_ == nullptr) {
    return;
  }
  cleanupStaleStates(world);
  drainCompleted();

  bool has_camera = false;
  glm::vec3 camera_position{0.0f};
  world.forEach<components::CameraComponent, components::TransformComponent>(
      [&](const world::Entity entity) {
    const auto& camera = world.get<components::CameraComponent>(entity);
    if (!camera.is_primary && has_camera) {
      return true;
    }
    const auto& transform = world.get<components::TransformComponent>(entity);
    camera_position = toTerrainGlm(transform.getInterpolatedPosition(interpolation_alpha));
    has_camera = true;
    return !camera.is_primary;
  });
  world.forEach<components::TerrainComponent, components::TransformComponent>(
      [&](const world::Entity entity) {
    const auto& terrain = world.get<components::TerrainComponent>(entity);
    bool visible = terrain.visible;
    if (world.has<components::VisibilityComponent>(entity)) {
      visible = visible && world.get<components::VisibilityComponent>(entity).visible;
    }
    const uint64_t key = entityKey(entity);
    TerrainState& state = ensureState(key, terrain);
    if (state.terrain == rendering::kInvalidTerrain) {
      return true;
    }

    const auto& transform = world.get<components::TransformComponent>(entity);
    const glm::vec3 terrain_origin =
        toTerrainGlm(transform.getInterpolatedPosition(interpolation_alpha));
    std::vector<TileCoord> coords;
    if (terrain.source == components::TerrainSourceType::SingleImage) {
      coords.push_back(TileCoord{.x = terrain.origin_tile_x,
                                 .z = terrain.origin_tile_z});
    } else {
      if (!has_camera) {
        return true;
      }
      const TileCoord center =
          terrainTileCoordForWorldPosition(camera_position, terrain_origin, terrain);
      coords = terrainChunkCoordsAround(center, terrain.view_distance, terrain.tile_size);
    }
    std::unordered_set<TileCoord, TileCoordHash> desired;
    desired.reserve(coords.size());
    for (TileCoord coord : coords) {
      desired.insert(coord);
    }
    state.desired = std::move(desired);

    for (auto it = state.loaded.begin(); it != state.loaded.end();) {
      if (!state.desired.contains(*it)) {
        device_->evictTerrainTile(state.terrain, *it);
        it = state.loaded.erase(it);
      } else {
        ++it;
      }
    }

    for (TileCoord coord : coords) {
      queueTile(key, state, terrain, coord);
    }

    glm::mat4 model(1.0f);
    model = glm::translate(model, terrain_origin);
    for (TileCoord coord : coords) {
      if (!state.loaded.contains(coord)) {
        continue;
      }
      device_->submitTerrain(rendering::TerrainDrawItem{
          .instance = terrainInstanceKey(key, coord),
          .terrain = state.terrain,
          .coord = coord,
          .transform = model,
          .layer = static_cast<rendering::LayerId>(terrain.layer),
          .visible = visible,
      });
    }
    return true;
  });

  drainCompleted();
}

void TerrainSystem::workerLoop() {
  for (;;) {
    TileRequest request{};
    {
      std::unique_lock<std::mutex> lock(queue_mutex_);
      queue_cv_.wait(lock, [&] { return stop_worker_ || !requests_.empty(); });
      if (stop_worker_ && requests_.empty()) {
        return;
      }
      request = std::move(requests_.front());
      requests_.pop_front();
    }

    std::optional<rendering::TerrainTileData> data;
    switch (request.terrain.source) {
      case components::TerrainSourceType::Procedural:
        data = generateProceduralTerrainTile(request.terrain, request.coord);
        break;
      case components::TerrainSourceType::ImageTileDirectory:
        data = loadImageTerrainTile(request.terrain, request.coord);
        break;
      case components::TerrainSourceType::SingleImage:
        data = loadSingleImageTerrainTile(request.terrain);
        break;
    }

    {
      std::lock_guard<std::mutex> lock(completed_mutex_);
      completed_.push_back(CompletedTile{
          .entity_key = request.entity_key,
          .generation = request.generation,
          .coord = request.coord,
          .data = std::move(data),
      });
    }
  }
}

void TerrainSystem::stopWorker() {
  {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    stop_worker_ = true;
    requests_.clear();
  }
  queue_cv_.notify_all();
  if (worker_.joinable()) {
    worker_.join();
  }
  {
    std::lock_guard<std::mutex> lock(completed_mutex_);
    completed_.clear();
  }
}

}  // namespace karma::visual::terrain
