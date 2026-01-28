#include "karma/network/transport_factory.h"

#include "karma/network/enet_transport.h"

namespace karma::net {

std::unique_ptr<IClientTransport> createDefaultClientTransport() {
  return createEnetClientTransport();
}

std::unique_ptr<IServerTransport> createDefaultServerTransport(uint16_t port,
                                                               int max_clients,
                                                               int num_channels) {
  return createEnetServerTransport(port, max_clients, num_channels);
}

}  // namespace karma::net
