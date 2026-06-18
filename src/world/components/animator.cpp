#include "karma/world/components/animator.h"

#include <algorithm>

namespace karma::components {

bool setAnimatorClip(AnimatorComponent& animator,
                     size_t clip_index,
                     bool reset_time,
                     float blend_duration_seconds) {
  if (clip_index >= animator.clips.size()) {
    return false;
  }

  if (blend_duration_seconds > 0.0f && clip_index != animator.current_clip_index &&
      animator.current_clip_index < animator.clips.size()) {
    animator.blend_active = true;
    animator.blend_from_clip_index = animator.current_clip_index;
    animator.blend_from_time_seconds = animator.time_seconds;
    animator.blend_elapsed_seconds = 0.0f;
    animator.blend_duration_seconds = blend_duration_seconds;
  } else {
    animator.blend_active = false;
    animator.blend_elapsed_seconds = 0.0f;
    animator.blend_duration_seconds = 0.0f;
  }

  animator.current_clip_index = clip_index;
  if (reset_time) {
    animator.time_seconds = 0.0f;
  }
  return true;
}

bool setAnimatorClip(AnimatorComponent& animator,
                     std::string_view clip_name,
                     bool reset_time,
                     float blend_duration_seconds) {
  const auto it = std::find_if(animator.clips.begin(),
                               animator.clips.end(),
                               [&](const animation::AnimationClip& clip) {
                                 return clip.name == clip_name;
                               });
  if (it == animator.clips.end()) {
    return false;
  }
  return setAnimatorClip(animator,
                         static_cast<size_t>(std::distance(animator.clips.begin(), it)),
                         reset_time,
                         blend_duration_seconds);
}

void playAnimator(AnimatorComponent& animator) {
  animator.playing = true;
}

void pauseAnimator(AnimatorComponent& animator) {
  animator.playing = false;
}

void stopAnimator(AnimatorComponent& animator) {
  animator.playing = false;
  animator.time_seconds = 0.0f;
  animator.state_time_seconds = 0.0f;
  animator.transition = AnimatorTransitionRuntime{};
  animator.blend_active = false;
  animator.root_motion_delta = animation::SampledTransform{};
}

AnimatorParameter* findAnimatorParameter(AnimatorComponent& animator, std::string_view name) {
  const auto it = std::find_if(animator.state_machine.parameters.begin(),
                               animator.state_machine.parameters.end(),
                               [&](const AnimatorParameter& parameter) {
                                 return parameter.name == name;
                               });
  return it == animator.state_machine.parameters.end() ? nullptr : &*it;
}

const AnimatorParameter* findAnimatorParameter(const AnimatorComponent& animator,
                                               std::string_view name) {
  const auto it = std::find_if(animator.state_machine.parameters.begin(),
                               animator.state_machine.parameters.end(),
                               [&](const AnimatorParameter& parameter) {
                                 return parameter.name == name;
                               });
  return it == animator.state_machine.parameters.end() ? nullptr : &*it;
}

bool setAnimatorBool(AnimatorComponent& animator, std::string_view name, bool value) {
  AnimatorParameter* parameter = findAnimatorParameter(animator, name);
  if (parameter == nullptr || parameter->type != AnimatorParameterType::Bool) {
    return false;
  }
  parameter->bool_value = value;
  return true;
}

bool setAnimatorInt(AnimatorComponent& animator, std::string_view name, int value) {
  AnimatorParameter* parameter = findAnimatorParameter(animator, name);
  if (parameter == nullptr || parameter->type != AnimatorParameterType::Int) {
    return false;
  }
  parameter->int_value = value;
  return true;
}

bool setAnimatorFloat(AnimatorComponent& animator, std::string_view name, float value) {
  AnimatorParameter* parameter = findAnimatorParameter(animator, name);
  if (parameter == nullptr || parameter->type != AnimatorParameterType::Float) {
    return false;
  }
  parameter->float_value = value;
  return true;
}

bool setAnimatorTrigger(AnimatorComponent& animator, std::string_view name) {
  AnimatorParameter* parameter = findAnimatorParameter(animator, name);
  if (parameter == nullptr || parameter->type != AnimatorParameterType::Trigger) {
    return false;
  }
  parameter->trigger_value = true;
  return true;
}

bool resetAnimatorTrigger(AnimatorComponent& animator, std::string_view name) {
  AnimatorParameter* parameter = findAnimatorParameter(animator, name);
  if (parameter == nullptr || parameter->type != AnimatorParameterType::Trigger) {
    return false;
  }
  parameter->trigger_value = false;
  return true;
}

animation::SampledTransform consumeRootMotionDelta(RootMotionComponent& root_motion) {
  animation::SampledTransform out = root_motion.delta;
  root_motion.delta = animation::SampledTransform{};
  root_motion.has_unconsumed_delta = false;
  return out;
}

}  // namespace karma::components
