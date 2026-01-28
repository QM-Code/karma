#include <chrono>
#include <cstddef>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include <spdlog/spdlog.h>

#include "karma/karma.h"

namespace {

constexpr uint16_t kDefaultPort = 12345;

void runServer(uint16_t port) {
  auto server = karma::net::createDefaultServerTransport(port);
  if (!server) {
    spdlog::error("Server: failed to create transport");
    return;
  }

  spdlog::info("Server: listening on {}", port);
  std::vector<karma::net::Event> events;

  while (true) {
    events.clear();
    server->poll(events);
    for (const auto& evt : events) {
      switch (evt.type) {
        case karma::net::Event::Type::Connect:
          spdlog::info("Server: client connected {}:{}", evt.peer_ip, evt.peer_port);
          break;
        case karma::net::Event::Type::Disconnect:
        case karma::net::Event::Type::DisconnectTimeout:
          spdlog::info("Server: client disconnected {}:{}", evt.peer_ip, evt.peer_port);
          break;
        case karma::net::Event::Type::Receive: {
          std::string msg(reinterpret_cast<const char*>(evt.payload.data()),
                          evt.payload.size());
          spdlog::info("Server: recv '{}'", msg);
          server->send(evt.connection,
                       reinterpret_cast<const std::byte*>(msg.data()),
                       msg.size(),
                       karma::net::Delivery::Reliable,
                       true);
          break;
        }
      }
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
}

void runClient(const std::string& host, uint16_t port) {
  auto client = karma::net::createDefaultClientTransport();
  if (!client) {
    spdlog::error("Client: failed to create transport");
    return;
  }

  if (!client->connect(host, port, 3000)) {
    spdlog::error("Client: failed to connect to {}:{}", host, port);
    return;
  }

  spdlog::info("Client: connected to {}:{}", host, port);
  const std::string hello = "hello from karma client";
  client->send(reinterpret_cast<const std::byte*>(hello.data()),
               hello.size(),
               karma::net::Delivery::Reliable,
               true);

  std::vector<karma::net::Event> events;
  while (client->isConnected()) {
    events.clear();
    client->poll(events);
    for (const auto& evt : events) {
      if (evt.type == karma::net::Event::Type::Receive) {
        std::string msg(reinterpret_cast<const char*>(evt.payload.data()),
                        evt.payload.size());
        spdlog::info("Client: recv '{}'", msg);
      }
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    spdlog::info("Usage:");
    spdlog::info("  {} server [port]", argv[0]);
    spdlog::info("  {} client <host> [port]", argv[0]);
    return 0;
  }

  const std::string mode = argv[1];
  if (mode == "server") {
    const uint16_t port = argc >= 3 ? static_cast<uint16_t>(std::stoi(argv[2])) : kDefaultPort;
    runServer(port);
  } else if (mode == "client") {
    if (argc < 3) {
      spdlog::error("Client mode requires host");
      return 1;
    }
    const std::string host = argv[2];
    const uint16_t port = argc >= 4 ? static_cast<uint16_t>(std::stoi(argv[3])) : kDefaultPort;
    runClient(host, port);
  } else {
    spdlog::error("Unknown mode '{}'", mode);
    return 1;
  }

  return 0;
}
