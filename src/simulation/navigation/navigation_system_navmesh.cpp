#include "detail/navigation_system_helpers.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <memory>

#include <spdlog/spdlog.h>

#include "karma/core.h"
#include "karma/math.h"
#include "karma/navigation.h"
#include "karma/components.h"
#include "karma/world.h"
#include "detail/nav_cache.h"

namespace karma::navigation::detail {
namespace {

bool envFlagEnabled(const char* value) {
  if (value == nullptr || value[0] == '\0') {
    return false;
  }
  return std::strcmp(value, "0") != 0 &&
         std::strcmp(value, "false") != 0 &&
         std::strcmp(value, "FALSE") != 0 &&
         std::strcmp(value, "off") != 0 &&
         std::strcmp(value, "OFF") != 0;
}

bool startupDiagnosticsEnabled() {
  static const bool enabled = envFlagEnabled(std::getenv("KARMA_ENGINE_STARTUP_DIAG"));
  return enabled;
}

void logNavigationStartupDiag(world::Entity entity,
                              const char* stage,
                              core::SteadyClock::time_point start,
                              core::SteadyClock::time_point end) {
  if (!startupDiagnosticsEnabled()) {
    return;
  }
  spdlog::info("Engine startup diag: area=navigation_rebuild entity={}:{} stage={} ms={:.2f}",
               entity.index,
               entity.generation,
               stage ? stage : "unknown",
               core::elapsedMilliseconds(start, end));
}

void logNavigationStartupDiag(world::Entity entity,
                              const char* stage,
                              core::SteadyClock::time_point start,
                              core::SteadyClock::time_point end,
                              const NavMeshInputGeometry& geometry) {
  if (!startupDiagnosticsEnabled()) {
    return;
  }
  spdlog::info(
      "Engine startup diag: area=navigation_rebuild entity={}:{} stage={} ms={:.2f} vertices={} triangles={} off_mesh={} convex_volumes={}",
      entity.index,
      entity.generation,
      stage ? stage : "unknown",
      core::elapsedMilliseconds(start, end),
      geometry.vertices.size(),
      geometry.triangleCount(),
      geometry.off_mesh_connections.size(),
      geometry.convex_volumes.size());
}

void recordCacheHit(NavigationSystemStats* stats) {
  if (stats == nullptr) {
    return;
  }
  stats->last_cache_hit = true;
  stats->last_cache_miss = false;
  ++stats->cache_hits;
}

void recordCacheMiss(NavigationSystemStats* stats) {
  if (stats == nullptr) {
    return;
  }
  stats->last_cache_hit = false;
  stats->last_cache_miss = true;
  ++stats->cache_misses;
}

void recordCacheWrite(NavigationSystemStats* stats) {
  if (stats == nullptr) {
    return;
  }
  stats->last_cache_write = true;
  ++stats->cache_writes;
}

}  // namespace

void rebuildNavMeshes(world::World& world,
                      const assets::AssetRegistry* assets,
                      NavigationSystemStats* stats) {
  if (stats != nullptr) {
    stats->last_cache_read_ms = 0.0;
    stats->last_cache_write_ms = 0.0;
    stats->last_cache_hit = false;
    stats->last_cache_miss = false;
    stats->last_cache_write = false;
  }

  world.forEach<components::NavMeshComponent>([&](world::Entity entity) {
    auto& nav_mesh = world.get<components::NavMeshComponent>(entity);
    if (!nav_mesh.enabled) {
      return;
    }

    components::NavTileCacheComponent* tile_cache = nullptr;
    if (world.has<components::NavTileCacheComponent>(entity)) {
      tile_cache = &world.get<components::NavTileCacheComponent>(entity);
    }

    const bool should_build = nav_mesh.rebuild_requested ||
                              (nav_mesh.build_on_start && !nav_mesh.built);
    const bool should_build_cache =
        tile_cache != nullptr &&
        tile_cache->enabled &&
        (tile_cache->rebuild_requested ||
         (tile_cache->build_on_start && !tile_cache->built));
    if (!should_build && !should_build_cache) {
      return;
    }

    const auto rebuild_start = core::SteadyClock::now();
    incrementBuildVersion(nav_mesh);

    auto stage_start = rebuild_start;
    const NavMeshInputGeometry geometry =
        collectNavMeshGeometry(world, assets, nav_mesh.source_mask);
    logNavigationStartupDiag(entity,
                             "collect geometry",
                             stage_start,
                             core::SteadyClock::now(),
                             geometry);
    const bool force_build_debug_draw = nav_mesh.build_debug_draw_requested;
    const bool cache_enabled = nav_mesh.cache.enabled &&
                               !force_build_debug_draw &&
                               navCacheConfig().enabled;
    NavMeshBuildConfig effective_config = cache_enabled
        ? cacheEffectiveConfig(nav_mesh.build_config)
        : nav_mesh.build_config;
    if (force_build_debug_draw) {
      effective_config.collect_build_debug_draw = true;
    }

    if (cache_enabled && nav_mesh.cache.read) {
      const auto read_start = core::SteadyClock::now();
      bool loaded_from_cache = false;
      std::string diagnostic;
      if (tile_cache != nullptr && tile_cache->enabled) {
        const std::string fingerprint =
            navTileCacheFingerprint(geometry,
                                    nav_mesh.source_mask,
                                    effective_config,
                                    tile_cache->build_config);
        NavTileCacheSnapshot snapshot;
        NavTileCacheBuildResult cache_result;
        if (readNavTileCache(navTileCachePath(fingerprint), snapshot, &diagnostic) &&
            tile_cache->tile_cache.loadSnapshot(nav_mesh.nav_mesh, snapshot, &cache_result)) {
          tile_cache->last_build_result = cache_result;
          tile_cache->rebuild_requested = false;
          tile_cache->updates_pending = false;
          tile_cache->built = true;
          nav_mesh.built = nav_mesh.nav_mesh.isValid();
          nav_mesh.last_build_result = nav_mesh.nav_mesh.lastBuildResult();
          nav_mesh.rebuild_requested = false;
          invalidateObstacleRefsForCache(world, entity);
          loaded_from_cache = nav_mesh.built;
        }
      } else {
        const std::string fingerprint =
            navMeshCacheFingerprint(geometry, nav_mesh.source_mask, effective_config);
        NavMeshSnapshot snapshot;
        NavMeshSnapshotMetadata metadata;
        NavMeshBuildResult result;
        if (readNavMeshCache(navMeshCachePath(fingerprint), snapshot, metadata, &diagnostic) &&
            nav_mesh.nav_mesh.loadSnapshot(snapshot, metadata, &result)) {
          nav_mesh.last_build_result = result;
          nav_mesh.rebuild_requested = false;
          nav_mesh.built = true;
          loaded_from_cache = true;
        }
      }
      const auto read_end = core::SteadyClock::now();
      if (stats != nullptr) {
        stats->last_cache_read_ms = core::elapsedMilliseconds(read_start, read_end);
      }
      if (loaded_from_cache) {
        recordCacheHit(stats);
        logNavigationStartupDiag(entity, "nav cache hit", read_start, read_end);
        logNavigationStartupDiag(entity, "total", rebuild_start, core::SteadyClock::now());
        return;
      }
      recordCacheMiss(stats);
      logNavigationStartupDiag(entity, "nav cache miss", read_start, read_end);
    }

    if (tile_cache != nullptr && tile_cache->enabled) {
      NavTileCacheBuildResult cache_result;
      stage_start = core::SteadyClock::now();
      tile_cache->built = tile_cache->tile_cache.build(nav_mesh.nav_mesh,
                                                       geometry,
                                                       effective_config,
                                                       tile_cache->build_config,
                                                       &cache_result);
      logNavigationStartupDiag(entity, "tile cache build", stage_start, core::SteadyClock::now());
      tile_cache->last_build_result = cache_result;
      tile_cache->rebuild_requested = false;
      tile_cache->updates_pending = false;
      nav_mesh.built = tile_cache->built && nav_mesh.nav_mesh.isValid();
      nav_mesh.last_build_result = nav_mesh.nav_mesh.lastBuildResult();
      if (!nav_mesh.built) {
        nav_mesh.last_build_result.status = cache_result.status;
        nav_mesh.last_build_result.message = cache_result.message;
      }
      invalidateObstacleRefsForCache(world, entity);
      if (cache_enabled && nav_mesh.cache.write && tile_cache->built) {
        const auto write_start = core::SteadyClock::now();
        const std::string fingerprint =
            navTileCacheFingerprint(geometry,
                                    nav_mesh.source_mask,
                                    effective_config,
                                    tile_cache->build_config);
        const NavTileCacheSnapshot snapshot = tile_cache->tile_cache.snapshot(nav_mesh.nav_mesh);
        std::string diagnostic;
        if (writeNavTileCache(navTileCachePath(fingerprint), snapshot, &diagnostic)) {
          recordCacheWrite(stats);
        }
        const auto write_end = core::SteadyClock::now();
        if (stats != nullptr) {
          stats->last_cache_write_ms = core::elapsedMilliseconds(write_start, write_end);
        }
        logNavigationStartupDiag(entity, "nav cache write", write_start, write_end);
      }
    } else {
      NavMeshBuildResult result;
      stage_start = core::SteadyClock::now();
      nav_mesh.built = nav_mesh.nav_mesh.build(geometry, effective_config, &result);
      logNavigationStartupDiag(entity, "nav mesh build", stage_start, core::SteadyClock::now());
      nav_mesh.last_build_result = result;
      if (cache_enabled && nav_mesh.cache.write && nav_mesh.built) {
        const auto write_start = core::SteadyClock::now();
        const std::string fingerprint =
            navMeshCacheFingerprint(geometry, nav_mesh.source_mask, effective_config);
        const std::shared_ptr<const NavMeshSnapshot> snapshot = nav_mesh.nav_mesh.snapshot();
        std::string diagnostic;
        if (snapshot != nullptr &&
            writeNavMeshCache(navMeshCachePath(fingerprint),
                              *snapshot,
                              makeSnapshotMetadata(nav_mesh.nav_mesh),
                              &diagnostic)) {
          recordCacheWrite(stats);
        }
        const auto write_end = core::SteadyClock::now();
        if (stats != nullptr) {
          stats->last_cache_write_ms = core::elapsedMilliseconds(write_start, write_end);
        }
        logNavigationStartupDiag(entity, "nav cache write", write_start, write_end);
      }
    }
    nav_mesh.rebuild_requested = false;
    nav_mesh.build_debug_draw_requested = false;
    logNavigationStartupDiag(entity, "total", rebuild_start, core::SteadyClock::now());
  });
}

void moveAgents(world::World& world, float dt) {
  if (dt <= 0.0f) {
    return;
  }

  world.forEach<components::NavMeshAgentComponent, components::TransformComponent>(
      [&](world::Entity entity) {
        auto& agent = world.get<components::NavMeshAgentComponent>(entity);
        if (!agent.enabled ||
            agent.path.empty() ||
            agent.next_waypoint >= agent.path.size() ||
            agent.speed <= 0.0f) {
          agent.current_velocity = {};
          return;
        }

        auto& transform = world.get<components::TransformComponent>(entity);
        const math::Vec3 previous_world = transform.getPosition();
        math::Vec3 current = navSpacePosition(previous_world, agent);
        float remaining = agent.speed * dt;
        while (remaining > 0.0f && agent.next_waypoint < agent.path.size()) {
          const math::Vec3 target = agent.path[agent.next_waypoint];
          const math::Vec3 delta = math::subtract(target, current);
          const float distance = math::length(delta);
          if (distance <= agent.stopping_distance) {
            current = target;
            ++agent.next_waypoint;
            continue;
          }

          const float step = std::min(remaining, distance);
          current = math::add(current, math::scale(delta, step / distance));
          remaining -= step;
          if (step < distance) {
            break;
          }
        }

        const math::Vec3 next_world = worldSpacePosition(current, agent, previous_world.y);
        transform.setPosition(next_world);
        agent.current_velocity =
            math::scale(math::subtract(next_world, previous_world), 1.0f / dt);

        if (agent.next_waypoint >= agent.path.size()) {
          agent.status = agent.current_path_partial
              ? components::NavMeshAgentStatus::PartialPath
              : components::NavMeshAgentStatus::Arrived;
          agent.path_resolved = false;
          agent.current_velocity = {};
        } else {
          agent.status = components::NavMeshAgentStatus::Moving;
          agent.path_resolved = false;
        }
      });
}

}  // namespace karma::navigation::detail
