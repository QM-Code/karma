#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include "demo_asset_paths.h"
#include "karma/features/ui/rmlui/rmlui_layer.h"
#include "karma/karma.h"

#include <RmlUi/Core.h>
#include <spdlog/spdlog.h>

namespace karma::demo {

namespace {

std::filesystem::path resolveAssetPath(std::string_view relative) {
  std::filesystem::path candidate(relative);
  if (candidate.is_absolute() && std::filesystem::exists(candidate)) {
    return candidate;
  }
  std::filesystem::path cwd = std::filesystem::current_path();
  for (int depth = 0; depth < 6; ++depth) {
    const std::filesystem::path direct = cwd / candidate;
    if (std::filesystem::exists(direct)) {
      return direct;
    }
    const std::filesystem::path examples = cwd / "examples" / "assets" / candidate.filename();
    if (std::filesystem::exists(examples)) {
      return examples;
    }
    if (!cwd.has_parent_path()) {
      break;
    }
    cwd = cwd.parent_path();
  }
  return candidate;
}

void replaceAll(std::string& haystack, std::string_view needle, std::string_view value) {
  if (needle.empty()) {
    return;
  }
  size_t pos = 0;
  while ((pos = haystack.find(needle, pos)) != std::string::npos) {
    haystack.replace(pos, needle.size(), value);
    pos += value.size();
  }
}

const char* kDemoRmlTemplate =
    "<rml><body>"
    "<div style=\"width:360px;padding:12px;background:#1b2433;"
    "border-width:1px;border-color:#32435f;\">"
    "<div style=\"font-family:Roboto;font-weight:900;font-size:20px;\">Karma RmlUi</div>"
    "<div style=\"font-family:Roboto;font-weight:900;margin-top:6px;\">"
    "Built-in Karma RmlUi adapter</div>"
    "<div style=\"margin-top:10px;\">"
    "<img src=\"{PNG}\" width=\"128\" height=\"128\"/>"
    "</div>"
    "<div style=\"margin-top:10px;\">"
    "<svg src=\"{SVG}\" width=\"128\" height=\"128\"></svg>"
    "</div>"
    "</div>"
    "</body></rml>";

}  // namespace

class DemoRmlContent final {
 public:
  void onContextReady(Rml::Context& context) {
    const auto weight = static_cast<Rml::Style::FontWeight>(900);
    const auto font_path = resolveAssetPath("examples/assets/Roboto-Black.ttf");
    if (!std::filesystem::exists(font_path)) {
      spdlog::warn("RmlUi: font not found at {}", font_path.string());
    } else {
      if (!Rml::LoadFontFace(font_path.string(), false, weight)) {
        spdlog::warn("RmlUi: failed to load font {}", font_path.string());
      }
      if (!Rml::LoadFontFace(font_path.string(), true, weight)) {
        spdlog::warn("RmlUi: failed to load italic font {}", font_path.string());
      }
    }

    const auto png_path = resolveAssetPath("examples/assets/demo_image.png");
    const auto svg_path = resolveAssetPath("examples/assets/demo_icon.svg");
    std::string rml = kDemoRmlTemplate;
    replaceAll(rml, "{PNG}", png_path.string());
    replaceAll(rml, "{SVG}", svg_path.string());

    document_ = context.LoadDocumentFromMemory(rml, "[rmlui-minimal]");
    if (!document_) {
      spdlog::warn("RmlUi: failed to load demo RML");
      return;
    }
    document_->Show();
  }

 private:
  Rml::ElementDocument* document_ = nullptr;
};

class DemoGame : public app::GameInterface {
 public:
  void onStart() override {}
  void onFixedUpdate(float /*dt*/) override {}
  void onUpdate(float /*dt*/) override {}
  void onShutdown() override {}
};

}  // namespace karma::demo

int main() {
  karma::app::EngineApp app;
  karma::demo::DemoGame game;

  auto content = std::make_shared<karma::demo::DemoRmlContent>();
  karma::rmlui::RmlUiLayerConfig ui_config;
  ui_config.asset_roots.push_back(karma::demo::resolveAssetPath("examples/assets"));
  app.setUi(karma::rmlui::createUiLayer(
      [content](Rml::Context& context) { content->onContextReady(context); },
      std::move(ui_config)));

  karma::app::EngineConfig config;
  config.window.title = "Karma RmlUi";
  config.window.samples = 1;
  config.cursor_visible = true;
  config.enable_anisotropy = true;
  config.anisotropy_level = 16;
  config.generate_mipmaps = true;
  config.shadow_map_size = 2048;
  config.shadow_pcf_radius = 1;

  app.start(game, config);
  while (app.isRunning()) {
    app.tick();
  }

  return 0;
}
