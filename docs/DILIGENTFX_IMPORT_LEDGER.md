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
  - `include/karma/rendering/renderer/post_process.h`
  - `include/karma/rendering/renderer/backend.hpp`
  - `include/karma/rendering/renderer/device.h`
  - `src/rendering/renderer/device.cpp`
  - `src/rendering/renderer/backends/diligent/backend.hpp`
  - `src/rendering/renderer/backends/diligent/backend_render.cpp`
  - `src/rendering/renderer/backends/diligent/passes/post_process/`
  - `docs/DILIGENTFX_IMPORT_LEDGER.md`
- Verification: built `rendering_postprocess`; launched the
  example under `timeout` without Diligent shader errors.
- Follow-up: add explicit normal, material roughness, and motion-vector outputs
  when Karma moves beyond forward-only rendering.

## Components/ToneMapping

- Status: integrated
- Upstream capability: tone-mapping component and shader utilities.
- Karma capability: postprocess tone mapping pass.
- Engine owner: rendering
- Existing Karma coverage: partial
- Decision: integrate
- Reason: Karma now exposes `PostProcessSettings` and applies tone exposure,
  contrast, saturation, and filmic color mapping in the fullscreen postprocess
  pass.
- Files changed:
  - `include/karma/rendering/renderer/post_process.h`
  - `src/rendering/renderer/backends/diligent/passes/post_process/`
  - `examples/diligentfx_postprocess_example.cpp`
  - `docs/DILIGENTFX_IMPORT_LEDGER.md`
- Verification: built `rendering_postprocess`; launched the
  example under `timeout` without Diligent shader errors.
- Follow-up: move the default scene color path to an HDR format before removing
  inline forward tone mapping entirely.

## Components/EnvMapRenderer

- Status: partially integrated
- Upstream capability: environment map rendering and conversion helpers.
- Karma capability: environment map loading, prefiltering, BRDF LUT, and skybox.
- Engine owner: rendering
- Existing Karma coverage: partial
- Decision: improve existing
- Reason: Karma already supports HDR/KTX environment loading, irradiance,
  prefiltered environment maps, BRDF LUT generation, and skybox drawing. Use
  DiligentFX only to close quality or format gaps.
- Files changed:
  - `docs/DILIGENTFX_IMPORT_LEDGER.md`
- Verification: initial inventory only.
- Follow-up: compare DiligentFX environment precompute/sample code if IBL
  quality diverges from GLTFViewer expectations.

## Components/BoundBoxRenderer

- Status: pending
- Upstream capability: debug bounding-box rendering.
- Karma capability: renderer debug primitives.
- Engine owner: rendering
- Existing Karma coverage: partial
- Decision: improve existing
- Reason: Karma has line rendering and debug overlays. A bounding-box helper
  should be an engine debug primitive, not a DiligentFX-shaped component.
- Files changed:
  - `docs/DILIGENTFX_IMPORT_LEDGER.md`
- Verification: initial inventory only.
- Follow-up: add only if runtime/editor debug workflows need box overlays.

## Components/CoordinateGridRenderer

- Status: pending
- Upstream capability: coordinate grid overlay rendering.
- Karma capability: debug/editor grid visual.
- Engine owner: features
- Existing Karma coverage: none
- Decision: future work
- Reason: This belongs in a debug/editor visual layer and should not be imported
  before the editor/debug visualization surface needs it.
- Files changed:
  - `docs/DILIGENTFX_IMPORT_LEDGER.md`
- Verification: initial inventory only.
- Follow-up: revisit with debug editor work.

## Components/VectorFieldRenderer

- Status: pending
- Upstream capability: vector field visualization.
- Karma capability: debug vector-field visual.
- Engine owner: features
- Existing Karma coverage: none
- Decision: future work
- Reason: Useful for diagnostics, but not a current renderer capability blocker.
- Files changed:
  - `docs/DILIGENTFX_IMPORT_LEDGER.md`
- Verification: initial inventory only.
- Follow-up: revisit if physics/navigation/flow-field debug views need it.

## Components/DepthRangeCalculator

- Status: pending
- Upstream capability: compute depth range/min-max helpers.
- Karma capability: depth reduction utility for postprocess and culling.
- Engine owner: rendering
- Existing Karma coverage: none
- Decision: future work
- Reason: Depth reduction is useful for postprocess effects and clustered
  culling, but should be introduced with a concrete consumer.
- Files changed:
  - `docs/DILIGENTFX_IMPORT_LEDGER.md`
- Verification: initial inventory only.
- Follow-up: revisit with SSAO/SSR/TAA/depth-of-field or culling work.

## PostProcess/Common

- Status: integrated
- Upstream capability: shared postprocess context, frame resources, camera
  history, depth history, motion-vector utilities, blue-noise resources, and
  render-technique plumbing.
- Karma capability: renderer postprocess stack foundation.
- Engine owner: rendering
- Existing Karma coverage: partial
- Decision: integrate
- Reason: Karma now has a renderer-level `PostProcessSettings` contract,
  camera-selected post-process profiles in `AssetRegistry`, runtime startup wiring,
  Diligent fullscreen pass infrastructure, backend shader assets, ping/pong
  textures, and two history textures for temporal effects.
