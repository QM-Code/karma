#pragma once

#include "features/ui/native/paint_engine.h"

#include "karma/math.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>

namespace karma::ui::native {

/// Units that can be interpolated without layout context. Numeric values only
/// interpolate when both endpoints have exactly the same unit.
enum class MotionUnit : std::uint8_t {
  Number,
  Pixels,
  Percent,
  ViewportWidth,
  ViewportHeight,
  Em,
  Rem,
  Fraction,
  Degrees,
};

struct MotionNumber {
  double value = 0.0;
  MotionUnit unit = MotionUnit::Number;

  friend constexpr bool operator==(MotionNumber, MotionNumber) = default;
};

/// Typed value accepted by a transition track. Transform lists use the same
/// normalized representation as static painting, so animated and static
/// transforms compose identically.
using MotionValue = std::variant<MotionNumber, math::Color, paint::Transform>;

/// Parses a finite CSS-like time. `ms`, `s`, and unitless zero are accepted.
/// Negative values are retained so the caller can allow them for delays while
/// rejecting them for durations.
[[nodiscard]] std::optional<double> parseTimeSeconds(std::string_view source);

enum class Easing : std::uint8_t {
  Linear,
  Ease,
  EaseIn,
  EaseOut,
  EaseInOut,
};

[[nodiscard]] std::optional<Easing> parseEasing(std::string_view source);

/// Evaluates the named CSS cubic-bezier at a normalized time. Input and output
/// are clamped to [0, 1].
[[nodiscard]] double evaluateEasing(Easing easing, double progress);

[[nodiscard]] std::optional<MotionNumber> parseMotionNumber(
    std::string_view source);
[[nodiscard]] std::optional<math::Color> parseMotionColor(
    std::string_view source);
[[nodiscard]] std::optional<paint::Transform> parseMotionTransform(
    std::string_view source);
[[nodiscard]] std::optional<MotionValue> parseMotionValue(
    std::string_view source);

/// Interpolates compatible endpoints. A type or numeric-unit mismatch returns
/// nullopt, allowing an adapter to apply the new value discretely.
[[nodiscard]] std::optional<MotionValue> interpolateMotionValue(
    const MotionValue& from,
    const MotionValue& to,
    double progress);

/// Produces a KSS-compatible spelling for a typed motion value.
[[nodiscard]] std::string serializeMotionValue(const MotionValue& value);

struct TransitionSpec {
  std::string property = "all";
  double duration_seconds = 0.0;
  double delay_seconds = 0.0;
  Easing easing = Easing::Ease;
};

/// Parses comma-separated `transition` shorthand items. Each item accepts a
/// property, duration, named easing, and delay in CSS order-independent form.
[[nodiscard]] bool parseTransitionShorthand(
    std::string_view source,
    std::vector<TransitionSpec>& output,
    std::string* error = nullptr);

/// Parses the four transition longhands. Empty strings use CSS initial values;
/// shorter duration/easing/delay lists repeat to match transition-property.
[[nodiscard]] bool parseTransitionLonghands(
    std::string_view properties,
    std::string_view durations,
    std::string_view easing_functions,
    std::string_view delays,
    std::vector<TransitionSpec>& output,
    std::string* error = nullptr);

/// Resolves a transition for a property, preferring an exact match over `all`.
[[nodiscard]] const TransitionSpec* findTransition(
    const std::vector<TransitionSpec>& transitions,
    std::string_view property);

enum class RetargetResult : std::uint8_t {
  Unchanged,
  Started,
  AppliedImmediately,
};

/// Stateful transition for one computed property. Retargeting samples the
/// current interpolated value first, so interrupted transitions are continuous.
class TransitionTrack {
 public:
  explicit TransitionTrack(MotionValue initial_value);

  /// UI-clock timestamps are expressed in seconds. `motion_scale` scales both
  /// duration and delay; zero (or an invalid scale) applies the target now.
  RetargetResult retarget(MotionValue target,
                          const TransitionSpec& spec,
                          double now_seconds,
                          double motion_scale = 1.0);

