#include "particle_effect_tools.h"

#include "karma/assets.h"
#include "karma/components.h"
#include "karma/prefabs.h"
#include "karma/world.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

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

constexpr float kFeetToMeters = 0.3048f;
constexpr float kFireballDefaultBlastRadius = 30.0f * kFeetToMeters;
constexpr float kFireballMaxBlastRadius = 50.0f * kFeetToMeters;
constexpr float kFireballOrbRadius = 0.18f;
constexpr float kDetectMagicRadius = 30.0f * kFeetToMeters;

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

bool near(float a, float b, float tolerance = 0.001f) {
  return std::fabs(a - b) <= tolerance;
}

float distanceToAxis(const karma::math::Vec3& point,
                     const karma::math::Vec3& axis_start,
                     const karma::math::Vec3& axis_end) {
  const karma::math::Vec3 axis =
      karma::math::normalize(karma::math::subtract(axis_end, axis_start));
  const karma::math::Vec3 relative = karma::math::subtract(point, axis_start);
  const float projected = karma::math::dot(relative, axis);
  const karma::math::Vec3 closest =
      karma::math::add(axis_start, karma::math::scale(axis, projected));
  return karma::math::length(karma::math::subtract(point, closest));
}

float unwrappedChromaticTurns(const std::vector<karma::math::Vec3>& path, float axis_y) {
  if (path.size() < 2u) {
    return 0.0f;
  }

  float previous = std::atan2(path.front().y - axis_y, path.front().z);
  float total = 0.0f;
  for (std::size_t i = 1u; i < path.size(); ++i) {
    float current = std::atan2(path[i].y - axis_y, path[i].z);
    float delta = current - previous;
    while (delta > 3.14159265358979323846f) {
      delta -= 2.0f * 3.14159265358979323846f;
    }
    while (delta < -3.14159265358979323846f) {
      delta += 2.0f * 3.14159265358979323846f;
    }
    total += delta;
    previous = current;
  }

  return std::fabs(total) / (2.0f * 3.14159265358979323846f);
}

