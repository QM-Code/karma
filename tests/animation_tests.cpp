#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>

#include "karma/world.h"
#include "karma/world.h"
#include "karma/world.h"
#include "karma/world.h"
#include "karma/world.h"
#include "karma/components.h"
#include "karma/components.h"
#include "karma/components.h"
#include "karma/world.h"
#include "karma/world.h"
#include "karma/world.h"
#include "../src/content/importers/gltf_scene_animation_import.h"
#include "../src/content/importers/gltf_scene_import_internal.h"
#include "../src/content/importers/gltf_scene_mesh_import.h"

#include <algorithm>
#include <cstdint>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
#include <string>
#include <unordered_map>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include "karma/math.h"

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

std::string dataUriForBytes(const std::vector<std::uint8_t>& data) {
  constexpr char kHex[] = "0123456789ABCDEF";
  std::string uri = "data:application/octet-stream,";
  uri.reserve(uri.size() + data.size() * 3u);
  for (const std::uint8_t byte : data) {
    uri.push_back('%');
    uri.push_back(kHex[(byte >> 4u) & 0x0Fu]);
    uri.push_back(kHex[byte & 0x0Fu]);
  }
  return uri;
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

glm::mat4 gltfNodeLocalMatrix(const karma::world::GltfDocument& doc, uint32_t node_index) {
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

std::vector<glm::mat4> buildGltfWorldMatrices(const karma::world::GltfDocument& doc) {
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
    const karma::world::GltfScenePrefab& prefab) {
  std::unordered_map<std::string, uint32_t> out;
  for (uint32_t i = 0; i < prefab.nodes.size(); ++i) {
    if (!prefab.nodes[i].name.empty()) {
      out.emplace(prefab.nodes[i].name, i);
    }
  }
  return out;
}

karma::world::PoseHierarchy buildPrefabPoseHierarchy(
    const karma::world::GltfScenePrefab& prefab) {
  karma::world::PoseHierarchy hierarchy{};
  hierarchy.parent_indices.assign(prefab.nodes.size(), karma::world::kInvalidAnimationIndex);
  hierarchy.rest_local_transforms.resize(prefab.nodes.size());
  for (uint32_t node_index = 0; node_index < prefab.nodes.size(); ++node_index) {
    const auto& node = prefab.nodes[node_index];
    hierarchy.rest_local_transforms[node_index] = karma::world::PoseTransform{
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

karma::world::AnimationClip makeMoveClip(float end_x) {
  karma::world::AnimationClip clip{};
  clip.name = "Move";
  clip.duration_seconds = 1.0f;
  clip.channels.push_back(karma::world::AnimationChannel{
      .target_node_index = 1,
      .position_keys = {
          {.time_seconds = 0.0f, .value = {0.0f, 0.0f, 0.0f}},
          {.time_seconds = 1.0f, .value = {end_x, 0.0f, 0.0f}},
      },
  });
  return clip;
}

void testClipSampling() {
  const std::vector<karma::world::Vec3Keyframe> vec_keys{
      {.time_seconds = 0.5f, .value = {1.0f, 2.0f, 3.0f}},
      {.time_seconds = 1.5f, .value = {3.0f, 6.0f, 9.0f}},
  };
  const auto before = karma::world::sampleVec3Keyframes(vec_keys, 0.0f);
  assert(before && near(before->x, 1.0f));

  const auto middle = karma::world::sampleVec3Keyframes(vec_keys, 1.0f);
  assert(middle && near(middle->x, 2.0f) && near(middle->y, 4.0f) && near(middle->z, 6.0f));

  const auto after = karma::world::sampleVec3Keyframes(vec_keys, 2.0f);
  assert(after && near(after->x, 3.0f));

  karma::world::AnimationClip clip{};
  clip.duration_seconds = 1.0f;
  assert(near(karma::world::normalizeAnimationTime(clip, 1.25f, true), 0.25f));

  const std::vector<karma::world::QuatKeyframe> quat_keys{
      {.time_seconds = 0.0f, .value = {0.0f, 0.0f, 0.0f, 1.0f}},
      {.time_seconds = 1.0f, .value = {0.0f, 1.0f, 0.0f, 0.0f}},
  };
  const auto rot = karma::world::sampleQuatKeyframes(quat_keys, 0.5f);
  assert(rot && near(quatLength(*rot), 1.0f));
}

void testInterpolationModes() {
  const std::vector<karma::world::Vec3Keyframe> keys{
      {.time_seconds = 0.0f,
       .value = {0.0f, 0.0f, 0.0f},
       .out_tangent = {0.0f, 0.0f, 0.0f}},
      {.time_seconds = 1.0f,
       .value = {10.0f, 0.0f, 0.0f},
       .in_tangent = {0.0f, 0.0f, 0.0f}},
  };

  const auto step = karma::world::sampleVec3Keyframes(
      keys, 0.5f, karma::world::InterpolationMode::Step);
  assert(step && near(step->x, 0.0f));

  const auto linear = karma::world::sampleVec3Keyframes(
      keys, 0.5f, karma::world::InterpolationMode::Linear);
  assert(linear && near(linear->x, 5.0f));

  const auto cubic = karma::world::sampleVec3Keyframes(
      keys, 0.25f, karma::world::InterpolationMode::CubicSpline);
  assert(cubic && near(cubic->x, 1.5625f));
}

void testHierarchyAndPlayback() {
  karma::world::World world;
  karma::world::Scene scene;

  const auto root = world.createEntity();
  world.add(root, karma::components::TransformComponent{{10.0f, 0.0f, 0.0f}});
  const auto root_node = scene.createNode(root);

  const auto child = world.createEntity();
  world.add(child, karma::components::TransformComponent{{2.0f, 0.0f, 0.0f}});
  const auto child_node = scene.createNode(child);
  scene.reparent(child_node, root_node);

  karma::world::updateWorldTransforms(world, scene);
  const auto& initial_child_transform =
      world.get<karma::components::TransformComponent>(child);
  assert(near(initial_child_transform.getPosition().x, 12.0f));
  assert(near(initial_child_transform.getInterpolatedPosition(0.0f).x, 12.0f));
  assert(near(initial_child_transform.getInterpolatedPosition(0.5f).x, 12.0f));

  world.add(root, karma::components::AnimatorComponent{
                      .clips = {makeMoveClip(4.0f)},
                      .node_entities_by_index = {root, child},
                      .current_clip_index = 0,
                      .time_seconds = 0.0f,
                      .speed = 1.0f,
                      .loop = true,
                      .playing = true});

  karma::world::AnimationSystem animation_system;
  animation_system.update(world, scene, 0.5f);
  karma::world::updateWorldTransforms(world, scene);
  assert(near(world.get<karma::components::TransformComponent>(child).getPosition().x, 12.0f));

  auto& player = world.get<karma::components::AnimatorComponent>(root);
  karma::components::pauseAnimator(player);
  animation_system.update(world, scene, 0.5f);
  karma::world::updateWorldTransforms(world, scene);
  assert(near(world.get<karma::components::TransformComponent>(child).getPosition().x, 12.0f));

  player.clips.push_back(makeMoveClip(8.0f));
  player.time_seconds = 0.75f;
  assert(karma::components::setAnimatorClip(player, 1, true));
  assert(player.current_clip_index == 1 && near(player.time_seconds, 0.0f));
}

void testAnimatorStateMachineAndEvents() {
  karma::world::World world;
  karma::world::Scene scene;

  const auto root = world.createEntity();
  world.add(root, karma::components::TransformComponent{});
  const auto root_node = scene.createNode(root);

  const auto child = world.createEntity();
  world.add(child, karma::components::TransformComponent{});
  const auto child_node = scene.createNode(child);
  scene.reparent(child_node, root_node);

  karma::world::AnimationClip idle = makeMoveClip(0.0f);
  idle.name = "Idle";
  karma::world::AnimationClip run = makeMoveClip(10.0f);
  run.name = "Run";
  run.events.push_back({.name = "Footstep", .time_seconds = 0.25f});

  world.add(root, karma::components::AnimationEventBufferComponent{});

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
  karma::world::AnimationSystem animation_system;
  animation_system.update(world, scene, 0.0f);
  assert(live_animator.current_state_index == 1);
  const auto* trigger = karma::components::findAnimatorParameter(live_animator, "go");
  assert(trigger && !trigger->trigger_value);

  animation_system.update(world, scene, 0.25f);
  assert(live_animator.event_queue.size() == 1);
  assert(live_animator.event_queue.front().name == "Footstep");
  auto& event_buffer = world.get<karma::components::AnimationEventBufferComponent>(root);
  assert(event_buffer.events.size() == 1);
  assert(event_buffer.events.front().name == "Footstep");
  assert(event_buffer.sequence == 1u);
  assert(near(world.get<karma::components::TransformComponent>(child).localPosition().x, 2.5f));

  animation_system.update(world, scene, 0.1f);
  assert(live_animator.event_queue.empty());
  assert(event_buffer.events.empty());
  assert(event_buffer.sequence == 1u);
}

void testAnimatorTransitionInterruption() {
  karma::world::World world;
  karma::world::Scene scene;

  const auto root = world.createEntity();
  world.add(root, karma::components::TransformComponent{});
  scene.createNode(root);

  karma::world::AnimationClip idle = makeMoveClip(0.0f);
  idle.name = "Idle";
  karma::world::AnimationClip walk = makeMoveClip(1.0f);
  walk.name = "Walk";
  karma::world::AnimationClip run = makeMoveClip(2.0f);
  run.name = "Run";

  karma::components::AnimatorComponent animator{};
  animator.clips = {idle, walk, run};
  animator.node_entities_by_index = {root};
  animator.playing = true;
  animator.state_machine.parameters = {
      {.name = "go", .type = karma::components::AnimatorParameterType::Trigger},
      {.name = "run", .type = karma::components::AnimatorParameterType::Trigger},
  };
  animator.state_machine.states.push_back(karma::components::AnimatorState{
      .name = "Idle",
      .clip_index = 0u,
      .transitions = {karma::components::AnimatorTransition{
          .to_state_index = 1u,
          .conditions = {karma::components::AnimatorCondition{
              .parameter = "go",
              .op = karma::components::AnimatorConditionOp::If,
          }},
          .duration_seconds = 1.0f,
          .interrupt_policy = karma::components::AnimatorInterruptPolicy::Destination,
      }},
  });
  animator.state_machine.states.push_back(karma::components::AnimatorState{
      .name = "Walk",
      .clip_index = 1u,
      .transitions = {karma::components::AnimatorTransition{
          .to_state_index = 2u,
          .conditions = {karma::components::AnimatorCondition{
              .parameter = "run",
              .op = karma::components::AnimatorConditionOp::If,
          }},
          .duration_seconds = 0.25f,
      }},
  });
  animator.state_machine.states.push_back(karma::components::AnimatorState{
      .name = "Run",
      .clip_index = 2u,
  });
  world.add(root, animator);

  auto& live_animator = world.get<karma::components::AnimatorComponent>(root);
  karma::world::AnimationSystem animation_system;
  assert(karma::components::setAnimatorTrigger(live_animator, "go"));
  animation_system.update(world, scene, 0.0f);
  assert(live_animator.transition.active);
  assert(live_animator.transition.to_state_index == 1u);

  assert(karma::components::setAnimatorTrigger(live_animator, "run"));
  animation_system.update(world, scene, 0.1f);
  assert(live_animator.transition.active);
  assert(live_animator.transition.from_state_index == 1u);
  assert(live_animator.transition.to_state_index == 2u);
}

void testAnimatorBlendTreeAndRootMotion() {
  karma::world::World world;
  karma::world::Scene scene;

  const auto root = world.createEntity();
  world.add(root, karma::components::TransformComponent{});
  const auto root_node = scene.createNode(root);

  const auto child = world.createEntity();
  world.add(child, karma::components::TransformComponent{});
  const auto child_node = scene.createNode(child);
  scene.reparent(child_node, root_node);

  karma::world::AnimationClip slow = makeMoveClip(2.0f);
  slow.name = "Slow";
  slow.root_motion = karma::world::RootMotionTrack{
      .target_node_index = 0,
      .position_keys = {
          {.time_seconds = 0.0f, .value = {0.0f, 0.0f, 0.0f}},
          {.time_seconds = 1.0f, .value = {4.0f, 0.0f, 0.0f}},
      },
  };
  karma::world::AnimationClip fast = makeMoveClip(10.0f);
  fast.name = "Fast";

  world.add(root, karma::components::RootMotionComponent{
                      .mode = karma::components::RootMotionMode::ExposeDelta,
                      .root_motion_node_index = 0u,
                  });

  karma::components::AnimatorComponent animator{};
  animator.clips = {slow, fast};
  animator.node_entities_by_index = {root, child};
  animator.playing = true;
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

  karma::world::AnimationSystem animation_system;
  animation_system.update(world, scene, 0.5f);

  auto& live_animator = world.get<karma::components::AnimatorComponent>(root);
  assert(near(world.get<karma::components::TransformComponent>(child).localPosition().x, 3.0f));
  assert(live_animator.root_motion_delta.position);
  assert(near(live_animator.root_motion_delta.position->x, 2.0f));
  auto& root_motion = world.get<karma::components::RootMotionComponent>(root);
  assert(root_motion.has_unconsumed_delta);
  assert(root_motion.delta.position);
  assert(near(root_motion.delta.position->x, 2.0f));
  const karma::world::SampledTransform consumed =
      karma::components::consumeRootMotionDelta(root_motion);
  assert(consumed.position);
  assert(near(consumed.position->x, 2.0f));
  assert(!root_motion.has_unconsumed_delta);
  assert(!root_motion.delta.position);
}

void testCpuSkinning() {
  karma::world::MeshData bind_mesh{};
  bind_mesh.vertices.push_back({1.0f, 0.0f, 0.0f});
  bind_mesh.normals.push_back({1.0f, 0.0f, 0.0f});
  bind_mesh.indices.push_back(0);

  std::vector<karma::components::VertexSkinInfluence> influences{
      {.joints = {0u, 0u, 0u, 0u}, .weights = {1.0f, 0.0f, 0.0f, 0.0f}},
  };
  std::vector<glm::mat4> skin_matrices{
      glm::translate(glm::mat4(1.0f), glm::vec3(2.0f, 0.0f, 0.0f)),
  };

  const karma::world::MeshData skinned =
      karma::world::skinMesh(bind_mesh, influences, skin_matrices);
  assert(skinned.vertices.size() == 1);
  assert(near(skinned.vertices[0].x, 3.0f));
  assert(near(skinned.vertices[0].y, 0.0f));
  assert(near(skinned.vertices[0].z, 0.0f));
}

void testMorphTargets() {
  karma::world::MeshData bind_mesh{};
  bind_mesh.vertices.push_back({1.0f, 0.0f, 0.0f});
  bind_mesh.normals.push_back({1.0f, 0.0f, 0.0f});
  bind_mesh.tangents.push_back({1.0f, 0.0f, 0.0f, 1.0f});
  bind_mesh.indices.push_back(0);
  bind_mesh.morph_targets.push_back(karma::world::MeshData::MorphTarget{
      .position_deltas = {{2.0f, 0.0f, 0.0f}},
      .normal_deltas = {{0.0f, 1.0f, 0.0f}},
      .tangent_deltas = {{0.0f, 1.0f, 0.0f}},
  });

  const karma::world::MeshData morphed =
      karma::world::morphMesh(bind_mesh, {0.25f});
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
  const karma::world::MeshData morphed_then_skinned =
      karma::world::skinMesh(morphed, influences, skin_matrices);
  assert(near(morphed_then_skinned.vertices[0].x, 2.5f));
}

void testAnimationSystemUpdatesMorphWeights() {
  karma::world::World world;
  karma::world::Scene scene;

  const auto root = world.createEntity();
  world.add(root, karma::components::TransformComponent{});
  scene.createNode(root);

  karma::world::MeshData bind_mesh{};
  bind_mesh.vertices.push_back({0.0f, 0.0f, 0.0f});
  bind_mesh.morph_targets.push_back(karma::world::MeshData::MorphTarget{
      .position_deltas = {{1.0f, 0.0f, 0.0f}},
  });

  const auto primitive = world.createEntity();
  world.add(primitive, karma::components::DeformableMeshComponent{
                           .bind_mesh = bind_mesh,
                           .cpu_deformed_mesh = bind_mesh,
                           .base_morph_weights = {0.0f},
                           .morph_weights = {0.0f},
                           .morph_weights_dirty = false,
                           .enabled = true});

  karma::world::AnimationClip clip{};
  clip.name = "Morph";
  clip.duration_seconds = 1.0f;
  clip.morph_target_tracks.push_back(karma::world::MorphTargetTrack{
      .target_node_index = 0,
      .interpolation = karma::world::InterpolationMode::Linear,
      .weight_keys = {
          {.time_seconds = 0.0f, .values = {0.0f}},
          {.time_seconds = 1.0f, .values = {1.0f}},
      },
  });
  world.add(root, karma::components::AnimatorComponent{
                      .clips = {clip},
                      .node_entities_by_index = {root},
                      .morph_entities_by_node_index = {{primitive}},
                      .current_clip_index = 0,
                      .time_seconds = 0.0f,
                      .speed = 1.0f,
                      .loop = true,
                      .playing = true});

  karma::world::AnimationSystem animation_system;
  animation_system.update(world, scene, 0.5f);

  auto& morph = world.get<karma::components::DeformableMeshComponent>(primitive);
  assert(morph.morph_weights.size() == 1);
  assert(near(morph.morph_weights[0], 0.5f));
  assert(morph.morph_weights_dirty);

  karma::world::AnimationClip base_clip{};
  base_clip.name = "Base";
  base_clip.duration_seconds = 1.0f;
  auto& player = world.get<karma::components::AnimatorComponent>(root);
  player.clips.push_back(base_clip);
  assert(karma::components::setAnimatorClip(player, 1, true));
  morph.morph_weights_dirty = false;
  animation_system.update(world, scene, 0.0f);
  assert(near(morph.morph_weights[0], 0.0f));
  assert(morph.morph_weights_dirty);
}

void testPoseCompositionAndPalette() {
  karma::world::PoseHierarchy hierarchy{};
  hierarchy.parent_indices = {
      karma::world::kInvalidAnimationIndex,
      0u,
  };
  hierarchy.rest_local_transforms.resize(2);
  hierarchy.rest_local_transforms[0].position = {1.0f, 0.0f, 0.0f};
  hierarchy.rest_local_transforms[1].position = {2.0f, 0.0f, 0.0f};

  karma::world::LocalPose pose = karma::world::makeRestLocalPose(hierarchy);
  karma::world::SampledTransform child_sample{};
  child_sample.position = karma::math::Vec3{3.0f, 0.0f, 0.0f};
  karma::world::applySampleToLocalPose(pose, 1u, child_sample);

  const karma::world::ModelPose model_pose =
      karma::world::composeModelPose(hierarchy, pose);
  assert(model_pose.node_matrices.size() == 2);
  assert(near(model_pose.node_matrices[1][3].x, 4.0f));

  const std::vector<uint32_t> joint_node_indices{1u};
  const std::vector<glm::mat4> inverse_binds{
      glm::translate(glm::mat4(1.0f), glm::vec3(-3.0f, 0.0f, 0.0f)),
  };
  const karma::world::SkinningPalette palette =
      karma::world::buildSkinningPalette(joint_node_indices,
                                             inverse_binds,
                                             model_pose.node_matrices,
                                             glm::mat4(1.0f));
  assert(palette.valid);
  assert(palette.joint_matrices.size() == 1);
  assert(near(palette.joint_matrices[0][3].x, 1.0f));
}

void testSkeletonRetargeting() {
  karma::world::Skeleton source{};
  source.joints = {
      {.name = "Root", .node_index = 0u},
      {.name = "Hand", .parent_joint_index = 0u, .node_index = 1u},
  };
  source.root_joint_indices = {0u};

  karma::world::Skeleton target{};
  target.joints = {
      {.name = "TargetRoot", .node_index = 10u},
      {.name = "TargetHand", .parent_joint_index = 0u, .node_index = 11u},
  };
  target.root_joint_indices = {0u};

  karma::world::SkeletonMap map{};
  map.source_root_joint_index = 0u;
  map.target_root_joint_index = 0u;
  map.joints = {
      {.source_joint_index = 0u, .target_joint_index = 0u},
      {.source_joint_index = 1u, .target_joint_index = 1u},
  };
  std::string diagnostic;
  assert(karma::world::validateSkeletonMap(source, target, map, &diagnostic));
  assert(diagnostic.empty());

  karma::world::AnimationClip clip{};
  clip.name = "Reach";
  clip.duration_seconds = 1.0f;
  clip.channels.push_back(karma::world::AnimationChannel{
      .target_node_index = 0u,
      .position_keys = {
          {.time_seconds = 0.0f, .value = {1.0f, 0.0f, 0.0f}},
          {.time_seconds = 1.0f, .value = {2.0f, 0.0f, 0.0f}},
      },
  });
  clip.channels.push_back(karma::world::AnimationChannel{
      .target_node_index = 1u,
      .rotation_keys = {
          {.time_seconds = 0.0f, .value = {0.0f, 0.0f, 0.0f, 1.0f}},
      },
  });
  clip.root_motion = karma::world::RootMotionTrack{
      .target_node_index = 0u,
      .position_keys = {
          {.time_seconds = 0.0f, .value = {0.0f, 0.0f, 0.0f}},
          {.time_seconds = 1.0f, .value = {1.0f, 0.0f, 0.0f}},
      },
  };

  const karma::world::AnimationClip retargeted = karma::world::retargetClip(
      clip,
      source,
      target,
      map,
      karma::world::RetargetOptions{
          .root_scale_policy = karma::world::RetargetRootScalePolicy::ExplicitScale,
          .root_translation_scale = 2.0f,
      });

  assert(retargeted.channels.size() == 2u);
  assert(retargeted.channels[0].target_node_index == 10u);
  assert(near(retargeted.channels[0].position_keys[0].value.x, 2.0f));
  assert(near(retargeted.channels[0].position_keys[1].value.x, 4.0f));
  assert(retargeted.channels[1].target_node_index == 11u);
  assert(retargeted.root_motion);
  assert(retargeted.root_motion->target_node_index == 10u);
  assert(near(retargeted.root_motion->position_keys[1].value.x, 2.0f));

  map.joints.back().target_joint_index = 99u;
  assert(!karma::world::validateSkeletonMap(source, target, map, &diagnostic));
  assert(!diagnostic.empty());
}

void testSkinnedGlbImport() {
  const std::filesystem::path path =
      std::filesystem::temp_directory_path() / "karma_test_skinned_animation.glb";
  assert(writeSkinnedGlb(path));

  const karma::world::GltfScenePrefab prefab = karma::world::loadGltfScenePrefab(path);
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

  const karma::world::GltfScenePrefab prefab = karma::world::loadGltfScenePrefab(path);
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
  karma::world::GltfDocument doc{};
  for (int i = 0; i < 3; ++i) {
    appendFloat(doc.bin, static_cast<float>(i));
    appendFloat(doc.bin, 0.0f);
    appendFloat(doc.bin, 0.0f);
  }
  doc.json = karma::world::Json{
      {"asset", {{"version", "2.0"}}},
      {"nodes",
       karma::world::Json::array({
           {{"name", "StaticNode"}, {"mesh", 0u}},
           {{"name", "SkinnedNode"}, {"mesh", 1u}, {"skin", 0u}},
       })},
      {"meshes",
       karma::world::Json::array({
           {{"primitives",
             karma::world::Json::array({
                 {{"attributes", {{"POSITION", 0u}}}, {"material", 1u}},
             })}},
           {{"primitives",
             karma::world::Json::array({
                 {{"attributes", {{"POSITION", 0u}}}, {"material", 2u}},
             })}},
       })},
      {"skins", karma::world::Json::array({{{"joints", karma::world::Json::array()}}})},
      {"bufferViews",
       karma::world::Json::array({
           {{"buffer", 0u}, {"byteOffset", 0u}, {"byteLength", doc.bin.size()}},
       })},
      {"accessors",
       karma::world::Json::array({
           {{"bufferView", 0u}, {"componentType", 5126}, {"count", 3u}, {"type", "VEC3"}},
       })},
  };

  karma::world::GltfScenePrefab prefab{};
  prefab.nodes.resize(2);
  prefab.nodes[0].name = "StaticNode";
  prefab.nodes[0].primitives.push_back(karma::world::GltfScenePrefabPrimitive{});
  prefab.nodes[0].primitives.front().source_material_index = 10;
  prefab.nodes[0].primitives.front().mesh.vertices.push_back({42.0f, 0.0f, 0.0f});
  prefab.nodes[1].name = "SkinnedNode";
  prefab.nodes[1].primitives.push_back(karma::world::GltfScenePrefabPrimitive{});
  prefab.nodes[1].primitives.front().source_material_index = 11;
  prefab.nodes[1].primitives.front().mesh.vertices.push_back({42.0f, 0.0f, 0.0f});

  const std::unordered_map<std::string, uint32_t> node_indices_by_name{
      {"StaticNode", 0u},
      {"SkinnedNode", 1u},
  };
  karma::world::populateGltfMeshData(doc, node_indices_by_name, prefab);

  assert(prefab.nodes[0].primitives.front().mesh.vertices.size() == 3);
  assert(prefab.nodes[0].primitives.front().source_material_index == 10u);
  assert(prefab.nodes[0].primitives.front().source_gltf_material_index == 1u);
  assert(prefab.nodes[1].primitives.front().mesh.vertices.size() == 3);
  assert(prefab.nodes[1].primitives.front().source_material_index == 11u);
  assert(prefab.nodes[1].primitives.front().source_gltf_material_index == 2u);
}

void testGltfMorphTargetImport() {
  karma::world::GltfDocument doc{};
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

  doc.json = karma::world::Json{
      {"asset", {{"version", "2.0"}}},
      {"nodes", karma::world::Json::array({
                    {{"name", "MorphNode"}, {"mesh", 0u}},
                })},
      {"meshes",
       karma::world::Json::array({
           {{"weights", karma::world::Json::array({0.25f})},
            {"primitives",
             karma::world::Json::array({
                 {{"attributes", {{"POSITION", 0u}}},
                  {"targets", karma::world::Json::array({
                                  {{"POSITION", 1u}},
                              })}},
             })}},
       })},
      {"animations",
       karma::world::Json::array({
           {{"samplers",
             karma::world::Json::array({
                 {{"input", 2u}, {"output", 3u}},
             })},
            {"channels",
             karma::world::Json::array({
                 {{"sampler", 0u}, {"target", {{"node", 0u}, {"path", "weights"}}}},
             })}},
       })},
      {"bufferViews",
       karma::world::Json::array({
           {{"buffer", 0u}, {"byteOffset", position_offset}, {"byteLength", 36u}},
           {{"buffer", 0u}, {"byteOffset", morph_offset}, {"byteLength", 36u}},
           {{"buffer", 0u}, {"byteOffset", time_offset}, {"byteLength", 8u}},
           {{"buffer", 0u}, {"byteOffset", weight_offset}, {"byteLength", 8u}},
       })},
      {"accessors",
       karma::world::Json::array({
           {{"bufferView", 0u}, {"componentType", 5126}, {"count", 3u}, {"type", "VEC3"}},
           {{"bufferView", 1u}, {"componentType", 5126}, {"count", 3u}, {"type", "VEC3"}},
           {{"bufferView", 2u}, {"componentType", 5126}, {"count", 2u}, {"type", "SCALAR"}},
           {{"bufferView", 3u}, {"componentType", 5126}, {"count", 2u}, {"type", "SCALAR"}},
       })},
  };

  karma::world::GltfScenePrefab prefab{};
  prefab.nodes.resize(1);
  prefab.nodes[0].name = "MorphNode";
  prefab.nodes[0].primitives.push_back(karma::world::GltfScenePrefabPrimitive{});

  const std::unordered_map<std::string, uint32_t> node_indices_by_name{{"MorphNode", 0u}};
  karma::world::populateGltfMeshData(doc, node_indices_by_name, prefab);
  const auto& primitive = prefab.nodes[0].primitives.front();
  assert(primitive.morphable());
  assert(primitive.mesh.morph_targets.size() == 1);
  assert(primitive.mesh.morph_targets.front().position_deltas.size() == 3);
  assert(near(primitive.mesh.morph_targets.front().position_deltas.front().z, 1.0f));
  assert(primitive.morph_weights.size() == 1);
  assert(near(primitive.morph_weights[0], 0.25f));

  const std::vector<karma::world::AnimationClip> clips =
      karma::world::loadGltfAnimationClips(doc, node_indices_by_name, prefab);
  assert(clips.size() == 1);
  assert(clips.front().morph_target_tracks.size() == 1);
  assert(clips.front().morph_target_tracks.front().target_node_index == 0u);
  assert(clips.front().morph_target_tracks.front().target_mesh_index == 0u);

  bool sampled_morph = false;
  karma::world::sampleAnimationClip(
      clips.front(),
      0.5f,
      true,
      [&](uint32_t, const karma::world::SampledTransform&) {},
      [&](uint32_t target_node_index, const std::vector<float>& weights) {
        sampled_morph = true;
        assert(target_node_index == 0u);
        assert(weights.size() == 1);
        assert(near(weights[0], 0.5f));
      });
  assert(sampled_morph);
}

void testGltfDocumentExternalAndDataBuffers() {
  std::vector<std::uint8_t> bin;
  appendFloat(bin, 0.0f); appendFloat(bin, 0.0f); appendFloat(bin, 0.0f);
  appendFloat(bin, 1.0f); appendFloat(bin, 2.0f); appendFloat(bin, 3.0f);

  const std::filesystem::path dir =
      std::filesystem::temp_directory_path() / "karma_gltf_buffer_modes";
  std::filesystem::create_directories(dir);
  const std::filesystem::path external_bin = dir / "positions.bin";
  {
    std::ofstream out(external_bin, std::ios::binary);
    assert(out);
    out.write(reinterpret_cast<const char*>(bin.data()),
              static_cast<std::streamsize>(bin.size()));
    assert(out.good());
  }

  auto make_json = [&](const std::string& uri) {
    return karma::world::Json{
        {"asset", {{"version", "2.0"}}},
        {"buffers", karma::world::Json::array({
                        {{"uri", uri}, {"byteLength", bin.size()}},
                    })},
        {"bufferViews", karma::world::Json::array({
                            {{"buffer", 0u}, {"byteOffset", 0u}, {"byteLength", bin.size()}},
                        })},
        {"accessors", karma::world::Json::array({
                          {{"bufferView", 0u},
                           {"componentType", 5126},
                           {"count", 2u},
                           {"type", "VEC3"}},
                      })},
    };
  };

  const std::filesystem::path external_gltf = dir / "external.gltf";
  {
    std::ofstream out(external_gltf);
    assert(out);
    out << make_json("positions.bin").dump();
  }
  karma::world::GltfDocument external_doc = karma::world::loadGltfDocument(external_gltf);
  std::vector<float> values;
  size_t count = 0;
  assert(karma::world::readFloatAccessor(external_doc, 0u, 3u, values, &count));
  assert(count == 2u);
  assert(near(values[3], 1.0f));
  assert(near(values[4], 2.0f));
  assert(near(values[5], 3.0f));

  const std::filesystem::path data_uri_gltf = dir / "data_uri.gltf";
  {
    std::ofstream out(data_uri_gltf);
    assert(out);
    out << make_json(dataUriForBytes(bin)).dump();
  }
  karma::world::GltfDocument data_uri_doc = karma::world::loadGltfDocument(data_uri_gltf);
  values.clear();
  count = 0;
  assert(karma::world::readFloatAccessor(data_uri_doc, 0u, 3u, values, &count));
  assert(count == 2u);
  assert(near(values[3], 1.0f));
  assert(near(values[4], 2.0f));
  assert(near(values[5], 3.0f));
}

void testGltfSparseAccessors() {
  karma::world::GltfDocument doc{};

  const std::uint32_t position_sparse_indices_offset =
      static_cast<std::uint32_t>(doc.bin.size());
  appendU16(doc.bin, 1u);
  appendU16(doc.bin, 2u);

  const std::uint32_t position_sparse_values_offset =
      static_cast<std::uint32_t>(doc.bin.size());
  appendFloat(doc.bin, 1.0f); appendFloat(doc.bin, 2.0f); appendFloat(doc.bin, 3.0f);
  appendFloat(doc.bin, 4.0f); appendFloat(doc.bin, 5.0f); appendFloat(doc.bin, 6.0f);

  const std::uint32_t index_sparse_indices_offset =
      static_cast<std::uint32_t>(doc.bin.size());
  appendU16(doc.bin, 2u);

  const std::uint32_t index_sparse_values_offset =
      static_cast<std::uint32_t>(doc.bin.size());
  appendU16(doc.bin, 7u);

  doc.json = karma::world::Json{
      {"asset", {{"version", "2.0"}}},
      {"bufferViews",
       karma::world::Json::array({
           {{"buffer", 0u}, {"byteOffset", position_sparse_indices_offset}, {"byteLength", 4u}},
           {{"buffer", 0u}, {"byteOffset", position_sparse_values_offset}, {"byteLength", 24u}},
           {{"buffer", 0u}, {"byteOffset", index_sparse_indices_offset}, {"byteLength", 2u}},
           {{"buffer", 0u}, {"byteOffset", index_sparse_values_offset}, {"byteLength", 2u}},
       })},
      {"accessors",
       karma::world::Json::array({
           {{"componentType", 5126},
            {"count", 3u},
            {"type", "VEC3"},
            {"sparse",
             {{"count", 2u},
              {"indices", {{"bufferView", 0u}, {"componentType", 5123}}},
              {"values", {{"bufferView", 1u}}}}}},
           {{"componentType", 5123},
            {"count", 3u},
            {"type", "SCALAR"},
            {"sparse",
             {{"count", 1u},
              {"indices", {{"bufferView", 2u}, {"componentType", 5123}}},
              {"values", {{"bufferView", 3u}}}}}},
       })},
  };

  std::vector<float> positions;
  size_t position_count = 0;
  assert(karma::world::readFloatAccessor(doc, 0u, 3u, positions, &position_count));
  assert(position_count == 3u);
  assert(near(positions[0], 0.0f));
  assert(near(positions[3], 1.0f));
  assert(near(positions[4], 2.0f));
  assert(near(positions[5], 3.0f));
  assert(near(positions[6], 4.0f));
  assert(near(positions[7], 5.0f));
  assert(near(positions[8], 6.0f));

  std::vector<uint32_t> indices;
  assert(karma::world::readIndexAccessor(doc, 1u, indices));
  assert(indices.size() == 3u);
  assert(indices[0] == 0u);
  assert(indices[1] == 0u);
  assert(indices[2] == 7u);
}

void testWalkingGlbImportSmoke() {
  const std::filesystem::path path =
      findRepoRoot() / "examples/assets/animation_model/source/walking.glb";
  assert(std::filesystem::exists(path));

  const karma::world::GltfScenePrefab prefab = karma::world::loadGltfScenePrefab(path);
  const karma::world::GltfDocument gltf = karma::world::loadGltfDocument(path);
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
  const karma::world::MeshData& suit_mesh =
      prefab.nodes[suit_node_it->second].primitives.front().mesh;
  assert(!suit_mesh.uvs.empty());
  assert(near(suit_mesh.uvs.front().x, 0.5138f, 0.0001f));
  assert(near(suit_mesh.uvs.front().y, 0.4959f, 0.0001f));
  assert(suit_mesh.tangents.size() == suit_mesh.vertices.size());
  auto gltf_node_by_name = [&](const char* name) -> uint32_t {
    for (uint32_t node_index = 0; node_index < gltf.json["nodes"].size(); ++node_index) {
      if (karma::world::gltfNodeName(gltf, node_index) == name) {
        return node_index;
      }
    }
    return karma::world::kInvalidGltfSceneNode;
  };
  auto assert_imported_world_matches_gltf = [&](const char* name) {
    const uint32_t gltf_node = gltf_node_by_name(name);
    const auto prefab_it = prefab_nodes_by_name.find(name);
    assert(gltf_node != karma::world::kInvalidGltfSceneNode);
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
  const karma::world::SkinningPalette rest_palette =
      karma::world::buildSkinningPalette(prefab.skins.front().joint_node_indices,
                                             prefab.skins.front().inverse_bind_matrices,
                                             prefab_world_matrices,
                                             glm::mat4(1.0f),
                                             0u);
  assert(rest_palette.valid);
  for (const glm::mat4& joint_matrix : rest_palette.joint_matrices) {
    assert(maxMatrixDiff(joint_matrix, glm::mat4(1.0f)) < 0.001f);
  }

  const karma::world::PoseHierarchy hierarchy = buildPrefabPoseHierarchy(prefab);
  karma::world::LocalPose sampled_pose =
      karma::world::makeRestLocalPose(hierarchy);
  karma::world::sampleAnimationClip(
      prefab.animations.front(),
      0.5f,
      true,
      [&](uint32_t target_node_index, const karma::world::SampledTransform& sampled) {
        karma::world::applySampleToLocalPose(sampled_pose, target_node_index, sampled);
      });
  const karma::world::ModelPose sampled_model_pose =
      karma::world::composeModelPose(hierarchy, sampled_pose);
  const karma::world::SkinningPalette sampled_palette =
      karma::world::buildSkinningPalette(prefab.skins.front().joint_node_indices,
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
      const karma::world::MeshData skinned_mesh =
          karma::world::skinMesh(primitive.mesh,
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

  karma::world::World runtime_world;
  karma::world::Scene runtime_scene;
  std::vector<karma::world::Entity> runtime_nodes(prefab.nodes.size());
  std::function<karma::world::NodeId(uint32_t, karma::world::NodeId)> instantiate_node =
      [&](uint32_t prefab_node_index, karma::world::NodeId parent_node) {
        const auto& prefab_node = prefab.nodes[prefab_node_index];
        const karma::world::Entity entity = runtime_world.createEntity();
        runtime_world.setName(entity, prefab_node.name);
        runtime_world.add(entity,
                          karma::components::TransformComponent{
                              prefab_node.local_position,
                              prefab_node.local_rotation,
                              prefab_node.local_scale});
        const karma::world::NodeId node_id = runtime_scene.createNode(entity);
        if (parent_node != karma::world::Node::kInvalidId) {
          runtime_scene.reparent(node_id, parent_node);
        }
        runtime_nodes[prefab_node_index] = entity;
        for (const uint32_t child : prefab_node.children) {
          instantiate_node(child, node_id);
        }
        return node_id;
      };
  const karma::world::NodeId runtime_root_node =
      instantiate_node(prefab.root_node, karma::world::Node::kInvalidId);
  const karma::world::Entity runtime_root = runtime_scene.get(runtime_root_node).entity;
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
    const karma::world::GltfScenePrefabPrimitive* primitive = nullptr;
    karma::components::DeformableMeshComponent deformation;
  };
  std::vector<RuntimePrimitive> runtime_primitives;
  for (const auto& node : prefab.nodes) {
    for (const auto& primitive : node.primitives) {
      if (!primitive.skinned()) {
        continue;
      }
      std::vector<karma::world::Entity> joint_entities;
      joint_entities.reserve(primitive.joint_node_indices.size());
      for (const uint32_t joint_node_index : primitive.joint_node_indices) {
        joint_entities.push_back(joint_node_index < runtime_nodes.size()
                                     ? runtime_nodes[joint_node_index]
                                     : karma::world::Entity{});
      }
      runtime_primitives.push_back(RuntimePrimitive{
          .primitive = &primitive,
          .deformation = karma::components::DeformableMeshComponent{
              .bind_mesh = primitive.mesh,
              .cpu_deformed_mesh = primitive.mesh,
              .vertex_influences = primitive.vertex_influences,
              .joint_entities = std::move(joint_entities),
              .inverse_bind_matrices = primitive.inverse_bind_matrices,
              .render_transform_entity = runtime_root,
              .skin_index = primitive.skin_index,
              .path = karma::components::DeformationPath::CpuReference,
              .override_render_transform = true,
              .enabled = true,
          }});
    }
  }

  karma::world::AnimationSystem animation_system;
  animation_system.update(runtime_world, runtime_scene, 0.5f);
  karma::world::updateWorldTransforms(runtime_world, runtime_scene);
  const glm::mat4 runtime_render_world =
      toMatrix(runtime_world.get<karma::components::TransformComponent>(runtime_root));
  Bounds runtime_bounds{};
  for (const RuntimePrimitive& runtime_primitive : runtime_primitives) {
    const karma::world::SkinningPalette palette =
        karma::world::buildSkinningPaletteFromScene(runtime_primitive.deformation,
                                                        runtime_world,
                                                        runtime_scene,
                                                        glm::mat4(1.0f));
    assert(palette.valid);
    const karma::world::MeshData skinned_mesh =
        karma::world::skinMesh(runtime_primitive.primitive->mesh,
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
                       [&](const karma::world::Joint& joint) {
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
  testAnimatorTransitionInterruption();
  testAnimatorBlendTreeAndRootMotion();
  testCpuSkinning();
  testMorphTargets();
  testAnimationSystemUpdatesMorphWeights();
  testPoseCompositionAndPalette();
  testSkeletonRetargeting();
  testSkinnedGlbImport();
  testSplitWeightGlbImport();
  testGltfMeshReplacementImportsAllMeshNodes();
  testGltfMorphTargetImport();
  testGltfDocumentExternalAndDataBuffers();
  testGltfSparseAccessors();
  testWalkingGlbImportSmoke();
  return 0;
}
