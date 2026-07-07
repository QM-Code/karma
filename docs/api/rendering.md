# Rendering API Guide {#karma_rendering_guide}

The rendering API is split between gameplay-facing ECS data and lower-level
renderer services. Most game code should author components and shared resource
registries, then let karma::rendering::RenderSystem submit the frame.

## Camera-Resolved Frame Graphs

Renderer frame graphs are selected by camera, not by global backend state.

- karma::rendering::FrameGraphDesc stores graph resources and passes.
- karma::assets::AssetRegistry stores named frame graphs and shader passes.
- karma::components::CameraComponent::frame_graph_key selects a graph for one
  camera pass.
- An empty or missing graph key resolves to the default graph.
- karma::app::EngineConfig::default_frame_graph seeds the startup default graph.

The renderer resolves a graph immediately before each camera pass. Primary
cameras and render-to-texture cameras can therefore use different graph assets
in the same frame. Cameras only carry graph intent; backend pass objects,
render targets, bloom mip chains, shader assets, and temporal history resources
remain renderer-owned.

Use karma::app::GameInterface::assets or
karma::app::RuntimeModuleContext::assets to register graphs:

```cpp
karma::rendering::PostProcessSettings cinematic{};
cinematic.bloom_enabled = true;
cinematic.bloom_threshold = 0.7f;
cinematic.bloom_intensity = 0.35f;
cinematic.tone_mapping_enabled = true;
cinematic.tone_exposure = 1.1f;

assets->registerFrameGraph(
    "camera/cinematic",
    karma::rendering::frameGraphFromPostProcessSettings(cinematic, "camera/cinematic"));

world->add(camera, karma::components::CameraComponent{
    .is_primary = true,
    .frame_graph_key = "camera/cinematic",
});
```

## Render Submission Boundary

karma::rendering::GraphicsDevice::renderLayer requires resolved
karma::rendering::FrameGraphDesc. Normal applications do not call this
directly; karma::rendering::RenderSystem resolves camera graphs, submits
offscreen camera passes, then submits the primary camera pass.

There is no global `setPostProcessSettings` API. To change post-processing,
register or update a graph and assign that graph key to the intended
camera.

## Diligent Backend Assets

The Diligent backend loads built-in post-process HLSL from:

- `src/rendering/renderer/backends/diligent/shaders/post_process/` in source
  builds
- `share/karma/shaders/diligent/post_process/` in installed packages
- `KARMA_DILIGENT_SHADER_DIR` when an override is needed for local experiments

Minimal embedded fallback shaders are used only if required files cannot be
found. User cameras should not reference these backend shader assets directly.

## Shadows And Lights

Directional, point, and spot light authoring uses
karma::components::LightComponent. Directional and point lights cast shadows
only when `casts_shadows` is true and the active camera has
karma::components::CameraComponent::render_shadows enabled.

Directional shadow quality is controlled by karma::app::EngineConfig shadow
fields such as `shadow_map_size`, `shadow_bias`, and `shadow_pcf_radius`.
Point-light shadows additionally use `point_shadow_max_lights` and point-shadow
bias fields. See `docs/ENGINE_USAGE.md` for recommended baseline values.

The default `gameplay_tank` scene uses a shadow-casting directional sun over the
tank/world movement scene, plus local point lights.

## Source Layout

Renderer source is intentionally split by responsibility:

- public renderer API: `include/karma/rendering.h
