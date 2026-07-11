# Mini Scene Editor Content Root

This directory is a self-contained content root for the standalone scene
editor. It contains one scene plus linked light, grass-LOD, and pine-tree-LOD
prefabs, with no external asset paths or generated files required.

From the Karma repository root:

```bash
./build/portable/tools/scenes/scene_editor \
  examples/assets/scene_editor_content/scenes/mini.kscene.json \
  --content-root examples/assets/scene_editor_content
```

The scene starts with a selectable point light plus one light rig, grass LOD,
and pine-tree LOD instance. The whole content root is the default asset catalog
root, so all three prefabs and their mesh/material assets can be used
immediately. `prefabs/grass_lod/` reproduces the procedural grass example's
28-unit billboard transition. `prefabs/pine_tree_lod/` contains an aligned,
approximately 10.5-unit pine tree with high detail at the base, low detail from
35 units, an upright billboard from 90 units, and a static trunk collider. Use
**+ Terrain** to add a flat editable surface and assign the packaged meshes and
materials to foliage layers. The grass and tree prefab roots are marked Static
for rendering, lighting, shadows, collision, and navigation, so placed copies
are ready for both lighting and navmesh baking by default.

Copy this directory to a scratch location before saving experiments if you do
not want to modify the checked-in scene. Editor-local settings, recovery data,
and working sidecars are ignored under `.karma/`.
