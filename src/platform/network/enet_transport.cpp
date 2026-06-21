#include "karma/network.h"

#if __has_include(<enet/enet.h>)
#include <enet/enet.h>
#else
#include <enet.h>
#endif
#include <spdlog/spdlog.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <unordered_map>

namespace karma::network {
namespace {

constexpr PeerId kServerPeer{1};

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

  static uint32_t& refCount() {
    static uint32_t count = 0;
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

std::optional<std::string> peerIpString(const ENetAddress& addr) {
  std::array<char, 128> ip_buffer{};
  if (enet_address_get_host_ip(&addr, ip_buffer.data(), ip_buffer.size()) == 0) {
    return std::string(ip_buffer.data());
  }
  return std::nullopt;
}

Endpoint endpointFromPeer(const ENetPeer* peer) {
  Endpoint endpoint{};
  if (!peer) {
    return endpoint;
  }
  if (auto ip = peerIpString(peer->address)) {
    endpoint.ip = *ip;
  }
  endpoint.port = peer->address.port;
  return endpoint;
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

void* peerData(PeerId id) {
  return reinterpret_cast<void*>(static_cast<uintptr_t>(id.value));
}

PeerId peerIdFromData(void* data) {
  return PeerId{static_cast<uint32_t>(reinterpret_cast<uintptr_t>(data))};
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

  ConnectResult connect(const std::string& host_name,
                        uint16_t port,
                        int timeout_ms) override {
    disconnect();
    remote_endpoint_.reset();

    if (!host_) {
      host_ = enet_host_create(nullptr, 1, channel_count_, 0, 0);
      if (!host_) {
        spdlog::error("ENet client: failed to create host");
        return {.status = ConnectStatus::HostCreateFailed};
      }
    }

    ENetAddress address;
    if (enet_address_set_host(&address, host_name.c_str()) != 0) {
      spdlog::error("ENet client: failed to resolve host {}", host_name);
      return {.status = ConnectStatus::ResolveFailed};
    }
    address.port = port;

    peer_ = enet_host_connect(host_, &address, channel_count_, 0);
    if (!peer_) {
      spdlog::error("ENet client: no available peers");
      return {.status = ConnectStatus::NoAvailablePeer};
    }
    peer_->data = peerData(kServerPeer);

    ENetEvent event;
    if (enet_host_service(host_, &event, timeout_ms) > 0 &&
        event.type == ENET_EVENT_TYPE_CONNECT) {
      remote_endpoint_ = endpointFromPeer(event.peer);
      enet_host_flush(host_);
      return {
          .status = ConnectStatus::Connected,
          .peer = kServerPeer,
          .endpoint = *remote_endpoint_,
      };
    }

    enet_peer_reset(peer_);
    peer_ = nullptr;
    return {.status = ConnectStatus::Timeout};
  }

  void disconnect(DisconnectReason reason = DisconnectReason::Local) override {
    (void)reason;
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
    remote_endpoint_.reset();
  }

  bool isConnected() const override {
    return peer_ != nullptr;
  }

  PeerId serverPeer() const override {
    return peer_ ? kServerPeer : PeerId{};
  }

  void poll(std::vector<TransportEvent>& out_events) override {
    if (!host_) {
      return;
    }

    ENetEvent event;
    while (enet_host_service(host_, &event, 0) > 0) {
      switch (event.type) {
        case ENET_EVENT_TYPE_RECEIVE: {
          TransportEvent e;
          e.type = TransportEvent::Type::Receive;
          e.peer = kServerPeer;
          e.channel = event.channelID;
          e.payload.resize(event.packet->dataLength);
          std::memcpy(e.payload.data(), event.packet->data, event.packet->dataLength);
          out_events.push_back(std::move(e));
          enet_packet_destroy(event.packet);
          break;
        }
        case ENET_EVENT_TYPE_DISCONNECT: {
          TransportEvent e;
          e.type = TransportEvent::Type::Disconnect;
          e.peer = kServerPeer;
          e.disconnect_reason = DisconnectReason::Remote;
          e.endpoint = endpointFromPeer(event.peer);
          out_events.push_back(std::move(e));
          peer_ = nullptr;
          remote_endpoint_.reset();
          break;
        }
#ifdef ENET_EVENT_TYPE_DISCONNECT_TIMEOUT
        case ENET_EVENT_TYPE_DISCONNECT_TIMEOUT: {
          TransportEvent e;
          e.type = TransportEvent::Type::Disconnect;
          e.peer = kServerPeer;
          e.disconnect_reason = DisconnectReason::Timeout;
          e.endpoint = endpointFromPeer(event.peer);
          out_events.push_back(std::move(e));
          peer_ = nullptr;
          remote_endpoint_.reset();
          break;
        }
#endif
        default:
          break;
      }
    }
  }

  SendResult send(ChannelId channel,
                  const std::byte* data,
                  std::size_t size,
                  Delivery delivery,
                  bool flush) override {
    if (!host_ || !peer_) {
      return {.status = SendStatus::NotConnected};
    }
    if (size > 0 && data == nullptr) {
      return {.status = SendStatus::InvalidPayload};
    }
    if (channel >= channel_count_) {
      return {.status = SendStatus::InvalidChannel};
    }

    ENetPacket* packet = enet_packet_create(data, size, toEnetFlag(delivery));
    if (!packet) {
      return {.status = SendStatus::BackendError};
    }
    if (enet_peer_send(peer_, channel, packet) != 0) {
      enet_packet_destroy(packet);
      return {.status = SendStatus::BackendError};
    }

    if (flush) {
      enet_host_flush(host_);
    }
    return {.status = SendStatus::Ok, .bytes_queued = size};
  }

  void flush() override {
    if (host_) {
      enet_host_flush(host_);
    }
  }

  std::optional<Endpoint> remoteEndpoint() const override {
    return remote_endpoint_;
  }

 private:
  EnetGlobal global_;
  ENetHost* host_ = nullptr;
  ENetPeer* peer_ = nullptr;
  std::optional<Endpoint> remote_endpoint_;
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

  void poll(std::vector<TransportEvent>& out_events) override {
    if (!host_) {
      return;
    }

    ENetEvent event;
    while (enet_host_service(host_, &event, 0) > 0) {
      switch (event.type) {
        case ENET_EVENT_TYPE_CONNECT: {
          const PeerId peer_id = allocatePeer(event.peer);
          TransportEvent e;
          e.type = TransportEvent::Type::Connect;
          e.peer = peer_id;
          e.endpoint = endpointFromPeer(event.peer);
          out_events.push_back(std::move(e));
          break;
        }
        case ENET_EVENT_TYPE_RECEIVE: {
          TransportEvent e;
          e.type = TransportEvent::Type::Receive;
          e.peer = peerIdFromData(event.peer->data);
          e.channel = event.channelID;
          e.payload.resize(event.packet->dataLength);
          std::memcpy(e.payload.data(), event.packet->data, event.packet->dataLength);
          out_events.push_back(std::move(e));
          enet_packet_destroy(event.packet);
          break;
        }
        case ENET_EVENT_TYPE_DISCONNECT: {
          const PeerId peer_id = peerIdFromData(event.peer->data);
          TransportEvent e;
          e.type = TransportEvent::Type::Disconnect;
          e.peer = peer_id;
          e.disconnect_reason = DisconnectReason::Remote;
          e.endpoint = endpointFromPeer(event.peer);
          out_events.push_back(std::move(e));
          forgetPeer(peer_id);
          event.peer->data = nullptr;
          break;
        }
#ifdef ENET_EVENT_TYPE_DISCONNECT_TIMEOUT
        case ENET_EVENT_TYPE_DISCONNECT_TIMEOUT: {
          const PeerId peer_id = peerIdFromData(event.peer->data);
          TransportEvent e;
          e.type = TransportEvent::Type::Disconnect;
          e.peer = peer_id;
          e.disconnect_reason = DisconnectReason::Timeout;
          e.endpoint = endpointFromPeer(event.peer);
          out_events.push_back(std::move(e));
          forgetPeer(peer_id);
          event.peer->data = nullptr;
          break;
        }
#endif
        default:
          break;
      }
    }
  }

  SendResult send(PeerId peer,
                  ChannelId channel,
                  const std::byte* data,
                  std::size_t size,
                  Delivery delivery,
                  bool flush) override {
    if (!host_) {
      return {.status = SendStatus::NotConnected};
    }
    if (size > 0 && data == nullptr) {
      return {.status = SendStatus::InvalidPayload};
    }
    if (channel >= channel_count_) {
      return {.status = SendStatus::InvalidChannel};
    }
    auto it = peers_.find(peer.value);
    if (it == peers_.end() || it->second == nullptr) {
      return {.status = SendStatus::UnknownPeer};
    }

    ENetPacket* packet = enet_packet_create(data, size, toEnetFlag(delivery));
    if (!packet) {
      return {.status = SendStatus::BackendError};
    }
    if (enet_peer_send(it->second, channel, packet) != 0) {
      enet_packet_destroy(packet);
      return {.status = SendStatus::BackendError};
    }

    if (flush) {
      enet_host_flush(host_);
    }
    return {.status = SendStatus::Ok, .bytes_queued = size};
  }

  void disconnect(PeerId peer,
                  DisconnectReason reason = DisconnectReason::Local) override {
    (void)reason;
    auto it = peers_.find(peer.value);
    if (it == peers_.end() || it->second == nullptr) {
      return;
    }
    enet_peer_disconnect(it->second, 0);
  }

  void flush() override {
    if (host_) {
      enet_host_flush(host_);
    }
  }

  std::optional<Endpoint> endpoint(PeerId peer) const override {
    auto it = endpoints_.find(peer.value);
    if (it == endpoints_.end()) {
      return std::nullopt;
    }
    return it->second;
  }

  std::vector<PeerId> peers() const override {
    std::vector<PeerId> ids;
    ids.reserve(peers_.size());
    for (const auto& [id, peer] : peers_) {
      if (peer != nullptr) {
        ids.push_back(PeerId{id});
      }
    }
    return ids;
  }

 private:
  PeerId allocatePeer(ENetPeer* peer) {
    if (!peer) {
      return {};
    }
    const PeerId id{next_peer_id_++};
    peer->data = peerData(id);
    peers_[id.value] = peer;
    endpoints_[id.value] = endpointFromPeer(peer);
    return id;
  }

  void forgetPeer(PeerId id) {
    peers_.erase(id.value);
    endpoints_.erase(id.value);
  }

  EnetGlobal global_;
  ENetHost* host_ = nullptr;
  int channel_count_ = 1;
  uint32_t next_peer_id_ = 1;
  std::unordered_map<uint32_t, ENetPeer*> peers_;
  std::unordered_map<uint32_t, Endpoint> endpoints_;
};

}  // namespace

std::unique_ptr<IClientTransport> createEnetClientTransport() {
  return std::make_unique<EnetClientTransport>();
}

std::unique_ptr<IServerTransport> createEnetServerTransport(uint16_t port,
                                                            int max_clients,
                                                            int num_channels) {
  return std::make_unique<EnetServerTransport>(port, max_clients, num_channels);
}

}  // namespace karma::network
