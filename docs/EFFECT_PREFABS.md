# Karma Prefabs

Karma prefabs are JSON-backed ECS subtrees. A prefab stores a root entity, its
`world::Scene` children, and a map of component payloads for each saved entity.

Component map keys use the real component struct names, for example:

- `TagComponent`
- `TransformComponent`
- `MeshComponent`
- `LightComponent`
- `ParticleEffectComponent`
- `ParticleEmitterComponent`
- `VolumetricComponent`

Do not invent shortened schema names unless the engine has a matching component
type. This keeps prefab files honest snapshots of engine entities.

## Format

Directory prefabs are loaded from `prefab.json`:

```json
{
  "version": 2,
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
          "mesh_asset_key": "assets/crate",
          "materials": [
            {
              "slot": 0,
              "material_key": "crate"
            }
          ]
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
#include "karma/prefabs.h"

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

## Asset Packages

Use `assets.package.json` beside `prefab.json` when a prefab needs texture
assets or particle effect registration before its entities are created. The
runtime imports the package automatically when the prefab directory is passed to
`prefabs::instantiatePrefab(...)`:

```json
{
  "version": 1,
  "assets": [
    {
      "type": "texture_rgba8",
      "key": "orb_core_atlas",
      "path": "textures/orb_core_atlas.png"
    },
    {
      "type": "particle_effect",
      "key": "energy_orb_core",
      "path": "particles/energy_orb_core.kpeffect"
    }
  ]
}
```

The paths are relative to the prefab directory. Package assets are cached by
prefab directory and registry, then released when the prefab instance is
destroyed or when the app clears cached prefab asset packages.

Variants should be separate JSON prefab files or post-instantiation component
edits in C++. The old prefab parameter binding syntax is removed.
