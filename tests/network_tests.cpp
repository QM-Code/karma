#include <cstddef>
#include <cstdint>
#include <atomic>
#include <algorithm>
#include <chrono>
#include <functional>
#include <iterator>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "karma/network.h"
#include "karma/network.h"
#include "karma/network.h"
#include "karma/network.h"
#include "karma/network.h"
#include "karma/network.h"
#include "karma/components.h"
#include "karma/components.h"
#include "karma/components.h"
#include "karma/world.h"

namespace {

constexpr uint32_t kAppId = 0x4B544553u;  // KTES

void expect(bool condition, const char* message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

std::span<const std::byte> bytesOf(const std::string& value) {
  return {reinterpret_cast<const std::byte*>(value.data()), value.size()};
}

struct FakeLink {
  karma::network::PeerId server_peer{1};
  karma::network::PeerId client_peer{7};
  karma::network::Endpoint endpoint{.ip = "127.0.0.1", .port = 27015};
  bool connected = false;
  std::vector<karma::network::TransportEvent> server_events;
  std::vector<karma::network::TransportEvent> client_events;
};

class FakeClientTransport final : public karma::network::IClientTransport {
 public:
  explicit FakeClientTransport(FakeLink& link) : link_(link) {}

  karma::network::ConnectResult connect(const std::string& host,
                                    uint16_t port,
                                    int timeout_ms) override {
    (void)host;
    (void)port;
    (void)timeout_ms;
    link_.connected = true;
    link_.server_events.push_back(karma::network::TransportEvent{
        .type = karma::network::TransportEvent::Type::Connect,
        .peer = link_.client_peer,
        .endpoint = link_.endpoint,
    });
    return {
        .status = karma::network::ConnectStatus::Connected,
        .peer = link_.server_peer,
        .endpoint = link_.endpoint,
    };
  }

  void disconnect(karma::network::DisconnectReason reason =
                      karma::network::DisconnectReason::Local) override {
    if (!link_.connected) {
      return;
    }
    link_.connected = false;
    link_.server_events.push_back(karma::network::TransportEvent{
        .type = karma::network::TransportEvent::Type::Disconnect,
        .peer = link_.client_peer,
        .disconnect_reason = reason,
        .endpoint = link_.endpoint,
    });
  }

  bool isConnected() const override { return link_.connected; }
  karma::network::PeerId serverPeer() const override { return link_.server_peer; }

  void poll(std::vector<karma::network::TransportEvent>& out_events) override {
    out_events.insert(out_events.end(),
                      std::make_move_iterator(link_.client_events.begin()),
                      std::make_move_iterator(link_.client_events.end()));
    link_.client_events.clear();
  }

  karma::network::SendResult send(karma::network::ChannelId channel,
                              const std::byte* data,
                              std::size_t size,
                              karma::network::Delivery delivery,
                              bool flush) override {
    (void)delivery;
    (void)flush;
    if (!link_.connected) {
      return {.status = karma::network::SendStatus::NotConnected};
    }
    link_.server_events.push_back(karma::network::TransportEvent{
        .type = karma::network::TransportEvent::Type::Receive,
        .peer = link_.client_peer,
        .channel = channel,
        .payload = std::vector<std::byte>(data, data + size),
        .endpoint = link_.endpoint,
    });
    return {.status = karma::network::SendStatus::Ok, .bytes_queued = size};
  }

  void flush() override {}

  std::optional<karma::network::Endpoint> remoteEndpoint() const override {
    return link_.endpoint;
  }

 private:
  FakeLink& link_;
};

class FakeServerTransport final : public karma::network::IServerTransport {
 public:
  explicit FakeServerTransport(FakeLink& link) : link_(link) {}

  void poll(std::vector<karma::network::TransportEvent>& out_events) override {
    out_events.insert(out_events.end(),
                      std::make_move_iterator(link_.server_events.begin()),
                      std::make_move_iterator(link_.server_events.end()));
    link_.server_events.clear();
  }

  karma::network::SendResult send(karma::network::PeerId peer,
                              karma::network::ChannelId channel,
                              const std::byte* data,
                              std::size_t size,
                              karma::network::Delivery delivery,
                              bool flush) override {
    (void)delivery;
    (void)flush;
    if (!link_.connected) {
      return {.status = karma::network::SendStatus::NotConnected};
    }
    if (peer != link_.client_peer) {
      return {.status = karma::network::SendStatus::UnknownPeer};
    }
    link_.client_events.push_back(karma::network::TransportEvent{
        .type = karma::network::TransportEvent::Type::Receive,
        .peer = link_.server_peer,
        .channel = channel,
        .payload = std::vector<std::byte>(data, data + size),
        .endpoint = link_.endpoint,
    });
    return {.status = karma::network::SendStatus::Ok, .bytes_queued = size};
  }

  void disconnect(karma::network::PeerId peer,
                  karma::network::DisconnectReason reason =
                      karma::network::DisconnectReason::Local) override {
    if (peer != link_.client_peer || !link_.connected) {
      return;
    }
    link_.connected = false;
    link_.client_events.push_back(karma::network::TransportEvent{
        .type = karma::network::TransportEvent::Type::Disconnect,
        .peer = link_.server_peer,
        .disconnect_reason = reason,
        .endpoint = link_.endpoint,
    });
  }

  void flush() override {}

  std::optional<karma::network::Endpoint> endpoint(karma::network::PeerId peer) const override {
    if (peer != link_.client_peer) {
      return std::nullopt;
    }
    return link_.endpoint;
  }

  std::vector<karma::network::PeerId> peers() const override {
    return link_.connected ? std::vector<karma::network::PeerId>{link_.client_peer}
                           : std::vector<karma::network::PeerId>{};
  }

 private:
  FakeLink& link_;
};

struct SessionHarness {
  FakeLink link;
  karma::network::ServerSession server;
  karma::network::ClientSession client;

  SessionHarness()
      : server(std::make_unique<FakeServerTransport>(link), kAppId),
        client(std::make_unique<FakeClientTransport>(link), kAppId, "test-client") {}
};

void handshake(SessionHarness& harness,
               std::vector<karma::network::SessionEvent>& server_events,
               std::vector<karma::network::SessionEvent>& client_events) {
  const auto connected = harness.client.connect("127.0.0.1", 27015, 1);
  expect(connected.connected(), "fake client should connect");
  harness.server.poll(server_events);
  harness.client.poll(client_events);
  expect(!server_events.empty(), "server should emit handshake event");
  expect(!client_events.empty(), "client should emit handshake accept");
  expect(server_events.front().type == karma::network::SessionEventType::PeerConnected,
         "server should report peer connected");
  expect(client_events.front().type == karma::network::SessionEventType::PeerConnected,
         "client should report session connected");
  expect(harness.client.isConnected(), "client session should be connected");
}

void testProtocolRoundTrip() {
  const std::string payload_text = "payload";
  karma::network::PacketHeader header;
  header.app_id = kAppId;
  header.message_type = karma::network::MessageType::CustomReliable;
  header.flags = karma::network::PacketFlagReliable;
  header.tick = 42;
  header.sequence = 99;

  std::vector<std::byte> encoded = karma::network::encodePacket(header, bytesOf(payload_text));
  const auto decoded = karma::network::decodePacket(encoded, kAppId);
  expect(decoded.ok(), "packet should decode");
  expect(decoded.packet.header.tick == 42, "tick should round trip");
  expect(decoded.packet.header.sequence == 99, "sequence should round trip");
  expect(decoded.packet.payload.size() == payload_text.size(), "payload should round trip");

  encoded[0] = std::byte{0};
  expect(karma::network::decodePacket(encoded, kAppId).status == karma::network::DecodeStatus::BadMagic,
         "bad magic should be rejected");

  encoded = karma::network::encodePacket(header, bytesOf(payload_text));
  encoded[4] = std::byte{0xFF};
  expect(karma::network::decodePacket(encoded, kAppId).status ==
             karma::network::DecodeStatus::UnsupportedVersion,
         "version mismatch should be rejected");

  encoded = karma::network::encodePacket(header, bytesOf(payload_text));
  encoded.pop_back();
  expect(karma::network::decodePacket(encoded, kAppId).status ==
             karma::network::DecodeStatus::PayloadLengthMismatch,
         "truncated payload should be rejected");
}

void testNetworkRoleHelpers() {
  static_assert(!karma::network::isServerRole(karma::network::NetworkRole::Offline));
  static_assert(karma::network::isServerRole(karma::network::NetworkRole::Server));
  static_assert(!karma::network::isServerRole(karma::network::NetworkRole::Client));
  static_assert(karma::network::isServerRole(karma::network::NetworkRole::ListenServer));
  static_assert(!karma::network::isClientRole(karma::network::NetworkRole::Offline));
  static_assert(!karma::network::isClientRole(karma::network::NetworkRole::Server));
  static_assert(karma::network::isClientRole(karma::network::NetworkRole::Client));
  static_assert(karma::network::isClientRole(karma::network::NetworkRole::ListenServer));
  static_assert(karma::network::isAuthorityRole(karma::network::NetworkRole::Server));
  static_assert(karma::network::isAuthorityRole(karma::network::NetworkRole::ListenServer));

  const karma::network::NetworkRoleContext listen{
      .role = karma::network::NetworkRole::ListenServer,
      .local_peer = karma::network::PeerId{42},
  };
  expect(listen.isServer(), "listen server should be a server role");
  expect(listen.isClient(), "listen server should be a client role");
  expect(listen.isAuthority(), "listen server should be an authority role");
  expect(listen.local_peer.value == 42, "role context should preserve local peer id");
}

#if defined(KARMA_NETWORK_BACKEND_ENET)
bool waitUntil(const std::function<bool()>& predicate, int timeout_ms) {
  const auto deadline = std::chrono::steady_clock::now() +
                        std::chrono::milliseconds(timeout_ms);
  while (std::chrono::steady_clock::now() < deadline) {
    if (predicate()) {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  return predicate();
}

void testEnetLoopbackTransport() {
  constexpr uint16_t port = 32147;
  auto server = karma::network::createDefaultServerTransport(port, 4, 2);
  expect(server != nullptr, "ENet server transport should be available");

  auto timeout_client = karma::network::createDefaultClientTransport();
  expect(timeout_client != nullptr, "ENet client transport should be available");
  const auto timeout = timeout_client->connect("127.0.0.1",
                                              static_cast<uint16_t>(port + 1),
                                              25);
  expect(!timeout.connected(), "connect to unused port should fail");

  auto client = karma::network::createDefaultClientTransport();
  std::atomic<bool> connect_done{false};
  karma::network::ConnectResult connect_result{};
  std::thread connect_thread([&]() {
    connect_result = client->connect("127.0.0.1", port, 1000);
    connect_done.store(true, std::memory_order_release);
  });

  std::vector<karma::network::TransportEvent> server_events;
  karma::network::PeerId peer{};
  const bool saw_connect = waitUntil(
      [&]() {
        server->poll(server_events);
        for (const auto& event : server_events) {
          if (event.type == karma::network::TransportEvent::Type::Connect) {
            peer = event.peer;
          }
        }
        return peer.isValid() &&
               connect_done.load(std::memory_order_acquire);
      },
      1000);
  connect_thread.join();
  expect(saw_connect, "server should observe ENet connect");
  expect(connect_result.connected(), "client should complete ENet connect");

  const std::string reliable = "reliable";
  const std::string unreliable = "unreliable";
  expect(client->send(0,
                      reinterpret_cast<const std::byte*>(reliable.data()),
                      reliable.size(),
                      karma::network::Delivery::Reliable,
                      true)
             .ok(),
         "reliable client send should succeed");
  expect(client->send(1,
                      reinterpret_cast<const std::byte*>(unreliable.data()),
                      unreliable.size(),
                      karma::network::Delivery::Unreliable,
                      true)
             .ok(),
         "unreliable client send should succeed");

  bool saw_reliable = false;
  bool saw_unreliable = false;
  server_events.clear();
  expect(waitUntil(
             [&]() {
               server->poll(server_events);
               for (const auto& event : server_events) {
                 if (event.type != karma::network::TransportEvent::Type::Receive) {
                   continue;
                 }
                 saw_reliable = saw_reliable || event.channel == 0;
                 saw_unreliable = saw_unreliable || event.channel == 1;
               }
               return saw_reliable && saw_unreliable;
             },
             1000),
         "server should receive reliable and unreliable packets on selected channels");

  const std::string reply = "reply";
  expect(server->send(peer,
                      0,
                      reinterpret_cast<const std::byte*>(reply.data()),
                      reply.size(),
                      karma::network::Delivery::Reliable,
                      true)
             .ok(),
         "server send should succeed");

  std::vector<karma::network::TransportEvent> client_events;
  expect(waitUntil(
             [&]() {
               client->poll(client_events);
               return std::any_of(client_events.begin(),
                                  client_events.end(),
                                  [](const karma::network::TransportEvent& event) {
                                    return event.type ==
                                           karma::network::TransportEvent::Type::Receive;
                                  });
             },
             1000),
         "client should receive server reply");

  expect(server->send(karma::network::PeerId{999},
                      0,
                      reinterpret_cast<const std::byte*>(reply.data()),
                      reply.size(),
                      karma::network::Delivery::Reliable,
                      true)
             .status == karma::network::SendStatus::UnknownPeer,
         "send to missing peer should fail cleanly");

  client->disconnect();
  expect(client->send(0,
                      reinterpret_cast<const std::byte*>(reply.data()),
                      reply.size(),
                      karma::network::Delivery::Reliable,
                      true)
             .status == karma::network::SendStatus::NotConnected,
         "send after client disconnect should fail cleanly");
}
#endif

void testSessionHandshakeCustomAndFilters() {
  SessionHarness harness;
  std::vector<karma::network::SessionEvent> server_events;
  std::vector<karma::network::SessionEvent> client_events;
  handshake(harness, server_events, client_events);

  server_events.clear();
  const std::string client_message = "hello server";
  harness.client.sendCustom(bytesOf(client_message), karma::network::Delivery::Reliable, 0, 3);
  harness.server.poll(server_events);
  expect(server_events.size() == 1, "server should receive one custom event");
  expect(server_events[0].type == karma::network::SessionEventType::CustomMessage,
         "server should classify custom payloads");
  expect(!server_events[0].stale_sequence, "first custom message should not be stale");

  karma::network::PacketHeader duplicate_header;
  duplicate_header.app_id = kAppId;
  duplicate_header.message_type = karma::network::MessageType::CustomReliable;
  duplicate_header.flags = karma::network::PacketFlagReliable;
  duplicate_header.tick = 3;
  duplicate_header.sequence = server_events[0].sequence;
  harness.link.server_events.push_back(karma::network::TransportEvent{
      .type = karma::network::TransportEvent::Type::Receive,
      .peer = harness.link.client_peer,
      .channel = 0,
      .payload = karma::network::encodePacket(duplicate_header, bytesOf(client_message)),
      .endpoint = harness.link.endpoint,
  });
  server_events.clear();
  harness.server.poll(server_events);
  expect(server_events.size() == 1 && server_events[0].stale_sequence,
         "duplicate sequence should be marked stale");

  harness.server.addPeerToGroup(harness.link.client_peer, "alpha");
  const std::string targeted = "targeted";
  const auto result = harness.server.sendCustomWhere(
      [](const karma::network::SessionPeer& peer) {
        return peer.groups.find("alpha") != peer.groups.end();
      },
      bytesOf(targeted),
      karma::network::Delivery::Reliable,
      0,
      4);
  expect(result.ok(), "group send should succeed");
  client_events.clear();
  harness.client.poll(client_events);
  expect(client_events.size() == 1, "client should receive targeted group message");
  expect(client_events[0].type == karma::network::SessionEventType::CustomMessage,
         "targeted message should dispatch as custom");
}

karma::world::Entity makeReplicatedEntity(karma::world::World& world) {
  auto entity = world.createEntity();
  world.add(entity,
            karma::components::TransformComponent{
                karma::math::Vec3{1.0f, 2.0f, 3.0f}});
  world.add(entity, karma::components::TagComponent{.name = "replicated"});
  world.add(entity, karma::components::NetworkIdentityComponent{});
  world.add(entity,
            karma::components::NetworkAuthorityComponent{
                .mode = karma::components::AuthorityMode::Server,
                .owner_peer = 7,
                .server_can_override = true,
            });
  world.add(entity,
            karma::components::NetworkReplicatedComponent{
                .components = {
                    {karma::network::kTransformComponentWireId,
                     karma::components::ReplicationPolicy::Delta},
                    {karma::network::kTagComponentWireId,
                     karma::components::ReplicationPolicy::Snapshot},
                },
            });
  return entity;
}

std::vector<std::byte> encodeTransformUpdatePayload(karma::components::NetworkEntityId id,
                                                    const karma::math::Vec3& position) {
  karma::network::BinaryWriter component;
  component.writeFloat32(position.x);
  component.writeFloat32(position.y);
  component.writeFloat32(position.z);
  component.writeFloat32(0.0f);
  component.writeFloat32(0.0f);
  component.writeFloat32(0.0f);
  component.writeFloat32(1.0f);
  component.writeFloat32(1.0f);
  component.writeFloat32(1.0f);
  component.writeFloat32(1.0f);
  const std::vector<std::byte> component_bytes = component.takeBytes();

  karma::network::BinaryWriter payload;
  payload.writeUInt64(id);
  payload.writeUInt32(karma::network::kTransformComponentWireId);
  payload.writeUInt32(static_cast<uint32_t>(component_bytes.size()));
  payload.writeBytes(component_bytes);
  return payload.takeBytes();
}

std::vector<std::byte> encodeDespawnPayload(karma::components::NetworkEntityId id) {
  karma::network::BinaryWriter payload;
  payload.writeUInt64(id);
  return payload.takeBytes();
}

std::vector<std::byte> encodeSpawnPayload(karma::components::NetworkEntityId id,
                                          const karma::math::Vec3& position,
                                          karma::components::AuthorityMode mode =
                                              karma::components::AuthorityMode::Server,
                                          uint32_t owner_peer = 0) {
  karma::network::BinaryWriter component;
  component.writeFloat32(position.x);
  component.writeFloat32(position.y);
  component.writeFloat32(position.z);
  component.writeFloat32(0.0f);
  component.writeFloat32(0.0f);
  component.writeFloat32(0.0f);
  component.writeFloat32(1.0f);
  component.writeFloat32(1.0f);
  component.writeFloat32(1.0f);
  component.writeFloat32(1.0f);
  const std::vector<std::byte> component_bytes = component.takeBytes();

  karma::network::BinaryWriter payload;
  payload.writeUInt64(id);
  payload.writeUInt8(static_cast<uint8_t>(mode));
  payload.writeUInt32(owner_peer);
  payload.writeUInt8(1);
  payload.writeUInt16(1);
  payload.writeUInt32(karma::network::kTransformComponentWireId);
  payload.writeUInt8(static_cast<uint8_t>(karma::components::ReplicationPolicy::Delta));
  payload.writeUInt32(static_cast<uint32_t>(component_bytes.size()));
  payload.writeBytes(component_bytes);
  return payload.takeBytes();
}

karma::network::SessionEvent makeReplicationEvent(
    karma::network::MessageType message_type,
    std::vector<std::byte> payload,
    uint32_t tick,
    uint32_t sequence,
    karma::network::PeerId peer = {}) {
  return karma::network::SessionEvent{
      .type = karma::network::SessionEventType::ReplicationMessage,
      .peer = peer,
      .message_type = message_type,
      .tick = tick,
      .sequence = sequence,
      .payload = std::move(payload),
  };
}

void pumpClientReplication(karma::network::ClientSession& client,
                           karma::network::ClientReplicationState& replication,
                           karma::world::World& client_world,
                           const karma::network::ComponentReplicationRegistry& registry) {
  std::vector<karma::network::SessionEvent> events;
  client.poll(events);
  for (const auto& event : events) {
    replication.applyEvent(client_world, registry, event);
  }
}

void testReplicationSpawnUpdateDespawn() {
  SessionHarness harness;
  std::vector<karma::network::SessionEvent> server_events;
  std::vector<karma::network::SessionEvent> client_events;
  handshake(harness, server_events, client_events);

  karma::network::ComponentReplicationRegistry registry;
  karma::network::registerBuiltinReplicators(registry);
  karma::network::ServerReplicationState server_replication;
  karma::network::ClientReplicationState client_replication;
  karma::world::World server_world;
  karma::world::World client_world;
  const auto server_entity = makeReplicatedEntity(server_world);

  auto sent = server_replication.replicate(server_world, harness.server, registry, 10);
  expect(sent.ok() && sent.sent == 1, "initial replication should send spawn");
  pumpClientReplication(harness.client, client_replication, client_world, registry);

  const auto network_id =
      server_world.get<karma::components::NetworkIdentityComponent>(server_entity).id;
  const auto maybe_client_entity = client_replication.entityFor(network_id);
  expect(maybe_client_entity.has_value(), "client should create spawned entity");
  expect(client_world.has<karma::components::TagComponent>(*maybe_client_entity),
         "client spawn should apply tag");
  expect(client_world.get<karma::components::TagComponent>(*maybe_client_entity).name ==
             "replicated",
         "tag should round trip");
  expect(client_world.has<karma::components::NetworkAuthorityComponent>(*maybe_client_entity),
         "authority metadata should be applied");

  auto& transform = server_world.get<karma::components::TransformComponent>(server_entity);
  transform.setPosition({4.0f, 5.0f, 6.0f});
  sent = server_replication.replicate(server_world, harness.server, registry, 11);
  expect(sent.ok() && sent.sent == 1, "dirty transform should send one update");
  pumpClientReplication(harness.client, client_replication, client_world, registry);
  const auto position =
      client_world.get<karma::components::TransformComponent>(*maybe_client_entity)
          .getPosition();
  expect(position.x == 4.0f && position.y == 5.0f && position.z == 6.0f,
         "transform snapshot should apply on client");

  karma::network::SessionEvent client_update{
      .type = karma::network::SessionEventType::ReplicationMessage,
      .peer = harness.link.client_peer,
      .message_type = karma::network::MessageType::ComponentSnapshot,
      .payload = encodeTransformUpdatePayload(network_id, {9.0f, 9.0f, 9.0f}),
  };
  expect(!server_replication.applyClientComponentEvent(server_world,
                                                       registry,
                                                       client_update,
                                                       false),
         "server-authority component should reject client writes");
  auto server_position =
      server_world.get<karma::components::TransformComponent>(server_entity).getPosition();
  expect(server_position.x == 4.0f && server_position.y == 5.0f && server_position.z == 6.0f,
         "rejected client write should not mutate server state");

  expect(server_replication.applyClientComponentEvent(server_world,
                                                      registry,
                                                      client_update,
                                                      true),
         "server override should accept client component payload");
  server_position =
      server_world.get<karma::components::TransformComponent>(server_entity).getPosition();
  expect(server_position.x == 9.0f && server_position.y == 9.0f && server_position.z == 9.0f,
         "server override should apply payload");

  auto& authority =
      server_world.get<karma::components::NetworkAuthorityComponent>(server_entity);
  authority.mode = karma::components::AuthorityMode::Owner;
  authority.owner_peer = harness.link.client_peer.value;
  client_update.payload = encodeTransformUpdatePayload(network_id, {7.0f, 8.0f, 9.0f});
  expect(server_replication.applyClientComponentEvent(server_world,
                                                      registry,
                                                      client_update,
                                                      false),
         "owner-authority component should accept owning peer writes");
  server_position =
      server_world.get<karma::components::TransformComponent>(server_entity).getPosition();
  expect(server_position.x == 7.0f && server_position.y == 8.0f && server_position.z == 9.0f,
         "owner-authority write should apply");

  expect(server_replication.sendDespawn(harness.server, network_id, harness.link.client_peer, 12),
         "despawn send should succeed");
  pumpClientReplication(harness.client, client_replication, client_world, registry);
  expect(!client_world.isAlive(*maybe_client_entity), "client entity should be destroyed");

  karma::network::BinaryWriter stale_payload;
  stale_payload.writeUInt64(network_id);
  stale_payload.writeUInt32(karma::network::kTransformComponentWireId);
  stale_payload.writeUInt32(0);
  karma::network::SessionEvent stale_event{
      .type = karma::network::SessionEventType::ReplicationMessage,
      .message_type = karma::network::MessageType::ComponentSnapshot,
      .payload = stale_payload.takeBytes(),
  };
  expect(!client_replication.applyEvent(client_world, registry, stale_event),
         "stale component update for missing entity should be rejected");
}

void testReplicationPeerCleanupAndReconnect() {
  SessionHarness harness;
  std::vector<karma::network::SessionEvent> server_events;
  std::vector<karma::network::SessionEvent> client_events;
  handshake(harness, server_events, client_events);

  karma::network::ComponentReplicationRegistry registry;
  karma::network::registerBuiltinReplicators(registry);
  karma::network::ServerReplicationState server_replication;
  karma::network::ClientReplicationState client_replication;
  karma::world::World server_world;
  karma::world::World client_world;
  makeReplicatedEntity(server_world);

  auto sent = server_replication.replicate(server_world, harness.server, registry, 20);
  expect(sent.ok() && sent.sent == 1, "initial peer should receive spawn");
  pumpClientReplication(harness.client, client_replication, client_world, registry);

  harness.link.server_events.push_back(karma::network::TransportEvent{
      .type = karma::network::TransportEvent::Type::Disconnect,
      .peer = harness.link.client_peer,
      .disconnect_reason = karma::network::DisconnectReason::Remote,
      .endpoint = harness.link.endpoint,
  });
  server_events.clear();
  harness.server.poll(server_events);
  expect(!server_events.empty() &&
             server_events.front().type == karma::network::SessionEventType::PeerDisconnected,
         "server should observe peer disconnect");
  server_replication.removePeer(harness.link.client_peer);
  harness.link.connected = false;

  server_events.clear();
  client_events.clear();
  handshake(harness, server_events, client_events);
  sent = server_replication.replicate(server_world, harness.server, registry, 21);
  expect(sent.ok() && sent.sent == 1,
         "reconnected peer should receive a fresh spawn after cleanup");
}

void testReplicationVisibilityTransitions() {
  SessionHarness harness;
  std::vector<karma::network::SessionEvent> server_events;
  std::vector<karma::network::SessionEvent> client_events;
  handshake(harness, server_events, client_events);

  karma::network::ComponentReplicationRegistry registry;
  karma::network::registerBuiltinReplicators(registry);
  karma::network::ServerReplicationState server_replication;
  karma::network::ClientReplicationState client_replication;
  karma::world::World server_world;
  karma::world::World client_world;
  const auto server_entity = makeReplicatedEntity(server_world);

  bool visible = true;
  const karma::network::ReplicationVisibilityPredicate visibility =
      [&visible](const karma::network::SessionPeer& peer,
                 karma::world::Entity entity,
                 karma::components::NetworkEntityId id) {
        (void)peer;
        (void)entity;
        (void)id;
        return visible;
      };

  auto sent = server_replication.replicate(server_world,
                                          harness.server,
                                          registry,
                                          30,
                                          visibility);
  expect(sent.ok() && sent.sent == 1, "visible entity should spawn");
  pumpClientReplication(harness.client, client_replication, client_world, registry);
  const auto network_id =
      server_world.get<karma::components::NetworkIdentityComponent>(server_entity).id;
  auto maybe_client_entity = client_replication.entityFor(network_id);
  expect(maybe_client_entity.has_value(), "client should map visible entity");

  visible = false;
  sent = server_replication.replicate(server_world, harness.server, registry, 31, visibility);
  expect(sent.ok() && sent.sent == 1, "visible-to-hidden should send one despawn");
  pumpClientReplication(harness.client, client_replication, client_world, registry);
  expect(!client_world.isAlive(*maybe_client_entity), "hidden entity should be despawned");

  sent = server_replication.replicate(server_world, harness.server, registry, 32, visibility);
  expect(sent.sent == 0, "hidden entity should not repeatedly despawn");

  auto& transform = server_world.get<karma::components::TransformComponent>(server_entity);
  transform.setPosition({10.0f, 11.0f, 12.0f});
  visible = true;
  sent = server_replication.replicate(server_world, harness.server, registry, 33, visibility);
  expect(sent.ok() && sent.sent == 1, "hidden-to-visible should send a fresh spawn");
  pumpClientReplication(harness.client, client_replication, client_world, registry);
  maybe_client_entity = client_replication.entityFor(network_id);
  expect(maybe_client_entity.has_value() && client_world.isAlive(*maybe_client_entity),
         "client should recreate visible entity");
  const auto position =
      client_world.get<karma::components::TransformComponent>(*maybe_client_entity)
          .getPosition();
  expect(position.x == 10.0f && position.y == 11.0f && position.z == 12.0f,
         "fresh spawn should include full component state");
}

void testClientStaleReplicationRejection() {
  karma::network::ComponentReplicationRegistry registry;
  karma::network::registerBuiltinReplicators(registry);
  karma::network::ClientReplicationState replication;
  karma::world::World world;
  constexpr karma::components::NetworkEntityId network_id = 42;

  expect(replication.applyEvent(
             world,
             registry,
             makeReplicationEvent(karma::network::MessageType::EntitySpawn,
                                  encodeSpawnPayload(network_id, {1.0f, 2.0f, 3.0f}),
                                  10,
                                  1)),
         "initial spawn should apply");
  auto entity = replication.entityFor(network_id);
  expect(entity.has_value(), "spawn should create entity");

  expect(replication.applyEvent(
             world,
             registry,
             makeReplicationEvent(karma::network::MessageType::ComponentSnapshot,
                                  encodeTransformUpdatePayload(network_id,
                                                               {5.0f, 6.0f, 7.0f}),
                                  12,
                                  2)),
         "newer snapshot should apply");
  auto position = world.get<karma::components::TransformComponent>(*entity).getPosition();
  expect(position.x == 5.0f && position.y == 6.0f && position.z == 7.0f,
         "newer snapshot should mutate component");

  expect(!replication.applyEvent(
             world,
             registry,
             makeReplicationEvent(karma::network::MessageType::ComponentDelta,
                                  encodeTransformUpdatePayload(network_id,
                                                               {8.0f, 8.0f, 8.0f}),
                                  11,
                                  9)),
         "older delta should be rejected even with a higher sequence");
  position = world.get<karma::components::TransformComponent>(*entity).getPosition();
  expect(position.x == 5.0f && position.y == 6.0f && position.z == 7.0f,
         "stale delta should not mutate component");

  expect(!replication.applyEvent(
             world,
             registry,
             makeReplicationEvent(karma::network::MessageType::EntityDespawn,
                                  encodeDespawnPayload(network_id),
                                  9,
                                  10)),
         "older despawn should be rejected");
  expect(world.isAlive(*entity), "stale despawn should not destroy entity");

  expect(replication.applyEvent(
             world,
             registry,
             makeReplicationEvent(karma::network::MessageType::EntityDespawn,
                                  encodeDespawnPayload(network_id),
                                  13,
                                  11)),
         "newer despawn should apply");
  expect(!world.isAlive(*entity), "newer despawn should destroy entity");

  expect(!replication.applyEvent(
             world,
             registry,
             makeReplicationEvent(karma::network::MessageType::ComponentSnapshot,
                                  encodeTransformUpdatePayload(network_id,
                                                               {9.0f, 9.0f, 9.0f}),
                                  12,
                                  12)),
         "component update older than despawn should be rejected");
  expect(!replication.applyEvent(
             world,
             registry,
             makeReplicationEvent(karma::network::MessageType::EntitySpawn,
                                  encodeSpawnPayload(network_id, {2.0f, 2.0f, 2.0f}),
                                  12,
                                  13)),
         "spawn older than despawn should be rejected");

  expect(replication.applyEvent(
             world,
             registry,
             makeReplicationEvent(karma::network::MessageType::EntitySpawn,
                                  encodeSpawnPayload(network_id, {3.0f, 3.0f, 3.0f}),
                                  14,
                                  14)),
         "newer spawn should recreate entity");
  entity = replication.entityFor(network_id);
  expect(entity.has_value() && world.isAlive(*entity), "newer spawn should be alive");
  const auto recreated = *entity;
  expect(replication.applyEvent(
             world,
             registry,
             makeReplicationEvent(karma::network::MessageType::EntitySpawn,
                                  encodeSpawnPayload(network_id, {3.0f, 3.0f, 3.0f}),
                                  14,
                                  14)),
         "same newest spawn should be idempotent");
  expect(replication.entityFor(network_id).value() == recreated,
         "idempotent spawn should keep the entity mapping");
  expect(replication.staleRejects() == 4, "four stale replication events should be counted");
}

void testServerAuthorityModes() {
  karma::network::ComponentReplicationRegistry registry;
  karma::network::registerBuiltinReplicators(registry);
  karma::network::ServerReplicationState replication;
  karma::world::World world;
  const auto entity = makeReplicatedEntity(world);
  replication.ensureNetworkIds(world);
  const auto network_id = world.get<karma::components::NetworkIdentityComponent>(entity).id;
  auto& authority = world.get<karma::components::NetworkAuthorityComponent>(entity);

  auto apply_position = [&](karma::network::PeerId peer, const karma::math::Vec3& position) {
    return replication.applyClientComponentEvent(
        world,
        registry,
        makeReplicationEvent(karma::network::MessageType::ComponentSnapshot,
                             encodeTransformUpdatePayload(network_id, position),
                             1,
                             1,
                             peer),
        false);
  };

  authority.mode = karma::components::AuthorityMode::Server;
  expect(!apply_position(karma::network::PeerId{7}, {1.0f, 1.0f, 1.0f}),
         "server-authority should reject client writes");

  authority.mode = karma::components::AuthorityMode::Custom;
  expect(!apply_position(karma::network::PeerId{7}, {2.0f, 2.0f, 2.0f}),
         "custom-authority should reject client writes by default");

  authority.mode = karma::components::AuthorityMode::Owner;
  authority.owner_peer = 7;
  expect(!apply_position(karma::network::PeerId{8}, {3.0f, 3.0f, 3.0f}),
         "owner-authority should reject non-owner writes");
  expect(apply_position(karma::network::PeerId{7}, {4.0f, 4.0f, 4.0f}),
         "owner-authority should accept owner writes");
  auto position = world.get<karma::components::TransformComponent>(entity).getPosition();
  expect(position.x == 4.0f && position.y == 4.0f && position.z == 4.0f,
         "owner write should mutate state");

  authority.mode = karma::components::AuthorityMode::Client;
  expect(apply_position(karma::network::PeerId{8}, {5.0f, 5.0f, 5.0f}),
         "client-authority should accept any client write");
  position = world.get<karma::components::TransformComponent>(entity).getPosition();
  expect(position.x == 5.0f && position.y == 5.0f && position.z == 5.0f,
         "client-authority write should mutate state");

  authority.mode = karma::components::AuthorityMode::Server;
  expect(replication.applyClientComponentEvent(
             world,
             registry,
             makeReplicationEvent(karma::network::MessageType::ComponentSnapshot,
                                  encodeTransformUpdatePayload(network_id,
                                                               {6.0f, 6.0f, 6.0f}),
                                  1,
                                  1,
                                  karma::network::PeerId{9}),
             true),
         "server override should bypass authority checks");
  position = world.get<karma::components::TransformComponent>(entity).getPosition();
  expect(position.x == 6.0f && position.y == 6.0f && position.z == 6.0f,
         "server override should mutate state");
  expect(replication.authorityRejects() == 3, "three authority rejects should be counted");
}

void testNetworkRuntimeModuleStats() {
  SessionHarness harness;
  karma::network::ComponentReplicationRegistry registry;
  karma::network::registerBuiltinReplicators(registry);
  karma::world::World server_world;
  karma::world::World client_world;
  const auto server_entity = makeReplicatedEntity(server_world);
  int server_callbacks = 0;
  int client_callbacks = 0;
  karma::network::ServerNetworkRuntimeModule server_module(
      harness.server,
      registry,
      karma::network::ServerNetworkRuntimeConfig{
          .app_id = kAppId,
          .event_handler =
              [&server_callbacks](const karma::network::SessionEvent& event,
                                  karma::world::World& world) {
                (void)event;
                (void)world;
                ++server_callbacks;
              },
          .replication_enabled = true,
      });
  karma::network::ClientNetworkRuntimeModule client_module(
      harness.client,
      registry,
      karma::network::ClientNetworkRuntimeConfig{
          .app_id = kAppId,
          .event_handler =
              [&client_callbacks](const karma::network::SessionEvent& event,
                                  karma::world::World& world) {
                (void)event;
                (void)world;
                ++client_callbacks;
              },
          .replication_enabled = true,
      });

  const auto connected = harness.client.connect("127.0.0.1", 27015, 1);
  expect(connected.connected(), "runtime stats fake client should connect");
  server_module.onFrameBegin(server_world, 0.0f);
  client_module.onFrameBegin(client_world, 0.0f);
  expect(server_module.stats().events_received == 1, "server should count handshake event");
  expect(client_module.stats().events_received == 1, "client should count handshake event");

  const std::string custom = "runtime-custom";
  harness.client.sendCustom(bytesOf(custom), karma::network::Delivery::Reliable, 0, 2);
  const std::string input = "runtime-input";
  harness.client.sendInputCommand(bytesOf(input), 3);
  server_module.onFrameBegin(server_world, 0.0f);
  expect(server_module.stats().custom_messages == 1, "server should count custom messages");
  expect(server_module.stats().input_commands == 1, "server should count input commands");

  server_module.onAfterFixedUpdate(server_world, 0.016f, 4);
  expect(server_module.stats().replication_sends_attempted == 1,
         "server stats should count replication send attempts");
  expect(server_module.stats().replication_sends_succeeded == 1,
         "server stats should count successful replication sends");
  client_module.onFrameBegin(client_world, 0.0f);
  expect(client_module.stats().replication_messages == 1,
         "client stats should count replication messages");

  const auto network_id =
      server_world.get<karma::components::NetworkIdentityComponent>(server_entity).id;
  karma::network::PacketHeader stale_header;
  stale_header.app_id = kAppId;
  stale_header.message_type = karma::network::MessageType::ComponentSnapshot;
  stale_header.tick = 1;
  stale_header.sequence = 1;
  harness.link.client_events.push_back(karma::network::TransportEvent{
      .type = karma::network::TransportEvent::Type::Receive,
      .peer = harness.link.server_peer,
      .channel = 1,
      .payload = karma::network::encodePacket(
          stale_header,
          encodeTransformUpdatePayload(network_id, {99.0f, 99.0f, 99.0f})),
      .endpoint = harness.link.endpoint,
  });
  client_module.onFrameBegin(client_world, 0.0f);
  expect(client_module.stats().stale_replication_rejects == 1,
         "client stats should count stale replication rejects");

  harness.client.send(karma::network::MessageType::ComponentSnapshot,
                      encodeTransformUpdatePayload(network_id, {7.0f, 7.0f, 7.0f}),
                      karma::network::Delivery::Reliable,
                      1,
                      5);
  server_module.onFrameBegin(server_world, 0.0f);
  expect(server_module.stats().replication_messages == 1,
         "server stats should count inbound replication messages");
  expect(server_module.stats().authority_rejects == 1,
         "server stats should count authority rejects");

  harness.link.server_events.push_back(karma::network::TransportEvent{
      .type = karma::network::TransportEvent::Type::Receive,
      .peer = harness.link.client_peer,
      .channel = 0,
      .payload = {std::byte{0}},
      .endpoint = harness.link.endpoint,
  });
  server_module.onFrameBegin(server_world, 0.0f);
  expect(server_module.stats().protocol_errors == 1,
         "server stats should count protocol errors");
  expect(server_callbacks >= 5, "server event callback should run for polled events");
  expect(client_callbacks >= 3, "client event callback should run for polled events");
}

}  // namespace

int main() {
  testProtocolRoundTrip();
  testNetworkRoleHelpers();
#if defined(KARMA_NETWORK_BACKEND_ENET)
  testEnetLoopbackTransport();
#endif
  testSessionHandshakeCustomAndFilters();
  testReplicationSpawnUpdateDespawn();
  testReplicationPeerCleanupAndReconnect();
  testReplicationVisibilityTransitions();
  testClientStaleReplicationRejection();
  testServerAuthorityModes();
  testNetworkRuntimeModuleStats();
  return 0;
}
