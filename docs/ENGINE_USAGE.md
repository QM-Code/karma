# Karma Engine — Usage Guide

## Quick Start
Build and run the default sample:

```bash
./setup.sh
cmake --build build
BZ3_DATA_DIR="$PWD/data" ./build/karma_example
```

## Build Options
Common toggles:

```bash
cmake -B build \
  -DKARMA_FETCH_DEPS=OFF
```

Optional demo:

```bash
cmake -B build \
  -DKARMA_BUILD_IMGUI_DEMO=ON
```

RmlUi demo:

```bash
cmake -B build \
  -DKARMA_BUILD_RMLUI_DEMO=ON
```

Headless build (no window or renderer backends, graphics demos disabled):

```bash
cmake -B build \
  -DKARMA_HEADLESS=ON
cmake --build build --target karma_network_demo
```

## Basic App Structure
```cpp
class MyGame : public karma::app::GameInterface {
public:
  void onStart() override { /* create entities */ }
  void onUpdate(float dt) override { /* per-frame logic */ }
  void onFixedUpdate(float dt) override { /* fixed timestep */ }
};

class MyUi : public karma::app::UiLayer {
 public:
  void onFrame(karma::app::UIContext& ctx) override { /* fill ctx.drawData() */ }
};

int main() {
  karma::app::EngineApp engine;
  MyGame game;

  // Optional UI layer
  auto ui = std::make_unique<MyUi>();
  engine.setUi(std::move(ui));

  karma::app::EngineConfig config;
  config.window.title = "My Game";
  config.forward_plus_tile_size = 16;
  config.forward_plus_max_lights_per_tile = 128;
  config.shadow_map_size = 2048;
  config.shadow_bias = 0.0006f;
  config.shadow_pcf_radius = 1;
  config.shadow_raster_depth_bias = 0;
  config.shadow_raster_slope_bias = 0.0f;
  config.shadow_receiver_bias_scale = 0.75f;
  config.shadow_normal_bias_scale = 1.0f;
  config.point_shadow_constant_bias = 0.0012f;
  config.point_shadow_slope_bias_scale = 2.0f;
  config.point_shadow_normal_bias_scale = 1.5f;
  config.point_shadow_receiver_bias_scale = 0.35f;
  config.local_light_distance_damping = 0.08f;
  config.local_light_range_falloff_exponent = 1.1f;
  config.ao_affects_local_lights = false;
  config.local_light_directional_shadow_lift_strength = 0.85f;
  config.lighting_exposure = 1.1f;

  engine.start(game, config);
  while (engine.isRunning()) {
    engine.tick();
  }
}
```

## UI draw data
Karma's UI integration is backend-agnostic. You provide a `UiLayer` implementation
that owns its UI system and submits draw data into `UIContext` each frame:

- `onEvent(...)` for input
- `onFrame(...)` for timing + draw list submission
- `UIContext::createTextureRGBA8(...)` for UI textures

The engine renders your UI draw lists on top of the 3D frame.

## Rendering Features
- Directional light with shadows (PCF supported)
- Cascaded shadow maps (CSM)
- Point and spot lights via Forward+ tiled local lights (GPU light culling per screen tile)
- Point-light shadows for `LightComponent::Type::Point` lights with `casts_shadows = true`
- Optional anisotropy + mip generation

## Shadow Defaults
Recommended baseline values for the current directional-shadow pipeline:

- `shadow_map_size = 2048`
- `shadow_pcf_radius = 1`
- `shadow_bias = 0.0006f`
- `shadow_receiver_bias_scale = 0.75f`
- `shadow_normal_bias_scale = 1.0f`
- `shadow_raster_depth_bias = 0`
- `shadow_raster_slope_bias = 0.0f`

Current CSM setup:
- 4 cascades (texture-array based)
- Stabilized light-space snapping to reduce shimmer
- Cascade transition blending near split boundaries

Point-light shadow setup:
- Up to 2 shadow-casting point lights are rendered each frame (nearest to camera).
- Each selected point light renders 6 faces into a depth texture array.
- Point shadow map resolution defaults to half of `shadow_map_size` (min 256).
- Local lights use inverse-square attenuation with a smooth range cutoff.
- Local lights can optionally lift directional-shadow darkness via `local_light_directional_shadow_lift_strength`.
- Point/local-light tuning is exposed in Debug UI for bias, attenuation, AO interaction, shadow lift, and exposure.

## Data Path
Assets and configs are typically loaded from the `data/` directory.
Use `BZ3_DATA_DIR` at runtime when needed:

```bash
BZ3_DATA_DIR="$PWD/data" ./build/karma_example
```

## Demos
- `karma_example` (default scene)
- `karma_imgui_ui_demo` (ImGui draw data bridge)
- `karma_rmlui_ui_demo` (RmlUi draw data bridge)
