#include "scene_editor_model.h"
#include "scene_editor_foliage_prefab.h"
#include "scene_editor_markers.h"
#include "scene_editor_migration.h"
#include "scene_editor_placement.h"

#include "karma/assets.h"
#include "karma/components.h"
#include "karma/prefabs.h"
#include "karma/visual.h"
#include "karma/world.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>

namespace {

#define KARMA_REQUIRE(expression)                                     \
  do {                                                               \
    if (!(expression)) {                                             \
      std::cerr << "Requirement failed: " << #expression << " at " \
                << __FILE__ << ':' << __LINE__ << '\n';             \
      std::abort();                                                  \
    }                                                                \
  } while (false)

namespace editor = karma::tools::scene_editor;

bool nearly(float a, float b) {
  return std::abs(a - b) < 0.0001f;
}

bool nearlyVec3(const karma::math::Vec3& a,
                const karma::math::Vec3& b) {
  return nearly(a.x, b.x) && nearly(a.y, b.y) && nearly(a.z, b.z);
}

bool nearlyRotation(const karma::math::Quat& a,
                    const karma::math::Quat& b) {
  const karma::math::Quat normalized_a = karma::math::normalize(a);
  const karma::math::Quat normalized_b = karma::math::normalize(b);
  return nearly(std::abs(karma::math::dot(normalized_a, normalized_b)), 1.0f);
}

bool nearlyTransform(const karma::scenes::SceneTransform& a,
                     const karma::scenes::SceneTransform& b) {
  return nearlyVec3(a.position, b.position) &&
         nearlyRotation(a.rotation, b.rotation) &&
         nearlyVec3(a.scale, b.scale);
}

std::filesystem::path tempDirectory() {
  const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
  const std::filesystem::path path =
      std::filesystem::temp_directory_path() /
      ("karma_scene_editor_model_" + std::to_string(stamp));
  std::filesystem::create_directories(path);
  return path;
}

std::filesystem::path findRepoRoot() {
  std::vector<std::filesystem::path> starts{std::filesystem::current_path()};
  const std::filesystem::path source_path = std::filesystem::path(__FILE__);
  if (source_path.is_absolute()) {
    starts.push_back(source_path.parent_path());
  }
  for (std::filesystem::path start : starts) {
    for (std::filesystem::path cursor = start;
         !cursor.empty();
         cursor = cursor.parent_path()) {
      if (std::filesystem::exists(
              cursor / "examples/assets/scene_editor_content/scenes/mini.kscene.json")) {
        return cursor;
      }
      if (cursor == cursor.parent_path()) break;
    }
  }
  return {};
}

std::string stableRecoveryKey(const std::filesystem::path& scene_path) {
  std::error_code ec;
  std::filesystem::path canonical =
      std::filesystem::weakly_canonical(scene_path, ec);
  if (ec) {
    ec.clear();
    canonical = std::filesystem::absolute(scene_path, ec);
  }
  if (ec) canonical = scene_path.lexically_normal();
  const std::string input = canonical.generic_string();
  uint64_t hash = 14695981039346656037ull;
  for (const unsigned char byte : input) {
    hash ^= static_cast<uint64_t>(byte);
    hash *= 1099511628211ull;
  }
  std::ostringstream stream;
  stream << std::hex << std::setfill('0') << std::setw(16) << hash;
  return stream.str();
}

void writeText(const std::filesystem::path& path, std::string_view text) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream stream(path);
  stream << text;
}

std::string readText(const std::filesystem::path& path) {
  std::ifstream stream(path, std::ios::binary);
  std::ostringstream text;
  text << stream.rdbuf();
  return text.str();
}

karma::scenes::SceneDocument makeSceneDocument(
    const std::filesystem::path& content_root,
    const std::filesystem::path& scene_path) {
  karma::scenes::SceneDocument document{};
  document.name = "Existing Scene";
  document.source_path = scene_path;
  document.reference_root = content_root;
  document.entities.push_back(
      karma::scenes::SceneEntity{.id = "root", .name = "Scene"});
  document.entities.push_back(karma::scenes::SceneEntity{
      .id = "existing", .name = "Existing", .parent_id = "root"});
  return document;
}

karma::scene_authoring::TerrainCanvas makeTerrainCanvas() {
  auto canvas = karma::scene_authoring::TerrainCanvas::create(
      karma::scene_authoring::TerrainCanvasDesc{
          .resolution = 5u,
          .control_resolution = 5u,
          .terrain_size = 16.0f,
          .height_scale = 8.0f,
          .height_offset = -2.0f,
      },
      0.25f);
  KARMA_REQUIRE(canvas.has_value());
  return std::move(*canvas);
}

void testPathContainment() {
  const auto root = tempDirectory();
  const auto asset = root / "assets/prefabs/tree/prefab.json";
  writeText(asset, "{}");
  KARMA_REQUIRE(editor::pathIsWithin(root, asset));
  const auto relative = editor::contentRelativePath(root, asset);
  KARMA_REQUIRE(relative.has_value());
  KARMA_REQUIRE(relative->generic_string() == "assets/prefabs/tree/prefab.json");
  KARMA_REQUIRE(!editor::pathIsWithin(root, root.parent_path() / "outside.json"));
  std::filesystem::remove_all(root);
}

void testCatalogAndConflicts() {
  const auto root = tempDirectory();
  writeText(root / "prefabs/tree/prefab.json", R"({
    "version": 2,
    "root": 0,
    "nodes": [{"id": 0, "name": "Oak Tree", "parent": null, "components": {}}]
  })");
  writeText(root / "nature/assets.package.json", R"({
    "version": 1,
    "assets": [
      {"type": "mesh", "key": "nature/oak", "path": "oak.glb"},
      {"type": "material", "key": "nature/bark", "path": "bark.json"}
    ]
  })");
  writeText(root / "duplicate/assets.package.json", R"({
    "version": 1,
    "assets": [{"type": "mesh", "key": "nature/oak", "path": "other.glb"}]
  })");

  editor::AssetCatalog catalog;
  const auto result = catalog.scan(root, {"."});
  KARMA_REQUIRE(result.entries.size() == 6u);
  const auto* prefab = catalog.findPrefab(root / "prefabs/tree/prefab.json");
  KARMA_REQUIRE(prefab != nullptr);
  KARMA_REQUIRE(prefab->name == "Oak Tree");
  const auto* mesh = catalog.findByKey("nature/oak");
  KARMA_REQUIRE(mesh != nullptr);
  KARMA_REQUIRE(!mesh->valid);
  const auto* material = catalog.findByKey("nature/bark");
  KARMA_REQUIRE(material != nullptr);
  KARMA_REQUIRE(material->path.lexically_normal() ==
                (root / "nature/bark.json").lexically_normal());
  KARMA_REQUIRE(material->package_path.lexically_normal() ==
                (root / "nature/assets.package.json").lexically_normal());
  std::filesystem::remove_all(root);
}

void testMiniContentRoot() {
  const std::filesystem::path repo_root = findRepoRoot();
  KARMA_REQUIRE(!repo_root.empty());
  const std::filesystem::path content_root =
      repo_root / "examples/assets/scene_editor_content";
  const std::filesystem::path scene_path =
      content_root / "scenes/mini.kscene.json";
  const std::filesystem::path prefab_path =
      content_root / "prefabs/warm_light_rig/prefab.json";

  const karma::scenes::SceneLoadResult scene = karma::scenes::loadSceneDocument(
      karma::scenes::SceneLoadDesc{
          .path = scene_path,
          .reference_root = content_root,
      });
  KARMA_REQUIRE(scene.success());
  KARMA_REQUIRE(scene.document->prefab_instances.size() == 3u);
  KARMA_REQUIRE(std::any_of(
      scene.document->prefab_instances.begin(),
      scene.document->prefab_instances.end(),
      [](const karma::scenes::ScenePrefabInstance& instance) {
        return instance.id == "grass_lod_instance";
      }));
  KARMA_REQUIRE(std::any_of(
      scene.document->prefab_instances.begin(),
      scene.document->prefab_instances.end(),
      [](const karma::scenes::ScenePrefabInstance& instance) {
        return instance.id == "pine_tree_lod_instance";
      }));
  KARMA_REQUIRE(scene.document->lights.size() == 2u);
  KARMA_REQUIRE(karma::scenes::validateSceneDocument(*scene.document).success());

  const karma::prefabs::PrefabLoadResult prefab =
      karma::prefabs::loadPrefabDocument(prefab_path);
  KARMA_REQUIRE(prefab.success());
  KARMA_REQUIRE(prefab.document->nodes.size() == 2u);

  editor::AssetCatalog catalog;
  const editor::CatalogScanResult catalog_result = catalog.scan(content_root, {"."});
  KARMA_REQUIRE(catalog_result.success());
  const editor::AssetEntry* entry = catalog.findPrefab(prefab_path);
  KARMA_REQUIRE(entry != nullptr);
  KARMA_REQUIRE(entry->valid);
  KARMA_REQUIRE(entry->name == "Warm Light Rig");
}

void testCatalogWatchesSourcesAndNewManifests() {
  const auto root = tempDirectory();
  const auto package = root / "nature/assets.package.json";
  const auto source = root / "nature/oak.glb";
  writeText(source, "mesh");
  writeText(package, R"({
    "version": 1,
    "assets": [{"type": "mesh", "key": "nature/oak", "path": "oak.glb"}]
  })");

  editor::AssetCatalog catalog;
  catalog.scan(root, {"."});
  std::error_code ec;
  const auto source_time = std::filesystem::last_write_time(source, ec);
  KARMA_REQUIRE(!ec);
  std::filesystem::last_write_time(source, source_time + std::chrono::seconds(2), ec);
  KARMA_REQUIRE(!ec);
  const auto source_changes = catalog.changedFiles();
  KARMA_REQUIRE(std::find(source_changes.begin(), source_changes.end(), source) !=
                source_changes.end());

  catalog.scan(root, {"."});
  const auto directory_time = std::filesystem::last_write_time(root, ec);
  KARMA_REQUIRE(!ec);
  writeText(root / "new/assets.package.json", R"({"version": 1, "assets": []})");
  std::filesystem::last_write_time(root, directory_time + std::chrono::seconds(2), ec);
  KARMA_REQUIRE(!ec);
  KARMA_REQUIRE(!catalog.changedFiles().empty());
  std::filesystem::remove_all(root);
}

void testCatalogRejectsMalformedFieldTypes() {
  const auto root = tempDirectory();
  writeText(root / "wrong_type/assets.package.json", R"({
    "version": 1,
    "assets": [{"type": 42, "key": "nature/oak", "path": "oak.glb"}]
  })");
  writeText(root / "wrong_key/assets.package.json", R"({
    "version": 1,
    "assets": [{"type": "mesh", "key": ["nature/oak"], "path": "oak.glb"}]
  })");
  writeText(root / "wrong_assets/assets.package.json", R"({
    "version": 1,
    "assets": {"type": "mesh", "key": "nature/oak"}
  })");
  writeText(root / "missing_version/assets.package.json", R"({
    "assets": []
  })");
  writeText(root / "empty_key/assets.package.json", R"({
    "version": 1,
    "assets": [{"type": "mesh", "key": "", "path": "oak.glb"}]
  })");
  writeText(root / "wrong_path/assets.package.json", R"({
    "version": 1,
    "assets": [{"type": "mesh", "key": "nature/path", "path": 42}]
  })");
  writeText(root / "invalid_prefab/prefab.json", R"({
    "version": 2,
    "root": 0,
    "nodes": "not an array"
  })");

  editor::AssetCatalog catalog;
  const auto result = catalog.scan(root, {"."});
  KARMA_REQUIRE(result.entries.size() == 7u);
  KARMA_REQUIRE(std::all_of(
      result.entries.begin(), result.entries.end(),
      [](const editor::AssetEntry& entry) {
        return (entry.kind == editor::AssetKind::Package ||
                entry.kind == editor::AssetKind::Prefab) &&
               !entry.valid && !entry.diagnostic.empty();
      }));
  KARMA_REQUIRE(catalog.findByKey("nature/oak") == nullptr);
  std::filesystem::remove_all(root);
}

