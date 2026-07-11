# Pine Tree LOD Acceptance Asset

The three OBJ meshes were extracted from the supplied `tree-with-lod.zip`
archive's nested `source/Trees.zip` / `Trees.gltf` source. The source nodes
`Paine tree 1 - high`, `Paine tree 1 - low`, and `lod last` were exported
separately. Their display translations were discarded, their authored
rotations and scales were applied, and all three meshes received the same
uniform scale so the high-detail tree is 10.5 units tall.

The original archive is intentionally not part of this content package. The
processed leaf, bark, normal, and billboard textures are stored beside the
materials and prefab needed by the editor. The prefab root is marked Static in
all authoring domains, including baked lighting, shadows, collision, and
navigation.
