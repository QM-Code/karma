#include "karma/scenes.h"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace {

#define KARMA_REQUIRE(expression)                                      \
  do {                                                                \
    if (!(expression)) {                                               \
      std::cerr << "Requirement failed: " << #expression << " at "   \
                << __FILE__ << ":" << __LINE__ << '\n';              \
      std::abort();                                                    \
    }                                                                 \
  } while (false)

std::filesystem::path makeTempDir() {
  const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
  const std::filesystem::path dir =
      std::filesystem::temp_directory_path() /
      ("karma_scene_bake_tests_" + std::to_string(now));
  std::filesystem::create_directories(dir);
  return dir;
}

void writeText(const std::filesystem::path& path, const std::string& text) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream stream(path);
  stream << text;
}

std::string sceneJson(std::string_view root_name) {
  return std::string(R"({
  "version": 1,
  "name": "Bake Fixture",
  "entities": [
    {
      "id": "root",
      "name": ")") + std::string(root_name) + R"(",
      "transform": {
        "position": [1.0, 2.0, 3.0],
        "rotation": [0.0, 0.0, 0.0, 1.0],
        "scale": [1.0, 1.0, 1.0]
      }
    }
  ],
  "static": [
    {
      "id": "root_static",
      "entity": "root",
      "transform": true,
      "render": false,
      "lighting": true
    }
  ],
  "bakes": [
    {
      "id": "main",
      "path": "bakes/main.kbake.json",
      "static": ["root_static"],
      "nav_cache": ["bakes/main.knav"]
    }
  ]
})";
}

karma::scenes::SceneDocument loadDocument(const std::filesystem::path& path) {
  karma::scenes::SceneLoadResult load = karma::scenes::loadSceneDocument(path);
  if (!load.success()) {
    for (const std::string& diagnostic : load.diagnostics) {
      std::cerr << diagnostic << '\n';
    }
  }
  KARMA_REQUIRE(load.success());
  return *load.document;
}

void testSceneBakeOutputIsDeterministic() {
  const std::filesystem::path dir = makeTempDir();
  const std::filesystem::path scene_path = dir / "fixture.kscene.json";
  writeText(scene_path, sceneJson("Root"));
  writeText(dir / "bakes/main.knav", "nav-cache-v1");

  const karma::scenes::SceneDocument document = loadDocument(scene_path);
  KARMA_REQUIRE(document.bakes.size() == 1u);

  const karma::scenes::SceneBakeResult first =
      karma::scenes::bakeScene(document, document.bakes.front());
  const karma::scenes::SceneBakeResult second =
      karma::scenes::bakeScene(document, document.bakes.front());

  KARMA_REQUIRE(first.success);
  KARMA_REQUIRE(second.success);
  KARMA_REQUIRE(!first.scene_fingerprint.empty());
  KARMA_REQUIRE(first.scene_fingerprint == second.scene_fingerprint);
  KARMA_REQUIRE(first.metadata.dump(2) == second.metadata.dump(2));
  KARMA_REQUIRE(first.metadata["schema"] == "karma.scene_bake");
  KARMA_REQUIRE(first.metadata["static_ids"].size() == 1u);
  KARMA_REQUIRE(first.metadata["static_metadata"]["transforms"].size() == 1u);
  KARMA_REQUIRE(first.metadata["nav_cache_files"].size() == 1u);
}

void testSceneBakeFingerprintChangesWhenSourceChanges() {
  const std::filesystem::path dir = makeTempDir();
  const std::filesystem::path scene_path = dir / "fixture.kscene.json";
  writeText(scene_path, sceneJson("Root"));
  writeText(dir / "bakes/main.knav", "nav-cache-v1");

  const karma::scenes::SceneDocument original = loadDocument(scene_path);
  const karma::scenes::SceneBakeResult original_bake =
      karma::scenes::bakeScene(original, original.bakes.front());
  KARMA_REQUIRE(original_bake.success);

  writeText(scene_path, sceneJson("Changed Root"));
  const karma::scenes::SceneDocument changed = loadDocument(scene_path);
  const karma::scenes::SceneBakeResult changed_bake =
      karma::scenes::bakeScene(changed, changed.bakes.front());
  KARMA_REQUIRE(changed_bake.success);
  KARMA_REQUIRE(original_bake.scene_fingerprint != changed_bake.scene_fingerprint);
}

}  // namespace

int main() {
  testSceneBakeOutputIsDeterministic();
  testSceneBakeFingerprintChangesWhenSourceChanges();
  return 0;
}
