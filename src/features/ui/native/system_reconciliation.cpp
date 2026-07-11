#include "features/ui/native/system_impl.h"

#include "features/ui/native/authoring.h"
#include "features/ui/native/computed_style_values.h"
#include "features/ui/native/diagnostics.h"
#include "features/ui/native/document_reconciler.h"
#include "features/ui/native/runtime_dom.h"
#include "features/ui/native/string_utils.h"
#include "features/ui/native/style_runtime.h"
#include "karma/assets.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace karma::ui {
namespace {

using native::addDiagnostic;
using native::computed_style_values::kDefaultFontSize;
using native::computed_style_values::parseLength;
using native::computed_style_values::resolveLength;
using native::jsonNumber;
using native::jsonStyleValue;
using native::runtime_dom::attributeBoolean;
using native::runtime_dom::cloneNode;
using native::runtime_dom::DocumentInstance;
using native::runtime_dom::forRuntimeChildren;
using native::runtime_dom::invalidatePaint;
using native::runtime_dom::invalidatePaintTree;
using native::runtime_dom::invalidateRuntimeChildOrder;
using native::runtime_dom::isVisibleForInteraction;
using native::runtime_dom::isWithin;
using native::runtime_dom::Node;
using native::runtime_dom::styleFloat;
using native::runtime_dom::TemplateInstance;
using native::string_utils::lower;
using native::string_utils::parseFiniteDouble;
using native::string_utils::trim;
using native::style_runtime::setInlineStyleProperty;

}  // namespace

void System::Impl::markDirty(DocumentInstance& doc, bool bindings) {
  doc.binding_revision = doc.binding_revision || bindings;
  doc.style_revision = true;
  doc.layout_revision = true;
  doc.accessibility_revision = true;
  doc.placement_revision = true;
  doc.virtual_range_revision = true;
  doc.overlay_order_revision = true;
  if (doc.body) invalidatePaintTree(*doc.body);
}

void System::Impl::queueModelPath(DocumentInstance& doc, std::string_view path) {
  if (std::find(doc.pending_model_paths.begin(),
                doc.pending_model_paths.end(), path) ==
      doc.pending_model_paths.end()) {
    doc.pending_model_paths.emplace_back(path);
  }
  doc.binding_revision = true;
}

bool System::Impl::setModelFromWidget(DocumentInstance& doc,
                        std::string_view path,
                        Value value) {
  const std::optional<Value> current = bindings.get(doc.model, path);
  if (current.has_value() && *current == value) return false;
  if (!bindings.set(doc.model, path, std::move(value))) return false;
  queueModelPath(doc, path);
  return true;
}

void System::Impl::refreshFeatureFlags(DocumentInstance& doc) {
  native::style_runtime::rebuildDocumentStyleMetadata(doc);
  doc.has_tooltips = false;
  doc.has_virtual_lists = false;
  doc.has_transients = false;
  auto scan = [&](auto&& self, const Node& node) -> void {
    doc.has_tooltips = doc.has_tooltips || node.tag == "tooltip";
    doc.has_virtual_lists = doc.has_virtual_lists || node.tag == "list";
    doc.has_transients = doc.has_transients || node.tag == "select" ||
                         node.tag == "popup" || node.tag == "menu" ||
                         node.tag == "tooltip";
    for (const auto& child : node.children) self(self, *child);
  };
  if (doc.body) scan(scan, *doc.body);
}

