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
  config.shadow_map_size = 2048;

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
- Optional anisotropy + mip generation

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
