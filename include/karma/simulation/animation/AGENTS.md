# Animation Public API Notes

This directory exposes public animation APIs.

Before changing headers, read:

- `NEXT_AGENT.md`
- `docs/RIGGED_GLB_AUTHORING.md`
- `src/simulation/animation/AGENTS.md`

API constraints:

- Keep `AnimationClip` data renderer-agnostic.
- Channels currently target imported GLB node indices. If adding skeleton-level
  APIs, make the mapping explicit rather than overloading node animation fields.
- `skinMesh(...)` is exposed for unit tests and CPU fallback behavior; avoid
  growing it into a full renderer abstraction.
- Helper behavior for clip switching, play, pause, and stop should remain
  documented in tests.
