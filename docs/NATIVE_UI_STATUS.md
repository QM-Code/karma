# Native UI Implementation Status And Roadmap

Last updated: 2026-07-11

This is the implementation and continuation record for Karma's first-party
retained UI. Read [NATIVE_UI.md](NATIVE_UI.md) for the public API and authoring
contract. Native authoring is a hard cutover to `.kui.json5` documents and
`.kstyle.json5` themes. Do not restore the removed XML/KSS path.

The current project version is 0.8.0. See the
[0.8.0 changelog](../CHANGELOG.md) for the user-facing release summary; this
document retains the implementation and verification detail.

The second pass is now usable for desktop game menus, settings, HUDs, tool
panels, popup controls, scrollable and virtualized lists, tabs, trees, menus,
tooltips, disclosures, split panes, and floating windows. Native UI is the
graphical-profile default and builds with ImGui and RmlUi disabled.

## Completion snapshot

| Area | Current state |
| --- | --- |
| Engine ownership | `EngineApp` owns one `ui::System`, exposes it through `nativeUi()` and `GameInterface::ui`, renders it before extension/debug layers, arbitrates native/custom/debug cursor requests, and commits one final shape per frame. |
| Provider isolation | Native UI, ImGui, and RmlUi have separate flags, headers, and targets. Native consumers have no dependency on either optional provider. |
| Authoring | Deterministic JSON5 v2 documents/themes, source-mapped strict validation, checked-in schemas, explicit bindings/actions, recursive theme imports, and canonical assets are implemented. |
| Styling/skinning | Type defaults, ordered named styles with `extends`, variables, state overlays, fonts, motion, per-widget parts, cursors, borders, rounded parts, and nine-slice images are implemented. There is no authored selector language. |
| Runtime | Retained instances, generational handles, cached binding expressions/paths, keyed repeats, localization, indexed listeners, modal routing, focus, accessibility, and RAII controllers are live. |
| Layout | Flex, practical Grid, reference canvases, safe areas, normalized anchors/pivots/offsets, overlay placement, intrinsic sizing, and nesting work. |
| Widgets | Buttons, toggles, sliders, progress, selects/options, tabs, trees, popups, menus, delayed tooltips, disclosures, splitters, floating windows, scrollbars, and fixed-extent keyed virtual lists have behavior and tests. |
| Text/paint | ICU, HarfBuzz, FreeType, LunaSVG, gradients, rounded boxes/borders, nine-slice, images, transforms, transitions, keyframes, mask glyphs, sampler variants, and draw-command coalescing are integrated. |
| Assets/reload | UI documents/themes/fonts/SVGs participate in package/cache/bake/unload. Sandboxed loose graphs, native watching, debounce, worker staging, transitive invalidation, last-good swaps, and state restoration are implemented. |
| Adoption | Native Menu, the exhaustive medieval UI Forge showcase, Constraint Lab, and Tank HUD use JSON5 and build provider-free. |

## Delivered second-pass work

### Strict JSON authoring and theme composition

- Package types are `ui_document`, `ui_theme`, `font`, and `svg`; raster images
  remain texture assets.
- `.kui.json5` and `.kstyle.json5` accept the documented deterministic JSON5
  subset and reject duplicate keys, malformed UTF-8, non-finite values, and
  unsupported extensions.
- `ui_json_validation.*` validates nested fields and types with original source
  line/column locations. Draft 2020-12 schemas live under `schemas/ui/`.
- Theme `imports` compose recursively in deterministic import order. Missing
  sources and cycles are diagnostics; variables and `extends` work across the
  composed graph.
- A node's `styles` array is an explicit precedence list: later entries win
  property conflicts. Inline layout/appearance remains final.
- Document and transitive theme hashes are tracked for reload. Package manifest
  order does not affect cross-reference resolution.
- Packaged import rejects unresolved `{file: ...}` references. Those references
  are allowed only inside the sandboxed development-file graph.

