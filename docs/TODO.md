# TODO (Ranked)

Completed:
- [x] Add runtime-configurable shadow tuning in Debug UI (map size, const/receiver/normal bias, PCF radius, raster depth/slope bias).
- [x] Stabilize directional shadows (fixed light-space near/far depth mapping and tuned default bias values).
- [x] Add explicit render instance retirement plus mesh/material cleanup on despawn and mesh-key changes.
- [x] Remove duplicate Assimp bounds import from `RenderSystem`; use backend mesh bounds from the existing mesh load path.
- [x] Replace per-frame ECS `view()` allocations in render/physics/audio with allocation-free `World::forEach` traversal.
- [x] Add GPU instancing for identical meshes/materials (reduce draw calls).

1. Add render batching/sorting by pipeline/material/mesh (reduce state changes).
2. Add occlusion culling (HZB or occlusion queries) to skip hidden geometry.
3. Add async asset loading + streaming (avoid blocking loads on the main thread).
4. Add texture streaming + mip residency control (reduce VRAM spikes).
5. Add render graph / pass scheduler (explicit dependencies, fewer redundant transitions).
6. Add PSO/shader caching + warmup (reduce hitching).
7. Add material/shader feature variants (avoid sampling unused textures).
8. Add shadow atlas or cached shadow maps for static lights (reduce shadow cost).
9. Add CPU-side multithreaded render prep (parallel culling, sorting, uploads).
