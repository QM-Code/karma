#include "karma/scenes.h"

#include "karma/assets.h"

#include <atomic>
#include <chrono>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

namespace {

struct Options {
  bool check = false;
  bool bake_packages = false;
  std::string bake_id;
  std::filesystem::path asset_cache_root;
  std::filesystem::path output_path;
  std::filesystem::path scene_path;
};

void printUsage() {
  std::cerr << "usage: karma_scene_bake [--check] [--bake-packages] "
               "[--asset-cache-root PATH] [--bake ID] [--output PATH] "
               "SCENE.kscene.json\n";
}

bool hasSuffix(std::string_view value, std::string_view suffix) {
  return value.size() >= suffix.size() &&
         value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
}

std::optional<Options> parseOptions(int argc, char** argv) {
  Options options{};
  for (int i = 1; i < argc; ++i) {
    const std::string_view arg(argv[i]);
    if (arg == "--check") {
      options.check = true;
    } else if (arg == "--bake-packages") {
      options.bake_packages = true;
    } else if (arg == "--asset-cache-root") {
      if (++i >= argc) {
        return std::nullopt;
      }
      options.asset_cache_root = argv[i];
    } else if (arg == "--bake") {
      if (++i >= argc) {
        return std::nullopt;
      }
      options.bake_id = argv[i];
    } else if (arg == "--output" || arg == "-o") {
      if (++i >= argc) {
        return std::nullopt;
      }
      options.output_path = argv[i];
    } else if (arg == "--help" || arg == "-h") {
      printUsage();
      std::exit(0);
    } else if (!arg.empty() && arg.front() == '-') {
      return std::nullopt;
    } else if (options.scene_path.empty()) {
      options.scene_path = argv[i];
    } else {
      return std::nullopt;
    }
  }
  if (options.scene_path.empty()) {
    return std::nullopt;
  }
  return options;
}

std::string sceneBaseName(const std::filesystem::path& scene_path) {
  const std::string filename = scene_path.filename().string();
  constexpr std::string_view suffix = ".kscene.json";
  if (hasSuffix(filename, suffix)) {
    return filename.substr(0, filename.size() - suffix.size());
  }
  return scene_path.stem().string();
}

std::filesystem::path defaultOutputPath(const std::filesystem::path& scene_path) {
  return scene_path.parent_path() / (sceneBaseName(scene_path) + ".kscenebake.json");
}

std::filesystem::path documentBasePath(const karma::scenes::SceneDocument& document) {
  if (!document.reference_root.empty()) {
    return document.reference_root;
  }
  const std::filesystem::path parent = document.source_path.parent_path();
  return parent.empty() ? std::filesystem::path(".") : parent;
}

std::filesystem::path resolveDocumentPath(const karma::scenes::SceneDocument& document,
                                          const std::filesystem::path& path) {
  if (path.empty() || path.is_absolute()) {
    return path;
  }
  return (documentBasePath(document) / path).lexically_normal();
}

std::filesystem::path defaultAssetCacheRoot(
    const karma::scenes::SceneDocument& document) {
  return documentBasePath(document) / "bakes" / "asset_cache";
}

std::filesystem::path assetPackageBakePath(
    const karma::scenes::SceneDocument& document,
    const Options& options,
    const karma::scenes::SceneAssetRef& package) {
  const std::filesystem::path root =
      options.asset_cache_root.empty() ? defaultAssetCacheRoot(document)
                                       : options.asset_cache_root;
  return root / package.id;
}

karma::scenes::SceneBakeDesc selectBakeDesc(const karma::scenes::SceneDocument& document,
                                            const Options& options,
                                            bool& found) {
  found = true;
  if (!options.bake_id.empty()) {
    for (const karma::scenes::SceneBakeDesc& bake : document.bakes) {
      if (bake.id == options.bake_id) {
        return bake;
      }
    }
    found = false;
    return {};
  }
  if (!document.bakes.empty()) {
    return document.bakes.front();
  }

  karma::scenes::SceneBakeDesc bake{};
  bake.id = sceneBaseName(document.source_path);
  return bake;
}

bool readJsonFile(const std::filesystem::path& path, nlohmann::json& out) {
  std::ifstream stream(path);
  if (!stream) {
    return false;
  }
  try {
    stream >> out;
  } catch (const std::exception&) {
    return false;
  }
  return out.is_object();
}

bool writeJsonFile(const std::filesystem::path& path,
                   const nlohmann::json& json) {
  std::error_code error;
  if (!path.parent_path().empty()) {
    std::filesystem::create_directories(path.parent_path(), error);
    if (error) return false;
  }
  static std::atomic<uint64_t> sequence{0u};
  const std::string token = std::to_string(
                                std::chrono::steady_clock::now()
                                    .time_since_epoch()
                                    .count()) +
                            "." +
                            std::to_string(sequence.fetch_add(1u));
  const std::filesystem::path temporary =
      path.parent_path() /
      (path.filename().string() + ".tmp." + token);
  {
    std::ofstream stream(temporary, std::ios::trunc);
    if (!stream) return false;
    stream << json.dump(2) << '\n';
    stream.flush();
    if (!stream) {
      std::filesystem::remove(temporary, error);
      return false;
    }
  }
  std::filesystem::rename(temporary, path, error);
  if (!error) return true;

  error.clear();
  const std::filesystem::path backup =
      path.parent_path() /
      (path.filename().string() + ".backup." + token);
  const bool had_previous = std::filesystem::exists(path, error);
  error.clear();
  if (had_previous) {
    std::filesystem::rename(path, backup, error);
    if (error) {
      std::filesystem::remove(temporary, error);
      return false;
    }
  }
  std::filesystem::rename(temporary, path, error);
  if (error) {
    std::filesystem::remove(temporary, error);
    error.clear();
    if (had_previous) std::filesystem::rename(backup, path, error);
    return false;
  }
  if (had_previous) std::filesystem::remove(backup, error);
  return true;
}

bool portableArtifactPath(const std::filesystem::path& path) {
  if (path.empty() || path.is_absolute() || path.has_root_path()) return false;
  const std::filesystem::path normalized = path.lexically_normal();
  if (normalized.empty() || normalized == ".") return false;
  for (const auto& part : normalized) {
    if (part == "..") return false;
  }
  return true;
}

bool checkProducedArtifacts(
    const karma::scenes::SceneDocument& document,
    const nlohmann::json& manifest) {
  const auto produced = manifest.find("produced_assets");
  if (produced == manifest.end()) return true;
  if (!produced->is_array()) {
    std::cerr << "scene bake produced_assets must be an array\n";
    return false;
  }
  for (const nlohmann::json& asset : *produced) {
    if (!asset.is_object() || !asset.contains("path") ||
        !asset["path"].is_string() || !asset.contains("type") ||
        !asset["type"].is_string()) {
      std::cerr << "scene bake contains an invalid produced asset record\n";
      return false;
    }
    const std::filesystem::path relative_path =
        asset["path"].get<std::string>();
    if (!portableArtifactPath(relative_path)) {
      std::cerr << "scene bake artifact path is not portable: "
                << relative_path << '\n';
      return false;
    }
    const std::filesystem::path path =
        resolveDocumentPath(document, relative_path);
    std::error_code error;
    if (!std::filesystem::is_regular_file(path, error) || error) {
      std::cerr << "scene bake artifact is missing: " << path << '\n';
      return false;
    }
    const std::string type = asset["type"].get<std::string>();
    std::string diagnostic;
    if (type == "baked_mesh") {
      if (!karma::assets::loadBakedMeshArtifact(path, &diagnostic)) {
        std::cerr << "scene bake mesh artifact is unreadable: " << path
                  << ": " << diagnostic << '\n';
        return false;
      }
    } else if (type == "baked_irradiance_rgba8" ||
               type == "baked_direction_rgba8") {
      if (!karma::assets::loadBakedRgba8Artifact(path, &diagnostic)) {
        std::cerr << "scene bake image artifact is unreadable: " << path
                  << ": " << diagnostic << '\n';
        return false;
      }
#if defined(KARMA_ENABLE_NAVIGATION)
    } else if (type == "navigation_navmesh") {
      if (!karma::assets::loadNavMeshSnapshot(path).valid()) {
        std::cerr << "scene bake navmesh artifact is unreadable: " << path
                  << '\n';
        return false;
      }
    } else if (type == "navigation_tile_cache") {
      if (!karma::assets::loadNavTileCacheSnapshot(path).valid()) {
        std::cerr << "scene bake tile-cache artifact is unreadable: " << path
                  << '\n';
        return false;
      }
#endif
    } else if (std::filesystem::file_size(path, error) == 0u || error) {
      std::cerr << "scene bake artifact is empty: " << path << '\n';
      return false;
    }
  }
  return true;
}

bool bakeAssetPackages(const karma::scenes::SceneDocument& document,
                       const Options& options,
                       std::string_view scene_fingerprint) {
  for (const karma::scenes::SceneAssetRef& package : document.asset_packages) {
    karma::assets::AssetPackageBakeOptions bake_options{};
    bake_options.package_id = package.id;
    bake_options.scene_fingerprint = std::string(scene_fingerprint);
    std::string diagnostic;
    const std::filesystem::path source_package =
        resolveDocumentPath(document, package.path);
    const std::filesystem::path output_dir =
        assetPackageBakePath(document, options, package);
    if (!karma::assets::bakeAssetPackage(source_package,
                                         output_dir,
                                         bake_options,
                                         &diagnostic)) {
      std::cerr << "failed to bake asset package '" << package.id
                << "' to " << output_dir << ": " << diagnostic << '\n';
      return false;
    }
  }
  return true;
}

bool checkAssetPackages(const karma::scenes::SceneDocument& document,
                        const Options& options,
                        std::string_view scene_fingerprint) {
  for (const karma::scenes::SceneAssetRef& package : document.asset_packages) {
    karma::assets::AssetPackageBakeOptions bake_options{};
    bake_options.package_id = package.id;
    bake_options.scene_fingerprint = std::string(scene_fingerprint);
    std::string diagnostic;
    const std::filesystem::path source_package =
        resolveDocumentPath(document, package.path);
    const std::filesystem::path output_dir =
        assetPackageBakePath(document, options, package);
    if (!karma::assets::checkBakedAssetPackage(source_package,
                                              output_dir,
                                              bake_options,
                                              &diagnostic)) {
      std::cerr << "baked asset package is stale: " << output_dir << '\n'
                << diagnostic << '\n';
      return false;
    }
  }
  return true;
}

}  // namespace

