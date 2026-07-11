# Scene Editor

Karma's scene editor is a focused, standalone authoring tool. It assembles
linked prefabs, lights, an environment, one editable terrain, and visual
foliage without turning the engine into a general-purpose game editor. It also
authors every persistent component, edits PBR materials, marks static bake
membership, and runs lighting/navigation bakes while deliberately keeping
simulation, gameplay, and full prefab mode out of the viewport.

## Build and launch

The editor is built when `KARMA_BUILD_SCENE_EDITOR`, `KARMA_BUILD_TOOLS`, and
the graphical profile are enabled. Top-level graphical tool builds enable the
Scene Editor and its optional ImGui adapter automatically; runtime-only builds
remain free of the ImGui dependency. The portable preset enables the editor:

```bash
cmake --preset portable
cmake --build build/portable --target karma_scene_editor --parallel
./build/portable/tools/scenes/scene_editor --content-root /path/to/content
```

Open an existing scene by passing it first:

```bash
./build/portable/tools/scenes/scene_editor \
  /path/to/content/scenes/world.kscene.json \
  --content-root /path/to/content \
  --asset-root assets
```

For a small checked-in content root that needs no external assets, launch the
mini example directly:

```bash
./build/portable/tools/scenes/scene_editor \
  examples/assets/scene_editor_content/scenes/mini.kscene.json \
  --content-root examples/assets/scene_editor_content
```

It includes a movable point light plus placed light-rig, grass-LOD, and
pine-tree-LOD prefabs; the same assets remain ready to drag from the catalog.
See the content root's
[README](../examples/assets/scene_editor_content/README.md) for details.

Scene and asset roots must remain inside the content root. Additional roots can
also be added from **File > Add Asset Root**. That local list, panel layout,
camera/grid preferences, Terrain tab, active foliage layer, and component
foldouts are stored in
`.karma/scene-editor.local.json` and are not part of the authored scene.

Renderable distance LODs use ordinary components rather than an embedded
instancing format. `MeshComponent` or `InstancedMeshComponent` owns the base
mesh/material batch, while a sibling `LODComponent` owns up to three strictly
increasing distance levels. `InstanceSetComponent` separately owns matrix or
planar transforms and their GPU upload revision. An `InstancedMeshComponent`
references an instance set (the same entity by default) and adds only its own
mesh/material binding and batch-local transform. This lets several mesh
batches share one GPU transform buffer instead of uploading the same foliage
placements once per trunk, leaves, accessory mesh, or LOD-capable batch.

## Authoring workflow

The persistent workspace places the Hierarchy at left, the Scene viewport over
a tabbed Assets/Console/Lighting/Navigation workspace in the center, and the
Inspector at right. Drag the splitters to resize the panes; their dimensions,
active tab, and asset/terrain filters are local settings. The searchable Assets panel
indexes `prefab.json` and `assets.package.json` documents beneath the configured
asset roots. Duplicate asset keys are shown as invalid instead of choosing one
silently. Manifest directories and referenced package files are watched for
changes and the preview reloads automatically.

- Double-click or drag a prefab into the viewport to create a rendered,
  simulation-safe placement preview. It snaps to terrain or the construction
  plane with the current grid settings. Click to commit one linked instance or
  press Escape to cancel. The selected editable group becomes its parent;
  otherwise it is placed at scene root. Prefab variables remain overrides on
  that instance, and the resolved source hierarchy is visible read-only.
- Selecting a prefab asset opens the focused prefab source editor. It can edit
  only `MeshComponent` and `LODComponent` payloads, with node selection,
  drag-and-drop mesh/material assignment, typed LOD controls, independent
  undo/redo, Revert, atomic Save, and external-change conflict detection. Node
  structure, transforms, variables, and all other components remain linked and
  read-only. Foliage layers backed by that prefab expose **Edit Source...** and
  update their preview through the live source link after a successful save.
- Add groups, point lights, and environment settings from the hierarchy and
  inspector. Existing scene cameras are editable but are not used for the
  editor preview.
- Any entity can carry a Static component with independent Rendering,
  Lighting, Shadows, Collision, and Navigation participation. Membership can
  flow through a group hierarchy; an explicit disabled component opts a child
  subtree out. Linked prefab instances provide the same Inherit/Static/Not
  Static classification without allowing arbitrary per-instance component
  overrides.
