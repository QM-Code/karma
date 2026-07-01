#include "particle_effect_tools.h"

#include "karma/assets.h"
#include "karma/components.h"
#include "karma/prefabs.h"
#include "karma/world.h"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

#include <nlohmann/json.hpp>

#define KARMA_REQUIRE(expr)                                                         \
  do {                                                                              \
    if (!(expr)) {                                                                  \
      std::cerr << "Requirement failed: " #expr " at " << __FILE__ << ":"         \
                << __LINE__ << "\n";                                               \
      std::abort();                                                                 \
    }                                                                               \
  } while (false)

namespace {

using Json = nlohmann::json;

std::filesystem::path makeTempDir() {
  const auto ticks = std::chrono::steady_clock::now().time_since_epoch().count();
  std::filesystem::path dir =
      std::filesystem::temp_directory_path() /
      ("karma_particle_generation_tests_" + std::to_string(ticks));
  std::filesystem::create_directories(dir);
  return dir;
}

void writeJson(const std::filesystem::path& path, const Json& json) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream stream(path);
  KARMA_REQUIRE(stream);
  stream << json.dump(2) << '\n';
}

void testPreset(const std::filesystem::path& root,
                const std::string& preset,
                std::size_t expected_beams,
                std::size_t min_effects) {
  const std::filesystem::path spec_path = root / (preset + ".kpspec.json");
  const std::filesystem::path output_dir = root / preset;
  const std::string ns = "generated/test_" + preset;
  writeJson(spec_path,
            Json{
                {"version", 1},
                {"preset", preset},
                {"namespace", ns},
                {"name", "Test " + preset},
                {"length", 5.0f},
                {"radius", 1.1f},
            });

  std::string diagnostic;
  KARMA_REQUIRE(karma::tools::particles::generateParticleEffectPackage(spec_path,
                                                                       output_dir,
                                                                       &diagnostic));

  std::ifstream manifest_stream(output_dir / "assets.package.json");
  KARMA_REQUIRE(manifest_stream);
  Json manifest;
  manifest_stream >> manifest;
  KARMA_REQUIRE(manifest.is_object());
  KARMA_REQUIRE(manifest["assets"].is_array());
  for (const Json& asset : manifest["assets"]) {
    KARMA_REQUIRE(asset.is_object());
    KARMA_REQUIRE(asset["key"].is_string());
    KARMA_REQUIRE(asset["path"].is_string());
    const std::string key = asset["key"].get<std::string>();
    const std::string path = asset["path"].get<std::string>();
    KARMA_REQUIRE(key.starts_with(ns + "/"));
    KARMA_REQUIRE(karma::assets::AssetRegistry::isValidAssetKey(key));
    KARMA_REQUIRE(std::filesystem::exists(output_dir / path));
  }

  karma::assets::AssetRegistry assets;
  karma::prefabs::bindPrefabAssetRegistry(&assets);
  karma::world::World world;
  karma::world::Scene scene;
  const auto instance = karma::prefabs::instantiatePrefab(world, scene, output_dir);
  KARMA_REQUIRE(instance.has_value());
  KARMA_REQUIRE(instance->valid());

  std::size_t beam_count = 0u;
  std::size_t effect_count = 0u;
  for (const karma::world::Entity entity : instance->entities) {
    if (world.has<karma::components::ParticleBeamComponent>(entity)) {
      beam_count += 1u;
      const auto& beam = world.get<karma::components::ParticleBeamComponent>(entity);
      KARMA_REQUIRE(beam.local_path_points.size() >= 2u);
      KARMA_REQUIRE(beam.start_width > 0.0f);
      KARMA_REQUIRE(beam.end_width > 0.0f);
      KARMA_REQUIRE(assets.findTextureAsset(beam.texture_key) != nullptr);
    }
    if (world.has<karma::components::ParticleEffectComponent>(entity)) {
      effect_count += 1u;
      const auto& effect = world.get<karma::components::ParticleEffectComponent>(entity);
      const auto* effect_asset = assets.findParticleEffect(effect.effect_key);
      KARMA_REQUIRE(effect_asset != nullptr);
      for (const auto& emitter : effect_asset->emitters) {
        KARMA_REQUIRE(assets.findTextureAsset(emitter.texture_key) != nullptr);
      }
    }
  }
  KARMA_REQUIRE(beam_count == expected_beams);
  KARMA_REQUIRE(effect_count >= min_effects);

  if (preset == "energy_orb") {
    KARMA_REQUIRE(effect_count == 4u);
    const karma::world::Entity shell = instance->find("shell");
    KARMA_REQUIRE(shell.isValid());
    KARMA_REQUIRE(world.has<karma::components::MeshComponent>(shell));
    KARMA_REQUIRE(world.get<karma::components::MeshComponent>(shell).mesh_asset_key ==
                  ns + "/orb_shell");
    KARMA_REQUIRE(assets.findMeshAsset(ns + "/orb_shell") != nullptr);
    const karma::world::Entity glow = instance->find("glow");
    KARMA_REQUIRE(glow.isValid());
    KARMA_REQUIRE(world.has<karma::components::LightComponent>(glow));
    KARMA_REQUIRE(instance->find("core").isValid());
    KARMA_REQUIRE(instance->find("arcs").isValid());
    KARMA_REQUIRE(instance->find("halo").isValid());
    KARMA_REQUIRE(instance->find("distortion").isValid());
  }

  if (preset == "arcane_barrage") {
    KARMA_REQUIRE(effect_count >= 5u);
    KARMA_REQUIRE(instance->find("missile_1_core").isValid());
    KARMA_REQUIRE(instance->find("missile_1_haze").isValid());
    KARMA_REQUIRE(instance->find("missile_6_core").isValid());
    KARMA_REQUIRE(instance->find("missile_6_haze").isValid());
    KARMA_REQUIRE(instance->find("caster_flare").isValid());
    KARMA_REQUIRE(instance->find("missile_heads").isValid());
    KARMA_REQUIRE(instance->find("trail_sparks").isValid());
    KARMA_REQUIRE(instance->find("trail_mist").isValid());
    KARMA_REQUIRE(instance->find("trail_distortion").isValid());
  }

  if (preset == "blade_barrier") {
    KARMA_REQUIRE(effect_count >= 6u);
    KARMA_REQUIRE(instance->find("wind_ring_1").isValid());
    KARMA_REQUIRE(instance->find("wind_ring_8").isValid());
    KARMA_REQUIRE(instance->find("blade_shell").isValid());
    KARMA_REQUIRE(instance->find("blade_glints").isValid());
    KARMA_REQUIRE(instance->find("dust_shroud").isValid());
    KARMA_REQUIRE(instance->find("wind_shroud").isValid());
    KARMA_REQUIRE(instance->find("blade_swishes").isValid());
    KARMA_REQUIRE(instance->find("blade_distortion").isValid());
    const karma::world::Entity glow = instance->find("steel_glow");
    KARMA_REQUIRE(glow.isValid());
    KARMA_REQUIRE(world.has<karma::components::LightComponent>(glow));

    auto require_orbiting_blades = [&](const std::string& effect_name) {
      const auto* effect_asset = assets.findParticleEffect(ns + "/" + effect_name);
      KARMA_REQUIRE(effect_asset != nullptr);
      KARMA_REQUIRE(!effect_asset->emitters.empty());
      for (const auto& emitter_desc : effect_asset->emitters) {
        const auto& emitter = emitter_desc.emitter;
        KARMA_REQUIRE(emitter.orbit_axis.x == 0.0f);
        KARMA_REQUIRE(emitter.orbit_axis.y == 1.0f);
        KARMA_REQUIRE(emitter.orbit_axis.z == 0.0f);
        KARMA_REQUIRE(emitter.orbit_speed > 0.0f);
        KARMA_REQUIRE(emitter.velocity_min.x == 0.0f);
        KARMA_REQUIRE(emitter.velocity_min.y == 0.0f);
        KARMA_REQUIRE(emitter.velocity_min.z == 0.0f);
        KARMA_REQUIRE(emitter.velocity_max.x == 0.0f);
        KARMA_REQUIRE(emitter.velocity_max.y == 0.0f);
        KARMA_REQUIRE(emitter.velocity_max.z == 0.0f);
        KARMA_REQUIRE(emitter.radial_speed_min == 0.0f);
        KARMA_REQUIRE(emitter.radial_speed_max == 0.0f);
      }
    };
    require_orbiting_blades("blade_shell");
    require_orbiting_blades("blade_glints");
    require_orbiting_blades("blade_swishes");
  }

  if (preset == "chromatic_ray") {
    KARMA_REQUIRE(effect_count >= 4u);
    KARMA_REQUIRE(instance->find("chromatic_core").isValid());
    KARMA_REQUIRE(instance->find("chromatic_ribbon").isValid());
    KARMA_REQUIRE(instance->find("chromatic_haze").isValid());
    KARMA_REQUIRE(instance->find("red_fire_thread").isValid());
    KARMA_REQUIRE(instance->find("orange_acid_thread").isValid());
    KARMA_REQUIRE(instance->find("yellow_electric_thread").isValid());
    KARMA_REQUIRE(instance->find("green_poison_thread").isValid());
    KARMA_REQUIRE(instance->find("blue_stone_thread").isValid());
    KARMA_REQUIRE(instance->find("indigo_mind_thread").isValid());
    KARMA_REQUIRE(instance->find("violet_shift_thread").isValid());
    KARMA_REQUIRE(instance->find("flares").isValid());
    KARMA_REQUIRE(instance->find("sparks").isValid());
    KARMA_REQUIRE(instance->find("wisps").isValid());
    KARMA_REQUIRE(instance->find("distortion").isValid());
    KARMA_REQUIRE(!instance->find("shell").isValid());
    const karma::world::Entity glow = instance->find("chromatic_glow");
    KARMA_REQUIRE(glow.isValid());
    KARMA_REQUIRE(world.has<karma::components::LightComponent>(glow));
  }

  if (preset == "daze") {
    KARMA_REQUIRE(effect_count >= 5u);
    KARMA_REQUIRE(instance->find("halo_ring").isValid());
    KARMA_REQUIRE(instance->find("halo_glow").isValid());
    KARMA_REQUIRE(instance->find("crescent_arc_1").isValid());
    KARMA_REQUIRE(instance->find("crescent_arc_5").isValid());
    KARMA_REQUIRE(instance->find("haze").isValid());
    KARMA_REQUIRE(instance->find("stars").isValid());
    KARMA_REQUIRE(instance->find("streaks").isValid());
    KARMA_REQUIRE(instance->find("pulse").isValid());
    KARMA_REQUIRE(instance->find("distortion").isValid());
    KARMA_REQUIRE(!instance->find("shell").isValid());
    const karma::world::Entity glow = instance->find("daze_glow");
    KARMA_REQUIRE(glow.isValid());
    KARMA_REQUIRE(world.has<karma::components::LightComponent>(glow));
  }

  if (preset == "heal") {
    KARMA_REQUIRE(effect_count >= 5u);
    KARMA_REQUIRE(instance->find("base_healing_ring").isValid());
    KARMA_REQUIRE(instance->find("waist_healing_ring").isValid());
    KARMA_REQUIRE(instance->find("chest_healing_ring").isValid());
    KARMA_REQUIRE(instance->find("head_shimmer_ring").isValid());
    KARMA_REQUIRE(instance->find("healing_spiral_1").isValid());
    KARMA_REQUIRE(instance->find("healing_spiral_2").isValid());
    KARMA_REQUIRE(instance->find("healing_column").isValid());
    KARMA_REQUIRE(instance->find("mist").isValid());
    KARMA_REQUIRE(instance->find("shimmer").isValid());
    KARMA_REQUIRE(instance->find("glints").isValid());
    KARMA_REQUIRE(instance->find("pulse").isValid());
    KARMA_REQUIRE(instance->find("distortion").isValid());
    KARMA_REQUIRE(!instance->find("shell").isValid());
    const karma::world::Entity glow = instance->find("heal_glow");
    KARMA_REQUIRE(glow.isValid());
    KARMA_REQUIRE(world.has<karma::components::LightComponent>(glow));
  }

  if (preset == "breathe_fire") {
    KARMA_REQUIRE(effect_count >= 5u);
    KARMA_REQUIRE(instance->find("white_hot_breath_core").isValid());
    KARMA_REQUIRE(instance->find("central_flame_cone").isValid());
    KARMA_REQUIRE(instance->find("left_inner_flame_sheet").isValid());
    KARMA_REQUIRE(instance->find("right_inner_flame_sheet").isValid());
    KARMA_REQUIRE(instance->find("front_flame_billow").isValid());
    const karma::world::Entity dense_fan_0 = instance->find("dense_flame_fan_0");
    const karma::world::Entity dense_fan_95 = instance->find("dense_flame_fan_95");
    KARMA_REQUIRE(dense_fan_0.isValid());
    KARMA_REQUIRE(dense_fan_95.isValid());
    KARMA_REQUIRE(world.has<karma::components::ParticleBeamComponent>(dense_fan_0));
    KARMA_REQUIRE(world.has<karma::components::ParticleBeamComponent>(dense_fan_95));
    const auto& dense_fan_0_beam =
        world.get<karma::components::ParticleBeamComponent>(dense_fan_0);
    const auto& dense_fan_95_beam =
        world.get<karma::components::ParticleBeamComponent>(dense_fan_95);
    KARMA_REQUIRE(dense_fan_0_beam.local_path_points.size() >= 9u);
    KARMA_REQUIRE(dense_fan_95_beam.local_path_points.size() >= 9u);
    KARMA_REQUIRE(dense_fan_0_beam.start_width >= 0.06f);
    KARMA_REQUIRE(dense_fan_95_beam.start_width >= 0.06f);
    KARMA_REQUIRE(dense_fan_0_beam.end_width >= 0.6f);
    KARMA_REQUIRE(dense_fan_95_beam.end_width >= 0.6f);
    KARMA_REQUIRE(dense_fan_0_beam.local_path_points.front().x ==
                  dense_fan_95_beam.local_path_points.front().x);
    KARMA_REQUIRE(dense_fan_0_beam.local_path_points.front().y ==
                  dense_fan_95_beam.local_path_points.front().y);
    KARMA_REQUIRE(dense_fan_0_beam.local_path_points.front().z ==
                  dense_fan_95_beam.local_path_points.front().z);
    KARMA_REQUIRE(instance->find("outer_smoke_fan_0").isValid());
    KARMA_REQUIRE(instance->find("outer_smoke_fan_11").isValid());
    KARMA_REQUIRE(instance->find("upper_outer_flame_shear").isValid());
    KARMA_REQUIRE(instance->find("lower_outer_flame_shear").isValid());
    KARMA_REQUIRE(instance->find("upper_flame_tongue").isValid());
    KARMA_REQUIRE(instance->find("lower_flame_tongue").isValid());
    KARMA_REQUIRE(instance->find("left_flame_tongue").isValid());
    KARMA_REQUIRE(instance->find("right_flame_tongue").isValid());
    KARMA_REQUIRE(instance->find("smoke_edge_haze").isValid());
    KARMA_REQUIRE(instance->find("mouth_flash").isValid());
    KARMA_REQUIRE(instance->find("flame_plumes").isValid());
    KARMA_REQUIRE(instance->find("embers").isValid());
    KARMA_REQUIRE(instance->find("smoke").isValid());
    KARMA_REQUIRE(instance->find("heat").isValid());
    KARMA_REQUIRE(!instance->find("shell").isValid());
    const karma::world::Entity glow = instance->find("breathe_fire_glow");
    KARMA_REQUIRE(glow.isValid());
    KARMA_REQUIRE(world.has<karma::components::LightComponent>(glow));

    const auto* flame_effect = assets.findParticleEffect(ns + "/flame_plumes");
    KARMA_REQUIRE(flame_effect != nullptr);
    KARMA_REQUIRE(!flame_effect->emitters.empty());
    for (const auto& emitter_desc : flame_effect->emitters) {
      const auto& emitter = emitter_desc.emitter;
      KARMA_REQUIRE(emitter.source_shape == karma::components::ParticleSourceShape::Path);
      KARMA_REQUIRE(emitter.local_space);
      KARMA_REQUIRE(emitter.velocity_min.x > 0.0f);
      KARMA_REQUIRE(emitter.velocity_max.x > emitter.velocity_min.x);
    }

    const auto* heat_effect = assets.findParticleEffect(ns + "/heat");
    KARMA_REQUIRE(heat_effect != nullptr);
    KARMA_REQUIRE(!heat_effect->emitters.empty());
    for (const auto& emitter_desc : heat_effect->emitters) {
      const auto& emitter = emitter_desc.emitter;
      KARMA_REQUIRE(emitter.blend_mode == karma::components::ParticleBlendMode::Distortion);
      KARMA_REQUIRE(emitter.distortion_strength > 0.0f);
    }
  }

  KARMA_REQUIRE(karma::prefabs::destroyPrefab(world, scene, instance->root));
  karma::prefabs::clearPrefabAssetPackages();
}

}  // namespace

int main() {
  const std::filesystem::path root = makeTempDir();
  testPreset(root, "fire_ray", 1u, 1u);
  testPreset(root, "magic_missile", 1u, 1u);
  testPreset(root, "arcane_barrage", 12u, 5u);
  testPreset(root, "blade_barrier", 8u, 6u);
  testPreset(root, "chromatic_ray", 10u, 4u);
  testPreset(root, "daze", 7u, 5u);
  testPreset(root, "heal", 7u, 5u);
  testPreset(root, "breathe_fire", 120u, 5u);
  testPreset(root, "impact_burst", 0u, 1u);
  testPreset(root, "energy_orb", 0u, 4u);
  std::filesystem::remove_all(root);
  return 0;
}
