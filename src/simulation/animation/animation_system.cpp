#include "karma/world.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "karma/components.h"
#include "karma/math.h"

namespace karma::world {

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

struct AccumulatedMorphWeights {
  std::vector<float> values;
  std::vector<float> weights;
};

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
    const components::AnimatorState& state,
    float state_time_seconds) {
  for (const components::AnimatorTransition& transition : state.transitions) {
    if (transition.to_state_index >= animator.state_machine.states.size()) {
      continue;
    }
    if (transition.has_exit_time) {
      const float duration = std::max(stateDuration(animator, state), 0.0001f);
      if ((state_time_seconds / duration) < transition.exit_time_normalized) {
        continue;
      }
    }
    if (transitionConditionsPass(animator, transition)) {
      return &transition;
    }
  }
  return nullptr;
}

struct InterruptTransitionCandidate {
  const components::AnimatorTransition* transition = nullptr;
  uint32_t from_state_index = kInvalidAnimationIndex;
  float from_time_seconds = 0.0f;
};

InterruptTransitionCandidate findInterruptingTransition(
    components::AnimatorComponent& animator) {
  if (!animator.transition.active) {
    return {};
  }

  auto check_state = [&](uint32_t state_index,
                         float state_time_seconds) -> InterruptTransitionCandidate {
    if (state_index >= animator.state_machine.states.size()) {
      return {};
    }
    const components::AnimatorState& state = animator.state_machine.states[state_index];
    if (const components::AnimatorTransition* transition =
            findReadyTransition(animator, state, state_time_seconds)) {
      if (transition->to_state_index != animator.transition.to_state_index ||
          state_index != animator.transition.from_state_index) {
        return InterruptTransitionCandidate{
            .transition = transition,
            .from_state_index = state_index,
            .from_time_seconds = state_time_seconds,
        };
      }
    }
    return {};
  };

  switch (animator.transition.interrupt_policy) {
    case components::AnimatorInterruptPolicy::None:
      return {};
    case components::AnimatorInterruptPolicy::Source:
      return check_state(animator.transition.from_state_index,
                         animator.transition.from_time_seconds);
    case components::AnimatorInterruptPolicy::Destination:
      return check_state(animator.transition.to_state_index,
                         animator.transition.to_time_seconds);
    case components::AnimatorInterruptPolicy::SourceThenDestination:
      if (InterruptTransitionCandidate source =
              check_state(animator.transition.from_state_index,
                          animator.transition.from_time_seconds);
          source.transition != nullptr) {
        return source;
      }
      return check_state(animator.transition.to_state_index,
                         animator.transition.to_time_seconds);
    case components::AnimatorInterruptPolicy::Any:
      for (uint32_t state_index = 0;
           state_index < animator.state_machine.states.size();
           ++state_index) {
        float state_time = 0.0f;
        if (state_index == animator.transition.from_state_index) {
          state_time = animator.transition.from_time_seconds;
        } else if (state_index == animator.transition.to_state_index) {
          state_time = animator.transition.to_time_seconds;
        }
        if (InterruptTransitionCandidate candidate = check_state(state_index, state_time);
            candidate.transition != nullptr) {
          return candidate;
        }
      }
      return {};
  }
  return {};
}

void beginAnimatorTransition(components::AnimatorComponent& animator,
                             uint32_t from_state_index,
                             float from_time_seconds,
                             const components::AnimatorTransition& transition) {
  consumeTransitionTriggers(animator, transition);
  if (transition.duration_seconds <= 0.0f) {
    animator.current_state_index = transition.to_state_index;
    animator.state_time_seconds = 0.0f;
    animator.transition = components::AnimatorTransitionRuntime{};
    return;
  }
  animator.current_state_index = from_state_index;
  animator.state_time_seconds = from_time_seconds;
  animator.transition = components::AnimatorTransitionRuntime{
      .active = true,
      .from_state_index = from_state_index,
      .to_state_index = transition.to_state_index,
      .elapsed_seconds = 0.0f,
      .duration_seconds = transition.duration_seconds,
      .from_time_seconds = from_time_seconds,
      .to_time_seconds = 0.0f,
      .interrupt_policy = transition.interrupt_policy,
  };
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
    dst.position = math::add(dst.position, math::scale(*sampled.position, weight));
    dst.position_weight += weight;
  }
  if (sampled.scale) {
    dst.scale = math::add(dst.scale, math::scale(*sampled.scale, weight));
    dst.scale_weight += weight;
  }
  if (sampled.rotation) {
    math::Quat q = *sampled.rotation;
    if (dst.rotation_weight > 0.0f && math::dot(dst.rotation, q) < 0.0f) {
      q = {-q.x, -q.y, -q.z, -q.w};
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
    out.position = math::scale(accumulated.position, 1.0f / accumulated.position_weight);
  }
  if (accumulated.scale_weight > 0.0f) {
    out.scale = math::scale(accumulated.scale, 1.0f / accumulated.scale_weight);
  }
  if (accumulated.rotation_weight > 0.0f) {
    out.rotation = math::normalize(accumulated.rotation);
  }
  return out;
}

