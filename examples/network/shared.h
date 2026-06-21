#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "karma/network.h"
#include "karma/network.h"
#include "karma/network.h"
#include "karma/world.h"
#include "karma/world.h"

namespace karma::examples::network_demo {

inline constexpr uint16_t kDefaultPort = 12345;
inline constexpr uint32_t kDemoAppId = 0x4B41524Du;  // KARM

struct PlayerInput {
  float dx = 0.0f;
  float dz = 0.0f;
};

std::span<const std::byte> asBytes(const std::string& text);
std::string payloadText(std::span<const std::byte> payload);

std::vector<std::byte> encodeInput(PlayerInput input);
bool decodeInput(std::span<const std::byte> bytes, PlayerInput& input);

world::Entity spawnReplicatedPlayer(world::World& world,
                                  network::PeerId peer,
                                  const std::string& name,
                                  float offset);

void logReplicatedWorld(const world::World& world);

}  // namespace karma::examples::network_demo
