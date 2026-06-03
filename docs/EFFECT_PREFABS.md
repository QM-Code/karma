# Karma Prefabs

Karma prefabs are JSON-backed ECS subtrees. A prefab stores a root entity, its
`scene::Scene` children, and a map of component payloads for each saved entity.

Component map keys use the real component struct names, for example:

- `TagComponent`
- `TransformComponent`
- `LocalTransformComponent`
- `MeshComponent`
- `LightComponent`
- `ParticleEffectComponent`
- `ParticleEmitterComponent`
- `BeamPathComponent`
- `VolumeSphereComponent`

Do not invent shortened schema names unless the engine has a matching component
type. This keeps prefab files honest snapshots of engine entities.

## Format

Directory prefabs are loaded from `prefab.json`:

```json
{
  "version": 1,
  "root": 0,
  "nodes": [
    {
      "id": 0,
      "name": "Crate",
      "parent": null,
      "components": {
        "TransformComponent": {
          "position": [0, 0, 0],
          "rotation": [0, 0, 0, 1],
          "scale": [1, 1, 1]
        },
        "MeshComponent": {
          "mesh_key": "assets/crate.glb",
          "material_key": "crate"
        }
      }
    }
  ]
}
```

Runtime handles such as `MeshId`, `MaterialId`, `TextureId`, ownership flags,
and particle applied-cache fields are not persisted. Save stable keys and let
the renderer or particle systems resolve them.

## API

```cpp
#include "karma/content/prefabs/prefab.h"

prefabs::PrefabInstantiateDesc desc{};
desc.root_transform.setPosition({0.0f, 2.0f, 0.0f});
desc.name_override = "Shield";

const auto instance = prefabs::instantiatePrefab(
    *world,
    *scene,
    "examples/assets/prefabs/volumetric_sphere",
    desc);
```

`PrefabInstance` returns the root entity, root scene node, all created entities,
and lookup maps by saved node name and id.

Use `prefabs::destroyPrefab(world, scene, instance.root)` to remove a loaded
subtree. Use `prefabs::savePrefab(...)` to write an entity subtree back to JSON.

## Resource Sidecars

Use `prefab.resources.json` beside `prefab.json` when a prefab needs texture
aliases or particle effect registration before its entities are created. The
runtime loads the sidecar automatically when the prefab directory is passed to
`prefabs::instantiatePrefab(...)`:

```json
{
  "version": 1,
  "textures": [
    { "key": "orb_core_atlas", "path": "textures/orb_core_atlas.png" }
  ],
  "particle_effects": [
    { "key": "energy_orb_core", "path": "particles/energy_orb_core.kpeffect" }
  ]
}
```

The paths are relative to the prefab directory. Sidecar resources are cached by
prefab directory and released when the app clears the prefab resource context.

Variants should be separate JSON prefab files or post-instantiation component
edits in C++. The old prefab parameter binding syntax is removed.
