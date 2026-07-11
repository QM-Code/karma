#include "karma/foliage.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <new>
#include <stdexcept>
#include <system_error>
#include <tuple>

#if defined(_WIN32)
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace karma::foliage {
namespace {

constexpr std::array<uint8_t, 8> kMagic{
    'K', 'F', 'O', 'L', 'I', 'A', 'G', 'E'};
constexpr uint32_t kHeaderSize = 48u;
constexpr uint32_t kDirectoryEntrySize = 52u;
constexpr uint32_t kMaxChunkCount = 1000000u;
constexpr uint64_t kMaxInstanceCount = kMaxAuthoredFoliageInstances;

void setError(std::string* error, std::string message);

std::filesystem::path temporaryPath(const std::filesystem::path& path) {
  static std::atomic<uint64_t> sequence{0u};
  const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
  return path.parent_path() /
         (path.filename().string() + ".tmp." + std::to_string(stamp) + "." +
          std::to_string(sequence.fetch_add(1u)));
}

void removeTemporary(const std::filesystem::path& path) {
  std::error_code ignored;
  std::filesystem::remove(path, ignored);
}

bool commitTemporary(const std::filesystem::path& temporary,
                     const std::filesystem::path& destination,
                     std::string* error) {
  std::error_code ec;
#if defined(_WIN32)
  if (!MoveFileExW(temporary.c_str(),
                   destination.c_str(),
                   MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
    ec = std::error_code(static_cast<int>(GetLastError()),
                         std::system_category());
  }
#else
  std::filesystem::rename(temporary, destination, ec);
#endif
  if (!ec) return true;
  removeTemporary(temporary);
  setError(error, "failed to atomically replace foliage file: " + ec.message());
  return false;
}

void setError(std::string* error, std::string message) {
  if (error != nullptr) {
    *error = std::move(message);
  }
}

bool checkedAdd(uint64_t a, uint64_t b, uint64_t& out) {
  if (a > std::numeric_limits<uint64_t>::max() - b) {
    return false;
  }
  out = a + b;
  return true;
}

bool checkedMul(uint64_t a, uint64_t b, uint64_t& out) {
  if (a != 0u && b > std::numeric_limits<uint64_t>::max() / a) {
    return false;
  }
  out = a * b;
  return true;
}

uint32_t readU32(const uint8_t* bytes) {
  return static_cast<uint32_t>(bytes[0]) |
         (static_cast<uint32_t>(bytes[1]) << 8u) |
         (static_cast<uint32_t>(bytes[2]) << 16u) |
         (static_cast<uint32_t>(bytes[3]) << 24u);
}

uint64_t readU64(const uint8_t* bytes) {
  return static_cast<uint64_t>(readU32(bytes)) |
         (static_cast<uint64_t>(readU32(bytes + 4u)) << 32u);
}

float readF32(const uint8_t* bytes) {
  return std::bit_cast<float>(readU32(bytes));
}

int32_t readI32(const uint8_t* bytes) {
  return std::bit_cast<int32_t>(readU32(bytes));
}

void writeU32(std::ostream& stream, uint32_t value) {
  const std::array<uint8_t, 4> bytes{
      static_cast<uint8_t>(value & 0xffu),
      static_cast<uint8_t>((value >> 8u) & 0xffu),
      static_cast<uint8_t>((value >> 16u) & 0xffu),
      static_cast<uint8_t>((value >> 24u) & 0xffu),
  };
  stream.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
}

void writeU64(std::ostream& stream, uint64_t value) {
  writeU32(stream, static_cast<uint32_t>(value & 0xffffffffull));
  writeU32(stream, static_cast<uint32_t>(value >> 32u));
}

void writeF32(std::ostream& stream, float value) {
  if (value == 0.0f) {
    value = 0.0f;
  }
  writeU32(stream, std::bit_cast<uint32_t>(value));
}

void writeI32(std::ostream& stream, int32_t value) {
  writeU32(stream, std::bit_cast<uint32_t>(value));
}

bool readExact(std::istream& stream, void* output, std::size_t size) {
  if (size == 0u) {
    return true;
  }
  stream.read(static_cast<char*>(output), static_cast<std::streamsize>(size));
  return stream.good() ||
         stream.gcount() == static_cast<std::streamsize>(size);
}

bool validChunkSize(float chunk_size) {
  return std::isfinite(chunk_size) && chunk_size > 0.0f;
}

bool validInstance(const FoliageInstance& instance) {
  if (!math::isFinite(instance.position) ||
      !std::isfinite(instance.yaw_radians) ||
      !math::isFinite(instance.scale)) {
    return false;
  }
  for (const float value : instance.params) {
    if (!std::isfinite(value)) {
      return false;
    }
  }
  return true;
}

auto instanceSortKey(const FoliageInstance& instance) {
  return std::tuple{
      instance.position.x,
      instance.position.z,
      instance.position.y,
      instance.yaw_radians,
      instance.scale.x,
      instance.scale.y,
      instance.scale.z,
      instance.params[0],
      instance.params[1],
      instance.params[2],
      instance.params[3],
  };
}

bool coordMatches(const FoliageInstance& instance,
                  FoliageChunkCoord coord,
                  float chunk_size) {
  return foliageChunkCoordForPosition(
             instance.position.x, instance.position.z, chunk_size) == coord;
}

struct CanonicalChunk {
  FoliageChunkCoord coord{};
  math::Vec3 bounds_min{};
  math::Vec3 bounds_max{};
  std::vector<FoliageInstance> instances;
  uint64_t data_offset = 0u;
};

std::optional<std::vector<CanonicalChunk>> canonicalChunks(
    const FoliageDocument& document,
    std::string* error) {
  if (!validChunkSize(document.chunk_size)) {
    setError(error, "foliage chunk size must be finite and positive");
    return std::nullopt;
  }

  std::map<FoliageChunkCoord, std::vector<FoliageInstance>> grouped;
  uint64_t total = 0u;
  for (const FoliageChunk& chunk : document.chunks) {
    auto& instances = grouped[chunk.coord];
    if (chunk.instances.size() >
        std::numeric_limits<std::size_t>::max() - instances.size()) {
      setError(error, "foliage chunk instance count overflows host size");
      return std::nullopt;
    }
    instances.insert(instances.end(), chunk.instances.begin(), chunk.instances.end());
  }

  std::vector<CanonicalChunk> result;
  result.reserve(grouped.size());
  for (auto& [coord, instances] : grouped) {
    if (instances.empty()) {
      continue;
    }
    if (instances.size() > std::numeric_limits<uint32_t>::max()) {
      setError(error, "one foliage chunk exceeds the v1 instance limit");
      return std::nullopt;
    }
    if (instances.size() > kMaxInstanceCount ||
        total > kMaxInstanceCount - instances.size()) {
      setError(error, "foliage document exceeds the supported instance limit");
      return std::nullopt;
    }
    total += instances.size();

    for (const FoliageInstance& instance : instances) {
      if (!validInstance(instance)) {
        setError(error, "foliage document contains a non-finite instance");
        return std::nullopt;
      }
      if (!coordMatches(instance, coord, document.chunk_size)) {
        setError(error, "foliage instance is stored under the wrong chunk coordinate");
        return std::nullopt;
      }
    }
    std::sort(instances.begin(), instances.end(), [](const auto& a, const auto& b) {
      return instanceSortKey(a) < instanceSortKey(b);
    });

    CanonicalChunk canonical{};
    canonical.coord = coord;
    canonical.instances = std::move(instances);
    canonical.bounds_min = canonical.instances.front().position;
    canonical.bounds_max = canonical.instances.front().position;
    for (const FoliageInstance& instance : canonical.instances) {
      canonical.bounds_min.x = std::min(canonical.bounds_min.x, instance.position.x);
      canonical.bounds_min.y = std::min(canonical.bounds_min.y, instance.position.y);
      canonical.bounds_min.z = std::min(canonical.bounds_min.z, instance.position.z);
      canonical.bounds_max.x = std::max(canonical.bounds_max.x, instance.position.x);
      canonical.bounds_max.y = std::max(canonical.bounds_max.y, instance.position.y);
      canonical.bounds_max.z = std::max(canonical.bounds_max.z, instance.position.z);
    }
    result.push_back(std::move(canonical));
  }
  if (result.size() > kMaxChunkCount) {
    setError(error, "foliage document exceeds the supported chunk limit");
    return std::nullopt;
  }
  return result;
}

bool validBounds(const FoliageChunkInfo& info) {
  return math::isFinite(info.bounds_min) && math::isFinite(info.bounds_max) &&
         info.bounds_min.x <= info.bounds_max.x &&
         info.bounds_min.y <= info.bounds_max.y &&
         info.bounds_min.z <= info.bounds_max.z;
}

}  // namespace

std::size_t FoliageDocument::instanceCount() const {
  std::size_t count = 0u;
  for (const FoliageChunk& chunk : chunks) {
    if (chunk.instances.size() > std::numeric_limits<std::size_t>::max() - count) {
      return std::numeric_limits<std::size_t>::max();
    }
    count += chunk.instances.size();
  }
  return count;
}

const FoliageChunkInfo* FoliageFileIndex::find(FoliageChunkCoord coord) const {
  const auto it = std::lower_bound(
      chunks.begin(), chunks.end(), coord,
      [](const FoliageChunkInfo& info, FoliageChunkCoord wanted) {
        return info.coord < wanted;
      });
  return it != chunks.end() && it->coord == coord ? &*it : nullptr;
}

FoliageChunkCoord foliageChunkCoordForPosition(float x,
                                               float z,
                                               float chunk_size) {
  if (!std::isfinite(x) || !std::isfinite(z) || !validChunkSize(chunk_size)) {
    return {};
  }
  const double chunk_x = std::floor(static_cast<double>(x) / chunk_size);
  const double chunk_z = std::floor(static_cast<double>(z) / chunk_size);
  return {
      static_cast<int32_t>(std::clamp(
          chunk_x,
          static_cast<double>(std::numeric_limits<int32_t>::min()),
          static_cast<double>(std::numeric_limits<int32_t>::max()))),
      static_cast<int32_t>(std::clamp(
          chunk_z,
          static_cast<double>(std::numeric_limits<int32_t>::min()),
          static_cast<double>(std::numeric_limits<int32_t>::max()))),
  };
}

bool writeFoliageFile(const std::filesystem::path& path,
                      const FoliageDocument& document,
                      std::string* error) {
  if (error != nullptr) {
    error->clear();
  }
  auto canonical = canonicalChunks(document, error);
  if (!canonical.has_value()) {
    return false;
  }

  uint64_t directory_bytes = 0u;
  if (!checkedMul(canonical->size(), kDirectoryEntrySize, directory_bytes)) {
    setError(error, "foliage directory size overflows the v1 format");
    return false;
  }
  uint64_t next_offset = 0u;
  if (!checkedAdd(kHeaderSize, directory_bytes, next_offset)) {
    setError(error, "foliage data offset overflows the v1 format");
    return false;
  }
  uint64_t total_instances = 0u;
  for (CanonicalChunk& chunk : *canonical) {
    chunk.data_offset = next_offset;
    uint64_t data_bytes = 0u;
    if (!checkedMul(chunk.instances.size(), kFoliageInstanceRecordSize, data_bytes) ||
        !checkedAdd(next_offset, data_bytes, next_offset)) {
      setError(error, "foliage payload size overflows the v1 format");
      return false;
    }
    total_instances += chunk.instances.size();
  }

  std::error_code ec;
  if (!path.parent_path().empty()) {
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) {
      setError(error, "failed to create foliage output directory: " + ec.message());
      return false;
    }
  }
  const std::filesystem::path temporary = temporaryPath(path);
  std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
  if (!stream) {
    setError(error, "failed to open foliage file for writing");
    return false;
  }

  stream.write(reinterpret_cast<const char*>(kMagic.data()), kMagic.size());
  writeU32(stream, kFoliageFileVersion);
  writeU32(stream, kHeaderSize);
  writeF32(stream, document.chunk_size);
  writeU32(stream, static_cast<uint32_t>(canonical->size()));
  writeU64(stream, total_instances);
  writeU64(stream, kHeaderSize);
  writeU32(stream, kFoliageInstanceRecordSize);
  writeU32(stream, kDirectoryEntrySize);

  for (const CanonicalChunk& chunk : *canonical) {
    writeI32(stream, chunk.coord.x);
    writeI32(stream, chunk.coord.z);
    writeF32(stream, chunk.bounds_min.x);
    writeF32(stream, chunk.bounds_min.y);
    writeF32(stream, chunk.bounds_min.z);
    writeF32(stream, chunk.bounds_max.x);
    writeF32(stream, chunk.bounds_max.y);
    writeF32(stream, chunk.bounds_max.z);
    writeU32(stream, static_cast<uint32_t>(chunk.instances.size()));
    writeU64(stream, chunk.data_offset);
    writeU64(stream,
             static_cast<uint64_t>(chunk.instances.size()) *
                 kFoliageInstanceRecordSize);
  }
  for (const CanonicalChunk& chunk : *canonical) {
    for (const FoliageInstance& instance : chunk.instances) {
      writeF32(stream, instance.position.x);
      writeF32(stream, instance.position.y);
      writeF32(stream, instance.position.z);
      writeF32(stream, instance.yaw_radians);
      writeF32(stream, instance.scale.x);
      writeF32(stream, instance.scale.y);
      writeF32(stream, instance.scale.z);
      for (const float value : instance.params) {
        writeF32(stream, value);
      }
    }
  }
  stream.flush();
  if (!stream) {
    stream.close();
    removeTemporary(temporary);
    setError(error, "failed while writing foliage file");
    return false;
  }
  stream.close();
  if (!stream) {
    removeTemporary(temporary);
    setError(error, "failed while closing foliage file");
    return false;
  }
  return commitTemporary(temporary, path, error);
}

std::optional<FoliageFileIndex> readFoliageFileIndex(
    const std::filesystem::path& path,
    std::string* error) {
  if (error != nullptr) {
    error->clear();
  }
  std::error_code ec;
  const uint64_t file_size = std::filesystem::file_size(path, ec);
  if (ec) {
    setError(error, "failed to inspect foliage file: " + ec.message());
    return std::nullopt;
  }
  if (file_size < kHeaderSize) {
    setError(error, "foliage file is truncated before its header");
    return std::nullopt;
  }
  std::ifstream stream(path, std::ios::binary);
  if (!stream) {
    setError(error, "failed to open foliage file");
    return std::nullopt;
  }
  std::array<uint8_t, kHeaderSize> header{};
  if (!readExact(stream, header.data(), header.size())) {
    setError(error, "foliage file is truncated while reading its header");
    return std::nullopt;
  }
  if (!std::equal(kMagic.begin(), kMagic.end(), header.begin())) {
    setError(error, "foliage file has an invalid magic value");
    return std::nullopt;
  }
  const uint32_t version = readU32(header.data() + 8u);
  if (version != kFoliageFileVersion) {
    setError(error, "unsupported foliage file version " + std::to_string(version));
    return std::nullopt;
  }
  if (readU32(header.data() + 12u) != kHeaderSize ||
      readU32(header.data() + 40u) != kFoliageInstanceRecordSize ||
      readU32(header.data() + 44u) != kDirectoryEntrySize) {
    setError(error, "foliage file uses an incompatible v1 record layout");
    return std::nullopt;
  }
  const float chunk_size = readF32(header.data() + 16u);
  const uint32_t chunk_count = readU32(header.data() + 20u);
  const uint64_t instance_count = readU64(header.data() + 24u);
  const uint64_t directory_offset = readU64(header.data() + 32u);
  if (!validChunkSize(chunk_size)) {
    setError(error, "foliage file has an invalid chunk size");
    return std::nullopt;
  }
  if (chunk_count > kMaxChunkCount || instance_count > kMaxInstanceCount) {
    setError(error, "foliage file exceeds supported v1 count limits");
    return std::nullopt;
  }
  if (static_cast<uint64_t>(chunk_count) > instance_count) {
    setError(error,
             "foliage file chunk count exceeds its total instance count");
    return std::nullopt;
  }
  if (directory_offset != kHeaderSize) {
    setError(error, "foliage file has an invalid directory offset");
    return std::nullopt;
  }
  uint64_t directory_bytes = 0u;
  uint64_t directory_end = 0u;
  if (!checkedMul(chunk_count, kDirectoryEntrySize, directory_bytes) ||
      !checkedAdd(directory_offset, directory_bytes, directory_end) ||
      directory_end > file_size) {
    setError(error, "foliage file has a truncated or overflowing directory");
    return std::nullopt;
  }
  try {
    std::vector<uint8_t> directory(static_cast<std::size_t>(directory_bytes));
    if (!readExact(stream, directory.data(), directory.size())) {
      setError(error, "foliage file is truncated while reading its directory");
      return std::nullopt;
    }

    FoliageFileIndex index{};
    index.chunk_size = chunk_size;
    index.instance_count = instance_count;
    index.file_size = file_size;
    index.chunks.reserve(chunk_count);
    uint64_t counted_instances = 0u;
    uint64_t expected_data_offset = directory_end;
    for (uint32_t i = 0u; i < chunk_count; ++i) {
      const uint8_t* entry =
          directory.data() +
          static_cast<std::size_t>(i) * kDirectoryEntrySize;
      FoliageChunkInfo info{};
      info.coord = {readI32(entry), readI32(entry + 4u)};
      info.bounds_min = {readF32(entry + 8u),
                         readF32(entry + 12u),
                         readF32(entry + 16u)};
      info.bounds_max = {readF32(entry + 20u),
                         readF32(entry + 24u),
                         readF32(entry + 28u)};
      info.instance_count = readU32(entry + 32u);
      info.data_offset = readU64(entry + 36u);
      const uint64_t data_bytes = readU64(entry + 44u);
      uint64_t expected_bytes = 0u;
      uint64_t data_end = 0u;
      if (!validBounds(info) || info.instance_count == 0u) {
        setError(
            error,
            "foliage directory contains invalid bounds or an empty chunk");
        return std::nullopt;
      }
      if (i != 0u && !(index.chunks.back().coord < info.coord)) {
        setError(
            error,
            "foliage directory is not strictly sorted by chunk coordinate");
        return std::nullopt;
      }
      if (!checkedMul(info.instance_count,
                      kFoliageInstanceRecordSize,
                      expected_bytes) ||
          data_bytes != expected_bytes ||
          info.data_offset != expected_data_offset ||
          !checkedAdd(info.data_offset, data_bytes, data_end) ||
          data_end > file_size) {
        setError(
            error,
            "foliage directory contains an invalid chunk payload range");
        return std::nullopt;
      }
      expected_data_offset = data_end;
      if (counted_instances > kMaxInstanceCount - info.instance_count) {
        setError(error, "foliage directory instance total overflows");
        return std::nullopt;
      }
      counted_instances += info.instance_count;
      index.chunks.push_back(info);
    }
    if (counted_instances != instance_count ||
        expected_data_offset != file_size) {
      setError(
          error,
          "foliage header counts or final file size do not match its directory");
      return std::nullopt;
    }
    return index;
  } catch (const std::bad_alloc&) {
    setError(error, "insufficient memory while reading foliage file index");
    return std::nullopt;
  } catch (const std::length_error&) {
    setError(error, "foliage file index exceeds host container limits");
    return std::nullopt;
  }
}

std::optional<FoliageChunk> readFoliageChunk(
    const std::filesystem::path& path,
    const FoliageFileIndex& index,
    FoliageChunkCoord coord,
    std::string* error) {
  return readFoliageChunk(path,
                          index,
                          coord,
                          std::numeric_limits<uint32_t>::max(),
                          error);
}

std::optional<FoliageChunk> readFoliageChunk(
    const std::filesystem::path& path,
    const FoliageFileIndex& index,
    FoliageChunkCoord coord,
    uint32_t max_instances,
    std::string* error) {
  if (error != nullptr) {
    error->clear();
  }
  const FoliageChunkInfo* info = index.find(coord);
  if (info == nullptr) {
    setError(error, "requested foliage chunk is not present in the file index");
    return std::nullopt;
  }
  if (!validChunkSize(index.chunk_size) || !validBounds(*info) ||
      info->instance_count == 0u || info->data_offset > index.file_size) {
    setError(error, "foliage file index contains an invalid chunk entry");
    return std::nullopt;
  }
  std::error_code ec;
  if (std::filesystem::file_size(path, ec) != index.file_size || ec) {
    setError(error, "foliage file changed after its directory was indexed");
    return std::nullopt;
  }
  const uint32_t read_count = std::min(info->instance_count, max_instances);
  uint64_t indexed_byte_count = 0u;
  uint64_t byte_count_u64 = 0u;
  if (!checkedMul(info->instance_count,
                  kFoliageInstanceRecordSize,
                  indexed_byte_count) ||
      indexed_byte_count > index.file_size - info->data_offset ||
      !checkedMul(read_count,
                  kFoliageInstanceRecordSize,
                  byte_count_u64) ||
      byte_count_u64 > std::numeric_limits<std::size_t>::max()) {
    setError(error, "foliage chunk payload is too large for this host");
    return std::nullopt;
  }
  std::ifstream stream(path, std::ios::binary);
  if (!stream) {
    setError(error, "failed to open foliage file for chunk streaming");
    return std::nullopt;
  }
  stream.seekg(static_cast<std::streamoff>(info->data_offset), std::ios::beg);
  if (!stream) {
    setError(error, "failed to seek to foliage chunk payload");
    return std::nullopt;
  }
  std::vector<uint8_t> bytes(static_cast<std::size_t>(byte_count_u64));
  if (!readExact(stream, bytes.data(), bytes.size())) {
    setError(error, "foliage chunk payload is truncated");
    return std::nullopt;
  }

  FoliageChunk chunk{};
  chunk.coord = coord;
  chunk.instances.reserve(read_count);
  for (uint32_t i = 0u; i < read_count; ++i) {
    const uint8_t* record = bytes.data() +
                            static_cast<std::size_t>(i) *
                                kFoliageInstanceRecordSize;
    FoliageInstance instance{};
    instance.position = {readF32(record),
                         readF32(record + 4u),
                         readF32(record + 8u)};
    instance.yaw_radians = readF32(record + 12u);
    instance.scale = {readF32(record + 16u),
                      readF32(record + 20u),
                      readF32(record + 24u)};
    for (std::size_t param = 0u; param < instance.params.size(); ++param) {
      instance.params[param] = readF32(record + 28u + param * 4u);
    }
    if (!validInstance(instance) ||
        !coordMatches(instance, coord, index.chunk_size) ||
        instance.position.x < info->bounds_min.x ||
        instance.position.y < info->bounds_min.y ||
        instance.position.z < info->bounds_min.z ||
        instance.position.x > info->bounds_max.x ||
        instance.position.y > info->bounds_max.y ||
        instance.position.z > info->bounds_max.z) {
      setError(error, "foliage chunk contains an invalid or misindexed instance");
      return std::nullopt;
    }
    chunk.instances.push_back(instance);
  }
  return chunk;
}

std::optional<FoliageDocument> readFoliageFile(
    const std::filesystem::path& path,
    std::string* error) {
  auto index = readFoliageFileIndex(path, error);
  if (!index.has_value()) {
    return std::nullopt;
  }
  FoliageDocument document{};
  document.chunk_size = index->chunk_size;
  document.chunks.reserve(index->chunks.size());
  for (const FoliageChunkInfo& info : index->chunks) {
    auto chunk = readFoliageChunk(path, *index, info.coord, error);
    if (!chunk.has_value()) {
      return std::nullopt;
    }
    document.chunks.push_back(std::move(*chunk));
  }
  return document;
}

}  // namespace karma::foliage
