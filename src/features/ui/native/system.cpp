#include "features/ui/native/system_impl.h"

#include "features/ui/native/hot_reload_coordinator.h"
#include "features/ui/native/presentation_resources.h"
#include "features/ui/native/system_lifetime.h"
#include "karma/assets.h"

#include <algorithm>
#include <memory>
#include <utility>

namespace karma::ui {

System::Impl::Impl() = default;
System::Impl::~Impl() = default;

System::System(assets::AssetRegistry& assets,
               rendering::GraphicsDevice* graphics,
               UiSystemConfig config)
    : impl_(std::make_unique<Impl>()),
      lifetime_(std::make_shared<detail::SystemLifetime>()) {
  lifetime_->system = this;
  impl_->assets = &assets;
  impl_->graphics = graphics;
  impl_->config = std::move(config);
  impl_->locale = impl_->config.locale.empty() ? "en" : impl_->config.locale;
  impl_->hot_reload_coordinator =
      std::make_unique<native::HotReloadCoordinator>(
          assets,
          native::HotReloadCoordinatorConfig{
              .enabled = impl_->config.hot_reload,
              .source_poll_interval = impl_->config.source_poll_interval,
              .development_files = impl_->config.development_files,
          });
  impl_->config.motion_scale = std::max(0.0f, impl_->config.motion_scale);
  impl_->config.glyph_atlas_page_width =
      std::clamp(impl_->config.glyph_atlas_page_width, 64, 4096);
  impl_->config.glyph_atlas_page_height =
      std::clamp(impl_->config.glyph_atlas_page_height, 64, 4096);
  impl_->text_engine = native::TextEngine({
      .shaped_cache_entries = 512u,
      .glyph_cache_bytes = std::max<std::size_t>(
          1u, impl_->config.glyph_atlas_budget_bytes / 4u),
  });
  impl_->presentation_resources =
      std::make_unique<native::PresentationResources>(
          assets, graphics, impl_->text_engine,
          native::PresentationResourceConfig{
              .glyph_atlas_page_width =
                  impl_->config.glyph_atlas_page_width,
              .glyph_atlas_page_height =
                  impl_->config.glyph_atlas_page_height,
              .glyph_atlas_budget_bytes =
                  impl_->config.glyph_atlas_budget_bytes,
              .svg_raster_budget_bytes =
                  impl_->config.svg_raster_budget_bytes,
          });
}

System::~System() {
  shutdown();
}

System::System(System&& other) noexcept
    : impl_(std::move(other.impl_)), lifetime_(std::move(other.lifetime_)) {
  if (lifetime_) lifetime_->system = this;
}

System& System::operator=(System&& other) noexcept {
  if (this != &other) {
    shutdown();
    impl_ = std::move(other.impl_);
    lifetime_ = std::move(other.lifetime_);
    if (lifetime_) lifetime_->system = this;
  }
  return *this;
}

DynamicImageHandle System::createImage(
    const rendering::TextureDesc& desc,
    const rendering::TextureUploadData& upload) {
  return impl_ && impl_->presentation_resources
             ? impl_->presentation_resources->createImage(desc, upload)
             : DynamicImageHandle{};
}

bool System::updateImage(DynamicImageHandle image,
                         const rendering::TextureUploadData& upload) {
  return impl_ && impl_->presentation_resources &&
         impl_->presentation_resources->updateImage(image, upload);
}

bool System::destroyImage(DynamicImageHandle image) {
  return impl_ && impl_->presentation_resources &&
         impl_->presentation_resources->destroyImage(image);
}

const AccessibilityTree& System::accessibilityTree() const {
  static const AccessibilityTree empty;
  return impl_ ? impl_->accessibility : empty;
}

const UiFrameDiagnostics& System::frameDiagnostics() const {
  static const UiFrameDiagnostics empty;
  return impl_ ? impl_->last_frame_diagnostics : empty;
}

const UiSystemConfig& System::config() const {
  static const UiSystemConfig defaults;
  return impl_ ? impl_->config : defaults;
}

void System::shutdown() {
  if (lifetime_) lifetime_->system = nullptr;
  if (!impl_) return;
  if (impl_->hot_reload_coordinator) {
    impl_->hot_reload_coordinator->invalidate();
  }
  impl_->dispatch_depth = 0;
  impl_->deferred_closes.clear();
  impl_->document_runtime.clear();
  if (impl_->presentation_resources) {
    impl_->presentation_resources->shutdown();
  }
  impl_->bindings.clear();
  impl_->accessibility = {};
}

}  // namespace karma::ui
