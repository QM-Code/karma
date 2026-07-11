#include "scene_editor_placement.h"

#include <algorithm>
#include <cmath>

namespace karma::tools::scene_editor {

math::Vec3 snapPrefabPlacementPoint(const math::Vec3& world_point,
                                    bool snap_enabled,
                                    float grid_size) {
  if (!math::isFinite(world_point) || !snap_enabled ||
      !std::isfinite(grid_size) || grid_size <= 0.0f) {
    return world_point;
  }
  const auto snap = [grid_size](float value) {
    return std::round(value / grid_size) * grid_size;
  };
  return {snap(world_point.x), world_point.y, snap(world_point.z)};
}

std::string selectedEditableGroupId(const scenes::SceneDocument& document,
                                    const Selection& selection) {
  if (selection.kind != SelectionKind::Entity || selection.id.empty()) return {};
  const auto entity = std::find_if(
      document.entities.begin(), document.entities.end(),
      [&](const scenes::SceneEntity& candidate) {
        return candidate.id == selection.id;
      });
  if (entity == document.entities.end() || !entity->components.is_object()) {
    return {};
  }
  const bool only_static_membership =
      entity->components.size() == 1u &&
      entity->components.contains("StaticComponent");
  if (!entity->components.empty() && !only_static_membership) return {};
  if (std::any_of(document.cameras.begin(), document.cameras.end(),
                  [&](const scenes::SceneCamera& camera) {
                    return camera.entity_id == selection.id;
                  }) ||
      std::any_of(document.lights.begin(), document.lights.end(),
                  [&](const scenes::SceneLight& light) {
                    return light.entity_id == selection.id;
                  }) ||
      (document.environment.has_value() &&
       document.environment->entity_id == selection.id) ||
      std::any_of(document.static_components.begin(), document.static_components.end(),
                  [&](const scenes::SceneStaticComponent& component) {
                    return component.entity_id == selection.id;
                  }) ||
      std::any_of(document.bakes.begin(), document.bakes.end(),
                  [&](const scenes::SceneBakeDesc& bake) {
                    return bake.baked_lighting.entity_id == selection.id;
                  })) {
    return {};
  }
  return entity->id;
}

std::optional<scenes::SceneTransform> linkedPrefabLocalTransform(
    const scenes::SceneDocument& document,
    std::string_view parent_entity_id,
    const scenes::SceneTransform& world_transform,
    std::string* diagnostic) {
  if (diagnostic != nullptr) diagnostic->clear();
  if (!math::isFinite(world_transform.position) ||
      !math::isFinite(world_transform.rotation) ||
      !math::isFinite(world_transform.scale)) {
    if (diagnostic != nullptr) *diagnostic = "prefab placement transform is not finite";
    return std::nullopt;
  }
  if (parent_entity_id.empty()) return world_transform;
  const Selection parent{SelectionKind::Entity, std::string(parent_entity_id)};
  std::string parent_error;
  const auto parent_world = sceneWorldTransform(document, parent, &parent_error);
  if (!parent_world.has_value()) {
    if (diagnostic != nullptr) {
      *diagnostic = parent_error.empty()
                        ? "prefab placement parent is unavailable"
                        : std::move(parent_error);
    }
    return std::nullopt;
  }
  return sceneTransformRelativeTo(*parent_world, world_transform);
}

}  // namespace karma::tools::scene_editor
