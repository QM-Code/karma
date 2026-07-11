#include "features/ui/native/system_impl.h"

#include "features/ui/native/accessibility_builder.h"
#include "features/ui/native/authoring.h"
#include "features/ui/native/canvas_layout.h"
#include "features/ui/native/diagnostics.h"
#include "features/ui/native/document_layout_runtime.h"
#include "features/ui/native/hot_reload_coordinator.h"
#include "features/ui/native/presentation_builder.h"
#include "features/ui/native/presentation_resources.h"
#include "features/ui/native/runtime_dom.h"
#include "features/ui/native/style_runtime.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace karma::ui {
namespace {

using native::addDiagnostic;
using native::HotReloadStage;
using native::jsonNumber;
using native::runtime_dom::attributeBoolean;
using native::runtime_dom::DocumentInstance;
using native::runtime_dom::forRuntimeChildren;
using native::runtime_dom::isVisibleForInteraction;
using native::runtime_dom::Node;
using native::runtime_dom::nonEmpty;
using native::runtime_dom::Rect;
using native::style_runtime::setInlineStyleProperty;

}  // namespace

void System::Impl::layoutDocument(DocumentInstance& doc) {
  const native::document_layout_runtime::LayoutResult result =
      native::document_layout_runtime::layoutDocument(
          doc,
          {.logical_width = logical_width,
           .logical_height = logical_height,
           .locale = locale},
          text_engine, *presentation_resources);
  pending_frame_diagnostics.laid_out_nodes += result.laid_out_nodes;
}

void System::Impl::rebuildAccessibility() {
  const std::shared_ptr<const native::DocumentRuntime::DocumentOrder> ordered =
      document_runtime.documentsInPaintOrder();
  accessibility = native::buildAccessibilityTree(std::move(accessibility),
                                                 *ordered);
  pending_frame_diagnostics.accessibility_nodes += accessibility.nodes.size();
  accessibility_dirty = false;
  const auto all_documents = document_runtime.allDocuments();
  for (DocumentInstance* document : *all_documents) {
    document->accessibility_revision = false;
  }
}

