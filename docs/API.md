# Karma API Documentation

Karma's exhaustive API reference is generated from public headers under
`include/karma/**` with Doxygen. The generated HTML is a build artifact and
should not be committed.

## Generate Locally

```bash
cmake -S . -B build -DKARMA_FETCH_DEPS=ON
cmake --build build --target karma_docs_api
```

Open the generated entry point:

```text
build/docs/api/html/index.html
```

If CMake reports that Doxygen is missing, install `doxygen` and rebuild the
`karma_docs_api` target.

## What Belongs Where

- Public API contracts belong in Doxygen comments in `include/karma/**`.
- Official narrative API pages belong under `docs/api/` and must be listed in
  `docs/Doxyfile.in`.
- Durable workflow guides belong under `docs/`.
- Runnable examples belong under `examples/`.
- Short-lived continuation notes belong in `NEXT_AGENT.md`.
- Generated HTML belongs under the build directory only.

## Documentation Standard

When adding or changing public APIs:

1. Add a brief comment for every public class, struct, enum, and free function.
2. Document ownership and lifetime when pointers, references, handles, or
   backend resources are involved.
3. Document ECS contracts for components and systems: who writes the data, who
   consumes it, and when it is updated.
4. Link to a runnable example or guide when an API is normally used by gameplay
   code.
5. Keep implementation details in source comments unless they affect public
   behavior.

Small data-only component fields do not need a paragraph each, but the component
itself should explain how the fields are interpreted.

Current handwritten generated-reference pages:

- `docs/api/mainpage.md`: generated API entry point.
- `docs/api/animation.md`: animation, skinning, and morph-target runtime flow.
- `docs/api/rendering.md`: renderer submission, camera post-process profiles,
  backend shader assets, and shadow authoring.
- `docs/api/groups.dox`: Doxygen module/group definitions.
