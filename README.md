# Karma sandbox

This folder holds a minimal, abstract engine skeleton you can grow into a full
ECS + scene graph. It is intentionally decoupled from rendering/physics/etc so
other layers can bind components to systems.

## Layout

- `karma/include/karma/core/`: core types (IDs, type registry helpers).
- `karma/include/karma/ecs/`: entity/component storage and world registry.
- `karma/include/karma/components/`: shared gameplay-ready component types.
- `karma/include/karma/renderer/`: renderer resource descriptors/registry.
- `karma/include/karma/scene/`: hierarchical scene graph.
- `karma/include/karma/systems/`: abstract system interfaces and scheduling.

## Design notes

- Entities are just IDs; components live in per-type storages.
- A `World` owns the entity registry and component storages.
- The scene graph owns nodes and can reference entities for hierarchical
  transforms or grouping.
- Systems operate on a `World` and are scheduled by a small dependency graph.

This is header-only for now; add `.cpp` files when implementations grow.
