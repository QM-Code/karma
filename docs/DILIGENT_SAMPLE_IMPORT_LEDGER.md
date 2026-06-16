# Diligent Sample Import Ledger

Source checkout:
`/home/quinn/Documents/codex-projects/karma/fork1/DiligentSamples`

Process note: the import loop targets upstream `Tutorials/` and `Samples/`
capabilities first. Platform packaging trees such as `Android/` and
`UnityPlugin/` are not treated as primary capability sources unless a later pass
explicitly needs platform integration.

## Future Work Backlog

These items are intentionally deferred rather than forgotten. Keep future
sample-import passes focused on engine-level renderer APIs, not sample-shaped
ports.

- Hardware-gated ray tracing: `Tutorials/Tutorial21_RayTracing` and
  `Tutorials/Tutorial22_HybridRendering` require an RT-capable GPU for local
  development and useful validation. Do not prioritize these on the current
  workstation. Revisit only after renderer capability discovery exists and an
  RT-capable development or CI machine is available.
- Renderer architecture prerequisites: render graph/deferred G-buffer support,
  postprocess stack API, custom material pipeline variants, GPU query scopes,
  MSAA resolve support, and texture-array/bindless resource contracts.
- Platform/runtime feature decisions: multi-window, XR/VisionOS, Nuklear UI,
  and USD/Hydra are product/dependency decisions, not automatic Diligent sample
  imports.

## Tutorials/Tutorial00_HelloLinux

- Status: skipped
- Upstream capability: Linux platform bootstrap for Diligent plus a minimal
  triangle draw.
- Karma capability: platform window creation and renderer backend startup.
- Engine owner: platform
- Existing Karma coverage: complete
- Decision: skip
- Reason: Karma already routes Linux window/device creation through the platform
  and Diligent backend factories. The tutorial is platform bootstrap code, not a
  reusable renderer capability.
- Files changed:
  - `docs/DILIGENT_SAMPLE_IMPORT_LEDGER.md`
- Verification: inspected the tutorial README/source inventory and Karma's
  platform/backend factory layout.
- Follow-up: none.

## Tutorials/Tutorial00_HelloWin32

- Status: skipped
- Upstream capability: Win32 platform bootstrap for Diligent plus a minimal
  triangle draw.
- Karma capability: platform window creation and renderer backend startup.
- Engine owner: platform
- Existing Karma coverage: partial
- Decision: skip
- Reason: Karma keeps native window details behind `platform` backends and does
  not expose Win32 sample lifecycle code as engine API.
- Files changed:
  - `docs/DILIGENT_SAMPLE_IMPORT_LEDGER.md`
- Verification: inspected the tutorial README/source inventory and Karma's
  platform/backend factory layout.
- Follow-up: add a Win32 window backend only if the platform layer needs native
  Win32 support beyond GLFW/SDL.

## Tutorials/Tutorial01_HelloTriangle

- Status: skipped
- Upstream capability: minimal graphics pipeline creation, HLSL vertex/pixel
  shader creation, backbuffer/depth clear, and non-indexed triangle draw.
- Karma capability: baseline Diligent graphics pass support.
- Engine owner: rendering
- Existing Karma coverage: complete
- Decision: skip
- Reason: Karma already has Diligent shader creation, graphics PSO creation,
  render target/depth clearing, pipeline binding, resource binding, and draw
  submission in the renderer backend. Tutorial 01 is app bootstrap plus a
  procedural triangle and does not expose a reusable engine capability beyond
  the baseline renderer path.
- Files changed:
  - `docs/DILIGENT_SAMPLE_IMPORT_LEDGER.md`
- Verification: inspected the upstream tutorial README, CMake file, source, and
  header; checked Karma's Diligent backend for PSO/shader creation, clear,
  pipeline binding, shader resource binding, and draw submission coverage.
- Follow-up: continue with `Tutorials/Tutorial02_Cube`, which introduces
  file-loaded shaders, vertex/index buffers, uniform buffers, and camera-space
  transforms.

## Tutorials/Tutorial02_Cube

- Status: partially integrated
- Upstream capability: file-loaded shaders, immutable vertex/index buffers,
  dynamic uniform buffers, indexed drawing, and world-view-projection updates.
- Karma capability: mesh resources, material bindings, camera transforms, and
  per-draw constants.
