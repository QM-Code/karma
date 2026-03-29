#pragma once

#include <optional>
#include <string_view>
#include <vector>

#include "karma/ecs/world.h"
#include "karma/prefabs/prefab.h"
#include "karma/prefabs/prefab_registry.h"

namespace karma::demo {

inline constexpr std::string_view kExplosionPrefabKey = "explosion";

enum class ExplosionFlipbookTextureSource {
  Unknown,
  ExrSequence,
  LegacySheet,
  ProceduralAtlas,
};

struct ExplosionPrefabPackageDebugInfo {
  ExplosionFlipbookTextureSource core_flipbook_source =
      ExplosionFlipbookTextureSource::Unknown;
  ExplosionFlipbookTextureSource smoke_flipbook_source =
      ExplosionFlipbookTextureSource::Unknown;
};

struct ExplosionPrefabController {
  struct ScheduledRestart {
    ecs::Entity entity{};
    float trigger_time = 0.0f;
  };

  prefabs::PrefabInstance instance{};
  ecs::Entity flash{};
  ecs::Entity fireball{};
  ecs::Entity heat{};
  ecs::Entity core_flipbook{};
  ecs::Entity smoke_flipbook{};
  ecs::Entity embers{};
  ecs::Entity shock_ring{};
  ecs::Entity debris{};
  ecs::Entity dust_ring{};
  ecs::Entity smoke{};
  ecs::Entity scorch{};
  ecs::Entity light{};
  math::Color light_peak_color{};
  float light_peak_intensity = 0.0f;
  float light_peak_range = 0.0f;
  float light_off_range = 0.1f;
  bool light_active = false;
  float light_start_time = 0.0f;
  float light_end_time = 0.0f;
  std::vector<ScheduledRestart> scheduled_restarts;
};

bool registerExplosionPrefabPackage(prefabs::PrefabRegistry& registry);

std::optional<ExplosionPrefabController> instantiateExplosionPrefabController(
    ecs::World& world,
    prefabs::PrefabRegistry& registry,
    const prefabs::PrefabInstantiateDesc& desc = {});

void triggerExplosionPrefab(ecs::World& world,
                            ExplosionPrefabController& controller,
                            float time_seconds);

void updateExplosionPrefab(ecs::World& world,
                           ExplosionPrefabController& controller,
                           float time_seconds);

bool destroyExplosionPrefabController(ecs::World& world,
                                      ExplosionPrefabController& controller);

ExplosionPrefabPackageDebugInfo getExplosionPrefabPackageDebugInfo();
std::string_view explosionFlipbookTextureSourceName(
    ExplosionFlipbookTextureSource source);

}  // namespace karma::demo
