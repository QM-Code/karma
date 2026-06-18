#include "gltf_scene_animation_import.h"

#include <algorithm>
#include <string_view>
#include <utility>

#include <assimp/scene.h>

namespace karma::scene {

namespace {

math::Vec3 toVec3(const aiVector3D& v) {
  return {v.x, v.y, v.z};
}

math::Quat toQuat(const aiQuaternion& q) {
  return {q.x, q.y, q.z, q.w};
}

std::string safeName(std::string_view base, std::string_view fallback) {
  return base.empty() ? std::string(fallback) : std::string(base);
}

animation::InterpolationMode parseInterpolation(const Json& sampler) {
  const std::string interpolation = sampler.value("interpolation", std::string("LINEAR"));
  if (interpolation == "STEP") {
    return animation::InterpolationMode::Step;
  }
  if (interpolation == "CUBICSPLINE") {
    return animation::InterpolationMode::CubicSpline;
  }
  return animation::InterpolationMode::Linear;
}

animation::AnimationChannel& findOrCreateChannel(animation::AnimationClip& clip,
                                                 uint32_t target_node_index,
                                                 const GltfScenePrefab& prefab) {
  const auto it = std::find_if(clip.channels.begin(),
                               clip.channels.end(),
                               [&](const animation::AnimationChannel& channel) {
                                 return channel.target_node_index == target_node_index;
                               });
  if (it != clip.channels.end()) {
    return *it;
  }

  animation::AnimationChannel channel{};
  channel.target_node_index = target_node_index;
  for (size_t skin_index = 0; skin_index < prefab.skins.size(); ++skin_index) {
    const animation::Skin& skin = prefab.skins[skin_index];
    const auto joint_it = std::find(skin.joint_node_indices.begin(),
                                    skin.joint_node_indices.end(),
                                    target_node_index);
    if (joint_it != skin.joint_node_indices.end()) {
      channel.target_skin_index = static_cast<uint32_t>(skin_index);
      channel.target_joint_index =
          static_cast<uint32_t>(std::distance(skin.joint_node_indices.begin(), joint_it));
      break;
    }
  }
  clip.channels.push_back(std::move(channel));
  return clip.channels.back();
}

}  // namespace

std::vector<animation::AnimationClip> loadGltfAnimationClips(
    const GltfDocument& doc,
    const std::unordered_map<std::string, uint32_t>& node_indices_by_name,
    const GltfScenePrefab& prefab) {
  std::vector<animation::AnimationClip> clips;
  if (!doc.valid() ||
      !doc.json.contains("animations") ||
      !doc.json["animations"].is_array()) {
    return clips;
  }

  const auto gltf_node_to_prefab = buildGltfNodeToPrefabIndex(doc, node_indices_by_name);
  clips.reserve(doc.json["animations"].size());
  for (size_t animation_index = 0; animation_index < doc.json["animations"].size(); ++animation_index) {
    const Json& source = doc.json["animations"][animation_index];
    if (!source.contains("samplers") ||
        !source["samplers"].is_array() ||
        !source.contains("channels") ||
        !source["channels"].is_array()) {
      continue;
    }

    animation::AnimationClip clip{};
    clip.name = source.value("name", "Animation " + std::to_string(animation_index));
    clip.ticks_per_second = 1.0f;
    clip.source_index = static_cast<uint32_t>(animation_index);

    for (const Json& channel_json : source["channels"]) {
      if (!channel_json.contains("sampler") ||
          !channel_json.contains("target") ||
          !channel_json["target"].contains("node") ||
          !channel_json["target"].contains("path")) {
        continue;
      }
      const uint32_t sampler_index = channel_json["sampler"].get<uint32_t>();
      if (sampler_index >= source["samplers"].size()) {
        continue;
      }
      const Json& sampler = source["samplers"][sampler_index];
      if (!sampler.contains("input") || !sampler.contains("output")) {
        continue;
      }

      const uint32_t gltf_target_node = channel_json["target"]["node"].get<uint32_t>();
      const auto prefab_node_it = gltf_node_to_prefab.find(gltf_target_node);
      if (prefab_node_it == gltf_node_to_prefab.end()) {
        continue;
      }
      const uint32_t target_node_index = prefab_node_it->second;
      const std::string path = channel_json["target"].value("path", std::string{});
      const animation::InterpolationMode interpolation = parseInterpolation(sampler);

      std::vector<float> times;
      size_t key_count = 0;
      if (!readFloatAccessor(doc, sampler["input"].get<uint32_t>(), 1, times, &key_count) ||
          key_count == 0) {
        continue;
      }
      clip.duration_seconds = std::max(clip.duration_seconds, times.back());

      if (path == "translation" || path == "scale") {
        std::vector<float> values;
        size_t output_count = 0;
        if (!readFloatAccessor(doc, sampler["output"].get<uint32_t>(), 3, values, &output_count)) {
          continue;
        }
        const bool cubic = interpolation == animation::InterpolationMode::CubicSpline;
        if ((!cubic && output_count < key_count) || (cubic && output_count < key_count * 3)) {
          continue;
        }
        std::vector<animation::Vec3Keyframe> keys;
        keys.reserve(key_count);
        for (size_t key_index = 0; key_index < key_count; ++key_index) {
          const size_t base = cubic ? key_index * 9 : key_index * 3;
          const size_t value_base = cubic ? base + 3 : base;
          animation::Vec3Keyframe key{};
          key.time_seconds = times[key_index];
          key.value = {values[value_base], values[value_base + 1], values[value_base + 2]};
          if (cubic) {
            key.in_tangent = {values[base], values[base + 1], values[base + 2]};
            key.out_tangent = {values[base + 6], values[base + 7], values[base + 8]};
          }
          keys.push_back(key);
        }
        animation::AnimationChannel& channel =
            findOrCreateChannel(clip, target_node_index, prefab);
        if (path == "translation") {
          channel.position_interpolation = interpolation;
          channel.position_keys = std::move(keys);
        } else {
          channel.scale_interpolation = interpolation;
          channel.scale_keys = std::move(keys);
        }
      } else if (path == "rotation") {
        std::vector<float> values;
        size_t output_count = 0;
        if (!readFloatAccessor(doc, sampler["output"].get<uint32_t>(), 4, values, &output_count)) {
          continue;
        }
        const bool cubic = interpolation == animation::InterpolationMode::CubicSpline;
        if ((!cubic && output_count < key_count) || (cubic && output_count < key_count * 3)) {
          continue;
        }
        std::vector<animation::QuatKeyframe> keys;
        keys.reserve(key_count);
        for (size_t key_index = 0; key_index < key_count; ++key_index) {
          const size_t base = cubic ? key_index * 12 : key_index * 4;
          const size_t value_base = cubic ? base + 4 : base;
          animation::QuatKeyframe key{};
          key.time_seconds = times[key_index];
          key.value = {values[value_base],
                       values[value_base + 1],
                       values[value_base + 2],
                       values[value_base + 3]};
          if (cubic) {
            key.in_tangent = {values[base], values[base + 1], values[base + 2], values[base + 3]};
            key.out_tangent = {values[base + 8],
                               values[base + 9],
                               values[base + 10],
                               values[base + 11]};
          }
          keys.push_back(key);
        }
        animation::AnimationChannel& channel =
            findOrCreateChannel(clip, target_node_index, prefab);
        channel.rotation_interpolation = interpolation;
        channel.rotation_keys = std::move(keys);
      } else if (path == "weights") {
        std::vector<float> values;
        size_t output_count = 0;
        if (!readFloatAccessor(doc, sampler["output"].get<uint32_t>(), 1, values, &output_count)) {
          continue;
        }
        const bool cubic = interpolation == animation::InterpolationMode::CubicSpline;
        const size_t divisor = key_count * (cubic ? 3 : 1);
        if (divisor == 0 || output_count < divisor || (output_count % divisor) != 0) {
          continue;
        }
        const size_t morph_target_count = output_count / divisor;
        animation::MorphTargetTrack track{};
        track.target_node_index = target_node_index;
        const Json& target_node_json = doc.json["nodes"][gltf_target_node];
        if (target_node_json.contains("mesh") && target_node_json["mesh"].is_number_unsigned()) {
          track.target_mesh_index = target_node_json["mesh"].get<uint32_t>();
        }
        track.interpolation = interpolation;
        track.weight_keys.reserve(key_count);
        for (size_t key_index = 0; key_index < key_count; ++key_index) {
          animation::MorphWeightKeyframe key{};
          key.time_seconds = times[key_index];
          key.values.resize(morph_target_count);
          if (cubic) {
            key.in_tangents.resize(morph_target_count);
            key.out_tangents.resize(morph_target_count);
          }
          const size_t base = cubic ? key_index * morph_target_count * 3
                                    : key_index * morph_target_count;
          const size_t value_base = cubic ? base + morph_target_count : base;
          for (size_t i = 0; i < morph_target_count; ++i) {
            key.values[i] = values[value_base + i];
            if (cubic) {
              key.in_tangents[i] = values[base + i];
              key.out_tangents[i] = values[value_base + morph_target_count + i];
            }
          }
          track.weight_keys.push_back(std::move(key));
        }
        clip.morph_target_tracks.push_back(std::move(track));
      }
    }

    if (!clip.channels.empty() || !clip.morph_target_tracks.empty()) {
      clips.push_back(std::move(clip));
    }
  }

  return clips;
}

std::vector<animation::AnimationClip> loadAnimationClips(
    const aiScene& scene,
    const std::unordered_map<std::string, uint32_t>& node_indices_by_name) {
  std::vector<animation::AnimationClip> clips;
  clips.reserve(scene.mNumAnimations);

  for (unsigned int animation_index = 0; animation_index < scene.mNumAnimations; ++animation_index) {
    const aiAnimation* source = scene.mAnimations[animation_index];
    if (source == nullptr) {
      continue;
    }

    const double ticks_per_second =
        source->mTicksPerSecond > 0.0 ? source->mTicksPerSecond : 1.0;
    animation::AnimationClip clip{};
    clip.name = safeName(source->mName.C_Str(), "Animation " + std::to_string(animation_index));
    clip.ticks_per_second = static_cast<float>(ticks_per_second);
    clip.duration_seconds = static_cast<float>(source->mDuration / ticks_per_second);
    clip.channels.reserve(source->mNumChannels);

    for (unsigned int channel_index = 0; channel_index < source->mNumChannels; ++channel_index) {
      const aiNodeAnim* source_channel = source->mChannels[channel_index];
      if (source_channel == nullptr) {
        continue;
      }
      const auto node_it = node_indices_by_name.find(source_channel->mNodeName.C_Str());
      if (node_it == node_indices_by_name.end()) {
        continue;
      }

      animation::AnimationChannel channel{};
      channel.target_node_index = node_it->second;
      channel.position_keys.reserve(source_channel->mNumPositionKeys);
      channel.rotation_keys.reserve(source_channel->mNumRotationKeys);
      channel.scale_keys.reserve(source_channel->mNumScalingKeys);

      for (unsigned int key_index = 0; key_index < source_channel->mNumPositionKeys; ++key_index) {
        const aiVectorKey& key = source_channel->mPositionKeys[key_index];
        channel.position_keys.push_back(animation::Vec3Keyframe{
            .time_seconds = static_cast<float>(key.mTime / ticks_per_second),
            .value = toVec3(key.mValue),
        });
      }
      for (unsigned int key_index = 0; key_index < source_channel->mNumRotationKeys; ++key_index) {
        const aiQuatKey& key = source_channel->mRotationKeys[key_index];
        channel.rotation_keys.push_back(animation::QuatKeyframe{
            .time_seconds = static_cast<float>(key.mTime / ticks_per_second),
            .value = toQuat(key.mValue),
        });
      }
      for (unsigned int key_index = 0; key_index < source_channel->mNumScalingKeys; ++key_index) {
        const aiVectorKey& key = source_channel->mScalingKeys[key_index];
        channel.scale_keys.push_back(animation::Vec3Keyframe{
            .time_seconds = static_cast<float>(key.mTime / ticks_per_second),
            .value = toVec3(key.mValue),
        });
      }

      clip.channels.push_back(std::move(channel));
    }

    if (!clip.channels.empty()) {
      clips.push_back(std::move(clip));
    }
  }

  return clips;
}

}  // namespace karma::scene
