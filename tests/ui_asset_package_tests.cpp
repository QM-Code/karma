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
#include <vector>

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
  std::ofstream stream(path, std::ios::binary | std::ios::trunc);
  stream.write(text.data(), static_cast<std::streamsize>(text.size()));
  KARMA_REQUIRE(static_cast<bool>(stream));
}

void writeBinary(const std::filesystem::path& path,
                 const std::vector<uint8_t>& bytes) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream stream(path, std::ios::binary | std::ios::trunc);
  stream.write(reinterpret_cast<const char*>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
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

void writeU32(std::vector<uint8_t>& bytes, std::size_t offset, uint32_t value) {
  KARMA_REQUIRE(offset + 4u <= bytes.size());
  for (uint32_t index = 0u; index < 4u; ++index) {
    bytes[offset + index] =
        static_cast<uint8_t>((value >> (index * 8u)) & 0xffu);
  }
}

std::vector<uint8_t> minimalFontBytes() {
  std::vector<uint8_t> bytes(28u, 0u);
  bytes[0] = 0x00u;
  bytes[1] = 0x01u;
  bytes[2] = 0x00u;
  bytes[3] = 0x00u;
  bytes[4] = 0x00u;
  bytes[5] = 0x01u;
  bytes[12] = 'n';
  bytes[13] = 'a';
  bytes[14] = 'm';
  bytes[15] = 'e';
  return bytes;
}

constexpr std::string_view kDocument = R"({
  // Development authoring accepts Karma's deterministic JSON5 profile.
  format: 'karma.ui.document',
  version: 2,
  themes: [{asset: 'ui/theme'},],
  root: {
    type: 'panel',
    id: 'main',
    children: [{
      type: 'svg',
      props: {src: {asset: 'ui/logo', kind: 'svg'}},
    }],
  },
})";

constexpr std::string_view kDocumentCanonical =
    R"({"format":"karma.ui.document","root":{"children":[{"props":{"src":{"asset":"ui/logo","kind":"svg"}},"type":"svg"}],"id":"main","type":"panel"},"themes":[{"asset":"ui/theme"}],"version":2})";

constexpr std::string_view kTheme = R"({
  /* Theme assets are canonicalized before registry storage. */
  format: 'karma.ui.theme',
  version: 2,
  fonts: {
    Test: {src: {asset: 'ui/font'}},
  },
  styles: {
    body: {appearance: {text: {color: '#ffffff'}}},
  },
})";

constexpr std::string_view kThemeCanonical =
    R"({"fonts":{"Test":{"src":{"asset":"ui/font"}}},"format":"karma.ui.theme","styles":{"body":{"appearance":{"text":{"color":"#ffffff"}}}},"version":2})";

constexpr std::string_view kSvg = R"(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 1 1">
  <rect width="1" height="1"/>
</svg>)";

void writeValidPackageSources(const std::filesystem::path& dir) {
  writeText(dir / "main.kui.json5", kDocument);
  writeText(dir / "theme.kstyle.json5", kTheme);
  writeText(dir / "logo.svg", kSvg);
  writeBinary(dir / "font.ttf", minimalFontBytes());
  writeText(dir / "assets.package.json", R"({
    "version": 1,
    "assets": [
      { "type": "ui_document", "key": "ui/main", "path": "main.kui.json5" },
      { "type": "ui_theme", "key": "ui/theme", "path": "theme.kstyle.json5" },
      { "type": "svg", "key": "ui/logo", "path": "logo.svg" },
      { "type": "font", "key": "ui/font", "path": "font.ttf" }
    ]
  })");
}

