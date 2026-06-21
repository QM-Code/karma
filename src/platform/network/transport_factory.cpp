#include "karma/network.h"

#if defined(KARMA_NETWORK_BACKEND_ENET)
#include "karma/network.h"
#endif

namespace karma::network {

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

}  // namespace karma::network
