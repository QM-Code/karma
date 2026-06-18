# Karma Examples

The built examples are grouped by domain. CMake target names use the category as
the prefix, and executable names are normalized inside matching output
directories under `build/examples/<category>/`.

Shared example helpers live in `examples/common/`.

## Gameplay

- `gameplay/tank.cpp`: target `gameplay_tank`, output `examples/gameplay/tank`.
  Base tank/world movement demo with follow camera, radar camera, HDR lighting,
  local lights, and a small ImGui overlay.

## Physics

- `physics/collision_events.cpp`: target `physics_collision_events`, output
  `examples/physics/collision_events`. Drivable trigger/contact demo with
  contact listeners, ground/support state, jump, and collision overlays.
- `physics/shape_gallery.cpp`: target `physics_shape_gallery`, output
  `examples/physics/shape_gallery`. Rigid-body shape gallery for dynamic
  primitives, static mesh/height-field surfaces, materials, CCD, sleeping,
  triggers, collision filters, impulses, continuous forces, and grounded state.
- `physics/body_controls.cpp`: target `physics_body_controls`, output
  `examples/physics/body_controls`. Body-control lab for shape replacement,
  kinematic toggling, motion quality, gravity, activation/sleep, velocity,
  forces, torque, impulses, teleport, material/user data, and buoyancy.
- `physics/query_lab.cpp`: target `physics_query_lab`, output
  `examples/physics/query_lab`. Ray, all-ray, point-overlap, shape-overlap, and
  shape-cast lab with masks, ignored handles, back-face modes, scale, and
  shrunken/deepest casts.
- `physics/constraint_lab.cpp`: target `physics_constraint_lab`, output
  `examples/physics/constraint_lab`. ECS constraint editor for fixed, point,
  distance, hinge, slider, cone, swing-twist, and six-DOF constraints.
- `physics/car.cpp`: target `physics_car`, output `examples/physics/car`.
  Vehicle sample with ImGui tuning, wheel/contact/suspension telemetry, road
  obstacles, chase/free camera, drive modes, and collision tester selection.

## Rendering

- `rendering/gltf_viewer.cpp`: target `rendering_gltf_viewer`, output
  `examples/rendering/gltf_viewer`. Karma-native GLTFViewer-style inspection
  sample using local copied assets and imported materials.
- `rendering/postprocess.cpp`: target `rendering_postprocess`, output
  `examples/rendering/postprocess`. Bloom, tone/color controls, SSAO,
  screen-space reflections, TAA, and depth of field.
- `rendering/bloom.cpp`: target `rendering_bloom`, output
  `examples/rendering/bloom`. Focused bloom scene using local reference media,
  emissive materials, HDR lights, and free camera controls.
- `rendering/postwar_city.cpp`: target `rendering_postwar_city`, output
  `examples/rendering/postwar_city`. Free-fly inspection scene for the postwar
  city GLB.
- `rendering/light_stress.cpp`: target `rendering_light_stress`, output
  `examples/rendering/light_stress`. Forward+ local-light and point-shadow
  probe with `--lights`, `--stats`, and unsafe stress modes.
- `rendering/material_override.cpp`: target `rendering_material_override`,
  output `examples/rendering/material_override`. Runtime material binding
  example with side-by-side GLB tints.
- `rendering/terrain.cpp`: target `rendering_terrain`, output
  `examples/rendering/terrain`. Fixed-size heightmap terrain renderer sample
  using `examples/assets/Heightmap.png`, with GPU tessellation when available
  and CPU grid fallback.

## Scene And Animation

- `scene/gltf_import.cpp`: target `scene_gltf_import`, output
  `examples/scene/gltf_import`. Minimal authored glTF/GLB scene import example.
