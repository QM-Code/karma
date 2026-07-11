#pragma once

#include "karma/prefabs.h"
#include "karma/scene_authoring.h"
#include "karma/scenes.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace karma::tools::scene_editor {

enum class SelectionKind : uint8_t {
  None,
  Entity,
  Prefab,
};

struct Selection {
  SelectionKind kind = SelectionKind::None;
  std::string id;

  bool valid() const { return kind != SelectionKind::None && !id.empty(); }
  void clear() {
    kind = SelectionKind::None;
    id.clear();
  }
};

struct HierarchyNode {
  Selection item;
  std::vector<HierarchyNode> children;
};

struct HierarchyBuildResult {
  std::vector<HierarchyNode> roots;
  std::vector<std::string> diagnostics;

  bool success() const { return diagnostics.empty(); }
};

enum class ComponentEditorCategory : uint8_t {
  General,
  Rendering,
  Lighting,
  Physics,
  Terrain,
  Animation,
  Audio,
  Navigation,
  Networking,
  Scripting,
  Effects,
  Other,
};

enum class ComponentEditorKind : uint8_t {
  Transform,
  Mesh,
  InstancedMesh,
  InstanceSet,
  Lod,
  Light,
  Visibility,
  RenderTags,
  Collider,
  Rigidbody,
  PhysicsMaterial,
  PhysicsCollisionFilter,
  CharacterController,
  Terrain,
  Foliage,
  AdvancedJson,
};

enum class ComponentCreationPolicy : uint8_t {
  /// A validated default payload can be added immediately.
  DirectDefault,
  /// The add menu must collect and validate a JSON draft before insertion.
  ValidatedJsonDraft,
  /// Creation is owned by a contextual authoring workflow.
  ContextualWorkflow,
};

enum class ComponentRuntimeUpdatePolicy : uint8_t {
  /// Authored data changes without touching the simulation-only preview state.
  DocumentOnly,
  /// The preview can consume the authored change directly.
  LivePatch,
  /// The preview must be rebuilt transactionally after the edit commits.
  RebuildPreview,
};

using ComponentPayloadFactory = std::function<nlohmann::json()>;
using ComponentPayloadValidator =
    std::function<bool(const nlohmann::json&, std::string*)>;

/// UI-independent metadata and authoring behavior for one serialized component.
struct ComponentEditorDescriptor {
  std::string type_name;
  std::string display_name;
  ComponentEditorCategory category = ComponentEditorCategory::Other;
  ComponentEditorKind editor = ComponentEditorKind::AdvancedJson;
  ComponentCreationPolicy creation_policy =
      ComponentCreationPolicy::ValidatedJsonDraft;
  ComponentRuntimeUpdatePolicy runtime_update =
      ComponentRuntimeUpdatePolicy::RebuildPreview;
  ComponentPayloadFactory default_payload;
  ComponentPayloadValidator validate_payload;
  std::vector<std::string> dependencies;
  /// At least one of these sibling components must be present. Unlike hard
  /// dependencies, alternatives are never created implicitly.
  std::vector<std::string> one_of_dependencies;
  bool removable = true;
};

class ComponentEditorRegistry {
 public:
  bool registerDescriptor(ComponentEditorDescriptor descriptor);
  const ComponentEditorDescriptor* find(std::string_view type_name) const;
  const std::vector<ComponentEditorDescriptor>& descriptors() const {
    return descriptors_;
  }

 private:
  std::vector<ComponentEditorDescriptor> descriptors_;
  std::unordered_map<std::string, size_t> indices_;
};

/// Builds descriptors for every serializer currently registered with prefabs.
ComponentEditorRegistry buildComponentEditorRegistry();

/// Validates a component payload in an isolated staging world.
bool validateComponentPayload(const ComponentEditorRegistry& registry,
                              std::string_view type_name,
                              const nlohmann::json& payload,
                              std::string* diagnostic = nullptr);