- Engine owner: rendering
- Existing Karma coverage: partial
- Decision: skip
- Reason: Karma already uploads mesh vertex/index buffers, updates uniform data,
  and submits indexed draws through the Diligent forward path. The remaining gap
  is a general custom-material shader pipeline; the current public
  `MaterialDesc` shader-path fields are not enough by themselves because custom
  shader resource layouts need an engine-level design.
- Files changed:
  - `docs/DILIGENT_SAMPLE_IMPORT_LEDGER.md`
- Verification: inspected README, CMake, source, shader assets, and Karma mesh,
  material, device, and Diligent forward-pass code.
- Follow-up: design custom material pipeline support before honoring arbitrary
  material shader paths.

## Tutorials/Tutorial03_Texturing

- Status: skipped
- Upstream capability: texture loading, immutable sampler/resource binding, and
  textured material sampling.
- Karma capability: texture handles, texture uploads, imported material
  textures, and PBR texture bindings.
- Engine owner: rendering
- Existing Karma coverage: complete
- Decision: skip
- Reason: Karma already supports renderer texture handles, RGBA texture uploads,
  glTF material texture import, immutable sampler setup, and material SRB
  binding in the Diligent backend.
- Files changed:
  - `docs/DILIGENT_SAMPLE_IMPORT_LEDGER.md`
- Verification: inspected README, CMake, source, shader assets, and Karma
  texture/material resource code.
- Follow-up: none.

## Tutorials/Tutorial03_Texturing-C

- Status: skipped
- Upstream capability: C API version of the texturing tutorial.
- Karma capability: none.
- Engine owner: rendering
- Existing Karma coverage: complete
- Decision: skip
- Reason: Karma is a C++ engine API and does not maintain Diligent C API
  bindings or compatibility wrappers.
- Files changed:
  - `docs/DILIGENT_SAMPLE_IMPORT_LEDGER.md`
- Verification: inspected README, CMake, source, and shader assets.
- Follow-up: none.

## Tutorials/Tutorial03_Texturing-DotNet

- Status: skipped
- Upstream capability: .NET API version of the texturing tutorial.
- Karma capability: none.
- Engine owner: runtime
- Existing Karma coverage: none
- Decision: skip
- Reason: language binding support is outside the current engine layering and
  should not be imported as renderer API.
- Files changed:
  - `docs/DILIGENT_SAMPLE_IMPORT_LEDGER.md`
- Verification: inspected README, CMake, source, and shader assets.
- Follow-up: revisit only if Karma deliberately adds managed bindings.

## Tutorials/Tutorial04_Instancing

- Status: skipped
- Upstream capability: per-instance transform buffers and instanced indexed
  drawing.
- Karma capability: backend instancing for repeated mesh/material submissions.
- Engine owner: rendering
- Existing Karma coverage: complete
- Decision: skip
- Reason: Karma's Diligent forward path batches opaque submissions by
  mesh/material/submesh and writes instance transform buffers before issuing
  instanced draws.
- Files changed:
  - `docs/DILIGENT_SAMPLE_IMPORT_LEDGER.md`
- Verification: inspected README, CMake, source, shader assets, and Karma
  forward batching code.
- Follow-up: none.

## Tutorials/Tutorial05_TextureArray

- Status: partially integrated
- Upstream capability: 2D texture arrays indexed per instance.
- Karma capability: texture handles and per-instance rendering.
- Engine owner: rendering
- Existing Karma coverage: partial
- Decision: skip
- Reason: Karma has texture handles and instancing, but no public texture-array
  resource contract or material binding model for array slices. Adding only a
  Diligent texture-array helper would leak sample/backend structure.
- Files changed:
  - `docs/DILIGENT_SAMPLE_IMPORT_LEDGER.md`
- Verification: inspected README, CMake, source, shader assets, and Karma
  texture/material APIs.
- Follow-up: add texture-array support as a renderer texture contract when an
  engine feature needs indexed texture slices.

## Tutorials/Tutorial06_Multithreading

- Status: partially integrated
- Upstream capability: deferred contexts and command list recording from worker
  threads.
- Karma capability: renderer submission facade and backend frame rendering.
- Engine owner: rendering
- Existing Karma coverage: none
- Decision: skip
- Reason: Karma's renderer backend is currently immediate-context oriented.
  Importing deferred contexts would require a scheduler and resource lifetime
  policy, not a sample-shaped API.
