#pragma once

#include "karma/app.h"
#include "karma/components.h"
#include "karma/platform.h"
#include "karma/world.h"



#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace karma::network {

/// \ingroup karma_platform
/// Wire protocol version for Karma networking v1 packets.
inline constexpr uint16_t kProtocolVersion = 1;
/// \ingroup karma_platform
/// Fixed packet header size in bytes.
inline constexpr uint16_t kPacketHeaderSize = 28;
/// \ingroup karma_platform
/// Packet magic for quick malformed datagram rejection.
inline constexpr uint32_t kPacketMagic = 0x4B4E4554u;  // KNET

/// \ingroup karma_platform
/// Built-in network message types understood by the v1 session layer.
enum class MessageType : uint16_t {
  HandshakeRequest = 1,
  HandshakeAccept = 2,
  Disconnect = 3,
  Ping = 4,
  Pong = 5,
  CustomReliable = 6,
  CustomUnreliable = 7,
  EntitySpawn = 8,
  EntityDespawn = 9,
  ComponentSnapshot = 10,
  ComponentDelta = 11,
  AuthorityTransfer = 12,
  InputCommand = 13
};

/// \ingroup karma_platform
/// Packet flags stored in `PacketHeader::flags`.
enum PacketFlag : uint16_t {
  PacketFlagNone = 0,
  PacketFlagReliable = 1u << 0u,
  PacketFlagFragment = 1u << 1u,
  PacketFlagAck = 1u << 2u
};

/// \ingroup karma_platform
/// Versioned binary packet header.
struct PacketHeader {
  uint32_t magic = kPacketMagic;
  uint16_t version = kProtocolVersion;
  uint16_t header_size = kPacketHeaderSize;
  uint32_t app_id = 0;
  MessageType message_type = MessageType::CustomReliable;
  uint16_t flags = PacketFlagNone;
  uint32_t tick = 0;
  uint32_t sequence = 0;
  uint32_t payload_length = 0;
};

/// \ingroup karma_platform
/// Decoded protocol packet.
struct Packet {
  PacketHeader header{};
  std::vector<std::byte> payload;
};

/// \ingroup karma_platform
/// Packet decode status.
enum class DecodeStatus {
  Ok,
  TooSmall,
  BadMagic,
  UnsupportedVersion,
  HeaderSizeMismatch,
  AppIdMismatch,
  PayloadLengthMismatch,
  PayloadTooLarge
};

/// \ingroup karma_platform
/// Result for packet decode attempts.
struct DecodeResult {
  DecodeStatus status = DecodeStatus::TooSmall;
  Packet packet{};

  bool ok() const { return status == DecodeStatus::Ok; }
};

/// \ingroup karma_platform
/// Binary writer used by protocol and component replication payloads.
class BinaryWriter {
 public:
  void writeUInt8(uint8_t value);
  void writeUInt16(uint16_t value);
  void writeUInt32(uint32_t value);
  void writeUInt64(uint64_t value);
  void writeFloat32(float value);
  void writeBytes(std::span<const std::byte> bytes);
  void writeString(const std::string& value);

  const std::vector<std::byte>& bytes() const { return bytes_; }
  std::vector<std::byte> takeBytes() { return std::move(bytes_); }

 private:
  std::vector<std::byte> bytes_;
};

/// \ingroup karma_platform
/// Bounds-checked binary reader used by protocol and replication payloads.
class BinaryReader {
 public:
  explicit BinaryReader(std::span<const std::byte> bytes) : bytes_(bytes) {}

  bool readUInt8(uint8_t& value);
  bool readUInt16(uint16_t& value);
  bool readUInt32(uint32_t& value);
  bool readUInt64(uint64_t& value);
  bool readFloat32(float& value);
  bool readBytes(uint32_t size, std::vector<std::byte>& value);
  bool readString(std::string& value);

  std::span<const std::byte> remainingBytes() const;
  std::size_t remaining() const { return bytes_.size() - offset_; }
  bool exhausted() const { return offset_ == bytes_.size(); }

 private:
  bool readRaw(std::byte* out, std::size_t size);

  std::span<const std::byte> bytes_;
  std::size_t offset_ = 0;
};

/// \ingroup karma_platform
/// Encodes `header` and `payload` into one wire packet.
std::vector<std::byte> encodePacket(PacketHeader header,
                                    std::span<const std::byte> payload);

