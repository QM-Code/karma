#include "features/ui/native/canvas_layout.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <string_view>

namespace karma::ui::native {
namespace {

using Json = nlohmann::json;

bool finiteNumber(const Json& value, float& output) {
  if (!value.is_number()) return false;
  const double number = value.get<double>();
  if (!std::isfinite(number) ||
      number < -static_cast<double>(std::numeric_limits<float>::max()) ||
      number > static_cast<double>(std::numeric_limits<float>::max())) {
    return false;
  }
  output = static_cast<float>(number);
  return true;
}

bool point(const Json& value, CanvasPoint& output) {
  return value.is_array() && value.size() == 2u &&
         finiteNumber(value[0], output.x) && finiteNumber(value[1], output.y);
}

float sanitizedInset(float value) {
  return std::isfinite(value) ? std::max(0.0f, value) : 0.0f;
}

}  // namespace

CanvasPoint CanvasLayout::windowToLayout(float x, float y) const {
  return {.x = x / std::max(scale_x, 0.0001f),
          .y = y / std::max(scale_y, 0.0001f)};
}

CanvasPoint CanvasLayout::layoutToWindow(float x, float y) const {
  return {.x = x * scale_x, .y = y * scale_y};
}

CanvasRect CanvasLayout::layoutToWindow(const CanvasRect& rect) const {
  return {.x = rect.x * scale_x,
          .y = rect.y * scale_y,
          .width = rect.width * scale_x,
          .height = rect.height * scale_y};
}

CanvasParseResult parseCanvasSpec(const Json* canvas) {
  CanvasSpec spec;
  if (canvas == nullptr || canvas->is_null()) return {.value = spec};
  if (!canvas->is_object()) {
    return {.error = "canvas must be an object"};
  }

  if (const auto mode = canvas->find("scale_mode"); mode != canvas->end()) {
    if (!mode->is_string()) {
      return {.error = "canvas.scale_mode must be a string"};
    }
    const std::string value = mode->get<std::string>();
    if (value == "logical") spec.scale_mode = CanvasScaleMode::Logical;
    else if (value == "fit") spec.scale_mode = CanvasScaleMode::Fit;
    else if (value == "fill") spec.scale_mode = CanvasScaleMode::Fill;
    else if (value == "stretch") spec.scale_mode = CanvasScaleMode::Stretch;
    else if (value == "pixel-perfect" || value == "pixel_perfect") {
      spec.scale_mode = CanvasScaleMode::PixelPerfect;
    } else {
      return {.error =
                  "canvas.scale_mode must be logical, fit, fill, stretch, or "
                  "pixel-perfect"};
    }
  }

  if (const auto reference = canvas->find("reference_size");
      reference != canvas->end()) {
    CanvasPoint size;
    if (!point(*reference, size) || size.x <= 0.0f || size.y <= 0.0f) {
      return {.error =
                  "canvas.reference_size must be [width, height] with positive "
                  "finite values"};
    }
    spec.reference_width = size.x;
    spec.reference_height = size.y;
  }

  if (const auto safe = canvas->find("safe_area"); safe != canvas->end()) {
    if (safe->is_boolean()) {
      spec.use_platform_safe_area = safe->get<bool>();
    } else if (safe->is_string()) {
      const std::string value = safe->get<std::string>();
      if (value == "platform") spec.use_platform_safe_area = true;
      else if (value == "none") spec.use_platform_safe_area = false;
      else {
        return {.error =
                    "canvas.safe_area must be 'platform', 'none', or a boolean"};
      }
    } else {
      return {.error =
                  "canvas.safe_area must be 'platform', 'none', or a boolean"};
    }
  }

  if (spec.scale_mode != CanvasScaleMode::Logical &&
      (spec.reference_width <= 0.0f || spec.reference_height <= 0.0f)) {
    return {.error =
                "canvas.reference_size is required for non-logical scale modes"};
  }
  return {.value = spec};
}

CanvasLayout resolveCanvas(const CanvasSpec& spec,
                           float logical_width,
                           float logical_height,
                           SafeAreaInsets platform_safe_area) {
  const float width = std::isfinite(logical_width)
                          ? std::max(0.0f, logical_width)
                          : 0.0f;
  const float height = std::isfinite(logical_height)
                           ? std::max(0.0f, logical_height)
                           : 0.0f;
  SafeAreaInsets insets = spec.use_platform_safe_area ? platform_safe_area
                                                      : SafeAreaInsets{};
  insets.left = std::min(sanitizedInset(insets.left), width);
  insets.right = std::min(sanitizedInset(insets.right), width - insets.left);
  insets.top = std::min(sanitizedInset(insets.top), height);
  insets.bottom = std::min(sanitizedInset(insets.bottom), height - insets.top);
  const CanvasRect safe{.x = insets.left,
                        .y = insets.top,
                        .width = width - insets.left - insets.right,
                        .height = height - insets.top - insets.bottom};

  CanvasLayout output;
  output.scale_mode = spec.scale_mode;
  output.safe_window_rect = safe;
  if (spec.scale_mode == CanvasScaleMode::Logical ||
      spec.reference_width <= 0.0f || spec.reference_height <= 0.0f) {
    output.layout_rect = safe;
    output.layout_clip = safe;
    return output;
  }

  const float fit = std::min(safe.width / spec.reference_width,
                             safe.height / spec.reference_height);
  const float fill = std::max(safe.width / spec.reference_width,
                              safe.height / spec.reference_height);
  switch (spec.scale_mode) {
    case CanvasScaleMode::Logical:
      break;
    case CanvasScaleMode::Fit:
      output.scale_x = output.scale_y = fit;
      break;
    case CanvasScaleMode::Fill:
      output.scale_x = output.scale_y = fill;
      break;
    case CanvasScaleMode::Stretch:
      output.scale_x = safe.width / spec.reference_width;
      output.scale_y = safe.height / spec.reference_height;
      break;
    case CanvasScaleMode::PixelPerfect:
      output.scale_x = output.scale_y = fit >= 1.0f ? std::floor(fit) : fit;
      break;
  }
  output.scale_x = std::max(output.scale_x, 0.0001f);
  output.scale_y = std::max(output.scale_y, 0.0001f);

  const float visual_width = spec.reference_width * output.scale_x;
  const float visual_height = spec.reference_height * output.scale_y;
  const float origin_x = safe.x + (safe.width - visual_width) * 0.5f;
  const float origin_y = safe.y + (safe.height - visual_height) * 0.5f;
  output.layout_rect = {.x = origin_x / output.scale_x,
                        .y = origin_y / output.scale_y,
                        .width = spec.reference_width,
                        .height = spec.reference_height};
  output.layout_clip = {.x = safe.x / output.scale_x,
                        .y = safe.y / output.scale_y,
                        .width = safe.width / output.scale_x,
                        .height = safe.height / output.scale_y};
  return output;
}

AnchorParseResult parseAnchorSpec(const Json& layout) {
  if (!layout.is_object()) return {};
  const auto anchors = layout.find("anchors");
  const bool has_related = layout.contains("pivot") || layout.contains("offsets");
  if (anchors == layout.end()) {
    // position:[x,y] remains the concise absolute-position contract when no
    // anchors are supplied.
    if (has_related) {
      return {.error = "layout.pivot and offsets require layout.anchors"};
    }
    return {};
  }
  if (!anchors->is_object()) {
    return {.error =
                "layout.anchors must contain normalized min and max points"};
  }

  AnchorSpec spec;
  const auto minimum = anchors->find("min");
  const auto maximum = anchors->find("max");
  if (minimum == anchors->end() || maximum == anchors->end() ||
      !point(*minimum, spec.minimum) || !point(*maximum, spec.maximum)) {
    return {.error =
                "layout.anchors.min and max must be finite [x, y] points"};
  }
  auto normalized = [](CanvasPoint value) {
    return value.x >= 0.0f && value.x <= 1.0f &&
           value.y >= 0.0f && value.y <= 1.0f;
  };
  if (!normalized(spec.minimum) || !normalized(spec.maximum) ||
      spec.minimum.x > spec.maximum.x || spec.minimum.y > spec.maximum.y) {
    return {.error =
                "layout anchors must be normalized and min must not exceed max"};
  }
  if (const auto pivot = layout.find("pivot"); pivot != layout.end()) {
    if (!point(*pivot, spec.pivot) || !normalized(spec.pivot)) {
      return {.error = "layout.pivot must be a normalized [x, y] point"};
    }
  }
  if (const auto position = layout.find("position"); position != layout.end() &&
      !point(*position, spec.position)) {
    return {.error = "layout.position must be a finite [x, y] point"};
  }
  if (const auto offsets = layout.find("offsets"); offsets != layout.end()) {
    if (offsets->is_array() && offsets->size() == 4u) {
      if (!finiteNumber((*offsets)[0], spec.offset_left) ||
          !finiteNumber((*offsets)[1], spec.offset_top) ||
          !finiteNumber((*offsets)[2], spec.offset_right) ||
          !finiteNumber((*offsets)[3], spec.offset_bottom)) {
        return {.error = "layout.offsets must contain four finite numbers"};
      }
    } else if (offsets->is_object()) {
      auto read = [&](std::string_view name, float& target) {
        const auto found = offsets->find(std::string(name));
        return found == offsets->end() || finiteNumber(*found, target);
      };
      if (!read("left", spec.offset_left) || !read("top", spec.offset_top) ||
          !read("right", spec.offset_right) ||
          !read("bottom", spec.offset_bottom)) {
        return {.error = "layout.offsets members must be finite numbers"};
      }
    } else {
      return {.error =
                  "layout.offsets must be [left, top, right, bottom] or an object"};
    }
  }
  return {.value = spec};
}

CanvasRect resolveAnchor(const CanvasRect& parent,
                         const CanvasRect& measured,
                         const AnchorSpec& anchor) {
  CanvasRect output = measured;
  constexpr float epsilon = 0.00001f;
  if (std::abs(anchor.maximum.x - anchor.minimum.x) <= epsilon) {
    output.x = parent.x + parent.width * anchor.minimum.x + anchor.position.x -
               output.width * anchor.pivot.x;
  } else {
    output.x = parent.x + parent.width * anchor.minimum.x + anchor.offset_left +
               anchor.position.x;
    const float end = parent.x + parent.width * anchor.maximum.x -
                      anchor.offset_right + anchor.position.x;
    output.width = std::max(0.0f, end - output.x);
  }
  if (std::abs(anchor.maximum.y - anchor.minimum.y) <= epsilon) {
    output.y = parent.y + parent.height * anchor.minimum.y + anchor.position.y -
               output.height * anchor.pivot.y;
  } else {
    output.y = parent.y + parent.height * anchor.minimum.y + anchor.offset_top +
               anchor.position.y;
    const float end = parent.y + parent.height * anchor.maximum.y -
                      anchor.offset_bottom + anchor.position.y;
    output.height = std::max(0.0f, end - output.y);
  }
  return output;
}

}  // namespace karma::ui::native
