#pragma once

#include <cstdint>
#include <memory>

#include "karma/network/transport.h"

namespace karma::net {

std::unique_ptr<IClientTransport> createDefaultClientTransport();
std::unique_ptr<IServerTransport> createDefaultServerTransport(uint16_t port,
                                                               int max_clients = 50,
                                                               int num_channels = 2);

}  // namespace karma::net
