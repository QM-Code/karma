# Native UI

Karma's native UI is a first-party retained-mode system owned by
`karma::app::EngineApp`. Its public API is in `<karma/ui.h>` and does not expose
ImGui or RmlUi types. A graphical frame renders the scene, native UI, an
optional custom `UiLayer`, and the optional debug overlay in that order. Input
is offered in reverse order.

The second-pass authoring format is JSON-based and game-oriented. Documents
use `.kui.json5`; reusable themes use `.kstyle.json5`. These are hard-cutover
formats: version-1 markup and stylesheet sources are not accepted by the
native asset importer.

For implementation ownership, verified commands, and unfinished work, see
[Native UI Implementation Status And Roadmap](NATIVE_UI_STATUS.md).

## Enable and access native UI

`KARMA_ENABLE_NATIVE_UI` follows the graphical profile and is forced off for
headless builds. Optional adapters have separate flags and headers:

| Feature | CMake flag | Header |
| --- | --- | --- |
| Native retained UI | `KARMA_ENABLE_NATIVE_UI` | `<karma/ui.h>` |
| ImGui adapter/tooling | `KARMA_ENABLE_IMGUI` | `<karma/ui_imgui.h>` |
| RmlUi adapter | `KARMA_ENABLE_RMLUI` | `<karma/ui_rmlui.h>` |

ImGui and RmlUi default off and are not native-UI dependencies. Access the
system through `EngineApp::nativeUi()` or the borrowed `GameInterface::ui`
pointer. Both are null when native UI is unavailable or
`EngineConfig::native_ui.enabled` is false.

The low-level handle API remains available:

```cpp
void MenuGame::onStart() {
  if (ui == nullptr) return;

  auto opened = ui->open("ui/main_menu", {.layer = 100, .modal = true});
  menu_ = opened.document;
  if (!menu_) return;

  ui->set(menu_, "settings.volume", 0.8);
  play_ = ui->onAction(menu_, "play", [this](const karma::ui::ActionEvent&) {
    ui->close(menu_); // Safely deferred when called during dispatch.
  });
}
```

For normal screen ownership, `DocumentController` is more concise:

```cpp
class MenuGame final : public karma::app::GameInterface {
 public:
  void onStart() override {
    if (ui == nullptr) return;

    auto opened = ui->openController(
        "ui/main_menu", {.layer = 100, .modal = true});
    if (!opened) return;

    menu_ = std::move(opened.controller);
    menu_.set("settings.volume", 0.8);
    menu_.bindActions({
        {"play", [this](const karma::ui::ActionEvent&) { startGame(); }},
        {"close", [this](const karma::ui::ActionEvent&) { menu_.close(); }},
    });
  }

 private:
  karma::ui::DocumentController menu_;
};
```

`DocumentController` is movable and noncopyable. It owns the document, closes
it on destruction, tracks listeners registered through it, and becomes inert
if its `System` is destroyed first. `release()` transfers the raw document
handle to code using the low-level API.

Use `setMany()` when initialization or telemetry changes several model paths:

```cpp
menu_.setMany({
    {"profile.name", "Rowan"},
    {"settings.volume", 0.8},
    {"settings.vsync", true},
});
```

Every path is validated before mutation, updates are applied in order (so the
last duplicate wins), and affected bindings reconcile once. An invalid path
returns `false` without changing the model. `set()` and `setMany()` keep model
reads and structural bindings synchronous outside active event dispatch;
layout and paint remain frame-bound.

Retained paint fragments use a 32 MiB per-system budget by default. Games may
adjust `UiSystemConfig::retained_paint_budget_bytes`, or set it to zero to
disable fragment retention. The runtime evicts least-recently-used fragments
without invalidating document or element handles.

Binding, style, measure, layout, placement, font, and accessibility scheduling
uses requested/completed work revisions rather than one broad document-dirty
bit. Paint fragments and paint/hit child order carry node-local revisions, so
paint-only controls, scrolling, and stacking changes do not promote unrelated
subtrees into style or Yoga work. Visible-document and transient-overlay order
is also retained until lifecycle, visibility, structure, or z-order changes.