- Files changed:
  - `include/karma/content/assets/asset_registry.h`
  - `include/karma/rendering/renderer/post_process.h`
  - `include/karma/world/components/camera.h`
  - `include/karma/rendering/renderer/backend.hpp`
  - `include/karma/rendering/renderer/device.h`
  - `include/karma/runtime/app/engine_app.h`
  - `src/rendering/renderer/device.cpp`
  - `src/runtime/app/engine_app.cpp`
  - `src/rendering/renderer/backends/diligent/backend.hpp`
  - `src/rendering/renderer/backends/diligent/backend_render.cpp`
  - `src/rendering/renderer/backends/diligent/passes/post_process/`
  - `src/rendering/renderer/backends/diligent/shaders/post_process/`
  - `cmake/KarmaEngine.cmake`
  - `cmake/KarmaExamples.cmake`
  - `examples/diligentfx_postprocess_example.cpp`
  - `examples/README.md`
  - `docs/DILIGENTFX_IMPORT_LEDGER.md`
- Verification: inspected `PostProcess/Common/interface`,
  `PostProcess/Common/src`, and `Shaders/Common/private` postprocess utilities;
  built `rendering_postprocess`; launched the example under
  `timeout` without Diligent shader errors.
- Follow-up: add projection jitter and motion-vector inputs when the render
  graph grows a stable G-buffer path.

## PostProcess/Bloom

- Status: integrated
- Upstream capability: HDR bloom with threshold, soft threshold, intensity,
  radius, downsample/upsample chain, and Karis-average firefly reduction.
- Karma capability: bloom postprocess effect.
- Engine owner: rendering
- Existing Karma coverage: `rendering_bloom`
- Decision: integrate
- Reason: Karma now has opt-in bloom threshold, intensity, and radius settings
  backed by a Diligent prefilter/downsample/upsample pass chain.
- Files changed:
  - `include/karma/rendering/renderer/post_process.h`
  - `src/rendering/renderer/backends/diligent/passes/post_process/`
  - `examples/diligentfx_postprocess_example.cpp`
  - `examples/diligentfx_bloom_example.cpp`
  - `examples/assets/diligentfx_bloom/`
  - `docs/DILIGENTFX_IMPORT_LEDGER.md`
- Verification: inspected `PostProcess/Bloom/README.md`,
  `PostProcess/Bloom/interface/Bloom.hpp`, `PostProcess/Bloom/src/Bloom.cpp`,
  and `Shaders/PostProcess/Bloom`; built `rendering_postprocess`
  and `rendering_bloom`; launched both examples under `timeout`
  without Diligent shader errors.
- Follow-up: switch the scene buffer to HDR before matching DiligentFX bloom
  quality more closely.

## PostProcess/ScreenSpaceAmbientOcclusion

- Status: partially integrated
- Upstream capability: SSAO with depth prefiltering, temporal accumulation,
  spatial reconstruction, bilateral upsampling, and history resources.
- Karma capability: screen-space ambient occlusion.
- Engine owner: rendering
- Existing Karma coverage: none
- Decision: integrate
- Reason: Karma now has opt-in SSAO driven from scene depth and depth-derived
  normals. The full DiligentFX-style prefilter/reconstruct/bilateral/temporal
  path still needs explicit normal and motion-vector resources.
- Files changed:
  - `include/karma/rendering/renderer/post_process.h`
  - `src/rendering/renderer/backends/diligent/passes/post_process/`
  - `examples/diligentfx_postprocess_example.cpp`
  - `docs/DILIGENTFX_IMPORT_LEDGER.md`
- Verification: inspected README and shader/source inventory; built
  `rendering_postprocess`; launched the example under `timeout`
  without Diligent shader errors.
- Follow-up: upgrade to normal-buffer and bilateral-filtered SSAO when a full
  G-buffer exists.

## PostProcess/ScreenSpaceReflection

- Status: partially integrated
- Upstream capability: hierarchical-depth SSR with roughness/material inputs,
  optional previous-frame color, spatial reconstruction, temporal accumulation,
  bilateral cleanup, and half-resolution mode.
- Karma capability: full screen-space reflections.
- Engine owner: rendering
- Existing Karma coverage: partial
- Decision: integrate
- Reason: Karma now has opt-in fullscreen SSR using scene color, depth-derived
  normals, history-capable postprocess resources, and user-facing intensity /
  roughness / thickness controls. Hierarchical depth, material roughness, and
  motion-vector reconstruction remain future work.
- Files changed:
  - `include/karma/rendering/renderer/post_process.h`
  - `src/rendering/renderer/backends/diligent/passes/post_process/`
  - `examples/diligentfx_postprocess_example.cpp`
  - `docs/DILIGENTFX_IMPORT_LEDGER.md`
- Verification: inspected README and shader/source inventory; built
  `rendering_postprocess`; launched the example under `timeout`
  without Diligent shader errors.
- Follow-up: add Hi-Z tracing and material-aware resolve when normal/material
  buffers exist.

## PostProcess/TemporalAntiAliasing

