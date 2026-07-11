#include "features/ui/native/presentation_builder.h"

#include "features/ui/native/computed_style_values.h"
#include "features/ui/native/paint_engine.h"
#include "features/ui/native/presentation_resources.h"
#include "features/ui/native/presentation_runtime.h"
#include "features/ui/native/runtime_dom.h"
#include "features/ui/native/string_utils.h"
#include "features/ui/native/text_engine.h"
#include "features/ui/native/transient_runtime.h"
#include "features/ui/native/widget_paint.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace karma::ui::native::presentation_builder {
namespace {

using computed_style_values::kDefaultFontSize;
using computed_style_values::nodeFontSize;
using computed_style_values::nodeLineHeight;
using computed_style_values::oneBoxValue;
using computed_style_values::parseColor;
using computed_style_values::parseLength;
using computed_style_values::resolveLength;
using computed_style_values::splitWhitespace;
using runtime_dom::attributeBoolean;
using runtime_dom::clipForOverflow;
using runtime_dom::clipsOverflow;
using runtime_dom::DocumentInstance;
using runtime_dom::intersectRects;
using runtime_dom::Node;
using runtime_dom::nonEmpty;
using runtime_dom::Rect;
using runtime_dom::runtimeChildrenInPaintOrder;
using runtime_dom::ScrollbarPart;
using runtime_dom::styleFloat;
using runtime_dom::styleString;
using string_utils::lower;
using string_utils::parseFiniteDouble;
using string_utils::trim;
using transient_runtime::isOverlayTransientRoot;

struct PaintContext {
  const FrameInputs& frame;
  TextEngine& text_engine;
  PresentationResources& resources;
  DiagnosticSink diagnostics;
  std::unordered_set<const Node*> volatile_nodes;
  DocumentInstance* document = nullptr;
  int logical_width = 0;
  int logical_height = 0;
  float scale_x = 1.0f;
  float scale_y = 1.0f;
  std::size_t rebuilt_fragments = 0u;
};

std::uint32_t packColor(math::Color color, float opacity = 1.0f) {
  auto channel = [](float value) {
    return static_cast<std::uint32_t>(std::clamp(value, 0.0f, 1.0f) * 255.0f + 0.5f);
  };
  return channel(color.r) | (channel(color.g) << 8u) | (channel(color.b) << 16u) |
         (channel(color.a * opacity) << 24u);
}

struct DrawDataCheckpoint {
  std::size_t vertex_count = 0u;
  std::size_t index_count = 0u;
  std::size_t command_count = 0u;
  std::optional<rendering::UIDrawCmd> tail_command;

  explicit DrawDataCheckpoint(const rendering::UIDrawData& draw_data)
      : vertex_count(draw_data.vertices.size()),
        index_count(draw_data.indices.size()),
        command_count(draw_data.commands.size()) {
    if (!draw_data.commands.empty()) {
      tail_command = draw_data.commands.back();
    }
  }