- `animation/gltf.cpp`: target `animation_gltf`, output `examples/animation/gltf`.
  Rigged glTF/GLB animation showcase using
  `animation_model/source/dustbound_wayfarer_merged_animations.glb` by default,
  with ImGui clip playback, crossfade, auto-cycle, deformation, and root-motion
  controls. An optional first command-line argument can point at another glTF/GLB
  model. See [../docs/ANIMATION_V2.md](../docs/ANIMATION_V2.md) and
  [../docs/RIGGED_GLTF_AUTHORING.md](../docs/RIGGED_GLTF_AUTHORING.md).

## Particles, Effects, And Prefabs

- `particles/billboard.cpp`: target `particles_billboard`, output
  `examples/particles/billboard`. Minimal billboard particle example with
  file-backed `.kpeffect` assets and hot reload.
- `particles/gallery.cpp`: target `particles_gallery`, output
  `examples/particles/gallery`. Particle prefab gallery for beam impostors,
  energy orbs, and explosions.
- `particles/explosion_stress.cpp`: target `particles_explosion_stress`, output
  `examples/particles/explosion_stress`. Configurable staged-explosion stress
  scene.
- `effects/laser.cpp`: target `effects_laser`, output `examples/effects/laser`.
- `effects/energy_orb.cpp`: target `effects_energy_orb`, output
  `examples/effects/energy_orb`.
- `effects/wave.cpp`: target `effects_wave`, output `examples/effects/wave`.
- `effects/volumetric_sphere.cpp`: target `effects_volumetric_sphere`, output
  `examples/effects/volumetric_sphere`.
- `prefabs/laser.cpp`: target `prefabs_laser`, output `examples/prefabs/laser`.
- `prefabs/volumetric_sphere.cpp`: target `prefabs_volumetric_sphere`, output
  `examples/prefabs/volumetric_sphere`.
- `prefabs/gallery.cpp`: target `prefabs_gallery`, output
  `examples/prefabs/gallery`.
- `prefabs/particle_isolation.cpp`: target `prefabs_particle_isolation`, output
  `examples/prefabs/particle_isolation`.

## Navigation

- `navigation/navmesh.cpp`: target `navigation_navmesh`, output
  `examples/navigation/navmesh`. Click-to-move sample over the GLB world.
- `navigation/samples/headless.cpp`: target `navigation_samples_headless`,
  output `examples/navigation/samples/headless`. Headless parity runner for the
  copied upstream navigation sample assets.
- `navigation/samples/solo_mesh.cpp`: target `navigation_solo_mesh`, output
  `examples/navigation/samples/solo_mesh`.
- `navigation/samples/tile_mesh.cpp`: target `navigation_tile_mesh`, output
  `examples/navigation/samples/tile_mesh`.
- `navigation/samples/temp_obstacles.cpp`: target `navigation_temp_obstacles`,
  output `examples/navigation/samples/temp_obstacles`.
- `navigation/samples/debug.cpp`: target `navigation_debug`, output
  `examples/navigation/samples/debug`.
- `navigation/samples/gallery.cpp`: target `navigation_samples_gallery`, output
  `examples/navigation/samples/gallery`.
- `navigation/point_click.cpp`: target `navigation_point_click`, output
  `examples/navigation/point_click`.
- `navigation/crowds.cpp`: target `navigation_crowds`, output
  `examples/navigation/crowds`.
- `navigation/tile_cache.cpp`: target `navigation_tile_cache`, output
  `examples/navigation/tile_cache`.
- `navigation/query_lab.cpp`: target `navigation_query_lab`, output
  `examples/navigation/query_lab`.
- `navigation/offmesh_areas.cpp`: target `navigation_offmesh_areas`, output
  `examples/navigation/offmesh_areas`.
- `navigation/physics_bridge.cpp`: target `navigation_physics_bridge`, output
  `examples/navigation/physics_bridge`.

## UI And Network

- `ui/imgui.cpp`: target `ui_imgui`, output `examples/ui/imgui`.
- `ui/rmlui.cpp`: target `ui_rmlui`, output `examples/ui/rmlui` when RmlUi is
  enabled.
- `network/server.cpp`: target `network_server`, output
  `examples/network/server`.
- `network/client.cpp`: target `network_client`, output
  `examples/network/client`.
