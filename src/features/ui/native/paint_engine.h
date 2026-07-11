#pragma once

#include <karma/math.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace karma::ui::paint {

/// A point or size in KSS logical pixels.
struct Vec2 {
  float x = 0.0f;
  float y = 0.0f;

  friend constexpr bool operator==(Vec2, Vec2) = default;
};

/// A logical-coordinate rectangle.
struct Rect {
  float x = 0.0f;
  float y = 0.0f;
  float width = 0.0f;
  float height = 0.0f;

  friend constexpr bool operator==(Rect, Rect) = default;
};

/// A renderer-neutral UI vertex. UVs are normalized and colors are straight alpha.
struct Vertex {
  Vec2 position;
  Vec2 uv;
  math::Color color;
};

/// Indexed triangles in logical coordinates.
struct Mesh {
  std::vector<Vertex> vertices;
  std::vector<std::uint32_t> indices;

  [[nodiscard]] bool empty() const noexcept { return indices.empty(); }
  void clear() noexcept;
  /// Appends another mesh while rebasing its indices. Returns false on overflow.
  [[nodiscard]] bool append(const Mesh& other);
  /// Checks finite attributes, triangle alignment, and index bounds.
  [[nodiscard]] bool valid() const noexcept;
};

/// Circular radii for the four corners. Oversized values are CSS-normalized.
struct CornerRadii {
  float top_left = 0.0f;
  float top_right = 0.0f;
  float bottom_right = 0.0f;
  float bottom_left = 0.0f;

  friend constexpr bool operator==(CornerRadii, CornerRadii) = default;
};

/// Parses one-to-four non-negative circular radii in CSS corner order.
[[nodiscard]] bool parseCornerRadii(std::string_view source,
                                    CornerRadii& output,
                                    std::string* error = nullptr);

struct BorderWidths {
  float left = 0.0f;
  float top = 0.0f;
  float right = 0.0f;
  float bottom = 0.0f;
};

/// Parses one-to-four non-negative border widths in CSS side order.
[[nodiscard]] bool parseBorderWidths(std::string_view source,
                                     BorderWidths& output,
                                     std::string* error = nullptr);

/// Applies CSS's proportional radius reduction to a border box.
[[nodiscard]] CornerRadii normalizeCornerRadii(Rect box, CornerRadii radii);

/// Generates a rounded fill as a triangle fan. `corner_segments` is per quarter.
[[nodiscard]] Mesh roundedRectFill(Rect box,
                                   CornerRadii radii,
                                   math::Color color,
                                   std::size_t corner_segments = 6u);

/// Generates the area between the outside and inside rounded border curves.
[[nodiscard]] Mesh roundedRectBorder(Rect box,
                                     CornerRadii radii,
                                     BorderWidths widths,
                                     math::Color color,
                                     std::size_t corner_segments = 6u);

struct GradientStop {
  float offset = 0.0f;  ///< Normalized distance. Values are resolved monotonically.
  math::Color color;
};

struct LinearGradient {
  /// CSS angle: 0deg points up, 90deg right, and 180deg down.
  float angle_degrees = 180.0f;
  std::vector<GradientStop> stops;
};

struct RadialGradient {
  /// Center and radii are fractions of the painted rectangle.
  Vec2 center{0.5f, 0.5f};
  Vec2 radius{0.5f, 0.5f};
  /// A circle uses half the shorter box dimension for both physical radii.
  bool circle = false;
  std::vector<GradientStop> stops;
};

/// Parses the practical KSS linear-gradient() form, including `to` directions.
[[nodiscard]] bool parseLinearGradient(std::string_view source,
                                       LinearGradient& output,
                                       std::string* error = nullptr);

/// Parses radial-gradient() with circle/ellipse and an optional `at` position.
[[nodiscard]] bool parseRadialGradient(std::string_view source,
                                       RadialGradient& output,
                                       std::string* error = nullptr);

/// Splits a rectangle into exact piecewise-linear gradient polygons.
[[nodiscard]] Mesh linearGradientFill(Rect box, const LinearGradient& gradient);

/// Tessellates a radial gradient over the complete box (including its corners).
[[nodiscard]] Mesh radialGradientFill(Rect box,
                                      const RadialGradient& gradient,
                                      std::size_t columns = 16u,
                                      std::size_t rows = 16u);