### Game layout

- `canvas.scale_mode` supports `logical`, `fit`, `fill`, `stretch`, and
  `pixel-perfect` with independent framebuffer X/Y scales.
- `safe_area: platform` consumes logical window insets. Paint, hit testing, and
  accessibility consistently map through the resolved canvas transform.
- Anchored children support normalized min/max anchors, pivot, position, and
  four-sided offsets. Fixed axes retain intrinsic size; separated anchors
  stretch and remeasure nested layout.
- Flex and practical Grid remain interoperable. The Grid subset includes
  explicit/implicit tracks, `px/%/fr/auto/minmax/repeat`, gaps, spans,
  non-dense placement, and alignment.

### Widgets and input

- Scrollbars paint horizontal/vertical tracks, proportional rounded thumbs,
  minimum sizes, RTL gutters, and corners. Thumb drag captures the pointer;
  tracks page; wheel input chains; keys/gamepad page; public scroll methods and
  semantic ranges are available. Per-axis overflow and independent horizontal/
  vertical track/thumb skins work on scroll nodes and ordinary panels.
- Disclosure, tree selection/expansion, tab selection/focus, and splitter
  pointer/keyboard resizing are implemented independently.
- Select opens an anchored option listbox. Popup and menu nodes place against
  anchors, dismiss on outside click/Escape/cancel, restore focus, and dispatch
  menu actions. Open transients use a document overlay paint/hit pass rather
  than remaining trapped in a local sibling stacking context.
- Tooltips use the UI clock and authored delay. Fixed-extent `list` nodes
  reconcile only the visible keyed range plus bounded overscan and preserve
  stable handles while an item remains live.
- Floating windows move, resize on every edge/corner, change z-order, close,
  collapse, expose state bindings, and select matching cursor shapes. Title-bar
  movement translates live placement and interaction geometry on the pointer
  event without model reconciliation, style traversal, or Yoga layout; resize
  remains layout-affecting so children can reflow.
- Horizontal and vertical sliders support pointer, keyboard, and gamepad input.
- Capture/target/bubble dispatch, propagation/default cancellation, deferred
  close, modal capture, gameplay suppression, tab focus, and spatial gamepad
  navigation remain intact.

### Skinning and cursors

- Theme `appearance.parts` maps track, fill, thumb, checkmark, arrow, chevron,
  grip, scrollbar, and window parts into typed runtime properties.
- Toggle tracks/checkmarks, slider track/fill/thumb, progress track/fill,
  select arrows, disclosure chevrons, splitter grip marks, and rounded
  scrollbars use the isolated renderer-neutral `widget_paint.*` module.
- Part colors, radii, opacity, borders, and implemented metrics are honored.
  Nested `hover`/`pressed` background colors are supported only on vertical
  and horizontal scrollbar thumbs and select options; other state changes use
  top-level `appearance.states`, and the schema rejects unsupported no-ops.
  A part or ordinary box can use `border_image.source/slice/width/repeat`;
  stretch, cropped repeat, and rounded whole-tile modes share the CPU
  nine-slice path with a bounded generated-cell count.
- Authored cursors and automatic button/control, disabled, splitter, window
  move/resize, and scrollbar cursors route through the common platform cursor
  service. Later custom/debug UI layers may still override the native choice.

### Direct files and hot reload

- `openFile()`/`openFileController()` use ordered sandbox roots and reject
  absolute paths, traversal, schemes, drive syntax, missing files, and
  canonical/symlink escapes.
- Relative document/theme/font/SVG/texture references stage into an isolated
  graph. Linux uses inotify, Windows uses `ReadDirectoryChangesW`, and all
  platforms retain metadata/content fingerprint polling as a safety net.
- Fingerprinting avoids rehashing unchanged files on every native-watcher poll
  while periodically forcing a content audit to catch lost notifications.
- Reload validates in staging and swaps only the newest valid generation at a
  frame boundary. Invalid edits keep the complete last-good UI.
