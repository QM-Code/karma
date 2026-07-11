#include "features/ui/native/style_runtime.h"

#include "features/ui/native/asset_reference.h"
#include "features/ui/native/authoring.h"
#include "features/ui/native/computed_style_values.h"
#include "features/ui/native/font_face.h"
#include "features/ui/native/motion_engine.h"
#include "features/ui/native/runtime_dom.h"
#include "features/ui/native/string_utils.h"
#include "karma/assets.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cctype>
#include <limits>
#include <memory>
#include <optional>
#include <unordered_set>
#include <utility>

namespace karma::ui::native {
namespace {

bool parseSelectorCompound(std::string_view text,
                           SelectorCompound& output) {
  if (text.empty()) return false;
  std::size_t cursor = 0u;
  if (text[cursor] != '.' && text[cursor] != '#' && text[cursor] != ':') {
    const std::size_t begin = cursor;
    while (cursor < text.size() && text[cursor] != '.' &&
           text[cursor] != '#' && text[cursor] != ':') {
      ++cursor;
    }
    output.tag = std::string(text.substr(begin, cursor - begin));
    if (output.tag.empty()) return false;
  }
  while (cursor < text.size()) {
    const char kind = text[cursor++];
    const std::size_t begin = cursor;
    while (cursor < text.size() && text[cursor] != '.' &&
           text[cursor] != '#' && text[cursor] != ':') {
      ++cursor;
    }
    if (begin == cursor) return false;
    std::string value(text.substr(begin, cursor - begin));
    if (kind == '#') {
      output.id = std::move(value);
    } else if (kind == '.') {
      output.classes.push_back(std::move(value));
    } else if (kind == ':') {
      output.pseudos.push_back(std::move(value));
    } else {
      return false;
    }
  }
  return true;
}

}  // namespace

CompiledSelector compileSelector(std::string_view selector) {
  CompiledSelector output;
  std::size_t cursor = 0u;
  const auto skipWhitespace = [&]() {
    const std::size_t before = cursor;
    while (cursor < selector.size() &&
           std::isspace(static_cast<unsigned char>(selector[cursor])) != 0) {
      ++cursor;
    }
    return cursor != before;
  };

  skipWhitespace();
  while (cursor < selector.size()) {
    const std::size_t begin = cursor;
    while (cursor < selector.size() && selector[cursor] != '>' &&
           std::isspace(static_cast<unsigned char>(selector[cursor])) == 0) {
      ++cursor;
    }
    SelectorCompound compound;
    if (!parseSelectorCompound(selector.substr(begin, cursor - begin),
                               compound)) {
      return output;
    }
    output.compounds.push_back(std::move(compound));

    const bool had_space = skipWhitespace();
    if (cursor >= selector.size()) break;
    if (selector[cursor] == '>') {
      output.combinators.push_back('>');
      ++cursor;
      skipWhitespace();
      if (cursor >= selector.size() || selector[cursor] == '>') return output;
    } else if (had_space) {
      output.combinators.push_back(' ');
    } else {
      return output;
    }
  }
  output.valid = !output.compounds.empty() &&
                 output.combinators.size() + 1u == output.compounds.size();
  return output;
}

void StyleRuleCandidateIndex::clear() {
  universal.clear();
  by_tag.clear();
  by_id.clear();
  by_class.clear();
}

void rebuildStyleRuleCandidates(std::vector<StyleRule>& rules,
                                StyleRuleCandidateIndex& index) {
  index.clear();
  for (std::size_t rule_index = 0u; rule_index < rules.size(); ++rule_index) {
    StyleRule& rule = rules[rule_index];
    if (!rule.compiled_selector.valid) {
      rule.compiled_selector = compileSelector(rule.selector);
    }
    if (!rule.compiled_selector.valid ||
        rule.compiled_selector.compounds.empty()) {
      index.universal.push_back(rule_index);
      continue;
    }
    const SelectorCompound& rightmost =
        rule.compiled_selector.compounds.back();
    if (!rightmost.id.empty()) {
      index.by_id[rightmost.id].push_back(rule_index);
    } else if (!rightmost.classes.empty()) {
      // Indexing one required class is sufficient. The matcher still verifies
      // every class, pseudo, and ancestor compound.
      index.by_class[rightmost.classes.front()].push_back(rule_index);
    } else if (!rightmost.tag.empty() && rightmost.tag != "*") {
      index.by_tag[rightmost.tag].push_back(rule_index);
    } else {
      index.universal.push_back(rule_index);
    }
  }
}

namespace {

using computed_style_values::kDefaultFontSize;
using computed_style_values::Length;
using computed_style_values::nodeFontSize;
using computed_style_values::parseLength;
using computed_style_values::splitCommaList;
using computed_style_values::splitWhitespace;
using computed_style_values::Unit;
using runtime_dom::AnimationState;
using runtime_dom::DocumentInstance;
using runtime_dom::forRuntimeChildren;
using runtime_dom::invalidatePaint;
using runtime_dom::invalidateRuntimeChildOrder;
using runtime_dom::Node;
using runtime_dom::styleString;
using style_runtime::StyleResult;
using string_utils::lower;
using string_utils::trim;
using string_utils::unquote;

struct StyleValue {
  std::string value;
  int specificity = 0;
  int style_position = -1;
  std::size_t order = 0u;
};

constexpr std::array<std::string_view, 10> kInheritedProperties = {
    "color",       "font-family", "font-size", "font-weight",
    "font-style",  "line-height", "letter-spacing", "text-align",
    "direction",   "locale"};

bool matchesSimple(const Node& node, const SelectorCompound& simple) {
  if (!simple.tag.empty() && simple.tag != "*" && simple.tag != node.tag) {
    return false;
  }
  if (!simple.id.empty() && simple.id != node.id) return false;
  for (const std::string& class_name : simple.classes) {
    if (!node.classes.contains(class_name)) return false;
  }
  for (const std::string& pseudo : simple.pseudos) {
    if (pseudo == "hover" && !node.hovered) return false;
    if (pseudo == "active" && !node.active) return false;
    if (pseudo == "focus" && !node.focused) return false;
    if (pseudo == "disabled" && !node.disabled) return false;
    if (pseudo == "checked" && !node.checked) return false;
    const auto truthy_attribute = [&](std::string_view name) {
      const auto found = node.attributes.find(std::string(name));
      if (found == node.attributes.end()) return false;
      const std::string value = lower(trim(found->second));
      return value.empty() || value == "true" || value == "1" ||
             value == name;
    };
    if (pseudo == "selected" && !truthy_attribute("selected") &&
        !node.checked) {
      return false;
    }
    if (pseudo == "expanded" && !truthy_attribute("expanded")) return false;
    if (pseudo == "root" && node.parent != nullptr) return false;
    if (pseudo != "hover" && pseudo != "active" && pseudo != "focus" &&
        pseudo != "disabled" && pseudo != "checked" &&
        pseudo != "selected" && pseudo != "expanded" && pseudo != "root") {
      return false;
    }
  }
  return true;
}

bool matchesSelector(const Node& node, const CompiledSelector& selector) {
  if (!selector.valid || selector.compounds.empty()) return false;
  const Node* current = &node;
  std::size_t index = selector.compounds.size() - 1u;
  if (!matchesSimple(*current, selector.compounds[index])) return false;
  while (index > 0u) {
    const char combinator = selector.combinators[index - 1u];
    --index;
    if (combinator == '>') {
      current = current->parent;
      if (current == nullptr ||
          !matchesSimple(*current, selector.compounds[index])) {
        return false;
      }
    } else {
      current = current->parent;
      while (current != nullptr &&
             !matchesSimple(*current, selector.compounds[index])) {
        current = current->parent;
      }
      if (current == nullptr) return false;
    }
  }
  return true;
}

std::string resolveVariables(
    std::string value,
    const std::unordered_map<std::string, std::string>& properties,
    int depth = 0) {
  if (depth > 16) return {};
  std::size_t begin = value.find("var(");
  while (begin != std::string::npos) {
    const std::size_t end = value.find(')', begin + 4u);
    if (end == std::string::npos) return {};
    const auto parts = splitCommaList(
        std::string_view(value).substr(begin + 4u, end - begin - 4u));
    if (parts.empty()) return {};
    const std::string name = trim(parts[0]);
    std::string replacement;
    if (const auto found = properties.find(name); found != properties.end()) {
      replacement = resolveVariables(found->second, properties, depth + 1);
    } else if (parts.size() > 1u) {
      replacement = resolveVariables(parts[1], properties, depth + 1);
    } else {
      return {};
    }
    value.replace(begin, end + 1u - begin, replacement);
    begin = value.find("var(", begin + replacement.size());
  }
  return value;
}

std::string defaultStyle(std::string_view tag, std::string_view property) {
  if (property == "display") return tag == "template" ? "none" : "block";
  if (property == "box-sizing") return "border-box";
  if (property == "font-size") return "16px";
  if (property == "line-height") return "1.2";
  if (property == "color") return "#ffffff";
  if (property == "opacity") return "1";
  if (property == "position") return tag == "window" ? "absolute" : "relative";
  if (property == "overflow") {
    return tag == "scroll" || tag == "list" ? "auto" : "visible";
  }
  if (property == "pointer-events") return tag == "tooltip" ? "none" : "auto";
  if (property == "width") {
    if (tag == "body" || tag == "div" || tag == "scroll" || tag == "list") {
      return "100%";
    }
    if (tag == "button" || tag == "select") return "160px";
    if (tag == "slider" || tag == "progress") return "200px";
    if (tag == "toggle") return "24px";
  }
  if (property == "height") {
    if (tag == "body") return "100%";
    if (tag == "button" || tag == "select") return "40px";
    if (tag == "slider" || tag == "progress") return "20px";
    if (tag == "toggle") return "24px";
  }
  if (property == "background-color") {
    if (tag == "button" || tag == "select") return "#344054";
    if (tag == "toggle" || tag == "slider" || tag == "progress") {
      return "#202632";
    }
    return "transparent";
  }
  if (property == "padding") {
    return tag == "button" || tag == "select" ? "8px" : "0px";
  }
  return {};
}

void applyDefaultStyles(Node& node) {
  static constexpr std::array<std::string_view, 14> properties = {
      "display", "box-sizing", "font-size", "line-height", "color",
      "opacity", "position", "overflow", "pointer-events", "width",
      "height", "background-color", "padding", "margin"};
  for (const std::string_view property : properties) {
    const std::string value = defaultStyle(node.tag, property);
    if (!value.empty()) node.style[std::string(property)] = value;
  }
}

void inheritStyles(Node& node, const Node* parent) {
  if (parent == nullptr) return;
  for (const std::string_view property : kInheritedProperties) {
    if (const auto found = parent->style.find(std::string(property));
        found != parent->style.end()) {
      node.style[std::string(property)] = found->second;
    }
  }
  for (const auto& [name, value] : parent->style) {
    if (name.starts_with("--")) node.style[name] = value;
  }
}

void computeStyle(Node& node,
                  const std::vector<StyleRule>& rules,
                  const StyleRuleCandidateIndex& candidates) {
  std::unordered_map<std::string, StyleValue> cascade;
  node.style.clear();
  applyDefaultStyles(node);
  inheritStyles(node, node.parent);
  for (const auto& [name, value] : node.style) {
    cascade[name] = {.value = value, .specificity = -1, .order = 0u};
  }
  const auto apply_rule = [&](std::size_t rule_index) {
    const StyleRule& rule = rules[rule_index];
    if (!matchesSelector(node, rule.compiled_selector)) return;
    int style_position = -1;
    if (!rule.style_name.empty()) {
      const auto found = std::find(node.style_names.begin(),
                                   node.style_names.end(), rule.style_name);
      if (found != node.style_names.end()) {
        style_position = static_cast<int>(found - node.style_names.begin());
      }
    }
    for (const auto& [name, value] : rule.declarations) {
      StyleValue& existing = cascade[name];
      if (rule.specificity > existing.specificity ||
          (rule.specificity == existing.specificity &&
           (style_position > existing.style_position ||
            (style_position == existing.style_position &&
             rule.order >= existing.order)))) {
        existing = {.value = value,
                    .specificity = rule.specificity,
                    .style_position = style_position,
                    .order = rule.order};
      }
    }
  };
  const auto apply_bucket = [&](const auto& map, const std::string& key) {
    if (const auto found = map.find(key); found != map.end()) {
      for (const std::size_t rule_index : found->second) {
        apply_rule(rule_index);
      }
    }
  };
  for (const std::size_t rule_index : candidates.universal) {
    apply_rule(rule_index);
  }
  apply_bucket(candidates.by_tag, node.tag);
  if (!node.id.empty()) apply_bucket(candidates.by_id, node.id);
  for (const std::string& class_name : node.classes) {
    apply_bucket(candidates.by_class, class_name);
  }
  for (const auto& [name, value] : node.inline_style) {
    cascade[name] = {.value = value,
                     .specificity = 1000,
                     .order = std::numeric_limits<std::size_t>::max()};
  }
  node.style.clear();
  for (const auto& [name, value] : cascade) node.style[name] = value.value;
  for (auto& [name, value] : node.style) {
    if (!name.starts_with("--")) value = resolveVariables(value, node.style);
  }
}

void resolveComputedFontSize(Node& node,
                             float viewport_width,
                             float viewport_height) {
  const float parent_size = node.parent == nullptr ? kDefaultFontSize
                                                   : nodeFontSize(*node.parent);
  float root_size = kDefaultFontSize;
  if (node.parent != nullptr) {
    const Node* root = node.parent;
    while (root->parent != nullptr) root = root->parent;
    root_size = nodeFontSize(*root);
  }
  const auto found = node.style.find("font-size");
  const Length length = found == node.style.end()
                            ? Length{parent_size, Unit::Px}
                            : parseLength(found->second);
  float resolved = parent_size;
  switch (length.unit) {
    case Unit::Auto: resolved = parent_size; break;
    case Unit::Px: resolved = length.value; break;
    case Unit::Percent: resolved = parent_size * length.value * 0.01f; break;
    case Unit::Vw: resolved = viewport_width * length.value * 0.01f; break;
    case Unit::Vh: resolved = viewport_height * length.value * 0.01f; break;
    case Unit::Em: resolved = parent_size * length.value; break;
    case Unit::Rem: resolved = root_size * length.value; break;
    case Unit::Fr: resolved = parent_size; break;
  }
  if (!std::isfinite(resolved)) resolved = parent_size;
  node.style["font-size"] =
      std::to_string(std::max(1.0f, resolved)) + "px";
}

std::vector<TransitionSpec> nodeTransitionSpecs(const Node& node) {
  std::vector<TransitionSpec> specs;
  if (const auto shorthand = node.style.find("transition");
      shorthand != node.style.end()) {
    if (!parseTransitionShorthand(shorthand->second, specs)) specs.clear();
    return specs;
  }
  const auto value = [&](std::string_view name) -> std::string_view {
    const auto found = node.style.find(std::string(name));
    return found == node.style.end() ? std::string_view{}
                                     : std::string_view(found->second);
  };
  if (!parseTransitionLonghands(
          value("transition-property"), value("transition-duration"),
          value("transition-timing-function"), value("transition-delay"),
          specs)) {
    specs.clear();
  }
  return specs;
}

bool isLayoutMotionProperty(std::string_view property) {
  static constexpr std::array<std::string_view, 29> properties = {
      "width",         "height",       "min-width",     "min-height",
      "max-width",     "max-height",   "left",          "top",
      "right",         "bottom",       "margin",        "margin-left",
      "margin-top",    "margin-right", "margin-bottom", "padding",
      "padding-left",  "padding-top",  "padding-right", "padding-bottom",
      "border-width",  "gap",          "row-gap",       "column-gap",
      "font-size",     "line-height",  "letter-spacing", "flex-grow",
      "flex-basis"};
  return std::find(properties.begin(), properties.end(), property) !=
         properties.end();
}

bool isInheritedMotionProperty(std::string_view property) {
  return std::find(kInheritedProperties.begin(), kInheritedProperties.end(),
                   property) != kInheritedProperties.end();
}

bool propagateInheritedMotion(
    Node& parent,
    std::string_view property,
    const std::optional<std::string>& previous,
    const std::optional<std::string>& current) {
  if (!isInheritedMotionProperty(property)) return false;
  bool layout_changed = false;
  forRuntimeChildren(parent, [&](Node& child, const Value::Object*) {
    if (!child.present) return;
    auto inherited = child.style.find(std::string(property));
    const bool followed_parent =
        previous.has_value()
            ? inherited != child.style.end() && inherited->second == *previous
            : inherited == child.style.end();
    if (!followed_parent) return;
    if (current.has_value()) {
      child.style[std::string(property)] = *current;
    } else {
      child.style.erase(std::string(property));
    }
    invalidatePaint(&child);
    layout_changed = layout_changed || isLayoutMotionProperty(property);
    layout_changed =
        propagateInheritedMotion(child, property, previous, current) ||
        layout_changed;
  });
  return layout_changed;
}

bool propagateChangedInheritedStyles(
    Node& node,
    const std::unordered_map<std::string, std::string>& previous_style) {
  bool layout_changed = false;
  for (const std::string_view property : kInheritedProperties) {
    const auto previous = previous_style.find(std::string(property));
    const auto current = node.style.find(std::string(property));
    const std::optional<std::string> previous_value =
        previous == previous_style.end()
            ? std::nullopt
            : std::optional<std::string>{previous->second};
    const std::optional<std::string> current_value =
        current == node.style.end()
            ? std::nullopt
            : std::optional<std::string>{current->second};
    if (previous_value == current_value) continue;
    layout_changed =
        propagateInheritedMotion(node, property, previous_value,
                                 current_value) ||
        layout_changed;
  }
  return layout_changed;
}

void applyStyleTransitions(
    Node& node,
    const std::unordered_map<std::string, std::string>& previous_style,
    double now_seconds,
    float motion_scale) {
  const std::vector<TransitionSpec> specs = nodeTransitionSpecs(node);
  std::unordered_set<std::string> retained;
  for (auto& [property, target_text] : node.style) {
    if (property.starts_with("transition") ||
        property.starts_with("animation") || property.starts_with("--")) {
      continue;
    }
    const auto previous = previous_style.find(property);
    if (previous == previous_style.end() || previous->second == target_text) {
      continue;
    }
    const TransitionSpec* spec = findTransition(specs, property);
    const auto from = parseMotionValue(previous->second);
    const auto target = parseMotionValue(target_text);
    if (spec == nullptr || !from.has_value() || !target.has_value()) {
      node.transitions.erase(property);
      continue;
    }
    auto track = node.transitions.find(property);
    if (track == node.transitions.end()) {
      track = node.transitions
                  .emplace(property, std::make_unique<TransitionTrack>(*from))
                  .first;
    }
    track->second->retarget(*target, *spec, now_seconds, motion_scale);
    target_text = serializeMotionValue(track->second->valueAt(now_seconds));
    if (track->second->active(now_seconds)) retained.insert(property);
  }
  for (auto iterator = node.transitions.begin();
       iterator != node.transitions.end();) {
    if (!retained.contains(iterator->first)) {
      iterator = node.transitions.erase(iterator);
    } else {
      ++iterator;
    }
  }
}

bool advanceNodeTransitions(Node& node,
                            double now_seconds,
                            bool* stacking_changed = nullptr) {
  bool layout_changed = false;
  for (auto iterator = node.transitions.begin();
       iterator != node.transitions.end();) {
    const std::string value =
        serializeMotionValue(iterator->second->valueAt(now_seconds));
    auto style = node.style.find(iterator->first);
    if (style != node.style.end() && style->second != value) {
      const std::optional<std::string> previous = style->second;
      style->second = value;
      if (iterator->first == "z-index" && node.parent != nullptr) {
        invalidateRuntimeChildOrder(*node.parent);
        if (stacking_changed != nullptr) *stacking_changed = true;
      }
      invalidatePaint(&node);
      layout_changed = layout_changed ||
                       isLayoutMotionProperty(iterator->first);
      layout_changed =
          propagateInheritedMotion(node, iterator->first, previous, value) ||
          layout_changed;
    }
    if (!iterator->second->active(now_seconds)) {
      iterator = node.transitions.erase(iterator);
    } else {
      ++iterator;
    }
  }
  return layout_changed;
}

bool finishNodeTransitions(Node& node,
                           bool* stacking_changed = nullptr) {
  bool layout_changed = false;
  for (auto& [property, track] : node.transitions) {
    const std::string value = serializeMotionValue(track->target());
    if (auto style = node.style.find(property);
        style != node.style.end() && style->second != value) {
      const std::optional<std::string> previous = style->second;
      style->second = value;
      if (property == "z-index" && node.parent != nullptr) {
        invalidateRuntimeChildOrder(*node.parent);
        if (stacking_changed != nullptr) *stacking_changed = true;
      }
      invalidatePaint(&node);
      layout_changed = layout_changed || isLayoutMotionProperty(property);
      layout_changed =
          propagateInheritedMotion(node, property, previous, value) ||
          layout_changed;
    }
  }
  node.transitions.clear();
  forRuntimeChildren(node, [&](Node& child, const Value::Object*) {
    if (child.present) {
      layout_changed = finishNodeTransitions(child, stacking_changed) ||
                       layout_changed;
    }
  });
  return layout_changed;
}

bool parseNodeAnimation(const Node& node,
                        std::string_view style_generation,
                        AnimationState& output) {
  AnimationSpec spec;
  std::string name;
  std::string signature(style_generation);
  if (const auto shorthand = node.style.find("animation");
      shorthand != node.style.end()) {
    signature += "|" + shorthand->second;
    const std::vector<std::string> tokens =
        splitWhitespace(shorthand->second);
    int time_count = 0;
    for (const std::string& token : tokens) {
      if (lower(token) == "none") return false;
      if (const auto time = parseTimeSeconds(token)) {
        if (time_count++ == 0) {
          spec.duration_seconds = std::max(0.0, *time);
        } else if (time_count == 2) {
          spec.delay_seconds = *time;
        }
        continue;
      }
      if (const auto easing = parseEasing(token)) {
        spec.easing = *easing;
        continue;
      }
      if (const auto iterations = parseAnimationIterationCount(token)) {
        spec.iteration_count = *iterations;
        continue;
      }
      if (const auto direction = parseAnimationDirection(token)) {
        spec.direction = *direction;
        continue;
      }
      if (const auto fill = parseAnimationFillMode(token)) {
        spec.fill_mode = *fill;
        continue;
      }
      if (name.empty()) name = unquote(token);
    }
  } else {
    const auto value = [&](std::string_view property) -> std::string {
      const auto found = node.style.find(std::string(property));
      return found == node.style.end() ? std::string{}
                                       : trim(found->second);
    };
    name = unquote(value("animation-name"));
    const std::string duration = value("animation-duration");
    const std::string delay = value("animation-delay");
    const std::string easing = value("animation-timing-function");
    const std::string iterations = value("animation-iteration-count");
    const std::string direction = value("animation-direction");
    const std::string fill = value("animation-fill-mode");
    signature += "|" + name + "|" + duration + "|" + delay + "|" +
                 easing + "|" + iterations + "|" + direction + "|" + fill;
    if (const auto parsed = parseTimeSeconds(duration)) {
      spec.duration_seconds = std::max(0.0, *parsed);
    }
    if (const auto parsed = parseTimeSeconds(delay)) {
      spec.delay_seconds = *parsed;
    }
    if (const auto parsed = parseEasing(easing)) spec.easing = *parsed;
    if (const auto parsed = parseAnimationIterationCount(iterations)) {
      spec.iteration_count = *parsed;
    }
    if (const auto parsed = parseAnimationDirection(direction)) {
      spec.direction = *parsed;
    }
    if (const auto parsed = parseAnimationFillMode(fill)) {
      spec.fill_mode = *parsed;
    }
  }
  name = trim(name);
  if (name.empty() || lower(name) == "none") return false;
  output.name = std::move(name);
  output.signature = std::move(signature);
  output.spec = spec;
  return true;
}

void configureNodeAnimation(Node& node,
                            const std::vector<Keyframes>& definitions,
                            std::string_view style_generation,
                            double now_seconds,
                            float motion_scale) {
  AnimationState requested;
  if (!parseNodeAnimation(node, style_generation, requested)) {
    node.animation.reset();
  } else if (const Keyframes* keyframes =
                 findKeyframes(definitions, requested.name)) {
    const bool restart = !node.animation.has_value() ||
                         node.animation->signature != requested.signature;
    if (restart) {
      requested.start_seconds = now_seconds;
      for (const Keyframe& frame : keyframes->frames) {
        for (const auto& [property, declaration] : frame.declarations) {
          (void)declaration;
          if (const auto base = node.style.find(property);
              base != node.style.end()) {
            requested.underlay[property] = base->second;
          } else {
            requested.underlay[property] = std::nullopt;
          }
        }
      }
      node.animation = std::move(requested);
    } else {
      for (auto& [property, base] : node.animation->underlay) {
        if (const auto computed = node.style.find(property);
            computed != node.style.end()) {
          base = computed->second;
        } else {
          base = std::nullopt;
        }
      }
    }
    const AnimationSample sample = sampleAnimation(
        *keyframes, node.animation->spec, node.animation->start_seconds,
        now_seconds, motion_scale);
    if (sample.contributes) {
      for (const auto& [property, value] : sample.values) {
        node.style[property] = serializeMotionValue(value);
      }
    }
    node.animation->completed = sample.finished;
  } else {
    node.animation.reset();
  }
}

bool advanceNodeAnimations(Node& node,
                           const std::vector<Keyframes>& definitions,
                           double now_seconds,
                           float motion_scale,
                           bool* stacking_changed = nullptr) {
  bool layout_changed = false;
  if (!node.animation.has_value()) return layout_changed;
  const Keyframes* keyframes = findKeyframes(definitions, node.animation->name);
  if (keyframes == nullptr) {
    node.animation.reset();
    return layout_changed;
  }

  const AnimationSample sample = sampleAnimation(
      *keyframes, node.animation->spec, node.animation->start_seconds,
      now_seconds, motion_scale);
  if (sample.contributes) {
    for (const auto& [property, value] : sample.values) {
      const std::string text = serializeMotionValue(value);
      const auto found = node.style.find(property);
      const std::optional<std::string> previous =
          found == node.style.end()
              ? std::nullopt
              : std::optional<std::string>{found->second};
      auto& current = node.style[property];
      if (current == text) continue;
      current = text;
      if (property == "z-index" && node.parent != nullptr) {
        invalidateRuntimeChildOrder(*node.parent);
        if (stacking_changed != nullptr) *stacking_changed = true;
      }
      invalidatePaint(&node);
      layout_changed = layout_changed || isLayoutMotionProperty(property);
      layout_changed =
          propagateInheritedMotion(node, property, previous, text) ||
          layout_changed;
    }
  } else {
    for (const auto& [property, value] : node.animation->underlay) {
      if (value.has_value()) {
        const auto found = node.style.find(property);
        const std::optional<std::string> previous =
            found == node.style.end()
                ? std::nullopt
                : std::optional<std::string>{found->second};
        auto& current = node.style[property];
        if (current == *value) continue;
        current = *value;
        if (property == "z-index" && node.parent != nullptr) {
          invalidateRuntimeChildOrder(*node.parent);
          if (stacking_changed != nullptr) *stacking_changed = true;
        }
        invalidatePaint(&node);
        layout_changed = layout_changed || isLayoutMotionProperty(property);
        layout_changed =
            propagateInheritedMotion(node, property, previous, value) ||
            layout_changed;
      } else {
        const auto found = node.style.find(property);
        if (found == node.style.end()) continue;
        const std::optional<std::string> previous = found->second;
        node.style.erase(found);
        if (property == "z-index" && node.parent != nullptr) {
          invalidateRuntimeChildOrder(*node.parent);
          if (stacking_changed != nullptr) *stacking_changed = true;
        }
        invalidatePaint(&node);
        layout_changed = layout_changed || isLayoutMotionProperty(property);
        layout_changed =
            propagateInheritedMotion(node, property, previous, std::nullopt) ||
            layout_changed;
      }
    }
  }
  node.animation->completed = sample.finished;
  return layout_changed;
}

bool layoutStyleChanged(
    const std::unordered_map<std::string, std::string>& before,
    const std::unordered_map<std::string, std::string>& after) {
  static constexpr std::array<std::string_view, 59> properties = {
      "display", "position", "width", "height", "min-width", "min-height",
      "max-width", "max-height", "left", "top", "right", "bottom",
      "margin", "margin-left", "margin-top", "margin-right", "margin-bottom",
      "padding", "padding-left", "padding-top", "padding-right",
      "padding-bottom", "border-width", "flex-direction", "flex-wrap",
      "flex-grow", "flex-shrink", "flex-basis", "justify-content",
      "align-content", "align-items", "align-self", "gap", "row-gap",
      "column-gap", "grid-template-columns", "grid-template-rows",
      "grid-auto-columns", "grid-auto-rows", "grid-auto-flow", "grid-column",
      "grid-row", "justify-items", "justify-self", "font-size", "line-height",
      "letter-spacing", "white-space", "overflow", "overflow-x", "overflow-y",
      "scrollbar-width", "scrollbar-placement", "scrollbar-visibility",
      "scrollbar-min-thumb", "scrollbar-vertical-width",
      "scrollbar-horizontal-height", "scrollbar-vertical-min-thumb",
      "scrollbar-horizontal-min-thumb"};
  for (const std::string_view property : properties) {
    const auto previous = before.find(std::string(property));
    const auto current = after.find(std::string(property));
    if (previous == before.end() && current == after.end()) continue;
    if (previous == before.end() || current == after.end() ||
        previous->second != current->second) {
      return true;
    }
  }
  return false;
}

StyleResult styleNodeSelf(DocumentInstance& document,
                          Node& node,
                          const style_runtime::StyleInputs& inputs) {
  if (!node.present) return {};
  StyleResult result{.restyled_nodes = 1u};
  const auto previous_style = node.style;
  computeStyle(node, document.rules, document.rule_candidates);
  resolveComputedFontSize(node, inputs.viewport_width, inputs.viewport_height);
  applyStyleTransitions(node, previous_style, inputs.now_seconds,
                        inputs.motion_scale);

  node.font_keys.clear();
  node.font_sources.clear();
  const auto append_font = [&](std::string_view asset_key,
                               std::uint32_t face_index) {
    const std::string registration_key =
        fontRegistrationKey(asset_key, face_index);
    if (std::find(node.font_keys.begin(), node.font_keys.end(),
                  registration_key) != node.font_keys.end()) {
      return;
    }
    node.font_keys.push_back(registration_key);
    node.font_sources.push_back({.registration_key = registration_key,
                                 .asset_key = std::string(asset_key),
                                 .face_index = face_index});
  };
  const int requested_weight =
      parseFontWeight(styleString(node, "font-weight", "400"));
  const FontFaceStyle requested_style =
      parseFontFaceStyle(styleString(node, "font-style", "normal"));
  std::unordered_set<std::string> requested_families;
  if (const auto families = node.style.find("font-family");
      families != node.style.end()) {
    for (std::string family : splitCommaList(families->second)) {
      family = lower(unquote(std::move(family)));
      requested_families.insert(family);
      if (const FontFaceDefinition* face = selectBestFontFace(
              document.font_faces, family, requested_weight,
              requested_style)) {
        append_font(face->asset_key, face->face_index);
      } else if (isSafeAssetReference(family) &&
                 inputs.assets.findFontAsset(family) != nullptr) {
        append_font(family, 0u);
      }
    }
  }
  if (node.font_keys.empty()) {
    for (const FontFaceDefinition& face : document.font_faces) {
      if (!requested_families.insert(face.family).second) continue;
      if (const FontFaceDefinition* selected = selectBestFontFace(
              document.font_faces, face.family, requested_weight,
              requested_style)) {
        append_font(selected->asset_key, selected->face_index);
      }
    }
  }

  configureNodeAnimation(node, document.keyframes, document.style_hash,
                         inputs.now_seconds, inputs.motion_scale);
  if (node.transitions.empty()) {
    document.active_transition_nodes.erase(&node);
  } else {
    document.active_transition_nodes.insert(&node);
  }
  if (node.animation.has_value() && !node.animation->completed) {
    document.active_animation_nodes.insert(&node);
  } else {
    document.active_animation_nodes.erase(&node);
  }

  const auto previous_z = previous_style.find("z-index");
  const auto current_z = node.style.find("z-index");
  const bool z_changed =
      (previous_z == previous_style.end()) !=
          (current_z == node.style.end()) ||
      (previous_z != previous_style.end() && current_z != node.style.end() &&
       previous_z->second != current_z->second);
  if (z_changed && node.parent != nullptr) {
    invalidateRuntimeChildOrder(*node.parent);
  }
  const auto previous_display = previous_style.find("display");
  const auto current_display = node.style.find("display");
  const bool display_changed =
      (previous_display == previous_style.end()) !=
          (current_display == node.style.end()) ||
      (previous_display != previous_style.end() &&
       current_display != node.style.end() &&
       previous_display->second != current_display->second);
  if (z_changed || display_changed) document.overlay_order_revision = true;
  if (previous_style != node.style) invalidatePaint(&node);
  result.layout_changed = layoutStyleChanged(previous_style, node.style);
  return result;
}

StyleResult merge(StyleResult left, const StyleResult& right) {
  left.restyled_nodes += right.restyled_nodes;
  left.layout_changed = left.layout_changed || right.layout_changed;
  return left;
}

}  // namespace

namespace style_runtime {

void setInlineStyleProperty(Node& node,
                            std::string property,
                            std::string value) {
  node.inline_style[std::move(property)] = std::move(value);
  node.attributes["style"] = declarationsInline(node.inline_style);
}

void rebuildDocumentStyleMetadata(DocumentInstance& document) {
  rebuildStyleRuleCandidates(document.rules, document.rule_candidates);
  document.has_motion = !document.keyframes.empty();
  for (const StyleRule& rule : document.rules) {
    document.has_motion =
        document.has_motion ||
        std::any_of(rule.declarations.begin(), rule.declarations.end(),
                    [](const auto& declaration) {
                      return declaration.first.starts_with("transition") ||
                             declaration.first.starts_with("animation");
                    });
  }
  const auto scan = [&](auto&& self, const Node& node) -> void {
    document.has_motion =
        document.has_motion ||
        std::any_of(node.inline_style.begin(), node.inline_style.end(),
                    [](const auto& declaration) {
                      return declaration.first.starts_with("transition") ||
                             declaration.first.starts_with("animation");
                    });
    for (const auto& child : node.children) self(self, *child);
  };
  if (document.body) scan(scan, *document.body);
}

StyleResult styleSubtree(DocumentInstance& document,
                         Node& root,
                         const StyleInputs& inputs) {
  if (!root.present) return {};
  StyleResult result = styleNodeSelf(document, root, inputs);
  forRuntimeChildren(root, [&](Node& child, const Value::Object*) {
    result = merge(std::move(result),
                   styleSubtree(document, child, inputs));
  });
  return result;
}

StyleResult styleDocument(DocumentInstance& document,
                          const StyleInputs& inputs) {
  StyleResult result;
  if (document.body) result = styleSubtree(document, *document.body, inputs);
  document.style_revision = false;
  document.font_revision = true;
  document.layout_revision = result.layout_changed || document.layout_revision;
  return result;
}

StyleResult restyleNode(DocumentInstance& document,
                        Node* node,
                        const StyleInputs& inputs) {
  if (node == nullptr || !node->present) return {};
  const auto previous_style = node->style;
  StyleResult result = styleNodeSelf(document, *node, inputs);
  result.layout_changed =
      propagateChangedInheritedStyles(*node, previous_style) ||
      result.layout_changed;
  document.layout_revision = result.layout_changed || document.layout_revision;
  document.font_revision = true;
  document.accessibility_revision = true;
  return result;
}

MotionResult advanceActiveMotion(DocumentInstance& document,
                                 double now_seconds,
                                 float motion_scale,
                                 std::uint64_t motion_frame) {
  if (motion_frame == 0u) motion_frame = 1u;
  MotionResult result;
  const auto record = [&](Node& node) {
    if (node.motion_advance_frame == motion_frame) return;
    node.motion_advance_frame = motion_frame;
    ++result.advanced_nodes;
  };
  for (auto active = document.active_transition_nodes.begin();
       active != document.active_transition_nodes.end();) {
    Node* node = *active;
    record(*node);
    bool stacking_changed = false;
    result.layout_changed =
        advanceNodeTransitions(*node, now_seconds, &stacking_changed) ||
        result.layout_changed;
    result.stacking_changed = result.stacking_changed || stacking_changed;
    if (node->transitions.empty()) {
      active = document.active_transition_nodes.erase(active);
    } else {
      ++active;
    }
  }
  for (auto active = document.active_animation_nodes.begin();
       active != document.active_animation_nodes.end();) {
    Node* node = *active;
    record(*node);
    bool stacking_changed = false;
    result.layout_changed =
        advanceNodeAnimations(*node, document.keyframes, now_seconds,
                              motion_scale, &stacking_changed) ||
        result.layout_changed;
    result.stacking_changed = result.stacking_changed || stacking_changed;
    if (!node->animation.has_value() || node->animation->completed) {
      active = document.active_animation_nodes.erase(active);
    } else {
      ++active;
    }
  }
  return result;
}

MotionResult finishActiveMotion(DocumentInstance& document,
                                double now_seconds) {
  MotionResult result;
  if (document.body) {
    result.layout_changed =
        finishNodeTransitions(*document.body, &result.stacking_changed);
  }
  document.active_transition_nodes.clear();
  for (Node* node : document.active_animation_nodes) {
    bool stacking_changed = false;
    result.layout_changed =
        advanceNodeAnimations(*node, document.keyframes, now_seconds, 0.0f,
                              &stacking_changed) ||
        result.layout_changed;
    result.stacking_changed = result.stacking_changed || stacking_changed;
  }
  document.active_animation_nodes.clear();
  return result;
}

}  // namespace style_runtime

}  // namespace karma::ui::native