void testSettingsRoundTrip() {
  KARMA_REQUIRE(static_cast<int>(editor::ViewportRenderMode::Rendered) == 0);
  KARMA_REQUIRE(static_cast<int>(editor::ViewportRenderMode::Diffuse) == 1);
  KARMA_REQUIRE(static_cast<int>(editor::ViewportRenderMode::Texture) == 2);
  KARMA_REQUIRE(static_cast<int>(editor::ViewportRenderMode::Wire) == 3);
  const auto root = tempDirectory();
  editor::EditorSettings saved{};
  saved.asset_roots = {"assets", "shared"};
  saved.camera_move_speed = 24.0f;
  saved.grid_size = 0.25f;
  saved.snap_enabled = true;
  saved.markers_visible = false;
  saved.panel_layout.hierarchy_width = 310.0f;
  saved.panel_layout.inspector_width = 375.0f;
  saved.panel_layout.assets_height = 205.0f;
  saved.asset_filter = "tree bark";
  saved.hierarchy_filter = "pine";
  saved.inspector_filter = "physics";
  saved.selected_bake_id = "production";
  saved.asset_type_filter = 3;
  saved.console_min_level = 2;
  saved.terrain_inspector_tab = 2;
  saved.terrain_material_layer = 2;
  saved.active_foliage_layer_id = "pine_foliage";
  saved.component_foldouts = {
      {"TransformComponent", true},
      {"MeshComponent", false},
      {"TerrainComponent", true},
  };
  saved.bottom_panel_tab = editor::BottomPanelTab::Lighting;
  saved.viewport_render_mode = editor::ViewportRenderMode::Texture;
  std::string diagnostic;
  KARMA_REQUIRE(editor::saveEditorSettings(root, saved, &diagnostic));
  editor::EditorSettings loaded{};
  KARMA_REQUIRE(editor::loadEditorSettings(root, loaded, &diagnostic));
  KARMA_REQUIRE(loaded.asset_roots == saved.asset_roots);
  KARMA_REQUIRE(loaded.camera_move_speed == saved.camera_move_speed);
  KARMA_REQUIRE(loaded.grid_size == saved.grid_size);
  KARMA_REQUIRE(loaded.snap_enabled);
  KARMA_REQUIRE(!loaded.markers_visible);
  KARMA_REQUIRE(loaded.panel_layout.hierarchy_width == 310.0f);
  KARMA_REQUIRE(loaded.panel_layout.inspector_width == 375.0f);
  KARMA_REQUIRE(loaded.panel_layout.assets_height == 205.0f);
  KARMA_REQUIRE(loaded.asset_filter == "tree bark");
  KARMA_REQUIRE(loaded.hierarchy_filter == "pine");
  KARMA_REQUIRE(loaded.inspector_filter == "physics");
  KARMA_REQUIRE(loaded.selected_bake_id == "production");
  KARMA_REQUIRE(loaded.asset_type_filter == 3);
  KARMA_REQUIRE(loaded.console_min_level == 2);
  KARMA_REQUIRE(loaded.terrain_inspector_tab == 2);
  KARMA_REQUIRE(loaded.terrain_material_layer == 2);
  KARMA_REQUIRE(loaded.active_foliage_layer_id == "pine_foliage");
  KARMA_REQUIRE(loaded.component_foldouts == saved.component_foldouts);
  KARMA_REQUIRE(loaded.bottom_panel_tab == editor::BottomPanelTab::Lighting);
  KARMA_REQUIRE(loaded.viewport_render_mode ==
                editor::ViewportRenderMode::Texture);

  writeText(editor::settingsPath(root), R"({
    "version": 1,
    "asset_roots": ["."]
  })");
  KARMA_REQUIRE(editor::loadEditorSettings(root, loaded, &diagnostic));
  KARMA_REQUIRE(loaded.markers_visible);
  KARMA_REQUIRE(loaded.panel_layout.hierarchy_width == 285.0f);
  KARMA_REQUIRE(loaded.panel_layout.inspector_width == 340.0f);
  KARMA_REQUIRE(loaded.panel_layout.assets_height == 240.0f);
  KARMA_REQUIRE(loaded.asset_filter.empty());
  KARMA_REQUIRE(loaded.hierarchy_filter.empty());
  KARMA_REQUIRE(loaded.inspector_filter.empty());
  KARMA_REQUIRE(loaded.selected_bake_id.empty());
  KARMA_REQUIRE(loaded.asset_type_filter == 0);
  KARMA_REQUIRE(loaded.console_min_level == 0);
  KARMA_REQUIRE(loaded.terrain_inspector_tab == 0);
  KARMA_REQUIRE(loaded.terrain_material_layer == 0);
  KARMA_REQUIRE(loaded.active_foliage_layer_id.empty());
  KARMA_REQUIRE(loaded.component_foldouts.empty());
  KARMA_REQUIRE(loaded.bottom_panel_tab == editor::BottomPanelTab::Assets);
  KARMA_REQUIRE(loaded.viewport_render_mode ==
                editor::ViewportRenderMode::Rendered);

  writeText(editor::settingsPath(root), R"({
    "version": 1,
    "terrain_inspector_tab": 3
  })");
  KARMA_REQUIRE(!editor::loadEditorSettings(root, loaded, &diagnostic));
  KARMA_REQUIRE(diagnostic.find("terrain_inspector_tab") !=
                std::string::npos);

  writeText(editor::settingsPath(root), R"({
    "version": 1,
    "component_foldouts": {"TransformComponent": "open"}
  })");
  KARMA_REQUIRE(!editor::loadEditorSettings(root, loaded, &diagnostic));
  KARMA_REQUIRE(diagnostic.find("component_foldouts") != std::string::npos);
  std::filesystem::remove_all(root);
}

void testWorkspaceLayoutDefaultsAndConstraints() {
  const editor::EditorSettings settings{};
  const editor::EditorWorkspaceLayout normal =
      editor::resolveEditorWorkspaceLayout(settings.panel_layout, 1280.0f, 640.0f);
  KARMA_REQUIRE(nearly(normal.hierarchy_width, 285.0f));
  KARMA_REQUIRE(nearly(normal.inspector_width, 340.0f));
  KARMA_REQUIRE(nearly(normal.assets_height, 240.0f));
  KARMA_REQUIRE(nearly(normal.hierarchy_width + normal.center_width +
                           normal.inspector_width + normal.splitter_size * 2.0f,
                       1280.0f));
  KARMA_REQUIRE(nearly(normal.viewport_height + normal.assets_height +
                           normal.splitter_size,
                       640.0f));
  KARMA_REQUIRE(!normal.compact_width);
  KARMA_REQUIRE(!normal.compact_height);

  editor::EditorSettings::PanelLayout oversized{};
  oversized.hierarchy_width = 5000.0f;
  oversized.inspector_width = 7000.0f;
  oversized.assets_height = 9000.0f;
  const editor::EditorWorkspaceLayout clamped =
      editor::resolveEditorWorkspaceLayout(oversized, 1280.0f, 640.0f);
  KARMA_REQUIRE(clamped.hierarchy_width >= 220.0f);
  KARMA_REQUIRE(clamped.inspector_width >= 300.0f);
  KARMA_REQUIRE(clamped.center_width >= 320.0f);
  KARMA_REQUIRE(clamped.viewport_height >= 220.0f);
  KARMA_REQUIRE(clamped.assets_height >= 150.0f);
  KARMA_REQUIRE(nearly(clamped.hierarchy_width + clamped.center_width +
                           clamped.inspector_width + clamped.splitter_size * 2.0f,
                       1280.0f));

  const editor::EditorWorkspaceLayout small =
      editor::resolveEditorWorkspaceLayout(oversized, 360.0f, 180.0f);
  KARMA_REQUIRE(small.compact_width);
  KARMA_REQUIRE(small.compact_height);
  KARMA_REQUIRE(small.hierarchy_width >= 0.0f);
  KARMA_REQUIRE(small.inspector_width >= 0.0f);
  KARMA_REQUIRE(small.center_width >= 0.0f);
  KARMA_REQUIRE(small.viewport_height >= 0.0f);
  KARMA_REQUIRE(small.assets_height >= 0.0f);
  KARMA_REQUIRE(nearly(small.hierarchy_width + small.center_width +
                           small.inspector_width + small.splitter_size * 2.0f,
                       360.0f));
  KARMA_REQUIRE(nearly(small.viewport_height + small.assets_height +
                           small.splitter_size,
                       180.0f));

  editor::EditorSettings::PanelLayout invalid{};
  invalid.hierarchy_width = std::numeric_limits<float>::quiet_NaN();
  invalid.inspector_width = -4.0f;
  invalid.assets_height = std::numeric_limits<float>::infinity();
  const editor::EditorWorkspaceLayout recovered =
      editor::resolveEditorWorkspaceLayout(invalid, 1280.0f, 640.0f);
  KARMA_REQUIRE(nearly(recovered.hierarchy_width, 285.0f));
  KARMA_REQUIRE(nearly(recovered.inspector_width, 340.0f));
  KARMA_REQUIRE(nearly(recovered.assets_height, 240.0f));
}

void testViewportPointerInputCapture() {
  KARMA_REQUIRE(!editor::blocksViewportPointerInput({}));
  KARMA_REQUIRE(editor::blocksViewportPointerInput(
      editor::EditorPointerCaptureState{.popup_open = true}));
  KARMA_REQUIRE(editor::blocksViewportPointerInput(
      editor::EditorPointerCaptureState{.drag_drop_active = true}));
  KARMA_REQUIRE(editor::blocksViewportPointerInput(
      editor::EditorPointerCaptureState{.panel_item_active = true,
                                        .viewport_item_hovered = true}));
  KARMA_REQUIRE(editor::blocksViewportPointerInput(
      editor::EditorPointerCaptureState{.want_capture_mouse = true}));
  KARMA_REQUIRE(!editor::blocksViewportPointerInput(
      editor::EditorPointerCaptureState{.want_capture_mouse = true,
                                        .viewport_item_hovered = true}));
  KARMA_REQUIRE(!editor::blocksViewportPointerInput(
      editor::EditorPointerCaptureState{
          .panel_item_active = true,
          .want_capture_mouse = true,
          .viewport_navigation_owned = true}));
  KARMA_REQUIRE(editor::blocksViewportPointerInput(
      editor::EditorPointerCaptureState{
          .popup_open = true,
          .viewport_navigation_owned = true}));
  KARMA_REQUIRE(editor::blocksViewportPointerInput(
      editor::EditorPointerCaptureState{
          .drag_drop_active = true,
          .viewport_navigation_owned = true}));
}

void testSettingsRejectMalformedFieldTypes() {
  const auto root = tempDirectory();
  editor::EditorSettings settings{};
  std::string diagnostic;

  writeText(editor::settingsPath(root), R"({
    "version": 1,
    "asset_roots": ["."],
    "camera_move_speed": "fast"
  })");
  KARMA_REQUIRE(!editor::loadEditorSettings(root, settings, &diagnostic));
  KARMA_REQUIRE(diagnostic.find("camera_move_speed") != std::string::npos);

  writeText(editor::settingsPath(root), R"({
    "version": 1,
    "asset_roots": ["."],
    "snap_enabled": 1
  })");
  KARMA_REQUIRE(!editor::loadEditorSettings(root, settings, &diagnostic));
  KARMA_REQUIRE(diagnostic.find("snap_enabled") != std::string::npos);

  writeText(editor::settingsPath(root), R"({
    "version": 1,
    "asset_roots": ["."],
    "markers_visible": 1
  })");
  KARMA_REQUIRE(!editor::loadEditorSettings(root, settings, &diagnostic));
  KARMA_REQUIRE(diagnostic.find("markers_visible") != std::string::npos);

  writeText(editor::settingsPath(root), R"({
    "version": 1,
    "layout": {"hierarchy_width": "wide"}
  })");
  KARMA_REQUIRE(!editor::loadEditorSettings(root, settings, &diagnostic));
  KARMA_REQUIRE(diagnostic.find("hierarchy_width") != std::string::npos);

  writeText(editor::settingsPath(root), R"({
    "version": 1,
    "layout": {"assets_height": -1}
  })");
  KARMA_REQUIRE(!editor::loadEditorSettings(root, settings, &diagnostic));
  KARMA_REQUIRE(diagnostic.find("assets_height") != std::string::npos);

  writeText(editor::settingsPath(root), R"({
    "version": 1,
    "asset_type_filter": 99
  })");
  KARMA_REQUIRE(!editor::loadEditorSettings(root, settings, &diagnostic));
  KARMA_REQUIRE(diagnostic.find("asset_type_filter") != std::string::npos);

  writeText(editor::settingsPath(root), R"({
    "version": 1,
    "console_min_level": 9
  })");
  KARMA_REQUIRE(!editor::loadEditorSettings(root, settings, &diagnostic));
  KARMA_REQUIRE(diagnostic.find("console_min_level") != std::string::npos);

  writeText(editor::settingsPath(root), R"({
    "version": 1,
    "bottom_panel_tab": "profiler"
  })");
  KARMA_REQUIRE(!editor::loadEditorSettings(root, settings, &diagnostic));
  KARMA_REQUIRE(diagnostic.find("bottom_panel_tab") != std::string::npos);

  writeText(editor::settingsPath(root), R"({
    "version": 1,
    "terrain_material_layer": 4
  })");
  KARMA_REQUIRE(!editor::loadEditorSettings(root, settings, &diagnostic));
  KARMA_REQUIRE(diagnostic.find("terrain_material_layer") != std::string::npos);

  writeText(editor::settingsPath(root), R"({
    "version": 1,
    "viewport_render_mode": 2
  })");
  KARMA_REQUIRE(!editor::loadEditorSettings(root, settings, &diagnostic));
  KARMA_REQUIRE(diagnostic.find("viewport_render_mode") != std::string::npos);

  writeText(editor::settingsPath(root), R"({
    "version": 1,
    "viewport_render_mode": "solid"
  })");
  KARMA_REQUIRE(!editor::loadEditorSettings(root, settings, &diagnostic));
  KARMA_REQUIRE(diagnostic.find("viewport_render_mode") != std::string::npos);

  writeText(editor::settingsPath(root), R"({
    "version": 18446744073709551615,
    "asset_roots": ["."]
  })");
  KARMA_REQUIRE(!editor::loadEditorSettings(root, settings, &diagnostic));
  KARMA_REQUIRE(diagnostic.find("version") != std::string::npos);
  std::filesystem::remove_all(root);
}

void testHistory() {
  karma::scenes::SceneDocument first{};
  first.name = "First";
  karma::scenes::SceneDocument second = first;
  second.name = "Second";
  karma::scenes::SceneDocument third = second;
  third.name = "Third";

  editor::DocumentHistory history;
  history.push("Rename", first, second);
  history.push("Rename again", second, third);
  karma::scenes::SceneDocument current = third;
  KARMA_REQUIRE(history.dirty());
  KARMA_REQUIRE(history.undo(current));
  KARMA_REQUIRE(current.name == "Second");
  KARMA_REQUIRE(history.undo(current));
  KARMA_REQUIRE(current.name == "First");
  KARMA_REQUIRE(history.redo(current));
  KARMA_REQUIRE(current.name == "Second");
  history.markSaved();
  KARMA_REQUIRE(!history.dirty());

  KARMA_REQUIRE(history.redo(current));
  KARMA_REQUIRE(current.name == "Third");
  history.markSaved();
  KARMA_REQUIRE(history.undo(current));
  karma::scenes::SceneDocument fourth = current;
  fourth.name = "Fourth";
  history.push("Branch", current, fourth);
  current = fourth;
  KARMA_REQUIRE(history.dirty());

  history.clear();
  history.markSaved();
  history.setLimits(1u, 1u);
  history.push("Too large", first, second);
  KARMA_REQUIRE(history.dirty());
}