void requireValidRegistry(const karma::assets::AssetRegistry& assets) {
  const auto* document = assets.findUiDocumentAsset("ui/main");
  const auto* theme = assets.findUiThemeAsset("ui/theme");
  const auto* font = assets.findFontAsset("ui/font");
  const auto* svg = assets.findSvgAsset("ui/logo");
  KARMA_REQUIRE(document != nullptr);
  KARMA_REQUIRE(theme != nullptr);
  KARMA_REQUIRE(font != nullptr);
  KARMA_REQUIRE(svg != nullptr);
  KARMA_REQUIRE(document->canonical_json_utf8 == kDocumentCanonical);
  KARMA_REQUIRE(document->dependencies.size() == 2u);
  const karma::assets::UiAssetDependency expected_theme{
      .kind = karma::assets::UiAssetDependencyKind::UiTheme,
      .key = "ui/theme",
  };
  const karma::assets::UiAssetDependency expected_svg{
      .kind = karma::assets::UiAssetDependencyKind::Svg,
      .key = "ui/logo",
  };
  KARMA_REQUIRE(document->dependencies[0] == expected_theme);
  KARMA_REQUIRE(document->dependencies[1] == expected_svg);
  KARMA_REQUIRE(theme->dependencies.size() == 1u);
  const karma::assets::UiAssetDependency expected_font{
      .kind = karma::assets::UiAssetDependencyKind::Font,
      .key = "ui/font",
  };
  KARMA_REQUIRE(theme->dependencies.front() == expected_font);
  KARMA_REQUIRE(font->bytes == minimalFontBytes());
  KARMA_REQUIRE(svg->source_utf8 == kSvg);
  KARMA_REQUIRE(!document->content_hash.empty());
  KARMA_REQUIRE(!theme->content_hash.empty());
  KARMA_REQUIRE(!font->content_hash.empty());
  KARMA_REQUIRE(!svg->content_hash.empty());
}

void testRegistryAndDirectCacheRoundTrips() {
  karma::assets::AssetRegistry source;
  const uint64_t initial_version = source.version();
  KARMA_REQUIRE(source.registerUiDocumentAsset(
      "ui/main",
      karma::assets::UiDocumentAsset{
          .canonical_json_utf8 = std::string(kDocumentCanonical),
          .dependencies = {
              {karma::assets::UiAssetDependencyKind::UiTheme, "ui/theme"},
              {karma::assets::UiAssetDependencyKind::Svg, "ui/logo"},
          },
      }));
  KARMA_REQUIRE(source.registerUiThemeAsset(
      "ui/theme",
      karma::assets::UiThemeAsset{
          .canonical_json_utf8 = std::string(kThemeCanonical),
          .dependencies = {
              {karma::assets::UiAssetDependencyKind::Font, "ui/font"},
          },
      }));
  KARMA_REQUIRE(source.registerFontAsset(
      "ui/font", karma::assets::FontAsset{.bytes = minimalFontBytes()}));
  KARMA_REQUIRE(source.registerSvgAsset(
      "ui/logo", karma::assets::SvgAsset{.source_utf8 = std::string(kSvg)}));
  KARMA_REQUIRE(source.version() > initial_version);

  karma::assets::AssetRegistry moved;
  KARMA_REQUIRE(moved.moveAssetFrom(source, "ui_document", "ui/main"));
  KARMA_REQUIRE(moved.moveAssetFrom(source, "ui_theme", "ui/theme"));
  KARMA_REQUIRE(moved.moveAssetFrom(source, "font", "ui/font"));
  KARMA_REQUIRE(moved.moveAssetFrom(source, "svg", "ui/logo"));
  requireValidRegistry(moved);
  KARMA_REQUIRE(source.findUiDocumentAsset("ui/main") == nullptr);

  const std::filesystem::path root = makeTempDir("karma_ui_asset_cache_tests");
  karma::assets::AssetCache cache({
      .root = root,
      .enabled = true,
      .flush = false,
      .ensure_layout = true,
  });
  KARMA_REQUIRE(cache.writeUiDocument("ui_document", *moved.findUiDocumentAsset("ui/main")));
  KARMA_REQUIRE(cache.writeUiTheme("ui_theme",
                                       *moved.findUiThemeAsset("ui/theme")));
  KARMA_REQUIRE(cache.writeFont("font", *moved.findFontAsset("ui/font")));
  KARMA_REQUIRE(cache.writeSvg("svg", *moved.findSvgAsset("ui/logo")));
  KARMA_REQUIRE(cache.readUiDocument("ui_document").has_value());
  KARMA_REQUIRE(cache.readUiTheme("ui_theme").has_value());
  KARMA_REQUIRE(cache.readFont("font").has_value());
  KARMA_REQUIRE(cache.readSvg("svg").has_value());

  std::vector<uint8_t> wrong_kind =
      readBinary(root / "blobs" / "ui_document.kasset");
  writeU32(wrong_kind, 12u, 13u);
  writeBinary(root / "blobs" / "wrong_kind.kasset", wrong_kind);
  KARMA_REQUIRE(!cache.readUiDocument("wrong_kind").has_value());

  std::vector<uint8_t> bad_dependency_kind =
      readBinary(root / "blobs" / "ui_document.kasset");
  const std::size_t dependency_kind_offset =
      48u + kDocumentCanonical.size();
  writeU32(bad_dependency_kind, dependency_kind_offset, 99u);
  writeBinary(root / "blobs" / "bad_dependency_kind.kasset",
              bad_dependency_kind);
  KARMA_REQUIRE(!cache.readUiDocument("bad_dependency_kind").has_value());

  std::vector<uint8_t> truncated =
      readBinary(root / "blobs" / "ui_theme.kasset");
  truncated.resize(truncated.size() - 1u);
  writeBinary(root / "blobs" / "truncated.kasset", truncated);
  KARMA_REQUIRE(!cache.readUiTheme("truncated").has_value());

  KARMA_REQUIRE(moved.unregisterUiDocumentAsset("ui/main"));
  KARMA_REQUIRE(moved.unregisterUiThemeAsset("ui/theme"));
  KARMA_REQUIRE(moved.unregisterFontAsset("ui/font"));
  KARMA_REQUIRE(moved.unregisterSvgAsset("ui/logo"));
  std::filesystem::remove_all(root);
}

