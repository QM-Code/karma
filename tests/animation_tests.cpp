#include "karma/animation/animation_clip.h"
#include "karma/animation/animation_system.h"
#include "karma/animation/cpu_skinning_system.h"
#include "karma/components/animation_player.h"
#include "karma/components/skinned_mesh.h"
#include "karma/components/transform.h"
#include "karma/ecs/world.h"
#include "karma/scene/glb_scene_import.h"
#include "karma/scene/scene.h"
#include "karma/scene/transform_hierarchy.h"

#include <cassert>
#include <cstdint>
#include <cmath>
#include <filesystem>
#include <fstream>

#include <glm/gtc/matrix_transform.hpp>

namespace {

bool near(float a, float b, float epsilon = 0.0001f) {
  return std::abs(a - b) <= epsilon;
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

void testCpuSkinning() {
  karma::renderer::MeshData bind_mesh{};
  bind_mesh.vertices.push_back({1.0f, 0.0f, 0.0f});
  bind_mesh.normals.push_back({1.0f, 0.0f, 0.0f});
  bind_mesh.indices.push_back(0);

  std::vector<karma::components::VertexSkinInfluence> influences{
      {.joints = {0u, 0u, 0u, 0u}, .weights = {1.0f, 0.0f, 0.0f, 0.0f}},
  };
  std::vector<glm::mat4> skin_matrices{
      glm::translate(glm::mat4(1.0f), glm::vec3(2.0f, 0.0f, 0.0f)),
  };

  const karma::renderer::MeshData skinned =
      karma::animation::skinMesh(bind_mesh, influences, skin_matrices);
  assert(skinned.vertices.size() == 1);
  assert(near(skinned.vertices[0].x, 3.0f));
  assert(near(skinned.vertices[0].y, 0.0f));
  assert(near(skinned.vertices[0].z, 0.0f));
}

void testSkinnedGlbImport() {
  const std::filesystem::path path =
      std::filesystem::temp_directory_path() / "karma_test_skinned_animation.glb";
  assert(writeSkinnedGlb(path));

  const karma::scene::GlbScenePrefab prefab = karma::scene::loadGlbScenePrefab(path);
  assert(prefab.valid());
  assert(!prefab.animations.empty());

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

}  // namespace

int main() {
  testClipSampling();
  testHierarchyAndPlayback();
  testCpuSkinning();
  testSkinnedGlbImport();
  return 0;
}
