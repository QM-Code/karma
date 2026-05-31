# Source Layout Rules

Implementation files mirror `include/karma` by layer.

Backends remain nested under their owner:

- `src/simulation/physics/backends/*`
- `src/rendering/renderer/backends/*`
- `src/media/audio/backends/*`

Do not introduce implementation-only code at old flat paths such as:

- `src/physics`
- `src/renderer`
- `src/components`

Keep implementation helpers close to the subsystem that owns them. If a helper
is needed across layers, promote a small public abstraction instead of including
source-private headers from another layer.