/// \ingroup karma_platform
/// Decodes a wire packet. Set `expected_app_id` to `0` to skip app id matching.
DecodeResult decodePacket(std::span<const std::byte> bytes,
                          uint32_t expected_app_id = 0,
                          uint16_t expected_version = kProtocolVersion);

}  // namespace karma::network


#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace karma::network {

/// \ingroup karma_platform
/// Stable transport peer handle exposed by client/server transports.
struct PeerId {
  uint32_t value = 0;

  static constexpr uint32_t kInvalidValue = 0;

  constexpr bool isValid() const { return value != kInvalidValue; }

  friend constexpr bool operator==(PeerId a, PeerId b) {
    return a.value == b.value;
  }

  friend constexpr bool operator!=(PeerId a, PeerId b) {
    return !(a == b);
  }

  friend constexpr bool operator<(PeerId a, PeerId b) {
    return a.value < b.value;
  }
};

/// \ingroup karma_platform
/// Transport channel index.
using ChannelId = uint8_t;

/// \ingroup karma_platform
/// Packet delivery mode.
enum class Delivery {
  Reliable,
  Unreliable
};

/// \ingroup karma_platform
/// Remote endpoint metadata known by the transport.
struct Endpoint {
  std::string ip;
  uint16_t port = 0;
};

/// \ingroup karma_platform
/// High-level reason associated with transport disconnect events.
enum class DisconnectReason {
  None,
  Local,
  Remote,
  Timeout,
  Kicked,
  TransportError,
  ProtocolError
};

/// \ingroup karma_platform
/// Result code for an outbound transport send.
enum class SendStatus {
  Ok,
  NotConnected,
  UnknownPeer,
  InvalidChannel,
  InvalidPayload,
  BackendError
};

/// \ingroup karma_platform
/// Result for an outbound transport send.
struct SendResult {
  SendStatus status = SendStatus::BackendError;
  std::size_t bytes_queued = 0;

  constexpr bool ok() const { return status == SendStatus::Ok; }
};

/// \ingroup karma_platform
/// Result code for client connection attempts.
enum class ConnectStatus {
  Connected,
  HostCreateFailed,
  ResolveFailed,
  NoAvailablePeer,
  Timeout,
  BackendError
};

/// \ingroup karma_platform
/// Result for a client connection attempt.
struct ConnectResult {
  ConnectStatus status = ConnectStatus::BackendError;
  PeerId peer{};
  Endpoint endpoint{};

  constexpr bool connected() const { return status == ConnectStatus::Connected; }
};

/// \ingroup karma_platform
/// Network transport event.
struct TransportEvent {
  enum class Type {
    Receive,
    Connect,
    Disconnect
  };

  Type type{};
  PeerId peer{};
  ChannelId channel = 0;
  DisconnectReason disconnect_reason = DisconnectReason::None;
  std::vector<std::byte> payload;
  Endpoint endpoint{};
};

/// \ingroup karma_platform
/// Client-side transport interface.
class IClientTransport {
 public:
  virtual ~IClientTransport() = default;

  virtual ConnectResult connect(const std::string& host, uint16_t port, int timeout_ms) = 0;
  virtual void disconnect(DisconnectReason reason = DisconnectReason::Local) = 0;
  virtual bool isConnected() const = 0;
  virtual PeerId serverPeer() const = 0;

  virtual void poll(std::vector<TransportEvent>& out_events) = 0;

  virtual SendResult send(ChannelId channel,
                          const std::byte* data,
                          std::size_t size,
                          Delivery delivery,
                          bool flush) = 0;
  virtual void flush() = 0;

  virtual std::optional<Endpoint> remoteEndpoint() const = 0;
};

/// \ingroup karma_platform
/// Server-side transport interface.
class IServerTransport {
 public:
  virtual ~IServerTransport() = default;

  virtual void poll(std::vector<TransportEvent>& out_events) = 0;

  virtual SendResult send(PeerId peer,
                          ChannelId channel,
                          const std::byte* data,
                          std::size_t size,
                          Delivery delivery,
                          bool flush) = 0;
  virtual void disconnect(PeerId peer,
                          DisconnectReason reason = DisconnectReason::Local) = 0;
  virtual void flush() = 0;
  virtual std::optional<Endpoint> endpoint(PeerId peer) const = 0;
  virtual std::vector<PeerId> peers() const = 0;
};

}  // namespace karma::network


