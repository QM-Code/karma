# Rendering API Guide {#karma_rendering_guide}

The rendering API is split between gameplay-facing ECS data and lower-level
renderer services. Most game code should author components and shared resource
registries, then let karma::renderer::RenderSystem submit the frame.

## Camera-Resolved Post Processing

Post processing is selected by camera, not by global backend state.

- karma::renderer::PostProcessSettings stores the tunable effect values.
- karma::content::AssetRegistry stores named settings profiles.
- karma::components::CameraComponent::post_process_profile_key selects a
  profile for one camera pass.
- An empty or missing profile key resolves to the default profile.
- karma::app::EngineConfig::post_process seeds the startup default profile.

The renderer resolves a profile immediately before each camera pass. Primary
cameras and render-to-texture cameras can therefore use different post-process
profiles in the same frame. Cameras only carry profile intent; backend pass
objects, render targets, bloom mip chains, shader assets, and temporal history
resources remain renderer-owned.

Use karma::app::GameInterface::assets or
karma::app::RuntimeModuleContext::assets to register profiles:

```cpp
karma::renderer::PostProcessSettings cinematic{};
cinematic.bloom_enabled = true;
cinematic.bloom_threshold = 0.7f;
cinematic.bloom_intensity = 0.35f;
cinematic.tone_mapping_enabled = true;
cinematic.tone_exposure = 1.1f;

assets->registerPostProcessProfile("camera/cinematic", cinematic);

world->add(camera, karma::components::CameraComponent{
    .is_primary = true,
    .post_process_profile_key = "camera/cinematic",
});
```

## Render Submission Boundary

karma::renderer::GraphicsDevice::renderLayer requires resolved
karma::renderer::PostProcessSettings. Normal applications do not call this
directly; karma::renderer::RenderSystem resolves camera profiles, submits
offscreen camera passes, then submits the primary camera pass.

There is no global `setPostProcessSettings` API. To change post-processing,
register or update a profile and assign that profile key to the intended
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

- public renderer API: `include/karma/rendering/renderer/`
- renderer extraction helpers:
  `src/rendering/renderer/render_system/`
- Diligent backend passes:
  `src/rendering/renderer/backends/diligent/passes/`
- Diligent post-process pass pieces:
  `src/rendering/renderer/backends/diligent/passes/post_process/`

Backend implementation files are private. Keep new gameplay-facing renderer
contracts under the public `karma/rendering/...` include hierarchy, and keep
backend pass details inside the owning backend directory.
