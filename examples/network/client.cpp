#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <spdlog/spdlog.h>

#include "karma/karma.h"

#include "shared.h"

namespace demo = karma::examples::network_demo;

namespace {

std::optional<karma::network::ServerListing> discoverLanServer(uint16_t game_port) {
  const uint16_t discovery_port = karma::network::defaultLanDiscoveryPort(game_port);
  karma::network::LanServerBrowser browser(
      karma::network::LanDiscoveryConfig{
          .discovery_port = discovery_port,
          .app_id = demo::kDemoAppId,
          .game_port = game_port,
          .entry_ttl = std::chrono::milliseconds(1500),
      });
  const auto started = browser.start();
  if (!started.ok()) {
    spdlog::error("Client: failed to start LAN browser on discovery port {}",
                  discovery_port);
    return std::nullopt;
  }

  browser.sendQuery();
  auto next_query = std::chrono::steady_clock::now() + std::chrono::milliseconds(250);
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(1500);
  std::vector<karma::network::ServerListEvent> events;
  while (std::chrono::steady_clock::now() < deadline) {
    const auto now = std::chrono::steady_clock::now();
    if (now >= next_query) {
      browser.sendQuery();
      next_query = now + std::chrono::milliseconds(250);
    }

    events.clear();
    browser.poll(now, events);
    const auto listings = browser.cache().list();
    if (!listings.empty()) {
      return listings.front();
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(25));
  }
  return std::nullopt;
}

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
    spdlog::info("Usage: {} <host|--lan> [port] [name]", argv[0]);
    spdlog::info("Run two client processes with one network_server.");
    return argc < 2 ? 1 : 0;
  }

  const std::string host = argv[1];
  const uint16_t port =
      argc >= 3 ? static_cast<uint16_t>(std::stoi(argv[2])) : demo::kDefaultPort;
  const std::string name = argc >= 4 ? argv[3] : "karma-client";
  if (host == "--lan") {
    auto discovered = discoverLanServer(port);
    if (!discovered) {
      spdlog::error("Client: no LAN server discovered for app id {}", demo::kDemoAppId);
      return 1;
    }
    spdlog::info("Client: discovered '{}' at {}:{}",
                 discovered->name,
                 discovered->connect_endpoint.ip,
                 discovered->connect_endpoint.port);
    runClient(discovered->connect_endpoint.ip, discovered->connect_endpoint.port, name);
    return 0;
  }

  runClient(host, port, name);
  return 0;
}