#include <cstdint>
#include <memory>


namespace karma::network {

/// \ingroup karma_platform
/// Creates the configured default client transport.
std::unique_ptr<IClientTransport> createDefaultClientTransport();
/// Creates the configured default server transport.
std::unique_ptr<IServerTransport> createDefaultServerTransport(uint16_t port,
                                                               int max_clients = 50,
                                                               int num_channels = 2);

}  // namespace karma::network


#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>


namespace karma::network {

/// \ingroup karma_platform
/// Session-level peer state.
enum class SessionPeerState {
  TransportConnected,
  Connected
};

/// \ingroup karma_platform
/// Peer metadata tracked by `ServerSession`.
struct SessionPeer {
  PeerId id{};
  Endpoint endpoint{};
  SessionPeerState state = SessionPeerState::TransportConnected;
  uint32_t last_received_sequence = 0;
  uint32_t last_sent_sequence = 0;
  std::unordered_set<std::string> groups;
};

/// \ingroup karma_platform
/// Session event type emitted after transport polling and packet dispatch.
enum class SessionEventType {
  PeerConnected,
  PeerDisconnected,
  ProtocolError,
  CustomMessage,
  InputCommand,
  ReplicationMessage,
  Pong
};

/// \ingroup karma_platform
/// Session-level event.
struct SessionEvent {
  SessionEventType type = SessionEventType::CustomMessage;
  PeerId peer{};
  MessageType message_type = MessageType::CustomReliable;
  ChannelId channel = 0;
  DisconnectReason disconnect_reason = DisconnectReason::None;
  DecodeStatus decode_status = DecodeStatus::Ok;
  Endpoint endpoint{};
  uint32_t tick = 0;
  uint32_t sequence = 0;
  bool stale_sequence = false;
  std::vector<std::byte> payload;
};

/// \ingroup karma_platform
/// Aggregate result for sends that target several recipients.
struct MultiSendResult {
  std::size_t attempted = 0;
  std::size_t sent = 0;
  SendStatus first_error = SendStatus::Ok;

  bool ok() const { return attempted == sent; }
};

/// \ingroup karma_platform
/// Predicate used for explicit per-recipient session targeting.
using RecipientPredicate = std::function<bool(const SessionPeer&)>;

/// \ingroup karma_platform
/// Authoritative server session above a transport.
class ServerSession {
 public:
  explicit ServerSession(std::unique_ptr<IServerTransport> transport,
                         uint32_t app_id);

  IServerTransport* transport() { return transport_.get(); }
  const IServerTransport* transport() const { return transport_.get(); }
  uint32_t appId() const { return app_id_; }

  void poll(std::vector<SessionEvent>& out_events);
  void disconnect(PeerId peer, DisconnectReason reason = DisconnectReason::Local);
  void flush();

  SendResult sendTo(PeerId peer,
                    MessageType type,
                    std::span<const std::byte> payload,
                    Delivery delivery,
                    ChannelId channel,
                    uint32_t tick = 0);
  MultiSendResult broadcast(MessageType type,
                            std::span<const std::byte> payload,
                            Delivery delivery,
                            ChannelId channel,
                            uint32_t tick = 0);
  MultiSendResult sendWhere(const RecipientPredicate& predicate,
                            MessageType type,
                            std::span<const std::byte> payload,
                            Delivery delivery,
                            ChannelId channel,
                            uint32_t tick = 0);

  SendResult sendCustomTo(PeerId peer,
                          std::span<const std::byte> payload,
                          Delivery delivery,
                          ChannelId channel,
                          uint32_t tick = 0);
  MultiSendResult broadcastCustom(std::span<const std::byte> payload,
                                  Delivery delivery,
                                  ChannelId channel,
                                  uint32_t tick = 0);
  MultiSendResult sendCustomWhere(const RecipientPredicate& predicate,
                                  std::span<const std::byte> payload,
                                  Delivery delivery,
                                  ChannelId channel,
                                  uint32_t tick = 0);

