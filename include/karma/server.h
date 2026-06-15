#pragma once

/// \file
/// Convenience umbrella header for the minimal Karma server profile.
///
/// This profile exposes ECS data, network transport/session/protocol APIs, and
/// the built-in networking feature layer without pulling in content, physics,
/// rendering, media, window, or application runtime libraries.

#include "karma/world/ecs/entity.h"
#include "karma/world/ecs/world.h"
#include "karma/world/components/network.h"
#include "karma/platform/network/protocol.h"
#include "karma/platform/network/session.h"
#include "karma/platform/network/transport.h"
#include "karma/platform/network/transport_factory.h"
#include "karma/features/network/component_replication.h"
#include "karma/features/network/role.h"