- Files changed:
  - `docs/DILIGENT_SAMPLE_IMPORT_LEDGER.md`
- Verification: inspected README, CMake, source, shader assets, and current
  backend frame/render paths.
- Follow-up: design a render work graph or backend command recording layer
  before adding multithreaded rendering.

## Tutorials/Tutorial07_GeometryShader

- Status: partially integrated
- Upstream capability: geometry-shader smooth wireframe rendering.
- Karma capability: material-level debug/wire rendering.
- Engine owner: rendering
- Existing Karma coverage: partial
- Decision: skip
- Reason: Karma exposes a `wireframe` material flag but does not yet route
  material-specific pipeline variants, and smooth geometry-shader wireframe
  should be a renderer feature rather than copied sample shaders.
- Files changed:
  - `docs/DILIGENT_SAMPLE_IMPORT_LEDGER.md`
- Verification: inspected README, CMake, source, shader assets, and Karma
  material/pipeline code.
- Follow-up: implement material pipeline variants before adding wireframe or
  geometry-shader material families.

## Tutorials/Tutorial08_Tessellation

- Status: skipped
- Upstream capability: hull/domain tessellation for adaptive terrain, plus
  geometry-shader wireframe visualization.
- Karma capability: terrain/render material extensions.
- Engine owner: rendering
- Existing Karma coverage: none
- Decision: skip
- Reason: Karma has no tessellation-stage material model or terrain subsystem.
  Adding one directly from this tutorial would create a sample-specific
  renderer path.
- Files changed:
  - `docs/DILIGENT_SAMPLE_IMPORT_LEDGER.md`
- Verification: inspected README, CMake, source, shader assets, and current
  renderer pipeline construction.
- Follow-up: revisit with a terrain feature design.

## Tutorials/Tutorial09_Quads

- Status: partially integrated
- Upstream capability: 2D quad batching with frequent texture and blend-state
  changes.
- Karma capability: provider-neutral UI draw data and particle billboard draws.
- Engine owner: rendering
- Existing Karma coverage: partial
- Decision: skip
- Reason: Karma already has UI geometry submission and particle billboard
  batching. A generic sprite/quad renderer should be added as a visual feature
  module, not by copying tutorial batch code into the backend.
- Files changed:
  - `docs/DILIGENT_SAMPLE_IMPORT_LEDGER.md`
- Verification: inspected README, CMake, source, shader assets, UI renderer,
  and particle draw paths.
- Follow-up: add a `features/visual` sprite system if game code needs
  world/screen quad batching outside UI and particles.

## Tutorials/Tutorial10_DataStreaming

- Status: skipped
- Upstream capability: dynamic buffer streaming with discard/no-overwrite
  mapping.
- Karma capability: dynamic constant/instance/particle buffer updates.
- Engine owner: rendering
- Existing Karma coverage: complete
- Decision: skip
- Reason: Karma's Diligent backend already uses dynamic buffer mapping,
  `UpdateBuffer`, and structured buffer updates in the render and particle
  paths. There is no public raw-buffer API to extend from this tutorial.
- Files changed:
  - `docs/DILIGENT_SAMPLE_IMPORT_LEDGER.md`
- Verification: inspected README, CMake, source, shader assets, and Karma
  dynamic buffer usage.
- Follow-up: none.

## Tutorials/Tutorial11_ResourceUpdates

- Status: partially integrated
- Upstream capability: buffer and texture update strategies.
- Karma capability: mesh updates and RGBA8 texture updates.
- Engine owner: rendering
- Existing Karma coverage: partial
- Decision: skip
- Reason: Karma exposes safe mesh and RGBA8 texture updates, while broader raw
  buffer/texture update strategies remain backend internals. Importing the
  tutorial's API surface would expose low-level Diligent mechanics.
- Files changed:
  - `docs/DILIGENT_SAMPLE_IMPORT_LEDGER.md`
- Verification: inspected README, CMake, source, shader assets, and
  `GraphicsDevice::updateMesh` / `updateTextureRGBA8`.
- Follow-up: broaden texture upload formats only when a content pipeline needs
  them.

## Tutorials/Tutorial12_RenderTarget

