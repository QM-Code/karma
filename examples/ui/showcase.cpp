#include "demo_asset_paths.h"
#include "karma/karma.h"

#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace karma::demo {
namespace {

class ShowcaseLocalization final : public ui::LocalizationProvider {
 public:
  std::optional<std::string> localize(
      std::string_view locale,
      std::string_view key,
      const ui::Value::Object& arguments) override {
    const bool arabic = locale == "ar";
    if (key == "showcase.title") {
      return arabic ? "معرض واجهة كارما" : "Karma UI Forge";
    }
    if (key == "showcase.subtitle") {
      return arabic ? "واجهة ألعاب أصلية مع إعادة تحميل فورية"
                    : "Retained game UI, live-authored";
    }
    if (key == "showcase.greeting") {
      std::string name = "Adventurer";
      if (const auto found = arguments.find("name"); found != arguments.end()) {
        name = found->second.toString();
      }
      return arabic ? "مرحباً " + name : "Welcome, " + name;
    }
    return std::nullopt;
  }
};

rendering::TextureUploadData checkerUpload(int phase) {
  constexpr int kWidth = 64;
  constexpr int kHeight = 64;
  rendering::TextureUploadData upload;
  upload.format = rendering::TextureFormat::RGBA8;
  upload.bytes.resize(static_cast<std::size_t>(kWidth * kHeight * 4));
  for (int y = 0; y < kHeight; ++y) {
    for (int x = 0; x < kWidth; ++x) {
      const bool bright = ((x / 8 + y / 8 + phase) & 1) == 0;
      const std::size_t pixel =
          static_cast<std::size_t>((y * kWidth + x) * 4);
      upload.bytes[pixel + 0] = bright ? 211u : 56u;
      upload.bytes[pixel + 1] = bright ? 166u : 43u;
      upload.bytes[pixel + 2] = bright ? 76u : 29u;
      upload.bytes[pixel + 3] = 255u;
    }
  }
  upload.subresources.push_back({.mip_level = 0u,
                                 .array_layer = 0u,
                                 .width = kWidth,
                                 .height = kHeight,
                                 .offset = 0u,
                                 .size = upload.bytes.size(),
                                 .row_stride = kWidth * 4u});
  return upload;
}

ui::Value::Array makeRows(int count) {
  ui::Value::Array rows;
  rows.reserve(static_cast<std::size_t>(count));
  for (int index = 0; index < count; ++index) {
    rows.emplace_back(ui::Value::Object{
        {"id", index},
        {"name", "Retained inventory row " + std::to_string(index + 1)},
    });
  }
  return rows;
}

}  // namespace

class UiShowcaseGame final : public app::GameInterface {
 public:
  explicit UiShowcaseGame(app::EngineApp& engine) : engine_(engine) {}