  void restore(rendering::UIDrawData& draw_data) const {
    draw_data.vertices.resize(vertex_count);
    draw_data.indices.resize(index_count);
    draw_data.commands.resize(command_count);
    // Appending may coalesce into the pre-existing tail command. Restore that
    // command as well as the vector sizes so a retry preserves an earlier UI
    // layer exactly.
    if (tail_command.has_value() && !draw_data.commands.empty()) {
      draw_data.commands.back() = *tail_command;
    }
  }
};

void appendQuad(rendering::UIDrawData& output,
                const Rect& logical_rect,
                const Rect& logical_clip,
                float scale_x,
                float scale_y,
                int framebuffer_width,
                int framebuffer_height,
                math::Color color,
                float opacity,
                rendering::UITextureHandle texture = 0,
                rendering::UISamplerMode sampler = rendering::UISamplerMode::Linear,
                rendering::UITextureMode texture_mode = rendering::UITextureMode::Color,
                float u0 = 0.0f,
                float v0 = 0.0f,
                float u1 = 1.0f,
                float v1 = 1.0f) {
  const Rect clipped = intersectRects(logical_rect, logical_clip);
  if (logical_rect.width <= 0.0f || logical_rect.height <= 0.0f ||
      clipped.width <= 0.0f || clipped.height <= 0.0f) return;
  const float x = std::round(logical_rect.x * scale_x);
  const float y = std::round(logical_rect.y * scale_y);
  const float right = std::round((logical_rect.x + logical_rect.width) * scale_x);
  const float bottom = std::round((logical_rect.y + logical_rect.height) * scale_y);
  if (right <= x || bottom <= y) return;
  const auto scissor = native::presentation::framebufferScissor(
      {.x = logical_clip.x,
       .y = logical_clip.y,
       .width = logical_clip.width,
       .height = logical_clip.height},
      scale_x, scale_y, framebuffer_width, framebuffer_height);
  if (!scissor.has_value()) return;
  const std::uint32_t base = static_cast<std::uint32_t>(output.vertices.size());
  const std::uint32_t rgba = packColor(color, opacity);
  output.vertices.push_back({.x = x, .y = y, .u = u0, .v = v0, .rgba = rgba});
  output.vertices.push_back({.x = right, .y = y, .u = u1, .v = v0, .rgba = rgba});
  output.vertices.push_back({.x = right, .y = bottom, .u = u1, .v = v1, .rgba = rgba});
  output.vertices.push_back({.x = x, .y = bottom, .u = u0, .v = v1, .rgba = rgba});
  const std::uint32_t index_offset = static_cast<std::uint32_t>(output.indices.size());
  output.indices.insert(output.indices.end(), {base, base + 1u, base + 2u,
                                               base, base + 2u, base + 3u});
  rendering::UIDrawCmd command{};
  command.index_offset = index_offset;
  command.index_count = 6;
  command.scissor_enabled = true;
  // The primitive intersection above is only a culling test.  Keeping the
  // authored clip as command state lets adjacent quads (most importantly,
  // glyphs on the same line) share one scissor and coalesce into a batch.
  command.scissor_x = scissor->x;
  command.scissor_y = scissor->y;
  command.scissor_w = scissor->width;
  command.scissor_h = scissor->height;
  command.texture = texture;
  command.blend_mode = rendering::UIBlendMode::StraightAlpha;
  command.sampler_mode = sampler;
  command.texture_mode = texture_mode;
  if (!output.commands.empty() &&
      presentation::compatibleCommandState(output.commands.back(), command) &&
      output.commands.back().index_offset +
              output.commands.back().index_count ==
          index_offset) {
    output.commands.back().index_count += command.index_count;
  } else {
    output.commands.push_back(command);
  }
}

void appendPaintMesh(rendering::UIDrawData& output,
                     const paint::Mesh& mesh,
                     const Rect& logical_clip,
                     float scale_x,
                     float scale_y,
                     int framebuffer_width,
                     int framebuffer_height,
                     float opacity,
                     rendering::UITextureHandle texture = 0,
                     rendering::UISamplerMode sampler =
                         rendering::UISamplerMode::Linear) {
  constexpr std::size_t max_index =
      std::numeric_limits<std::uint32_t>::max();
  if (mesh.empty() || !mesh.valid() || logical_clip.width <= 0.0f ||
      logical_clip.height <= 0.0f || scale_x <= 0.0f || scale_y <= 0.0f ||
      output.vertices.size() > max_index || mesh.vertices.size() > max_index ||
      mesh.vertices.size() > max_index - output.vertices.size() ||
      mesh.indices.size() > max_index) {
    return;
  }
  float mesh_left = std::numeric_limits<float>::infinity();
  float mesh_top = std::numeric_limits<float>::infinity();
  float mesh_right = -std::numeric_limits<float>::infinity();
  float mesh_bottom = -std::numeric_limits<float>::infinity();
  for (const paint::Vertex& vertex : mesh.vertices) {
    mesh_left = std::min(mesh_left, vertex.position.x);
    mesh_top = std::min(mesh_top, vertex.position.y);
    mesh_right = std::max(mesh_right, vertex.position.x);
    mesh_bottom = std::max(mesh_bottom, vertex.position.y);
  }
  const Rect mesh_bounds{mesh_left, mesh_top, mesh_right - mesh_left,
                         mesh_bottom - mesh_top};
  const Rect clipped = intersectRects(mesh_bounds, logical_clip);
  if (clipped.width <= 0.0f || clipped.height <= 0.0f) return;
  const auto scissor = native::presentation::framebufferScissor(
      {.x = logical_clip.x,
       .y = logical_clip.y,
       .width = logical_clip.width,
       .height = logical_clip.height},
      scale_x, scale_y, framebuffer_width, framebuffer_height);
  if (!scissor.has_value()) return;

  const std::uint32_t base = static_cast<std::uint32_t>(output.vertices.size());
  output.vertices.reserve(output.vertices.size() + mesh.vertices.size());
  for (const paint::Vertex& vertex : mesh.vertices) {
    output.vertices.push_back({.x = vertex.position.x * scale_x,
                               .y = vertex.position.y * scale_y,
                               .u = vertex.uv.x,
                               .v = vertex.uv.y,
                               .rgba = packColor(vertex.color, opacity)});
  }
  const std::uint32_t index_offset = static_cast<std::uint32_t>(output.indices.size());
  output.indices.reserve(output.indices.size() + mesh.indices.size());
  for (std::uint32_t index : mesh.indices) output.indices.push_back(base + index);

  rendering::UIDrawCmd command{};
  command.index_offset = index_offset;
  command.index_count = static_cast<std::uint32_t>(mesh.indices.size());
  command.scissor_enabled = true;
  command.scissor_x = scissor->x;
  command.scissor_y = scissor->y;
  command.scissor_w = scissor->width;
  command.scissor_h = scissor->height;
  command.texture = texture;
  command.blend_mode = rendering::UIBlendMode::StraightAlpha;
  command.sampler_mode = sampler;
  command.texture_mode = rendering::UITextureMode::Color;
  if (!output.commands.empty() &&
      presentation::compatibleCommandState(output.commands.back(), command) &&
      output.commands.back().index_offset + output.commands.back().index_count ==
          index_offset) {
    output.commands.back().index_count += command.index_count;
  } else {
    output.commands.push_back(command);
  }
}

std::optional<std::string> styleAssetKey(const Node& node,
                                         std::string_view property) {
  const auto found = node.style.find(std::string(property));
  if (found == node.style.end()) return std::nullopt;
  std::string value = trim(found->second);
  if (lower(value) == "none" || value.empty()) return std::nullopt;
  const std::string lowered = lower(value);
  for (const std::string_view function : {std::string_view{"asset"},
                                          std::string_view{"image"},
                                          std::string_view{"svg"}}) {
    const std::string prefix = std::string(function) + "(";
    if (lowered.starts_with(prefix) && value.back() == ')') {
      value = trim(std::string_view(value).substr(
          prefix.size(), value.size() - prefix.size() - 1u));
      break;
    }
  }
  if (value.size() >= 2u &&
      ((value.front() == '\"' && value.back() == '\"') ||
       (value.front() == '\'' && value.back() == '\''))) {
    value = value.substr(1u, value.size() - 2u);
  }
  return value.empty() ? std::nullopt
                       : std::optional<std::string>{std::move(value)};
}

paint::CornerRadii nodeCornerRadii(const Node& node,
                                   int viewport_width,
                                   int viewport_height) {
  paint::CornerRadii radii;
  const float reference = std::max(0.0f, std::min(node.layout.width,
                                                  node.layout.height));
  const float font_size = nodeFontSize(node);
  auto resolve = [&](std::string_view source) {
    return std::max(0.0f, resolveLength(parseLength(source), reference,
                                        viewport_width, viewport_height,
                                        font_size, kDefaultFontSize, 0.0f));
  };
  if (const auto shorthand = node.style.find("border-radius");
      shorthand != node.style.end()) {
    std::vector<std::string> values = splitWhitespace(shorthand->second);
    if (!values.empty() && values.size() <= 4u &&
        std::none_of(values.begin(), values.end(),
                     [](const std::string& value) { return value == "/"; })) {
      const float first = resolve(values[0]);
      const float second = resolve(values.size() > 1u ? values[1] : values[0]);
      const float third = resolve(values.size() > 2u ? values[2] : values[0]);
      const float fourth = resolve(values.size() > 3u ? values[3] :
                                   (values.size() > 1u ? values[1] : values[0]));
      radii = values.size() == 1u
                  ? paint::CornerRadii{first, first, first, first}
              : values.size() == 2u
                  ? paint::CornerRadii{first, second, first, second}
              : values.size() == 3u
                  ? paint::CornerRadii{first, second, third, second}
                  : paint::CornerRadii{first, second, third, fourth};
    }
  }
  const std::array<std::pair<std::string_view, float*>, 4u> corners = {{
      {"border-top-left-radius", &radii.top_left},
      {"border-top-right-radius", &radii.top_right},
      {"border-bottom-right-radius", &radii.bottom_right},
      {"border-bottom-left-radius", &radii.bottom_left},
  }};
  for (const auto& [property, destination] : corners) {
    if (const auto found = node.style.find(std::string(property));
        found != node.style.end()) {
      const std::vector<std::string> values = splitWhitespace(found->second);
      if (!values.empty()) *destination = resolve(values.front());
    }
  }
  return radii;
}

paint::BorderWidths nodeBorderWidths(const Node& node,
                                     int viewport_width,
                                     int viewport_height) {
  const float font_size = nodeFontSize(node);
  return {
      .left = oneBoxValue(node, "border-width", "left", node.layout.width,
                          viewport_width, viewport_height, font_size),
      .top = oneBoxValue(node, "border-width", "top", node.layout.width,
                         viewport_width, viewport_height, font_size),
      .right = oneBoxValue(node, "border-width", "right", node.layout.width,
                           viewport_width, viewport_height, font_size),
      .bottom = oneBoxValue(node, "border-width", "bottom", node.layout.width,
                            viewport_width, viewport_height, font_size),
  };
}

math::Color nodeColor(const Node& node,
                      std::string_view property,
                      math::Color fallback = {1, 1, 1, 1}) {
  const auto found = node.style.find(std::string(property));
  if (found == node.style.end()) return fallback;
  return parseColor(found->second).value_or(fallback);
}

rendering::UISamplerMode nodeSampler(const Node& node) {
  return styleString(node, "image-sampling", "linear") == "nearest"
             ? rendering::UISamplerMode::Nearest
             : rendering::UISamplerMode::Linear;
}

void drawShapedText(PaintContext& context,
                    const Node& node,
                    rendering::UIDrawData& output,
                    float opacity) {
  if (node.text.empty() || !context.frame.graphics_available) return;
  const float font_size = nodeFontSize(node);
  const float padding_left = oneBoxValue(node, "padding", "left", node.layout.width,
                                         context.logical_width, context.logical_height, font_size);
  const float padding_right = oneBoxValue(node, "padding", "right", node.layout.width,
                                          context.logical_width, context.logical_height, font_size);
  const float padding_top = oneBoxValue(node, "padding", "top", node.layout.width,
                                        context.logical_width, context.logical_height, font_size);
  const float available_width =
      std::max(0.0f, node.layout.width - padding_left - padding_right);
  native::ShapeRequest request;
  request.text_utf8 = node.text;
  request.font_keys = node.font_keys;
  if (request.font_keys.empty()) request.font_keys.emplace_back("__karma_no_font__");
  request.locale = styleString(node, "locale", std::string(context.frame.locale));
  request.pixel_size = font_size;
  request.max_width = styleString(node, "white-space", "normal") == "nowrap"
                          ? 0.0f
                          : available_width;
  request.line_height = nodeLineHeight(node, font_size);
  request.letter_spacing = styleFloat(node, "letter-spacing", 0.0f);
  const std::string direction = styleString(node, "direction", "auto");
  request.direction = direction == "rtl" ? native::TextDirection::RightToLeft
                      : direction == "ltr" ? native::TextDirection::LeftToRight
                                           : native::TextDirection::Auto;
  std::string error;
  const auto shaped = context.text_engine.shape(request, &error);
  if (!shaped.has_value()) return;

  const float raster_scale = std::max(0.0001f, context.scale_y);
  const float physical_pixel_size = font_size * raster_scale;
  math::Color text_color = nodeColor(node, "color");
  if (node.tag == "option") {
    for (const Node* parent = node.parent; parent != nullptr;
         parent = parent->parent) {
      if (parent->tag != "select") continue;
      text_color = nodeColor(*parent, "select-option-text-color", text_color);
      break;
    }
  }
  const std::string alignment = styleString(node, "text-align", "start");
  const float content_x = node.layout.x + padding_left;
  const float content_y = node.layout.y + padding_top;
  const Rect text_clip = !clipsOverflow(node)
                             ? node.clip
                             : clipForOverflow(node, node.clip, node.layout);
  for (const native::ShapedLine& line : shaped->lines) {
    float line_offset = 0.0f;
    const bool line_rtl = direction == "rtl" ||
                          (direction == "auto" && !line.runs.empty() &&
                           line.runs.front().direction == native::TextDirection::RightToLeft);
    const bool right_aligned = alignment == "right" ||
                               (alignment == "start" && line_rtl) ||
                               (alignment == "end" && !line_rtl);
    if (alignment == "center") line_offset = (available_width - line.width) * 0.5f;
    if (right_aligned) {
      line_offset = available_width - line.width;
    }
    line_offset = std::max(0.0f, line_offset);
    for (const native::ShapedRun& run : line.runs) {
      for (const native::ShapedGlyph& glyph : run.glyphs) {
        const auto placement = context.resources.ensureGlyphPlacement(
            glyph, physical_pixel_size);
        if (!placement.has_value() || placement->texture == 0u ||
            placement->atlas_width <= 0 || placement->atlas_height <= 0) {
          continue;
        }
        const float x = content_x + line_offset + glyph.x + glyph.x_offset +
                        static_cast<float>(placement->bearing_x) / raster_scale;
        const float y = content_y + glyph.y + glyph.y_offset -
                        static_cast<float>(placement->bearing_y) / context.scale_y;
        const Rect glyph_rect{.x = x,
                              .y = y,
                              .width = static_cast<float>(placement->width) / raster_scale,
                              .height = static_cast<float>(placement->height) / context.scale_y};
        const float u0 = static_cast<float>(placement->x) /
                         static_cast<float>(placement->atlas_width);
        const float v0 = static_cast<float>(placement->y) /
                         static_cast<float>(placement->atlas_height);
        const float u1 = static_cast<float>(placement->x + placement->width) /
                         static_cast<float>(placement->atlas_width);
        const float v1 = static_cast<float>(placement->y + placement->height) /
                         static_cast<float>(placement->atlas_height);
        const bool alpha_mask = placement->format == native::GlyphPixelFormat::R8;
        appendQuad(output, glyph_rect, text_clip, context.scale_x, context.scale_y,
                   context.frame.framebuffer_width, context.frame.framebuffer_height,
                   alpha_mask ? text_color : math::Color{1, 1, 1, 1}, opacity,
                   placement->texture,
                   rendering::UISamplerMode::Linear,
                   alpha_mask ? rendering::UITextureMode::AlphaMask
                              : rendering::UITextureMode::Color,
                   u0, v0, u1, v1);
      }
    }
  }
}

rendering::UITextureHandle resolveNodeTexture(PaintContext& context, const Node& node) {
  return context.resources.resolveTexture(
      node.image, node.layout.width, node.layout.height,
      nodeColor(node, "color", {1, 1, 1, 1}), context.scale_x, context.scale_y);
}

std::optional<paint::Vec2> nodeImageIntrinsicSize(PaintContext& context,
                                                  const Node& node) {
  const auto size = context.resources.intrinsicSize(node.image);
  return size.has_value()
             ? std::optional<paint::Vec2>{{size->first, size->second}}
             : std::nullopt;
}

rendering::UITextureHandle resolveAssetTexture(PaintContext& context,
                                               const Node& styled_node,
                                               std::string key,
                                               const Rect& logical_rect) {
  return context.resources.resolveTexture(
      ImageSource::asset(std::move(key)), logical_rect.width,
      logical_rect.height, nodeColor(styled_node, "color", {1, 1, 1, 1}),
      context.scale_x, context.scale_y);
}

paint::CornerRadii uniformPartRadii(const Node& node,
                                    std::string_view property,
                                    float fallback) {
  const float radius = std::max(0.0f, styleFloat(node, property, fallback));
  return {radius, radius, radius, radius};
}

void reportNineSliceLimit(PaintContext& context,
                          const Node& node,
                          const paint::NineSliceBuildStatus& status) {
  if (!status.cell_limit_reduced || node.reported_nine_slice_cell_limit) return;
  if (context.document == nullptr ||
      context.diagnostics.nine_slice_cell_limit == nullptr) {
    return;
  }
  context.diagnostics.nine_slice_cell_limit(
      context.diagnostics.context, *context.document, node);
  node.reported_nine_slice_cell_limit = true;
}

void appendSkinnedPart(PaintContext& context,
                       const Node& node,
                       rendering::UIDrawData& output,
                       const paint::Mesh& mesh,
                       const paint::Rect& bounds,
                       std::string_view prefix,
                       float opacity) {
  if (bounds.width <= 0.0f || bounds.height <= 0.0f) return;
  const std::string base(prefix);
  const float part_opacity = std::clamp(
      styleFloat(node, base + "-opacity", 1.0f), 0.0f, 1.0f);
  // Keep the authored part fill under its border image. Opaque nine-slice
  // centers retain their previous appearance, while transparent-center RPG
  // frames preserve hover/pressed/selected fill feedback.
  appendPaintMesh(output, mesh, node.clip, context.scale_x, context.scale_y,
                  context.frame.framebuffer_width, context.frame.framebuffer_height,
                  opacity * part_opacity);
  if (const auto asset =
          styleAssetKey(node, base + "-border-image-source")) {
    paint::Insets source_slices;
    const auto slice = node.style.find(base + "-border-image-slice");
    if (slice != node.style.end() &&
        paint::parseInsets(slice->second, source_slices)) {
      Node source;
      source.image = ImageSource::asset(*asset);
      const paint::Vec2 source_size =
          nodeImageIntrinsicSize(context, source).value_or(paint::Vec2{});
      std::optional<paint::Insets> destination_slices;
      if (const auto width =
              node.style.find(base + "-border-image-width");
          width != node.style.end()) {
        paint::Insets parsed;
        if (paint::parseInsets(width->second, parsed)) {
          destination_slices = parsed;
        }
      }
      const rendering::UITextureHandle texture =
          resolveAssetTexture(context, node, *asset,
                              {bounds.x, bounds.y, bounds.width, bounds.height});
      paint::NineSliceRepeat repeat;
      if (const auto authored_repeat =
              node.style.find(base + "-border-image-repeat");
          authored_repeat != node.style.end()) {
        (void)paint::parseNineSliceRepeat(authored_repeat->second, repeat);
      }
      paint::NineSliceBuildStatus nine_slice_status;
      const paint::Mesh panel = paint::nineSlicePanel(
          bounds, {.source_size = source_size,
                   .source_slices = source_slices,
                   .destination_slices = destination_slices,
                   .repeat = repeat,
                   .pixel_scale = {context.scale_x, context.scale_y}},
          {}, &nine_slice_status);
      reportNineSliceLimit(context, node, nine_slice_status);
      if (texture != 0u && !panel.empty()) {
        appendPaintMesh(output, panel, node.clip, context.scale_x, context.scale_y,
                        context.frame.framebuffer_width, context.frame.framebuffer_height,
                        opacity * part_opacity, texture, nodeSampler(node));
      }
    }
  }
  const float border_width =
      std::max(0.0f, styleFloat(node, base + "-border-width", 0.0f));
  if (border_width > 0.0f) {
    const paint::CornerRadii radii = uniformPartRadii(
        node, base + "-radius", 0.0f);
    const paint::BorderWidths widths{border_width, border_width,
                                     border_width, border_width};
    appendPaintMesh(
        output,
        paint::roundedRectBorder(
            bounds, radii, widths,
            nodeColor(node, base + "-border-color", {1, 1, 1, 1})),
        node.clip, context.scale_x, context.scale_y, context.frame.framebuffer_width,
        context.frame.framebuffer_height, opacity * part_opacity);
  }
}

void drawScrollbars(PaintContext& context,
                    const Node& node,
                    rendering::UIDrawData& output,
                    float opacity) {
  auto has_part_style = [&](std::string_view prefix) {
    const std::string needle = std::string(prefix) + "-";
    return std::any_of(node.style.begin(), node.style.end(),
                       [&](const auto& declaration) {
      return declaration.first.starts_with(needle);
    });
  };
  auto draw_part = [&](const Rect& rect,
                       const math::Color& color,
                       std::string_view prefix,
                       bool rounded_thumb = false) {
    if (!nonEmpty(rect)) return;
    const paint::Rect bounds{rect.x, rect.y, rect.width, rect.height};
    const paint::CornerRadii radii = uniformPartRadii(
        node, std::string(prefix) + "-radius",
        rounded_thumb ? std::min(rect.width, rect.height) * 0.5f : 0.0f);
    appendSkinnedPart(
        context, node, output, paint::roundedRectFill(bounds, radii, color),
        bounds, prefix, opacity);
  };
  const math::Color track = nodeColor(
      node, "scrollbar-track-color", {0.05f, 0.07f, 0.1f, 0.82f});
  const math::Color track_hover = nodeColor(
      node, "scrollbar-track-hover-color", track);
  auto axis_prefix = [&](std::string_view axis,
                         std::string_view part) {
    const std::string specific =
        "scrollbar-" + std::string(axis) + "-" + std::string(part);
    return has_part_style(specific) ? specific
                                    : "scrollbar-" + std::string(part);
  };
  auto draw_track = [&](const Rect& rect,
                        std::string_view axis,
                        bool hovered) {
    const std::string prefix = axis_prefix(axis, "track");
    math::Color color = nodeColor(node, prefix + "-color", track);
    if (hovered) {
      color = nodeColor(node, prefix + "-hover-color", track_hover);
    }
    draw_part(rect, color, prefix);
  };
  auto draw_thumb = [&](const Rect& rect,
                        std::string_view axis,
                        bool hovered,
                        bool active) {
    const std::string prefix = axis_prefix(axis, "thumb");
    math::Color color = nodeColor(
        node, prefix + "-color",
        nodeColor(node, "scrollbar-thumb-color",
                  {0.34f, 0.4f, 0.5f, 0.95f}));
    if (hovered) {
      color = nodeColor(
          node, prefix + "-hover-color",
          nodeColor(node, "scrollbar-thumb-hover-color",
                    {0.46f, 0.54f, 0.67f, 1.0f}));
    }
    if (active) {
      color = nodeColor(
          node, prefix + "-active-color",
          nodeColor(node, "scrollbar-thumb-active-color",
                    {0.28f, 0.52f, 0.88f, 1.0f}));
    }
    draw_part(rect, color, prefix, true);
  };
  draw_track(node.horizontal_scroll_track, "horizontal",
             node.hovered_scrollbar_part == ScrollbarPart::HorizontalTrack);
  draw_track(node.vertical_scroll_track, "vertical",
             node.hovered_scrollbar_part == ScrollbarPart::VerticalTrack);
  draw_part(node.scrollbar_corner,
            nodeColor(node, "scrollbar-corner-color", track),
            "scrollbar-corner");
  draw_thumb(node.horizontal_scroll_thumb, "horizontal",
             node.hovered_scrollbar_part == ScrollbarPart::HorizontalThumb,
             node.active_scrollbar_part == ScrollbarPart::HorizontalThumb);
  draw_thumb(node.vertical_scroll_thumb, "vertical",
             node.hovered_scrollbar_part == ScrollbarPart::VerticalThumb,
             node.active_scrollbar_part == ScrollbarPart::VerticalThumb);
}

void drawWindowChrome(PaintContext& context,
                      const Node& node,
                      rendering::UIDrawData& output,
                      float opacity) {
  if (node.tag != "window" || !nonEmpty(node.window_titlebar)) return;
  const Rect clip = intersectRects(node.clip, node.layout);
  const paint::Rect titlebar_bounds{
      node.window_titlebar.x, node.window_titlebar.y,
      node.window_titlebar.width, node.window_titlebar.height};
  appendSkinnedPart(
      context, node, output,
      paint::roundedRectFill(
          titlebar_bounds,
          uniformPartRadii(node, "window-titlebar-radius", 0.0f),
          nodeColor(node, "window-titlebar-color",
                    {0.1f, 0.13f, 0.19f, 1.0f})),
      titlebar_bounds, "window-titlebar", opacity);
  if (!node.title.empty()) {
    Node title;
    title.tag = "text";
    title.text = node.title;
    title.source_text = node.title;
    title.layout = node.window_titlebar;
    title.layout.x += 10.0f;
    title.layout.width = std::max(
        0.0f, title.layout.width - 20.0f -
                  node.window_close_button.width -
                  node.window_collapse_button.width);
    title.clip = clip;
    title.style = node.style;
    title.style["background-color"] = "transparent";
    title.style["text-align"] = "left";
    title.font_keys = node.font_keys;
    title.font_sources = node.font_sources;
    drawShapedText(context, title, output, opacity);
  }
  auto button = [&](const Rect& rect, std::string_view prefix) {
    if (!nonEmpty(rect)) return;
    const std::string base(prefix);
    const paint::Rect bounds{rect.x, rect.y, rect.width, rect.height};
    appendSkinnedPart(
        context, node, output,
        paint::roundedRectFill(
            bounds, uniformPartRadii(node, base + "-radius", 0.0f),
            nodeColor(node, base + "-color",
                      {0.18f, 0.22f, 0.3f, 1.0f})),
        bounds, prefix, opacity);
  };
  button(node.window_collapse_button, "window-collapse-button");
  button(node.window_close_button, "window-close-button");
  const auto resizable_attribute = node.attributes.find("resizable");
  const bool resizable = resizable_attribute == node.attributes.end() ||
      [&] {
        const std::string value = lower(trim(resizable_attribute->second));
        return value.empty() || value == "true" || value == "1" ||
               value == "resizable";
      }();
  const float grip_size = std::clamp(
      styleFloat(node, "window-resize-grip", 7.0f), 0.0f,
      std::min(node.layout.width, node.layout.height));
  if (resizable && grip_size > 0.0f) {
    const bool rtl = styleString(node, "direction", "ltr") == "rtl";
    const paint::Rect grip_bounds{
        rtl ? node.layout.x : node.layout.x + node.layout.width - grip_size,
        node.layout.y + node.layout.height - grip_size,
        grip_size, grip_size};
    appendSkinnedPart(
        context, node, output,
        paint::roundedRectFill(
            grip_bounds,
            uniformPartRadii(node, "window-resize-grip-radius", 0.0f),
            nodeColor(node, "window-resize-grip-color",
                      {0.32f, 0.37f, 0.46f, 0.85f})),
        grip_bounds, "window-resize-grip", opacity);
  }
  const math::Color glyph = nodeColor(node, "window-button-glyph-color",
                                      {0.95f, 0.97f, 1.0f, 1.0f});
  if (nonEmpty(node.window_collapse_button)) {
    const Rect& rect = node.window_collapse_button;
    appendQuad(output,
               {rect.x + rect.width * 0.28f,
                rect.y + rect.height * 0.52f,
                rect.width * 0.44f,
                std::max(1.0f, rect.height * 0.06f)},
               clip, context.scale_x, context.scale_y, context.frame.framebuffer_width,
               context.frame.framebuffer_height, glyph, opacity);
  }
  if (nonEmpty(node.window_close_button)) {
    const Rect& rect = node.window_close_button;
    const float thickness = std::max(1.0f, rect.width * 0.07f);
    // A compact plus-like close glyph remains clear at low DPI without
    // requiring rotated geometry.
    appendQuad(output,
               {rect.x + rect.width * 0.28f,
                rect.y + rect.height * 0.5f - thickness * 0.5f,
                rect.width * 0.44f, thickness},
               clip, context.scale_x, context.scale_y, context.frame.framebuffer_width,
               context.frame.framebuffer_height, glyph, opacity);
    appendQuad(output,
               {rect.x + rect.width * 0.5f - thickness * 0.5f,
                rect.y + rect.height * 0.28f,
                thickness, rect.height * 0.44f},
               clip, context.scale_x, context.scale_y, context.frame.framebuffer_width,
               context.frame.framebuffer_height, glyph, opacity);
  }
}

void drawNode(PaintContext& context,
              const Node& node,
              rendering::UIDrawData& output,
              float inherited_opacity = 1.0f,
              bool overlay_subtree = false);

void drawNodeUncached(PaintContext& context,
                      const Node& node,
                      rendering::UIDrawData& output,
                      float inherited_opacity,
                      bool overlay_subtree) {
  if (!node.present || node.collapsed_hidden ||
      styleString(node, "display", "block") == "none") return;
  if (!overlay_subtree && isOverlayTransientRoot(node)) return;
  const std::size_t first_vertex = output.vertices.size();
  const float opacity = inherited_opacity *
                        std::clamp(styleFloat(node, "opacity", 1.0f), 0.0f, 1.0f);
  const paint::Rect paint_box{node.layout.x, node.layout.y,
                              node.layout.width, node.layout.height};
  const paint::CornerRadii radii =
      nodeCornerRadii(node, context.logical_width, context.logical_height);
  const paint::BorderWidths border_widths =
      nodeBorderWidths(node, context.logical_width, context.logical_height);
  const math::Color background = nodeColor(node, "background-color", {0, 0, 0, 0});
  const bool part_painted_surface = node.tag == "toggle" ||
                                    node.tag == "slider" ||
                                    node.tag == "progress";
  if (background.a > 0.0f && !part_painted_surface) {
    if (radii.top_left > 0.0f || radii.top_right > 0.0f ||
        radii.bottom_right > 0.0f || radii.bottom_left > 0.0f) {
      appendPaintMesh(output, paint::roundedRectFill(paint_box, radii, background),
                      node.clip, context.scale_x, context.scale_y,
                      context.frame.framebuffer_width, context.frame.framebuffer_height, opacity);
    } else {
      appendQuad(output, node.layout, node.clip, context.scale_x, context.scale_y,
                 context.frame.framebuffer_width, context.frame.framebuffer_height,
                 background, opacity);
    }
  }
  drawWindowChrome(context, node, output, opacity);

  std::string gradient_source;
  if (const auto found = node.style.find("background-image");
      found != node.style.end()) {
    gradient_source = trim(found->second);
  } else if (const auto found = node.style.find("background");
             found != node.style.end()) {
    gradient_source = trim(found->second);
  }
  if (lower(gradient_source).starts_with("linear-gradient(")) {
    paint::LinearGradient gradient;
    if (paint::parseLinearGradient(gradient_source, gradient)) {
      appendPaintMesh(output, paint::linearGradientFill(paint_box, gradient),
                      node.clip, context.scale_x, context.scale_y,
                      context.frame.framebuffer_width, context.frame.framebuffer_height, opacity);
    }
  } else if (lower(gradient_source).starts_with("radial-gradient(")) {
    paint::RadialGradient gradient;
    if (paint::parseRadialGradient(gradient_source, gradient)) {
      appendPaintMesh(output, paint::radialGradientFill(paint_box, gradient),
                      node.clip, context.scale_x, context.scale_y,
                      context.frame.framebuffer_width, context.frame.framebuffer_height, opacity);
    }
  } else if (const auto background_asset = styleAssetKey(node, "background-image")) {
    const rendering::UITextureHandle background_texture =
        resolveAssetTexture(context, node, *background_asset, node.layout);
    if (background_texture != 0u) {
      appendQuad(output, node.layout, node.clip, context.scale_x, context.scale_y,
                 context.frame.framebuffer_width, context.frame.framebuffer_height,
                 {1, 1, 1, 1}, opacity, background_texture, nodeSampler(node));
    }
  }

  const rendering::UITextureHandle texture = resolveNodeTexture(context, node);
  if (texture != 0) {
    const float font_size = nodeFontSize(node);
    const float left = border_widths.left +
        oneBoxValue(node, "padding", "left", node.layout.width,
                    context.logical_width, context.logical_height, font_size);
    const float top = border_widths.top +
        oneBoxValue(node, "padding", "top", node.layout.width,
                    context.logical_width, context.logical_height, font_size);
    const float right = border_widths.right +
        oneBoxValue(node, "padding", "right", node.layout.width,
                    context.logical_width, context.logical_height, font_size);
    const float bottom = border_widths.bottom +
        oneBoxValue(node, "padding", "bottom", node.layout.width,
                    context.logical_width, context.logical_height, font_size);
    const paint::Rect content_box{
        node.layout.x + left,
        node.layout.y + top,
        std::max(0.0f, node.layout.width - left - right),
        std::max(0.0f, node.layout.height - top - bottom)};
    paint::ObjectFit fit = paint::ObjectFit::Fill;
    if (const auto parsed = paint::parseObjectFit(
            styleString(node, "object-fit", "fill"))) {
      fit = *parsed;
    }
    paint::ObjectPosition position;
    if (const auto found = node.style.find("object-position");
        found != node.style.end()) {
      (void)paint::parseObjectPosition(found->second, position);
    }
    const paint::Vec2 intrinsic =
        nodeImageIntrinsicSize(context, node).value_or(
            paint::Vec2{content_box.width, content_box.height});
    const paint::ObjectPlacement placement =
        paint::placeObject(content_box, intrinsic, fit, position);
    appendQuad(output,
               {placement.destination.x, placement.destination.y,
                placement.destination.width, placement.destination.height},
               intersectRects(node.clip,
                              {content_box.x, content_box.y,
                               content_box.width, content_box.height}),
               context.scale_x, context.scale_y, context.frame.framebuffer_width,
               context.frame.framebuffer_height, {1, 1, 1, 1}, opacity, texture,
               nodeSampler(node), rendering::UITextureMode::Color,
               placement.uv.x, placement.uv.y,
               placement.uv.x + placement.uv.width,
               placement.uv.y + placement.uv.height);
  }

  bool painted_border_image = false;
  if (const auto border_asset = styleAssetKey(node, "border-image-source")) {
    const auto slice_value = node.style.find("border-image-slice");
    paint::Insets source_slices;
    if (slice_value != node.style.end() &&
        paint::parseInsets(slice_value->second, source_slices)) {
      Node source;
      source.image = ImageSource::asset(*border_asset);
      const paint::Vec2 source_size =
          nodeImageIntrinsicSize(context, source).value_or(paint::Vec2{});
      std::optional<paint::Insets> destination_slices;
      if (const auto width = node.style.find("border-image-width");
          width != node.style.end()) {
        paint::Insets parsed_width;
        if (paint::parseInsets(width->second, parsed_width)) {
          destination_slices = parsed_width;
        }
      }
      const rendering::UITextureHandle border_texture =
          resolveAssetTexture(context, node, *border_asset, node.layout);
      if (border_texture != 0u) {
        paint::NineSliceRepeat repeat;
        if (const auto authored_repeat =
                node.style.find("border-image-repeat");
            authored_repeat != node.style.end()) {
          (void)paint::parseNineSliceRepeat(authored_repeat->second, repeat);
        }
        paint::NineSliceBuildStatus nine_slice_status;
        const paint::Mesh panel = paint::nineSlicePanel(
            paint_box,
            {.source_size = source_size,
             .source_slices = source_slices,
             .destination_slices = destination_slices,
             .repeat = repeat,
             .pixel_scale = {context.scale_x, context.scale_y}},
            {}, &nine_slice_status);
        reportNineSliceLimit(context, node, nine_slice_status);
        if (!panel.empty()) {
          appendPaintMesh(output, panel, node.clip, context.scale_x, context.scale_y,
                          context.frame.framebuffer_width, context.frame.framebuffer_height,
                          opacity, border_texture, nodeSampler(node));
          painted_border_image = true;
        }
      }
    }
  }
  const bool has_border = border_widths.left > 0.0f || border_widths.top > 0.0f ||
                          border_widths.right > 0.0f ||
                          border_widths.bottom > 0.0f;
  if (has_border && !painted_border_image) {
    appendPaintMesh(output,
                    paint::roundedRectBorder(
                        paint_box, radii, border_widths,
                        nodeColor(node, "border-color", {1, 1, 1, 1})),
                    node.clip, context.scale_x, context.scale_y,
                    context.frame.framebuffer_width, context.frame.framebuffer_height, opacity);
  }
  if (node.tag == "toggle") {
    widget_paint::ToggleStyle style;
    style.track_color = nodeColor(
        node, "control-track-color",
        nodeColor(node, "background-color", {0.12f, 0.15f, 0.20f, 1.0f}));
    style.checked_track_color = style.track_color;
    style.checkmark_color = nodeColor(
        node, "toggle-checkmark-color",
        nodeColor(node, "color", {1.0f, 1.0f, 1.0f, 1.0f}));
    style.track_radii = uniformPartRadii(
        node, "control-track-radius",
        std::max(0.0f, std::min(node.layout.width, node.layout.height) * 0.18f));
    style.track_inset =
        std::max(0.0f, styleFloat(node, "control-track-inset", 0.0f));
    style.checkmark_inset = std::max(
        0.0f, styleFloat(node, "toggle-checkmark-inset",
                         std::min(node.layout.width, node.layout.height) * 0.2f));
    style.checkmark_thickness = std::max(
        0.5f, styleFloat(node, "toggle-checkmark-thickness", 2.0f));
    const widget_paint::TogglePaintResult painted = widget_paint::toggle(
        {.bounds = paint_box, .checked = node.checked, .style = style});
    appendSkinnedPart(context, node, output, painted.track,
                      painted.track_bounds, "control-track", opacity);
    appendPaintMesh(output, painted.checkmark, node.clip, context.scale_x,
                    context.scale_y, context.frame.framebuffer_width,
                    context.frame.framebuffer_height, opacity);
  } else if (node.tag == "slider" || node.tag == "progress") {
    double minimum = 0.0;
    double maximum = 1.0;
    if (const auto found = node.attributes.find("min"); found != node.attributes.end()) {
      minimum = parseFiniteDouble(found->second).value_or(minimum);
    }
    if (const auto found = node.attributes.find("max"); found != node.attributes.end()) {
      maximum = parseFiniteDouble(found->second).value_or(maximum);
    }
    double value = node.control_value.asNumber().value_or(minimum);
    if (const auto authored = node.attributes.find("value");
        authored != node.attributes.end() && node.control_value.isNull()) {
      value = parseFiniteDouble(authored->second).value_or(value);
    }
    const float fraction = maximum != minimum
                               ? static_cast<float>(std::clamp((value - minimum) /
                                                                  (maximum - minimum),
                                                              0.0, 1.0))
                               : 0.0f;
    const bool vertical = lower(styleString(
        node, "orientation",
        node.attributes.contains("orientation")
            ? node.attributes.at("orientation")
            : "horizontal")) == "vertical";
    const widget_paint::Orientation orientation =
        vertical ? widget_paint::Orientation::Vertical
                 : widget_paint::Orientation::Horizontal;
    const widget_paint::LayoutDirection direction =
        styleString(node, "direction", "ltr") == "rtl"
            ? widget_paint::LayoutDirection::RightToLeft
            : widget_paint::LayoutDirection::LeftToRight;
    const math::Color track_color = nodeColor(
        node, "control-track-color",
        nodeColor(node, "background-color", {0.12f, 0.15f, 0.20f, 1.0f}));
    const math::Color fill_color = nodeColor(
        node, "control-fill-color",
        nodeColor(node, "accent-color", {0.24f, 0.56f, 1.0f, 1.0f}));
    if (node.tag == "slider") {
      widget_paint::SliderStyle style;
      style.track_color = track_color;
      style.fill_color = fill_color;
      style.thumb_color = nodeColor(
          node, "control-thumb-color", {0.94f, 0.96f, 1.0f, 1.0f});
      style.track_radii = uniformPartRadii(
          node, "control-track-radius", 2.0f);
      style.fill_radii = uniformPartRadii(
          node, "control-fill-radius", 2.0f);
      const float authored_thumb_width =
          styleFloat(node, "control-thumb-width", 16.0f);
      const float authored_thumb_height =
          styleFloat(node, "control-thumb-height", authored_thumb_width);
      style.thumb_size = std::max(
          1.0f, std::min(authored_thumb_width, authored_thumb_height));
      style.thumb_radii = uniformPartRadii(
          node, "control-thumb-radius", style.thumb_size * 0.5f);
      style.track_thickness = std::max(
          1.0f, styleFloat(node, "control-track-thickness", 4.0f));
      style.edge_inset =
          std::max(0.0f, styleFloat(node, "control-edge-inset", 0.0f));
      const widget_paint::SliderPaintResult painted = widget_paint::slider(
          {.bounds = paint_box,
           .fraction = fraction,
           .orientation = orientation,
           .direction = direction,
           .style = style});
      appendSkinnedPart(context, node, output, painted.track,
                        painted.track_bounds, "control-track", opacity);
      appendSkinnedPart(context, node, output, painted.fill,
                        painted.fill_bounds, "control-fill", opacity);
      appendSkinnedPart(context, node, output, painted.thumb,
                        painted.thumb_bounds, "control-thumb", opacity);
    } else {
      widget_paint::ProgressStyle style;
      style.track_color = track_color;
      style.fill_color = fill_color;
      style.track_radii = uniformPartRadii(
          node, "control-track-radius", 3.0f);
      style.fill_radii = uniformPartRadii(
          node, "control-fill-radius", 3.0f);
      style.inset =
          std::max(0.0f, styleFloat(node, "control-track-inset", 0.0f));
      const widget_paint::ProgressPaintResult painted = widget_paint::progress(
          {.bounds = paint_box,
           .fraction = fraction,
           .orientation = orientation,
           .direction = direction,
           .style = style});
      appendSkinnedPart(context, node, output, painted.track,
                        painted.track_bounds, "control-track", opacity);
      appendSkinnedPart(context, node, output, painted.fill,
                        painted.fill_bounds, "control-fill", opacity);
    }
  } else if (node.tag == "option") {
    const Node* select = node.parent;
    while (select != nullptr && select->tag != "select") {
      select = select->parent;
    }
    if (select != nullptr &&
        std::any_of(select->style.begin(), select->style.end(),
                    [](const auto& declaration) {
          return declaration.first.starts_with("select-option-");
        })) {
      math::Color color = nodeColor(
          *select, "select-option-color", {0.0f, 0.0f, 0.0f, 0.0f});
      if (node.hovered) {
        color = nodeColor(*select, "select-option-hover-color", color);
      }
      if (node.active) {
        color = nodeColor(*select, "select-option-active-color", color);
      }
      appendSkinnedPart(
          context, *select, output,
          paint::roundedRectFill(
              paint_box,
              uniformPartRadii(*select, "select-option-radius", 0.0f),
              color),
          paint_box, "select-option", opacity);
    }
  } else if (node.tag == "select") {
    widget_paint::ChevronStyle style;
    style.color = nodeColor(node, "select-arrow-color",
                            nodeColor(node, "color", {1, 1, 1, 1}));
    style.size = std::max(1.0f, styleFloat(node, "select-arrow-size", 12.0f));
    style.thickness =
        std::max(0.5f, styleFloat(node, "select-arrow-thickness", 2.0f));
    style.edge_inset =
        std::max(0.0f, styleFloat(node, "select-arrow-inset", 8.0f));
    const auto painted = widget_paint::selectArrow(
        {.bounds = paint_box,
         .expanded = attributeBoolean(node, "open"),
         .direction = styleString(node, "direction", "ltr") == "rtl"
                          ? widget_paint::LayoutDirection::RightToLeft
                          : widget_paint::LayoutDirection::LeftToRight,
         .style = style});
    appendSkinnedPart(context, node, output, painted.glyph,
                      painted.glyph_bounds, "select-arrow", opacity);
  } else if (node.tag == "disclosure") {
    widget_paint::ChevronStyle style;
    style.color = nodeColor(node, "disclosure-chevron-color",
                            nodeColor(node, "color", {1, 1, 1, 1}));
    style.size = std::max(
        1.0f, styleFloat(node, "disclosure-chevron-size", 12.0f));
    style.thickness = std::max(
        0.5f, styleFloat(node, "disclosure-chevron-thickness", 2.0f));
    style.edge_inset = std::max(
        0.0f, styleFloat(node, "disclosure-chevron-inset", 8.0f));
    const auto painted = widget_paint::disclosureChevron(
        {.bounds = paint_box,
         .expanded = attributeBoolean(node, "expanded"),
         .direction = styleString(node, "direction", "ltr") == "rtl"
                          ? widget_paint::LayoutDirection::RightToLeft
                          : widget_paint::LayoutDirection::LeftToRight,
         .style = style});
    appendSkinnedPart(context, node, output, painted.glyph,
                      painted.glyph_bounds, "disclosure-chevron", opacity);
  } else if (node.tag == "splitter") {
    widget_paint::SplitterGripStyle style;
    style.color = nodeColor(node, "splitter-grip-color",
                            {0.48f, 0.54f, 0.64f, 1.0f});
    style.mark_length = std::max(
        1.0f, styleFloat(node, "splitter-grip-size", 12.0f));
    style.mark_thickness = std::max(
        0.5f, styleFloat(node, "splitter-grip-thickness", 2.0f));
    style.mark_radii = uniformPartRadii(
        node, "splitter-grip-radius", 1.0f);
    const bool vertical = !node.attributes.contains("orientation") ||
                          lower(node.attributes.at("orientation")) !=
                              "horizontal";
    const auto painted = widget_paint::splitterGrip(
        {.bounds = paint_box,
         .orientation = vertical ? widget_paint::Orientation::Vertical
                                 : widget_paint::Orientation::Horizontal,
         .style = style});
    appendSkinnedPart(context, node, output, painted.grip, paint_box,
                      "splitter-grip", opacity);
  }
  drawShapedText(context, node, output, opacity);
  for (const Node* child : runtimeChildrenInPaintOrder(node)) {
    if (!child->present) continue;
    if (!overlay_subtree && node.tag == "select" && child->tag == "option") {
      continue;
    }
    drawNode(context, *child, output, opacity, overlay_subtree);
  }
  drawScrollbars(context, node, output, opacity);

  if (const auto found = node.style.find("transform"); found != node.style.end()) {
    paint::Transform transform;
    if (paint::parseTransform(found->second, transform) &&
        !transform.operations.empty()) {
      paint::ObjectPosition origin;
      if (const auto origin_style = node.style.find("transform-origin");
          origin_style != node.style.end()) {
        (void)paint::parseObjectPosition(origin_style->second, origin);
      }
      paint::Affine2D matrix = paint::composeTransform(
          transform, {node.layout.width, node.layout.height}, origin);
      matrix.tx += node.layout.x - matrix.a * node.layout.x -
                   matrix.c * node.layout.y;
      matrix.ty += node.layout.y - matrix.b * node.layout.x -
                   matrix.d * node.layout.y;
      for (std::size_t index = first_vertex; index < output.vertices.size(); ++index) {
        rendering::UIVertex& vertex = output.vertices[index];
        const paint::Vec2 transformed = paint::applyTransform(
            matrix, {vertex.x / context.scale_x, vertex.y / context.scale_y});
        vertex.x = transformed.x * context.scale_x;
        vertex.y = transformed.y * context.scale_y;
      }
    }
  }
}

void drawNode(PaintContext& context,
              const Node& node,
              rendering::UIDrawData& output,
              float inherited_opacity,
              bool overlay_subtree) {
  // A subtree containing active motion is invalidated every frame. Building
  // and then retaining every ancestor fragment would repeatedly copy the same
  // descendant vertices through each cache level. Assemble those ancestors
  // directly into the frame while unchanged sibling subtrees still append
  // from their retained fragments.
  if (context.volatile_nodes.contains(&node)) {
    node.retained_fragment.reset();
    node.retained_paint_revision = 0u;
    ++context.rebuilt_fragments;
    drawNodeUncached(context, node, output, inherited_opacity, overlay_subtree);
    return;
  }
  const bool cache_matches = node.retained_fragment != nullptr &&
      node.retained_paint_revision == node.paint_revision &&
      node.retained_resource_generation ==
          context.resources.resourceGeneration() &&
      node.retained_scale_x == context.scale_x &&
      node.retained_scale_y == context.scale_y &&
      node.retained_inherited_opacity == inherited_opacity &&
      node.retained_overlay_subtree == overlay_subtree;
  if (cache_matches) {
    node.retained_last_use_frame = context.resources.frame();
    (void)native::presentation::appendRebased(output,
                                               *node.retained_fragment);
    return;
  }

  rendering::UIDrawData fragment;
  const std::uint64_t build_resource_generation =
      context.resources.resourceGeneration();
  ++context.rebuilt_fragments;
  drawNodeUncached(context, node, fragment, inherited_opacity, overlay_subtree);
  const bool retain = native::presentation::canRetainFragment(
      fragment, context.frame.retained_paint_budget_bytes,
      build_resource_generation,
      context.resources.resourceGeneration());
  if (retain) {
    node.retained_fragment =
        std::make_unique<rendering::UIDrawData>(std::move(fragment));
    node.retained_paint_revision = node.paint_revision;
    node.retained_resource_generation = build_resource_generation;
    node.retained_last_use_frame = context.resources.frame();
    node.retained_scale_x = context.scale_x;
    node.retained_scale_y = context.scale_y;
    node.retained_inherited_opacity = inherited_opacity;
    node.retained_overlay_subtree = overlay_subtree;
    (void)native::presentation::appendRebased(output,
                                               *node.retained_fragment);
  } else {
    node.retained_fragment.reset();
    node.retained_paint_revision = 0u;
    (void)native::presentation::appendRebased(output, fragment);
  }
}

}  // namespace

