#include "karma/platform/network/transport_factory.h"

#if defined(KARMA_NETWORK_BACKEND_ENET)
#include "karma/platform/network/enet_transport.h"
#endif

namespace karma::net {

std::unique_ptr<IClientTransport> createDefaultClientTransport() {
#if defined(KARMA_NETWORK_BACKEND_ENET)
  return createEnetClientTransport();
#else
  return nullptr;
#endif
}

std::unique_ptr<IServerTransport> createDefaultServerTransport(uint16_t port,
                                                               int max_clients,
                                                               int num_channels) {
#if defined(KARMA_NETWORK_BACKEND_ENET)
  return createEnetServerTransport(port, max_clients, num_channels);
#else
  (void)port;
  (void)max_clients;
  (void)num_channels;
  return nullptr;
#endif
}

}  // namespace karma::net
