#include "karma/network/enet_transport.h"

#include <enet.h>
#include <spdlog/spdlog.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <mutex>

namespace karma::net {
namespace {

class EnetGlobal {
 public:
  EnetGlobal() {
    std::lock_guard<std::mutex> lock(mutex());
    if (refCount()++ == 0) {
      if (enet_initialize() != 0) {
        spdlog::error("ENet: failed to initialize");
      }
    }
  }

  ~EnetGlobal() {
    std::lock_guard<std::mutex> lock(mutex());
    auto& count = refCount();
    if (count == 0) {
      return;
    }
    if (--count == 0) {
      enet_deinitialize();
    }
  }

 private:
  static std::mutex& mutex() {
    static std::mutex m;
    return m;
  }

  static std::uint32_t& refCount() {
    static std::uint32_t count = 0;
    return count;
  }
};

ENetPacketFlag toEnetFlag(Delivery delivery) {
  switch (delivery) {
    case Delivery::Reliable:
      return ENET_PACKET_FLAG_RELIABLE;
    case Delivery::Unreliable:
      return static_cast<ENetPacketFlag>(0);
  }
  return ENET_PACKET_FLAG_RELIABLE;
}

enet_uint8 toEnetChannel(Delivery delivery, int num_channels) {
  if (delivery == Delivery::Unreliable && num_channels > 1) {
    return 1;
  }
  return 0;
}

std::optional<std::string> peerIpString(const ENetAddress& addr) {
  std::array<char, 128> ip_buffer{};
  if (enet_address_get_host_ip(&addr, ip_buffer.data(), ip_buffer.size()) == 0) {
    return std::string(ip_buffer.data());
  }
  return std::nullopt;
}

void drainHostEvents(ENetHost* host, int max_events) {
  if (!host || max_events <= 0) {
    return;
  }
  ENetEvent event;
  int drained = 0;
  while (drained < max_events && enet_host_service(host, &event, 0) > 0) {
    if (event.type == ENET_EVENT_TYPE_RECEIVE) {
      enet_packet_destroy(event.packet);
    }
    drained += 1;
  }
}

class EnetClientTransport final : public IClientTransport {
 public:
  EnetClientTransport() = default;
  ~EnetClientTransport() override {
    disconnect();
    if (host_) {
      enet_host_destroy(host_);
      host_ = nullptr;
    }
  }

  bool connect(const std::string& host_name, uint16_t port, int timeout_ms) override {
    disconnect();
    remote_ip_.reset();
    remote_port_.reset();

    if (!host_) {
      host_ = enet_host_create(nullptr, 1, channel_count_, 0, 0);
      if (!host_) {
        spdlog::error("ENet client: failed to create host");
        return false;
      }
    }

    ENetAddress address;
    if (enet_address_set_host(&address, host_name.c_str()) != 0) {
      spdlog::error("ENet client: failed to resolve host {}", host_name);
      return false;
    }
    address.port = port;

    peer_ = enet_host_connect(host_, &address, channel_count_, 0);
    if (!peer_) {
      spdlog::error("ENet client: no available peers");
      return false;
    }

    ENetEvent event;
    if (enet_host_service(host_, &event, timeout_ms) > 0 &&
        event.type == ENET_EVENT_TYPE_CONNECT) {
      remote_ip_ = peerIpString(event.peer->address);
      remote_port_ = event.peer->address.port;
      enet_host_flush(host_);
      return true;
    }

    enet_peer_reset(peer_);
    peer_ = nullptr;
    return false;
  }

  void disconnect() override {
    if (!peer_) {
      return;
    }
    enet_peer_disconnect(peer_, 0);
    if (host_) {
      enet_host_flush(host_);
      ENetEvent event;
      bool disconnected = false;
      for (int i = 0; i < 32; ++i) {
        if (enet_host_service(host_, &event, 0) <= 0) {
          break;
        }
        if (event.type == ENET_EVENT_TYPE_RECEIVE) {
          enet_packet_destroy(event.packet);
        }
        if (event.type == ENET_EVENT_TYPE_DISCONNECT) {
          disconnected = true;
          break;
        }
      }
      if (!disconnected) {
        enet_peer_reset(peer_);
      }
      drainHostEvents(host_, 32);
    }
    peer_ = nullptr;
  }

  bool isConnected() const override {
    return peer_ != nullptr;
  }

  void poll(std::vector<Event>& out_events) override {
    if (!host_) {
      return;
    }

    ENetEvent event;
    while (enet_host_service(host_, &event, 0) > 0) {
      switch (event.type) {
        case ENET_EVENT_TYPE_RECEIVE: {
          Event e;
          e.type = Event::Type::Receive;
          e.connection = reinterpret_cast<ConnectionHandle>(event.peer);
          e.payload.resize(event.packet->dataLength);
          std::memcpy(e.payload.data(), event.packet->data, event.packet->dataLength);
          out_events.push_back(std::move(e));
          enet_packet_destroy(event.packet);
          break;
        }
        case ENET_EVENT_TYPE_DISCONNECT: {
          Event e;
          e.type = Event::Type::Disconnect;
          e.connection = reinterpret_cast<ConnectionHandle>(event.peer);
          if (auto ip = peerIpString(event.peer->address)) {
            e.peer_ip = *ip;
          }
          e.peer_port = event.peer->address.port;
          out_events.push_back(std::move(e));
          peer_ = nullptr;
          remote_ip_.reset();
          remote_port_.reset();
          break;
        }
        case ENET_EVENT_TYPE_DISCONNECT_TIMEOUT: {
          Event e;
          e.type = Event::Type::DisconnectTimeout;
          e.connection = reinterpret_cast<ConnectionHandle>(event.peer);
          if (auto ip = peerIpString(event.peer->address)) {
            e.peer_ip = *ip;
          }
          e.peer_port = event.peer->address.port;
          out_events.push_back(std::move(e));
          peer_ = nullptr;
          remote_ip_.reset();
          remote_port_.reset();
          break;
        }
        default:
          break;
      }
    }
  }

