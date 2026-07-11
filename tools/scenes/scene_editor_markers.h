#pragma once

#include "scene_editor_model.h"
#include "scene_editor_viewport.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace karma::tools::scene_editor {

/// Marker shapes for authored objects that do not otherwise have dependable
/// render geometry in the scene viewport.
enum class SceneMarkerKind : uint8_t {
  PointLight,
  SpotLight,
  DirectionalLight,
  EmptyEntity,
  PrefabRoot,
  EnvironmentAnchor,
};

/// One classified, world-space scene marker. Light-only fields are zero for
/// non-light markers.
struct SceneMarker {
  Selection selection;
  SceneMarkerKind kind = SceneMarkerKind::EmptyEntity;
  scenes::SceneTransform world_transform{};
  math::Color color{};
  float range = 0.0f;
  float outer_cone_degrees = 0.0f;
  bool selected = false;
};

struct SceneMarkerClassificationResult {
  std::vector<SceneMarker> markers;
  std::vector<std::string> diagnostics;

  bool success() const { return diagnostics.empty(); }
};

/// Returns whether the entity is expected to contribute visible renderer
/// geometry. Light, environment, camera, physics, and grouping data alone are
/// intentionally not considered renderable.
bool sceneEntityHasRenderableContent(const scenes::SceneDocument& document,
                                     const scenes::SceneEntity& entity);

/// Classifies authored lights, environments, empty entities, and prefab roots
/// and resolves their composed transforms. Invalid hierarchy entries are
/// skipped and reported without invalidating otherwise usable markers.
SceneMarkerClassificationResult classifySceneMarkers(
    const scenes::SceneDocument& document,
    const Selection& selected = {});

/// Selected markers remain visible when the global marker toggle is disabled.
bool sceneMarkerVisible(const SceneMarker& marker, bool markers_visible);

/// Overlay lines are rendered without depth testing. Bounds lines represent
/// selected light influence and should be rendered with normal depth testing.
enum class SceneMarkerLineLayer : uint8_t {
  Overlay,
  Bounds,
};

struct SceneMarkerLine {
  math::Vec3 from{};
  math::Vec3 to{};
  math::Color color{};
  SceneMarkerLineLayer layer = SceneMarkerLineLayer::Overlay;
};

struct SceneMarkerGeometryOptions {
  /// Desired apparent radius of the marker's base shape.
  float icon_radius_pixels = 12.0f;
  uint32_t circle_segments = 24u;
};

struct SceneMarkerGeometry {
  math::Vec3 pivot{};
  float icon_world_radius = 0.0f;
  std::vector<SceneMarkerLine> lines;

  bool empty() const { return lines.empty(); }
};

/// Builds renderer-agnostic line geometry. Marker icon lines have constant
/// apparent size and use the overlay layer; selected point/spot bounds retain
/// their authored world-space range and use the depth-tested bounds layer.
SceneMarkerGeometry buildSceneMarkerGeometry(
    const SceneMarker& marker,
    const ViewportProjection& projection,
    const SceneMarkerGeometryOptions& options = {});

struct SceneMarkerHit {
  size_t marker_index = 0u;
  Selection selection;
  SceneMarkerKind kind = SceneMarkerKind::EmptyEntity;
  float screen_distance_pixels = 0.0f;
  float view_depth = 0.0f;
};

/// Picks marker pivots with a constant screen-space radius. Hidden unselected
/// markers, markers behind the camera, and markers outside the clip/viewport
/// rectangles are rejected. Screen distance wins ties, then nearest depth.
std::optional<SceneMarkerHit> pickSceneMarker(
    std::span<const SceneMarker> markers,
    const ViewportProjection& projection,
    ViewportPoint cursor,
    bool markers_visible,
    float pick_radius_pixels = 16.0f);

}  // namespace karma::tools::scene_editor
