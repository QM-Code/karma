#pragma once

#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "karma/world/components/beam_path.h"
#include "karma/world/components/transform.h"
#include "karma/world/ecs/world.h"

namespace karma::beams {

struct BeamPathEntityDesc {
  std::string_view name;
  components::TransformComponent transform{};
  components::BeamPathComponent beam{};
};

inline bool bindBeamPath(ecs::World& world,
                         ecs::Entity entity,
                         components::BeamPathComponent beam) {
  if (!world.isAlive(entity)) {
    return false;
  }
  world.add(entity, std::move(beam));
  return true;
}

inline ecs::Entity createBeamPathEntity(ecs::World& world, const BeamPathEntityDesc& desc) {
  ecs::Entity entity = world.createEntity();
  if (!desc.name.empty()) {
    world.setName(entity, std::string(desc.name));
  }
  world.add(entity, desc.transform);
  world.add(entity, desc.beam);
  return entity;
}

inline bool setBeamPathPoints(ecs::World& world,
                              ecs::Entity entity,
                              std::vector<math::Vec3> points) {
  if (!world.isAlive(entity) || !world.has<components::BeamPathComponent>(entity)) {
    return false;
  }
  world.get<components::BeamPathComponent>(entity).points = std::move(points);
  return true;
}

inline bool setBeamPathVisible(ecs::World& world, ecs::Entity entity, bool visible) {
  if (!world.isAlive(entity) || !world.has<components::BeamPathComponent>(entity)) {
    return false;
  }
  world.get<components::BeamPathComponent>(entity).visible = visible;
  return true;
}

inline bool setBeamPathColors(ecs::World& world,
                              ecs::Entity entity,
                              const math::Color& core_color,
                              const math::Color& glow_color) {
  if (!world.isAlive(entity) || !world.has<components::BeamPathComponent>(entity)) {
    return false;
  }
  auto& beam = world.get<components::BeamPathComponent>(entity);
  beam.core_color = core_color;
  beam.glow_color = glow_color;
  return true;
}

}  // namespace karma::beams
