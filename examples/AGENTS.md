# Agent Instructions

When adding or moving examples, keep example source files under the appropriate
category subdirectory. Do not add new standalone example sources directly under
`examples/`.

Use the existing categories unless there is a clear reason to extend the
hierarchy:

- `animation`
- `effects`
- `gameplay`
- `navigation`
- `navigation/samples`
- `network`
- `particles`
- `physics`
- `prefabs`
- `rendering`
- `scene`
- `ui`

Shared example helpers belong under `examples/common/`. Runtime assets belong
under `examples/assets/`.

Normalize source filenames by purpose inside the category, for example
`examples/rendering/gltf_viewer.cpp` or `examples/physics/shape_gallery.cpp`.
Do not preserve old prefixes such as `karma_`, `render_`, `navigation_`, or
backend/vendor names in filenames. Physics examples should be named as physics
examples, not as Jolt examples.

When wiring examples in CMake, keep targets and output paths aligned with the
directory hierarchy:

- target names should use the category prefix, such as `rendering_gltf_viewer`
  or `physics_shape_gallery`
- binaries should be emitted under `build/examples/<category>/...`
- avoid creating new example binaries at the top level of `build/`

When renaming or moving examples, update `cmake/KarmaExamples.cmake`,
`examples/README.md`, top-level documentation references, and any smoke-test
commands that mention the old path or target.