  void addPeerToGroup(PeerId peer, const std::string& group);
  void removePeerFromGroup(PeerId peer, const std::string& group);
  bool peerInGroup(PeerId peer, const std::string& group) const;

  const SessionPeer* peer(PeerId peer) const;
  std::vector<PeerId> peers() const;

 private:
  SessionPeer* mutablePeer(PeerId peer);
  void handleTransportEvent(const TransportEvent& event,
                            std::vector<SessionEvent>& out_events);
  void handlePacket(SessionPeer& peer,
                    const TransportEvent& event,
                    Packet packet,
                    std::vector<SessionEvent>& out_events);
  SendResult sendPacket(SessionPeer& peer,
                        MessageType type,
                        std::span<const std::byte> payload,
                        Delivery delivery,
                        ChannelId channel,
                        uint32_t tick);
  void emitProtocolError(PeerId peer,
                         Endpoint endpoint,
                         DecodeStatus status,
                         std::vector<SessionEvent>& out_events);

  std::unique_ptr<IServerTransport> transport_;
  uint32_t app_id_ = 0;
  std::vector<TransportEvent> transport_events_;
  std::unordered_map<uint32_t, SessionPeer> peers_;
};

/// \ingroup karma_platform
/// Client session above a transport.
class ClientSession {
 public:
  explicit ClientSession(std::unique_ptr<IClientTransport> transport,
                         uint32_t app_id,
                         std::string client_name = {});

  IClientTransport* transport() { return transport_.get(); }
  const IClientTransport* transport() const { return transport_.get(); }
  uint32_t appId() const { return app_id_; }

  ConnectResult connect(const std::string& host, uint16_t port, int timeout_ms);
  void poll(std::vector<SessionEvent>& out_events);
  void disconnect(DisconnectReason reason = DisconnectReason::Local);
  void flush();

  bool isTransportConnected() const;
  bool isConnected() const { return connected_; }
  PeerId serverPeer() const;
  std::optional<Endpoint> remoteEndpoint() const;
  uint32_t lastReceivedSequence() const { return last_received_sequence_; }

  SendResult send(MessageType type,
                  std::span<const std::byte> payload,
                  Delivery delivery,
                  ChannelId channel,
                  uint32_t tick = 0);
  SendResult sendCustom(std::span<const std::byte> payload,
                        Delivery delivery,
                        ChannelId channel,
                        uint32_t tick = 0);
  SendResult sendInputCommand(std::span<const std::byte> payload,
                              uint32_t tick,
                              ChannelId channel = 1);

 private:
  void sendHandshake(uint32_t tick = 0);
  void handleTransportEvent(const TransportEvent& event,
                            std::vector<SessionEvent>& out_events);
  void handlePacket(const TransportEvent& event,
                    Packet packet,
                    std::vector<SessionEvent>& out_events);
  SendResult sendPacket(MessageType type,
                        std::span<const std::byte> payload,
                        Delivery delivery,
                        ChannelId channel,
                        uint32_t tick);
  void emitProtocolError(PeerId peer,
                         Endpoint endpoint,
                         DecodeStatus status,
                         std::vector<SessionEvent>& out_events);

  std::unique_ptr<IClientTransport> transport_;
  uint32_t app_id_ = 0;
  std::string client_name_;
  bool connected_ = false;
  uint32_t last_received_sequence_ = 0;
  uint32_t last_sent_sequence_ = 0;
  std::vector<TransportEvent> transport_events_;
};

}  // namespace karma::network


#include <memory>


namespace karma::network {

/// \ingroup karma_platform
/// Creates an ENet client transport.
std::unique_ptr<IClientTransport> createEnetClientTransport();
/// \ingroup karma_platform
/// Creates an ENet server transport.
std::unique_ptr<IServerTransport> createEnetServerTransport(uint16_t port,
                                                            int max_clients = 50,
                                                            int num_channels = 2);

}  // namespace karma::network



