# DiligentFX Import Loop

This prompt is for repeatedly mining DiligentFX for reusable renderer
capabilities and integrating those capabilities into Karma as engine features.
DiligentFX is a framework repository, not a tutorial application repository, so
the loop is organized around component families rather than sample apps.

The goal is not to embed DiligentFX wholesale. Each pass should extract one
engine-level capability, route it through Karma's layered architecture, and add
examples only after the capability exists naturally in the engine.

## Source Checkout

Keep the DiligentFX checkout outside the Karma repo:

```bash
export DILIGENT_FX_DIR=/home/quinn/Documents/codex-projects/karma/fork1/DiligentFX
```

Current checkout:
`/home/quinn/Documents/codex-projects/karma/fork1/DiligentFX`

## Operating Rules

- Read `docs/AGENTS.md`, `src/AGENTS.md`, `include/karma/AGENTS.md`, and
  `docs/ARCHITECTURE.md` before changing engine code.
- If a change touches the runtime debug editor, read `docs/DEBUG_EDITOR.md`.
- Do not add new top-level engine subsystems under `src/` or `include/karma/`.
- Do not add compatibility shims, forwarding headers, legacy include paths, or
  DiligentFX-named public API.
- Do not hardcode DiligentFX component names, repo paths, framework classes, or
  demo behavior into engine API.
- Keep Diligent-specific implementation details inside
  `src/rendering/renderer/backends/diligent` unless a narrow public access
  surface already exists or is genuinely required.
- Prefer vertical slices over broad rewrites. One loop pass should integrate one
  capability or one tightly related capability family.
- If DiligentFX has a complete framework object but Karma needs only a subset,
  design the Karma contract first and use DiligentFX as a reference.
- If Karma already has the capability, improve the existing implementation only
  when DiligentFX reveals a real gap. Otherwise record it as already covered.
- Before copying source, shaders, or assets, verify the upstream license and
  preserve required attribution. Prefer reimplementing concepts in Karma style
  over copying framework code.

## Capability Routing

Route new work by ownership:

- Renderer-facing data contracts: `include/karma/rendering.h`
- UI provider adapters: `src/features/ui/<provider>`, with public factories in
  `include/karma/ui.h`
- Runtime composition only: `include/karma/app.h` and `src/runtime/...`

When in doubt, add the smallest public data contract in the lower layer and let
`runtime` wire systems together.

## Loop State

Maintain `docs/DILIGENTFX_IMPORT_LEDGER.md` with one entry per inspected
component family:

```markdown
## <component path or family>

- Status: pending | skipped | integrated | partially integrated
- Upstream capability: <what DiligentFX provides>
- Karma capability: <engine-level name, not DiligentFX-specific>
- Engine owner: rendering | world | content | features | runtime
- Existing Karma coverage: none | partial | complete
- Decision: integrate | improve existing | skip | future work
- Reason:
- Files changed:
- Verification:
- Follow-up:
```

The ledger is process state, not product API. It may mention DiligentFX paths.
Engine headers, components, configs, and examples should use engine concepts.

## Component Loop

1. Inventory the component.
   - Read README, CMake, public interface headers, implementation files,
     shaders, resource layouts, and dependencies.
   - Identify required render targets, textures, buffers, constants, pass order,
     history resources, motion vectors, depth/normal/G-buffer requirements, and
     feature flags.
   - Ignore DiligentFX object lifecycle patterns unless Karma lacks the
     underlying engine capability.

2. Classify the reusable capability.
   - Name the capability in engine terms: "postprocess stack", "bloom pass",
     "scene normal buffer", "temporal history resources", "shadow filtering",
     "tone mapping pass", "debug grid overlay", etc.
   - Decide whether it is a renderer contract, backend-only improvement,
     content importer extension, ECS component, visual feature module, or
     runtime composition change.

3. Check Karma first.
   - Search for equivalent types, settings, shaders, passes, components, and
     examples.
   - Prefer extending an existing narrow contract over adding a parallel
     concept.
   - If an existing API is wrong for the project direction, replace it cleanly.
     Do not keep a backward-compatible path unless the project explicitly asks
     for one.

4. Design the integration.
   - Define the smallest public API that represents the capability without
     exposing DiligentFX structure.
   - Keep backend resource ownership private.
   - Add ECS components only for persistent world state.
   - Add feature modules only when behavior exists above raw rendering.
   - Add examples only to prove engine usage, not to recreate DiligentFX.

5. Implement a vertical slice.
   - Public contract or component.
   - Renderer extraction or feature system, if needed.
   - Diligent backend support.
   - Shader/resource changes, if needed.
   - Focused example or test fixture after the engine capability exists.
   - Documentation update.

6. Verify.
   - Run the smallest relevant build or tests first.
   - For renderer-visible work, build and run a graphical example when
     practical.
   - If graphical validation is blocked by hardware, document the limitation and
     provide the strongest compile-time or unit-level verification available.

7. Update the ledger.
   - Mark the component status and record the engine capability integrated.
   - Add follow-ups only for real gaps, not for generic wishlist items.

## Ready-To-Paste Codex Prompt

Use this prompt in a fresh Codex turn for each DiligentFX loop pass:

```text
You are working in the Karma engine repo. Follow all docs/AGENTS.md instructions.
Read docs/ARCHITECTURE.md before changing engine code. Do not add new top-level
engine subsystems, forwarding headers, backward compatibility shims, legacy API,
or DiligentFX-named public API.

Goal: inspect the next pending DiligentFX component from $DILIGENT_FX_DIR and
integrate its reusable graphics capability naturally into Karma.

Process:
1. Open docs/DILIGENTFX_IMPORT_LOOP.md and follow it.
2. Create or update docs/DILIGENTFX_IMPORT_LEDGER.md.
3. Pick the next pending component in deterministic ledger order.
4. Inventory the component README, CMake, public interfaces, source, shaders,
   resource dependencies, and pass ordering.
5. Identify the engine-level capability. Do not embed the DiligentFX framework
   object model directly.
6. Search Karma for existing coverage and choose integrate, improve existing,
   skip, or future work.
7. Implement one vertical slice only, using Karma's layered architecture:
   world components for persistent ECS data, rendering contracts for renderer
   API, content for import metadata, features/visual for optional visual
   systems, Diligent details under the Diligent backend, and runtime only for
   composition.
8. Add focused docs/tests/examples only where they prove the engine capability.
9. Build or test the smallest relevant target, then update the ledger with
   files changed, verification, and follow-up.

Constraints:
- No hardcoded DiligentFX component names, repo paths, sample assets, or demo
  scene logic in engine API or backend behavior.
- No compatibility shims or old include paths.
- Do not vendor upstream code blindly. Check license before copying; when
  possible, reimplement the concept in Karma style.
- Keep the change small enough to review.

Report:
- Which DiligentFX component was inspected.
- What capability was integrated or why it was skipped/deferred.
- Files changed.
- Verification command and result.
- Next component or follow-up.
```
