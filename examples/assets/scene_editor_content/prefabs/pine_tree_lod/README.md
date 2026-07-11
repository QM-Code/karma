# Pine Tree LOD Acceptance Asset

The three OBJ meshes were extracted from the supplied `tree-with-lod.zip`
archive's nested `source/Trees.zip` / `Trees.gltf` source. The source nodes
`Paine tree 1 - high`, `Paine tree 1 - low`, and `lod last` were exported
separately. Their display translations were discarded, their authored
rotations and scales were applied, and all three meshes received the same
uniform scale so the high-detail tree is 10.5 units tall.

The original archive is intentionally not part of this content package. The
processed leaf, bark, normal, and billboard textures are stored beside the
materials and prefab needed by the editor. The root's `MeshComponent` uses the
high-detail mesh as its base while a sibling `LODComponent` selects the low
mesh from 35 units and the upright billboard from 90 units. Bark and leaf
material-slot boundaries remain intact. This is an ordinary mesh LOD prefab,
not a one-instance `InstancedMeshComponent` workaround.

The prefab can be placed normally or used as a prefab-backed foliage source.
Foliage compilation extracts its render batches and batch-local transforms,
then points them at the layer's shared `InstanceSetComponent` GPU transform
buffer. A layer may override variables declared by a source prefab, and Mesh or
LOD saves made in the focused source editor remain live-linked to painted
instances. The authored cylinder collider stays part of normal prefab
placement; foliage remains visual-only.

The prefab root is marked Static in all authoring domains, including baked
lighting, shadows, collision, and navigation. Authoring tools migrate legacy
embedded instance/LOD fields transactionally and retain one sibling
`.pre-lod-component.bak` before the first rewrite; runtime loading rejects an
unmigrated source instead of changing it implicitly.