void testPackageColdWarmAsyncBakeAndFingerprint() {
  const std::filesystem::path dir = makeTempDir("karma_ui_asset_package_tests");
  const std::filesystem::path cache_dir = dir / "cache";
  const std::filesystem::path baked_dir = dir / "baked";
  writeValidPackageSources(dir);

  karma::assets::AssetPackageOptions options;
  options.cache.root = cache_dir;
  options.cache.enabled = true;
  options.cache.flush = false;

  std::string diagnostic;
  karma::assets::AssetRegistry cold;
  auto cold_package = karma::assets::importAssetPackage(cold, dir, options, &diagnostic);
  KARMA_REQUIRE(cold_package.has_value());
  KARMA_REQUIRE(diagnostic.empty());
  requireValidRegistry(cold);
  for (const auto& asset : cold_package->assets) {
    if (asset.type == "ui_document" || asset.type == "ui_theme" ||
        asset.type == "font" || asset.type == "svg") {
      KARMA_REQUIRE(!asset.cache_blob_key.empty());
      KARMA_REQUIRE(std::filesystem::exists(
          cache_dir / "blobs" / (asset.cache_blob_key + ".kasset")));
    }
  }

  karma::assets::AssetRegistry warm;
  diagnostic.clear();
  auto warm_package = karma::assets::importAssetPackage(warm, dir, options, &diagnostic);
  KARMA_REQUIRE(warm_package.has_value());
  KARMA_REQUIRE(diagnostic.empty());
  requireValidRegistry(warm);
  KARMA_REQUIRE(karma::assets::unloadAssetPackage(warm, *warm_package));
  KARMA_REQUIRE(warm.findUiDocumentAsset("ui/main") == nullptr);
  KARMA_REQUIRE(warm.findUiThemeAsset("ui/theme") == nullptr);
  KARMA_REQUIRE(warm.findFontAsset("ui/font") == nullptr);
  KARMA_REQUIRE(warm.findSvgAsset("ui/logo") == nullptr);

  karma::assets::AssetRegistry async_assets;
  karma::assets::AssetPackageJob job =
      karma::assets::loadAssetPackageAsync(dir, options);
  KARMA_REQUIRE(async_assets.findUiDocumentAsset("ui/main") == nullptr);
  job.wait();
  KARMA_REQUIRE(job.success());
  karma::assets::AssetPackageHandle async_handle;
  KARMA_REQUIRE(karma::assets::commitAssetPackageJob(async_assets, job, &async_handle));
  requireValidRegistry(async_assets);
  KARMA_REQUIRE(karma::assets::unloadAssetPackage(async_assets, async_handle));

  karma::assets::AssetPackageBakeOptions bake_options;
  bake_options.package_id = "native_ui";
  bake_options.scene_fingerprint = "ui-assets-test";
  bake_options.import_options = options;
  diagnostic.clear();
  KARMA_REQUIRE(karma::assets::bakeAssetPackage(
      dir, baked_dir, bake_options, &diagnostic));
  KARMA_REQUIRE(diagnostic.empty());
  KARMA_REQUIRE(karma::assets::checkBakedAssetPackage(
      dir, baked_dir, bake_options, &diagnostic));

  const auto original_mtime = std::filesystem::last_write_time(dir / "logo.svg");
  std::string changed_svg(kSvg);
  const std::size_t width = changed_svg.find("width=\"1\"");
  KARMA_REQUIRE(width != std::string::npos);
  changed_svg[width + 7u] = '2';
  KARMA_REQUIRE(changed_svg.size() == kSvg.size());
  writeText(dir / "logo.svg", changed_svg);
  std::filesystem::last_write_time(dir / "logo.svg", original_mtime);
  diagnostic.clear();
  KARMA_REQUIRE(!karma::assets::checkBakedAssetPackage(
      dir, baked_dir, bake_options, &diagnostic));
  KARMA_REQUIRE(diagnostic.find("stale") != std::string::npos);

  std::filesystem::remove(dir / "assets.package.json");
  std::filesystem::remove(dir / "main.kui.json5");
  std::filesystem::remove(dir / "theme.kstyle.json5");
  std::filesystem::remove(dir / "logo.svg");
  std::filesystem::remove(dir / "font.ttf");
  karma::assets::AssetRegistry baked;
  diagnostic.clear();
  auto baked_package =
      karma::assets::importBakedAssetPackage(baked, baked_dir, &diagnostic);
  KARMA_REQUIRE(baked_package.has_value());
  KARMA_REQUIRE(diagnostic.empty());
  requireValidRegistry(baked);
  KARMA_REQUIRE(karma::assets::unloadAssetPackage(baked, *baked_package));
  KARMA_REQUIRE(karma::assets::unloadAssetPackage(cold, *cold_package));
  std::filesystem::remove_all(dir);
}

