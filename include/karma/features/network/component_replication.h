#pragma once

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

#include "karma/platform/network/protocol.h"
#include "karma/platform/network/session.h"
#include "karma/runtime/app/runtime_module.h"
#include "karma/world/components/network.h"
#include "karma/world/ecs/entity.h"
#include "karma/world/ecs/world.h"

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
  std::function<bool(const ecs::World&, ecs::Entity, net::BinaryWriter&)> encode_full;
  std::function<bool(const ecs::World&,
                     ecs::Entity,
                     std::span<const std::byte>,
                     net::BinaryWriter&)>
      encode_delta;
  std::function<bool(ecs::World&, ecs::Entity, net::BinaryReader&, bool)> decode_apply;
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
    std::function<bool(const net::SessionPeer&,
                       ecs::Entity,
                       components::NetworkEntityId)>;

/// \ingroup karma_features
/// Callback invoked by networking runtime modules after session event polling.
using NetworkEventHandler = std::function<void(const net::SessionEvent&, ecs::World&)>;

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
  void ensureNetworkIds(ecs::World& world);
  void removePeer(net::PeerId peer);

  net::MultiSendResult replicate(ecs::World& world,
                                 net::ServerSession& session,
                                 const ComponentReplicationRegistry& registry,
                                 uint32_t tick,
                                 const ReplicationVisibilityPredicate& visibility = {});

  bool sendDespawn(net::ServerSession& session,
                   components::NetworkEntityId network_id,
                   net::PeerId peer,
                   uint32_t tick);
  bool applyClientComponentEvent(ecs::World& world,
                                 const ComponentReplicationRegistry& registry,
                                 const net::SessionEvent& event,
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

  bool hasSpawnedFor(components::NetworkEntityId id, net::PeerId peer) const;
  void markSpawnedFor(components::NetworkEntityId id, net::PeerId peer);
  void clearSpawnedFor(components::NetworkEntityId id, net::PeerId peer);
  std::vector<std::byte> encodeSpawn(ecs::World& world,
                                     ecs::Entity entity,
                                     const ComponentReplicationRegistry& registry,
                                     net::PeerId peer);
  std::optional<std::vector<std::byte>> encodeComponent(ecs::World& world,
                                                        ecs::Entity entity,
                                                        const ComponentReplicator& replicator,
                                                        std::span<const std::byte> previous,
                                                        bool delta);
  void rememberComponent(net::PeerId peer,
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
  bool applyEvent(ecs::World& world,
                  const ComponentReplicationRegistry& registry,
                  const net::SessionEvent& event);

  std::optional<ecs::Entity> entityFor(components::NetworkEntityId id) const;
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

  bool applySpawn(ecs::World& world,
                  const ComponentReplicationRegistry& registry,
                  std::span<const std::byte> payload,
                  ReplicationStamp stamp);
  bool applyDespawn(ecs::World& world,
                    std::span<const std::byte> payload,
                    ReplicationStamp stamp);
  bool applyComponentUpdate(ecs::World& world,
                            const ComponentReplicationRegistry& registry,
                            std::span<const std::byte> payload,
                            ReplicationStamp stamp);
  bool applyAuthorityTransfer(ecs::World& world,
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

  std::unordered_map<components::NetworkEntityId, ecs::Entity> entities_;
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

  ServerNetworkRuntimeModule(net::ServerSession& session,
                             ComponentReplicationRegistry& registry);
  ServerNetworkRuntimeModule(net::ServerSession& session,
                             ComponentReplicationRegistry& registry,
                             ServerNetworkRuntimeConfig config);
  ServerNetworkRuntimeModule(std::unique_ptr<net::IServerTransport> transport,
                             ComponentReplicationRegistry& registry,
                             ServerNetworkRuntimeConfig config);

  net::ServerSession& session() { return *session_; }
  const net::ServerSession& session() const { return *session_; }
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
  void onFrameBegin(ecs::World& world, float dt) override;
  void onAfterFixedUpdate(ecs::World& world, float fixed_dt, uint64_t fixed_tick) override;
  void onFrameEnd(ecs::World& world) override;
  void onUpdate(ecs::World& world, float dt, float interpolation_alpha) override;

 private:
  std::unique_ptr<net::ServerSession> owned_session_;
  net::ServerSession* session_ = nullptr;
  ComponentReplicationRegistry& registry_;
  ServerReplicationState replication_;
  EventHandler event_handler_;
  ReplicationVisibilityPredicate visibility_;
  NetworkRuntimeStats stats_;
  bool replication_enabled_ = true;
  std::vector<net::SessionEvent> events_;
};

/// \ingroup karma_features
/// Client runtime module that polls a session, applies replication events before
/// simulation, and flushes at frame end.
class ClientNetworkRuntimeModule final : public app::RuntimeModule {
 public:
  using EventHandler = NetworkEventHandler;

  ClientNetworkRuntimeModule(net::ClientSession& session,
                             ComponentReplicationRegistry& registry);
  ClientNetworkRuntimeModule(net::ClientSession& session,
                             ComponentReplicationRegistry& registry,
                             ClientNetworkRuntimeConfig config);
  ClientNetworkRuntimeModule(std::unique_ptr<net::IClientTransport> transport,
                             ComponentReplicationRegistry& registry,
                             ClientNetworkRuntimeConfig config);

  net::ClientSession& session() { return *session_; }
  const net::ClientSession& session() const { return *session_; }
  ClientReplicationState& replicationState() { return replication_; }
  const ClientReplicationState& replicationState() const { return replication_; }
  const NetworkRuntimeStats& stats() const { return stats_; }
  void resetStats() { stats_ = {}; }
  void setEventHandler(EventHandler handler) { event_handler_ = std::move(handler); }
  void setReplicationEnabled(bool enabled) { replication_enabled_ = enabled; }
  bool replicationEnabled() const { return replication_enabled_; }

  void onAttach(const app::RuntimeModuleContext& context) override;
  void onFrameBegin(ecs::World& world, float dt) override;
  void onFrameEnd(ecs::World& world) override;
  void onUpdate(ecs::World& world, float dt, float interpolation_alpha) override;

 private:
  std::unique_ptr<net::ClientSession> owned_session_;
  net::ClientSession* session_ = nullptr;
  ComponentReplicationRegistry& registry_;
  ClientReplicationState replication_;
  EventHandler event_handler_;
  NetworkRuntimeStats stats_;
  bool replication_enabled_ = true;
  std::vector<net::SessionEvent> events_;
};

}  // namespace karma::network