void testPreset(const std::filesystem::path& root,
                const std::string& preset,
                std::size_t expected_beams,
                std::size_t min_effects) {
  const std::filesystem::path spec_path = root / (preset + ".kpspec.json");
  const std::filesystem::path output_dir = root / preset;
  const std::string ns = "generated/test_" + preset;
  Json spec{
      {"version", 1},
      {"preset", preset},
      {"namespace", ns},
      {"name", "Test " + preset},
      {"length", 5.0f},
  };
  if (preset != "detect_magic") {
    spec["radius"] = 1.1f;
  }
  writeJson(spec_path, spec);

  std::string diagnostic;
  KARMA_REQUIRE(karma::tools::particles::generateParticleEffectPackage(spec_path,
                                                                       output_dir,
                                                                       &diagnostic));

  std::ifstream prefab_stream(output_dir / "prefab.json");
  KARMA_REQUIRE(prefab_stream);
  Json prefab;
  prefab_stream >> prefab;
  KARMA_REQUIRE(prefab["version"] == 2);

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

    const auto& core_beam = world.get<karma::components::ParticleBeamComponent>(
        instance->find("chromatic_core"));
    const karma::math::Vec3 axis_start = core_beam.local_path_points.front();
    const karma::math::Vec3 axis_end = core_beam.local_path_points.back();
    const float ray_length =
        karma::math::length(karma::math::subtract(axis_end, axis_start));
    const float expected_turns = 2.45f * ray_length / 6.4f;
    const char* thread_names[] = {
        "red_fire_thread",
        "orange_acid_thread",
        "yellow_electric_thread",
        "green_poison_thread",
        "blue_stone_thread",
        "indigo_mind_thread",
        "violet_shift_thread",
    };
    for (const char* thread_name : thread_names) {
      const auto& thread_beam = world.get<karma::components::ParticleBeamComponent>(
          instance->find(thread_name));
      KARMA_REQUIRE(thread_beam.local_path_points.size() >= 8u);
      for (const karma::math::Vec3& point : thread_beam.local_path_points) {
        KARMA_REQUIRE(near(distanceToAxis(point, axis_start, axis_end), 0.25f, 0.002f));
      }
    }
    const auto& red_thread = world.get<karma::components::ParticleBeamComponent>(
        instance->find("red_fire_thread"));
    KARMA_REQUIRE(near(unwrappedChromaticTurns(red_thread.local_path_points, axis_start.y),
                       expected_turns,
                       0.02f));
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

    auto require_circular_beam = [&](const char* name, float tolerance) {
      const karma::world::Entity entity = instance->find(name);
      KARMA_REQUIRE(entity.isValid());
      KARMA_REQUIRE(world.has<karma::components::ParticleBeamComponent>(entity));
      const auto& beam = world.get<karma::components::ParticleBeamComponent>(entity);
      KARMA_REQUIRE(!beam.local_path_points.empty());
      float min_x = beam.local_path_points.front().x;
      float max_x = beam.local_path_points.front().x;
      float min_z = beam.local_path_points.front().z;
      float max_z = beam.local_path_points.front().z;
      for (const auto& point : beam.local_path_points) {
        min_x = std::min(min_x, point.x);
        max_x = std::max(max_x, point.x);
        min_z = std::min(min_z, point.z);
        max_z = std::max(max_z, point.z);
      }
      const float x_span = max_x - min_x;
      const float z_span = max_z - min_z;
      const float max_span = std::max(x_span, z_span);
      KARMA_REQUIRE(max_span > 0.0f);
      KARMA_REQUIRE(std::abs(x_span - z_span) <= max_span * tolerance);
    };
    require_circular_beam("base_healing_ring", 0.08f);
    require_circular_beam("waist_healing_ring", 0.08f);
    require_circular_beam("chest_healing_ring", 0.08f);
    require_circular_beam("head_shimmer_ring", 0.08f);
    require_circular_beam("healing_spiral_1", 0.18f);
    require_circular_beam("healing_spiral_2", 0.18f);
  }

  if (preset == "haste") {
    KARMA_REQUIRE(effect_count == 4u);
    KARMA_REQUIRE(prefab.contains("variables"));
    KARMA_REQUIRE(prefab["variables"].contains("height"));
    KARMA_REQUIRE(prefab["variables"].contains("radius"));
    KARMA_REQUIRE(prefab["variables"].contains("duration"));
    KARMA_REQUIRE(prefab["variables"].contains("intensity"));
    KARMA_REQUIRE(prefab["variables"].contains("color"));
    KARMA_REQUIRE(instance->find("base_speed_ring").isValid());
    KARMA_REQUIRE(instance->find("ankle_speed_ring").isValid());
    KARMA_REQUIRE(instance->find("waist_speed_ring").isValid());
    KARMA_REQUIRE(instance->find("shoulder_speed_ring").isValid());
    KARMA_REQUIRE(instance->find("speed_streak_0").isValid());
    KARMA_REQUIRE(instance->find("speed_streak_7").isValid());
    KARMA_REQUIRE(instance->find("afterimage_left").isValid());
    KARMA_REQUIRE(instance->find("afterimage_right").isValid());
    KARMA_REQUIRE(instance->find("speed_streaks").isValid());
    KARMA_REQUIRE(instance->find("tick_sparks").isValid());
    KARMA_REQUIRE(instance->find("afterimage_haze").isValid());
    KARMA_REQUIRE(instance->find("distortion").isValid());
    KARMA_REQUIRE(!instance->find("shell").isValid());

    const karma::world::Entity streaks = instance->find("speed_streaks");
    KARMA_REQUIRE(world.has<karma::components::ParticleEffectOverrideComponent>(streaks));
    const auto& streak_override =
        world.get<karma::components::ParticleEffectOverrideComponent>(streaks);
    KARMA_REQUIRE(streak_override.source_height.has_value());
    KARMA_REQUIRE(streak_override.source_outer_radius.has_value());
    KARMA_REQUIRE(near(*streak_override.source_height, 2.55f * 1.1f));
    KARMA_REQUIRE(near(*streak_override.source_outer_radius, 1.06f * 1.1f));

    const karma::world::Entity glow = instance->find("haste_glow");
    KARMA_REQUIRE(glow.isValid());
    KARMA_REQUIRE(world.has<karma::components::LightComponent>(glow));
    const auto& light = world.get<karma::components::LightComponent>(glow);
    KARMA_REQUIRE(near(light.intensity, 3.4f));
    KARMA_REQUIRE(near(light.range, 3.6f * 1.1f));
  }

  if (preset == "detect_magic") {
    KARMA_REQUIRE(effect_count == 4u);
    KARMA_REQUIRE(beam_count == 0u);
    KARMA_REQUIRE(prefab["variables"].is_object());
    KARMA_REQUIRE(prefab["variables"]["radius"]["type"] == "float");
    KARMA_REQUIRE(near(prefab["variables"]["radius"]["default"].get<float>(),
                       kDetectMagicRadius));

    const karma::world::Entity volume = instance->find("shimmer_volume");
    KARMA_REQUIRE(volume.isValid());
    KARMA_REQUIRE(world.has<karma::components::VolumetricComponent>(volume));
    const auto& volume_component =
        world.get<karma::components::VolumetricComponent>(volume);
    const std::string interior_material_key =
        ns + "/detect_magic_interior_volume";
    const std::string surface_material_key =
        ns + "/detect_magic_surface_volume";
    KARMA_REQUIRE(volume_component.shape == karma::components::VolumetricShape::Sphere);
    KARMA_REQUIRE(near(volume_component.radius, kDetectMagicRadius));
    KARMA_REQUIRE(!volume_component.scale_with_transform);
    KARMA_REQUIRE(volume_component.surface_double_sided);
    KARMA_REQUIRE(volume_component.interior_material_key == interior_material_key);
    KARMA_REQUIRE(volume_component.surface_material_key == surface_material_key);

    const auto* interior_material = assets.findMaterialAsset(interior_material_key);
    const auto* surface_material = assets.findMaterialAsset(surface_material_key);
    KARMA_REQUIRE(interior_material != nullptr);
    KARMA_REQUIRE(surface_material != nullptr);
    KARMA_REQUIRE(interior_material->pipeline.name == "custom");
    KARMA_REQUIRE(surface_material->pipeline.name == "custom");
    KARMA_REQUIRE(interior_material->pipeline.fragment_shader_path.filename() ==
                  "detect_magic_interior_ps.hlsl");
    KARMA_REQUIRE(surface_material->pipeline.fragment_shader_path.filename() ==
                  "detect_magic_surface_ps.hlsl");
    KARMA_REQUIRE(interior_material->surface.transparent);
    KARMA_REQUIRE(surface_material->surface.transparent);
    KARMA_REQUIRE(std::filesystem::exists(output_dir / "shaders" /
                                          "detect_magic_volume_vs.hlsl"));
    KARMA_REQUIRE(std::filesystem::exists(output_dir / "shaders" /
                                          "detect_magic_interior_ps.hlsl"));
    KARMA_REQUIRE(std::filesystem::exists(output_dir / "shaders" /
                                          "detect_magic_surface_ps.hlsl"));
    KARMA_REQUIRE(std::filesystem::exists(output_dir / "textures" /
                                          "pixie_dust_atlas.png"));
    KARMA_REQUIRE(assets.findTextureAsset(ns + "/pixie_dust_atlas") != nullptr);

    KARMA_REQUIRE(instance->find("swirl").isValid());
    const karma::world::Entity pixie_node = instance->find("pixie_dust");
    KARMA_REQUIRE(pixie_node.isValid());
    KARMA_REQUIRE(world.has<karma::components::ParticleEmitterComponent>(pixie_node));
    const auto& pixie_component =
        world.get<karma::components::ParticleEmitterComponent>(pixie_node);
    KARMA_REQUIRE(pixie_component.enabled);
    KARMA_REQUIRE(pixie_component.playing);
    KARMA_REQUIRE(world.has<karma::components::ParticleEffectOverrideComponent>(
        pixie_node));
    const auto& pixie_override =
        world.get<karma::components::ParticleEffectOverrideComponent>(pixie_node);
    KARMA_REQUIRE(pixie_override.source_outer_radius.has_value());
    KARMA_REQUIRE(near(*pixie_override.source_outer_radius, kDetectMagicRadius));
    KARMA_REQUIRE(instance->find("mist").isValid());
    KARMA_REQUIRE(instance->find("shimmer_distortion").isValid());
    KARMA_REQUIRE(!instance->find("projectile_core").isValid());
    const karma::world::Entity glow = instance->find("detect_magic_glow");
    KARMA_REQUIRE(glow.isValid());
    KARMA_REQUIRE(world.has<karma::components::LightComponent>(glow));

    const auto* dust = assets.findParticleEffect(ns + "/pixie_dust");
    KARMA_REQUIRE(dust != nullptr);
    KARMA_REQUIRE(dust->primaryEmitter() != nullptr);
    KARMA_REQUIRE(dust->primaryEmitter()->texture_key == ns + "/pixie_dust_atlas");
    const auto& dust_emitter = dust->primaryEmitter()->emitter;
    KARMA_REQUIRE(dust_emitter.atlas_columns == 4u);
    KARMA_REQUIRE(dust_emitter.atlas_rows == 4u);
    KARMA_REQUIRE(dust_emitter.atlas_frame_count == 16u);
    KARMA_REQUIRE(dust_emitter.source_shape ==
                  karma::components::ParticleSourceShape::Sphere);
    KARMA_REQUIRE(near(dust_emitter.source_radius_max, kDetectMagicRadius));
    KARMA_REQUIRE(dust_emitter.layer == 0u);
    KARMA_REQUIRE(dust_emitter.max_particles >= 2000u);
    KARMA_REQUIRE(dust_emitter.spawn_rate >= 170.0f);
    KARMA_REQUIRE(dust_emitter.blend_mode ==
                  karma::components::ParticleBlendMode::Additive);

    const auto* mist = assets.findParticleEffect(ns + "/mist");
    KARMA_REQUIRE(mist != nullptr);
    KARMA_REQUIRE(mist->primaryEmitter() != nullptr);
    KARMA_REQUIRE(mist->primaryEmitter()->emitter.blend_mode ==
                  karma::components::ParticleBlendMode::Alpha);

    const auto* distortion = assets.findParticleEffect(ns + "/shimmer_distortion");
    KARMA_REQUIRE(distortion != nullptr);
    KARMA_REQUIRE(distortion->primaryEmitter() != nullptr);
    KARMA_REQUIRE(distortion->primaryEmitter()->emitter.blend_mode ==
                  karma::components::ParticleBlendMode::Distortion);
    KARMA_REQUIRE(distortion->primaryEmitter()->emitter.distortion_strength > 0.0f);
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

  if (preset == "fireball") {
    KARMA_REQUIRE(effect_count == 11u);
    KARMA_REQUIRE(beam_count == 0u);
    KARMA_REQUIRE(instance->find("core_flash").isValid());
    KARMA_REQUIRE(instance->find("core_flipbook").isValid());
    KARMA_REQUIRE(instance->find("flame_shell").isValid());
    KARMA_REQUIRE(instance->find("flame_tongues").isValid());
    KARMA_REQUIRE(instance->find("embers").isValid());
    KARMA_REQUIRE(instance->find("ember_storm").isValid());
    KARMA_REQUIRE(instance->find("hot_ash_embers").isValid());
    KARMA_REQUIRE(instance->find("smoke_edge").isValid());
    KARMA_REQUIRE(instance->find("smoke_plumes").isValid());
    KARMA_REQUIRE(instance->find("smoke_roll").isValid());
    KARMA_REQUIRE(instance->find("heat_shimmer").isValid());
    KARMA_REQUIRE(!instance->find("shell").isValid());
    const karma::world::Entity glow = instance->find("fireball_light");
    KARMA_REQUIRE(glow.isValid());
    KARMA_REQUIRE(world.has<karma::components::LightComponent>(glow));

    const auto* core = assets.findParticleEffect(ns + "/core_flash");
    KARMA_REQUIRE(core != nullptr);
    KARMA_REQUIRE(!core->emitters.empty());
    for (const auto& emitter_desc : core->emitters) {
      const auto& emitter = emitter_desc.emitter;
      KARMA_REQUIRE(!emitter.loop);
      KARMA_REQUIRE(emitter.emit_burst_on_start);
      KARMA_REQUIRE(emitter.local_space);
      KARMA_REQUIRE(emitter.burst_count > 0u);
      KARMA_REQUIRE(emitter.spawn_rate == 0.0f);
      KARMA_REQUIRE(emitter.source_shape == karma::components::ParticleSourceShape::Sphere);
      KARMA_REQUIRE(emitter.radial_speed_max > emitter.radial_speed_min);
    }

    const auto* smoke = assets.findParticleEffect(ns + "/smoke_edge");
    KARMA_REQUIRE(smoke != nullptr);
    KARMA_REQUIRE(!smoke->emitters.empty());
    const auto* smoke_primary = smoke->primaryEmitter();
    KARMA_REQUIRE(smoke_primary != nullptr);
    KARMA_REQUIRE(smoke_primary->emitter.start_delay > 0.0f);

    const auto* heat = assets.findParticleEffect(ns + "/heat_shimmer");
    KARMA_REQUIRE(heat != nullptr);
    KARMA_REQUIRE(!heat->emitters.empty());
    for (const auto& emitter_desc : heat->emitters) {
      const auto& emitter = emitter_desc.emitter;
      KARMA_REQUIRE(emitter.blend_mode == karma::components::ParticleBlendMode::Distortion);
      KARMA_REQUIRE(emitter.distortion_strength > 0.0f);
      KARMA_REQUIRE(!emitter.loop);
    }
  }

  KARMA_REQUIRE(karma::prefabs::destroyPrefab(world, scene, instance->root));
  karma::prefabs::clearPrefabAssetPackages();
}