- Status: partially integrated
- Upstream capability: TAA with projection jitter, history accumulation,
  variance clipping, optional Gaussian weighting, optional bicubic history
  sampling, and disocclusion handling.
- Karma capability: temporal anti-aliasing.
- Engine owner: rendering
- Existing Karma coverage: none
- Decision: integrate
- Reason: Karma now has opt-in temporal history textures, neighborhood-clamped
  history blending, and sharpening in the postprocess resolve. Projection
  jitter, velocity reprojection, and disocclusion rejection still need motion
  vectors.
- Files changed:
  - `include/karma/rendering/renderer/post_process.h`
  - `src/rendering/renderer/backends/diligent/backend.hpp`
  - `src/rendering/renderer/backends/diligent/passes/post_process/`
  - `examples/diligentfx_postprocess_example.cpp`
  - `docs/DILIGENTFX_IMPORT_LEDGER.md`
- Verification: inspected README and shader/source inventory; built
  `rendering_postprocess`; launched the example under `timeout`
  without Diligent shader errors.
- Follow-up: add camera jitter and motion vectors before making this the
  default antialiasing path.

## PostProcess/DepthOfField

- Status: partially integrated
- Upstream capability: depth-of-field effect using depth buffer, circle of
  confusion, dilation, bokeh passes, temporal CoC, and near/far blur handling.
- Karma capability: depth-of-field postprocess effect.
- Engine owner: rendering
- Existing Karma coverage: none
- Decision: integrate
- Reason: Karma now has opt-in depth-based circle-of-confusion blur controlled
  through `PostProcessSettings`. The full DiligentFX bokeh/dilation/temporal
  CoC pipeline remains future work.
- Files changed:
  - `include/karma/rendering/renderer/post_process.h`
  - `src/rendering/renderer/backends/diligent/passes/post_process/`
  - `examples/diligentfx_postprocess_example.cpp`
  - `docs/DILIGENTFX_IMPORT_LEDGER.md`
- Verification: inspected README and shader/source inventory; built
  `rendering_postprocess`; launched the example under `timeout`
  without Diligent shader errors.
- Follow-up: add separate near/far bokeh layers if cinematic DOF becomes a
  priority.

## PostProcess/EpipolarLightScattering

- Status: pending
- Upstream capability: epipolar light scattering, shadow-map ray marching,
  min/max shadow map hierarchy, atmosphere/scattering parameters, and optional
  debug views.
- Karma capability: atmospheric light shafts / volumetric sky scattering.
- Engine owner: features
- Existing Karma coverage: none
- Decision: future work
- Reason: This is a larger visual feature depending on directional light,
  shadow resources, postprocess stack, atmosphere parameters, and debug controls.
- Files changed:
  - `docs/DILIGENTFX_IMPORT_LEDGER.md`
- Verification: inspected README and shader/source inventory.
- Follow-up: revisit after postprocess stack and shadow quality work.

## Hydrogent

- Status: pending
- Upstream capability: Hydra render delegate, USD scene/material/light/texture
  integration, render buffers, render passes, and USD plugin build plumbing.
- Karma capability: USD content/runtime integration.
- Engine owner: content
- Existing Karma coverage: none
- Decision: future work
- Reason: USD/Hydra support is a large dependency and product direction. It
  should enter through Karma content/runtime contracts only if USD becomes a
  supported workflow.
- Files changed:
  - `docs/DILIGENTFX_IMPORT_LEDGER.md`
- Verification: inspected `Hydrogent/readme.md`, `Hydrogent/interface`,
  `Hydrogent/include`, `Hydrogent/src`, and `Hydrogent/shaders`.
- Follow-up: evaluate USD separately from renderer postprocess work.

## Radient

- Status: pending
- Upstream capability: high-level renderer abstraction, scene model, assets,
  views, backend interface, and scene import/write helpers.
- Karma capability: renderer architecture reference.
- Engine owner: rendering
- Existing Karma coverage: partial
- Decision: future work
- Reason: Karma already has a renderer/world/runtime architecture. Radient may
  be useful as a reference, but importing it directly would create a parallel
  engine architecture.
- Files changed:
  - `docs/DILIGENTFX_IMPORT_LEDGER.md`
- Verification: inspected `Radient/readme.md`, `Radient/interface`, and
  `Radient/src`.
- Follow-up: revisit only if Karma deliberately redesigns renderer abstraction.

## Utilities/DiligentFXShaderSourceStreamFactory

- Status: pending
- Upstream capability: DiligentFX shader source stream/include factory.
- Karma capability: shader include/source management.
- Engine owner: rendering
- Existing Karma coverage: partial
- Decision: improve existing
- Reason: Karma currently embeds shader strings and uses Diligent shader
  creation directly. A shader include/source system may become useful for
  postprocess effects, but should be designed as a Karma shader asset/helper
  path rather than importing DiligentFX utility shape.
- Files changed:
  - `docs/DILIGENTFX_IMPORT_LEDGER.md`
- Verification: inspected `Utilities/interface` and `Utilities/src`.
- Follow-up: revisit during postprocess stack work if shader include management
  becomes the limiting factor.
