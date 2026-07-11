#include "features/ui/native/system_impl.h"

#include "features/ui/native/authoring.h"
#include "features/ui/native/canvas_layout.h"
#include "features/ui/native/development_loader.h"
#include "features/ui/native/development_path.h"
#include "features/ui/native/diagnostics.h"
#include "features/ui/native/document_loader.h"
#include "features/ui/native/font_face.h"
#include "features/ui/native/hot_reload_coordinator.h"
#include "features/ui/native/runtime_dom.h"
#include "features/ui/native/string_utils.h"
#include "features/ui/native/style_runtime.h"
#include "karma/assets.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <iterator>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace karma::ui {
namespace {

using native::addDiagnostic;
using native::FontFaceDefinition;
using native::jsonNumber;
using native::parseDocumentSource;
using native::parseThemeGraph;
using native::ParsedDocument;
using native::ParsedTheme;
using native::runtime_dom::DocumentInstance;
using native::runtime_dom::forRuntimeChildren;
using native::runtime_dom::isScrollContainer;
using native::runtime_dom::isVisibleForInteraction;
using native::runtime_dom::Node;
using native::runtime_dom::styleFloat;
using native::string_utils::trim;
using native::style_runtime::setInlineStyleProperty;
using FontFace = FontFaceDefinition;
using StyleRule = native::StyleRule;
using ResolvedElement = native::DocumentRuntime::ResolvedElement;

bool hasErrors(const std::vector<Diagnostic>& diagnostics) {
  return std::any_of(
      diagnostics.begin(), diagnostics.end(), [](const Diagnostic& diagnostic) {
        return diagnostic.severity == DiagnosticSeverity::Error;
      });
}

}  // namespace

OpenDocumentResult System::open(std::string_view asset_key,
                                const OpenDocumentOptions& options) {
  OpenDocumentResult result;
  if (!impl_ || !impl_->config.enabled) {
    addDiagnostic(result.diagnostics, asset_key, "UI_DISABLED", "native UI is disabled");
    return result;
  }
  if (!assets::AssetRegistry::isValidAssetKey(asset_key)) {
    addDiagnostic(result.diagnostics, asset_key, "UI_INVALID_ASSET_KEY",
                  assets::AssetRegistry::assetKeyValidationError(asset_key));
    return result;
  }
  const assets::UiDocumentAsset* asset = impl_->assets->findUiDocumentAsset(asset_key);
  if (asset == nullptr) {
    addDiagnostic(result.diagnostics, asset_key, "UI_DOCUMENT_NOT_FOUND",
                  "UI document asset was not found");
    return result;
  }
  ParsedDocument parsed = parseDocumentSource(asset->canonical_json_utf8, asset_key);
  result.diagnostics = parsed.diagnostics;
  std::vector<StyleRule> rules;
  std::vector<FontFace> font_faces;
  std::vector<native::Keyframes> keyframes;
  std::string style_hash;
  std::size_t source_order = 0;
  std::map<std::string, std::string> theme_hashes;
  const native::ThemeSourceResolver theme_resolver =
      [&](std::string_view key) -> std::optional<native::ThemeSource> {
    const assets::UiThemeAsset* theme = impl_->assets->findUiThemeAsset(key);
    if (theme == nullptr) return std::nullopt;
    return native::ThemeSource{.source = theme->canonical_json_utf8,
                               .content_hash = theme->content_hash};
  };
  for (const std::string& key : parsed.stylesheet_keys) {
    ParsedTheme parsed_theme = parseThemeGraph(key, theme_resolver, source_order);
    for (const std::string& source_key : parsed_theme.source_keys) {
      const auto source = theme_resolver(source_key);
      if (!source.has_value()) continue;
      theme_hashes[source_key] = source->content_hash;
    }
    result.diagnostics.insert(
        result.diagnostics.end(),
        std::make_move_iterator(parsed_theme.diagnostics.begin()),
        std::make_move_iterator(parsed_theme.diagnostics.end()));
    const std::size_t font_order = font_faces.size();
    for (FontFace& face : parsed_theme.font_faces) face.source_order += font_order;
    rules.insert(rules.end(),
                 std::make_move_iterator(parsed_theme.rules.begin()),
                 std::make_move_iterator(parsed_theme.rules.end()));
    font_faces.insert(font_faces.end(),
                      std::make_move_iterator(parsed_theme.font_faces.begin()),
                      std::make_move_iterator(parsed_theme.font_faces.end()));
    keyframes.insert(keyframes.end(),
                     std::make_move_iterator(parsed_theme.keyframes.begin()),
                     std::make_move_iterator(parsed_theme.keyframes.end()));
  }
  for (const auto& [key, hash] : theme_hashes) {
    style_hash += key + ":" + hash + ";";
  }
  if (!parsed.body || hasErrors(result.diagnostics)) return result;

  auto document = std::make_unique<DocumentInstance>();
  document->asset_key = std::string(asset_key);
  document->source_hash = asset->content_hash;
  document->style_hash = std::move(style_hash);
  document->stylesheet_keys = parsed.stylesheet_keys;
  document->options = options;
  document->model = std::move(parsed.model_defaults);
  document->canvas_spec = parsed.canvas;
  document->canvas_layout = native::resolveCanvas(
      document->canvas_spec, static_cast<float>(impl_->logical_width),
      static_cast<float>(impl_->logical_height), impl_->safe_area);
  document->body = std::move(parsed.body);
  document->rules = std::move(rules);
  document->font_faces = std::move(font_faces);
  document->keyframes = std::move(keyframes);
  document->diagnostics = result.diagnostics;
  result.document = impl_->document_runtime.adopt(std::move(document));
  DocumentInstance* opened = impl_->document_runtime.document(result.document);
  if (opened == nullptr) return result;
  impl_->refreshFeatureFlags(*opened);
  impl_->refreshBindingsFully(*opened);
  impl_->recordStyleResult(native::style_runtime::styleDocument(
      *opened, impl_->styleInputs(*opened)));
  return result;
}

