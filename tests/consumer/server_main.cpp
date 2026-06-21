#include <karma/server.h>

int main() {
  static_assert(karma::network::isServerRole(karma::network::NetworkRole::Server));
  static_assert(karma::network::isClientRole(karma::network::NetworkRole::ListenServer));
  static_assert(karma::network::isAuthorityRole(karma::network::NetworkRole::ListenServer));

  karma::network::NetworkRoleContext context{
      .role = karma::network::NetworkRole::Server,
      .local_peer = karma::network::PeerId{1},
  };
  if (!context.isServer() || !context.isAuthority() || context.local_peer.value != 1) {
    return 1;
  }

  karma::network::ComponentReplicationRegistry registry;
  karma::network::registerBuiltinReplicators(registry);
  return registry.contains(karma::network::kTransformComponentWireId) ? 0 : 2;
}
