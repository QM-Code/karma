# Karma Examples

The built examples are grouped by domain. CMake target names use the category as
the prefix, and executable names are normalized inside matching output
directories under `build/examples/<category>/`.

Shared example helpers live in `examples/common/`.

## Gameplay

- `gameplay/tank.cpp`: target `gameplay_tank`, output `examples/gameplay/tank`.
  Base tank/world movement demo with follow camera, radar camera, HDR lighting,
  local lights, and a native HUD that displays the borrowed radar render target.
  An ImGui fallback remains available only when that optional provider is built.

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
  distance, hinge, slider, cone, swing-twist, and six-DOF constraints. Its
  controls and live statistics use native UI when available, with an optional
  ImGui fallback.
- `physics/car.cpp`: target `physics_car`, output `examples/physics/car`.
  Vehicle sample with ImGui tuning, wheel/contact/suspension telemetry, road
  obstacles, chase/free camera, drive modes, and collision tester selection.

## Rendering

- `rendering/gltf_viewer.cpp`: target `rendering_gltf_viewer`, output
  `examples/rendering/gltf_viewer`. Karma-native GLTFViewer-style inspection
  sample using a baked startup scene asset for the DamagedHelmet package.
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
- `rendering/material_assignment.cpp`: target `rendering_material_assignment`,
  output `examples/rendering/material_assignment`. Runtime material assignment
  example with side-by-side GLB tints.
- `rendering/grass_card.cpp`: target `rendering_grass_card`, output
  `examples/rendering/grass_card`. Simple material scene with a lit ground
  plane, free camera, and transparent double-sided upright grass image plane.
- `rendering/grass_field.cpp`: target `rendering_grass_field`, output
  `examples/rendering/grass_field`. Instanced grass field over a 100x80 ground
  plane using masked alpha and per-instance transform/parameter payloads. Use
  `--aa none|msaa|ssaa|taa`, `--msaa-samples`, `--ssaa-scale`, and `--instances`
  to select startup quality and load.
- `rendering/terrain.cpp`: target `rendering_terrain`, output
  `examples/rendering/terrain`. Fixed-size heightmap terrain renderer sample
  using `examples/assets/Heightmap.png`, with GPU tessellation when available
  and CPU grid fallback.

## Scene And Animation

- `assets/scene_editor_content/`: self-contained content root for the
  standalone scene editor, with a launchable scene and linked light, grass-LOD,
  and pine-tree-LOD prefabs.
  See [its README](assets/scene_editor_content/README.md) for the command.

- `scene/gltf_import.cpp`: target `scene_gltf_import`, output
  `examples/scene/gltf_import`. Static FBX and GLB package-import comparison,
  normalized and displayed side by side without animation playback.
- `scene/world_bake.cpp`: target `scene_world_bake`, output
  `examples/scene/world_bake`. Loads the baked world scene document, prefers
  baked package blobs for the packaged GLB, and renders it with a skybox.
- `animation/gltf.cpp`: target `animation_gltf`, output `examples/animation/gltf`.
  Rigged glTF/GLB animation showcase using the
  `examples/assets/animation/dustbound_wayfarer/assets.package.json` package by
  default, with ImGui clip playback, crossfade, auto-cycle, deformation, and
  root-motion controls. The optional first command-line argument is another
  package directory or manifest; the optional second argument is its scene
  asset key.
  Build with `cmake --build --preset portable --target animation_gltf` and run
  `./build/portable/examples/animation/gltf path/to/assets.package.json characters/hero`.
- `animation/humanoid_rpg.cpp`: target `animation_humanoid_rpg`, output
  `examples/animation/humanoid_rpg`. Imports the checked-in Mixamo character FBX
  and nine standalone FBX clips through asset packages, binds the built-in
  Mixamo humanoid profile, and retargets every clip to the model. The UI exposes
  clip selection, playback, crossfade, and GPU/CPU reference deformation.
  Build with `cmake --build --preset portable --target animation_humanoid_rpg`
  and run `./build/portable/examples/animation/humanoid_rpg`.
  See [../docs/ANIMATION_V2.md](../docs/ANIMATION_V2.md) and
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
- `particles/generated_preview.cpp`: target `particles_generated_preview`,
  output `examples/particles/generated_preview`. Loads a generated particle
  package path, accepts a `.kpspec.json` for auto-generation when tools are
  enabled, accepts `--scene`/`--scenery` for preview geometry, and defaults to
  the fire-ray spec with no arguments.
- `particles/generated_preview.cpp`: target `particles_generated_scale_preview`,
  output `examples/particles/generated_scale_preview`. Uses the same particle
  package loading path, but adds a stationary capsule actor and ground grid for
  judging effect scale.
- Generated spell effect prefabs are checked in under `assets/prefabs/` for
  direct loading: `arcane_barrage`, `blade_barrier`, `breathe_fire`,
  `chromatic_ray`, `daze`, `detect_magic`, `fire_ray`, `fireball`, `heal`,
  `haste`, `impact_burst`, and `magic_missile`. Their source specs live in
  `particles/specs/`.
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

- `ui/native.cpp`: target `ui_native`, output `examples/ui/native`. First-party
  retained `.kui.json5`/`.kstyle.json5` menu demonstrating explicit bindings,
  widgets, keyed lists, actions, localization-ready text, modal input, themed
  scrollbars, controller-based ownership, direct-file hot reload in development,
  and packaged font/SVG fallback. See
  [Native UI](../docs/NATIVE_UI.md) and the
  [implementation roadmap](../docs/NATIVE_UI_STATUS.md).
- `ui/showcase.cpp`: target `ui_showcase`, output `examples/ui/showcase`.
  Exhaustive provider-free native UI forge covering JSON5 theme imports and
  hot reload, localization/RTL, bindings, conditions, keyed repeats, virtual
  lists, tabs, trees, disclosures, select/popup/menu/tooltip overlays,
  splitters, floating windows, per-axis scrollbars, Grid/Flex/anchors,
  raster/SVG/dynamic images, motion, accessibility metadata, and generated
  medieval nine-slice skinning. Its live files are under
  `examples/assets/ui/showcase/`.
- `ui/imgui.cpp`: target `ui_imgui`, output `examples/ui/imgui` when ImGui and
  its demo are enabled.
- `ui/rmlui.cpp`: target `ui_rmlui`, output `examples/ui/rmlui` when RmlUi is
  enabled.
- `network/server.cpp`: target `network_server`, output
  `examples/network/server`.
- `network/client.cpp`: target `network_client`, output
  `examples/network/client`.
- `network/discovery_directory.cpp`: target `network_discovery_directory`,
  output `examples/network/discovery_directory`. Graphical server-directory lab
  for LAN advertise/query/cache events, embedded local server probes, cache
  sorting/filtering/pinning, fake master-list integration, and selected-server
  client connection probes. `network/http_master_adapter.h` is a header-only
  reference adapter for wiring `IMasterServerClient` to a game-provided JSON
  HTTP transport.