- A full reload preserves the document handle, model, action listeners, focus,
  scroll offsets, values/check state, unbound open/expanded/collapsed state,
  and unbound window geometry/z by stable identity. Element handles/listeners
  still invalidate when nodes are recreated.

### Runtime optimization and decomposition

- `BindingEngine` owns expression/property-path parsing and bounded caches;
  retained refresh no longer reparses every expression and path.
- `style_runtime.*` owns compiled selector chains, tag/id/class candidate
  indexes, cascade/default/inheritance policy, font-face selection, targeted
  and full style passes, and active transition/animation advancement. Theme
  staging compiles rules once, nodes retain parsed inline declarations, and
  style/motion work returns explicit counters and invalidation results to the
  frame orchestrator.
- `ListenerRegistry` owns generational slots and indexed document/action and
  element/event/capture lookup. Dispatch and hit-target resolution no longer
  scan all listeners.
- `document_loader.*` owns strict document adaptation and retained-node
  construction. `diagnostics.*` provides the common source-diagnostic dedup
  policy used by document/theme loading and runtime warnings.
- `hot_reload_runtime.*` owns reload staging, queue coalescing, worker-thread
  synchronization, theme-graph composition, and exception diagnostics.
- `hot_reload_coordinator.*` owns watcher/poll/debounce state, development
  graph commits, source/theme snapshots, fingerprints, and worker requests.
  `System` adopts completed immutable stages but does not own reload machinery.
- `canvas_layout.*`, `widget_paint.*`, `widget_runtime.*`, `focus_runtime.*`,
  `transient_runtime.*`,
  `development_loader.*`, `controller.cpp`, and `authoring.cpp` isolate
  additional responsibilities from the runtime monolith. `widget_runtime`
  owns scrollbar/window/slider interaction geometry and cursor selection;
  `focus_runtime` owns focusable collection, tab ordering, and spatial
  candidate selection; `transient_runtime` owns cached overlay order,
  anchor resolution, containment, tooltip timing, and frame-bound placement.
- Glyph atlas updates use texture-region uploads after initial page creation,
  with a safe full-page fallback, instead of uploading a complete 1024x1024
  page for every new glyph.
- SVG intrinsic dimensions are cached by asset content hash. Texture-cache
  pruning is periodic instead of a full scan every frame. SVG textures are
  evicted for stale content or byte-budget pressure, not frame age alone,
  because retained fragments may reference them without resolving them again.
- Documents carry feature flags so idle frames skip tooltip, virtual-list,
  transient, and motion tree traversals when those features are absent.
- `setMany()` validates and commits model batches transactionally. Compiled
  binding dependencies route ordinary updates to affected nodes; progress and
  slider value changes invalidate their paint path without a whole-document
  style/layout pass.
- `document_reconciler.*` owns compiled dependency-index construction,
  repeat-local filtering/ownership, boundary-aware bidirectional path matching,
  affected-owner de-duplication, and repeat-owner signaling.
- `runtime_dom.*` owns shared node/document state, traversal and retained child
  order, normalized style/overflow/visibility queries, authored-subtree cloning,
  node-local paint revisions, and requested/completed work revisions for
  binding, style, measure, layout, placement, font, and accessibility stages.
- `document_runtime.*` owns document and element generational slots, tree
  registration/release, opening and retained document order, dense frame
  traversal snapshots, and listener lifetime. Slot types and free lists are no
  longer exposed through `runtime_dom` or scanned directly by `System`.
- `document_layout_runtime.*` owns fallback and Yoga layout adaptation, text
  and image measurement, anchors, scrolling geometry, clipping, and layout
  revision completion behind explicit inputs/results.
- `accessibility_builder.*` owns semantic role/name/state/range/action
  derivation, visibility filtering, document traversal, snapshot assembly,
  generation advancement, and vector reuse.
