#include "demo_asset_paths.h"
#include "karma/karma.h"

#include <string>
#include <utility>

namespace karma::demo {

class NativeUiGame final : public app::GameInterface {
 public:
  void onStart() override {
    if (ui == nullptr) return;
    auto opened = ui->openFileController(
        "main_menu.kui.json5", {.layer = 100, .modal = true});
    if (!opened) {
      opened =
          ui->openController("ui/demo/main_menu", {.layer = 100, .modal = true});
    }
    if (!opened) return;
    menu_ = std::move(opened.controller);

    menu_.set("title", "Karma Native UI");
    menu_.set("status", "Ready");
    menu_.set("settings.volume", 0.75);
    menu_.set("settings.fullscreen", false);
    menu_.set("saves",
              ui::Value::Array{
                  ui::Value::Object{{"id", 1}, {"name", "Sky Harbor"}},
                  ui::Value::Object{{"id", 2}, {"name", "Constraint Lab"}},
                  ui::Value::Object{{"id", 3}, {"name", "Tank Arena"}},
                  ui::Value::Object{{"id", 4}, {"name", "Neon Foundry"}},
                  ui::Value::Object{{"id", 5}, {"name", "Cloud Ruins"}},
              });

    menu_.bindActions({
        {"play", [this](const ui::ActionEvent&) {
           menu_.set("status", "Native action callback received");
         }},
        {"volume-changed", [this](const ui::ActionEvent& event) {
           menu_.set("status", "Volume: " + event.value.toString());
         }},
    });
  }

  void onFixedUpdate(float) override {}
  void onUpdate(float) override {}

  void onShutdown() override { menu_.close(); }

 private:
  ui::DocumentController menu_;
};

}  // namespace karma::demo

int main() {
  karma::app::EngineApp engine;
  karma::demo::NativeUiGame game;
  karma::app::EngineConfig config;
  config.window.title = "Karma Native UI";
  config.window.width = 1280;
  config.window.height = 720;
  config.window.samples = 1;
  config.cursor_visible = true;
  config.loading_splash.enabled = false;
  const auto ui_assets =
      karma::demo::resolveExamplePath("examples/assets/ui/native_menu");
  config.native_ui.development_files.roots.push_back(ui_assets);
  config.startup_asset_packages.push_back(ui_assets);
  engine.start(game, config);
  while (engine.isRunning()) engine.tick();
  return 0;
}