void System::Impl::refreshTemplate(DocumentInstance& doc,
                     Node& node,
                     const Value::Object* outer_locals) {
  const auto loop_attribute = node.attributes.find("k-for");
  if (loop_attribute == node.attributes.end()) return;
  const std::string expression = trim(loop_attribute->second);
  const std::size_t in = expression.find(" in ");
  if (in == expression.npos) return;
  const std::string variable = trim(std::string_view(expression).substr(0, in));
  const std::string collection_expression = trim(std::string_view(expression).substr(in + 4));
  const native::BindingEvaluationContext context{
      .model = &doc.model, .locals = outer_locals};
  const auto collection_value = bindings.evaluate(collection_expression, context);
  const Value::Array* collection = collection_value ? collection_value->asArray() : nullptr;

  Node* virtual_list =
      node.parent != nullptr && node.parent->tag == "list" ? node.parent : nullptr;
  std::size_t first_index = 0u;
  std::size_t last_index = collection == nullptr ? 0u : collection->size();
  float item_extent = 0.0f;
  if (virtual_list != nullptr) {
    item_extent = std::max(
        1.0f, styleFloat(*virtual_list, "virtual-item-extent",
                         styleFloat(*virtual_list, "item-extent", 0.0f)));
    if (const auto found = virtual_list->attributes.find("item_extent");
        found != virtual_list->attributes.end()) {
      const std::optional<double> parsed_extent =
          parseFiniteDouble(found->second);
      if (parsed_extent.has_value() && *parsed_extent > 0.0) {
        item_extent = static_cast<float>(*parsed_extent);
      }
    }
    if (item_extent <= 1.0f) item_extent = 32.0f;
    std::size_t overscan = 1u;
    if (const auto found = virtual_list->attributes.find("overscan");
        found != virtual_list->attributes.end()) {
      const std::optional<double> parsed_overscan =
          parseFiniteDouble(found->second);
      if (parsed_overscan.has_value() && *parsed_overscan >= 0.0) {
        overscan = static_cast<std::size_t>(
            std::min(*parsed_overscan, 64.0));
      }
    }
    float authored_height = styleFloat(*virtual_list, "height", 0.0f);
    if (authored_height <= 0.0f) {
      if (const auto height = virtual_list->inline_style.find("height");
          height != virtual_list->inline_style.end()) {
          authored_height = resolveLength(
              parseLength(height->second), item_extent,
              static_cast<float>(std::max(1, logical_width)),
              static_cast<float>(std::max(1, logical_height)),
              kDefaultFontSize, kDefaultFontSize, 0.0f);
      }
    }
    const float viewport_height =
        virtual_list->scroll_viewport.height > 0.0f
            ? virtual_list->scroll_viewport.height
            : (virtual_list->layout.height > 0.0f
                   ? virtual_list->layout.height
                   : std::max(item_extent, authored_height));
    const std::size_t visible_begin = static_cast<std::size_t>(
        std::max(0.0f, std::floor(virtual_list->scroll_y / item_extent)));
    const std::size_t visible_count = static_cast<std::size_t>(
        std::max(1.0f, std::ceil(viewport_height / item_extent)));
    first_index = visible_begin > overscan ? visible_begin - overscan : 0u;
    last_index = collection == nullptr
                     ? 0u
                     : std::min(collection->size(),
                                visible_begin + visible_count + overscan);
    virtual_list->virtual_total_count =
        collection == nullptr ? 0u : collection->size();
    virtual_list->virtual_first_index = first_index;
    virtual_list->virtual_last_index = last_index;
    virtual_list->virtual_item_extent = item_extent;
  }

  std::unordered_map<std::string, TemplateInstance> previous;
  for (TemplateInstance& instance : node.instances) {
    previous.emplace(instance.key, std::move(instance));
  }
  node.instances.clear();
  if (collection == nullptr) {
    if (virtual_list != nullptr) {
      virtual_list->virtual_total_count = 0u;
      virtual_list->virtual_first_index = 0u;
      virtual_list->virtual_last_index = 0u;
    }
    for (auto& [key, instance] : previous) {
      for (auto& child : instance.children) document_runtime.releaseTree(doc, *child);
    }
    invalidateRuntimeChildOrder(node.parent != nullptr ? *node.parent : node);
    doc.overlay_order_revision = true;
    return;
  }
  std::unordered_set<std::string> seen_keys;
  for (std::size_t index = first_index; index < last_index; ++index) {
    Value::Object locals = outer_locals == nullptr ? Value::Object{} : *outer_locals;
    locals[variable] = (*collection)[index];
    locals[variable + "_index"] = static_cast<Value::Integer>(index);
    std::string key = std::to_string(index);
    if (const auto key_attribute = node.attributes.find("k-key");
        key_attribute != node.attributes.end()) {
      const native::BindingEvaluationContext key_context{
          .model = &doc.model, .locals = &locals};
      if (auto evaluated = bindings.evaluate(key_attribute->second, key_context)) {
        key = evaluated->toString();
      }
    }
    if (!seen_keys.insert(key).second) continue;
    TemplateInstance instance;
    if (auto found = previous.find(key); found != previous.end()) {
      instance = std::move(found->second);
      previous.erase(found);
    } else {
      instance.key = key;
      for (const auto& prototype : node.children) {
        instance.children.push_back(
            cloneNode(*prototype, node.parent, node.identity + "[" + key + "]"));
      }
      for (auto& child : instance.children) document_runtime.allocateTree(doc, *child);
    }
    instance.locals = std::move(locals);
    if (virtual_list != nullptr) {
      for (auto& child : instance.children) {
        setInlineStyleProperty(*child, "position", "absolute");
        setInlineStyleProperty(*child, "left", "0");
        setInlineStyleProperty(*child, "top",
                               jsonNumber(static_cast<double>(index) *
                                          item_extent));
        setInlineStyleProperty(*child, "width", "100%");
        setInlineStyleProperty(*child, "height", jsonNumber(item_extent));
        setInlineStyleProperty(*child, "flex-shrink", "0");
      }
    }
    node.instances.push_back(std::move(instance));
  }
  for (auto& [key, instance] : previous) {
    for (auto& child : instance.children) document_runtime.releaseTree(doc, *child);
  }
  invalidateRuntimeChildOrder(node.parent != nullptr ? *node.parent : node);
  doc.overlay_order_revision = true;
}

