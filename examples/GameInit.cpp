#include "common/demo_asset_paths.h"
#include "karma/world/ecs/world.h"
#include "karma/world/scene/scene.h"

#include "karma/world/components/audio_source.h"
#include "karma/world/components/camera.h"
#include "karma/world/components/collider.h"
#include "karma/world/components/environment.h"
#include "karma/world/components/layers.h"
#include "karma/world/components/mesh.h"
#include "karma/world/components/rigidbody.h"
#include "karma/world/components/tag.h"
#include "karma/world/components/transform.h"
#include "karma/world/components/visibility.h"

namespace karma::demo {

struct GameInitResult {
  ecs::World world;
  scene::Scene scene;
  ecs::Entity player;
  ecs::Entity camera;
};

GameInitResult BuildDemoScene(content::AssetRegistry& assets) {
  GameInitResult result{};

  // Player entity
  result.player = result.world.createEntity();
  result.world.add(result.player, components::TagComponent{"player"});
  result.world.add(result.player, components::TransformComponent{});
  result.world.add(result.player, components::MeshComponent{
      .mesh_asset_key = importExampleMeshAsset(&assets, "tank_final.glb"),
      .visible = true});
  result.world.add(result.player,
                   components::ColliderComponent::capsule(
                       components::CapsuleColliderShape{
                           .center = {},
                           .radius = 0.4f,
                           .height = 1.6f}));
  result.world.add(result.player, components::RigidbodyComponent{});
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
      .environment_map_asset_key =
          registerExampleEnvironmentMap(&assets, "golden_gate_hills_4k.hdr"),
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