- `presentation_runtime.*` assembles retained draw fragments, coalesces command
  boundaries, clamps authored scissors, accounts retained bytes, traverses
  retained runtime fragments, and enforces the configured 32 MiB deterministic
  LRU budget. A fragment is retained only when its
  resource generation remains stable through construction; the complete paint
  slice retries once rather than submit handles invalidated during paint.
  Paint-only motion invalidates affected fragments while static siblings remain
  reusable.
- `presentation_resources.*` is the single renderer-facing owner for font
  registration hashes, glyph atlases, static/SVG GPU texture caches, SVG
  intrinsic metadata, generational dynamic images, resource aging, and
  shutdown. It borrows the asset registry, nullable graphics device, and text
  engine through an explicit provider-free interface; `System` no longer owns
  or reaches into those caches.
- `presentation_builder.*` owns retained subtree assembly, widget/skin paint,
  shaped-glyph expansion, batching/scissors, overlays, generation-safe retry,
  and retained-budget enforcement. `computed_style_values.*`,
  `string_utils.h`, and `asset_reference.*` provide shared private value and
  validation policy without pulling document loading into style/layout.
- Active transition and animation nodes are registered directly, so animation
  sampling is O(active tracks), not a pair of full-document traversals.
  Z-sorted runtime child order is shared by painting and reverse hit testing;
  visible-document and overlay-root forward/reverse order is retained across
  paint, input, cursor, and accessibility consumers. Volatile motion ancestors
  assemble directly from retained sibling fragments, avoiding repeated copies
  of the same subtree geometry.
- A duplicate font traversal and two extra recursive layout-style fingerprint
  passes were removed; layout-affecting style changes are detected during the
  existing style traversal.
- Adjacent draw commands still coalesce by texture/blend/sampler/texture-mode/
  clip state.
- Floating-window title drag is a placement-only fast path: it invalidates the
  moved presentation fragments and required assembly ancestors, keeps sibling
  bounds stable, and commits bound position only on release. Bringing a window
  forward is a targeted restyle; resizing is intentionally not on this path.

## Debug showcase benchmark

Measured on 2026-07-10 at 1600x900 with the Debug provider-free showcase,
VSync disabled, and CPU frame pacing disabled. The run discarded 120 warm-up
frames and sampled 4,570 subsequent frames (including the former 3,600-frame
SVG age-eviction boundary):

| Metric | Reported baseline | Retained pass |
| --- | ---: | ---: |
| Native UI frame p95 | approximately 50 ms | 2.980 ms |
| Native UI maximum after warm-up | not bounded | 5.402 ms |
| Native paint p95 | not previously split | 2.350 ms |
| Frames above 10 ms | periodic spikes present | 0 |
| UI draw commands | 947 | 75 |

The benchmark used `KARMA_ENGINE_FRAME_DIAG=1`, a zero diagnostic threshold,
`KARMA_ENGINE_FRAME_PACING_FPS=0`, and `KARMA_ENGINE_VSYNC=0`. Loose-file hot
reload remained enabled. Its periodic content safety audit was included in the
sample rather than disabled for the measurement.

## Source map and architecture