- Status: partially integrated
- Upstream capability: offscreen render target rendering and sampling in a
  fullscreen postprocess pass.
- Karma capability: render targets, layer rendering into targets, and UI texture
  interop.
- Engine owner: rendering
- Existing Karma coverage: partial
- Decision: skip
- Reason: Karma already creates render targets and can render layers into them.
  The engine now has camera-resolved built-in post-process profiles, but still
  does not expose a general user-authored fullscreen material pass contract.
- Files changed:
  - `docs/DILIGENT_SAMPLE_IMPORT_LEDGER.md`
- Verification: inspected README, CMake, source, shader assets, and Karma render
  target/resource code.
- Follow-up: design a renderer fullscreen material/pass API only if gameplay
  code needs custom user-authored post effects.

## Tutorials/Tutorial13_ShadowMap

- Status: partially integrated
- Upstream capability: shadow-map rendering and comparison sampling.
- Karma capability: directional cascaded shadows and point shadow maps.
- Engine owner: rendering
- Existing Karma coverage: complete
- Decision: skip
- Reason: Karma already owns shadow resources, bias settings, shadow passes,
  and material shadow sampling in the Diligent backend.
- Files changed:
  - `docs/DILIGENT_SAMPLE_IMPORT_LEDGER.md`
- Verification: inspected README, CMake, source, shader assets, and Karma
  Diligent shadow passes.
- Follow-up: none.

## Tutorials/Tutorial14_ComputeShader

- Status: skipped
- Upstream capability: compute-shader particle simulation using UAV/SRV
  buffers.
- Karma capability: GPU particle simulation, sorting, culling, and renderer
  particle diagnostics.
- Engine owner: features
- Existing Karma coverage: complete
- Decision: skip
- Reason: Karma already has a renderer-backed particle system with compute
  simulation, structured buffers, indirect work, and particle stats. The
  tutorial's particle behavior is sample-specific.
- Files changed:
  - `docs/DILIGENT_SAMPLE_IMPORT_LEDGER.md`
- Verification: inspected README, CMake, source, compute shaders, and Karma
  particle feature/backend code.
- Follow-up: none.

## Tutorials/Tutorial15_MultipleWindows

- Status: skipped
- Upstream capability: multiple swapchains/windows sharing a Diligent device.
- Karma capability: runtime window and graphics-device composition.
- Engine owner: runtime
- Existing Karma coverage: none
- Decision: skip
- Reason: Karma currently models one runtime window/graphics device. Multi-window
  support needs runtime ownership and input-focus design, not tutorial
  lifecycle code.
- Files changed:
  - `docs/DILIGENT_SAMPLE_IMPORT_LEDGER.md`
- Verification: inspected README, CMake, source, and Karma runtime/window
  composition.
- Follow-up: design multi-window runtime support if editor tooling requires it.

## Tutorials/Tutorial16_BindlessResources

- Status: skipped
- Upstream capability: bindless texture/resource indexing for many materials.
- Karma capability: material texture binding and renderer resource tables.
- Engine owner: rendering
- Existing Karma coverage: none
- Decision: skip
- Reason: bindless resources would change material/resource binding strategy
  across the renderer. It should be imported as a renderer architecture change,
  not as a sample mode.
- Files changed:
  - `docs/DILIGENT_SAMPLE_IMPORT_LEDGER.md`
- Verification: inspected README, CMake, source, shader assets, and Karma
  material resource binding code.
- Follow-up: design bindless material/resource tables when renderer scale
  demands it.

## Tutorials/Tutorial17_MSAA

- Status: skipped
- Upstream capability: multisampled render targets and resolve into the
  swapchain.
- Karma capability: render target creation and present path.
- Engine owner: rendering
- Existing Karma coverage: none
- Decision: skip
- Reason: adding MSAA requires sample-count-aware PSO variants and resolve
  resources in the render target/present path. A public `samples` field alone
  would be incomplete.
- Files changed:
  - `docs/DILIGENT_SAMPLE_IMPORT_LEDGER.md`
- Verification: inspected README, CMake, source, shader assets, Diligent
  resolve API, and Karma render target/pipeline state code.
- Follow-up: add MSAA as a full renderer setting with PSO and resolve support.

## Tutorials/Tutorial18_Queries

