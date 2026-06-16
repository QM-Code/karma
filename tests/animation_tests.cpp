#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>

#include "karma/simulation/animation/animation_clip.h"
#include "karma/simulation/animation/animation_system.h"
#include "karma/simulation/animation/cpu_skinning_system.h"
#include "karma/simulation/animation/pose.h"
#include "karma/world/components/animation_player.h"
#include "karma/world/components/animator.h"
#include "karma/world/components/morph_target.h"
#include "karma/world/components/skinned_mesh.h"
#include "karma/world/components/transform.h"
#include "karma/world/ecs/world.h"
#include "karma/content/importers/glb_scene_import.h"
#include "karma/world/scene/scene.h"
#include "karma/world/scene/transform_hierarchy.h"
#include "../src/content/importers/glb_scene_animation_import.h"
#include "../src/content/importers/glb_scene_mesh_import.h"

#include <algorithm>
#include <cstdint>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
#include <unordered_map>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include "karma/core/math/glm.h"

namespace {

bool near(float a, float b, float epsilon = 0.0001f) {
  return std::abs(a - b) <= epsilon;
}

glm::mat4 composeTransform(const karma::math::Vec3& position,
                           const karma::math::Quat& rotation,
                           const karma::math::Vec3& scale) {
  glm::mat4 matrix(1.0f);
  matrix = glm::translate(matrix, karma::math::toGlm(position));
  matrix *= glm::mat4_cast(karma::math::toGlm(rotation));
  matrix = glm::scale(matrix, karma::math::toGlm(scale));
  return matrix;
}

float maxMatrixDiff(const glm::mat4& a, const glm::mat4& b) {
  float diff = 0.0f;
  for (int column = 0; column < 4; ++column) {
    for (int row = 0; row < 4; ++row) {
      diff = std::max(diff, std::abs(a[column][row] - b[column][row]));
    }
  }
  return diff;
}

std::filesystem::path findRepoRoot() {
  std::filesystem::path current = std::filesystem::current_path();
  for (int i = 0; i < 8; ++i) {
    if (std::filesystem::exists(current / "cmake" / "KarmaEngine.cmake")) {
      return current;
    }
    if (!current.has_parent_path()) {
      break;
    }
    current = current.parent_path();
  }
  return std::filesystem::current_path();
}

float quatLength(const karma::math::Quat& q) {
  return std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
}

void appendU16(std::vector<std::uint8_t>& data, std::uint16_t value) {
  data.push_back(static_cast<std::uint8_t>(value & 0xFFu));
  data.push_back(static_cast<std::uint8_t>((value >> 8u) & 0xFFu));
}

void appendU32(std::vector<std::uint8_t>& data, std::uint32_t value) {
  data.push_back(static_cast<std::uint8_t>(value & 0xFFu));
  data.push_back(static_cast<std::uint8_t>((value >> 8u) & 0xFFu));
  data.push_back(static_cast<std::uint8_t>((value >> 16u) & 0xFFu));
  data.push_back(static_cast<std::uint8_t>((value >> 24u) & 0xFFu));
}

void appendFloat(std::vector<std::uint8_t>& data, float value) {
  const auto* bytes = reinterpret_cast<const std::uint8_t*>(&value);
  data.insert(data.end(), bytes, bytes + sizeof(float));
}

void align4(std::vector<std::uint8_t>& data) {
  while ((data.size() % 4u) != 0u) {
    data.push_back(0);
  }
}

bool writeSkinnedGlb(const std::filesystem::path& path) {
  std::vector<std::uint8_t> bin;

  const std::uint32_t position_offset = static_cast<std::uint32_t>(bin.size());
  appendFloat(bin, 0.0f); appendFloat(bin, 0.0f); appendFloat(bin, 0.0f);
  appendFloat(bin, 1.0f); appendFloat(bin, 0.0f); appendFloat(bin, 0.0f);
  appendFloat(bin, 0.0f); appendFloat(bin, 1.0f); appendFloat(bin, 0.0f);

  const std::uint32_t joint_offset = static_cast<std::uint32_t>(bin.size());
  for (int i = 0; i < 3; ++i) {
    appendU16(bin, 0); appendU16(bin, 0); appendU16(bin, 0); appendU16(bin, 0);
  }

  const std::uint32_t weight_offset = static_cast<std::uint32_t>(bin.size());
  for (int i = 0; i < 3; ++i) {
    appendFloat(bin, 1.0f); appendFloat(bin, 0.0f); appendFloat(bin, 0.0f); appendFloat(bin, 0.0f);
  }

  const std::uint32_t inverse_bind_offset = static_cast<std::uint32_t>(bin.size());
  for (int i = 0; i < 16; ++i) {
    appendFloat(bin, (i % 5) == 0 ? 1.0f : 0.0f);
  }

  const std::uint32_t index_offset = static_cast<std::uint32_t>(bin.size());
  appendU16(bin, 0); appendU16(bin, 1); appendU16(bin, 2);
  align4(bin);

  const std::uint32_t time_offset = static_cast<std::uint32_t>(bin.size());
  appendFloat(bin, 0.0f); appendFloat(bin, 1.0f);

  const std::uint32_t translation_offset = static_cast<std::uint32_t>(bin.size());
  appendFloat(bin, 0.0f); appendFloat(bin, 0.0f); appendFloat(bin, 0.0f);
  appendFloat(bin, 1.0f); appendFloat(bin, 0.0f); appendFloat(bin, 0.0f);
  align4(bin);

  const std::string json =
      "{\"asset\":{\"version\":\"2.0\"},\"scene\":0,"
      "\"scenes\":[{\"nodes\":[0,1]}],"
      "\"nodes\":[{\"name\":\"Joint\"},{\"name\":\"MeshNode\",\"mesh\":0,\"skin\":0}],"
      "\"skins\":[{\"joints\":[0],\"inverseBindMatrices\":3}],"
      "\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":0,\"JOINTS_0\":1,\"WEIGHTS_0\":2},"
      "\"indices\":4,\"mode\":4}]}],"
      "\"buffers\":[{\"byteLength\":" + std::to_string(bin.size()) + "}],"
      "\"bufferViews\":["
      "{\"buffer\":0,\"byteOffset\":" + std::to_string(position_offset) + ",\"byteLength\":36,\"target\":34962},"
      "{\"buffer\":0,\"byteOffset\":" + std::to_string(joint_offset) + ",\"byteLength\":24,\"target\":34962},"
      "{\"buffer\":0,\"byteOffset\":" + std::to_string(weight_offset) + ",\"byteLength\":48,\"target\":34962},"
      "{\"buffer\":0,\"byteOffset\":" + std::to_string(inverse_bind_offset) + ",\"byteLength\":64},"
      "{\"buffer\":0,\"byteOffset\":" + std::to_string(index_offset) + ",\"byteLength\":6,\"target\":34963},"
      "{\"buffer\":0,\"byteOffset\":" + std::to_string(time_offset) + ",\"byteLength\":8},"
      "{\"buffer\":0,\"byteOffset\":" + std::to_string(translation_offset) + ",\"byteLength\":24}],"
      "\"accessors\":["
      "{\"bufferView\":0,\"componentType\":5126,\"count\":3,\"type\":\"VEC3\",\"min\":[0,0,0],\"max\":[1,1,0]},"
      "{\"bufferView\":1,\"componentType\":5123,\"count\":3,\"type\":\"VEC4\"},"
      "{\"bufferView\":2,\"componentType\":5126,\"count\":3,\"type\":\"VEC4\"},"
      "{\"bufferView\":3,\"componentType\":5126,\"count\":1,\"type\":\"MAT4\"},"
      "{\"bufferView\":4,\"componentType\":5123,\"count\":3,\"type\":\"SCALAR\"},"
      "{\"bufferView\":5,\"componentType\":5126,\"count\":2,\"type\":\"SCALAR\",\"min\":[0],\"max\":[1]},"
      "{\"bufferView\":6,\"componentType\":5126,\"count\":2,\"type\":\"VEC3\"}],"
      "\"animations\":[{\"samplers\":[{\"input\":5,\"output\":6}],"
      "\"channels\":[{\"sampler\":0,\"target\":{\"node\":0,\"path\":\"translation\"}}]}]}";

  std::vector<std::uint8_t> json_chunk(json.begin(), json.end());
  while ((json_chunk.size() % 4u) != 0u) {
    json_chunk.push_back(' ');
  }

  std::vector<std::uint8_t> glb;
  const std::uint32_t total_length =
      12u + 8u + static_cast<std::uint32_t>(json_chunk.size()) +
      8u + static_cast<std::uint32_t>(bin.size());
  appendU32(glb, 0x46546C67u);
  appendU32(glb, 2u);
  appendU32(glb, total_length);
  appendU32(glb, static_cast<std::uint32_t>(json_chunk.size()));
  appendU32(glb, 0x4E4F534Au);
  glb.insert(glb.end(), json_chunk.begin(), json_chunk.end());
  appendU32(glb, static_cast<std::uint32_t>(bin.size()));
  appendU32(glb, 0x004E4942u);
  glb.insert(glb.end(), bin.begin(), bin.end());

  std::ofstream out(path, std::ios::binary);
  if (!out) {
    return false;
  }
  out.write(reinterpret_cast<const char*>(glb.data()), static_cast<std::streamsize>(glb.size()));
  return out.good();
}

bool writeSplitWeightGlb(const std::filesystem::path& path) {
  std::vector<std::uint8_t> bin;

  const std::uint32_t position_offset = static_cast<std::uint32_t>(bin.size());
  appendFloat(bin, 0.0f); appendFloat(bin, 0.0f); appendFloat(bin, 0.0f);
  appendFloat(bin, 0.0f); appendFloat(bin, 0.0f); appendFloat(bin, 0.0f);
  appendFloat(bin, 1.0f); appendFloat(bin, 0.0f); appendFloat(bin, 0.0f);
  appendFloat(bin, 0.0f); appendFloat(bin, 1.0f); appendFloat(bin, 0.0f);

  const std::uint32_t joint_offset = static_cast<std::uint32_t>(bin.size());
  appendU16(bin, 0); appendU16(bin, 0); appendU16(bin, 0); appendU16(bin, 0);
  appendU16(bin, 1); appendU16(bin, 0); appendU16(bin, 0); appendU16(bin, 0);
  appendU16(bin, 0); appendU16(bin, 0); appendU16(bin, 0); appendU16(bin, 0);
  appendU16(bin, 1); appendU16(bin, 0); appendU16(bin, 0); appendU16(bin, 0);

  const std::uint32_t weight_offset = static_cast<std::uint32_t>(bin.size());
  for (int i = 0; i < 4; ++i) {
    appendFloat(bin, 1.0f); appendFloat(bin, 0.0f); appendFloat(bin, 0.0f); appendFloat(bin, 0.0f);
  }

  const std::uint32_t inverse_bind_offset = static_cast<std::uint32_t>(bin.size());
  for (int matrix = 0; matrix < 2; ++matrix) {
    for (int i = 0; i < 16; ++i) {
      appendFloat(bin, (i % 5) == 0 ? 1.0f : 0.0f);
    }
  }

  const std::uint32_t index_offset = static_cast<std::uint32_t>(bin.size());
  appendU16(bin, 0); appendU16(bin, 2); appendU16(bin, 3);
  appendU16(bin, 1); appendU16(bin, 3); appendU16(bin, 2);
  align4(bin);

  const std::string json =
      "{\"asset\":{\"version\":\"2.0\"},\"scene\":0,"
      "\"scenes\":[{\"nodes\":[0,1,2]}],"
      "\"nodes\":[{\"name\":\"JointA\"},{\"name\":\"JointB\"},{\"name\":\"MeshNode\",\"mesh\":0,\"skin\":0}],"
      "\"skins\":[{\"joints\":[0,1],\"inverseBindMatrices\":3}],"
      "\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":0,\"JOINTS_0\":1,\"WEIGHTS_0\":2},"
      "\"indices\":4,\"mode\":4}]}],"
      "\"buffers\":[{\"byteLength\":" + std::to_string(bin.size()) + "}],"
      "\"bufferViews\":["
      "{\"buffer\":0,\"byteOffset\":" + std::to_string(position_offset) + ",\"byteLength\":48,\"target\":34962},"
      "{\"buffer\":0,\"byteOffset\":" + std::to_string(joint_offset) + ",\"byteLength\":32,\"target\":34962},"
      "{\"buffer\":0,\"byteOffset\":" + std::to_string(weight_offset) + ",\"byteLength\":64,\"target\":34962},"
      "{\"buffer\":0,\"byteOffset\":" + std::to_string(inverse_bind_offset) + ",\"byteLength\":128},"
      "{\"buffer\":0,\"byteOffset\":" + std::to_string(index_offset) + ",\"byteLength\":12,\"target\":34963}],"
      "\"accessors\":["
      "{\"bufferView\":0,\"componentType\":5126,\"count\":4,\"type\":\"VEC3\",\"min\":[0,0,0],\"max\":[1,1,0]},"
      "{\"bufferView\":1,\"componentType\":5123,\"count\":4,\"type\":\"VEC4\"},"
      "{\"bufferView\":2,\"componentType\":5126,\"count\":4,\"type\":\"VEC4\"},"
      "{\"bufferView\":3,\"componentType\":5126,\"count\":2,\"type\":\"MAT4\"},"
      "{\"bufferView\":4,\"componentType\":5123,\"count\":6,\"type\":\"SCALAR\"}]}";

  std::vector<std::uint8_t> json_chunk(json.begin(), json.end());
  while ((json_chunk.size() % 4u) != 0u) {
    json_chunk.push_back(' ');
  }

  std::vector<std::uint8_t> glb;
  const std::uint32_t total_length =
      12u + 8u + static_cast<std::uint32_t>(json_chunk.size()) +
      8u + static_cast<std::uint32_t>(bin.size());
  appendU32(glb, 0x46546C67u);
  appendU32(glb, 2u);
  appendU32(glb, total_length);
  appendU32(glb, static_cast<std::uint32_t>(json_chunk.size()));
  appendU32(glb, 0x4E4F534Au);
  glb.insert(glb.end(), json_chunk.begin(), json_chunk.end());
  appendU32(glb, static_cast<std::uint32_t>(bin.size()));
  appendU32(glb, 0x004E4942u);
  glb.insert(glb.end(), bin.begin(), bin.end());

  std::ofstream out(path, std::ios::binary);
  if (!out) {
    return false;
  }
  out.write(reinterpret_cast<const char*>(glb.data()), static_cast<std::streamsize>(glb.size()));
  return out.good();
}

glm::mat4 gltfNodeLocalMatrix(const karma::scene::GltfDocument& doc, uint32_t node_index) {
  if (!doc.valid() ||
      !doc.json.contains("nodes") ||
      !doc.json["nodes"].is_array() ||
      node_index >= doc.json["nodes"].size()) {
    return glm::mat4(1.0f);
  }

  const auto& node = doc.json["nodes"][node_index];
  if (node.contains("matrix") && node["matrix"].is_array() && node["matrix"].size() == 16) {
    glm::mat4 matrix(1.0f);
    for (int column = 0; column < 4; ++column) {
      for (int row = 0; row < 4; ++row) {
        matrix[column][row] = node["matrix"][column * 4 + row].get<float>();
      }
    }
    return matrix;
  }

  glm::vec3 translation(0.0f);
  if (node.contains("translation") && node["translation"].is_array() &&
      node["translation"].size() == 3) {
    translation = {node["translation"][0].get<float>(),
                   node["translation"][1].get<float>(),
                   node["translation"][2].get<float>()};
  }

  glm::quat rotation(1.0f, 0.0f, 0.0f, 0.0f);
  if (node.contains("rotation") && node["rotation"].is_array() &&
      node["rotation"].size() == 4) {
    rotation = glm::normalize(glm::quat(node["rotation"][3].get<float>(),
                                        node["rotation"][0].get<float>(),
                                        node["rotation"][1].get<float>(),
                                        node["rotation"][2].get<float>()));
  }

  glm::vec3 scale(1.0f);
  if (node.contains("scale") && node["scale"].is_array() && node["scale"].size() == 3) {
    scale = {node["scale"][0].get<float>(),
             node["scale"][1].get<float>(),
             node["scale"][2].get<float>()};
  }

  glm::mat4 matrix(1.0f);
  matrix = glm::translate(matrix, translation);
  matrix *= glm::mat4_cast(rotation);
  matrix = glm::scale(matrix, scale);
  return matrix;
}

std::vector<glm::mat4> buildGltfWorldMatrices(const karma::scene::GltfDocument& doc) {
  std::vector<glm::mat4> matrices;
  if (!doc.valid() || !doc.json.contains("nodes") || !doc.json["nodes"].is_array()) {
    return matrices;
  }

  const size_t node_count = doc.json["nodes"].size();
  matrices.assign(node_count, glm::mat4(1.0f));
  std::vector<int> parents(node_count, -1);
  for (size_t node_index = 0; node_index < node_count; ++node_index) {
    const auto& node = doc.json["nodes"][node_index];
    if (!node.contains("children") || !node["children"].is_array()) {
      continue;
    }
    for (const auto& child : node["children"]) {
      if (!child.is_number_unsigned()) {
        continue;
      }
      const uint32_t child_index = child.get<uint32_t>();
      if (child_index < parents.size()) {
        parents[child_index] = static_cast<int>(node_index);
      }
    }
  }

  std::vector<uint8_t> composed(node_count, 0u);
  auto compose_node = [&](auto&& self, size_t node_index) -> glm::mat4 {
    if (composed[node_index] != 0u) {
      return matrices[node_index];
    }
    glm::mat4 matrix = gltfNodeLocalMatrix(doc, static_cast<uint32_t>(node_index));
    if (parents[node_index] >= 0) {
      matrix = self(self, static_cast<size_t>(parents[node_index])) * matrix;
    }
    matrices[node_index] = matrix;
    composed[node_index] = 1u;
    return matrix;
  };

  for (size_t node_index = 0; node_index < node_count; ++node_index) {
    compose_node(compose_node, node_index);
  }
  return matrices;
}

std::unordered_map<std::string, uint32_t> buildPrefabNodeMap(
    const karma::scene::GlbScenePrefab& prefab) {
  std::unordered_map<std::string, uint32_t> out;
  for (uint32_t i = 0; i < prefab.nodes.size(); ++i) {
    if (!prefab.nodes[i].name.empty()) {
      out.emplace(prefab.nodes[i].name, i);
    }
  }
  return out;
}

karma::animation::PoseHierarchy buildPrefabPoseHierarchy(
    const karma::scene::GlbScenePrefab& prefab) {
  karma::animation::PoseHierarchy hierarchy{};
  hierarchy.parent_indices.assign(prefab.nodes.size(), karma::animation::kInvalidAnimationIndex);
  hierarchy.rest_local_transforms.resize(prefab.nodes.size());
  for (uint32_t node_index = 0; node_index < prefab.nodes.size(); ++node_index) {
    const auto& node = prefab.nodes[node_index];
    hierarchy.rest_local_transforms[node_index] = karma::animation::PoseTransform{
        .position = node.local_position,
        .rotation = node.local_rotation,
        .scale = node.local_scale,
        .has_position = true,
        .has_rotation = true,
        .has_scale = true,
    };
    for (const uint32_t child : node.children) {
      if (child < hierarchy.parent_indices.size()) {
        hierarchy.parent_indices[child] = node_index;
      }
    }
  }
  return hierarchy;
}

struct Bounds {
  glm::vec3 min{0.0f};
  glm::vec3 max{0.0f};
  bool valid = false;
};

void expandBounds(Bounds& bounds, const glm::vec3& point) {
  if (!bounds.valid) {
    bounds.min = point;
    bounds.max = point;
    bounds.valid = true;
    return;
  }
  bounds.min = glm::min(bounds.min, point);
  bounds.max = glm::max(bounds.max, point);
}

bool nearVec3(const glm::vec3& a, const glm::vec3& b, float epsilon = 0.001f) {
  return near(a.x, b.x, epsilon) && near(a.y, b.y, epsilon) && near(a.z, b.z, epsilon);
}

glm::mat4 toMatrix(const karma::components::TransformComponent& transform) {
  return composeTransform(transform.getPosition(), transform.getRotation(), transform.getScale());
}

karma::animation::AnimationClip makeMoveClip(float end_x) {
  karma::animation::AnimationClip clip{};
  clip.name = "Move";
  clip.duration_seconds = 1.0f;
  clip.channels.push_back(karma::animation::AnimationChannel{
      .target_node_index = 1,
      .position_keys = {
          {.time_seconds = 0.0f, .value = {0.0f, 0.0f, 0.0f}},
          {.time_seconds = 1.0f, .value = {end_x, 0.0f, 0.0f}},
      },
  });
  return clip;
}

void testClipSampling() {
  const std::vector<karma::animation::Vec3Keyframe> vec_keys{
      {.time_seconds = 0.5f, .value = {1.0f, 2.0f, 3.0f}},
      {.time_seconds = 1.5f, .value = {3.0f, 6.0f, 9.0f}},
  };
  const auto before = karma::animation::sampleVec3Keyframes(vec_keys, 0.0f);
  assert(before && near(before->x, 1.0f));

  const auto middle = karma::animation::sampleVec3Keyframes(vec_keys, 1.0f);
  assert(middle && near(middle->x, 2.0f) && near(middle->y, 4.0f) && near(middle->z, 6.0f));

  const auto after = karma::animation::sampleVec3Keyframes(vec_keys, 2.0f);
  assert(after && near(after->x, 3.0f));

  karma::animation::AnimationClip clip{};
  clip.duration_seconds = 1.0f;
  assert(near(karma::animation::normalizeAnimationTime(clip, 1.25f, true), 0.25f));

  const std::vector<karma::animation::QuatKeyframe> quat_keys{
      {.time_seconds = 0.0f, .value = {0.0f, 0.0f, 0.0f, 1.0f}},
      {.time_seconds = 1.0f, .value = {0.0f, 1.0f, 0.0f, 0.0f}},
  };
  const auto rot = karma::animation::sampleQuatKeyframes(quat_keys, 0.5f);
  assert(rot && near(quatLength(*rot), 1.0f));
}

void testInterpolationModes() {
  const std::vector<karma::animation::Vec3Keyframe> keys{
      {.time_seconds = 0.0f,
       .value = {0.0f, 0.0f, 0.0f},
       .out_tangent = {0.0f, 0.0f, 0.0f}},
      {.time_seconds = 1.0f,
       .value = {10.0f, 0.0f, 0.0f},
       .in_tangent = {0.0f, 0.0f, 0.0f}},
  };

  const auto step = karma::animation::sampleVec3Keyframes(
      keys, 0.5f, karma::animation::InterpolationMode::Step);
  assert(step && near(step->x, 0.0f));

  const auto linear = karma::animation::sampleVec3Keyframes(
      keys, 0.5f, karma::animation::InterpolationMode::Linear);
  assert(linear && near(linear->x, 5.0f));

  const auto cubic = karma::animation::sampleVec3Keyframes(
      keys, 0.25f, karma::animation::InterpolationMode::CubicSpline);
  assert(cubic && near(cubic->x, 1.5625f));
}

void testHierarchyAndPlayback() {
  karma::ecs::World world;
  karma::scene::Scene scene;

  const auto root = world.createEntity();
  world.add(root, karma::components::TransformComponent{});
  world.add(root, karma::components::LocalTransformComponent{{10.0f, 0.0f, 0.0f}});
  const auto root_node = scene.createNode(root);

  const auto child = world.createEntity();
  world.add(child, karma::components::TransformComponent{});
  world.add(child, karma::components::LocalTransformComponent{{2.0f, 0.0f, 0.0f}});
  const auto child_node = scene.createNode(child);
  scene.reparent(child_node, root_node);

  karma::scene::updateWorldTransforms(world, scene);
  assert(near(world.get<karma::components::TransformComponent>(child).getPosition().x, 12.0f));

  world.add(root, karma::components::AnimationPlayerComponent{
                      .clips = {makeMoveClip(4.0f)},
                      .node_entities_by_index = {root, child},
                      .current_clip_index = 0,
                      .time_seconds = 0.0f,
                      .speed = 1.0f,
                      .loop = true,
                      .playing = true});

  karma::animation::AnimationSystem animation_system;
  animation_system.update(world, scene, 0.5f);
  karma::scene::updateWorldTransforms(world, scene);
  assert(near(world.get<karma::components::TransformComponent>(child).getPosition().x, 12.0f));

  auto& player = world.get<karma::components::AnimationPlayerComponent>(root);
  karma::components::pauseAnimation(player);
  animation_system.update(world, scene, 0.5f);
  karma::scene::updateWorldTransforms(world, scene);
  assert(near(world.get<karma::components::TransformComponent>(child).getPosition().x, 12.0f));

  player.clips.push_back(makeMoveClip(8.0f));
  player.time_seconds = 0.75f;
  assert(karma::components::setAnimationClip(player, 1, true));
  assert(player.current_clip_index == 1 && near(player.time_seconds, 0.0f));
}

void testAnimatorStateMachineAndEvents() {
  karma::ecs::World world;
  karma::scene::Scene scene;

  const auto root = world.createEntity();
  world.add(root, karma::components::TransformComponent{});
  world.add(root, karma::components::LocalTransformComponent{});
  const auto root_node = scene.createNode(root);

  const auto child = world.createEntity();
  world.add(child, karma::components::TransformComponent{});
  world.add(child, karma::components::LocalTransformComponent{});
  const auto child_node = scene.createNode(child);
  scene.reparent(child_node, root_node);

  karma::animation::AnimationClip idle = makeMoveClip(0.0f);
  idle.name = "Idle";
  karma::animation::AnimationClip run = makeMoveClip(10.0f);
  run.name = "Run";
  run.events.push_back({.name = "Footstep", .time_seconds = 0.25f});

  karma::components::AnimatorComponent animator{};
  animator.clips = {idle, run};
  animator.node_entities_by_index = {root, child};
  animator.playing = true;
  animator.state_machine.parameters.push_back(karma::components::AnimatorParameter{
      .name = "go",
      .type = karma::components::AnimatorParameterType::Trigger,
  });
  animator.state_machine.states.push_back(karma::components::AnimatorState{
      .name = "Idle",
      .motion_type = karma::components::AnimatorMotionType::Clip,
      .clip_index = 0,
      .transitions = {karma::components::AnimatorTransition{
          .to_state_index = 1,
          .conditions = {karma::components::AnimatorCondition{
              .parameter = "go",
              .op = karma::components::AnimatorConditionOp::If,
          }},
      }},
  });
  animator.state_machine.states.push_back(karma::components::AnimatorState{
      .name = "Run",
      .motion_type = karma::components::AnimatorMotionType::Clip,
      .clip_index = 1,
  });
  world.add(root, animator);

  auto& live_animator = world.get<karma::components::AnimatorComponent>(root);
  assert(karma::components::setAnimatorTrigger(live_animator, "go"));
  karma::animation::AnimationSystem animation_system;
  animation_system.update(world, scene, 0.0f);
  assert(live_animator.current_state_index == 1);
  const auto* trigger = karma::components::findAnimatorParameter(live_animator, "go");
  assert(trigger && !trigger->trigger_value);

  animation_system.update(world, scene, 0.25f);
  assert(live_animator.event_queue.size() == 1);
  assert(live_animator.event_queue.front().name == "Footstep");
  assert(near(world.get<karma::components::LocalTransformComponent>(child).position.x, 2.5f));

  animation_system.update(world, scene, 0.1f);
  assert(live_animator.event_queue.empty());
}

void testAnimatorBlendTreeAndRootMotion() {
  karma::ecs::World world;
  karma::scene::Scene scene;

  const auto root = world.createEntity();
  world.add(root, karma::components::TransformComponent{});
  world.add(root, karma::components::LocalTransformComponent{});
  const auto root_node = scene.createNode(root);

  const auto child = world.createEntity();
  world.add(child, karma::components::TransformComponent{});
  world.add(child, karma::components::LocalTransformComponent{});
  const auto child_node = scene.createNode(child);
  scene.reparent(child_node, root_node);

  karma::animation::AnimationClip slow = makeMoveClip(2.0f);
  slow.name = "Slow";
  slow.root_motion = karma::animation::RootMotionTrack{
      .target_node_index = 0,
      .position_keys = {
          {.time_seconds = 0.0f, .value = {0.0f, 0.0f, 0.0f}},
          {.time_seconds = 1.0f, .value = {4.0f, 0.0f, 0.0f}},
      },
  };
  karma::animation::AnimationClip fast = makeMoveClip(10.0f);
  fast.name = "Fast";

  karma::components::AnimatorComponent animator{};
  animator.clips = {slow, fast};
  animator.node_entities_by_index = {root, child};
  animator.playing = true;
  animator.root_motion_mode = karma::components::RootMotionMode::ExposeDelta;
  animator.state_machine.parameters.push_back(karma::components::AnimatorParameter{
      .name = "speed",
      .type = karma::components::AnimatorParameterType::Float,
      .float_value = 0.5f,
  });
  animator.state_machine.states.push_back(karma::components::AnimatorState{
      .name = "Move",
      .motion_type = karma::components::AnimatorMotionType::BlendTree1D,
      .blend_tree = karma::components::AnimatorBlendTree1D{
          .parameter = "speed",
          .children = {
              {.clip_index = 0, .threshold = 0.0f},
              {.clip_index = 1, .threshold = 1.0f},
          },
      },
  });
  world.add(root, animator);

  karma::animation::AnimationSystem animation_system;
  animation_system.update(world, scene, 0.5f);

  auto& live_animator = world.get<karma::components::AnimatorComponent>(root);
  assert(near(world.get<karma::components::LocalTransformComponent>(child).position.x, 3.0f));
  assert(live_animator.root_motion_delta.position);
  assert(near(live_animator.root_motion_delta.position->x, 2.0f));
}

void testCpuSkinning() {
  karma::geometry::MeshData bind_mesh{};
  bind_mesh.vertices.push_back({1.0f, 0.0f, 0.0f});
  bind_mesh.normals.push_back({1.0f, 0.0f, 0.0f});
  bind_mesh.indices.push_back(0);

  std::vector<karma::components::VertexSkinInfluence> influences{
      {.joints = {0u, 0u, 0u, 0u}, .weights = {1.0f, 0.0f, 0.0f, 0.0f}},
  };
  std::vector<glm::mat4> skin_matrices{
      glm::translate(glm::mat4(1.0f), glm::vec3(2.0f, 0.0f, 0.0f)),
  };

  const karma::geometry::MeshData skinned =
      karma::animation::skinMesh(bind_mesh, influences, skin_matrices);
  assert(skinned.vertices.size() == 1);
  assert(near(skinned.vertices[0].x, 3.0f));
  assert(near(skinned.vertices[0].y, 0.0f));
  assert(near(skinned.vertices[0].z, 0.0f));
}

void testMorphTargets() {
  karma::geometry::MeshData bind_mesh{};
  bind_mesh.vertices.push_back({1.0f, 0.0f, 0.0f});
  bind_mesh.normals.push_back({1.0f, 0.0f, 0.0f});
  bind_mesh.tangents.push_back({1.0f, 0.0f, 0.0f, 1.0f});
  bind_mesh.indices.push_back(0);
  bind_mesh.morph_targets.push_back(karma::geometry::MeshData::MorphTarget{
      .position_deltas = {{2.0f, 0.0f, 0.0f}},
      .normal_deltas = {{0.0f, 1.0f, 0.0f}},
      .tangent_deltas = {{0.0f, 1.0f, 0.0f}},
  });

  const karma::geometry::MeshData morphed =
      karma::animation::morphMesh(bind_mesh, {0.25f});
  assert(morphed.vertices.size() == 1);
  assert(near(morphed.vertices[0].x, 1.5f));
  assert(near(glm::length(morphed.normals[0]), 1.0f));
  assert(near(glm::length(glm::vec3(morphed.tangents[0])), 1.0f));

  std::vector<karma::components::VertexSkinInfluence> influences{
      {.joints = {0u, 0u, 0u, 0u}, .weights = {1.0f, 0.0f, 0.0f, 0.0f}},
  };
  const std::vector<glm::mat4> skin_matrices{
      glm::translate(glm::mat4(1.0f), glm::vec3(1.0f, 0.0f, 0.0f)),
  };
  const karma::geometry::MeshData morphed_then_skinned =
      karma::animation::skinMesh(morphed, influences, skin_matrices);
  assert(near(morphed_then_skinned.vertices[0].x, 2.5f));
}

void testAnimationSystemUpdatesMorphWeights() {
  karma::ecs::World world;
  karma::scene::Scene scene;

  const auto root = world.createEntity();
  world.add(root, karma::components::TransformComponent{});
  world.add(root, karma::components::LocalTransformComponent{});
  scene.createNode(root);

  karma::geometry::MeshData bind_mesh{};
  bind_mesh.vertices.push_back({0.0f, 0.0f, 0.0f});
  bind_mesh.morph_targets.push_back(karma::geometry::MeshData::MorphTarget{
      .position_deltas = {{1.0f, 0.0f, 0.0f}},
  });

  const auto primitive = world.createEntity();
  world.add(primitive, karma::components::MorphTargetComponent{
                           .bind_mesh = bind_mesh,
                           .deformed_mesh = bind_mesh,
                           .base_weights = {0.0f},
                           .weights = {0.0f},
                           .weights_dirty = false,
                           .enabled = true});

  karma::animation::AnimationClip clip{};
  clip.name = "Morph";
  clip.duration_seconds = 1.0f;
  clip.morph_target_tracks.push_back(karma::animation::MorphTargetTrack{
      .target_node_index = 0,
      .interpolation = karma::animation::InterpolationMode::Linear,
      .weight_keys = {
          {.time_seconds = 0.0f, .values = {0.0f}},
          {.time_seconds = 1.0f, .values = {1.0f}},
      },
  });
  world.add(root, karma::components::AnimationPlayerComponent{
                      .clips = {clip},
                      .node_entities_by_index = {root},
                      .morph_entities_by_node_index = {{primitive}},
                      .current_clip_index = 0,
                      .time_seconds = 0.0f,
                      .speed = 1.0f,
                      .loop = true,
                      .playing = true});

  karma::animation::AnimationSystem animation_system;
  animation_system.update(world, scene, 0.5f);

  auto& morph = world.get<karma::components::MorphTargetComponent>(primitive);
  assert(morph.weights.size() == 1);
  assert(near(morph.weights[0], 0.5f));
  assert(morph.weights_dirty);

  karma::animation::AnimationClip base_clip{};
  base_clip.name = "Base";
  base_clip.duration_seconds = 1.0f;
  auto& player = world.get<karma::components::AnimationPlayerComponent>(root);
  player.clips.push_back(base_clip);
  assert(karma::components::setAnimationClip(player, 1, true));
  morph.weights_dirty = false;
  animation_system.update(world, scene, 0.0f);
  assert(near(morph.weights[0], 0.0f));
  assert(morph.weights_dirty);
}

void testPoseCompositionAndPalette() {
  karma::animation::PoseHierarchy hierarchy{};
  hierarchy.parent_indices = {
      karma::animation::kInvalidAnimationIndex,
      0u,
  };
  hierarchy.rest_local_transforms.resize(2);
  hierarchy.rest_local_transforms[0].position = {1.0f, 0.0f, 0.0f};
  hierarchy.rest_local_transforms[1].position = {2.0f, 0.0f, 0.0f};

  karma::animation::LocalPose pose = karma::animation::makeRestLocalPose(hierarchy);
  karma::animation::SampledTransform child_sample{};
  child_sample.position = karma::math::Vec3{3.0f, 0.0f, 0.0f};
  karma::animation::applySampleToLocalPose(pose, 1u, child_sample);

  const karma::animation::ModelPose model_pose =
      karma::animation::composeModelPose(hierarchy, pose);
  assert(model_pose.node_matrices.size() == 2);
  assert(near(model_pose.node_matrices[1][3].x, 4.0f));

  const std::vector<uint32_t> joint_node_indices{1u};
  const std::vector<glm::mat4> inverse_binds{
      glm::translate(glm::mat4(1.0f), glm::vec3(-3.0f, 0.0f, 0.0f)),
  };
  const karma::animation::SkinningPalette palette =
      karma::animation::buildSkinningPalette(joint_node_indices,
                                             inverse_binds,
                                             model_pose.node_matrices,
                                             glm::mat4(1.0f));
  assert(palette.valid);
  assert(palette.joint_matrices.size() == 1);
  assert(near(palette.joint_matrices[0][3].x, 1.0f));
}

void testSkinnedGlbImport() {
  const std::filesystem::path path =
      std::filesystem::temp_directory_path() / "karma_test_skinned_animation.glb";
  assert(writeSkinnedGlb(path));

  const karma::scene::GlbScenePrefab prefab = karma::scene::loadGlbScenePrefab(path);
  assert(prefab.valid());
  assert(!prefab.animations.empty());
  assert(!prefab.skins.empty());
  assert(!prefab.skeletons.empty());
  assert(prefab.skins.front().joint_node_indices.size() == 1);
  assert(prefab.skeletons.front().joints.size() == 1);

  bool found_skinned_primitive = false;
  for (const auto& node : prefab.nodes) {
    for (const auto& primitive : node.primitives) {
      if (!primitive.skinned()) {
        continue;
      }
      found_skinned_primitive = true;
      assert(primitive.joint_node_indices.size() == 1);
      assert(primitive.inverse_bind_matrices.size() == 1);
      assert(primitive.vertex_influences.size() == primitive.mesh.vertices.size());
      assert(near(primitive.vertex_influences.front().weights.x, 1.0f));
    }
  }
  assert(found_skinned_primitive);
}

void testSplitWeightGlbImport() {
  const std::filesystem::path path =
      std::filesystem::temp_directory_path() / "karma_test_split_weight_skin.glb";
  assert(writeSplitWeightGlb(path));

  const karma::scene::GlbScenePrefab prefab = karma::scene::loadGlbScenePrefab(path);
  assert(prefab.valid());
  assert(!prefab.skins.empty());
  assert(prefab.skins.front().joint_node_indices.size() == 2);

  bool found_skinned_primitive = false;
  for (const auto& node : prefab.nodes) {
    for (const auto& primitive : node.primitives) {
      if (!primitive.skinned()) {
        continue;
      }
      found_skinned_primitive = true;
      assert(primitive.mesh.vertices.size() == 4);
      assert(primitive.vertex_influences.size() == 4);
      assert(primitive.mesh.joint_indices.size() == 4);
      assert(primitive.mesh.joint_weights.size() == 4);
      assert(primitive.vertex_influences[0].joints.x == 0u);
      assert(primitive.vertex_influences[1].joints.x == 1u);
      assert(primitive.vertex_influences[2].joints.x == 0u);
      assert(primitive.vertex_influences[3].joints.x == 1u);
      assert(near(primitive.vertex_influences[1].weights.x, 1.0f));
    }
  }
  assert(found_skinned_primitive);
}

void testGltfMeshReplacementImportsAllMeshNodes() {
  karma::scene::GltfDocument doc{};
  for (int i = 0; i < 3; ++i) {
    appendFloat(doc.bin, static_cast<float>(i));
    appendFloat(doc.bin, 0.0f);
    appendFloat(doc.bin, 0.0f);
  }
  doc.json = karma::scene::Json{
      {"asset", {{"version", "2.0"}}},
      {"nodes",
       karma::scene::Json::array({
           {{"name", "StaticNode"}, {"mesh", 0u}},
           {{"name", "SkinnedNode"}, {"mesh", 1u}, {"skin", 0u}},
       })},
      {"meshes",
       karma::scene::Json::array({
           {{"primitives",
             karma::scene::Json::array({
                 {{"attributes", {{"POSITION", 0u}}}, {"material", 1u}},
             })}},
           {{"primitives",
             karma::scene::Json::array({
                 {{"attributes", {{"POSITION", 0u}}}, {"material", 2u}},
             })}},
       })},
      {"skins", karma::scene::Json::array({{{"joints", karma::scene::Json::array()}}})},
      {"bufferViews",
       karma::scene::Json::array({
           {{"buffer", 0u}, {"byteOffset", 0u}, {"byteLength", doc.bin.size()}},
       })},
      {"accessors",
       karma::scene::Json::array({
           {{"bufferView", 0u}, {"componentType", 5126}, {"count", 3u}, {"type", "VEC3"}},
       })},
  };

  karma::scene::GlbScenePrefab prefab{};
  prefab.nodes.resize(2);
  prefab.nodes[0].name = "StaticNode";
  prefab.nodes[0].primitives.push_back(karma::scene::GlbScenePrefabPrimitive{});
  prefab.nodes[0].primitives.front().source_material_index = 10;
  prefab.nodes[0].primitives.front().mesh.vertices.push_back({42.0f, 0.0f, 0.0f});
  prefab.nodes[1].name = "SkinnedNode";
  prefab.nodes[1].primitives.push_back(karma::scene::GlbScenePrefabPrimitive{});
  prefab.nodes[1].primitives.front().source_material_index = 11;
  prefab.nodes[1].primitives.front().mesh.vertices.push_back({42.0f, 0.0f, 0.0f});

  const std::unordered_map<std::string, uint32_t> node_indices_by_name{
      {"StaticNode", 0u},
      {"SkinnedNode", 1u},
  };
  karma::scene::populateGltfMeshData(doc, node_indices_by_name, prefab);

  assert(prefab.nodes[0].primitives.front().mesh.vertices.size() == 3);
  assert(prefab.nodes[0].primitives.front().source_material_index == 10u);
  assert(prefab.nodes[0].primitives.front().source_gltf_material_index == 1u);
  assert(prefab.nodes[1].primitives.front().mesh.vertices.size() == 3);
  assert(prefab.nodes[1].primitives.front().source_material_index == 11u);
  assert(prefab.nodes[1].primitives.front().source_gltf_material_index == 2u);
}

void testGltfMorphTargetImport() {
  karma::scene::GltfDocument doc{};
  const std::uint32_t position_offset = static_cast<std::uint32_t>(doc.bin.size());
  appendFloat(doc.bin, 0.0f); appendFloat(doc.bin, 0.0f); appendFloat(doc.bin, 0.0f);
  appendFloat(doc.bin, 1.0f); appendFloat(doc.bin, 0.0f); appendFloat(doc.bin, 0.0f);
  appendFloat(doc.bin, 0.0f); appendFloat(doc.bin, 1.0f); appendFloat(doc.bin, 0.0f);

  const std::uint32_t morph_offset = static_cast<std::uint32_t>(doc.bin.size());
  appendFloat(doc.bin, 0.0f); appendFloat(doc.bin, 0.0f); appendFloat(doc.bin, 1.0f);
  appendFloat(doc.bin, 0.0f); appendFloat(doc.bin, 0.0f); appendFloat(doc.bin, 1.0f);
  appendFloat(doc.bin, 0.0f); appendFloat(doc.bin, 0.0f); appendFloat(doc.bin, 1.0f);

  const std::uint32_t time_offset = static_cast<std::uint32_t>(doc.bin.size());
  appendFloat(doc.bin, 0.0f); appendFloat(doc.bin, 1.0f);

  const std::uint32_t weight_offset = static_cast<std::uint32_t>(doc.bin.size());
  appendFloat(doc.bin, 0.0f); appendFloat(doc.bin, 1.0f);

  doc.json = karma::scene::Json{
      {"asset", {{"version", "2.0"}}},
      {"nodes", karma::scene::Json::array({
                    {{"name", "MorphNode"}, {"mesh", 0u}},
                })},
      {"meshes",
       karma::scene::Json::array({
           {{"weights", karma::scene::Json::array({0.25f})},
            {"primitives",
             karma::scene::Json::array({
                 {{"attributes", {{"POSITION", 0u}}},
                  {"targets", karma::scene::Json::array({
                                  {{"POSITION", 1u}},
                              })}},
             })}},
       })},
      {"animations",
       karma::scene::Json::array({
           {{"samplers",
             karma::scene::Json::array({
                 {{"input", 2u}, {"output", 3u}},
             })},
            {"channels",
             karma::scene::Json::array({
                 {{"sampler", 0u}, {"target", {{"node", 0u}, {"path", "weights"}}}},
             })}},
       })},
      {"bufferViews",
       karma::scene::Json::array({
           {{"buffer", 0u}, {"byteOffset", position_offset}, {"byteLength", 36u}},
           {{"buffer", 0u}, {"byteOffset", morph_offset}, {"byteLength", 36u}},
           {{"buffer", 0u}, {"byteOffset", time_offset}, {"byteLength", 8u}},
           {{"buffer", 0u}, {"byteOffset", weight_offset}, {"byteLength", 8u}},
       })},
      {"accessors",
       karma::scene::Json::array({
           {{"bufferView", 0u}, {"componentType", 5126}, {"count", 3u}, {"type", "VEC3"}},
           {{"bufferView", 1u}, {"componentType", 5126}, {"count", 3u}, {"type", "VEC3"}},
           {{"bufferView", 2u}, {"componentType", 5126}, {"count", 2u}, {"type", "SCALAR"}},
           {{"bufferView", 3u}, {"componentType", 5126}, {"count", 2u}, {"type", "SCALAR"}},
       })},
  };

  karma::scene::GlbScenePrefab prefab{};
  prefab.nodes.resize(1);
  prefab.nodes[0].name = "MorphNode";
  prefab.nodes[0].primitives.push_back(karma::scene::GlbScenePrefabPrimitive{});

  const std::unordered_map<std::string, uint32_t> node_indices_by_name{{"MorphNode", 0u}};
  karma::scene::populateGltfMeshData(doc, node_indices_by_name, prefab);
  const auto& primitive = prefab.nodes[0].primitives.front();
  assert(primitive.morphable());
  assert(primitive.mesh.morph_targets.size() == 1);
  assert(primitive.mesh.morph_targets.front().position_deltas.size() == 3);
  assert(near(primitive.mesh.morph_targets.front().position_deltas.front().z, 1.0f));
  assert(primitive.morph_weights.size() == 1);
  assert(near(primitive.morph_weights[0], 0.25f));

  const std::vector<karma::animation::AnimationClip> clips =
      karma::scene::loadGltfAnimationClips(doc, node_indices_by_name, prefab);
  assert(clips.size() == 1);
  assert(clips.front().morph_target_tracks.size() == 1);
  assert(clips.front().morph_target_tracks.front().target_node_index == 0u);
  assert(clips.front().morph_target_tracks.front().target_mesh_index == 0u);

  bool sampled_morph = false;
  karma::animation::sampleAnimationClip(
      clips.front(),
      0.5f,
      true,
      [&](uint32_t, const karma::animation::SampledTransform&) {},
      [&](uint32_t target_node_index, const std::vector<float>& weights) {
        sampled_morph = true;
        assert(target_node_index == 0u);
        assert(weights.size() == 1);
        assert(near(weights[0], 0.5f));
      });
  assert(sampled_morph);
}

void testWalkingGlbImportSmoke() {
  const std::filesystem::path path =
      findRepoRoot() / "examples/assets/animation_model/source/walking.glb";
  assert(std::filesystem::exists(path));

  const karma::scene::GlbScenePrefab prefab = karma::scene::loadGlbScenePrefab(path);
  const karma::scene::GltfDocument gltf = karma::scene::loadGltfDocument(path);
  assert(prefab.valid());
  assert(gltf.valid());
  assert(gltf.json.contains("materials") && gltf.json["materials"].size() == 7);
  assert(gltf.json.contains("images") && gltf.json["images"].size() == 11);
  assert(prefab.diagnostics.empty());
  assert(prefab.animations.size() == 1);
  assert(prefab.skins.size() == 1);
  assert(prefab.skeletons.size() == 1);
  assert(prefab.skins.front().joint_node_indices.size() == 65);
  assert(prefab.skeletons.front().joints.size() == 65);

  const std::vector<glm::mat4> gltf_world_matrices = buildGltfWorldMatrices(gltf);
  const std::unordered_map<std::string, uint32_t> prefab_nodes_by_name =
      buildPrefabNodeMap(prefab);
  const auto suit_node_it = prefab_nodes_by_name.find("000_suit_sheet");
  assert(suit_node_it != prefab_nodes_by_name.end());
  assert(suit_node_it->second < prefab.nodes.size());
  assert(!prefab.nodes[suit_node_it->second].primitives.empty());
  const karma::geometry::MeshData& suit_mesh =
      prefab.nodes[suit_node_it->second].primitives.front().mesh;
  assert(!suit_mesh.uvs.empty());
  assert(near(suit_mesh.uvs.front().x, 0.5138f, 0.0001f));
  assert(near(suit_mesh.uvs.front().y, 0.4959f, 0.0001f));
  assert(suit_mesh.tangents.size() == suit_mesh.vertices.size());
  auto gltf_node_by_name = [&](const char* name) -> uint32_t {
    for (uint32_t node_index = 0; node_index < gltf.json["nodes"].size(); ++node_index) {
      if (karma::scene::gltfNodeName(gltf, node_index) == name) {
        return node_index;
      }
    }
    return karma::scene::kInvalidGlbSceneNode;
  };
  auto assert_imported_world_matches_gltf = [&](const char* name) {
    const uint32_t gltf_node = gltf_node_by_name(name);
    const auto prefab_it = prefab_nodes_by_name.find(name);
    assert(gltf_node != karma::scene::kInvalidGlbSceneNode);
    assert(prefab_it != prefab_nodes_by_name.end());
    assert(gltf_node < gltf_world_matrices.size());
    assert(prefab_it->second < prefab.nodes.size());
    const auto& prefab_node = prefab.nodes[prefab_it->second];
    const glm::mat4 prefab_world = composeTransform(prefab_node.world_position,
                                                    prefab_node.world_rotation,
                                                    prefab_node.world_scale);
    assert(maxMatrixDiff(prefab_world, gltf_world_matrices[gltf_node]) < 0.001f);
  };
  assert_imported_world_matches_gltf("Armature");
  assert_imported_world_matches_gltf("mixamorig:Hips");
  assert_imported_world_matches_gltf("mixamorig:LeftUpLeg");
  assert_imported_world_matches_gltf("mixamorig:LeftLeg");
  assert_imported_world_matches_gltf("mixamorig:RightUpLeg");
  assert_imported_world_matches_gltf("mixamorig:RightLeg");

  std::vector<glm::mat4> prefab_world_matrices(prefab.nodes.size(), glm::mat4(1.0f));
  for (size_t node_index = 0; node_index < prefab.nodes.size(); ++node_index) {
    const auto& node = prefab.nodes[node_index];
    prefab_world_matrices[node_index] = composeTransform(node.world_position,
                                                         node.world_rotation,
                                                         node.world_scale);
  }
  const karma::animation::SkinningPalette rest_palette =
      karma::animation::buildSkinningPalette(prefab.skins.front().joint_node_indices,
                                             prefab.skins.front().inverse_bind_matrices,
                                             prefab_world_matrices,
                                             glm::mat4(1.0f),
                                             0u);
  assert(rest_palette.valid);
  for (const glm::mat4& joint_matrix : rest_palette.joint_matrices) {
    assert(maxMatrixDiff(joint_matrix, glm::mat4(1.0f)) < 0.001f);
  }

  const karma::animation::PoseHierarchy hierarchy = buildPrefabPoseHierarchy(prefab);
  karma::animation::LocalPose sampled_pose =
      karma::animation::makeRestLocalPose(hierarchy);
  karma::animation::sampleAnimationClip(
      prefab.animations.front(),
      0.5f,
      true,
      [&](uint32_t target_node_index, const karma::animation::SampledTransform& sampled) {
        karma::animation::applySampleToLocalPose(sampled_pose, target_node_index, sampled);
      });
  const karma::animation::ModelPose sampled_model_pose =
      karma::animation::composeModelPose(hierarchy, sampled_pose);
  const karma::animation::SkinningPalette sampled_palette =
      karma::animation::buildSkinningPalette(prefab.skins.front().joint_node_indices,
                                             prefab.skins.front().inverse_bind_matrices,
                                             sampled_model_pose.node_matrices,
                                             glm::mat4(1.0f),
                                             0u);
  assert(sampled_palette.valid);
  Bounds sampled_bounds{};
  for (const auto& node : prefab.nodes) {
    for (const auto& primitive : node.primitives) {
      if (!primitive.skinned()) {
        continue;
      }
      const karma::geometry::MeshData skinned_mesh =
          karma::animation::skinMesh(primitive.mesh,
                                     primitive.vertex_influences,
                                     sampled_palette.joint_matrices);
      for (const glm::vec3& vertex : skinned_mesh.vertices) {
        expandBounds(sampled_bounds, vertex);
      }
    }
  }
  assert(sampled_bounds.valid);
  assert(nearVec3(sampled_bounds.min, glm::vec3(-0.2676f, -0.0088f, -0.2146f), 0.002f));
  assert(nearVec3(sampled_bounds.max, glm::vec3(0.3042f, 1.2849f, 0.2677f), 0.002f));

  karma::ecs::World runtime_world;
  karma::scene::Scene runtime_scene;
  std::vector<karma::ecs::Entity> runtime_nodes(prefab.nodes.size());
  std::function<karma::scene::NodeId(uint32_t, karma::scene::NodeId)> instantiate_node =
      [&](uint32_t prefab_node_index, karma::scene::NodeId parent_node) {
        const auto& prefab_node = prefab.nodes[prefab_node_index];
        const karma::ecs::Entity entity = runtime_world.createEntity();
        runtime_world.setName(entity, prefab_node.name);
        runtime_world.add(entity,
                          karma::components::TransformComponent{
                              prefab_node.world_position,
                              prefab_node.world_rotation,
                              prefab_node.world_scale});
        runtime_world.add(entity,
                          karma::components::LocalTransformComponent{
                              prefab_node.local_position,
                              prefab_node.local_rotation,
                              prefab_node.local_scale});
        const karma::scene::NodeId node_id = runtime_scene.createNode(entity);
        if (parent_node != karma::scene::Node::kInvalidId) {
          runtime_scene.reparent(node_id, parent_node);
        }
        runtime_nodes[prefab_node_index] = entity;
        for (const uint32_t child : prefab_node.children) {
          instantiate_node(child, node_id);
        }
        return node_id;
      };
  const karma::scene::NodeId runtime_root_node =
      instantiate_node(prefab.root_node, karma::scene::Node::kInvalidId);
  const karma::ecs::Entity runtime_root = runtime_scene.get(runtime_root_node).entity;
  runtime_world.add(runtime_root,
                    karma::components::AnimatorComponent{
                        .clips = prefab.animations,
                        .node_entities_by_index = runtime_nodes,
                        .skeletons = prefab.skeletons,
                        .skins = prefab.skins,
                        .current_clip_index = 0,
                        .time_seconds = 0.0f,
                        .speed = 1.0f,
                        .loop = true,
                        .playing = true});

  struct RuntimePrimitive {
    const karma::scene::GlbScenePrefabPrimitive* primitive = nullptr;
    karma::components::SkinnedMeshComponent skin;
  };
  std::vector<RuntimePrimitive> runtime_primitives;
  for (const auto& node : prefab.nodes) {
    for (const auto& primitive : node.primitives) {
      if (!primitive.skinned()) {
        continue;
      }
      std::vector<karma::ecs::Entity> joint_entities;
      joint_entities.reserve(primitive.joint_node_indices.size());
      for (const uint32_t joint_node_index : primitive.joint_node_indices) {
        joint_entities.push_back(joint_node_index < runtime_nodes.size()
                                     ? runtime_nodes[joint_node_index]
                                     : karma::ecs::Entity{});
      }
      runtime_primitives.push_back(RuntimePrimitive{
          .primitive = &primitive,
          .skin = karma::components::SkinnedMeshComponent{
              .bind_mesh = primitive.mesh,
              .skinned_mesh = primitive.mesh,
              .vertex_influences = primitive.vertex_influences,
              .joint_entities = std::move(joint_entities),
              .inverse_bind_matrices = primitive.inverse_bind_matrices,
              .render_transform_entity = runtime_root,
              .skin_index = primitive.skin_index,
              .skinning_path = karma::components::SkinningPath::Cpu,
              .override_render_transform = true,
              .enabled = true,
          }});
    }
  }

  karma::animation::AnimationSystem animation_system;
  animation_system.update(runtime_world, runtime_scene, 0.5f);
  karma::scene::updateWorldTransforms(runtime_world, runtime_scene);
  const glm::mat4 runtime_render_world =
      toMatrix(runtime_world.get<karma::components::TransformComponent>(runtime_root));
  Bounds runtime_bounds{};
  for (const RuntimePrimitive& runtime_primitive : runtime_primitives) {
    const karma::animation::SkinningPalette palette =
        karma::animation::buildSkinningPaletteFromScene(runtime_primitive.skin,
                                                        runtime_world,
                                                        runtime_scene,
                                                        glm::mat4(1.0f));
    assert(palette.valid);
    const karma::geometry::MeshData skinned_mesh =
        karma::animation::skinMesh(runtime_primitive.primitive->mesh,
                                   runtime_primitive.primitive->vertex_influences,
                                   palette.joint_matrices);
    for (const glm::vec3& vertex : skinned_mesh.vertices) {
      expandBounds(runtime_bounds, glm::vec3(runtime_render_world * glm::vec4(vertex, 1.0f)));
    }
  }
  assert(runtime_bounds.valid);
  assert(nearVec3(runtime_bounds.min, glm::vec3(-0.2676f, -0.0088f, -0.2146f), 0.002f));
  assert(nearVec3(runtime_bounds.max, glm::vec3(0.3042f, 1.2849f, 0.2677f), 0.002f));

  auto has_joint = [&](const char* name) {
    return std::any_of(prefab.skeletons.front().joints.begin(),
                       prefab.skeletons.front().joints.end(),
                       [&](const karma::animation::Joint& joint) {
                         return joint.name == name;
                       });
  };
  assert(has_joint("mixamorig:Hips"));
  assert(has_joint("mixamorig:LeftUpLeg"));
  assert(has_joint("mixamorig:LeftLeg"));
  assert(has_joint("mixamorig:RightUpLeg"));
  assert(has_joint("mixamorig:RightLeg"));

  bool found_skinned_primitive = false;
  for (const auto& node : prefab.nodes) {
    for (const auto& primitive : node.primitives) {
      if (!primitive.skinned()) {
        continue;
      }
      found_skinned_primitive = true;
      assert(primitive.source_material_index < gltf.json["materials"].size());
      assert(primitive.vertex_influences.size() == primitive.mesh.vertices.size());
      assert(primitive.mesh.joint_indices.size() == primitive.mesh.vertices.size());
      assert(primitive.mesh.joint_weights.size() == primitive.mesh.vertices.size());
      assert(primitive.mesh.uvs.size() == primitive.mesh.vertices.size());
    }
  }
  assert(found_skinned_primitive);
}

}  // namespace

int main() {
  testClipSampling();
  testInterpolationModes();
  testHierarchyAndPlayback();
  testAnimatorStateMachineAndEvents();
  testAnimatorBlendTreeAndRootMotion();
  testCpuSkinning();
  testMorphTargets();
  testAnimationSystemUpdatesMorphWeights();
  testPoseCompositionAndPalette();
  testSkinnedGlbImport();
  testSplitWeightGlbImport();
  testGltfMeshReplacementImportsAllMeshNodes();
  testGltfMorphTargetImport();
  testWalkingGlbImportSmoke();
  return 0;
}
