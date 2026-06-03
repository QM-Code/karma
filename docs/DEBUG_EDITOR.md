# Debug Editor Extension Guide

This guide applies to the runtime debug editor in
`src/runtime/debug/debug_overlay.cpp` and
`include/karma/runtime/debug/debug_overlay.h`.

## Scope

- Keep debug editor changes scoped to `DebugOverlayLayer` unless a runtime API is
  genuinely needed.
- Preserve runtime behavior when reorganizing controls. Renderer tuning should
  continue to call the existing `GraphicsDevice` setters with the same clamping.
- Do not add persistence beyond normal ImGui layout state unless explicitly
  requested.
- Keep editor UI simple: standard ImGui tabs, child panes, separators, tree
  nodes, inputs, and buttons are preferred.

## Layout

- Top-level categories belong in the existing `Karma Debug` tab bar.
- Scene data belongs in the `Scene` tab. Use the hierarchy pane for selection
  and the inspector pane for selected entity/component controls.
- Rendering controls and stats belong in the `Renderer` tab.
- Frame pacing and timing diagnostics belong in the `Performance` tab.

## Controls

- Use the local edit helpers (`editFloat`, `editInt`, `editBool`,
  `inputTextString`, and similar helpers) instead of open-coded widgets when
  possible.
- Boolean fields must be represented as toggle buttons, not numeric 0/1 inputs.
- Fields that are reasonably boolean should also use toggle buttons. Examples
  include integer-backed settings where valid editable values are only 0 or 1.
- Numeric controls should clamp to the same valid range used by the runtime
  setter or component contract.
- Enums should use combo boxes with explicit labels.
- Bit masks should remain explicit numeric or scalar inputs unless the intended
  bit meanings are known and can be shown as individual toggles.

## Verification

- Build `karma_example` after changes.
- Run `KARMA_ENGINE_EDITOR_DEBUG=1 ./build/karma_example` from the repository
  root, or run `KARMA_ENGINE_EDITOR_DEBUG=1 ./karma_example` from `build/`.
- Check that edited controls still mutate the same runtime state and that the
  debug overlay survives renderer warm-up.
- Avoid committing or intentionally editing `imgui.ini` as part of debug editor
  UI changes.
