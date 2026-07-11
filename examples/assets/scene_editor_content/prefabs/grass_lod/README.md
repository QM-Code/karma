# Grass LOD Acceptance Asset

The cluster and billboard meshes match the procedural geometry authored in
`examples/rendering/grass_field.cpp`: a 0.95 by 1.25 unit crossed-card cluster
and its single-card billboard. The prefab keeps that example's 28-unit upright
billboard transition and packages its grass texture and foliage materials into
the mini scene editor content root. Its root is marked Static in every
authoring domain so the fixture participates in lighting and navmesh bakes.
