# TODO (Ranked)

Completed:
- [x] Add Forward+ tiled GPU local-light culling and per-tile light lists.
- [x] Enable point-light and spot-light rendering through the Forward+ local-light path, with runtime tile tuning/stats in Debug UI.
- [x] Add point-light shadow maps (cubemap-style 6-face depth rendering per light) and integrate sampling in local-light shading.
- [x] Add shadow-update caching/scheduling (directional cache invalidation thresholds + budgeted point-shadow face updates).
- [x] Add a depth pre-pass for multi-batch forward rendering to reduce overdraw in heavy scenes.
- [x] Harden render stability on Vulkan/NVIDIA: validate indexed draw ranges and gate depth pre-pass on known driver-crash path.
- [x] Implement cascaded shadow maps (4 splits) using a shadow texture array and per-cascade render pass.
- [x] Stabilize CSM against camera motion/rotation via texel-snapped cascade centers in a stable light-space basis.
- [x] Add CSM cascade-transition blending and comparison-linear shadow sampling to reduce split seams and flicker.
- [x] Fix stabilized CSM light-view handedness/depth ordering regression that caused self-shadow artifacts and missing receiver shadows.
- [x] Add runtime-configurable shadow tuning in Debug UI (map size, const/receiver/normal bias, PCF radius, raster depth/slope bias).
- [x] Stabilize directional shadows (fixed light-space near/far depth mapping and tuned default bias values).
- [x] Add explicit render instance retirement plus mesh/material cleanup on despawn and mesh-key changes.
- [x] Remove duplicate Assimp bounds import from `RenderSystem`; use backend mesh bounds from the existing mesh load path.
- [x] Replace per-frame ECS `view()` allocations in render/physics/audio with allocation-free `World::forEach` traversal.
- [x] Add GPU instancing for identical meshes/materials (reduce draw calls).
- [x] Add `KARMA_PARTICLE_STATS=1` renderer particle diagnostics and capture first explosion/gallery bottleneck baselines.
- [x] Reduce additive particle grouping overhead by avoiding the extra per-group particle copy.
- [x] Route analytic `VolumetricSphere` draws after particles so foreground volumes are not permanently hidden behind beam particles.
- [x] Sort analytic `VolumetricSphere` transparent draws by real sphere center depth instead of screen-proxy quad depth.
- [x] Composite analytic `VolumetricSphere` draws with path-length transparent alpha so foreground volumes no longer fully erase background volumes.

1. Add render batching/sorting by pipeline/material/mesh (reduce state changes).
2. Add occlusion culling (HZB or occlusion queries) to skip hidden geometry.
3. Add async asset loading + streaming (avoid blocking loads on the main thread).
4. Add texture streaming + mip residency control (reduce VRAM spikes).
5. Add render graph / pass scheduler (explicit dependencies, fewer redundant transitions).
6. Add PSO/shader caching + warmup (reduce hitching).
7. Add material/shader feature variants (avoid sampling unused textures).
8. Add shadow atlas or cached shadow maps for static lights (reduce shadow cost).
9. Add CPU-side multithreaded render prep (parallel culling, sorting, uploads).

## Transparent Effects Queue

1. Design a shared transparent-effect depth/composition strategy for beams, particles, wave volumes, and analytic volume spheres.
2. Add a focused visual regression scene with beams crossing in front of and behind a volumetric sphere.
3. Respect material-level `depth_test` / `depth_write` in transparent pipeline selection instead of relying on fixed transparent PSOs.
4. Decide whether selected transparent effects need an optional depth prepass/proxy depth write for intentional occlusion.

## Particle System Optimization Queue

Reference analysis:

- [PARTICLE_SYSTEM_ANALYSIS.md](PARTICLE_SYSTEM_ANALYSIS.md)
- [PARTICLE_EFFECT_GENERATION.md](PARTICLE_EFFECT_GENERATION.md)

1. Reduce packing cost for high-count one-shot emitters.
2. Isolate simulation cost from ground-collision-heavy emitters.
3. Add persistent/reused packed-batch storage or a lower-copy particle submission path.
4. Prototype bucketed or approximate alpha sorting for high-count smoke layers only after grouping/packing costs are reduced.
5. Introduce particle material/state IDs to reduce duplicated renderer state and oversized batch keys.
6. Split particle shader variants so standard/additive particles do not carry distortion, shell, and soft-depth complexity.
7. Evaluate SoA particle storage for CPU simulation cache behavior.
8. Add scoped particle package registration handles for effect files and texture aliases.
9. Add validation tooling for `.kpeffect` files and particle prefab entries.
10. Add a `.kpeffect` schema/formatter and generator-safe preset layer.
11. Add a generated-effect preview harness that can capture screenshots and `KARMA_PARTICLE_STATS=1` output.
12. Consider threaded CPU simulation after emitter state ownership and benchmark coverage are clearer.
13. Treat full GPU simulation as a separate architecture project, not an incremental optimization.