void testComponentEditorRegistryMetadataAndCoverage() {
  const editor::ComponentEditorRegistry registry =
      editor::buildComponentEditorRegistry();
  const auto& serializers =
      karma::prefabs::componentSerializerRegistry().serializers();
  KARMA_REQUIRE(registry.descriptors().size() >= serializers.size());
  for (const karma::prefabs::ComponentSerializer& serializer : serializers) {
    const editor::ComponentEditorDescriptor* descriptor =
        registry.find(serializer.type_name);
    KARMA_REQUIRE(descriptor != nullptr);
    KARMA_REQUIRE(!descriptor->display_name.empty());
    KARMA_REQUIRE(static_cast<bool>(descriptor->default_payload));
    KARMA_REQUIRE(static_cast<bool>(descriptor->validate_payload));
  }

  const auto* transform = registry.find("TransformComponent");
  const auto* rigidbody = registry.find("RigidbodyComponent");
  const auto* material = registry.find("PhysicsMaterialComponent");
  const auto* filter = registry.find("PhysicsCollisionFilterComponent");
  const auto* foliage = registry.find("FoliageComponent");
  const auto* lod = registry.find("LODComponent");
  const auto* instance_set = registry.find("InstanceSetComponent");
  const auto* instanced_mesh = registry.find("InstancedMeshComponent");
  const auto* particles = registry.find("ParticleEffectComponent");
  KARMA_REQUIRE(transform != nullptr && !transform->removable);
  KARMA_REQUIRE(transform->editor == editor::ComponentEditorKind::Transform);
  KARMA_REQUIRE(rigidbody != nullptr);
  KARMA_REQUIRE(rigidbody->category ==
                editor::ComponentEditorCategory::Physics);
  KARMA_REQUIRE(rigidbody->editor == editor::ComponentEditorKind::Rigidbody);
  KARMA_REQUIRE(rigidbody->dependencies ==
                std::vector<std::string>{"ColliderComponent"});
  KARMA_REQUIRE(material != nullptr &&
                material->editor ==
                    editor::ComponentEditorKind::PhysicsMaterial);
  KARMA_REQUIRE(filter != nullptr &&
                filter->editor ==
                    editor::ComponentEditorKind::PhysicsCollisionFilter);
  KARMA_REQUIRE(foliage != nullptr &&
                foliage->creation_policy ==
                    editor::ComponentCreationPolicy::ContextualWorkflow);
  KARMA_REQUIRE(lod != nullptr &&
                lod->category == editor::ComponentEditorCategory::Rendering &&
                lod->editor == editor::ComponentEditorKind::Lod);
  KARMA_REQUIRE(
      lod->one_of_dependencies ==
      (std::vector<std::string>{"MeshComponent", "InstancedMeshComponent",
                                "FoliageComponent"}));
  KARMA_REQUIRE(instance_set != nullptr &&
                instance_set->editor ==
                    editor::ComponentEditorKind::InstanceSet);
  KARMA_REQUIRE(instanced_mesh != nullptr &&
                instanced_mesh->dependencies ==
                    std::vector<std::string>{"InstanceSetComponent"});
  KARMA_REQUIRE(particles != nullptr &&
                particles->creation_policy ==
                    editor::ComponentCreationPolicy::ValidatedJsonDraft);
}

void testLodAndInstanceSetEditorSchemas() {
  const editor::ComponentEditorRegistry registry =
      editor::buildComponentEditorRegistry();
  std::string diagnostic;
  KARMA_REQUIRE(editor::validateLodComponentPayload(
      editor::defaultLodComponentPayload(), &diagnostic));
  KARMA_REQUIRE(editor::validateInstanceSetComponentPayload(
      editor::defaultInstanceSetComponentPayload(), &diagnostic));

  nlohmann::json lod = {
      {"levels",
       nlohmann::json::array({
           {{"start_distance", 25.0f},
            {"mesh_asset_key", "trees/low"},
            {"materials",
             nlohmann::json::array(
                 {{{"slot", 0u}, {"material_key", "trees/bark"}}})},
            {"render_mode", "mesh"},
            {"shadow_visible", true}},
           {{"start_distance", 80.0f},
            {"mesh_asset_key", "trees/billboard"},
            {"materials", nlohmann::json::array()},
            {"render_mode", "upright_billboard"},
            {"shadow_visible", false}},
       })},
  };
  KARMA_REQUIRE(editor::validateLodComponentPayload(lod, &diagnostic));

  nlohmann::json invalid = lod;
  invalid["levels"][1]["start_distance"] = 25.0f;
  KARMA_REQUIRE(!editor::validateLodComponentPayload(invalid, &diagnostic));
  KARMA_REQUIRE(diagnostic.find("strictly increasing") != std::string::npos);
  invalid = lod;
  invalid["levels"][0]["render_mode"] = "impostor";
  KARMA_REQUIRE(!editor::validateLodComponentPayload(invalid, &diagnostic));
  invalid = lod;
  invalid["levels"].push_back(invalid["levels"].back());
  invalid["levels"].push_back(invalid["levels"].back());
  KARMA_REQUIRE(!editor::validateLodComponentPayload(invalid, &diagnostic));

  karma::scenes::SceneEntity entity{.id = "tree", .name = "Tree"};
  const nlohmann::json before = entity.components;
  KARMA_REQUIRE(!editor::addComponentWithDependencies(
      entity, registry, "LODComponent", lod, nullptr, &diagnostic));
  KARMA_REQUIRE(diagnostic.find("compatible source") != std::string::npos);
  KARMA_REQUIRE(entity.components == before);

  const nlohmann::json prefab_foliage = {
      {"sidecar_path", "foliage/tree.kfoliage"},
      {"prefab_path", "prefabs/tree/prefab.json"},
      {"prefab_variables", nlohmann::json::object()},
      {"chunk_size", 32.0f},
      {"view_distance", 256.0f},
      {"max_resident_instances", 100000u},
      {"source_revision", 0u},
      {"visible", true},
      {"shadow_visible", true},
  };
  karma::scenes::SceneEntity prefab_foliage_entity{
      .id = "prefab_foliage",
      .name = "Prefab Foliage",
      .components = {{"FoliageComponent", prefab_foliage}},
  };
  KARMA_REQUIRE(!editor::addComponentWithDependencies(prefab_foliage_entity,
                                                        registry,
                                                        "LODComponent",
                                                        lod,
                                                        nullptr,
                                                        &diagnostic));
  KARMA_REQUIRE(diagnostic.find("direct-mesh Foliage") != std::string::npos);
  KARMA_REQUIRE(
      !prefab_foliage_entity.components.contains("LODComponent"));

  const nlohmann::json direct_mesh_foliage = {
      {"sidecar_path", "foliage/tree.kfoliage"},
      {"mesh_asset_key", "trees/high"},
      {"materials", nlohmann::json::array()},
      {"chunk_size", 32.0f},
      {"view_distance", 256.0f},
      {"max_resident_instances", 100000u},
      {"source_revision", 0u},
      {"visible", true},
      {"shadow_visible", true},
  };
  karma::scenes::SceneEntity direct_foliage_entity{
      .id = "direct_foliage",
      .name = "Direct Mesh Foliage",
      .components = {{"FoliageComponent", direct_mesh_foliage}},
  };
  KARMA_REQUIRE(editor::addComponentWithDependencies(direct_foliage_entity,
                                                       registry,
                                                       "LODComponent",
                                                       lod,
                                                       nullptr,
                                                       &diagnostic));
  const nlohmann::json direct_before_source_switch =
      direct_foliage_entity.components;
  KARMA_REQUIRE(!editor::replaceComponentPayload(direct_foliage_entity,
                                                   registry,
                                                   "FoliageComponent",
                                                   prefab_foliage,
                                                   &diagnostic));
  KARMA_REQUIRE(diagnostic.find("direct-mesh Foliage") != std::string::npos);
  KARMA_REQUIRE(direct_foliage_entity.components ==
                direct_before_source_switch);

  KARMA_REQUIRE(editor::addDefaultComponentWithDependencies(
      entity, registry, "MeshComponent", nullptr, &diagnostic));
  KARMA_REQUIRE(editor::addComponentWithDependencies(
      entity, registry, "LODComponent", lod, nullptr, &diagnostic));
  KARMA_REQUIRE(entity.components["LODComponent"] == lod);
  KARMA_REQUIRE(!editor::removeComponentsTogether(
      entity, registry, {"MeshComponent"}, &diagnostic));
  KARMA_REQUIRE(diagnostic.find("depends") != std::string::npos ||
                diagnostic.find("compatible") != std::string::npos);
  KARMA_REQUIRE(editor::removeComponentsTogether(
      entity, registry, {"LODComponent", "MeshComponent"}, &diagnostic));

  nlohmann::json planar = editor::defaultInstanceSetComponentPayload();
  planar["gpu_layout"] = "position_yaw_scale_params";
  planar["planar_instances"].push_back({
      {"position", nlohmann::json::array({1.0f, 0.0f, 2.0f})},
      {"yaw_radians", 0.5f},
      {"scale", nlohmann::json::array({1.0f, 2.0f, 1.0f})},
      {"params", nlohmann::json::array({0.0f, 1.0f, 0.0f, 0.0f})},
  });
  KARMA_REQUIRE(editor::validateInstanceSetComponentPayload(planar,
                                                             &diagnostic));
  planar["instances"].push_back({
      {"position", nlohmann::json::array({0.0f, 0.0f, 0.0f})},
      {"rotation", nlohmann::json::array({0.0f, 0.0f, 0.0f, 1.0f})},
      {"scale", nlohmann::json::array({1.0f, 1.0f, 1.0f})},
      {"params", nlohmann::json::array({0.0f, 0.0f, 0.0f, 0.0f})},
  });
  KARMA_REQUIRE(!editor::validateInstanceSetComponentPayload(planar,
                                                              &diagnostic));
}

void testFoliagePrefabInspectionEligibility() {
  using karma::components::DeformableMeshComponent;
  using karma::components::InstanceSetComponent;
  using karma::components::InstancedMeshComponent;
  using karma::components::LodComponent;
  using karma::components::LodLevel;
  using karma::components::MeshComponent;
  using karma::components::TransformComponent;

  const std::filesystem::path root = tempDirectory();
  const std::filesystem::path prefab_path = root / "mixed/prefab.json";
  karma::world::World world;
  karma::world::Scene scene;
  const auto add_node = [&](std::string name,
                            std::optional<karma::world::NodeId> parent) {
    const karma::world::Entity entity = world.createEntity();
    world.setName(entity, std::move(name));
    world.add(entity, TransformComponent{});
    const karma::world::NodeId node = scene.createNode(entity);
    if (parent.has_value()) {
      KARMA_REQUIRE(scene.reparent(node, *parent));
    }
    return std::pair{entity, node};
  };

  const auto [rigid, rigid_node] = add_node("Rigid", std::nullopt);
  world.add(rigid, MeshComponent{
                       .mesh_asset_key = "trees/default",
                       .visible = true,
                       .shadow_visible = true,
                   });
  world.add(rigid,
            LodComponent{.levels = {LodLevel{
                             .start_distance = 40.0f,
                             .mesh_asset_key = "trees/billboard",
                         }}});

  const auto [invisible, invisible_node] =
      add_node("Invisible", rigid_node);
  (void)invisible_node;
  world.add(invisible, MeshComponent{
                           .mesh_asset_key = "trees/invisible",
                           .visible = false,
                       });

  const auto [deformable, deformable_node] =
      add_node("Deformable", rigid_node);
  (void)deformable_node;
  world.add(deformable, MeshComponent{
                            .mesh_asset_key = "trees/deformable",
                            .visible = true,
                        });
  world.add(deformable, DeformableMeshComponent{.enabled = true});

  const auto [instance_source, instance_source_node] =
      add_node("Instance Source", rigid_node);
  (void)instance_source_node;
  world.add(instance_source, InstanceSetComponent{});
  const auto [instanced, instanced_node] =
      add_node("Authored Instances", rigid_node);
  (void)instanced_node;
  world.add(instanced, InstancedMeshComponent{
                         .mesh_asset_key = "trees/instanced",
                         .instance_source = instance_source,
                     });

  KARMA_REQUIRE(karma::prefabs::savePrefab(
      world, scene, rigid, prefab_path));
  nlohmann::json prefab = nlohmann::json::parse(readText(prefab_path));
  prefab["variables"] = {
      {"rigid_mesh", {{"type", "string"},
                       {"default", "trees/default"}}},
  };
  for (nlohmann::json& node : prefab["nodes"]) {
    if (node.value("name", std::string{}) == "Rigid") {
      node["components"]["MeshComponent"]["mesh_asset_key"] = {
          {"$var", "rigid_mesh"},
      };
    }
  }
  writeText(prefab_path, prefab.dump(2));

  editor::FoliagePrefabInspector inspector;
  const editor::FoliagePrefabInspection inspected = inspector.inspect(
      prefab_path, {{"rigid_mesh", "trees/override"}});
  KARMA_REQUIRE(inspected.inspected());
  KARMA_REQUIRE(inspected.paintable());
  KARMA_REQUIRE(inspected.eligible_rigid_meshes == 1u);
  const auto find_renderer = [&](std::string_view node_name,
                                 editor::FoliagePrefabRendererDisposition
                                     disposition) {
    return std::find_if(
        inspected.renderers.begin(), inspected.renderers.end(),
        [&](const editor::FoliagePrefabRendererSummary& renderer) {
          return renderer.node_name == node_name &&
                 renderer.disposition == disposition;
        });
  };
  const auto painted = find_renderer(
      "Rigid",
      editor::FoliagePrefabRendererDisposition::PaintedRigidMesh);
  KARMA_REQUIRE(painted != inspected.renderers.end());
  KARMA_REQUIRE(painted->mesh_asset_key == "trees/override");
  KARMA_REQUIRE(painted->lod_level_count == 1u);
  KARMA_REQUIRE(find_renderer(
                    "Invisible",
                    editor::FoliagePrefabRendererDisposition::
                        IgnoredInvisibleMesh) != inspected.renderers.end());
  KARMA_REQUIRE(find_renderer(
                    "Deformable",
                    editor::FoliagePrefabRendererDisposition::
                        IgnoredDeformableMesh) != inspected.renderers.end());
  KARMA_REQUIRE(find_renderer(
                    "Authored Instances",
                    editor::FoliagePrefabRendererDisposition::
                        IgnoredInstancedMesh) != inspected.renderers.end());

  const editor::FoliagePrefabInspection changed_override = inspector.inspect(
      prefab_path, {{"rigid_mesh", "trees/second_override"}});
  const auto changed_painted = std::find_if(
      changed_override.renderers.begin(), changed_override.renderers.end(),
      [](const editor::FoliagePrefabRendererSummary& renderer) {
        return renderer.disposition ==
               editor::FoliagePrefabRendererDisposition::PaintedRigidMesh;
      });
  KARMA_REQUIRE(changed_painted != changed_override.renderers.end());
  KARMA_REQUIRE(changed_painted->mesh_asset_key ==
                "trees/second_override");
  std::string diagnostic;
  KARMA_REQUIRE(inspector.validate(prefab_path,
                                   {{"rigid_mesh", "trees/valid"}},
                                   &diagnostic));
  KARMA_REQUIRE(diagnostic.empty());

  const std::filesystem::path ignored_path = root / "ignored/prefab.json";
  for (nlohmann::json& node : prefab["nodes"]) {
    if (node.value("name", std::string{}) == "Rigid") {
      node["components"]["MeshComponent"]["visible"] = false;
    }
  }
  writeText(ignored_path, prefab.dump(2));
  KARMA_REQUIRE(!inspector.validate(ignored_path,
                                    nlohmann::json::object(),
                                    &diagnostic));
  KARMA_REQUIRE(diagnostic.find("visible rigid MeshComponent") !=
                std::string::npos);
  const editor::FoliagePrefabInspection ignored =
      inspector.inspect(ignored_path);
  KARMA_REQUIRE(ignored.inspected());
  KARMA_REQUIRE(!ignored.paintable());
  KARMA_REQUIRE(ignored.eligible_rigid_meshes == 0u);

  std::filesystem::remove_all(root);
}