void testMissingDependenciesAndUnsafeSourcesRollback() {
  const std::filesystem::path dir = makeTempDir("karma_ui_asset_security_tests");
  writeText(dir / "main.kui.json5", kDocument);
  writeText(dir / "logo.svg", kSvg);
  writeText(dir / "missing.package.json", R"({
    "version": 1,
    "assets": [
      { "type": "ui_document", "key": "ui/main", "path": "main.kui.json5" },
      { "type": "svg", "key": "ui/logo", "path": "logo.svg" }
    ]
  })");
  karma::assets::AssetPackageOptions no_cache;
  no_cache.cache.enabled = false;
  karma::assets::AssetRegistry assets;
  std::string diagnostic;
  auto missing = karma::assets::importAssetPackage(
      assets, dir / "missing.package.json", no_cache, &diagnostic);
  KARMA_REQUIRE(!missing.has_value());
  KARMA_REQUIRE(diagnostic.find("ui/theme") != std::string::npos);
  KARMA_REQUIRE(assets.findUiDocumentAsset("ui/main") == nullptr);
  KARMA_REQUIRE(assets.findSvgAsset("ui/logo") == nullptr);

  writeText(dir / "unsafe.kui.json5", R"({
    "format": "karma.ui.document",
    "version": 2,
    "themes": [{"file": "../secret.kstyle.json5"}],
    "root": {"type": "panel"}
  })");
  writeText(dir / "unsafe.package.json", R"({
    "version": 1,
    "assets": [
      { "type": "ui_document", "key": "ui/unsafe", "path": "unsafe.kui.json5" }
    ]
  })");
  diagnostic.clear();
  KARMA_REQUIRE(!karma::assets::importAssetPackage(
      assets, dir / "unsafe.package.json", no_cache, &diagnostic).has_value());
  KARMA_REQUIRE(!diagnostic.empty());

  writeText(dir / "unsafe.kstyle.json5", R"({
    "format": "karma.ui.theme",
    "version": 2,
    "imports": [{"file": "https://example.com/theme.kstyle.json5"}]
  })");
  writeText(dir / "unsafe_style.package.json", R"({
    "version": 1,
    "assets": [
      { "type": "ui_theme", "key": "ui/unsafe_style", "path": "unsafe.kstyle.json5" }
    ]
  })");
  diagnostic.clear();
  KARMA_REQUIRE(!karma::assets::importAssetPackage(
      assets, dir / "unsafe_style.package.json", no_cache, &diagnostic).has_value());

  writeText(dir / "unresolved.kstyle.json5", R"({
    "format": "karma.ui.theme",
    "version": 2,
    "imports": [{"file": "base.kstyle.json5"}]
  })");
  writeText(dir / "unresolved_style.package.json", R"({
    "version": 1,
    "assets": [
      { "type": "ui_theme", "key": "ui/unresolved_style", "path": "unresolved.kstyle.json5" }
    ]
  })");
  diagnostic.clear();
  KARMA_REQUIRE(!karma::assets::importAssetPackage(
      assets, dir / "unresolved_style.package.json", no_cache,
      &diagnostic).has_value());
  KARMA_REQUIRE(diagnostic.find("loose-file loader") != std::string::npos);

  writeText(dir / "unsafe.svg", R"(<svg xmlns="http://www.w3.org/2000/svg">
    <script>bad()</script>
  </svg>)");
  writeText(dir / "unsafe_svg.package.json", R"({
    "version": 1,
    "assets": [
      { "type": "svg", "key": "ui/unsafe_svg", "path": "unsafe.svg" }
    ]
  })");
  diagnostic.clear();
  KARMA_REQUIRE(!karma::assets::importAssetPackage(
      assets, dir / "unsafe_svg.package.json", no_cache, &diagnostic).has_value());
  KARMA_REQUIRE(assets.findSvgAsset("ui/unsafe_svg") == nullptr);

  writeText(dir / "wrong_type.kstyle.json5", R"({
    "format": "karma.ui.theme",
    "version": 2,
    "fonts": {"Wrong": {"src": {"asset": "ui/logo"}}}
  })");
  writeText(dir / "wrong_type.package.json", R"({
    "version": 1,
    "assets": [
      { "type": "ui_theme", "key": "ui/wrong_type", "path": "wrong_type.kstyle.json5" },
      { "type": "svg", "key": "ui/logo", "path": "logo.svg" }
    ]
  })");
  diagnostic.clear();
  KARMA_REQUIRE(!karma::assets::importAssetPackage(
      assets, dir / "wrong_type.package.json", no_cache, &diagnostic).has_value());
  KARMA_REQUIRE(diagnostic.find("missing font") != std::string::npos);
  KARMA_REQUIRE(assets.findUiThemeAsset("ui/wrong_type") == nullptr);

  writeText(dir / "unknown_field.kui.json5", R"({
    format: 'karma.ui.document',
    version: 2,
    root: {
      type: 'panel',
      props: {widht: 10},
    },
  })");
  writeText(dir / "unknown_field.package.json", R"({
    "version": 1,
    "assets": [
      { "type": "ui_document", "key": "ui/unknown", "path": "unknown_field.kui.json5" }
    ]
  })");
  diagnostic.clear();
  KARMA_REQUIRE(!karma::assets::importAssetPackage(
      assets, dir / "unknown_field.package.json", no_cache,
      &diagnostic).has_value());
  KARMA_REQUIRE(diagnostic.find("UI_JSON_UNKNOWN_FIELD") != std::string::npos);
  KARMA_REQUIRE(diagnostic.find("line 6, column 15") != std::string::npos);
  KARMA_REQUIRE(assets.findUiDocumentAsset("ui/unknown") == nullptr);

  std::vector<uint8_t> invalid_utf8{'{', '"', 'x', '"', ':', '"',
                                    static_cast<uint8_t>(0xc0u),
                                    static_cast<uint8_t>(0xafu), '"', '}'};
  writeBinary(dir / "invalid_utf8.kui.json5", invalid_utf8);
  writeText(dir / "invalid_utf8.package.json", R"({
    "version": 1,
    "assets": [
      { "type": "ui_document", "key": "ui/invalid_utf8", "path": "invalid_utf8.kui.json5" }
    ]
  })");
  diagnostic.clear();
  KARMA_REQUIRE(!karma::assets::importAssetPackage(
      assets, dir / "invalid_utf8.package.json", no_cache, &diagnostic).has_value());
  KARMA_REQUIRE(diagnostic.find("UTF-8") != std::string::npos);

  std::filesystem::remove_all(dir);
}

