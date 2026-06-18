# Animation Public API Notes

This directory exposes public animation APIs.

Before changing headers, read:

- `docs/NEXT_AGENT.md`
- `docs/ANIMATION_V2.md`
- `docs/RIGGED_GLTF_AUTHORING.md`
- `src/simulation/animation/AGENTS.md`

API constraints:

- Keep `AnimationClip` data renderer-agnostic.
- Channels currently target imported glTF node indices. If adding skeleton-level
  APIs, make the mapping explicit rather than overloading node animation fields.
- `skinMesh(...)` and `morphMesh(...)` are exposed for unit tests and CPU
  fallback behavior; avoid growing them into renderer abstractions.
- Helper behavior for clip switching, play, pause, and stop should remain
  documented in tests.
