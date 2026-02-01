# TODO (Ranked)

1. Add GPU instancing for identical meshes/materials (reduce draw calls).
2. Add render batching/sorting by pipeline/material/mesh (reduce state changes).
3. Add occlusion culling (HZB or occlusion queries) to skip hidden geometry.
4. Add async asset loading + streaming (avoid blocking loads on the main thread).
5. Add texture streaming + mip residency control (reduce VRAM spikes).
6. Add render graph / pass scheduler (explicit dependencies, fewer redundant transitions).
7. Add PSO/shader caching + warmup (reduce hitching).
8. Add material/shader feature variants (avoid sampling unused textures).
9. Add shadow atlas or cached shadow maps for static lights (reduce shadow cost).
10. Add CPU-side multithreaded render prep (parallel culling, sorting, uploads).