void System::buildFrame(float dt,
                        int logical_width,
                        int logical_height,
                        int framebuffer_width,
                        int framebuffer_height,
                        float scale_x,
                        float scale_y,
                        rendering::UIDrawData& output,
                        platform::SafeAreaInsets safe_area) {
  output.clear();
  if (!impl_ || !impl_->config.enabled || logical_width <= 0 || logical_height <= 0 ||
      framebuffer_width <= 0 || framebuffer_height <= 0) return;
  const bool dimensions_changed = impl_->logical_width != logical_width ||
                                  impl_->logical_height != logical_height ||
                                  impl_->framebuffer_width != framebuffer_width ||
                                  impl_->framebuffer_height != framebuffer_height ||
                                  impl_->scale_x != scale_x || impl_->scale_y != scale_y ||
                                  impl_->safe_area != native::SafeAreaInsets{
                                      safe_area.left, safe_area.top,
                                      safe_area.right, safe_area.bottom};
  impl_->logical_width = logical_width;
  impl_->logical_height = logical_height;
  impl_->framebuffer_width = framebuffer_width;
  impl_->framebuffer_height = framebuffer_height;
  impl_->scale_x = std::max(0.0001f, scale_x);
  impl_->scale_y = std::max(0.0001f, scale_y);
  impl_->safe_area = {safe_area.left, safe_area.top,
                      safe_area.right, safe_area.bottom};
  impl_->presentation_resources->advanceFrame();
  if (std::isfinite(dt) && dt > 0.0f) {
    impl_->clock_seconds += static_cast<double>(dt);
  }

  auto apply_reload_stage = [&](HotReloadStage stage) {
    DocumentInstance* document =
        impl_->document_runtime.document(stage.document);
    if (document == nullptr ||
        document->reload_request_sequence != stage.sequence) {
      return;
    }
    DocumentInstance& doc = *document;
    doc.reload_pending = false;
    if (!stage.valid) {
      doc.diagnostics = std::move(stage.diagnostics);
      if (stage.needs_stylesheet_resnapshot) {
        doc.reload_stylesheet_keys = std::move(stage.stylesheet_keys);
        doc.reload_requested_fingerprint.clear();
      }
      return;
    }
    doc.reload_stylesheet_keys.clear();
    doc.reload_requested_fingerprint.clear();

    if (!stage.replace_document) {
      doc.rules = std::move(stage.rules);
      doc.font_faces = std::move(stage.font_faces);
      doc.keyframes = std::move(stage.keyframes);
      doc.style_hash = std::move(stage.style_hash);
      doc.diagnostics = std::move(stage.diagnostics);
      impl_->refreshFeatureFlags(doc);
      doc.style_revision = true;
      return;
    }

    struct ReloadState {
      bool checked = false;
      Value control_value{};
      float scroll_x = 0.0f;
      float scroll_y = 0.0f;
      std::optional<bool> open;
      std::optional<bool> expanded;
      std::optional<bool> collapsed;
      std::optional<Rect> window_rect;
      std::optional<std::string> window_z;
    };
    std::unordered_map<std::string, ReloadState> preserved;
    auto preserve = [&](auto&& self, const Node& node) -> void {
      ReloadState state{.checked = node.checked,
                        .control_value = node.control_value,
                        .scroll_x = node.scroll_x,
                        .scroll_y = node.scroll_y};
      if (!node.attributes.contains("bind-open") &&
          (node.tag == "select" || node.tag == "popup" ||
           node.tag == "menu" || node.tag == "window")) {
        state.open = node.tag == "window" &&
                             !node.attributes.contains("open")
                         ? true
                         : attributeBoolean(node, "open");
      }
      if (!node.attributes.contains("bind-expanded") &&
          (node.tag == "disclosure" || node.tag == "tree-item")) {
        state.expanded = attributeBoolean(node, "expanded");
      }
      if (!node.attributes.contains("bind-collapsed") &&
          node.tag == "window") {
        state.collapsed = attributeBoolean(node, "collapsed");
      }
      if (node.tag == "window" &&
          !node.attributes.contains("bind-window-state") &&
          nonEmpty(node.layout)) {
        state.window_rect = node.layout;
        if (const auto z = node.style.find("z-index"); z != node.style.end()) {
          state.window_z = z->second;
        }
      }
      preserved[node.identity] = std::move(state);
      forRuntimeChildren(node, [&](const Node& child, const Value::Object*) {
        self(self, child);
      });
    };
    if (doc.body) preserve(preserve, *doc.body);

    std::string focus_identity;
    if (const Node* focused = impl_->document_runtime.element(doc.focused)) {
      focus_identity = focused->identity;
    }
    if (doc.body) impl_->document_runtime.releaseTree(doc, *doc.body);
    doc.body = std::move(stage.body);
    doc.canvas_spec = stage.canvas;
    doc.canvas_layout = native::resolveCanvas(
        doc.canvas_spec, static_cast<float>(impl_->logical_width),
        static_cast<float>(impl_->logical_height), impl_->safe_area);
    doc.ids.clear();
    doc.focused = {};
    doc.hovered = {};
    doc.pointer_capture = {};
    doc.pointer_down = false;
    doc.source_hash = std::move(stage.document_hash);
    doc.stylesheet_keys = std::move(stage.stylesheet_keys);
    doc.rules = std::move(stage.rules);
    doc.font_faces = std::move(stage.font_faces);
    doc.keyframes = std::move(stage.keyframes);
    doc.style_hash = std::move(stage.style_hash);
    doc.diagnostics = std::move(stage.diagnostics);
    impl_->refreshFeatureFlags(doc);
    impl_->document_runtime.allocateTree(doc, *doc.body);
    impl_->markDirty(doc, true);
    impl_->refreshBindingsFully(doc);

    auto restore = [&](auto&& self, Node& node) -> void {
      if (const auto found = preserved.find(node.identity);
          found != preserved.end()) {
        node.scroll_x = found->second.scroll_x;
        node.scroll_y = found->second.scroll_y;
        if (!node.attributes.contains("bind-value") &&
            !node.attributes.contains("bind-checked")) {
          node.checked = found->second.checked;
          node.attributes["checked"] =
              found->second.checked ? "true" : "false";
        }
        if (!node.attributes.contains("bind-value")) {
          node.control_value = found->second.control_value;
        }
        auto restore_state = [&](std::string_view name,
                                 const std::optional<bool>& value) {
          if (value.has_value() &&
              !node.attributes.contains("bind-" + std::string(name))) {
            node.attributes[std::string(name)] = *value ? "true" : "false";
          }
        };
        restore_state("open", found->second.open);
        restore_state("expanded", found->second.expanded);
        restore_state("collapsed", found->second.collapsed);
        if (found->second.window_rect.has_value() &&
            node.tag == "window" &&
            !node.attributes.contains("bind-window-state")) {
          const Rect& rect = *found->second.window_rect;
          setInlineStyleProperty(node, "left", jsonNumber(rect.x));
          setInlineStyleProperty(node, "top", jsonNumber(rect.y));
          setInlineStyleProperty(node, "width", jsonNumber(rect.width));
          setInlineStyleProperty(node, "height", jsonNumber(rect.height));
          if (found->second.window_z.has_value()) {
            setInlineStyleProperty(node, "z-index", *found->second.window_z);
          }
        }
      }
      forRuntimeChildren(node, [&](Node& child, const Value::Object*) {
        self(self, child);
      });
    };
    restore(restore, *doc.body);
    // Restored widget attributes and control values drive derived visibility,
    // option checks, and tab/tree selection. Reconcile once more before style
    // and layout so the first post-swap frame is internally consistent.
    impl_->refreshBindingsFully(doc);
    Node* restored_focus = nullptr;
    auto find_restored_focus = [&](auto&& self, Node& node) -> void {
      if (restored_focus == nullptr && !focus_identity.empty() &&
          node.identity == focus_identity && !node.disabled) {
        restored_focus = &node;
      }
      forRuntimeChildren(node, [&](Node& child, const Value::Object*) {
        self(self, child);
      });
    };
    find_restored_focus(find_restored_focus, *doc.body);
    // Restore the pseudo-state before the broad pass so :focus declarations
    // participate in the first post-swap cascade. Visibility depends on that
    // computed style, so validate it immediately afterwards and undo the
    // targeted state if the replacement node cannot receive focus.
    if (restored_focus != nullptr) {
      restored_focus->focused = true;
      doc.focused = restored_focus->handle;
    }
    impl_->recordStyleResult(native::style_runtime::styleDocument(
        doc, impl_->styleInputs(doc)));
    doc.layout_revision = true;
    if (restored_focus != nullptr &&
        !isVisibleForInteraction(*restored_focus)) {
      restored_focus->focused = false;
      doc.focused = {};
      impl_->recordStyleResult(native::style_runtime::restyleNode(
          doc, restored_focus, impl_->styleInputs(doc)));
    }
  };

  std::vector<DocumentInstance*> open_documents;
  if (impl_->config.hot_reload) {
    const auto documents = impl_->document_runtime.allDocuments();
    open_documents.assign(documents->begin(), documents->end());
  }
  for (HotReloadStage& stage :
       impl_->hot_reload_coordinator->tick(dt, open_documents)) {
    apply_reload_stage(std::move(stage));
  }

  bool accessibility_dirty = dimensions_changed || impl_->accessibility_dirty;
  const auto all_documents = impl_->document_runtime.allDocuments();
  for (DocumentInstance* document : *all_documents) {
    DocumentInstance& doc = *document;
    doc.canvas_layout = native::resolveCanvas(
        doc.canvas_spec, static_cast<float>(logical_width),
        static_cast<float>(logical_height), impl_->safe_area);
    if (doc.has_tooltips) impl_->updateTimedTooltips(doc);
    if (dimensions_changed) doc.style_revision = true;
    if (doc.binding_revision && doc.body) {
      const auto stage_start = std::chrono::steady_clock::now();
      if (!doc.pending_model_paths.empty()) {
        std::vector<std::string> pending =
            std::move(doc.pending_model_paths);
        doc.pending_model_paths.clear();
        impl_->reconcileModelPaths(doc, pending);
      } else {
        impl_->refreshBindingsFully(doc);
        doc.style_revision = true;
      }
      impl_->pending_frame_diagnostics.reconcile_ms +=
          std::chrono::duration<double, std::milli>(
              std::chrono::steady_clock::now() - stage_start)
              .count();
    }
    if (doc.has_virtual_lists && doc.virtual_range_revision && doc.body &&
        impl_->refreshVirtualLists(doc, *doc.body, nullptr)) {
      doc.style_revision = true;
      doc.layout_revision = true;
      doc.accessibility_revision = true;
    }
    doc.virtual_range_revision = false;
    // Active transition/animation tracks update their computed values below.
    // A paint-only animation must not force a full cascade traversal every
    // frame; changed fragments invalidate themselves directly.
    if (doc.style_revision && doc.body) {
      const auto stage_start = std::chrono::steady_clock::now();
      impl_->recordStyleResult(native::style_runtime::styleDocument(
          doc, impl_->styleInputs(doc)));
      impl_->pending_frame_diagnostics.style_ms +=
          std::chrono::duration<double, std::milli>(
              std::chrono::steady_clock::now() - stage_start)
              .count();
    }
    std::uint64_t motion_frame = impl_->diagnostics_frame + 1u;
    if (motion_frame == 0u) motion_frame = 1u;
    const native::style_runtime::MotionResult motion =
        native::style_runtime::advanceActiveMotion(
            doc, impl_->clock_seconds, impl_->config.motion_scale,
            motion_frame);
    impl_->pending_frame_diagnostics.advanced_motion_nodes +=
        motion.advanced_nodes;
    doc.layout_revision = motion.layout_changed || doc.layout_revision;
    if (motion.stacking_changed) doc.overlay_order_revision = true;
    if (doc.font_revision && doc.body &&
        impl_->presentation_resources->ensureTreeFonts(*doc.body)) {
      for (DocumentInstance* dependent : *all_documents) {
        dependent->layout_revision = true;
      }
    }
    doc.font_revision = false;
    if ((doc.layout_revision || dimensions_changed) && doc.body) {
      doc.measure_revision = true;
      const auto stage_start = std::chrono::steady_clock::now();
      impl_->layoutDocument(doc);
      impl_->pending_frame_diagnostics.layout_ms +=
          std::chrono::duration<double, std::milli>(
              std::chrono::steady_clock::now() - stage_start)
              .count();
    }
    if (doc.has_transients && doc.placement_revision && doc.body) {
      const auto stage_start = std::chrono::steady_clock::now();
      impl_->placeTransientWidgets(doc);
      doc.placement_revision = false;
      impl_->pending_frame_diagnostics.placement_ms +=
          std::chrono::duration<double, std::milli>(
              std::chrono::steady_clock::now() - stage_start)
              .count();
    }
    accessibility_dirty = accessibility_dirty || doc.accessibility_revision;
  }
  const std::shared_ptr<const native::DocumentRuntime::DocumentOrder> ordered =
      impl_->document_runtime.documentsInPaintOrder();
  const auto paint_start = std::chrono::steady_clock::now();
  const native::presentation_builder::BuildResult paint_result =
      native::presentation_builder::build(
          *ordered,
          {.framebuffer_width = impl_->framebuffer_width,
           .framebuffer_height = impl_->framebuffer_height,
           .framebuffer_scale_x = impl_->scale_x,
           .framebuffer_scale_y = impl_->scale_y,
           .locale = impl_->locale,
           .retained_paint_budget_bytes =
               impl_->config.retained_paint_budget_bytes,
           .graphics_available = impl_->graphics != nullptr},
          impl_->text_engine, *impl_->presentation_resources, output,
          {.nine_slice_cell_limit =
               +[](void*, DocumentInstance& document, const Node&) {
                 addDiagnostic(
                     document.diagnostics, document.asset_key,
                     "UI_NINE_SLICE_CELL_LIMIT",
                     "border image repeat counts were reduced to the "
                     "16,384-cell panel safety limit", 0u,
                     DiagnosticSeverity::Warning);
               }});
  impl_->pending_frame_diagnostics.rebuilt_fragments +=
      paint_result.rebuilt_fragments;
  impl_->pending_frame_diagnostics.paint_ms +=
      std::chrono::duration<double, std::milli>(
          std::chrono::steady_clock::now() - paint_start)
          .count();
  if (accessibility_dirty) {
    const auto stage_start = std::chrono::steady_clock::now();
    impl_->rebuildAccessibility();
    impl_->pending_frame_diagnostics.accessibility_ms +=
        std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - stage_start)
            .count();
  }
  UiFrameDiagnostics completed = impl_->pending_frame_diagnostics;
  ++impl_->diagnostics_frame;
  if (impl_->diagnostics_frame == 0u) ++impl_->diagnostics_frame;
  completed.frame = impl_->diagnostics_frame;
  completed.output_vertices = output.vertices.size();
  completed.output_commands = output.commands.size();
  impl_->last_frame_diagnostics = completed;
  impl_->pending_frame_diagnostics = {};
}

}  // namespace karma::ui
