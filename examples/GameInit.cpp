#include "karma/ecs/world.h"
#include "karma/scene/scene.h"

#include "karma/components/audio_source.h"
#include "karma/components/camera.h"
#include "karma/components/collider.h"
#include "karma/components/environment.h"
#include "karma/components/layers.h"
#include "karma/components/mesh.h"
#include "karma/components/rigidbody.h"
#include "karma/components/tag.h"
#include "karma/components/transform.h"
#include "karma/components/visibility.h"

namespace karma::demo {

struct GameInitResult {
  ecs::World world;
  scene::Scene scene;
  ecs::Entity player;
  ecs::Entity camera;
};

GameInitResult BuildDemoScene() {
  GameInitResult result{};

  // Player entity
  result.player = result.world.createEntity();
  result.world.add(result.player, components::TagComponent{"player"});
  result.world.add(result.player, components::TransformComponent{});
  result.world.add(result.player, components::MeshComponent{
      .mesh_key = "player.glb",
      .material_key = "player.mat",
      .texture_key = "player_albedo.png",
      .visible = true});
  result.world.add(result.player, components::RigidbodyComponent{});
  result.world.add(result.player, components::ColliderComponent{
      .shape = components::ColliderComponent::Shape::Capsule,
      .radius = 0.4f,
      .height = 1.6f});
  result.world.add(result.player, components::VisibilityComponent{
      .visible = true,
      .render_layer_mask = components::layerBit(components::RenderLayer::World),
      .collision_layer_mask = components::layerBit(components::CollisionLayer::Dynamic)});

  // Camera entity
  result.camera = result.world.createEntity();
  result.world.add(result.camera, components::TagComponent{"main_camera"});
  result.world.add(result.camera, components::TransformComponent({0.0f, 2.0f, 6.0f}));
  result.world.add(result.camera, components::CameraComponent{
      .is_primary = true});

  // Example environment entity (optional)
  auto sky = result.world.createEntity();
  result.world.add(sky, components::EnvironmentComponent{
      .environment_map = "/home/quinn/Documents/karma/examples/assets/demo_env.png",
      .intensity = 0.6f,
      .draw_skybox = true});

  // Scene nodes (hierarchy)
  auto player_node = result.scene.createNode(result.player);
  auto camera_node = result.scene.createNode(result.camera);
  result.scene.reparent(camera_node, player_node);

  // Ambient audio source near player
  auto audio = result.world.createEntity();
  result.world.add(audio, components::TransformComponent({0.0f, 0.0f, 0.0f}));
  result.world.add(audio, components::AudioSourceComponent{
      .clip_key = "wind.ogg",
      .looping = true,
      .play_on_start = true});

  return result;
}

}  // namespace karma::demo
