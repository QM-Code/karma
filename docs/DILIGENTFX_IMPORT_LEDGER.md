# DiligentFX Import Ledger

Source checkout:
`/home/quinn/Documents/codex-projects/karma/fork1/DiligentFX`

Current source revision when this ledger was created: `a2259ac3` on `master`.

Process note: DiligentFX is a framework repository. Import passes should inspect
component families and extract reusable engine capabilities. Do not graft
DiligentFX framework classes into Karma's public API.

## Future Work Backlog

These items are intentionally deferred rather than forgotten.

- USD/Hydra integration: `Hydrogent/` and `Samples/USDViewer` style workflows
  are content/runtime product decisions. Revisit only if Karma chooses USD as a
  supported scene pipeline.
- Full deferred renderer: DiligentFX `GBuffer`, SSAO, SSR, TAA, and DOF all
  benefit from stable scene color, depth, normal, material, motion-vector, and
  history resources. Prefer a clean renderer postprocess stack and render-target
  contract before importing individual effects deeply.
- Hardware-gated ray tracing remains deferred from the DiligentSamples ledger.
  Do not prioritize ray-traced or hybrid features on the current workstation.
- Framework migration: `Radient/` is a higher-level renderer abstraction. Use it
  only as architecture reference unless the project deliberately migrates the
  renderer model.

## Priority Queue

1. Postprocess stack foundation: frame history, fullscreen/compute dispatch
   utilities, scene color/depth input contracts, and examples. Integrated as a
   Diligent backend fullscreen stack with color/depth/history resources.
2. Bloom: practical first postprocess effect; only needs HDR scene color.
   Integrated as a Karma-native prefilter/downsample/upsample pass chain.
3. Tone mapping as a pass: move beyond inline forward-shader tone mapping when
   postprocess stack exists. Integrated as a postprocess tone/color pass.
4. Scene normal/material/motion resources: prerequisite for SSAO, SSR, TAA, and
   high-quality DOF. Partially addressed with scene color/depth/history and
   depth-derived normals; full material/motion buffers remain future work.
5. SSAO and SSR: replace current forward-only approximations with proper
   screen-space postprocess passes. Integrated as depth-derived screen-space
   effects; full G-buffer/Hi-Z versions remain future work.
6. TAA and DOF: require frame history, jitter, motion vectors, and depth.
   Integrated as history TAA and depth-based DOF; jitter/motion-vector upgrades
   remain future work.
7. Shadow quality improvements: compare VSM/EVSM, cascade filtering, and
   artifact reduction against Karma's existing cascaded/point shadows. Improved
   existing PCF with tent-weighted filtering; VSM/EVSM remain future work.

## PBR

- Status: partially integrated
- Upstream capability: glTF/USD physically based rendering with IBL,
  precomputed BRDF/environment resources, material feature flags, clearcoat,
  sheen, anisotropy, transmission, volume, iridescence, OIT helpers, and
  renderer-specific shader permutations.
- Karma capability: native glTF/PBR material pipeline.
- Engine owner: rendering
- Existing Karma coverage: partial
- Decision: improve existing
- Reason: Karma already owns native glTF import, ECS scene data, material
  records, IBL, and a forward PBR path. DiligentFX PBR should be used as a
  reference for gaps, not imported as a standalone renderer.
- Files changed:
  - `docs/DILIGENTFX_IMPORT_LEDGER.md`
- Verification: inspected DiligentFX top-level README, `PBR/README.md`,
  `PBR/interface`, `PBR/src`, and `Shaders/PBR`.
- Follow-up: compare DiligentFX PBR shader feature coverage against Karma's
  current shader after the Diligent GLTFViewer parity pass. Candidate gaps:
  iridescence, OIT, more exact BRDF helpers, debug views, and material
  permutation controls.

## Components/ShadowMapManager

- Status: partially integrated
- Upstream capability: cascaded shadow map manager with cascade stabilization,
  PCF, VSM, EVSM, fixed/world-sized kernels, best-cascade search, filtering
  across cascades, and artifact-reduction helpers.
- Karma capability: shadow quality settings and filtering modes.
- Engine owner: rendering
- Existing Karma coverage: partial
- Decision: improve existing
- Reason: Karma already has native directional cascaded shadows, point shadows,
  bias tuning, and Diligent shadow passes. DiligentFX can inform quality
  upgrades without replacing backend ownership.
- Files changed:
  - `src/rendering/renderer/backends/diligent/backend_init.cpp`
  - `docs/DILIGENTFX_IMPORT_LEDGER.md`
- Verification: inspected `Components/README.md`,
  `Components/interface/ShadowMapManager.hpp`, `Components/src/ShadowMapManager.cpp`,
  and `Shaders/Common/public/Shadows.fxh`; built
  `rendering_postprocess`; launched the example under `timeout`
  without Diligent shader errors.
- Follow-up: compare VSM/EVSM and cross-cascade filtering as a focused shadow
  quality pass if shadow artifacts become a priority.

## Components/GBuffer

- Status: partially integrated
- Upstream capability: reusable G-buffer render target set for radiance, normal,
  base color, material data, motion vectors, depth, and related shader
  structures.
- Karma capability: scene render target contract for deferred/postprocess
  inputs.
- Engine owner: rendering
- Existing Karma coverage: partial
- Decision: integrate
- Reason: Karma now has a public postprocess setting contract and Diligent
  backend scene color/depth/history resources. The first SSAO/SSR/DOF pass
  reconstructs normals from depth, but Karma still does not expose a full
  normal/material/motion-vector G-buffer.
- Files changed:
  - `include/karma/rendering.h