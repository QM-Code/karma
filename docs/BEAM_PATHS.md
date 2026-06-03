# Beam Paths

Karma beam paths are an ECS-authored runtime effect for rendering an ordered list
of 3D points as a continuous energy beam.

## Authoring

Use [BeamPathComponent](../include/karma/world/components/beam_path.h)
to describe the path and its look:

- `points`: ordered path vertices
- `core_color`: hot inner beam color
- `glow_color`: outer beam color
- `core_radius` / `glow_radius`: beam thickness
- `endpoint_core_size` / `endpoint_glow_size`: flare size at each point
- `light_count` / `light_intensity` / `light_range`: optional beam-driven point lights
- `light_spacing`: when set, distributes lights along the full beam length instead of using a fixed count
- `electric_intensity` / `electric_size` / `electric_spacing`: subtle electric shimmer riding the beam
- `electric_jitter_radius` / `electric_speed`: how far the shimmer wanders from the beam and how fast it animates
- `distortion_intensity` / `distortion_size` / `distortion_spacing`: optional heat-shimmer shell around the beam
- `distortion_jitter_radius` / `distortion_strength` / `distortion_speed`: how wide, strong, and fast the distortion wobbles
- `distortion_soft_particle_distance`: optional depth fade distance for distortion against nearby geometry
- `world_space`: treat points as world-space instead of local-space
- `closed_loop`: connect the final point back to the first point

## Helper API

For concise ECS setup, use
[beam_path_api.h](../include/karma/features/visual/beams/beam_path_api.h):

```cpp
const std::vector<karma::math::Vec3> points = {
    {-6.0f, 2.0f, -2.0f},
    {-1.0f, 3.5f, 1.0f},
    {4.0f, 2.4f, 0.5f},
};

karma::beams::createBeamPathEntity(
    world,
    karma::beams::BeamPathEntityDesc{
        .name = "Beam",
        .beam =
            karma::components::BeamPathComponent{
                .points = points,
                .core_color = {1.0f, 1.0f, 1.0f, 1.0f},
                .glow_color = {0.2f, 0.8f, 1.0f, 1.0f},
                .light_count = 0,
                .light_intensity = 1.4f,
                .light_range = 3.2f,
                .light_spacing = 1.0f,
                .electric_intensity = 0.5f,
                .electric_speed = 1.5f,
                .distortion_intensity = 0.2f,
                .distortion_strength = 3.5f,
                .world_space = true,
            },
    });
```

To update an existing beam, call `setBeamPathPoints(...)`,
`setBeamPathColors(...)`, or `setBeamPathVisible(...)`.

Beam paths can also be authored as JSON prefabs by saving an entity subtree with
a real `BeamPathComponent` on one of its entities. The prefab component payload
uses the same field names as the ECS component:

```json
{
  "components": {
    "BeamPathComponent": {
      "points": [[-6, 2, -2], [0, 3, 0], [5, 2, 1]],
      "core_color": [1, 1, 1, 1],
      "glow_color": [1, 0.18, 0.14, 1],
      "core_radius": 0.21,
      "glow_radius": 0.56,
      "visible": true
    }
  }
}
```

Instantiate authored beam prefabs with `prefabs::instantiatePrefab(...)`. Use
runtime setters only for gameplay-driven variation after load, or save a
separate JSON prefab for each authored variant.

## Runtime

Register
[BeamPathRuntimeModule](../include/karma/features/visual/beams/beam_path_runtime_module.h)
with `EngineApp` before `start(...)` to enable rendering for entities with
`BeamPathComponent`:

```cpp
engine.addRuntimeModule(std::make_unique<karma::beams::BeamPathRuntimeModule>());
```

That module owns
[BeamPathSystem](../include/karma/features/visual/beams/beam_path_system.h)
and builds and updates the render data automatically:

- repeated additive hot-core particles along the full path
- additive endpoint flares at every authored point
- optional point lights distributed across the path, either by fixed count or by spacing
- optional tight electric shimmer particles riding the beam
- optional distortion particles that refract the scene around the beam

The current end-to-end sample is
[laser_example.cpp](../examples/laser_example.cpp).
For the fixed-camera minimal prefab scene, see
[laser_prefab_example.cpp](../examples/laser_prefab_example.cpp).
