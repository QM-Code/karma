#include "karma/assets.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <nlohmann/json.hpp>

namespace {

#define KARMA_REQUIRE(expression)                                      \
  do {                                                                 \
    if (!(expression)) {                                               \
      std::cerr << "Requirement failed: " << #expression << " at "   \
                << __FILE__ << ":" << __LINE__ << '\n';              \
      std::abort();                                                     \
    }                                                                  \
  } while (false)

std::filesystem::path makeTempDir(std::string_view label) {
  static std::atomic<uint64_t> sequence{0u};
  const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
  const std::filesystem::path dir =
      std::filesystem::temp_directory_path() /
      (std::string(label) + "_" + std::to_string(stamp) + "_" +
       std::to_string(sequence.fetch_add(1u, std::memory_order_relaxed)));
  std::filesystem::create_directories(dir);
  return dir;
}

void writeText(const std::filesystem::path& path, std::string_view text) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream stream(path, std::ios::trunc);
  stream << text;
  KARMA_REQUIRE(static_cast<bool>(stream));
}

std::vector<uint8_t> readBinary(const std::filesystem::path& path) {
  std::ifstream stream(path, std::ios::binary);
  KARMA_REQUIRE(static_cast<bool>(stream));
  stream.seekg(0, std::ios::end);
  const std::streamoff size = stream.tellg();
  KARMA_REQUIRE(size >= 0);
  stream.seekg(0, std::ios::beg);
  std::vector<uint8_t> bytes(static_cast<std::size_t>(size));
  if (!bytes.empty()) {
    stream.read(reinterpret_cast<char*>(bytes.data()), size);
  }
  KARMA_REQUIRE(static_cast<bool>(stream));
  return bytes;
}

void writeBinary(const std::filesystem::path& path,
                 const std::vector<uint8_t>& bytes) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream stream(path, std::ios::binary | std::ios::trunc);
  stream.write(reinterpret_cast<const char*>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
  KARMA_REQUIRE(static_cast<bool>(stream));
}

void writeU32(std::vector<uint8_t>& bytes, std::size_t offset, uint32_t value) {
  KARMA_REQUIRE(offset + 4u <= bytes.size());
  for (uint32_t index = 0u; index < 4u; ++index) {
    bytes[offset + index] = static_cast<uint8_t>((value >> (index * 8u)) & 0xffu);
  }
}

void writeU64(std::vector<uint8_t>& bytes, std::size_t offset, uint64_t value) {
  KARMA_REQUIRE(offset + 8u <= bytes.size());
  for (uint32_t index = 0u; index < 8u; ++index) {
    bytes[offset + index] = static_cast<uint8_t>((value >> (index * 8u)) & 0xffu);
  }
}

karma::assets::TextureAsset makeTexture() {
  karma::assets::TextureAsset texture{};
  texture.desc.width = 1;
  texture.desc.height = 1;
  texture.desc.format = karma::rendering::TextureFormat::RGBA8;
  texture.desc.mip_levels = 1u;
  texture.payload_format = karma::assets::TextureAsset::PayloadFormat::RGBA8;
  texture.semantic = karma::assets::TextureAsset::Semantic::Color;
  texture.bytes = {10u, 20u, 30u, 255u};
  texture.subresources.push_back(karma::rendering::TextureUploadSubresource{
      .mip_level = 0u,
      .array_layer = 0u,
      .width = 1,
      .height = 1,
      .offset = 0u,
      .size = 4u,
      .row_stride = 4u,
  });
  return texture;
}

karma::assets::AssetCache makeCache(const std::filesystem::path& root) {
  return karma::assets::AssetCache({
      .root = root,
      .enabled = true,
      .flush = false,
      .ensure_layout = true,
  });
}

