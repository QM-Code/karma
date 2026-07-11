#include "asset_cache_serializers.h"

#include "asset_ui_source_import.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace karma::assets::detail {

namespace {

// UI authoring changed from KUI/KSS source blobs to canonical JSON in pass two.
// Keep this family version independent from the global cache schema so stale UI
// blobs are rejected without invalidating unrelated meshes and textures.
constexpr std::array<char, 8> kMagic{'K', 'A', 'S', 'S', 'E', 'T', '0', '3'};
constexpr uint32_t kKindUiDocument = 10u;
constexpr uint32_t kKindUiTheme = 11u;
constexpr uint32_t kKindFont = 12u;
constexpr uint32_t kKindSvg = 13u;
constexpr uint32_t kChunkSource = 0x55535243u;        // USRC
constexpr uint32_t kChunkDependencies = 0x55444550u;  // UDEP
constexpr uint32_t kChunkFontBytes = 0x55464e54u;     // UFNT

bool fail(std::string* diagnostic, std::string message) {
  if (diagnostic != nullptr) {
    *diagnostic = std::move(message);
  }
  return false;
}

void appendU32(std::vector<uint8_t>& out, uint32_t value) {
  out.push_back(static_cast<uint8_t>(value & 0xffu));
  out.push_back(static_cast<uint8_t>((value >> 8u) & 0xffu));
  out.push_back(static_cast<uint8_t>((value >> 16u) & 0xffu));
  out.push_back(static_cast<uint8_t>((value >> 24u) & 0xffu));
}

void appendU64(std::vector<uint8_t>& out, uint64_t value) {
  for (uint32_t index = 0u; index < 8u; ++index) {
    out.push_back(static_cast<uint8_t>((value >> (index * 8u)) & 0xffu));
  }
}

bool readU32(const std::vector<uint8_t>& bytes, std::size_t& offset, uint32_t& value) {
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

bool readU64(const std::vector<uint8_t>& bytes, std::size_t& offset, uint64_t& value) {
  if (offset > bytes.size() || bytes.size() - offset < 8u) {
    return false;
  }
  value = 0u;
  for (uint32_t index = 0u; index < 8u; ++index) {
    value |= static_cast<uint64_t>(bytes[offset + index]) << (index * 8u);
  }
  offset += 8u;
  return true;
}

void appendChunk(std::vector<uint8_t>& out,
                 uint32_t id,
                 const std::vector<uint8_t>& payload) {
  appendU32(out, id);
  appendU64(out, static_cast<uint64_t>(payload.size()));
  out.insert(out.end(), payload.begin(), payload.end());
}

std::vector<uint8_t> stringBytes(std::string_view value) {
  return std::vector<uint8_t>(value.begin(), value.end());
}

std::vector<uint8_t> serializeDependencies(
    const std::vector<UiAssetDependency>& dependencies) {
  std::vector<uint8_t> payload;
  appendU64(payload, static_cast<uint64_t>(dependencies.size()));
  for (const UiAssetDependency& dependency : dependencies) {
    appendU32(payload, static_cast<uint32_t>(dependency.kind));
    appendU64(payload, static_cast<uint64_t>(dependency.key.size()));
    payload.insert(payload.end(), dependency.key.begin(), dependency.key.end());
  }
  return payload;
}

bool deserializeDependencies(const std::vector<uint8_t>& payload,
                             std::vector<UiAssetDependency>& dependencies) {
  std::size_t offset = 0u;
  uint64_t count = 0u;
  if (!readU64(payload, offset, count) ||
      count > static_cast<uint64_t>((payload.size() - offset) / 12u)) {
    return false;
  }
  dependencies.clear();
  dependencies.reserve(static_cast<std::size_t>(count));
  for (uint64_t index = 0u; index < count; ++index) {
    uint32_t raw_kind = 0u;
    uint64_t key_size = 0u;
    if (!readU32(payload, offset, raw_kind) ||
        raw_kind > static_cast<uint32_t>(UiAssetDependencyKind::Svg) ||
        !readU64(payload, offset, key_size) || key_size == 0u ||
        key_size > static_cast<uint64_t>(payload.size() - offset)) {
      return false;
    }
    std::string key(reinterpret_cast<const char*>(payload.data() + offset),
                    static_cast<std::size_t>(key_size));
    offset += static_cast<std::size_t>(key_size);
    if (!AssetRegistry::isValidAssetKey(key)) {
      return false;
    }
    UiAssetDependency dependency{
        .kind = static_cast<UiAssetDependencyKind>(raw_kind),
        .key = std::move(key),
    };
    if (std::find(dependencies.begin(), dependencies.end(), dependency) !=
        dependencies.end()) {
      return false;
    }
    dependencies.push_back(std::move(dependency));
  }
  return offset == payload.size();
}

std::vector<uint8_t> serializeSourceAsset(
    uint32_t kind,
    std::string_view source,
    const std::vector<UiAssetDependency>* dependencies) {
  std::vector<uint8_t> out;
  out.insert(out.end(), kMagic.begin(), kMagic.end());
  appendU32(out, AssetCache::kSchemaVersion);
  appendU32(out, kind);
  appendChunk(out, kChunkSource, stringBytes(source));
  if (dependencies != nullptr) {
    appendChunk(out, kChunkDependencies, serializeDependencies(*dependencies));
  }
  return out;
}

struct SourceBlob {
  std::string source;
  std::vector<UiAssetDependency> dependencies;
  bool has_dependencies = false;
};

std::optional<SourceBlob> deserializeSourceBlob(const std::vector<uint8_t>& bytes,
                                                uint32_t expected_kind,
                                                bool require_dependencies,
                                                std::string* diagnostic) {
  if (bytes.size() < kMagic.size() + 8u ||
      !std::equal(kMagic.begin(), kMagic.end(), bytes.begin())) {
    fail(diagnostic, "cache blob magic mismatch");
    return std::nullopt;
  }
  std::size_t offset = kMagic.size();
  uint32_t schema = 0u;
  uint32_t kind = 0u;
  if (!readU32(bytes, offset, schema) || !readU32(bytes, offset, kind) ||
      schema != AssetCache::kSchemaVersion || kind != expected_kind) {
    fail(diagnostic, "cache blob version or kind mismatch");
    return std::nullopt;
  }
  SourceBlob result;
  bool has_source = false;
  while (offset < bytes.size()) {
    uint32_t chunk_id = 0u;
    uint64_t chunk_size = 0u;
    if (!readU32(bytes, offset, chunk_id) || !readU64(bytes, offset, chunk_size) ||
        chunk_size > static_cast<uint64_t>(bytes.size() - offset)) {
      fail(diagnostic, "cache UI asset chunk is truncated");
      return std::nullopt;
    }
    const std::size_t size = static_cast<std::size_t>(chunk_size);
    if (chunk_id == kChunkSource) {
      if (has_source) {
        fail(diagnostic, "cache UI asset has duplicate source chunks");
        return std::nullopt;
      }
      result.source.assign(reinterpret_cast<const char*>(bytes.data() + offset), size);
      has_source = true;
    } else if (chunk_id == kChunkDependencies) {
      if (result.has_dependencies) {
        fail(diagnostic, "cache UI asset has duplicate dependency chunks");
        return std::nullopt;
      }
      const std::vector<uint8_t> payload(
          bytes.begin() + static_cast<std::ptrdiff_t>(offset),
          bytes.begin() + static_cast<std::ptrdiff_t>(offset + size));
      if (!deserializeDependencies(payload, result.dependencies)) {
        fail(diagnostic, "cache UI asset dependency payload failed validation");
        return std::nullopt;
      }
      result.has_dependencies = true;
    }
    offset += size;
  }
  if (!has_source || (require_dependencies && !result.has_dependencies)) {
    fail(diagnostic, "cache UI asset is missing a required chunk");
    return std::nullopt;
  }
  if (!require_dependencies && result.has_dependencies) {
    fail(diagnostic, "cache UI asset contains an unexpected dependency chunk");
    return std::nullopt;
  }
  return result;
}

}  // namespace

std::vector<uint8_t> serializeUiDocument(const UiDocumentAsset& document) {
  return serializeSourceAsset(kKindUiDocument,
                              document.canonical_json_utf8,
                              &document.dependencies);
}

std::optional<UiDocumentAsset> deserializeUiDocument(
    const std::vector<uint8_t>& bytes,
    std::string* diagnostic) {
  auto blob = deserializeSourceBlob(bytes, kKindUiDocument, true, diagnostic);
  if (!blob.has_value()) {
    return std::nullopt;
  }
  std::string canonical_json;
  std::vector<UiAssetDependency> parsed_dependencies;
  if (!validateUiDocumentJson(blob->source,
                              canonical_json,
                              parsed_dependencies,
                              diagnostic) ||
      canonical_json != blob->source ||
      parsed_dependencies != blob->dependencies) {
    if (diagnostic != nullptr && diagnostic->empty()) {
      *diagnostic = "cache UI document is noncanonical or its dependencies do not match";
    }
    return std::nullopt;
  }
  UiDocumentAsset document{
      .canonical_json_utf8 = std::move(blob->source),
      .dependencies = std::move(blob->dependencies),
  };
  document.content_hash = hashString(document.canonical_json_utf8);
  return document;
}

std::vector<uint8_t> serializeUiTheme(const UiThemeAsset& theme) {
  return serializeSourceAsset(kKindUiTheme,
                              theme.canonical_json_utf8,
                              &theme.dependencies);
}

std::optional<UiThemeAsset> deserializeUiTheme(
    const std::vector<uint8_t>& bytes,
    std::string* diagnostic) {
  auto blob = deserializeSourceBlob(bytes, kKindUiTheme, true, diagnostic);
  if (!blob.has_value()) {
    return std::nullopt;
  }
  std::string canonical_json;
  std::vector<UiAssetDependency> parsed_dependencies;
  if (!validateUiThemeJson(blob->source,
                           canonical_json,
                           parsed_dependencies,
                           diagnostic) ||
      canonical_json != blob->source ||
      parsed_dependencies != blob->dependencies) {
    if (diagnostic != nullptr && diagnostic->empty()) {
      *diagnostic = "cache UI theme is noncanonical or its dependencies do not match";
    }
    return std::nullopt;
  }
  UiThemeAsset theme{
      .canonical_json_utf8 = std::move(blob->source),
      .dependencies = std::move(blob->dependencies),
  };
  theme.content_hash = hashString(theme.canonical_json_utf8);
  return theme;
}

std::vector<uint8_t> serializeFont(const FontAsset& font) {
  std::vector<uint8_t> out;
  out.insert(out.end(), kMagic.begin(), kMagic.end());
  appendU32(out, AssetCache::kSchemaVersion);
  appendU32(out, kKindFont);
  appendChunk(out, kChunkFontBytes, font.bytes);
  return out;
}

std::optional<FontAsset> deserializeFont(const std::vector<uint8_t>& bytes,
                                         std::string* diagnostic) {
  if (bytes.size() < kMagic.size() + 8u ||
      !std::equal(kMagic.begin(), kMagic.end(), bytes.begin())) {
    fail(diagnostic, "cache blob magic mismatch");
    return std::nullopt;
  }
  std::size_t offset = kMagic.size();
  uint32_t schema = 0u;
  uint32_t kind = 0u;
  if (!readU32(bytes, offset, schema) || !readU32(bytes, offset, kind) ||
      schema != AssetCache::kSchemaVersion || kind != kKindFont) {
    fail(diagnostic, "cache blob version or kind mismatch");
    return std::nullopt;
  }
  FontAsset font;
  bool has_bytes = false;
  while (offset < bytes.size()) {
    uint32_t chunk_id = 0u;
    uint64_t chunk_size = 0u;
    if (!readU32(bytes, offset, chunk_id) || !readU64(bytes, offset, chunk_size) ||
        chunk_size > static_cast<uint64_t>(bytes.size() - offset)) {
      fail(diagnostic, "cache font chunk is truncated");
      return std::nullopt;
    }
    const std::size_t size = static_cast<std::size_t>(chunk_size);
    if (chunk_id == kChunkFontBytes) {
      if (has_bytes) {
        fail(diagnostic, "cache font has duplicate byte chunks");
        return std::nullopt;
      }
      font.bytes.assign(bytes.begin() + static_cast<std::ptrdiff_t>(offset),
                        bytes.begin() + static_cast<std::ptrdiff_t>(offset + size));
      has_bytes = true;
    }
    offset += size;
  }
  if (!has_bytes || !validateFontBytes(font.bytes, diagnostic)) {
    if (!has_bytes) {
      fail(diagnostic, "cache font is missing its byte payload");
    }
    return std::nullopt;
  }
  font.content_hash = hashBytes(font.bytes.data(), font.bytes.size());
  return font;
}

std::vector<uint8_t> serializeSvg(const SvgAsset& svg) {
  return serializeSourceAsset(kKindSvg, svg.source_utf8, nullptr);
}

std::optional<SvgAsset> deserializeSvg(const std::vector<uint8_t>& bytes,
                                       std::string* diagnostic) {
  auto blob = deserializeSourceBlob(bytes, kKindSvg, false, diagnostic);
  if (!blob.has_value() || !validateSvgSource(blob->source, diagnostic)) {
    return std::nullopt;
  }
  SvgAsset svg{.source_utf8 = std::move(blob->source)};
  svg.content_hash = hashString(svg.source_utf8);
  return svg;
}

}  // namespace karma::assets::detail
