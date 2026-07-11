#pragma once

#include "scene_editor_model.h"

#include <optional>
#include <string>

namespace karma::tools::scene_editor {

/// Snaps horizontal placement coordinates to the construction grid. Surface Y
/// is intentionally preserved so callers can resample terrain after snapping.
math::Vec3 snapPrefabPlacementPoint(const math::Vec3& world_point,
                                    bool snap_enabled,
                                    float grid_size);

/// Returns the selected entity id only when it represents an editable group.
/// Render/component-bearing entities and special scene records are not valid
/// placement parents.
std::string selectedEditableGroupId(const scenes::SceneDocument& document,
                                    const Selection& selection);

/// Converts a desired world-space prefab transform into the local transform
/// stored by a linked scene instance. Empty parent ids keep world-space values.
std::optional<scenes::SceneTransform> linkedPrefabLocalTransform(
    const scenes::SceneDocument& document,
    std::string_view parent_entity_id,
    const scenes::SceneTransform& world_transform,
    std::string* diagnostic = nullptr);

}  // namespace karma::tools::scene_editor
