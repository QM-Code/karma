# Agent Instructions

Before adding or moving engine modules, read `docs/ARCHITECTURE.md`.

Do not add new top-level engine subsystems under `src/` or `include/karma/`.
Use the layered hierarchy:

- `core`
- `world`
- `simulation`
- `rendering`
- `media`
- `content`
- `platform`
- `features`
- `runtime`

Dependency direction:

- Foundational layers must not include higher-level orchestration layers.
- `runtime` wires subsystems together.
- Backends stay under their owning subsystem.
- Public include paths must use layered `karma/...` paths.
- Do not add forwarding headers for old public include paths.