  [[nodiscard]] MotionValue valueAt(double now_seconds) const;
  [[nodiscard]] bool active(double now_seconds) const;
  [[nodiscard]] const MotionValue& target() const { return target_; }

  /// Cancels animation and makes `value` both the current and target value.
  void reset(MotionValue value);

 private:
  MotionValue from_;
  MotionValue target_;
  double start_seconds_ = 0.0;
  double duration_seconds_ = 0.0;
  double delay_seconds_ = 0.0;
  Easing easing_ = Easing::Ease;
  bool running_ = false;
};

/// One declaration retained from a keyframe block. Source text is preserved so
/// future adapters can handle discrete or richer values; motion_value is set
/// for numeric, color, and supported 2D transform values sampled by this module.
struct KeyframeDeclaration {
  std::string source_value;
  std::optional<MotionValue> motion_value;
};

using KeyframeDeclarations =
    std::unordered_map<std::string, KeyframeDeclaration>;
using MotionProperties = std::unordered_map<std::string, MotionValue>;

struct Keyframe {
  double offset = 0.0;  // Normalized [0, 1].
  KeyframeDeclarations declarations;
};

struct Keyframes {
  std::string name;
  std::vector<Keyframe> frames;
};

/// Extracts every `@keyframes` block from KSS. Selectors may be `from`, `to`,
/// percentages, or comma-separated combinations. Duplicate offsets merge in
/// source order; missing 0%/100% endpoints copy the nearest declared frame.
[[nodiscard]] bool parseKeyframes(std::string_view source,
                                  std::vector<Keyframes>& output,
                                  std::string* error = nullptr);

/// Finds the last definition with a matching name, mirroring source-order
/// replacement when stylesheets contain duplicate @keyframes names.
[[nodiscard]] const Keyframes* findKeyframes(
    const std::vector<Keyframes>& keyframes,
    std::string_view name);

/// Samples all typed numeric/color/transform declarations at a normalized keyframe
/// offset. Easing is evaluated independently within each surrounding frame
/// segment; incompatible units use a discrete half-way switch.
[[nodiscard]] MotionProperties sampleKeyframesAtOffset(
    const Keyframes& keyframes,
    double offset,
    Easing easing = Easing::Linear);

enum class AnimationDirection : std::uint8_t {
  Normal,
  Reverse,
  Alternate,
  AlternateReverse,
};

enum class AnimationFillMode : std::uint8_t {
  None,
  Forwards,
  Backwards,
  Both,
};

enum class AnimationPhase : std::uint8_t {
  Before,
  Active,
  After,
};

[[nodiscard]] std::optional<double> parseAnimationIterationCount(
    std::string_view source);
[[nodiscard]] std::optional<AnimationDirection> parseAnimationDirection(
    std::string_view source);
[[nodiscard]] std::optional<AnimationFillMode> parseAnimationFillMode(
    std::string_view source);

struct AnimationSpec {
  double duration_seconds = 0.0;
  double delay_seconds = 0.0;
  /// Non-negative finite values and positive infinity are supported.
  double iteration_count = 1.0;
  Easing easing = Easing::Ease;
  AnimationDirection direction = AnimationDirection::Normal;
  AnimationFillMode fill_mode = AnimationFillMode::None;
};

struct AnimationSample {
  AnimationPhase phase = AnimationPhase::Before;
  bool contributes = false;
  bool finished = false;
  std::uint64_t iteration = 0;
  /// Directed keyframe offset after normal/reverse/alternate handling.
  double offset = 0.0;
  MotionProperties values;
};

/// Samples a keyframe animation against the UI clock. Duration and delay are
/// scaled by motion_scale. A zero/invalid scale returns the finite animation's
/// final state immediately (or the first iteration's end for `infinite`).
[[nodiscard]] AnimationSample sampleAnimation(
    const Keyframes& keyframes,
    const AnimationSpec& spec,
    double start_seconds,
    double now_seconds,
    double motion_scale = 1.0);

}  // namespace karma::ui::native
