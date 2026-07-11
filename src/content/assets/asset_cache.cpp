#include "karma/assets.h"

#include "asset_cache_serializers.h"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <exception>
#include <fstream>
#include <mutex>
#include <optional>
#include <sstream>
#include <system_error>
#include <unordered_set>
#include <vector>

#if !defined(_WIN32)
#include <fcntl.h>
#include <unistd.h>
#endif

#include <nlohmann/json.hpp>

namespace karma::assets {

namespace {

using Json = nlohmann::json;

constexpr std::size_t kMaxCacheKeyLength = 128u;

std::mutex& cacheIndexMutex() {
  static std::mutex mutex;
  return mutex;
}

std::atomic<uint64_t>& cacheTempSequence() {
  static std::atomic<uint64_t> sequence{0u};
  return sequence;
}

std::filesystem::path normalizedRoot(const std::filesystem::path& root) {
  std::error_code ec;
  std::filesystem::path normalized = std::filesystem::weakly_canonical(root, ec);
  if (ec) {
    ec.clear();
    normalized = std::filesystem::absolute(root, ec);
  }
  if (ec) {
    normalized = root;
  }
  return normalized.lexically_normal();
}

bool isFilesystemRoot(const std::filesystem::path& root) {
  if (root.empty()) {
    return false;
  }
  const std::filesystem::path normalized = normalizedRoot(root);
  return !normalized.empty() && normalized == normalized.root_path();
}

std::string normalizedRootKey(const std::filesystem::path& root) {
  return normalizedRoot(root).generic_string();
}

bool claimProcessFlush(const std::filesystem::path& root) {
  static std::mutex mutex;
  static std::unordered_set<std::string> flushed_roots;
  const std::string key = normalizedRootKey(root);
  std::scoped_lock lock(mutex);
  return flushed_roots.insert(key).second;
}

bool validateCacheKey(std::string_view key, std::string* diagnostic) {
  const std::string error = AssetCache::cacheKeyValidationError(key);
  if (error.empty()) {
    return true;
  }
  if (diagnostic != nullptr) {
    *diagnostic = error;
  }
  return false;
}

bool envFlagOff(const char* value) {
  if (value == nullptr || value[0] == '\0') {
    return false;
  }
  const std::string_view text(value);
  return text == "0" || text == "false" || text == "FALSE" || text == "off" || text == "OFF";
}

bool envFlagOn(const char* value) {
  if (value == nullptr || value[0] == '\0') {
    return false;
  }
  return !envFlagOff(value);
}

std::filesystem::path homePath() {
  if (const char* home = std::getenv("HOME"); home != nullptr && home[0] != '\0') {
    return home;
  }
  return {};
}

std::filesystem::path defaultCacheRoot() {
#if defined(_WIN32)
  if (const char* local = std::getenv("LOCALAPPDATA"); local != nullptr && local[0] != '\0') {
    return std::filesystem::path(local) / "Karma" / "assets";
  }
  return std::filesystem::path("Karma") / "assets";
#elif defined(__APPLE__)
  const std::filesystem::path home = homePath();
  return home.empty() ? std::filesystem::path("karma/assets")
                      : home / "Library" / "Caches" / "karma" / "assets";
#else
  if (const char* xdg = std::getenv("XDG_CACHE_HOME"); xdg != nullptr && xdg[0] != '\0') {
    return std::filesystem::path(xdg) / "karma" / "assets";
  }
  const std::filesystem::path home = homePath();
  return home.empty() ? std::filesystem::path(".cache/karma/assets")
                      : home / ".cache" / "karma" / "assets";
#endif
}

std::optional<std::vector<uint8_t>> readBinaryFile(const std::filesystem::path& path) {
  std::ifstream stream(path, std::ios::binary);
  if (!stream) {
    return std::nullopt;
  }
  stream.seekg(0, std::ios::end);
  const std::streamoff size = stream.tellg();
  if (size < 0) {
    return std::nullopt;
  }
  stream.seekg(0, std::ios::beg);
  std::vector<uint8_t> bytes(static_cast<std::size_t>(size));
  if (!bytes.empty()) {
    stream.read(reinterpret_cast<char*>(bytes.data()), size);
  }
  if (!stream && size > 0) {
    return std::nullopt;
  }
  return bytes;
}

bool fsyncFile(const std::filesystem::path& path) {
#if defined(_WIN32)
  (void)path;
  return true;
#else
  const int fd = ::open(path.c_str(), O_RDONLY);
  if (fd < 0) {
    return false;
  }
  const bool ok = ::fsync(fd) == 0;
  ::close(fd);
  return ok;
#endif
}

bool writeAtomic(const std::filesystem::path& path,
                 const std::vector<uint8_t>& bytes,
                 std::string* diagnostic) {
  std::error_code ec;
  std::filesystem::create_directories(path.parent_path(), ec);
  if (ec) {
    if (diagnostic != nullptr) {
      *diagnostic = "failed to create cache directory: " + ec.message();
    }
    return false;
  }

  const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
  const uint64_t sequence = cacheTempSequence().fetch_add(1u, std::memory_order_relaxed);
  const std::filesystem::path temp =
      path.parent_path() /
      (path.filename().string() + ".tmp." + std::to_string(stamp) + "." +
       std::to_string(sequence));
  {
    std::ofstream stream(temp, std::ios::binary | std::ios::trunc);
    if (!stream) {
      if (diagnostic != nullptr) {
        *diagnostic = "failed to open temp cache file: " + temp.string();
      }
      return false;
    }
    if (!bytes.empty()) {
      stream.write(reinterpret_cast<const char*>(bytes.data()),
                   static_cast<std::streamsize>(bytes.size()));
    }
    stream.flush();
    if (!stream) {
      if (diagnostic != nullptr) {
        *diagnostic = "failed to write temp cache file: " + temp.string();
      }
      std::filesystem::remove(temp, ec);
      return false;
    }
  }
  (void)fsyncFile(temp);

  std::filesystem::rename(temp, path, ec);
  if (ec) {
    std::filesystem::remove(path, ec);
    ec.clear();
    std::filesystem::rename(temp, path, ec);
  }
  if (ec) {
    if (diagnostic != nullptr) {
      *diagnostic = "failed to commit cache file: " + ec.message();
    }
    std::filesystem::remove(temp, ec);
    return false;
  }
  return true;
}

bool writeAtomicText(const std::filesystem::path& path,
                     const std::string& text,
                     std::string* diagnostic) {
  const auto* begin = reinterpret_cast<const uint8_t*>(text.data());
  return writeAtomic(path, std::vector<uint8_t>(begin, begin + text.size()), diagnostic);
}

std::string hex64(uint64_t value) {
  constexpr char kDigits[] = "0123456789abcdef";
  std::string out(16u, '0');
  for (int i = 15; i >= 0; --i) {
    out[static_cast<std::size_t>(i)] = kDigits[value & 0x0full];
    value >>= 4u;
  }
  return out;
}

uint64_t fnv1aAppend(uint64_t hash, const uint8_t* data, std::size_t size) {
  for (std::size_t i = 0u; i < size; ++i) {
    hash ^= static_cast<uint64_t>(data[i]);
    hash *= 1099511628211ull;
  }
  return hash;
}

}  // namespace

AssetCacheConfig AssetCacheConfig::fromEnvironment() {
  AssetCacheConfig config{};
  if (const char* dir = std::getenv("KARMA_ASSET_CACHE_DIR"); dir != nullptr && dir[0] != '\0') {
    config.root = dir;
  } else {
    config.root = defaultCacheRoot();
  }
  if (envFlagOff(std::getenv("KARMA_ASSET_CACHE"))) {
    config.enabled = false;
  }
  config.flush = envFlagOn(std::getenv("KARMA_ASSET_CACHE_FLUSH"));
  return config;
}

AssetCache::AssetCache(AssetCacheConfig config) : config_(std::move(config)) {
  if (isFilesystemRoot(config_.root)) {
    config_.enabled = false;
  }
  if (enabled()) {
    if (config_.flush && claimProcessFlush(config_.root)) {
      flush();
    }
    if (config_.ensure_layout) {
      ensureLayout();
    }
  }
}

bool AssetCache::isValidCacheKey(std::string_view key) {
  return cacheKeyValidationError(key).empty();
}

std::string AssetCache::cacheKeyValidationError(std::string_view key) {
  if (key.empty()) {
    return "cache key must not be empty";
  }
  if (key.size() > kMaxCacheKeyLength) {
    return "cache key exceeds 128 characters";
  }
  if (key == "." || key == "..") {
    return "cache key must be a logical identifier";
  }
  for (const unsigned char byte : key) {
    const bool ascii_alphanumeric =
        (byte >= 'a' && byte <= 'z') ||
        (byte >= 'A' && byte <= 'Z') ||
        (byte >= '0' && byte <= '9');
    if (ascii_alphanumeric || byte == '-' || byte == '_' || byte == '.') {
      continue;
    }
    return "cache key may contain only ASCII letters, digits, '.', '-', and '_'";
  }
  return {};
}

void AssetCache::flush() {
  if (config_.root.empty()) {
    return;
  }
  if (isFilesystemRoot(config_.root)) {
    return;
  }
  const std::filesystem::path root = config_.root.lexically_normal();
  std::scoped_lock lock(cacheIndexMutex());
  std::error_code ec;
  std::filesystem::remove_all(root / "blobs", ec);
  ec.clear();
  std::filesystem::remove_all(root / "packages", ec);
  ec.clear();
  std::filesystem::remove(root / "index.json", ec);
}

std::string AssetCache::makeSourceKey(const std::filesystem::path& source,
                                      std::string_view importer_version,
                                      const Json& import_options,
                                      std::string_view package_manifest_hash,
                                      std::string_view dependency_version) const {
  Json key{
      {"asset_cache_version", std::string(kAssetCacheVersion)},
      {"source", source.lexically_normal().generic_string()},
      {"importer_version", std::string(importer_version)},
      {"options", import_options},
      {"package_manifest_hash", std::string(package_manifest_hash)},
      {"dependency_version", std::string(dependency_version)},
  };
  std::error_code ec;
  if (std::filesystem::exists(source, ec)) {
    key["source_size"] = static_cast<uint64_t>(std::filesystem::file_size(source, ec));
    if (const auto content_hash = hashFile(source)) {
      key["source_hash"] = *content_hash;
    }
    const auto mtime = std::filesystem::last_write_time(source, ec);
    if (!ec) {
      key["source_mtime"] = mtime.time_since_epoch().count();
    }
  }
  return hashString(key.dump());
}

std::optional<TextureAsset> AssetCache::readTexture(std::string_view cache_key,
                                                    std::string* diagnostic) {
  if (!enabled() || !validateCacheKey(cache_key, diagnostic)) {
    return std::nullopt;
  }
  auto bytes = readBinaryFile(blobPath(cache_key));
  if (!bytes.has_value()) {
    return std::nullopt;
  }
  auto texture = detail::deserializeTexture(*bytes, diagnostic);
  if (texture.has_value()) {
    texture->content_hash = "asset-cache:" + std::string(cache_key);
  }
  return texture;
}

bool AssetCache::writeTexture(std::string_view cache_key,
                              const TextureAsset& texture,
                              std::string* diagnostic) {
  if (!enabled() || !validateCacheKey(cache_key, diagnostic)) {
    return false;
  }
  const bool ok = writeAtomic(blobPath(cache_key), detail::serializeTexture(texture), diagnostic);
  if (ok) {
    touchIndex(cache_key, "texture");
  }
  return ok;
}

bool AssetCache::writeTextureNoIndex(std::string_view cache_key,
                                     const TextureAsset& texture,
                                     std::string* diagnostic) {
  if (!enabled() || !validateCacheKey(cache_key, diagnostic)) {
    return false;
  }
  return writeAtomic(blobPath(cache_key), detail::serializeTexture(texture), diagnostic);
}

std::optional<world::MeshData> AssetCache::readMesh(std::string_view cache_key,
                                                       std::string* diagnostic) {
  if (!enabled() || !validateCacheKey(cache_key, diagnostic)) {
    return std::nullopt;
  }
  auto bytes = readBinaryFile(blobPath(cache_key));
  if (!bytes.has_value()) {
    return std::nullopt;
  }
  return detail::deserializeMesh(*bytes, diagnostic);
}

bool AssetCache::writeMesh(std::string_view cache_key,
                           const world::MeshData& mesh,
                           std::string* diagnostic) {
  if (!enabled() || !validateCacheKey(cache_key, diagnostic)) {
    return false;
  }
  const bool ok = writeAtomic(blobPath(cache_key), detail::serializeMesh(mesh), diagnostic);
  if (ok) {
    touchIndex(cache_key, "mesh");
  }
  return ok;
}

std::optional<rendering::MaterialAssetDesc> AssetCache::readMaterialAsset(
    std::string_view cache_key,
    std::string* diagnostic) {
  if (!enabled() || !validateCacheKey(cache_key, diagnostic)) {
    return std::nullopt;
  }
  auto bytes = readBinaryFile(blobPath(cache_key));
  if (!bytes.has_value()) {
    return std::nullopt;
  }
  return detail::deserializeMaterialAsset(*bytes, diagnostic);
}

bool AssetCache::writeMaterialAsset(std::string_view cache_key,
                                    const rendering::MaterialAssetDesc& material,
                                    std::string* diagnostic) {
  if (!enabled() || !validateCacheKey(cache_key, diagnostic)) {
    return false;
  }
  const bool ok = writeAtomic(blobPath(cache_key),
                              detail::serializeMaterialAsset(material),
                              diagnostic);
  if (ok) {
    touchIndex(cache_key, "material_asset");
  }
  return ok;
}

std::optional<rendering::MaterialVariantDesc> AssetCache::readMaterialVariant(
    std::string_view cache_key,
    std::string* diagnostic) {
  if (!enabled() || !validateCacheKey(cache_key, diagnostic)) {
    return std::nullopt;
  }
  auto bytes = readBinaryFile(blobPath(cache_key));
  if (!bytes.has_value()) {
    return std::nullopt;
  }
  return detail::deserializeMaterialVariant(*bytes, diagnostic);
}

bool AssetCache::writeMaterialVariant(std::string_view cache_key,
                                      const rendering::MaterialVariantDesc& material,
                                      std::string* diagnostic) {
  if (!enabled() || !validateCacheKey(cache_key, diagnostic)) {
    return false;
  }
  const bool ok = writeAtomic(blobPath(cache_key),
                              detail::serializeMaterialVariant(material),
                              diagnostic);
  if (ok) {
    touchIndex(cache_key, "material_variant");
  }
  return ok;
}

std::optional<visual::particles::ParticleEffectAsset> AssetCache::readParticleEffect(
    std::string_view cache_key,
    std::string* diagnostic) {
  if (!enabled() || !validateCacheKey(cache_key, diagnostic)) {
    return std::nullopt;
  }
  auto bytes = readBinaryFile(blobPath(cache_key));
  if (!bytes.has_value()) {
    return std::nullopt;
  }
  return detail::deserializeParticleEffect(*bytes, diagnostic);
}

bool AssetCache::writeParticleEffect(std::string_view cache_key,
                                     const visual::particles::ParticleEffectAsset& effect,
                                     std::string* diagnostic) {
  if (!enabled() || !validateCacheKey(cache_key, diagnostic)) {
    return false;
  }
  const bool ok = writeAtomic(blobPath(cache_key),
                              detail::serializeParticleEffect(effect),
                              diagnostic);
  if (ok) {
    touchIndex(cache_key, "particle_effect");
  }
  return ok;
}

std::optional<GltfSceneAsset> AssetCache::readGltfScene(std::string_view cache_key,
                                                        std::string* diagnostic) {
  if (!enabled() || !validateCacheKey(cache_key, diagnostic)) {
    return std::nullopt;
  }
  auto bytes = readBinaryFile(blobPath(cache_key));
  if (!bytes.has_value()) {
    return std::nullopt;
  }
  return detail::deserializeGltfScene(*bytes, diagnostic);
}

bool AssetCache::writeGltfScene(std::string_view cache_key,
                                const GltfSceneAsset& scene,
                                std::string* diagnostic) {
  if (!enabled() || !validateCacheKey(cache_key, diagnostic)) {
    return false;
  }
  const bool ok = writeAtomic(blobPath(cache_key),
                              detail::serializeGltfScene(scene),
                              diagnostic);
  if (ok) {
    touchIndex(cache_key, "gltf_scene");
  }
  return ok;
}

std::optional<world::AnimationClip> AssetCache::readAnimationClip(
    std::string_view cache_key,
    std::string* diagnostic) {
  if (!enabled() || !validateCacheKey(cache_key, diagnostic)) {
    return std::nullopt;
  }
  auto bytes = readBinaryFile(blobPath(cache_key));
  if (!bytes.has_value()) {
    return std::nullopt;
  }
  return detail::deserializeAnimationClip(*bytes, diagnostic);
}

bool AssetCache::writeAnimationClip(std::string_view cache_key,
                                    const world::AnimationClip& clip,
                                    std::string* diagnostic) {
  if (!enabled() || !validateCacheKey(cache_key, diagnostic)) {
    return false;
  }
  const bool ok = writeAtomic(blobPath(cache_key),
                              detail::serializeAnimationClip(clip),
                              diagnostic);
  if (ok) {
    touchIndex(cache_key, "animation_clip");
  }
  return ok;
}

std::optional<world::Skeleton> AssetCache::readSkeleton(std::string_view cache_key,
                                                            std::string* diagnostic) {
  if (!enabled() || !validateCacheKey(cache_key, diagnostic)) {
    return std::nullopt;
  }
  auto bytes = readBinaryFile(blobPath(cache_key));
  if (!bytes.has_value()) {
    return std::nullopt;
  }
  return detail::deserializeSkeleton(*bytes, diagnostic);
}

bool AssetCache::writeSkeleton(std::string_view cache_key,
                               const world::Skeleton& skeleton,
                               std::string* diagnostic) {
  if (!enabled() || !validateCacheKey(cache_key, diagnostic)) {
    return false;
  }
  const bool ok = writeAtomic(blobPath(cache_key),
                              detail::serializeSkeleton(skeleton),
                              diagnostic);
  if (ok) {
    touchIndex(cache_key, "skeleton");
  }
  return ok;
}

std::optional<world::Skin> AssetCache::readSkin(std::string_view cache_key,
                                                    std::string* diagnostic) {
  if (!enabled() || !validateCacheKey(cache_key, diagnostic)) {
    return std::nullopt;
  }
  auto bytes = readBinaryFile(blobPath(cache_key));
  if (!bytes.has_value()) {
    return std::nullopt;
  }
  return detail::deserializeSkin(*bytes, diagnostic);
}

bool AssetCache::writeSkin(std::string_view cache_key,
                           const world::Skin& skin,
                           std::string* diagnostic) {
  if (!enabled() || !validateCacheKey(cache_key, diagnostic)) {
    return false;
  }
  const bool ok = writeAtomic(blobPath(cache_key),
                              detail::serializeSkin(skin),
                              diagnostic);
  if (ok) {
    touchIndex(cache_key, "skin");
  }
  return ok;
}

std::optional<world::HumanoidRig> AssetCache::readHumanoidRig(
    std::string_view cache_key,
    std::string* diagnostic) {
  if (!enabled()) {
    return std::nullopt;
  }
  auto bytes = readBinaryFile(blobPath(cache_key));
  if (!bytes.has_value()) {
    return std::nullopt;
  }
  auto rig = detail::deserializeHumanoidRig(*bytes, diagnostic);
  world::HumanoidRetargetDiagnostic validation;
  if (!rig.has_value() ||
      !world::validateHumanoidRig(*rig,
                                  rig->skeleton,
                                  rig->profile,
                                  &validation)) {
    if (diagnostic != nullptr && diagnostic->empty()) {
      *diagnostic = "cached humanoid rig failed semantic validation";
    }
    return std::nullopt;
  }
  return rig;
}

bool AssetCache::writeHumanoidRig(std::string_view cache_key,
                                  const world::HumanoidRig& rig,
                                  std::string* diagnostic) {
  if (!enabled() || cache_key.empty()) {
    return false;
  }
  world::HumanoidRetargetDiagnostic validation;
  if (!world::validateHumanoidRig(rig,
                                  rig.skeleton,
                                  rig.profile,
                                  &validation)) {
    if (diagnostic != nullptr) {
      *diagnostic = "refusing to cache an invalid humanoid rig";
    }
    return false;
  }
  const bool ok = writeAtomic(blobPath(cache_key),
                              detail::serializeHumanoidRig(rig),
                              diagnostic);
  if (ok) {
    touchIndex(cache_key, "humanoid_rig");
  }
  return ok;
}

std::optional<UiDocumentAsset> AssetCache::readUiDocument(
    std::string_view cache_key,
    std::string* diagnostic) {
  if (!enabled() || !validateCacheKey(cache_key, diagnostic)) {
    return std::nullopt;
  }
  auto bytes = readBinaryFile(blobPath(cache_key));
  return bytes.has_value() ? detail::deserializeUiDocument(*bytes, diagnostic)
                           : std::nullopt;
}

bool AssetCache::writeUiDocument(std::string_view cache_key,
                                 const UiDocumentAsset& document,
                                 std::string* diagnostic) {
  if (!enabled() || !validateCacheKey(cache_key, diagnostic)) {
    return false;
  }
  const bool ok = writeAtomic(blobPath(cache_key),
                              detail::serializeUiDocument(document),
                              diagnostic);
  if (ok) {
    touchIndex(cache_key, "ui_document");
  }
  return ok;
}

std::optional<UiThemeAsset> AssetCache::readUiTheme(
    std::string_view cache_key,
    std::string* diagnostic) {
  if (!enabled() || !validateCacheKey(cache_key, diagnostic)) {
    return std::nullopt;
  }
  auto bytes = readBinaryFile(blobPath(cache_key));
  return bytes.has_value() ? detail::deserializeUiTheme(*bytes, diagnostic)
                           : std::nullopt;
}

bool AssetCache::writeUiTheme(std::string_view cache_key,
                              const UiThemeAsset& theme,
                              std::string* diagnostic) {
  if (!enabled() || !validateCacheKey(cache_key, diagnostic)) {
    return false;
  }
  const bool ok = writeAtomic(blobPath(cache_key),
                              detail::serializeUiTheme(theme),
                              diagnostic);
  if (ok) {
    touchIndex(cache_key, "ui_theme");
  }
  return ok;
}

std::optional<FontAsset> AssetCache::readFont(std::string_view cache_key,
                                              std::string* diagnostic) {
  if (!enabled() || !validateCacheKey(cache_key, diagnostic)) {
    return std::nullopt;
  }
  auto bytes = readBinaryFile(blobPath(cache_key));
  return bytes.has_value() ? detail::deserializeFont(*bytes, diagnostic)
                           : std::nullopt;
}

bool AssetCache::writeFont(std::string_view cache_key,
                           const FontAsset& font,
                           std::string* diagnostic) {
  if (!enabled() || !validateCacheKey(cache_key, diagnostic)) {
    return false;
  }
  const bool ok =
      writeAtomic(blobPath(cache_key), detail::serializeFont(font), diagnostic);
  if (ok) {
    touchIndex(cache_key, "font");
  }
  return ok;
}

std::optional<SvgAsset> AssetCache::readSvg(std::string_view cache_key,
                                            std::string* diagnostic) {
  if (!enabled() || !validateCacheKey(cache_key, diagnostic)) {
    return std::nullopt;
  }
  auto bytes = readBinaryFile(blobPath(cache_key));
  return bytes.has_value() ? detail::deserializeSvg(*bytes, diagnostic)
                           : std::nullopt;
}

bool AssetCache::writeSvg(std::string_view cache_key,
                          const SvgAsset& svg,
                          std::string* diagnostic) {
  if (!enabled() || !validateCacheKey(cache_key, diagnostic)) {
    return false;
  }
  const bool ok =
      writeAtomic(blobPath(cache_key), detail::serializeSvg(svg), diagnostic);
  if (ok) {
    touchIndex(cache_key, "svg");
  }
  return ok;
}

std::optional<Json> AssetCache::readPackageManifest(std::string_view manifest_hash,
                                                    std::string* diagnostic) {
  if (!enabled() || !validateCacheKey(manifest_hash, diagnostic)) {
    return std::nullopt;
  }
  std::ifstream stream(packageManifestPath(manifest_hash));
  if (!stream) {
    return std::nullopt;
  }
  try {
    Json out;
    stream >> out;
    if (!out.is_object() ||
        out.value("schema_version", 0u) != kSchemaVersion ||
        out.value("asset_cache_version", std::string{}) != std::string(kAssetCacheVersion)) {
      if (diagnostic != nullptr) {
        *diagnostic = "package cache manifest schema mismatch";
      }
      return std::nullopt;
    }
    return out;
  } catch (const std::exception& e) {
    if (diagnostic != nullptr) {
      *diagnostic = e.what();
    }
    return std::nullopt;
  }
}

bool AssetCache::writePackageManifest(std::string_view manifest_hash,
                                      const Json& manifest,
                                      std::string* diagnostic) {
  if (!enabled() || !validateCacheKey(manifest_hash, diagnostic)) {
    return false;
  }
  Json copy = manifest;
  copy["schema_version"] = kSchemaVersion;
  copy["asset_cache_version"] = std::string(kAssetCacheVersion);
  const bool ok = writeAtomicText(packageManifestPath(manifest_hash),
                                  copy.dump(2),
                                  diagnostic);
  if (ok) {
    touchIndex(manifest_hash, "package");
  }
  return ok;
}

std::filesystem::path AssetCache::blobPath(std::string_view cache_key) const {
  return config_.root / "blobs" / (std::string(cache_key) + ".kasset");
}

std::filesystem::path AssetCache::packageManifestPath(std::string_view manifest_hash) const {
  return config_.root / "packages" / (std::string(manifest_hash) + ".json");
}

void AssetCache::ensureLayout() {
  std::scoped_lock lock(cacheIndexMutex());
  std::error_code ec;
  std::filesystem::create_directories(config_.root / "blobs", ec);
  std::filesystem::create_directories(config_.root / "packages", ec);
  const std::filesystem::path index = config_.root / "index.json";
  if (!std::filesystem::exists(index, ec)) {
    Json root{
        {"schema_version", kSchemaVersion},
        {"asset_cache_version", std::string(kAssetCacheVersion)},
        {"entries", Json::object()},
    };
    std::string diagnostic;
    (void)writeAtomicText(index, root.dump(2), &diagnostic);
  }
}

void AssetCache::touchIndex(std::string_view cache_key, std::string_view kind) {
  if (!enabled() || !isValidCacheKey(cache_key)) {
    return;
  }
  std::scoped_lock lock(cacheIndexMutex());
  const std::filesystem::path index_path = config_.root / "index.json";
  Json root;
  {
    std::ifstream in(index_path);
    if (in) {
      try {
        in >> root;
      } catch (...) {
        root = Json::object();
      }
    }
  }
  if (!root.is_object()) {
    root = Json::object();
  }
  root["schema_version"] = kSchemaVersion;
  root["asset_cache_version"] = std::string(kAssetCacheVersion);
  Json& entries = root["entries"];
  if (!entries.is_object()) {
    entries = Json::object();
  }
  const auto now = std::chrono::system_clock::now().time_since_epoch();
  entries[std::string(cache_key)] = Json{
      {"kind", std::string(kind)},
      {"last_use_unix_ms",
       std::chrono::duration_cast<std::chrono::milliseconds>(now).count()},
  };
  std::string diagnostic;
  (void)writeAtomicText(index_path, root.dump(2), &diagnostic);
}

std::string hashBytes(const std::uint8_t* data, std::size_t size) {
  uint64_t hash = 14695981039346656037ull;
  if (data != nullptr && size > 0u) {
    hash = fnv1aAppend(hash, data, size);
  }
  return hex64(hash);
}

std::string hashString(std::string_view value) {
  return hashBytes(reinterpret_cast<const std::uint8_t*>(value.data()), value.size());
}

std::optional<std::string> hashFile(const std::filesystem::path& path) {
  auto bytes = readBinaryFile(path);
  if (!bytes.has_value()) {
    return std::nullopt;
  }
  return hashBytes(bytes->data(), bytes->size());
}

}  // namespace karma::assets