BuildResult build(std::span<DocumentInstance* const> documents,
                  const FrameInputs& frame,
                  TextEngine& text_engine,
                  PresentationResources& resources,
                  rendering::UIDrawData& output,
                  DiagnosticSink diagnostics) {
  PaintContext context{.frame = frame,
                       .text_engine = text_engine,
                       .resources = resources,
                       .diagnostics = diagnostics};

  const auto mark_volatile_ancestors = [&](const Node* node) {
    for (const Node* current = node; current != nullptr;
         current = current->parent) {
      context.volatile_nodes.insert(current);
    }
  };
  for (const DocumentInstance* document : documents) {
    for (const Node* node : document->active_transition_nodes) {
      mark_volatile_ancestors(node);
    }
    for (const Node* node : document->active_animation_nodes) {
      mark_volatile_ancestors(node);
    }
  }

  const DrawDataCheckpoint checkpoint(output);
  const auto paint_documents = [&] {
    for (DocumentInstance* document : documents) {
      if (document == nullptr || document->body == nullptr) continue;
      context.document = document;
      context.logical_width = static_cast<int>(
          std::round(document->canvas_layout.layout_rect.width));
      context.logical_height = static_cast<int>(
          std::round(document->canvas_layout.layout_rect.height));
      context.scale_x =
          frame.framebuffer_scale_x * document->canvas_layout.scale_x;
      context.scale_y =
          frame.framebuffer_scale_y * document->canvas_layout.scale_y;
      drawNode(context, *document->body, output);

      if (!document->has_transients) continue;
      const std::vector<Node*>& overlays =
          transient_runtime::overlayRootsInPaintOrder(*document);
      const auto inherited_opacity = [](const Node& node) {
        float opacity = 1.0f;
        for (const Node* parent = node.parent; parent != nullptr;
             parent = parent->parent) {
          opacity *= std::clamp(styleFloat(*parent, "opacity", 1.0f),
                                0.0f, 1.0f);
        }
        return opacity;
      };
      for (const Node* overlay : overlays) {
        if (overlay->tag != "select") {
          drawNode(context, *overlay, output, inherited_opacity(*overlay),
                   true);
          continue;
        }

        std::vector<const Node*> options;
        for (const Node* child : runtimeChildrenInPaintOrder(*overlay)) {
          if (child->tag == "option" && child->present &&
              !child->collapsed_hidden) {
            options.push_back(child);
          }
        }
        const bool has_popup_skin = std::any_of(
            overlay->style.begin(), overlay->style.end(),
            [](const auto& declaration) {
              return declaration.first.starts_with("select-popup-");
            });
        if (has_popup_skin && !options.empty()) {
          Rect popup = options.front()->layout;
          for (const Node* option : options) {
            const float right =
                std::max(popup.x + popup.width,
                         option->layout.x + option->layout.width);
            const float bottom =
                std::max(popup.y + popup.height,
                         option->layout.y + option->layout.height);
            popup.x = std::min(popup.x, option->layout.x);
            popup.y = std::min(popup.y, option->layout.y);
            popup.width = right - popup.x;
            popup.height = bottom - popup.y;
          }
          const paint::Rect bounds{popup.x, popup.y, popup.width,
                                   popup.height};
          appendSkinnedPart(
              context, *overlay, output,
              paint::roundedRectFill(
                  bounds,
                  uniformPartRadii(*overlay, "select-popup-radius", 0.0f),
                  nodeColor(*overlay, "select-popup-color",
                            {0.0f, 0.0f, 0.0f, 0.0f})),
              bounds, "select-popup",
              inherited_opacity(*overlay) *
                  std::clamp(styleFloat(*overlay, "opacity", 1.0f), 0.0f,
                             1.0f));
        }
        for (const Node* option : options) {
          drawNode(context, *option, output, inherited_opacity(*option),
                   true);
        }
      }
    }
  };

  bool generation_stable = false;
  for (int attempt = 0; attempt < 2; ++attempt) {
    const std::uint64_t generation = resources.resourceGeneration();
    paint_documents();
    if (generation == resources.resourceGeneration()) {
      generation_stable = true;
      break;
    }
    checkpoint.restore(output);
  }

  std::size_t evicted_fragments = 0u;
  std::vector<Node*> roots;
  roots.reserve(documents.size());
  for (DocumentInstance* document : documents) {
    if (document != nullptr && document->body != nullptr) {
      roots.push_back(document->body.get());
    }
  }
  if (generation_stable) {
    evicted_fragments = presentation::enforceRetainedPaintBudget(
        roots, frame.retained_paint_budget_bytes);
  }
  return {.rebuilt_fragments = context.rebuilt_fragments,
          .evicted_fragments = evicted_fragments,
          .generation_stable = generation_stable};
}

}  // namespace karma::ui::native::presentation_builder