void requireGeneratedManifest(const std::filesystem::path& package_dir,
                              const std::string& ns) {
  std::ifstream prefab_stream(package_dir / "prefab.json");
  KARMA_REQUIRE(prefab_stream);
  Json prefab;
  prefab_stream >> prefab;
  KARMA_REQUIRE(prefab["version"] == 2);

  std::ifstream manifest_stream(package_dir / "assets.package.json");
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
    KARMA_REQUIRE(std::filesystem::exists(package_dir / path));
  }
}

void testFireballPreset(const std::filesystem::path& root) {
  const std::filesystem::path spec_path = root / "fireball.kpspec.json";
  const std::filesystem::path output_dir = root / "fireball";
  const std::string ns = "generated/test_fireball";
  writeJson(spec_path,
            Json{
                {"version", 1},
                {"preset", "fireball"},
                {"namespace", ns},
                {"name", "Test Fireball"},
            });

  std::string diagnostic;
  KARMA_REQUIRE(karma::tools::particles::generateParticleEffectPackage(spec_path,
                                                                       output_dir,
                                                                       &diagnostic));
  KARMA_REQUIRE(!std::filesystem::exists(output_dir / "prefab.json"));

  const std::filesystem::path projectile_dir = output_dir / "projectile";
  const std::filesystem::path explosion_dir = output_dir / "explosion";
  requireGeneratedManifest(projectile_dir, ns + "/projectile");
  requireGeneratedManifest(explosion_dir, ns + "/explosion");

  std::ifstream explosion_prefab_stream(explosion_dir / "prefab.json");
  KARMA_REQUIRE(explosion_prefab_stream);
  Json explosion_prefab;
  explosion_prefab_stream >> explosion_prefab;
  KARMA_REQUIRE(explosion_prefab["variables"].is_object());
  KARMA_REQUIRE(explosion_prefab["variables"]["radius"]["type"] == "float");
  KARMA_REQUIRE(near(explosion_prefab["variables"]["radius"]["default"].get<float>(),
                     kFireballDefaultBlastRadius));

  auto instantiate_and_count =
      [](const std::filesystem::path& package_dir,
         karma::assets::AssetRegistry& assets,
         karma::world::World& world,
         karma::world::Scene& scene,
         std::size_t& effect_count) {
    karma::prefabs::bindPrefabAssetRegistry(&assets);
    const auto instance = karma::prefabs::instantiatePrefab(world, scene, package_dir);
    KARMA_REQUIRE(instance.has_value());
    KARMA_REQUIRE(instance->valid());
    for (const karma::world::Entity entity : instance->entities) {
      if (!world.has<karma::components::ParticleEffectComponent>(entity)) {
        continue;
      }
      effect_count += 1u;
      const auto& effect = world.get<karma::components::ParticleEffectComponent>(entity);
      const auto* effect_asset = assets.findParticleEffect(effect.effect_key);
      KARMA_REQUIRE(effect_asset != nullptr);
      for (const auto& emitter : effect_asset->emitters) {
        KARMA_REQUIRE(assets.findTextureAsset(emitter.texture_key) != nullptr);
      }
    }
    return instance;
  };

  karma::assets::AssetRegistry projectile_assets;
  karma::world::World projectile_world;
  karma::world::Scene projectile_scene;
  std::size_t projectile_effect_count = 0u;
  const auto projectile = instantiate_and_count(projectile_dir,
                                                projectile_assets,
                                                projectile_world,
                                                projectile_scene,
                                                projectile_effect_count);
  KARMA_REQUIRE(projectile_effect_count == 6u);
  KARMA_REQUIRE(projectile->find("projectile_core").isValid());
  KARMA_REQUIRE(projectile->find("projectile_flames").isValid());
  KARMA_REQUIRE(projectile->find("projectile_smoke_trail").isValid());
  KARMA_REQUIRE(projectile->find("projectile_embers").isValid());
  KARMA_REQUIRE(projectile->find("projectile_ember_sparks").isValid());
  KARMA_REQUIRE(projectile->find("projectile_heat").isValid());
  const karma::world::Entity projectile_light = projectile->find("projectile_light");
  KARMA_REQUIRE(projectile_light.isValid());
  KARMA_REQUIRE(projectile_world.has<karma::components::LightComponent>(projectile_light));

  const auto* projectile_core = projectile_assets.findParticleEffect(ns + "/projectile/core");
  KARMA_REQUIRE(projectile_core != nullptr);
  KARMA_REQUIRE(projectile_core->primaryEmitter() != nullptr);
  KARMA_REQUIRE(projectile_core->primaryEmitter()->emitter.loop);
  KARMA_REQUIRE(projectile_core->primaryEmitter()->emitter.spawn_rate > 0.0f);

  const auto* projectile_smoke =
      projectile_assets.findParticleEffect(ns + "/projectile/smoke_trail");
  KARMA_REQUIRE(projectile_smoke != nullptr);
  KARMA_REQUIRE(projectile_smoke->primaryEmitter() != nullptr);
  KARMA_REQUIRE(projectile_smoke->primaryEmitter()->emitter.blend_mode ==
                karma::components::ParticleBlendMode::Alpha);
  KARMA_REQUIRE(projectile_smoke->primaryEmitter()->emitter.max_particles == 360u);
  KARMA_REQUIRE(near(projectile_smoke->primaryEmitter()->emitter.spawn_rate, 96.0f));
  KARMA_REQUIRE(projectile_smoke->primaryEmitter()->emitter.particle_lifetime_max > 1.0f);

  const auto* projectile_ember_sparks =
      projectile_assets.findParticleEffect(ns + "/projectile/ember_sparks");
  KARMA_REQUIRE(projectile_ember_sparks != nullptr);
  KARMA_REQUIRE(projectile_ember_sparks->primaryEmitter() != nullptr);
  KARMA_REQUIRE(projectile_ember_sparks->primaryEmitter()->emitter.max_particles == 420u);
  KARMA_REQUIRE(projectile_ember_sparks->primaryEmitter()->emitter.spawn_rate > 0.0f);
  KARMA_REQUIRE(projectile_ember_sparks->primaryEmitter()->emitter.atlas_frame_count == 8u);
  KARMA_REQUIRE(karma::prefabs::destroyPrefab(projectile_world,
                                             projectile_scene,
                                             projectile->root));
  karma::prefabs::clearPrefabAssetPackages();

  karma::assets::AssetRegistry explosion_assets;
  karma::world::World explosion_world;
  karma::world::Scene explosion_scene;
  std::size_t explosion_effect_count = 0u;
  const auto explosion = instantiate_and_count(explosion_dir,
                                               explosion_assets,
                                               explosion_world,
                                               explosion_scene,
                                               explosion_effect_count);
  KARMA_REQUIRE(explosion_effect_count == 11u);
  KARMA_REQUIRE(explosion->find("core_flash").isValid());
  KARMA_REQUIRE(explosion->find("core_flipbook").isValid());
  KARMA_REQUIRE(explosion->find("flame_shell").isValid());
  KARMA_REQUIRE(explosion->find("flame_tongues").isValid());
  KARMA_REQUIRE(explosion->find("embers").isValid());
  KARMA_REQUIRE(explosion->find("ember_storm").isValid());
  KARMA_REQUIRE(explosion->find("hot_ash_embers").isValid());
  KARMA_REQUIRE(explosion->find("smoke_edge").isValid());
  KARMA_REQUIRE(explosion->find("smoke_plumes").isValid());
  KARMA_REQUIRE(explosion->find("smoke_roll").isValid());
  KARMA_REQUIRE(explosion->find("heat_shimmer").isValid());
  const karma::world::Entity explosion_light = explosion->find("fireball_light");
  KARMA_REQUIRE(explosion_light.isValid());
  KARMA_REQUIRE(explosion_world.has<karma::components::LightComponent>(explosion_light));

  const auto* explosion_core = explosion_assets.findParticleEffect(ns + "/explosion/core_flash");
  KARMA_REQUIRE(explosion_core != nullptr);
  KARMA_REQUIRE(explosion_core->primaryEmitter() != nullptr);
  const auto& explosion_core_emitter = explosion_core->primaryEmitter()->emitter;
  KARMA_REQUIRE(!explosion_core_emitter.loop);
  KARMA_REQUIRE(explosion_core_emitter.spawn_rate == 0.0f);
  KARMA_REQUIRE(explosion_core_emitter.max_particles == 128u);
  KARMA_REQUIRE(explosion_core_emitter.burst_count == 104u);
  KARMA_REQUIRE(near(explosion_core_emitter.duration, 0.56f));
  KARMA_REQUIRE(near(explosion_core_emitter.particle_lifetime_min, 0.22f));
  KARMA_REQUIRE(near(explosion_core_emitter.particle_lifetime_max, 0.48f));
  KARMA_REQUIRE(near(explosion_core_emitter.start_size_min, 0.18f));
  KARMA_REQUIRE(near(explosion_core_emitter.start_size_max, 0.34f));
  KARMA_REQUIRE(near(explosion_core_emitter.end_size_max,
                     3.05f * kFireballDefaultBlastRadius));
  KARMA_REQUIRE(explosion_core_emitter.size_curve_exponent > 2.0f);
  KARMA_REQUIRE(near(explosion_core_emitter.source_radius_max,
                     1.10f * kFireballOrbRadius));
  KARMA_REQUIRE(near(explosion_core_emitter.radial_speed_max,
                     3.40f * kFireballDefaultBlastRadius));

  const auto* flame_shell = explosion_assets.findParticleEffect(ns + "/explosion/flame_shell");
  KARMA_REQUIRE(flame_shell != nullptr);
  KARMA_REQUIRE(flame_shell->primaryEmitter() != nullptr);
  const auto& flame_shell_emitter = flame_shell->primaryEmitter()->emitter;
  KARMA_REQUIRE(flame_shell_emitter.max_particles == 560u * 4u);
  KARMA_REQUIRE(flame_shell_emitter.burst_count == 380u * 4u);
  KARMA_REQUIRE(near(flame_shell_emitter.start_size_min, 0.12f));
  KARMA_REQUIRE(near(flame_shell_emitter.start_size_max, 0.28f));
  KARMA_REQUIRE(near(flame_shell_emitter.end_size_max,
                     2.28f * kFireballDefaultBlastRadius * 0.5f));
  KARMA_REQUIRE(flame_shell_emitter.size_curve_exponent > 2.0f);
  KARMA_REQUIRE(near(flame_shell_emitter.source_radius_max,
                     1.45f * kFireballOrbRadius));

  const auto* flame_tongues =
      explosion_assets.findParticleEffect(ns + "/explosion/flame_tongues");
  KARMA_REQUIRE(flame_tongues != nullptr);
  KARMA_REQUIRE(flame_tongues->primaryEmitter() != nullptr);
  const auto& flame_tongues_emitter = flame_tongues->primaryEmitter()->emitter;
  KARMA_REQUIRE(flame_tongues_emitter.max_particles == 460u * 4u);
  KARMA_REQUIRE(flame_tongues_emitter.burst_count == 260u * 4u);
  KARMA_REQUIRE(near(flame_tongues_emitter.start_size_min, 0.10f));
  KARMA_REQUIRE(near(flame_tongues_emitter.start_size_max, 0.24f));
  KARMA_REQUIRE(near(flame_tongues_emitter.end_size_max,
                     2.02f * kFireballDefaultBlastRadius * 0.5f));
  KARMA_REQUIRE(flame_tongues_emitter.size_curve_exponent > 2.0f);
  KARMA_REQUIRE(near(flame_tongues_emitter.source_radius_max,
                     1.75f * kFireballOrbRadius));

  const auto* explosion_embers =
      explosion_assets.findParticleEffect(ns + "/explosion/embers");
  KARMA_REQUIRE(explosion_embers != nullptr);
  KARMA_REQUIRE(explosion_embers->primaryEmitter() != nullptr);
  KARMA_REQUIRE(explosion_embers->primaryEmitter()->emitter.max_particles == 760u);
  KARMA_REQUIRE(explosion_embers->primaryEmitter()->emitter.burst_count == 260u);

  const auto* ember_storm =
      explosion_assets.findParticleEffect(ns + "/explosion/ember_storm");
  KARMA_REQUIRE(ember_storm != nullptr);
  KARMA_REQUIRE(ember_storm->primaryEmitter() != nullptr);
  const auto& ember_storm_emitter = ember_storm->primaryEmitter()->emitter;
  KARMA_REQUIRE(ember_storm_emitter.max_particles == 900u);
  KARMA_REQUIRE(ember_storm_emitter.burst_count == 360u);
  KARMA_REQUIRE(ember_storm_emitter.atlas_frame_count == 8u);
  KARMA_REQUIRE(ember_storm_emitter.radial_speed_max >
                ember_storm_emitter.radial_speed_min);

  const auto* hot_ash_embers =
      explosion_assets.findParticleEffect(ns + "/explosion/hot_ash_embers");
  KARMA_REQUIRE(hot_ash_embers != nullptr);
  KARMA_REQUIRE(hot_ash_embers->primaryEmitter() != nullptr);
  const auto& hot_ash_emitter = hot_ash_embers->primaryEmitter()->emitter;
  KARMA_REQUIRE(hot_ash_emitter.blend_mode ==
                karma::components::ParticleBlendMode::Alpha);
  KARMA_REQUIRE(hot_ash_emitter.max_particles == 2200u);
  KARMA_REQUIRE(hot_ash_emitter.burst_count == 1400u);
  KARMA_REQUIRE(hot_ash_emitter.particle_lifetime_min >= 4.0f);
  KARMA_REQUIRE(hot_ash_emitter.particle_lifetime_max >= 7.0f);
  KARMA_REQUIRE(near(hot_ash_emitter.start_size_min, 0.110f));
  KARMA_REQUIRE(near(hot_ash_emitter.start_size_max, 0.240f));
  KARMA_REQUIRE(near(hot_ash_emitter.source_radius_max,
                     0.85f * kFireballOrbRadius));
  KARMA_REQUIRE(near(hot_ash_emitter.radial_speed_min,
                     0.45f * kFireballDefaultBlastRadius));
  KARMA_REQUIRE(near(hot_ash_emitter.radial_speed_max,
                     1.35f * kFireballDefaultBlastRadius));
  KARMA_REQUIRE(hot_ash_emitter.end_color.r < hot_ash_emitter.start_color.r);
  KARMA_REQUIRE(hot_ash_emitter.end_color.g < hot_ash_emitter.start_color.g);
  KARMA_REQUIRE(hot_ash_emitter.end_color.b < hot_ash_emitter.start_color.b);
  KARMA_REQUIRE(hot_ash_emitter.end_color.a == 0.0f);

  const auto* explosion_smoke =
      explosion_assets.findParticleEffect(ns + "/explosion/smoke_edge");
  KARMA_REQUIRE(explosion_smoke != nullptr);
  KARMA_REQUIRE(explosion_smoke->primaryEmitter() != nullptr);
  const auto& explosion_smoke_emitter = explosion_smoke->primaryEmitter()->emitter;
  KARMA_REQUIRE(near(explosion_smoke_emitter.start_size_min, 0.32f));
  KARMA_REQUIRE(near(explosion_smoke_emitter.start_size_max, 0.70f));
  KARMA_REQUIRE(explosion_smoke_emitter.max_particles == 420u);
  KARMA_REQUIRE(explosion_smoke_emitter.burst_count == 210u);
  KARMA_REQUIRE(explosion_smoke_emitter.size_curve_exponent > 2.0f);
  KARMA_REQUIRE(near(explosion_smoke_emitter.source_radius_max,
                     2.20f * kFireballOrbRadius));

  const auto* smoke_plumes =
      explosion_assets.findParticleEffect(ns + "/explosion/smoke_plumes");
  KARMA_REQUIRE(smoke_plumes != nullptr);
  KARMA_REQUIRE(smoke_plumes->primaryEmitter() != nullptr);
  const auto& smoke_plumes_emitter = smoke_plumes->primaryEmitter()->emitter;
  KARMA_REQUIRE(smoke_plumes_emitter.max_particles == 320u);
  KARMA_REQUIRE(smoke_plumes_emitter.burst_count == 150u);
  KARMA_REQUIRE(smoke_plumes_emitter.atlas_frame_count == 8u);
  KARMA_REQUIRE(smoke_plumes_emitter.blend_mode ==
                karma::components::ParticleBlendMode::Alpha);
  KARMA_REQUIRE(smoke_plumes_emitter.start_delay > 0.0f);

  const auto* explosion_heat =
      explosion_assets.findParticleEffect(ns + "/explosion/heat_shimmer");
  KARMA_REQUIRE(explosion_heat != nullptr);
  KARMA_REQUIRE(explosion_heat->primaryEmitter() != nullptr);
  KARMA_REQUIRE(explosion_heat->primaryEmitter()->emitter.blend_mode ==
                karma::components::ParticleBlendMode::Distortion);
  KARMA_REQUIRE(explosion_heat->primaryEmitter()->emitter.distortion_strength > 0.0f);
  KARMA_REQUIRE(near(explosion_heat->primaryEmitter()->emitter.start_size_min, 0.22f));
  KARMA_REQUIRE(explosion_heat->primaryEmitter()->emitter.size_curve_exponent > 2.0f);
  KARMA_REQUIRE(near(explosion_heat->primaryEmitter()->emitter.source_radius_max,
                     0.85f * kFireballOrbRadius));
  KARMA_REQUIRE(karma::prefabs::destroyPrefab(explosion_world,
                                             explosion_scene,
                                             explosion->root));
  karma::prefabs::clearPrefabAssetPackages();

  karma::assets::AssetRegistry max_radius_assets;
  karma::world::World max_radius_world;
  karma::world::Scene max_radius_scene;
  karma::prefabs::bindPrefabAssetRegistry(&max_radius_assets);
  karma::prefabs::PrefabInstantiateDesc max_radius_desc{};
  max_radius_desc.assets = &max_radius_assets;
  max_radius_desc.variables["radius"] = kFireballMaxBlastRadius;
  const auto max_radius_explosion =
      karma::prefabs::instantiatePrefab(max_radius_world,
                                        max_radius_scene,
                                        explosion_dir,
                                        max_radius_desc);
  KARMA_REQUIRE(max_radius_explosion.has_value());
  const float max_scale = kFireballMaxBlastRadius / kFireballDefaultBlastRadius;
  const karma::world::Entity max_flame_shell =
      max_radius_explosion->find("flame_shell");
  KARMA_REQUIRE(max_flame_shell.isValid());
  KARMA_REQUIRE(max_radius_world.has<karma::components::ParticleEffectOverrideComponent>(
      max_flame_shell));
  const auto& max_flame_override =
      max_radius_world.get<karma::components::ParticleEffectOverrideComponent>(
          max_flame_shell);
  KARMA_REQUIRE(near(max_flame_override.radius_scale, max_scale));
  KARMA_REQUIRE(near(max_flame_override.size_scale, max_scale));
  KARMA_REQUIRE(near(max_flame_override.velocity_scale, max_scale));
  const karma::world::Entity max_light =
      max_radius_explosion->find("fireball_light");
  KARMA_REQUIRE(max_light.isValid());
  KARMA_REQUIRE(max_radius_world.has<karma::components::LightComponent>(max_light));
  const auto& max_light_component =
      max_radius_world.get<karma::components::LightComponent>(max_light);
  KARMA_REQUIRE(near(max_light_component.range, kFireballMaxBlastRadius * 1.4f));
  KARMA_REQUIRE(near(max_light_component.intensity, kFireballMaxBlastRadius * 2.9f));
  KARMA_REQUIRE(karma::prefabs::destroyPrefab(max_radius_world,
                                             max_radius_scene,
                                             max_radius_explosion->root));
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
  testPreset(root, "haste", 14u, 4u);
  testPreset(root, "detect_magic", 0u, 4u);
  testPreset(root, "breathe_fire", 120u, 5u);
  testFireballPreset(root);
  testPreset(root, "impact_burst", 0u, 1u);
  testPreset(root, "energy_orb", 0u, 4u);
  std::filesystem::remove_all(root);
  return 0;
}
