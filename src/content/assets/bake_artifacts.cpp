#include "karma/assets.h"

#include "asset_cache_serializers.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstring>
#include <fstream>
#include <limits>
#include <system_error>

namespace karma::assets {
namespace {

constexpr std::array<uint8_t, 8> kMeshMagic{
    'K', 'B', 'M', 'E', 'S', 'H', 0, 0};
constexpr std::array<uint8_t, 8> kRgbaMagic{
    'K', 'B', 'R', 'G', 'B', 'A', '8', 0};
constexpr uint32_t kArtifactVersion = 1u;

void appendU32(std::vector<uint8_t>& bytes, uint32_t value) {
  bytes.push_back(static_cast<uint8_t>(value & 0xffu));
  bytes.push_back(static_cast<uint8_t>((value >> 8u) & 0xffu));
  bytes.push_back(static_cast<uint8_t>((value >> 16u) & 0xffu));
  bytes.push_back(static_cast<uint8_t>((value >> 24u) & 0xffu));
}

bool readU32(const std::vector<uint8_t>& bytes,
             size_t& offset,
             uint32_t& value) {
  if (offset > bytes.size() || bytes.size() - offset < 4u) {
    return false;
  }
  value = static_cast<uint32_t>(bytes[offset]) |
          (static_cast<uint32_t>(bytes[offset + 1u]) << 8u) |
          (static_cast<uint32_t>(bytes[offset + 2u]) << 16u) |
          (static_cast<uint32_t>(bytes[offset + 3u]) << 24u);
  offset += 4u;
  return true;
}

std::optional<std::vector<uint8_t>> readFile(
    const std::filesystem::path& path,
    std::string* diagnostic) {
  std::ifstream stream(path, std::ios::binary);
  if (!stream) {
    if (diagnostic) *diagnostic = "failed to open bake artifact";
    return std::nullopt;
  }
  stream.seekg(0, std::ios::end);
  const std::streamoff size = stream.tellg();
  if (size < 0 ||
      static_cast<uint64_t>(size) >
          static_cast<uint64_t>(std::numeric_limits<size_t>::max())) {
    if (diagnostic) *diagnostic = "invalid bake artifact size";
    return std::nullopt;
  }
  stream.seekg(0, std::ios::beg);
  std::vector<uint8_t> bytes(static_cast<size_t>(size));
  if (!bytes.empty()) {
    stream.read(reinterpret_cast<char*>(bytes.data()), size);
  }
  if (!stream && size != 0) {
    if (diagnostic) *diagnostic = "failed to read bake artifact";
    return std::nullopt;
  }
  return bytes;
}

bool writeAtomic(const std::filesystem::path& path,
                 const std::vector<uint8_t>& bytes,
                 std::string* diagnostic) {
  std::error_code error;
  if (!path.parent_path().empty()) {
    std::filesystem::create_directories(path.parent_path(), error);
    if (error) {
      if (diagnostic) {
        *diagnostic = "failed to create bake artifact directory: " +
                      error.message();
      }
      return false;
    }
  }
  static std::atomic<uint64_t> sequence{0u};
  const auto stamp =
      std::chrono::steady_clock::now().time_since_epoch().count();
  const std::filesystem::path temporary =
      path.parent_path() /
      (path.filename().string() + ".tmp." + std::to_string(stamp) + "." +
       std::to_string(sequence.fetch_add(1u)));
  {
    std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
    if (!stream) {
      if (diagnostic) *diagnostic = "failed to create temporary bake artifact";
      return false;
    }
    if (!bytes.empty()) {
      stream.write(reinterpret_cast<const char*>(bytes.data()),
                   static_cast<std::streamsize>(bytes.size()));
    }
    stream.flush();
    if (!stream) {
      if (diagnostic) *diagnostic = "failed to write temporary bake artifact";
      std::filesystem::remove(temporary, error);
      return false;
    }
  }
  std::filesystem::rename(temporary, path, error);
  if (error) {
    std::filesystem::remove(path, error);
    error.clear();
    std::filesystem::rename(temporary, path, error);
  }
  if (error) {
    if (diagnostic) {
      *diagnostic = "failed to commit bake artifact: " + error.message();
    }
    std::filesystem::remove(temporary, error);
    return false;
  }
  return true;
}

}  // namespace

bool saveBakedMeshArtifact(const std::filesystem::path& path,
                           const world::MeshData& mesh,
                           std::string* diagnostic) {
  const std::vector<uint8_t> payload = detail::serializeMesh(mesh);
  if (payload.empty() ||
      payload.size() > static_cast<size_t>(std::numeric_limits<uint32_t>::max())) {
    if (diagnostic) *diagnostic = "failed to serialize derived mesh";
    return false;
  }
  std::vector<uint8_t> bytes(kMeshMagic.begin(), kMeshMagic.end());
  appendU32(bytes, kArtifactVersion);
  appendU32(bytes, static_cast<uint32_t>(payload.size()));
  bytes.insert(bytes.end(), payload.begin(), payload.end());
  return writeAtomic(path, bytes, diagnostic);
}

std::optional<world::MeshData> loadBakedMeshArtifact(
    const std::filesystem::path& path,
    std::string* diagnostic) {
  const auto bytes = readFile(path, diagnostic);
  if (!bytes || bytes->size() < kMeshMagic.size() + 8u ||
      !std::equal(kMeshMagic.begin(), kMeshMagic.end(), bytes->begin())) {
    if (diagnostic && diagnostic->empty()) {
      *diagnostic = "invalid mesh artifact magic";
    }
    return std::nullopt;
  }
  size_t offset = kMeshMagic.size();
  uint32_t version = 0u;
  uint32_t payload_size = 0u;
  if (!readU32(*bytes, offset, version) || version != kArtifactVersion ||
      !readU32(*bytes, offset, payload_size) ||
      offset > bytes->size() || payload_size != bytes->size() - offset) {
    if (diagnostic) *diagnostic = "invalid mesh artifact header";
    return std::nullopt;
  }
  std::vector<uint8_t> payload(bytes->begin() + static_cast<std::ptrdiff_t>(offset),
                               bytes->end());
  return detail::deserializeMesh(payload, diagnostic);
}

bool saveBakedRgba8Artifact(const std::filesystem::path& path,
                            const Rgba8Image& image,
                            std::string* diagnostic) {
  if (!image.valid() ||
      static_cast<uint64_t>(image.pixels.size()) >
          std::numeric_limits<uint32_t>::max()) {
    if (diagnostic) *diagnostic = "invalid RGBA8 bake artifact";
    return false;
  }
  std::vector<uint8_t> bytes(kRgbaMagic.begin(), kRgbaMagic.end());
  appendU32(bytes, kArtifactVersion);
  appendU32(bytes, static_cast<uint32_t>(image.width));
  appendU32(bytes, static_cast<uint32_t>(image.height));
  appendU32(bytes, static_cast<uint32_t>(image.pixels.size()));
  bytes.insert(bytes.end(), image.pixels.begin(), image.pixels.end());
  return writeAtomic(path, bytes, diagnostic);
}

std::optional<Rgba8Image> loadBakedRgba8Artifact(
    const std::filesystem::path& path,
    std::string* diagnostic) {
  const auto bytes = readFile(path, diagnostic);
  if (!bytes || bytes->size() < kRgbaMagic.size() + 16u ||
      !std::equal(kRgbaMagic.begin(), kRgbaMagic.end(), bytes->begin())) {
    if (diagnostic && diagnostic->empty()) {
      *diagnostic = "invalid RGBA8 artifact magic";
    }
    return std::nullopt;
  }
  size_t offset = kRgbaMagic.size();
  uint32_t version = 0u;
  uint32_t width = 0u;
  uint32_t height = 0u;
  uint32_t payload_size = 0u;
  if (!readU32(*bytes, offset, version) || version != kArtifactVersion ||
      !readU32(*bytes, offset, width) ||
      !readU32(*bytes, offset, height) ||
      !readU32(*bytes, offset, payload_size) || width == 0u || height == 0u ||
      static_cast<uint64_t>(width) * static_cast<uint64_t>(height) * 4u !=
          payload_size ||
      offset > bytes->size() || payload_size != bytes->size() - offset ||
      width > static_cast<uint32_t>(std::numeric_limits<int>::max()) ||
      height > static_cast<uint32_t>(std::numeric_limits<int>::max())) {
    if (diagnostic) *diagnostic = "invalid RGBA8 artifact header";
    return std::nullopt;
  }
  Rgba8Image image{};
  image.width = static_cast<int>(width);
  image.height = static_cast<int>(height);
  image.pixels.assign(bytes->begin() + static_cast<std::ptrdiff_t>(offset),
                      bytes->end());
  return image.valid() ? std::optional<Rgba8Image>(std::move(image))
                       : std::nullopt;
}

}  // namespace karma::assets