  void send(const std::byte* data, std::size_t size, Delivery delivery,
            bool flush) override {
    if (!host_ || !peer_) {
      return;
    }

    ENetPacket* packet = enet_packet_create(data, size, toEnetFlag(delivery));
    enet_peer_send(peer_, toEnetChannel(delivery, channel_count_), packet);

    if (flush) {
      enet_host_flush(host_);
    }
  }

  std::optional<std::string> getRemoteIp() const override {
    return remote_ip_;
  }

  std::optional<uint16_t> getRemotePort() const override {
    return remote_port_;
  }

 private:
  EnetGlobal global_;
  ENetHost* host_ = nullptr;
  ENetPeer* peer_ = nullptr;
  std::optional<std::string> remote_ip_;
  std::optional<uint16_t> remote_port_;
  static constexpr int channel_count_ = 2;
};

class EnetServerTransport final : public IServerTransport {
 public:
  EnetServerTransport(uint16_t port, int max_clients, int num_channels) {
    ENetAddress address;
    address.host = ENET_HOST_ANY;
    address.port = port;

    host_ = enet_host_create(&address, max_clients, num_channels, 0, 0);
    channel_count_ = num_channels;
    if (!host_) {
      spdlog::error("ENet server: failed to create host on port {}", port);
    }
  }

  ~EnetServerTransport() override {
    if (host_) {
      for (size_t i = 0; i < host_->peerCount; ++i) {
        ENetPeer* peer = &host_->peers[i];
        if (peer->state != ENET_PEER_STATE_DISCONNECTED) {
          enet_peer_disconnect(peer, 0);
        }
      }
      enet_host_flush(host_);
      drainHostEvents(host_, 128);
      enet_host_destroy(host_);
      host_ = nullptr;
    }
  }

  void poll(std::vector<Event>& out_events) override {
    if (!host_) {
      return;
    }

    ENetEvent event;
    while (enet_host_service(host_, &event, 0) > 0) {
      switch (event.type) {
        case ENET_EVENT_TYPE_CONNECT: {
          Event e;
          e.type = Event::Type::Connect;
          e.connection = reinterpret_cast<ConnectionHandle>(event.peer);
          if (auto ip = peerIpString(event.peer->address)) {
            e.peer_ip = *ip;
          }
          e.peer_port = event.peer->address.port;
          out_events.push_back(std::move(e));
          break;
        }
        case ENET_EVENT_TYPE_RECEIVE: {
          Event e;
          e.type = Event::Type::Receive;
          e.connection = reinterpret_cast<ConnectionHandle>(event.peer);
          e.payload.resize(event.packet->dataLength);
          std::memcpy(e.payload.data(), event.packet->data, event.packet->dataLength);
          out_events.push_back(std::move(e));
          enet_packet_destroy(event.packet);
          break;
        }
        case ENET_EVENT_TYPE_DISCONNECT: {
          Event e;
          e.type = Event::Type::Disconnect;
          e.connection = reinterpret_cast<ConnectionHandle>(event.peer);
          if (auto ip = peerIpString(event.peer->address)) {
            e.peer_ip = *ip;
          }
          e.peer_port = event.peer->address.port;
          out_events.push_back(std::move(e));
          break;
        }
        case ENET_EVENT_TYPE_DISCONNECT_TIMEOUT: {
          Event e;
          e.type = Event::Type::DisconnectTimeout;
          e.connection = reinterpret_cast<ConnectionHandle>(event.peer);
          if (auto ip = peerIpString(event.peer->address)) {
            e.peer_ip = *ip;
          }
          e.peer_port = event.peer->address.port;
          out_events.push_back(std::move(e));
          break;
        }
        default:
          break;
      }
    }
  }

  void send(ConnectionHandle connection, const std::byte* data, std::size_t size,
            Delivery delivery, bool flush) override {
    if (!host_) {
      return;
    }

    auto* peer = reinterpret_cast<ENetPeer*>(connection);
    if (!peer) {
      return;
    }

    ENetPacket* packet = enet_packet_create(data, size, toEnetFlag(delivery));
    enet_peer_send(peer, toEnetChannel(delivery, channel_count_), packet);

    if (flush) {
      enet_host_flush(host_);
    }
  }

  void disconnect(ConnectionHandle connection) override {
    auto* peer = reinterpret_cast<ENetPeer*>(connection);
    if (!peer) {
      return;
    }

    enet_peer_disconnect(peer, 0);
  }

 private:
  EnetGlobal global_;
  ENetHost* host_ = nullptr;
  int channel_count_ = 1;
};

}  // namespace

std::unique_ptr<IClientTransport> createEnetClientTransport() {
  return std::make_unique<EnetClientTransport>();
}

std::unique_ptr<IServerTransport> createEnetServerTransport(uint16_t port, int max_clients,
                                                            int num_channels) {
  return std::make_unique<EnetServerTransport>(port, max_clients, num_channels);
}

}  // namespace karma::net