`System::frameDiagnostics()` exposes the most recently completed native UI
frame. Its stage timings are intended for profiling; its reconciled, restyled,
laid-out, placed, advanced-motion, accessibility, fragment, vertex, and command
counters are deterministic enough for regression tests. With
`KARMA_ENGINE_FRAME_DIAG=1`,
the engine frame log includes the same native stage timings and work counters
alongside custom/debug UI timings.

## Packaged assets

Asset packages recognize these first-party UI types:

| Package type | Source |
| --- | --- |
| `ui_document` | UTF-8 `.kui.json5` |
| `ui_theme` | UTF-8 `.kstyle.json5` |
| `font` | `.ttf`, `.otf`, `.ttc`, or `.otc` bytes |
| `svg` | UTF-8 static `.svg` |

Raster images continue to use texture assets. A minimal package is:

```json
{
  "version": 1,
  "assets": [
    {
      "type": "ui_document",
      "key": "ui/main_menu",
      "path": "main_menu.kui.json5"
    },
    {
      "type": "ui_theme",
      "key": "ui/theme",
      "path": "theme.kstyle.json5"
    }
  ]
}
```

Documents and themes are validated and stored as canonical strict JSON plus
typed dependencies. Cross-reference validation runs after package staging, so
manifest entry order does not matter. UI documents, themes, fonts, and SVGs
participate in async package commit, cache serialization, bake/restore,
fingerprinting, and unload.