OpenDocumentResult System::openFile(const std::filesystem::path& relative_path,
                                    const OpenDocumentOptions& options) {
  OpenDocumentResult result;
  const std::string diagnostic_key = relative_path.generic_string();
  if (!impl_ || !impl_->config.enabled) {
    addDiagnostic(result.diagnostics, diagnostic_key, "UI_DISABLED",
                  "native UI is disabled");
    return result;
  }
  if (!impl_->config.development_files.enabled) {
    addDiagnostic(result.diagnostics, diagnostic_key, "UI_DEV_FILES_DISABLED",
                  "development UI file loading is disabled");
    return result;
  }
  if (relative_path.empty() || relative_path.is_absolute()) {
    addDiagnostic(result.diagnostics, diagnostic_key, "UI_DEV_FILE_PATH",
                  "openFile requires a non-empty path relative to a configured root");
    return result;
  }

  std::optional<std::filesystem::path> document_path;
  std::filesystem::path selected_root;
  const std::vector<std::filesystem::path>& roots =
      impl_->hot_reload_coordinator->developmentRoots();
  for (const std::filesystem::path& requested_root : roots) {
    std::error_code error;
    const std::filesystem::path root =
        std::filesystem::weakly_canonical(requested_root, error);
    if (error) continue;
    if (auto candidate =
            native::resolveDevelopmentPath(relative_path, root, root)) {
      document_path = std::move(candidate);
      selected_root = root;
      break;
    }
  }
  if (!document_path) {
    addDiagnostic(result.diagnostics, diagnostic_key, "UI_DEV_FILE_NOT_FOUND",
                  "development UI file was not found under any configured root");
    return result;
  }

  assets::AssetRegistry staging;
  native::DevelopmentGraphBuild graph;
  if (!native::buildDevelopmentGraph(*document_path, selected_root, staging,
                                     graph, result.diagnostics) ||
      !native::commitDevelopmentGraph(*impl_->assets, staging, graph)) {
    if (result.diagnostics.empty()) {
      addDiagnostic(result.diagnostics, diagnostic_key, "UI_DEV_GRAPH_COMMIT",
                    "could not commit the validated development UI graph");
    }
    return result;
  }
  OpenDocumentResult opened = open(graph.document_key, options);
  result.diagnostics.insert(result.diagnostics.end(),
                            std::make_move_iterator(opened.diagnostics.begin()),
                            std::make_move_iterator(opened.diagnostics.end()));
  result.document = opened.document;
  if (DocumentInstance* document =
          impl_->document_runtime.document(result.document)) {
    document->development_path = graph.document_path;
    document->development_root = selected_root;
    document->development_dependencies = std::move(graph.watched_paths);
  }
  return result;
}