/// Adds a validated payload and any missing dependencies transactionally.
/// Rigidbody and Character Controller additions therefore create a default box
/// collider when needed. Contextual-workflow components are intentionally
/// rejected here.
bool addComponentWithDependencies(
    scenes::SceneEntity& entity,
    const ComponentEditorRegistry& registry,
    std::string_view type_name,
    const nlohmann::json& payload,
    std::vector<std::string>* added_types = nullptr,
    std::string* diagnostic = nullptr);

/// Adds a descriptor's typed default payload and missing dependencies.
bool addDefaultComponentWithDependencies(
    scenes::SceneEntity& entity,
    const ComponentEditorRegistry& registry,
    std::string_view type_name,
    std::vector<std::string>* added_types = nullptr,
    std::string* diagnostic = nullptr);

/// Validates and replaces one existing component payload transactionally.
bool replaceComponentPayload(scenes::SceneEntity& entity,
                             const ComponentEditorRegistry& registry,
                             std::string_view type_name,
                             const nlohmann::json& payload,
                             std::string* diagnostic = nullptr);

/// Returns present dependent components that prevent removing `type_name` by
/// itself.
std::vector<std::string> componentRemovalBlockers(
    const scenes::SceneEntity& entity,
    const ComponentEditorRegistry& registry,
    std::string_view type_name);

/// Removes one or more components atomically. A dependency can only be removed
/// when all of its present dependents are included in the same request.
bool removeComponentsTogether(scenes::SceneEntity& entity,
                              const ComponentEditorRegistry& registry,
                              const std::vector<std::string>& type_names,
                              std::string* diagnostic = nullptr);

inline constexpr size_t kMaxEditorLodLevels = 3u;

/// Canonical editor defaults for the split rendering authoring components.
nlohmann::json defaultLodComponentPayload();
nlohmann::json defaultInstanceSetComponentPayload();

/// Schema validation used by the editor before the engine serializer is
/// invoked. This keeps malformed drafts out of both scenes and prefabs.
bool validateLodComponentPayload(const nlohmann::json& payload,
                                 std::string* diagnostic = nullptr);
bool validateInstanceSetComponentPayload(const nlohmann::json& payload,
                                         std::string* diagnostic = nullptr);

/// Focused source-prefab editing session. Only MeshComponent and LODComponent
/// payloads can be changed; node structure, transforms, variables, and all
/// other components remain immutable. Draft history is independent from the
/// scene document history.
class PrefabAssetDraft {
 public:
  struct Entry {
    std::string label;
    prefabs::PrefabDocument before;
    prefabs::PrefabDocument after;
  };

  const std::filesystem::path& sourcePath() const { return source_path_; }
  const prefabs::PrefabDocument& document() const { return document_; }
  prefabs::PrefabDocument& document() { return document_; }
  bool valid() const { return !source_path_.empty(); }
  bool dirty() const;
  bool canUndo() const { return cursor_ > 0u; }
  bool canRedo() const { return cursor_ < history_.size(); }
  std::string_view undoLabel() const;
  std::string_view redoLabel() const;

  bool setNodeComponent(size_t node_index,
                        std::string_view type_name,
                        const nlohmann::json& payload,
                        const ComponentEditorRegistry& registry,
                        std::string label,
                        std::string* diagnostic = nullptr,
                        bool coalesce = false);
  bool removeNodeComponent(size_t node_index,
                           std::string_view type_name,
                           const ComponentEditorRegistry& registry,
                           std::string label,
                           std::string* diagnostic = nullptr);
  bool undo();
  bool redo();
  void finishCoalescedEdit();

  /// Returns true when the source no longer matches the version from which the
  /// draft (or most recent successful save) was derived.
  bool sourceChangedExternally() const;

  /// Validates through the prefab loader using a sibling staging file, checks
  /// the source fingerprint again, then atomically replaces the source.
  bool save(const ComponentEditorRegistry& registry,
            std::string* diagnostic = nullptr);
  /// Discards local history and reloads the current source from disk.
  bool revert(std::string* diagnostic = nullptr);

 private:
  friend std::optional<PrefabAssetDraft> openPrefabAssetDraft(
      const std::filesystem::path&, std::string*);

  void push(std::string label,
            prefabs::PrefabDocument before,
            prefabs::PrefabDocument after);

