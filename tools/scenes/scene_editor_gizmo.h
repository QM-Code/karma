#pragma once

#include "scene_editor_viewport.h"

#include "karma/scenes.h"

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

namespace karma::tools::scene_editor {

enum class GizmoTool : uint8_t {
  Move,
  Rotate,
  Scale,
};

enum class GizmoSpace : uint8_t {
  World,
  Local,
};

/// Stable handle identities shared by drawing, picking, and drag constraints.
enum class GizmoHandle : uint8_t {
  None,
  MoveX,
  MoveY,
  MoveZ,
  MoveXY,
  MoveXZ,
  MoveYZ,
  MoveView,
  RotateX,
  RotateY,
  RotateZ,
  RotateView,
  ScaleX,
  ScaleY,
  ScaleZ,
  ScaleUniform,
};

struct GizmoSnapSettings {
  bool enabled = false;
  float translation_step = 1.0f;
  float rotation_step_degrees = 15.0f;
  float scale_step = 0.1f;
};

/// One world-space line ready for GraphicsDevice::drawLine. Gizmo lines are
/// intended to be submitted with depth testing disabled.
struct GizmoLineSegment {
  math::Vec3 start{};
  math::Vec3 end{};
  math::Color color{};
  GizmoHandle handle = GizmoHandle::None;
  float thickness_pixels = 2.0f;
};

/// Projected hit geometry for one logical handle. Segment distance is used for
/// arrows, boxes, and rings; polygon interiors are used for plane/center grips.
struct GizmoHandleGeometry {
  GizmoHandle handle = GizmoHandle::None;
  std::vector<GizmoLineSegment> segments;
  std::vector<math::Vec3> polygon;
  float pick_radius_pixels = 7.0f;
};

struct GizmoGeometry {
  GizmoTool tool = GizmoTool::Move;
  GizmoSpace space = GizmoSpace::World;
  math::Vec3 pivot{};
  std::array<math::Vec3, 3> axes{
      math::Vec3{1.0f, 0.0f, 0.0f},
      math::Vec3{0.0f, 1.0f, 0.0f},
      math::Vec3{0.0f, 0.0f, 1.0f},
  };
  float world_size = 0.0f;
  std::vector<GizmoLineSegment> lines;
  std::vector<GizmoHandleGeometry> handles;
};

struct GizmoBuildDesc {
  GizmoTool tool = GizmoTool::Move;
  GizmoSpace space = GizmoSpace::World;
  scenes::SceneTransform world_transform{};
  ViewportProjection projection{};
  float apparent_size_pixels = 96.0f;
};

/// Builds renderer-neutral, world-space geometry at an approximately constant
/// apparent size. Scale handles always use the selected transform's local axes
/// so applying them cannot introduce shear through a rotated parent.
GizmoGeometry buildGizmoGeometry(const GizmoBuildDesc& desc);

struct GizmoHit {
  GizmoHandle handle = GizmoHandle::None;
  float screen_distance_pixels = 0.0f;
};

/// Hit-tests the projected geometry. Plane and center grips win exact ties,
/// followed by axis handles. No hit is returned outside the viewport rectangle.
std::optional<GizmoHit> hitTestGizmo(const GizmoGeometry& geometry,
                                     const ViewportProjection& projection,
                                     ViewportPoint cursor);

struct GizmoDragBegin {
  GizmoHandle handle = GizmoHandle::None;
  GizmoSpace space = GizmoSpace::World;
  ViewportProjection projection{};
  scenes::SceneTransform local_transform{};
  scenes::SceneTransform world_transform{};
  std::optional<scenes::SceneTransform> parent_world_transform;
  float world_size = 0.0f;
  GizmoSnapSettings snap{};
};

struct GizmoDragUpdate {
  scenes::SceneTransform local_transform{};
  scenes::SceneTransform world_transform{};
  bool changed = false;
};

/// A terminal drag result. `commit` is true at most once per begin/finish
/// cycle, and only for a changed drag. Cancel returns the exact pre-drag state.
struct GizmoDragCompletion {
  scenes::SceneTransform before_local{};
  scenes::SceneTransform before_world{};
  scenes::SceneTransform after_local{};
  scenes::SceneTransform after_world{};
  bool changed = false;
  bool commit = false;
  bool cancelled = false;
};

/// Pure ray/plane transform drag state. Updates are always evaluated from the
/// original transform, which prevents frame-rate-dependent drift and makes
/// snapping deterministic.
class GizmoDragState {
 public:
  bool begin(const GizmoDragBegin& desc, ViewportPoint cursor);
  std::optional<GizmoDragUpdate> update(ViewportPoint cursor);
  std::optional<GizmoDragCompletion> finish();
  std::optional<GizmoDragCompletion> cancel();

  bool active() const { return active_; }
  GizmoHandle handle() const { return desc_.handle; }
  const GizmoDragUpdate& current() const { return current_; }

 private:
  enum class Constraint : uint8_t {
    None,
    Axis,
    Plane,
    Rotation,
    UniformScale,
  };

  GizmoDragBegin desc_{};
  GizmoDragUpdate current_{};
  Constraint constraint_ = Constraint::None;
  std::array<math::Vec3, 3> axes_{};
  math::Vec3 axis_a_{};
  math::Vec3 axis_b_{};
  math::Vec3 plane_normal_{};
  math::Vec3 start_point_{};
  math::Vec3 start_vector_{};
  float start_scalar_ = 0.0f;
  bool axis_uses_plane_ = false;
  bool active_ = false;
  bool completion_emitted_ = false;
};

}  // namespace karma::tools::scene_editor