  void onStart() override {
    if (ui == nullptr) return;

    ui->setLocalizationProvider(&localization_);
    ui->setLocale("en");
    auto opened = ui->openFileController(
        "showcase.kui.json5", {.layer = 200, .modal = true});
    if (!opened) {
      for (const ui::Diagnostic& diagnostic : opened.diagnostics) {
        std::cerr << diagnostic.asset_key << ':' << diagnostic.line << ':'
                  << diagnostic.column << ' ' << diagnostic.code << ' '
                  << diagnostic.message << '\n';
      }
      engine_.requestStop();
      return;
    }
    showcase_ = std::move(opened.controller);

    showcase_.setMany({
        {"profile.name", "Rowan"},
        {"status", "Ready — edit either JSON5 file to hot reload"},
        {"active_tab", "controls"},
        {"settings.volume", 0.68},
        {"settings.gamma", 0.42},
        {"settings.vsync", true},
        {"settings.quality", "high"},
        {"show_advanced", true},
        {"advanced_open", true},
        {"tree_selection", "weapons"},
        {"tree_expanded", true},
        {"locale_rtl", false},
        {"progress", 0.0},
        {"popup_open", false},
        {"menu_open", false},
        {"rows", makeRows(250)},
        {"quests",
         ui::Value::Array{
             ui::Value::Object{{"id", 1}, {"name", "Repair the eastern gate"}},
             ui::Value::Object{{"id", 2}, {"name", "Recover the brass seal"}},
             ui::Value::Object{{"id", 3}, {"name", "Map the old catacombs"}},
             ui::Value::Object{{"id", 4}, {"name", "Escort the archivist"}},
             ui::Value::Object{{"id", 5}, {"name", "Light the watch beacons"}},
             ui::Value::Object{{"id", 6}, {"name", "Test the arena winch"}},
             ui::Value::Object{{"id", 7}, {"name", "Tune the forge bellows"}},
             ui::Value::Object{{"id", 8}, {"name", "Inventory the armory"}},
             ui::Value::Object{{"id", 9}, {"name", "Rehang the western portcullis"}},
             ui::Value::Object{{"id", 10}, {"name", "Deliver the wardstone ledger"}},
             ui::Value::Object{{"id", 11}, {"name", "Inspect the river watchtower"}},
             ui::Value::Object{{"id", 12}, {"name", "Sharpen the ranger's blades"}},
         }},
        {"windows.inspector", inspectorWindowState()},
    });

    showcase_.bindActions({
        {"quit", [this](const ui::ActionEvent&) { engine_.requestStop(); }},
        {"locale-changed",
         [this](const ui::ActionEvent& event) {
           const bool rtl = event.value.truthy();
           ui->setLocale(rtl ? "ar" : "en");
           if (const ui::ElementHandle root =
                   showcase_.findById("showcase-root")) {
             if (rtl) ui->addClass(root, "rtl");
             else ui->removeClass(root, "rtl");
           }
           setStatus(rtl ? "Arabic shaping and RTL text enabled"
                         : "English locale restored");
         }},
        {"control-change", [this](const ui::ActionEvent& event) {
           setStatus(event.action + ": " + event.value.toString());
         }},
        {"advanced-changed", [this](const ui::ActionEvent& event) {
           setStatus(event.value.truthy() ? "Advanced subtree mounted"
                                          : "Advanced subtree removed by when");
         }},
        {"tab-changed", [this](const ui::ActionEvent& event) {
           setStatus("Tab selected: " + event.value.toString());
         }},
        {"tree-changed", [this](const ui::ActionEvent& event) {
           setStatus("Tree selection: " + event.value.toString());
         }},
        {"row-click", [this](const ui::ActionEvent&) {
           setStatus("Virtualized row action dispatched");
         }},
        {"quest-click", [this](const ui::ActionEvent&) {
           setStatus("Keyed repeat action dispatched");
         }},
        {"menu-save", [this](const ui::ActionEvent&) {
           setStatus("Menu action: Save layout");
         }},
        {"menu-reset", [this](const ui::ActionEvent&) {
           resetWindow();
           setStatus("Window state reset through its model binding");
         }},
        {"popup-cancel", [this](const ui::ActionEvent&) {
           setStatus("Popup dismissed and focus restored");
         }},
        {"menu-cancel", [this](const ui::ActionEvent&) {
           setStatus("Menu dismissed and focus restored");
         }},
        {"reopen-window", [this](const ui::ActionEvent&) {
           showcase_.set("windows.inspector.open", true);
           const ui::ElementHandle window =
               showcase_.findById("inspector-window");
           if (window) ui->bringToFront(window);
           setStatus("Inspector window reopened and raised");
         }},
        {"window-close", [this](const ui::ActionEvent&) {
           setStatus("Inspector window closed; use Reopen Window");
         }},
        {"window-toggle", [this](const ui::ActionEvent& event) {
           setStatus(event.value.truthy() ? "Inspector window collapsed"
                                          : "Inspector window expanded");
         }},
        {"scroll-end", [this](const ui::ActionEvent&) {
           const ui::ElementHandle list = showcase_.findById("virtual-list");
           if (list) showcase_.scrollTo(list, 0.0f, 100000.0f);
           setStatus("Virtual list scrolled to its retained extent");
         }},
        {"scroll-start", [this](const ui::ActionEvent&) {
           const ui::ElementHandle list = showcase_.findById("virtual-list");
           if (list) showcase_.scrollTo(list, 0.0f, 0.0f);
           setStatus("Virtual list returned to the first keyed row");
         }},
    });

    createDynamicPreview();
  }

