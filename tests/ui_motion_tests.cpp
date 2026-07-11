#include "features/ui/native/motion_engine.h"

#include <cassert>
#include <cmath>
#include <initializer_list>
#include <iostream>
#include <limits>
#include <string>
#include <variant>
#include <vector>

namespace {

using karma::ui::native::Easing;
using karma::ui::native::MotionNumber;
using karma::ui::native::MotionUnit;
using karma::ui::native::MotionValue;
using karma::ui::native::RetargetResult;
using karma::ui::native::TransitionSpec;
using karma::ui::native::TransitionTrack;

bool near(double left, double right, double epsilon = 1.0e-6) {
  return std::abs(left - right) <= epsilon;
}

double numberValue(const MotionValue& value) {
  return std::get<MotionNumber>(value).value;
}

const karma::ui::paint::Transform& transformValue(const MotionValue& value) {
  return std::get<karma::ui::paint::Transform>(value);
}

karma::ui::native::KeyframeDeclaration declaration(std::string value) {
  return {.source_value = value,
          .motion_value = karma::ui::native::parseMotionValue(value)};
}

karma::ui::native::Keyframe frame(
    double offset,
    std::initializer_list<std::pair<const std::string,
                                    karma::ui::native::KeyframeDeclaration>>
        declarations) {
  return {.offset = offset, .declarations = declarations};
}

void testTimesAndEasing() {
  using namespace karma::ui::native;
  assert(near(*parseTimeSeconds("250ms"), 0.25));
  assert(near(*parseTimeSeconds(" .5S "), 0.5));
  assert(near(*parseTimeSeconds("-50ms"), -0.05));
  assert(near(*parseTimeSeconds("0"), 0.0));
  assert(!parseTimeSeconds("10"));
  assert(!parseTimeSeconds("nanms"));

  assert(parseEasing("EASE-IN") == Easing::EaseIn);
  assert(!parseEasing("steps(2)"));
  assert(evaluateEasing(Easing::Linear, 0.25) == 0.25);
  assert(evaluateEasing(Easing::EaseIn, 0.5) < 0.5);
  assert(evaluateEasing(Easing::EaseOut, 0.5) > 0.5);
  assert(evaluateEasing(Easing::EaseInOut, 0.0) == 0.0);
  assert(evaluateEasing(Easing::EaseInOut, 1.0) == 1.0);
}

void testTransitionParsing() {
  using namespace karma::ui::native;
  std::vector<TransitionSpec> parsed;
  std::string error;
  assert(parseTransitionShorthand(
      "opacity 200ms ease-in 50ms, background-color 1s linear", parsed, &error));
  assert(error.empty());
  assert(parsed.size() == 2);
  assert(parsed[0].property == "opacity");
  assert(near(parsed[0].duration_seconds, 0.2));
  assert(near(parsed[0].delay_seconds, 0.05));
  assert(parsed[0].easing == Easing::EaseIn);
  assert(parsed[1].property == "background-color");
  assert(near(parsed[1].duration_seconds, 1.0));
  assert(parsed[1].easing == Easing::Linear);

  assert(parseTransitionShorthand("none", parsed, &error));
  assert(parsed.empty());
  assert(!parseTransitionShorthand("opacity -1s", parsed, &error));
  assert(!error.empty());

  assert(parseTransitionLonghands("opacity, color, width", "100ms, 1s",
                                  "linear, ease-in", "0s, -200ms", parsed,
                                  &error));
  assert(parsed.size() == 3);
  assert(near(parsed[0].duration_seconds, 0.1));
  assert(near(parsed[1].duration_seconds, 1.0));
  assert(near(parsed[2].duration_seconds, 0.1));
  assert(parsed[2].easing == Easing::Linear);
  assert(near(parsed[1].delay_seconds, -0.2));

  parsed.push_back({.property = "all", .duration_seconds = 2.0});
  assert(findTransition(parsed, "color") == &parsed[1]);
  assert(findTransition(parsed, "height") == &parsed.back());
}

void testTypedValues() {
  using namespace karma::ui::native;
  const auto pixels = parseMotionValue("12.5px");
  assert(pixels && std::get<MotionNumber>(*pixels).unit == MotionUnit::Pixels);
  assert(near(std::get<MotionNumber>(*pixels).value, 12.5));
  const auto angle = parseMotionNumber("-2deg");
  assert(angle && angle->unit == MotionUnit::Degrees && near(angle->value, -2.0));

  const auto color = parseMotionColor("#4488ff80");
  assert(color);
  assert(near(color->r, 68.0 / 255.0));
  assert(near(color->a, 128.0 / 255.0));
  assert(parseMotionColor("rgba(255, 0, 128, .5)"));

  const MotionValue start = MotionNumber{.value = 10.0, .unit = MotionUnit::Pixels};
  const MotionValue finish = MotionNumber{.value = 20.0, .unit = MotionUnit::Pixels};
  const auto midpoint = interpolateMotionValue(start, finish, 0.5);
  assert(midpoint && near(numberValue(*midpoint), 15.0));
  assert(!interpolateMotionValue(
      start, MotionNumber{.value = 20.0, .unit = MotionUnit::Percent}, 0.5));

  const MotionValue black = karma::math::Color{0, 0, 0, 0};
  const MotionValue white = karma::math::Color{1, 1, 1, 1};
  const auto gray = interpolateMotionValue(black, white, 0.5);
  assert(gray && near(std::get<karma::math::Color>(*gray).r, 0.5));
  assert(serializeMotionValue(finish) == "20px");
}

void testTransformValuesAndCompatibility() {
  using namespace karma::ui::native;
  using namespace karma::ui::paint;

  const auto start =
      parseMotionValue("translate(10px, 20%) scale(1, 2) rotate(10deg)");
  const auto finish =
      parseMotionValue("translate(30px, 60%) scale(3, 4) rotate(50deg)");
  assert(start && finish);
  assert(std::holds_alternative<Transform>(*start));
  const auto midpoint = interpolateMotionValue(*start, *finish, 0.5);
  assert(midpoint);
  const Transform& value = transformValue(*midpoint);
  assert(value.operations.size() == 3u);
  assert(value.operations[0].kind == TransformOpKind::Translate);
  assert(near(value.operations[0].x.value, 20.0));
  assert(value.operations[0].x.unit == LengthUnit::Pixels);
  assert(near(value.operations[0].y.value, 40.0));
  assert(value.operations[0].y.unit == LengthUnit::Percent);
  assert(near(value.operations[1].x.value, 2.0));
  assert(near(value.operations[1].y.value, 3.0));
  assert(near(value.operations[2].angle_degrees, 30.0));

  const std::string serialized = serializeMotionValue(*midpoint);
  assert(serialized ==
         "translate(20px, 40%) scale(2, 3) rotate(30deg)");
  assert(parseMotionTransform(serialized));
  const auto none = parseMotionTransform("none");
  assert(none && none->operations.empty());
  assert(serializeMotionValue(MotionValue(*none)) == "none");

  const auto percent = parseMotionValue("translate(30%, 60%)");
  const auto reordered = parseMotionValue("rotate(50deg) translate(30px, 60%)");
  const auto shorter = parseMotionValue("translate(30px, 60%) scale(3)");
  assert(percent && reordered && shorter);
  assert(!interpolateMotionValue(*start, *percent, 0.5));
  assert(!interpolateMotionValue(*start, *reordered, 0.5));
  assert(!interpolateMotionValue(*start, *shorter, 0.5));
}

void testInterruptedTracksAndReducedMotion() {
  const TransitionSpec linear{.property = "width",
                              .duration_seconds = 1.0,
                              .delay_seconds = 0.0,
                              .easing = Easing::Linear};
  TransitionTrack track(MotionNumber{.value = 0.0, .unit = MotionUnit::Pixels});
  assert(track.retarget(MotionNumber{.value = 10.0, .unit = MotionUnit::Pixels},
                        linear, 0.0) == RetargetResult::Started);
  assert(near(numberValue(track.valueAt(0.25)), 2.5));

  assert(track.retarget(MotionNumber{.value = 20.0, .unit = MotionUnit::Pixels},
                        linear, 0.25) == RetargetResult::Started);
  assert(near(numberValue(track.valueAt(0.25)), 2.5));
  assert(near(numberValue(track.valueAt(0.75)), 11.25));
  assert(track.active(0.75));
  assert(near(numberValue(track.valueAt(1.25)), 20.0));
  assert(!track.active(1.25));

  track.reset(MotionNumber{.value = 0.0, .unit = MotionUnit::Pixels});
  assert(track.retarget(MotionNumber{.value = 10.0, .unit = MotionUnit::Pixels},
                        linear, 0.0, 2.0) == RetargetResult::Started);
  assert(near(numberValue(track.valueAt(1.0)), 5.0));
  assert(track.retarget(MotionNumber{.value = 30.0, .unit = MotionUnit::Pixels},
                        linear, 1.0, 0.0) == RetargetResult::AppliedImmediately);
  assert(near(numberValue(track.valueAt(1.0)), 30.0));
  assert(!track.active(1.0));

  assert(track.retarget(MotionNumber{.value = 50.0, .unit = MotionUnit::Percent},
                        linear, 2.0) == RetargetResult::AppliedImmediately);
  assert(std::get<MotionNumber>(track.valueAt(2.0)).unit == MotionUnit::Percent);

  const TransitionSpec negative_delay{.property = "opacity",
                                      .duration_seconds = 1.0,
                                      .delay_seconds = -0.5,
                                      .easing = Easing::Linear};
  TransitionTrack delayed(MotionNumber{.value = 0.0, .unit = MotionUnit::Number});
  assert(delayed.retarget(MotionNumber{.value = 1.0, .unit = MotionUnit::Number},
                          negative_delay, 10.0) == RetargetResult::Started);
  assert(near(numberValue(delayed.valueAt(10.0)), 0.5));
}

void testInterruptedTransformTracksAndReducedMotion() {
  using namespace karma::ui::native;

  const auto start = parseMotionValue("translateX(0px) scale(1) rotate(0deg)");
  const auto first = parseMotionValue("translateX(100px) scale(3) rotate(90deg)");
  const auto second =
      parseMotionValue("translateX(200px) scale(5) rotate(180deg)");
  assert(start && first && second);
  const TransitionSpec linear{.property = "transform",
                              .duration_seconds = 1.0,
                              .delay_seconds = 0.0,
                              .easing = Easing::Linear};
  TransitionTrack track(*start);
  assert(track.retarget(*first, linear, 0.0) == RetargetResult::Started);
  const auto before_interrupt = transformValue(track.valueAt(0.25));
  assert(near(before_interrupt.operations[0].x.value, 25.0));
  assert(near(before_interrupt.operations[1].x.value, 1.5));
  assert(near(before_interrupt.operations[2].angle_degrees, 22.5));

  assert(track.retarget(*second, linear, 0.25) == RetargetResult::Started);
  const auto at_interrupt = transformValue(track.valueAt(0.25));
  assert(near(at_interrupt.operations[0].x.value, 25.0));
  const auto halfway = transformValue(track.valueAt(0.75));
  assert(near(halfway.operations[0].x.value, 112.5));
  assert(near(halfway.operations[1].x.value, 3.25));
  assert(near(halfway.operations[2].angle_degrees, 101.25));

  const auto reduced_target =
      parseMotionValue("translateX(300px) scale(7) rotate(270deg)");
  assert(reduced_target);
  assert(track.retarget(*reduced_target, linear, 0.75, 0.0) ==
         RetargetResult::AppliedImmediately);
  assert(serializeMotionValue(track.valueAt(0.75)) ==
         "translate(300px, 0px) scale(7, 7) rotate(270deg)");
  assert(!track.active(0.75));
}

void testKeyframeOffsetSampling() {
  using namespace karma::ui::native;
  // These are the normalized frames produced by a version-2 theme's typed
  // `motions.*.keyframes` array. Duplicate offsets are already merged and
  // omitted endpoints inherit the nearest declared frame during theme load.
  std::vector<Keyframes> animations{
      {.name = "pulse",
       .frames = {
           frame(0.0, {{"opacity", declaration("0")},
                       {"color", declaration("#000")},
                       {"display", {.source_value = "none"}}}),
           frame(0.5, {{"opacity", declaration("1")},
                       {"color", declaration("#fff")}}),
           frame(1.0, {{"opacity", declaration(".5")},
                       {"color", declaration("#000")},
                       {"display", {.source_value = "none"}}}),
       }},
      {.name = "grow",
       .frames = {
           frame(0.0, {{"width", declaration("10px")}}),
           frame(0.25, {{"width", declaration("10px")}}),
           frame(0.75, {{"width", declaration("30px")}}),
           frame(1.0, {{"width", declaration("30px")}}),
       }},
  };
  assert(animations.size() == 2);

  const Keyframes* pulse = findKeyframes(animations, "pulse");
  assert(pulse && pulse->frames.size() == 3);
  assert(near(pulse->frames[0].offset, 0.0));
  assert(near(pulse->frames[1].offset, 0.5));
  assert(near(pulse->frames[2].offset, 1.0));
  assert(pulse->frames[2].declarations.at("opacity").source_value == ".5");
  assert(pulse->frames[2].declarations.at("color").motion_value.has_value());
  assert(!pulse->frames[0].declarations.at("display").motion_value.has_value());

  auto quarter = sampleKeyframesAtOffset(*pulse, 0.25, Easing::Linear);
  assert(near(numberValue(quarter.at("opacity")), 0.5));
  const auto quarter_color = std::get<karma::math::Color>(quarter.at("color"));
  assert(near(quarter_color.r, 0.5));
  auto final = sampleKeyframesAtOffset(*pulse, 1.0, Easing::Linear);
  assert(near(numberValue(final.at("opacity")), 0.5));
  assert(!final.contains("display"));

  const Keyframes* grow = findKeyframes(animations, "grow");
  assert(grow && grow->frames.size() == 4);
  assert(near(grow->frames.front().offset, 0.0));
  assert(near(grow->frames.back().offset, 1.0));
  const auto halfway = sampleKeyframesAtOffset(*grow, 0.5);
  assert(near(numberValue(halfway.at("width")), 20.0));
}

void testTransformKeyframes() {
  using namespace karma::ui::native;
  using karma::ui::paint::LengthUnit;

  std::vector<Keyframes> parsed{
      {.name = "move",
       .frames = {
           frame(0.0,
                 {{"transform", declaration(
                                    "translate(0px, 10%) scale(1) rotate(0deg)")}}),
           frame(1.0,
                 {{"transform", declaration(
                                    "translate(100px, 30%) scale(3) rotate(90deg)")}}),
       }},
      {.name = "unit-switch",
       .frames = {
           frame(0.0, {{"transform", declaration("translateX(10px)")}}),
           frame(1.0, {{"transform", declaration("translateX(50%)")}}),
       }},
  };
  assert(parsed.size() == 2u);

  const Keyframes* move = findKeyframes(parsed, "move");
  assert(move);
  const auto midpoint = sampleKeyframesAtOffset(*move, 0.5, Easing::Linear);
  const auto& transform = transformValue(midpoint.at("transform"));
  assert(near(transform.operations[0].x.value, 50.0));
  assert(near(transform.operations[0].y.value, 20.0));
  assert(transform.operations[0].y.unit == LengthUnit::Percent);
  assert(near(transform.operations[1].x.value, 2.0));
  assert(near(transform.operations[2].angle_degrees, 45.0));

  const Keyframes* unit_switch = findKeyframes(parsed, "unit-switch");
  assert(unit_switch);
  const auto early = sampleKeyframesAtOffset(*unit_switch, 0.49, Easing::Linear);
  const auto late = sampleKeyframesAtOffset(*unit_switch, 0.5, Easing::Linear);
  assert(transformValue(early.at("transform")).operations[0].x.unit ==
         LengthUnit::Pixels);
  assert(transformValue(late.at("transform")).operations[0].x.unit ==
         LengthUnit::Percent);

  AnimationSpec reduced{.duration_seconds = 5.0,
                        .iteration_count = 1.0,
                        .easing = Easing::Linear,
                        .fill_mode = AnimationFillMode::None};
  const AnimationSample final = sampleAnimation(*move, reduced, 0.0, 0.0, 0.0);
  assert(final.finished && final.contributes);
  const auto& final_transform = transformValue(final.values.at("transform"));
  assert(near(final_transform.operations[0].x.value, 100.0));
  assert(near(final_transform.operations[2].angle_degrees, 90.0));
}

void testAnimationClockSampling() {
  using namespace karma::ui::native;
  const Keyframes fade{
      .name = "fade",
      .frames = {
          frame(0.0, {{"opacity", declaration("0")}}),
          frame(1.0, {{"opacity", declaration("1")}}),
      }};

  assert(parseAnimationIterationCount("2.5") == 2.5);
  assert(std::isinf(*parseAnimationIterationCount("infinite")));
  assert(!parseAnimationIterationCount("-1"));
  assert(parseAnimationDirection("alternate-reverse") ==
         AnimationDirection::AlternateReverse);
  assert(parseAnimationFillMode("both") == AnimationFillMode::Both);

  AnimationSpec spec{.duration_seconds = 2.0,
                     .delay_seconds = 1.0,
                     .iteration_count = 2.0,
                     .easing = Easing::Linear,
                     .direction = AnimationDirection::Normal,
                     .fill_mode = AnimationFillMode::None};
  auto before = sampleAnimation(fade, spec, 0.0, 0.5);
  assert(before.phase == AnimationPhase::Before && !before.contributes);
  spec.fill_mode = AnimationFillMode::Backwards;
  before = sampleAnimation(fade, spec, 0.0, 0.5);
  assert(before.contributes && near(numberValue(before.values.at("opacity")), 0.0));

  auto active = sampleAnimation(fade, spec, 0.0, 2.0);
  assert(active.phase == AnimationPhase::Active && active.contributes);
  assert(active.iteration == 0 && near(active.offset, 0.5));
  assert(near(numberValue(active.values.at("opacity")), 0.5));

  active = sampleAnimation(fade, spec, 0.0, 3.5);
  assert(active.iteration == 1 && near(active.offset, 0.25));
  spec.direction = AnimationDirection::Alternate;
  active = sampleAnimation(fade, spec, 0.0, 3.5);
  assert(active.iteration == 1 && near(active.offset, 0.75));
  assert(near(numberValue(active.values.at("opacity")), 0.75));

  spec.fill_mode = AnimationFillMode::Forwards;
  auto after = sampleAnimation(fade, spec, 0.0, 5.0);
  assert(after.phase == AnimationPhase::After && after.finished && after.contributes);
  assert(near(after.offset, 0.0));  // Second alternate iteration ends reversed.
  assert(near(numberValue(after.values.at("opacity")), 0.0));

  spec.direction = AnimationDirection::Reverse;
  spec.delay_seconds = 0.0;
  spec.iteration_count = 1.0;
  auto reversed_start = sampleAnimation(fade, spec, 0.0, 0.0);
  assert(near(reversed_start.offset, 1.0));
  assert(near(numberValue(reversed_start.values.at("opacity")), 1.0));

  spec.direction = AnimationDirection::Normal;
  spec.iteration_count = 1.5;
  after = sampleAnimation(fade, spec, 0.0, 3.0);
  assert(after.finished && near(after.offset, 0.5));
  assert(near(numberValue(after.values.at("opacity")), 0.5));

  spec.iteration_count = std::numeric_limits<double>::infinity();
  active = sampleAnimation(fade, spec, 0.0, 1000.5);
  assert(active.phase == AnimationPhase::Active && !active.finished);
  assert(near(active.offset, 0.25));

  spec.iteration_count = 1.0;
  spec.duration_seconds = 1.0;
  active = sampleAnimation(fade, spec, 0.0, 1.0, 2.0);
  assert(active.phase == AnimationPhase::Active && near(active.offset, 0.5));
  spec.fill_mode = AnimationFillMode::None;
  after = sampleAnimation(fade, spec, 0.0, 0.0, 0.0);
  assert(after.phase == AnimationPhase::After && after.contributes && after.finished);
  assert(near(numberValue(after.values.at("opacity")), 1.0));

  spec.easing = Easing::EaseIn;
  active = sampleAnimation(fade, spec, 0.0, 0.5);
  assert(numberValue(active.values.at("opacity")) < 0.5);
}

}  // namespace

int main() {
  testTimesAndEasing();
  testTransitionParsing();
  testTypedValues();
  testTransformValuesAndCompatibility();
  testInterruptedTracksAndReducedMotion();
  testInterruptedTransformTracksAndReducedMotion();
  testKeyframeOffsetSampling();
  testTransformKeyframes();
  testAnimationClockSampling();
  std::cout << "ui motion tests passed\n";
  return 0;
}
