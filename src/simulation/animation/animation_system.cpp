#include "karma/simulation/animation/animation_system.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_map>
#include <vector>

#include <glm/gtx/quaternion.hpp>

#include "karma/core/math/quat.h"
#include "karma/core/math/vec3.h"
#include "karma/simulation/animation/pose.h"
#include "karma/world/components/animation_player.h"
#include "karma/world/components/animator.h"
#include "karma/world/components/transform.h"

namespace karma::animation {

namespace {

struct WeightedClipSample {
  uint32_t clip_index = kInvalidAnimationIndex;
  uint32_t state_index = kInvalidAnimationIndex;
  float time_seconds = 0.0f;
  bool loop = true;
  float weight = 1.0f;
};

struct AccumulatedTransform {
  math::Vec3 position{};
  math::Quat rotation{0.0f, 0.0f, 0.0f, 0.0f};
  math::Vec3 scale{};
  float position_weight = 0.0f;
  float rotation_weight = 0.0f;
  float scale_weight = 0.0f;
};

math::Vec3 addVec3(const math::Vec3& a, const math::Vec3& b) {
  return {a.x + b.x, a.y + b.y, a.z + b.z};
}

math::Vec3 subtractVec3(const math::Vec3& a, const math::Vec3& b) {
  return {a.x - b.x, a.y - b.y, a.z - b.z};
}

math::Vec3 scaleVec3(const math::Vec3& v, float s) {
  return {v.x * s, v.y * s, v.z * s};
}

glm::quat toGlm(const math::Quat& q) {
  return {q.w, q.x, q.y, q.z};
}

math::Quat fromGlm(const glm::quat& q) {
  return {q.x, q.y, q.z, q.w};
}

float clipDuration(const components::AnimatorComponent& animator, uint32_t clip_index) {
  if (clip_index >= animator.clips.size()) {
    return 0.0f;
  }
  return animator.clips[clip_index].duration_seconds;
}

float stateDuration(const components::AnimatorComponent& animator,
                    const components::AnimatorState& state) {
  if (state.motion_type == components::AnimatorMotionType::Clip) {
    return clipDuration(animator, state.clip_index);
  }
  float duration = 0.0f;
  for (const components::AnimatorBlendTree1DChild& child : state.blend_tree.children) {
    duration = std::max(duration, clipDuration(animator, child.clip_index));
  }
  return duration;
}

float parameterFloat(const components::AnimatorComponent& animator, std::string_view name) {
  const components::AnimatorParameter* parameter =
      components::findAnimatorParameter(animator, name);
  if (parameter == nullptr) {
    return 0.0f;
  }
  switch (parameter->type) {
    case components::AnimatorParameterType::Bool:
      return parameter->bool_value ? 1.0f : 0.0f;
    case components::AnimatorParameterType::Int:
      return static_cast<float>(parameter->int_value);
    case components::AnimatorParameterType::Float:
      return parameter->float_value;
    case components::AnimatorParameterType::Trigger:
      return parameter->trigger_value ? 1.0f : 0.0f;
  }
  return 0.0f;
}

bool evaluateCondition(const components::AnimatorComponent& animator,
                       const components::AnimatorCondition& condition) {
  const components::AnimatorParameter* parameter =
      components::findAnimatorParameter(animator, condition.parameter);
  if (parameter == nullptr) {
    return false;
  }

  switch (condition.op) {
    case components::AnimatorConditionOp::If:
      if (parameter->type == components::AnimatorParameterType::Trigger) {
        return parameter->trigger_value;
      }
      if (parameter->type == components::AnimatorParameterType::Bool) {
        return parameter->bool_value;
      }
      return parameterFloat(animator, condition.parameter) != 0.0f;
    case components::AnimatorConditionOp::IfNot:
      if (parameter->type == components::AnimatorParameterType::Trigger) {
        return !parameter->trigger_value;
      }
      if (parameter->type == components::AnimatorParameterType::Bool) {
        return !parameter->bool_value;
      }
      return parameterFloat(animator, condition.parameter) == 0.0f;
    case components::AnimatorConditionOp::Equals:
      if (parameter->type == components::AnimatorParameterType::Bool) {
        return parameter->bool_value == condition.bool_value;
      }
      if (parameter->type == components::AnimatorParameterType::Int) {
        return parameter->int_value == condition.int_value;
      }
      return std::abs(parameterFloat(animator, condition.parameter) - condition.float_value) <=
             0.0001f;
    case components::AnimatorConditionOp::NotEquals:
      if (parameter->type == components::AnimatorParameterType::Bool) {
        return parameter->bool_value != condition.bool_value;
      }
      if (parameter->type == components::AnimatorParameterType::Int) {
        return parameter->int_value != condition.int_value;
      }
      return std::abs(parameterFloat(animator, condition.parameter) - condition.float_value) >
             0.0001f;
    case components::AnimatorConditionOp::Greater:
      return parameterFloat(animator, condition.parameter) > condition.float_value;
    case components::AnimatorConditionOp::GreaterOrEqual:
      return parameterFloat(animator, condition.parameter) >= condition.float_value;
    case components::AnimatorConditionOp::Less:
      return parameterFloat(animator, condition.parameter) < condition.float_value;
    case components::AnimatorConditionOp::LessOrEqual:
      return parameterFloat(animator, condition.parameter) <= condition.float_value;
  }
  return false;
}

bool transitionConditionsPass(const components::AnimatorComponent& animator,
                              const components::AnimatorTransition& transition) {
  for (const components::AnimatorCondition& condition : transition.conditions) {
    if (!evaluateCondition(animator, condition)) {
      return false;
    }
  }
  return true;
}

void consumeTransitionTriggers(components::AnimatorComponent& animator,
                               const components::AnimatorTransition& transition) {
  for (const components::AnimatorCondition& condition : transition.conditions) {
    components::AnimatorParameter* parameter =
        components::findAnimatorParameter(animator, condition.parameter);
    if (parameter != nullptr &&
        parameter->type == components::AnimatorParameterType::Trigger &&
        condition.op == components::AnimatorConditionOp::If) {
      parameter->trigger_value = false;
    }
  }
}

const components::AnimatorTransition* findReadyTransition(
    components::AnimatorComponent& animator,
    const components::AnimatorState& state) {
  for (const components::AnimatorTransition& transition : state.transitions) {
    if (transition.to_state_index >= animator.state_machine.states.size()) {
      continue;
    }
    if (transition.has_exit_time) {
      const float duration = std::max(stateDuration(animator, state), 0.0001f);
      if ((animator.state_time_seconds / duration) < transition.exit_time_normalized) {
        continue;
      }
    }
    if (transitionConditionsPass(animator, transition)) {
      return &transition;
    }
  }
  return nullptr;
}

void collectClipEvents(components::AnimatorComponent& animator,
                       const AnimationClip& clip,
                       uint32_t clip_index,
                       uint32_t state_index,
                       float previous_time,
                       float current_time,
                       bool loop) {
  if (clip.events.empty() || clip.duration_seconds <= 0.0f) {
    return;
  }

  const float duration = clip.duration_seconds;
  const float prev = normalizeAnimationTime(clip, previous_time, loop);
  const float cur = normalizeAnimationTime(clip, current_time, loop);
  const float delta = current_time - previous_time;
  if (delta <= 0.0f) {
    return;
  }

  auto push_event = [&](const AnimationEvent& event) {
    animator.event_queue.push_back(components::AnimatorEventRecord{
        .name = event.name,
        .payload = event.payload,
        .clip_index = clip_index,
        .state_index = state_index,
        .time_seconds = event.time_seconds,
    });
  };

  if (loop && delta >= duration) {
    for (const AnimationEvent& event : clip.events) {
      push_event(event);
    }
    return;
  }

  if (loop && cur < prev) {
    for (const AnimationEvent& event : clip.events) {
      if ((event.time_seconds > prev && event.time_seconds <= duration) ||
          (event.time_seconds >= 0.0f && event.time_seconds <= cur)) {
        push_event(event);
      }
    }
    return;
  }

  for (const AnimationEvent& event : clip.events) {
    if (event.time_seconds > prev && event.time_seconds <= cur) {
      push_event(event);
    }
  }
}

void accumulateTransform(AccumulatedTransform& dst,
                         const SampledTransform& sampled,
                         float weight) {
  if (weight <= 0.0f) {
    return;
  }
  if (sampled.position) {
    dst.position = addVec3(dst.position, scaleVec3(*sampled.position, weight));
    dst.position_weight += weight;
  }
  if (sampled.scale) {
    dst.scale = addVec3(dst.scale, scaleVec3(*sampled.scale, weight));
    dst.scale_weight += weight;
  }
  if (sampled.rotation) {
    glm::quat q = toGlm(*sampled.rotation);
    if (dst.rotation_weight > 0.0f && glm::dot(toGlm(dst.rotation), q) < 0.0f) {
      q = -q;
    }
    dst.rotation.x += q.x * weight;
    dst.rotation.y += q.y * weight;
    dst.rotation.z += q.z * weight;
    dst.rotation.w += q.w * weight;
    dst.rotation_weight += weight;
  }
}

SampledTransform finalizeTransform(const AccumulatedTransform& accumulated) {
  SampledTransform out{};
  if (accumulated.position_weight > 0.0f) {
    out.position = scaleVec3(accumulated.position, 1.0f / accumulated.position_weight);
  }
  if (accumulated.scale_weight > 0.0f) {
    out.scale = scaleVec3(accumulated.scale, 1.0f / accumulated.scale_weight);
  }
  if (accumulated.rotation_weight > 0.0f) {
    glm::quat q = toGlm(accumulated.rotation);
    if (glm::length(q) <= 0.000001f) {
      q = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    } else {
      q = glm::normalize(q);
    }
    out.rotation = fromGlm(q);
  }
  return out;
}

void applyLocalPoseToEntities(ecs::World& world,
                              const std::vector<ecs::Entity>& node_entities_by_index,
                              const LocalPose& pose) {
  const size_t count = std::min(node_entities_by_index.size(), pose.nodes.size());
  for (size_t node_index = 0; node_index < count; ++node_index) {
    const ecs::Entity target = node_entities_by_index[node_index];
    if (!world.isAlive(target) || !world.has<components::LocalTransformComponent>(target)) {
      continue;
    }
    const PoseTransform& sampled = pose.nodes[node_index];
    auto& local = world.get<components::LocalTransformComponent>(target);
    if (sampled.has_position) {
      local.position = sampled.position;
    }
    if (sampled.has_rotation) {
      local.rotation = sampled.rotation;
    }
    if (sampled.has_scale) {
      local.scale = sampled.scale;
    }
  }
}

void applyWeightedClipSamples(ecs::World& world,
                              components::AnimatorComponent& animator,
                              const std::vector<WeightedClipSample>& samples) {
  std::unordered_map<uint32_t, AccumulatedTransform> accumulated;
  for (const WeightedClipSample& sample : samples) {
    if (sample.clip_index >= animator.clips.size() || sample.weight <= 0.0f) {
      continue;
    }
    const AnimationClip& clip = animator.clips[sample.clip_index];
    sampleAnimationClip(
        clip,
        sample.time_seconds,
        sample.loop,
        [&](uint32_t target_node_index, const SampledTransform& sampled) {
          accumulateTransform(accumulated[target_node_index], sampled, sample.weight);
        });
  }

  LocalPose pose{};
  pose.nodes.resize(animator.node_entities_by_index.size());
  for (const auto& [target_node_index, value] : accumulated) {
    const SampledTransform sampled = finalizeTransform(value);
    applySampleToLocalPose(pose, target_node_index, sampled);
  }
  applyLocalPoseToEntities(world, animator.node_entities_by_index, pose);
}

void appendStateSamples(const components::AnimatorComponent& animator,
                        uint32_t state_index,
                        float state_time_seconds,
                        float weight,
                        std::vector<WeightedClipSample>& out_samples) {
  if (state_index >= animator.state_machine.states.size() || weight <= 0.0f) {
    return;
  }
  const components::AnimatorState& state = animator.state_machine.states[state_index];
  if (state.motion_type == components::AnimatorMotionType::Clip) {
    if (state.clip_index < animator.clips.size()) {
      out_samples.push_back(WeightedClipSample{
          .clip_index = state.clip_index,
          .state_index = state_index,
          .time_seconds = state_time_seconds * state.speed,
          .loop = state.loop,
          .weight = weight,
      });
    }
    return;
  }

  std::vector<components::AnimatorBlendTree1DChild> children = state.blend_tree.children;
  children.erase(std::remove_if(children.begin(),
                                children.end(),
                                [&](const components::AnimatorBlendTree1DChild& child) {
                                  return child.clip_index >= animator.clips.size();
                                }),
                 children.end());
  if (children.empty()) {
    return;
  }
  std::sort(children.begin(),
            children.end(),
            [](const components::AnimatorBlendTree1DChild& a,
               const components::AnimatorBlendTree1DChild& b) {
              return a.threshold < b.threshold;
            });

  const float parameter = parameterFloat(animator, state.blend_tree.parameter);
  if (children.size() == 1 || parameter <= children.front().threshold) {
    const auto& child = children.front();
    out_samples.push_back(WeightedClipSample{
        .clip_index = child.clip_index,
        .state_index = state_index,
        .time_seconds = state_time_seconds * state.speed * child.speed,
        .loop = state.loop,
        .weight = weight,
    });
    return;
  }
  if (parameter >= children.back().threshold) {
    const auto& child = children.back();
    out_samples.push_back(WeightedClipSample{
        .clip_index = child.clip_index,
        .state_index = state_index,
        .time_seconds = state_time_seconds * state.speed * child.speed,
        .loop = state.loop,
        .weight = weight,
    });
    return;
  }

  for (size_t i = 1; i < children.size(); ++i) {
    const auto& hi = children[i];
    if (parameter > hi.threshold) {
      continue;
    }
    const auto& lo = children[i - 1];
    const float span = std::max(hi.threshold - lo.threshold, 0.0001f);
    const float t = std::clamp((parameter - lo.threshold) / span, 0.0f, 1.0f);
    out_samples.push_back(WeightedClipSample{
        .clip_index = lo.clip_index,
        .state_index = state_index,
        .time_seconds = state_time_seconds * state.speed * lo.speed,
        .loop = state.loop,
        .weight = weight * (1.0f - t),
    });
    out_samples.push_back(WeightedClipSample{
        .clip_index = hi.clip_index,
        .state_index = state_index,
        .time_seconds = state_time_seconds * state.speed * hi.speed,
        .loop = state.loop,
        .weight = weight * t,
    });
    return;
  }
}

const AnimationChannel* findRootMotionChannel(const AnimationClip& clip, uint32_t node_index) {
  for (const AnimationChannel& channel : clip.channels) {
    if (channel.target_node_index == node_index) {
      return &channel;
    }
  }
  return nullptr;
}

SampledTransform sampleRootMotion(const AnimationClip& clip, uint32_t node_index, float time, bool loop) {
  const float sample_time = normalizeAnimationTime(clip, time, loop);
  SampledTransform out{};
  if (clip.root_motion) {
    out.position = sampleVec3Keyframes(clip.root_motion->position_keys,
                                       sample_time,
                                       clip.root_motion->position_interpolation);
    out.rotation = sampleQuatKeyframes(clip.root_motion->rotation_keys,
                                       sample_time,
                                       clip.root_motion->rotation_interpolation);
    return out;
  }

  const AnimationChannel* channel = findRootMotionChannel(clip, node_index);
  if (channel == nullptr) {
    return out;
  }
  out.position = sampleVec3Keyframes(channel->position_keys,
                                     sample_time,
                                     channel->position_interpolation);
  out.rotation = sampleQuatKeyframes(channel->rotation_keys,
                                     sample_time,
                                     channel->rotation_interpolation);
  return out;
}

void updateRootMotion(ecs::World& world,
                      ecs::Entity owner,
                      components::AnimatorComponent& animator,
                      const AnimationClip& clip,
                      float previous_time,
                      float current_time,
                      bool loop) {
  animator.root_motion_delta = SampledTransform{};
  if (animator.root_motion_mode == components::RootMotionMode::Disabled) {
    return;
  }

  uint32_t source_node = animator.root_motion_node_index;
  if (clip.root_motion && clip.root_motion->target_node_index != kInvalidAnimationIndex) {
    source_node = clip.root_motion->target_node_index;
  }
  if (source_node == kInvalidAnimationIndex) {
    return;
  }

  const SampledTransform previous = sampleRootMotion(clip, source_node, previous_time, loop);
  const SampledTransform current = sampleRootMotion(clip, source_node, current_time, loop);
  if (previous.position && current.position) {
    animator.root_motion_delta.position = subtractVec3(*current.position, *previous.position);
    animator.root_motion_accumulated.position =
        animator.root_motion_accumulated.position
            ? addVec3(*animator.root_motion_accumulated.position,
                      *animator.root_motion_delta.position)
            : animator.root_motion_delta.position;
  }
  if (previous.rotation && current.rotation) {
    const glm::quat prev = toGlm(*previous.rotation);
    const glm::quat cur = toGlm(*current.rotation);
    animator.root_motion_delta.rotation = fromGlm(glm::normalize(cur * glm::inverse(prev)));
    animator.root_motion_accumulated.rotation =
        animator.root_motion_accumulated.rotation
            ? fromGlm(glm::normalize(toGlm(*animator.root_motion_delta.rotation) *
                                     toGlm(*animator.root_motion_accumulated.rotation)))
            : animator.root_motion_delta.rotation;
  }

  if (animator.root_motion_mode == components::RootMotionMode::ApplyToLocalTransform &&
      world.isAlive(owner) &&
      world.has<components::LocalTransformComponent>(owner)) {
    auto& local = world.get<components::LocalTransformComponent>(owner);
    if (animator.root_motion_delta.position) {
      local.position = addVec3(local.position, *animator.root_motion_delta.position);
    }
    if (animator.root_motion_delta.rotation) {
      local.rotation = math::mul(local.rotation, *animator.root_motion_delta.rotation);
    }
  }
}

void updateSimpleAnimator(ecs::World& world,
                          ecs::Entity entity,
                          components::AnimatorComponent& animator,
                          float dt) {
  if (animator.clips.empty() || animator.current_clip_index >= animator.clips.size()) {
    return;
  }

  const float previous_time = animator.time_seconds;
  float advanced_time = animator.time_seconds;
  if (animator.playing) {
    advanced_time += dt * animator.speed;
    animator.time_seconds = advanced_time;
  }

  const AnimationClip& clip = animator.clips[animator.current_clip_index];
  if (animator.playing) {
    animator.time_seconds = normalizeAnimationTime(clip, animator.time_seconds, animator.loop);
    collectClipEvents(animator,
                      clip,
                      static_cast<uint32_t>(animator.current_clip_index),
                      kInvalidAnimationIndex,
                      previous_time,
                      advanced_time,
                      animator.loop);
    updateRootMotion(world, entity, animator, clip, previous_time, advanced_time, animator.loop);
  }

  std::vector<WeightedClipSample> samples;
  if (animator.blend_active &&
      animator.blend_from_clip_index < animator.clips.size() &&
      animator.blend_duration_seconds > 0.0f) {
    if (animator.playing) {
      animator.blend_elapsed_seconds += dt;
      const AnimationClip& from_clip = animator.clips[animator.blend_from_clip_index];
      animator.blend_from_time_seconds =
          normalizeAnimationTime(from_clip,
                                 animator.blend_from_time_seconds + dt * animator.speed,
                                 animator.loop);
    }
    const float t =
        std::clamp(animator.blend_elapsed_seconds / animator.blend_duration_seconds, 0.0f, 1.0f);
    samples.push_back(WeightedClipSample{
        .clip_index = static_cast<uint32_t>(animator.blend_from_clip_index),
        .time_seconds = animator.blend_from_time_seconds,
        .loop = animator.loop,
        .weight = 1.0f - t,
    });
    samples.push_back(WeightedClipSample{
        .clip_index = static_cast<uint32_t>(animator.current_clip_index),
        .time_seconds = animator.time_seconds,
        .loop = animator.loop,
        .weight = t,
    });
    if (t >= 1.0f) {
      animator.blend_active = false;
    }
  } else {
    samples.push_back(WeightedClipSample{
        .clip_index = static_cast<uint32_t>(animator.current_clip_index),
        .time_seconds = animator.time_seconds,
        .loop = animator.loop,
        .weight = 1.0f,
    });
  }
  applyWeightedClipSamples(world, animator, samples);
}

void updateStateMachineAnimator(ecs::World& world,
                                ecs::Entity entity,
                                components::AnimatorComponent& animator,
                                float dt) {
  if (animator.state_machine.states.empty()) {
    updateSimpleAnimator(world, entity, animator, dt);
    return;
  }

  if (animator.current_state_index >= animator.state_machine.states.size()) {
    animator.current_state_index = std::min<uint32_t>(
        animator.state_machine.entry_state_index,
        static_cast<uint32_t>(animator.state_machine.states.size() - 1));
    animator.state_time_seconds = 0.0f;
    animator.transition = components::AnimatorTransitionRuntime{};
  }

  if (animator.playing && animator.transition.active) {
    animator.transition.elapsed_seconds += dt;
    animator.transition.from_time_seconds += dt * animator.speed;
    animator.transition.to_time_seconds += dt * animator.speed;
    const float t = animator.transition.duration_seconds > 0.0f
                        ? animator.transition.elapsed_seconds / animator.transition.duration_seconds
                        : 1.0f;
    if (t >= 1.0f) {
      animator.current_state_index = animator.transition.to_state_index;
      animator.state_time_seconds = animator.transition.to_time_seconds;
      animator.transition = components::AnimatorTransitionRuntime{};
    }
  } else if (animator.playing) {
    const components::AnimatorState& state =
        animator.state_machine.states[animator.current_state_index];
    const float previous_state_time = animator.state_time_seconds;
    animator.state_time_seconds += dt * animator.speed;

    if (state.motion_type == components::AnimatorMotionType::Clip &&
        state.clip_index < animator.clips.size()) {
      collectClipEvents(animator,
                        animator.clips[state.clip_index],
                        state.clip_index,
                        animator.current_state_index,
                        previous_state_time * state.speed,
                        animator.state_time_seconds * state.speed,
                        state.loop);
      updateRootMotion(world,
                       entity,
                       animator,
                       animator.clips[state.clip_index],
                       previous_state_time * state.speed,
                       animator.state_time_seconds * state.speed,
                       state.loop);
    } else if (state.motion_type == components::AnimatorMotionType::BlendTree1D) {
      std::vector<WeightedClipSample> previous_samples;
      std::vector<WeightedClipSample> current_samples;
      appendStateSamples(animator, animator.current_state_index, previous_state_time, 1.0f, previous_samples);
      appendStateSamples(animator, animator.current_state_index, animator.state_time_seconds, 1.0f, current_samples);
      const auto dominant_it =
          std::max_element(current_samples.begin(),
                           current_samples.end(),
                           [](const WeightedClipSample& a, const WeightedClipSample& b) {
                             return a.weight < b.weight;
                           });
      if (dominant_it != current_samples.end() &&
          dominant_it->clip_index < animator.clips.size()) {
        const auto previous_it =
            std::find_if(previous_samples.begin(),
                         previous_samples.end(),
                         [&](const WeightedClipSample& sample) {
                           return sample.clip_index == dominant_it->clip_index;
                         });
        const float previous_time =
            previous_it != previous_samples.end() ? previous_it->time_seconds : previous_state_time;
        collectClipEvents(animator,
                          animator.clips[dominant_it->clip_index],
                          dominant_it->clip_index,
                          animator.current_state_index,
                          previous_time,
                          dominant_it->time_seconds,
                          dominant_it->loop);
        updateRootMotion(world,
                         entity,
                         animator,
                         animator.clips[dominant_it->clip_index],
                         previous_time,
                         dominant_it->time_seconds,
                         dominant_it->loop);
      }
    }

    if (const components::AnimatorTransition* transition = findReadyTransition(animator, state)) {
      consumeTransitionTriggers(animator, *transition);
      if (transition->duration_seconds <= 0.0f) {
        animator.current_state_index = transition->to_state_index;
        animator.state_time_seconds = 0.0f;
        animator.transition = components::AnimatorTransitionRuntime{};
      } else {
        animator.transition = components::AnimatorTransitionRuntime{
            .active = true,
            .from_state_index = animator.current_state_index,
            .to_state_index = transition->to_state_index,
            .elapsed_seconds = 0.0f,
            .duration_seconds = transition->duration_seconds,
            .from_time_seconds = animator.state_time_seconds,
            .to_time_seconds = 0.0f,
        };
      }
    }
  }

  std::vector<WeightedClipSample> samples;
  if (animator.transition.active) {
    const float t = animator.transition.duration_seconds > 0.0f
                        ? std::clamp(animator.transition.elapsed_seconds /
                                         animator.transition.duration_seconds,
                                     0.0f,
                                     1.0f)
                        : 1.0f;
    appendStateSamples(animator,
                       animator.transition.from_state_index,
                       animator.transition.from_time_seconds,
                       1.0f - t,
                       samples);
    appendStateSamples(animator,
                       animator.transition.to_state_index,
                       animator.transition.to_time_seconds,
                       t,
                       samples);
  } else {
    appendStateSamples(animator, animator.current_state_index, animator.state_time_seconds, 1.0f, samples);
  }
  applyWeightedClipSamples(world, animator, samples);
}

}  // namespace

void AnimationSystem::update(ecs::World& world, scene::Scene& scene, float dt) {
  (void)scene;

  const std::vector<ecs::Entity> players = world.view<components::AnimationPlayerComponent>();
  for (const ecs::Entity entity : players) {
    auto& player = world.get<components::AnimationPlayerComponent>(entity);
    if (player.clips.empty() || player.current_clip_index >= player.clips.size()) {
      continue;
    }

    if (player.playing) {
      player.time_seconds += dt * player.speed;
    }

    const AnimationClip& clip = player.clips[player.current_clip_index];
    if (player.playing) {
      player.time_seconds = normalizeAnimationTime(clip, player.time_seconds, player.loop);
    }

    LocalPose pose{};
    pose.nodes.resize(player.node_entities_by_index.size());
    sampleAnimationClip(
        clip,
        player.time_seconds,
        player.loop,
        [&](uint32_t target_node_index, const SampledTransform& sampled) {
          applySampleToLocalPose(pose, target_node_index, sampled);
        });
    applyLocalPoseToEntities(world, player.node_entities_by_index, pose);
  }

  const std::vector<ecs::Entity> animators = world.view<components::AnimatorComponent>();
  for (const ecs::Entity entity : animators) {
    auto& animator = world.get<components::AnimatorComponent>(entity);
    animator.event_queue.clear();
    animator.root_motion_delta = SampledTransform{};
    updateStateMachineAnimator(world, entity, animator, dt);
  }
}

}  // namespace karma::animation
