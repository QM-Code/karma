# Karma Architecture

Karma uses a layered directory hierarchy. The folder path should make ownership
and dependency direction clear before reading implementation details.

## Layers

- `core`: foundational IDs, type IDs, and math. Depends only on the standard
  library and small third-party math types already used by core math. Shared
  scalar, vector, quaternion, and GLM interop helpers belong here rather than in
  subsystem-local anonymous helpers.
- `world`: ECS, components, scene graph, and system graph. Defines shared data
  contracts.
- `simulation`: animation, physics, collision, and navigation behavior.
- `rendering`: renderer-facing APIs, renderer systems, and renderer backends.
- `media`: audio-facing APIs, audio systems, and audio backends.
- `content`: asset loading, importers, geometry, prefabs, and content runtime.
- `platform`: window and network infrastructure edges.
- `features`: optional feature modules and provider adapters built from
  lower-level systems.
- `runtime`: application composition, input, UI context, and debug overlays.

## Dependency Direction

Prefer dependencies from higher-level orchestration toward lower-level data and
services:

```text
core <- world <- simulation/rendering/media/content/platform <- features <- runtime
```

Rules:

- `core` must not include other Karma layers.
- `world` may depend on `core`; it should not depend on `runtime`.
- `simulation`, `rendering`, and `media` may depend on `core` and `world`.
- `content` may depend on `core`, `world`, and narrow subsystem data needed to
  instantiate assets.
- `features` may depend on `core`, `world`, and the public subsystem APIs they
  are built from.
- `runtime` may depend on most layers because it wires the engine together.
- Lower layers should not call higher layers directly. Use data, callbacks,
  registration, or a narrow interface instead.

## Placement Rules

- Public headers live under `include/karma/<layer>/...`.
- Source files mirror the public layer structure under `src/`.
- Backend implementations stay under their owning subsystem:
  `rendering/renderer/backends`, `simulation/physics/backends`, and
  `media/audio/backends`.
- Importers and asset-specific loading belong in `content`.
- ECS components belong in `world/components`, even when a subsystem consumes
  them.
- Visual feature modules belong in `features/visual`, not renderer internals.
- UI provider adapters belong in `features/ui/<provider>`. The shared engine
  contract is `runtime/app/UiLayer`; provider-specific setup belongs behind a
  factory such as `karma::imgui::createUiLayer(...)`.

## Public API Rules

- Do not preserve old include paths with forwarding headers.
- Keep public headers narrow. Split broad type aggregators when they mix
  unrelated contracts.
- Keep concrete backend internals private unless an example or integration needs
  a small public access surface.
- Keep math operations centralized under `karma/core/math`. Use
  `karma/core/math/glm.h` for conversions between engine math types and GLM at
  renderer, physics, content, and example boundaries. Do not add subsystem-local
  `Vec3`/`Quat` conversion, clamp, interpolation, or scale helpers when the
  shared math API covers the operation.
