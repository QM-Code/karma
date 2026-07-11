# Grass LOD Acceptance Asset

The cluster and billboard meshes match the procedural geometry authored in
`examples/rendering/grass_field.cpp`: a 0.95 by 1.25 unit crossed-card cluster
and its single-card billboard. The prefab's root uses a `MeshComponent` for the
crossed-card base and a sibling `LODComponent` for the 28-unit upright billboard
transition. It is an ordinary mesh LOD fixture, not a one-instance
`InstancedMeshComponent` workaround.

The prefab packages its grass texture and materials into the mini scene editor
content root. It can be placed as a normal linked prefab or selected as a
prefab-backed foliage source. In foliage mode, every painted placement is held
once in the layer's shared `InstanceSetComponent` GPU transform buffer while
the prefab's mesh/LOD batch references that set. Saving Mesh or LOD changes in
the focused prefab source editor updates live-linked foliage layers without
repainting them.

The root is marked Static in every authoring domain so the placed fixture
participates in lighting and navmesh bakes. Legacy embedded LOD content should
be opened through the editor/tool migration path; its first rewrite keeps one
`.pre-lod-component.bak`, while runtime loading rejects unmigrated fields.