  std::filesystem::path source_path_;
  nlohmann::json source_json_ = nlohmann::json::object();
  prefabs::PrefabDocument saved_document_{};
  prefabs::PrefabDocument document_{};
  std::string source_hash_;
  std::vector<Entry> history_;
  size_t cursor_ = 0u;
  std::optional<prefabs::PrefabDocument> coalesced_before_;
  std::string coalesced_label_;
  size_t coalesced_node_index_ = 0u;
  std::string coalesced_type_name_;
};

/// Opens a validated prefab source and preserves its raw top-level JSON so
/// fields outside PrefabDocument (for example asset_package) survive saving.
std::optional<PrefabAssetDraft> openPrefabAssetDraft(
    const std::filesystem::path& path,
    std::string* diagnostic = nullptr);

/// Orbit state used by the standalone editor camera. Negative pitch looks
/// down in Karma's +Y-up, -Z-forward convention.
struct EditorOrbitCamera {
  math::Vec3 pivot{0.0f, 2.0f, 0.0f};
  float distance = 38.0f;
  float yaw = 0.7853982f;
  float pitch = -0.42f;
};

/// Applies top-left screen-space mouse motion to an editor orbit camera.
void applyEditorCameraLookDelta(EditorOrbitCamera& camera,
                                float mouse_delta_x,
                                float mouse_delta_y,
                                float sensitivity);

/// Returns the world transform for an orbit camera looking at its pivot.
scenes::SceneTransform editorOrbitCameraTransform(
    const EditorOrbitCamera& camera);

/// Composes parent/child TRS using the same convention as Karma's scene graph.
scenes::SceneTransform composeSceneTransforms(
    const scenes::SceneTransform& parent,
    const scenes::SceneTransform& child);

/// Converts a composed scene-space TRS back into parent-local space.
/// Near-zero parent scale axes preserve the composed value on that axis.
scenes::SceneTransform sceneTransformRelativeTo(
    const scenes::SceneTransform& parent,
    const scenes::SceneTransform& composed);

/// Removes a known trailing child TRS from a composed transform. This is used
/// to recover a linked prefab instance transform from its runtime root.
scenes::SceneTransform sceneTransformWithoutChild(
    const scenes::SceneTransform& composed,
    const scenes::SceneTransform& child);

/// Resolves a deferred viewport click only when the current frame's gizmo did
/// not hover or consume it.
bool shouldResolveViewportSelection(bool selection_pending,
                                    bool gizmo_active,
                                    bool gizmo_hovered);

/// Builds the entity/prefab hierarchy without trusting the document to be
/// acyclic. Every addressable item is returned at most once.
HierarchyBuildResult buildHierarchy(const scenes::SceneDocument& document);

/// Reparents foliage rows beneath the editable terrain for editor presentation
/// only. The authored document, entity parents, and transforms are untouched.
/// If the terrain cannot be found, the original hierarchy is returned.
HierarchyBuildResult projectFoliageUnderTerrain(
    HierarchyBuildResult hierarchy,
    std::string_view terrain_entity_id,
    const std::vector<std::string>& foliage_entity_ids);

/// Computes an authored entity or prefab placement transform in scene space.
std::optional<scenes::SceneTransform> sceneWorldTransform(
    const scenes::SceneDocument& document,
    const Selection& item,
    std::string* diagnostic = nullptr);

/// Returns whether `item` can be parented to the authored entity identified by
/// `new_parent_entity_id`. An empty parent places the item at scene root.
bool canReparent(const scenes::SceneDocument& document,
                 const Selection& item,
                 std::string_view new_parent_entity_id);

/// Changes parent while preserving the item's scene-space transform.
bool reparentPreservingWorld(scenes::SceneDocument& document,
                             const Selection& item,
                             std::string new_parent_entity_id,
                             std::string* diagnostic = nullptr);

/// Deletes a prefab or entity transactionally. Direct children of an entity
/// are promoted to its parent without changing their scene-space transforms.
bool deleteSelectionPreservingWorld(scenes::SceneDocument& document,
                                    const Selection& item,
                                    std::string* diagnostic = nullptr);

