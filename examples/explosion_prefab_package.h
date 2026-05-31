#pragma once

#include "demo_asset_paths.h"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "karma/world/ecs/world.h"
#include "karma/content/prefabs/prefab.h"
#include "karma/content/prefabs/prefab_runtime.h"
#include "karma/content/prefabs/prefab_registry.h"
#include "karma/rendering/renderer/ids.h"

namespace karma::demo {

inline constexpr std::string_view kExplosionPrefabKey = "explosion";
inline constexpr std::string_view kExplosionPackageAssetRoot = "prefabs/explosion";

inline constexpr std::string_view kExplosionEffectFlash = "prefabs/explosion/flash";
inline constexpr std::string_view kExplosionEffectFireball =
    "prefabs/explosion/fireball";
inline constexpr std::string_view kExplosionEffectSmoke = "prefabs/explosion/smoke";
inline constexpr std::string_view kExplosionEffectHeat = "prefabs/explosion/heat";
inline constexpr std::string_view kExplosionEffectShockRing =
    "prefabs/explosion/shock_ring";
inline constexpr std::string_view kExplosionEffectDustRing =
    "prefabs/explosion/dust_ring";
inline constexpr std::string_view kExplosionEffectScorch = "prefabs/explosion/scorch";
inline constexpr std::string_view kExplosionEffectDebris = "prefabs/explosion/debris";
inline constexpr std::string_view kExplosionEffectEmbers = "prefabs/explosion/embers";
inline constexpr std::string_view kExplosionEffectCoreFlipbook =
    "prefabs/explosion/core_flipbook";
inline constexpr std::string_view kExplosionEffectSmokeFlipbook =
    "prefabs/explosion/smoke_flipbook";

inline constexpr std::string_view kExplosionTextureSpark =
    "prefabs/explosion/spark_atlas";
inline constexpr std::string_view kExplosionTextureGlow =
    "prefabs/explosion/glow_atlas";
inline constexpr std::string_view kExplosionTextureSmoke =
    "prefabs/explosion/smoke_atlas";
inline constexpr std::string_view kExplosionTextureHeat =
    "prefabs/explosion/heat_atlas";
inline constexpr std::string_view kExplosionTextureDustRing =
    "prefabs/explosion/dust_ring_atlas";
inline constexpr std::string_view kExplosionTextureShockRing =
    "prefabs/explosion/shock_ring_atlas";
inline constexpr std::string_view kExplosionTextureScorch =
    "prefabs/explosion/scorch_atlas";
inline constexpr std::string_view kExplosionTextureDebris =
    "prefabs/explosion/debris_atlas";
inline constexpr std::string_view kExplosionTextureCoreFlipbook =
    "prefabs/explosion/explosion00_flipbook";
inline constexpr std::string_view kExplosionTextureSmokeFlipbook =
    "prefabs/explosion/explosion01_smoke_flipbook";

inline std::filesystem::path explosionPackageAssetPath(std::string_view relative_path) {
  std::filesystem::path path =
      resolveExampleAssetPath(std::string(kExplosionPackageAssetRoot));
  if (!relative_path.empty()) {
    path /= std::filesystem::path(std::string(relative_path));
  }
  return path;
}

enum class ExplosionFlipbookSourceMode {
  Fast,
  Exr,
  Auto,
};

enum class ExplosionFlipbookTextureSource {
  Unknown,
  ExrSequence,
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

std::string_view explosionFlipbookSourceModeName(ExplosionFlipbookSourceMode mode);
ExplosionFlipbookSourceMode parseExplosionFlipbookSourceMode();
bool explosionFlipbookRebuildRequested();

renderer::TextureId buildExplosionPackageFireExrFlipbook(
    renderer::GraphicsDevice& graphics,
    bool rebuild_cache);
renderer::TextureId buildExplosionPackageSmokeExrFlipbook(
    renderer::GraphicsDevice& graphics,
    bool rebuild_cache);

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