int main(int argc, char** argv) {
  const std::optional<Options> parsed = parseOptions(argc, argv);
  if (!parsed.has_value()) {
    printUsage();
    return 1;
  }
  const Options& options = *parsed;

  karma::scenes::SceneLoadResult load = karma::scenes::loadSceneDocument(options.scene_path);
  if (!load.success()) {
    for (const std::string& diagnostic : load.diagnostics) {
      std::cerr << diagnostic << '\n';
    }
    return 1;
  }

  bool found_bake = false;
  karma::scenes::SceneBakeDesc bake = selectBakeDesc(*load.document, options, found_bake);
  if (!found_bake) {
    std::cerr << "scene bake id not found: " << options.bake_id << '\n';
    return 1;
  }

  const std::filesystem::path output_path =
      !options.output_path.empty()
          ? options.output_path
          : (!bake.path.empty()
                 ? resolveDocumentPath(*load.document, bake.path)
                 : defaultOutputPath(options.scene_path));

  if (options.check) {
    nlohmann::json existing;
    if (!readJsonFile(output_path, existing)) {
      std::cerr << "failed to read existing scene bake: " << output_path << '\n';
      return 1;
    }
    const std::string expected_fingerprint =
        karma::scenes::sceneBakeFingerprint(*load.document, bake);
    const std::string existing_fingerprint =
        existing.value("scene_fingerprint", std::string{});
    if (existing_fingerprint != expected_fingerprint) {
      std::cerr << "scene bake is stale: " << output_path << '\n'
                << "expected fingerprint: " << expected_fingerprint << '\n'
                << "existing fingerprint: " << existing_fingerprint << '\n';
      return 2;
    }
    if (!checkProducedArtifacts(*load.document, existing)) return 2;
    if (options.bake_packages &&
        !checkAssetPackages(*load.document, options, expected_fingerprint)) {
      return 2;
    }
    return 0;
  }

  karma::scenes::SceneBakeResult result =
      karma::scenes::bakeScene(*load.document, bake);
  if (!result.success) {
    std::cerr << result.diagnostic << '\n';
    return 1;
  }

  if (options.bake_packages &&
      !bakeAssetPackages(*load.document, options, result.scene_fingerprint)) {
    return 1;
  }

  if (!writeJsonFile(output_path, result.metadata)) {
    std::cerr << "failed to write scene bake: " << output_path << '\n';
    return 1;
  }
  std::cout << output_path.lexically_normal().generic_string() << '\n';
  return 0;
}
