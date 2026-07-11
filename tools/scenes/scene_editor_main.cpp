#include "scene_editor_model.h"
#include "scene_editor_colliders.h"
#include "scene_editor_foliage_prefab.h"
#include "scene_editor_gizmo.h"
#include "scene_editor_markers.h"
#include "scene_editor_migration.h"
#include "scene_editor_placement.h"
#include "scene_editor_pointer_input.h"
#include "scene_editor_viewport.h"

#include "karma/foliage.h"
#include "karma/karma.h"
#include "karma/scene_authoring.h"
#include "karma/ui.h"
#include "karma/ui_imgui.h"

#include <nfd.hpp>

#include <algorithm>
#include <atomic>
#include <array>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <future>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/base_sink.h>

namespace karma::tools::scene_editor {
namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kCameraLookSensitivity = 0.0014f;
constexpr float kMinimumViewportExtent = 64.0f;
constexpr const char* kEditorViewModeShaderParam =
    "karma_editor_view_mode";
constexpr std::chrono::milliseconds kCatalogPollInterval{1000};
constexpr std::chrono::milliseconds kRecoveryDebounce{1200};
constexpr std::chrono::milliseconds kTerrainPreviewInterval{50};
constexpr std::chrono::milliseconds kFoliagePreviewInterval{100};
constexpr size_t kMaxAuthoredFoliageInstances =
    foliage::kMaxAuthoredFoliageInstances;
constexpr size_t kMaxFoliageInstancesPerUpdate = 2048u;
constexpr size_t kMaxConsoleEntries = 2000u;

struct ConsoleEntry {
  spdlog::level::level_enum level = spdlog::level::info;
  std::string text;
};

struct EditorBakeSharedState {
  std::atomic_bool cancel_requested{false};
  std::mutex progress_mutex;
  scenes::SceneBakeProgress progress{};
};

bool writeJsonAtomic(const std::filesystem::path& path,
                     const nlohmann::json& json,
                     std::string& diagnostic) {
  diagnostic.clear();
  if (path.empty()) {
    diagnostic = "Bake output path is empty";
    return false;
  }
  std::error_code error;
  if (!path.parent_path().empty()) {
    std::filesystem::create_directories(path.parent_path(), error);
    if (error) {
      diagnostic = "Failed to create bake output directory: " +
                   error.message();
      return false;
    }
  }
  std::filesystem::path temporary = path;
  temporary += ".tmp";
  {
    std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
    if (!stream) {
      diagnostic = "Failed to open temporary bake manifest";
      return false;
    }
    stream << json.dump(2) << '\n';
    if (!stream) {
      diagnostic = "Failed to write temporary bake manifest";
      stream.close();
      std::filesystem::remove(temporary, error);
      return false;
    }
  }
  std::filesystem::rename(temporary, path, error);
  if (error) {
    std::error_code remove_error;
    std::filesystem::remove(path, remove_error);
    error.clear();
    std::filesystem::rename(temporary, path, error);
  }
  if (error) {
    diagnostic = "Failed to replace bake manifest: " + error.message();
    std::filesystem::remove(temporary, error);
    return false;
  }
  return true;
}

class EditorConsoleSink final : public spdlog::sinks::base_sink<std::mutex> {
 public:
  std::vector<ConsoleEntry> snapshot() {
    const std::lock_guard lock(this->mutex_);
    return entries_;
  }

  void clear() {
    const std::lock_guard lock(this->mutex_);
    entries_.clear();
  }

 protected:
  void sink_it_(const spdlog::details::log_msg& message) override {
    spdlog::memory_buf_t formatted;
    this->formatter_->format(message, formatted);
    if (entries_.size() == kMaxConsoleEntries) {
      entries_.erase(entries_.begin(),
                     entries_.begin() + static_cast<std::ptrdiff_t>(
                         kMaxConsoleEntries / 4u));
    }
    entries_.push_back(ConsoleEntry{
        .level = message.level,
        .text = std::string(formatted.data(), formatted.size()),
    });
  }

  void flush_() override {}

 private:
  std::vector<ConsoleEntry> entries_;
};

enum class BakeScope : uint8_t {
  Lighting,
  Navigation,
  All,
};

enum class ToolMode : uint8_t {
  Select,
  PlacePrefab,
  SculptRaise,
  SculptLower,
  SculptSmooth,
  SculptFlatten,
  SculptSetHeight,
  PaintSplat,
  PaintFoliage,
  EraseFoliage,
};

enum class PendingSceneAction : uint8_t {
  None,
  New,
  Open,
};

struct FoliageLayerState {
  std::string entity_id;
  std::string name;
  std::filesystem::path working_path;
  bool source_valid = true;
  foliage::FoliageLayer layer{};
};

struct EditCommand {
  enum class Kind : uint8_t { Document, Terrain, Foliage };
  Kind kind = Kind::Document;
  std::string label;
  scenes::SceneDocument before_document;
  scenes::SceneDocument after_document;
  std::vector<float> before_heights;
  std::vector<float> after_heights;
  std::vector<uint8_t> before_control;
  std::vector<uint8_t> after_control;
  std::string foliage_entity_id;
  foliage::FoliageEditResult foliage_edit;
  size_t bytes = 0u;
};

struct LaunchOptions {
  std::filesystem::path scene_path;
  std::filesystem::path content_root;
  std::filesystem::path executable_directory;
  std::vector<std::filesystem::path> extra_asset_roots;
  bool valid = true;
  bool show_help = false;
};

struct CatalogBuild {
  AssetCatalog catalog;
  CatalogScanResult result;
};

std::string joinDiagnostics(const std::vector<std::string>& diagnostics) {
  std::string joined;
  for (const std::string& diagnostic : diagnostics) {
    if (!joined.empty()) joined += "\n";
    joined += diagnostic;
  }
  return joined;
}

std::string pathUtf8(const std::filesystem::path& path) {
#if defined(_WIN32)
  const std::u8string value = path.u8string();
  return std::string(reinterpret_cast<const char*>(value.data()), value.size());
#else
  return path.string();
#endif
}

std::filesystem::path fromNfdPath(const nfdu8char_t* path) {
  if (path == nullptr) return {};
  return std::filesystem::path(reinterpret_cast<const char8_t*>(path));
}

std::optional<std::filesystem::path> openFileDialog(
    const nfdu8filteritem_t* filters,
    nfdfiltersize_t filter_count,
    const std::filesystem::path& initial = {}) {
  NFD::UniquePathU8 path;
  const std::string initial_utf8 = pathUtf8(initial);
  const nfdresult_t result = NFD::OpenDialog(
      path, filters, filter_count,
      initial_utf8.empty() ? nullptr
                           : reinterpret_cast<const nfdu8char_t*>(initial_utf8.c_str()));
  return result == NFD_OKAY ? std::optional<std::filesystem::path>{fromNfdPath(path.get())}
                            : std::nullopt;
}

std::optional<std::filesystem::path> saveFileDialog(
    const nfdu8filteritem_t* filters,
    nfdfiltersize_t filter_count,
    const std::filesystem::path& initial,
    std::string_view default_name) {
  NFD::UniquePathU8 path;
  const std::string initial_utf8 = pathUtf8(initial);
  const std::string name(default_name);
  const nfdresult_t result = NFD::SaveDialog(
      path, filters, filter_count,
      initial_utf8.empty() ? nullptr
                           : reinterpret_cast<const nfdu8char_t*>(initial_utf8.c_str()),
      reinterpret_cast<const nfdu8char_t*>(name.c_str()));
  return result == NFD_OKAY ? std::optional<std::filesystem::path>{fromNfdPath(path.get())}
                            : std::nullopt;
}

std::optional<std::filesystem::path> folderDialog(
    const std::filesystem::path& initial = {}) {
  NFD::UniquePathU8 path;
  const std::string initial_utf8 = pathUtf8(initial);
  const nfdresult_t result = NFD::PickFolder(
      path, initial_utf8.empty() ? nullptr
                                 : reinterpret_cast<const nfdu8char_t*>(initial_utf8.c_str()));
  return result == NFD_OKAY ? std::optional<std::filesystem::path>{fromNfdPath(path.get())}
                            : std::nullopt;
}

std::string filenameStemForScene(const std::filesystem::path& path) {
  std::string filename = path.filename().string();
  constexpr std::string_view suffix = ".kscene.json";
  if (filename.size() >= suffix.size() &&
      filename.compare(filename.size() - suffix.size(), suffix.size(), suffix) == 0) {
    filename.resize(filename.size() - suffix.size());
  } else {
    filename = path.stem().string();
  }
  return filename.empty() ? "untitled" : filename;
}

uint64_t hashFile(const std::filesystem::path& path) {
  std::ifstream stream(path, std::ios::binary);
  uint64_t hash = 1469598103934665603ull;
  std::array<char, 64 * 1024> bytes{};
  while (stream) {
    stream.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    const std::streamsize count = stream.gcount();
    for (std::streamsize i = 0; i < count; ++i) {
      hash ^= static_cast<uint8_t>(bytes[static_cast<size_t>(i)]);
      hash *= 1099511628211ull;
    }
  }
  return hash;
}

std::string hexHash(uint64_t value) {
  std::ostringstream stream;
  stream << std::hex << std::setfill('0') << std::setw(16) << value;
  return stream.str();
}

std::filesystem::path ensureSceneExtension(std::filesystem::path path) {
  const std::string filename = path.filename().string();
  constexpr std::string_view suffix = ".kscene.json";
  if (filename.size() < suffix.size() ||
      filename.compare(filename.size() - suffix.size(), suffix.size(), suffix) != 0) {
    path += ".kscene.json";
  }
  return path;
}

scenes::SceneTransform fromRuntimeTransform(const components::TransformComponent& transform) {
  return scenes::SceneTransform{.position = transform.localPosition(),
                                .rotation = transform.localRotation(),
                                .scale = transform.localScale()};
}

scenes::SceneTransform fromRuntimeWorldTransform(
    const components::TransformComponent& transform) {
  return scenes::SceneTransform{.position = transform.worldPosition(),
                                .rotation = transform.worldRotation(),
                                .scale = transform.worldScale()};
}

nlohmann::json serializeComponent(const world::World& world,
                                  world::Entity entity,
                                  std::string_view type_name) {
  prefabs::ensureBuiltinComponentSerializers();
  const auto* serializer = prefabs::componentSerializerRegistry().find(type_name);
  if (serializer == nullptr || !serializer->has(world, entity)) {
    return nlohmann::json::object();
  }
  return serializer->serialize(world, entity);
}

scenes::SceneDocument makeNewDocument(const std::filesystem::path& path,
                                      const std::filesystem::path& reference_root) {
  scenes::SceneDocument document{};
  document.name = filenameStemForScene(path);
  document.source_path = path;
  document.reference_root = reference_root;
  const std::string root_id = makeStableId("root");
  document.entities.push_back(scenes::SceneEntity{.id = root_id, .name = "Scene"});

  const std::string sun_entity_id = makeStableId("entity");
  document.entities.push_back(scenes::SceneEntity{
      .id = sun_entity_id,
      .name = "Sun",
      .parent_id = root_id,
      .transform = scenes::SceneTransform{
          .position = {0.0f, 50.0f, 0.0f},
          .rotation = math::fromYawPitch(0.65f, -0.9f),
      },
  });
  components::LightComponent sun{};
  sun.type = components::LightComponent::Type::Directional;
  sun.color = {1.0f, 0.96f, 0.88f, 1.0f};
  sun.intensity = 1.2f;
  sun.casts_shadows = true;
  sun.shadow_extent = 250.0f;
  document.lights.push_back(scenes::SceneLight{
      .id = makeStableId("light"), .entity_id = sun_entity_id, .component = sun});

  const std::string environment_entity_id = makeStableId("entity");
  document.entities.push_back(scenes::SceneEntity{.id = environment_entity_id,
                                                  .name = "Environment",
                                                  .parent_id = root_id});
  scenes::SceneEnvironment environment{};
  environment.id = makeStableId("environment");
  environment.entity_id = environment_entity_id;
  environment.component.intensity = 0.5f;
  environment.component.draw_skybox = true;
  document.environment = std::move(environment);
  return document;
}

LaunchOptions parseLaunchOptions(int argc, char** argv) {
  LaunchOptions options{};
  if (argc > 0 && argv[0] != nullptr && argv[0][0] != '\0') {
    options.executable_directory =
        std::filesystem::absolute(argv[0]).lexically_normal().parent_path();
  } else {
    options.executable_directory = std::filesystem::current_path();
  }
  for (int index = 1; index < argc; ++index) {
    const std::string_view argument(argv[index]);
    if (argument == "--help" || argument == "-h") {
      options.show_help = true;
    } else if (argument == "--content-root" && index + 1 < argc) {
      options.content_root = argv[++index];
    } else if (argument == "--asset-root" && index + 1 < argc) {
      options.extra_asset_roots.emplace_back(argv[++index]);
    } else if (!argument.empty() && argument.front() != '-' && options.scene_path.empty()) {
      options.scene_path = argv[index];
    } else {
      options.valid = false;
    }
  }
  if (options.content_root.empty()) {
    options.content_root = options.scene_path.empty()
                               ? std::filesystem::current_path()
                               : std::filesystem::absolute(options.scene_path).parent_path();
  }
  options.content_root = std::filesystem::absolute(options.content_root).lexically_normal();
  if (options.scene_path.empty()) {
    options.scene_path = options.content_root / "untitled.kscene.json";
  } else if (options.scene_path.is_relative()) {
    options.scene_path = std::filesystem::absolute(options.scene_path);
  }
  options.scene_path = options.scene_path.lexically_normal();
  return options;
}

const char* toolName(ToolMode tool) {
  switch (tool) {
    case ToolMode::Select: return "Select";
    case ToolMode::PlacePrefab: return "Place Prefab";
    case ToolMode::SculptRaise: return "Raise";
    case ToolMode::SculptLower: return "Lower";
    case ToolMode::SculptSmooth: return "Smooth";
    case ToolMode::SculptFlatten: return "Flatten";
    case ToolMode::SculptSetHeight: return "Set Height";
    case ToolMode::PaintSplat: return "Paint Texture";
    case ToolMode::PaintFoliage: return "Paint Foliage";
    case ToolMode::EraseFoliage: return "Erase Foliage";
  }
  return "Select";
}

bool isTerrainTool(ToolMode tool) {
  return tool >= ToolMode::SculptRaise && tool <= ToolMode::PaintSplat;
}

bool pathExistsNoThrow(const std::filesystem::path& path) {
  std::error_code error;
  return std::filesystem::exists(path, error) && !error;
}

void applySceneEditorTheme() {
  ImGui::StyleColorsDark();
  ImGuiStyle& style = ImGui::GetStyle();
  style.WindowPadding = {8.0f, 8.0f};
  style.FramePadding = {7.0f, 5.0f};
  style.CellPadding = {7.0f, 5.0f};
  style.ItemSpacing = {7.0f, 6.0f};
  style.ItemInnerSpacing = {6.0f, 4.0f};
  style.IndentSpacing = 16.0f;
  style.ScrollbarSize = 13.0f;
  style.GrabMinSize = 10.0f;
  style.WindowBorderSize = 1.0f;
  style.ChildBorderSize = 1.0f;
  style.PopupBorderSize = 1.0f;
  style.FrameBorderSize = 1.0f;
  style.TabBorderSize = 0.0f;
  style.WindowRounding = 3.0f;
  style.ChildRounding = 3.0f;
  style.FrameRounding = 3.0f;
  style.PopupRounding = 3.0f;
  style.ScrollbarRounding = 3.0f;
  style.GrabRounding = 2.0f;
  style.TabRounding = 3.0f;

  auto& colors = style.Colors;
  colors[ImGuiCol_Text] = {0.90f, 0.91f, 0.92f, 1.0f};
  colors[ImGuiCol_TextDisabled] = {0.58f, 0.61f, 0.65f, 1.0f};
  colors[ImGuiCol_WindowBg] = {0.118f, 0.122f, 0.133f, 1.0f};
  colors[ImGuiCol_ChildBg] = {0.153f, 0.161f, 0.176f, 1.0f};
  colors[ImGuiCol_PopupBg] = {0.135f, 0.141f, 0.153f, 0.99f};
  colors[ImGuiCol_Border] = {0.282f, 0.294f, 0.318f, 1.0f};
  colors[ImGuiCol_BorderShadow] = {0.0f, 0.0f, 0.0f, 0.0f};
  colors[ImGuiCol_FrameBg] = {0.176f, 0.184f, 0.200f, 1.0f};
  colors[ImGuiCol_FrameBgHovered] = {0.220f, 0.239f, 0.263f, 1.0f};
  colors[ImGuiCol_FrameBgActive] = {0.243f, 0.267f, 0.298f, 1.0f};
  colors[ImGuiCol_TitleBg] = {0.102f, 0.106f, 0.114f, 1.0f};
  colors[ImGuiCol_TitleBgActive] = {0.133f, 0.141f, 0.153f, 1.0f};
  colors[ImGuiCol_MenuBarBg] = {0.102f, 0.106f, 0.114f, 1.0f};
  colors[ImGuiCol_ScrollbarBg] = {0.102f, 0.106f, 0.114f, 0.85f};
  colors[ImGuiCol_ScrollbarGrab] = {0.282f, 0.294f, 0.318f, 1.0f};
  colors[ImGuiCol_ScrollbarGrabHovered] = {0.353f, 0.373f, 0.404f, 1.0f};
  colors[ImGuiCol_ScrollbarGrabActive] = {0.247f, 0.510f, 0.769f, 1.0f};
  colors[ImGuiCol_CheckMark] = {0.247f, 0.510f, 0.769f, 1.0f};
  colors[ImGuiCol_SliderGrab] = {0.247f, 0.510f, 0.769f, 1.0f};
  colors[ImGuiCol_SliderGrabActive] = {0.329f, 0.624f, 0.886f, 1.0f};
  colors[ImGuiCol_Button] = {0.192f, 0.204f, 0.224f, 1.0f};
  colors[ImGuiCol_ButtonHovered] = {0.247f, 0.510f, 0.769f, 0.78f};
  colors[ImGuiCol_ButtonActive] = {0.208f, 0.431f, 0.655f, 1.0f};
  colors[ImGuiCol_Header] = {0.192f, 0.204f, 0.224f, 1.0f};
  colors[ImGuiCol_HeaderHovered] = {0.247f, 0.510f, 0.769f, 0.55f};
  colors[ImGuiCol_HeaderActive] = {0.247f, 0.510f, 0.769f, 0.78f};
  colors[ImGuiCol_Separator] = {0.282f, 0.294f, 0.318f, 1.0f};
  colors[ImGuiCol_SeparatorHovered] = {0.247f, 0.510f, 0.769f, 0.75f};
  colors[ImGuiCol_SeparatorActive] = {0.247f, 0.510f, 0.769f, 1.0f};
  colors[ImGuiCol_ResizeGrip] = {0.247f, 0.510f, 0.769f, 0.22f};
  colors[ImGuiCol_ResizeGripHovered] = {0.247f, 0.510f, 0.769f, 0.67f};
  colors[ImGuiCol_ResizeGripActive] = {0.247f, 0.510f, 0.769f, 0.95f};
  colors[ImGuiCol_Tab] = {0.153f, 0.161f, 0.176f, 1.0f};
  colors[ImGuiCol_TabHovered] = {0.247f, 0.510f, 0.769f, 0.70f};
  colors[ImGuiCol_TabSelected] = {0.208f, 0.431f, 0.655f, 1.0f};
  colors[ImGuiCol_TabSelectedOverline] = {0.329f, 0.624f, 0.886f, 1.0f};
  colors[ImGuiCol_TabDimmed] = {0.133f, 0.141f, 0.153f, 1.0f};
  colors[ImGuiCol_TabDimmedSelected] = {0.176f, 0.302f, 0.427f, 1.0f};
  colors[ImGuiCol_TableHeaderBg] = {0.176f, 0.184f, 0.200f, 1.0f};
  colors[ImGuiCol_TableBorderStrong] = {0.282f, 0.294f, 0.318f, 1.0f};
  colors[ImGuiCol_TableBorderLight] = {0.220f, 0.227f, 0.247f, 1.0f};
  colors[ImGuiCol_TableRowBgAlt] = {1.0f, 1.0f, 1.0f, 0.018f};
  colors[ImGuiCol_TextSelectedBg] = {0.247f, 0.510f, 0.769f, 0.40f};
  colors[ImGuiCol_DragDropTarget] = {0.329f, 0.624f, 0.886f, 1.0f};
  colors[ImGuiCol_NavCursor] = {0.329f, 0.624f, 0.886f, 1.0f};
  colors[ImGuiCol_NavWindowingDimBg] = {0.0f, 0.0f, 0.0f, 0.38f};
  colors[ImGuiCol_ModalWindowDimBg] = {0.0f, 0.0f, 0.0f, 0.58f};
  // Karma's graphical UI target is composited through a linear scene target.
  // Define the palette in familiar sRGB values, then linearize it once here.
  const auto to_linear = [](float value) {
    return value <= 0.04045f
               ? value / 12.92f
               : std::pow((value + 0.055f) / 1.055f, 2.4f);
  };
  for (int index = 0; index < ImGuiCol_COUNT; ++index) {
    colors[index].x = to_linear(colors[index].x);
    colors[index].y = to_linear(colors[index].y);
    colors[index].z = to_linear(colors[index].z);
  }
}

}  // namespace

class SceneEditorGame final : public app::GameInterface {
 public:
  SceneEditorGame(std::filesystem::path executable_directory,
                  std::filesystem::path content_root,
                  std::filesystem::path scene_path,
                  scenes::SceneDocument document,
                  EditorSettings settings,
                  std::string migration_status,
                  visual::terrain::TerrainRuntimeModule* terrain_runtime,
                  foliage::FoliageRuntimeModule* foliage_runtime)
      : executable_directory_(std::move(executable_directory)),
        content_root_(std::move(content_root)),
        scene_path_(std::move(scene_path)),
        document_(std::move(document)),
        settings_(std::move(settings)),
        scene_migration_status_(std::move(migration_status)),
        terrain_runtime_(terrain_runtime),
        foliage_runtime_(foliage_runtime) {
    document_.source_path = scene_path_;
    document_.reference_root = content_root_;
    has_disk_version_ = pathExistsNoThrow(scene_path_);
    splat_layer_ = settings_.terrain_material_layer;
    std::copy_n(settings_.asset_filter.data(),
                std::min(settings_.asset_filter.size(), sizeof(asset_filter_) - 1u),
                asset_filter_);
    std::copy_n(settings_.hierarchy_filter.data(),
                std::min(settings_.hierarchy_filter.size(),
                         sizeof(hierarchy_filter_) - 1u),
                hierarchy_filter_);
    std::copy_n(settings_.inspector_filter.data(),
                std::min(settings_.inspector_filter.size(),
                         sizeof(component_filter_) - 1u),
                component_filter_);
  }

  void onStart() override {
    applySceneEditorTheme();
    loadEditorFonts();
    bindInput();
    // Registry mutation and use require external serialization. Populate every
    // built-in before the catalog worker and main-thread preview can read it.
    prefabs::ensureBuiltinComponentSerializers();
    component_editors_ = buildComponentEditorRegistry();
    console_sink_ = std::make_shared<EditorConsoleSink>();
    console_sink_->set_pattern("[%H:%M:%S] [%l] %v");
    console_logger_ = spdlog::default_logger();
    if (console_logger_) {
      console_logger_->sinks().push_back(console_sink_);
    }
    scanCatalog();
    rebuildPreview();
    createEditorCamera();
    catalog_poll_at_ = std::chrono::steady_clock::now() + kCatalogPollInterval;
    if (pathExistsNoThrow(scene_path_)) {
      std::error_code ec;
      scene_modified_ = std::filesystem::last_write_time(scene_path_, ec);
    }
    checkRecoveryAtStartup();
  }

  void onFixedUpdate(float) override {}

  void onUpdate(float dt) override {
    if (save_before_scene_action_pending_) {
      save_before_scene_action_pending_ = false;
      saveScene(false);
      if (hasLocalEdits()) {
        show_unsaved_prompt_ = true;
      } else {
        execute_scene_action_pending_ = true;
      }
    }
    if (execute_scene_action_pending_) {
      executePendingSceneAction();
    }
    if (preview_rebuild_pending_) {
      preview_rebuild_pending_ = false;
      const std::string selected_id = std::move(preview_pending_selection_);
      const std::optional<ToolMode> pending_tool = preview_pending_tool_;
      preview_pending_tool_.reset();
      rebuildPreview();
      if (!selected_id.empty()) {
        if (findFoliageLayer(selected_id) != nullptr) {
          focusFoliageLayer(selected_id, true);
        } else {
          selection_ = {SelectionKind::Entity, selected_id};
        }
        revalidateSelection();
      }
      if (pending_tool.has_value()) {
        tool_ = *pending_tool;
      }
    }
    resizeViewportTarget();
    updateCatalogScan();
    pollSceneBake();
    updateEditorCamera(dt);
    updatePrefabPlacementPreview();
    updateTransformGizmo();
    updateViewportInteraction();
    drawEditorGrid();
    drawSceneEditorOverlays();
    updateExternalFiles();
    updateRecovery();
  }

  void onShutdown() override {
    if (bake_shared_) bake_shared_->cancel_requested.store(true);
    if (bake_future_.valid()) {
      try {
        (void)bake_future_.get();
      } catch (...) {
      }
    }
    finishGizmoDrag(false);
    finishTerrainStroke();
    finishFoliageStroke();
    if (dirty()) {
      persistTerrainPreview();
      for (auto& layer : foliage_layers_) persistFoliagePreview(layer);
      std::string recovery_error;
      if (!writeRecovery(content_root_,
                         scene_path_,
                         scenes::sceneDocumentToJson(document_),
                         &recovery_error)) {
        spdlog::warn("Failed to write scene editor recovery: {}", recovery_error);
      }
    }
    std::string ignored;
    saveEditorSettings(content_root_, settings_, &ignored);
    if (console_logger_ && console_sink_) {
      auto& sinks = console_logger_->sinks();
      sinks.erase(std::remove(sinks.begin(), sinks.end(), console_sink_),
                  sinks.end());
    }
    console_sink_.reset();
    console_logger_.reset();
    destroyPreview();
    if (graphics != nullptr && viewport_target_ != rendering::kDefaultRenderTarget) {
      graphics->destroyRenderTarget(viewport_target_);
      viewport_target_ = rendering::kDefaultRenderTarget;
    }
  }

  void drawUi(app::UIContext& context) {
    panel_item_active_ = false;
    drawMainMenu();
    capturePanelInteraction();
    drawToolbar();
    capturePanelInteraction();
    updateWorkspaceLayout();
    drawHierarchyPanel();
    capturePanelInteraction();
    drawBottomPanel();
    capturePanelInteraction();
    drawInspector();
    capturePanelInteraction();
    drawViewport(context);
    if (ImGui::GetCurrentContext() != nullptr) {
      const ImGuiIO& io = ImGui::GetIO();
      const bool popup_open = ImGui::IsPopupOpen(
          "", ImGuiPopupFlags_AnyPopupId |
                  ImGuiPopupFlags_AnyPopupLevel);
      const bool drag_drop_active = ImGui::GetDragDropPayload() != nullptr;
      const bool navigation_can_acquire =
          viewport_item_hovered_ && !popup_open && !drag_drop_active;
      viewport_primary_owned_ = io.AppFocusLost
                                    ? false
                                    : updateViewportButtonOwnership(
                                          viewport_primary_owned_,
                                          io.MouseDown[ImGuiMouseButton_Left],
                                          io.MouseClicked[ImGuiMouseButton_Left],
                                          viewport_item_hovered_);
      viewport_middle_owned_ = io.AppFocusLost
                                   ? false
                                   : updateViewportButtonOwnership(
                                         viewport_middle_owned_,
                                         io.MouseDown[ImGuiMouseButton_Middle],
                                         io.MouseClicked[ImGuiMouseButton_Middle],
                                         navigation_can_acquire);
      viewport_right_owned_ = io.AppFocusLost
                                  ? false
                                  : updateViewportButtonOwnership(
                                        viewport_right_owned_,
                                        io.MouseDown[ImGuiMouseButton_Right],
                                        io.MouseClicked[ImGuiMouseButton_Right],
                                        navigation_can_acquire);
      const bool navigation_focus_requested =
          navigation_can_acquire &&
          (io.MouseClicked[ImGuiMouseButton_Middle] ||
           io.MouseClicked[ImGuiMouseButton_Right]);
      if (navigation_focus_requested) {
        // RMB/MMB over the viewport is an explicit focus transfer. Clear a
        // lingering text/slider ActiveId so it cannot keep keyboard capture
        // latched and make fly controls appear to stop working.
        ImGui::SetWindowFocus("Viewport");
        panel_item_active_ = false;
        finishDocumentPropertyEditNow();
      }
    }
    drawWorkspaceSplitters();
    capturePanelInteraction();
    drawStatusBar();
    drawModals();
    capturePanelInteraction();
    // Sample inside the Scene Editor's active ImGui frame. Engine update runs
    // before ImGui::NewFrame(), so sampling from onUpdate() would observe the
    // previous context/frame and lose the input events queued this tick.
    viewport_input_ = captureViewportInputSnapshot();
  }

 private:
  void loadEditorFonts() {
    std::vector<std::filesystem::path> roots{
        executable_directory_ / "assets" / "fonts",
    };
#if defined(KARMA_SCENE_EDITOR_FONT_SOURCE_DIR)
    roots.emplace_back(KARMA_SCENE_EDITOR_FONT_SOURCE_DIR);
#endif
    std::filesystem::path regular_path;
    std::filesystem::path semibold_path;
    for (const std::filesystem::path& root : roots) {
      const std::filesystem::path regular = root / "Inter-Regular.ttf";
      const std::filesystem::path semibold = root / "Inter-SemiBold.ttf";
      if (pathExistsNoThrow(regular) && pathExistsNoThrow(semibold)) {
        regular_path = regular;
        semibold_path = semibold;
        break;
      }
    }
    if (regular_path.empty()) {
      last_error_ =
          "Scene Editor font load failed; using the built-in fallback font";
      return;
    }

    ImGuiIO& io = ImGui::GetIO();
    ImFontConfig regular_config{};
    regular_config.OversampleH = 2;
    regular_config.OversampleV = 1;
    ImFont* regular = io.Fonts->AddFontFromFileTTF(
        regular_path.string().c_str(), 15.0f, &regular_config);
    if (regular != nullptr) io.FontDefault = regular;
    ImFontConfig semibold_config{};
    semibold_config.OversampleH = 2;
    semibold_config.OversampleV = 1;
    editor_semibold_font_ = io.Fonts->AddFontFromFileTTF(
        semibold_path.string().c_str(), 15.0f, &semibold_config);
    if (regular == nullptr || editor_semibold_font_ == nullptr) {
      editor_semibold_font_ = regular;
      last_error_ =
          "Scene Editor font load failed; using the available fallback font";
      return;
    }
  }

  void capturePanelInteraction() {
    if (!viewport_primary_owned_ && !viewport_middle_owned_ &&
        !viewport_right_owned_) {
      panel_item_active_ |= ImGui::IsAnyItemActive();
    }
  }

  void updateWorkspaceLayout() {
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    workspace_top_ = ImGui::GetFrameHeight() + 44.0f;
    workspace_height_ = std::max(display.y - workspace_top_ - 24.0f, 0.0f);
    workspace_layout_ = resolveEditorWorkspaceLayout(
        settings_.panel_layout, display.x, workspace_height_);
  }

  void bindInput() {
    input->bindKey("editor_forward", platform::Key::W);
    input->bindKey("editor_backward", platform::Key::S);
    input->bindKey("editor_left", platform::Key::A);
    input->bindKey("editor_right", platform::Key::D);
    input->bindKey("editor_down", platform::Key::Q);
    input->bindKey("editor_up", platform::Key::E);
    input->bindKey("editor_fast", platform::Key::LeftShift);
    input->bindKey("editor_frame", platform::Key::F, app::Trigger::Pressed);
    input->bindKey("editor_new", platform::Key::N, app::Trigger::Pressed);
    input->setRequiredModifiers("editor_new", platform::Modifiers{.control = true});
    input->bindKey("editor_open", platform::Key::O, app::Trigger::Pressed);
    input->setRequiredModifiers("editor_open", platform::Modifiers{.control = true});
    input->bindKey("editor_undo", platform::Key::Z, app::Trigger::Pressed);
    input->setRequiredModifiers("editor_undo", platform::Modifiers{.control = true});
    input->bindKey("editor_redo", platform::Key::Y, app::Trigger::Pressed);
    input->setRequiredModifiers("editor_redo", platform::Modifiers{.control = true});
    input->bindKey("editor_duplicate", platform::Key::D, app::Trigger::Pressed);
    input->setRequiredModifiers("editor_duplicate",
                                platform::Modifiers{.control = true});
    input->bindKey("editor_save", platform::Key::S, app::Trigger::Pressed);
    input->setRequiredModifiers("editor_save", platform::Modifiers{.control = true});
    input->bindKey("editor_delete", platform::Key::Delete, app::Trigger::Pressed);
    input->bindKey("editor_orbit_modifier", platform::Key::LeftAlt);
    input->bindKey("editor_orbit_modifier", platform::Key::RightAlt);
    input->bindKey("editor_gizmo_translate", platform::Key::W,
                   app::Trigger::Pressed);
    input->bindKey("editor_gizmo_translate", platform::Key::G,
                   app::Trigger::Pressed);
    input->bindKey("editor_gizmo_rotate", platform::Key::E,
                   app::Trigger::Pressed);
    input->bindKey("editor_gizmo_scale", platform::Key::R,
                   app::Trigger::Pressed);
    input->bindKey("editor_gizmo_scale", platform::Key::T,
                   app::Trigger::Pressed);
    input->bindKey("editor_cancel", platform::Key::Escape,
                   app::Trigger::Pressed);
    input->bindMouse("editor_look", platform::MouseButton::Right);
    input->bindMouse("editor_pan", platform::MouseButton::Middle);
    input->bindMouse("editor_primary", platform::MouseButton::Left);
    input->bindMouse("editor_primary", platform::MouseButton::Left,
                     app::Trigger::Pressed);
  }

  void scanCatalog(bool rebuild_preview = false) {
    catalog_rebuild_preview_ = catalog_rebuild_preview_ || rebuild_preview;
    if (catalog_future_.valid()) {
      catalog_rescan_requested_ = true;
      return;
    }
    const std::filesystem::path content_root = content_root_;
    const std::vector<std::filesystem::path> asset_roots = settings_.asset_roots;
    catalog_future_ = std::async(
        std::launch::async,
        [content_root, asset_roots] {
          CatalogBuild build{};
          build.result = build.catalog.scan(content_root, asset_roots);
          return build;
        });
  }

  void updateCatalogScan() {
    if (!catalog_future_.valid() ||
        catalog_future_.wait_for(std::chrono::seconds(0)) !=
            std::future_status::ready) {
      return;
    }
    const bool rescan = catalog_rescan_requested_;
    const bool rebuild_preview = catalog_rebuild_preview_;
    catalog_rescan_requested_ = false;
    catalog_rebuild_preview_ = false;
    try {
      CatalogBuild build = catalog_future_.get();
      catalog_ = std::move(build.catalog);
      catalog_diagnostics_ = std::move(build.result.diagnostics);
      if (rebuild_preview && !rescan) rebuildPreview();
    } catch (const std::exception& error) {
      last_error_ = std::string("Asset indexing failed: ") + error.what();
    }
    if (rescan) scanCatalog(rebuild_preview);
  }

  void destroyPreview() {
    destroyPrefabPlacementPreview();
    if (terrain_runtime_ != nullptr && terrain_entity_.isValid()) {
      terrain_runtime_->clearSingleImageTileOverride(terrain_entity_);
    }
    if (foliage_runtime_ != nullptr) foliage_runtime_->clearLayerOverrides();
    terrain_canvas_.reset();
    terrain_entity_ = {};
    terrain_entity_id_.clear();
    terrain_authoring_valid_ = true;
    foliage_layers_.clear();
    prefab_saved_root_transforms_.clear();
    if (preview_.success || !preview_.entities.empty()) {
      scenes::destroyScene(*world, *scene, preview_);
    }
    preview_ = {};
  }

  void rebuildPreview() {
    try {
      cancelPrefabPlacement();
      finishGizmoDrag(false);
      finishTerrainStroke();
      finishFoliageStroke();
      finishDocumentPropertyEditNow(false);
      destroyPreview();
      scenes::SceneDocument preview_document = document_;
      preview_document.cameras.clear();
      preview_document.reference_root = content_root_;
      scenes::SceneInstantiateDesc desc{};
      desc.reference_root = content_root_;
      desc.autoplay_gltf_animations = false;
      preview_ =
          scenes::instantiateScene(*world, *scene, *assets, preview_document, desc);
      if (!preview_.success) {
        last_error_ = joinDiagnostics(preview_.diagnostics);
        revalidateSelection();
        return;
      }
      cachePrefabRootTransforms();
      sanitizePreviewSimulation();
      loadEditableTerrain();
      loadFoliageLayers();
      if (world->isAlive(editor_camera_)) {
        world->get<components::CameraComponent>(editor_camera_).is_primary = true;
      }
      if (material_draft_dirty_) previewMaterialDraft();
      revalidateSelection();
    } catch (const std::exception& error) {
      try {
        destroyPreview();
      } catch (...) {
      }
      last_error_ = std::string("Failed to rebuild scene preview: ") + error.what();
    }
  }

  void cachePrefabRootTransforms() {
    for (const scenes::ScenePrefabInstance& prefab : document_.prefab_instances) {
      const auto runtime = preview_.prefab_roots_by_id.find(prefab.id);
      if (runtime == preview_.prefab_roots_by_id.end() ||
          !world->isAlive(runtime->second) ||
          !world->has<components::TransformComponent>(runtime->second)) {
        continue;
      }
      prefab_saved_root_transforms_[prefab.id] = sceneTransformRelativeTo(
          prefab.transform,
          fromRuntimeTransform(
              world->get<components::TransformComponent>(runtime->second)));
    }
  }

  void sanitizePreviewSimulation() {
    for (world::Entity entity : preview_.entities) {
      if (!world->isAlive(entity)) continue;
      world->remove<components::RigidbodyComponent>(entity);
      world->remove<components::CharacterControllerComponent>(entity);
      world->remove<components::PhysicsConstraintComponent>(entity);
      world->remove<components::PhysicsSoftBodyComponent>(entity);
      world->remove<components::PhysicsVehicleComponent>(entity);
      world->remove<components::PhysicsBodyForcesComponent>(entity);
      world->remove<components::ParticleEmitterComponent>(entity);
      world->remove<components::ParticleEffectComponent>(entity);
      world->remove<components::LightPulseComponent>(entity);
      world->remove<components::AudioSourceComponent>(entity);
      if (world->has<components::NavMeshComponent>(entity)) {
        auto& nav = world->get<components::NavMeshComponent>(entity);
        nav.build_on_start = false;
        nav.rebuild_requested = false;
      }
      if (world->has<components::NavTileCacheComponent>(entity)) {
        auto& cache = world->get<components::NavTileCacheComponent>(entity);
        cache.build_on_start = false;
        cache.rebuild_requested = false;
      }
      if (world->has<components::NavCrowdComponent>(entity)) {
        auto& crowd = world->get<components::NavCrowdComponent>(entity);
        crowd.build_on_start = false;
        crowd.rebuild_requested = false;
        crowd.simulation_paused = true;
      }
      if (world->has<components::NavMeshAgentComponent>(entity)) {
        world->get<components::NavMeshAgentComponent>(entity).enabled = false;
      }
      if (world->has<components::NavCrowdAgentComponent>(entity)) {
        world->get<components::NavCrowdAgentComponent>(entity).enabled = false;
      }
      if (world->has<components::NavTileCacheObstacleComponent>(entity)) {
        world->get<components::NavTileCacheObstacleComponent>(entity).enabled = false;
      }
      if (world->has<components::TerrainComponent>(entity)) {
        world->remove<components::ColliderComponent>(entity);
      }
    }
  }

  void createEditorCamera() {
    if (world->isAlive(editor_camera_)) return;
    editor_camera_ = world->createEntity();
    world->setName(editor_camera_, "Scene Editor Camera");
    components::TransformComponent transform{};
    camera_navigation_ = ViewportNavigationState{};
    camera_navigation_.fly_speed = settings_.camera_move_speed;
    const ViewportCameraPose pose = viewportCameraPose(camera_navigation_);
    transform.setPosition(pose.position);
    transform.setRotation(pose.rotation);
    world->add(editor_camera_, transform);
    world->add(editor_camera_, components::CameraComponent{
                                   .near_clip = 0.05f,
                                   .far_clip = 20000.0f,
                                   .is_primary = true,
                                   .render_to_texture = true,
                                   .render_target = viewport_target_,
                                   .render_target_key = "scene_editor_viewport",
                               });
    applyViewportRenderMode();
    scene->createNode(editor_camera_);
  }

  void applyViewportRenderMode() {
    if (!world->isAlive(editor_camera_) ||
        !world->has<components::CameraComponent>(editor_camera_)) {
      return;
    }
    world->get<components::CameraComponent>(editor_camera_)
        .shader_user_params[kEditorViewModeShaderParam] = math::Color{
        static_cast<float>(settings_.viewport_render_mode), 0.0f, 0.0f,
        0.0f};
  }

  void resizeViewportTarget() {
    const int requested_width = std::max(pending_viewport_width_, 1);
    const int requested_height = std::max(pending_viewport_height_, 1);
    if (graphics == nullptr || requested_width < static_cast<int>(kMinimumViewportExtent) ||
        requested_height < static_cast<int>(kMinimumViewportExtent) ||
        (requested_width == viewport_width_ && requested_height == viewport_height_)) {
      return;
    }
    if (viewport_target_ != rendering::kDefaultRenderTarget) {
      graphics->destroyRenderTarget(viewport_target_);
    }
    viewport_target_ = graphics->createRenderTarget(rendering::RenderTargetDesc{
        .width = requested_width, .height = requested_height, .depth = true, .stencil = false});
    viewport_width_ = requested_width;
    viewport_height_ = requested_height;
    if (world->isAlive(editor_camera_)) {
      auto& camera = world->get<components::CameraComponent>(editor_camera_);
      camera.render_target = viewport_target_;
      camera.render_target_key = "scene_editor_viewport";
    }
  }

  EditorPointerCaptureState pointerCaptureState() const {
    if (ImGui::GetCurrentContext() == nullptr) return {};
    const ImGuiIO& io = ImGui::GetIO();
    return EditorPointerCaptureState{
        .popup_open = ImGui::IsPopupOpen(
            "", ImGuiPopupFlags_AnyPopupId |
                    ImGuiPopupFlags_AnyPopupLevel),
        .drag_drop_active = ImGui::GetDragDropPayload() != nullptr,
        .panel_item_active = panel_item_active_,
        .want_capture_mouse = io.WantCaptureMouse,
        .viewport_item_hovered = viewport_item_hovered_,
        .viewport_navigation_owned =
            viewport_middle_owned_ || viewport_right_owned_,
    };
  }

  ViewportInputSnapshot captureViewportInputSnapshot() const {
    ViewportInputSnapshot input_fallback{};
    if (input != nullptr) {
      input_fallback.primary_down = input->actionDown("editor_primary");
      input_fallback.primary_pressed = input->actionPressed("editor_primary");
      input_fallback.middle_down = input->actionDown("editor_pan");
      input_fallback.right_down = input->actionDown("editor_look");
      input_fallback.orbit_modifier_down =
          input->actionDown("editor_orbit_modifier");
      input_fallback.fast_down = input->actionDown("editor_fast");
      input_fallback.move_forward = input->actionDown("editor_forward");
      input_fallback.move_backward = input->actionDown("editor_backward");
      input_fallback.move_left = input->actionDown("editor_left");
      input_fallback.move_right = input->actionDown("editor_right");
      input_fallback.move_down = input->actionDown("editor_down");
      input_fallback.move_up = input->actionDown("editor_up");
      input_fallback.delta_x = input->mouseDeltaX();
      input_fallback.delta_y = input->mouseDeltaY();
    }

    std::optional<ViewportInputSnapshot> imgui_snapshot;
    bool app_focus_lost = false;
    if (ImGui::GetCurrentContext() != nullptr) {
      const ImGuiIO& io = ImGui::GetIO();
      app_focus_lost = io.AppFocusLost;
      imgui_snapshot = ViewportInputSnapshot{
          .primary_down = io.MouseDown[ImGuiMouseButton_Left],
          .primary_pressed = io.MouseClicked[ImGuiMouseButton_Left],
          .middle_down = io.MouseDown[ImGuiMouseButton_Middle],
          .right_down = io.MouseDown[ImGuiMouseButton_Right],
          .orbit_modifier_down = ImGui::IsKeyDown(ImGuiKey_LeftAlt) ||
                                 ImGui::IsKeyDown(ImGuiKey_RightAlt),
          .fast_down = ImGui::IsKeyDown(ImGuiKey_LeftShift) ||
                       ImGui::IsKeyDown(ImGuiKey_RightShift),
          .move_forward = ImGui::IsKeyDown(ImGuiKey_W),
          .move_backward = ImGui::IsKeyDown(ImGuiKey_S),
          .move_left = ImGui::IsKeyDown(ImGuiKey_A),
          .move_right = ImGui::IsKeyDown(ImGuiKey_D),
          .move_down = ImGui::IsKeyDown(ImGuiKey_Q),
          .move_up = ImGui::IsKeyDown(ImGuiKey_E),
          .delta_x = io.MouseDelta.x,
          .delta_y = io.MouseDelta.y,
          .wheel = io.MouseWheel,
      };
    }
    return resolveViewportInputSnapshot(std::move(imgui_snapshot),
                                        input_fallback,
                                        app_focus_lost);
  }

  void updateEditorCamera(float dt) {
    if (!world->isAlive(editor_camera_)) return;
    ImGuiIO* io = ImGui::GetCurrentContext() != nullptr ? &ImGui::GetIO() : nullptr;
    const EditorPointerCaptureState pointer_capture = pointerCaptureState();
    const bool modal_open = pointer_capture.popup_open;
    const bool input_blocked = blocksViewportPointerInput(pointer_capture);

    const bool navigation_owned =
        viewport_middle_owned_ || viewport_right_owned_;
    const bool can_start_navigation =
        (viewport_hovered_ || navigation_owned) && !input_blocked;
    ViewportNavigationButtons navigation_buttons{};
    if (can_start_navigation ||
        camera_navigation_.mode != ViewportNavigationMode::None) {
      navigation_buttons.alt = viewport_input_.orbit_modifier_down;
      navigation_buttons.left = viewport_input_.primary_down;
      navigation_buttons.middle = viewport_input_.middle_down;
      navigation_buttons.right = viewport_input_.right_down;
    }
    ViewportNavigationMode requested_mode =
        unityViewportNavigationMode(navigation_buttons);
    if (gizmo_drag_.active() || input_blocked) {
      requested_mode = ViewportNavigationMode::None;
    }
    if (io != nullptr && io->AppFocusLost) {
      requested_mode = ViewportNavigationMode::None;
      finishGizmoDrag(false);
      finishTerrainStroke();
      finishFoliageStroke();
      finishDocumentPropertyEditNow();
      cancelPrefabPlacement();
    }
    if (requested_mode != camera_navigation_.mode) {
      if (requested_mode != ViewportNavigationMode::None) {
        finishTerrainStroke();
        finishFoliageStroke();
      }
      setViewportNavigationMode(camera_navigation_, requested_mode);
    }

    const bool keyboard_captured = io != nullptr &&
                                   (io->WantTextInput || io->WantCaptureKeyboard);
    if (!modal_open && tool_ == ToolMode::PlacePrefab &&
        input->actionPressed("editor_cancel")) {
      cancelPrefabPlacement();
    } else if (!modal_open && property_edit_active_ &&
               input->actionPressed("editor_cancel")) {
      cancelDocumentPropertyEdit();
    }
    if (!keyboard_captured && !modal_open) {
      if (viewport_hovered_ && input->actionPressed("editor_frame")) {
        frameSelection();
      }
      if (input->actionPressed("editor_new")) newScene();
      if (input->actionPressed("editor_open")) openSceneDialog();
      if (input->actionPressed("editor_undo")) undo();
      if (input->actionPressed("editor_redo")) redo();
      if (input->actionPressed("editor_duplicate")) duplicateSelected();
      if (input->actionPressed("editor_save")) {
        saveScene(io != nullptr && io->KeyShift);
      }
      if (input->actionPressed("editor_delete")) deleteSelection();
      const bool command_modifier = io != nullptr &&
                                    (io->KeyCtrl || io->KeySuper);
      const bool rmb_fly_chord = viewport_input_.right_down &&
                                 !viewport_input_.orbit_modifier_down;
      if (!command_modifier &&
          !rmb_fly_chord &&
          camera_navigation_.mode != ViewportNavigationMode::Fly &&
          input->actionPressed("editor_gizmo_translate")) {
        changeTransformTool(GizmoTool::Move);
      }
      if (!command_modifier &&
          !rmb_fly_chord &&
          camera_navigation_.mode != ViewportNavigationMode::Fly &&
          input->actionPressed("editor_gizmo_rotate")) {
        changeTransformTool(GizmoTool::Rotate);
      }
      if (!command_modifier &&
          !rmb_fly_chord &&
          camera_navigation_.mode != ViewportNavigationMode::Fly &&
          input->actionPressed("editor_gizmo_scale")) {
        changeTransformTool(GizmoTool::Scale);
      }
    }

    const float mouse_dx = viewport_input_.delta_x;
    const float mouse_dy = viewport_input_.delta_y;
    if (camera_navigation_.mode == ViewportNavigationMode::Orbit ||
        camera_navigation_.mode == ViewportNavigationMode::Fly) {
      applyViewportLookDrag(camera_navigation_, mouse_dx, mouse_dy,
                            kCameraLookSensitivity);
    } else if (camera_navigation_.mode == ViewportNavigationMode::Pan) {
      const float viewport_height = std::max(viewport_display_size_.y, 1.0f);
      const auto& camera =
          world->get<components::CameraComponent>(editor_camera_);
      applyViewportPanDrag(camera_navigation_, mouse_dx, mouse_dy,
                           viewport_height, camera.fov_y_degrees);
    } else if (camera_navigation_.mode == ViewportNavigationMode::Dolly) {
      const float speed = viewport_input_.fast_down ? 4.0f : 1.0f;
      applyViewportDollyDrag(camera_navigation_, mouse_dy,
                             0.01f * speed);
    }

    if (camera_navigation_.mode == ViewportNavigationMode::Fly) {
      ViewportFlyMotion motion{};
      if (viewport_input_.move_forward) motion.forward += 1.0f;
      if (viewport_input_.move_backward) motion.forward -= 1.0f;
      if (viewport_input_.move_right) motion.right += 1.0f;
      if (viewport_input_.move_left) motion.right -= 1.0f;
      if (viewport_input_.move_up) motion.up += 1.0f;
      if (viewport_input_.move_down) motion.up -= 1.0f;
      applyViewportFlyMotion(camera_navigation_, motion, dt,
                             viewport_input_.fast_down);
    }
    if (viewport_hovered_ && !input_blocked &&
        std::abs(viewport_input_.wheel) > 0.0f) {
      const float wheel = viewport_input_.wheel *
                          (viewport_input_.fast_down ? 4.0f : 1.0f);
      applyViewportWheel(camera_navigation_, wheel);
      if (camera_navigation_.mode == ViewportNavigationMode::Fly) {
        settings_.camera_move_speed = camera_navigation_.fly_speed;
      }
    }

    const ViewportCameraPose pose = viewportCameraPose(camera_navigation_);
    auto& transform =
        world->get<components::TransformComponent>(editor_camera_);
    transform.setPosition(pose.position);
    transform.setRotation(pose.rotation);
  }

  void frameSelection() {
    const world::Entity selected = selectedRuntimeEntity();
    if (!world->isAlive(selected) || !world->has<components::TransformComponent>(selected)) return;
    const math::Vec3 target =
        world->get<components::TransformComponent>(selected).getPosition();
    const auto& camera =
        world->get<components::CameraComponent>(editor_camera_);
    if (!frameViewportCamera(camera_navigation_, target, 1.0f,
                             camera.fov_y_degrees)) {
      return;
    }
    const ViewportCameraPose pose = viewportCameraPose(camera_navigation_);
    auto& camera_transform =
        world->get<components::TransformComponent>(editor_camera_);
    camera_transform.setPosition(pose.position);
    camera_transform.setRotation(pose.rotation);
  }

  std::optional<ViewportProjection> currentViewportProjection() const {
    if (viewport_display_size_.x <= 0.0f ||
        viewport_display_size_.y <= 0.0f ||
        !world->isAlive(editor_camera_)) {
      return std::nullopt;
    }
    const auto& transform =
        world->get<components::TransformComponent>(editor_camera_);
    const auto& camera =
        world->get<components::CameraComponent>(editor_camera_);
    ViewportProjection projection{
        .rect = {viewport_min_.x, viewport_min_.y,
                 viewport_display_size_.x, viewport_display_size_.y},
        .camera = {.position = transform.getPosition(),
                   .rotation = transform.getRotation(),
                   .fov_y_degrees = camera.fov_y_degrees,
                   .near_clip = camera.near_clip,
                   .far_clip = camera.far_clip},
    };
    return validViewportProjection(projection)
               ? std::optional<ViewportProjection>{projection}
               : std::nullopt;
  }

  std::optional<ViewportPoint> cursorViewportPoint() const {
    double mouse_x = 0.0;
    double mouse_y = 0.0;
    if (!input->mousePosition(mouse_x, mouse_y)) return std::nullopt;
    return ViewportPoint{static_cast<float>(mouse_x),
                         static_cast<float>(mouse_y)};
  }

  std::optional<ViewportRay> cursorRay() const {
    if (!viewport_hovered_ || viewport_width_ <= 0 || viewport_height_ <= 0 ||
        !world->isAlive(editor_camera_)) {
      return std::nullopt;
    }
    const auto projection = currentViewportProjection();
    const auto cursor = cursorViewportPoint();
    return projection && cursor
               ? viewportPointToWorldRay(*projection, *cursor)
               : std::nullopt;
  }

  std::optional<math::Vec3> cursorSurfacePoint() const {
    const auto ray = cursorRay();
    if (!ray) return std::nullopt;
    if (terrain_canvas_ && world->isAlive(terrain_entity_)) {
      const math::Vec3 origin =
          world->get<components::TransformComponent>(terrain_entity_).getPosition();
      if (auto hit = terrain_canvas_->raycast(ray->origin, ray->direction, origin, 100000.0f)) {
        return hit->position;
      }
    }
    if (std::abs(ray->direction.y) <= 1.0e-6f) return std::nullopt;
    const float distance = (construction_plane_y_ - ray->origin.y) / ray->direction.y;
    if (distance < 0.0f) return std::nullopt;
    return math::add(ray->origin, math::scale(ray->direction, distance));
  }

  void changeTransformTool(GizmoTool tool) {
    finishGizmoDrag(false);
    finishTerrainStroke();
    finishFoliageStroke();
    finishDocumentPropertyEditNow();
    tool_ = ToolMode::Select;
    gizmo_tool_ = tool;
  }

  void changeTool(ToolMode tool) {
    if (tool_ == tool) return;
    finishGizmoDrag(false);
    finishTerrainStroke();
    finishFoliageStroke();
    finishDocumentPropertyEditNow();
    if (tool_ == ToolMode::PlacePrefab && tool != ToolMode::PlacePrefab) {
      destroyPrefabPlacementPreview();
      pending_prefab_.clear();
      placement_world_point_.reset();
    }
    tool_ = tool;
  }

  void destroyPrefabPlacementPreview() {
    if (placement_preview_.has_value() &&
        world->isAlive(placement_preview_->root)) {
      prefabs::destroyPrefab(*world, *scene, placement_preview_->root);
    }
    placement_preview_.reset();
    placement_source_transform_ = {};
  }

  void cancelPrefabPlacement() {
    if (tool_ != ToolMode::PlacePrefab && !placement_preview_.has_value()) return;
    destroyPrefabPlacementPreview();
    pending_prefab_.clear();
    placement_world_point_.reset();
    tool_ = ToolMode::Select;
  }

  void beginPrefabPlacement(const std::filesystem::path& prefab_path) {
    const auto relative = contentRelativePath(content_root_, prefab_path);
    if (!relative) {
      last_error_ = "Prefab must be inside the content root";
      return;
    }
    const LegacyRenderMigrationReport migration =
        migratePrefabSourceClosure({prefab_path}, content_root_);
    if (!migration.success()) {
      last_error_ = migration.diagnostics.empty()
                        ? "Prefab migration failed before placement"
                        : joinDiagnostics(migration.diagnostics);
      return;
    }
    if (migration.changed) {
      prefab_asset_draft_status_ =
          "Automatically migrated prefab before placement; backup preserved";
      prefab_asset_draft_.reset();
      scanCatalog(false);
    }
    const prefabs::PrefabLoadResult loaded =
        prefabs::loadPrefabDocument(prefab_path);
    if (!loaded.success()) {
      last_error_ = loaded.diagnostics.empty()
                        ? "Prefab could not be loaded for placement"
                        : joinDiagnostics(loaded.diagnostics);
      return;
    }

    finishGizmoDrag(false);
    finishTerrainStroke();
    finishFoliageStroke();
    finishDocumentPropertyEditNow();
    destroyPrefabPlacementPreview();
    tool_ = ToolMode::PlacePrefab;
    pending_prefab_ = prefab_path;
    placement_world_point_.reset();

    prefabs::PrefabInstantiateDesc desc{};
    desc.assets = assets;
    std::optional<prefabs::PrefabInstance> preview =
        prefabs::instantiatePrefab(*world, *scene, prefab_path, desc);
    if (!preview.has_value() || !preview->valid() ||
        !world->has<components::TransformComponent>(preview->root)) {
      if (preview.has_value() && preview->valid()) {
        prefabs::destroyPrefab(*world, *scene, preview->root);
      }
      pending_prefab_.clear();
      tool_ = ToolMode::Select;
      last_error_ = "Prefab preview could not be instantiated";
      return;
    }

    placement_source_transform_ = fromRuntimeTransform(
        world->get<components::TransformComponent>(preview->root));
    for (const world::Entity entity : preview->entities) {
      if (!world->isAlive(entity)) continue;
      world->remove<components::RigidbodyComponent>(entity);
      world->remove<components::CharacterControllerComponent>(entity);
      world->remove<components::ParticleEmitterComponent>(entity);
      world->remove<components::ParticleEffectComponent>(entity);
      world->remove<components::LightPulseComponent>(entity);
    }
    placement_preview_ = std::move(preview);
    last_error_.clear();
  }

  void updatePrefabPlacementPreview() {
    if (tool_ != ToolMode::PlacePrefab || !placement_preview_.has_value() ||
        !world->isAlive(placement_preview_->root)) {
      return;
    }
    const auto surface = cursorSurfacePoint();
    if (!surface.has_value()) return;
    math::Vec3 point = snapPrefabPlacementPoint(
        *surface, settings_.snap_enabled, settings_.grid_size);
    if (terrain_canvas_ && world->isAlive(terrain_entity_) &&
        world->has<components::TransformComponent>(terrain_entity_)) {
      const math::Vec3 terrain_origin =
          world->get<components::TransformComponent>(terrain_entity_)
              .getPosition();
      if (const auto height = terrain_canvas_->sampleWorldHeight(
              point.x, point.z, terrain_origin)) {
        point.y = *height;
      }
    }
    placement_world_point_ = point;
    const scenes::SceneTransform placed{.position = point};
    const scenes::SceneTransform final =
        composeSceneTransforms(placed, placement_source_transform_);
    auto& transform = world->get<components::TransformComponent>(
        placement_preview_->root);
    transform.setLocalPosition(final.position);
    transform.setLocalRotation(final.rotation);
    transform.setLocalScale(final.scale);
    transform.setWorldPosition(final.position);
    transform.setWorldRotation(final.rotation);
    transform.setWorldScale(final.scale);
    world::updateWorldTransforms(*world, *scene);
  }

  std::optional<scenes::SceneTransform> selectedParentWorldTransform(
      world::Entity selected) const {
    const world::NodeId selected_node = scene->findNode(selected);
    if (!scene->isAlive(selected_node)) return std::nullopt;
    world::NodeId parent_node = scene->get(selected_node).parent;
    size_t remaining = scene->nodes().size();
    while (scene->isAlive(parent_node) && remaining-- > 0u) {
      const world::Node& parent = scene->get(parent_node);
      if (world->isAlive(parent.entity) &&
          world->has<components::TransformComponent>(parent.entity)) {
        return fromRuntimeWorldTransform(
            world->get<components::TransformComponent>(parent.entity));
      }
      parent_node = parent.parent;
    }
    return std::nullopt;
  }

  void applyGizmoTransform(const scenes::SceneTransform& local,
                           const scenes::SceneTransform& world_transform,
                           bool sync_document) {
    const world::Entity selected = selectedRuntimeEntity();
    if (!world->isAlive(selected) ||
        !world->has<components::TransformComponent>(selected)) {
      return;
    }
    auto& transform = world->get<components::TransformComponent>(selected);
    transform.setLocalPosition(local.position);
    transform.setLocalRotation(local.rotation);
    transform.setLocalScale(local.scale);
    // Gizmo input runs before the hierarchy update. Keep renderer-facing world
    // values coherent with the authored local values for this frame.
    transform.setWorldPosition(world_transform.position);
    transform.setWorldRotation(world_transform.rotation);
    transform.setWorldScale(world_transform.scale);
    if (sync_document) syncSelectionTransform(transform);
  }

  void finishGizmoDrag(bool cancel) {
    if (!gizmo_drag_.active()) {
      gizmo_active_ = false;
      return;
    }
    const std::optional<GizmoDragCompletion> completion =
        cancel ? gizmo_drag_.cancel() : gizmo_drag_.finish();
    gizmo_active_ = false;
    if (!completion) return;
    if (completion->cancelled) {
      document_ = gizmo_before_;
      applyGizmoTransform(completion->before_local,
                          completion->before_world,
                          false);
    } else if (completion->commit) {
      pushDocumentCommand("Transform", std::move(gizmo_before_));
    }
    gizmo_before_ = {};
  }

  void updateTransformGizmo() {
    gizmo_hovered_ = false;
    gizmo_hot_handle_ = GizmoHandle::None;
    gizmo_active_ = gizmo_drag_.active();

    const auto projection = currentViewportProjection();
    const auto cursor = cursorViewportPoint();
    const world::Entity selected = selectedRuntimeEntity();
    if (tool_ != ToolMode::Select || !selection_.valid() || !projection ||
        !cursor || !world->isAlive(selected) ||
        !world->has<components::TransformComponent>(selected)) {
      if (gizmo_drag_.active()) finishGizmoDrag(false);
      gizmo_geometry_ = {};
      return;
    }

    auto& transform = world->get<components::TransformComponent>(selected);
    auto rebuild_geometry = [&] {
      gizmo_geometry_ = buildGizmoGeometry(GizmoBuildDesc{
          .tool = gizmo_tool_,
          .space = gizmo_space_,
          .world_transform = fromRuntimeWorldTransform(transform),
          .projection = *projection,
          .apparent_size_pixels = 96.0f,
      });
    };
    rebuild_geometry();

    if (gizmo_drag_.active()) {
      gizmo_hot_handle_ = gizmo_drag_.handle();
      gizmo_hovered_ = true;
      if (input->actionPressed("editor_cancel")) {
        finishGizmoDrag(true);
        rebuild_geometry();
        return;
      }
      if (!viewport_input_.primary_down) {
        finishGizmoDrag(false);
        rebuild_geometry();
        return;
      }
      if (const auto update = gizmo_drag_.update(*cursor)) {
        if (update->changed) {
          applyGizmoTransform(update->local_transform,
                              update->world_transform,
                              true);
          rebuild_geometry();
        }
      }
      gizmo_active_ = true;
      return;
    }

    const bool can_interact = viewport_hovered_ &&
                              camera_navigation_.mode ==
                                  ViewportNavigationMode::None &&
                              !blocksViewportPointerInput(pointerCaptureState());
    if (!can_interact) return;

    const auto hit = hitTestGizmo(gizmo_geometry_, *projection, *cursor);
    if (!hit) return;
    gizmo_hovered_ = true;
    gizmo_hot_handle_ = hit->handle;
    if (!viewport_input_.primary_pressed) return;

    const GizmoDragBegin begin{
        .handle = hit->handle,
        .space = gizmo_space_,
        .projection = *projection,
        .local_transform = fromRuntimeTransform(transform),
        .world_transform = fromRuntimeWorldTransform(transform),
        .parent_world_transform = selectedParentWorldTransform(selected),
        .world_size = gizmo_geometry_.world_size,
        .snap = {.enabled = settings_.snap_enabled,
                 .translation_step = settings_.grid_size,
                 .rotation_step_degrees = 15.0f,
                 .scale_step = 0.1f},
    };
    gizmo_before_ = document_;
    if (gizmo_drag_.begin(begin, *cursor)) {
      gizmo_active_ = true;
      gizmo_hot_handle_ = hit->handle;
    } else {
      gizmo_before_ = {};
    }
  }

  void updateViewportInteraction() {
    if (!viewport_hovered_) {
      finishTerrainStroke();
      finishFoliageStroke();
      return;
    }
    if (gizmo_active_) return;
    if (blocksViewportPointerInput(pointerCaptureState())) {
      finishTerrainStroke();
      finishFoliageStroke();
      return;
    }
    if (camera_navigation_.mode != ViewportNavigationMode::None ||
        viewport_input_.orbit_modifier_down ||
        viewport_input_.middle_down ||
        viewport_input_.right_down) {
      finishTerrainStroke();
      finishFoliageStroke();
      return;
    }

    if (gizmo_hovered_ && viewport_input_.primary_pressed) return;
    if (tool_ == ToolMode::PlacePrefab &&
        viewport_input_.primary_pressed) {
      if (placement_world_point_.has_value()) {
        placePendingPrefab(*placement_world_point_);
      } else if (auto point = cursorSurfacePoint()) {
        placePendingPrefab(snapPrefabPlacementPoint(
            *point, settings_.snap_enabled, settings_.grid_size));
      }
      return;
    }
    if (tool_ == ToolMode::Select && viewport_input_.primary_pressed) {
      if (selectMarkerAtCursor()) {
        return;
      }
      selectObjectAtCursor(true);
      return;
    }
    if (isTerrainTool(tool_)) {
      updateTerrainStroke();
      return;
    }
    if (tool_ == ToolMode::PaintFoliage || tool_ == ToolMode::EraseFoliage) {
      updateFoliageStroke();
      return;
    }
  }

  bool selectMarkerAtCursor() {
    const auto projection = currentViewportProjection();
    const auto cursor = cursorViewportPoint();
    if (!projection || !cursor) return false;
    const SceneMarkerClassificationResult marker_result =
        classifySceneMarkers(document_, selection_);
    const auto marker = pickSceneMarker(marker_result.markers,
                                        *projection,
                                        *cursor,
                                        settings_.markers_visible);
    if (!marker) return false;
    if (marker->selection.kind == SelectionKind::Entity &&
        findFoliageLayer(marker->selection.id) != nullptr &&
        !terrain_entity_id_.empty()) {
      focusFoliageLayer(marker->selection.id, true);
    } else {
      selection_ = marker->selection;
    }
    gizmo_geometry_ = {};
    return true;
  }

  bool selectObjectAtCursor(bool clear_on_miss) {
    const auto projection = currentViewportProjection();
    const auto cursor = cursorViewportPoint();
    if (!projection || !cursor) return false;
    const auto ray = viewportPointToWorldRay(*projection, *cursor);
    if (!ray) return false;
    float best_distance = std::numeric_limits<float>::max();
    Selection best{};
    auto consider = [&](SelectionKind kind, const std::string& id, world::Entity entity) {
      if (!world->isAlive(entity) || !world->has<components::TransformComponent>(entity)) return;
      const math::Vec3 center = world->get<components::TransformComponent>(entity).getPosition();
      const math::Vec3 delta = math::subtract(center, ray->origin);
      const float along = math::dot(delta, ray->direction);
      if (along < 0.0f) return;
      const math::Vec3 closest = math::add(ray->origin, math::scale(ray->direction, along));
      const float radius = 1.25f;
      if (math::lengthSquared(math::subtract(center, closest)) <= radius * radius &&
          along < best_distance) {
        best_distance = along;
        best = Selection{kind, id};
      }
    };
    for (const auto& [id, entity] : preview_.entities_by_id) {
      const auto authored = std::find_if(
          document_.entities.begin(), document_.entities.end(),
          [&](const scenes::SceneEntity& candidate) {
            return candidate.id == id;
          });
      if (authored != document_.entities.end() &&
          sceneEntityHasRenderableContent(document_, *authored)) {
        consider(SelectionKind::Entity, id, entity);
      }
    }
    for (const auto& [id, entity] : preview_.prefab_roots_by_id) {
      consider(SelectionKind::Prefab, id, entity);
    }
    if (best.valid()) {
      if (best.kind == SelectionKind::Entity &&
          findFoliageLayer(best.id) != nullptr &&
          !terrain_entity_id_.empty()) {
        focusFoliageLayer(best.id, true);
      } else {
        selection_ = std::move(best);
      }
      gizmo_geometry_ = {};
      return true;
    }
    if (clear_on_miss) {
      selection_.clear();
      gizmo_geometry_ = {};
    }
    return false;
  }

  void updateTerrainStroke() {
    if (!terrain_canvas_ || !world->isAlive(terrain_entity_)) return;
    if (!terrain_authoring_valid_) {
      last_error_ = "Terrain editing is disabled because its source maps did not load";
      return;
    }
    const bool down = viewport_input_.primary_down;
    if (!down) {
      finishTerrainStroke();
      return;
    }
    const auto point = cursorSurfacePoint();
    if (!point) return;
    if (!terrain_stroke_active_) {
      terrain_stroke_active_ = true;
      terrain_before_heights_.assign(terrain_canvas_->heights().begin(),
                                     terrain_canvas_->heights().end());
      terrain_before_control_.assign(terrain_canvas_->controlRgba8().begin(),
                                     terrain_canvas_->controlRgba8().end());
      if (tool_ == ToolMode::SculptFlatten) {
        const auto sampled = terrain_canvas_->sampleWorldHeight(point->x, point->z,
            world->get<components::TransformComponent>(terrain_entity_).getPosition());
        if (sampled) {
          const auto& desc = terrain_canvas_->desc();
          flatten_target_ = (*sampled - desc.height_offset -
                             world->get<components::TransformComponent>(terrain_entity_)
                                 .getPosition().y) /
                            desc.height_scale;
        }
      }
    }
    const math::Vec3 origin =
        world->get<components::TransformComponent>(terrain_entity_).getPosition();
    const float local_x = point->x - origin.x;
    const float local_z = point->z - origin.z;
    bool changed = false;
    if (tool_ == ToolMode::PaintSplat) {
      changed = terrain_canvas_->paintLayer(local_x, local_z,
                                            static_cast<uint32_t>(splat_layer_), terrain_brush_);
    } else {
      scene_authoring::TerrainSculptMode mode = scene_authoring::TerrainSculptMode::Raise;
      switch (tool_) {
        case ToolMode::SculptLower: mode = scene_authoring::TerrainSculptMode::Lower; break;
        case ToolMode::SculptSmooth: mode = scene_authoring::TerrainSculptMode::Smooth; break;
        case ToolMode::SculptFlatten: mode = scene_authoring::TerrainSculptMode::Flatten; break;
        case ToolMode::SculptSetHeight: mode = scene_authoring::TerrainSculptMode::SetHeight; break;
        default: break;
      }
      changed = terrain_canvas_->applySculpt(local_x, local_z, mode, terrain_brush_,
                                              tool_ == ToolMode::SculptFlatten
                                                  ? flatten_target_
                                                  : set_height_target_);
    }
    const auto now = std::chrono::steady_clock::now();
    if (changed && terrain_runtime_ != nullptr && now >= terrain_preview_at_) {
      terrain_runtime_->setSingleImageTileOverride(terrain_entity_,
                                                   terrain_canvas_->buildTileData());
      terrain_preview_at_ = now + kTerrainPreviewInterval;
      pending_recovery_ = true;
      recovery_at_ = std::chrono::steady_clock::now() + kRecoveryDebounce;
    }
  }

  void finishTerrainStroke() {
    if (!terrain_stroke_active_ || !terrain_canvas_) return;
    terrain_stroke_active_ = false;
    EditCommand command{};
    command.kind = EditCommand::Kind::Terrain;
    command.label = toolName(tool_);
    command.before_heights = std::move(terrain_before_heights_);
    command.before_control = std::move(terrain_before_control_);
    command.after_heights.assign(terrain_canvas_->heights().begin(),
                                 terrain_canvas_->heights().end());
    command.after_control.assign(terrain_canvas_->controlRgba8().begin(),
                                 terrain_canvas_->controlRgba8().end());
    command.bytes = (command.before_heights.size() + command.after_heights.size()) * sizeof(float) +
                    command.before_control.size() + command.after_control.size();
    if (command.before_heights != command.after_heights ||
        command.before_control != command.after_control) {
      if (terrain_runtime_ != nullptr) {
        terrain_runtime_->setSingleImageTileOverride(
            terrain_entity_, terrain_canvas_->buildTileData());
      }
      pushCommand(std::move(command));
      persistTerrainPreview();
    }
  }

  void updateFoliageStroke() {
    FoliageLayerState* state = selectedFoliageLayer();
    if (state == nullptr || !terrain_canvas_ || !world->isAlive(terrain_entity_)) return;
    if (!state->source_valid) {
      last_error_ = "Foliage editing is disabled because its sidecar did not load";
      return;
    }
    const bool down = viewport_input_.primary_down;
    if (!down) {
      finishFoliageStroke();
      return;
    }
    const auto point = cursorSurfacePoint();
    if (!point) return;
    const world::Entity source = preview_.find(state->entity_id);
    if (!world->isAlive(source) ||
        !world->has<components::TransformComponent>(source)) {
      return;
    }
    const math::Vec3 terrain_origin =
        world->get<components::TransformComponent>(terrain_entity_).getPosition();
    const math::Vec3 foliage_origin =
        world->get<components::TransformComponent>(source).getPosition();
    const math::Vec3 local = math::subtract(*point, foliage_origin);
    foliage::FoliageEditResult edit{};
    if (tool_ == ToolMode::PaintFoliage) {
      size_t authored_instance_total = 0u;
      for (const FoliageLayerState& layer : foliage_layers_) {
        if (layer.source_valid) authored_instance_total += layer.layer.instanceCount();
      }
      if (authored_instance_total >= kMaxAuthoredFoliageInstances) {
        last_error_ = "Scene reached the editor limit of 1,000,000 foliage instances";
        return;
      }
      const size_t remaining =
          kMaxAuthoredFoliageInstances - authored_instance_total;
      const size_t update_budget =
          std::min(remaining, kMaxFoliageInstancesPerUpdate);
      foliage::FoliagePaintBrush brush = foliage_brush_;
      brush.center = local;
      brush.seed = foliage_stroke_seed_++;
      brush.min_height = foliage_min_height_;
      brush.max_height = foliage_max_height_;
      const double area = kPi * static_cast<double>(brush.radius) * brush.radius;
      if (area > 0.0) {
        brush.density = std::min(
            brush.density,
            static_cast<float>(static_cast<double>(update_budget) / area));
      }
      edit = state->layer.paint(brush, [&](float x, float z) {
        const float world_x = x + foliage_origin.x;
        const float world_z = z + foliage_origin.z;
        const auto height = terrain_canvas_->sampleWorldHeight(
            world_x, world_z, terrain_origin);
        if (!height) return std::optional<foliage::FoliageSurfaceSample>{};
        constexpr float sample_offset = 0.25f;
        const float left = terrain_canvas_->sampleWorldHeight(
            world_x - sample_offset, world_z, terrain_origin)
                               .value_or(*height);
        const float right = terrain_canvas_->sampleWorldHeight(
            world_x + sample_offset, world_z, terrain_origin)
                                .value_or(*height);
        const float back = terrain_canvas_->sampleWorldHeight(
            world_x, world_z - sample_offset, terrain_origin)
                               .value_or(*height);
        const float front = terrain_canvas_->sampleWorldHeight(
            world_x, world_z + sample_offset, terrain_origin)
                                .value_or(*height);
        const math::Vec3 normal = math::normalize(
            math::Vec3{left - right, sample_offset * 2.0f, back - front});
        return std::optional<foliage::FoliageSurfaceSample>{
            foliage::FoliageSurfaceSample{.height = *height - foliage_origin.y,
                                          .normal = normal}};
      });
      if (edit.added.size() > remaining) {
        foliage::FoliageEditResult excess{};
        excess.added.assign(edit.added.begin() +
                                static_cast<std::ptrdiff_t>(remaining),
                            edit.added.end());
        state->layer.applyEdit(excess, true);
        edit.added.resize(remaining);
      }
    } else {
      foliage_erase_.center = local;
      foliage_erase_.seed = foliage_stroke_seed_++;
      edit = state->layer.erase(foliage_erase_);
    }
    if (edit.empty()) return;
    foliage_stroke_active_ = true;
    active_foliage_entity_ = state->entity_id;
    foliage_stroke_edit_.added.insert(foliage_stroke_edit_.added.end(),
                                      edit.added.begin(), edit.added.end());
    foliage_stroke_edit_.removed.insert(foliage_stroke_edit_.removed.end(),
                                        edit.removed.begin(), edit.removed.end());
    foliage_stroke_edit_.affected_chunks.insert(foliage_stroke_edit_.affected_chunks.end(),
                                                edit.affected_chunks.begin(),
                                                edit.affected_chunks.end());
    const auto now = std::chrono::steady_clock::now();
    if (foliage_runtime_ != nullptr && world->isAlive(source) &&
        now >= foliage_preview_at_) {
      foliage_runtime_->setLayerOverride(source, state->layer);
      foliage_preview_at_ = now + kFoliagePreviewInterval;
    }
  }

  void finishFoliageStroke() {
    if (!foliage_stroke_active_) return;
    foliage_stroke_active_ = false;
    EditCommand command{};
    command.kind = EditCommand::Kind::Foliage;
    command.label = toolName(tool_);
    command.foliage_entity_id = active_foliage_entity_;
    command.foliage_edit = std::move(foliage_stroke_edit_);
    command.bytes = (command.foliage_edit.added.size() + command.foliage_edit.removed.size()) *
                    sizeof(foliage::FoliageInstanceEdit);
    pushCommand(std::move(command));
    if (FoliageLayerState* state = findFoliageLayer(active_foliage_entity_)) {
      const world::Entity source = preview_.find(active_foliage_entity_);
      if (foliage_runtime_ != nullptr && world->isAlive(source)) {
        foliage_runtime_->setLayerOverride(source, state->layer);
      }
      persistFoliagePreview(*state);
    }
    foliage_stroke_edit_ = {};
    active_foliage_entity_.clear();
  }

  void drawEditorGrid() {
    if (graphics == nullptr || !show_grid_) return;
    constexpr int lines = 40;
    const float spacing = std::max(settings_.grid_size, 0.01f);
    const float extent = lines * spacing;
    for (int i = -lines; i <= lines; ++i) {
      const float offset = i * spacing;
      const math::Color color = i == 0 ? math::Color{0.32f, 0.42f, 0.62f, 0.8f}
                                        : math::Color{0.16f, 0.18f, 0.22f, 0.38f};
      graphics->drawLine({-extent, construction_plane_y_, offset},
                         {extent, construction_plane_y_, offset}, color);
      graphics->drawLine({offset, construction_plane_y_, -extent},
                         {offset, construction_plane_y_, extent}, color);
    }
    if ((isTerrainTool(tool_) || tool_ == ToolMode::PaintFoliage ||
         tool_ == ToolMode::EraseFoliage) && viewport_hovered_) {
      if (const auto center = cursorSurfacePoint()) {
        const float radius = isTerrainTool(tool_) ? terrain_brush_.radius
            : (tool_ == ToolMode::PaintFoliage ? foliage_brush_.radius : foliage_erase_.radius);
        constexpr int segments = 48;
        for (int segment = 0; segment < segments; ++segment) {
          const float a = static_cast<float>(segment) / segments * kPi * 2.0f;
          const float b = static_cast<float>(segment + 1) / segments * kPi * 2.0f;
          math::Vec3 first{center->x + std::cos(a) * radius,
                           center->y + 0.08f,
                           center->z + std::sin(a) * radius};
          math::Vec3 second{center->x + std::cos(b) * radius,
                            center->y + 0.08f,
                            center->z + std::sin(b) * radius};
          graphics->drawLine(first, second, {0.95f, 0.7f, 0.15f, 1.0f});
        }
      }
    }
  }

  void drawSceneEditorOverlays() {
    if (graphics == nullptr) return;
    const auto projection = currentViewportProjection();
    if (!projection) return;

    const SceneMarkerClassificationResult classified =
        classifySceneMarkers(document_, selection_);
    for (const SceneMarker& marker : classified.markers) {
      if (!sceneMarkerVisible(marker, settings_.markers_visible)) continue;
      const SceneMarkerGeometry geometry =
          buildSceneMarkerGeometry(marker, *projection);
      for (const SceneMarkerLine& line : geometry.lines) {
        graphics->drawLine(
            line.from,
            line.to,
            line.color,
            line.layer == SceneMarkerLineLayer::Bounds,
            line.layer == SceneMarkerLineLayer::Bounds ? 1.0f : 2.0f);
      }
    }

    const world::Entity selected_runtime = selectedRuntimeEntity();
    if (selection_.valid() && world->isAlive(selected_runtime) &&
        world->has<components::TransformComponent>(selected_runtime) &&
        world->has<components::ColliderComponent>(selected_runtime)) {
      const auto& collider =
          world->get<components::ColliderComponent>(selected_runtime);
      const ColliderWireGeometry wire = buildColliderWireGeometry(
          collider,
          fromRuntimeWorldTransform(
              world->get<components::TransformComponent>(selected_runtime)));
      const math::Color collider_color = collider.is_trigger
                                             ? math::Color{0.35f, 0.92f, 0.85f, 0.95f}
                                             : math::Color{0.45f, 0.95f, 0.35f, 0.95f};
      for (const ColliderWireLine& line : wire.lines) {
        graphics->drawLine(line.from, line.to, collider_color, true, 1.6f);
      }
    }

    if (tool_ != ToolMode::Select || !selection_.valid()) return;
    const GizmoHandle active_handle = gizmo_drag_.active()
                                          ? gizmo_drag_.handle()
                                          : GizmoHandle::None;
    for (const GizmoLineSegment& line : gizmo_geometry_.lines) {
      math::Color color = line.color;
      float thickness = line.thickness_pixels;
      if (line.handle != GizmoHandle::None &&
          (line.handle == gizmo_hot_handle_ ||
           line.handle == active_handle)) {
        color.r = std::min(1.0f, color.r * 1.4f + 0.2f);
        color.g = std::min(1.0f, color.g * 1.4f + 0.2f);
        color.b = std::min(1.0f, color.b * 1.4f + 0.2f);
        color.a = 1.0f;
        thickness += 1.0f;
      }
      graphics->drawLine(line.start, line.end, color, false, thickness);
    }
  }

  void loadEditableTerrain() {
    for (const scenes::SceneEntity& authored : document_.entities) {
      const world::Entity entity = preview_.find(authored.id);
      if (!world->isAlive(entity) || !world->has<components::TerrainComponent>(entity)) continue;
      const auto& terrain = world->get<components::TerrainComponent>(entity);
      if (terrain.source != components::TerrainSourceType::SingleImage || terrain_canvas_) continue;
      assets::ScalarImageLoadOptions options{};
      switch (terrain.height_format) {
        case components::TerrainHeightFormat::ImageFile:
          options.format = assets::ScalarImageFormat::ImageFile;
          break;
        case components::TerrainHeightFormat::Raw16Unsigned:
          options.format = assets::ScalarImageFormat::Raw16Unsigned;
          break;
        case components::TerrainHeightFormat::R32Float:
          options.format = assets::ScalarImageFormat::R32Float;
          break;
        default: options.format = assets::ScalarImageFormat::Auto; break;
      }
      options.raw_width = terrain.raw_width;
      options.raw_height = terrain.raw_height;
      options.little_endian = terrain.raw_little_endian;
      options.flip_y = terrain.flip_y;
      options.value_min = terrain.height_value_min;
      options.value_max = terrain.height_value_max;
      const std::filesystem::path height_path =
          !terrain.height_image.empty() ? terrain.height_image : terrain.heatmap_image;
      auto image = assets::loadScalarImage(height_path, options);
      if (!image) {
        last_error_ = "Failed to load editable terrain height image: " + height_path.string();
        continue;
      }
      scene_authoring::TerrainCanvasDesc canvas_desc{
          .resolution = terrain.tile_resolution,
          .control_resolution = terrain.tile_resolution,
          .terrain_size = terrain.terrain_size,
          .height_scale = terrain.height_scale,
          .height_offset = terrain.height_offset,
      };
      terrain_canvas_ = scene_authoring::TerrainCanvas::import(canvas_desc, *image);
      if (!terrain_canvas_) {
        last_error_ = "Terrain height image could not be imported into an authoring canvas";
        continue;
      }
      terrain_entity_ = entity;
      terrain_entity_id_ = authored.id;
      if (!terrain.control_image.empty() && !loadControlMap(terrain.control_image)) {
        terrain_authoring_valid_ = false;
      }
      if (terrain_runtime_ != nullptr) {
        terrain_runtime_->setSingleImageTileOverride(entity, terrain_canvas_->buildTileData());
      }
      if (terrain_authoring_valid_) establishTerrainWorkingCopy(terrain);
      break;
    }
  }

  bool loadControlMap(const std::filesystem::path& path) {
    if (!terrain_canvas_) return false;
    auto image = assets::loadRgba8Image(path);
    if (!image || !image->valid()) {
      last_error_ = "Failed to load editable terrain control map: " + path.string();
      return false;
    }
    auto target = terrain_canvas_->mutableControlRgba8();
    const uint32_t resolution = terrain_canvas_->controlResolution();
    for (uint32_t y = 0; y < resolution; ++y) {
      const int source_y = std::clamp(static_cast<int>((static_cast<double>(y) /
          std::max<uint32_t>(resolution - 1u, 1u)) * (image->height - 1)), 0, image->height - 1);
      for (uint32_t x = 0; x < resolution; ++x) {
        const int source_x = std::clamp(static_cast<int>((static_cast<double>(x) /
            std::max<uint32_t>(resolution - 1u, 1u)) * (image->width - 1)), 0, image->width - 1);
        const size_t source = (static_cast<size_t>(source_y) * image->width + source_x) * 4u;
        const size_t destination = (static_cast<size_t>(y) * resolution + x) * 4u;
        std::copy_n(image->pixels.begin() + static_cast<std::ptrdiff_t>(source),
                    4u,
                    target.begin() + static_cast<std::ptrdiff_t>(destination));
      }
    }
    return true;
  }

  void makeContentRelative(std::filesystem::path& path) const {
    if (!path.is_absolute()) return;
    if (const auto relative = contentRelativePath(content_root_, path)) path = *relative;
  }

  components::TerrainComponent portableTerrainComponent(
      components::TerrainComponent component) const {
    makeContentRelative(component.tile_directory);
    makeContentRelative(component.height_image);
    makeContentRelative(component.heatmap_image);
    makeContentRelative(component.color_image);
    makeContentRelative(component.control_image);
    for (auto& layer : component.material_layers) {
      makeContentRelative(layer.albedo_image);
      makeContentRelative(layer.normal_image);
      makeContentRelative(layer.roughness_image);
    }
    for (auto& map : component.data_maps) makeContentRelative(map.image);
    return component;
  }

  void establishTerrainWorkingCopy(
      const components::TerrainComponent& source_component) {
    if (!terrain_canvas_ || terrain_entity_id_.empty()) return;
    const std::filesystem::path directory = editorPreviewDirectory();
    const std::filesystem::path height =
        directory / (terrain_entity_id_ + "-height.r32");
    const std::filesystem::path control =
        directory / (terrain_entity_id_ + "-control.tga");
    const auto relative_height = contentRelativePath(content_root_, height);
    const auto relative_control = contentRelativePath(content_root_, control);
    if (!relative_height || !relative_control) {
      last_error_ = "Terrain working paths must remain inside the content root";
      terrain_authoring_valid_ = false;
      return;
    }
    const bool already_working =
        source_component.height_image.lexically_normal() ==
            height.lexically_normal() &&
        source_component.control_image.lexically_normal() ==
            control.lexically_normal();
    if (!already_working && !persistTerrainPreview()) {
      terrain_authoring_valid_ = false;
      return;
    }
    components::TerrainComponent authored =
        portableTerrainComponent(source_component);
    authored.source = components::TerrainSourceType::SingleImage;
    authored.height_image = *relative_height;
    authored.control_image = *relative_control;
    authored.height_format = components::TerrainHeightFormat::R32Float;
    authored.raw_width = terrain_canvas_->resolution();
    authored.raw_height = terrain_canvas_->resolution();
    authored.raw_little_endian = true;
    authored.flip_y = false;
    authored.height_value_min = 0.0f;
    authored.height_value_max = 1.0f;
    if (auto entity = findEntity(terrain_entity_id_);
        entity != document_.entities.end()) {
      entity->components["TerrainComponent"] =
          serializeTemporaryComponent(authored, "TerrainComponent");
    }
    auto& runtime = world->get<components::TerrainComponent>(terrain_entity_);
    runtime.height_image = height;
    runtime.control_image = control;
    runtime.height_format = components::TerrainHeightFormat::R32Float;
    runtime.raw_width = authored.raw_width;
    runtime.raw_height = authored.raw_height;
    runtime.raw_little_endian = true;
    runtime.flip_y = false;
    runtime.height_value_min = 0.0f;
    runtime.height_value_max = 1.0f;
  }

  void loadFoliageLayers() {
    foliage_layers_.clear();
    size_t authored_instance_total = 0u;
    if (foliage_runtime_ != nullptr) foliage_runtime_->setReferenceRoot(content_root_);
    for (const scenes::SceneEntity& authored : document_.entities) {
      const world::Entity entity = preview_.find(authored.id);
      if (!world->isAlive(entity) || !world->has<components::FoliageComponent>(entity)) continue;
      auto& component = world->get<components::FoliageComponent>(entity);
      std::string error;
      const auto index = foliage::readFoliageFileIndex(component.sidecar_path, &error);
      std::optional<foliage::FoliageDocument> foliage_document;
      if (index &&
          index->instance_count <=
              kMaxAuthoredFoliageInstances - authored_instance_total) {
        foliage_document = foliage::readFoliageFile(component.sidecar_path, &error);
      } else if (index) {
        error = "scene exceeds the editor limit of 1,000,000 foliage instances";
      }
      const std::filesystem::path working_path =
          editorPreviewDirectory() / (authored.id + ".kfoliage");
      std::error_code directory_error;
      std::filesystem::create_directories(working_path.parent_path(),
                                          directory_error);
      if (directory_error) {
        error = "failed to create foliage preview directory: " +
                directory_error.message();
      }
      FoliageLayerState state{
          .entity_id = authored.id,
          .name = authored.name.empty() ? "Foliage" : authored.name,
          .working_path = working_path,
          .source_valid = foliage_document.has_value() && !directory_error,
          .layer = foliage_document ? foliage::FoliageLayer(*foliage_document)
                                    : foliage::FoliageLayer(component.chunk_size)};
      const bool already_working =
          component.sidecar_path.lexically_normal() ==
          working_path.lexically_normal();
      if (!state.source_valid) {
        last_error_ = "Foliage layer '" + state.name + "': " + error;
      } else if (!already_working &&
                 !foliage::writeFoliageFile(working_path,
                                            state.layer.toDocument(), &error)) {
        state.source_valid = false;
        last_error_ = "Foliage layer '" + state.name + "': " + error;
      }
      std::optional<std::filesystem::path> relative_working;
      if (state.source_valid) {
        relative_working = contentRelativePath(content_root_, working_path);
        if (!relative_working) {
          state.source_valid = false;
          last_error_ = "Foliage working path must remain inside the content root";
        }
      }
      if (state.source_valid) {
        authored_instance_total += state.layer.instanceCount();
        component.sidecar_path = working_path;
        components::FoliageComponent authored_component = component;
        authored_component.sidecar_path = *relative_working;
        if (!authored_component.prefab_path.empty()) {
          const std::filesystem::path absolute_prefab =
              authored_component.prefab_path.is_absolute()
                  ? authored_component.prefab_path
                  : content_root_ / authored_component.prefab_path;
          if (const auto relative_prefab = contentRelativePath(
                  content_root_, absolute_prefab)) {
            authored_component.prefab_path = *relative_prefab;
          }
        }
        if (auto authored_entity = findEntity(authored.id);
            authored_entity != document_.entities.end()) {
          authored_entity->components["FoliageComponent"] =
              serializeTemporaryComponent(std::move(authored_component),
                                          "FoliageComponent");
        }
        if (foliage_runtime_ != nullptr) {
          foliage_runtime_->setLayerOverride(entity, state.layer);
        }
      }
      foliage_layers_.push_back(std::move(state));
    }
    if (findFoliageLayer(settings_.active_foliage_layer_id) == nullptr) {
      settings_.active_foliage_layer_id =
          foliage_layers_.empty() ? std::string{}
                                  : foliage_layers_.front().entity_id;
    }
  }

  void drawMainMenu() {
    if (!ImGui::BeginMainMenuBar()) return;
    if (ImGui::BeginMenu("File")) {
      if (ImGui::MenuItem("New", "Ctrl+N")) newScene();
      if (ImGui::MenuItem("Open...", "Ctrl+O")) openSceneDialog();
      if (ImGui::MenuItem("Save", "Ctrl+S")) saveScene(false);
      if (ImGui::MenuItem("Save As...", "Ctrl+Shift+S")) saveScene(true);
      if (ImGui::MenuItem("Revert to Saved", nullptr, false,
                          has_disk_version_ && hasLocalEdits())) {
        queueSceneLoad(scene_path_, true);
      }
      ImGui::Separator();
      if (ImGui::MenuItem("Add Asset Root...")) addAssetRootDialog();
      if (ImGui::MenuItem("Refresh Assets")) refreshAssets(true);
      ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Edit")) {
      const std::string undo_text = command_cursor_ > 0u
                                        ? "Undo " + commands_[command_cursor_ - 1u].label
                                        : "Undo";
      const std::string redo_text = command_cursor_ < commands_.size()
                                        ? "Redo " + commands_[command_cursor_].label
                                        : "Redo";
      if (ImGui::MenuItem(undo_text.c_str(), "Ctrl+Z", false, command_cursor_ > 0u)) undo();
      if (ImGui::MenuItem(redo_text.c_str(), "Ctrl+Y", false,
                          command_cursor_ < commands_.size())) redo();
      ImGui::Separator();
      if (ImGui::MenuItem("Duplicate", "Ctrl+D", false,
                          selection_.valid())) {
        duplicateSelected();
      }
      if (ImGui::MenuItem("Delete", "Delete", false, selection_.valid())) {
        deleteSelection();
      }
      ImGui::EndMenu();
    }
    ImGui::TextDisabled("%s%s", document_.name.c_str(), dirty() ? " *" : "");
    ImGui::EndMainMenuBar();
  }

  void drawToolbar() {
    const float menu_height = ImGui::GetFrameHeight();
    ImGui::SetNextWindowPos({0.0f, menu_height}, ImGuiCond_Always);
    ImGui::SetNextWindowSize({ImGui::GetIO().DisplaySize.x, 44.0f}, ImGuiCond_Always);
    ImGui::Begin("##scene_editor_toolbar", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar);
    auto tool_button = [&](ToolMode mode, const char* label) {
      const bool selected = tool_ == mode;
      if (selected) {
        ImGui::PushStyleColor(
            ImGuiCol_Button,
            ImGui::GetStyleColorVec4(ImGuiCol_HeaderActive));
      }
      if (ImGui::Button(label) && !selected) {
        changeTool(mode);
      }
      if (selected) ImGui::PopStyleColor();
      ImGui::SameLine();
    };
    tool_button(ToolMode::Select, "Select");
    auto gizmo_button = [&](GizmoTool operation, const char* label) {
      const bool selected = tool_ == ToolMode::Select &&
                            gizmo_tool_ == operation;
      if (selected) {
        ImGui::PushStyleColor(
            ImGuiCol_Button,
            ImGui::GetStyleColorVec4(ImGuiCol_HeaderActive));
      }
      if (ImGui::Button(label) && !selected) {
        changeTransformTool(operation);
      }
      if (selected) ImGui::PopStyleColor();
      ImGui::SameLine();
    };
    gizmo_button(GizmoTool::Move, "Move [W]");
    gizmo_button(GizmoTool::Rotate, "Rotate [E]");
    gizmo_button(GizmoTool::Scale, "Scale [R]");
    bool local = gizmo_space_ == GizmoSpace::Local;
    if (ImGui::Checkbox("Local", &local)) {
      finishGizmoDrag(false);
      finishDocumentPropertyEditNow();
      gizmo_space_ = local ? GizmoSpace::Local : GizmoSpace::World;
    }
    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();
    ImGui::Checkbox("Grid", &show_grid_);
    ImGui::SameLine();
    ImGui::Checkbox("Markers", &settings_.markers_visible);
    ImGui::SameLine();
    ImGui::Checkbox("Snap", &settings_.snap_enabled);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(80.0f);
    ImGui::DragFloat("Grid size", &settings_.grid_size, 0.05f, 0.05f, 100.0f, "%.2f");
    ImGui::SameLine();
    int view_mode = static_cast<int>(settings_.viewport_render_mode);
    ImGui::SetNextItemWidth(110.0f);
    if (ImGui::Combo("View", &view_mode,
                     "Rendered\0Diffuse\0Texture\0Wire\0")) {
      settings_.viewport_render_mode = static_cast<ViewportRenderMode>(
          std::clamp(view_mode, 0, 3));
      applyViewportRenderMode();
    }
    ImGui::End();
  }

  void drawHierarchyPanel() {
    ImGui::SetNextWindowPos({0.0f, workspace_top_}, ImGuiCond_Always);
    ImGui::SetNextWindowSize(
        {std::max(workspace_layout_.hierarchy_width, 1.0f),
         std::max(workspace_height_, 1.0f)},
        ImGuiCond_Always);
    ImGui::Begin("Hierarchy", nullptr,
                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                     ImGuiWindowFlags_NoSavedSettings);
    drawHierarchy();
    ImGui::End();
  }

  void drawBottomPanel() {
    const float x = workspace_layout_.hierarchy_width +
                    workspace_layout_.splitter_size;
    const float y = workspace_top_ + workspace_layout_.viewport_height +
                    workspace_layout_.splitter_size;
    ImGui::SetNextWindowPos({x, y}, ImGuiCond_Always);
    ImGui::SetNextWindowSize(
        {std::max(workspace_layout_.center_width, 1.0f),
         std::max(workspace_layout_.assets_height, 1.0f)},
        ImGuiCond_Always);
    ImGui::Begin("Workspace", nullptr,
                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                     ImGuiWindowFlags_NoSavedSettings);
    if (ImGui::BeginTabBar("##workspace_tabs")) {
      const auto tab_flags = [&](BottomPanelTab tab) {
        return !bottom_tab_initialized_ && settings_.bottom_panel_tab == tab
                   ? ImGuiTabItemFlags_SetSelected
                   : ImGuiTabItemFlags_None;
      };
      if (ImGui::BeginTabItem("Assets", nullptr,
                              tab_flags(BottomPanelTab::Assets))) {
        settings_.bottom_panel_tab = BottomPanelTab::Assets;
        drawAssetCatalog();
        ImGui::EndTabItem();
      }
      if (ImGui::BeginTabItem("Console", nullptr,
                              tab_flags(BottomPanelTab::Console))) {
        settings_.bottom_panel_tab = BottomPanelTab::Console;
        drawConsolePanel();
        ImGui::EndTabItem();
      }
      if (ImGui::BeginTabItem("Lighting", nullptr,
                              tab_flags(BottomPanelTab::Lighting))) {
        settings_.bottom_panel_tab = BottomPanelTab::Lighting;
        drawLightingPanel();
        ImGui::EndTabItem();
      }
      if (ImGui::BeginTabItem("Navigation", nullptr,
                              tab_flags(BottomPanelTab::Navigation))) {
        settings_.bottom_panel_tab = BottomPanelTab::Navigation;
        drawNavigationPanel();
        ImGui::EndTabItem();
      }
      ImGui::EndTabBar();
    }
    bottom_tab_initialized_ = true;
    ImGui::End();
  }

  void drawConsolePanel() {
    if (ImGui::Button("Clear") && console_sink_) {
      console_sink_->clear();
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(120.0f);
    ImGui::Combo("Level", &settings_.console_min_level,
                 "Trace\0Debug\0Info\0Warning\0Error\0Critical\0");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextWithHint("##console_filter", "Filter log output",
                             console_filter_, sizeof(console_filter_));

    const auto lowercase = [](std::string value) {
      std::transform(value.begin(), value.end(), value.begin(),
                     [](unsigned char character) {
                       return static_cast<char>(std::tolower(character));
                     });
      return value;
    };
    const std::string filter = lowercase(console_filter_);
    const std::vector<ConsoleEntry> entries =
        console_sink_ ? console_sink_->snapshot() : std::vector<ConsoleEntry>{};
    ImGui::BeginChild("##console_entries", {0.0f, 0.0f}, false,
                      ImGuiWindowFlags_HorizontalScrollbar);
    for (const ConsoleEntry& entry : entries) {
      if (static_cast<int>(entry.level) < settings_.console_min_level ||
          (!filter.empty() &&
           lowercase(entry.text).find(filter) == std::string::npos)) {
        continue;
      }
      const ImVec4 color = entry.level >= spdlog::level::err
                               ? ImVec4{1.0f, 0.35f, 0.3f, 1.0f}
                           : entry.level >= spdlog::level::warn
                               ? ImVec4{1.0f, 0.72f, 0.3f, 1.0f}
                               : ImGui::GetStyleColorVec4(ImGuiCol_Text);
      ImGui::PushStyleColor(ImGuiCol_Text, color);
      ImGui::TextUnformatted(entry.text.c_str());
      ImGui::PopStyleColor();
    }
    ImGui::EndChild();
  }

  void startSceneBake(BakeScope scope) {
    if (bake_future_.valid()) return;
    finishDocumentPropertyEditNow();
    if (dirty()) {
      saveScene(false);
      if (dirty()) {
        last_error_ = "Save the scene successfully before baking";
        return;
      }
    }
    if (settings_.selected_bake_id.empty() && !document_.bakes.empty()) {
      settings_.selected_bake_id = document_.bakes.front().id;
    }
    const auto selected = std::find_if(
        document_.bakes.begin(), document_.bakes.end(),
        [&](const scenes::SceneBakeDesc& bake) {
          return bake.id == settings_.selected_bake_id;
        });
    if (selected == document_.bakes.end()) {
      last_error_ = "Select a scene bake definition first";
      return;
    }
    if (selected->path.empty()) {
      last_error_ = "The selected bake has no output path";
      return;
    }
    scenes::SceneDocument snapshot = document_;
    snapshot.source_path = scene_path_;
    snapshot.reference_root = content_root_;
    const scenes::SceneBakeDesc bake = *selected;
    const bool bake_lighting = scope != BakeScope::Navigation;
    const bool bake_navigation = scope != BakeScope::Lighting;
    bake_shared_ = std::make_shared<EditorBakeSharedState>();
    bake_shared_->progress = scenes::SceneBakeProgress{
        .stage = scenes::SceneBakeStage::Preparing,
        .current = 0u,
        .total = 1u,
        .message = "Preparing scene bake",
    };
    const std::shared_ptr<EditorBakeSharedState> state = bake_shared_;
    bake_started_at_ = std::chrono::steady_clock::now();
    active_bake_scope_ = scope;
    bake_status_ = scope == BakeScope::Lighting
                       ? "Lighting bake running"
                       : (scope == BakeScope::Navigation
                              ? "Navigation bake running"
                              : "Scene bake running");
    bake_future_ = std::async(
        std::launch::async,
        [snapshot = std::move(snapshot), bake, state, bake_lighting,
         bake_navigation]() mutable {
          return scenes::bakeScene(
              snapshot,
              bake,
              scenes::SceneBakeExecutionOptions{
                  .bake_lighting = bake_lighting,
                  .bake_navigation = bake_navigation,
                  .is_cancelled = [state] {
                    return state->cancel_requested.load();
                  },
                  .on_progress = [state](
                                     const scenes::SceneBakeProgress& progress) {
                    const std::lock_guard lock(state->progress_mutex);
                    state->progress = progress;
                  },
              });
        });
  }

  void pollSceneBake() {
    if (!bake_future_.valid() ||
        bake_future_.wait_for(std::chrono::seconds(0)) !=
            std::future_status::ready) {
      return;
    }
    try {
      scenes::SceneBakeResult result = bake_future_.get();
      if (result.success) {
        std::string diagnostic;
        if (!writeJsonAtomic(result.output_path, result.metadata, diagnostic)) {
          bake_status_ = "Bake output failed";
          last_error_ = std::move(diagnostic);
        } else {
          bake_status_ = active_bake_scope_ == BakeScope::Lighting
                             ? "Lighting bake complete"
                             : (active_bake_scope_ == BakeScope::Navigation
                                    ? "Navigation bake complete"
                                    : "Scene bake complete");
          if (!result.lighting_warnings.empty()) {
            bake_status_ += " (" +
                            std::to_string(result.lighting_warnings.size()) +
                            " warning" +
                            (result.lighting_warnings.size() == 1u ? ")"
                                                                    : "s)");
            for (const std::string& warning : result.lighting_warnings) {
              spdlog::warn("Scene lighting bake: {}", warning);
            }
          }
          bake_fingerprint_ = result.scene_fingerprint;
          bake_stale_ = active_bake_scope_ != BakeScope::All;
          last_error_.clear();
          rebuildPreview();
        }
      } else if (result.cancelled) {
        bake_status_ = "Bake cancelled";
      } else {
        bake_status_ = "Bake failed";
        last_error_ = result.diagnostic.empty()
                          ? "Scene bake failed"
                          : std::move(result.diagnostic);
      }
    } catch (const std::exception& error) {
      bake_status_ = "Bake failed";
      last_error_ = std::string("Scene bake worker failed: ") + error.what();
    }
    bake_shared_.reset();
  }

  void drawBakeProgress() {
    if (!bake_future_.valid() || !bake_shared_) {
      if (!bake_status_.empty()) {
        ImGui::TextDisabled("%s%s", bake_status_.c_str(),
                            bake_stale_ ? " (stale)" : "");
      }
      return;
    }
    scenes::SceneBakeProgress progress{};
    {
      const std::lock_guard lock(bake_shared_->progress_mutex);
      progress = bake_shared_->progress;
    }
    const float fraction = progress.total == 0u
                               ? 0.0f
                               : std::clamp(
                                     static_cast<float>(progress.current) /
                                         static_cast<float>(progress.total),
                                     0.0f, 1.0f);
    ImGui::ProgressBar(fraction, {-80.0f, 0.0f}, progress.message.c_str());
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) {
      bake_shared_->cancel_requested.store(true);
    }
    const double seconds = std::chrono::duration<double>(
                               std::chrono::steady_clock::now() -
                               bake_started_at_)
                               .count();
    ImGui::TextDisabled("Elapsed %.1f seconds", seconds);
  }

  void drawLightingPanel() {
    size_t realtime_lights = 0u;
    size_t mixed_lights = 0u;
    size_t baked_lights = 0u;
    for (const scenes::SceneLight& light : document_.lights) {
      switch (light.component.bake_mode) {
        case components::LightComponent::BakeMode::Realtime:
          ++realtime_lights;
          break;
        case components::LightComponent::BakeMode::Mixed:
          ++mixed_lights;
          break;
        case components::LightComponent::BakeMode::Baked:
          ++baked_lights;
          break;
      }
    }
    ImGui::Text("Lights  Realtime %zu  |  Mixed %zu  |  Baked %zu",
                realtime_lights, mixed_lights, baked_lights);
    ImGui::TextDisabled(
        "Mixed and Baked lights contribute to explicit lightmap bakes.");
    if (document_.bakes.empty()) {
      ImGui::TextDisabled("This scene has no bake definition.");
      if (ImGui::Button("Create Bake")) {
        scenes::SceneDocument next = document_;
        scenes::SceneBakeDesc bake{};
        bake.id = makeStableId("bake");
        bake.path = std::filesystem::path("bakes") /
                    (bake.id + ".kbake.json");
        next.bakes.push_back(bake);
        if (commitDocumentCommand("Create Scene Bake", std::move(next))) {
          settings_.selected_bake_id = bake.id;
        }
      }
      return;
    }

    if (settings_.selected_bake_id.empty() ||
        std::none_of(document_.bakes.begin(), document_.bakes.end(),
                     [&](const scenes::SceneBakeDesc& bake) {
                       return bake.id == settings_.selected_bake_id;
                     })) {
      settings_.selected_bake_id = document_.bakes.front().id;
    }
    if (ImGui::BeginCombo("Bake", settings_.selected_bake_id.c_str())) {
      for (const scenes::SceneBakeDesc& bake : document_.bakes) {
        const bool selected = bake.id == settings_.selected_bake_id;
        if (ImGui::Selectable(bake.id.c_str(), selected)) {
          settings_.selected_bake_id = bake.id;
        }
        if (selected) ImGui::SetItemDefaultFocus();
      }
      ImGui::EndCombo();
    }
    auto selected = std::find_if(
        document_.bakes.begin(), document_.bakes.end(),
        [&](const scenes::SceneBakeDesc& bake) {
          return bake.id == settings_.selected_bake_id;
        });
    if (selected != document_.bakes.end()) {
      ImGui::TextWrapped("Output: %s", selected->path.generic_string().c_str());
      ImGui::Text("Static records: %zu", selected->static_component_ids.empty()
                                               ? document_.static_components.size()
                                               : selected->static_component_ids.size());

      ImGui::SeparatorText("Lightmap Settings");
      scenes::SceneDocument before = document_;
      bool changed = false;
      changed |= ImGui::Checkbox("Bake enabled", &selected->enabled);
      changed |= ImGui::Checkbox("Load bake at runtime",
                                 &selected->load_at_runtime);
      changed |= ImGui::Checkbox("Lighting enabled", &selected->lighting.enabled);
      changed |= ImGui::Checkbox("Generate missing UV1",
                                 &selected->lighting.generate_uv1);
      changed |= ImGui::Checkbox("Directional lightmaps",
                                 &selected->lighting.directional);
      changed |= ImGui::DragFloat("Texels per unit",
                                  &selected->lighting.texels_per_unit,
                                  0.25f, 0.25f, 256.0f, "%.2f");
      int atlas_size = static_cast<int>(selected->lighting.max_atlas_size);
      if (ImGui::DragInt("Maximum atlas size", &atlas_size, 16.0f, 64, 8192)) {
        selected->lighting.max_atlas_size =
            static_cast<uint32_t>(std::clamp(atlas_size, 64, 8192));
        changed = true;
      }
      int padding = static_cast<int>(selected->lighting.padding);
      if (ImGui::DragInt("Chart padding", &padding, 1.0f, 0, 64)) {
        selected->lighting.padding =
            static_cast<uint32_t>(std::clamp(padding, 0, 64));
        changed = true;
      }
      int dilation = static_cast<int>(selected->lighting.dilation);
      if (ImGui::DragInt("Dilation", &dilation, 1.0f, 0, 128)) {
        selected->lighting.dilation =
            static_cast<uint32_t>(std::clamp(dilation, 0, 128));
        changed = true;
      }
      int sky_samples = static_cast<int>(selected->lighting.sky_samples);
      if (ImGui::DragInt("Sky / AO samples", &sky_samples, 1.0f, 1, 4096)) {
        selected->lighting.sky_samples =
            static_cast<uint32_t>(std::clamp(sky_samples, 1, 4096));
        changed = true;
      }
      changed |= ImGui::DragFloat("AO maximum distance",
                                  &selected->lighting.ao_max_distance,
                                  0.1f, 0.0f, 1000.0f, "%.2f");
      ImGui::TextDisabled(
          "Portable CPU bake limits: 512px per target, 64 sky/AO samples.");
      if (changed) {
        beginDocumentPropertyEdit("Edit Lighting Bake Settings",
                                  std::move(before));
      }
    }
    const bool can_bake_lighting =
        selected != document_.bakes.end() && selected->enabled &&
        selected->lighting.enabled && !bake_future_.valid();
    const bool can_bake_all =
        selected != document_.bakes.end() && selected->enabled &&
        (selected->lighting.enabled || selected->navigation.enabled) &&
        !bake_future_.valid();
    ImGui::BeginDisabled(!can_bake_lighting);
    if (ImGui::Button("Bake Lighting")) startSceneBake(BakeScope::Lighting);
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!can_bake_all);
    if (ImGui::Button("Bake All")) startSceneBake(BakeScope::All);
    ImGui::EndDisabled();
    drawBakeProgress();
  }

  void drawNavigationPanel() {
    size_t owners = 0u;
    size_t surfaces = 0u;
    size_t links = 0u;
    size_t volumes = 0u;
    for (const scenes::SceneEntity& entity : document_.entities) {
      owners += entity.components.contains("NavMeshComponent") ? 1u : 0u;
      surfaces += entity.components.contains("NavMeshSurfaceComponent") ? 1u : 0u;
      links += entity.components.contains("NavOffMeshLinkComponent") ? 1u : 0u;
      volumes += entity.components.contains("NavConvexVolumeComponent") ? 1u : 0u;
    }
    ImGui::Text("Owners %zu  |  Surfaces %zu  |  Links %zu  |  Volumes %zu",
                owners, surfaces, links, volumes);
    ImGui::TextDisabled(
        "Static objects contribute when their Navigation flag is enabled.");
    if (owners == 0u) {
      ImGui::TextDisabled(
          "Add a NavMesh component to an entity to create a bake owner.");
    }
    bool navigation_enabled = false;
    bool bake_enabled = false;
    if (!document_.bakes.empty()) {
      auto selected = std::find_if(
          document_.bakes.begin(), document_.bakes.end(),
          [&](const scenes::SceneBakeDesc& bake) {
            return bake.id == settings_.selected_bake_id;
          });
      if (selected == document_.bakes.end()) {
        selected = document_.bakes.begin();
        settings_.selected_bake_id = selected->id;
      }
      bake_enabled = selected->enabled;
      navigation_enabled = selected->navigation.enabled;
      scenes::SceneDocument before = document_;
      if (ImGui::Checkbox("Navigation baking enabled",
                          &selected->navigation.enabled)) {
        navigation_enabled = selected->navigation.enabled;
        beginDocumentPropertyEdit("Edit Navigation Bake Settings",
                                  std::move(before));
      }
    }
    ImGui::BeginDisabled(owners == 0u || bake_future_.valid() ||
                         document_.bakes.empty() || !bake_enabled ||
                         !navigation_enabled);
    if (ImGui::Button("Bake Navigation")) {
      startSceneBake(BakeScope::Navigation);
    }
    ImGui::EndDisabled();
    if (document_.bakes.empty()) {
      ImGui::SameLine();
      ImGui::TextDisabled("Create a bake definition in the Lighting tab first");
    }
    drawBakeProgress();
  }

  void drawHierarchy() {
    if (ImGui::Button("+ Group")) addGroup();
    ImGui::SameLine();
    if (ImGui::Button("+ Light")) addLight();
    ImGui::SameLine();
    if (ImGui::Button("+ Terrain")) open_create_terrain_ = true;
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::InputTextWithHint("##hierarchy_filter", "Search hierarchy",
                                 hierarchy_filter_,
                                 sizeof(hierarchy_filter_))) {
      settings_.hierarchy_filter = hierarchy_filter_;
    }
    std::vector<std::string> foliage_entity_ids;
    foliage_entity_ids.reserve(foliage_layers_.size());
    for (const FoliageLayerState& layer : foliage_layers_) {
      foliage_entity_ids.push_back(layer.entity_id);
    }
    const HierarchyBuildResult hierarchy = projectFoliageUnderTerrain(
        buildHierarchy(document_), terrain_entity_id_, foliage_entity_ids);
    const auto lowercase = [](std::string value) {
      std::transform(value.begin(), value.end(), value.begin(),
                     [](unsigned char character) {
                       return static_cast<char>(std::tolower(character));
                     });
      return value;
    };
    const std::string filter = lowercase(hierarchy_filter_);
    const auto node_matches = [&](const auto& self,
                                  const HierarchyNode& node) -> bool {
      if (filter.empty()) return true;
      std::string searchable = node.item.id;
      if (node.item.kind == SelectionKind::Entity) {
        const auto entity = std::find_if(
            document_.entities.begin(), document_.entities.end(),
            [&](const scenes::SceneEntity& value) {
              return value.id == node.item.id;
            });
        if (entity != document_.entities.end()) searchable += " " + entity->name;
      } else {
        const auto prefab = std::find_if(
            document_.prefab_instances.begin(),
            document_.prefab_instances.end(),
            [&](const scenes::ScenePrefabInstance& value) {
              return value.id == node.item.id;
            });
        if (prefab != document_.prefab_instances.end()) {
          searchable += " " + prefab->prefab_path.generic_string();
        }
      }
      if (lowercase(std::move(searchable)).find(filter) != std::string::npos) {
        return true;
      }
      return std::any_of(node.children.begin(), node.children.end(),
                         [&](const HierarchyNode& child) {
                           return self(self, child);
                         });
    };
    const auto draw_node = [&](const auto& self,
                               const HierarchyNode& node,
                               bool top_level) -> void {
      if (!node_matches(node_matches, node)) return;
      std::string label;
      if (node.item.kind == SelectionKind::Entity) {
        const auto entity = std::find_if(
            document_.entities.begin(), document_.entities.end(),
            [&](const scenes::SceneEntity& value) {
              return value.id == node.item.id;
            });
        label = entity == document_.entities.end() || entity->name.empty()
                    ? node.item.id
                    : entity->name;
        if (node.item.id == terrain_entity_id_) label += " [Terrain]";
        if (findFoliageLayer(node.item.id) != nullptr) label += " [Foliage]";
        const bool component_static =
            entity != document_.entities.end() &&
            entity->components.contains("StaticComponent");
        const bool record_static = std::any_of(
            document_.static_components.begin(),
            document_.static_components.end(),
            [&](const scenes::SceneStaticComponent& value) {
              return value.entity_id == node.item.id;
            });
        if (component_static || record_static) label += " [Static]";
      } else {
        const auto prefab = std::find_if(
            document_.prefab_instances.begin(), document_.prefab_instances.end(),
            [&](const scenes::ScenePrefabInstance& value) {
              return value.id == node.item.id;
            });
        if (prefab != document_.prefab_instances.end()) {
          label = prefab->prefab_path.parent_path().filename().string();
        }
        if (label.empty()) label = node.item.id;
        label += " [Prefab]";
        if (prefab != document_.prefab_instances.end() &&
            prefab->static_component.has_value()) {
          label += prefab->static_component->enabled ? " [Static]"
                                                     : " [Static Off]";
        }
      }
      ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow |
                                 ImGuiTreeNodeFlags_OpenOnDoubleClick |
                                 ImGuiTreeNodeFlags_SpanAvailWidth;
      if (top_level) flags |= ImGuiTreeNodeFlags_DefaultOpen;
      if (node.children.empty()) {
        flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
      }
      const bool active_virtual_foliage =
          node.item.kind == SelectionKind::Entity &&
          node.item.id == settings_.active_foliage_layer_id &&
          selection_.kind == SelectionKind::Entity &&
          selection_.id == terrain_entity_id_;
      if ((selection_.kind == node.item.kind &&
           selection_.id == node.item.id) || active_virtual_foliage) {
        flags |= ImGuiTreeNodeFlags_Selected;
      }
      ImGui::PushID(node.item.id.c_str());
      const bool open = ImGui::TreeNodeEx("##hierarchy_item", flags, "%s", label.c_str());
      if (ImGui::IsItemClicked()) {
        changeTool(ToolMode::Select);
        selected_asset_path_.clear();
        selected_asset_key_.clear();
        if (!terrain_entity_id_.empty() &&
            node.item.kind == SelectionKind::Entity &&
            findFoliageLayer(node.item.id) != nullptr) {
          focusFoliageLayer(node.item.id, true);
        } else {
          selection_ = node.item;
        }
        gizmo_geometry_ = {};
      }
      if (open && !node.children.empty()) {
        for (const HierarchyNode& child : node.children) {
          self(self, child, false);
        }
        ImGui::TreePop();
      }
      ImGui::PopID();
    };
    for (const HierarchyNode& root : hierarchy.roots) {
      draw_node(draw_node, root, true);
    }
    for (const std::string& diagnostic : hierarchy.diagnostics) {
      ImGui::TextColored({1.0f, 0.35f, 0.3f, 1.0f}, "%s",
                         diagnostic.c_str());
    }
  }

  void drawAssetCatalog() {
    if (catalog_future_.valid()) ImGui::TextDisabled("Indexing assets...");
    for (const std::string& diagnostic : catalog_diagnostics_) {
      ImGui::TextColored({1.0f, 0.55f, 0.25f, 1.0f}, "%s", diagnostic.c_str());
    }
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::InputTextWithHint("##asset_filter", "Search prefabs and assets",
                                 asset_filter_, sizeof(asset_filter_))) {
      settings_.asset_filter = asset_filter_;
    }
    ImGui::SetNextItemWidth(180.0f);
    ImGui::Combo("Type", &settings_.asset_type_filter,
                 "All\0Prefabs\0Meshes\0Materials\0Environments\0Textures\0Other\0");
    const auto lowercase = [](std::string value) {
      std::transform(value.begin(), value.end(), value.begin(),
                     [](unsigned char c) {
                       return static_cast<char>(std::tolower(c));
                     });
      return value;
    };
    const std::string filter = lowercase(asset_filter_);
    const int card_columns = std::max(
        1, static_cast<int>(ImGui::GetContentRegionAvail().x / 220.0f));
    if (!ImGui::BeginTable("##asset_cards", card_columns,
                           ImGuiTableFlags_SizingStretchSame)) {
      return;
    }
    for (const AssetEntry& entry : catalog_.entries()) {
      if (entry.kind == AssetKind::Package) continue;
      const bool type_matches = [&] {
        switch (settings_.asset_type_filter) {
          case 0: return true;
          case 1: return entry.kind == AssetKind::Prefab;
          case 2: return entry.kind == AssetKind::Mesh;
          case 3: return entry.kind == AssetKind::Material;
          case 4: return entry.kind == AssetKind::Environment;
          case 5: return entry.kind == AssetKind::Texture;
          case 6: return entry.kind == AssetKind::Other;
          default: return true;
        }
      }();
      if (!type_matches) continue;
      const std::string haystack =
          lowercase(entry.name + " " + entry.key + " " + entry.type);
      if (!filter.empty() && haystack.find(filter) == std::string::npos) continue;
      ImGui::TableNextColumn();
      ImGui::PushID((entry.path.generic_string() + entry.key).c_str());
      if (!entry.valid) ImGui::PushStyleColor(ImGuiCol_Text, {1.0f, 0.35f, 0.3f, 1.0f});
      const char* kind = entry.kind == AssetKind::Prefab ? "PFB" :
                         entry.kind == AssetKind::Mesh ? "MSH" :
                         entry.kind == AssetKind::Material ? "MAT" :
                         entry.kind == AssetKind::Environment ? "ENV" :
                         entry.kind == AssetKind::Texture ? "TEX" : "AST";
      std::string label = "[::] [" + std::string(kind) + "] " + entry.name +
                          "\n" + (entry.key.empty()
                                        ? entry.path.generic_string()
                                        : entry.key);
      if (!entry.valid && !entry.diagnostic.empty()) {
        label += "\n! " + entry.diagnostic;
      }
      const bool selected_asset =
          !selected_asset_path_.empty() &&
          selected_asset_path_.lexically_normal() == entry.path.lexically_normal();
      const bool activated = ImGui::Selectable(label.c_str(), selected_asset,
                                               ImGuiSelectableFlags_AllowDoubleClick,
                                               {0.0f, entry.valid ? 50.0f : 68.0f});
      if (!entry.valid) ImGui::PopStyleColor();
      if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::TextUnformatted(entry.key.empty() ? entry.path.string().c_str() : entry.key.c_str());
        if (!entry.diagnostic.empty()) ImGui::TextWrapped("%s", entry.diagnostic.c_str());
        ImGui::EndTooltip();
      }
      if (activated) {
        if (prefab_asset_draft_.has_value() &&
            prefab_asset_draft_->dirty() &&
            prefab_asset_draft_->sourcePath().lexically_normal() !=
                entry.path.lexically_normal()) {
          last_error_ =
              "Save or Revert the open prefab draft before selecting another asset";
          ImGui::PopID();
          continue;
        }
        selected_asset_path_ = entry.path;
        selected_asset_key_ = entry.key;
        selection_.clear();
      }
      if (activated && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && entry.valid) {
        if (entry.kind == AssetKind::Prefab) {
          beginPrefabPlacement(entry.path);
        } else if (entry.kind == AssetKind::Mesh) {
          pending_foliage_mesh_ = entry.key;
          pending_foliage_prefab_.clear();
          pending_foliage_package_ = entry.package_path;
          open_create_foliage_ = true;
        } else if (entry.kind == AssetKind::Material) {
          if (selectedFoliageLayer() != nullptr) {
            assignFoliageMaterial(entry);
          } else if (terrain_canvas_) {
            assignTerrainMaterial(entry);
          }
        } else if (entry.kind == AssetKind::Environment) {
          setEnvironmentAsset(entry);
        }
      }
      if (entry.kind == AssetKind::Prefab && ImGui::BeginDragDropSource()) {
        const std::string path = entry.path.generic_string();
        ImGui::SetDragDropPayload("KARMA_PREFAB", path.c_str(), path.size() + 1u);
        ImGui::Text("Place %s", entry.name.c_str());
        ImGui::EndDragDropSource();
      }
      if (entry.kind == AssetKind::Mesh && entry.valid &&
          ImGui::BeginDragDropSource()) {
        ImGui::SetDragDropPayload("KARMA_MESH_ASSET",
                                  entry.key.c_str(),
                                  entry.key.size() + 1u);
        ImGui::Text("Assign %s", entry.name.c_str());
        ImGui::EndDragDropSource();
      }
      if (entry.kind == AssetKind::Material && entry.valid &&
          ImGui::BeginDragDropSource()) {
        ImGui::SetDragDropPayload("KARMA_TERRAIN_MATERIAL",
                                  entry.key.c_str(),
                                  entry.key.size() + 1u);
        ImGui::Text("Assign %s", entry.name.c_str());
        ImGui::EndDragDropSource();
      }
      if (entry.kind == AssetKind::Texture && entry.valid &&
          ImGui::BeginDragDropSource()) {
        ImGui::SetDragDropPayload("KARMA_TEXTURE_ASSET",
                                  entry.key.c_str(),
                                  entry.key.size() + 1u);
        ImGui::Text("Assign %s", entry.name.c_str());
        ImGui::EndDragDropSource();
      }
      ImGui::PopID();
    }
    ImGui::EndTable();
  }

  static std::string lowercaseUi(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char character) {
                     return static_cast<char>(std::tolower(character));
                   });
    return value;
  }

  bool inspectorFilterMatches(std::string_view display,
                              std::string_view type = {}) const {
    if (component_filter_[0] == '\0') return true;
    return lowercaseUi(std::string(display) + " " + std::string(type))
               .find(lowercaseUi(component_filter_)) != std::string::npos;
  }

  template <typename DrawBody>
  void drawComponentCard(std::string_view key,
                         std::string_view display_name,
                         bool default_open,
                         DrawBody&& draw_body) {
    const std::string stable_key(key);
    ImGui::PushID(stable_key.c_str());
    const auto stored = settings_.component_foldouts.find(stable_key);
    const bool requested_open = stored == settings_.component_foldouts.end()
                                    ? default_open
                                    : stored->second;
    ImGui::SetNextItemOpen(requested_open, ImGuiCond_Always);
    if (editor_semibold_font_ != nullptr) ImGui::PushFont(editor_semibold_font_);
    const std::string header = std::string(display_name) + "##component_header";
    const bool open = ImGui::CollapsingHeader(
        header.c_str(), ImGuiTreeNodeFlags_SpanAvailWidth |
                            ImGuiTreeNodeFlags_FramePadding);
    if (editor_semibold_font_ != nullptr) ImGui::PopFont();
    settings_.component_foldouts[stable_key] = open;
    if (open) {
      ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{9.0f, 8.0f});
      ImGui::BeginChild("##component_body", {0.0f, 0.0f},
                        ImGuiChildFlags_Borders |
                            ImGuiChildFlags_AutoResizeY,
                        ImGuiWindowFlags_NoScrollbar |
                            ImGuiWindowFlags_NoScrollWithMouse);
      ImGui::PopStyleVar();
      draw_body();
      ImGui::EndChild();
    }
    ImGui::PopID();
    ImGui::Dummy({0.0f, 2.0f});
  }

  std::string inspectorContextKey() const {
    if (selection_.valid()) {
      return (selection_.kind == SelectionKind::Prefab ? "prefab:" :
                                                        "entity:") +
             selection_.id;
    }
    if (!selected_asset_key_.empty()) return "asset:" + selected_asset_key_;
    if (!selected_asset_path_.empty()) {
      return "asset:" + selected_asset_path_.generic_string();
    }
    return "scene";
  }

  void drawInspector() {
    const float x = workspace_layout_.hierarchy_width +
                    workspace_layout_.splitter_size +
                    workspace_layout_.center_width +
                    workspace_layout_.splitter_size;
    ImGui::SetNextWindowPos({x, workspace_top_}, ImGuiCond_Always);
    ImGui::SetNextWindowSize(
        {std::max(workspace_layout_.inspector_width, 1.0f),
         std::max(workspace_height_, 1.0f)},
        ImGuiCond_Always);
    ImGui::Begin("Inspector", nullptr,
                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                     ImGuiWindowFlags_NoSavedSettings);
    const std::string context_key = inspectorContextKey();
    if (context_key != inspector_context_key_ || inspector_scroll_to_top_) {
      inspector_context_key_ = context_key;
      inspector_scroll_to_top_ = false;
      ImGui::SetScrollY(0.0f);
    }
    if (!selection_.valid() && !selected_asset_path_.empty()) {
      drawAssetInspector();
    } else if (!selection_.valid()) {
      drawSceneInspector();
    } else {
      drawSelectionInspector();
    }
    finishDocumentPropertyEdit();
    ImGui::End();
  }

  void drawAssetInspector() {
    const AssetEntry* selected = nullptr;
    for (const AssetEntry& entry : catalog_.entries()) {
      if ((!selected_asset_key_.empty() && entry.key == selected_asset_key_) ||
          entry.path.lexically_normal() == selected_asset_path_.lexically_normal()) {
        selected = &entry;
        break;
      }
    }
    if (selected == nullptr) {
      ImGui::TextDisabled("The selected asset is no longer in the catalog.");
      return;
    }
    ImGui::Text("%s", selected->name.c_str());
    ImGui::TextDisabled("%s", selected->type.c_str());
    ImGui::Separator();
    ImGui::TextWrapped("Key: %s",
                       selected->key.empty() ? "<none>" : selected->key.c_str());
    ImGui::TextWrapped("Path: %s", selected->path.generic_string().c_str());
    if (!selected->package_path.empty()) {
      ImGui::TextWrapped("Package: %s",
                         selected->package_path.generic_string().c_str());
    }
    if (!selected->diagnostic.empty()) {
      ImGui::TextColored(selected->valid
                             ? ImVec4{1.0f, 0.72f, 0.3f, 1.0f}
                             : ImVec4{1.0f, 0.35f, 0.3f, 1.0f},
                         "%s", selected->diagnostic.c_str());
    }
    if (selected->kind == AssetKind::Material) {
      drawMaterialAssetInspector(*selected);
    } else if (selected->kind == AssetKind::Prefab) {
      drawPrefabAssetInspector(*selected);
    }
  }

  bool ensurePrefabAssetDraft(const AssetEntry& entry) {
    if (prefab_asset_draft_.has_value() &&
        prefab_asset_draft_->sourcePath().lexically_normal() ==
            entry.path.lexically_normal()) {
      return true;
    }
    if (prefab_asset_draft_.has_value() && prefab_asset_draft_->dirty()) {
      prefab_asset_draft_error_ =
          "Save or Revert the current prefab draft before opening another";
      return false;
    }
    prefab_asset_draft_.reset();
    prefab_asset_draft_error_.clear();
    const LegacyRenderMigrationReport migration =
        migratePrefabSourceClosure({entry.path}, content_root_);
    if (!migration.success()) {
      prefab_asset_draft_error_ = migration.diagnostics.empty()
                                      ? "Prefab migration failed"
                                      : joinDiagnostics(migration.diagnostics);
      return false;
    }
    if (migration.changed) {
      prefab_asset_draft_status_ =
          "Automatically migrated prefab source; backup preserved";
      scanCatalog(true);
    }
    std::optional<PrefabAssetDraft> opened =
        openPrefabAssetDraft(entry.path, &prefab_asset_draft_error_);
    if (!opened.has_value()) return false;
    prefab_asset_draft_ = std::move(*opened);
    prefab_draft_node_index_ = std::min(
        prefab_asset_draft_->document().root,
        prefab_asset_draft_->document().nodes.empty()
            ? 0u
            : prefab_asset_draft_->document().nodes.size() - 1u);
    return true;
  }

  bool prefabDraftAcceptsAsset(const AssetEntry& asset) {
    if (!prefab_asset_draft_.has_value() || asset.package_path.empty()) {
      prefab_asset_draft_error_ =
          "Prefab mesh/material references must come from its local asset package";
      return false;
    }
    std::error_code error;
    const std::filesystem::path prefab_directory =
        std::filesystem::weakly_canonical(
            prefab_asset_draft_->sourcePath().parent_path(), error);
    error.clear();
    const std::filesystem::path package_directory =
        std::filesystem::weakly_canonical(asset.package_path.parent_path(),
                                          error);
    if (error || prefab_directory != package_directory) {
      prefab_asset_draft_error_ =
          "The focused prefab editor cannot attach assets from another package";
      return false;
    }
    return true;
  }

  bool commitPrefabDraftComponent(std::string_view type_name,
                                  const nlohmann::json& payload,
                                  std::string label,
                                  bool coalesce = false) {
    if (!prefab_asset_draft_.has_value()) return false;
    std::string error;
    if (!prefab_asset_draft_->setNodeComponent(prefab_draft_node_index_,
                                                type_name,
                                                payload,
                                                component_editors_,
                                                std::move(label),
                                                &error,
                                                coalesce)) {
      prefab_asset_draft_error_ = error.empty()
                                      ? "Prefab component edit was rejected"
                                      : std::move(error);
      return false;
    }
    prefab_asset_draft_error_.clear();
    return true;
  }

  bool drawPrefabMeshDraftEditor(nlohmann::json& payload) {
    bool changed = false;
    const std::string mesh_key =
        payload.value("mesh_asset_key", std::string{});
    ImGui::Selectable(("Mesh: " +
                       (mesh_key.empty() ? std::string("<drop mesh>")
                                         : mesh_key))
                          .c_str(),
                      false);
    if (ImGui::BeginDragDropTarget()) {
      if (const ImGuiPayload* drop =
              ImGui::AcceptDragDropPayload("KARMA_MESH_ASSET")) {
        const AssetEntry* asset = catalog_.findByKey(
            static_cast<const char*>(drop->Data));
        if (asset != nullptr && asset->valid &&
            asset->kind == AssetKind::Mesh &&
            prefabDraftAcceptsAsset(*asset)) {
          payload["mesh_asset_key"] = asset->key;
          commitPrefabDraftComponent(
              "MeshComponent", payload, "Assign Prefab Mesh");
          ImGui::EndDragDropTarget();
          return false;
        }
      }
      ImGui::EndDragDropTarget();
    }
    changed |= drawJsonBool(payload, "visible", "Visible", true);
    changed |= drawJsonBool(payload, "shadow_visible", "Cast shadows", true);
    auto& materials = payload["materials"];
    if (!materials.is_array()) materials = nlohmann::json::array();
    for (uint32_t slot = 0u; slot < 4u; ++slot) {
      auto assignment = std::find_if(
          materials.begin(), materials.end(),
          [&](const nlohmann::json& candidate) {
            return candidate.is_object() &&
                   candidate.value("slot", UINT32_MAX) == slot;
          });
      const std::string key = assignment == materials.end()
                                  ? "<drop material>"
                                  : assignment->value(
                                        "material_key", std::string{});
      ImGui::PushID(static_cast<int>(slot));
      ImGui::Selectable(
          ("Material " + std::to_string(slot) + ": " + key).c_str(), false);
      if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* drop = ImGui::AcceptDragDropPayload(
                "KARMA_TERRAIN_MATERIAL")) {
          const AssetEntry* asset = catalog_.findByKey(
              static_cast<const char*>(drop->Data));
          if (asset != nullptr && asset->valid &&
              asset->kind == AssetKind::Material &&
              prefabDraftAcceptsAsset(*asset)) {
            if (assignment == materials.end()) {
              materials.push_back(
                  {{"slot", slot}, {"material_key", asset->key}});
            } else {
              (*assignment)["material_key"] = asset->key;
            }
            commitPrefabDraftComponent(
                "MeshComponent", payload, "Assign Prefab Material");
            ImGui::EndDragDropTarget();
            ImGui::PopID();
            return false;
          }
        }
        ImGui::EndDragDropTarget();
      }
      ImGui::PopID();
    }
    return changed;
  }

  void drawPrefabAssetInspector(const AssetEntry& entry) {
    ImGui::SeparatorText("Focused Prefab Source Editor");
    ImGui::TextDisabled(
        "Mesh and LOD components only. Node structure and other components stay linked.");
    if (ImGui::Button("Check / Migrate Legacy Rendering")) {
      const LegacyRenderMigrationReport migrated =
          migratePrefabSourceClosure({entry.path}, content_root_);
      if (!migrated.success()) {
        prefab_asset_draft_status_.clear();
        prefab_asset_draft_error_ = migrated.diagnostics.empty()
                                        ? "Prefab migration failed"
                                        : migrated.diagnostics.front();
      } else if (migrated.changed) {
        prefab_asset_draft_.reset();
        prefab_asset_draft_status_ =
            "Migrated " + std::to_string(migrated.migrated_owners) +
            " prefab node(s)";
        prefab_asset_draft_error_.clear();
        scanCatalog(true);
      } else {
        prefab_asset_draft_status_ = "No legacy render fields were found";
        prefab_asset_draft_error_.clear();
      }
    }
    if (!prefab_asset_draft_status_.empty()) {
      ImGui::TextColored({0.35f, 0.85f, 0.55f, 1.0f}, "%s",
                         prefab_asset_draft_status_.c_str());
    }
    if (!ensurePrefabAssetDraft(entry)) {
      if (!prefab_asset_draft_error_.empty()) {
        ImGui::TextColored({1.0f, 0.35f, 0.25f, 1.0f}, "%s",
                           prefab_asset_draft_error_.c_str());
      }
      return;
    }
    PrefabAssetDraft& draft = *prefab_asset_draft_;
    if (!ImGui::IsAnyItemActive()) draft.finishCoalescedEdit();
    const bool conflict = draft.sourceChangedExternally();
    if (conflict) {
      ImGui::TextColored(
          {1.0f, 0.35f, 0.25f, 1.0f},
          "Source changed externally. Save is blocked until Revert.");
    }

    ImGui::BeginDisabled(!draft.canUndo());
    if (ImGui::Button("Undo")) draft.undo();
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!draft.canRedo());
    if (ImGui::Button("Redo")) draft.redo();
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!draft.dirty() || conflict);
    if (ImGui::Button("Save")) {
      std::string error;
      if (draft.save(component_editors_, &error)) {
        prefab_asset_draft_status_ = "Prefab source saved atomically";
        prefab_asset_draft_error_.clear();
        scanCatalog(true);
      } else {
        prefab_asset_draft_error_ = std::move(error);
      }
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!draft.dirty() && !conflict);
    if (ImGui::Button("Revert") ||
        (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
         ImGui::IsKeyPressed(ImGuiKey_Escape))) {
      std::string error;
      if (!draft.revert(&error)) {
        prefab_asset_draft_error_ = std::move(error);
      } else {
        prefab_asset_draft_error_.clear();
        prefab_asset_draft_status_ = "Prefab draft reverted";
        prefab_draft_node_index_ = std::min(
            draft.document().root,
            draft.document().nodes.empty()
                ? 0u
                : draft.document().nodes.size() - 1u);
      }
    }
    ImGui::EndDisabled();
    if (!prefab_asset_draft_error_.empty()) {
      ImGui::TextColored({1.0f, 0.35f, 0.25f, 1.0f}, "%s",
                         prefab_asset_draft_error_.c_str());
    }
    if (draft.document().nodes.empty()) {
      ImGui::TextDisabled("Prefab contains no nodes.");
      return;
    }

    prefab_draft_node_index_ =
        std::min(prefab_draft_node_index_, draft.document().nodes.size() - 1u);
    const auto& selected_node =
        draft.document().nodes[prefab_draft_node_index_];
    const std::string node_preview = selected_node.name.empty()
                                         ? "Node " +
                                               std::to_string(selected_node.id)
                                         : selected_node.name;
    if (ImGui::BeginCombo("Node", node_preview.c_str())) {
      for (size_t index = 0u; index < draft.document().nodes.size(); ++index) {
        const prefabs::PrefabNode& node = draft.document().nodes[index];
        const std::string label =
            (node.name.empty() ? "Node " + std::to_string(node.id)
                               : node.name) +
            (index == draft.document().root ? " [Root]" : "") + "##" +
            std::to_string(node.id);
        const bool selected = index == prefab_draft_node_index_;
        if (ImGui::Selectable(label.c_str(), selected)) {
          draft.finishCoalescedEdit();
          prefab_draft_node_index_ = index;
        }
        if (selected) ImGui::SetItemDefaultFocus();
      }
      ImGui::EndCombo();
    }

    if (!draft.document()
             .nodes[prefab_draft_node_index_]
             .components.is_object()) {
      ImGui::TextDisabled("Node components are unavailable.");
      return;
    }
    if (draft.document()
            .nodes[prefab_draft_node_index_]
            .components.contains("MeshComponent")) {
      drawComponentCard("PrefabDraftMesh", "Mesh Renderer", true, [&] {
        nlohmann::json payload =
            draft.document()
                .nodes[prefab_draft_node_index_]
                .components["MeshComponent"];
        if (drawPrefabMeshDraftEditor(payload)) {
          commitPrefabDraftComponent("MeshComponent",
                                     payload,
                                     "Edit Prefab Mesh",
                                     ImGui::IsAnyItemActive());
        }
        if (ImGui::Button("Remove Mesh Component")) {
          std::string error;
          if (!draft.removeNodeComponent(prefab_draft_node_index_,
                                         "MeshComponent",
                                         component_editors_,
                                         "Remove Prefab Mesh",
                                         &error)) {
            prefab_asset_draft_error_ = std::move(error);
          }
        }
      });
    } else if (ImGui::Button("Add Mesh Component")) {
      const auto* descriptor = component_editors_.find("MeshComponent");
      if (descriptor != nullptr) {
        commitPrefabDraftComponent("MeshComponent",
                                   descriptor->default_payload(),
                                   "Add Prefab Mesh");
      }
      return;
    }

    if (draft.document()
            .nodes[prefab_draft_node_index_]
            .components.contains("LODComponent")) {
      drawComponentCard("PrefabDraftLOD", "Level of Detail", true, [&] {
        const auto& current_components =
            draft.document().nodes[prefab_draft_node_index_].components;
        nlohmann::json payload = current_components["LODComponent"];
        const nlohmann::json mesh =
            current_components.value("MeshComponent", nlohmann::json::object());
        bool committed = false;
        const bool changed = drawLodLevelsEditor(
            payload,
            mesh.value("mesh_asset_key", std::string{}),
            mesh.value("materials", nlohmann::json::array()),
            [&](const nlohmann::json& edited,
                const AssetEntry& asset,
                std::string label) {
              if (!prefabDraftAcceptsAsset(asset)) return false;
              return commitPrefabDraftComponent(
                  "LODComponent", edited, "Prefab " + label);
            },
            committed);
        if (!committed && changed) {
          commitPrefabDraftComponent("LODComponent",
                                     payload,
                                     "Edit Prefab LOD",
                                     ImGui::IsAnyItemActive());
        }
        if (ImGui::Button("Remove LOD Component")) {
          std::string error;
          if (!draft.removeNodeComponent(prefab_draft_node_index_,
                                         "LODComponent",
                                         component_editors_,
                                         "Remove Prefab LOD",
                                         &error)) {
            prefab_asset_draft_error_ = std::move(error);
          }
        }
      });
    } else {
      const auto& current_components =
          draft.document().nodes[prefab_draft_node_index_].components;
      const bool compatible =
          current_components.contains("MeshComponent") ||
          current_components.contains("InstancedMeshComponent") ||
          current_components.contains("FoliageComponent");
      ImGui::BeginDisabled(!compatible);
      if (ImGui::Button("Add LOD Component")) {
        commitPrefabDraftComponent("LODComponent",
                                   defaultLodComponentPayload(),
                                   "Add Prefab LOD");
        ImGui::EndDisabled();
        return;
      }
      ImGui::EndDisabled();
    }

    ImGui::SeparatorText("Other Components (read-only)");
    const auto& current_components =
        draft.document().nodes[prefab_draft_node_index_].components;
    for (auto component = current_components.begin();
         component != current_components.end(); ++component) {
      if (component.key() == "MeshComponent" ||
          component.key() == "LODComponent") {
        continue;
      }
      ImGui::BulletText("%s", component.key().c_str());
    }
  }

  void restoreMaterialPreview() {
    if (!material_original_.has_value() || material_draft_key_.empty() ||
        preview_.asset_registry == nullptr) {
      return;
    }
    preview_.asset_registry->registerMaterialAsset(material_draft_key_,
                                                   *material_original_);
  }

  void resetMaterialDraft(bool restore_preview) {
    if (restore_preview && material_draft_dirty_) restoreMaterialPreview();
    material_draft_.reset();
    material_original_.reset();
    material_draft_path_.clear();
    material_draft_key_.clear();
    material_draft_dirty_ = false;
    material_draft_error_.clear();
  }

  bool ensureMaterialDraft(const AssetEntry& entry) {
    if (material_draft_.has_value() &&
        material_draft_path_.lexically_normal() ==
            entry.path.lexically_normal() &&
        material_draft_key_ == entry.key) {
      return true;
    }
    resetMaterialDraft(true);
    std::string diagnostic;
    auto loaded = assets::loadMaterialAssetDesc(entry.path, &diagnostic);
    if (!loaded.has_value()) {
      material_draft_path_ = entry.path;
      material_draft_key_ = entry.key;
      material_draft_error_ = diagnostic.empty()
                                  ? "Material variants use validated JSON editing"
                                  : std::move(diagnostic);
      return false;
    }
    loaded->material_key = entry.key;
    loaded->material_asset_path = entry.path;
    material_original_ = *loaded;
    material_draft_ = std::move(*loaded);
    material_draft_path_ = entry.path;
    material_draft_key_ = entry.key;
    return true;
  }

  void previewMaterialDraft() {
    if (!material_draft_.has_value() || material_draft_key_.empty() ||
        preview_.asset_registry == nullptr) {
      return;
    }
    material_draft_->material_key = material_draft_key_;
    preview_.asset_registry->registerMaterialAsset(material_draft_key_,
                                                   *material_draft_);
  }

  bool drawMaterialTextureSlot(rendering::MaterialAssetDesc& material,
                               const char* alias,
                               const char* label) {
    const auto current = material.textures.find(alias);
    const bool had_value = current != material.textures.end();
    const std::string value = current == material.textures.end()
                                  ? std::string("<none>")
                                  : current->second;
    ImGui::PushID(alias);
    ImGui::TextUnformatted(label);
    ImGui::SameLine(125.0f);
    ImGui::Button(value.c_str(), {-28.0f, 0.0f});
    bool changed = false;
    if (ImGui::BeginDragDropTarget()) {
      if (const ImGuiPayload* payload =
              ImGui::AcceptDragDropPayload("KARMA_TEXTURE_ASSET")) {
        const char* key = static_cast<const char*>(payload->Data);
        if (key != nullptr && key[0] != '\0') {
          material.textures[alias] = key;
          changed = true;
        }
      }
      ImGui::EndDragDropTarget();
    }
    if (had_value) {
      ImGui::SameLine();
      if (ImGui::SmallButton("X")) {
        material.textures.erase(alias);
        changed = true;
      }
    }
    ImGui::PopID();
    return changed;
  }

  void drawMaterialAssetInspector(const AssetEntry& entry) {
    ImGui::SeparatorText("PBR Material");
    if (!ensureMaterialDraft(entry)) {
      ImGui::TextColored({1.0f, 0.45f, 0.3f, 1.0f}, "%s",
                         material_draft_error_.c_str());
      return;
    }
    rendering::MaterialDesc& surface = material_draft_->surface;
    bool changed = false;
    changed |= ImGui::ColorEdit4("Base color", &surface.base_color.r);
    changed |= ImGui::ColorEdit4("Emissive", &surface.emissive_color.r);
    changed |= ImGui::DragFloat("Emissive strength", &surface.emissive_strength,
                                0.02f, 0.0f, 1000.0f);
    changed |= ImGui::SliderFloat("Metallic", &surface.metallic, 0.0f, 1.0f);
    changed |= ImGui::SliderFloat("Roughness", &surface.roughness, 0.0f, 1.0f);
    changed |= ImGui::SliderFloat("Specular", &surface.specular_factor,
                                  0.0f, 1.0f);
    changed |= ImGui::ColorEdit4("Specular color", &surface.specular_color.r);
    changed |= ImGui::DragFloat("Normal scale", &surface.normal_scale,
                                0.02f, 0.0f, 8.0f);
    int normal_convention =
        surface.normal_map_convention ==
                rendering::MaterialDesc::NormalMapConvention::DirectX
            ? 1
            : 0;
    if (ImGui::Combo("Normal convention", &normal_convention,
                     "OpenGL (+Y)\0DirectX (-Y)\0")) {
      surface.normal_map_convention =
          normal_convention == 1
              ? rendering::MaterialDesc::NormalMapConvention::DirectX
              : rendering::MaterialDesc::NormalMapConvention::OpenGL;
      changed = true;
    }
    changed |= ImGui::SliderFloat("Occlusion", &surface.occlusion_strength,
                                  0.0f, 1.0f);

    if (ImGui::CollapsingHeader("Extensions")) {
      changed |= ImGui::SliderFloat("Clearcoat", &surface.clearcoat, 0.0f, 1.0f);
      changed |= ImGui::SliderFloat("Clearcoat roughness",
                                    &surface.clearcoat_roughness, 0.0f, 1.0f);
      changed |= ImGui::ColorEdit4("Sheen color", &surface.sheen_color.r);
      changed |= ImGui::SliderFloat("Sheen roughness", &surface.sheen_roughness,
                                    0.0f, 1.0f);
      changed |= ImGui::SliderFloat("Anisotropy", &surface.anisotropy,
                                    -1.0f, 1.0f);
      changed |= ImGui::SliderFloat("Transmission", &surface.transmission,
                                    0.0f, 1.0f);
      changed |= ImGui::DragFloat("IOR", &surface.ior, 0.01f, 1.0f, 3.0f);
      changed |= ImGui::DragFloat("Thickness", &surface.thickness,
                                  0.01f, 0.0f, 10000.0f);
    }
    if (ImGui::CollapsingHeader("Render State")) {
      int alpha_mode = static_cast<int>(surface.alpha_mode);
      if (ImGui::Combo("Alpha mode", &alpha_mode,
                       "Opaque\0Masked\0Blend\0")) {
        surface.alpha_mode =
            static_cast<rendering::MaterialDesc::AlphaMode>(alpha_mode);
        surface.transparent =
            surface.alpha_mode == rendering::MaterialDesc::AlphaMode::Blend;
        changed = true;
      }
      if (surface.alpha_mode == rendering::MaterialDesc::AlphaMode::Masked) {
        changed |= ImGui::SliderFloat("Alpha cutoff", &surface.alpha_cutoff,
                                      0.0f, 1.0f);
      }
      changed |= ImGui::Checkbox("Double sided", &surface.double_sided);
      changed |= ImGui::Checkbox("Depth test", &surface.depth_test);
      changed |= ImGui::Checkbox("Depth write", &surface.depth_write);
      changed |= ImGui::Checkbox("Unlit", &surface.unlit);
    }

    ImGui::SeparatorText("Textures");
    changed |= drawMaterialTextureSlot(*material_draft_, "baseColor", "Base color");
    changed |= drawMaterialTextureSlot(*material_draft_, "normal", "Normal");
    changed |= drawMaterialTextureSlot(*material_draft_, "metallicRoughness",
                                       "Metal/Rough");
    changed |= drawMaterialTextureSlot(*material_draft_, "occlusion", "Occlusion");
    changed |= drawMaterialTextureSlot(*material_draft_, "emissive", "Emissive");
    changed |= drawMaterialTextureSlot(*material_draft_, "specular", "Specular");
    changed |= drawMaterialTextureSlot(*material_draft_, "specularColor",
                                       "Specular color");
    changed |= drawMaterialTextureSlot(*material_draft_, "clearcoat", "Clearcoat");
    changed |= drawMaterialTextureSlot(*material_draft_, "clearcoatRoughness",
                                       "Coat roughness");
    changed |= drawMaterialTextureSlot(*material_draft_, "clearcoatNormal",
                                       "Coat normal");
    changed |= drawMaterialTextureSlot(*material_draft_, "sheenColor", "Sheen color");
    changed |= drawMaterialTextureSlot(*material_draft_, "sheenRoughness",
                                       "Sheen roughness");
    changed |= drawMaterialTextureSlot(*material_draft_, "transmission",
                                       "Transmission");
    changed |= drawMaterialTextureSlot(*material_draft_, "thickness", "Thickness");

    if (changed) {
      material_draft_dirty_ = true;
      material_draft_error_.clear();
      previewMaterialDraft();
    }
    if (!material_draft_error_.empty()) {
      ImGui::TextColored({1.0f, 0.35f, 0.3f, 1.0f}, "%s",
                         material_draft_error_.c_str());
    }
    ImGui::BeginDisabled(!material_draft_dirty_);
    if (ImGui::Button("Save Material")) {
      const assets::MaterialSaveResult result =
          assets::saveMaterialAssetDesc(*material_draft_, material_draft_path_);
      if (result) {
        material_original_ = *material_draft_;
        material_draft_dirty_ = false;
        material_draft_error_.clear();
        bake_stale_ = true;
        scanCatalog(false);
      } else {
        material_draft_error_ = result.diagnostic;
      }
    }
    ImGui::SameLine();
    if (ImGui::Button("Revert") ||
        (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
         ImGui::IsKeyPressed(ImGuiKey_Escape))) {
      restoreMaterialPreview();
      material_draft_ = material_original_;
      material_draft_dirty_ = false;
      material_draft_error_.clear();
    }
    ImGui::EndDisabled();
  }

  void beginDocumentPropertyEdit(std::string label,
                                 scenes::SceneDocument before,
                                 bool rebuild_on_finish = false) {
    if (!property_edit_active_) {
      property_edit_active_ = true;
      property_edit_label_ = std::move(label);
      property_edit_before_ = std::move(before);
      property_edit_rebuild_ = rebuild_on_finish;
    }
    pending_recovery_ = true;
    recovery_at_ = std::chrono::steady_clock::now() + kRecoveryDebounce;
  }

  void finishDocumentPropertyEdit() {
    if (!property_edit_active_ || ImGui::IsAnyItemActive()) return;
    finishDocumentPropertyEditNow();
  }

  void finishDocumentPropertyEditNow(bool rebuild_preview = true) {
    if (!property_edit_active_) return;
    property_edit_active_ = false;
    const bool rebuild = property_edit_rebuild_;
    property_edit_rebuild_ = false;
    pushDocumentCommand(std::move(property_edit_label_),
                        std::move(property_edit_before_));
    if (rebuild && rebuild_preview) rebuildPreview();
  }

  void cancelDocumentPropertyEdit() {
    if (!property_edit_active_) return;
    document_ = std::move(property_edit_before_);
    property_edit_active_ = false;
    property_edit_rebuild_ = false;
    property_edit_label_.clear();
    rebuildPreview();
  }

  void drawSceneInspector() {
    std::array<char, 256> name{};
    std::copy_n(document_.name.data(), std::min(document_.name.size(), name.size() - 1u), name.data());
    if (ImGui::InputText("Scene name", name.data(), name.size(), ImGuiInputTextFlags_EnterReturnsTrue)) {
      scenes::SceneDocument before = document_;
      document_.name = name.data();
      pushDocumentCommand("Rename Scene", std::move(before));
    }
    ImGui::TextWrapped("Content root: %s", content_root_.string().c_str());
    ImGui::TextWrapped("Scene: %s", scene_path_.string().c_str());
    if (ImGui::Button("Migrate Legacy Render Components")) {
      scenes::SceneDocument next = document_;
      const LegacyRenderMigrationReport migrated =
          migrateSceneLegacyRenderComponents(next);
      if (!migrated.success()) {
        scene_migration_status_.clear();
        last_error_ = migrated.diagnostics.empty()
                          ? "Scene render migration failed"
                          : migrated.diagnostics.front();
      } else if (migrated.changed) {
        if (commitDocumentCommand("Migrate Legacy Render Components",
                                  std::move(next))) {
          scene_migration_status_ =
              "Migrated " + std::to_string(migrated.migrated_owners) +
              " scene owner(s); save the scene to commit";
          rebuildPreview();
          last_error_.clear();
        }
      } else {
        scene_migration_status_ = "No legacy render fields were found";
        last_error_.clear();
      }
    }
    if (!scene_migration_status_.empty()) {
      ImGui::TextColored({0.35f, 0.85f, 0.55f, 1.0f}, "%s",
                         scene_migration_status_.c_str());
    }
    ImGui::SeparatorText("Environment");
    if (!document_.environment) {
      if (ImGui::Button("Add Environment")) addEnvironment();
    } else {
      scenes::SceneEnvironment& environment = *document_.environment;
      scenes::SceneDocument before = document_;
      bool changed = ImGui::DragFloat("Intensity", &environment.component.intensity, 0.01f, 0.0f, 20.0f);
      changed |= ImGui::Checkbox("Draw sky", &environment.component.draw_skybox);
      changed |= ImGui::Checkbox("Enabled", &environment.component.enabled);
      ImGui::TextWrapped("Map: %s", environment.environment_map_asset_id.empty()
                                         ? "<none>"
                                         : environment.environment_map_asset_id.c_str());
      if (changed) {
        beginDocumentPropertyEdit("Edit Environment", std::move(before), true);
      }
    }
  }

  bool drawSelectedObjectHeader(world::Entity runtime) {
    bool selection_changed = false;
    ImGui::BeginChild("##object_header", {0.0f, 0.0f},
                      ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY,
                      ImGuiWindowFlags_NoScrollbar |
                          ImGuiWindowFlags_NoScrollWithMouse);
    if (selection_.kind == SelectionKind::Entity) {
      auto entity = findEntity(selection_.id);
      if (entity != document_.entities.end()) {
        std::array<char, 256> name{};
        std::copy_n(entity->name.data(),
                    std::min(entity->name.size(), name.size() - 1u),
                    name.data());
        ImGui::SetNextItemWidth(-36.0f);
        if (editor_semibold_font_ != nullptr) ImGui::PushFont(editor_semibold_font_);
        const bool renamed = ImGui::InputText(
            "##object_name", name.data(), name.size(),
            ImGuiInputTextFlags_EnterReturnsTrue);
        if (editor_semibold_font_ != nullptr) ImGui::PopFont();
        if (renamed) {
          scenes::SceneDocument before = document_;
          entity->name = name.data();
          world->setName(runtime, entity->name);
          pushDocumentCommand("Rename Entity", std::move(before));
        }
      }
    } else {
      const auto prefab = findPrefab(selection_.id);
      const std::string title =
          prefab == document_.prefab_instances.end()
              ? "Linked Prefab"
              : prefab->prefab_path.parent_path().filename().string();
      if (editor_semibold_font_ != nullptr) ImGui::PushFont(editor_semibold_font_);
      ImGui::TextUnformatted(title.empty() ? "Linked Prefab" : title.c_str());
      if (editor_semibold_font_ != nullptr) ImGui::PopFont();
    }
    ImGui::SameLine(ImGui::GetContentRegionMax().x - 28.0f);
    if (ImGui::Button("...", {28.0f, 0.0f})) {
      ImGui::OpenPopup("##object_actions");
    }
    if (ImGui::BeginPopup("##object_actions")) {
      if (ImGui::MenuItem("Duplicate", "Ctrl+D")) {
        duplicateSelected();
        selection_changed = true;
      }
      if (ImGui::MenuItem("Delete", "Delete")) {
        deleteSelection();
        selection_changed = true;
      }
      ImGui::EndPopup();
    }
    ImGui::TextDisabled("%s", selection_.id.c_str());
    if (selection_.kind == SelectionKind::Prefab) {
      const auto prefab = findPrefab(selection_.id);
      if (prefab != document_.prefab_instances.end()) {
        ImGui::TextWrapped("Source: %s",
                           prefab->prefab_path.generic_string().c_str());
      }
    }
    ImGui::EndChild();
    ImGui::Dummy({0.0f, 3.0f});
    return selection_changed;
  }

  void drawSelectionInspector() {
    const world::Entity runtime = selectedRuntimeEntity();
    if (!world->isAlive(runtime)) {
      ImGui::TextDisabled("Selection is unavailable in the preview");
      return;
    }
    if (drawSelectedObjectHeader(runtime)) return;
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::InputTextWithHint("##inspector_filter", "Search inspector",
                                 component_filter_,
                                 sizeof(component_filter_))) {
      settings_.inspector_filter = component_filter_;
    }
    const auto camera_record = selection_.kind == SelectionKind::Entity
                                   ? std::find_if(
                                         document_.cameras.begin(),
                                         document_.cameras.end(),
                                         [&](const scenes::SceneCamera& camera) {
                                           return camera.entity_id == selection_.id;
                                         })
                                   : document_.cameras.end();
    const auto static_record = selection_.kind == SelectionKind::Entity
                                   ? std::find_if(
                                         document_.static_components.begin(),
                                         document_.static_components.end(),
                                         [&](const scenes::SceneStaticComponent& value) {
                                           return value.entity_id == selection_.id;
                                         })
                                   : document_.static_components.end();
    bool reparented = false;
    if (inspectorFilterMatches("Transform", "TransformComponent")) {
      drawComponentCard("TransformComponent", "Transform", true, [&] {
        reparented = drawParentInspector();
        if (!reparented) drawTransformInspector(runtime);
      });
      if (reparented) return;
    }
    if (camera_record != document_.cameras.end() &&
        inspectorFilterMatches("Camera", "SceneCamera")) {
      drawComponentCard("SceneCamera", "Camera", false, [&] {
        drawCameraRecordInspector(*camera_record);
      });
    }
    if (static_record != document_.static_components.end() &&
        inspectorFilterMatches("Static Bake Membership", "SceneStatic")) {
      drawComponentCard("SceneStatic", "Static Bake Membership", false, [&] {
        drawStaticRecordInspector(*static_record);
      });
    }
    if (selection_.kind == SelectionKind::Prefab) {
      if (inspectorFilterMatches("Static Membership", "StaticComponent")) {
        drawComponentCard("PrefabStatic", "Static Membership", false, [&] {
          drawPrefabStaticInspector();
        });
      }
      if (inspectorFilterMatches("Prefab Variables", "PrefabVariables")) {
        drawComponentCard("PrefabVariables", "Prefab Variables", false, [&] {
          drawPrefabVariables();
        });
      }
      if (inspectorFilterMatches("Linked Prefab Contents", "PrefabSource")) {
        drawComponentCard("PrefabSource", "Linked Prefab Contents", false, [&] {
          drawResolvedPrefabHierarchy();
        });
      }
    }
    if (selection_.kind == SelectionKind::Entity) {
      const bool has_scene_light = std::any_of(
          document_.lights.begin(), document_.lights.end(),
          [&](const scenes::SceneLight& light) {
            return light.entity_id == selection_.id;
          });
      if (has_scene_light && inspectorFilterMatches("Light", "SceneLight")) {
        drawComponentCard("SceneLight", "Light", false, [&] {
          drawLightInspector(runtime);
        });
      }
      if (selection_.id == terrain_entity_id_ &&
          inspectorFilterMatches("Terrain", "TerrainComponent")) {
        drawComponentCard("TerrainComponent", "Terrain", true, [&] {
          drawTerrainInspector();
        });
      }
      drawAuthoredComponentCards();
      drawAddComponentMenu();
    }
  }

  void drawCameraRecordInspector(scenes::SceneCamera& camera) {
    scenes::SceneDocument before = document_;
    bool changed = false;
    changed |= ImGui::Checkbox("Perspective", &camera.component.perspective);
    changed |= ImGui::Checkbox("Primary camera", &camera.component.is_primary);
    changed |= ImGui::Checkbox("Render shadows", &camera.component.render_shadows);
    if (camera.component.perspective) {
      changed |= ImGui::DragFloat("Field of view",
                                  &camera.component.fov_y_degrees,
                                  0.25f, 1.0f, 179.0f);
    } else {
      changed |= ImGui::DragFloat("Ortho left", &camera.component.ortho_left,
                                  0.05f);
      changed |= ImGui::DragFloat("Ortho right", &camera.component.ortho_right,
                                  0.05f);
      changed |= ImGui::DragFloat("Ortho top", &camera.component.ortho_top,
                                  0.05f);
      changed |= ImGui::DragFloat("Ortho bottom", &camera.component.ortho_bottom,
                                  0.05f);
    }
    changed |= ImGui::DragFloat("Near clip", &camera.component.near_clip,
                                0.01f, 0.0001f,
                                std::max(camera.component.far_clip - 0.0001f,
                                         0.0001f));
    changed |= ImGui::DragFloat("Far clip", &camera.component.far_clip,
                                1.0f,
                                camera.component.near_clip + 0.0001f,
                                10000000.0f);
    if (changed) {
      camera.component.fov_y_degrees =
          std::clamp(camera.component.fov_y_degrees, 1.0f, 179.0f);
      camera.component.near_clip = std::max(camera.component.near_clip, 0.0001f);
      camera.component.far_clip =
          std::max(camera.component.far_clip,
                   camera.component.near_clip + 0.0001f);
      if (camera.component.is_primary) {
        for (scenes::SceneCamera& other : document_.cameras) {
          if (&other != &camera) other.component.is_primary = false;
        }
      }
      beginDocumentPropertyEdit("Edit Camera", std::move(before), true);
    }
  }

  void drawStaticRecordInspector(scenes::SceneStaticComponent& record) {
    scenes::SceneDocument before = document_;
    bool changed = false;
    changed |= ImGui::Checkbox("Static transform", &record.transform);
    changed |= ImGui::Checkbox("Static rendering", &record.render);
    changed |= ImGui::Checkbox("Bake lighting", &record.lighting);
    changed |= ImGui::Checkbox("Cast baked shadows", &record.casts_shadows);
    changed |= ImGui::Checkbox("Receive lightmaps",
                               &record.receives_baked_lighting);
    changed |= ImGui::Checkbox("Bake collision", &record.collision);
    changed |= ImGui::Checkbox("Bake navigation", &record.navigation);
    if (!record.mesh_asset_key.empty()) {
      ImGui::TextWrapped("Mesh: %s", record.mesh_asset_key.c_str());
    }
    if (!record.material_asset_key.empty()) {
      ImGui::TextWrapped("Material: %s", record.material_asset_key.c_str());
    }
    if (changed) {
      beginDocumentPropertyEdit("Edit Static Flags", std::move(before), true);
    }
  }

  bool drawParentInspector() {
    std::string current_parent;
    if (selection_.kind == SelectionKind::Entity) {
      const auto entity = findEntity(selection_.id);
      if (entity == document_.entities.end() || entity->parent_id.empty()) return false;
      current_parent = entity->parent_id;
    } else if (selection_.kind == SelectionKind::Prefab) {
      const auto prefab = findPrefab(selection_.id);
      if (prefab == document_.prefab_instances.end()) return false;
      current_parent = prefab->parent_entity_id;
    } else {
      return false;
    }

    const auto current = std::find_if(
        document_.entities.begin(), document_.entities.end(),
        [&](const scenes::SceneEntity& entity) { return entity.id == current_parent; });
    const std::string preview = current == document_.entities.end()
                                    ? std::string("<scene root>")
                                    : (current->name.empty() ? current->id : current->name);
    if (std::none_of(document_.entities.begin(), document_.entities.end(),
                     [&](const scenes::SceneEntity& candidate) {
                       return canReparent(document_, selection_, candidate.id);
                     })) {
      ImGui::TextDisabled("Parent: protected scene data");
      return false;
    }
    if (!ImGui::BeginCombo("Parent", preview.c_str())) return false;
    for (const scenes::SceneEntity& candidate : document_.entities) {
      if (!canReparent(document_, selection_, candidate.id)) {
        continue;
      }
      const bool selected = candidate.id == current_parent;
      const std::string label =
          (candidate.name.empty() ? candidate.id : candidate.name) +
          "##" + candidate.id;
      if (ImGui::Selectable(label.c_str(), selected) && !selected) {
        scenes::SceneDocument before = document_;
        std::string error;
        const bool reparented = reparentPreservingWorld(
            document_, selection_, candidate.id, &error);
        ImGui::EndCombo();
        if (!reparented) {
          last_error_ = std::move(error);
          return true;
        }
        pushDocumentCommand("Reparent", std::move(before));
        rebuildPreview();
        return true;
      }
      if (selected) ImGui::SetItemDefaultFocus();
    }
    ImGui::EndCombo();
    return false;
  }

  void drawTransformInspector(world::Entity entity) {
    if (!world->has<components::TransformComponent>(entity)) return;
    auto& transform = world->get<components::TransformComponent>(entity);
    scenes::SceneTransform edited = fromRuntimeTransform(transform);
    if (selection_.kind == SelectionKind::Prefab) {
      const auto prefab = findPrefab(selection_.id);
      if (prefab != document_.prefab_instances.end()) edited = prefab->transform;
    }
    glm::vec3 euler =
        glm::degrees(glm::eulerAngles(math::toGlm(edited.rotation)));
    scenes::SceneDocument before = document_;
    bool changed = false;
    changed |= ImGui::DragFloat3("Position", &edited.position.x, 0.1f);
    changed |= ImGui::DragFloat3("Rotation", &euler.x, 0.5f);
    changed |= ImGui::DragFloat3("Scale", &edited.scale.x, 0.02f, 0.001f, 1000.0f);
    if (changed) {
      edited.rotation = math::fromGlm(glm::quat(glm::radians(euler)));
      if (selection_.kind == SelectionKind::Prefab) {
        const auto prefab = findPrefab(selection_.id);
        if (prefab != document_.prefab_instances.end()) prefab->transform = edited;
        const auto saved = prefab_saved_root_transforms_.find(selection_.id);
        const scenes::SceneTransform runtime_transform =
            saved == prefab_saved_root_transforms_.end()
                ? edited
                : composeSceneTransforms(edited, saved->second);
        transform.setLocalPosition(runtime_transform.position);
        transform.setLocalRotation(runtime_transform.rotation);
        transform.setLocalScale(runtime_transform.scale);
        beginDocumentPropertyEdit("Transform", std::move(before), true);
      } else {
        transform.setLocalPosition(edited.position);
        transform.setLocalRotation(edited.rotation);
        transform.setLocalScale(edited.scale);
        syncSelectionTransform(transform);
        beginDocumentPropertyEdit("Transform", std::move(before));
      }
    }
  }

  void drawPrefabVariables() {
    auto prefab = findPrefab(selection_.id);
    if (prefab == document_.prefab_instances.end()) return;
    const std::filesystem::path absolute = content_root_ / prefab->prefab_path;
    const auto loaded = prefabs::loadPrefabDocument(absolute);
    if (!loaded.success() || !loaded.document || loaded.document->variables.empty()) return;
    for (auto it = loaded.document->variables.begin(); it != loaded.document->variables.end(); ++it) {
      if (!it.value().is_object()) continue;
      const std::string type = it.value().value("type", "");
      const nlohmann::json default_value = it.value().value("default", nlohmann::json{});
      nlohmann::json value = prefab->variables.contains(it.key()) ? prefab->variables[it.key()]
                                                                 : default_value;
      bool changed = false;
      if (type == "bool" && value.is_boolean()) {
        bool scalar = value.get<bool>();
        changed = ImGui::Checkbox(it.key().c_str(), &scalar);
        value = scalar;
      } else if (type == "float" && value.is_number()) {
        float scalar = value.get<float>();
        changed = ImGui::DragFloat(it.key().c_str(), &scalar, 0.05f);
        value = scalar;
      } else if (type == "int" && value.is_number_integer()) {
        int scalar = value.get<int>();
        changed = ImGui::DragInt(it.key().c_str(), &scalar);
        value = scalar;
      } else if (type == "vec3" && value.is_array() && value.size() == 3u) {
        math::Vec3 vector{value[0].get<float>(), value[1].get<float>(), value[2].get<float>()};
        changed = ImGui::DragFloat3(it.key().c_str(), &vector.x, 0.05f);
        value = nlohmann::json::array({vector.x, vector.y, vector.z});
      } else if (type == "color" && value.is_array() && value.size() >= 3u) {
        float color[4]{value[0].get<float>(), value[1].get<float>(), value[2].get<float>(),
                       value.size() > 3u ? value[3].get<float>() : 1.0f};
        changed = ImGui::ColorEdit4(it.key().c_str(), color);
        value = nlohmann::json::array({color[0], color[1], color[2], color[3]});
      } else if (type == "string" && value.is_string()) {
        std::array<char, 256> text{};
        const std::string current = value.get<std::string>();
        std::copy_n(current.data(), std::min(current.size(), text.size() - 1u), text.data());
        changed = ImGui::InputText(it.key().c_str(), text.data(), text.size(),
                                   ImGuiInputTextFlags_EnterReturnsTrue);
        value = text.data();
      }
      if (changed) {
        scenes::SceneDocument before = document_;
        prefab->variables[it.key()] = std::move(value);
        beginDocumentPropertyEdit("Edit Prefab Variable", std::move(before), true);
        break;
      }
    }
  }

  void drawPrefabStaticInspector() {
    auto prefab = findPrefab(selection_.id);
    if (prefab == document_.prefab_instances.end()) return;
    scenes::SceneDocument before = document_;
    int mode = !prefab->static_component.has_value()
                   ? 0
                   : (prefab->static_component->enabled ? 1 : 2);
    bool changed = false;
    if (ImGui::Combo("Membership", &mode,
                     "Inherit from parent\0Static\0Not static\0")) {
      if (mode == 0) {
        prefab->static_component.reset();
      } else if (mode == 1) {
        if (!prefab->static_component.has_value()) {
          prefab->static_component = components::StaticComponent{};
        }
        prefab->static_component->enabled = true;
      } else {
        if (!prefab->static_component.has_value()) {
          prefab->static_component = components::StaticComponent{};
        }
        prefab->static_component->enabled = false;
      }
      changed = true;
    }
    if (prefab->static_component.has_value()) {
      auto& membership = *prefab->static_component;
      if (membership.enabled) {
        changed |= ImGui::Checkbox("Apply to prefab descendants",
                                   &membership.include_descendants);
        static constexpr std::array<const char*, 5> names{
            "Rendering", "Lighting", "Shadows", "Collision", "Navigation"};
        for (uint32_t bit = 0u; bit < names.size(); ++bit) {
          bool enabled = (membership.flags & (1u << bit)) != 0u;
          if (ImGui::Checkbox(names[bit], &enabled)) {
            if (enabled) {
              membership.flags |= 1u << bit;
            } else {
              membership.flags &= ~(1u << bit);
            }
            changed = true;
          }
        }
      } else {
        ImGui::TextDisabled(
            "This prefab subtree opts out of inherited static membership.");
      }
    } else {
      ImGui::TextDisabled(
          "The selected parent group determines static bake membership.");
    }
    if (changed) {
      beginDocumentPropertyEdit("Edit Prefab Static Membership",
                                std::move(before), true);
    }
  }

  void drawResolvedPrefabHierarchy() {
    const auto prefab = findPrefab(selection_.id);
    if (prefab == document_.prefab_instances.end()) return;
    const auto loaded = prefabs::loadPrefabDocument(
        content_root_ / prefab->prefab_path);
    if (!loaded.success() || !loaded.document) {
      ImGui::TextColored({1.0f, 0.35f, 0.25f, 1.0f},
                         "The linked prefab source could not be resolved.");
      return;
    }
    ImGui::TextDisabled("Read-only; edit the source prefab to change components.");
    const prefabs::PrefabDocument& source = *loaded.document;
    std::vector<std::vector<size_t>> children(source.nodes.size());
    for (size_t index = 0u; index < source.nodes.size(); ++index) {
      if (source.nodes[index].parent.has_value() &&
          *source.nodes[index].parent < children.size()) {
        children[*source.nodes[index].parent].push_back(index);
      }
    }
    const auto draw_node = [&](const auto& self, size_t index) -> void {
      if (index >= source.nodes.size()) return;
      const prefabs::PrefabNode& node = source.nodes[index];
      ImGui::PushID(static_cast<int>(index));
      ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanAvailWidth |
                                 ImGuiTreeNodeFlags_DefaultOpen;
      if (children[index].empty() && node.components.empty()) {
        flags |= ImGuiTreeNodeFlags_Leaf;
      }
      const std::string name = node.name.empty()
                                   ? "Node " + std::to_string(node.id)
                                   : node.name;
      const bool open = ImGui::TreeNodeEx("##resolved_prefab_node", flags,
                                          "%s%s", name.c_str(),
                                          index == source.root ? " [Root]" : "");
      if (open) {
        if (node.components.is_object()) {
          for (auto it = node.components.begin(); it != node.components.end(); ++it) {
            ImGui::BulletText("%s", it.key().c_str());
          }
        }
        for (const size_t child : children[index]) self(self, child);
        ImGui::TreePop();
      }
      ImGui::PopID();
    };
    draw_node(draw_node, source.root);
  }

  void drawLightInspector(world::Entity runtime) {
    const auto light = std::find_if(document_.lights.begin(), document_.lights.end(),
                                    [&](const scenes::SceneLight& value) {
                                      return value.entity_id == selection_.id;
                                    });
    if (light == document_.lights.end()) return;
    scenes::SceneDocument before = document_;
    int type = static_cast<int>(light->component.type);
    bool changed = ImGui::Combo("Type", &type, "Directional\0Point\0Spot\0");
    light->component.type = static_cast<components::LightComponent::Type>(type);
    int bake_mode = static_cast<int>(light->component.bake_mode);
    if (ImGui::Combo("Mode", &bake_mode, "Realtime\0Mixed\0Baked\0")) {
      light->component.bake_mode =
          static_cast<components::LightComponent::BakeMode>(bake_mode);
      changed = true;
    }
    changed |= ImGui::ColorEdit4("Color", &light->component.color.r);
    changed |= ImGui::DragFloat("Intensity", &light->component.intensity, 0.05f, 0.0f, 10000.0f);
    if (light->component.type != components::LightComponent::Type::Directional) {
      changed |= ImGui::DragFloat("Range", &light->component.range, 0.1f, 0.0f, 100000.0f);
    }
    if (light->component.type == components::LightComponent::Type::Spot) {
      changed |= ImGui::DragFloat("Inner cone", &light->component.inner_cone_degrees,
                                  0.25f, 0.0f, 179.0f);
      changed |= ImGui::DragFloat("Outer cone", &light->component.outer_cone_degrees,
                                  0.25f, 0.0f, 179.0f);
      light->component.outer_cone_degrees = std::clamp(
          light->component.outer_cone_degrees, 0.0f, 179.0f);
      light->component.inner_cone_degrees = std::clamp(
          light->component.inner_cone_degrees, 0.0f,
          light->component.outer_cone_degrees);
    }
    changed |= ImGui::Checkbox("Cast shadows", &light->component.casts_shadows);
    if (light->component.type == components::LightComponent::Type::Directional) {
      changed |= ImGui::DragFloat("Shadow extent",
                                  &light->component.shadow_extent,
                                  0.5f, 0.0f, 100000.0f);
    }
    if (changed) {
      world->add(runtime, light->component);
      beginDocumentPropertyEdit("Edit Light", std::move(before));
    }
  }

  void drawInspectorSectionHeading(const char* label) {
    if (editor_semibold_font_ != nullptr) ImGui::PushFont(editor_semibold_font_);
    ImGui::TextColored({0.72f, 0.77f, 0.83f, 1.0f}, "%s", label);
    if (editor_semibold_font_ != nullptr) ImGui::PopFont();
    ImGui::Separator();
  }

  void drawTerrainInspector() {
    if (!terrain_canvas_ || !world->isAlive(terrain_entity_) ||
        !world->has<components::TerrainComponent>(terrain_entity_)) {
      ImGui::TextDisabled("Terrain preview data is unavailable.");
      return;
    }
    if (!terrain_authoring_valid_) {
      ImGui::TextColored({1.0f, 0.42f, 0.28f, 1.0f},
                         "Source map failed to load; authoring is disabled.");
    }
    const auto mode_button = [&](ToolMode mode, const char* label) {
      const bool selected = tool_ == mode;
      if (selected) {
        ImGui::PushStyleColor(
            ImGuiCol_Button,
            ImGui::GetStyleColorVec4(ImGuiCol_HeaderActive));
      }
      if (ImGui::Button(label) && !selected) changeTool(mode);
      if (selected) ImGui::PopStyleColor();
    };
    const auto draw_brush = [&] {
      ImGui::DragFloat("Radius", &terrain_brush_.radius, 0.25f, 0.1f,
                       500.0f);
      ImGui::SliderFloat("Strength", &terrain_brush_.strength, 0.001f,
                         1.0f);
      int falloff = static_cast<int>(terrain_brush_.falloff);
      if (ImGui::Combo("Falloff", &falloff,
                       "Constant\0Linear\0Smooth\0")) {
        terrain_brush_.falloff =
            static_cast<scene_authoring::TerrainBrushFalloff>(falloff);
      }
    };
    const auto tab_flags = [&](int tab) {
      return terrain_tab_selection_pending_ &&
                     settings_.terrain_inspector_tab == tab
                 ? ImGuiTabItemFlags_SetSelected
                 : ImGuiTabItemFlags_None;
    };

    bool component_changed = false;
    if (ImGui::BeginTabBar("##terrain_authoring_tabs")) {
      if (ImGui::BeginTabItem("Sculpt", nullptr, tab_flags(0))) {
        settings_.terrain_inspector_tab = 0;
        ImGui::BeginChild("##sculpt_window", {0.0f, 0.0f},
                          ImGuiChildFlags_Borders |
                              ImGuiChildFlags_AutoResizeY,
                          ImGuiWindowFlags_NoScrollbar |
                              ImGuiWindowFlags_NoScrollWithMouse);
        const auto& component =
            world->get<components::TerrainComponent>(terrain_entity_);
        const auto& desc = terrain_canvas_->desc();
        drawInspectorSectionHeading("Component Data");
        ImGui::Text("Source: %s",
                    terrain_authoring_valid_ ? "Valid" : "Invalid");
        ImGui::Text("Resolution: %u x %u (control %u x %u)",
                    desc.resolution, desc.resolution,
                    terrain_canvas_->controlResolution(),
                    terrain_canvas_->controlResolution());
        ImGui::Text("Dimensions: %.1f x %.1f", desc.terrain_size,
                    desc.terrain_size);
        ImGui::Text("Height: %.1f scale, %.1f offset", desc.height_scale,
                    desc.height_offset);
        ImGui::TextWrapped("Height source: %s",
                           component.height_image.generic_string().c_str());
        ImGui::Dummy({0.0f, 3.0f});
        drawInspectorSectionHeading("Authoring Tools");
        ImGui::BeginDisabled(!terrain_authoring_valid_);
        mode_button(ToolMode::SculptRaise, "Raise");
        ImGui::SameLine();
        mode_button(ToolMode::SculptLower, "Lower");
        ImGui::SameLine();
        mode_button(ToolMode::SculptSmooth, "Smooth");
        mode_button(ToolMode::SculptFlatten, "Flatten");
        ImGui::SameLine();
        mode_button(ToolMode::SculptSetHeight, "Set Height");
        if (tool_ >= ToolMode::SculptRaise &&
            tool_ <= ToolMode::SculptSetHeight) {
          ImGui::SameLine();
          if (ImGui::Button("Stop")) changeTool(ToolMode::Select);
        }
        ImGui::SeparatorText("Brush");
        draw_brush();
        if (tool_ == ToolMode::SculptSetHeight) {
          ImGui::SliderFloat("Target height", &set_height_target_, 0.0f,
                             1.0f);
        } else if (tool_ == ToolMode::SculptFlatten) {
          ImGui::TextDisabled(
              "Flatten target is sampled when the stroke begins");
        }
        ImGui::EndDisabled();
        ImGui::EndChild();
        ImGui::EndTabItem();
      }

      if (ImGui::BeginTabItem("Materials", nullptr, tab_flags(1))) {
        settings_.terrain_inspector_tab = 1;
        ImGui::BeginChild("##materials_window", {0.0f, 0.0f},
                          ImGuiChildFlags_Borders |
                              ImGuiChildFlags_AutoResizeY,
                          ImGuiWindowFlags_NoScrollbar |
                              ImGuiWindowFlags_NoScrollWithMouse);
        const auto& component =
            world->get<components::TerrainComponent>(terrain_entity_);
        drawInspectorSectionHeading("Component Data");
        for (int layer = 0; layer < 4 && !component_changed; ++layer) {
          ImGui::PushID(layer);
          components::TerrainMaterialLayer layer_value{};
          layer_value.name = "Layer " + std::to_string(layer + 1);
          if (static_cast<size_t>(layer) < component.material_layers.size()) {
            layer_value = component.material_layers[static_cast<size_t>(layer)];
          }
          ImGui::SeparatorText(
              ("Layer " + std::to_string(layer + 1)).c_str());
          std::array<char, 128> name{};
          std::copy_n(layer_value.name.data(),
                      std::min(layer_value.name.size(), name.size() - 1u),
                      name.data());
          if (ImGui::InputText("Name", name.data(), name.size())) {
            scenes::SceneDocument before = document_;
            auto edited = component;
            edited.material_layers.resize(4u);
            edited.material_layers[static_cast<size_t>(layer)] = layer_value;
            edited.material_layers[static_cast<size_t>(layer)].name =
                name.data();
            component_changed = applyTerrainComponentEdit(
                std::move(edited), std::move(before),
                "Edit Terrain Material Layer");
          }
          bool enabled = layer_value.enabled;
          if (!component_changed && ImGui::Checkbox("Enabled", &enabled)) {
            if (enabled && layer_value.material_key.empty() &&
                layer_value.albedo_image.empty()) {
              last_error_ =
                  "Assign a material before enabling this terrain layer";
            } else {
              scenes::SceneDocument before = document_;
              auto edited = component;
              edited.material_layers.resize(4u);
              edited.material_layers[static_cast<size_t>(layer)] =
                  layer_value;
              edited.material_layers[static_cast<size_t>(layer)].enabled =
                  enabled;
              component_changed = applyTerrainComponentEdit(
                  std::move(edited), std::move(before),
                  "Edit Terrain Material Layer");
            }
          }
          float uv_scale = layer_value.uv_scale;
          if (!component_changed &&
              ImGui::DragFloat("UV scale", &uv_scale, 0.1f, 0.01f,
                               10000.0f, "%.2f")) {
            scenes::SceneDocument before = document_;
            auto edited = component;
            edited.material_layers.resize(4u);
            edited.material_layers[static_cast<size_t>(layer)] = layer_value;
            edited.material_layers[static_cast<size_t>(layer)].uv_scale =
                std::max(uv_scale, 0.01f);
            component_changed = applyTerrainComponentEdit(
                std::move(edited), std::move(before),
                "Edit Terrain Material Layer");
          }
          const std::string material = layer_value.material_key.empty()
                                           ? "<drop material>"
                                           : layer_value.material_key;
          if (!component_changed) {
            ImGui::Selectable(("Material: " + material).c_str(), false);
            if (ImGui::BeginDragDropTarget()) {
              if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(
                      "KARMA_TERRAIN_MATERIAL")) {
                const std::string key(
                    static_cast<const char*>(payload->Data));
                const AssetEntry* entry = catalog_.findByKey(key);
                if (entry != nullptr && entry->valid &&
                    entry->kind == AssetKind::Material) {
                  splat_layer_ = layer;
                  settings_.terrain_material_layer = layer;
                  assignTerrainMaterial(*entry);
                  component_changed = true;
                }
              }
              ImGui::EndDragDropTarget();
            }
          }
          ImGui::PopID();
        }
        if (!component_changed) {
          ImGui::Dummy({0.0f, 3.0f});
          drawInspectorSectionHeading("Authoring Tools");
          ImGui::BeginDisabled(!terrain_authoring_valid_);
          ImGui::TextDisabled("Active paint layer");
          for (int layer = 0; layer < 4; ++layer) {
            ImGui::PushID(100 + layer);
            if (layer > 0) ImGui::SameLine();
            const std::string label = std::to_string(layer + 1);
            if (ImGui::RadioButton(label.c_str(), splat_layer_ == layer)) {
              splat_layer_ = layer;
              settings_.terrain_material_layer = layer;
            }
            ImGui::PopID();
          }
          mode_button(ToolMode::PaintSplat, "Paint Material");
          if (tool_ == ToolMode::PaintSplat) {
            ImGui::SameLine();
            if (ImGui::Button("Stop")) changeTool(ToolMode::Select);
          }
          ImGui::SeparatorText("Brush");
          draw_brush();
          ImGui::EndDisabled();
        }
        ImGui::EndChild();
        ImGui::EndTabItem();
      }

      if (ImGui::BeginTabItem("Foliage", nullptr, tab_flags(2))) {
        settings_.terrain_inspector_tab = 2;
        ImGui::BeginChild("##foliage_window", {0.0f, 0.0f},
                          ImGuiChildFlags_Borders |
                              ImGuiChildFlags_AutoResizeY,
                          ImGuiWindowFlags_NoScrollbar |
                              ImGuiWindowFlags_NoScrollWithMouse);
        drawFoliageInspector();
        ImGui::EndChild();
        ImGui::EndTabItem();
      }
      ImGui::EndTabBar();
    }
    terrain_tab_selection_pending_ = false;
  }

  bool applyTerrainComponentEdit(components::TerrainComponent edited,
                                 scenes::SceneDocument before,
                                 std::string label) {
    edited.material_layers.resize(4u);
    for (size_t index = 0u; index < edited.material_layers.size(); ++index) {
      auto& layer = edited.material_layers[index];
      if (layer.name.empty()) {
        layer.name = "Layer " + std::to_string(index + 1u);
      }
      if (layer.material_key.empty() && layer.albedo_image.empty()) {
        layer.enabled = false;
      }
      if (!std::isfinite(layer.uv_scale) || layer.uv_scale <= 0.0f ||
          (layer.enabled && layer.material_key.empty() &&
           layer.albedo_image.empty())) {
        last_error_ = "Terrain material layer settings are invalid";
        return false;
      }
    }
    auto authored_entity = findEntity(terrain_entity_id_);
    if (authored_entity == document_.entities.end()) {
      last_error_ = "The editable terrain is missing from the scene document";
      return false;
    }
    components::TerrainComponent authored =
        portableTerrainComponent(edited);
    const nlohmann::json serialized =
        serializeTemporaryComponent(authored, "TerrainComponent");
    if (serialized.empty()) {
      last_error_ = "Terrain component validation failed";
      return false;
    }
    authored_entity->components["TerrainComponent"] = serialized;
    world->add(terrain_entity_, std::move(edited));
    beginDocumentPropertyEdit(std::move(label), std::move(before), true);
    last_error_.clear();
    return true;
  }

  void drawFoliageInspector() {
    drawInspectorSectionHeading("Foliage Layers");
    if (foliage_layers_.empty()) {
      ImGui::PushStyleColor(ImGuiCol_Text,
                            ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
      ImGui::TextWrapped(
          "No foliage layers. Create one and assign a base mesh to begin.");
      ImGui::PopStyleColor();
    } else {
      for (const FoliageLayerState& layer : foliage_layers_) {
        ImGui::PushID(layer.entity_id.c_str());
        const bool active =
            settings_.active_foliage_layer_id == layer.entity_id;
        const std::string label = layer.name + "  (" +
                                  std::to_string(layer.layer.instanceCount()) +
                                  ")";
        if (ImGui::Selectable(label.c_str(), active)) {
          finishFoliageStroke();
          settings_.active_foliage_layer_id = layer.entity_id;
          changeTool(ToolMode::Select);
          ImGui::PopID();
          return;
        }
        ImGui::PopID();
      }
    }
    if (ImGui::Button("+ New Layer")) {
      pending_foliage_mesh_.clear();
      pending_foliage_prefab_.clear();
      pending_foliage_package_.clear();
      open_create_foliage_ = true;
    }
    ImGui::SameLine();
    FoliageLayerState* selected_layer = selectedFoliageLayer();
    ImGui::BeginDisabled(selected_layer == nullptr ||
                         !selected_layer->source_valid);
    if (ImGui::Button("Duplicate")) {
      duplicateActiveFoliageLayer();
      ImGui::EndDisabled();
      return;
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(selected_layer == nullptr);
    if (ImGui::Button("Delete")) {
      deleteActiveFoliageLayer();
      ImGui::EndDisabled();
      return;
    }
    ImGui::EndDisabled();
    ImGui::Selectable(
        "Drop a mesh or prefab here to create a foliage layer", false);
    if (ImGui::BeginDragDropTarget()) {
      if (const ImGuiPayload* payload =
              ImGui::AcceptDragDropPayload("KARMA_MESH_ASSET")) {
        const std::string key(static_cast<const char*>(payload->Data));
        const AssetEntry* entry = catalog_.findByKey(key);
        if (entry != nullptr && entry->valid &&
            entry->kind == AssetKind::Mesh) {
          pending_foliage_mesh_ = entry->key;
          pending_foliage_prefab_.clear();
          pending_foliage_package_ = entry->package_path;
          open_create_foliage_ = true;
        }
      }
      if (const ImGuiPayload* payload =
              ImGui::AcceptDragDropPayload("KARMA_PREFAB")) {
        const std::filesystem::path path(
            static_cast<const char*>(payload->Data));
        const AssetEntry* entry = catalog_.findPrefab(path);
        if (entry != nullptr && entry->valid) {
          pending_foliage_prefab_ = entry->path;
          pending_foliage_mesh_.clear();
          pending_foliage_package_.clear();
          open_create_foliage_ = true;
        }
      }
      ImGui::EndDragDropTarget();
    }

    FoliageLayerState* state = selectedFoliageLayer();
    if (state == nullptr) return;
    const world::Entity runtime = preview_.find(state->entity_id);
    if (!world->isAlive(runtime) ||
        !world->has<components::FoliageComponent>(runtime)) {
      ImGui::TextDisabled("The selected foliage preview is unavailable.");
      return;
    }
    const auto& component = world->get<components::FoliageComponent>(runtime);
    if (!state->source_valid) {
      ImGui::TextColored({1.0f, 0.35f, 0.25f, 1.0f},
                         "Sidecar failed to load; editing is disabled.");
    }
    std::array<char, 128> layer_name{};
    std::copy_n(state->name.data(),
                std::min(state->name.size(), layer_name.size() - 1u),
                layer_name.data());
    if (ImGui::InputText("Layer name", layer_name.data(), layer_name.size(),
                         ImGuiInputTextFlags_EnterReturnsTrue)) {
      scenes::SceneDocument before = document_;
      if (auto entity = findEntity(state->entity_id);
          entity != document_.entities.end()) {
        entity->name = layer_name.data();
        state->name = entity->name;
        world->setName(runtime, entity->name);
        pushDocumentCommand("Rename Foliage Layer", std::move(before));
      }
      return;
    }
    ImGui::Dummy({0.0f, 3.0f});
    drawInspectorSectionHeading("Component Data");
    ImGui::Text("Instances: %zu", state->layer.instanceCount());
    ImGui::SeparatorText("Render Source");
    const bool prefab_backed = !component.prefab_path.empty();
    const std::string source_label = prefab_backed
                                         ? "Prefab: " +
                                               component.prefab_path.generic_string()
                                         : "Mesh: " + component.mesh_asset_key;
    ImGui::Selectable(source_label.c_str(), false);
    if (ImGui::BeginDragDropTarget()) {
      if (const ImGuiPayload* payload =
              ImGui::AcceptDragDropPayload("KARMA_MESH_ASSET")) {
        const std::string key(static_cast<const char*>(payload->Data));
        const auto entry = std::find_if(
            catalog_.entries().begin(), catalog_.entries().end(),
            [&](const AssetEntry& candidate) {
              return candidate.valid && candidate.kind == AssetKind::Mesh &&
                     candidate.key == key;
            });
        if (entry != catalog_.entries().end()) {
          assignFoliageMesh(*entry);
          ImGui::EndDragDropTarget();
          return;
        }
      }
      if (const ImGuiPayload* payload =
              ImGui::AcceptDragDropPayload("KARMA_PREFAB")) {
        const AssetEntry* entry = catalog_.findPrefab(
            std::filesystem::path(static_cast<const char*>(payload->Data)));
        if (entry != nullptr && entry->valid) {
          assignFoliagePrefab(*entry);
          ImGui::EndDragDropTarget();
          return;
        }
      }
      ImGui::EndDragDropTarget();
    }
    if (prefab_backed) {
      if (drawFoliagePrefabSource(component)) return;
    } else {
      if (drawFoliageMaterialSlots(component)) return;
      auto authored = findEntity(state->entity_id);
      if (authored != document_.entities.end()) {
        if (authored->components.contains("LODComponent")) {
          nlohmann::json lod_payload = authored->components["LODComponent"];
          bool committed = false;
          const bool lod_changed = drawLodLevelsEditor(
              lod_payload,
              component.mesh_asset_key,
              authored->components["FoliageComponent"].value(
                  "materials", nlohmann::json::array()),
              [&](const nlohmann::json& edited,
                  const AssetEntry& asset,
                  std::string label) {
                return commitFoliageLodAssetPayload(
                    state->entity_id, edited, asset, std::move(label));
              },
              committed);
          if (committed) return;
          if (lod_changed) {
            applyFoliageLodPayload(state->entity_id,
                                   std::move(lod_payload),
                                   "Edit Foliage LOD");
            return;
          }
        } else if (ImGui::Button("Add LOD Component")) {
          applyFoliageLodPayload(state->entity_id,
                                 defaultLodComponentPayload(),
                                 "Add Foliage LOD");
          return;
        }
      }
    }
    bool visible = component.visible;
    bool base_shadows = component.shadow_visible;
    bool base_changed = ImGui::Checkbox("Visible", &visible);
    base_changed |= ImGui::Checkbox("Base casts shadows", &base_shadows);
    if (base_changed) {
      scenes::SceneDocument before = document_;
      auto edited = component;
      edited.visible = visible;
      edited.shadow_visible = base_shadows;
      applyFoliageComponentEdit(std::move(edited), std::move(before),
                                "Edit Foliage Rendering");
      return;
    }

    ImGui::SeparatorText("Streaming");
    float view_distance = component.view_distance;
    float chunk_size = component.chunk_size;
    int max_resident = static_cast<int>(std::min<uint32_t>(
        component.max_resident_instances,
        static_cast<uint32_t>(std::numeric_limits<int>::max())));
    bool streaming_changed = ImGui::DragFloat(
        "View distance", &view_distance, 1.0f, 1.0f, 100000.0f);
    streaming_changed |= ImGui::DragFloat(
        "Chunk size", &chunk_size, 0.25f, 1.0f, 10000.0f);
    streaming_changed |= ImGui::DragInt(
        "Resident limit", &max_resident, 100, 1, 1000000);
    if (streaming_changed) {
      scenes::SceneDocument before = document_;
      auto edited = component;
      edited.view_distance = std::max(view_distance, 1.0f);
      edited.chunk_size = std::max(chunk_size, 1.0f);
      edited.max_resident_instances =
          static_cast<uint32_t>(std::max(max_resident, 1));
      applyFoliageComponentEdit(std::move(edited), std::move(before),
                                "Edit Foliage Streaming");
      return;
    }

    ImGui::Dummy({0.0f, 3.0f});
    drawInspectorSectionHeading("Authoring Tools");
    const auto mode_button = [&](ToolMode mode, const char* label) {
      const bool selected = tool_ == mode;
      if (selected) {
        ImGui::PushStyleColor(
            ImGuiCol_Button,
            ImGui::GetStyleColorVec4(ImGuiCol_HeaderActive));
      }
      if (ImGui::Button(label) && !selected) changeTool(mode);
      if (selected) ImGui::PopStyleColor();
    };
    ImGui::BeginDisabled(!state->source_valid || !terrain_authoring_valid_);
    mode_button(ToolMode::PaintFoliage, "Paint");
    ImGui::SameLine();
    mode_button(ToolMode::EraseFoliage, "Erase");
    if (tool_ == ToolMode::PaintFoliage ||
        tool_ == ToolMode::EraseFoliage) {
      ImGui::SameLine();
      if (ImGui::Button("Stop")) changeTool(ToolMode::Select);
    }
    ImGui::SeparatorText("Brush");
    ImGui::DragFloat("Paint radius", &foliage_brush_.radius, 0.25f, 0.1f,
                     500.0f);
    ImGui::DragFloat("Density", &foliage_brush_.density, 0.01f, 0.0f,
                     100.0f);
    ImGui::DragFloat("Min spacing", &foliage_brush_.min_spacing, 0.02f,
                     0.0f, 100.0f);
    ImGui::DragFloat3("Min scale", &foliage_brush_.min_scale.x, 0.02f,
                      0.01f, 100.0f);
    ImGui::DragFloat3("Max scale", &foliage_brush_.max_scale.x, 0.02f,
                      0.01f, 100.0f);
    ImGui::DragFloatRange2("Height range", &foliage_min_height_,
                           &foliage_max_height_, 0.25f, -100000.0f,
                           100000.0f);
    ImGui::SliderFloat("Max slope", &foliage_brush_.max_slope_degrees,
                       0.0f, 90.0f);
    ImGui::DragFloat("Erase radius", &foliage_erase_.radius, 0.25f, 0.1f,
                     500.0f);
    ImGui::SliderFloat("Erase strength", &foliage_erase_.strength, 0.0f,
                       1.0f);
    ImGui::EndDisabled();
  }

  bool drawFoliageMaterialSlots(
      const components::FoliageComponent& component) {
    const auto& materials = component.materials;
    uint32_t maximum_slot = 1u;
    for (const auto& material : materials) {
      maximum_slot = std::max(maximum_slot, material.slot);
    }
    maximum_slot = std::min(maximum_slot + 1u, 7u);
    for (uint32_t slot = 0u; slot <= maximum_slot; ++slot) {
      const auto assigned = std::find_if(
          materials.begin(), materials.end(),
          [&](const components::MeshMaterialAssignment& material) {
            return material.slot == slot;
          });
      const std::string key = assigned == materials.end()
                                  ? "<drop material>"
                                  : assigned->material_key;
      ImGui::PushID(static_cast<int>(slot));
      ImGui::Selectable(("Slot " + std::to_string(slot) + ": " + key).c_str(),
                        false);
      if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(
                "KARMA_TERRAIN_MATERIAL")) {
          const std::string asset_key(static_cast<const char*>(payload->Data));
          const AssetEntry* entry = catalog_.findByKey(asset_key);
          if (entry != nullptr && entry->valid &&
              entry->kind == AssetKind::Material) {
            assignFoliageMaterial(*entry, slot);
            ImGui::EndDragDropTarget();
            ImGui::PopID();
            return true;
          }
        }
        ImGui::EndDragDropTarget();
      }
      ImGui::PopID();
    }
    return false;
  }

  bool drawFoliagePrefabSource(
      const components::FoliageComponent& component) {
    const std::filesystem::path prefab_path = component.prefab_path.is_absolute()
                                                  ? component.prefab_path
                                                  : content_root_ /
                                                        component.prefab_path;
    const prefabs::PrefabLoadResult loaded =
        prefabs::loadPrefabDocument(prefab_path);
    if (!loaded.success() || !loaded.document.has_value()) {
      ImGui::TextColored({1.0f, 0.35f, 0.25f, 1.0f},
                         "Prefab source could not be resolved.");
      return false;
    }
    if (ImGui::Button("Edit Source...")) {
      const AssetEntry* source = catalog_.findPrefab(prefab_path);
      if (source == nullptr) {
        last_error_ = "Prefab source is outside the indexed asset roots";
      } else if (prefab_asset_draft_.has_value() &&
                 prefab_asset_draft_->dirty() &&
                 prefab_asset_draft_->sourcePath().lexically_normal() !=
                     source->path.lexically_normal()) {
        last_error_ =
            "Save or Revert the current prefab draft before opening another";
      } else {
        selected_asset_path_ = source->path;
        selected_asset_key_ = source->key;
        selection_.clear();
        inspector_scroll_to_top_ = true;
        last_error_.clear();
      }
      return true;
    }

    ImGui::SeparatorText("Prefab Variables");
    if (loaded.document->variables.empty()) {
      ImGui::TextDisabled("This prefab declares no variables.");
    }
    for (auto declaration = loaded.document->variables.begin();
         declaration != loaded.document->variables.end(); ++declaration) {
      if (!declaration.value().is_object()) continue;
      const std::string type =
          declaration.value().value("type", std::string{});
      const nlohmann::json default_value = declaration.value().value(
          "default", nlohmann::json{});
      nlohmann::json value = component.prefab_variables.contains(
                                 declaration.key())
                                 ? component.prefab_variables[declaration.key()]
                                 : default_value;
      bool changed = false;
      ImGui::PushID(declaration.key().c_str());
      if (type == "bool" && value.is_boolean()) {
        bool scalar = value.get<bool>();
        changed = ImGui::Checkbox(declaration.key().c_str(), &scalar);
        value = scalar;
      } else if (type == "float" && value.is_number()) {
        float scalar = value.get<float>();
        changed = ImGui::DragFloat(declaration.key().c_str(), &scalar, 0.05f);
        value = scalar;
      } else if (type == "int" && value.is_number_integer()) {
        int scalar = value.get<int>();
        changed = ImGui::DragInt(declaration.key().c_str(), &scalar);
        value = scalar;
      } else if (type == "vec3" && value.is_array() && value.size() == 3u) {
        math::Vec3 vector{value[0].get<float>(), value[1].get<float>(),
                          value[2].get<float>()};
        changed =
            ImGui::DragFloat3(declaration.key().c_str(), &vector.x, 0.05f);
        value = nlohmann::json::array({vector.x, vector.y, vector.z});
      } else if (type == "color" && value.is_array() &&
                 value.size() >= 3u) {
        float color[4]{value[0].get<float>(), value[1].get<float>(),
                       value[2].get<float>(),
                       value.size() > 3u ? value[3].get<float>() : 1.0f};
        changed = ImGui::ColorEdit4(declaration.key().c_str(), color);
        value = nlohmann::json::array(
            {color[0], color[1], color[2], color[3]});
      } else if (type == "string" && value.is_string()) {
        std::array<char, 256> text{};
        const std::string current = value.get<std::string>();
        std::copy_n(current.data(),
                    std::min(current.size(), text.size() - 1u),
                    text.data());
        changed = ImGui::InputText(declaration.key().c_str(),
                                   text.data(),
                                   text.size(),
                                   ImGuiInputTextFlags_EnterReturnsTrue);
        value = text.data();
      } else {
        ImGui::TextDisabled("%s (%s)", declaration.key().c_str(),
                            type.c_str());
      }
      ImGui::PopID();
      if (changed) {
        scenes::SceneDocument before = document_;
        components::FoliageComponent edited = component;
        edited.prefab_variables[declaration.key()] = std::move(value);
        std::string prefab_error;
        if (!foliage_prefab_inspector_.validate(
                prefab_path, edited.prefab_variables, &prefab_error)) {
          last_error_ = std::move(prefab_error);
          return true;
        }
        return applyFoliageComponentEdit(std::move(edited),
                                         std::move(before),
                                         "Edit Foliage Prefab Variable");
      }
    }

    ImGui::SeparatorText("Resolved Renderer / LOD Summary");
    const FoliagePrefabInspection inspection =
        foliage_prefab_inspector_.inspect(
            prefab_path, component.prefab_variables);
    if (!inspection.inspected()) {
      ImGui::TextColored({1.0f, 0.35f, 0.25f, 1.0f}, "%s",
                         inspection.diagnostic.c_str());
    }
    for (const FoliagePrefabRendererSummary& renderer :
         inspection.renderers) {
      const std::string mesh_key = renderer.mesh_asset_key.empty()
                                       ? "<none>"
                                       : renderer.mesh_asset_key;
      switch (renderer.disposition) {
        case FoliagePrefabRendererDisposition::PaintedRigidMesh:
          ImGui::BulletText("Painted rigid mesh - %s: %s",
                            renderer.node_name.c_str(), mesh_key.c_str());
          if (renderer.lod_level_count > 0u) {
            ImGui::Indent();
            ImGui::TextDisabled("%zu LOD level(s)",
                                renderer.lod_level_count);
            ImGui::Unindent();
          }
          break;
        case FoliagePrefabRendererDisposition::IgnoredInvisibleMesh:
          ImGui::BulletText("Ignored invisible mesh - %s: %s",
                            renderer.node_name.c_str(), mesh_key.c_str());
          break;
        case FoliagePrefabRendererDisposition::IgnoredDeformableMesh:
          ImGui::BulletText("Ignored deformable renderer - %s: %s",
                            renderer.node_name.c_str(), mesh_key.c_str());
          break;
        case FoliagePrefabRendererDisposition::IgnoredInstancedMesh:
          ImGui::BulletText("Ignored authored instanced renderer - %s: %s",
                            renderer.node_name.c_str(), mesh_key.c_str());
          break;
        case FoliagePrefabRendererDisposition::IgnoredMeshWithoutTransform:
          ImGui::BulletText("Ignored mesh without transform - %s: %s",
                            renderer.node_name.c_str(), mesh_key.c_str());
          break;
      }
    }
    if (inspection.inspected() && inspection.eligible_rigid_meshes == 0u) {
      ImGui::TextColored(
          {1.0f, 0.55f, 0.25f, 1.0f},
          "This prefab cannot be painted: no visible rigid MeshComponent was resolved.");
    } else if (inspection.inspected()) {
      ImGui::TextDisabled("%zu rigid mesh renderer(s) will be painted.",
                          inspection.eligible_rigid_meshes);
    }
    ImGui::TextDisabled(
        "Prefab renderer and LOD components are read-only from the foliage layer. "
        "Authored instancing and deformable renderers are not recursively painted.");
    return false;
  }

  bool applyFoliageLodPayload(std::string_view entity_id,
                              nlohmann::json payload,
                              std::string label) {
    auto entity = findEntity(std::string(entity_id));
    if (entity == document_.entities.end()) return false;
    scenes::SceneDocument before = document_;
    std::string error;
    const bool present = entity->components.contains("LODComponent");
    const bool valid = present
                           ? replaceComponentPayload(*entity,
                                                     component_editors_,
                                                     "LODComponent",
                                                     payload,
                                                     &error)
                           : addComponentWithDependencies(*entity,
                                                          component_editors_,
                                                          "LODComponent",
                                                          payload,
                                                          nullptr,
                                                          &error);
    if (!valid) {
      last_error_ = error.empty() ? "Foliage LOD edit failed"
                                  : std::move(error);
      return false;
    }
    beginDocumentPropertyEdit(std::move(label), std::move(before), true);
    last_error_.clear();
    return true;
  }

  bool commitFoliageLodAssetPayload(std::string_view entity_id,
                                    const nlohmann::json& payload,
                                    const AssetEntry& asset,
                                    std::string label) {
    scenes::SceneDocument next = document_;
    std::string error;
    if (!asset.package_path.empty() &&
        !ensurePackageReferenced(next, asset.package_path, &error)) {
      last_error_ = std::move(error);
      return false;
    }
    const auto entity = std::find_if(
        next.entities.begin(), next.entities.end(),
        [&](const scenes::SceneEntity& value) { return value.id == entity_id; });
    if (entity == next.entities.end() ||
        !replaceComponentPayload(*entity,
                                 component_editors_,
                                 "LODComponent",
                                 payload,
                                 &error)) {
      last_error_ = error.empty() ? "Foliage LOD asset assignment failed"
                                  : std::move(error);
      return false;
    }
    const std::string selected_id(entity_id);
    if (!commitDocumentCommand("Foliage " + label, std::move(next))) {
      return false;
    }
    rebuildPreview();
    focusFoliageLayer(selected_id);
    last_error_.clear();
    return true;
  }

  bool applyFoliageComponentEdit(components::FoliageComponent edited,
                                 scenes::SceneDocument before,
                                 std::string label) {
    FoliageLayerState* state = selectedFoliageLayer();
    if (state == nullptr) {
      last_error_ = "No foliage layer is selected";
      return false;
    }
    const std::string layer_id = state->entity_id;
    std::string validation_error;
    if (!foliage::validateFoliageComponent(edited, &validation_error)) {
      last_error_ = validation_error.empty()
                        ? "Foliage component settings are invalid"
                        : std::move(validation_error);
      return false;
    }
    auto authored_entity = findEntity(layer_id);
    if (authored_entity == document_.entities.end()) {
      last_error_ = "The foliage layer is missing from the scene document";
      return false;
    }
    components::FoliageComponent authored = edited;
    if (const auto relative = contentRelativePath(content_root_, authored.sidecar_path)) {
      authored.sidecar_path = *relative;
    } else {
      last_error_ = "Foliage sidecar must remain inside the content root";
      return false;
    }
    if (!authored.prefab_path.empty()) {
      const std::filesystem::path absolute_prefab =
          authored.prefab_path.is_absolute()
              ? authored.prefab_path
              : content_root_ / authored.prefab_path;
      if (const auto relative =
              contentRelativePath(content_root_, absolute_prefab)) {
        authored.prefab_path = *relative;
      } else {
        last_error_ = "Foliage prefab must remain inside the content root";
        return false;
      }
    }
    const nlohmann::json serialized =
        serializeTemporaryComponent(authored, "FoliageComponent");
    if (serialized.empty()) {
      last_error_ = "Foliage component validation failed";
      return false;
    }
    authored_entity->components["FoliageComponent"] = serialized;
    const world::Entity runtime = preview_.find(layer_id);
    if (world->isAlive(runtime)) world->add(runtime, std::move(edited));
    beginDocumentPropertyEdit(std::move(label), std::move(before), true);
    last_error_.clear();
    return true;
  }

  bool drawJsonBool(nlohmann::json& payload,
                    const char* key,
                    const char* label,
                    bool fallback = false) {
    bool value = payload.value(key, fallback);
    if (!ImGui::Checkbox(label, &value)) return false;
    payload[key] = value;
    return true;
  }

  bool drawJsonFloat(nlohmann::json& payload,
                     const char* key,
                     const char* label,
                     float fallback,
                     float speed,
                     float minimum,
                     float maximum) {
    float value = payload.value(key, fallback);
    if (!ImGui::DragFloat(label, &value, speed, minimum, maximum)) return false;
    payload[key] = std::clamp(value, minimum, maximum);
    return true;
  }

  bool drawJsonUint(nlohmann::json& payload,
                    const char* key,
                    const char* label,
                    uint32_t fallback,
                    uint32_t minimum = 0u,
                    uint32_t maximum = std::numeric_limits<uint32_t>::max()) {
    uint32_t value = payload.value(key, fallback);
    constexpr uint32_t step = 1u;
    constexpr uint32_t fast_step = 16u;
    if (!ImGui::InputScalar(label, ImGuiDataType_U32, &value,
                            &step, &fast_step, "%u")) {
      return false;
    }
    payload[key] = std::clamp(value, minimum, maximum);
    return true;
  }

  bool drawJsonVec3(nlohmann::json& payload,
                    const char* key,
                    const char* label,
                    const math::Vec3& fallback = {},
                    float speed = 0.05f) {
    math::Vec3 value = fallback;
    const auto field = payload.find(key);
    if (field != payload.end() && field->is_array() && field->size() == 3u &&
        (*field)[0].is_number() && (*field)[1].is_number() &&
        (*field)[2].is_number()) {
      value = {(*field)[0].get<float>(), (*field)[1].get<float>(),
               (*field)[2].get<float>()};
    }
    if (!ImGui::DragFloat3(label, &value.x, speed)) return false;
    payload[key] = nlohmann::json::array({value.x, value.y, value.z});
    return true;
  }

  bool applyTypedComponentPayload(std::string_view type_name,
                                  nlohmann::json payload,
                                  std::string label) {
    auto entity = findEntity(selection_.id);
    if (entity == document_.entities.end()) return false;
    scenes::SceneDocument before = document_;
    std::string error;
    if (!replaceComponentPayload(*entity, component_editors_, type_name,
                                 payload, &error)) {
      last_error_ = std::move(error);
      return false;
    }
    beginDocumentPropertyEdit(std::move(label), std::move(before), true);
    last_error_.clear();
    return true;
  }

  bool commitComponentAssetPayload(std::string_view type_name,
                                   nlohmann::json payload,
                                   const AssetEntry& asset,
                                   std::string label) {
    scenes::SceneDocument next = document_;
    std::string error;
    if (!asset.package_path.empty() &&
        !ensurePackageReferenced(next, asset.package_path, &error)) {
      last_error_ = std::move(error);
      return false;
    }
    const auto entity = std::find_if(
        next.entities.begin(), next.entities.end(),
        [&](const scenes::SceneEntity& value) {
          return value.id == selection_.id;
        });
    if (entity == next.entities.end() ||
        !replaceComponentPayload(*entity, component_editors_, type_name,
                                 payload, &error)) {
      last_error_ = error.empty() ? "Component asset assignment failed"
                                  : std::move(error);
      return false;
    }
    const std::string selected_id = selection_.id;
    if (!commitDocumentCommand(std::move(label), std::move(next))) return false;
    rebuildPreview();
    selection_ = {SelectionKind::Entity, selected_id};
    last_error_.clear();
    return true;
  }

  bool drawMeshComponentEditor(nlohmann::json& payload,
                               bool& document_committed) {
    bool changed = false;
    const std::string mesh_key = payload.value("mesh_asset_key", std::string{});
    ImGui::Selectable(("Mesh: " +
                       (mesh_key.empty() ? std::string("<drop mesh>") : mesh_key))
                          .c_str(),
                      false);
    if (ImGui::BeginDragDropTarget()) {
      if (const ImGuiPayload* drop =
              ImGui::AcceptDragDropPayload("KARMA_MESH_ASSET")) {
        const AssetEntry* entry = catalog_.findByKey(
            static_cast<const char*>(drop->Data));
        if (entry != nullptr && entry->valid && entry->kind == AssetKind::Mesh) {
          payload["mesh_asset_key"] = entry->key;
          document_committed = commitComponentAssetPayload(
              "MeshComponent", payload, *entry, "Assign Mesh Asset");
          ImGui::EndDragDropTarget();
          return false;
        }
      }
      ImGui::EndDragDropTarget();
    }
    changed |= drawJsonBool(payload, "visible", "Visible", true);
    changed |= drawJsonBool(payload, "shadow_visible", "Cast shadows", true);

    auto& materials = payload["materials"];
    if (!materials.is_array()) materials = nlohmann::json::array();
    for (uint32_t slot = 0u; slot < 4u; ++slot) {
      auto assigned = std::find_if(
          materials.begin(), materials.end(),
          [&](const nlohmann::json& value) {
            return value.is_object() && value.value("slot", UINT32_MAX) == slot;
          });
      const std::string key = assigned == materials.end()
                                  ? "<drop material>"
                                  : assigned->value("material_key", std::string{});
      ImGui::PushID(static_cast<int>(slot));
      ImGui::Selectable(("Material slot " + std::to_string(slot) + ": " + key)
                            .c_str(),
                        false);
      if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* drop = ImGui::AcceptDragDropPayload(
                "KARMA_TERRAIN_MATERIAL")) {
          const AssetEntry* entry = catalog_.findByKey(
              static_cast<const char*>(drop->Data));
          if (entry != nullptr && entry->valid &&
              entry->kind == AssetKind::Material) {
            if (assigned == materials.end()) {
              materials.push_back({{"slot", slot},
                                   {"material_key", entry->key}});
            } else {
              (*assigned)["material_key"] = entry->key;
            }
            document_committed = commitComponentAssetPayload(
                "MeshComponent", payload, *entry, "Assign Mesh Material");
            ImGui::EndDragDropTarget();
            ImGui::PopID();
            return false;
          }
        }
        ImGui::EndDragDropTarget();
      }
      ImGui::PopID();
    }
    return changed;
  }

  using LodAssetCommit = std::function<bool(
      const nlohmann::json&, const AssetEntry&, std::string)>;

  bool drawLodLevelsEditor(nlohmann::json& payload,
                           std::string_view base_mesh_asset_key,
                           const nlohmann::json& base_materials,
                           const LodAssetCommit& commit_asset,
                           bool& document_committed) {
    auto& levels = payload["levels"];
    if (!levels.is_array()) levels = nlohmann::json::array();
    bool changed = false;
    for (size_t index = 0u; index < levels.size(); ++index) {
      nlohmann::json& level = levels[index];
      if (!level.is_object()) level = nlohmann::json::object();
      ImGui::PushID(static_cast<int>(index));
      ImGui::SeparatorText(("LOD " + std::to_string(index + 1u)).c_str());

      const float previous = index == 0u
                                 ? 0.0f
                                 : levels[index - 1u].value(
                                       "start_distance", 0.0f);
      const float next = index + 1u < levels.size()
                             ? levels[index + 1u].value(
                                   "start_distance", 1000000.0f)
                             : 1000000.0f;
      const float minimum = previous + 0.001f;
      const float maximum = std::max(minimum, next - 0.001f);
      float distance = level.value("start_distance", minimum);
      if (ImGui::DragFloat("Start distance", &distance, 0.25f, minimum,
                           maximum, "%.2f")) {
        level["start_distance"] = std::clamp(distance, minimum, maximum);
        changed = true;
      }

      const std::string mesh_key =
          level.value("mesh_asset_key", std::string{});
      ImGui::Selectable(("Mesh: " +
                         (mesh_key.empty() ? std::string("<drop mesh>")
                                           : mesh_key))
                            .c_str(),
                        false);
      if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* drop =
                ImGui::AcceptDragDropPayload("KARMA_MESH_ASSET")) {
          const AssetEntry* entry = catalog_.findByKey(
              static_cast<const char*>(drop->Data));
          if (entry != nullptr && entry->valid &&
              entry->kind == AssetKind::Mesh) {
            level["mesh_asset_key"] = entry->key;
            document_committed = commit_asset(
                payload, *entry, "Assign LOD Mesh");
            ImGui::EndDragDropTarget();
            ImGui::PopID();
            return false;
          }
        }
        ImGui::EndDragDropTarget();
      }

      auto& materials = level["materials"];
      if (!materials.is_array()) materials = nlohmann::json::array();
      for (uint32_t slot = 0u; slot < 4u; ++slot) {
        auto assignment = std::find_if(
            materials.begin(), materials.end(),
            [&](const nlohmann::json& candidate) {
              return candidate.is_object() &&
                     candidate.value("slot", UINT32_MAX) == slot;
            });
        const std::string material_key =
            assignment == materials.end()
                ? std::string("<drop material>")
                : assignment->value("material_key", std::string{});
        ImGui::PushID(static_cast<int>(slot));
        ImGui::Selectable(
            ("Material " + std::to_string(slot) + ": " + material_key)
                .c_str(),
            false);
        if (ImGui::BeginDragDropTarget()) {
          if (const ImGuiPayload* drop = ImGui::AcceptDragDropPayload(
                  "KARMA_TERRAIN_MATERIAL")) {
            const AssetEntry* entry = catalog_.findByKey(
                static_cast<const char*>(drop->Data));
            if (entry != nullptr && entry->valid &&
                entry->kind == AssetKind::Material) {
              if (assignment == materials.end()) {
                materials.push_back(
                    {{"slot", slot}, {"material_key", entry->key}});
              } else {
                (*assignment)["material_key"] = entry->key;
              }
              document_committed = commit_asset(
                  payload, *entry, "Assign LOD Material");
              ImGui::EndDragDropTarget();
              ImGui::PopID();
              ImGui::PopID();
              return false;
            }
          }
          ImGui::EndDragDropTarget();
        }
        ImGui::PopID();
      }

      const std::string render_mode =
          level.value("render_mode", std::string("mesh"));
      int render_mode_index =
          render_mode == "upright_billboard" ? 1 : 0;
      if (ImGui::Combo("Render mode", &render_mode_index,
                       "Mesh\0Upright billboard\0")) {
        level["render_mode"] = render_mode_index == 1
                                   ? "upright_billboard"
                                   : "mesh";
        changed = true;
      }
      changed |= drawJsonBool(level, "shadow_visible", "Cast shadows", false);
      if (ImGui::Button("Remove LOD")) {
        levels.erase(levels.begin() + static_cast<std::ptrdiff_t>(index));
        changed = true;
        ImGui::PopID();
        return changed;
      }
      ImGui::PopID();
    }

    const bool can_add = levels.size() < kMaxEditorLodLevels &&
                         !base_mesh_asset_key.empty();
    ImGui::BeginDisabled(!can_add);
    if (ImGui::Button("+ Add LOD")) {
      const float distance = levels.empty()
                                 ? 35.0f
                                 : levels.back().value("start_distance", 0.0f) +
                                       35.0f;
      levels.push_back({
          {"start_distance", distance},
          {"mesh_asset_key", std::string(base_mesh_asset_key)},
          {"materials",
           base_materials.is_array() ? base_materials
                                     : nlohmann::json::array()},
          {"render_mode", "mesh"},
          {"shadow_visible", false},
      });
      changed = true;
    }
    ImGui::EndDisabled();
    if (base_mesh_asset_key.empty()) {
      ImGui::TextDisabled("Assign a compatible base mesh before adding LODs.");
    }
    return changed;
  }

  bool drawInstanceSetComponentEditor(nlohmann::json& payload) {
    auto& instances = payload["instances"];
    auto& planar_instances = payload["planar_instances"];
    if (!instances.is_array()) instances = nlohmann::json::array();
    if (!planar_instances.is_array()) {
      planar_instances = nlohmann::json::array();
    }
    std::string layout =
        payload.value("gpu_layout", std::string("matrix4x4_params"));
    int layout_index = layout == "position_yaw_scale_params" ? 1 : 0;
    bool changed = false;
    ImGui::BeginDisabled(!instances.empty() || !planar_instances.empty());
    if (ImGui::Combo("GPU layout", &layout_index,
                     "Matrix + params\0Position/yaw/scale + params\0")) {
      payload["gpu_layout"] = layout_index == 1
                                  ? "position_yaw_scale_params"
                                  : "matrix4x4_params";
      changed = true;
    }
    ImGui::EndDisabled();
    changed |= drawJsonBool(payload, "dynamic", "Dynamic upload", false);
    ImGui::Text("Matrix instances: %zu", instances.size());
    ImGui::Text("Planar instances: %zu", planar_instances.size());
    ImGui::TextDisabled("Instance arrays remain available in Edit JSON.");
    return changed;
  }

  static nlohmann::json defaultColliderShape(std::string_view type) {
    if (type == "sphere") {
      return {{"center", nlohmann::json::array({0.0f, 0.0f, 0.0f})},
              {"radius", 0.5f}};
    }
    if (type == "capsule") {
      return {{"center", nlohmann::json::array({0.0f, 0.0f, 0.0f})},
              {"radius", 0.5f}, {"height", 2.0f}};
    }
    if (type == "cylinder") {
      return {{"center", nlohmann::json::array({0.0f, 0.0f, 0.0f})},
              {"radius", 0.5f}, {"height", 1.0f}, {"convex_radius", 0.0f}};
    }
    if (type == "tapered_capsule") {
      return {{"center", nlohmann::json::array({0.0f, 0.0f, 0.0f})},
              {"top_radius", 0.4f}, {"bottom_radius", 0.5f},
              {"height", 2.0f}};
    }
    if (type == "mesh") {
      return {{"mesh_asset_key", ""}, {"vertices", nlohmann::json::array()},
              {"indices", nlohmann::json::array()}};
    }
    if (type == "convex_hull") {
      return {{"center", nlohmann::json::array({0.0f, 0.0f, 0.0f})},
              {"points", nlohmann::json::array()}, {"convex_radius", 0.0f}};
    }
    if (type == "triangle") {
      return {{"points", nlohmann::json::array({
                            nlohmann::json::array({0.0f, 0.0f, 0.0f}),
                            nlohmann::json::array({1.0f, 0.0f, 0.0f}),
                            nlohmann::json::array({0.0f, 1.0f, 0.0f})})},
              {"convex_radius", 0.0f}};
    }
    if (type == "height_field") {
      return {{"samples", nlohmann::json::array({0.0f})},
              {"sample_count", 1u},
              {"offset", nlohmann::json::array({0.0f, 0.0f, 0.0f})},
              {"scale", nlohmann::json::array({1.0f, 1.0f, 1.0f})},
              {"block_size", 2u}, {"bits_per_sample", 8u}};
    }
    return {{"center", nlohmann::json::array({0.0f, 0.0f, 0.0f})},
            {"half_extents", nlohmann::json::array({0.5f, 0.5f, 0.5f})}};
  }

  bool drawColliderComponentEditor(nlohmann::json& payload,
                                   bool& document_committed) {
    bool changed = false;
    static constexpr const char* types[] = {
        "box", "sphere", "capsule", "cylinder", "tapered_capsule",
        "mesh", "convex_hull", "triangle", "height_field"};
    std::string current = payload.value("type", std::string("box"));
    int type_index = 0;
    for (int index = 0; index < 9; ++index) {
      if (current == types[index]) type_index = index;
    }
    if (ImGui::Combo("Shape", &type_index,
                     "Box\0Sphere\0Capsule\0Cylinder\0Tapered capsule\0Mesh bounds\0Convex hull (JSON)\0Triangle (JSON)\0Height field (JSON)\0")) {
      current = types[std::clamp(type_index, 0, 8)];
      payload["type"] = current;
      payload["shape"] = defaultColliderShape(current);
      changed = true;
    }
    changed |= drawJsonBool(payload, "is_trigger", "Trigger", false);
    changed |= drawJsonBool(payload, "debug_draw", "Runtime debug draw", false);
    auto& shape = payload["shape"];
    if (!shape.is_object()) shape = defaultColliderShape(current);
    if (current != "mesh" && current != "triangle" &&
        current != "height_field") {
      changed |= drawJsonVec3(shape, "center", "Center");
    }
    if (current == "box") {
      changed |= drawJsonVec3(shape, "half_extents", "Half extents",
                              {0.5f, 0.5f, 0.5f});
    } else if (current == "sphere") {
      changed |= drawJsonFloat(shape, "radius", "Radius", 0.5f, 0.02f,
                               0.001f, 100000.0f);
    } else if (current == "capsule" || current == "cylinder") {
      changed |= drawJsonFloat(shape, "radius", "Radius", 0.5f, 0.02f,
                               0.001f, 100000.0f);
      changed |= drawJsonFloat(shape, "height", "Height", 1.0f, 0.02f,
                               0.001f, 100000.0f);
      if (current == "cylinder") {
        changed |= drawJsonFloat(shape, "convex_radius", "Convex radius",
                                 0.0f, 0.01f, 0.0f, 100000.0f);
      }
    } else if (current == "tapered_capsule") {
      changed |= drawJsonFloat(shape, "top_radius", "Top radius", 0.4f,
                               0.02f, 0.001f, 100000.0f);
      changed |= drawJsonFloat(shape, "bottom_radius", "Bottom radius", 0.5f,
                               0.02f, 0.001f, 100000.0f);
      changed |= drawJsonFloat(shape, "height", "Height", 2.0f, 0.02f,
                               0.001f, 100000.0f);
    } else if (current == "mesh") {
      const std::string key = shape.value("mesh_asset_key", std::string{});
      ImGui::Selectable(("Mesh: " +
                         (key.empty() ? std::string("<drop mesh>") : key))
                            .c_str(),
                        false);
      if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* drop =
                ImGui::AcceptDragDropPayload("KARMA_MESH_ASSET")) {
          const AssetEntry* entry = catalog_.findByKey(
              static_cast<const char*>(drop->Data));
          if (entry != nullptr && entry->valid && entry->kind == AssetKind::Mesh) {
            shape["mesh_asset_key"] = entry->key;
            document_committed = commitComponentAssetPayload(
                "ColliderComponent", payload, *entry, "Assign Collider Mesh");
            ImGui::EndDragDropTarget();
            return false;
          }
        }
        ImGui::EndDragDropTarget();
      }
      ImGui::TextDisabled("Vertex/index arrays are available in Edit JSON.");
    } else {
      ImGui::TextDisabled("Point/sample arrays are available in Edit JSON.");
    }
    return changed;
  }

  bool drawRigidbodyComponentEditor(nlohmann::json& payload) {
    bool changed = false;
    std::string motion = payload.value("motion_type", std::string("dynamic"));
    int motion_index = motion == "kinematic" ? 1 : (motion == "static" ? 2 : 0);
    if (ImGui::Combo("Motion type", &motion_index,
                     "Dynamic\0Kinematic\0Static\0")) {
      payload["motion_type"] =
          motion_index == 1 ? "kinematic" : (motion_index == 2 ? "static" : "dynamic");
      payload["is_kinematic"] = motion_index == 1;
      changed = true;
    }
    std::string quality = payload.value("motion_quality", std::string("discrete"));
    int quality_index = quality == "linear_cast" ? 1 : 0;
    if (ImGui::Combo("Motion quality", &quality_index,
                     "Discrete\0Linear cast (CCD)\0")) {
      payload["motion_quality"] = quality_index == 1 ? "linear_cast" : "discrete";
      changed = true;
    }
    changed |= drawJsonFloat(payload, "mass", "Mass", 1.0f, 0.05f,
                             0.0f, 1000000.0f);
    changed |= drawJsonVec3(payload, "velocity", "Velocity");
    changed |= drawJsonVec3(payload, "angular_velocity", "Angular velocity");
    changed |= drawJsonBool(payload, "use_gravity", "Use gravity", true);
    changed |= drawJsonBool(payload, "is_trigger", "Body is trigger", false);
    changed |= drawJsonFloat(payload, "gravity_factor", "Gravity factor", 1.0f,
                             0.02f, 0.0f, 1000.0f);
    changed |= drawJsonFloat(payload, "linear_damping", "Linear damping", 0.05f,
                             0.01f, 0.0f, 1000.0f);
    changed |= drawJsonFloat(payload, "angular_damping", "Angular damping", 0.05f,
                             0.01f, 0.0f, 1000.0f);
    changed |= drawJsonFloat(payload, "max_linear_velocity", "Max linear velocity",
                             500.0f, 1.0f, 0.001f, 1000000.0f);
    changed |= drawJsonFloat(payload, "max_angular_velocity", "Max angular velocity",
                             47.12389f, 0.1f, 0.001f, 1000000.0f);
    changed |= drawJsonFloat(payload, "inertia_multiplier", "Inertia multiplier",
                             1.0f, 0.02f, 0.001f, 1000000.0f);
    changed |= drawJsonUint(payload, "velocity_solver_steps", "Velocity solver steps", 0u,
                            0u, 255u);
    changed |= drawJsonUint(payload, "position_solver_steps", "Position solver steps", 0u,
                            0u, 255u);
    changed |= drawJsonBool(payload, "allow_sleeping", "Allow sleeping", true);
    if (ImGui::TreeNode("Degrees of freedom")) {
      uint32_t dofs = payload.value("allowed_dofs", 63u);
      static constexpr const char* names[] = {
          "Translate X", "Translate Y", "Translate Z",
          "Rotate X", "Rotate Y", "Rotate Z"};
      for (uint32_t bit = 0u; bit < 6u; ++bit) {
        bool enabled = (dofs & (1u << bit)) != 0u;
        if (ImGui::Checkbox(names[bit], &enabled)) {
          if (enabled) dofs |= 1u << bit;
          else dofs &= ~(1u << bit);
          payload["allowed_dofs"] = dofs;
          changed = true;
        }
      }
      ImGui::TreePop();
    }
    if (ImGui::TreeNode("Advanced body flags")) {
      changed |= drawJsonBool(payload, "allow_dynamic_or_kinematic",
                              "Allow dynamic/kinematic switching", false);
      changed |= drawJsonBool(payload, "collide_kinematic_vs_non_dynamic",
                              "Kinematic vs non-dynamic collisions", false);
      changed |= drawJsonBool(payload, "use_manifold_reduction",
                              "Use manifold reduction", true);
      changed |= drawJsonBool(payload, "apply_gyroscopic_force",
                              "Apply gyroscopic force", false);
      changed |= drawJsonBool(payload, "enhanced_internal_edge_removal",
                              "Enhanced internal-edge removal", false);
      ImGui::TreePop();
    }
    return changed;
  }

  bool drawTypedComponentEditor(const ComponentEditorDescriptor& descriptor,
                                nlohmann::json& payload,
                                bool& document_committed) {
    if (descriptor.type_name == "StaticComponent") {
      bool changed = false;
      changed |= drawJsonBool(payload, "enabled", "Static", true);
      changed |= drawJsonBool(payload, "include_descendants",
                              "Include descendants", true);
      uint32_t flags = payload.value("flags", 0x1fu);
      static constexpr std::array<const char*, 5> names{
          "Rendering", "Lighting", "Shadows", "Collision", "Navigation"};
      for (uint32_t bit = 0u; bit < names.size(); ++bit) {
        bool enabled = (flags & (1u << bit)) != 0u;
        if (ImGui::Checkbox(names[bit], &enabled)) {
          if (enabled) {
            flags |= 1u << bit;
          } else {
            flags &= ~(1u << bit);
          }
          payload["flags"] = flags;
          changed = true;
        }
      }
      ImGui::TextDisabled(
          "Changing static content marks only the selected bake domains stale.");
      return changed;
    }
    switch (descriptor.editor) {
      case ComponentEditorKind::Mesh:
        return drawMeshComponentEditor(payload, document_committed);
      case ComponentEditorKind::Lod: {
        std::string base_mesh;
        nlohmann::json base_materials = nlohmann::json::array();
        const auto entity = findEntity(selection_.id);
        if (entity != document_.entities.end()) {
          for (const char* source_type : {"MeshComponent",
                                          "InstancedMeshComponent",
                                          "FoliageComponent"}) {
            const auto source = entity->components.find(source_type);
            if (source == entity->components.end() || !source->is_object()) {
              continue;
            }
            base_mesh = source->value("mesh_asset_key", std::string{});
            base_materials =
                source->value("materials", nlohmann::json::array());
            if (!base_mesh.empty()) break;
          }
        }
        return drawLodLevelsEditor(
            payload,
            base_mesh,
            base_materials,
            [&](const nlohmann::json& edited,
                const AssetEntry& asset,
                std::string label) {
              return commitComponentAssetPayload(
                  "LODComponent", edited, asset, std::move(label));
            },
            document_committed);
      }
      case ComponentEditorKind::InstanceSet:
        return drawInstanceSetComponentEditor(payload);
      case ComponentEditorKind::Collider:
        return drawColliderComponentEditor(payload, document_committed);
      case ComponentEditorKind::Rigidbody:
        return drawRigidbodyComponentEditor(payload);
      case ComponentEditorKind::Visibility: {
        bool changed = drawJsonBool(payload, "visible", "Visible", true);
        changed |= drawJsonUint(payload, "render_layer_mask", "Render mask",
                                0xFFFFFFFFu);
        changed |= drawJsonUint(payload, "collision_layer_mask", "Collision mask",
                                0xFFFFFFFFu);
        return changed;
      }
      case ComponentEditorKind::RenderTags: {
        std::string joined;
        const auto tags = payload.find("tags");
        if (tags != payload.end() && tags->is_array()) {
          for (const auto& tag : *tags) {
            if (!tag.is_string()) continue;
            if (!joined.empty()) joined += ", ";
            joined += tag.get<std::string>();
          }
        }
        std::array<char, 512> buffer{};
        std::copy_n(joined.data(), std::min(joined.size(), buffer.size() - 1u),
                    buffer.data());
        if (!ImGui::InputText("Tags (comma separated)", buffer.data(),
                              buffer.size())) {
          return false;
        }
        nlohmann::json values = nlohmann::json::array();
        std::stringstream stream(buffer.data());
        std::string value;
        while (std::getline(stream, value, ',')) {
          const auto first = value.find_first_not_of(" \t");
          const auto last = value.find_last_not_of(" \t");
          if (first != std::string::npos) {
            values.push_back(value.substr(first, last - first + 1u));
          }
        }
        payload["tags"] = std::move(values);
        return true;
      }
      case ComponentEditorKind::PhysicsMaterial: {
        bool changed = drawJsonFloat(payload, "friction", "Friction", 0.2f,
                                     0.01f, 0.0f, 1000.0f);
        changed |= drawJsonFloat(payload, "restitution", "Restitution", 0.0f,
                                 0.01f, 0.0f, 1.0f);
        return changed;
      }
      case ComponentEditorKind::PhysicsCollisionFilter: {
        bool changed = drawJsonUint(payload, "layers", "Collision layers", 1u);
        changed |= drawJsonUint(payload, "collides_with", "Collides with",
                                0xFFFFFFFFu);
        return changed;
      }
      case ComponentEditorKind::CharacterController: {
        bool changed = drawJsonBool(payload, "enabled", "Enabled", true);
        changed |= drawJsonVec3(payload, "desired_velocity", "Desired velocity");
        changed |= drawJsonVec3(payload, "desired_angular_velocity",
                                "Desired angular velocity");
        ImGui::TextDisabled("Impulse/add velocity is available in Edit JSON.");
        return changed;
      }
      case ComponentEditorKind::Light: {
        std::string type = payload.value("type", std::string("point"));
        int type_index = type == "directional" ? 0 : (type == "spot" ? 2 : 1);
        bool changed = false;
        if (ImGui::Combo("Type", &type_index, "Directional\0Point\0Spot\0")) {
          payload["type"] = type_index == 0 ? "directional"
                                               : (type_index == 2 ? "spot" : "point");
          changed = true;
        }
        std::string bake_mode =
            payload.value("bake_mode", std::string("realtime"));
        int bake_mode_index =
            bake_mode == "mixed" ? 1 : (bake_mode == "baked" ? 2 : 0);
        if (ImGui::Combo("Mode", &bake_mode_index,
                         "Realtime\0Mixed\0Baked\0")) {
          payload["bake_mode"] = bake_mode_index == 1
                                     ? "mixed"
                                     : (bake_mode_index == 2 ? "baked"
                                                              : "realtime");
          changed = true;
        }
        float color[4]{1.0f, 1.0f, 1.0f, 1.0f};
        const auto field = payload.find("color");
        if (field != payload.end() && field->is_array() && field->size() == 4u) {
          for (size_t i = 0u; i < 4u; ++i) color[i] = (*field)[i].get<float>();
        }
        if (ImGui::ColorEdit4("Color", color)) {
          payload["color"] = nlohmann::json::array(
              {color[0], color[1], color[2], color[3]});
          changed = true;
        }
        changed |= drawJsonFloat(payload, "intensity", "Intensity", 1.0f,
                                 0.05f, 0.0f, 1000000.0f);
        changed |= drawJsonFloat(payload, "range", "Range", 10.0f, 0.1f,
                                 0.0f, 1000000.0f);
        float inner = payload.value("inner_cone_degrees", 15.0f);
        float outer = payload.value("outer_cone_degrees", 30.0f);
        if (ImGui::DragFloat("Inner cone", &inner, 0.25f, 0.0f, outer)) {
          payload["inner_cone_degrees"] = std::clamp(inner, 0.0f, outer);
          changed = true;
        }
        if (ImGui::DragFloat("Outer cone", &outer, 0.25f, inner, 179.0f)) {
          payload["outer_cone_degrees"] = std::clamp(outer, inner, 179.0f);
          changed = true;
        }
        changed |= drawJsonBool(payload, "casts_shadows", "Cast shadows", false);
        changed |= drawJsonFloat(payload, "shadow_extent", "Directional shadow extent",
                                 0.0f, 0.5f, 0.0f, 1000000.0f);
        return changed;
      }
      case ComponentEditorKind::Terrain:
        drawTerrainInspector();
        return false;
      case ComponentEditorKind::Foliage:
        drawFoliageInspector();
        return false;
      case ComponentEditorKind::Transform:
        ImGui::TextDisabled("Transform is owned by the Scene Transform card.");
        return false;
      case ComponentEditorKind::InstancedMesh:
      case ComponentEditorKind::AdvancedJson:
        ImGui::TextDisabled("This component uses validated JSON editing.");
        return false;
    }
    return false;
  }

  void openComponentJsonEditor(std::string type_name,
                               const nlohmann::json& payload,
                               bool adding) {
    const std::string formatted = payload.dump(2);
    const size_t capacity = std::min<size_t>(
        std::max<size_t>(formatted.size() + 65536u, 262144u),
        16u * 1024u * 1024u);
    component_json_buffer_.assign(capacity, '\0');
    std::copy_n(formatted.data(),
                std::min(formatted.size(), capacity - 1u),
                component_json_buffer_.data());
    component_json_type_ = std::move(type_name);
    component_json_entity_id_ = selection_.id;
    component_json_adding_ = adding;
    component_json_error_.clear();
    open_component_json_ = true;
  }

  bool removeAuthoredComponent(const std::string& type_name) {
    scenes::SceneDocument next = document_;
    const auto entity = std::find_if(
        next.entities.begin(), next.entities.end(),
        [&](const scenes::SceneEntity& value) {
          return value.id == selection_.id;
        });
    if (entity == next.entities.end()) return false;
    std::string error;
    if (!removeComponentsTogether(*entity, component_editors_, {type_name},
                                  &error)) {
      const auto blockers = componentRemovalBlockers(
          *entity, component_editors_, type_name);
      if (!blockers.empty()) {
        error += ". Remove dependent components first: ";
        for (size_t index = 0u; index < blockers.size(); ++index) {
          if (index != 0u) error += ", ";
          error += blockers[index];
        }
      }
      last_error_ = std::move(error);
      return false;
    }
    const std::string selected_id = selection_.id;
    if (!commitDocumentCommand("Remove " + type_name, std::move(next))) return false;
    rebuildPreview();
    selection_ = {SelectionKind::Entity, selected_id};
    return true;
  }

  void drawAuthoredComponentCards() {
    auto entity = findEntity(selection_.id);
    if (entity == document_.entities.end() || !entity->components.is_object()) return;
    std::vector<std::string> type_names;
    type_names.reserve(entity->components.size());
    for (auto component = entity->components.begin();
         component != entity->components.end(); ++component) {
      type_names.push_back(component.key());
    }
    std::sort(type_names.begin(), type_names.end());
    for (const std::string& type_name : type_names) {
      entity = findEntity(selection_.id);
      if (entity == document_.entities.end() ||
          !entity->components.contains(type_name)) return;
      if (type_name == "TransformComponent" ||
          (type_name == "TerrainComponent" &&
           selection_.id == terrain_entity_id_) ||
          (type_name == "FoliageComponent" && !terrain_entity_id_.empty())) {
        continue;
      }
      const ComponentEditorDescriptor* descriptor =
          component_editors_.find(type_name);
      const std::string display = descriptor == nullptr
                                      ? type_name
                                      : descriptor->display_name;
      if (!inspectorFilterMatches(display, type_name)) continue;
      bool stop = false;
      drawComponentCard(type_name, display, false, [&] {
        ImGui::TextDisabled("%s", type_name.c_str());
        ImGui::SameLine(ImGui::GetContentRegionMax().x - 28.0f);
        if (ImGui::Button("...", {28.0f, 0.0f})) {
          ImGui::OpenPopup("##component_actions");
        }
        nlohmann::json payload = entity->components[type_name];
        if (ImGui::BeginPopup("##component_actions")) {
          if (ImGui::MenuItem("Edit JSON...")) {
            openComponentJsonEditor(type_name, payload, false);
          }
          if (descriptor != nullptr && descriptor->removable &&
              ImGui::MenuItem("Remove Component")) {
            stop = removeAuthoredComponent(type_name);
          }
          ImGui::EndPopup();
        }
        if (stop) return;
        bool committed = false;
        bool changed = false;
        if (descriptor != nullptr) {
          changed = drawTypedComponentEditor(*descriptor, payload, committed);
        } else {
          ImGui::TextDisabled("No editor descriptor is registered.");
        }
        if (committed) {
          stop = true;
          return;
        }
        if (changed) {
          applyTypedComponentPayload(type_name, std::move(payload),
                                     "Edit " + display);
          stop = true;
          return;
        }
      });
      if (stop) return;
    }
  }

  void drawAddComponentMenu() {
    ImGui::Dummy({0.0f, 2.0f});
    if (ImGui::Button("Add Component",
                      {ImGui::GetContentRegionAvail().x, 0.0f})) {
      ImGui::OpenPopup("##add_component");
    }
    if (!ImGui::BeginPopup("##add_component")) return;
    if (ImGui::InputTextWithHint("##component_search", "Search components",
                                 component_filter_,
                                 sizeof(component_filter_))) {
      settings_.inspector_filter = component_filter_;
    }
    const auto lowercase = [](std::string value) {
      std::transform(value.begin(), value.end(), value.begin(),
                     [](unsigned char c) {
                       return static_cast<char>(std::tolower(c));
                     });
      return value;
    };
    const std::string filter = lowercase(component_filter_);
    auto current = findEntity(selection_.id);
    const bool has_special_light = std::any_of(
        document_.lights.begin(), document_.lights.end(),
        [&](const scenes::SceneLight& light) {
          return light.entity_id == selection_.id;
        });
    const bool has_special_camera = std::any_of(
        document_.cameras.begin(), document_.cameras.end(),
        [&](const scenes::SceneCamera& camera) {
          return camera.entity_id == selection_.id;
        });
    const bool has_special_static = std::any_of(
        document_.static_components.begin(),
        document_.static_components.end(),
        [&](const scenes::SceneStaticComponent& component) {
          return component.entity_id == selection_.id;
        });
    const bool has_special_environment =
        document_.environment.has_value() &&
        document_.environment->entity_id == selection_.id;
    std::vector<const ComponentEditorDescriptor*> candidates;
    candidates.reserve(component_editors_.descriptors().size());
    for (const ComponentEditorDescriptor& descriptor :
         component_editors_.descriptors()) {
      if (descriptor.type_name == "TransformComponent" ||
          (descriptor.type_name == "LightComponent" && has_special_light) ||
          (descriptor.type_name == "CameraComponent" && has_special_camera) ||
          (descriptor.type_name == "StaticComponent" && has_special_static) ||
          (descriptor.type_name == "EnvironmentComponent" &&
           has_special_environment) ||
          (current != document_.entities.end() &&
           current->components.contains(descriptor.type_name))) {
        continue;
      }
      if (!filter.empty() &&
          lowercase(descriptor.display_name + " " + descriptor.type_name)
                  .find(filter) == std::string::npos) {
        continue;
      }
      candidates.push_back(&descriptor);
    }
    std::sort(candidates.begin(), candidates.end(),
              [](const ComponentEditorDescriptor* lhs,
                 const ComponentEditorDescriptor* rhs) {
                if (lhs->category != rhs->category) {
                  return lhs->category < rhs->category;
                }
                return lhs->display_name < rhs->display_name;
              });
    const auto category_name = [](ComponentEditorCategory category) {
      switch (category) {
        case ComponentEditorCategory::General: return "General";
        case ComponentEditorCategory::Rendering: return "Rendering";
        case ComponentEditorCategory::Lighting: return "Lighting";
        case ComponentEditorCategory::Physics: return "Physics";
        case ComponentEditorCategory::Terrain: return "Terrain";
        case ComponentEditorCategory::Animation: return "Animation";
        case ComponentEditorCategory::Audio: return "Audio";
        case ComponentEditorCategory::Navigation: return "Navigation";
        case ComponentEditorCategory::Networking: return "Networking";
        case ComponentEditorCategory::Scripting: return "Scripting";
        case ComponentEditorCategory::Effects: return "Effects";
        case ComponentEditorCategory::Other: return "Other";
      }
      return "Other";
    };
    std::optional<ComponentEditorCategory> previous_category;
    for (const ComponentEditorDescriptor* candidate : candidates) {
      const ComponentEditorDescriptor& descriptor = *candidate;
      if (!previous_category.has_value() ||
          *previous_category != descriptor.category) {
        ImGui::SeparatorText(category_name(descriptor.category));
        previous_category = descriptor.category;
      }
      if (!ImGui::MenuItem(descriptor.display_name.c_str())) continue;
      if (descriptor.creation_policy ==
          ComponentCreationPolicy::ContextualWorkflow) {
        if (descriptor.type_name == "TerrainComponent") {
          open_create_terrain_ = true;
        } else if (descriptor.type_name == "FoliageComponent") {
          settings_.terrain_inspector_tab = 2;
          terrain_tab_selection_pending_ = true;
          open_create_foliage_ = true;
        }
        ImGui::CloseCurrentPopup();
        break;
      }
      if (descriptor.creation_policy ==
          ComponentCreationPolicy::ValidatedJsonDraft) {
        openComponentJsonEditor(descriptor.type_name,
                                descriptor.default_payload(), true);
        ImGui::CloseCurrentPopup();
        break;
      }
      scenes::SceneDocument next = document_;
      const auto entity = std::find_if(
          next.entities.begin(), next.entities.end(),
          [&](const scenes::SceneEntity& value) {
            return value.id == selection_.id;
          });
      std::vector<std::string> added;
      std::string error;
      if (entity == next.entities.end() ||
          !addDefaultComponentWithDependencies(
              *entity, component_editors_, descriptor.type_name, &added,
              &error)) {
        last_error_ = error.empty() ? "Component could not be added"
                                    : std::move(error);
        ImGui::CloseCurrentPopup();
        break;
      }
      const std::string selected_id = selection_.id;
      if (commitDocumentCommand("Add " + descriptor.display_name,
                                std::move(next))) {
        rebuildPreview();
        selection_ = {SelectionKind::Entity, selected_id};
      }
      ImGui::CloseCurrentPopup();
      break;
    }
    ImGui::EndPopup();
  }

  void drawViewport(app::UIContext&) {
    const float x = workspace_layout_.hierarchy_width +
                    workspace_layout_.splitter_size;
    ImGui::SetNextWindowPos({x, workspace_top_}, ImGuiCond_Always);
    ImGui::SetNextWindowSize(
        {std::max(workspace_layout_.center_width, 1.0f),
         std::max(workspace_layout_.viewport_height, 1.0f)},
        ImGuiCond_Always);
    ImGui::Begin("Viewport", nullptr,
                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar |
                     ImGuiWindowFlags_NoScrollWithMouse);
    const ImVec2 available = ImGui::GetContentRegionAvail();
    pending_viewport_width_ = std::max(1, static_cast<int>(available.x));
    pending_viewport_height_ = std::max(1, static_cast<int>(available.y));
    viewport_min_ = ImGui::GetCursorScreenPos();
    viewport_display_size_ = available;
    if (graphics != nullptr && viewport_target_ != rendering::kDefaultRenderTarget) {
      const auto texture = static_cast<app::UITextureHandle>(
          graphics->getRenderTargetTextureId(viewport_target_));
      const ImVec2 viewport_max{viewport_min_.x + available.x,
                                viewport_min_.y + available.y};
      // The scene projection, renderer target, and viewport interaction all use
      // the same upright orientation. Do not introduce a presentation-only UV
      // flip here: it would make drawing and picking disagree.
      ImGui::GetWindowDrawList()->AddImage(ui::imgui::toTextureId(texture),
                                           viewport_min_,
                                           viewport_max,
                                           {0.0f, 0.0f},
                                           {1.0f, 1.0f});
    }
    // A zero-ID Dummy advances layout and provides a drag/drop target without
    // covering the renderer-backed world-space handles.
    ImGui::SetCursorScreenPos(viewport_min_);
    ImGui::Dummy(available);
    const ImGuiIO& io = ImGui::GetIO();
    viewport_item_hovered_ = viewportContainsPointer(
        ViewportRect{viewport_min_.x, viewport_min_.y, available.x, available.y},
        io.MousePos.x,
        io.MousePos.y);
    viewport_hovered_ = viewport_item_hovered_;
    if (ImGui::BeginDragDropTarget()) {
      if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("KARMA_PREFAB")) {
        beginPrefabPlacement(
            std::filesystem::path(static_cast<const char*>(payload->Data)));
      }
      ImGui::EndDragDropTarget();
    }
    ImGui::End();
  }

  void drawWorkspaceSplitters() {
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    const float splitter = workspace_layout_.splitter_size;
    constexpr ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoNav |
        ImGuiWindowFlags_NoBackground;

    const auto begin_splitter = [&](const char* window,
                                    ImVec2 position,
                                    ImVec2 size,
                                    ImGuiMouseCursor cursor) {
      ImGui::SetNextWindowPos(position, ImGuiCond_Always);
      ImGui::SetNextWindowSize(size, ImGuiCond_Always);
      ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0.0f, 0.0f});
      ImGui::Begin(window, nullptr, flags);
      ImGui::InvisibleButton("##handle", size);
      const bool active = ImGui::IsItemActive();
      const bool hovered = ImGui::IsItemHovered();
      if (active || hovered) ImGui::SetMouseCursor(cursor);
      const ImU32 color = ImGui::GetColorU32(
          active ? ImGuiCol_SeparatorActive
                 : (hovered ? ImGuiCol_SeparatorHovered
                            : ImGuiCol_Separator));
      ImGui::GetWindowDrawList()->AddRectFilled(position,
                                                {position.x + size.x,
                                                 position.y + size.y},
                                                color);
      return active;
    };
    const auto end_splitter = [] {
      ImGui::End();
      ImGui::PopStyleVar();
    };
    const auto constrain_preferences = [&] {
      const EditorWorkspaceLayout constrained = resolveEditorWorkspaceLayout(
          settings_.panel_layout, display.x, workspace_height_);
      settings_.panel_layout.hierarchy_width = constrained.hierarchy_width;
      settings_.panel_layout.inspector_width = constrained.inspector_width;
      settings_.panel_layout.assets_height = constrained.assets_height;
    };

    const ImVec2 left_position{workspace_layout_.hierarchy_width,
                               workspace_top_};
    if (begin_splitter("##hierarchy_splitter", left_position,
                       {splitter, workspace_height_},
                       ImGuiMouseCursor_ResizeEW)) {
      settings_.panel_layout.hierarchy_width += ImGui::GetIO().MouseDelta.x;
      constrain_preferences();
    }
    end_splitter();

    const ImVec2 right_position{
        workspace_layout_.hierarchy_width + splitter +
            workspace_layout_.center_width,
        workspace_top_};
    if (begin_splitter("##inspector_splitter", right_position,
                       {splitter, workspace_height_},
                       ImGuiMouseCursor_ResizeEW)) {
      settings_.panel_layout.inspector_width -= ImGui::GetIO().MouseDelta.x;
      constrain_preferences();
    }
    end_splitter();

    const ImVec2 assets_position{
        workspace_layout_.hierarchy_width + splitter,
        workspace_top_ + workspace_layout_.viewport_height};
    if (begin_splitter("##assets_splitter", assets_position,
                       {workspace_layout_.center_width, splitter},
                       ImGuiMouseCursor_ResizeNS)) {
      settings_.panel_layout.assets_height -= ImGui::GetIO().MouseDelta.y;
      constrain_preferences();
    }
    end_splitter();
  }

  void drawStatusBar() {
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowPos({0.0f, display.y - 24.0f}, ImGuiCond_Always);
    ImGui::SetNextWindowSize({display.x, 24.0f}, ImGuiCond_Always);
    ImGui::Begin("##scene_editor_status", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoInputs);
    ImGui::Text("%s | %zu prefabs | %zu foliage layers | %s",
                toolName(tool_), document_.prefab_instances.size(), foliage_layers_.size(),
                dirty() ? "Modified" : "Saved");
    if (tool_ == ToolMode::PlacePrefab) {
      ImGui::SameLine();
      ImGui::TextColored({0.35f, 0.78f, 1.0f, 1.0f},
                         "| Click to place linked prefab; Escape cancels");
    }
    if (!last_error_.empty()) {
      ImGui::SameLine();
      ImGui::TextColored({1.0f, 0.35f, 0.25f, 1.0f}, "| %s", last_error_.c_str());
    }
    ImGui::End();
  }

  void drawModals() {
    if (open_component_json_) {
      ImGui::OpenPopup("Edit Component JSON");
      open_component_json_ = false;
    }
    if (ImGui::BeginPopupModal("Edit Component JSON", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
      ImGui::Text("%s", component_json_type_.c_str());
      ImGui::TextDisabled(
          "Changes are staged and deserialized before the scene document is updated.");
      ImGui::InputTextMultiline(
          "##component_json", component_json_buffer_.data(),
          component_json_buffer_.size(), {720.0f, 440.0f},
          ImGuiInputTextFlags_AllowTabInput);
      if (!component_json_error_.empty()) {
        ImGui::TextColored({1.0f, 0.35f, 0.25f, 1.0f}, "%s",
                           component_json_error_.c_str());
      }
      if (ImGui::Button(component_json_adding_ ? "Add" : "Apply")) {
        try {
          const nlohmann::json payload = nlohmann::json::parse(
              std::string(component_json_buffer_.data()));
          scenes::SceneDocument next = document_;
          const auto entity = std::find_if(
              next.entities.begin(), next.entities.end(),
              [&](const scenes::SceneEntity& value) {
                return value.id == component_json_entity_id_;
              });
          std::string error;
          const bool valid = entity != next.entities.end() &&
              (component_json_adding_
                   ? addComponentWithDependencies(
                         *entity, component_editors_, component_json_type_,
                         payload, nullptr, &error)
                   : replaceComponentPayload(
                         *entity, component_editors_, component_json_type_,
                         payload, &error));
          if (!valid) {
            component_json_error_ = error.empty()
                                        ? "Component payload failed validation"
                                        : std::move(error);
          } else {
            const std::string selected_id = component_json_entity_id_;
            if (commitDocumentCommand(
                    (component_json_adding_ ? "Add " : "Edit ") +
                        component_json_type_,
                    std::move(next))) {
              ImGui::CloseCurrentPopup();
              component_json_buffer_.clear();
              component_json_error_.clear();
              rebuildPreview();
              selection_ = {SelectionKind::Entity, selected_id};
            }
          }
        } catch (const std::exception& error) {
          component_json_error_ = std::string("Invalid JSON: ") + error.what();
        }
      }
      ImGui::SameLine();
      const bool escape = ImGui::IsKeyPressed(ImGuiKey_Escape);
      if (ImGui::Button("Cancel") || escape) {
        component_json_buffer_.clear();
        component_json_error_.clear();
        ImGui::CloseCurrentPopup();
      }
      ImGui::EndPopup();
    }

    if (open_create_terrain_) {
      ImGui::OpenPopup("Create Terrain");
      open_create_terrain_ = false;
    }
    if (ImGui::BeginPopupModal("Create Terrain", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
      ImGui::InputInt("Resolution", &new_terrain_resolution_);
      new_terrain_resolution_ = std::clamp(
          new_terrain_resolution_,
          3,
          static_cast<int>(visual::terrain::kMaxTerrainTileResolution));
      ImGui::DragFloat("Size", &new_terrain_size_, 1.0f, 1.0f, 100000.0f);
      ImGui::DragFloat("Height scale", &new_terrain_height_scale_, 1.0f, 0.01f, 100000.0f);
      ImGui::DragFloat("Height offset", &new_terrain_height_offset_, 0.5f, -100000.0f, 100000.0f);
      new_terrain_size_ = std::isfinite(new_terrain_size_)
                              ? std::clamp(new_terrain_size_, 1.0f, 100000.0f)
                              : 512.0f;
      new_terrain_height_scale_ =
          std::isfinite(new_terrain_height_scale_)
              ? std::clamp(new_terrain_height_scale_, 0.01f, 100000.0f)
              : 128.0f;
      new_terrain_height_offset_ =
          std::isfinite(new_terrain_height_offset_)
              ? std::clamp(new_terrain_height_offset_, -100000.0f, 100000.0f)
              : 0.0f;
      if (ImGui::Button("Import Heightmap...")) import_heightmap_on_create_ = true;
      ImGui::SameLine();
      if (ImGui::Button("Create Flat")) {
        if (createTerrain({})) ImGui::CloseCurrentPopup();
      }
      ImGui::SameLine();
      if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
      if (import_heightmap_on_create_) {
        import_heightmap_on_create_ = false;
        static const nfdu8filteritem_t filters[] = {{"Heightmap", "png,tga,jpg,bmp,pgm,r32,raw,r16"}};
        if (auto path = openFileDialog(filters, 1u, content_root_)) {
          if (createTerrain(*path)) ImGui::CloseCurrentPopup();
        }
      }
      ImGui::EndPopup();
    }

    if (open_create_foliage_) {
      ImGui::OpenPopup("Create Foliage Layer");
      open_create_foliage_ = false;
    }
    if (ImGui::BeginPopupModal("Create Foliage Layer", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
      ImGui::InputText("Name", new_foliage_name_, sizeof(new_foliage_name_));
      int source_kind = pending_foliage_prefab_.empty() ? 0 : 1;
      if (ImGui::Combo("Source type", &source_kind, "Direct mesh\0Prefab\0")) {
        if (source_kind == 0) {
          pending_foliage_prefab_.clear();
        } else {
          pending_foliage_mesh_.clear();
          pending_foliage_package_.clear();
        }
      }
      ImGui::SetNextItemWidth(420.0f);
      if (source_kind == 0) {
        const std::string mesh_preview = pending_foliage_mesh_.empty()
                                             ? "<choose base mesh>"
                                             : pending_foliage_mesh_;
        if (ImGui::BeginCombo("Base mesh", mesh_preview.c_str())) {
          for (const AssetEntry& entry : catalog_.entries()) {
            if (!entry.valid || entry.kind != AssetKind::Mesh) continue;
            const bool selected = entry.key == pending_foliage_mesh_;
            if (ImGui::Selectable(entry.name.c_str(), selected)) {
              pending_foliage_mesh_ = entry.key;
              pending_foliage_package_ = entry.package_path;
            }
            if (selected) ImGui::SetItemDefaultFocus();
          }
          ImGui::EndCombo();
        }
      } else {
        const std::string prefab_preview = pending_foliage_prefab_.empty()
                                               ? "<choose prefab>"
                                               : pending_foliage_prefab_
                                                     .parent_path()
                                                     .filename()
                                                     .string();
        if (ImGui::BeginCombo("Prefab", prefab_preview.c_str())) {
          for (const AssetEntry& entry : catalog_.entries()) {
            if (!entry.valid || entry.kind != AssetKind::Prefab) continue;
            const bool selected =
                entry.path.lexically_normal() ==
                pending_foliage_prefab_.lexically_normal();
            if (ImGui::Selectable(entry.name.c_str(), selected)) {
              pending_foliage_prefab_ = entry.path;
            }
            if (selected) ImGui::SetItemDefaultFocus();
          }
          ImGui::EndCombo();
        }
      }
      ImGui::DragFloat("Chunk size", &new_foliage_chunk_size_, 1.0f, 1.0f, 1000.0f);
      ImGui::DragFloat("View distance", &new_foliage_view_distance_, 1.0f, 1.0f, 100000.0f);
      new_foliage_chunk_size_ =
          std::isfinite(new_foliage_chunk_size_)
              ? std::clamp(new_foliage_chunk_size_, 1.0f, 1000.0f)
              : 32.0f;
      new_foliage_view_distance_ =
          std::isfinite(new_foliage_view_distance_)
              ? std::clamp(new_foliage_view_distance_, 1.0f, 100000.0f)
              : 256.0f;
      const bool has_source = !pending_foliage_mesh_.empty() ||
                              !pending_foliage_prefab_.empty();
      if (ImGui::Button("Create") && has_source) {
        if (createFoliageLayer()) ImGui::CloseCurrentPopup();
      }
      ImGui::SameLine();
      if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
      ImGui::EndPopup();
    }

    if (show_unsaved_prompt_) {
      ImGui::OpenPopup("Unsaved Scene Changes");
      show_unsaved_prompt_ = false;
    }
    if (ImGui::BeginPopupModal("Unsaved Scene Changes", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
      ImGui::TextWrapped(
          "Save changes to the current scene before continuing?");
      if (ImGui::Button("Save and continue")) {
        pending_scene_discard_unsaved_ = false;
        save_before_scene_action_pending_ = true;
        ImGui::CloseCurrentPopup();
      }
      ImGui::SameLine();
      if (ImGui::Button("Discard")) {
        pending_scene_discard_unsaved_ = true;
        ImGui::CloseCurrentPopup();
        continuePendingSceneAction();
      }
      ImGui::SameLine();
      if (ImGui::Button("Cancel")) {
        pending_scene_action_ = PendingSceneAction::None;
        pending_scene_path_.clear();
        pending_scene_discard_unsaved_ = false;
        ImGui::CloseCurrentPopup();
      }
      ImGui::EndPopup();
    }

    if (show_recovery_prompt_) ImGui::OpenPopup("Recover Scene");
    if (ImGui::BeginPopupModal("Recover Scene", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
      show_recovery_prompt_ = false;
      ImGui::TextWrapped("A newer recovery snapshot exists for this scene.");
      if (ImGui::Button("Restore")) {
        restoreRecovery();
        ImGui::CloseCurrentPopup();
      }
      ImGui::SameLine();
      if (ImGui::Button("Discard")) {
        discardRecovery(content_root_, scene_path_);
        ImGui::CloseCurrentPopup();
      }
      ImGui::EndPopup();
    }

    if (scene_conflict_) ImGui::OpenPopup("Scene Changed on Disk");
    if (ImGui::BeginPopupModal("Scene Changed on Disk", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
      scene_conflict_ = false;
      ImGui::TextWrapped("The scene changed on disk while local edits are unsaved.");
      if (ImGui::Button("Reload disk version")) {
        queueSceneLoad(scene_path_, true);
        ImGui::CloseCurrentPopup();
      }
      ImGui::SameLine();
      if (ImGui::Button("Keep local")) ImGui::CloseCurrentPopup();
      ImGui::SameLine();
      if (ImGui::Button("Save copy...")) {
        saveScene(true);
        ImGui::CloseCurrentPopup();
      }
      ImGui::EndPopup();
    }
  }

  void addGroup() {
    finishGizmoDrag(false);
    finishDocumentPropertyEditNow();
    scenes::SceneDocument before = document_;
    document_.entities.push_back(scenes::SceneEntity{
        .id = makeStableId("entity"), .name = "Group", .parent_id = rootEntityId()});
    pushDocumentCommand("Add Group", std::move(before));
    rebuildPreview();
  }

  void addLight() {
    finishGizmoDrag(false);
    finishDocumentPropertyEditNow();
    scenes::SceneDocument before = document_;
    const std::string entity_id = makeStableId("entity");
    document_.entities.push_back(scenes::SceneEntity{
        .id = entity_id,
        .name = "Point Light",
        .parent_id = rootEntityId(),
        .transform = scenes::SceneTransform{.position = {0.0f, 4.0f, 0.0f}},
    });
    components::LightComponent light{};
    light.type = components::LightComponent::Type::Point;
    light.intensity = 20.0f;
    light.range = 12.0f;
    document_.lights.push_back(scenes::SceneLight{
        .id = makeStableId("light"), .entity_id = entity_id, .component = light});
    pushDocumentCommand("Add Light", std::move(before));
    rebuildPreview();
    selection_ = {SelectionKind::Entity, entity_id};
  }

  void addEnvironment() {
    finishGizmoDrag(false);
    finishDocumentPropertyEditNow();
    scenes::SceneDocument before = document_;
    const std::string entity_id = makeStableId("entity");
    document_.entities.push_back(scenes::SceneEntity{
        .id = entity_id, .name = "Environment", .parent_id = rootEntityId()});
    scenes::SceneEnvironment environment{};
    environment.id = makeStableId("environment");
    environment.entity_id = entity_id;
    environment.component.intensity = 0.5f;
    environment.component.draw_skybox = true;
    document_.environment = std::move(environment);
    pushDocumentCommand("Add Environment", std::move(before));
    rebuildPreview();
  }

  bool createTerrain(const std::filesystem::path& import_path) {
    if (terrain_canvas_) {
      last_error_ = "V1 supports one editable terrain per scene";
      return false;
    }
    const uint32_t resolution = static_cast<uint32_t>(std::clamp(
        new_terrain_resolution_,
        3,
        static_cast<int>(visual::terrain::kMaxTerrainTileResolution)));
    scene_authoring::TerrainCanvasDesc desc{
        .resolution = resolution,
        .control_resolution = resolution,
        .terrain_size = new_terrain_size_,
        .height_scale = new_terrain_height_scale_,
        .height_offset = new_terrain_height_offset_,
    };
    std::optional<scene_authoring::TerrainCanvas> canvas;
    try {
      if (import_path.empty()) {
        canvas = scene_authoring::TerrainCanvas::create(desc, 0.25f);
      } else {
        assets::ScalarImageLoadOptions options{};
        if (import_path.extension() == ".r32") {
          options.format = assets::ScalarImageFormat::R32Float;
          options.raw_width = resolution;
          options.raw_height = resolution;
        } else if (import_path.extension() == ".raw" ||
                   import_path.extension() == ".r16") {
          options.format = assets::ScalarImageFormat::Raw16Unsigned;
          options.raw_width = resolution;
          options.raw_height = resolution;
        }
        auto image = assets::loadScalarImage(import_path, options);
        if (image) canvas = scene_authoring::TerrainCanvas::import(desc, *image);
      }
    } catch (const std::exception& error) {
      last_error_ = std::string("Failed to allocate or import terrain: ") +
                    error.what();
      return false;
    }
    if (!canvas) {
      last_error_ = "Terrain resolution must be power-of-two-plus-one and the heightmap must be readable";
      return false;
    }

    const std::string entity_id = makeStableId("terrain");
    std::string error;
    auto creation = createTerrainTransaction(
        document_,
        TerrainCreationRequest{
            .content_root = content_root_,
            .preview_directory = editorPreviewDirectory(),
            .entity_id = entity_id,
            .parent_entity_id = rootEntityId(),
        },
        std::move(*canvas),
        &error);
    if (!creation) {
      last_error_ = error.empty() ? "Failed to create terrain" : std::move(error);
      return false;
    }
    if (!commitDocumentCommand("Create Terrain", std::move(creation->document))) {
      std::error_code ignored;
      std::filesystem::remove(creation->height_path, ignored);
      std::filesystem::remove(creation->control_path, ignored);
      return false;
    }
    preview_rebuild_pending_ = true;
    preview_pending_selection_ = entity_id;
    last_error_.clear();
    return true;
  }

  template <typename Component>
  nlohmann::json serializeTemporaryComponent(Component component, std::string_view type) {
    world::World temporary_world;
    const world::Entity entity = temporary_world.createEntity();
    temporary_world.add(entity, std::move(component));
    return serializeComponent(temporary_world, entity, type);
  }

  bool createFoliageLayer() {
    const bool uses_mesh = !pending_foliage_mesh_.empty();
    const bool uses_prefab = !pending_foliage_prefab_.empty();
    if (uses_mesh == uses_prefab) {
      last_error_ =
          "Choose exactly one foliage source: a direct mesh or a prefab";
      return false;
    }
    if (uses_prefab) {
      const LegacyRenderMigrationReport migration =
          migratePrefabSourceClosure(
              {pending_foliage_prefab_}, content_root_);
      if (!migration.success()) {
        last_error_ = migration.diagnostics.empty()
                          ? "Foliage prefab migration failed"
                          : joinDiagnostics(migration.diagnostics);
        return false;
      }
      if (migration.changed) {
        prefab_asset_draft_status_ =
            "Automatically migrated foliage prefab; backup preserved";
        scanCatalog(false);
      }
      std::string prefab_error;
      if (!foliage_prefab_inspector_.validate(
              pending_foliage_prefab_,
              nlohmann::json::object(),
              &prefab_error)) {
        last_error_ = std::move(prefab_error);
        return false;
      }
    }
    if (!terrain_canvas_ || !terrain_authoring_valid_) {
      last_error_ = "Create or load an editable terrain before adding foliage";
      return false;
    }
    if (!std::isfinite(new_foliage_chunk_size_) ||
        new_foliage_chunk_size_ <= 0.0f ||
        !std::isfinite(new_foliage_view_distance_) ||
        new_foliage_view_distance_ <= 0.0f) {
      last_error_ = "Foliage chunk size and view distance must be finite and positive";
      return false;
    }
    const std::string entity_id = makeStableId("foliage");
    const std::filesystem::path preview_dir = editorPreviewDirectory();
    const std::filesystem::path sidecar = preview_dir / (entity_id + ".kfoliage");
    const auto relative_sidecar = contentRelativePath(content_root_, sidecar);
    if (!relative_sidecar) {
      last_error_ = "Foliage working path must remain inside the content root";
      return false;
    }
    std::error_code filesystem_error;
    if (std::filesystem::exists(sidecar, filesystem_error)) {
      last_error_ = filesystem_error
                        ? "Failed to inspect foliage working path: " +
                              filesystem_error.message()
                        : "Foliage working path already exists";
      return false;
    }
    if (filesystem_error) {
      last_error_ = "Failed to inspect foliage working path: " +
                    filesystem_error.message();
      return false;
    }

    scenes::SceneDocument next;
    try {
      next = document_;
      std::string package_error;
      if (uses_mesh && !pending_foliage_package_.empty() &&
          !ensurePackageReferenced(next,
                                   pending_foliage_package_,
                                   &package_error)) {
        last_error_ = std::move(package_error);
        return false;
      }
      components::FoliageComponent component{};
      component.sidecar_path = *relative_sidecar;
      if (uses_mesh) {
        component.mesh_asset_key = pending_foliage_mesh_;
      } else {
        const auto relative_prefab =
            contentRelativePath(content_root_, pending_foliage_prefab_);
        if (!relative_prefab.has_value()) {
          last_error_ = "Foliage prefab must remain inside the content root";
          return false;
        }
        component.prefab_path = *relative_prefab;
      }
      component.chunk_size = new_foliage_chunk_size_;
      component.view_distance = new_foliage_view_distance_;
      const nlohmann::json component_json =
          serializeTemporaryComponent(component, "FoliageComponent");
      if (component_json.empty()) {
        last_error_ = "Foliage component serializer is unavailable";
        return false;
      }
      scenes::SceneTransform foliage_transform{};
      if (const auto terrain = std::find_if(
              next.entities.begin(), next.entities.end(),
              [&](const scenes::SceneEntity& entity) {
                return entity.id == terrain_entity_id_;
              });
          terrain != next.entities.end()) {
        foliage_transform = terrain->transform;
      }
      next.entities.push_back(scenes::SceneEntity{
          .id = entity_id,
          .name = new_foliage_name_[0] == '\0' ? "Foliage" : new_foliage_name_,
          .parent_id = rootEntityId(),
          .transform = foliage_transform,
          .components = {{"FoliageComponent", component_json}},
      });
    } catch (const std::exception& error) {
      last_error_ = std::string("Failed to stage foliage layer: ") + error.what();
      return false;
    }

    foliage::FoliageDocument empty{.chunk_size = new_foliage_chunk_size_};
    std::string write_error;
    try {
      if (!foliage::writeFoliageFile(sidecar, empty, &write_error)) {
        last_error_ = write_error.empty() ? "Failed to create foliage sidecar"
                                          : std::move(write_error);
        return false;
      }
    } catch (const std::exception& error) {
      last_error_ = std::string("Failed to create foliage sidecar: ") +
                    error.what();
      return false;
    }
    if (!commitDocumentCommand("Create Foliage Layer", std::move(next))) {
      std::filesystem::remove(sidecar, filesystem_error);
      return false;
    }
    preview_rebuild_pending_ = true;
    preview_pending_selection_ = entity_id;
    preview_pending_tool_ = ToolMode::PaintFoliage;
    last_error_.clear();
    return true;
  }

  bool duplicateActiveFoliageLayer() {
    FoliageLayerState* state = selectedFoliageLayer();
    if (state == nullptr) return false;
    size_t authored_instance_total = 0u;
    for (const FoliageLayerState& layer : foliage_layers_) {
      if (layer.source_valid) authored_instance_total += layer.layer.instanceCount();
    }
    if (state->layer.instanceCount() >
        kMaxAuthoredFoliageInstances - authored_instance_total) {
      last_error_ =
          "Duplicating this layer would exceed the 1,000,000 instance limit";
      return false;
    }
    const world::Entity runtime = preview_.find(state->entity_id);
    if (!world->isAlive(runtime) ||
        !world->has<components::FoliageComponent>(runtime)) {
      last_error_ = "The selected foliage preview is unavailable";
      return false;
    }
    const auto source = findEntity(state->entity_id);
    if (source == document_.entities.end()) {
      last_error_ = "The selected foliage layer is missing from the scene";
      return false;
    }

    const std::string entity_id = makeStableId("foliage");
    const std::filesystem::path sidecar =
        editorPreviewDirectory() / (entity_id + ".kfoliage");
    const auto relative_sidecar = contentRelativePath(content_root_, sidecar);
    if (!relative_sidecar) {
      last_error_ = "Foliage working path must remain inside the content root";
      return false;
    }
    std::error_code filesystem_error;
    std::filesystem::create_directories(sidecar.parent_path(), filesystem_error);
    if (filesystem_error) {
      last_error_ = "Failed to create foliage preview directory: " +
                    filesystem_error.message();
      return false;
    }
    std::string write_error;
    if (!foliage::writeFoliageFile(sidecar, state->layer.toDocument(),
                                   &write_error)) {
      last_error_ = write_error.empty() ? "Failed to duplicate foliage sidecar"
                                        : std::move(write_error);
      return false;
    }

    scenes::SceneDocument next = document_;
    const auto next_source = std::find_if(
        next.entities.begin(), next.entities.end(),
        [&](const scenes::SceneEntity& entity) {
          return entity.id == state->entity_id;
        });
    if (next_source == next.entities.end()) {
      std::filesystem::remove(sidecar, filesystem_error);
      return false;
    }
    scenes::SceneEntity copy = *next_source;
    copy.id = entity_id;
    copy.name = copy.name.empty() ? "Foliage Copy" : copy.name + " Copy";
    auto component = world->get<components::FoliageComponent>(runtime);
    component.sidecar_path = *relative_sidecar;
    if (!component.prefab_path.empty()) {
      makeContentRelative(component.prefab_path);
    }
    const nlohmann::json serialized =
        serializeTemporaryComponent(component, "FoliageComponent");
    if (serialized.empty()) {
      std::filesystem::remove(sidecar, filesystem_error);
      last_error_ = "Foliage component validation failed";
      return false;
    }
    copy.components["FoliageComponent"] = serialized;
    next.entities.push_back(std::move(copy));
    if (!commitDocumentCommand("Duplicate Foliage Layer", std::move(next))) {
      std::filesystem::remove(sidecar, filesystem_error);
      return false;
    }
    preview_rebuild_pending_ = true;
    preview_pending_selection_ = entity_id;
    last_error_.clear();
    return true;
  }

  bool deleteActiveFoliageLayer() {
    FoliageLayerState* state = selectedFoliageLayer();
    if (state == nullptr) return false;
    finishFoliageStroke();
    scenes::SceneDocument next = document_;
    std::string error;
    if (!deleteSelectionPreservingWorld(
            next, {SelectionKind::Entity, state->entity_id}, &error)) {
      last_error_ = std::move(error);
      return false;
    }
    if (!commitDocumentCommand("Delete Foliage Layer", std::move(next))) {
      return false;
    }
    settings_.active_foliage_layer_id.clear();
    changeTool(ToolMode::Select);
    rebuildPreview();
    if (terrain_entity_id_.empty()) {
      selection_.clear();
    } else {
      selection_ = {SelectionKind::Entity, terrain_entity_id_};
    }
    last_error_.clear();
    return true;
  }

  void placePendingPrefab(const math::Vec3& point) {
    if (pending_prefab_.empty()) return;
    const auto relative = contentRelativePath(content_root_, pending_prefab_);
    if (!relative) {
      last_error_ = "Prefab must be inside the content root";
      return;
    }
    const std::string parent_id =
        selectedEditableGroupId(document_, selection_);
    const scenes::SceneTransform world_transform{.position = point};
    std::string transform_error;
    const auto local_transform = linkedPrefabLocalTransform(
        document_, parent_id, world_transform, &transform_error);
    if (!local_transform.has_value()) {
      last_error_ = transform_error.empty()
                        ? "Prefab placement parent transform is invalid"
                        : std::move(transform_error);
      return;
    }
    scenes::SceneDocument before = document_;
    const std::string id = makeStableId("prefab");
    document_.prefab_instances.push_back(scenes::ScenePrefabInstance{
        .id = id,
        .prefab_path = *relative,
        .parent_entity_id = parent_id,
        .transform = *local_transform,
    });
    pushDocumentCommand("Place Prefab", std::move(before));
    destroyPrefabPlacementPreview();
    pending_prefab_.clear();
    placement_world_point_.reset();
    tool_ = ToolMode::Select;
    rebuildPreview();
    selection_ = {SelectionKind::Prefab, id};
  }

  void assignTerrainMaterial(const AssetEntry& entry) {
    if (entry.kind != AssetKind::Material || entry.key.empty() ||
        !terrain_canvas_ || !world->isAlive(terrain_entity_)) {
      return;
    }
    settings_.terrain_material_layer = std::clamp(splat_layer_, 0, 3);
    scenes::SceneDocument next;
    try {
      next = document_;
      std::string package_error;
      if (!entry.package_path.empty() &&
          !ensurePackageReferenced(next, entry.package_path, &package_error)) {
        last_error_ = std::move(package_error);
        return;
      }
      auto component =
          world->get<components::TerrainComponent>(terrain_entity_);
      const size_t old_layer_count = component.material_layers.size();
      component.material_layers.resize(4u);
      for (size_t index = old_layer_count;
           index < component.material_layers.size();
           ++index) {
        component.material_layers[index].name =
            "Layer " + std::to_string(index + 1u);
        component.material_layers[index].enabled = false;
      }
      auto& layer = component.material_layers[static_cast<size_t>(
          std::clamp(splat_layer_, 0, 3))];
      if (layer.name.empty()) {
        layer.name =
            "Layer " + std::to_string(std::clamp(splat_layer_, 0, 3) + 1);
      }
      layer.material_key = entry.key;
      layer.enabled = true;
      const auto entity = std::find_if(
          next.entities.begin(), next.entities.end(),
          [&](const scenes::SceneEntity& value) {
            return value.id == terrain_entity_id_;
          });
      if (entity == next.entities.end()) {
        last_error_ = "The editable terrain is missing from the scene document";
        return;
      }
      const nlohmann::json serialized = serializeTemporaryComponent(
          portableTerrainComponent(std::move(component)), "TerrainComponent");
      if (serialized.empty()) {
        last_error_ = "Terrain component serializer is unavailable";
        return;
      }
      entity->components["TerrainComponent"] = serialized;
    } catch (const std::exception& error) {
      last_error_ = std::string("Failed to stage terrain material: ") +
                    error.what();
      return;
    }
    if (!commitDocumentCommand("Assign Terrain Material", std::move(next))) {
      return;
    }
    rebuildPreview();
    selection_ = {SelectionKind::Entity, terrain_entity_id_};
    changeTool(ToolMode::PaintSplat);
    last_error_.clear();
  }

  void assignFoliageMaterial(const AssetEntry& entry) {
    assignFoliageMaterial(entry, 0u);
  }

  void assignFoliageMaterial(const AssetEntry& entry,
                             uint32_t material_slot) {
    FoliageLayerState* state = selectedFoliageLayer();
    if (entry.kind != AssetKind::Material || entry.key.empty() ||
        state == nullptr) {
      return;
    }
    const world::Entity runtime = preview_.find(state->entity_id);
    if (!world->isAlive(runtime) ||
        !world->has<components::FoliageComponent>(runtime)) {
      return;
    }
    const std::string selected_id = state->entity_id;
    scenes::SceneDocument next;
    try {
      next = document_;
      std::string package_error;
      if (!entry.package_path.empty() &&
          !ensurePackageReferenced(next, entry.package_path, &package_error)) {
        last_error_ = std::move(package_error);
        return;
      }
      auto component = world->get<components::FoliageComponent>(runtime);
      if (!component.prefab_path.empty()) {
        last_error_ =
            "Prefab-backed foliage materials are authored in the prefab source";
        return;
      }
      auto& materials = component.materials;
      const auto slot = std::find_if(
          materials.begin(), materials.end(),
          [&](const components::MeshMaterialAssignment& material) {
            return material.slot == material_slot;
          });
      if (slot == materials.end()) {
        materials.push_back(components::MeshMaterialAssignment{
            .slot = material_slot,
            .material_key = entry.key,
        });
      } else {
        slot->material_key = entry.key;
      }
      std::sort(materials.begin(), materials.end(),
                [](const auto& a, const auto& b) { return a.slot < b.slot; });
      std::string validation_error;
      if (!foliage::validateFoliageComponent(component, &validation_error)) {
        last_error_ = std::move(validation_error);
        return;
      }
      makeContentRelative(component.sidecar_path);
      const auto entity = std::find_if(
          next.entities.begin(), next.entities.end(),
          [&](const scenes::SceneEntity& value) {
            return value.id == selected_id;
          });
      if (entity == next.entities.end()) {
        last_error_ = "The foliage layer is missing from the scene document";
        return;
      }
      const nlohmann::json serialized = serializeTemporaryComponent(
          std::move(component), "FoliageComponent");
      if (serialized.empty()) {
        last_error_ = "Foliage component serializer is unavailable";
        return;
      }
      entity->components["FoliageComponent"] = serialized;
    } catch (const std::exception& error) {
      last_error_ = std::string("Failed to stage foliage material: ") +
                    error.what();
      return;
    }
    if (!commitDocumentCommand("Assign Foliage Material", std::move(next))) {
      return;
    }
    rebuildPreview();
    focusFoliageLayer(selected_id);
    last_error_.clear();
  }

  void assignFoliageMesh(const AssetEntry& entry) {
    FoliageLayerState* state = selectedFoliageLayer();
    if (entry.kind != AssetKind::Mesh || entry.key.empty() || state == nullptr) {
      return;
    }
    const world::Entity runtime = preview_.find(state->entity_id);
    if (!world->isAlive(runtime) ||
        !world->has<components::FoliageComponent>(runtime)) {
      return;
    }
    const std::string selected_id = state->entity_id;
    scenes::SceneDocument next = document_;
    std::string package_error;
    if (!entry.package_path.empty() &&
        !ensurePackageReferenced(next, entry.package_path, &package_error)) {
      last_error_ = std::move(package_error);
      return;
    }
    auto component = world->get<components::FoliageComponent>(runtime);
    component.prefab_path.clear();
    component.prefab_variables = nlohmann::json::object();
    component.mesh_asset_key = entry.key;
    std::string validation_error;
    if (!foliage::validateFoliageComponent(component, &validation_error)) {
      last_error_ = std::move(validation_error);
      return;
    }
    makeContentRelative(component.sidecar_path);
    const auto entity = std::find_if(
        next.entities.begin(), next.entities.end(),
        [&](const scenes::SceneEntity& value) {
          return value.id == selected_id;
        });
    if (entity == next.entities.end()) {
      last_error_ = "The foliage layer is missing from the scene document";
      return;
    }
    const nlohmann::json serialized =
        serializeTemporaryComponent(component, "FoliageComponent");
    if (serialized.empty()) {
      last_error_ = "Foliage component serializer is unavailable";
      return;
    }
    entity->components["FoliageComponent"] = serialized;
    if (!commitDocumentCommand("Assign Foliage Mesh", std::move(next))) {
      return;
    }
    rebuildPreview();
    focusFoliageLayer(selected_id);
    last_error_.clear();
  }

  void assignFoliagePrefab(const AssetEntry& entry) {
    FoliageLayerState* state = selectedFoliageLayer();
    if (entry.kind != AssetKind::Prefab || !entry.valid || state == nullptr) {
      return;
    }
    const LegacyRenderMigrationReport migration =
        migratePrefabSourceClosure({entry.path}, content_root_);
    if (!migration.success()) {
      last_error_ = migration.diagnostics.empty()
                        ? "Foliage prefab migration failed"
                        : joinDiagnostics(migration.diagnostics);
      return;
    }
    if (migration.changed) {
      prefab_asset_draft_.reset();
      prefab_asset_draft_status_ =
          "Automatically migrated foliage prefab; backup preserved";
      scanCatalog(false);
    }
    std::string prefab_error;
    if (!foliage_prefab_inspector_.validate(entry.path,
                                            nlohmann::json::object(),
                                            &prefab_error)) {
      last_error_ = std::move(prefab_error);
      return;
    }
    const auto relative = contentRelativePath(content_root_, entry.path);
    if (!relative.has_value()) {
      last_error_ = "Foliage prefab must remain inside the content root";
      return;
    }
    const world::Entity runtime = preview_.find(state->entity_id);
    if (!world->isAlive(runtime) ||
        !world->has<components::FoliageComponent>(runtime)) {
      last_error_ = "The selected foliage preview is unavailable";
      return;
    }
    components::FoliageComponent component =
        world->get<components::FoliageComponent>(runtime);
    component.prefab_path = entry.path;
    component.prefab_variables = nlohmann::json::object();
    component.mesh_asset_key.clear();
    component.materials.clear();
    std::string validation_error;
    if (!foliage::validateFoliageComponent(component, &validation_error)) {
      last_error_ = std::move(validation_error);
      return;
    }
    components::FoliageComponent authored = component;
    authored.prefab_path = *relative;
    makeContentRelative(authored.sidecar_path);
    const nlohmann::json serialized =
        serializeTemporaryComponent(authored, "FoliageComponent");
    if (serialized.empty()) {
      last_error_ = "Foliage prefab source validation failed";
      return;
    }
    scenes::SceneDocument next = document_;
    const auto entity = std::find_if(
        next.entities.begin(), next.entities.end(),
        [&](const scenes::SceneEntity& value) {
          return value.id == state->entity_id;
        });
    if (entity == next.entities.end()) return;
    entity->components["FoliageComponent"] = serialized;
    entity->components.erase("LODComponent");
    const std::string selected_id = state->entity_id;
    if (!commitDocumentCommand("Assign Foliage Prefab", std::move(next))) {
      return;
    }
    rebuildPreview();
    focusFoliageLayer(selected_id);
    last_error_.clear();
  }

  void setEnvironmentAsset(const AssetEntry& entry) {
    if (entry.kind != AssetKind::Environment || entry.key.empty()) return;
    scenes::SceneDocument next;
    try {
      next = document_;
      std::string package_error;
      if (!entry.package_path.empty() &&
          !ensurePackageReferenced(next, entry.package_path, &package_error)) {
        last_error_ = std::move(package_error);
        return;
      }
      if (!next.environment) {
        const std::string entity_id = makeStableId("entity");
        next.entities.push_back(scenes::SceneEntity{
            .id = entity_id,
            .name = "Environment",
            .parent_id = rootEntityId()});
        scenes::SceneEnvironment environment{};
        environment.id = makeStableId("environment");
        environment.entity_id = entity_id;
        environment.component.intensity = 0.5f;
        environment.component.draw_skybox = true;
        next.environment = std::move(environment);
      }
      next.environment->environment_map_asset_id = entry.key;
      next.environment->component.environment_map_asset_key = entry.key;
    } catch (const std::exception& error) {
      last_error_ = std::string("Failed to stage environment asset: ") +
                    error.what();
      return;
    }
    if (!commitDocumentCommand("Set Environment", std::move(next))) {
      return;
    }
    rebuildPreview();
    last_error_.clear();
  }

  bool ensurePackageReferenced(scenes::SceneDocument& document,
                               const std::filesystem::path& package_path,
                               std::string* diagnostic = nullptr) {
    if (diagnostic != nullptr) diagnostic->clear();
    const auto relative = contentRelativePath(content_root_, package_path);
    if (!relative) {
      if (diagnostic != nullptr) {
        *diagnostic = "Asset package must remain inside the content root";
      }
      return false;
    }
    std::error_code filesystem_error;
    if (!std::filesystem::is_regular_file(package_path, filesystem_error)) {
      if (diagnostic != nullptr) {
        *diagnostic = filesystem_error
                          ? "Failed to inspect asset package: " +
                                filesystem_error.message()
                          : "Asset package no longer exists: " +
                                package_path.string();
      }
      return false;
    }
    const auto existing = std::find_if(document.asset_packages.begin(),
                                       document.asset_packages.end(),
                                       [&](const scenes::SceneAssetRef& package) {
                                         return package.path == *relative;
                                       });
    if (existing != document.asset_packages.end()) return true;
    document.asset_packages.push_back(scenes::SceneAssetRef{
        .id = makeStableId("package"),
        .path = *relative,
        .type = "asset_package",
    });
    return true;
  }

  void syncSelectionTransform(const components::TransformComponent& transform) {
    if (selection_.kind == SelectionKind::Entity) {
      const auto it = findEntity(selection_.id);
      if (it != document_.entities.end()) it->transform = fromRuntimeTransform(transform);
    } else if (selection_.kind == SelectionKind::Prefab) {
      const auto it = findPrefab(selection_.id);
      if (it == document_.prefab_instances.end()) return;
      const auto saved = prefab_saved_root_transforms_.find(selection_.id);
      it->transform = saved == prefab_saved_root_transforms_.end()
                          ? fromRuntimeTransform(transform)
                          : sceneTransformWithoutChild(
                                fromRuntimeTransform(transform), saved->second);
    }
  }

  void deleteSelection() {
    if (!selection_.valid()) return;
    finishGizmoDrag(false);
    finishTerrainStroke();
    finishFoliageStroke();
    finishDocumentPropertyEditNow();
    scenes::SceneDocument before = document_;
    std::string error;
    if (!deleteSelectionPreservingWorld(document_, selection_, &error)) {
      last_error_ = std::move(error);
      return;
    }
    pushDocumentCommand("Delete", std::move(before));
    selection_.clear();
    rebuildPreview();
  }

  void duplicateSelected() {
    if (!selection_.valid()) return;
    finishGizmoDrag(false);
    finishTerrainStroke();
    finishFoliageStroke();
    finishDocumentPropertyEditNow();
    scenes::SceneDocument before = document_;
    std::string error;
    const std::optional<Selection> duplicated =
        karma::tools::scene_editor::duplicateSelection(
            document_,
            selection_,
            [](std::string_view prefix) { return makeStableId(prefix); },
            &error);
    if (!duplicated) {
      last_error_ = std::move(error);
      return;
    }
    pushDocumentCommand("Duplicate", std::move(before));
    selection_ = *duplicated;
    rebuildPreview();
  }

  bool pushDocumentCommand(std::string label, scenes::SceneDocument before) {
    EditCommand command{};
    try {
      command.kind = EditCommand::Kind::Document;
      command.label = std::move(label);
      command.before_document = std::move(before);
      command.after_document = document_;
      command.bytes =
          scenes::sceneDocumentToJson(command.before_document).dump().size() +
          scenes::sceneDocumentToJson(command.after_document).dump().size();
      commands_.reserve(commands_.size() + 1u);
    } catch (const std::exception& error) {
      document_ = std::move(command.before_document);
      last_error_ = std::string("Failed to stage editor history: ") +
                    error.what();
      return false;
    }
    pushCommand(std::move(command));
    return true;
  }

  bool commitDocumentCommand(std::string label,
                             scenes::SceneDocument next) {
    EditCommand command{};
    try {
      command.kind = EditCommand::Kind::Document;
      command.label = std::move(label);
      command.before_document = document_;
      command.after_document = next;
      command.bytes =
          scenes::sceneDocumentToJson(command.before_document).dump().size() +
          scenes::sceneDocumentToJson(command.after_document).dump().size();
      commands_.reserve(commands_.size() + 1u);
    } catch (const std::exception& error) {
      last_error_ = std::string("Failed to stage editor history: ") +
                    error.what();
      return false;
    }
    document_ = std::move(next);
    pushCommand(std::move(command));
    return true;
  }

  void pushCommand(EditCommand command) {
    bake_stale_ = true;
    if (saved_state_reachable_ && saved_cursor_ > command_cursor_) {
      saved_state_reachable_ = false;
    }
    while (commands_.size() > command_cursor_) {
      command_bytes_ -= commands_.back().bytes;
      commands_.pop_back();
    }
    command_bytes_ += command.bytes;
    commands_.push_back(std::move(command));
    command_cursor_ = commands_.size();
    while (!commands_.empty() &&
           (commands_.size() > 256u || command_bytes_ > 512u * 1024u * 1024u)) {
      command_bytes_ -= commands_.front().bytes;
      commands_.erase(commands_.begin());
      if (command_cursor_ > 0u) --command_cursor_;
      if (saved_state_reachable_) {
        if (saved_cursor_ > 0u) {
          --saved_cursor_;
        } else {
          saved_state_reachable_ = false;
        }
      }
    }
    pending_recovery_ = true;
    recovery_at_ = std::chrono::steady_clock::now() + kRecoveryDebounce;
  }

  void applyCommand(EditCommand& command, bool forward) {
    if (command.kind == EditCommand::Kind::Document) {
      document_ = forward ? command.after_document : command.before_document;
      document_.source_path = scene_path_;
      document_.reference_root = content_root_;
      rebuildPreview();
      return;
    }
    if (command.kind == EditCommand::Kind::Terrain && terrain_canvas_) {
      auto heights = terrain_canvas_->mutableHeights();
      auto control = terrain_canvas_->mutableControlRgba8();
      const auto& source_heights = forward ? command.after_heights : command.before_heights;
      const auto& source_control = forward ? command.after_control : command.before_control;
      if (source_heights.size() == heights.size()) std::copy(source_heights.begin(), source_heights.end(), heights.begin());
      if (source_control.size() == control.size()) std::copy(source_control.begin(), source_control.end(), control.begin());
      if (terrain_runtime_ != nullptr) {
        terrain_runtime_->setSingleImageTileOverride(terrain_entity_, terrain_canvas_->buildTileData());
      }
      persistTerrainPreview();
      return;
    }
    if (command.kind == EditCommand::Kind::Foliage) {
      if (FoliageLayerState* state = findFoliageLayer(command.foliage_entity_id)) {
        state->layer.applyEdit(command.foliage_edit, !forward);
        const world::Entity source = preview_.find(command.foliage_entity_id);
        if (foliage_runtime_ != nullptr && world->isAlive(source)) {
          foliage_runtime_->setLayerOverride(source, state->layer);
        }
        persistFoliagePreview(*state);
      }
    }
  }

  void undo() {
    finishGizmoDrag(false);
    finishTerrainStroke();
    finishFoliageStroke();
    finishDocumentPropertyEditNow();
    if (command_cursor_ == 0u) return;
    --command_cursor_;
    applyCommand(commands_[command_cursor_], false);
    pending_recovery_ = true;
  }

  void redo() {
    finishGizmoDrag(false);
    finishTerrainStroke();
    finishFoliageStroke();
    finishDocumentPropertyEditNow();
    if (command_cursor_ >= commands_.size()) return;
    applyCommand(commands_[command_cursor_], true);
    ++command_cursor_;
    pending_recovery_ = true;
  }

  bool dirty() const {
    return !has_disk_version_ || !saved_state_reachable_ ||
           command_cursor_ != saved_cursor_;
  }

  bool hasLocalEdits() const {
    return !saved_state_reachable_ || command_cursor_ != saved_cursor_;
  }

  bool persistTerrainPreview() {
    if (!terrain_canvas_ || terrain_entity_id_.empty() ||
        !terrain_authoring_valid_) {
      return false;
    }
    const std::filesystem::path directory = editorPreviewDirectory();
    std::error_code ec;
    std::filesystem::create_directories(directory, ec);
    if (ec) {
      last_error_ = "Failed to create terrain preview directory: " + ec.message();
      return false;
    }
    std::string error;
    const auto height = directory / (terrain_entity_id_ + "-height.r32");
    const auto control = directory / (terrain_entity_id_ + "-control.tga");
    try {
      if (!terrain_canvas_->saveHeightR32(height, &error) ||
          !terrain_canvas_->saveControlTga(control, &error)) {
        last_error_ = error;
        return false;
      }
    } catch (const std::exception& exception) {
      last_error_ =
          std::string("Failed to write terrain preview: ") + exception.what();
      return false;
    }
    return true;
  }

  void persistFoliagePreview(FoliageLayerState& state) {
    if (!state.source_valid || state.working_path.empty()) return;
    std::error_code ec;
    std::filesystem::create_directories(state.working_path.parent_path(), ec);
    if (ec) {
      last_error_ = "Failed to create foliage preview directory: " + ec.message();
      return;
    }
    std::string error;
    if (!foliage::writeFoliageFile(state.working_path,
                                   state.layer.toDocument(),
                                   &error)) {
      last_error_ = error;
    }
  }

  bool saveAuthoredSidecars(const std::filesystem::path& target_scene,
                            scenes::SceneDocument& save_document) {
    const std::string stem = filenameStemForScene(target_scene);
    const std::filesystem::path asset_dir = target_scene.parent_path() /
                                            (stem + ".scene-assets");
    std::error_code ec;
    std::filesystem::create_directories(asset_dir, ec);
    if (ec) {
      last_error_ = "Failed to create scene asset directory: " + ec.message();
      return false;
    }
    if (terrain_canvas_ && !terrain_entity_id_.empty() &&
        terrain_authoring_valid_) {
      const auto temporary_height = asset_dir / (terrain_entity_id_ + "-height.tmp.r32");
      const auto temporary_control = asset_dir / (terrain_entity_id_ + "-control.tmp.tga");
      std::string error;
      if (!terrain_canvas_->saveHeightR32(temporary_height, &error) ||
          !terrain_canvas_->saveControlTga(temporary_control, &error)) {
        last_error_ = error;
        return false;
      }
      const auto final_height = asset_dir /
          (terrain_entity_id_ + "-height-" + hexHash(hashFile(temporary_height)) + ".r32");
      const auto final_control = asset_dir /
          (terrain_entity_id_ + "-control-" + hexHash(hashFile(temporary_control)) + ".tga");
      std::filesystem::rename(temporary_height, final_height, ec);
      if (ec && !pathExistsNoThrow(final_height)) {
        last_error_ = "Failed to commit terrain height: " + ec.message();
        return false;
      }
      if (ec) {
        std::error_code remove_error;
        std::filesystem::remove(temporary_height, remove_error);
      }
      ec.clear();
      std::filesystem::rename(temporary_control, final_control, ec);
      if (ec && !pathExistsNoThrow(final_control)) {
        last_error_ = "Failed to commit terrain control map: " + ec.message();
        return false;
      }
      if (ec) {
        std::error_code remove_error;
        std::filesystem::remove(temporary_control, remove_error);
      }
      auto entity = std::find_if(save_document.entities.begin(), save_document.entities.end(),
                                 [&](const scenes::SceneEntity& item) {
                                   return item.id == terrain_entity_id_;
                                 });
      const world::Entity runtime = preview_.find(terrain_entity_id_);
      if (entity != save_document.entities.end() && world->isAlive(runtime)) {
        auto component = portableTerrainComponent(
            world->get<components::TerrainComponent>(runtime));
        component.source = components::TerrainSourceType::SingleImage;
        component.height_image = final_height;
        component.control_image = final_control;
        makeContentRelative(component.height_image);
        makeContentRelative(component.control_image);
        component.height_format = components::TerrainHeightFormat::R32Float;
        component.raw_width = terrain_canvas_->resolution();
        component.raw_height = terrain_canvas_->resolution();
        component.raw_little_endian = true;
        component.flip_y = false;
        component.height_value_min = 0.0f;
        component.height_value_max = 1.0f;
        component.source_revision += 1u;
        entity->components["TerrainComponent"] =
            serializeTemporaryComponent(std::move(component), "TerrainComponent");
      }
    }
    for (FoliageLayerState& state : foliage_layers_) {
      if (!state.source_valid) continue;
      const auto temporary = asset_dir / (state.entity_id + ".tmp.kfoliage");
      std::string error;
      if (!foliage::writeFoliageFile(temporary, state.layer.toDocument(), &error)) {
        last_error_ = error;
        return false;
      }
      const auto final = asset_dir /
          (state.entity_id + '-' + hexHash(hashFile(temporary)) + ".kfoliage");
      ec.clear();
      std::filesystem::rename(temporary, final, ec);
      if (ec && !pathExistsNoThrow(final)) {
        last_error_ = "Failed to commit foliage sidecar: " + ec.message();
        return false;
      }
      if (ec) {
        std::error_code remove_error;
        std::filesystem::remove(temporary, remove_error);
      }
      const world::Entity runtime = preview_.find(state.entity_id);
      auto entity = std::find_if(save_document.entities.begin(), save_document.entities.end(),
                                 [&](const scenes::SceneEntity& item) {
                                   return item.id == state.entity_id;
                                 });
      if (entity != save_document.entities.end() && world->isAlive(runtime)) {
        auto component = world->get<components::FoliageComponent>(runtime);
        component.sidecar_path = final;
        makeContentRelative(component.sidecar_path);
        if (!component.prefab_path.empty()) {
          makeContentRelative(component.prefab_path);
        }
        component.source_revision += 1u;
        entity->components["FoliageComponent"] = serializeTemporaryComponent(component, "FoliageComponent");
      }
    }
    return true;
  }

  void saveScene(bool save_as) {
    cancelPrefabPlacement();
    finishGizmoDrag(false);
    finishTerrainStroke();
    finishFoliageStroke();
    finishDocumentPropertyEditNow();
    std::filesystem::path target = scene_path_;
    if (save_as || target.filename() == "untitled.kscene.json") {
      static const nfdu8filteritem_t filters[] = {{"Karma Scene", "json"}};
      auto selected = saveFileDialog(filters, 1u, target.parent_path(), target.filename().string());
      if (!selected) return;
      target = ensureSceneExtension(*selected);
    }
    if (!pathIsWithin(content_root_, target)) {
      last_error_ = "Scene must be saved inside the content root";
      return;
    }
    scenes::SceneDocument save_document = document_;
    save_document.source_path = target;
    save_document.reference_root = content_root_;
    if (!saveAuthoredSidecars(target, save_document)) return;
    const scenes::SceneSaveResult result = scenes::saveSceneDocument(save_document, target);
    if (!result.success()) {
      last_error_ = joinDiagnostics(result.diagnostics);
      return;
    }
    const std::filesystem::path previous_scene_path = scene_path_;
    scene_path_ = target;
    document_.source_path = target;
    document_.reference_root = content_root_;
    saved_cursor_ = command_cursor_;
    saved_state_reachable_ = true;
    has_disk_version_ = true;
    last_error_.clear();
    if (previous_scene_path != scene_path_) {
      discardRecovery(content_root_, previous_scene_path);
    }
    discardRecovery(content_root_, scene_path_);
    std::error_code ec;
    scene_modified_ = std::filesystem::last_write_time(scene_path_, ec);
    rebuildPreview();
  }

  void newScene() {
    finishGizmoDrag(false);
    finishTerrainStroke();
    finishFoliageStroke();
    finishDocumentPropertyEditNow();
    pending_scene_action_ = PendingSceneAction::New;
    pending_scene_path_.clear();
    pending_scene_discard_unsaved_ = false;
    if (hasLocalEdits()) {
      show_unsaved_prompt_ = true;
      return;
    }
    execute_scene_action_pending_ = true;
  }

  void createNewScene(bool discard_unsaved) {
    static const nfdu8filteritem_t filters[] = {{"Karma Scene", "json"}};
    auto selected = saveFileDialog(filters, 1u, content_root_, "untitled.kscene.json");
    if (!selected) return;
    const std::filesystem::path path = ensureSceneExtension(*selected);
    if (!pathIsWithin(content_root_, path)) {
      last_error_ = "Scene must be inside the content root";
      return;
    }
    if (discard_unsaved) {
      discardRecovery(content_root_, scene_path_);
    }
    scene_path_ = path;
    document_ = makeNewDocument(scene_path_, content_root_);
    commands_.clear();
    command_cursor_ = saved_cursor_ = command_bytes_ = 0u;
    saved_state_reachable_ = true;
    has_disk_version_ = false;
    selection_.clear();
    rebuildPreview();
  }

  void openSceneDialog() {
    finishGizmoDrag(false);
    finishTerrainStroke();
    finishFoliageStroke();
    finishDocumentPropertyEditNow();
    static const nfdu8filteritem_t filters[] = {{"Karma Scene", "json"}};
    const auto selected = openFileDialog(filters, 1u, content_root_);
    if (!selected) return;
    if (hasLocalEdits()) {
      pending_scene_action_ = PendingSceneAction::Open;
      pending_scene_path_ = *selected;
      pending_scene_discard_unsaved_ = false;
      show_unsaved_prompt_ = true;
      return;
    }
    queueSceneLoad(*selected, false);
  }

  void queueSceneLoad(const std::filesystem::path& path,
                      bool discard_unsaved) {
    pending_scene_action_ = PendingSceneAction::Open;
    pending_scene_path_ = path;
    pending_scene_discard_unsaved_ = discard_unsaved;
    execute_scene_action_pending_ = true;
  }

  void continuePendingSceneAction() {
    execute_scene_action_pending_ = true;
  }

  void executePendingSceneAction() {
    execute_scene_action_pending_ = false;
    const PendingSceneAction action = pending_scene_action_;
    const std::filesystem::path path = std::move(pending_scene_path_);
    const bool discard_unsaved = pending_scene_discard_unsaved_;
    pending_scene_action_ = PendingSceneAction::None;
    pending_scene_path_.clear();
    pending_scene_discard_unsaved_ = false;
    if (action == PendingSceneAction::New) {
      createNewScene(discard_unsaved);
    } else if (action == PendingSceneAction::Open && !path.empty()) {
      loadScene(path, discard_unsaved);
    }
  }

  void loadScene(const std::filesystem::path& path, bool discard_unsaved = false) {
    if (hasLocalEdits() && !discard_unsaved) {
      last_error_ = "Save or revert the current scene before opening another scene";
      return;
    }
    if (!pathIsWithin(content_root_, path)) {
      last_error_ = "Scene must be inside the content root";
      return;
    }
    scenes::SceneDocument loaded_document{};
    std::string migration_status;
    std::string load_error;
    if (!loadSceneWithEditorMigration(path,
                                      content_root_,
                                      loaded_document,
                                      migration_status,
                                      load_error)) {
      last_error_ = std::move(load_error);
      return;
    }
    if (discard_unsaved) {
      discardRecovery(content_root_, scene_path_);
    }
    scene_path_ = path;
    document_ = std::move(loaded_document);
    document_.reference_root = content_root_;
    commands_.clear();
    command_cursor_ = saved_cursor_ = command_bytes_ = 0u;
    saved_state_reachable_ = true;
    has_disk_version_ = true;
    selection_.clear();
    scene_migration_status_ = std::move(migration_status);
    last_error_.clear();
    rebuildPreview();
    std::error_code ec;
    scene_modified_ = std::filesystem::last_write_time(scene_path_, ec);
  }

  void addAssetRootDialog() {
    auto selected = folderDialog(content_root_);
    if (!selected) return;
    const auto relative = contentRelativePath(content_root_, *selected);
    if (!relative) {
      last_error_ = "Asset roots must be inside the content root";
      return;
    }
    if (std::find(settings_.asset_roots.begin(), settings_.asset_roots.end(), *relative) ==
        settings_.asset_roots.end()) {
      settings_.asset_roots.push_back(*relative);
      saveEditorSettings(content_root_, settings_);
      scanCatalog();
    }
  }

  void refreshAssets(bool rebuild) {
    if (rebuild) bake_stale_ = true;
    scanCatalog(rebuild);
  }

  void updateExternalFiles() {
    const auto now = std::chrono::steady_clock::now();
    if (now < catalog_poll_at_) return;
    catalog_poll_at_ = now + kCatalogPollInterval;
    if (!catalog_.changedFiles().empty()) refreshAssets(true);
    if (!pathExistsNoThrow(scene_path_)) return;
    std::error_code ec;
    const auto modified = std::filesystem::last_write_time(scene_path_, ec);
    if (ec || modified == scene_modified_) return;
    scene_modified_ = modified;
    if (dirty()) {
      scene_conflict_ = true;
    } else {
      loadScene(scene_path_);
    }
  }

  void checkRecoveryAtStartup() {
    std::string error;
    const auto recovery = loadRecovery(content_root_, scene_path_, &error);
    if (!recovery) return;
    std::error_code ec;
    const auto source_time = pathExistsNoThrow(scene_path_)
                                 ? std::filesystem::last_write_time(scene_path_, ec)
                                 : std::filesystem::file_time_type::min();
    if (!ec && recovery->written > source_time) show_recovery_prompt_ = true;
  }

  void updateRecovery() {
    if (!pending_recovery_ || std::chrono::steady_clock::now() < recovery_at_) return;
    pending_recovery_ = false;
    persistTerrainPreview();
    for (auto& layer : foliage_layers_) persistFoliagePreview(layer);
    std::string error;
    if (!writeRecovery(content_root_, scene_path_, scenes::sceneDocumentToJson(document_), &error)) {
      last_error_ = error;
    }
  }

  void restoreRecovery() {
    std::string error;
    auto recovery = loadRecovery(content_root_, scene_path_, &error);
    if (!recovery) {
      last_error_ = error;
      return;
    }
    const std::filesystem::path temporary = recoveryPath(content_root_, scene_path_).string() +
                                            ".restore.kscene.json";
    {
      std::ofstream stream(temporary);
      stream << std::setw(2) << recovery->scene_json << '\n';
      stream.flush();
      if (!stream) {
        last_error_ = "Failed to stage the recovery snapshot";
        return;
      }
    }
    scenes::SceneLoadResult loaded = scenes::loadSceneDocument(
        scenes::SceneLoadDesc{.path = temporary, .reference_root = content_root_});
    std::error_code remove_error;
    std::filesystem::remove(temporary, remove_error);
    if (!loaded.success() || !loaded.document) {
      last_error_ = joinDiagnostics(loaded.diagnostics);
      return;
    }
    scenes::SceneDocument before = document_;
    document_ = *loaded.document;
    document_.source_path = scene_path_;
    document_.reference_root = content_root_;
    pushDocumentCommand("Restore Recovery", std::move(before));
    preview_rebuild_pending_ = true;
  }

  std::filesystem::path editorPreviewDirectory() const {
    return content_root_ / ".karma" / "editor-preview" /
           filenameStemForScene(scene_path_);
  }

  std::string rootEntityId() const {
    const auto root = std::find_if(document_.entities.begin(), document_.entities.end(),
                                   [](const scenes::SceneEntity& entity) {
                                     return entity.parent_id.empty();
                                   });
    return root == document_.entities.end() ? std::string{} : root->id;
  }

  world::Entity selectedRuntimeEntity() const {
    if (selection_.kind == SelectionKind::Entity) {
      const auto it = preview_.entities_by_id.find(selection_.id);
      return it == preview_.entities_by_id.end() ? world::Entity{} : it->second;
    }
    if (selection_.kind == SelectionKind::Prefab) {
      const auto it = preview_.prefab_roots_by_id.find(selection_.id);
      return it == preview_.prefab_roots_by_id.end() ? world::Entity{} : it->second;
    }
    return {};
  }

  void revalidateSelection() {
    if (!selection_.valid()) return;
    const bool authored = selection_.kind == SelectionKind::Entity
                              ? std::any_of(document_.entities.begin(),
                                            document_.entities.end(),
                                            [&](const scenes::SceneEntity& entity) {
                                              return entity.id == selection_.id;
                                            })
                              : std::any_of(
                                    document_.prefab_instances.begin(),
                                    document_.prefab_instances.end(),
                                    [&](const scenes::ScenePrefabInstance& prefab) {
                                      return prefab.id == selection_.id;
                                    });
    if (!authored || !world->isAlive(selectedRuntimeEntity())) {
      selection_.clear();
    }
  }

  std::vector<scenes::SceneEntity>::iterator findEntity(const std::string& id) {
    return std::find_if(document_.entities.begin(), document_.entities.end(),
                        [&](const scenes::SceneEntity& entity) { return entity.id == id; });
  }
  std::vector<scenes::ScenePrefabInstance>::iterator findPrefab(const std::string& id) {
    return std::find_if(document_.prefab_instances.begin(), document_.prefab_instances.end(),
                        [&](const scenes::ScenePrefabInstance& prefab) { return prefab.id == id; });
  }
  FoliageLayerState* findFoliageLayer(const std::string& entity_id) {
    const auto it = std::find_if(foliage_layers_.begin(), foliage_layers_.end(),
                                 [&](const FoliageLayerState& state) {
                                   return state.entity_id == entity_id;
                                 });
    return it == foliage_layers_.end() ? nullptr : &*it;
  }
  const FoliageLayerState* findFoliageLayer(const std::string& entity_id) const {
    const auto it = std::find_if(foliage_layers_.begin(), foliage_layers_.end(),
                                 [&](const FoliageLayerState& state) {
                                   return state.entity_id == entity_id;
                                 });
    return it == foliage_layers_.end() ? nullptr : &*it;
  }
  FoliageLayerState* selectedFoliageLayer() {
    if (selection_.kind != SelectionKind::Entity) return nullptr;
    if (selection_.id == terrain_entity_id_) {
      return findFoliageLayer(settings_.active_foliage_layer_id);
    }
    return findFoliageLayer(selection_.id);
  }

  void focusFoliageLayer(const std::string& entity_id,
                         bool reset_inspector = false) {
    settings_.active_foliage_layer_id = entity_id;
    settings_.terrain_inspector_tab = 2;
    terrain_tab_selection_pending_ = true;
    inspector_scroll_to_top_ |= reset_inspector;
    selection_ = {SelectionKind::Entity,
                  terrain_entity_id_.empty() ? entity_id
                                             : terrain_entity_id_};
  }

  std::filesystem::path executable_directory_;
  std::filesystem::path content_root_;
  std::filesystem::path scene_path_;
  scenes::SceneDocument document_;
  EditorSettings settings_;
  ComponentEditorRegistry component_editors_;
  AssetCatalog catalog_;
  std::vector<std::string> catalog_diagnostics_;
  std::future<CatalogBuild> catalog_future_;
  bool catalog_rebuild_preview_ = false;
  bool catalog_rescan_requested_ = false;
  scenes::SceneInstantiateResult preview_{};
  std::unordered_map<std::string, scenes::SceneTransform>
      prefab_saved_root_transforms_;
  visual::terrain::TerrainRuntimeModule* terrain_runtime_ = nullptr;
  foliage::FoliageRuntimeModule* foliage_runtime_ = nullptr;

  world::Entity editor_camera_{};
  world::Entity terrain_entity_{};
  std::string terrain_entity_id_;
  std::optional<scene_authoring::TerrainCanvas> terrain_canvas_;
  bool terrain_authoring_valid_ = true;
  std::vector<FoliageLayerState> foliage_layers_;

  rendering::RenderTargetId viewport_target_ = rendering::kDefaultRenderTarget;
  int viewport_width_ = 0;
  int viewport_height_ = 0;
  int pending_viewport_width_ = 1280;
  int pending_viewport_height_ = 720;
  ImVec2 viewport_min_{};
  ImVec2 viewport_display_size_{};
  bool viewport_hovered_ = false;
  bool viewport_item_hovered_ = false;
  bool viewport_primary_owned_ = false;
  bool viewport_middle_owned_ = false;
  bool viewport_right_owned_ = false;

  Selection selection_{};
  ToolMode tool_ = ToolMode::Select;
  std::filesystem::path pending_prefab_;
  std::filesystem::path selected_asset_path_;
  std::string selected_asset_key_;
  std::optional<PrefabAssetDraft> prefab_asset_draft_;
  size_t prefab_draft_node_index_ = 0u;
  std::string prefab_asset_draft_error_;
  std::string prefab_asset_draft_status_;
  std::string scene_migration_status_;
  std::optional<rendering::MaterialAssetDesc> material_original_;
  std::optional<rendering::MaterialAssetDesc> material_draft_;
  std::filesystem::path material_draft_path_;
  std::string material_draft_key_;
  std::string material_draft_error_;
  bool material_draft_dirty_ = false;
  std::optional<prefabs::PrefabInstance> placement_preview_;
  scenes::SceneTransform placement_source_transform_{};
  std::optional<math::Vec3> placement_world_point_;
  std::string pending_foliage_mesh_;
  std::filesystem::path pending_foliage_prefab_;
  std::filesystem::path pending_foliage_package_;
  FoliagePrefabInspector foliage_prefab_inspector_;
  char asset_filter_[192]{};
  char hierarchy_filter_[128]{};
  char component_filter_[128]{};
  char console_filter_[128]{};
  std::string inspector_context_key_;
  bool inspector_scroll_to_top_ = false;
  std::shared_ptr<EditorConsoleSink> console_sink_;
  std::shared_ptr<spdlog::logger> console_logger_;
  bool bottom_tab_initialized_ = false;
  std::future<scenes::SceneBakeResult> bake_future_;
  std::shared_ptr<EditorBakeSharedState> bake_shared_;
  std::chrono::steady_clock::time_point bake_started_at_{};
  std::string bake_status_;
  std::string bake_fingerprint_;
  bool bake_stale_ = true;
  BakeScope active_bake_scope_ = BakeScope::All;
  bool open_component_json_ = false;
  bool component_json_adding_ = false;
  std::string component_json_type_;
  std::string component_json_entity_id_;
  std::vector<char> component_json_buffer_;
  std::string component_json_error_;

  ViewportNavigationState camera_navigation_{};
  float construction_plane_y_ = 0.0f;
  bool show_grid_ = true;
  EditorWorkspaceLayout workspace_layout_{};
  float workspace_top_ = 0.0f;
  float workspace_height_ = 0.0f;
  bool panel_item_active_ = false;
  ImFont* editor_semibold_font_ = nullptr;
  bool terrain_tab_selection_pending_ = true;

  ViewportInputSnapshot viewport_input_{};
  GizmoTool gizmo_tool_ = GizmoTool::Move;
  GizmoSpace gizmo_space_ = GizmoSpace::World;
  GizmoHandle gizmo_hot_handle_ = GizmoHandle::None;
  GizmoGeometry gizmo_geometry_{};
  GizmoDragState gizmo_drag_{};
  bool gizmo_active_ = false;
  bool gizmo_hovered_ = false;
  scenes::SceneDocument gizmo_before_{};
  bool property_edit_active_ = false;
  bool property_edit_rebuild_ = false;
  std::string property_edit_label_;
  scenes::SceneDocument property_edit_before_{};

  scene_authoring::TerrainBrush terrain_brush_{};
  int splat_layer_ = 0;
  float flatten_target_ = 0.25f;
  float set_height_target_ = 0.25f;
  bool terrain_stroke_active_ = false;
  bool preview_rebuild_pending_ = false;
  std::string preview_pending_selection_;
  std::optional<ToolMode> preview_pending_tool_;
  std::chrono::steady_clock::time_point terrain_preview_at_{};
  std::vector<float> terrain_before_heights_;
  std::vector<uint8_t> terrain_before_control_;

  foliage::FoliagePaintBrush foliage_brush_{};
  foliage::FoliageEraseBrush foliage_erase_{};
  float foliage_min_height_ = -100000.0f;
  float foliage_max_height_ = 100000.0f;
  uint64_t foliage_stroke_seed_ = 1u;
  bool foliage_stroke_active_ = false;
  std::chrono::steady_clock::time_point foliage_preview_at_{};
  std::string active_foliage_entity_;
  foliage::FoliageEditResult foliage_stroke_edit_{};

  std::vector<EditCommand> commands_;
  size_t command_cursor_ = 0u;
  size_t saved_cursor_ = 0u;
  size_t command_bytes_ = 0u;
  bool saved_state_reachable_ = true;
  bool has_disk_version_ = false;

  std::chrono::steady_clock::time_point catalog_poll_at_{};
  std::chrono::steady_clock::time_point recovery_at_{};
  std::filesystem::file_time_type scene_modified_{};
  bool pending_recovery_ = false;
  bool scene_conflict_ = false;
  bool show_recovery_prompt_ = false;
  bool show_unsaved_prompt_ = false;
  PendingSceneAction pending_scene_action_ = PendingSceneAction::None;
  std::filesystem::path pending_scene_path_;
  bool pending_scene_discard_unsaved_ = false;
  bool save_before_scene_action_pending_ = false;
  bool execute_scene_action_pending_ = false;

  bool open_create_terrain_ = false;
  bool import_heightmap_on_create_ = false;
  int new_terrain_resolution_ = 513;
  float new_terrain_size_ = 512.0f;
  float new_terrain_height_scale_ = 128.0f;
  float new_terrain_height_offset_ = -32.0f;
  bool open_create_foliage_ = false;
  char new_foliage_name_[128] = "Foliage";
  float new_foliage_chunk_size_ = 32.0f;
  float new_foliage_view_distance_ = 256.0f;

  std::string last_error_;
};

}  // namespace karma::tools::scene_editor

int main(int argc, char** argv) {
  using namespace karma;
  using namespace karma::tools::scene_editor;

  const LaunchOptions options = parseLaunchOptions(argc, argv);
  if (options.show_help || !options.valid) {
    std::cout << "Usage: karma_scene_editor [scene.kscene.json] "
                 "[--content-root PATH] [--asset-root PATH ...]\n";
    return options.valid ? 0 : 2;
  }
  if (!pathIsWithin(options.content_root, options.scene_path)) {
    std::cerr << "Scene path must be inside the content root\n";
    return 2;
  }

  EditorSettings settings{};
  std::string settings_error;
  if (!loadEditorSettings(options.content_root, settings, &settings_error)) {
    spdlog::warn("Failed to load scene editor settings: {}", settings_error);
    settings.asset_roots = {"."};
  }
  for (const auto& root : options.extra_asset_roots) {
    const std::filesystem::path candidate =
        root.is_absolute() ? root : options.content_root / root;
    auto relative = contentRelativePath(options.content_root, candidate);
    if (relative && std::find(settings.asset_roots.begin(), settings.asset_roots.end(), *relative) ==
                        settings.asset_roots.end()) {
      settings.asset_roots.push_back(*relative);
    }
  }

  scenes::SceneDocument document{};
  std::string startup_migration_status;
  if (pathExistsNoThrow(options.scene_path)) {
    std::string load_error;
    if (!loadSceneWithEditorMigration(options.scene_path,
                                      options.content_root,
                                      document,
                                      startup_migration_status,
                                      load_error)) {
      std::cerr << load_error << '\n';
      return 1;
    }
    if (!startup_migration_status.empty()) {
      std::cout << startup_migration_status << '\n';
    }
  } else {
    document = makeNewDocument(options.scene_path, options.content_root);
  }

  if (NFD::Init() != NFD_OKAY) {
    spdlog::warn("Native file dialogs could not initialize: {}", NFD::GetError());
  }

  app::EngineApp engine;
  auto terrain_runtime = std::make_unique<visual::terrain::TerrainRuntimeModule>();
  auto* terrain_runtime_ptr = terrain_runtime.get();
  engine.addRuntimeModule(std::move(terrain_runtime));
  auto foliage_runtime = std::make_unique<foliage::FoliageRuntimeModule>();
  auto* foliage_runtime_ptr = foliage_runtime.get();
  engine.addRuntimeModule(std::move(foliage_runtime));

  SceneEditorGame editor(options.executable_directory,
                         options.content_root,
                         options.scene_path,
                         std::move(document),
                         std::move(settings),
                         std::move(startup_migration_status),
                         terrain_runtime_ptr,
                         foliage_runtime_ptr);
  engine.setUi(ui::imgui::createUiLayer(
      [&editor](app::UIContext& context) { editor.drawUi(context); }));

  app::EngineConfig config{};
  config.window.title = "Karma Scene Editor";
  config.window.width = 1600;
  config.window.height = 960;
  config.window.samples = 1;
  config.cursor_visible = true;
  config.enable_anisotropy = true;
  config.anisotropy_level = 16;
  config.generate_mipmaps = true;
  config.shadow_map_size = 2048;
  config.background_color = {0.055f, 0.065f, 0.085f, 1.0f};
  config.loading_splash.enabled = false;
  config.frame_pacing_fps = 60.0f;

  engine.start(editor, config);
  while (engine.isRunning()) engine.tick();
  NFD::Quit();
  return 0;
}