| Responsibility | Primary files |
| --- | --- |
| Public contracts | `include/karma/ui.h` |
| JSON5 parsing/validation/import | `src/content/assets/ui_json_profile.*`, `ui_json_validation.*`, `asset_ui_source_import.*` |
| Registry/cache/package integration | `src/content/assets/asset_registry.cpp`, `asset_cache_ui.cpp`, `asset_package.cpp` |
| Theme adaptation and diagnostics | `src/features/ui/native/authoring.*`, `diagnostics.*` |
| Document adaptation | `src/features/ui/native/document_loader.*` |
| Retained DOM state/traversal/revisions/common queries | `src/features/ui/native/runtime_dom.*` |
| Compiled style/cascade/font/motion execution | `src/features/ui/native/style_runtime.*` |
| Binding evaluation/cache | `src/features/ui/native/binding_engine.*` |
| Dependency-index construction and owner selection | `src/features/ui/native/document_reconciler.*` |
| Document/element/listener ownership and order | `src/features/ui/native/document_runtime.*`, `listener_registry.*` |
| Accessibility snapshot assembly | `src/features/ui/native/accessibility_builder.*` |
| Canvas and document layout execution | `src/features/ui/native/canvas_layout.*`, `document_layout_runtime.*`, `layout_engine.*` |
| Widget paint geometry | `src/features/ui/native/widget_paint.*` |
| Focus traversal/order/spatial policy | `src/features/ui/native/focus_runtime.*` |
| Transient order/anchors/tooltips/placement | `src/features/ui/native/transient_runtime.*` |
| Widget interaction geometry/cursors/scrollbars/sliders | `src/features/ui/native/widget_runtime.*` |
| Controller and loose graph | `controller.cpp`, `development_path.*`, `development_loader.*`, `file_watcher.*`, `hot_reload_runtime.*`, `hot_reload_coordinator.*` |
| Presentation resource ownership | `src/features/ui/native/presentation_resources.*` |
| Retained presentation assembly/budget | `src/features/ui/native/presentation_builder.*`, `presentation_runtime.*` |
| Text/paint/motion/SVG primitives | `text_engine.*`, `paint_engine.*`, `motion_engine.*`, `svg_rasterizer.*` |
| Private shared values/validation | `computed_style_values.*`, `string_utils.h`, `asset_reference.*` |
| System lifetime/resource forwards | `src/features/ui/native/system.cpp`, `system_impl.h` |
| Public document/model APIs | `src/features/ui/native/system_documents.cpp` |
| Structural/model reconciliation bridge | `src/features/ui/native/system_reconciliation.cpp` |
| Focus/widget/transient behavior | `src/features/ui/native/system_interaction.cpp` |
| Platform input routing | `src/features/ui/native/system_input.cpp` |
| Frame/reload-stage orchestration | `src/features/ui/native/system_frame.cpp` |
| Engine routing | `src/runtime/app/engine_app.cpp`, `ui_context.cpp`, `input/input_system.cpp` |

The former two-pass self-include and later `system_api.inc` staging file are
gone. `system_impl.h` is private, declaration-only, and included by six normal
translation units. Their combined implementation is approximately 3.6k lines,
down from approximately 9.2k; the largest cohesive file is input routing at
approximately 1.0k lines. Runtime modules never include `system_impl.h`, so
dependency direction remains System orchestration to concrete services.

Frame sequencing intentionally remains top-level orchestration in
`system_frame.cpp`: reload adoption, reconciliation, style, active motion,
font resources, layout, placement, paint, accessibility, and diagnostics must
remain ordered. Event routing likewise remains in `system_input.cpp` while
synchronous dispatch/default behavior lives in `system_interaction.cpp`.
Creating a generic frame or input host now would reintroduce `System::Impl` as
a callback table rather than establish ownership.

## Remaining boundaries and next work

### P0: maintain the established boundaries and profile

1. Keep new layout, style, resource, document-lifetime, and presentation work
   inside their concrete runtimes; keep `System` files limited to coordination.
2. Consider a fuller reconciliation or input runtime only when it can depend
   on concrete services without an `Impl` callback/host interface.
3. Continue narrowing the retained fragment cache to explicit node-local
   pre/post-child fragments with assembly-time transforms and clip metadata.
4. Extend dependency ownership for deeply nested repeat-local expressions.

### P1: remaining game-tool polish

- Add min/max constraints, symmetric policy, and optional bound persistence to
  splitters; add window docking/snapping and safe-area-aware maximum bounds.
- Define richer per-part contracts for popup/option/window button chrome if a
  shipped game needs more than the current box/part properties.
- Decide whether transformed paint should also transform hit testing,
  accessibility bounds, and scissors; current transforms remain paint-only.
- Move the initial loose-graph read/import completely off the frame path and
  add a native macOS FSEvents watcher.

