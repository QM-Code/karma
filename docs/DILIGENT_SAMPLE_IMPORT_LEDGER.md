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
  The engine now has camera-resolved renderer frame graphs, but still does not
  expose a complete user-authored fullscreen material pass contract.
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
  `rendering::RendererCommandStats` and `GraphicsDevice::getRendererCommandStats`
  backed by Diligent context counters. It does not add occlusion/timestamp query
  objects yet because those need scoped engine instrumentation.
- Files changed:
  - `include/karma/rendering.h