Packaged references use `{asset: 'asset/key'}`. URLs, executable schemes, and
arbitrary filesystem paths are not runtime asset references. Development-only
`{file: 'relative/path'}` references are described under
[Loose files and hot reload](#loose-files-and-hot-reload).

## Karma's JSON5 authoring profile

The authoring parser accepts a deterministic subset of JSON5:

- line and block comments;
- trailing commas;
- single-quoted strings;
- unquoted ASCII identifier keys;
- an optional UTF-8 BOM at the beginning.

Values and escapes otherwise follow strict JSON. Duplicate object keys,
invalid UTF-8, `NaN`, infinity, hexadecimal numbers, leading plus signs,
numeric separators, leading/trailing decimal points, JSON5-only escape forms,
and unquoted values are errors. Unquoted keys cannot contain dashes; quote a
key such as `'setting-row'`.

Import canonicalizes valid authoring input to deterministic, minified strict
JSON. Syntax and strict nested schema diagnostics report original line and
column information. Draft 2020-12 schemas are checked in under `schemas/ui/`
and installed at the CMake package variable `KARMA_UI_SCHEMA_DIR`.

## Document schema

A document requires `format`, integer `version`, and an object `root`:

```json5
{
  format: 'karma.ui.document',
  version: 2,

  themes: [{asset: 'ui/theme'}],
  model: {
    title: 'Main Menu',
    settings: {volume: 0.75},
    saves: [],
  },

  root: {
    type: 'panel',
    id: 'main-menu',
    styles: ['screen'],
    layout: {mode: 'column', width: 520, padding: [32, 32, 32, 32], gap: 16},
    children: [
      {type: 'text', props: {text: {bind: 'title'}}},
      {
        type: 'button',
        id: 'play',
        styles: ['primary'],
        props: {
          text: {
            loc: 'menu.play',
            args: {player: {bind: 'profile.name'}},
          },
        },
        on: {click: 'play'},
      },
      {
        type: 'slider',
        props: {
          value: {bind: 'settings.volume'},
          min: 0,
          max: 1,
          step: 0.05,
        },
        on: {change: 'volume-changed'},
      },
      {
        type: 'repeat',
        props: {
          items: {bind: 'saves'},
          item: 'slot',
          key: {expr: 'slot.id'},
          template: {
            type: 'button',
            props: {text: {bind: 'slot.name'}},
            on: {click: 'load-save'},
          },
        },
      },
    ],
  },
}
```

Document-level fields are:

| Field | Meaning |
| --- | --- |
| `format` | Required literal `karma.ui.document`. |
| `version` | Required integer `2`. |
| `themes` | Ordered packaged `{asset: ...}` references, or `{file: ...}` references in a loose-file graph. |
| `model` | Optional initial `ui::Value` object. Calls to `set()` may extend it. |
| `root` | Required retained node object. |
| `canvas` | Optional logical/reference canvas, scale mode, and platform-safe-area policy. See [Canvas and anchors](#canvas-and-anchors). |

Every node requires a string `type`. Common fields are:

| Field | Meaning |
| --- | --- |
| `id` | Stable lookup and reload identity. IDs should be unique in a document. |
| `styles` | Explicit named styles from the active themes. There is no authored selector syntax. |
| `layout` | Typed game layout and box values. |
| `appearance` | Inline box/text/transition values; these win over theme styles. Stateful overlays and named motions are theme-only. |
| `when` | `{bind: 'path'}` or `{expr: '...'}` controlling retained presence. |
| `props` | Type-specific values and bindings. |
| `on` | Action names keyed by `click`, `change`, `cancel`, `toggle`, `close`, or `select`. |
| `semantics` | `label`, `description`, optional `tab_index`, and a supported lower-case `role`. |
| `children` | Ordered child node array. |

`props` is strict where the runtime has a fixed contract. Numeric control
bounds are numbers; orientation is `horizontal` or `vertical`; window feature
flags are Booleans; scrollbar placement/visibility, overflow, sampling,
pointer events, object fit, and transient placement use their documented enum
values. `position` and `size` are two-number arrays. Repeat/list `items` and
`key` are bindings or expressions, while `item` and transient `anchor` are
non-empty strings. Boolean state props (`disabled`, `checked`, `expanded`,
`selected`, `open`, and `collapsed`) may instead be bindings or expressions.
`value` and the document `model` remain intentionally value-polymorphic.

Implemented built-in types are `body`, `div`, `panel`, `text`, `img`/`image`,
`svg`, `button`, `toggle`, `slider`, `select`, `option`, `progress`, `scroll`,
`repeat`, `window`, `tabs`, `tab`, `disclosure`, `tree`, `tree-item`,
`splitter`, `separator`, `spacer`, `popup`, `menu`, `menu-item`, `tooltip`,
and `list`. See [Widget boundaries](#widget-boundaries) for the implemented
game-oriented contracts and intentionally deferred editor/browser features.

Common literal property shorthands include `pointer_events`, per-axis
`scroll_x`/`scroll_y`, and image `sampling`, `object_fit`, and
`object_position`. Floating nodes may use `position: [x, y]`,
`size: [width, height]`, and numeric `z`; equivalent `layout` fields remain the
preferred general-purpose form. Unsupported property names or enum values are
validation errors rather than silently ignored hints.

### Bindings, expressions, and actions

Use explicit binding objects:

- `{bind: 'settings.volume'}` reads a property path. On toggle, slider, and
  select `props.value`, default control changes also write to that path.
- `{expr: 'player.ready && saves[0].id != null'}` evaluates a safe expression.
- `{loc: 'menu.play', args: {name: {bind: 'profile.name'}}}` asks the installed
  `LocalizationProvider` to resolve text. Each argument must be a binding or
  expression; literal arguments are intentionally rejected instead of being
  silently ignored.

Expressions support property paths, string/number/Boolean/null literals,
parentheses, `!`, `&&`, `||`, equality, numeric/string comparisons, and a
ternary. Calls, assignment, and arbitrary mutation are unavailable. Missing
identifiers evaluate as null.

The v2 authoring contract does not require interpolation markers in strings;
use `bind`, `expr`, or `loc` explicitly. A missing localization displays its
key and emits one warning per locale/key pair.

`when` removes and recreates a live retained subtree, invalidating descendant
handles. A keyed `repeat` reconciles instances using `props.key`; omitting it
uses array order. The local scope contains the configured item name and
`<item>_index`.

`on` values are document action names, not script. Register callbacks with
`System::onAction()` or `DocumentController::bindActions()`. Element listeners
registered with `on()` remain the lower-level path for capture/target/bubble
events and cancellation.

## Theme schema and skinning

A theme requires `format: 'karma.ui.theme'` and `version: 2`:

```json5
{
  format: 'karma.ui.theme',
  version: 2,

  variables: {
    accent: '#438cff',
    text: '#f4f7ff',
  },

  fonts: {
    'Karma UI': {
      src: {asset: 'ui/font'},
      weight: 400,
      style: 'normal',
      face_index: 0,
    },
  },

  defaults: {
    button: {
      layout: {height: 44, padding: [10, 10, 10, 10]},
      appearance: {
        box: {background_color: '#29344a', border_color: '#65728a', border_width: 1},
        text: {color: {var: 'text'}, font_family: ['Karma UI'], align: 'center'},
        states: {
          hover: {box: {background_color: '#35435e'}},
          pressed: {box: {background_color: '#1d67d7'}},
          disabled: {box: {opacity: 0.45}},
        },
      },
    },
    scroll: {
      appearance: {
        parts: {
          vertical_track: {box: {background_color: '#101725cc'}, metrics: {width: 12}},
          vertical_thumb: {
            box: {background_color: '#52647f'},
            metrics: {min_length: 24},
            states: {hover: {box: {background_color: '#657a9b'}}},
          },
          horizontal_track: {box: {background_color: '#101725cc'}, metrics: {height: 12}},
          horizontal_thumb: {box: {background_color: '#52647f'}},
          corner: {box: {background_color: '#101725cc'}},
        },
      },
    },
  },

  styles: {
    control: {layout: {align_self: 'stretch'}},
    primary: {
      extends: 'control',
      appearance: {box: {background_color: {var: 'accent'}}},
    },
  },

  motions: {
    pulse: {
      duration_ms: 800,
      delay_ms: 0,
      easing: 'ease-in-out',
      iterations: 'infinite',
      direction: 'alternate',
      keyframes: [
        {at: 0, appearance: {box: {opacity: 0.7}}},
        {at: 1, appearance: {box: {opacity: 1.0}}},
      ],
    },
  },
}
```

There are no author-written type, class, descendant, ID, or pseudo selectors.
Themes provide type `defaults`; nodes explicitly opt into named `styles`.
Named styles may `extends` one name or an ordered array of names. Missing bases
and inheritance cycles produce diagnostics. Typed `{var: 'name'}` references
resolve theme variables.

The effective order is engine defaults, imported themes, document theme order,
type defaults, named styles in the node's explicit `styles` list, matching
state overlays, then node inline `layout`/`appearance`. Later named styles win
conflicts. Inline values are final and can therefore mask a themed state.
Implemented states are `checked`, `selected`, `expanded`, `hover`, `focus`,
`pressed`, and `disabled`.

`appearance.parts` is also a closed contract: `track`, `fill`, `thumb`,
`checkmark`, `arrow`, `chevron`, `grip`, `vertical_track`, `vertical_thumb`,
`horizontal_track`, `horizontal_thumb`, `corner`, `titlebar`, `close_button`,
`collapse_button`, `resize_grip`, `popup`, and `option`. Unknown part names are
validation errors rather than silently ignored skin declarations.

Appearance mappings cover box color/background, opacity, borders, radii,
images, sampling/object placement; text; transitions and motion; window and
scrollbar parts; and typed control track/fill/thumb/checkmark/arrow/chevron/grip
parts. Parts honor colors, radii, opacity, borders, and their implemented
metrics. Nested part states are deliberately narrow: `vertical_thumb`,
`horizontal_thumb`, and `option` accept only `hover`/`pressed` background
colors. Put all other state-dependent skin changes in top-level
`appearance.states`; unsupported part/state combinations are validation
errors instead of no-ops.

Ordinary boxes and widget parts accept `border_image` with required `source`
and four-value `slice`, plus optional four-value `width`. Omit `repeat` for the
compatibility default, `stretch` on both axes. A scalar `'stretch'`, `'repeat'`,
or `'round'` applies to both axes; an array must contain exactly two modes in
horizontal/vertical order. Unknown modes and malformed arrays are validation
errors. Repeat preserves motif scale and crops the final tile; round adjusts
the motif to a nearest positive whole count.

Corners remain single cells, edges repeat on their long axis, and the center
uses both axes. Panels are CPU-tessellated with a 16,384-cell safety limit.
`layout.cursor` or `appearance.cursor` accepts common game
cursor names such as `pointer`, `crosshair`, `move`, `ew-resize`, `ns-resize`,
the diagonal resize names, and `not-allowed`.

Theme `imports` merge recursively. Their variables, defaults, named styles,
fonts, motions, and `extends` bases participate in the composed theme. Import
cycles and missing sources are validation diagnostics.

## Game layout modes

The preferred layout modes are explicit and stable rather than browser-driven:

| `layout.mode` | Current behavior |
| --- | --- |
| `row` | Yoga Flex row. |
| `column` | Yoga Flex column. |
| `grid` | Karma's practical explicit/implicit grid. |
| `overlay` | Block container; unanchored children use ordinary flow and children with `anchors` use the overlay constraint pass. |
| `anchor` | Compatibility name for an overlay/block container. Anchor constraints live on each child. |
| `flex` | Advanced Yoga Flex container using the current defaults. |

Useful layout fields include `width`, `height`, min/max sizes, `margin`,
`padding`, `gap`, `row_gap`, `column_gap`, `grow`, `shrink`, `basis`, alignment
fields, `z`, and `position: [x, y]`. Grid adds `columns`, `rows`, `grid_column`,
and `grid_row`. Numbers are logical pixels. String values retain `%`, `vw`,
`vh`, `em`, `rem`, `fr`, `auto`, `minmax()`, and fixed `repeat()` where the
underlying layout field supports them.

Block/Flex and Grid share intrinsic text/image measurement and can nest in
either direction. Grid supports explicit/implicit tracks, gaps, spans,
non-dense placement, and alignment. Named lines/areas, dense flow, subgrid,
and masonry are not implemented.

### Canvas and anchors

A document defaults to the full logical window. A game can instead author a
stable reference canvas:

```json5
canvas: {
  reference_size: [1920, 1080],
  scale_mode: 'fit',
  safe_area: 'platform',
}
```

`scale_mode` has these exact meanings:

| Mode | Resolution |
| --- | --- |
| `logical` | One UI unit equals one window-logical unit. `reference_size` is optional and is not used. |
| `fit` | Uniformly scales the reference canvas to fit inside the safe rectangle and centers any letterbox. |
| `fill` | Uniformly scales it to cover the safe rectangle, centers it, and clips the cropped edges. |
| `stretch` | Independently scales X and Y so the reference canvas exactly fills the safe rectangle. |
| `pixel-perfect` | Uses the largest integral uniform fit scale when that scale is at least 1; below 1 it falls back to fractional `fit`. `pixel_perfect` is accepted as a JSON spelling alias. |

Every non-`logical` mode requires a positive finite `reference_size`.
`safe_area` accepts `platform`, `none`, `true`, or `false`. Platform insets are
window-logical units. Invalid/negative insets become zero. Insets that exceed
the window are clamped left before right and top before bottom, so even a
degenerate safe rectangle is deterministic. The desktop backends report zero
insets; platform backends can override `Window::getSafeAreaInsets()`.

KSTYLE pixels, layout, pointer-event coordinates, motion, and scroll offsets
use the selected canvas coordinate space. Paint scales again by the independent
framebuffer X/Y content scale. Pointer hit testing applies the inverse canvas
transform, and accessibility bounds are reported back in window-logical
coordinates. The resolved safe rectangle is the document clip, including for
`fill` cropping.

Normalized anchors are declared on a child node:

```json5
layout: {
  anchors: {min: [0.5, 0], max: [1, 1]},
  pivot: [0.5, 0.5],
  position: [-12, 4],
  offsets: {left: 8, top: 8, right: 16, bottom: 8},
}
```

Anchor and pivot coordinates must be finite in `[0, 1]`, with `min <= max`.
Offsets may also be `[left, top, right, bottom]`. An anchored node is removed
from normal Flex/Grid flow and resolved against its parent's content box after
the normal layout pass. On an axis where min equals max, authored/measured size
is preserved and `position - pivot * size` is applied at that anchor. On a
stretched axis, the near offset is added, the far offset is subtracted, and
`position` translates both edges; pivot is ignored on that axis. Negative
results clamp to zero size. A stretched subtree is remeasured at its resolved
size, so nested Flex/Grid content remains deterministic. `pivot` or `offsets`
without `anchors` is a document diagnostic; `position: [x, y]` alone retains
its existing absolute-position meaning.

## Widgets

Buttons, toggles, sliders, inline selects/options, progress controls, and
scroll containers retain the first-pass keyboard, pointer, gamepad, binding,
and accessibility behavior. Slider changes during pointer movement emit the
existing control update path; second-pass drag widgets and scrollbar thumbs
emit `Input` while moving and `Change` on release.

### Scroll containers and scrollbars

`type: 'scroll'` measures content extent, clips children, and enables both
axes as needed. Configure presentation with:

```json5
{
  type: 'scroll',
  id: 'save-list',
  props: {
    scrollbar_placement: 'gutter', // or 'overlay'
    scrollbar_visibility: 'auto', // or 'always' / 'hidden'
    scroll_x: 'hidden', // visible / hidden / auto / scroll
    scroll_y: 'auto',
  },
  layout: {height: 240},
  children: [],
}
```

Painted horizontal and vertical tracks, thumbs, and the two-axis corner are
implemented. Gutters are reserved by default; RTL places the vertical gutter
on the left. Thumb size reflects viewport/content extent and honors the themed
minimum. Pointer dragging captures outside the bar, track clicks page, wheel
input chains to scrollable ancestors at an extent, and a vertical wheel scrolls
a horizontal-only container. Shift+wheel selects the horizontal axis when both
axes are available. Home/End/PageUp/PageDown operate on a focused scroll
ancestor, gamepad shoulders page, and focus changes reveal descendants.

Programmatic methods are `scrollTo()`, `scrollBy()`, and `scrollIntoView()`;
the controller forwards all three. Accessibility nodes report current and
maximum offsets. Native scrollbar hover/drag participates in cursor feedback.
`scroll_x` and `scroll_y` independently control clipping/scrolling on ordinary
panels as well as `scroll`/`list`; only `auto` and `scroll` create a scrollable
axis. Horizontal and vertical tracks/thumbs retain separate skin colors,
borders, radii, nine-slice images, thickness, and minimum lengths.

### Disclosure

A disclosure retains its subtree while excluding collapsed content from
layout, paint, hit testing, focus traversal, and accessibility:

```json5
{
  type: 'disclosure',
  props: {expanded: {bind: 'advanced.open'}},
  on: {toggle: 'advanced-toggled'},
  children: [
    {type: 'text', props: {text: 'Advanced'}}, // Always-visible header.
    {type: 'panel', children: []},             // Hidden when collapsed.
  ],
}
```

Activation toggles `expanded`, writes a bound path, fires the `toggle` action,
and updates accessibility. The first child is the retained header; remaining
children are collapsed content. This differs from `when`, which removes the
subtree and invalidates handles. Accordion exclusivity and intrinsic-height
reveal animation are not built in.

### Splitter

`type: 'splitter'` is a focusable separator that resizes its immediately
preceding sibling. The default `orientation: 'vertical'` changes width;
`'horizontal'` changes height. Pointer dragging captures the pointer and emits
continuous `Input` followed by `Change`; arrow keys adjust by the internal
step. Minimum size is enforced and the native cursor reflects orientation.
Maximum constraints, symmetric resizing, and a bound/persisted size model
remain unfinished.

### Floating window

`type: 'window'` is an in-engine retained panel, not an operating-system
window:

```json5
{
  type: 'window',
  id: 'inventory',
  layout: {position: [80, 64], width: 480, height: 420},
  props: {
    title: 'Inventory',
    state: {bind: 'windows.inventory'},
    open: true,
    resizable: true,
    closable: true,
    collapsible: true,
  },
  on: {close: 'inventory-closed', toggle: 'inventory-collapsed'},
  children: [],
}
```

Windows paint a title bar, drag by the title bar, resize from edges/corners,
capture the pointer, clamp a visible title region to the logical window, and
come to front when manipulated. Close and collapse buttons update state and
actions. Titlebar, close/collapse buttons, and the resize grip accept the same
rounded, bordered, opacity, and nine-slice part skinning as controls.
`bringToFront()` is also public.

The optional state binding reads and writes an object with `open`, `collapsed`,
`position: [x, y]`, `size: [width, height]`, and `z`. Use a bound model when
geometry must also live in the game model. Valid hot reload restores unbound
open/collapsed state, current geometry, and z-order by stable identity. This is
session state only; maximum-size constraints, safe-area docking/snapping, and
cross-launch persistence are not implemented.

### Widget boundaries

`tabs`/`tab` implement selected value and keyboard focus movement. Tree
selection is separate from tree-item expansion. `popup` and `menu` place
against an authored anchor, dismiss on outside click or cancel, and restore
focus; menu items dispatch actions. Open select options, popups, menus, and
tooltips are promoted to a document overlay pass, so later ordinary siblings
cannot paint or hit-test over them. `tooltip` honors `delay_ms` on the UI
clock. `list` implements fixed-extent keyed virtualization with bounded
overscan using `props.item_extent` and `props.overscan`. Editable text,
selection, IME, color editors, tables, and docking remain outside the runtime.

## Events, focus, and handles

`System::on(element, type, callback, options)` installs synchronous listeners.
Capture listeners run from the root toward the target, target listeners run at
the target, and ordinary listeners bubble through ancestors.
`stopPropagation()` stops later propagation; `preventDefault()` suppresses
widget behavior.

Pointer hit testing respects retained visibility, rectangular overflow clips,
z-order, and `pointer-events`. Tab/Shift-Tab use explicit `tab_index` and stable
traversal. D-pad/left-stick input performs spatial focus navigation; A accepts,
B emits cancel, and shoulder bindings page. Bindings are configurable through
`UiSystemConfig::gamepad_navigation`.

Documents render by ascending layer and opening order; input uses the reverse.
A visible modal document blocks lower documents and gameplay. Consumed keys,
buttons, axes, and pointer drags are filtered from event-driven and live-polled
gameplay input.

Document, element, listener, and dynamic-image handles are generational. Stale
handles fail safely. Closing a document or removing a retained node invalidates
its element listeners. Action listeners belong to the document and survive a
successful hot reload. Closing during dispatch is deferred.

## Text, paint, resources, and accessibility

Theme `fonts` map family names to deterministic packaged faces. Weight, style,
and `face_index` select variants and TTC/OTC collection faces. No operating-
system font discovery occurs. ICU supplies bidi, grapheme, and line boundaries;
HarfBuzz shapes clusters; FreeType rasterizes grayscale and color glyphs.
Authors declare fallback order explicitly with the `font_family` array; the
font declaration has no separate `fallback` field. Resolution is per grapheme
cluster, then U+FFFD, then an engine tofu box.

The compositor generates indexed geometry for solid/rounded boxes, borders,
linear/radial gradients, text, images, SVGs, typed widget parts, transforms,
and nine-slice panels. Rectangular overflow scissors remain the only
clip shape. Transitions restart from the current interpolated value. Named
motion clips sample numeric, color, and compatible 2D transform values;
`motion_scale == 0` applies the finite final value immediately.

`ImageSource` can reference a packaged raster asset, an owned dynamic image,
or a borrowed render target. Render targets are never destroyed by UI. SVGs
are sandbox-validated and rasterized through LunaSVG at their physical size.
The asset registry, optional graphics device, localization provider, render
targets, and accessibility snapshot returned by `accessibilityTree()` are all
borrowed as documented in `karma/ui.h`; they must not outlive their owner or
documented rebuild boundary. Dynamic images are instead owned by their
creating `System` until explicit destruction or system shutdown.
Glyph and SVG caches are budgeted. Glyph additions update only their atlas
region after initial page creation, and SVG intrinsic dimensions are cached by
asset content hash. Each draw command carries blend, sampler, texture mode,
and rectangular clip state.

`accessibilityTree()` returns an engine semantic snapshot with roles, names,
descriptions, logical bounds, focus and control state, scroll ranges, and
supported actions. It is not an OS screen-reader bridge.

## Loose files and hot reload

Shipping code normally calls `open(asset_key)`. Development builds can open a
document directly beneath configured roots:

```cpp
karma::app::EngineConfig config;
config.native_ui.development_files.enabled = true;
config.native_ui.development_files.roots = {
    project_root / "game/ui",
    project_root / "shared/ui",
};
config.native_ui.development_files.debounce = std::chrono::milliseconds(75);
config.native_ui.development_files.polling_fallback =
    std::chrono::milliseconds(250);

// Later, after EngineApp creates the System:
auto opened = ui->openFileController(
    "menus/main.kui.json5", {.layer = 100, .modal = true});
```

The API is compiled in every profile that includes native UI, but development
file loading defaults on only without `NDEBUG`. When disabled, `openFile()` and
`openFileController()` return diagnostics rather than bypassing packaged
assets.

Roots are searched in configured order. The requested document path and every
recursive `{file: ...}` reference must be relative and canonically remain
inside the selected root. Absolute paths, `..` escapes, backslashes, drive or
scheme colons, missing files, and symlink escapes are rejected. Backslash,
drive, and scheme checks happen lexically before filesystem access. Relative
theme, font, SVG, and raster references are imported into a generated
development asset graph; the authoring JSON is rewritten to generated asset
keys in the staging registry.

The watcher is recursive. Linux uses inotify and Windows uses
`ReadDirectoryChangesW`; all platforms retain content-fingerprint polling as a
safety net and fallback. Queue overflow, inaccessible roots, and directory
topology uncertainty request a complete dependency rescan. macOS currently
uses polling rather than a native FSEvents backend.

File events are coalesced until the debounce interval expires. The complete
affected graph is rebuilt in an isolated registry and committed only if every
document/theme/resource validates. Runtime document/theme parsing is queued on
the reload worker; only the newest generation is swapped at a frame boundary.
Invalid or partially written edits keep the complete last-good live UI and
publish diagnostics.

Successful full-document reload preserves the document handle, model, action
listeners, focus, scroll offsets, unbound checked/control values,
open/expanded/collapsed state, and unbound window geometry/z by stable ID,
repeat key, or structural identity where possible. It recreates element
handles and element-specific listeners. A theme-only reload keeps retained
nodes and recomputes style.

Package-source documents and themes also retain private development paths and
participate in content-hash polling. Baked assets and directly registered
assets remain filesystem-independent. Loose-file graphs watch referenced
fonts/SVGs/textures; packaged resource sources otherwise update through
registry replacement or package reimport.

## Exhaustive showcase

`ui_showcase` is the runnable kitchen-sink example for this implementation:

```bash
cmake -S . -B build/native-ui -G Ninja \
  -DKARMA_HEADLESS=OFF \
  -DKARMA_ENABLE_NATIVE_UI=ON \
  -DKARMA_ENABLE_IMGUI=OFF \
  -DKARMA_ENABLE_RMLUI=OFF \
  -DKARMA_BUILD_EXAMPLES=ON \
  -DKARMA_FETCH_DEPS=ON
cmake --build build/native-ui --target ui_showcase --parallel
./build/native-ui/examples/ui/showcase
```

Its three pages cover controls and two-way bindings; conditions, disclosures,
selects, popups, menus, and tooltips; keyed repeats and per-axis scrollbars;
tabs, trees, a splitter, and a 250-row virtual list; Flex, Grid, anchors,
overflow, SVG/raster/dynamic images; localization and RTL shaping; motion;
modal input; and movable/resizable/collapsible windows. The medieval panel in
`examples/assets/ui/showcase/medieval_nine_slice.png` is shown at several
sizes through the real `border_image` nine-slice path.

The example opens `showcase.kui.json5` as a sandboxed loose document. Edit it,
`showcase.kstyle.json5`, or its imported `base.kstyle.json5` while the process
runs to demonstrate dependency-aware transactional hot reload. The generated
texture, packaged font, and SVG still load through the normal asset registry.

## Current boundaries

- The internal theme adapter still uses private selector/rule terminology even
  though authors never write selectors. Those private selectors and their
  candidate indexes are compiled during load/hot-reload staging.
- The main retained runtime remains large, although its DOM state/traversal is
  isolated in `runtime_dom.*` and its former two-pass self-include has been
  replaced by one private implementation include; see the extraction order in
  `NATIVE_UI_STATUS.md`.
- Splitter maximum/symmetric policy and window docking/snapping constraints
  are not yet productized.
- Painted transforms do not change layout, hit-test bounds, accessibility
  bounds, or scissors.
- Rounded/nested clipping, arbitrary masks, blur, and shadows are unavailable.
- Editable text, selection, IME, touch/multi-pointer input, and OS
  accessibility bridges are not implemented.
- UI resources have no in-place graphics-device reset transaction; recreate
  the native system with the engine graphics lifecycle.

Embedded scripts, arbitrary expression calls, remote resources, and full
HTML/CSS compatibility are intentionally outside the native UI model.