  void onFixedUpdate(float) override {}

  void onUpdate(float delta_seconds) override {
    if (!showcase_) return;
    ensureDynamicPreview();
    elapsed_ += delta_seconds;
    progress_update_ += delta_seconds;
    image_update_ += delta_seconds;
    if (progress_update_ >= 0.05f) {
      progress_update_ = 0.0f;
      const double progress =
          (std::sin(static_cast<double>(elapsed_) * 0.9) + 1.0) * 0.5;
      showcase_.set("progress", progress);
    }
    if (dynamic_image_ && image_update_ >= 0.4f) {
      image_update_ = 0.0f;
      ++checker_phase_;
      ui->updateImage(dynamic_image_, checkerUpload(checker_phase_));
    }
  }

  void onShutdown() override {
    showcase_.close();
    if (ui != nullptr && dynamic_image_) {
      ui->destroyImage(dynamic_image_);
      dynamic_image_ = {};
    }
  }

 private:
  static ui::Value inspectorWindowState() {
    return ui::Value::Object{
        {"open", true},
        {"collapsed", false},
        {"position", ui::Value::Array{1140, 560}},
        {"size", ui::Value::Array{400, 280}},
        {"z", 60},
    };
  }

  void setStatus(std::string text) {
    showcase_.set("status", std::move(text));
  }

  void resetWindow() {
    showcase_.set("windows.inspector", inspectorWindowState());
  }

  void createDynamicPreview() {
    const rendering::TextureDesc desc{
        .width = 64,
        .height = 64,
        .format = rendering::TextureFormat::RGBA8,
        .srgb = true,
        .generate_mips = false,
        .mip_levels = 1u,
    };
    dynamic_image_ = ui->createImage(desc, checkerUpload(0));
    ensureDynamicPreview();
  }

  void ensureDynamicPreview() {
    if (!dynamic_image_) return;
    const ui::ElementHandle preview = showcase_.findById("dynamic-preview");
    if (preview && preview != dynamic_preview_) {
      ui->setImage(preview, ui::ImageSource::dynamic(dynamic_image_));
      dynamic_preview_ = preview;
    }
  }

  app::EngineApp& engine_;
  ShowcaseLocalization localization_;
  ui::DocumentController showcase_;
  ui::DynamicImageHandle dynamic_image_{};
  ui::ElementHandle dynamic_preview_{};
  float elapsed_ = 0.0f;
  float progress_update_ = 0.0f;
  float image_update_ = 0.0f;
  int checker_phase_ = 0;
};

}  // namespace karma::demo

int main() {
  karma::app::EngineApp engine;
  karma::demo::UiShowcaseGame game(engine);
  karma::app::EngineConfig config;
  config.window.title = "Karma Native UI Forge";
  config.window.width = 1600;
  config.window.height = 900;
  config.window.samples = 1;
  config.cursor_visible = true;
  config.loading_splash.enabled = false;
  config.native_ui.hot_reload = true;
  config.native_ui.development_files.enabled = true;
  config.native_ui.source_poll_interval = std::chrono::milliseconds(100);
  const auto assets =
      karma::demo::resolveExamplePath("examples/assets/ui/showcase");
  config.native_ui.development_files.roots.push_back(assets);
  config.startup_asset_packages.push_back(assets);
  engine.start(game, config);
  while (engine.isRunning()) engine.tick();
  return 0;
}