namespace karma::network {

/// \ingroup karma_features
/// Global network mode for process-level multiplayer behavior.
enum class NetworkRole {
  Offline,
  Server,
  Client,
  ListenServer
};

/// \ingroup karma_features
/// Returns true for roles that own server-side session and replication flow.
constexpr bool isServerRole(NetworkRole role) {
  return role == NetworkRole::Server || role == NetworkRole::ListenServer;
}

/// \ingroup karma_features
/// Returns true for roles that participate as a client endpoint.
constexpr bool isClientRole(NetworkRole role) {
  return role == NetworkRole::Client || role == NetworkRole::ListenServer;
}

/// \ingroup karma_features
/// Returns true for roles that should make authoritative simulation decisions.
constexpr bool isAuthorityRole(NetworkRole role) {
  return isServerRole(role);
}

/// \ingroup karma_features
/// Runtime role plus the local transport peer id when one has been assigned.
struct NetworkRoleContext {
  NetworkRole role = NetworkRole::Offline;
  network::PeerId local_peer{};

  constexpr bool isServer() const { return isServerRole(role); }
  constexpr bool isClient() const { return isClientRole(role); }
  constexpr bool isAuthority() const { return isAuthorityRole(role); }
};

}  // namespace karma::network


#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>


namespace karma::network {

/// \ingroup karma_features
/// Stable component type ids used by built-in v1 replicators.
inline constexpr uint32_t kTransformComponentWireId = 1;
inline constexpr uint32_t kTagComponentWireId = 2;

/// \ingroup karma_features
/// Per-component encode/apply hooks for binary replication.
struct ComponentReplicator {
  uint32_t component_type = 0;
  std::string name;
  components::AuthorityMode authority = components::AuthorityMode::Server;
  std::function<bool(const world::World&, world::Entity, network::BinaryWriter&)> encode_full;
  std::function<bool(const world::World&,
                     world::Entity,
                     std::span<const std::byte>,
                     network::BinaryWriter&)>
      encode_delta;
  std::function<bool(world::World&, world::Entity, network::BinaryReader&, bool)> decode_apply;
  std::function<bool(std::span<const std::byte>, std::span<const std::byte>)> is_dirty;
};

/// \ingroup karma_features
/// Registry for explicit component replication hooks.
class ComponentReplicationRegistry {
 public:
  bool registerReplicator(ComponentReplicator replicator);
  const ComponentReplicator* find(uint32_t component_type) const;
  bool contains(uint32_t component_type) const { return find(component_type) != nullptr; }

 private:
  std::unordered_map<uint32_t, ComponentReplicator> replicators_;
};

/// \ingroup karma_features
/// Registers built-in TransformComponent and TagComponent replicators.
void registerBuiltinReplicators(ComponentReplicationRegistry& registry);

/// \ingroup karma_features
/// Per-entity recipient visibility predicate used by server replication.
using ReplicationVisibilityPredicate =
    std::function<bool(const network::SessionPeer&,
                       world::Entity,
                       components::NetworkEntityId)>;

/// \ingroup karma_features
/// Callback invoked by networking runtime modules after session event polling.
using NetworkEventHandler = std::function<void(const network::SessionEvent&, world::World&)>;

/// \ingroup karma_features
/// Runtime-facing counters for network polling, replication, and rejection paths.
struct NetworkRuntimeStats {
  uint64_t events_received = 0;
  uint64_t protocol_errors = 0;
  uint64_t custom_messages = 0;
  uint64_t input_commands = 0;
  uint64_t replication_messages = 0;
  uint64_t replication_sends_attempted = 0;
  uint64_t replication_sends_succeeded = 0;
  uint64_t authority_rejects = 0;
  uint64_t stale_replication_rejects = 0;
};

/// \ingroup karma_features
/// Configuration for the authoritative server networking runtime module.
struct ServerNetworkRuntimeConfig {
  uint32_t app_id = 0;
  ReplicationVisibilityPredicate visibility;
  NetworkEventHandler event_handler;
  bool replication_enabled = true;
};

/// \ingroup karma_features
/// Configuration for the client networking runtime module.
struct ClientNetworkRuntimeConfig {
  uint32_t app_id = 0;
  NetworkEventHandler event_handler;
  bool replication_enabled = true;
};

/// \ingroup karma_features
/// Server-side replication state and last-sent dirty tracking.
class ServerReplicationState {
 public:
  void reset();
  void ensureNetworkIds(world::World& world);
  void removePeer(network::PeerId peer);

  network::MultiSendResult replicate(world::World& world,
                                 network::ServerSession& session,
                                 const ComponentReplicationRegistry& registry,
                                 uint32_t tick,
                                 const ReplicationVisibilityPredicate& visibility = {});