void testPilotExamplePackages() {
  const std::filesystem::path source_root =
      std::filesystem::path(__FILE__).parent_path().parent_path();
  karma::assets::AssetPackageOptions options;
  options.cache.enabled = false;

  {
    karma::assets::AssetRegistry assets;
    std::string diagnostic;
    const auto package = karma::assets::importAssetPackage(
        assets, source_root / "examples/assets/ui/native_menu", options,
        &diagnostic);
    KARMA_REQUIRE(package.has_value());
    KARMA_REQUIRE(diagnostic.empty());
    KARMA_REQUIRE(assets.findUiDocumentAsset("ui/demo/main_menu") != nullptr);
    KARMA_REQUIRE(assets.findUiThemeAsset("ui/demo/theme") != nullptr);
    KARMA_REQUIRE(assets.findFontAsset("ui/demo/font") != nullptr);
    KARMA_REQUIRE(assets.findSvgAsset("ui/demo/icon") != nullptr);
    KARMA_REQUIRE(karma::assets::unloadAssetPackage(assets, *package));
  }

  {
    karma::assets::AssetRegistry assets;
    std::string diagnostic;
    const auto package = karma::assets::importAssetPackage(
        assets, source_root / "examples/assets/ui/constraint_lab", options,
        &diagnostic);
    KARMA_REQUIRE(package.has_value());
    KARMA_REQUIRE(diagnostic.empty());
    KARMA_REQUIRE(assets.findUiDocumentAsset("ui/pilots/constraint-lab") != nullptr);
    KARMA_REQUIRE(assets.findUiThemeAsset(
                      "ui/pilots/constraint-lab-theme") != nullptr);
    KARMA_REQUIRE(assets.findFontAsset("ui/pilots/font") != nullptr);
    KARMA_REQUIRE(karma::assets::unloadAssetPackage(assets, *package));
  }

  {
    karma::assets::AssetRegistry assets;
    std::string diagnostic;
    const auto package = karma::assets::importAssetPackage(
        assets, source_root / "examples/assets/ui/tank_hud", options,
        &diagnostic);
    KARMA_REQUIRE(package.has_value());
    KARMA_REQUIRE(diagnostic.empty());
    KARMA_REQUIRE(assets.findUiDocumentAsset("ui/pilots/tank-hud") != nullptr);
    KARMA_REQUIRE(assets.findUiThemeAsset(
                      "ui/pilots/tank-hud-theme") != nullptr);
    KARMA_REQUIRE(assets.findFontAsset("ui/pilots/font") != nullptr);
    KARMA_REQUIRE(assets.findSvgAsset("ui/pilots/radar-placeholder") != nullptr);
    KARMA_REQUIRE(karma::assets::unloadAssetPackage(assets, *package));
  }
}

}  // namespace

int main() {
  testRegistryAndDirectCacheRoundTrips();
  testPackageColdWarmAsyncBakeAndFingerprint();
  testMissingDependenciesAndUnsafeSourcesRollback();
  testPilotExamplePackages();
  return 0;
}
