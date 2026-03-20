# Beam Paths

Karma beam paths are an ECS-authored runtime effect for rendering an ordered list
of 3D points as a continuous energy beam.

## Authoring

Use [BeamPathComponent](/home/irie/Documents/karma/include/karma/components/beam_path.h)
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
[beam_path_api.h](/home/irie/Documents/karma/include/karma/beams/beam_path_api.h):

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

Beam paths can also be authored inside a `.kprefab` through a `[beam name]`
section, then driven at runtime by looking up the named member and calling
`setBeamPathPoints(...)` or `setBeamPathColors(...)`.

## Runtime

The engine-owned
[BeamPathSystem](/home/irie/Documents/karma/include/karma/beams/beam_path_system.h)
builds and updates the render data automatically:

- repeated additive hot-core particles along the full path
- additive endpoint flares at every authored point
- optional point lights distributed across the path, either by fixed count or by spacing
- optional tight electric shimmer particles riding the beam
- optional distortion particles that refract the scene around the beam

The current end-to-end sample is
[laser_example.cpp](/home/irie/Documents/karma/examples/laser_example.cpp).
For the minimal prefab-only path, see
[laser_prefab_example.cpp](/home/irie/Documents/karma/examples/laser_prefab_example.cpp).
