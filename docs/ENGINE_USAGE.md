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
  -DKARMA_WITH_IMGUI=ON \
  -DKARMA_WITH_RMLUI=ON \
  -DKARMA_FETCH_DEPS=OFF
```

### UI Backends
By default, UI backends are **disabled**. Enable one or both:
- `KARMA_WITH_IMGUI=ON`
- `KARMA_WITH_RMLUI=ON`

If `KARMA_FETCH_DEPS=OFF`, you must provide the libraries:

```bash
cmake -B build \
  -DKARMA_WITH_RMLUI=ON \
  -DRmlUi_DIR=/path/to/RmlUi/cmake
```

## Basic App Structure
```cpp
class MyGame : public karma::app::GameInterface {
public:
  void onStart() override { /* create entities */ }
  void onUpdate(float dt) override { /* per-frame logic */ }
  void onFixedUpdate(float dt) override { /* fixed timestep */ }
};

int main() {
  karma::app::EngineApp engine;
  MyGame game;

  // Optional UI overlay
  auto overlay = std::make_unique<karma::ui::RmlUiOverlay>();
  overlay->setDocumentFile("ui/main.rml");
  engine.setOverlay(std::move(overlay));

  karma::app::EngineConfig config;
  config.window.title = "My Game";
  config.shadow_map_size = 2048;

  engine.start(game, config);
  while (engine.isRunning()) {
    engine.tick();
  }
}
```

## Overlays
Karma uses a simple overlay interface. You can:
- Use built-in ImGui/RmlUi backends (if enabled).
- Provide your own overlay by inheriting `karma::ui::Overlay`.

### RmlUi Overlay
```cpp
auto overlay = std::make_unique<karma::ui::RmlUiOverlay>();
overlay->setDocumentFile("examples/assets/rmlui/overlay.rml");
engine.setOverlay(std::move(overlay));
```

Hot reload:
```cpp
overlay->setHotReloadEnabled(true);
overlay->setHotReloadInterval(0.5);
```

### ImGui Overlay
```cpp
auto overlay = std::make_unique<karma::ui::ImGuiOverlay>();
overlay->setDrawCallback([](){ ImGui::ShowDemoWindow(); });
engine.setOverlay(std::move(overlay));
```

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
- `karma_imgui_demo` (requires `KARMA_WITH_IMGUI`)
- `karma_rmlui_demo` (requires `KARMA_WITH_RMLUI`)

## Notes
- UI assets (RML/RCSS/images) are resolved relative to the current working directory unless `setAssetRoot()` is set.
- RmlUi clip masks require a stencil-capable swapchain (enabled by default in this repo).