void testFocusedPrefabAssetDraftTransactions() {
  const editor::ComponentEditorRegistry registry =
      editor::buildComponentEditorRegistry();
  const std::filesystem::path root = tempDirectory();
  const std::filesystem::path path = root / "tree/prefab.json";
  const nlohmann::json mesh_payload =
      registry.find("MeshComponent")->default_payload();
  nlohmann::json source = {
      {"version", 2u},
      {"asset_package", "assets.package.json"},
      {"root", 0u},
      {"nodes",
       nlohmann::json::array({
           {{"id", 0u},
            {"name", "Tree"},
            {"parent", nullptr},
            {"components", {{"MeshComponent", mesh_payload}}}},
       })},
  };
  writeText(path, source.dump(2));

  std::string diagnostic;
  std::optional<editor::PrefabAssetDraft> opened =
      editor::openPrefabAssetDraft(path, &diagnostic);
  KARMA_REQUIRE(opened.has_value());
  editor::PrefabAssetDraft& draft = *opened;
  KARMA_REQUIRE(!draft.dirty());
  KARMA_REQUIRE(!draft.sourceChangedExternally());

  nlohmann::json lod = {
      {"levels",
       nlohmann::json::array({
           {{"start_distance", 35.0f},
            {"mesh_asset_key", "trees/low"},
            {"materials", nlohmann::json::array()},
            {"render_mode", "mesh"},
            {"shadow_visible", true}},
       })},
  };
  KARMA_REQUIRE(draft.setNodeComponent(
      0u, "LODComponent", lod, registry, "Add LOD", &diagnostic));
  KARMA_REQUIRE(draft.dirty());
  KARMA_REQUIRE(draft.canUndo());
  KARMA_REQUIRE(draft.undoLabel() == "Add LOD");
  KARMA_REQUIRE(draft.undo());
  KARMA_REQUIRE(!draft.document().nodes[0].components.contains("LODComponent"));
  KARMA_REQUIRE(!draft.dirty());
  KARMA_REQUIRE(draft.redo());
  KARMA_REQUIRE(draft.document().nodes[0].components["LODComponent"] == lod);

  const nlohmann::json before_invalid =
      draft.document().nodes[0].components;
  nlohmann::json invalid_lod = lod;
  invalid_lod["levels"][0]["start_distance"] = -1.0f;
  KARMA_REQUIRE(!draft.setNodeComponent(
      0u, "LODComponent", invalid_lod, registry, "Invalid", &diagnostic));
  KARMA_REQUIRE(draft.document().nodes[0].components == before_invalid);
  KARMA_REQUIRE(!draft.removeNodeComponent(
      0u, "MeshComponent", registry, "Remove Mesh", &diagnostic));
  KARMA_REQUIRE(draft.document().nodes[0].components.contains("MeshComponent"));

  KARMA_REQUIRE(draft.save(registry, &diagnostic));
  KARMA_REQUIRE(!draft.dirty());
  std::ifstream saved_stream(path);
  nlohmann::json saved;
  saved_stream >> saved;
  KARMA_REQUIRE(saved["asset_package"] == "assets.package.json");
  KARMA_REQUIRE(saved["nodes"][0]["components"]["LODComponent"] == lod);

  nlohmann::json changed_lod = lod;
  changed_lod["levels"][0]["start_distance"] = 45.0f;
  KARMA_REQUIRE(draft.setNodeComponent(
      0u, "LODComponent", changed_lod, registry, "Move LOD", &diagnostic));
  saved["nodes"][0]["name"] = "Externally Changed";
  writeText(path, saved.dump(2));
  KARMA_REQUIRE(draft.sourceChangedExternally());
  KARMA_REQUIRE(!draft.save(registry, &diagnostic));
  KARMA_REQUIRE(diagnostic.find("externally") != std::string::npos);
  KARMA_REQUIRE(draft.revert(&diagnostic));
  KARMA_REQUIRE(!draft.dirty());
  KARMA_REQUIRE(draft.document().nodes[0].name == "Externally Changed");
  std::filesystem::remove_all(root);
}

void testExplicitLegacyRenderMigrationReporting() {
  const nlohmann::json legacy_instanced = {
      {"mesh_asset_key", "trees/high"},
      {"materials", nlohmann::json::array()},
      {"lods",
       nlohmann::json::array({
           {{"start_distance", 35.0f},
            {"mesh_asset_key", "trees/low"},
            {"materials", nlohmann::json::array()},
            {"render_mode", "mesh"},
            {"shadow_visible", true}},
       })},
      {"gpu_layout", "matrix4x4_params"},
      {"instances", nlohmann::json::array()},
      {"planar_instances", nlohmann::json::array()},
      {"instance_revision", 3u},
      {"dynamic", false},
      {"visible", true},
      {"shadow_visible", true},
  };

  karma::scenes::SceneDocument scene{};
  scene.entities.push_back({
      .id = "trees",
      .name = "Trees",
      .components = {{"InstancedMeshComponent", legacy_instanced}},
  });
  const karma::scenes::SceneDocument raw_scene = scene;
  const editor::LegacyRenderMigrationReport scene_result =
      editor::migrateSceneLegacyRenderComponents(scene);
  KARMA_REQUIRE(scene_result.success());
  KARMA_REQUIRE(scene_result.changed);
  KARMA_REQUIRE(scene_result.migrated_owners == 1u);
  KARMA_REQUIRE(
      scene.entities[0].components.contains("InstanceSetComponent"));
  KARMA_REQUIRE(scene.entities[0].components.contains("LODComponent"));
  KARMA_REQUIRE(!scene.entities[0]
                     .components["InstancedMeshComponent"]
                     .contains("lods"));

  const std::filesystem::path root = tempDirectory();
  const std::filesystem::path scene_path = root / "world.kscene.json";
  const std::string original_scene_bytes =
      karma::scenes::sceneDocumentToJson(raw_scene).dump(2);
  writeText(scene_path, original_scene_bytes);
  const editor::LegacyRenderMigrationReport scene_file_result =
      editor::migrateSceneFileLegacyRenderComponents(scene_path, root);
  KARMA_REQUIRE(scene_file_result.success());
  KARMA_REQUIRE(scene_file_result.changed);
  const std::filesystem::path scene_backup =
      scene_path.string() + ".pre-lod-component.bak";
  KARMA_REQUIRE(std::filesystem::exists(scene_backup));
  KARMA_REQUIRE(readText(scene_backup) == original_scene_bytes);
  KARMA_REQUIRE(karma::scenes::loadSceneDocument(
                    karma::scenes::SceneLoadDesc{
                        .path = scene_path,
                        .reference_root = root,
                    })
                    .success());
  const editor::LegacyRenderMigrationReport scene_file_second =
      editor::migrateSceneFileLegacyRenderComponents(scene_path, root);
  KARMA_REQUIRE(scene_file_second.success());
  KARMA_REQUIRE(!scene_file_second.changed);

  const std::filesystem::path path = root / "legacy/prefab.json";
  nlohmann::json prefab = {
      {"version", 2u},
      {"asset_package", "assets.package.json"},
      {"root", 0u},
      {"nodes",
       nlohmann::json::array({
           {{"id", 0u},
            {"name", "Trees"},
            {"parent", nullptr},
            {"components", {{"InstancedMeshComponent", legacy_instanced}}}},
       })},
  };
  const std::string original_prefab_bytes = prefab.dump(2);
  writeText(path, original_prefab_bytes);
  const editor::LegacyRenderMigrationReport prefab_result =
      editor::migratePrefabLegacyRenderComponents(path);
  KARMA_REQUIRE(prefab_result.success());
  KARMA_REQUIRE(prefab_result.changed);
  KARMA_REQUIRE(prefab_result.migrated_owners == 1u);
  const std::filesystem::path prefab_backup =
      path.string() + ".pre-lod-component.bak";
  KARMA_REQUIRE(std::filesystem::exists(prefab_backup));
  KARMA_REQUIRE(readText(prefab_backup) == original_prefab_bytes);
  std::ifstream migrated_stream(path);
  nlohmann::json migrated;
  migrated_stream >> migrated;
  KARMA_REQUIRE(migrated["asset_package"] == "assets.package.json");
  KARMA_REQUIRE(migrated["nodes"][0]["components"].contains(
      "InstanceSetComponent"));
  KARMA_REQUIRE(
      karma::prefabs::loadPrefabDocument(path).success());
  const editor::LegacyRenderMigrationReport second =
      editor::migratePrefabLegacyRenderComponents(path);
  KARMA_REQUIRE(second.success());
  KARMA_REQUIRE(!second.changed);
  std::filesystem::remove_all(root);
}

void testLegacyRenderMigrationFollowsNestedFoliagePrefabs() {
  const std::filesystem::path root = tempDirectory();
  const std::filesystem::path root_prefab = root / "root/prefab.json";
  const std::filesystem::path nested_prefab =
      root / "root/nested/prefab.json";
  const auto foliage_payload = [](std::string prefab_path,
                                  std::string sidecar_path) {
    return nlohmann::json{
        {"sidecar_path", std::move(sidecar_path)},
        {"prefab_path", std::move(prefab_path)},
        {"prefab_variables", nlohmann::json::object()},
        {"chunk_size", 32.0f},
        {"view_distance", 256.0f},
        {"max_resident_instances", 100000u},
        {"source_revision", 0u},
        {"visible", true},
        {"shadow_visible", true},
    };
  };
  const nlohmann::json legacy_instanced = {
      {"mesh_asset_key", "trees/high"},
      {"materials", nlohmann::json::array()},
      {"lods",
       nlohmann::json::array({
           {{"start_distance", 35.0f},
            {"mesh_asset_key", "trees/low"},
            {"materials", nlohmann::json::array()},
            {"render_mode", "mesh"},
            {"shadow_visible", true}},
       })},
      {"gpu_layout", "matrix4x4_params"},
      {"instances", nlohmann::json::array()},
      {"planar_instances", nlohmann::json::array()},
      {"instance_revision", 1u},
      {"dynamic", false},
      {"visible", true},
      {"shadow_visible", true},
  };

  const nlohmann::json root_document = {
      {"version", 2u},
      {"root", 0u},
      {"nodes",
       nlohmann::json::array({
           {{"id", 0u},
            {"name", "Paint Root"},
            {"parent", nullptr},
            {"components",
             {{"FoliageComponent",
               foliage_payload("nested/prefab.json",
                               "foliage/root.kfoliage")}}}},
       })},
  };
  const nlohmann::json nested_document = {
      {"version", 2u},
      {"root", 0u},
      {"nodes",
       nlohmann::json::array({
           {{"id", 0u},
            {"name", "Legacy Tree"},
            {"parent", nullptr},
            {"components", {{"InstancedMeshComponent", legacy_instanced}}}},
           {{"id", 1u},
            {"name", "Cycle Back To Root"},
            {"parent", 0u},
            {"components",
             {{"FoliageComponent",
               foliage_payload("prefab.json",
                               "foliage/cycle.kfoliage")}}}},
       })},
  };
  writeText(root_prefab, root_document.dump(2));
  const std::string original_nested_bytes = nested_document.dump(2);
  writeText(nested_prefab, original_nested_bytes);

  const editor::LegacyRenderMigrationReport result =
      editor::migratePrefabSourceClosure({"root/prefab.json"}, root);
  KARMA_REQUIRE(result.success());
  KARMA_REQUIRE(result.changed);
  KARMA_REQUIRE(result.migrated_owners == 1u);
  KARMA_REQUIRE(!std::filesystem::exists(
      root_prefab.string() + ".pre-lod-component.bak"));
  const std::filesystem::path nested_backup =
      nested_prefab.string() + ".pre-lod-component.bak";
  KARMA_REQUIRE(std::filesystem::exists(nested_backup));
  KARMA_REQUIRE(readText(nested_backup) == original_nested_bytes);

  std::ifstream migrated_stream(nested_prefab);
  nlohmann::json migrated;
  migrated_stream >> migrated;
  const nlohmann::json& migrated_components =
      migrated["nodes"][0]["components"];
  KARMA_REQUIRE(migrated_components.contains("InstanceSetComponent"));
  KARMA_REQUIRE(migrated_components.contains("LODComponent"));
  KARMA_REQUIRE(!migrated_components["InstancedMeshComponent"].contains(
      "lods"));

  const editor::LegacyRenderMigrationReport second =
      editor::migratePrefabSourceClosure({"root/prefab.json"}, root);
  KARMA_REQUIRE(second.success());
  KARMA_REQUIRE(!second.changed);
  std::filesystem::remove_all(root);
}

