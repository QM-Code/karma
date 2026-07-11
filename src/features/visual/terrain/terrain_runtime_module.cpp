#include "karma/visual.h"

#include <cassert>
#include <utility>

namespace karma::visual::terrain {
namespace {

uint64_t runtimeEntityKey(world::Entity entity) {
  return (static_cast<uint64_t>(entity.index) << 32u) |
         static_cast<uint64_t>(entity.generation);
}

}  // namespace

TerrainRuntimeModule::TerrainRuntimeModule() = default;

TerrainRuntimeModule::~TerrainRuntimeModule() = default;

void TerrainRuntimeModule::assertThreadAffinity() const {
  assert((system_ == nullptr ||
          attached_thread_ == std::this_thread::get_id()) &&
         "TerrainRuntimeModule must be accessed from the runtime thread once attached");
}

bool TerrainRuntimeModule::setSingleImageTileOverride(
    const world::World& world,
    world::Entity entity,
    rendering::TerrainTileData tile) {
  assertThreadAffinity();
  if (!world.isAlive(entity) ||
      !world.has<components::TerrainComponent>(entity) ||
      world.get<components::TerrainComponent>(entity).source !=
          components::TerrainSourceType::SingleImage ||
      !tile.valid()) {
    return false;
  }
  if (system_) {
    return system_->setSingleImageTileOverride(world, entity, std::move(tile));
  }
  pending_tile_overrides_by_world_[world.instanceId()][runtimeEntityKey(entity)] =
      PendingTileOverride{
          .entity = entity,
          .tile = std::move(tile),
      };
  return true;
}

bool TerrainRuntimeModule::setSingleImageTileOverride(
    world::Entity entity,
    rendering::TerrainTileData tile) {
  assertThreadAffinity();
  if (!entity.isValid() || !tile.valid()) {
    return false;
  }
  if (system_) {
    return system_->setSingleImageTileOverride(entity, std::move(tile));
  }
  pending_tile_overrides_by_world_[0u][runtimeEntityKey(entity)] =
      PendingTileOverride{
          .entity = entity,
          .tile = std::move(tile),
      };
  return true;
}

void TerrainRuntimeModule::clearSingleImageTileOverride(
    const world::World& world,
    world::Entity entity) {
  assertThreadAffinity();
  if (!entity.isValid()) {
    return;
  }
  const auto world_it =
      pending_tile_overrides_by_world_.find(world.instanceId());
  if (world_it != pending_tile_overrides_by_world_.end()) {
    world_it->second.erase(runtimeEntityKey(entity));
    if (world_it->second.empty()) {
      pending_tile_overrides_by_world_.erase(world_it);
    }
  }
  if (system_) {
    system_->clearSingleImageTileOverride(world, entity);
  }
}

void TerrainRuntimeModule::clearSingleImageTileOverride(world::Entity entity) {
  assertThreadAffinity();
  if (!entity.isValid()) {
    return;
  }
  const auto pending = pending_tile_overrides_by_world_.find(0u);
  if (pending != pending_tile_overrides_by_world_.end()) {
    pending->second.erase(runtimeEntityKey(entity));
    if (pending->second.empty()) {
      pending_tile_overrides_by_world_.erase(pending);
    }
  }
  if (system_) {
    system_->clearSingleImageTileOverride(entity);
  }
}

void TerrainRuntimeModule::applyPendingTileOverrides(world::World& world) {
  assertThreadAffinity();
  if (!system_) {
    return;
  }
  const auto apply = [&](uint64_t world_id) {
    const auto pending = pending_tile_overrides_by_world_.find(world_id);
    if (pending == pending_tile_overrides_by_world_.end()) {
      return;
    }
    for (auto it = pending->second.begin(); it != pending->second.end();) {
      const bool ready =
          world.isAlive(it->second.entity) &&
          world.has<components::TerrainComponent>(it->second.entity) &&
          world.get<components::TerrainComponent>(it->second.entity).source ==
              components::TerrainSourceType::SingleImage;
      if (!ready) {
        ++it;
        continue;
      }
      if (system_->setSingleImageTileOverride(
              world, it->second.entity, std::move(it->second.tile))) {
        it = pending->second.erase(it);
      } else {
        ++it;
      }
    }
    if (pending->second.empty()) {
      pending_tile_overrides_by_world_.erase(pending);
    }
  };
  apply(0u);
  apply(world.instanceId());
}

void TerrainRuntimeModule::onAttach(const app::RuntimeModuleContext& context) {
  assertThreadAffinity();
  attached_thread_ = std::this_thread::get_id();
  system_.reset();
  system_ = std::make_unique<TerrainSystem>(context.graphics, context.assets);
}

void TerrainRuntimeModule::onDetach() {
  assertThreadAffinity();
  system_.reset();
  attached_thread_ = {};
}

void TerrainRuntimeModule::onFrameBegin(world::World& world, float) {
  assertThreadAffinity();
  applyPendingTileOverrides(world);
  if (system_) {
    system_->syncTerrainColliders(world);
  }
}

void TerrainRuntimeModule::onUpdate(world::World& world,
                                    float dt,
                                    float interpolation_alpha) {
  assertThreadAffinity();
  applyPendingTileOverrides(world);
  if (system_) {
    system_->update(world, dt, interpolation_alpha);
  }
}

}  // namespace karma::visual::terrain