- Static instanced batches can cast into baked lighting and can feed navigation
  through an explicit `NavMeshSurfaceComponent`. Generated lightmap receiving
  remains per ordinary mesh: an instanced batch has one shared material
  binding and cannot represent different per-instance lightmaps without a
  dedicated GPU-instance ABI. The baker excludes that unsafe case and reports
  it in the Lighting status and Console instead of repeating one instance's
  lighting across the batch.
- The hierarchy mirrors authored parent/child relationships, including linked
  prefabs beneath their parent groups. Foliage layers appear as virtual
  children of the editable Terrain without changing their serialized parents
  or transforms. Clicking one selects Terrain and opens that layer in the
  Foliage tab. Change **Parent** in the inspector to
  reorganize a scene without changing the object's world placement. Preserved
  camera and static/baked subtrees cannot be reparented. Use **Ctrl+D** to
  duplicate a linked prefab or an editable group subtree; child
  groups, lights, and prefab placements are copied together. Protected scene
  roots, cameras, environments, terrain/foliage, opaque component payloads,
  and static/baked subtrees are not duplicated. Deleting a group promotes its
  direct children without changing their world placement.
- Create or import one power-of-two-plus-one height field with **+ Terrain**.
  Its Terrain Component contains true **Sculpt**, **Materials**, and
  **Foliage** tabs, with serialized Component Data separated from editor-only
  Authoring Tools. Four normalized splat layers expose names, enable state,
  material assignment, UV scale, and active paint selection. Double-click a
  material in Assets or drag it onto a layer card.
- Create a grass/tree foliage layer from Terrain's Foliage tab, then choose one
  of two exclusive render sources. Dropping a mesh creates a direct source with
  material slots and a sibling `LODComponent`; its LODs can use mesh or upright
  billboard rendering. Dropping a prefab creates a prefab-backed source. The
  runtime resolves that prefab's render nodes and their `LODComponent` data,
  preserves each batch's local transform, and points every batch at the
  layer's shared `InstanceSetComponent` GPU transform buffer. The layer
  Inspector exposes overrides for variables declared by the source prefab and
  a resolved renderer/LOD summary. Source file changes invalidate the compiled
  prototype and rebuild the layer automatically, so source edits stay live.
  Paint and Erase always modify spatial instances, never separate LOD copies.
  Layer actions include create, rename, duplicate, and delete; paint settings
  include density, spacing, scale, height/slope projection, chunk size, and
  view distance.
- The Inspector resets to the newly selected object and shows a compact object
  header followed only by that object's component cards. Transform, parent,
  mesh/render state, lights, colliders, rigid bodies, physics materials,
  collision filters, character controllers, terrain, foliage, LOD, and
  instance sets use typed controls. Other registered serializers use an
  explicit JSON draft that must parse and deserialize successfully before it
  can alter the scene. Adding an instanced mesh also adds its default same-
  entity instance set; a contextual source can instead reference another
  authored `InstanceSetComponent`. Adding a rigid body or character controller
  also adds a Box Collider when needed. Collider wire geometry is shown for
  the selected entity, but physics remains disabled in the editor preview.
- Selecting a material asset opens a typed PBR editor for base/emissive color,
  metal/roughness, normal convention and scale, AO, specular, clearcoat, sheen,
  anisotropy, transmission, IOR, thickness, alpha/render state, and texture
  assignments. Changes preview live, Save replaces the source atomically, and
  Escape/Revert restores the pre-edit material.
- The Lighting workspace assigns Realtime, Mixed, or Baked light behavior and
  configures generated UV1, texel density, atlas limits, padding/dilation,
  directional output, and deterministic sky/AO sampling. The Navigation
  workspace bakes authored NavMesh owners. Lighting, Navigation, and Bake All
  run on a cancellable worker, publish artifacts transactionally, and rebuild
  the preview after the manifest is committed.

Viewport controls follow Unity's perspective Scene view conventions: middle
mouse pans, Alt+left mouse orbits, Alt+right mouse and the wheel dolly, and F
frames the selection. Holding right mouse enters flythrough navigation;
WASD/QE move, Shift increases speed, and the wheel changes fly speed. W/E/R
select Move, Rotate, and Scale outside flythrough mode, with G and T retained
as Move and Scale aliases. World/local orientation, grid snapping, and visual
marker visibility are available from the toolbar. The View selector switches
between full PBR **Rendered** shading, matte-lit **Diffuse** shading, unlit
base-color **Texture** shading, and cyan triangle-topology **Wire** shading.
These views do not change authored scene materials. Non-rendered groups, prefab
roots, environments, and lights have pickable markers; a selected marker stays
visible when markers are globally hidden.

