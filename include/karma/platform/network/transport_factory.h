#pragma once

#include <cstdint>
#include <memory>

#include "karma/platform/network/transport.h"

namespace karma::net {

/// \ingroup karma_platform
/// Creates the configured default client transport.
std::unique_ptr<IClientTransport> createDefaultClientTransport();
/// Creates the configured default server transport.
std::unique_ptr<IServerTransport> createDefaultServerTransport(uint16_t port,
                                                               int max_clients = 50,
                                                               int num_channels = 2);

}  // namespace karma::net