  bool sendDespawn(network::ServerSession& session,
                   components::NetworkEntityId network_id,
                   network::PeerId peer,
                   uint32_t tick);
  bool applyClientComponentEvent(world::World& world,
                                 const ComponentReplicationRegistry& registry,
                                 const network::SessionEvent& event,
                                 bool server_override = false);
  std::size_t authorityRejects() const { return authority_rejects_; }

 private:
  struct SentComponentKey {
    uint32_t peer = 0;
    components::NetworkEntityId entity = 0;
    uint32_t component = 0;

    friend bool operator==(const SentComponentKey& a, const SentComponentKey& b) {
      return a.peer == b.peer && a.entity == b.entity && a.component == b.component;
    }
  };

  struct SentComponentKeyHash {
    std::size_t operator()(const SentComponentKey& key) const;
  };

  bool hasSpawnedFor(components::NetworkEntityId id, network::PeerId peer) const;
  void markSpawnedFor(components::NetworkEntityId id, network::PeerId peer);
  void clearSpawnedFor(components::NetworkEntityId id, network::PeerId peer);
  std::vector<std::byte> encodeSpawn(world::World& world,
                                     world::Entity entity,
                                     const ComponentReplicationRegistry& registry,
                                     network::PeerId peer);
  std::optional<std::vector<std::byte>> encodeComponent(world::World& world,
                                                        world::Entity entity,
                                                        const ComponentReplicator& replicator,
                                                        std::span<const std::byte> previous,
                                                        bool delta);
  void rememberComponent(network::PeerId peer,
                         components::NetworkEntityId id,
                         uint32_t component_type,
                         std::span<const std::byte> bytes);

  components::NetworkEntityId next_id_ = 1;
  std::unordered_map<components::NetworkEntityId, std::unordered_set<uint32_t>> spawned_;
  std::unordered_map<SentComponentKey, std::vector<std::byte>, SentComponentKeyHash> last_sent_;
  std::size_t authority_rejects_ = 0;
};

/// \ingroup karma_features
/// Client-side replicated entity map and snapshot application.
class ClientReplicationState {
 public:
  void reset();
  bool applyEvent(world::World& world,
                  const ComponentReplicationRegistry& registry,
                  const network::SessionEvent& event);

  std::optional<world::Entity> entityFor(components::NetworkEntityId id) const;
  std::size_t staleRejects() const { return stale_rejects_; }

 private:
  struct ReplicationStamp {
    uint32_t tick = 0;
    uint32_t sequence = 0;
  };

  struct ComponentStampKey {
    components::NetworkEntityId entity = 0;
    uint32_t component = 0;

    friend bool operator==(const ComponentStampKey& a, const ComponentStampKey& b) {
      return a.entity == b.entity && a.component == b.component;
    }
  };

  struct ComponentStampKeyHash {
    std::size_t operator()(const ComponentStampKey& key) const;
  };

  bool applySpawn(world::World& world,
                  const ComponentReplicationRegistry& registry,
                  std::span<const std::byte> payload,
                  ReplicationStamp stamp);
  bool applyDespawn(world::World& world,
                    std::span<const std::byte> payload,
                    ReplicationStamp stamp);
  bool applyComponentUpdate(world::World& world,
                            const ComponentReplicationRegistry& registry,
                            std::span<const std::byte> payload,
                            ReplicationStamp stamp);
  bool applyAuthorityTransfer(world::World& world,
                              std::span<const std::byte> payload,
                              ReplicationStamp stamp);
  bool isEntityEventStale(components::NetworkEntityId id, ReplicationStamp stamp) const;
  bool isComponentEventStale(components::NetworkEntityId id,
                             uint32_t component_type,
                             ReplicationStamp stamp) const;
  void rememberEntityStamp(components::NetworkEntityId id, ReplicationStamp stamp);
  void rememberComponentStamp(components::NetworkEntityId id,
                              uint32_t component_type,
                              ReplicationStamp stamp);
  void forgetComponentStamps(components::NetworkEntityId id);