Undo/redo covers document edits, terrain strokes, and foliage deltas. Inspector
drags are coalesced into one command. Closing with unsaved work writes a
recovery document under `.karma/recovery`; the editor offers to restore a newer
snapshot on the next launch. If the source scene changes externally while
local edits exist, the editor asks whether to reload, keep local work, or save a
copy.

## Saved data

Scene JSON is written atomically. Terrain and foliage working copies live under
`.karma/editor-preview`, while a save writes content-addressed immutable files
beside the scene:

```text
scenes/world.kscene.json
scenes/world.scene-assets/
  terrain_<id>-height-<hash>.r32
  terrain_<id>-control-<hash>.tga
  foliage_<id>-<hash>.kfoliage
```

The editor and migration-capable authoring tools automatically detect and
upgrade legacy render JSON before editing it: instance layout, transforms,
revision, and dynamic state move from `InstancedMeshComponent` to a sibling
`InstanceSetComponent`, while embedded instanced-mesh or foliage `lods` move to
a sibling `LODComponent`.
Migration is transactional and refuses ambiguous content that already has a
conflicting sibling component, is read-only, or changes while the staged
replacement is being validated. Before the first source rewrite, the authoring
tool keeps one sibling backup whose filename ends in
`.pre-lod-component.bak`; subsequent checks reuse that single safety copy
instead of producing a chain of backups. Runtime loading never performs this
migration or writes source content. An unmigrated payload is rejected with a
component diagnostic, so batch conversion belongs in the editor/tooling step
and remains reviewable.

The scene stores content-root-relative paths. The runtime resolves packages,
prefabs, terrain images/material maps, foliage sidecars, and camera shader
overrides with this precedence:

1. `SceneInstantiateDesc::reference_root`
2. `SceneDocument::reference_root`
3. the directory containing the scene

A terrain or foliage source that cannot be read is shown as non-editable and is
left untouched by unrelated saves. Foliage is visual-only, chunked, and capped
at 1,000,000 authored instances per scene and 100,000 resident runtime
instances by default.

## Reusable APIs

The GUI uses the same public APIs available to headless tools and game-specific
authoring utilities:

- `<karma/scene_authoring.h>` provides `scene_authoring::TerrainCanvas` for
  creation/import, ray queries, sculpting, normalized four-channel splat paint,
  preview tile data, and R32/TGA export.
- `<karma/foliage.h>` provides deterministic `.kfoliage` v1 I/O,
  `foliage::FoliageLayer` paint/erase deltas, and
  `foliage::FoliageRuntimeModule` chunk streaming.
- `<karma/scenes.h>` provides `sceneDocumentToJson`, atomic
  `saveSceneDocument`, portable reference roots, scene instantiation, bake
  fingerprints, progress/cancellation, lightmap bindings, and navigation
  bindings.
- `<karma/prefabs.h>` provides `loadPrefabDocument` for tooling that needs
  prefab metadata without instantiating it, validated atomic
  `savePrefabDocument`, and explicit raw-JSON legacy render migration helpers.

Graphical applications that use authored terrain and foliage must register the
corresponding runtime modules with `EngineApp` before starting the game, as the
scene editor does. Headless authoring and format validation do not require a
window or renderer.

## Acceptance content

The mini content root includes `prefabs/grass_lod/`, whose base
`MeshComponent` crossed-card grass uses a sibling `LODComponent` to switch to
an upright billboard at 28 units, and `prefabs/pine_tree_lod/`, whose base
high-detail `MeshComponent` switches to low detail at 35 units and an upright
billboard at 90 units. These are ordinary linked-prefab fixtures, not synthetic
one-instance `InstancedMeshComponent` batches. The scene places one of each for
immediate graphical validation, and either prefab can also be selected as a
prefab-backed foliage source. Both prefab roots are Static in all five
authoring domains; the approximately 10.5-unit tree package keeps bark/leaf
material-slot boundaries and includes a static trunk collider.

## V1 boundaries

The editor intentionally does not provide play mode, gameplay or physics
simulation, camera creation, freeform mesh modeling, multiple editable
terrains, terrain-distance materials, or more than four terrain layers. Prefab
internals stay linked; only mesh and LOD payloads are editable through the
focused source editor. The scene stores placement, declared variable
overrides, and static bake membership rather than arbitrary prefab overrides.