using StableIdGenerator =
    std::function<std::string(std::string_view prefix)>;

/// Duplicates a prefab or an editable entity subtree transactionally.
/// Entity subtrees include child prefabs and lights. Camera, environment,
/// component-bearing, static, and baked subtrees are intentionally rejected.
std::optional<Selection> duplicateSelection(
    scenes::SceneDocument& document,
    const Selection& item,
    const StableIdGenerator& generate_id,
    std::string* diagnostic = nullptr);

enum class AssetKind : uint8_t {
  Prefab,
  Package,
  Mesh,
  Material,
  Environment,
  Texture,
  Other,
};

struct AssetEntry {
  AssetKind kind = AssetKind::Other;
  std::string name;
  std::string key;
  std::string type;
  std::filesystem::path path;
  std::filesystem::path package_path;
  std::filesystem::file_time_type modified{};
  bool valid = true;
  std::string diagnostic;
};

struct CatalogScanResult {
  std::vector<AssetEntry> entries;
  std::vector<std::string> diagnostics;

  bool success() const { return diagnostics.empty(); }
};

class AssetCatalog {
 public:
  CatalogScanResult scan(const std::filesystem::path& content_root,
                         const std::vector<std::filesystem::path>& asset_roots);

  const std::vector<AssetEntry>& entries() const { return entries_; }
  std::vector<std::filesystem::path> changedFiles() const;
  const AssetEntry* findByKey(std::string_view key) const;
  const AssetEntry* findPrefab(const std::filesystem::path& path) const;

 private:
  std::vector<AssetEntry> entries_;
  std::unordered_map<std::string, size_t> keys_;
  std::unordered_map<std::string, std::filesystem::file_time_type> watched_files_;
};

/// Local Scene Editor viewport shading preference. Numeric values are also
/// passed to the editor camera's shader parameters and therefore form an
/// internal renderer contract.
enum class ViewportRenderMode : uint8_t {
  Rendered = 0,
  Diffuse = 1,
  Texture = 2,
  Wire = 3,
};

/// Persistent lower-workspace tab selection. Values are serialized as names,
/// but the stable ordering is useful to the immediate-mode UI.
enum class BottomPanelTab : uint8_t {
  Assets = 0,
  Console = 1,
  Lighting = 2,
  Navigation = 3,
};

struct EditorSettings {
  std::vector<std::filesystem::path> asset_roots;
  float camera_move_speed = 12.0f;
  float grid_size = 1.0f;
  bool snap_enabled = false;
  bool markers_visible = true;
  struct PanelLayout {
    float hierarchy_width = 285.0f;
    float inspector_width = 340.0f;
    float assets_height = 240.0f;
  } panel_layout;
  std::string asset_filter;
  std::string hierarchy_filter;
  std::string inspector_filter;
  std::string selected_bake_id;
  int asset_type_filter = 0;
  int console_min_level = 0;
  int terrain_inspector_tab = 0;
  int terrain_material_layer = 0;
  std::string active_foliage_layer_id;
  std::unordered_map<std::string, bool> component_foldouts;
  BottomPanelTab bottom_panel_tab = BottomPanelTab::Assets;
  ViewportRenderMode viewport_render_mode = ViewportRenderMode::Rendered;
};

/// Pixel dimensions for the editor's four-pane workspace after enforcing
/// usable panel sizes. The side panes span the full workspace height; the
/// center column is divided between viewport and assets.
struct EditorWorkspaceLayout {
  float hierarchy_width = 0.0f;
  float inspector_width = 0.0f;
  float center_width = 0.0f;
  float viewport_height = 0.0f;
  float assets_height = 0.0f;
  float splitter_size = 6.0f;
  bool compact_width = false;
  bool compact_height = false;
};

/// Resolves persisted splitter preferences for the current workspace extent.
/// Nominal minimums are proportionally reduced for very small windows so pane
/// geometry remains finite, non-negative, and non-overlapping.
EditorWorkspaceLayout resolveEditorWorkspaceLayout(
    const EditorSettings::PanelLayout& preferred,
    float workspace_width,
    float workspace_height);

