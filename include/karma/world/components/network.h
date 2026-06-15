#pragma once

#include <cstdint>
#include <vector>

#include "karma/world/ecs/component.h"

namespace karma::components {

/// \ingroup karma_components
/// Stable network entity id used on the wire.
using NetworkEntityId = uint64_t;
/// \ingroup karma_components
/// Stable network peer id as stored in ECS data contracts.
using NetworkPeerId = uint32_t;

inline constexpr NetworkEntityId kInvalidNetworkEntityId = 0;
inline constexpr NetworkPeerId kInvalidNetworkPeerId = 0;

/// \ingroup karma_components
/// Stable identity for an entity that participates in network replication.
struct NetworkIdentityComponent : ecs::ComponentTag {
  NetworkEntityId id = kInvalidNetworkEntityId;
};

/// \ingroup karma_components
/// Explicit authority mode for networked component state.
enum class AuthorityMode : uint8_t {
  Server,
  Owner,
  Client,
  Custom
};

/// \ingroup karma_components
/// Ownership and override contract for networked entities.
struct NetworkAuthorityComponent : ecs::ComponentTag {
  AuthorityMode mode = AuthorityMode::Server;
  NetworkPeerId owner_peer = kInvalidNetworkPeerId;
  bool server_can_override = true;
};

/// \ingroup karma_components
/// Per-component replication policy.
enum class ReplicationPolicy : uint8_t {
  Snapshot,
  Delta,
  OwnerInput
};

/// \ingroup karma_components
/// One replicated component entry with a stable wire component type id.
struct ReplicatedComponentEntry {
  uint32_t component_type = 0;
  ReplicationPolicy policy = ReplicationPolicy::Snapshot;
};

/// \ingroup karma_components
/// Metadata listing component types replicated for an entity.
struct NetworkReplicatedComponent : ecs::ComponentTag {
  std::vector<ReplicatedComponentEntry> components;
  bool visible_by_default = true;
};

}  // namespace karma::components