void accumulateMorphWeights(AccumulatedMorphWeights& dst,
                            const std::vector<float>& weights,
                            float sample_weight) {
  if (sample_weight <= 0.0f || weights.empty()) {
    return;
  }
  if (dst.values.size() < weights.size()) {
    dst.values.resize(weights.size(), 0.0f);
    dst.weights.resize(weights.size(), 0.0f);
  }
  for (size_t i = 0; i < weights.size(); ++i) {
    dst.values[i] += weights[i] * sample_weight;
    dst.weights[i] += sample_weight;
  }
}

std::vector<float> finalizeMorphWeights(const AccumulatedMorphWeights& accumulated) {
  std::vector<float> out(accumulated.values.size(), 0.0f);
  for (size_t i = 0; i < accumulated.values.size(); ++i) {
    out[i] = accumulated.weights[i] > 0.0f
                 ? accumulated.values[i] / accumulated.weights[i]
                 : 0.0f;
  }
  return out;
}

bool morphWeightsChanged(const std::vector<float>& a, const std::vector<float>& b) {
  if (a.size() != b.size()) {
    return true;
  }
  for (size_t i = 0; i < a.size(); ++i) {
    if (std::abs(a[i] - b[i]) > 0.000001f) {
      return true;
    }
  }
  return false;
}

void setMorphWeights(components::DeformableMeshComponent& deformation,
                     const std::vector<float>& sampled_weights) {
  const size_t target_count = deformation.bind_mesh.morph_targets.size();
  std::vector<float> next = deformation.base_morph_weights;
  next.resize(target_count, 0.0f);
  const size_t count = std::min(target_count, sampled_weights.size());
  for (size_t i = 0; i < count; ++i) {
    next[i] = sampled_weights[i];
  }
  if (morphWeightsChanged(deformation.morph_weights, next)) {
    deformation.morph_weights = std::move(next);
    deformation.morph_weights_dirty = true;
  }
}

void applyMorphWeightsToEntities(
    world::World& world,
    const std::vector<std::vector<world::Entity>>& morph_entities_by_node_index,
    const std::unordered_map<uint32_t, AccumulatedMorphWeights>& accumulated) {
  for (const auto& [target_node_index, value] : accumulated) {
    if (target_node_index >= morph_entities_by_node_index.size()) {
      continue;
    }
    const std::vector<float> weights = finalizeMorphWeights(value);
    for (const world::Entity entity : morph_entities_by_node_index[target_node_index]) {
      if (!world.isAlive(entity) || !world.has<components::DeformableMeshComponent>(entity)) {
        continue;
      }
      setMorphWeights(world.get<components::DeformableMeshComponent>(entity), weights);
    }
  }
}

uint32_t resolveChannelTargetNode(const AnimationChannel& channel,
                                  const std::vector<Skeleton>& skeletons,
                                  const std::vector<Skin>& skins,
                                  size_t node_entity_count) {
  auto valid_node = [node_entity_count](uint32_t node_index) {
    return node_index < node_entity_count ? node_index : kInvalidAnimationIndex;
  };
  if (channel.target_node_index != kInvalidAnimationIndex) {
    if (const uint32_t node = valid_node(channel.target_node_index);
        node != kInvalidAnimationIndex) {
      return node;
    }
  }
  if (channel.target_skin_index < skins.size()) {
    const Skin& skin = skins[channel.target_skin_index];
    if (channel.target_joint_index < skin.joint_node_indices.size()) {
      if (const uint32_t node =
              valid_node(skin.joint_node_indices[channel.target_joint_index]);
          node != kInvalidAnimationIndex) {
        return node;
      }
    }
  }
  if (skeletons.size() == 1u &&
      channel.target_joint_index < skeletons.front().joints.size()) {
    return valid_node(skeletons.front().joints[channel.target_joint_index].node_index);
  }
  return kInvalidAnimationIndex;
}

