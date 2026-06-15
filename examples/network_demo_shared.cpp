#include "network_demo_shared.h"

#include <spdlog/spdlog.h>

#include "karma/world/components/network.h"
#include "karma/world/components/tag.h"
#include "karma/world/components/transform.h"

namespace karma::examples::network_demo {

std::span<const std::byte> asBytes(const std::string& text) {
  return {reinterpret_cast<const std::byte*>(text.data()), text.size()};
}

std::string payloadText(std::span<const std::byte> payload) {
  if (payload.empty()) {
    return {};
  }
  return {reinterpret_cast<const char*>(payload.data()), payload.size()};
}

std::vector<std::byte> encodeInput(PlayerInput input) {
  net::BinaryWriter writer;
  writer.writeFloat32(input.dx);
  writer.writeFloat32(input.dz);
  return writer.takeBytes();
}

bool decodeInput(std::span<const std::byte> bytes, PlayerInput& input) {
  net::BinaryReader reader(bytes);
  return reader.readFloat32(input.dx) && reader.readFloat32(input.dz);
}

ecs::Entity spawnReplicatedPlayer(ecs::World& world,
                                  net::PeerId peer,
                                  const std::string& name,
                                  float offset) {
  auto entity = world.createEntity();
  world.add(entity,
            components::TransformComponent{
                math::Vec3{offset, 0.0f, 0.0f}});
  world.add(entity, components::TagComponent{.name = name});
  world.add(entity, components::NetworkIdentityComponent{});
  world.add(entity,
            components::NetworkAuthorityComponent{
                .mode = components::AuthorityMode::Server,
                .owner_peer = peer.value,
                .server_can_override = true,
            });
  world.add(entity,
            components::NetworkReplicatedComponent{
                .components = {
                    {network::kTransformComponentWireId,
                     components::ReplicationPolicy::Delta},
                    {network::kTagComponentWireId,
                     components::ReplicationPolicy::Snapshot},
                },
            });
  return entity;
}

void logReplicatedWorld(const ecs::World& world) {
  for (const ecs::Entity entity : world.entities()) {
    if (!world.has<components::NetworkIdentityComponent>(entity) ||
        !world.has<components::TransformComponent>(entity)) {
      continue;
    }
    const auto& identity = world.get<components::NetworkIdentityComponent>(entity);
    const auto& transform = world.get<components::TransformComponent>(entity);
    std::string name = "entity";
    if (world.has<components::TagComponent>(entity)) {
      name = world.get<components::TagComponent>(entity).name;
    }
    const auto position = transform.getPosition();
    spdlog::info("Client: net_entity={} {} pos=({:.2f}, {:.2f}, {:.2f})",
                 identity.id,
                 name,
                 position.x,
                 position.y,
                 position.z);
  }
}

}  // namespace karma::examples::network_demo