  std::unordered_map<components::NetworkEntityId, world::Entity> entities_;
  std::unordered_map<components::NetworkEntityId, ReplicationStamp> entity_stamps_;
  std::unordered_map<ComponentStampKey, ReplicationStamp, ComponentStampKeyHash>
      component_stamps_;
  std::size_t stale_rejects_ = 0;
};

/// \ingroup karma_features
/// Server runtime module that polls a session, dispatches events, replicates after
/// fixed simulation, and flushes at frame end.
class ServerNetworkRuntimeModule final : public app::RuntimeModule {
 public:
  using EventHandler = NetworkEventHandler;

  ServerNetworkRuntimeModule(network::ServerSession& session,
                             ComponentReplicationRegistry& registry);
  ServerNetworkRuntimeModule(network::ServerSession& session,
                             ComponentReplicationRegistry& registry,
                             ServerNetworkRuntimeConfig config);
  ServerNetworkRuntimeModule(std::unique_ptr<network::IServerTransport> transport,
                             ComponentReplicationRegistry& registry,
                             ServerNetworkRuntimeConfig config);

  network::ServerSession& session() { return *session_; }
  const network::ServerSession& session() const { return *session_; }
  ServerReplicationState& replicationState() { return replication_; }
  const ServerReplicationState& replicationState() const { return replication_; }
  const NetworkRuntimeStats& stats() const { return stats_; }
  void resetStats() { stats_ = {}; }
  void setEventHandler(EventHandler handler) { event_handler_ = std::move(handler); }
  void setVisibilityPredicate(ReplicationVisibilityPredicate predicate) {
    visibility_ = std::move(predicate);
  }
  void setReplicationEnabled(bool enabled) { replication_enabled_ = enabled; }
  bool replicationEnabled() const { return replication_enabled_; }

  void onAttach(const app::RuntimeModuleContext& context) override;
  void onFrameBegin(world::World& world, float dt) override;
  void onAfterFixedUpdate(world::World& world, float fixed_dt, uint64_t fixed_tick) override;
  void onFrameEnd(world::World& world) override;
  void onUpdate(world::World& world, float dt, float interpolation_alpha) override;

 private:
  std::unique_ptr<network::ServerSession> owned_session_;
  network::ServerSession* session_ = nullptr;
  ComponentReplicationRegistry& registry_;
  ServerReplicationState replication_;
  EventHandler event_handler_;
  ReplicationVisibilityPredicate visibility_;
  NetworkRuntimeStats stats_;
  bool replication_enabled_ = true;
  std::vector<network::SessionEvent> events_;
};

/// \ingroup karma_features
/// Client runtime module that polls a session, applies replication events before
/// simulation, and flushes at frame end.
class ClientNetworkRuntimeModule final : public app::RuntimeModule {
 public:
  using EventHandler = NetworkEventHandler;

  ClientNetworkRuntimeModule(network::ClientSession& session,
                             ComponentReplicationRegistry& registry);
  ClientNetworkRuntimeModule(network::ClientSession& session,
                             ComponentReplicationRegistry& registry,
                             ClientNetworkRuntimeConfig config);
  ClientNetworkRuntimeModule(std::unique_ptr<network::IClientTransport> transport,
                             ComponentReplicationRegistry& registry,
                             ClientNetworkRuntimeConfig config);

  network::ClientSession& session() { return *session_; }
  const network::ClientSession& session() const { return *session_; }
  ClientReplicationState& replicationState() { return replication_; }
  const ClientReplicationState& replicationState() const { return replication_; }
  const NetworkRuntimeStats& stats() const { return stats_; }
  void resetStats() { stats_ = {}; }
  void setEventHandler(EventHandler handler) { event_handler_ = std::move(handler); }
  void setReplicationEnabled(bool enabled) { replication_enabled_ = enabled; }
  bool replicationEnabled() const { return replication_enabled_; }

  void onAttach(const app::RuntimeModuleContext& context) override;
  void onFrameBegin(world::World& world, float dt) override;
  void onFrameEnd(world::World& world) override;
  void onUpdate(world::World& world, float dt, float interpolation_alpha) override;

 private:
  std::unique_ptr<network::ClientSession> owned_session_;
  network::ClientSession* session_ = nullptr;
  ComponentReplicationRegistry& registry_;
  ClientReplicationState replication_;
  EventHandler event_handler_;
  NetworkRuntimeStats stats_;
  bool replication_enabled_ = true;
  std::vector<network::SessionEvent> events_;
};

}  // namespace karma::network