std::vector<float> baseMorphWeightsForNode(world::World& world,
                                           const std::vector<world::Entity>& entities) {
  for (const world::Entity entity : entities) {
    if (!world.isAlive(entity) || !world.has<components::DeformableMeshComponent>(entity)) {
      continue;
    }
    const auto& deformation = world.get<components::DeformableMeshComponent>(entity);
    std::vector<float> weights = deformation.base_morph_weights;
    weights.resize(deformation.bind_mesh.morph_targets.size(), 0.0f);
    return weights;
  }
  return {};
}

void applyLocalPoseToEntities(world::World& world,
                              const std::vector<world::Entity>& node_entities_by_index,
                              const LocalPose& pose) {
  const size_t count = std::min(node_entities_by_index.size(), pose.nodes.size());
  for (size_t node_index = 0; node_index < count; ++node_index) {
    const world::Entity target = node_entities_by_index[node_index];
    if (!world.isAlive(target) || !world.has<components::TransformComponent>(target)) {
      continue;
    }
    const PoseTransform& sampled = pose.nodes[node_index];
    auto& transform = world.get<components::TransformComponent>(target);
    if (sampled.has_position) {
      transform.setLocalPosition(sampled.position);
    }
    if (sampled.has_rotation) {
      transform.setLocalRotation(sampled.rotation);
    }
    if (sampled.has_scale) {
      transform.setLocalScale(sampled.scale);
    }
  }
}

