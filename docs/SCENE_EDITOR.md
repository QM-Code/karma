# Scene Editor

Karma's scene editor is a focused, standalone authoring tool. It assembles
linked prefabs, lights, an environment, one editable terrain, and visual
foliage without turning the engine into a general-purpose game editor. It also
authors every persistent component, edits PBR materials, marks static bake
membership, and runs lighting/navigation bakes while deliberately keeping
simulation, gameplay, and prefab-source editing out of the viewport.

## Build and launch

The editor is built when both `KARMA_BUILD_TOOLS` and the graphical profile are
enabled. The portable preset enables both:

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
also be added from **File > Add Asset Root**. That local list and camera, grid,
and marker-visibility preferences are stored in
`.karma/scene-editor.local.json` and are not part of the authored scene.

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
  prefabs beneath their parent groups. Change **Parent** in the inspector to
  reorganize a scene without changing the object's world placement. Preserved
  camera and static/baked subtrees cannot be reparented. Use **Ctrl+D** to
  duplicate a linked prefab or an editable group subtree; child
  groups, lights, and prefab placements are copied together. Protected scene
  roots, cameras, environments, terrain/foliage, opaque component payloads,
  and static/baked subtrees are not duplicated. Deleting a group promotes its
  direct children without changing their world placement.
- Create or import one power-of-two-plus-one height field with **+ Terrain**.
  Select it to open contextual Sculpt and Materials controls. Four normalized
  splat layers expose names, enable state, material assignment, UV scale, and
  active paint selection. Double-click a material in Assets or drag it onto a
  layer card.
- Double-click a mesh asset to create a grass/tree foliage layer. Select that
  layer to edit its base mesh/material slots and as many as three strictly
  increasing mesh or upright-billboard LODs. Paint and Erase operate on the
  complete base/LOD configuration. Paint settings include density, spacing,
  scale, height/slope projection, chunk size, and view distance.
- Every serialized entity component is shown as an Inspector card. Transform,
  mesh/render state, lights, colliders, rigid bodies, physics materials,
  collision filters, character controllers, terrain, and foliage use typed
  controls. Other registered serializers use an explicit JSON draft that must
  parse and deserialize successfully before it can alter the scene. Adding a
  rigid body or character controller also adds a Box Collider when needed.
  Collider wire geometry is shown for the selected entity, but physics remains
  disabled in the editor preview.
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
  prefab metadata without instantiating it.

Graphical applications that use authored terrain and foliage must register the
corresponding runtime modules with `EngineApp` before starting the game, as the
scene editor does. Headless authoring and format validation do not require a
window or renderer.

## Acceptance content

The mini content root includes `prefabs/grass_lod/`, whose crossed-card grass
switches to an upright billboard at 28 units, and `prefabs/pine_tree_lod/`,
whose approximately 10.5-unit tree switches to low detail at 35 units and an
upright billboard at 90 units. The scene places one of each for immediate
graphical validation. Both prefab roots are Static in all five authoring
domains; the tree package keeps bark/leaf material-slot boundaries and includes
a static trunk collider.

## V1 boundaries

The editor intentionally does not provide play mode, gameplay or physics
simulation, camera creation, freeform mesh modeling, multiple editable
terrains, terrain-distance materials, or more than four terrain layers. Prefab
internals stay linked and are changed in their source documents; the scene
stores placement, declared variables, and static bake membership only.