void System::Impl::refreshNode(DocumentInstance& doc,
                 Node& node,
                 const Value::Object* locals) {
  ++pending_frame_diagnostics.reconciled_nodes;
  const bool previous_present = node.present;
  const bool previous_collapsed_hidden = node.collapsed_hidden;
  const bool previous_open = attributeBoolean(node, "open");
  auto invalidate_overlay_membership = [&] {
    if (previous_present != node.present ||
        previous_collapsed_hidden != node.collapsed_hidden ||
        previous_open != attributeBoolean(node, "open")) {
      doc.overlay_order_revision = true;
    }
  };
  const native::BindingEvaluationContext context{
      .model = &doc.model, .locals = locals};
  bool should_present = true;
  if (const auto condition = node.attributes.find("k-if");
      condition != node.attributes.end()) {
    const auto evaluated = bindings.evaluate(condition->second, context);
    should_present = evaluated.has_value() && evaluated->truthy();
  }
  if (should_present != node.present) {
    node.present = should_present;
    if (should_present) document_runtime.allocateTree(doc, node);
    else document_runtime.releaseTree(doc, node);
  }
  if (!node.present) {
    invalidate_overlay_membership();
    return;
  }

  node.disabled = attributeBoolean(node, "disabled");
  node.checked = attributeBoolean(node, "checked");
  if (const auto disabled = node.attributes.find("bind-disabled");
      disabled != node.attributes.end()) {
    if (auto value = bindings.evaluate(disabled->second, context)) {
      node.disabled = value->truthy();
    }
  }
  if (const auto checked = node.attributes.find("bind-checked"); checked != node.attributes.end()) {
    if (auto value = bindings.evaluate(checked->second, context)) {
      node.checked = value->truthy();
    }
  }
  for (const std::string state : {"expanded", "selected", "open", "collapsed"}) {
    if (const auto binding = node.attributes.find("bind-" + state);
        binding != node.attributes.end()) {
      const bool enabled = bindings.evaluate(binding->second, context)
                               .value_or(Value{})
                               .truthy();
      node.attributes[state] = enabled ? "true" : "false";
    }
  }
  if (const auto binding = node.attributes.find("bind-value"); binding != node.attributes.end()) {
    node.control_value = bindings.evaluate(binding->second, context).value_or(Value{});
    if (node.tag == "toggle") node.checked = node.control_value.truthy();
  }
  if (const auto binding = node.attributes.find("bind-title");
      binding != node.attributes.end()) {
    node.title = bindings.evaluate(binding->second, context)
                     .value_or(Value{})
                     .toString();
  }
  if (node.tag == "window") {
    if (const auto binding = node.attributes.find("bind-window-state");
        binding != node.attributes.end()) {
      const auto state = bindings.evaluate(binding->second, context);
      const Value::Object* object = state ? state->asObject() : nullptr;
      if (object != nullptr) {
        auto vector2 = [&](std::string_view name)
            -> std::optional<std::pair<float, float>> {
          const auto found = object->find(std::string(name));
          const Value::Array* values =
              found == object->end() ? nullptr : found->second.asArray();
          if (values == nullptr || values->size() != 2u) return std::nullopt;
          const auto first = (*values)[0].asNumber();
          const auto second = (*values)[1].asNumber();
          if (!first || !second || !std::isfinite(*first) || !std::isfinite(*second)) {
            return std::nullopt;
          }
          return std::pair{static_cast<float>(*first),
                           static_cast<float>(*second)};
        };
        if (const auto position = vector2("position")) {
          setInlineStyleProperty(node, "left", jsonNumber(position->first));
          setInlineStyleProperty(node, "top", jsonNumber(position->second));
        }
        if (const auto size = vector2("size")) {
          setInlineStyleProperty(node, "width", jsonNumber(size->first));
          setInlineStyleProperty(node, "height", jsonNumber(size->second));
        }
        for (const std::string name : {"open", "collapsed"}) {
          if (const auto found = object->find(name); found != object->end()) {
            node.attributes[name] = found->second.truthy() ? "true" : "false";
          }
        }
        if (const auto found = object->find("z"); found != object->end()) {
          if (const auto z = found->second.asNumber()) {
            setInlineStyleProperty(node, "z-index", jsonNumber(*z));
          }
        }
      }
    }
  }

  if (node.programmatic_text) {
    node.text = node.source_text;
  } else if (const auto binding = node.attributes.find("bind-text");
             binding != node.attributes.end()) {
    node.text = bindings.evaluate(binding->second, context)
                    .value_or(Value{})
                    .toString();
  } else if (const auto localization_key = node.attributes.find("loc");
      localization_key != node.attributes.end()) {
    Value::Object arguments;
    for (const auto& [name, expression] : node.attributes) {
      if (!name.starts_with("loc-arg-")) continue;
      arguments[name.substr(8)] =
          bindings.evaluate(expression, context).value_or(Value{});
    }
    std::optional<std::string> translated;
    if (localization != nullptr) {
      translated = localization->localize(locale, localization_key->second, arguments);
    }
    node.text = translated.value_or(localization_key->second);
    if (!translated.has_value()) {
      const std::string missing = locale + "\n" + localization_key->second;
      if (reported_missing_localizations.insert(missing).second) {
        addDiagnostic(doc.diagnostics, doc.asset_key, "UI_MISSING_LOCALIZATION",
                      "missing localization key: " + localization_key->second,
                      0, DiagnosticSeverity::Warning);
      }
    }
  } else {
    node.text = bindings.interpolate(node.source_text, context);
  }

  for (auto& child : node.children) {
    if (child->template_node) {
      refreshTemplate(doc, *child, locals);
      for (TemplateInstance& instance : child->instances) {
        for (auto& repeated : instance.children) refreshNode(doc, *repeated, &instance.locals);
      }
    } else {
      refreshNode(doc, *child, locals);
    }
  }
  if (node.tag == "disclosure") {
    const bool expanded = attributeBoolean(node, "expanded");
    for (std::size_t index = 0; index < node.children.size(); ++index) {
      node.children[index]->collapsed_hidden = !expanded && index > 0u;
    }
    Node* focused = document_runtime.element(doc.focused);
    if (!expanded && focused != nullptr && focused != &node &&
        isWithin(focused, &node)) {
      setFocus(doc, &node);
    }
    doc.overlay_order_revision = true;
  } else if (node.tag == "tree-item") {
    const bool expanded = attributeBoolean(node, "expanded");
    for (auto& child : node.children) {
      if (child->tag == "tree-item") {
        child->collapsed_hidden = !expanded;
      }
    }
    Node* focused = document_runtime.element(doc.focused);
    if (!expanded && focused != nullptr && focused != &node &&
        isWithin(focused, &node)) {
      setFocus(doc, &node);
    }
    doc.overlay_order_revision = true;
  } else if (node.tag == "window") {
    node.collapsed_hidden = node.attributes.contains("open") &&
                            !attributeBoolean(node, "open");
    const bool collapsed = attributeBoolean(node, "collapsed");
    for (auto& child : node.children) child->collapsed_hidden = collapsed;
    Node* focused = document_runtime.element(doc.focused);
    if ((node.collapsed_hidden || collapsed) && focused != nullptr &&
        isWithin(focused, &node)) {
      setFocus(doc, node.collapsed_hidden ? nullptr : &node);
    }
    doc.overlay_order_revision = true;
  } else if (node.tag == "popup" || node.tag == "menu") {
    node.collapsed_hidden = !attributeBoolean(node, "open");
    Node* focused = document_runtime.element(doc.focused);
    if (node.collapsed_hidden && focused != nullptr &&
        isWithin(focused, &node)) {
      Node* anchor = transientAnchor(doc, node);
      setFocus(doc, anchor != nullptr && isVisibleForInteraction(*anchor)
                        ? anchor
                        : nullptr);
    }
  } else if (node.tag == "tooltip") {
    node.collapsed_hidden = true;
  }
  if (node.tag == "select") {
    const bool open = attributeBoolean(node, "open");
    forRuntimeChildren(node, [&](Node& option, const Value::Object*) {
      if (option.tag != "option") return;
      const auto value = option.attributes.find("value");
      const Value option_value = value == option.attributes.end() ? Value(option.text)
                                                                   : Value(value->second);
      option.checked = option_value == node.control_value;
      option.collapsed_hidden = !open;
    });
    Node* focused = document_runtime.element(doc.focused);
    if (!open && focused != nullptr && focused != &node &&
        isWithin(focused, &node)) {
      setFocus(doc, &node);
    }
  }
  if (node.tag == "tabs" || node.tag == "tree") {
    auto item_value = [](const Node& item) {
      if (const auto value = item.attributes.find("value");
          value != item.attributes.end()) {
        return Value(value->second);
      }
      if (!item.id.empty()) return Value(item.id);
      return Value(item.text);
    };
    std::vector<Node*> items;
    std::function<void(Node&)> collect = [&](Node& current) {
      forRuntimeChildren(current, [&](Node& child, const Value::Object*) {
        if ((node.tag == "tabs" && child.tag == "tab") ||
            (node.tag == "tree" && child.tag == "tree-item")) {
          items.push_back(&child);
        }
        if (node.tag == "tree") collect(child);
      });
    };
    collect(node);
    Node* selected = nullptr;
    for (Node* item : items) {
      if (item_value(*item) == node.control_value) {
        selected = item;
        break;
      }
    }
    if (selected == nullptr) {
      const auto authored = std::find_if(items.begin(), items.end(), [](Node* item) {
        return attributeBoolean(*item, "selected") && !item->disabled;
      });
      if (authored != items.end()) selected = *authored;
    }
    if (selected == nullptr && !items.empty()) {
      const auto first_enabled = std::find_if(
          items.begin(), items.end(), [](Node* item) { return !item->disabled; });
      if (first_enabled != items.end()) selected = *first_enabled;
    }
    if (selected != nullptr) {
      node.control_value = item_value(*selected);
      if (const auto binding = node.attributes.find("bind-value");
          binding != node.attributes.end()) {
        setModelFromWidget(doc, binding->second, node.control_value);
      }
    }
    for (Node* item : items) {
      item->attributes["selected"] = item == selected ? "true" : "false";
    }
  }
  if (node.parent == nullptr) {
    Node* focused = document_runtime.element(doc.focused);
    if (focused != nullptr && !isVisibleForInteraction(*focused)) {
      setFocus(doc, nullptr);
    }
  }
  invalidate_overlay_membership();
}