struct EditorPointerCaptureState {
  bool popup_open = false;
  bool drag_drop_active = false;
  bool panel_item_active = false;
  bool want_capture_mouse = false;
  bool viewport_item_hovered = false;
  bool viewport_navigation_owned = false;
};

/// Centralizes the input gate shared by viewport navigation, gizmos, painting,
/// and placement. Active editor controls continue to capture input even if the
/// cursor crosses into the viewport while dragging. An RMB/MMB gesture that
/// explicitly began in the viewport owns navigation until that button is
/// released, while popups and drag/drop remain hard barriers.
bool blocksViewportPointerInput(const EditorPointerCaptureState& state);

std::filesystem::path settingsPath(const std::filesystem::path& content_root);
bool loadEditorSettings(const std::filesystem::path& content_root,
                        EditorSettings& settings,
                        std::string* diagnostic = nullptr);
bool saveEditorSettings(const std::filesystem::path& content_root,
                        const EditorSettings& settings,
                        std::string* diagnostic = nullptr);

bool pathIsWithin(const std::filesystem::path& root,
                  const std::filesystem::path& candidate);
std::optional<std::filesystem::path> contentRelativePath(
    const std::filesystem::path& content_root,
    const std::filesystem::path& candidate);

struct TerrainCreationRequest {
  std::filesystem::path content_root;
  std::filesystem::path preview_directory;
  std::string entity_id;
  std::string parent_entity_id;
};

struct TerrainCreationResult {
  scenes::SceneDocument document;
  scene_authoring::TerrainCanvas canvas;
  std::string entity_id;
  std::filesystem::path height_path;
  std::filesystem::path control_path;
};

/// Stages one editable terrain and its working sidecars as a transaction.
/// The source document is never modified. Existing output paths are rejected,
/// and partial sidecars are removed when either commit fails.
std::optional<TerrainCreationResult> createTerrainTransaction(
    const scenes::SceneDocument& source,
    TerrainCreationRequest request,
    scene_authoring::TerrainCanvas canvas,
    std::string* diagnostic = nullptr);

std::string makeStableId(std::string_view prefix);

class DocumentHistory {
 public:
  struct Entry {
    std::string label;
    scenes::SceneDocument before;
    scenes::SceneDocument after;
    size_t estimated_bytes = 0u;
  };

  void setLimits(size_t max_commands, size_t max_bytes);
  void clear();
  void push(std::string label,
            scenes::SceneDocument before,
            scenes::SceneDocument after);
  bool undo(scenes::SceneDocument& document);
  bool redo(scenes::SceneDocument& document);
  bool canUndo() const { return cursor_ > 0u; }
  bool canRedo() const { return cursor_ < entries_.size(); }
  std::string_view undoLabel() const;
  std::string_view redoLabel() const;
  void markSaved();
  bool dirty() const { return !saved_state_reachable_ || cursor_ != saved_cursor_; }

 private:
  void enforceLimits();

  std::vector<Entry> entries_;
  size_t cursor_ = 0u;
  size_t saved_cursor_ = 0u;
  bool saved_state_reachable_ = true;
  size_t total_bytes_ = 0u;
  size_t max_commands_ = 256u;
  size_t max_bytes_ = 512u * 1024u * 1024u;
};

struct RecoveryRecord {
  std::filesystem::path source_scene;
  std::filesystem::file_time_type written{};
  nlohmann::json scene_json;
};

std::filesystem::path recoveryPath(const std::filesystem::path& content_root,
                                   const std::filesystem::path& scene_path);
bool writeRecovery(const std::filesystem::path& content_root,
                   const std::filesystem::path& scene_path,
                   const nlohmann::json& scene_json,
                   std::string* diagnostic = nullptr);
std::optional<RecoveryRecord> loadRecovery(
    const std::filesystem::path& content_root,
    const std::filesystem::path& scene_path,
    std::string* diagnostic = nullptr);
bool discardRecovery(const std::filesystem::path& content_root,
                     const std::filesystem::path& scene_path,
                     std::string* diagnostic = nullptr);

}  // namespace karma::tools::scene_editor