void testLegacyRenderMigrationFilesystemSafety() {
  const nlohmann::json legacy_instanced = {
      {"mesh_asset_key", "trees/high"},
      {"materials", nlohmann::json::array()},
      {"lods",
       nlohmann::json::array({
           {{"start_distance", 35.0f},
            {"mesh_asset_key", "trees/low"},
            {"materials", nlohmann::json::array()},
            {"render_mode", "mesh"},
            {"shadow_visible", true}},
       })},
      {"gpu_layout", "matrix4x4_params"},
      {"instances", nlohmann::json::array()},
      {"planar_instances", nlohmann::json::array()},
      {"instance_revision", 3u},
      {"dynamic", false},
      {"visible", true},
      {"shadow_visible", true},
  };
  const auto scene_bytes = [](const nlohmann::json& components) {
    karma::scenes::SceneDocument scene{};
    scene.entities.push_back({
        .id = "trees",
        .name = "Trees",
        .components = components,
    });
    return karma::scenes::sceneDocumentToJson(scene).dump(2);
  };
  const auto prefab_bytes = [](const nlohmann::json& components) {
    return nlohmann::json{
        {"version", 2u},
        {"root", 0u},
        {"nodes",
         nlohmann::json::array({
             {{"id", 0u},
              {"name", "Trees"},
              {"parent", nullptr},
              {"components", components}},
         })},
    }.dump(2);
  };
  const auto backup_path = [](const std::filesystem::path& path) {
    return std::filesystem::path(path.string() +
                                 ".pre-lod-component.bak");
  };
  const auto require_unchanged_without_backup =
      [&](const std::filesystem::path& path,
          const std::string& original_bytes) {
        KARMA_REQUIRE(readText(path) == original_bytes);
        KARMA_REQUIRE(!std::filesystem::exists(backup_path(path)));
      };

  const std::filesystem::path root = tempDirectory();
  const nlohmann::json legacy_components = {
      {"InstancedMeshComponent", legacy_instanced},
  };

  {
    const std::filesystem::path path =
        root / "existing-scene-backup.kscene.json";
    const std::string original_bytes = scene_bytes(legacy_components);
    const std::string existing_backup_bytes =
        "existing scene migration backup\n";
    writeText(path, original_bytes);
    writeText(backup_path(path), existing_backup_bytes);
    const editor::LegacyRenderMigrationReport result =
        editor::migrateSceneFileLegacyRenderComponents(path, root);
    KARMA_REQUIRE(result.success());
    KARMA_REQUIRE(result.changed);
    KARMA_REQUIRE(readText(path) != original_bytes);
    KARMA_REQUIRE(readText(backup_path(path)) == existing_backup_bytes);
  }

  {
    const std::filesystem::path path =
        root / "existing-prefab-backup/prefab.json";
    const std::string original_bytes = prefab_bytes(legacy_components);
    const std::string existing_backup_bytes =
        "existing prefab migration backup\n";
    writeText(path, original_bytes);
    writeText(backup_path(path), existing_backup_bytes);
    const editor::LegacyRenderMigrationReport result =
        editor::migratePrefabLegacyRenderComponents(path);
    KARMA_REQUIRE(result.success());
    KARMA_REQUIRE(result.changed);
    KARMA_REQUIRE(readText(path) != original_bytes);
    KARMA_REQUIRE(readText(backup_path(path)) == existing_backup_bytes);
  }

  {
    nlohmann::json conflicting_components = legacy_components;
    conflicting_components["InstanceSetComponent"] =
        nlohmann::json::object();
    const std::filesystem::path path =
        root / "conflicting-instance-set.kscene.json";
    const std::string original_bytes = scene_bytes(conflicting_components);
    writeText(path, original_bytes);
    const editor::LegacyRenderMigrationReport result =
        editor::migrateSceneFileLegacyRenderComponents(path, root);
    KARMA_REQUIRE(!result.success());
    KARMA_REQUIRE(!result.changed);
    KARMA_REQUIRE(!result.diagnostics.empty());
    require_unchanged_without_backup(path, original_bytes);
  }

  {
    nlohmann::json conflicting_components = legacy_components;
    conflicting_components["LODComponent"] = {
        {"levels", nlohmann::json::array()},
    };
    const std::filesystem::path path =
        root / "conflicting-lod/prefab.json";
    const std::string original_bytes = prefab_bytes(conflicting_components);
    writeText(path, original_bytes);
    const editor::LegacyRenderMigrationReport result =
        editor::migratePrefabLegacyRenderComponents(path);
    KARMA_REQUIRE(!result.success());
    KARMA_REQUIRE(!result.changed);
    KARMA_REQUIRE(!result.diagnostics.empty());
    require_unchanged_without_backup(path, original_bytes);
  }

  nlohmann::json invalid_legacy_instanced = legacy_instanced;
  invalid_legacy_instanced["lods"][0]["start_distance"] = -1.0f;
  const nlohmann::json invalid_components = {
      {"InstancedMeshComponent", invalid_legacy_instanced},
  };
  {
    const std::filesystem::path path =
        root / "invalid-migrated.kscene.json";
    const std::string original_bytes = scene_bytes(invalid_components);
    writeText(path, original_bytes);
    const editor::LegacyRenderMigrationReport result =
        editor::migrateSceneFileLegacyRenderComponents(path, root);
    KARMA_REQUIRE(!result.success());
    KARMA_REQUIRE(!result.changed);
    KARMA_REQUIRE(!result.diagnostics.empty());
    require_unchanged_without_backup(path, original_bytes);
  }
  {
    const std::filesystem::path path =
        root / "invalid-migrated/prefab.json";
    const std::string original_bytes = prefab_bytes(invalid_components);
    writeText(path, original_bytes);
    const editor::LegacyRenderMigrationReport result =
        editor::migratePrefabLegacyRenderComponents(path);
    KARMA_REQUIRE(!result.success());
    KARMA_REQUIRE(!result.changed);
    KARMA_REQUIRE(!result.diagnostics.empty());
    require_unchanged_without_backup(path, original_bytes);
  }

  {
    const std::filesystem::path path = root / "malformed.kscene.json";
    const std::string original_bytes = "{ malformed migration source\n";
    writeText(path, original_bytes);
    const editor::LegacyRenderMigrationReport result =
        editor::migrateSceneFileLegacyRenderComponents(path, root);
    KARMA_REQUIRE(!result.success());
    KARMA_REQUIRE(!result.changed);
    require_unchanged_without_backup(path, original_bytes);
  }

  {
    const std::filesystem::path path = root / "read-only.kscene.json";
    const std::string original_bytes = scene_bytes(legacy_components);
    writeText(path, original_bytes);
    std::error_code permission_error;
    const std::filesystem::perms original_permissions =
        std::filesystem::status(path, permission_error).permissions();
    if (!permission_error) {
      constexpr std::filesystem::perms writable =
          std::filesystem::perms::owner_write |
          std::filesystem::perms::group_write |
          std::filesystem::perms::others_write;
      std::filesystem::permissions(path,
                                   writable,
                                   std::filesystem::perm_options::remove,
                                   permission_error);
      const std::filesystem::file_status read_only_status =
          std::filesystem::status(path, permission_error);
      if (!permission_error &&
          (read_only_status.permissions() & writable) ==
              std::filesystem::perms::none) {
        const editor::LegacyRenderMigrationReport result =
            editor::migrateSceneFileLegacyRenderComponents(path, root);
        KARMA_REQUIRE(!result.success());
        KARMA_REQUIRE(!result.changed);
        KARMA_REQUIRE(std::any_of(
            result.diagnostics.begin(),
            result.diagnostics.end(),
            [](const std::string& diagnostic) {
              return diagnostic.find("read-only") != std::string::npos;
            }));
        require_unchanged_without_backup(path, original_bytes);
      }
      std::error_code restore_error;
      std::filesystem::permissions(path,
                                   original_permissions,
                                   std::filesystem::perm_options::replace,
                                   restore_error);
      KARMA_REQUIRE(!restore_error);
    }
  }

  {
    const std::filesystem::path path =
        root / "blocked-replacement/prefab.json";
    const std::string original_bytes = prefab_bytes(legacy_components);
    writeText(path, original_bytes);
    std::filesystem::create_directories(path.string() + ".tmp");
    const editor::LegacyRenderMigrationReport result =
        editor::migratePrefabLegacyRenderComponents(path);
    KARMA_REQUIRE(!result.success());
    KARMA_REQUIRE(!result.changed);
    KARMA_REQUIRE(readText(path) == original_bytes);
    KARMA_REQUIRE(std::filesystem::exists(backup_path(path)));
    KARMA_REQUIRE(readText(backup_path(path)) == original_bytes);
  }

  std::filesystem::remove_all(root);
}

void testComponentDependencyTransactionsAndDuplicatePrevention() {
  const editor::ComponentEditorRegistry registry =
      editor::buildComponentEditorRegistry();
  karma::scenes::SceneEntity entity{.id = "physics", .name = "Physics"};
  std::vector<std::string> added;
  std::string diagnostic;

  KARMA_REQUIRE(editor::addDefaultComponentWithDependencies(
      entity,
      registry,
      "RigidbodyComponent",
      &added,
      &diagnostic));
  KARMA_REQUIRE(added ==
                (std::vector<std::string>{"ColliderComponent",
                                          "RigidbodyComponent"}));
  KARMA_REQUIRE(entity.components.contains("ColliderComponent"));
  KARMA_REQUIRE(entity.components.contains("RigidbodyComponent"));
  KARMA_REQUIRE(entity.components["ColliderComponent"]["type"] == "box");
  KARMA_REQUIRE(editor::validateComponentPayload(
      registry,
      "RigidbodyComponent",
      entity.components["RigidbodyComponent"],
      &diagnostic));

  const nlohmann::json after_first_add = entity.components;
  KARMA_REQUIRE(!editor::addDefaultComponentWithDependencies(
      entity,
      registry,
      "RigidbodyComponent",
      &added,
      &diagnostic));
  KARMA_REQUIRE(diagnostic.find("already has") != std::string::npos);
  KARMA_REQUIRE(entity.components == after_first_add);
  KARMA_REQUIRE(added.empty());

  KARMA_REQUIRE(editor::addDefaultComponentWithDependencies(
      entity,
      registry,
      "CharacterControllerComponent",
      &added,
      &diagnostic));
  KARMA_REQUIRE(added ==
                std::vector<std::string>{"CharacterControllerComponent"});
  const auto blockers = editor::componentRemovalBlockers(
      entity, registry, "ColliderComponent");
  KARMA_REQUIRE(blockers ==
                (std::vector<std::string>{"CharacterControllerComponent",
                                          "RigidbodyComponent"}));

  const nlohmann::json before_blocked_remove = entity.components;
  KARMA_REQUIRE(!editor::removeComponentsTogether(
      entity, registry, {"ColliderComponent"}, &diagnostic));
  KARMA_REQUIRE(diagnostic.find("depends") != std::string::npos);
  KARMA_REQUIRE(entity.components == before_blocked_remove);
  KARMA_REQUIRE(!editor::removeComponentsTogether(
      entity,
      registry,
      {"ColliderComponent", "RigidbodyComponent"},
      &diagnostic));
  KARMA_REQUIRE(entity.components == before_blocked_remove);

  KARMA_REQUIRE(editor::removeComponentsTogether(
      entity,
      registry,
      {"ColliderComponent",
       "RigidbodyComponent",
       "CharacterControllerComponent"},
      &diagnostic));
  KARMA_REQUIRE(entity.components.empty());
}

void testComponentJsonValidationIsTransactional() {
  const editor::ComponentEditorRegistry registry =
      editor::buildComponentEditorRegistry();
  karma::scenes::SceneEntity entity{.id = "json", .name = "JSON"};
  std::string diagnostic;

  KARMA_REQUIRE(editor::addDefaultComponentWithDependencies(
      entity,
      registry,
      "PhysicsMaterialComponent",
      nullptr,
      &diagnostic));
  const nlohmann::json material_before =
      entity.components["PhysicsMaterialComponent"];
  nlohmann::json invalid_material = material_before;
  invalid_material["restitution"] = 1.5f;
  KARMA_REQUIRE(!editor::replaceComponentPayload(
      entity,
      registry,
      "PhysicsMaterialComponent",
      invalid_material,
      &diagnostic));
  KARMA_REQUIRE(entity.components["PhysicsMaterialComponent"] ==
                material_before);

  nlohmann::json valid_material = material_before;
  valid_material["friction"] = 0.65f;
  valid_material["restitution"] = 0.25f;
  KARMA_REQUIRE(editor::replaceComponentPayload(
      entity,
      registry,
      "PhysicsMaterialComponent",
      valid_material,
      &diagnostic));
  KARMA_REQUIRE(entity.components["PhysicsMaterialComponent"] ==
                valid_material);

  const auto* particle = registry.find("ParticleEffectComponent");
  KARMA_REQUIRE(particle != nullptr);
  KARMA_REQUIRE(!editor::addDefaultComponentWithDependencies(
      entity,
      registry,
      "ParticleEffectComponent",
      nullptr,
      &diagnostic));
  KARMA_REQUIRE(!entity.components.contains("ParticleEffectComponent"));
  const nlohmann::json particle_draft = particle->default_payload();
  KARMA_REQUIRE(editor::validateComponentPayload(
      registry, "ParticleEffectComponent", particle_draft, &diagnostic));
  KARMA_REQUIRE(editor::addComponentWithDependencies(
      entity,
      registry,
      "ParticleEffectComponent",
      particle_draft,
      nullptr,
      &diagnostic));

  karma::scenes::SceneEntity controller_entity{
      .id = "controller", .name = "Controller"};
  KARMA_REQUIRE(editor::addDefaultComponentWithDependencies(
      controller_entity,
      registry,
      "CharacterControllerComponent",
      nullptr,
      &diagnostic));
  const nlohmann::json controller_before = controller_entity.components;
  nlohmann::json sphere =
      registry.find("ColliderComponent")->default_payload();
  sphere["type"] = "sphere";
  sphere["shape"] = {
      {"center", nlohmann::json::array({0.0f, 0.0f, 0.0f})},
      {"radius", 0.5f},
  };
  KARMA_REQUIRE(!editor::replaceComponentPayload(controller_entity,
                                                   registry,
                                                   "ColliderComponent",
                                                   sphere,
                                                   &diagnostic));
  KARMA_REQUIRE(controller_entity.components == controller_before);

  KARMA_REQUIRE(!editor::addDefaultComponentWithDependencies(
      entity, registry, "FoliageComponent", nullptr, &diagnostic));
  KARMA_REQUIRE(diagnostic.find("contextual") != std::string::npos);
  KARMA_REQUIRE(!entity.components.contains("FoliageComponent"));
}

karma::scenes::SceneDocument hierarchyDocument() {
  karma::scenes::SceneDocument document{};
  document.name = "Hierarchy";
  document.entities = {
      {.id = "root",
       .name = "Root",
       .transform = {.position = {10.0f, 0.0f, 0.0f},
                     .scale = {2.0f, 2.0f, 2.0f}}},
      {.id = "left",
       .name = "Left",
       .parent_id = "root",
       .transform = {.position = {1.0f, 0.0f, 0.0f},
                     .scale = {2.0f, 2.0f, 2.0f}}},
      {.id = "right",
       .name = "Right",
       .parent_id = "root",
       .transform = {.position = {-2.0f, 0.0f, 0.0f}}},
      {.id = "child",
       .name = "Child",
       .parent_id = "left",
       .transform = {.position = {1.0f, 0.0f, 0.0f}}},
  };
  document.prefab_instances.push_back(karma::scenes::ScenePrefabInstance{
      .id = "prefab",
      .prefab_path = "prefabs/tree/prefab.json",
      .parent_entity_id = "child",
      .transform = {.position = {0.0f, 3.0f, 0.0f}},
  });
  karma::components::LightComponent light{};
  light.type = karma::components::LightComponent::Type::Point;
  document.lights.push_back(karma::scenes::SceneLight{
      .id = "light", .entity_id = "child", .component = light});
  return document;
}

size_t hierarchyItemCount(const std::vector<editor::HierarchyNode>& nodes) {
  size_t count = 0u;
  for (const editor::HierarchyNode& node : nodes) {
    count += 1u + hierarchyItemCount(node.children);
  }
  return count;
}

