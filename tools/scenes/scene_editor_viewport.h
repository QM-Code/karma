#pragma once

#include "karma/math.h"

#include <cstdint>
#include <optional>

namespace karma::tools::scene_editor {

/// A point in top-left-origin viewport screen coordinates.
struct ViewportPoint {
  float x = 0.0f;
  float y = 0.0f;
};

/// Screen-space viewport rectangle. Width and height are measured in pixels.
struct ViewportRect {
  float x = 0.0f;
  float y = 0.0f;
  float width = 0.0f;
  float height = 0.0f;
};

/// Perspective camera data used by all scene-editor projection operations.
struct ViewportCamera {
  math::Vec3 position{};
  math::Quat rotation{};
  float fov_y_degrees = 60.0f;
  float near_clip = 0.1f;
  float far_clip = 1000.0f;
};

/// One projection contract for drawing, picking, markers, and transform handles.
/// Screen coordinates use a top-left origin and do not include a render-target
/// texture flip.
struct ViewportProjection {
  ViewportRect rect{};
  ViewportCamera camera{};
};

struct ViewportCameraBasis {
  math::Vec3 right{};
  math::Vec3 up{};
  math::Vec3 forward{};
};

/// The projected location of a world point. `view_depth` is positive in front
/// of the camera and `ndc_depth` follows OpenGL's [-1, 1] near/far convention.
struct ProjectedPoint {
  ViewportPoint screen{};
  float view_depth = 0.0f;
  float ndc_depth = 0.0f;
  bool inside_viewport = false;
  bool inside_clip = false;
};

struct ViewportRay {
  math::Vec3 origin{};
  math::Vec3 direction{};
};

/// Returns whether every field needed for perspective projection is usable.
bool validViewportProjection(const ViewportProjection& projection);

/// Returns the normalized world-space camera basis, or no value for an invalid
/// camera rotation.
std::optional<ViewportCameraBasis> viewportCameraBasis(
    const ViewportCamera& camera);

/// Projects a world point into the projection rectangle. Points on or behind
/// the camera plane and non-finite inputs return no value. Points outside the
/// viewport or depth range are returned with the corresponding flags cleared.
std::optional<ProjectedPoint> projectWorldToViewport(
    const ViewportProjection& projection,
    const math::Vec3& world_point);

/// Builds a world-space ray from a point in the same top-left screen coordinate
/// system returned by `projectWorldToViewport`.
std::optional<ViewportRay> viewportPointToWorldRay(
    const ViewportProjection& projection,
    ViewportPoint screen_point);

/// Returns the world-space height represented by one vertical viewport pixel at
/// `world_point`. Unprojectable or behind-camera points return zero.
float worldUnitsPerViewportPixel(const ViewportProjection& projection,
                                 const math::Vec3& world_point);

enum class ViewportNavigationMode : uint8_t {
  None,
  Orbit,
  Pan,
  Dolly,
  Fly,
};

struct ViewportNavigationButtons {
  bool alt = false;
  bool left = false;
  bool middle = false;
  bool right = false;
};

/// Resolves Unity Scene-view mouse chords into one exclusive navigation mode.
ViewportNavigationMode unityViewportNavigationMode(
    const ViewportNavigationButtons& buttons);

/// Pure state for Unity-style perspective Scene-view navigation. Outside fly
/// mode the camera position is derived from pivot, distance, yaw, and pitch.
/// During fly mode `fly_position` is authoritative.
struct ViewportNavigationState {
  ViewportNavigationMode mode = ViewportNavigationMode::None;
  math::Vec3 pivot{0.0f, 2.0f, 0.0f};
  float distance = 38.0f;
  float yaw = 0.7853982f;
  float pitch = -0.42f;
  math::Vec3 fly_position{};
  float fly_speed = 8.0f;
};

struct ViewportCameraPose {
  math::Vec3 position{};
  math::Quat rotation{};
};

struct ViewportFlyMotion {
  float right = 0.0f;
  float up = 0.0f;
  float forward = 0.0f;
};

/// Returns the camera pose represented by the current navigation state.
ViewportCameraPose viewportCameraPose(
    const ViewportNavigationState& navigation);

/// Transitions between navigation modes. Entering fly mode snapshots the orbit
/// position. Leaving fly mode rebuilds the orbit pivot in front of the flown
/// camera so the following orbit or dolly cannot jump.
void setViewportNavigationMode(ViewportNavigationState& navigation,
                               ViewportNavigationMode mode);

/// Applies top-left mouse motion to Orbit or Fly. Dragging right turns/orbits
/// right; dragging upward looks/orbits upward.
bool applyViewportLookDrag(ViewportNavigationState& navigation,
                           float mouse_delta_x,
                           float mouse_delta_y,
                           float sensitivity);

/// Applies direct-manipulation MMB panning. Dragging right/down moves visible
/// scene content right/down. The scaling is perspective-correct at the pivot.
bool applyViewportPanDrag(ViewportNavigationState& navigation,
                          float mouse_delta_x,
                          float mouse_delta_y,
                          float viewport_height,
                          float fov_y_degrees,
                          float pan_speed = 1.0f);

/// Applies Alt+RMB vertical dolly. An upward drag (negative Y delta) moves in.
bool applyViewportDollyDrag(ViewportNavigationState& navigation,
                            float mouse_delta_y,
                            float sensitivity = 0.01f,
                            float min_distance = 0.1f,
                            float max_distance = 100000.0f);

/// Applies a wheel tick. Outside Fly it dollies toward the pivot; in Fly it
/// adjusts fly speed without moving the camera.
bool applyViewportWheel(ViewportNavigationState& navigation,
                        float wheel_delta,
                        float dolly_base = 0.85f,
                        float fly_speed_base = 1.2f,
                        float min_distance = 0.1f,
                        float max_distance = 100000.0f,
                        float min_fly_speed = 0.01f,
                        float max_fly_speed = 100000.0f);

/// Applies normalized RMB flythrough motion. Forward/right follow the camera;
/// up follows world +Y. Shift-style fast motion uses `fast_multiplier`.
bool applyViewportFlyMotion(ViewportNavigationState& navigation,
                            const ViewportFlyMotion& motion,
                            float delta_time,
                            bool fast,
                            float fast_multiplier = 4.0f);

/// Frames a spherical selection and preserves the current viewing direction.
/// `radius` may be zero for a point selection.
bool frameViewportCamera(ViewportNavigationState& navigation,
                         const math::Vec3& target,
                         float radius,
                         float fov_y_degrees,
                         float padding = 1.2f,
                         float min_distance = 0.1f,
                         float max_distance = 100000.0f);

}  // namespace karma::tools::scene_editor
