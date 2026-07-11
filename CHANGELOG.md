# Changelog

This changelog records notable user-facing changes from Karma 0.7.0 onward.
Earlier project history remains available in Git.

The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and Karma uses [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [0.7.0] - 2026-07-11

### Added

- Added the first-party retained native UI authoring/runtime path based on
  validated `.kui.json5` documents and `.kstyle.json5` themes. Native UI is the
  graphical-profile default and remains independent of ImGui and RmlUi.
- Added retained buttons, toggles, sliders, progress bars, selects, tabs,
  trees, disclosures, splitters, popups, menus, delayed tooltips, scrollbars,
  virtual lists, and movable/resizable/collapsible in-engine windows.
- Added synchronous model bindings, conditional and repeated content,
  generational document/element/listener handles, `setMany()` bulk updates,
  accessibility snapshots, and dependency-aware transactional hot reload.
- Added stretch, repeat, and round modes for ordinary and widget-part
  nine-slice images; rounded tiling is demonstrated by the medieval showcase.
- Added retained-paint budgeting and frame diagnostics for reconciliation,
  style, layout, fragments, vertices, commands, and native UI stage timings.

### Changed

- Reworked the UI Forge showcase into a compact RPG skin using Marcellus SC,
  Alegreya Sans, and Noto Naskh Arabic fonts, thin borders, skinned control
  parts, per-axis scrollbars, and working wheel scrolling.
- Split the former native UI monolith into focused DOM, styling,
  reconciliation, interaction, layout, presentation, resource, accessibility,
  frame, and public API modules. Public provider ownership and private runtime
  dependencies are now explicit.
- Replaced broad document invalidation with dependency-indexed revisions and
  retained presentation fragments. Idle frames reuse retained work; targeted
  model changes, scrolling, motion, and z-order changes avoid unrelated tree
  traversal.
- Cursor selection is arbitrated once per frame across native, custom, and
  debug UI layers. Platform cursor objects are created lazily and reused until
  window shutdown.

### Performance

- Reduced the 1600x900 Debug UI Forge benchmark from approximately 50 ms native
  UI frame p95 to 2.980 ms after warm-up, with no sampled frame above 10 ms.
- Reduced the same scene from 947 UI draw commands to 75 through retained
  fragments, authored-scissor batching, and command coalescing. See the
  [native UI status benchmark](docs/NATIVE_UI_STATUS.md#debug-showcase-benchmark)
  for the measurement conditions.

### Fixed

- Fixed nine-slice seams and incorrect edge/center stretching by tessellating
  repeat and round cells with clipped partial tiles and texel-safe UVs.
- Fixed cursor flicker and per-frame native cursor allocation.
- Fixed over-specific primitive clipping that prevented adjacent glyph and
  quad commands from batching under one authored scissor.
- Fixed floating-window drag latency and the top-left drag case that could
  disturb sibling layout. Title-bar movement now updates the live window
  placement in the same pointer event without reconciliation, restyling, or
  Yoga layout; resizing still requests layout for child reflow.

### Known boundaries

- Radio buttons and radio groups are not built-in widgets in 0.7.0. Use a
  `select` or app-managed mutually exclusive toggles until a group/value
  authoring contract is introduced.
- Editable text, selection, clipboard editing, IME/composition,
  touch/multi-pointer input, OS accessibility bridges, window docking, and
  advanced clipping/effects remain outside this release.