- Status: partially integrated
- Upstream capability: GPU query objects for timing, duration, occlusion, and
  pipeline statistics.
- Karma capability: renderer command diagnostics.
- Engine owner: rendering
- Existing Karma coverage: partial
- Decision: improve existing
- Reason: Karma did not expose general renderer command counters. This pass adds
  `renderer::RendererCommandStats` and `GraphicsDevice::getRendererCommandStats`
  backed by Diligent context counters. It does not add occlusion/timestamp query
  objects yet because those need scoped engine instrumentation.
- Files changed:
  - `include/karma/rendering/renderer/stats.h`
  - `include/karma/rendering/renderer/backend.hpp`
  - `include/karma/rendering/renderer/device.h`
  - `src/rendering/renderer/device.cpp`
  - `src/rendering/renderer/backends/diligent/backend.hpp`
  - `src/rendering/renderer/backends/diligent/passes/render_state.cpp`
  - `docs/DILIGENT_SAMPLE_IMPORT_LEDGER.md`
- Verification: inspected README, CMake, source, shader assets, Diligent
  `DeviceContextStats`, and Karma renderer stats APIs; built
  `karma_rendering_graphical` and `karma_rendering_headless`.
- Follow-up: add scoped GPU timestamp/occlusion query APIs only when there is a
  concrete profiling or visibility-query use case.

## Tutorials/Tutorial19_RenderPasses

- Status: skipped
- Upstream capability: explicit render pass API with subpasses and simple
  deferred shading.
- Karma capability: forward rendering, render targets, and Forward+ lighting.
- Engine owner: rendering
- Existing Karma coverage: partial
- Decision: skip
- Reason: Karma does not currently expose a render-graph/deferred G-buffer
  architecture. Importing explicit render passes requires a renderer-level pass
  graph design.
- Files changed:
  - `docs/DILIGENT_SAMPLE_IMPORT_LEDGER.md`
- Verification: inspected README, CMake, source, shader assets, and Karma render
  target/forward pass code.
- Follow-up: design render graph or deferred renderer support before importing
  subpass/deferred shading behavior.

## Tutorials/Tutorial20_MeshShader

- Status: skipped
- Upstream capability: amplification and mesh shader pipeline rendering.
- Karma capability: mesh draw submission.
- Engine owner: rendering
- Existing Karma coverage: none
- Decision: skip
- Reason: Karma has no public mesh-shader capability flags, shader stages, or
  fallback strategy. A clean import requires a renderer feature capability model.
- Files changed:
  - `docs/DILIGENT_SAMPLE_IMPORT_LEDGER.md`
- Verification: inspected README, CMake, source, shader assets, and Karma
  pipeline construction.
- Follow-up: add renderer capability discovery before mesh-shader materials.

## Tutorials/Tutorial21_RayTracing

- Status: skipped
- Upstream capability: ray tracing pipeline, acceleration structures, shader
  binding table, and ray dispatch.
- Karma capability: none.
- Engine owner: rendering
- Existing Karma coverage: none
- Decision: skip
- Reason: ray tracing needs renderer capability discovery, scene acceleration
  structure ownership, material hit groups, and fallback behavior. It should not
  enter as tutorial-specific backend code.
- Files changed:
  - `docs/DILIGENT_SAMPLE_IMPORT_LEDGER.md`
- Verification: inspected README, CMake, source, ray tracing shaders, and Karma
  renderer APIs.
- Follow-up: future work only. Revisit after renderer capability discovery
  exists and an RT-capable development or CI GPU is available.

## Tutorials/Tutorial22_HybridRendering

- Status: skipped
- Upstream capability: rasterized G-buffer plus ray-traced reflections/shadows.
- Karma capability: forward renderer and screen-space scene sampling.
- Engine owner: rendering
- Existing Karma coverage: none
- Decision: skip
- Reason: hybrid rendering depends on the ray tracing and render-graph gaps
  already recorded for Tutorials 19 and 21.
- Files changed:
  - `docs/DILIGENT_SAMPLE_IMPORT_LEDGER.md`
- Verification: inspected README, CMake, source, shader assets, and Karma
  renderer APIs.
- Follow-up: future work only. Revisit after render graph/deferred rendering
  exists and an RT-capable development or CI GPU is available.

## Tutorials/Tutorial23_CommandQueues

