#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <spdlog/spdlog.h>

#include "karma/karma.h"

#include "shared.h"

namespace demo = karma::examples::network_demo;

namespace {

void runClient(const std::string& host,
               uint16_t port,
               const std::string& name) {
  auto transport = karma::network::createDefaultClientTransport();
  if (!transport) {
    spdlog::error("Client: failed to create transport");
    return;
  }

  karma::network::ComponentReplicationRegistry registry;
  karma::network::registerBuiltinReplicators(registry);
  karma::world::World world;
  karma::network::ClientSession session(std::move(transport), demo::kDemoAppId, name);

  bool running = true;
  uint32_t tick = 0;
  auto last_log = std::chrono::steady_clock::now();
  karma::network::ClientNetworkRuntimeModule network(
      session,
      registry,
      karma::network::ClientNetworkRuntimeConfig{
          .app_id = demo::kDemoAppId,
          .event_handler =
              [&](const karma::network::SessionEvent& event, karma::world::World& world) {
                (void)world;
                if (event.type == karma::network::SessionEventType::PeerConnected) {
                  spdlog::info("Client: session accepted as peer {}", event.peer.value);
                } else if (event.type == karma::network::SessionEventType::PeerDisconnected) {
                  spdlog::info("Client: disconnected");
                  running = false;
                } else if (event.type == karma::network::SessionEventType::CustomMessage) {
                  spdlog::info("Client: custom message '{}'",
                               demo::payloadText(event.payload));
                }
              },
          .replication_enabled = true,
      });

  const auto connected = network.session().connect(host, port, 3000);
  if (!connected.connected()) {
    spdlog::error("Client: failed to connect to {}:{}", host, port);
    return;
  }

  spdlog::info("Client: '{}' transport connected to {}:{}", name, host, port);
  while (running && network.session().isTransportConnected()) {
    network.onFrameBegin(world, 0.016f);

    if (network.session().isConnected()) {
      const float phase = static_cast<float>(tick) * 0.05f;
      const demo::PlayerInput input{std::cos(phase), std::sin(phase)};
      const std::vector<std::byte> payload = demo::encodeInput(input);
      network.session().sendInputCommand(payload, tick);
    }
    network.onFrameEnd(world);

    const auto now = std::chrono::steady_clock::now();
    if (now - last_log >= std::chrono::seconds(1)) {
      demo::logReplicatedWorld(world);
      last_log = now;
    }

    ++tick;
    std::this_thread::sleep_for(std::chrono::milliseconds(16));
  }
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2 || argc > 4) {
    spdlog::info("Usage: {} <host> [port] [name]", argv[0]);
    spdlog::info("Run two client processes with one network_server.");
    return argc < 2 ? 1 : 0;
  }

  const std::string host = argv[1];
  const uint16_t port =
      argc >= 3 ? static_cast<uint16_t>(std::stoi(argv[2])) : demo::kDefaultPort;
  const std::string name = argc >= 4 ? argv[3] : "karma-client";
  runClient(host, port, name);
  return 0;
}