void testHierarchyBuildAndCycleHandling() {
  const karma::scenes::SceneDocument document = hierarchyDocument();
  const editor::HierarchyBuildResult hierarchy = editor::buildHierarchy(document);
  KARMA_REQUIRE(hierarchy.success());
  KARMA_REQUIRE(hierarchy.roots.size() == 1u);
  KARMA_REQUIRE(hierarchy.roots[0].item.id == "root");
  KARMA_REQUIRE(hierarchy.roots[0].children.size() == 2u);
  KARMA_REQUIRE(hierarchy.roots[0].children[0].item.id == "left");
  KARMA_REQUIRE(hierarchy.roots[0].children[0].children[0].item.id == "child");
  KARMA_REQUIRE(hierarchy.roots[0]
                    .children[0]
                    .children[0]
                    .children[0]
                    .item.kind == editor::SelectionKind::Prefab);
  KARMA_REQUIRE(hierarchyItemCount(hierarchy.roots) == 5u);

  karma::scenes::SceneDocument cyclic = document;
  cyclic.entities[1].parent_id = "child";
  const editor::HierarchyBuildResult guarded = editor::buildHierarchy(cyclic);
  KARMA_REQUIRE(!guarded.success());
  KARMA_REQUIRE(std::any_of(guarded.diagnostics.begin(), guarded.diagnostics.end(),
                            [](const std::string& diagnostic) {
                              return diagnostic.find("cycle") != std::string::npos;
                            }));
  KARMA_REQUIRE(hierarchyItemCount(guarded.roots) == 5u);
  std::string diagnostic;
  KARMA_REQUIRE(!editor::sceneWorldTransform(
      cyclic,
      {editor::SelectionKind::Entity, "left"},
      &diagnostic));
  KARMA_REQUIRE(diagnostic.find("cycle") != std::string::npos);
}

void testFoliageHierarchyProjectionIsPresentationOnly() {
  karma::scenes::SceneDocument document{};
  document.entities = {
      {.id = "root", .name = "Root"},
      {.id = "group", .name = "Group", .parent_id = "root"},
      {.id = "terrain", .name = "Terrain", .parent_id = "root"},
      {.id = "grass", .name = "Grass", .parent_id = "group"},
      {.id = "tree", .name = "Trees", .parent_id = "root"},
  };
  const karma::scenes::SceneDocument before = document;
  const editor::HierarchyBuildResult projected =
      editor::projectFoliageUnderTerrain(
          editor::buildHierarchy(document), "terrain", {"grass", "tree"});
  KARMA_REQUIRE(projected.success());
  KARMA_REQUIRE(hierarchyItemCount(projected.roots) == 5u);
  const auto find_node = [&](const auto& self,
                             const std::vector<editor::HierarchyNode>& nodes,
                             const std::string& id)
      -> const editor::HierarchyNode* {
    for (const editor::HierarchyNode& node : nodes) {
      if (node.item.id == id) return &node;
      if (const editor::HierarchyNode* child = self(self, node.children, id)) {
        return child;
      }
    }
    return nullptr;
  };
  const editor::HierarchyNode* terrain =
      find_node(find_node, projected.roots, "terrain");
  KARMA_REQUIRE(terrain != nullptr);
  KARMA_REQUIRE(terrain->children.size() == 2u);
  KARMA_REQUIRE(terrain->children[0].item.id == "grass");
  KARMA_REQUIRE(terrain->children[1].item.id == "tree");
  KARMA_REQUIRE(document.entities[3].parent_id == "group");
  KARMA_REQUIRE(document.entities[4].parent_id == "root");
  KARMA_REQUIRE(karma::scenes::sceneDocumentToJson(document) ==
                karma::scenes::sceneDocumentToJson(before));

  const editor::HierarchyBuildResult missing_terrain =
      editor::projectFoliageUnderTerrain(
          editor::buildHierarchy(document), "missing", {"grass"});
  const editor::HierarchyNode* group =
      find_node(find_node, missing_terrain.roots, "group");
  KARMA_REQUIRE(group != nullptr);
  KARMA_REQUIRE(group->children.size() == 1u);
  KARMA_REQUIRE(group->children.front().item.id == "grass");
}

void testStaticGroupsRemainPrefabPlacementParents() {
  karma::scenes::SceneDocument document{};
  document.entities.push_back(karma::scenes::SceneEntity{
      .id = "static_group",
      .name = "Static Group",
      .components = nlohmann::json{{
          "StaticComponent",
          nlohmann::json{{"enabled", true},
                         {"include_descendants", true},
                         {"flags", karma::components::StaticComponentAll}},
      }},
  });
  document.entities.push_back(karma::scenes::SceneEntity{
      .id = "renderable",
      .name = "Renderable",
      .components = nlohmann::json{{"MeshComponent",
                                    nlohmann::json::object()}},
  });

  KARMA_REQUIRE(editor::selectedEditableGroupId(
                    document,
                    {editor::SelectionKind::Entity, "static_group"}) ==
                "static_group");
  KARMA_REQUIRE(editor::selectedEditableGroupId(
                    document,
                    {editor::SelectionKind::Entity, "renderable"})
                    .empty());

  document.static_components.push_back(
      karma::scenes::SceneStaticComponent{
          .id = "legacy_static",
          .entity_id = "static_group",
      });
  KARMA_REQUIRE(editor::selectedEditableGroupId(
                    document,
                    {editor::SelectionKind::Entity, "static_group"})
                    .empty());
}

void testSceneTransformRoundTrips() {
  const karma::scenes::SceneTransform parent{
      .position = {7.5f, -2.25f, 11.0f},
      .rotation = karma::math::fromYawPitch(0.63f, -0.31f),
      .scale = {2.5f, 0.75f, 1.25f},
  };
  const karma::scenes::SceneTransform child{
      .position = {-3.0f, 4.5f, 1.25f},
      .rotation = karma::math::fromYawPitch(-0.42f, 0.27f),
      .scale = {0.4f, 1.8f, 0.6f},
  };

  const karma::scenes::SceneTransform composed =
      editor::composeSceneTransforms(parent, child);
  KARMA_REQUIRE(nearlyTransform(
      editor::sceneTransformRelativeTo(parent, composed), child));
  KARMA_REQUIRE(nearlyTransform(
      editor::sceneTransformWithoutChild(composed, child), parent));
}

void testEditorOrbitCameraConvention() {
  editor::EditorOrbitCamera camera{};
  const karma::scenes::SceneTransform pose =
      editor::editorOrbitCameraTransform(camera);
  const karma::math::Vec3 forward = karma::math::normalize(
      karma::math::rotateVec(pose.rotation, {0.0f, 0.0f, -1.0f}));
  const karma::math::Vec3 toward_pivot = karma::math::normalize(
      karma::math::subtract(camera.pivot, pose.position));

  KARMA_REQUIRE(pose.position.y > camera.pivot.y);
  KARMA_REQUIRE(forward.y < 0.0f);
  KARMA_REQUIRE(nearly(karma::math::dot(forward, toward_pivot), 1.0f));

  const float original_pitch = camera.pitch;
  editor::applyEditorCameraLookDelta(camera, 0.0f, 20.0f, 0.004f);
  KARMA_REQUIRE(camera.pitch < original_pitch);
  const karma::math::Vec3 looked_forward = karma::math::normalize(
      karma::math::rotateVec(
          editor::editorOrbitCameraTransform(camera).rotation,
          {0.0f, 0.0f, -1.0f}));
  KARMA_REQUIRE(looked_forward.y < forward.y);
}

void testDeferredViewportSelectionResolution() {
  KARMA_REQUIRE(!editor::shouldResolveViewportSelection(false, false, false));
  KARMA_REQUIRE(editor::shouldResolveViewportSelection(true, false, false));
  KARMA_REQUIRE(!editor::shouldResolveViewportSelection(true, true, false));
  KARMA_REQUIRE(!editor::shouldResolveViewportSelection(true, false, true));
}

editor::ViewportProjection markerTestProjection() {
  return editor::ViewportProjection{
      .rect = {.x = 0.0f, .y = 0.0f, .width = 800.0f, .height = 600.0f},
      .camera = {.position = {0.0f, 0.0f, 0.0f},
                 .rotation = {},
                 .fov_y_degrees = 60.0f,
                 .near_clip = 0.1f,
                 .far_clip = 100.0f},
  };
}

const editor::SceneMarker* findMarker(
    const editor::SceneMarkerClassificationResult& result,
    std::string_view id) {
  const auto marker = std::find_if(
      result.markers.begin(), result.markers.end(),
      [&](const editor::SceneMarker& value) {
        return value.selection.id == id;
      });
  return marker == result.markers.end() ? nullptr : &*marker;
}

void testMarkerClassificationAndVisibility() {
  karma::scenes::SceneDocument document{};
  document.entities = {
      {.id = "root",
       .name = "Group",
       .transform = {.position = {0.0f, 0.0f, -5.0f}}},
      {.id = "point", .parent_id = "root",
       .transform = {.position = {-2.0f, 0.0f, 0.0f}}},
      {.id = "spot", .parent_id = "root",
       .transform = {.position = {2.0f, 0.0f, 0.0f}}},
      {.id = "sun", .parent_id = "root",
       .transform = {.position = {0.0f, 2.0f, 0.0f}}},
      {.id = "environment", .parent_id = "root",
       .transform = {.position = {0.0f, -2.0f, 0.0f}}},
      {.id = "rendered", .parent_id = "root",
       .components = nlohmann::json{{"MeshComponent",
                                      {{"visible", true}}}}},
      {.id = "hidden_renderable", .parent_id = "root",
       .components = nlohmann::json{{"MeshComponent",
                                      {{"visible", false}}}}},
      {.id = "static_rendered", .parent_id = "root"},
  };
  karma::components::LightComponent point{};
  point.type = karma::components::LightComponent::Type::Point;
  point.range = 7.0f;
  karma::components::LightComponent spot{};
  spot.type = karma::components::LightComponent::Type::Spot;
  spot.range = 11.0f;
  spot.outer_cone_degrees = 32.0f;
  karma::components::LightComponent sun{};
  sun.type = karma::components::LightComponent::Type::Directional;
  document.lights = {
      {.id = "point_light", .entity_id = "point", .component = point},
      {.id = "spot_light", .entity_id = "spot", .component = spot},
      {.id = "sun_light", .entity_id = "sun", .component = sun},
  };
  document.environment = karma::scenes::SceneEnvironment{
      .id = "environment_settings", .entity_id = "environment"};
  document.static_components.push_back(karma::scenes::SceneStaticComponent{
      .id = "static", .entity_id = "static_rendered", .render = true});
  document.prefab_instances.push_back(karma::scenes::ScenePrefabInstance{
      .id = "prefab",
      .prefab_path = "prefabs/tree/prefab.json",
      .parent_entity_id = "root",
      .transform = {.position = {0.0f, 3.0f, 0.0f}},
  });

  const editor::Selection selected{editor::SelectionKind::Entity, "spot"};
  const editor::SceneMarkerClassificationResult classified =
      editor::classifySceneMarkers(document, selected);
  KARMA_REQUIRE(classified.success());
  KARMA_REQUIRE(classified.markers.size() == 7u);
  KARMA_REQUIRE(findMarker(classified, "rendered") == nullptr);
  KARMA_REQUIRE(findMarker(classified, "static_rendered") == nullptr);

  const auto* root = findMarker(classified, "root");
  const auto* point_marker = findMarker(classified, "point");
  const auto* spot_marker = findMarker(classified, "spot");
  const auto* sun_marker = findMarker(classified, "sun");
  const auto* environment_marker = findMarker(classified, "environment");
  const auto* hidden_marker = findMarker(classified, "hidden_renderable");
  const auto* prefab_marker = findMarker(classified, "prefab");
  KARMA_REQUIRE(root != nullptr &&
                root->kind == editor::SceneMarkerKind::EmptyEntity);
  KARMA_REQUIRE(point_marker != nullptr &&
                point_marker->kind == editor::SceneMarkerKind::PointLight);
  KARMA_REQUIRE(nearly(point_marker->range, 7.0f));
  KARMA_REQUIRE(spot_marker != nullptr &&
                spot_marker->kind == editor::SceneMarkerKind::SpotLight);
  KARMA_REQUIRE(spot_marker->selected);
  KARMA_REQUIRE(nearly(spot_marker->outer_cone_degrees, 32.0f));
  KARMA_REQUIRE(sun_marker != nullptr &&
                sun_marker->kind == editor::SceneMarkerKind::DirectionalLight);
  KARMA_REQUIRE(environment_marker != nullptr &&
                environment_marker->kind ==
                    editor::SceneMarkerKind::EnvironmentAnchor);
  KARMA_REQUIRE(hidden_marker != nullptr &&
                hidden_marker->kind == editor::SceneMarkerKind::EmptyEntity);
  KARMA_REQUIRE(prefab_marker != nullptr &&
                prefab_marker->kind == editor::SceneMarkerKind::PrefabRoot);
  KARMA_REQUIRE(prefab_marker->selection.kind == editor::SelectionKind::Prefab);
  KARMA_REQUIRE(nearlyVec3(prefab_marker->world_transform.position,
                          {0.0f, 3.0f, -5.0f}));

  KARMA_REQUIRE(!editor::sceneMarkerVisible(*point_marker, false));
  KARMA_REQUIRE(editor::sceneMarkerVisible(*spot_marker, false));
  KARMA_REQUIRE(editor::sceneMarkerVisible(*point_marker, true));
}

size_t countMarkerLines(const editor::SceneMarkerGeometry& geometry,
                        editor::SceneMarkerLineLayer layer) {
  return static_cast<size_t>(std::count_if(
      geometry.lines.begin(), geometry.lines.end(),
      [&](const editor::SceneMarkerLine& line) {
        return line.layer == layer;
      }));
}