struct Insets {
  float left = 0.0f;
  float top = 0.0f;
  float right = 0.0f;
  float bottom = 0.0f;
};

/// Parses one-to-four non-negative px or unitless inset values in CSS order.
[[nodiscard]] bool parseInsets(std::string_view source,
                               Insets& output,
                               std::string* error = nullptr);

enum class NineSliceRepeatMode {
  Stretch,
  Repeat,
  Round,
};

struct NineSliceRepeat {
  NineSliceRepeatMode horizontal = NineSliceRepeatMode::Stretch;
  NineSliceRepeatMode vertical = NineSliceRepeatMode::Stretch;

  friend constexpr bool operator==(NineSliceRepeat, NineSliceRepeat) = default;
};

/// Parses one repeat mode for both axes or horizontal and vertical modes.
[[nodiscard]] bool parseNineSliceRepeat(std::string_view source,
                                        NineSliceRepeat& output,
                                        std::string* error = nullptr);

struct NineSlice {
  Vec2 source_size;
  Insets source_slices;
  /// If absent, source slice sizes are also used in logical destination pixels.
  std::optional<Insets> destination_slices;
  Rect source_uv{0.0f, 0.0f, 1.0f, 1.0f};
  NineSliceRepeat repeat{};
  /// Logical-to-framebuffer scale used to snap repeated-cell endpoints.
  Vec2 pixel_scale{1.0f, 1.0f};
};

inline constexpr std::size_t kNineSliceCellLimit = 16'384u;

struct NineSliceBuildStatus {
  std::size_t generated_cells = 0u;
  bool cell_limit_reduced = false;
};

/// Produces the nine textured panel regions, tessellating repeated cells on CPU.
/// The all-stretch compatibility path retains its shared 4x4 vertex grid.
[[nodiscard]] Mesh nineSlicePanel(Rect destination,
                                  const NineSlice& slice,
                                  math::Color tint = {},
                                  NineSliceBuildStatus* status = nullptr);

enum class ObjectFit {
  Fill,
  Contain,
  Cover,
  None,
  ScaleDown,
};

[[nodiscard]] std::optional<ObjectFit> parseObjectFit(std::string_view source);

/// Normalized alignment: (0,0) is left/top and (1,1) is right/bottom.
struct ObjectPosition {
  float x = 0.5f;
  float y = 0.5f;
};

[[nodiscard]] bool parseObjectPosition(std::string_view source,
                                       ObjectPosition& output,
                                       std::string* error = nullptr);

struct ObjectPlacement {
  Rect destination;
  Rect uv{0.0f, 0.0f, 1.0f, 1.0f};
};

/// Resolves replaced-content sizing. Cover crops UVs; other overflowing modes rely
/// on the caller's normal overflow clip.
[[nodiscard]] ObjectPlacement placeObject(Rect content_box,
                                          Vec2 intrinsic_size,
                                          ObjectFit fit,
                                          ObjectPosition position = {});

enum class LengthUnit {
  Pixels,
  Percent,
};

struct TransformLength {
  float value = 0.0f;
  LengthUnit unit = LengthUnit::Pixels;
};

enum class TransformOpKind {
  Translate,
  Scale,
  Rotate,
};

struct TransformOp {
  TransformOpKind kind = TransformOpKind::Translate;
  TransformLength x;
  TransformLength y;
  float angle_degrees = 0.0f;
};

struct Transform {
  std::vector<TransformOp> operations;
};

/// Parses translate/translateX/Y, scale/scaleX/Y, and rotate function lists.
[[nodiscard]] bool parseTransform(std::string_view source,
                                  Transform& output,
                                  std::string* error = nullptr);

/// Row-independent affine matrix: x'=a*x+c*y+tx, y'=b*x+d*y+ty.
struct Affine2D {
  float a = 1.0f;
  float b = 0.0f;
  float c = 0.0f;
  float d = 1.0f;
  float tx = 0.0f;
  float ty = 0.0f;
};

/// Composes CSS transform functions around a normalized transform origin.
[[nodiscard]] Affine2D composeTransform(const Transform& transform,
                                        Vec2 reference_size,
                                        ObjectPosition origin = {});
[[nodiscard]] Vec2 applyTransform(Affine2D transform, Vec2 point);
void applyTransform(Affine2D transform, Mesh& mesh);

}  // namespace karma::ui::paint