- Status: skipped
- Upstream capability: graphics/copy/compute queue coordination and async work.
- Karma capability: immediate graphics/compute submission.
- Engine owner: rendering
- Existing Karma coverage: none
- Decision: skip
- Reason: Karma has no queue scheduler or explicit resource-state graph.
  Importing multiple queues requires backend scheduling architecture.
- Files changed:
  - `docs/DILIGENT_SAMPLE_IMPORT_LEDGER.md`
- Verification: inspected README, CMake, source, shader assets, and current
  frame/render paths.
- Follow-up: design a render work scheduler before async queue support.

## Tutorials/Tutorial24_VRS

- Status: skipped
- Upstream capability: variable rate shading via per-draw, per-primitive, and
  texture-based shading rates.
- Karma capability: none.
- Engine owner: rendering
- Existing Karma coverage: none
- Decision: skip
- Reason: Karma lacks renderer capability discovery and shading-rate state in
  pipeline/render target descriptions.
- Files changed:
  - `docs/DILIGENT_SAMPLE_IMPORT_LEDGER.md`
- Verification: inspected README, CMake, source, shader assets, and Karma
  renderer public contracts.
- Follow-up: add renderer capability discovery before VRS support.

## Tutorials/Tutorial25_StatePackager

- Status: partially integrated
- Upstream capability: offline render state packaging plus path tracing sample
  shaders.
- Karma capability: Diligent render state cache.
- Engine owner: rendering
- Existing Karma coverage: partial
- Decision: skip
- Reason: Karma already uses Diligent `RenderDeviceWithCache` and runtime shader
  cache controls. Offline state packaging is a build/tooling decision and the
  path tracing sample depends on ray tracing architecture that Karma does not
  have yet.
- Files changed:
  - `docs/DILIGENT_SAMPLE_IMPORT_LEDGER.md`
- Verification: inspected README, CMake, source, shader assets, and Karma
  Diligent render-state cache setup.
- Follow-up: revisit offline PSO packaging if shader compile latency remains a
  release-build problem.

## Tutorials/Tutorial26_StateCache

- Status: partially integrated
- Upstream capability: runtime render state cache and shader hot reload.
- Karma capability: Diligent shader/render-state cache.
- Engine owner: rendering
- Existing Karma coverage: partial
- Decision: skip
- Reason: Karma already creates, loads, saves, versions, and can flush a
  Diligent render-state cache through environment controls. Hot shader reload
  needs a custom-material/shader asset pipeline first.
- Files changed:
  - `docs/DILIGENT_SAMPLE_IMPORT_LEDGER.md`
- Verification: inspected README, CMake, source, shader assets, and Karma cache
  initialization code.
- Follow-up: connect shader hot reload to a future shader asset pipeline.

## Tutorials/Tutorial27_PostProcessing

- Status: skipped
- Upstream capability: DiligentFX post-processing stack, tone mapping, SSR,
  bloom, SSAO/TAA-style resources, and fullscreen passes.
- Karma capability: camera-resolved post-process profiles, backend-owned
  fullscreen passes, bloom, tone/color controls, SSAO/SSR/TAA/DOF settings, and
  scene color/depth/history resources.
- Engine owner: rendering
- Existing Karma coverage: partial
- Decision: improve existing
- Reason: Karma does not depend on DiligentFX, but now owns a native renderer
  post-process path. DiligentSamples remains reference material; user-facing
  control should stay in Karma's camera/profile API rather than copied sample
  classes.
- Files changed:
  - `include/karma/rendering/renderer/post_process.h`
  - `include/karma/rendering/renderer/post_process_profile_library.h`
  - `include/karma/world/components/camera.h`
  - `src/rendering/renderer/backends/diligent/passes/post_process/`
  - `src/rendering/renderer/backends/diligent/shaders/post_process/`
  - `docs/DILIGENT_SAMPLE_IMPORT_LEDGER.md`
- Verification: inspected README, CMake, source, shader assets, and Karma
  renderer/post-scene sampling paths; built the DiligentFX postprocess and bloom
  examples during the renderer pass.
- Follow-up: add motion-vector/G-buffer inputs before promoting TAA, SSR, SSAO,
  and DOF beyond the current composite-path implementations.

## Tutorials/Tutorial28_HelloOpenXR

