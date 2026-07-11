#ifdef NDEBUG
#undef NDEBUG
#endif

#include "scene_editor_colliders.h"

#include <cassert>
#include <cmath>
#include <limits>

namespace {

bool near(float a, float b, float epsilon = 1.0e-4f) {
  return std::abs(a - b) <= epsilon;
}

void requireFinite(const karma::tools::scene_editor::ColliderWireGeometry& geometry) {
  assert(!geometry.empty());
  for (const auto& line : geometry.lines) {
    assert(karma::math::isFinite(line.from));
    assert(karma::math::isFinite(line.to));
  }
}

void testPrimitiveShapes() {
  namespace components = karma::components;
  namespace editor = karma::tools::scene_editor;
  const karma::scenes::SceneTransform transform{
      .position = {10.0f, 2.0f, -4.0f},
      .scale = {2.0f, 3.0f, 4.0f},
  };

  const auto box = editor::buildColliderWireGeometry(
      components::ColliderComponent::box({
          .center = {1.0f, 0.0f, 0.0f},
          .half_extents = {0.5f, 1.0f, 2.0f},
      }),
      transform);
  assert(box.lines.size() == 12u);
  requireFinite(box);
  assert(near(box.lines.front().from.x, 11.0f));

  requireFinite(editor::buildColliderWireGeometry(
      components::ColliderComponent::sphere({.radius = 2.0f}), transform, 12u));
  requireFinite(editor::buildColliderWireGeometry(
      components::ColliderComponent::capsule({.radius = 0.5f, .height = 3.0f}),
      transform, 12u));
  requireFinite(editor::buildColliderWireGeometry(
      components::ColliderComponent::cylinder({.radius = 0.75f, .height = 2.0f}),
      transform, 12u));
  requireFinite(editor::buildColliderWireGeometry(
      components::ColliderComponent::taperedCapsule({
          .top_radius = 0.25f,
          .bottom_radius = 0.75f,
          .height = 2.5f,
      }),
      transform, 12u));
}

void testMeshBoundsAndAdvancedShapes() {
  namespace components = karma::components;
  namespace editor = karma::tools::scene_editor;
  components::MeshColliderShape mesh{};
  mesh.vertices = {{-2.0f, -1.0f, -3.0f}, {4.0f, 5.0f, 6.0f}};
  const auto bounds = editor::buildColliderWireGeometry(
      components::ColliderComponent::mesh(mesh), {});
  assert(bounds.lines.size() == 12u);

  const auto asset_only = editor::buildColliderWireGeometry(
      components::ColliderComponent::mesh({.mesh_asset_key = "mesh.tree"}), {});
  assert(asset_only.lines.size() == 12u);

  assert(editor::buildColliderWireGeometry(
             components::ColliderComponent::convexHull({.points = {{}, {1.0f, 0.0f, 0.0f}}}),
             {})
             .empty());
  assert(editor::buildColliderWireGeometry(
             components::ColliderComponent::heightField({.samples = {0.0f}, .sample_count = 1u}),
             {})
             .empty());
}

void testMalformedInputsAreBounded() {
  namespace components = karma::components;
  namespace editor = karma::tools::scene_editor;
  const float nan = std::numeric_limits<float>::quiet_NaN();
  assert(editor::buildColliderWireGeometry(
             components::ColliderComponent::sphere({.radius = 1.0f}),
             {.position = {nan, 0.0f, 0.0f}})
             .empty());
  const auto finite = editor::buildColliderWireGeometry(
      components::ColliderComponent::sphere({.radius = nan}), {}, 100000u);
  assert(finite.empty());
}

}  // namespace

int main() {
  testPrimitiveShapes();
  testMeshBoundsAndAdvancedShapes();
  testMalformedInputsAreBounded();
  return 0;
}
