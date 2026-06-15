#pragma once

#include "karma/platform/network/transport.h"

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
  net::PeerId local_peer{};

  constexpr bool isServer() const { return isServerRole(role); }
  constexpr bool isClient() const { return isClientRole(role); }
  constexpr bool isAuthority() const { return isAuthorityRole(role); }
};

}  // namespace karma::network