- Status: skipped
- Upstream capability: OpenXR swapchains, per-eye render targets, and VR frame
  submission.
- Karma capability: runtime renderer composition.
- Engine owner: runtime
- Existing Karma coverage: none
- Decision: skip
- Reason: XR support crosses platform, runtime, input, and renderer ownership.
  It should not be imported as sample-specific render target code.
- Files changed:
  - `docs/DILIGENT_SAMPLE_IMPORT_LEDGER.md`
- Verification: inspected README, CMake, source, shader assets, and Karma
  runtime/platform layout.
- Follow-up: design XR as a runtime/platform feature.

## Tutorials/Tutorial29_OIT

- Status: skipped
- Upstream capability: order-independent transparency using layered and weighted
  blending methods.
- Karma capability: sorted transparent passes and particle transparency.
- Engine owner: rendering
- Existing Karma coverage: partial
- Decision: skip
- Reason: Karma currently uses sorted transparent draws and specialized particle
  passes. OIT needs explicit transparency mode selection, storage resources, and
  resolve/composite passes.
- Files changed:
  - `docs/DILIGENT_SAMPLE_IMPORT_LEDGER.md`
- Verification: inspected README, CMake, source, shader assets, and Karma
  transparent/particle render paths.
- Follow-up: design renderer transparency modes before adding OIT.

## Tutorials/Tutorial30_HelloVisionOS

- Status: skipped
- Upstream capability: Apple visionOS preview/immersive render targets, Metal
  swapchain images, multi-view rendering, and reverse-Z.
- Karma capability: none.
- Engine owner: platform
- Existing Karma coverage: none
- Decision: skip
- Reason: visionOS support is a platform/runtime feature, while Karma currently
  targets its existing window and Diligent Vulkan flow.
- Files changed:
  - `docs/DILIGENT_SAMPLE_IMPORT_LEDGER.md`
- Verification: inspected README, CMake, source, shader assets, and Karma
  platform/runtime layout.
- Follow-up: revisit as a platform target decision.

## Samples/Asteroids

- Status: partially integrated
- Upstream capability: high-volume asteroid benchmark with many meshes/textures,
  multiple native/Diligent backends, optional multithreaded rendering, bindless
  binding modes, skybox, sprite/font overlay, and DDS assets.
- Karma capability: instanced/batched mesh submission, environment skybox, UI
  rendering, texture uploads, and renderer command diagnostics.
- Engine owner: rendering
- Existing Karma coverage: partial
- Decision: skip
- Reason: the benchmark combines app-specific simulation, Win32/native D3D
  comparison code, bindless renderer modes, and custom shader compilation. Karma
  already covers the reusable baseline pieces and the remaining gaps are
  renderer architecture work, not sample code to port.
- Files changed:
  - `docs/DILIGENT_SAMPLE_IMPORT_LEDGER.md`
- Verification: inspected README, CMake, source, shader assets, and Karma
  instancing/environment/UI/diagnostics surfaces.
- Follow-up: use this sample as a stress target after bindless resources,
  multithreaded command recording, and DDS/texture-array content support are
  designed.

## Samples/Atmosphere

- Status: skipped
- Upstream capability: DiligentFX Epipolar Light Scattering atmosphere effect,
  terrain rendering, render state notation, and atmosphere postprocess assets.
- Karma capability: visual volumes and environment lighting.
- Engine owner: features
- Existing Karma coverage: partial
- Decision: skip
- Reason: Karma does not currently depend on DiligentFX or expose a postprocess
  graph. Physically based atmosphere should be a `features/visual` module backed
  by renderer postprocess support, not a copied DiligentFX sample.
- Files changed:
  - `docs/DILIGENT_SAMPLE_IMPORT_LEDGER.md`
- Verification: inspected README, CMake, source, asset inventory, and Karma
  visual feature/rendering layout.
- Follow-up: design atmosphere/sky as a visual feature after the renderer has a
  postprocess pass API.

## Samples/GLFWDemo

- Status: skipped
- Upstream capability: GLFW window/input integration plus procedural SDF maze
  generation and ray-marched 2D lighting.
- Karma capability: platform windows, input, and optional visual features.
- Engine owner: platform
- Existing Karma coverage: partial
- Decision: skip
- Reason: Karma already has GLFW/SDL platform backends. The SDF maze is game
  logic and custom shader content; importing it directly would hardcode a demo
  into the engine.