void testMarkerGeometryAndConstantApparentSize() {
  const editor::ViewportProjection projection = markerTestProjection();
  editor::SceneMarker near_marker{
      .selection = {editor::SelectionKind::Entity, "near"},
      .kind = editor::SceneMarkerKind::EmptyEntity,
      .world_transform = {.position = {0.0f, 0.0f, -5.0f}},
  };
  editor::SceneMarker far_marker = near_marker;
  far_marker.selection.id = "far";
  far_marker.world_transform.position.z = -10.0f;
  const editor::SceneMarkerGeometryOptions options{
      .icon_radius_pixels = 12.0f,
      .circle_segments = 16u,
  };
  const editor::SceneMarkerGeometry near_geometry =
      editor::buildSceneMarkerGeometry(near_marker, projection, options);
  const editor::SceneMarkerGeometry far_geometry =
      editor::buildSceneMarkerGeometry(far_marker, projection, options);
  KARMA_REQUIRE(!near_geometry.empty());
  KARMA_REQUIRE(!far_geometry.empty());
  KARMA_REQUIRE(nearly(far_geometry.icon_world_radius,
                       near_geometry.icon_world_radius * 2.0f));

  const auto near_pivot = editor::projectWorldToViewport(
      projection, near_marker.world_transform.position);
  const auto near_radius = editor::projectWorldToViewport(
      projection,
      karma::math::add(near_marker.world_transform.position,
                       {0.0f, near_geometry.icon_world_radius, 0.0f}));
  const auto far_pivot = editor::projectWorldToViewport(
      projection, far_marker.world_transform.position);
  const auto far_radius = editor::projectWorldToViewport(
      projection,
      karma::math::add(far_marker.world_transform.position,
                       {0.0f, far_geometry.icon_world_radius, 0.0f}));
  KARMA_REQUIRE(near_pivot && near_radius && far_pivot && far_radius);
  KARMA_REQUIRE(nearly(std::abs(near_radius->screen.y - near_pivot->screen.y),
                       12.0f));
  KARMA_REQUIRE(nearly(std::abs(far_radius->screen.y - far_pivot->screen.y),
                       12.0f));
  KARMA_REQUIRE(countMarkerLines(near_geometry,
                                 editor::SceneMarkerLineLayer::Bounds) == 0u);

  editor::SceneMarker selected_point{
      .selection = {editor::SelectionKind::Entity, "point"},
      .kind = editor::SceneMarkerKind::PointLight,
      .world_transform = {.position = {0.0f, 0.0f, -5.0f}},
      .color = {1.0f, 0.5f, 0.25f, 1.0f},
      .range = 3.0f,
      .selected = true,
  };
  const auto point_geometry =
      editor::buildSceneMarkerGeometry(selected_point, projection, options);
  KARMA_REQUIRE(countMarkerLines(point_geometry,
                                 editor::SceneMarkerLineLayer::Overlay) > 0u);
  KARMA_REQUIRE(countMarkerLines(point_geometry,
                                 editor::SceneMarkerLineLayer::Bounds) == 48u);
  const auto point_bound = std::find_if(
      point_geometry.lines.begin(), point_geometry.lines.end(),
      [](const editor::SceneMarkerLine& line) {
        return line.layer == editor::SceneMarkerLineLayer::Bounds;
      });
  KARMA_REQUIRE(point_bound != point_geometry.lines.end());
  KARMA_REQUIRE(nearly(karma::math::length(karma::math::subtract(
                           point_bound->from,
                           selected_point.world_transform.position)),
                       3.0f));

  editor::SceneMarker selected_spot{
      .selection = {editor::SelectionKind::Entity, "spot"},
      .kind = editor::SceneMarkerKind::SpotLight,
      .world_transform = {.position = {0.0f, 0.0f, -5.0f}},
      .color = {0.5f, 0.8f, 1.0f, 1.0f},
      .range = 4.0f,
      .outer_cone_degrees = 30.0f,
      .selected = true,
  };
  const auto spot_geometry =
      editor::buildSceneMarkerGeometry(selected_spot, projection, options);
  const auto spot_bound = std::find_if(
      spot_geometry.lines.begin(), spot_geometry.lines.end(),
      [](const editor::SceneMarkerLine& line) {
        return line.layer == editor::SceneMarkerLineLayer::Bounds;
      });
  KARMA_REQUIRE(spot_bound != spot_geometry.lines.end());
  KARMA_REQUIRE(nearly(spot_bound->from.z, -9.0f));
  KARMA_REQUIRE(nearly(std::sqrt(spot_bound->from.x * spot_bound->from.x +
                                 spot_bound->from.y * spot_bound->from.y),
                       4.0f * std::tan(30.0f * 3.14159265358979323846f /
                                       180.0f)));

  editor::SceneMarker behind = near_marker;
  behind.world_transform.position.z = 5.0f;
  KARMA_REQUIRE(editor::buildSceneMarkerGeometry(behind, projection, options)
                    .empty());
}

void testMarkerPickingPriorityAndRejection() {
  const editor::ViewportProjection projection = markerTestProjection();
  std::vector<editor::SceneMarker> markers{
      {.selection = {editor::SelectionKind::Entity, "near"},
       .kind = editor::SceneMarkerKind::EmptyEntity,
       .world_transform = {.position = {0.0f, 0.0f, -5.0f}}},
      {.selection = {editor::SelectionKind::Prefab, "far"},
       .kind = editor::SceneMarkerKind::PrefabRoot,
       .world_transform = {.position = {0.0f, 0.0f, -10.0f}}},
  };
  auto hit = editor::pickSceneMarker(
      markers, projection, {400.0f, 300.0f}, true, 16.0f);
  KARMA_REQUIRE(hit.has_value());
  KARMA_REQUIRE(hit->selection.id == "near");
  KARMA_REQUIRE(hit->marker_index == 0u);

  const std::vector<editor::SceneMarker> overlapping{
      {.selection = {editor::SelectionKind::Entity, "group"},
       .kind = editor::SceneMarkerKind::EmptyEntity,
       .world_transform = {.position = {0.0f, 0.0f, -5.0f}}},
      {.selection = {editor::SelectionKind::Entity, "environment"},
       .kind = editor::SceneMarkerKind::EnvironmentAnchor,
       .world_transform = {.position = {0.0f, 0.0f, -5.0f}}},
  };
  const auto overlapping_hit = editor::pickSceneMarker(
      overlapping, projection, {400.0f, 300.0f}, true, 16.0f);
  KARMA_REQUIRE(overlapping_hit.has_value());
  KARMA_REQUIRE(overlapping_hit->selection.id == "environment");

  markers[1].selected = true;
  hit = editor::pickSceneMarker(
      markers, projection, {400.0f, 300.0f}, false, 16.0f);
  KARMA_REQUIRE(hit.has_value());
  KARMA_REQUIRE(hit->selection.id == "far");
  markers[1].selected = false;
  KARMA_REQUIRE(!editor::pickSceneMarker(
      markers, projection, {400.0f, 300.0f}, false, 16.0f));

  markers[0].world_transform.position = {0.0f, 0.0f, 5.0f};
  markers[1].world_transform.position = {100.0f, 0.0f, -10.0f};
  KARMA_REQUIRE(!editor::pickSceneMarker(
      markers, projection, {400.0f, 300.0f}, true, 16.0f));

  markers[0].world_transform.position = {0.0f, 0.0f, -5.0f};
  const auto projected = editor::projectWorldToViewport(
      projection, markers[0].world_transform.position);
  KARMA_REQUIRE(projected.has_value());
  KARMA_REQUIRE(!editor::pickSceneMarker(
      markers,
      projection,
      {projected->screen.x + 17.0f, projected->screen.y},
      true,
      16.0f));
}

void testWorldTransformAndReparent() {
  karma::scenes::SceneDocument document = hierarchyDocument();
  const editor::Selection child{editor::SelectionKind::Entity, "child"};
  const auto before = editor::sceneWorldTransform(document, child);
  KARMA_REQUIRE(before.has_value());
  KARMA_REQUIRE(nearly(before->position.x, 16.0f));
  KARMA_REQUIRE(nearly(before->scale.x, 4.0f));
  KARMA_REQUIRE(!editor::canReparent(
      document, {editor::SelectionKind::Entity, "left"}, "child"));
  KARMA_REQUIRE(editor::canReparent(document, child, "right"));

  std::string diagnostic;
  KARMA_REQUIRE(editor::reparentPreservingWorld(
      document, child, "right", &diagnostic));
  KARMA_REQUIRE(diagnostic.empty());
  const auto child_record = std::find_if(
      document.entities.begin(), document.entities.end(),
      [](const karma::scenes::SceneEntity& entity) {
        return entity.id == "child";
      });
  KARMA_REQUIRE(child_record->parent_id == "right");
  KARMA_REQUIRE(nearly(child_record->transform.position.x, 5.0f));
  KARMA_REQUIRE(nearly(child_record->transform.scale.x, 2.0f));
  const auto after = editor::sceneWorldTransform(document, child);
  KARMA_REQUIRE(after.has_value());
  KARMA_REQUIRE(nearly(after->position.x, before->position.x));
  KARMA_REQUIRE(nearly(after->scale.x, before->scale.x));

  karma::scenes::SceneDocument protected_hierarchy = hierarchyDocument();
  protected_hierarchy.static_components.push_back(
      karma::scenes::SceneStaticComponent{
          .id = "static", .entity_id = "child"});
  KARMA_REQUIRE(!editor::canReparent(
      protected_hierarchy,
      {editor::SelectionKind::Entity, "left"},
      "right"));
  KARMA_REQUIRE(!editor::reparentPreservingWorld(
      protected_hierarchy,
      {editor::SelectionKind::Entity, "left"},
      "right",
      &diagnostic));
  KARMA_REQUIRE(protected_hierarchy.entities[1].parent_id == "root");

  const karma::scenes::SceneDocument unchanged = document;
  document.entities[2].transform.scale.x = 0.0f;
  KARMA_REQUIRE(!editor::reparentPreservingWorld(
      document,
      {editor::SelectionKind::Prefab, "prefab"},
      "right",
      &diagnostic));
  KARMA_REQUIRE(diagnostic.find("zero scale") != std::string::npos);
  KARMA_REQUIRE(document.prefab_instances[0].parent_entity_id ==
                unchanged.prefab_instances[0].parent_entity_id);
}

void testDuplicateHierarchySubtree() {
  karma::scenes::SceneDocument document = hierarchyDocument();
  size_t next_id = 0u;
  std::string diagnostic;
  const auto duplicated = editor::duplicateSelection(
      document,
      {editor::SelectionKind::Entity, "left"},
      [&](std::string_view prefix) {
        return std::string(prefix) + "_copy_" + std::to_string(next_id++);
      },
      &diagnostic);
  KARMA_REQUIRE(duplicated.has_value());
  KARMA_REQUIRE(duplicated->kind == editor::SelectionKind::Entity);
  KARMA_REQUIRE(duplicated->id == "entity_copy_0");
  KARMA_REQUIRE(document.entities.size() == 6u);
  KARMA_REQUIRE(document.prefab_instances.size() == 2u);
  KARMA_REQUIRE(document.lights.size() == 2u);
  const auto group_copy = std::find_if(
      document.entities.begin(), document.entities.end(),
      [&](const karma::scenes::SceneEntity& entity) {
        return entity.id == duplicated->id;
      });
  KARMA_REQUIRE(group_copy != document.entities.end());
  KARMA_REQUIRE(group_copy->name == "Left Copy");
  KARMA_REQUIRE(group_copy->parent_id == "root");
  const auto child_copy = std::find_if(
      document.entities.begin(), document.entities.end(),
      [&](const karma::scenes::SceneEntity& entity) {
        return entity.parent_id == duplicated->id;
      });
  KARMA_REQUIRE(child_copy != document.entities.end());
  const auto prefab_copy = std::find_if(
      document.prefab_instances.begin(), document.prefab_instances.end(),
      [&](const karma::scenes::ScenePrefabInstance& prefab) {
        return prefab.id != "prefab";
      });
  KARMA_REQUIRE(prefab_copy != document.prefab_instances.end());
  KARMA_REQUIRE(prefab_copy->parent_entity_id == child_copy->id);
  KARMA_REQUIRE(document.lights.back().entity_id == child_copy->id);
  KARMA_REQUIRE(karma::scenes::validateSceneDocument(document).success());

  karma::scenes::SceneDocument protected_document = hierarchyDocument();
  protected_document.entities[1].components["FoliageComponent"] =
      nlohmann::json::object();
  const size_t original_entities = protected_document.entities.size();
  KARMA_REQUIRE(!editor::duplicateSelection(
      protected_document,
      {editor::SelectionKind::Entity, "left"},
      [](std::string_view prefix) { return std::string(prefix) + "_copy"; },
      &diagnostic));
  KARMA_REQUIRE(protected_document.entities.size() == original_entities);
  KARMA_REQUIRE(diagnostic.find("cannot be duplicated") != std::string::npos);
}

void testDeletePromotesChildrenWithoutMovingThem() {
  std::string diagnostic;
  karma::scenes::SceneDocument document = hierarchyDocument();
  const editor::Selection child{editor::SelectionKind::Entity, "child"};
  const editor::Selection prefab{editor::SelectionKind::Prefab, "prefab"};
  const auto prefab_before = editor::sceneWorldTransform(document, prefab);
  KARMA_REQUIRE(prefab_before.has_value());
  KARMA_REQUIRE(editor::deleteSelectionPreservingWorld(
      document, child, &diagnostic));
  KARMA_REQUIRE(std::none_of(document.entities.begin(), document.entities.end(),
                             [](const karma::scenes::SceneEntity& entity) {
                               return entity.id == "child";
                             }));
  KARMA_REQUIRE(document.prefab_instances[0].parent_entity_id == "left");
  KARMA_REQUIRE(document.lights.empty());
  const auto prefab_after = editor::sceneWorldTransform(document, prefab);
  KARMA_REQUIRE(prefab_after.has_value());
  KARMA_REQUIRE(nearly(prefab_after->position.x, prefab_before->position.x));
  KARMA_REQUIRE(nearly(prefab_after->position.y, prefab_before->position.y));
  KARMA_REQUIRE(nearly(prefab_after->scale.x, prefab_before->scale.x));
  KARMA_REQUIRE(karma::scenes::validateSceneDocument(document).success());

  document = hierarchyDocument();
  const auto child_before = editor::sceneWorldTransform(document, child);
  KARMA_REQUIRE(child_before.has_value());
  KARMA_REQUIRE(editor::deleteSelectionPreservingWorld(
      document,
      {editor::SelectionKind::Entity, "left"},
      &diagnostic));
  const auto promoted_child = std::find_if(
      document.entities.begin(), document.entities.end(),
      [](const karma::scenes::SceneEntity& entity) {
        return entity.id == "child";
      });
  KARMA_REQUIRE(promoted_child != document.entities.end());
  KARMA_REQUIRE(promoted_child->parent_id == "root");
  const auto child_after = editor::sceneWorldTransform(document, child);
  KARMA_REQUIRE(child_after.has_value());
  KARMA_REQUIRE(nearly(child_after->position.x, child_before->position.x));
  KARMA_REQUIRE(nearly(child_after->scale.x, child_before->scale.x));

  karma::scenes::SceneDocument protected_document = hierarchyDocument();
  protected_document.static_components.push_back(
      karma::scenes::SceneStaticComponent{
          .id = "static", .entity_id = "child"});
  KARMA_REQUIRE(!editor::deleteSelectionPreservingWorld(
      protected_document,
      {editor::SelectionKind::Entity, "left"},
      &diagnostic));
  KARMA_REQUIRE(std::any_of(
      protected_document.entities.begin(), protected_document.entities.end(),
      [](const karma::scenes::SceneEntity& entity) {
        return entity.id == "left";
      }));
}

