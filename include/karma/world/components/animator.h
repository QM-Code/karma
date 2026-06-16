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

/// \ingroup karma_components
/// Runtime parameter storage kind for animator state machines.
enum class AnimatorParameterType : uint8_t {
  Bool,
  Int,
  Float,
  Trigger,
};

/// \ingroup karma_components
/// Comparison operation for animator transition conditions.
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

/// \ingroup karma_components
/// Motion source used by an animator state.
enum class AnimatorMotionType : uint8_t {
  Clip,
  BlendTree1D,
};

/// \ingroup karma_components
/// Policy for whether active transitions can be interrupted.
enum class AnimatorInterruptPolicy : uint8_t {
  None,
  Source,
  Destination,
  SourceThenDestination,
  Any,
};

/// \ingroup karma_components
/// How sampled root-motion deltas are exposed or applied.
enum class RootMotionMode : uint8_t {
  Disabled,
  ApplyToLocalTransform,
  ExposeDelta,
};

/// \ingroup karma_components
/// Named runtime parameter used by animator transitions and blend trees.
struct AnimatorParameter {
  std::string name;
  AnimatorParameterType type = AnimatorParameterType::Float;
  bool bool_value = false;
  int int_value = 0;
  float float_value = 0.0f;
  bool trigger_value = false;
};

/// \ingroup karma_components
/// Single transition condition evaluated against animator parameters.
struct AnimatorCondition {
  std::string parameter;
  AnimatorConditionOp op = AnimatorConditionOp::If;
  bool bool_value = false;
  int int_value = 0;
  float float_value = 0.0f;
};

/// \ingroup karma_components
/// Child clip entry in a one-dimensional blend tree.
struct AnimatorBlendTree1DChild {
  uint32_t clip_index = animation::kInvalidAnimationIndex;
  float threshold = 0.0f;
  float speed = 1.0f;
};

/// \ingroup karma_components
/// One-dimensional blend tree keyed by a float parameter.
struct AnimatorBlendTree1D {
  std::string parameter;
  std::vector<AnimatorBlendTree1DChild> children;
};

/// \ingroup karma_components
/// Transition from one animator state to another.
struct AnimatorTransition {
  uint32_t to_state_index = animation::kInvalidAnimationIndex;
  std::vector<AnimatorCondition> conditions;
  float duration_seconds = 0.0f;
  bool has_exit_time = false;
  float exit_time_normalized = 1.0f;
  AnimatorInterruptPolicy interrupt_policy = AnimatorInterruptPolicy::None;
};

/// \ingroup karma_components
/// State-machine state backed by either a clip or a one-dimensional blend tree.
struct AnimatorState {
  std::string name;
  AnimatorMotionType motion_type = AnimatorMotionType::Clip;
  uint32_t clip_index = animation::kInvalidAnimationIndex;
  AnimatorBlendTree1D blend_tree;
  float speed = 1.0f;
  bool loop = true;
  std::vector<AnimatorTransition> transitions;
};

/// \ingroup karma_components
/// Data-only animator graph stored on an entity.
struct AnimatorStateMachine {
  std::vector<AnimatorParameter> parameters;
  std::vector<AnimatorState> states;
  uint32_t entry_state_index = 0;
};

/// \ingroup karma_components
/// Runtime state for an active state-machine transition.
struct AnimatorTransitionRuntime {
  bool active = false;
  uint32_t from_state_index = animation::kInvalidAnimationIndex;
  uint32_t to_state_index = animation::kInvalidAnimationIndex;
  float elapsed_seconds = 0.0f;
  float duration_seconds = 0.0f;
  float from_time_seconds = 0.0f;
  float to_time_seconds = 0.0f;
};

/// \ingroup karma_components
/// Event emitted while sampling an animation clip/state.
struct AnimatorEventRecord {
  std::string name;
  std::string payload;
  uint32_t clip_index = animation::kInvalidAnimationIndex;
  uint32_t state_index = animation::kInvalidAnimationIndex;
  float time_seconds = 0.0f;
};

/// \ingroup karma_components
/// Full animation state machine, blending, root motion, and skinning metadata.
///
/// `AnimationSystem` consumes this component. Clips and node maps normally come
/// from GLB import. Game code changes parameters or calls helper functions to
/// drive playback.
struct AnimatorComponent : ecs::ComponentTag {
  std::vector<animation::AnimationClip> clips;
  std::vector<ecs::Entity> node_entities_by_index;
  /// Renderable morph primitive entities keyed by imported GLB node index.
  std::vector<std::vector<ecs::Entity>> morph_entities_by_node_index;
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

/// Switches animator playback to a clip by index.
bool setAnimatorClip(AnimatorComponent& animator,
                     size_t clip_index,
                     bool reset_time = true,
                     float blend_duration_seconds = 0.0f);
/// Switches animator playback to a clip by name.
bool setAnimatorClip(AnimatorComponent& animator,
                     std::string_view clip_name,
                     bool reset_time = true,
                     float blend_duration_seconds = 0.0f);
/// Starts or resumes animator playback.
void playAnimator(AnimatorComponent& animator);
/// Pauses animator playback without rewinding.
void pauseAnimator(AnimatorComponent& animator);
/// Stops animator playback and resets time.
void stopAnimator(AnimatorComponent& animator);

/// Finds a mutable animator parameter by name.
AnimatorParameter* findAnimatorParameter(AnimatorComponent& animator, std::string_view name);
/// Finds an animator parameter by name.
const AnimatorParameter* findAnimatorParameter(const AnimatorComponent& animator,
                                               std::string_view name);
/// Sets a bool parameter.
bool setAnimatorBool(AnimatorComponent& animator, std::string_view name, bool value);
/// Sets an int parameter.
bool setAnimatorInt(AnimatorComponent& animator, std::string_view name, int value);
/// Sets a float parameter.
bool setAnimatorFloat(AnimatorComponent& animator, std::string_view name, float value);
/// Raises a trigger parameter for one transition evaluation.
bool setAnimatorTrigger(AnimatorComponent& animator, std::string_view name);
/// Clears a trigger parameter manually.
bool resetAnimatorTrigger(AnimatorComponent& animator, std::string_view name);

}  // namespace karma::components