void testCacheKeyValidationAndContainment() {
  KARMA_REQUIRE(karma::assets::AssetCache::isValidCacheKey("texture_blob-v2.1"));
  KARMA_REQUIRE(karma::assets::AssetCache::isValidCacheKey("ABC_012"));
  for (const std::string invalid : {
           std::string{},
           std::string{"."},
           std::string{".."},
           std::string{"../escape"},
           std::string{"nested/key"},
           std::string{"nested\\key"},
           std::string{"space key"},
       }) {
    KARMA_REQUIRE(!karma::assets::AssetCache::isValidCacheKey(invalid));
    KARMA_REQUIRE(!karma::assets::AssetCache::cacheKeyValidationError(invalid).empty());
  }
  KARMA_REQUIRE(!karma::assets::AssetCache::isValidCacheKey(std::string(129u, 'x')));

  const std::filesystem::path root = makeTempDir("karma_asset_cache_key_tests");
  karma::assets::AssetCache cache = makeCache(root);
  std::string diagnostic;
  KARMA_REQUIRE(!cache.writeTexture("../escape", makeTexture(), &diagnostic));
  KARMA_REQUIRE(!diagnostic.empty());
  KARMA_REQUIRE(!std::filesystem::exists(root / "escape.kasset"));

  diagnostic.clear();
  KARMA_REQUIRE(!cache.readTexture("nested/key", &diagnostic).has_value());
  KARMA_REQUIRE(!diagnostic.empty());

  diagnostic.clear();
  KARMA_REQUIRE(!cache.writePackageManifest("../manifest",
                                            nlohmann::json::object(),
                                            &diagnostic));
  KARMA_REQUIRE(!diagnostic.empty());
  KARMA_REQUIRE(!std::filesystem::exists(root / "manifest.json"));
  std::filesystem::remove_all(root);
}

void testFlushPreservesRootAndRunsOncePerRoot() {
  const std::filesystem::path root = makeTempDir("karma_asset_cache_flush_tests");
  writeText(root / "keep.txt", "unrelated");
  writeText(root / "blobs" / "stale.kasset", "stale");
  writeText(root / "packages" / "stale.json", "stale");
  writeText(root / "index.json", "{}");

  const karma::assets::AssetCacheConfig config{
      .root = root,
      .enabled = true,
      .flush = true,
      .ensure_layout = false,
  };
  karma::assets::AssetCache first(config);
  KARMA_REQUIRE(std::filesystem::exists(root / "keep.txt"));
  KARMA_REQUIRE(!std::filesystem::exists(root / "blobs"));
  KARMA_REQUIRE(!std::filesystem::exists(root / "packages"));
  KARMA_REQUIRE(!std::filesystem::exists(root / "index.json"));

  writeText(root / "blobs" / "created_after_first_flush.kasset", "live");
  karma::assets::AssetCache second(config);
  KARMA_REQUIRE(std::filesystem::exists(root / "blobs" /
                                        "created_after_first_flush.kasset"));

  second.flush();
  KARMA_REQUIRE(!std::filesystem::exists(root / "blobs"));
  KARMA_REQUIRE(std::filesystem::exists(root / "keep.txt"));

  const std::filesystem::path filesystem_root = root.root_path();
  if (!filesystem_root.empty()) {
    karma::assets::AssetCache unsafe({
        .root = filesystem_root,
        .enabled = true,
        .flush = false,
        .ensure_layout = false,
    });
    KARMA_REQUIRE(!unsafe.enabled());

    std::error_code symlink_error;
    const std::filesystem::path symlink_root = root / "filesystem_root_link";
    std::filesystem::create_directory_symlink(filesystem_root,
                                              symlink_root,
                                              symlink_error);
    if (!symlink_error) {
      karma::assets::AssetCache unsafe_symlink({
          .root = symlink_root,
          .enabled = true,
          .flush = false,
          .ensure_layout = false,
      });
      KARMA_REQUIRE(!unsafe_symlink.enabled());
    }
  }
  std::filesystem::remove_all(root);
}

