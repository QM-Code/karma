#include "karma/prefabs/prefab.h"
#include "karma/prefabs/prefab_entry_handler.h"

#include <algorithm>
#include <optional>
#include <unordered_map>

#include "karma/components/particle_emitter.h"
#include "karma/components/visibility.h"
#include "karma/math/quat.h"
#include "karma/particles/effect_api.h"

namespace karma::prefabs {

namespace {

math::Vec3 multiplyVec3(const math::Vec3& a, const math::Vec3& b) {
  return {a.x * b.x, a.y * b.y, a.z * b.z};
}

math::Vec3 addVec3(const math::Vec3& a, const math::Vec3& b) {
  return {a.x + b.x, a.y + b.y, a.z + b.z};
}

components::TransformComponent composeTransform(
    const components::TransformComponent& root,
    const components::TransformComponent& local) {
  components::TransformComponent world_transform{};
  const math::Vec3 scaled_local = multiplyVec3(local.getPosition(), root.getScale());
  const math::Vec3 rotated_local = math::rotateVec(root.getRotation(), scaled_local);
  world_transform.setPosition(addVec3(root.getPosition(), rotated_local));
  world_transform.setRotation(math::mul(root.getRotation(), local.getRotation()));
  world_transform.setScale(multiplyVec3(root.getScale(), local.getScale()));
  return world_transform;
}

math::Color lerpColor(const math::Color& a, const math::Color& b, float t) {
  const float s = std::clamp(t, 0.0f, 1.0f);
  return {
      a.r + (b.r - a.r) * s,
      a.g + (b.g - a.g) * s,
      a.b + (b.b - a.b) * s,
      a.a + (b.a - a.a) * s,
  };
}

math::Color multiplyColor(const math::Color& a, const math::Color& b) {
  return {a.r * b.r, a.g * b.g, a.b * b.b, a.a * b.a};
}

bool tryGetParamColor(const std::unordered_map<std::string, PrefabParamValue>& params,
                      std::string_view name,
                      math::Color& out_color) {
  const auto it = params.find(std::string(name));
  if (it == params.end()) {
    return false;
  }
  if (const auto* color = std::get_if<math::Color>(&it->second)) {
    out_color = *color;
    return true;
  }
  if (const auto* vec = std::get_if<math::Vec3>(&it->second)) {
    out_color = {vec->x, vec->y, vec->z, 1.0f};
    return true;
  }
  return false;
}

bool tryGetParamFloat(const std::unordered_map<std::string, PrefabParamValue>& params,
                      std::string_view name,
                      float& out_value) {
  const auto it = params.find(std::string(name));
  if (it == params.end()) {
    return false;
  }
  if (const auto* value = std::get_if<float>(&it->second)) {
    out_value = *value;
    return true;
  }
  return false;
}

math::Color resolveColorBinding(
    const PrefabColorBinding& binding,
    const std::unordered_map<std::string, PrefabParamValue>& params,
    const math::Color& fallback) {
  math::Color color = binding.value.value_or(fallback);
  if (!binding.param.empty()) {
    math::Color param_color{};
    if (tryGetParamColor(params, binding.param, param_color)) {
      color = param_color;
    }
  }
  color = multiplyColor(color, binding.scale);
  if (binding.mix_color.has_value() && binding.mix_factor > 0.0f) {
    color = lerpColor(color, *binding.mix_color, binding.mix_factor);
  }
  return color;
}

float resolveFloatBinding(const PrefabFloatBinding& binding,
                          const std::unordered_map<std::string, PrefabParamValue>& params,
                          float fallback) {
  float value = binding.value.value_or(fallback);
  if (!binding.param.empty()) {
    float param_value = 0.0f;
    if (tryGetParamFloat(params, binding.param, param_value)) {
      value = param_value;
    }
  }
  return value * binding.scale + binding.bias;
}

std::unordered_map<std::string, PrefabParamValue> resolveParameters(
    const Prefab& prefab,
    const PrefabInstantiateDesc& desc) {
  std::unordered_map<std::string, PrefabParamValue> resolved;
  resolved.reserve(prefab.parameters.size() + desc.param_overrides.size());

  for (const auto& param : prefab.parameters) {
    resolved[param.name] = param.default_value;
  }
  for (const auto& override : desc.param_overrides) {
    resolved[override.name] = override.value;
  }
  return resolved;
}

ecs::Entity createMeshEntity(
    ecs::World& world,
    renderer::GraphicsDevice* graphics,
    const PrefabEntry& entry,
    const std::string& entity_name,
    const components::TransformComponent& world_transform,
    const std::unordered_map<std::string, PrefabParamValue>& resolved_params) {
  ecs::Entity entity = world.createEntity();
  if (!entity_name.empty()) {
    world.setName(entity, entity_name);
  }
  world.add(entity, world_transform);

  renderer::MaterialId material_id = renderer::kInvalidMaterial;
  if (graphics != nullptr) {
    renderer::MaterialDesc material_desc = entry.mesh.material.material;
    if (entry.mesh.material.base_color_binding.enabled) {
      material_desc.base_color = resolveColorBinding(entry.mesh.material.base_color_binding,
                                                     resolved_params,
                                                     material_desc.base_color);
    }
    if (entry.mesh.material.emissive_color_binding.enabled) {
      material_desc.emissive_color =
          resolveColorBinding(entry.mesh.material.emissive_color_binding,
                              resolved_params,
                              material_desc.emissive_color);
    }
    material_id = graphics->createMaterial(material_desc);
  }

  world.add(entity,
            components::MeshComponent{
                .mesh_key = entry.mesh.mesh_path.string(),
                .material_id = material_id,
                .owns_material_id = material_id != renderer::kInvalidMaterial,
                .visible = entry.mesh.visible,
                .shadow_visible = entry.mesh.shadow_visible,
            });
  return entity;
}

ecs::Entity createParticleEntity(
    ecs::World& world,
    const PrefabEntry& entry,
    const std::string& entity_name,
    const components::TransformComponent& world_transform,
    const std::unordered_map<std::string, PrefabParamValue>& resolved_params) {
  std::optional<components::ParticleEffectOverrideComponent> effect_override =
      entry.particle.effect_override;
  if (entry.particle.start_color_binding.enabled) {
    effect_override->start_color =
        resolveColorBinding(entry.particle.start_color_binding,
                            resolved_params,
                            effect_override->start_color.value_or(math::Color{}));
  }
  if (entry.particle.end_color_binding.enabled) {
    effect_override->end_color =
        resolveColorBinding(entry.particle.end_color_binding,
                            resolved_params,
                            effect_override->end_color.value_or(math::Color{}));
  }

  const ecs::Entity entity = particles::createEffectEntity(
      world,
      particles::ParticleEffectEntityDesc{
          .name = entity_name,
          .effect_key = entry.particle.effect_key,
          .transform = world_transform,
          .enabled = entry.particle.enabled,
          .playing = entry.particle.playing,
          .auto_apply = entry.particle.auto_apply,
          .preserve_enabled = entry.particle.preserve_enabled,
          .preserve_playing = entry.particle.preserve_playing,
          .effect_override = effect_override,
      });
  world.add(entity, components::VisibilityComponent{.visible = entry.particle.enabled});
  return entity;
}

ecs::Entity createLightEntity(
    ecs::World& world,
    const PrefabEntry& entry,
    const std::string& entity_name,
    const components::TransformComponent& world_transform,
    const std::unordered_map<std::string, PrefabParamValue>& resolved_params) {
  ecs::Entity entity = world.createEntity();
  if (!entity_name.empty()) {
    world.setName(entity, entity_name);
  }
  world.add(entity, world_transform);

  components::LightComponent light = entry.light.light;
  if (entry.light.color_binding.enabled) {
    light.color = resolveColorBinding(entry.light.color_binding, resolved_params, light.color);
  }
  if (entry.light.intensity_binding.enabled) {
    light.intensity =
        resolveFloatBinding(entry.light.intensity_binding, resolved_params, light.intensity);
  }
  if (entry.light.range_binding.enabled) {
    light.range = resolveFloatBinding(entry.light.range_binding, resolved_params, light.range);
  }
  world.add(entity, light);
  world.add(entity, components::VisibilityComponent{});
  return entity;
}

}  // namespace

std::optional<PrefabInstance> instantiatePrefab(
    ecs::World& world,
    renderer::GraphicsDevice* graphics,
    const Prefab& prefab,
    const PrefabInstantiateDesc& desc) {
  ecs::Entity root = world.createEntity();
  const std::string root_name = !desc.name.empty() ? desc.name : prefab.name;
  if (!root_name.empty()) {
    world.setName(root, root_name);
  }
  world.add(root, desc.transform);

  const auto resolved_params = resolveParameters(prefab, desc);

  components::PrefabInstanceComponent instance_component{};
  instance_component.prefab_name = prefab.name;
  instance_component.enabled = true;

  PrefabInstance instance{};
  instance.root = root;

  for (const auto& entry : prefab.entries) {
    const std::string entity_name = !root_name.empty() ? root_name + "/" + entry.name : entry.name;
    const components::TransformComponent world_transform =
        composeTransform(desc.transform, entry.local_transform);

    ecs::Entity member{};
    bool missing_handler = false;
    switch (entry.type) {
      case PrefabEntry::Type::Mesh:
        member = createMeshEntity(
            world, graphics, entry, entity_name, world_transform, resolved_params);
        break;
      case PrefabEntry::Type::Particle:
        member = createParticleEntity(world, entry, entity_name, world_transform, resolved_params);
        break;
      case PrefabEntry::Type::Light:
        member = createLightEntity(world, entry, entity_name, world_transform, resolved_params);
        break;
      case PrefabEntry::Type::Beam:
      case PrefabEntry::Type::VolumeSphere:
        member = instantiatePrefabEntry(PrefabEntryHandlerContext{
            .world = world,
            .graphics = graphics,
            .entry = entry,
            .entity_name = entity_name,
            .world_transform = world_transform,
            .resolved_params = resolved_params,
        });
        missing_handler = !member.isValid();
        break;
    }

    if (!member.isValid()) {
      if (missing_handler) {
        world.destroyEntity(root);
        for (const ecs::Entity created_member : instance.members) {
          if (world.isAlive(created_member)) {
            world.destroyEntity(created_member);
          }
        }
        return std::nullopt;
      }
      continue;
    }

    components::PrefabMemberComponent member_component{};
    member_component.root = root;
    member_component.name = entry.name;
    member_component.local_transform = entry.local_transform;
    switch (entry.type) {
      case PrefabEntry::Type::Mesh:
        member_component.kind = components::PrefabMemberKind::Mesh;
        member_component.mesh_visible = entry.mesh.visible;
        break;
      case PrefabEntry::Type::Particle:
        member_component.kind = components::PrefabMemberKind::Particle;
        break;
      case PrefabEntry::Type::Light: {
        member_component.kind = components::PrefabMemberKind::Light;
        components::LightComponent light = entry.light.light;
        if (entry.light.intensity_binding.enabled) {
          light.intensity =
              resolveFloatBinding(entry.light.intensity_binding, resolved_params, light.intensity);
        }
        if (entry.light.range_binding.enabled) {
          light.range = resolveFloatBinding(entry.light.range_binding, resolved_params, light.range);
        }
        member_component.light_intensity = light.intensity;
        member_component.light_range = light.range;
        break;
      }
      case PrefabEntry::Type::Beam:
        member_component.kind = components::PrefabMemberKind::Beam;
        member_component.beam_visible = entry.beam.beam.visible;
        break;
      case PrefabEntry::Type::VolumeSphere:
        member_component.kind = components::PrefabMemberKind::VolumeSphere;
        member_component.volume_sphere_visible = entry.volume_sphere.volume.visible;
        break;
    }
    world.add(member, std::move(member_component));

    instance_component.members.push_back(member);
    instance.members.push_back(member);
    instance.named_members[entry.name] = member;
  }

  world.add(root, std::move(instance_component));
  return instance;
}

std::optional<PrefabInstance> instantiatePrefab(
    ecs::World& world,
    renderer::GraphicsDevice* graphics,
    const std::filesystem::path& path,
    const PrefabInstantiateDesc& desc) {
  std::optional<Prefab> prefab = loadPrefab(path);
  if (!prefab.has_value()) {
    return std::nullopt;
  }
  return instantiatePrefab(world, graphics, *prefab, desc);
}

bool setPrefabPlayback(ecs::World& world, ecs::Entity root, bool enabled) {
  if (!world.isAlive(root) || !world.has<components::PrefabInstanceComponent>(root)) {
    return false;
  }

  auto& instance = world.get<components::PrefabInstanceComponent>(root);
  instance.enabled = enabled;

  for (const ecs::Entity member : instance.members) {
    if (!world.isAlive(member)) {
      continue;
    }

    if (world.has<components::PrefabMemberComponent>(member)) {
      const auto& prefab_member = world.get<components::PrefabMemberComponent>(member);
      if (prefab_member.kind == components::PrefabMemberKind::Mesh &&
          world.has<components::MeshComponent>(member)) {
        world.get<components::MeshComponent>(member).visible =
            enabled && prefab_member.mesh_visible;
      } else if (prefab_member.kind == components::PrefabMemberKind::Beam &&
                 world.has<components::BeamPathComponent>(member)) {
        world.get<components::BeamPathComponent>(member).visible =
            enabled && prefab_member.beam_visible;
      } else if (prefab_member.kind == components::PrefabMemberKind::Light &&
                 world.has<components::LightComponent>(member)) {
        auto& light = world.get<components::LightComponent>(member);
        light.intensity = enabled ? prefab_member.light_intensity : 0.0f;
        light.range = enabled ? prefab_member.light_range : 0.0f;
      } else if (prefab_member.kind == components::PrefabMemberKind::VolumeSphere &&
                 world.has<components::VolumeSphereComponent>(member)) {
        world.get<components::VolumeSphereComponent>(member).visible =
            enabled && prefab_member.volume_sphere_visible;
      }
    }

    if (world.has<components::ParticleEmitterComponent>(member)) {
      particles::setEffectPlayback(world, member, enabled, enabled);
    }
  }

  return true;
}

bool restartPrefab(ecs::World& world, ecs::Entity root) {
  if (!world.isAlive(root) || !world.has<components::PrefabInstanceComponent>(root)) {
    return false;
  }

  auto& instance = world.get<components::PrefabInstanceComponent>(root);
  bool restarted_any = false;
  for (const ecs::Entity member : instance.members) {
    restarted_any = particles::restartEffect(world, member) || restarted_any;
  }
  return restarted_any;
}

bool destroyPrefab(ecs::World& world, ecs::Entity root) {
  if (!world.isAlive(root) || !world.has<components::PrefabInstanceComponent>(root)) {
    return false;
  }

  const auto members = world.get<components::PrefabInstanceComponent>(root).members;
  for (const ecs::Entity member : members) {
    if (world.isAlive(member)) {
      world.destroyEntity(member);
    }
  }

  world.destroyEntity(root);
  return true;
}

}  // namespace karma::prefabs
