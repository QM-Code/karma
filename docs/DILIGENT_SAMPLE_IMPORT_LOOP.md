# Diligent Sample Import Loop

This prompt is for repeatedly mining Diligent Samples for renderer capabilities
and integrating those capabilities into Karma as engine features. The goal is
not to port sample applications. Each pass should extract one reusable
capability, route it to the correct Karma layer, and leave behind clean public
API, backend support, documentation, and verification.

## Operating Rules

- Read `docs/AGENTS.md`, `src/AGENTS.md`, `include/karma/AGENTS.md`, and
  `docs/ARCHITECTURE.md` before changing engine code.
- If a change touches the runtime debug editor, read `docs/DEBUG_EDITOR.md`.
- Do not add new top-level engine subsystems under `src/` or `include/karma/`.
- Do not add compatibility shims, forwarding headers, legacy include paths, or
  sample-named API.
- Do not hardcode Diligent sample names, tutorial numbers, sample assets, or
  sample scene behavior into the engine.
- Keep Diligent-specific implementation details inside
  `src/rendering/renderer/backends/diligent` unless a narrow public access
  surface already exists or is genuinely required.
- Prefer vertical slices over broad rewrites. One loop pass should integrate one
  capability or one tightly related capability family.
- If the same sample demonstrates several independent ideas, split them into
  separate loop passes.
- If Karma already has the capability, improve the existing implementation only
  when the sample reveals a real gap. Otherwise record it as already covered.
- Before copying source, shaders, or assets, verify the upstream license and
  preserve required attribution. Prefer reimplementing concepts in Karma style
  over copying sample code.

## Capability Routing

Route new work by ownership, not by where the Diligent sample placed it:

- Renderer-facing data contracts: `include/karma/rendering.h`
- UI provider adapters: `src/features/ui/<provider>`, with public factories in
  `include/karma/ui.h`
- Runtime composition only: `include/karma/app.h` and `src/runtime/...`

When in doubt, add the smallest public data contract in the lower layer and let
`runtime` wire systems together.

## Loop State

Maintain a ledger while working through samples. Create or update
`docs/DILIGENT_SAMPLE_IMPORT_LEDGER.md` with one entry per inspected sample:

```markdown
## <sample path or name>

- Status: pending | skipped | integrated | partially integrated
- Upstream capability: <what the sample actually demonstrates>
- Karma capability: <engine-level name, not sample-specific>
- Engine owner: rendering | world | content | features | runtime
- Existing Karma coverage: none | partial | complete
- Decision: integrate | improve existing | skip
- Reason:
- Files changed:
- Verification:
- Follow-up:
```

The ledger is process state, not product API. It may mention sample names. Engine
headers, components, configs, and examples should use engine concepts instead.

## Per-Sample Loop

1. Inventory the sample.
   - Read its README, CMake file, main source files, shaders, and asset list.
   - Identify the graphics technique, required resources, runtime controls, and
     data flow.
   - Ignore tutorial scaffolding, sample app lifecycle code, camera glue, and
     one-off scene setup unless Karma lacks the underlying engine capability.

2. Classify the reusable capability.
   - Name the capability in engine terms, for example "texture array material
     binding", "compute particle simulation", "instanced draw submission", or
     "offscreen render target sampling".
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
     exposing Diligent sample structure.
   - Keep backend resource ownership private.
   - Add ECS components only for persistent world state.
   - Add feature modules only when behavior exists above raw rendering.
   - Add examples only to prove engine usage, not to recreate the upstream
     sample.

5. Implement a vertical slice.
   - Public contract or component.
   - Renderer extraction or feature system, if needed.
   - Diligent backend support.
   - Shader/resource changes, if needed.
   - Focused example or test fixture, if useful.
   - Documentation update.

6. Verify.
   - Run the smallest relevant build or tests first.
   - For renderer-visible work, build and run a graphical example when
     practical.
   - If a graphical run is not possible, document the limitation and provide the
     strongest compile-time or unit-level verification available.

7. Update the ledger.
   - Mark the sample status and record the engine capability integrated.
   - Add follow-ups only for real gaps, not for generic wishlist items.

## Ready-To-Paste Codex Prompt

Use this prompt in a fresh Codex turn for each loop pass:

```text
You are working in the Karma engine repo. Follow all docs/AGENTS.md instructions.
Read docs/ARCHITECTURE.md before changing engine code. Do not add new top-level
engine subsystems, forwarding headers, backward compatibility shims, legacy API,
or sample-named public API.

Goal: inspect the next unprocessed Diligent Sample from
$DILIGENT_SAMPLES_DIR and integrate its reusable graphics capability naturally
into Karma.

Process:
1. Open docs/DILIGENT_SAMPLE_IMPORT_LOOP.md and follow it.
2. Create or update docs/DILIGENT_SAMPLE_IMPORT_LEDGER.md.
3. Pick the next ledger-pending or not-yet-ledgered sample in deterministic
   path order.
4. Inventory the sample's README, CMake, source, shaders, and assets.
5. Identify the engine-level capability. Do not port the sample app.
6. Search Karma for existing coverage and choose integrate, improve existing,
   or skip.
7. Implement one vertical slice only, using Karma's layered architecture:
   world components for persistent ECS data, rendering contracts for renderer
   API, content for import metadata, features/visual for optional visual
   systems, Diligent details under the Diligent backend, and runtime only for
   composition.
8. Add focused docs/tests/examples only where they prove the engine capability.
9. Build or test the smallest relevant target, then update the ledger with
   files changed, verification, and follow-up.

Constraints:
- No hardcoded sample names, tutorial numbers, sample assets, or sample scene
  logic in engine API or backend behavior.
- No compatibility shims or old include paths.
- Do not vendor upstream sample code blindly. Check license before copying; when
  possible, reimplement the concept in Karma style.
- Keep the change small enough to review.

Report:
- Which sample was inspected.
- What capability was integrated or why it was skipped.
- Files changed.
- Verification command and result.
- Next sample or follow-up.
```

## Suggested Bootstrap Command

Keep the Diligent Samples checkout outside Karma unless there is a deliberate
dependency decision:

```bash
export DILIGENT_SAMPLES_DIR=/path/to/DiligentSamples
```

Then start a Codex turn with the ready-to-paste prompt above.
