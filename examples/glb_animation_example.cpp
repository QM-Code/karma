#include "karma/karma.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <spdlog/spdlog.h>

namespace karma::demo {

namespace {

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

bool writeAnimatedGlb(const std::filesystem::path& path) {
  std::vector<std::uint8_t> bin;

  const std::uint32_t position_offset = static_cast<std::uint32_t>(bin.size());
  appendFloat(bin, -0.35f);
  appendFloat(bin, 0.0f);
  appendFloat(bin, 0.0f);
  appendFloat(bin, 0.35f);
  appendFloat(bin, 0.0f);
  appendFloat(bin, 0.0f);
  appendFloat(bin, 0.0f);
  appendFloat(bin, 0.65f);
  appendFloat(bin, 0.0f);

  const std::uint32_t index_offset = static_cast<std::uint32_t>(bin.size());
  appendU16(bin, 0);
  appendU16(bin, 1);
  appendU16(bin, 2);
  align4(bin);

  const std::uint32_t time_offset = static_cast<std::uint32_t>(bin.size());
  appendFloat(bin, 0.0f);
  appendFloat(bin, 1.0f);

  const std::uint32_t translation_offset = static_cast<std::uint32_t>(bin.size());
  appendFloat(bin, -1.0f);
  appendFloat(bin, 0.0f);
  appendFloat(bin, 0.0f);
  appendFloat(bin, 1.0f);
  appendFloat(bin, 0.0f);
  appendFloat(bin, 0.0f);
  align4(bin);

  const std::string json =
      "{\"asset\":{\"version\":\"2.0\",\"generator\":\"Karma\"},"
      "\"scene\":0,"
      "\"scenes\":[{\"nodes\":[0]}],"
      "\"nodes\":[{\"name\":\"Root\",\"children\":[1]},"
      "{\"name\":\"AnimatedNode\",\"mesh\":0,\"translation\":[-1,0,0]}],"
      "\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":0},\"indices\":1,\"mode\":4}]}],"
      "\"buffers\":[{\"byteLength\":" + std::to_string(bin.size()) + "}],"
      "\"bufferViews\":["
      "{\"buffer\":0,\"byteOffset\":" + std::to_string(position_offset) + ",\"byteLength\":36,\"target\":34962},"
      "{\"buffer\":0,\"byteOffset\":" + std::to_string(index_offset) + ",\"byteLength\":6,\"target\":34963},"
      "{\"buffer\":0,\"byteOffset\":" + std::to_string(time_offset) + ",\"byteLength\":8},"
      "{\"buffer\":0,\"byteOffset\":" + std::to_string(translation_offset) + ",\"byteLength\":24}],"
      "\"accessors\":["
      "{\"bufferView\":0,\"componentType\":5126,\"count\":3,\"type\":\"VEC3\","
      "\"min\":[-0.35,0,0],\"max\":[0.35,0.65,0]},"
      "{\"bufferView\":1,\"componentType\":5123,\"count\":3,\"type\":\"SCALAR\"},"
      "{\"bufferView\":2,\"componentType\":5126,\"count\":2,\"type\":\"SCALAR\",\"min\":[0],\"max\":[1]},"
      "{\"bufferView\":3,\"componentType\":5126,\"count\":2,\"type\":\"VEC3\"}],"
      "\"animations\":[{\"name\":\"Slide\","
      "\"samplers\":[{\"input\":2,\"output\":3,\"interpolation\":\"LINEAR\"}],"
      "\"channels\":[{\"sampler\":0,\"target\":{\"node\":1,\"path\":\"translation\"}}]}]}";

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

}  // namespace

class GlbAnimationExample final : public app::GameInterface {
 public:
  void onStart() override {
    const std::filesystem::path glb_path =
        std::filesystem::temp_directory_path() / "karma_animated_node.glb";
    if (!writeAnimatedGlb(glb_path)) {
      spdlog::error("Failed to write animated GLB to {}", glb_path.string());
      return;
    }

    const scene::GlbSceneImportResult imported = scene::importGlbScene(
        *world,
        *scene,
        *graphics,
        glb_path,
        scene::GlbSceneImportOptions{
            .load = {.import_meshes = true, .import_lights = false},
            .instantiate = {.create_synthetic_root = false, .autoplay_animations = true},
        });
    if (!imported.valid()) {
      spdlog::error("Failed to import animated GLB from {}", glb_path.string());
    }

    const auto camera = world->createEntity();
    world->setName(camera, "Camera");
    components::TransformComponent camera_transform{{0.0f, 0.7f, 3.0f}};
    camera_transform.setRotation(math::fromYawPitch(0.0f, -0.15f));
    world->add(camera, camera_transform);
    world->add(camera, components::CameraComponent{
                          .near_clip = 0.05f,
                          .far_clip = 50.0f,
                          .is_primary = true});

    const auto light = world->createEntity();
    world->setName(light, "Sun");
    components::TransformComponent light_transform{};
    light_transform.setRotation(math::fromYawPitch(-0.45f, -0.7f));
    world->add(light, light_transform);
    world->add(light, components::LightComponent{
                          .type = components::LightComponent::Type::Directional,
                          .color = {1.0f, 0.96f, 0.9f, 1.0f},
                          .intensity = 1.0f});
  }

  void onFixedUpdate(float dt) override {
    (void)dt;
  }

  void onUpdate(float dt) override {
    (void)dt;
  }

  void onShutdown() override {}
};

}  // namespace karma::demo

int main() {
  karma::app::EngineApp engine;
  karma::demo::GlbAnimationExample game;

  karma::app::EngineConfig config;
  config.window.title = "Karma GLB Animation Example";
  config.window.samples = 1;
  config.cursor_visible = true;

  engine.start(game, config);
  while (engine.isRunning()) {
    engine.tick();
  }

  return 0;
}