void testTextureBlobValidation() {
  const std::filesystem::path root = makeTempDir("karma_asset_cache_texture_tests");
  karma::assets::AssetCache cache = makeCache(root);
  KARMA_REQUIRE(cache.writeTexture("valid_texture", makeTexture()));
  const auto valid = cache.readTexture("valid_texture");
  KARMA_REQUIRE(valid.has_value());
  KARMA_REQUIRE(valid->bytes == makeTexture().bytes);

  const std::filesystem::path valid_path = root / "blobs" / "valid_texture.kasset";
  const std::vector<uint8_t> serialized = readBinary(valid_path);
  KARMA_REQUIRE(serialized.size() > 116u);

  std::vector<uint8_t> bad_count = serialized;
  writeU32(bad_count, 60u, 0xffffffffu);
  writeBinary(root / "blobs" / "bad_count.kasset", bad_count);
  KARMA_REQUIRE(!cache.readTexture("bad_count").has_value());

  std::vector<uint8_t> bad_format = serialized;
  writeU32(bad_format, 36u, 0xffffffffu);
  writeBinary(root / "blobs" / "bad_format.kasset", bad_format);
  KARMA_REQUIRE(!cache.readTexture("bad_format").has_value());

  std::vector<uint8_t> bad_range = serialized;
  writeU64(bad_range, 100u, 4096u);
  writeBinary(root / "blobs" / "bad_range.kasset", bad_range);
  KARMA_REQUIRE(!cache.readTexture("bad_range").has_value());

  std::filesystem::remove_all(root);
}

void testMeshBlobCountValidation() {
  const std::filesystem::path root = makeTempDir("karma_asset_cache_mesh_tests");
  karma::assets::AssetCache cache = makeCache(root);
  karma::world::MeshData mesh{};
  mesh.vertices.push_back(glm::vec3{1.0f, 2.0f, 3.0f});
  KARMA_REQUIRE(cache.writeMesh("valid_mesh", mesh));
  KARMA_REQUIRE(cache.readMesh("valid_mesh").has_value());

  std::vector<uint8_t> serialized =
      readBinary(root / "blobs" / "valid_mesh.kasset");
  KARMA_REQUIRE(serialized.size() > 36u);
  writeU64(serialized, 28u, 0xffffffffffffffffull);
  writeBinary(root / "blobs" / "bad_mesh_count.kasset", serialized);
  KARMA_REQUIRE(!cache.readMesh("bad_mesh_count").has_value());
  std::filesystem::remove_all(root);
}

void testAnimationBlobCountValidation() {
  const std::filesystem::path root = makeTempDir("karma_asset_cache_animation_tests");
  karma::assets::AssetCache cache = makeCache(root);
  karma::world::AnimationClip clip{};
  clip.name = "";
  KARMA_REQUIRE(cache.writeAnimationClip("valid_animation", clip));
  KARMA_REQUIRE(cache.readAnimationClip("valid_animation").has_value());

  std::vector<uint8_t> serialized =
      readBinary(root / "blobs" / "valid_animation.kasset");
  KARMA_REQUIRE(serialized.size() > 56u);
  writeU64(serialized, 48u, 0xffffffffffffffffull);
  writeBinary(root / "blobs" / "bad_animation_count.kasset", serialized);
  KARMA_REQUIRE(!cache.readAnimationClip("bad_animation_count").has_value());
  std::filesystem::remove_all(root);
}

void testConcurrentIndexUpdates() {
  const std::filesystem::path root = makeTempDir("karma_asset_cache_index_tests");
  karma::assets::AssetCache cache = makeCache(root);
  const karma::assets::TextureAsset texture = makeTexture();

  constexpr std::size_t kWorkerCount = 12u;
  std::atomic<bool> succeeded{true};
  std::vector<std::thread> workers;
  workers.reserve(kWorkerCount);
  for (std::size_t index = 0u; index < kWorkerCount; ++index) {
    workers.emplace_back([&, index]() {
      if (!cache.writeTexture("thread_" + std::to_string(index), texture)) {
        succeeded.store(false, std::memory_order_relaxed);
      }
    });
  }
  for (std::thread& worker : workers) {
    worker.join();
  }
  KARMA_REQUIRE(succeeded.load(std::memory_order_relaxed));

  std::ifstream stream(root / "index.json");
  nlohmann::json index;
  stream >> index;
  KARMA_REQUIRE(index.is_object());
  KARMA_REQUIRE(index["entries"].is_object());
  for (std::size_t worker = 0u; worker < kWorkerCount; ++worker) {
    KARMA_REQUIRE(index["entries"].contains("thread_" + std::to_string(worker)));
  }
  stream.close();
  std::filesystem::remove_all(root);
}

}  // namespace

int main() {
  testCacheKeyValidationAndContainment();
  testFlushPreservesRootAndRunsOncePerRoot();
  testTextureBlobValidation();
  testMeshBlobCountValidation();
  testAnimationBlobCountValidation();
  testConcurrentIndexUpdates();
  return 0;
}