// Full-tree reconciliation is used for initial load, hot reload, and the
// rare case where a tabs/tree binding must choose a valid authored default.
// A second pass makes earlier dependents observe that default. The cap also
// keeps two incompatible controls bound to the same path deterministic.
void System::Impl::refreshBindingsFully(DocumentInstance& doc) {
  if (!doc.body) return;
  doc.pending_model_paths.clear();
  refreshNode(doc, *doc.body, nullptr);
  if (!doc.pending_model_paths.empty()) {
    doc.pending_model_paths.clear();
    refreshNode(doc, *doc.body, nullptr);
  }
  doc.pending_model_paths.clear();
  doc.binding_revision = false;
}

bool System::Impl::refreshVirtualLists(DocumentInstance& doc,
                         Node& node,
                         const Value::Object* locals) {
  bool changed = false;
  if (node.tag == "list") {
    for (auto& child : node.children) {
      if (!child->template_node) continue;
      std::vector<std::string> previous_keys;
      previous_keys.reserve(child->instances.size());
      for (const TemplateInstance& instance : child->instances) {
        previous_keys.push_back(instance.key);
      }
      const std::size_t previous_total = node.virtual_total_count;
      const std::size_t previous_first = node.virtual_first_index;
      const std::size_t previous_last = node.virtual_last_index;
      refreshTemplate(doc, *child, locals);
      for (TemplateInstance& instance : child->instances) {
        for (auto& repeated : instance.children) {
          refreshNode(doc, *repeated, &instance.locals);
        }
      }
      std::vector<std::string> next_keys;
      next_keys.reserve(child->instances.size());
      for (const TemplateInstance& instance : child->instances) {
        next_keys.push_back(instance.key);
      }
      changed = changed || previous_keys != next_keys ||
                previous_total != node.virtual_total_count ||
                previous_first != node.virtual_first_index ||
                previous_last != node.virtual_last_index;
    }
  }
  for (auto& child : node.children) {
    if (child->template_node) {
      if (node.tag == "list") continue;
      for (TemplateInstance& instance : child->instances) {
        for (auto& repeated : instance.children) {
          changed = refreshVirtualLists(doc, *repeated, &instance.locals) ||
                    changed;
        }
      }
    } else {
      changed = refreshVirtualLists(doc, *child, locals) || changed;
    }
  }
  return changed;
}
native::style_runtime::StyleInputs System::Impl::styleInputs(
    const DocumentInstance& doc) const {
  return {
      .assets = *assets,
      .viewport_width = doc.canvas_layout.layout_rect.width > 0.0f
                            ? doc.canvas_layout.layout_rect.width
                            : static_cast<float>(logical_width),
      .viewport_height = doc.canvas_layout.layout_rect.height > 0.0f
                             ? doc.canvas_layout.layout_rect.height
                             : static_cast<float>(logical_height),
      .now_seconds = clock_seconds,
      .motion_scale = config.motion_scale,
  };
}