### P2: intentionally deferred systems

- Radio buttons/radio groups are not built-in. Use `select` or app-managed
  mutually exclusive toggles until a group/value authoring contract is
  designed.
- Editable text, selection, clipboard editing semantics, IME/composition, and
  touch/multi-pointer input.
- OS accessibility bridges and action round-trips.
- Rounded/nested clip masks, arbitrary masks, blur, shadows, and an advanced
  compositor.
- An engine-wide graphics-device loss/recreation transaction. Do not add an
  isolated UI-only reset hook.

## Invariants to preserve

1. Native public headers remain free of ImGui and RmlUi types.
2. Version-2 JSON authoring is the only native format.
3. Packaged references are asset keys; loose files are relative and sandboxed.
4. No operating-system font discovery occurs.
5. Handles remain generational and stale handles fail safely.
6. Callbacks remain synchronous on the main thread; unsafe closes remain
   deferred through dispatch.
7. Logical layout/hit testing remains separate from framebuffer geometry.
8. Render targets remain borrowed; UI destroys only owned resources.
9. Invalid reloads never replace last-good live content.
10. Graphical consumers continue to build with both optional providers off.
11. Title-bar movement remains placement-only and may not disturb sibling
    layout; window resizing may request layout for child reflow.

## Verification handoff

Use a durable repository-local graphical build:

```bash
cmake -S . -B build/native-ui -G Ninja \
  -DKARMA_HEADLESS=OFF \
  -DKARMA_FETCH_DEPS=ON \
  -DKARMA_BUILD_GRAPHICAL_PROFILE=ON \
  -DKARMA_ENABLE_NATIVE_UI=ON \
  -DKARMA_ENABLE_IMGUI=OFF \
  -DKARMA_ENABLE_RMLUI=OFF \
  -DKARMA_BUILD_DEBUG_UI=OFF \
  -DKARMA_BUILD_EXAMPLES=ON \
  -DBUILD_TESTING=ON \
  -DKARMA_BUILD_TESTS=ON \
  -DCMAKE_BUILD_TYPE=Debug

cmake --build build/native-ui --parallel --target \
  ui_native gameplay_tank physics_constraint_lab \
  karma_cursor_tests karma_ui_accessibility_builder_tests \
  karma_ui_json_profile_tests karma_ui_development_path_tests \
  karma_ui_authoring_tests \
  karma_ui_binding_tests karma_ui_document_reconciler_tests \
  karma_ui_document_runtime_tests karma_ui_listener_registry_tests \
  karma_ui_file_watcher_tests karma_ui_hot_reload_coordinator_tests \
  karma_ui_focus_runtime_tests karma_ui_transient_runtime_tests \
  karma_ui_tests karma_ui_layout_tests \
  karma_ui_document_layout_runtime_tests karma_ui_motion_tests \
  karma_ui_style_runtime_tests karma_ui_paint_tests \
  karma_ui_widget_paint_tests karma_ui_widget_runtime_tests \
  karma_ui_presentation_tests karma_ui_presentation_builder_tests \
  karma_ui_screenshot_golden_tests

ctest --test-dir build/native-ui \
  -R '^(karma_cursor_tests|karma_ui_.*)$' --output-on-failure
```

The visually reviewed current pilot hashes are:

- `native_menu_1x`: `0x4671783eb1901fcb`
- `native_menu_1_5x`: `0xbfb606a7ba328048`
- `native_menu_2x`: `0x440a4701282fa667`
- `native_menu_rtl_1x`: `0x460c81884cc3e389`

Also rebuild `tests/consumer/source-graphical` with native UI on and ImGui,
RmlUi, and debug UI off, then exercise `tests/consumer/install-graphical`
against a staged install. Both smokes instantiate `ui::System`, so they verify
the native archive and link-only dependencies rather than headers alone. Run
package/core/rendering tests in a headless profile before future handoff.