void applyWeightedClipSamples(world::World& world,
                              const std::vector<AnimationClip>& clips,
                              const std::vector<world::Entity>& node_entities_by_index,
                              const std::vector<std::vector<world::Entity>>&
                                  morph_entities_by_node_index,
                              const std::vector<Skeleton>& skeletons,
                              const std::vector<Skin>& skins,
                              const std::vector<WeightedClipSample>& samples) {
  std::unordered_map<uint32_t, AccumulatedTransform> accumulated;
  std::unordered_map<uint32_t, AccumulatedMorphWeights> accumulated_morphs;
  for (const WeightedClipSample& sample : samples) {
    if (sample.clip_index >= clips.size() || sample.weight <= 0.0f) {
      continue;
    }
    const AnimationClip& clip = clips[sample.clip_index];
    std::unordered_set<uint32_t> sampled_morph_nodes;
    const float sample_time = normalizeAnimationTime(clip, sample.time_seconds, sample.loop);
    for (const AnimationChannel& channel : clip.channels) {
      SampledTransform sampled{};
      sampled.position = sampleVec3Keyframes(channel.position_keys,
                                             sample_time,
                                             channel.position_interpolation);
      sampled.rotation = sampleQuatKeyframes(channel.rotation_keys,
                                             sample_time,
                                             channel.rotation_interpolation);
      sampled.scale = sampleVec3Keyframes(channel.scale_keys,
                                          sample_time,
                                          channel.scale_interpolation);
      if (!(sampled.position || sampled.rotation || sampled.scale)) {
        continue;
      }
      const uint32_t target_node_index =
          resolveChannelTargetNode(channel,
                                   skeletons,
                                   skins,
                                   node_entities_by_index.size());
      if (target_node_index != kInvalidAnimationIndex) {
        accumulateTransform(accumulated[target_node_index], sampled, sample.weight);
      }
    }
    for (const MorphTargetTrack& track : clip.morph_target_tracks) {
      const std::optional<std::vector<float>> weights =
          sampleMorphWeightKeyframes(track.weight_keys, sample_time, track.interpolation);
      if (!weights) {
        continue;
      }
      sampled_morph_nodes.insert(track.target_node_index);
      accumulateMorphWeights(accumulated_morphs[track.target_node_index],
                             *weights,
                             sample.weight);
    }
    for (uint32_t node_index = 0;
         node_index < morph_entities_by_node_index.size();
         ++node_index) {
      if (sampled_morph_nodes.find(node_index) != sampled_morph_nodes.end() ||
          morph_entities_by_node_index[node_index].empty()) {
        continue;
      }
      const std::vector<float> base_weights =
          baseMorphWeightsForNode(world, morph_entities_by_node_index[node_index]);
      if (!base_weights.empty()) {
        accumulateMorphWeights(accumulated_morphs[node_index], base_weights, sample.weight);
      }
    }
  }

  LocalPose pose{};
  pose.nodes.resize(node_entities_by_index.size());
  for (const auto& [target_node_index, value] : accumulated) {
    const SampledTransform sampled = finalizeTransform(value);
    applySampleToLocalPose(pose, target_node_index, sampled);
  }
  applyLocalPoseToEntities(world, node_entities_by_index, pose);
  applyMorphWeightsToEntities(world, morph_entities_by_node_index, accumulated_morphs);
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
    const float t = math::clamp01((parameter - lo.threshold) / span);
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

int64_t animationCycle(float time, float duration) {
  const double cycle = std::floor(static_cast<double>(time) /
                                  static_cast<double>(duration));
  if (cycle >= static_cast<double>(std::numeric_limits<int64_t>::max())) {
    return std::numeric_limits<int64_t>::max();
  }
  if (cycle <= static_cast<double>(std::numeric_limits<int64_t>::min())) {
    return std::numeric_limits<int64_t>::min();
  }
  return static_cast<int64_t>(cycle);
}

uint64_t unsignedMagnitude(int64_t value) {
  return value < 0
             ? static_cast<uint64_t>(-(value + 1)) + 1u
             : static_cast<uint64_t>(value);
}

math::Quat quaternionPower(math::Quat base, int64_t exponent) {
  if (exponent < 0) {
    base = math::inverse(base);
  }

  math::Quat result{};
  uint64_t remaining = unsignedMagnitude(exponent);
  while (remaining != 0u) {
    if ((remaining & 1u) != 0u) {
      result = math::normalize(math::mul(base, result));
    }
    remaining >>= 1u;
    if (remaining != 0u) {
      base = math::normalize(math::mul(base, base));
    }
  }
  return result;
}

SampledTransform sampleUnwrappedRootMotion(const AnimationClip& clip,
                                           uint32_t node_index,
                                           float time) {
  if (clip.duration_seconds <= 0.0f ||
      !std::isfinite(clip.duration_seconds) ||
      !std::isfinite(time)) {
    return sampleRootMotion(clip, node_index, time, false);
  }

  const float duration = clip.duration_seconds;
  float phase = std::fmod(time, duration);
  if (phase < 0.0f) {
    phase += duration;
  }
  const int64_t cycle = animationCycle(time, duration);

  const SampledTransform start = sampleRootMotion(clip, node_index, 0.0f, false);
  const SampledTransform end = sampleRootMotion(clip, node_index, duration, false);
  const SampledTransform sampled = sampleRootMotion(clip, node_index, phase, false);
  SampledTransform out{};

  if (start.position && end.position && sampled.position) {
    const math::Vec3 cycle_delta = math::subtract(*end.position, *start.position);
    out.position = math::add(
        math::subtract(*sampled.position, *start.position),
        math::scale(cycle_delta, static_cast<float>(cycle)));
  }
  if (start.rotation && end.rotation && sampled.rotation) {
    const math::Quat inverse_start = math::inverse(*start.rotation);
    const math::Quat phase_delta =
        math::normalize(math::mul(*sampled.rotation, inverse_start));
    const math::Quat cycle_delta =
        math::normalize(math::mul(*end.rotation, inverse_start));
    out.rotation = math::normalize(
        math::mul(phase_delta, quaternionPower(cycle_delta, cycle)));
  }
  return out;
}

void updateRootMotion(world::World& world,
                      world::Entity owner,
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

  const SampledTransform previous = loop
      ? sampleUnwrappedRootMotion(clip, source_node, previous_time)
      : sampleRootMotion(clip, source_node, previous_time, false);
  const SampledTransform current = loop
      ? sampleUnwrappedRootMotion(clip, source_node, current_time)
      : sampleRootMotion(clip, source_node, current_time, false);
  if (previous.position && current.position) {
    animator.root_motion_delta.position = math::subtract(*current.position, *previous.position);
    animator.root_motion_accumulated.position =
        animator.root_motion_accumulated.position
            ? math::add(*animator.root_motion_accumulated.position,
                        *animator.root_motion_delta.position)
            : animator.root_motion_delta.position;
  }
  if (previous.rotation && current.rotation) {
    animator.root_motion_delta.rotation =
        math::normalize(math::mul(*current.rotation, math::inverse(*previous.rotation)));
    animator.root_motion_accumulated.rotation =
        animator.root_motion_accumulated.rotation
            ? math::normalize(math::mul(*animator.root_motion_delta.rotation,
                                        *animator.root_motion_accumulated.rotation))
            : animator.root_motion_delta.rotation;
  }

  if (animator.root_motion_mode == components::RootMotionMode::ApplyToLocalTransform &&
      world.isAlive(owner) &&
      world.has<components::TransformComponent>(owner)) {
    auto& transform = world.get<components::TransformComponent>(owner);
    if (animator.root_motion_delta.position) {
      transform.setLocalPosition(
          math::add(transform.localPosition(), *animator.root_motion_delta.position));
    }
    if (animator.root_motion_delta.rotation) {
      transform.setLocalRotation(
          math::mul(transform.localRotation(), *animator.root_motion_delta.rotation));
    }
  }
}

bool hasSampledTransform(const SampledTransform& transform) {
  return transform.position || transform.rotation || transform.scale;
}

void appendRootMotionDelta(SampledTransform& dst, const SampledTransform& delta) {
  if (delta.position) {
    dst.position = dst.position ? math::add(*dst.position, *delta.position) : *delta.position;
  }
  if (delta.rotation) {
    dst.rotation = dst.rotation
                       ? math::normalize(math::mul(*delta.rotation, *dst.rotation))
                       : *delta.rotation;
  }
  if (delta.scale) {
    dst.scale = dst.scale ? math::add(*dst.scale, *delta.scale) : *delta.scale;
  }
}

void prepareRootMotionComponent(world::World& world,
                                world::Entity entity,
                                components::AnimatorComponent& animator) {
  if (!world.has<components::RootMotionComponent>(entity)) {
    return;
  }
  const auto& root_motion = world.get<components::RootMotionComponent>(entity);
  animator.root_motion_mode = root_motion.mode;
  animator.root_motion_node_index = root_motion.root_motion_node_index;
}

void publishAnimatorSideChannels(world::World& world,
                                 world::Entity entity,
                                 const components::AnimatorComponent& animator) {
  if (world.has<components::AnimationEventBufferComponent>(entity)) {
    auto& buffer = world.get<components::AnimationEventBufferComponent>(entity);
    buffer.events = animator.event_queue;
    if (!buffer.events.empty()) {
      ++buffer.sequence;
    }
  }

  if (!world.has<components::RootMotionComponent>(entity)) {
    return;
  }
  auto& root_motion = world.get<components::RootMotionComponent>(entity);
  root_motion.accumulated = animator.root_motion_accumulated;
  if (root_motion.mode == components::RootMotionMode::Disabled) {
    root_motion.delta = SampledTransform{};
    root_motion.has_unconsumed_delta = false;
    return;
  }
  if (root_motion.mode == components::RootMotionMode::ExposeDelta) {
    appendRootMotionDelta(root_motion.delta, animator.root_motion_delta);
    root_motion.has_unconsumed_delta =
        root_motion.has_unconsumed_delta || hasSampledTransform(animator.root_motion_delta);
    return;
  }
  root_motion.delta = animator.root_motion_delta;
  root_motion.has_unconsumed_delta = hasSampledTransform(root_motion.delta);
}

void updateSimpleAnimator(world::World& world,
                          world::Entity entity,
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
        math::clamp01(animator.blend_elapsed_seconds / animator.blend_duration_seconds);
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
  applyWeightedClipSamples(world,
                           animator.clips,
                           animator.node_entities_by_index,
                           animator.morph_entities_by_node_index,
                           animator.skeletons,
                           animator.skins,
                           samples);
}

void updateStateMachineAnimator(world::World& world,
                                world::Entity entity,
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
    } else {
      const InterruptTransitionCandidate interrupt = findInterruptingTransition(animator);
      if (interrupt.transition != nullptr) {
        beginAnimatorTransition(animator,
                                interrupt.from_state_index,
                                interrupt.from_time_seconds,
                                *interrupt.transition);
      }
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

    if (const components::AnimatorTransition* transition =
            findReadyTransition(animator, state, animator.state_time_seconds)) {
      beginAnimatorTransition(animator,
                              animator.current_state_index,
                              animator.state_time_seconds,
                              *transition);
    }
  }

  std::vector<WeightedClipSample> samples;
  if (animator.transition.active) {
    const float t = animator.transition.duration_seconds > 0.0f
                        ? math::clamp01(animator.transition.elapsed_seconds /
                                        animator.transition.duration_seconds)
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
  applyWeightedClipSamples(world,
                           animator.clips,
                           animator.node_entities_by_index,
                           animator.morph_entities_by_node_index,
                           animator.skeletons,
                           animator.skins,
                           samples);
}

}  // namespace

void AnimationSystem::update(world::World& world, world::Scene& scene, float dt) {
  (void)scene;

  const std::vector<world::Entity> animators = world.view<components::AnimatorComponent>();
  for (const world::Entity entity : animators) {
    auto& animator = world.get<components::AnimatorComponent>(entity);
    animator.event_queue.clear();
    animator.root_motion_delta = SampledTransform{};
    prepareRootMotionComponent(world, entity, animator);
    updateStateMachineAnimator(world, entity, animator, dt);
    publishAnimatorSideChannels(world, entity, animator);
  }
}

}  // namespace karma::world