bool System::isOpen(DocumentHandle document) const {
  return impl_ && impl_->document_runtime.document(document) != nullptr;
}

bool System::close(DocumentHandle document) {
  if (!impl_ || impl_->document_runtime.document(document) == nullptr) {
    return false;
  }
  if (impl_->dispatch_depth > 0) {
    if (std::find(impl_->deferred_closes.begin(), impl_->deferred_closes.end(), document) ==
        impl_->deferred_closes.end()) {
      impl_->deferred_closes.push_back(document);
    }
    return true;
  }
  return impl_->closeNow(document);
}

bool System::show(DocumentHandle document) {
  DocumentInstance* doc =
      impl_ ? impl_->document_runtime.document(document) : nullptr;
  if (doc == nullptr) return false;
  if (doc->options.visible) return true;
  doc->options.visible = true;
  impl_->document_runtime.invalidateOrder();
  doc->accessibility_revision = true;
  return true;
}

bool System::hide(DocumentHandle document) {
  DocumentInstance* doc =
      impl_ ? impl_->document_runtime.document(document) : nullptr;
  if (doc == nullptr) return false;
  if (!doc->options.visible) return true;
  if (Node* hovered = impl_->document_runtime.element(doc->hovered)) {
    hovered->hovered = false;
  }
  doc->hovered = {};
  doc->options.visible = false;
  impl_->document_runtime.invalidateOrder();
  doc->pointer_capture = {};
  doc->pointer_down = false;
  impl_->markDirty(*doc);
  doc->accessibility_revision = true;
  return true;
}

bool System::setModal(DocumentHandle document, bool modal) {
  DocumentInstance* doc =
      impl_ ? impl_->document_runtime.document(document) : nullptr;
  if (doc == nullptr) return false;
  doc->options.modal = modal;
  return true;
}

bool System::set(DocumentHandle document, std::string_view property_path, Value value) {
  return setMany(document,
                 {ModelUpdate{.property_path = std::string(property_path),
                              .value = std::move(value)}});
}

bool System::setMany(DocumentHandle document, std::vector<ModelUpdate> updates) {
  DocumentInstance* doc =
      impl_ ? impl_->document_runtime.document(document) : nullptr;
  if (doc == nullptr) return false;

  for (const ModelUpdate& update : updates) {
    if (!impl_->bindings.validPath(update.property_path)) return false;
  }

  std::vector<std::string> changed_paths;
  changed_paths.reserve(updates.size());
  for (const ModelUpdate& update : updates) {
    if (std::find(changed_paths.begin(), changed_paths.end(),
                  update.property_path) == changed_paths.end()) {
      changed_paths.push_back(update.property_path);
    }
  }

  Value staged_model = doc->model;
  for (ModelUpdate& update : updates) {
    if (!impl_->bindings.set(staged_model, update.property_path,
                             std::move(update.value))) {
      return false;
    }
  }
  if (staged_model == doc->model) return true;

  changed_paths.erase(
      std::remove_if(changed_paths.begin(), changed_paths.end(),
                     [&](const std::string& path) {
                       return impl_->bindings.get(doc->model, path) ==
                              impl_->bindings.get(staged_model, path);
                     }),
      changed_paths.end());

  doc->model = std::move(staged_model);
  for (std::string& path : changed_paths) {
    if (std::find(doc->pending_model_paths.begin(),
                  doc->pending_model_paths.end(), path) ==
        doc->pending_model_paths.end()) {
      doc->pending_model_paths.push_back(std::move(path));
    }
  }
  doc->binding_revision = true;
  if (impl_->dispatch_depth == 0 && doc->body) {
    std::vector<std::string> pending = std::move(doc->pending_model_paths);
    doc->pending_model_paths.clear();
    const auto reconcile_start = std::chrono::steady_clock::now();
    impl_->reconcileModelPaths(*doc, pending);
    impl_->pending_frame_diagnostics.reconcile_ms +=
        std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - reconcile_start)
            .count();
  }
  return true;
}

std::optional<Value> System::get(DocumentHandle document,
                                 std::string_view property_path) const {
  const DocumentInstance* doc =
      impl_ ? impl_->document_runtime.document(document) : nullptr;
  if (doc == nullptr) return std::nullopt;
  return impl_->bindings.get(doc->model, property_path);
}

