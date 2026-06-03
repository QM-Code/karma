#pragma once

#include <memory>

#include "karma/platform/network/transport.h"

namespace karma::net {

/// \ingroup karma_platform
/// Creates an ENet client transport.
std::unique_ptr<IClientTransport> createEnetClientTransport();
/// \ingroup karma_platform
/// Creates an ENet server transport.
std::unique_ptr<IServerTransport> createEnetServerTransport(uint16_t port,
                                                            int max_clients = 50,
                                                            int num_channels = 2);

}  // namespace karma::net