void System::Impl::recordStyleResult(const native::style_runtime::StyleResult& result) {
  pending_frame_diagnostics.restyled_nodes += result.restyled_nodes;
}
void System::Impl::reconcileModelPaths(
    DocumentInstance& doc,
    const std::vector<std::string>& changed_paths) {
  if (!doc.body || reconciling_models) return;
  reconciling_models = true;
  if (doc.dependency_index_revision) {
    native::reconciler::rebuildBindingDependencies(
        *doc.body, bindings, doc.binding_dependencies);
    doc.dependency_index_revision = false;
  }
  const native::reconciler::AffectedOwners selection =
      native::reconciler::selectAffectedOwners(doc.binding_dependencies,
                                                changed_paths);
  std::vector<Node*> affected;
  affected.reserve(selection.owners.size());
  for (void* owner : selection.owners) {
    affected.push_back(static_cast<Node*>(owner));
  }

  if (selection.repeat_owner_affected) {
    refreshBindingsFully(doc);
    doc.dependency_index_revision = true;
    recordStyleResult(
        native::style_runtime::styleDocument(doc, styleInputs(doc)));
    doc.layout_revision = true;
    doc.accessibility_revision = true;
    doc.placement_revision = true;
    doc.virtual_range_revision = true;
    invalidatePaintTree(*doc.body);
    reconciling_models = false;
    return;
  }

  std::sort(affected.begin(), affected.end(), [](const Node* left,
                                                  const Node* right) {
    auto depth = [](const Node* node) {
      std::size_t result = 0u;
      for (; node != nullptr; node = node->parent) ++result;
      return result;
    };
    return depth(left) < depth(right);
  });
  std::vector<Node*> topmost;
  for (Node* node : affected) {
    const bool covered = std::any_of(
        topmost.begin(), topmost.end(), [&](const Node* possible_parent) {
          for (const Node* current = node->parent; current != nullptr;
               current = current->parent) {
            if (current == possible_parent) return true;
          }
          return false;
        });
    if (!covered) topmost.push_back(node);
  }
  affected = std::move(topmost);

  for (Node* node : affected) {
    const bool was_present = node->present;
    const bool was_disabled = node->disabled;
    const bool was_checked = node->checked;
    const std::string previous_text = node->text;
    const std::string previous_title = node->title;
    const Value previous_control = node->control_value;
    const auto previous_attributes = node->attributes;
    refreshNode(doc, *node, nullptr);
    const bool paint_control_only =
        (node->tag == "progress" || node->tag == "slider") && was_present &&
        node->present && was_disabled == node->disabled &&
        was_checked == node->checked && previous_text == node->text &&
        previous_title == node->title &&
        previous_attributes == node->attributes;
    if (paint_control_only) {
      invalidatePaint(node);
      doc.accessibility_revision = true;
      continue;
    }
    native::style_runtime::StyleResult style_result;
    if (node->present) {
      style_result = native::style_runtime::styleSubtree(
          doc, *node, styleInputs(doc));
      recordStyleResult(style_result);
    }
    const bool layout_style_changed = style_result.layout_changed;
    doc.layout_revision = doc.layout_revision || layout_style_changed ||
                       was_present != node->present ||
                       previous_text != node->text ||
                       previous_title != node->title;
    doc.font_revision = true;
    doc.accessibility_revision = true;
    doc.placement_revision = true;
    invalidatePaint(node);
    if (was_present != node->present) doc.dependency_index_revision = true;
  }
  if (!doc.pending_model_paths.empty()) {
    // Selection fallback wrote through the same model path as public/widget
    // updates. Reconcile the resulting value before the initiating set()
    // returns; this path is structural and intentionally falls back to a full
    // pass rather than leaving an internally generated update queued.
    refreshBindingsFully(doc);
    doc.dependency_index_revision = true;
    recordStyleResult(
        native::style_runtime::styleDocument(doc, styleInputs(doc)));
    doc.layout_revision = true;
    doc.accessibility_revision = true;
    doc.placement_revision = true;
    doc.virtual_range_revision = true;
    invalidatePaintTree(*doc.body);
  }
  doc.binding_revision = false;
  reconciling_models = false;
}

}  // namespace karma::ui