ElementHandle System::findById(DocumentHandle document, std::string_view id) const {
  const DocumentInstance* doc =
      impl_ ? impl_->document_runtime.document(document) : nullptr;
  if (doc == nullptr) return {};
  const auto found = doc->ids.find(std::string(id));
  return found == doc->ids.end() ? ElementHandle{} : found->second;
}

bool System::addClass(ElementHandle element, std::string_view class_name) {
  const ResolvedElement resolved =
      impl_ ? impl_->document_runtime.resolve(element) : ResolvedElement{};
  const std::string name = trim(class_name);
  if (!resolved || name.empty() || name.find(' ') != name.npos) return false;
  if (resolved.node->classes.insert(name).second) {
    resolved.node->style_names.push_back(name);
    impl_->markDirty(*resolved.document);
  }
  return true;
}

bool System::removeClass(ElementHandle element, std::string_view class_name) {
  const ResolvedElement resolved =
      impl_ ? impl_->document_runtime.resolve(element) : ResolvedElement{};
  if (!resolved) return false;
  const std::string name(class_name);
  if (resolved.node->classes.erase(name) != 0u) {
    resolved.node->style_names.erase(
        std::remove(resolved.node->style_names.begin(),
                    resolved.node->style_names.end(), name),
        resolved.node->style_names.end());
    impl_->markDirty(*resolved.document);
  }
  return true;
}

bool System::setText(ElementHandle element, std::string_view text) {
  const ResolvedElement resolved =
      impl_ ? impl_->document_runtime.resolve(element) : ResolvedElement{};
  if (!resolved) return false;
  resolved.node->source_text = std::string(text);
  resolved.node->text = resolved.node->source_text;
  resolved.node->programmatic_text = true;
  resolved.node->attributes.erase("loc");
  impl_->markDirty(*resolved.document);
  return true;
}

bool System::setImage(ElementHandle element, const ImageSource& image) {
  const ResolvedElement resolved =
      impl_ ? impl_->document_runtime.resolve(element) : ResolvedElement{};
  if (!resolved) return false;
  resolved.node->image = image;
  impl_->markDirty(*resolved.document);
  return true;
}

bool System::focus(ElementHandle element) {
  const ResolvedElement resolved =
      impl_ ? impl_->document_runtime.resolve(element) : ResolvedElement{};
  if (!resolved || resolved.node->disabled ||
      !isVisibleForInteraction(*resolved.node)) {
    return false;
  }
  impl_->setFocus(*resolved.document, resolved.node);
  return true;
}

bool System::scrollTo(ElementHandle element, float x, float y) {
  const ResolvedElement resolved =
      impl_ ? impl_->document_runtime.resolve(element) : ResolvedElement{};
  if (!resolved || !isScrollContainer(*resolved.node)) {
    return false;
  }
  if (!std::isfinite(x) || !std::isfinite(y)) return false;
  const float next_x = std::clamp(x, 0.0f, resolved.node->scroll_max_x);
  const float next_y = std::clamp(y, 0.0f, resolved.node->scroll_max_y);
  if (next_x == resolved.node->scroll_x &&
      next_y == resolved.node->scroll_y) {
    return true;
  }
  const float previous_x = resolved.node->scroll_x;
  const float previous_y = resolved.node->scroll_y;
  resolved.node->scroll_x = next_x;
  resolved.node->scroll_y = next_y;
  impl_->applyScrollPlacement(*resolved.document, *resolved.node, previous_x,
                              previous_y);
  return true;
}

bool System::scrollBy(ElementHandle element, float x, float y) {
  Node* node = impl_ ? impl_->document_runtime.element(element) : nullptr;
  return node != nullptr && scrollTo(element, node->scroll_x + x, node->scroll_y + y);
}