- Files changed:
  - `docs/DILIGENT_SAMPLE_IMPORT_LEDGER.md`
- Verification: inspected README, CMake, source, shader assets, and Karma
  platform/input/rendering layout.
- Follow-up: add SDF/ray-marched 2D lighting only as a reusable visual feature
  if a game/editor workflow needs it.

## Samples/GLTFViewer

- Status: partially integrated
- Upstream capability: Diligent AssetLoader and DiligentFX GLTF PBR renderer,
  material texture binding modes, environment maps, model browser, and
  post-effect application.
- Karma capability: GLB/glTF import, PBR-ish material bindings, environment
  maps, lights, skinning, and scene examples.
- Engine owner: content
- Existing Karma coverage: partial
- Decision: skip
- Reason: Karma already owns a native content importer and renderer material
  path. DiligentFX's GLTF viewer is a standalone viewer stack; importing it
  wholesale would bypass Karma's ECS/content architecture.
- Files changed:
  - `docs/DILIGENT_SAMPLE_IMPORT_LEDGER.md`
- Verification: inspected README, CMake, source, shader assets, and Karma
  content importer/material/environment code.
- Follow-up: compare specific GLTF material extensions against Karma's importer
  after the current animation/skinning work settles.

## Samples/ImguiDemo

- Status: skipped
- Upstream capability: Dear ImGui integration.
- Karma capability: ImGui UI provider bridge.
- Engine owner: features
- Existing Karma coverage: complete
- Decision: skip
- Reason: Karma already has `features/ui/imgui` exposing a factory that returns
  `runtime/app/UiLayer`, matching the local architecture rule for UI providers.
- Files changed:
  - `docs/DILIGENT_SAMPLE_IMPORT_LEDGER.md`
- Verification: inspected README, CMake, source inventory, and Karma ImGui
  feature adapter.
- Follow-up: none.

## Samples/NuklearDemo

- Status: skipped
- Upstream capability: Nuklear UI rendering and Win32 input glue.
- Karma capability: provider-neutral UI layer bridge.
- Engine owner: features
- Existing Karma coverage: none
- Decision: skip
- Reason: Karma has no Nuklear dependency or provider adapter. Adding one would
  belong under `features/ui/nuklear` behind a `UiLayer` factory, but that is a
  product/dependency decision rather than a renderer capability import.
- Files changed:
  - `docs/DILIGENT_SAMPLE_IMPORT_LEDGER.md`
- Verification: inspected README, CMake, source inventory, and Karma UI provider
  architecture.
- Follow-up: add a Nuklear provider only if the project chooses Nuklear as a
  supported UI backend.

## Samples/Shadows

- Status: partially integrated
- Upstream capability: DiligentFX shadowing component with high-quality shadow
  settings and render state notation assets.
- Karma capability: directional cascaded shadows, point shadows, bias settings,
  and shadow diagnostics.
- Engine owner: rendering
- Existing Karma coverage: partial
- Decision: skip
- Reason: Karma already has native shadow-map support in the Diligent backend.
  The DiligentFX component is a separate stack and should not replace the
  engine's existing shadow ownership without a deliberate renderer migration.
- Files changed:
  - `docs/DILIGENT_SAMPLE_IMPORT_LEDGER.md`
- Verification: inspected README, CMake, source, shader/render-state assets, and
  Karma Diligent shadow passes.
- Follow-up: compare specific DiligentFX shadow filtering options if current
  shadow quality becomes a blocker.

## Samples/USDViewer

- Status: skipped
- Upstream capability: USD/Hydra rendering through Hydrogent, USD scene editing,
  material view modes, postprocess controls, and USD asset/plugin plumbing.
- Karma capability: content importers and runtime/editor tooling.
- Engine owner: content
- Existing Karma coverage: none
- Decision: skip
- Reason: USD support is a content/runtime integration project with large
  dependencies. It should enter Karma through `content` import/runtime contracts,
  not as a DiligentFX viewer graft.
- Files changed:
  - `docs/DILIGENT_SAMPLE_IMPORT_LEDGER.md`
- Verification: inspected README, CMake, source inventory, and Karma content
  importer/runtime architecture.
- Follow-up: evaluate USD as a separate content pipeline decision.
