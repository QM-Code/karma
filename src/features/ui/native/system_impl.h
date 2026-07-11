#pragma once

#include "features/ui/native/binding_engine.h"
#include "features/ui/native/canvas_layout.h"
#include "features/ui/native/document_runtime.h"
#include "features/ui/native/text_engine.h"
#include "karma/ui.h"

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace karma::ui::native {
class HotReloadCoordinator;
class PresentationResources;

namespace style_runtime {
struct StyleInputs;
struct StyleResult;
}  // namespace style_runtime

namespace transient_runtime {
struct NodeLookup;
}  // namespace transient_runtime

namespace runtime_dom {
struct DocumentInstance;
struct Node;
}  // namespace runtime_dom
}  // namespace karma::ui::native

namespace karma::ui {

struct System::Impl {
  assets::AssetRegistry* assets = nullptr;
  rendering::GraphicsDevice* graphics = nullptr;
  UiSystemConfig config{};
  std::unique_ptr<native::HotReloadCoordinator> hot_reload_coordinator;
  native::TextEngine text_engine{};
  std::unique_ptr<native::PresentationResources> presentation_resources;
  std::string locale;
  LocalizationProvider* localization = nullptr;
  native::DocumentRuntime document_runtime;
  native::BindingEngine bindings;
  AccessibilityTree accessibility;
  UiFrameDiagnostics pending_frame_diagnostics{};
  UiFrameDiagnostics last_frame_diagnostics{};
  std::uint64_t diagnostics_frame = 0u;
  bool accessibility_dirty = true;
  int dispatch_depth = 0;
  bool reconciling_models = false;
  std::vector<DocumentHandle> deferred_closes;
  std::unordered_set<std::string> reported_missing_localizations;
  int logical_width = 0;
  int logical_height = 0;
  int framebuffer_width = 0;
  int framebuffer_height = 0;
  float scale_x = 1.0f;
  float scale_y = 1.0f;
  native::SafeAreaInsets safe_area{};
  double pointer_x = 0.0;
  double pointer_y = 0.0;
  double clock_seconds = 0.0;

  Impl();
  ~Impl();

  void markDirty(native::runtime_dom::DocumentInstance& document,
                 bool bindings = false);
  void queueModelPath(native::runtime_dom::DocumentInstance& document,
                      std::string_view path);
  bool setModelFromWidget(native::runtime_dom::DocumentInstance& document,
                          std::string_view path,
                          Value value);
  void refreshFeatureFlags(native::runtime_dom::DocumentInstance& document);
  void refreshTemplate(native::runtime_dom::DocumentInstance& document,
                       native::runtime_dom::Node& node,
                       const Value::Object* outer_locals);
  void refreshNode(native::runtime_dom::DocumentInstance& document,
                   native::runtime_dom::Node& node,
                   const Value::Object* locals);
  void refreshBindingsFully(
      native::runtime_dom::DocumentInstance& document);
  bool refreshVirtualLists(native::runtime_dom::DocumentInstance& document,
                           native::runtime_dom::Node& node,
                           const Value::Object* locals);
  void reconcileModelPaths(
      native::runtime_dom::DocumentInstance& document,
      const std::vector<std::string>& changed_paths);

  native::runtime_dom::Node* nodeById(
      native::runtime_dom::DocumentInstance& document,
      std::string_view id);
  native::transient_runtime::NodeLookup transientNodeLookup();
  static Value choiceValue(const native::runtime_dom::Node& node);
  void setOpenState(native::runtime_dom::DocumentInstance& document,
                    native::runtime_dom::Node& node,
                    bool open);
  native::runtime_dom::Node* transientAnchor(
      native::runtime_dom::DocumentInstance& document,
      native::runtime_dom::Node& transient);
  native::runtime_dom::Node* topOpenTransient(
      native::runtime_dom::DocumentInstance& document);
  bool pointInsideTransient(native::runtime_dom::DocumentInstance& document,
                            native::runtime_dom::Node& transient,
                            double x,
                            double y);
  bool dismissTransient(native::runtime_dom::DocumentInstance& document,
                        native::runtime_dom::Node& transient,
                        bool restore_focus);
  bool dismissTransientOutside(
      native::runtime_dom::DocumentInstance& document,
      double x,
      double y);
  bool cancelTopTransient(native::runtime_dom::DocumentInstance& document);
  void closeOtherTransients(native::runtime_dom::DocumentInstance& document,
                            const native::runtime_dom::Node* except);
  bool toggleAnchoredTransient(
      native::runtime_dom::DocumentInstance& document,
      native::runtime_dom::Node& anchor);
  bool selectOwnedItem(native::runtime_dom::DocumentInstance& document,
                       native::runtime_dom::Node& item,
                       std::string_view owner_tag);
  bool moveTabSelection(native::runtime_dom::DocumentInstance& document,
                        native::runtime_dom::Node& current,
                        platform::Key key);
  bool moveMenuFocus(native::runtime_dom::DocumentInstance& document,
                     native::runtime_dom::Node& current,
                     platform::Key key);
  bool openSelectListbox(native::runtime_dom::DocumentInstance& document,
                         native::runtime_dom::Node& select);
  bool moveOptionFocus(native::runtime_dom::DocumentInstance& document,
                       native::runtime_dom::Node& current,
                       platform::Key key);
  bool handleTreeNavigation(native::runtime_dom::DocumentInstance& document,
                            native::runtime_dom::Node& item,
                            platform::Key key);
  void updateTimedTooltips(native::runtime_dom::DocumentInstance& document);
  void placeTransientWidgets(native::runtime_dom::DocumentInstance& document);
  void applyScrollPlacement(native::runtime_dom::DocumentInstance& document,
                            native::runtime_dom::Node& scroller,
                            float previous_x,
                            float previous_y);
  void setFocus(native::runtime_dom::DocumentInstance& document,
                native::runtime_dom::Node* node);
  void dispatchEvent(native::runtime_dom::DocumentInstance& document,
                     native::runtime_dom::Node& target,
                     Event& event);
  void fireAction(native::runtime_dom::DocumentInstance& document,
                  native::runtime_dom::Node& target,
                  std::string_view action,
                  Value value = {});
  void activateDefault(native::runtime_dom::DocumentInstance& document,
                       native::runtime_dom::Node& target,
                       double pointer_coordinate = 0.0,
                       bool emit_click_action = true);
  bool closeNow(DocumentHandle handle);
  void flushDeferredCloses();

  [[nodiscard]] native::style_runtime::StyleInputs styleInputs(
      const native::runtime_dom::DocumentInstance& document) const;
  void recordStyleResult(
      const native::style_runtime::StyleResult& result);
  void layoutDocument(native::runtime_dom::DocumentInstance& document);
  void rebuildAccessibility();
  native::runtime_dom::Node* hitTest(
      native::runtime_dom::DocumentInstance& document,
      double x,
      double y);
};

}  // namespace karma::ui