bool System::scrollIntoView(ElementHandle element,
                            ScrollAlignment horizontal,
                            ScrollAlignment vertical) {
  const ResolvedElement resolved =
      impl_ ? impl_->document_runtime.resolve(element) : ResolvedElement{};
  if (!resolved) return false;
  Node* scroller = resolved.node->parent;
  while (scroller != nullptr && !isScrollContainer(*scroller)) {
    scroller = scroller->parent;
  }
  if (scroller == nullptr) return false;

  auto aligned = [](float current,
                    float item_start,
                    float item_size,
                    float viewport_start,
                    float viewport_size,
                    ScrollAlignment alignment) {
    const float item_end = item_start + item_size;
    const float viewport_end = viewport_start + viewport_size;
    switch (alignment) {
      case ScrollAlignment::Start: return current + item_start - viewport_start;
      case ScrollAlignment::Center:
        return current + item_start + item_size * 0.5f -
               (viewport_start + viewport_size * 0.5f);
      case ScrollAlignment::End: return current + item_end - viewport_end;
      case ScrollAlignment::Nearest:
        if (item_start < viewport_start) return current + item_start - viewport_start;
        if (item_end > viewport_end) return current + item_end - viewport_end;
        return current;
    }
    return current;
  };
  return scrollTo(
      scroller->handle,
      aligned(scroller->scroll_x, resolved.node->layout.x,
              resolved.node->layout.width,
              scroller->scroll_viewport.x, scroller->scroll_viewport.width,
              horizontal),
      aligned(scroller->scroll_y, resolved.node->layout.y,
              resolved.node->layout.height,
              scroller->scroll_viewport.y, scroller->scroll_viewport.height,
              vertical));
}

bool System::bringToFront(ElementHandle element) {
  const ResolvedElement resolved =
      impl_ ? impl_->document_runtime.resolve(element) : ResolvedElement{};
  if (!resolved || resolved.node->tag != "window") return false;
  float maximum = 0.0f;
  if (resolved.node->parent != nullptr) {
    forRuntimeChildren(
        *resolved.node->parent,
        [&](const Node& sibling, const Value::Object*) {
          if (sibling.tag == "window") {
            maximum =
                std::max(maximum, styleFloat(sibling, "z-index", 0.0f));
          }
        });
  }
  setInlineStyleProperty(*resolved.node, "z-index",
                         std::to_string(maximum + 1.0f));
  impl_->recordStyleResult(native::style_runtime::restyleNode(
      *resolved.document, resolved.node,
      impl_->styleInputs(*resolved.document)));
  return true;
}

ListenerHandle System::onAction(DocumentHandle document,
                                std::string_view action,
                                ActionCallback callback) {
  if (!impl_ || action.empty() || !callback) return {};
  return impl_->document_runtime.addActionListener(
      document, action, std::move(callback));
}

ListenerHandle System::on(ElementHandle element,
                          EventType type,
                          EventCallback callback,
                          const EventListenerOptions& options) {
  if (!impl_ || !callback) return {};
  return impl_->document_runtime.addElementListener(
      element, type, options.capture, std::move(callback));
}

bool System::removeListener(ListenerHandle listener) {
  return impl_ && impl_->document_runtime.removeListener(listener);
}
void System::setLocale(std::string_view locale) {
  if (!impl_) return;
  const std::string next = trim(locale);
  if (next.empty() || next == impl_->locale) return;
  impl_->locale = next;
  impl_->reported_missing_localizations.clear();
  const auto documents = impl_->document_runtime.allDocuments();
  for (DocumentInstance* document : *documents) {
    impl_->markDirty(*document, true);
  }
}

std::string_view System::locale() const {
  return impl_ ? std::string_view(impl_->locale) : std::string_view{};
}

void System::setLocalizationProvider(LocalizationProvider* provider) {
  if (!impl_ || impl_->localization == provider) return;
  impl_->localization = provider;
  impl_->reported_missing_localizations.clear();
  const auto documents = impl_->document_runtime.allDocuments();
  for (DocumentInstance* document : *documents) {
    impl_->markDirty(*document, true);
  }
}

std::vector<Diagnostic> System::diagnostics(DocumentHandle document) const {
  const DocumentInstance* doc =
      impl_ ? impl_->document_runtime.document(document) : nullptr;
  return doc == nullptr ? std::vector<Diagnostic>{} : doc->diagnostics;
}

void System::setMotionScale(float scale) {
  if (!impl_ || !std::isfinite(scale)) return;
  impl_->config.motion_scale = std::max(0.0f, scale);
  if (impl_->config.motion_scale != 0.0f) return;
  const auto documents = impl_->document_runtime.allDocuments();
  for (DocumentInstance* document : *documents) {
    if (!document->body) continue;
    DocumentInstance& doc = *document;
    const native::style_runtime::MotionResult result =
        native::style_runtime::finishActiveMotion(doc, impl_->clock_seconds);
    doc.layout_revision = result.layout_changed || doc.layout_revision;
    if (result.stacking_changed) doc.overlay_order_revision = true;
  }
}

}  // namespace karma::ui
