#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "karma/simulation/animation/animation_clip.h"
#include "karma/world/ecs/component.h"
#include "karma/world/ecs/entity.h"

namespace karma::components {

enum class AnimatorParameterType : uint8_t {
  Bool,
  Int,
  Float,
  Trigger,
};

enum class AnimatorConditionOp : uint8_t {
  If,
  IfNot,
  Equals,
  NotEquals,
  Greater,
  GreaterOrEqual,
  Less,
  LessOrEqual,
};

enum class AnimatorMotionType : uint8_t {
  Clip,
  BlendTree1D,
};

enum class AnimatorInterruptPolicy : uint8_t {
  None,
  Source,
  Destination,
  SourceThenDestination,
  Any,
};

enum class RootMotionMode : uint8_t {
  Disabled,
  ApplyToLocalTransform,
  ExposeDelta,
};

struct AnimatorParameter {
  std::string name;
  AnimatorParameterType type = AnimatorParameterType::Float;
  bool bool_value = false;
  int int_value = 0;
  float float_value = 0.0f;
  bool trigger_value = false;
};

struct AnimatorCondition {
  std::string parameter;
  AnimatorConditionOp op = AnimatorConditionOp::If;
  bool bool_value = false;
  int int_value = 0;
  float float_value = 0.0f;
};

struct AnimatorBlendTree1DChild {
  uint32_t clip_index = animation::kInvalidAnimationIndex;
  float threshold = 0.0f;
  float speed = 1.0f;
};

struct AnimatorBlendTree1D {
  std::string parameter;
  std::vector<AnimatorBlendTree1DChild> children;
};

struct AnimatorTransition {
  uint32_t to_state_index = animation::kInvalidAnimationIndex;
  std::vector<AnimatorCondition> conditions;
  float duration_seconds = 0.0f;
  bool has_exit_time = false;
  float exit_time_normalized = 1.0f;
  AnimatorInterruptPolicy interrupt_policy = AnimatorInterruptPolicy::None;
};

struct AnimatorState {
  std::string name;
  AnimatorMotionType motion_type = AnimatorMotionType::Clip;
  uint32_t clip_index = animation::kInvalidAnimationIndex;
  AnimatorBlendTree1D blend_tree;
  float speed = 1.0f;
  bool loop = true;
  std::vector<AnimatorTransition> transitions;
};

struct AnimatorStateMachine {
  std::vector<AnimatorParameter> parameters;
  std::vector<AnimatorState> states;
  uint32_t entry_state_index = 0;
};

struct AnimatorTransitionRuntime {
  bool active = false;
  uint32_t from_state_index = animation::kInvalidAnimationIndex;
  uint32_t to_state_index = animation::kInvalidAnimationIndex;
  float elapsed_seconds = 0.0f;
  float duration_seconds = 0.0f;
  float from_time_seconds = 0.0f;
  float to_time_seconds = 0.0f;
};

struct AnimatorEventRecord {
  std::string name;
  std::string payload;
  uint32_t clip_index = animation::kInvalidAnimationIndex;
  uint32_t state_index = animation::kInvalidAnimationIndex;
  float time_seconds = 0.0f;
};

struct AnimatorComponent : ecs::ComponentTag {
  std::vector<animation::AnimationClip> clips;
  std::vector<ecs::Entity> node_entities_by_index;
  std::vector<animation::Skeleton> skeletons;
  std::vector<animation::Skin> skins;

  size_t current_clip_index = 0;
  float time_seconds = 0.0f;
  float speed = 1.0f;
  bool loop = true;
  bool playing = false;

  bool blend_active = false;
  size_t blend_from_clip_index = 0;
  float blend_from_time_seconds = 0.0f;
  float blend_elapsed_seconds = 0.0f;
  float blend_duration_seconds = 0.0f;

  AnimatorStateMachine state_machine;
  uint32_t current_state_index = animation::kInvalidAnimationIndex;
  float state_time_seconds = 0.0f;
  AnimatorTransitionRuntime transition;

  RootMotionMode root_motion_mode = RootMotionMode::Disabled;
  uint32_t root_motion_node_index = animation::kInvalidAnimationIndex;
  animation::SampledTransform root_motion_delta;
  animation::SampledTransform root_motion_accumulated;

  std::vector<AnimatorEventRecord> event_queue;
};

bool setAnimatorClip(AnimatorComponent& animator,
                     size_t clip_index,
                     bool reset_time = true,
                     float blend_duration_seconds = 0.0f);
bool setAnimatorClip(AnimatorComponent& animator,
                     std::string_view clip_name,
                     bool reset_time = true,
                     float blend_duration_seconds = 0.0f);
void playAnimator(AnimatorComponent& animator);
void pauseAnimator(AnimatorComponent& animator);
void stopAnimator(AnimatorComponent& animator);

AnimatorParameter* findAnimatorParameter(AnimatorComponent& animator, std::string_view name);
const AnimatorParameter* findAnimatorParameter(const AnimatorComponent& animator,
                                               std::string_view name);
bool setAnimatorBool(AnimatorComponent& animator, std::string_view name, bool value);
bool setAnimatorInt(AnimatorComponent& animator, std::string_view name, int value);
bool setAnimatorFloat(AnimatorComponent& animator, std::string_view name, float value);
bool setAnimatorTrigger(AnimatorComponent& animator, std::string_view name);
bool resetAnimatorTrigger(AnimatorComponent& animator, std::string_view name);

}  // namespace karma::components