void testRecoveryRoundTrip() {
  const auto root = tempDirectory();
  const auto scene = root / "scenes/test.kscene.json";
  const nlohmann::json value{{"version", 1}, {"name", "Recovered"}};
  std::string diagnostic;
  KARMA_REQUIRE(editor::recoveryPath(root, scene).filename() ==
                stableRecoveryKey(scene) + ".scene-recovery.json");
  KARMA_REQUIRE(editor::writeRecovery(root, scene, value, &diagnostic));
  const auto loaded = editor::loadRecovery(root, scene, &diagnostic);
  KARMA_REQUIRE(loaded.has_value());
  KARMA_REQUIRE(loaded->scene_json == value);
  KARMA_REQUIRE(editor::discardRecovery(root, scene, &diagnostic));
  KARMA_REQUIRE(!editor::loadRecovery(root, scene).has_value());
  std::filesystem::remove_all(root);
}

void testRecoveryRejectsMalformedFieldTypes() {
  const auto root = tempDirectory();
  const auto scene = root / "scenes/test.kscene.json";
  const auto recovery = editor::recoveryPath(root, scene);
  std::string diagnostic;

  writeText(recovery, R"({
    "version": "1",
    "source_scene": "/tmp/test.kscene.json",
    "scene": {}
  })");
  KARMA_REQUIRE(!editor::loadRecovery(root, scene, &diagnostic).has_value());
  KARMA_REQUIRE(diagnostic.find("version") != std::string::npos);

  writeText(recovery, R"({
    "version": 1,
    "source_scene": 42,
    "scene": {}
  })");
  KARMA_REQUIRE(!editor::loadRecovery(root, scene, &diagnostic).has_value());
  KARMA_REQUIRE(diagnostic.find("source_scene") != std::string::npos);

  writeText(recovery, R"({
    "version": 1,
    "source_scene": "/tmp/test.kscene.json",
    "scene": []
  })");
  KARMA_REQUIRE(!editor::loadRecovery(root, scene, &diagnostic).has_value());
  KARMA_REQUIRE(diagnostic.find("scene") != std::string::npos);
  KARMA_REQUIRE(!editor::writeRecovery(root, scene, nlohmann::json::array(), &diagnostic));

  writeText(recovery,
            nlohmann::json{{"version", 1},
                           {"source_scene", (root / "another.kscene.json").generic_string()},
                           {"scene", nlohmann::json::object()}}
                .dump(2));
  KARMA_REQUIRE(!editor::loadRecovery(root, scene, &diagnostic).has_value());
  KARMA_REQUIRE(diagnostic.find("does not match") != std::string::npos);
  std::filesystem::remove_all(root);
}

void testTerrainCreationTransactionAndRuntimeLifecycle() {
  const auto root = tempDirectory();
  const auto scene_path = root / "scenes/existing.kscene.json";
  const auto preview = root / ".karma/editor-preview/existing";
  const karma::scenes::SceneDocument source =
      makeSceneDocument(root, scene_path);
  const nlohmann::json source_json =
      karma::scenes::sceneDocumentToJson(source);

  std::string diagnostic;
  auto creation = editor::createTerrainTransaction(
      source,
      editor::TerrainCreationRequest{
          .content_root = root,
          .preview_directory = preview,
          .entity_id = "terrain_existing",
          .parent_entity_id = "root",
      },
      makeTerrainCanvas(),
      &diagnostic);
  KARMA_REQUIRE(creation.has_value());
  KARMA_REQUIRE(diagnostic.empty());
  KARMA_REQUIRE(karma::scenes::sceneDocumentToJson(source) == source_json);
  KARMA_REQUIRE(std::filesystem::is_regular_file(creation->height_path));
  KARMA_REQUIRE(std::filesystem::is_regular_file(creation->control_path));
  KARMA_REQUIRE(creation->document.entities.size() == source.entities.size() + 1u);
  const auto& terrain_json = creation->document.entities.back().components.at(
      "TerrainComponent");
  KARMA_REQUIRE(terrain_json.at("height_image") ==
                ".karma/editor-preview/existing/terrain_existing-height.r32");
  KARMA_REQUIRE(terrain_json.at("control_image") ==
                ".karma/editor-preview/existing/terrain_existing-control.tga");

  const auto saved =
      karma::scenes::saveSceneDocument(creation->document, scene_path);
  KARMA_REQUIRE(saved.success());
  const auto loaded = karma::scenes::loadSceneDocument(
      karma::scenes::SceneLoadDesc{.path = scene_path,
                                   .reference_root = root});
  KARMA_REQUIRE(loaded.success());
  KARMA_REQUIRE(loaded.document.has_value());

  karma::world::World world;
  karma::world::Scene scene;
  karma::assets::AssetRegistry assets;
  auto instance = karma::scenes::instantiateScene(
      world,
      scene,
      assets,
      *loaded.document,
      karma::scenes::SceneInstantiateDesc{.reference_root = root});
  KARMA_REQUIRE(instance.success);
  const karma::world::Entity terrain = instance.find("terrain_existing");
  KARMA_REQUIRE(world.isAlive(terrain));
  KARMA_REQUIRE(world.has<karma::components::TerrainComponent>(terrain));
  const auto& component =
      world.get<karma::components::TerrainComponent>(terrain);
  KARMA_REQUIRE(component.height_image == creation->height_path);

  karma::visual::terrain::TerrainSystem terrain_system(nullptr);
  KARMA_REQUIRE(terrain_system.setSingleImageTileOverride(
      terrain, creation->canvas.buildTileData()));
  terrain_system.update(world, 0.0f, 1.0f);
  terrain_system.clearSingleImageTileOverride(terrain);
  KARMA_REQUIRE(karma::scenes::destroyScene(world, scene, instance));
  terrain_system.update(world, 0.0f, 1.0f);
  std::filesystem::remove_all(root);
}

void testTerrainCreationFromRecoveryDocument() {
  const auto root = tempDirectory();
  const auto scene_path = root / "scenes/recovered.kscene.json";
  const auto source = makeSceneDocument(root, scene_path);
  std::string diagnostic;
  KARMA_REQUIRE(editor::writeRecovery(root,
                                      scene_path,
                                      karma::scenes::sceneDocumentToJson(source),
                                      &diagnostic));
  const auto recovery = editor::loadRecovery(root, scene_path, &diagnostic);
  KARMA_REQUIRE(recovery.has_value());
  const auto restored_path = root / "restored.kscene.json";
  writeText(restored_path, recovery->scene_json.dump(2));
  const auto restored = karma::scenes::loadSceneDocument(
      karma::scenes::SceneLoadDesc{.path = restored_path,
                                   .reference_root = root});
  KARMA_REQUIRE(restored.success());

  auto creation = editor::createTerrainTransaction(
      *restored.document,
      editor::TerrainCreationRequest{
          .content_root = root,
          .preview_directory = root / ".karma/editor-preview/recovered",
          .entity_id = "terrain_recovered",
          .parent_entity_id = "root",
      },
      makeTerrainCanvas(),
      &diagnostic);
  KARMA_REQUIRE(creation.has_value());
  KARMA_REQUIRE(creation->document.name == source.name);
  KARMA_REQUIRE(creation->document.entities[1].id == "existing");
  std::filesystem::remove_all(root);
}

void testTerrainCreationFilesystemFailuresAreTransactional() {
  const auto root = tempDirectory();
  const auto scene_path = root / "scenes/failure.kscene.json";
  const auto source = makeSceneDocument(root, scene_path);
  const nlohmann::json source_json =
      karma::scenes::sceneDocumentToJson(source);
  std::string diagnostic;

  const auto collision_preview = root / ".karma/editor-preview/collision";
  const auto collision_height =
      collision_preview / "terrain_collision-height.r32";
  std::filesystem::create_directories(collision_height);
  auto collision = editor::createTerrainTransaction(
      source,
      editor::TerrainCreationRequest{
          .content_root = root,
          .preview_directory = collision_preview,
          .entity_id = "terrain_collision",
          .parent_entity_id = "root",
      },
      makeTerrainCanvas(),
      &diagnostic);
  KARMA_REQUIRE(!collision.has_value());
  KARMA_REQUIRE(!diagnostic.empty());
  KARMA_REQUIRE(karma::scenes::sceneDocumentToJson(source) == source_json);
  KARMA_REQUIRE(!std::filesystem::exists(
      collision_preview / "terrain_collision-control.tga"));
  KARMA_REQUIRE(!std::filesystem::exists(
      collision_preview / "terrain_collision-height.r32.tmp"));

  const auto blocked_parent = root / "blocked-parent";
  writeText(blocked_parent, "not a directory");
  diagnostic.clear();
  auto blocked = editor::createTerrainTransaction(
      source,
      editor::TerrainCreationRequest{
          .content_root = root,
          .preview_directory = blocked_parent / "preview",
          .entity_id = "terrain_blocked",
          .parent_entity_id = "root",
      },
      makeTerrainCanvas(),
      &diagnostic);
  KARMA_REQUIRE(!blocked.has_value());
  KARMA_REQUIRE(!diagnostic.empty());
  KARMA_REQUIRE(karma::scenes::sceneDocumentToJson(source) == source_json);

  const auto outside = tempDirectory();
  const auto link = root / "linked-preview";
  std::error_code ec;
  std::filesystem::create_directory_symlink(outside, link, ec);
  if (!ec) {
    diagnostic.clear();
    auto escaped = editor::createTerrainTransaction(
        source,
        editor::TerrainCreationRequest{
            .content_root = root,
            .preview_directory = link,
            .entity_id = "terrain_escaped",
            .parent_entity_id = "root",
        },
        makeTerrainCanvas(),
        &diagnostic);
    KARMA_REQUIRE(!escaped.has_value());
    KARMA_REQUIRE(diagnostic.find("content root") != std::string::npos);
  }
  std::filesystem::remove_all(root);
  std::filesystem::remove_all(outside);
}

void testTerrainCreationRejectsPartialOrInvalidState() {
  const auto root = tempDirectory();
  const auto scene_path = root / "scenes/invalid.kscene.json";
  const auto source = makeSceneDocument(root, scene_path);
  const nlohmann::json source_json =
      karma::scenes::sceneDocumentToJson(source);
  std::string diagnostic;

  auto invalid_canvas = makeTerrainCanvas();
  invalid_canvas.mutableHeights().front() = 2.0f;
  auto invalid = editor::createTerrainTransaction(
      source,
      editor::TerrainCreationRequest{
          .content_root = root,
          .preview_directory = root / ".karma/editor-preview/invalid",
          .entity_id = "terrain_invalid",
          .parent_entity_id = "root",
      },
      std::move(invalid_canvas),
      &diagnostic);
  KARMA_REQUIRE(!invalid.has_value());
  KARMA_REQUIRE(karma::scenes::sceneDocumentToJson(source) == source_json);

  diagnostic.clear();
  auto unsafe_id = editor::createTerrainTransaction(
      source,
      editor::TerrainCreationRequest{
          .content_root = root,
          .preview_directory = root / ".karma/editor-preview/invalid",
          .entity_id = "../terrain",
          .parent_entity_id = "root",
      },
      makeTerrainCanvas(),
      &diagnostic);
  KARMA_REQUIRE(!unsafe_id.has_value());
  KARMA_REQUIRE(!diagnostic.empty());
  KARMA_REQUIRE(karma::scenes::sceneDocumentToJson(source) == source_json);

  karma::scenes::SceneDocument colliding_source = source;
  colliding_source.prefab_instances.push_back(
      karma::scenes::ScenePrefabInstance{
          .id = "terrain_prefab_collision",
          .prefab_path = "prefabs/tree/prefab.json",
          .parent_entity_id = "root",
      });
  diagnostic.clear();
  auto cross_kind_collision = editor::createTerrainTransaction(
      colliding_source,
      editor::TerrainCreationRequest{
          .content_root = root,
          .preview_directory = root / ".karma/editor-preview/invalid",
          .entity_id = "terrain_prefab_collision",
          .parent_entity_id = "root",
      },
      makeTerrainCanvas(),
      &diagnostic);
  KARMA_REQUIRE(!cross_kind_collision.has_value());
  KARMA_REQUIRE(diagnostic.find("invalid scene") != std::string::npos);
  KARMA_REQUIRE(!std::filesystem::exists(
      root / ".karma/editor-preview/invalid/terrain_prefab_collision-height.r32"));
  std::filesystem::remove_all(root);
}

}  // namespace

int main() {
  testPathContainment();
  testCatalogAndConflicts();
  testMiniContentRoot();
  testCatalogWatchesSourcesAndNewManifests();
  testCatalogRejectsMalformedFieldTypes();
  testSettingsRoundTrip();
  testWorkspaceLayoutDefaultsAndConstraints();
  testViewportPointerInputCapture();
  testSettingsRejectMalformedFieldTypes();
  testHistory();
  testComponentEditorRegistryMetadataAndCoverage();
  testLodAndInstanceSetEditorSchemas();
  testFoliagePrefabInspectionEligibility();
  testFocusedPrefabAssetDraftTransactions();
  testExplicitLegacyRenderMigrationReporting();
  testLegacyRenderMigrationFollowsNestedFoliagePrefabs();
  testLegacyRenderMigrationFilesystemSafety();
  testComponentDependencyTransactionsAndDuplicatePrevention();
  testComponentJsonValidationIsTransactional();
  testHierarchyBuildAndCycleHandling();
  testFoliageHierarchyProjectionIsPresentationOnly();
  testStaticGroupsRemainPrefabPlacementParents();
  testSceneTransformRoundTrips();
  testEditorOrbitCameraConvention();
  testDeferredViewportSelectionResolution();
  testMarkerClassificationAndVisibility();
  testMarkerGeometryAndConstantApparentSize();
  testMarkerPickingPriorityAndRejection();
  testWorldTransformAndReparent();
  testDuplicateHierarchySubtree();
  testDeletePromotesChildrenWithoutMovingThem();
  testRecoveryRoundTrip();
  testRecoveryRejectsMalformedFieldTypes();
  testTerrainCreationTransactionAndRuntimeLifecycle();
  testTerrainCreationFromRecoveryDocument();
  testTerrainCreationFilesystemFailuresAreTransactional();
  testTerrainCreationRejectsPartialOrInvalidState();
  std::cout << "scene editor model tests passed\n";
  return 0;
}
