#include <chrono>
#include <cstdint>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>

#include <spdlog/spdlog.h>

#include "karma/core/math/vec3.h"
#include "karma/server.h"
#include "karma/world/components/transform.h"

#include "shared.h"

namespace demo = karma::examples::network_demo;

namespace {

void runServer(uint16_t port) {
  auto transport = karma::net::createDefaultServerTransport(port, 16, 2);
  if (!transport) {
    spdlog::error("Server: failed to create transport");
    return;
  }

  karma::network::ComponentReplicationRegistry registry;
  karma::network::registerBuiltinReplicators(registry);
  karma::ecs::World world;

  std::unordered_map<uint32_t, karma::ecs::Entity> players;
  std::unordered_map<uint32_t, demo::PlayerInput> inputs;
  uint32_t tick = 0;
  bool targeted_message_sent = false;

  karma::network::ServerNetworkRuntimeModule network_module(
      std::move(transport),
      registry,
      karma::network::ServerNetworkRuntimeConfig{
          .app_id = demo::kDemoAppId,
          .replication_enabled = true,
      });

  network_module.setEventHandler(
      [&](const karma::net::SessionEvent& event, karma::ecs::World& world) {
        switch (event.type) {
          case karma::net::SessionEventType::PeerConnected: {
            const float offset = static_cast<float>(players.size()) * 2.0f;
            const std::string player_name = "player_" + std::to_string(event.peer.value);
            players[event.peer.value] =
                demo::spawnReplicatedPlayer(world, event.peer, player_name, offset);
            inputs[event.peer.value] = {};

            network_module.session().addPeerToGroup(event.peer, "players");
            spdlog::info("Server: peer {} joined as {}",
                         event.peer.value,
                         player_name);

            const std::string group_notice =
                "players group now includes peer " + std::to_string(event.peer.value);
            network_module.session().sendCustomWhere(
                [](const karma::net::SessionPeer& peer) {
                  return peer.groups.find("players") != peer.groups.end();
                },
                demo::asBytes(group_notice),
                karma::net::Delivery::Reliable,
                0,
                tick);

            if (!targeted_message_sent) {
              const std::string targeted =
                  "targeted-only hello for peer " + std::to_string(event.peer.value);
              network_module.session().sendCustomTo(event.peer,
                                                    demo::asBytes(targeted),
                                                    karma::net::Delivery::Reliable,
                                                    0,
                                                    tick);
              targeted_message_sent = true;
              spdlog::info("Server: sent targeted custom message to peer {}",
                           event.peer.value);
            }
            break;
          }
          case karma::net::SessionEventType::PeerDisconnected: {
            auto it = players.find(event.peer.value);
            if (it != players.end()) {
              if (world.isAlive(it->second) &&
                  world.has<karma::components::NetworkIdentityComponent>(it->second)) {
                const auto id =
                    world.get<karma::components::NetworkIdentityComponent>(it->second).id;
                for (const karma::net::PeerId peer : network_module.session().peers()) {
                  network_module.replicationState().sendDespawn(network_module.session(),
                                                                id,
                                                                peer,
                                                                tick);
                }
              }
              if (world.isAlive(it->second)) {
                world.destroyEntity(it->second);
              }
              players.erase(it);
            }
            inputs.erase(event.peer.value);
            spdlog::info("Server: peer {} left", event.peer.value);
            break;
          }
          case karma::net::SessionEventType::InputCommand: {
            demo::PlayerInput input{};
            if (demo::decodeInput(event.payload, input)) {
              inputs[event.peer.value] = input;
            }
            break;
          }
          case karma::net::SessionEventType::ProtocolError:
            spdlog::warn("Server: protocol error from peer {}", event.peer.value);
            break;
          default:
            break;
        }
      });

  spdlog::info("Server: listening on {}", port);
  while (true) {
    network_module.onFrameBegin(world, 0.016f);

    for (auto& [peer_value, entity] : players) {
      if (!world.isAlive(entity) ||
          !world.has<karma::components::TransformComponent>(entity)) {
        continue;
      }
      const demo::PlayerInput input = inputs[peer_value];
      auto& transform = world.get<karma::components::TransformComponent>(entity);
      karma::math::Vec3 position = transform.getPosition();
      position.x += input.dx * 0.05f;
      position.z += input.dz * 0.05f;
      transform.setPosition(position);
    }

    network_module.onAfterFixedUpdate(world, 0.016f, tick);
    network_module.onFrameEnd(world);
    ++tick;
    std::this_thread::sleep_for(std::chrono::milliseconds(16));
  }
}

}  // namespace

int main(int argc, char** argv) {
  if (argc > 2) {
    spdlog::info("Usage: {} [port]", argv[0]);
    return 0;
  }

  const uint16_t port =
      argc >= 2 ? static_cast<uint16_t>(std::stoi(argv[1])) : demo::kDefaultPort;
  runServer(port);
  return 0;
}
