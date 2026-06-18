#include "karma/simulation/navigation/navigation_system.h"
#include "karma/simulation/navigation/nav_query.h"

#include <algorithm>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

#include "karma/core/time.h"
#include "karma/world/components/nav_mesh.h"
#include "karma/world/components/nav_mesh_agent.h"
#include "karma/world/components/transform.h"
#include "karma/world/ecs/world.h"
#include "detail/navigation_system_helpers.h"

namespace karma::navigation {
namespace {

using detail::entityKey;

struct PathJob {
  ecs::Entity agent_entity{};
  ecs::Entity nav_mesh_entity{};
  uint64_t request_id = 0;
  uint64_t nav_mesh_build_version = 0;
  std::shared_ptr<const NavMeshSnapshot> snapshot;
  math::Vec3 start{};
  math::Vec3 destination{};
  math::Vec3 search_extents{2.0f, 4.0f, 2.0f};
  NavQueryFilter filter{};
  core::SteadyClock::time_point submitted_at{};
  int max_points = 256;
};

struct PathResult {
  ecs::Entity agent_entity{};
  ecs::Entity nav_mesh_entity{};
  uint64_t request_id = 0;
  uint64_t nav_mesh_build_version = 0;
  double worker_queue_wait_ms = 0.0;
  double worker_solve_ms = 0.0;
  uint32_t path_point_count = 0;
  bool worker_cache_rebuilt = false;
  NavPath path{};
};

}  // namespace

using detail::failPathRequest;
using detail::findNavMesh;
using detail::hasActivePath;
using detail::navMeshUsable;
using detail::navSpacePosition;
using detail::NavMeshSelection;

struct NavigationSystem::WorkerState {
  WorkerState() {
    worker = std::thread(&WorkerState::run, this);
  }

  ~WorkerState() {
    {
      std::lock_guard<std::mutex> lock(mutex);
      stop = true;
    }
    cv.notify_one();
    if (worker.joinable()) {
      worker.join();
    }
  }

  void submit(PathJob job) {
    {
      std::lock_guard<std::mutex> lock(mutex);
      const uint64_t agent_key = entityKey(job.agent_entity);
      jobs.erase(std::remove_if(jobs.begin(),
                                jobs.end(),
                                [&](const PathJob& queued) {
                                  return entityKey(queued.agent_entity) == agent_key;
                                }),
                 jobs.end());
      jobs.push_back(std::move(job));
    }
    cv.notify_one();
  }

  std::vector<PathResult> takeCompleted() {
    std::lock_guard<std::mutex> lock(mutex);
    std::vector<PathResult> out;
    out.swap(completed);
    return out;
  }

 private:
  void run() {
    for (;;) {
      PathJob job;
      {
        std::unique_lock<std::mutex> lock(mutex);
        cv.wait(lock, [&] { return stop || !jobs.empty(); });
        if (stop && jobs.empty()) {
          break;
        }
        job = std::move(jobs.front());
        jobs.pop_front();
      }

      PathResult result;
      result.agent_entity = job.agent_entity;
      result.nav_mesh_entity = job.nav_mesh_entity;
      result.request_id = job.request_id;
      result.nav_mesh_build_version = job.nav_mesh_build_version;
      result.worker_queue_wait_ms =
          core::elapsedMilliseconds(job.submitted_at, core::SteadyClock::now());

      const auto solve_start = core::SteadyClock::now();
      if (job.snapshot == nullptr || !job.snapshot->valid()) {
        result.path.status = NavStatus::NoNavMesh;
      } else {
        if (cached_query == nullptr ||
            cached_snapshot.get() != job.snapshot.get() ||
            cached_nav_mesh_entity != job.nav_mesh_entity ||
            cached_nav_mesh_build_version != job.nav_mesh_build_version) {
          result.worker_cache_rebuilt = true;
          cached_snapshot = job.snapshot;
          cached_nav_mesh_entity = job.nav_mesh_entity;
          cached_nav_mesh_build_version = job.nav_mesh_build_version;
          cached_query = std::make_unique<NavQuery>(*cached_snapshot);
        }

        if (cached_query == nullptr || !cached_query->isValid()) {
          result.path.status = NavStatus::NoNavMesh;
        } else {
          result.path = cached_query->findPath(job.start,
                                               job.destination,
                                               job.search_extents,
                                               job.max_points,
                                               job.filter);
        }
      }
      result.worker_solve_ms =
          core::elapsedMilliseconds(solve_start, core::SteadyClock::now());
      result.path_point_count = static_cast<uint32_t>(result.path.points.size());

      {
        std::lock_guard<std::mutex> lock(mutex);
        completed.push_back(std::move(result));
      }
    }
  }

  std::mutex mutex;
  std::condition_variable cv;
  std::deque<PathJob> jobs;
  std::vector<PathResult> completed;
  std::shared_ptr<const NavMeshSnapshot> cached_snapshot;
  ecs::Entity cached_nav_mesh_entity{};
  uint64_t cached_nav_mesh_build_version = 0;
  std::unique_ptr<NavQuery> cached_query;
  bool stop = false;
  std::thread worker;
};

void NavigationSystem::submitPathRequests(ecs::World& world) {
  world.forEach<components::NavMeshAgentComponent, components::TransformComponent>(
      [&](ecs::Entity entity) {
        auto& agent = world.get<components::NavMeshAgentComponent>(entity);
        if (!agent.enabled || !agent.path_requested || !agent.has_destination) {
          return;
        }

        const NavMeshSelection nav_mesh = findNavMesh(world, agent.nav_mesh_entity);
        if (nav_mesh.component == nullptr) {
          failPathRequest(agent, NavStatus::NoNavMesh);
          ++stats_.failed_requests;
          stats_.last_path_status = NavStatus::NoNavMesh;
          return;
        }

        const std::shared_ptr<const NavMeshSnapshot> snapshot =
            nav_mesh.component->nav_mesh.snapshot();
        if (snapshot == nullptr || !snapshot->valid()) {
          failPathRequest(agent, NavStatus::NoNavMesh);
          ++stats_.failed_requests;
          stats_.last_path_status = NavStatus::NoNavMesh;
          return;
        }

        const auto& transform = world.get<components::TransformComponent>(entity);
        const uint64_t request_id = next_request_id_++;
        if (next_request_id_ == 0) {
          next_request_id_ = 1;
        }

        PathJob job;
        job.agent_entity = entity;
        job.nav_mesh_entity = nav_mesh.entity;
        job.request_id = request_id;
        job.nav_mesh_build_version = nav_mesh.component->build_version;
        job.snapshot = snapshot;
        job.start = navSpacePosition(transform.getPosition(), agent);
        job.destination = agent.destination;
        job.search_extents = agent.search_extents;
        job.filter = agent.query_filter;
        job.submitted_at = core::SteadyClock::now();

        const bool active_path = hasActivePath(agent);
        agent.path_requested = false;
        agent.path_pending = true;
        agent.path_resolved = false;
        agent.path_request_id = request_id;
        agent.last_path_status = NavStatus::InProgress;
        if (!active_path) {
          agent.current_path_partial = false;
          agent.status = components::NavMeshAgentStatus::PathPending;
          agent.current_velocity = {};
        }

        ++stats_.submitted_requests;
        ++stats_.pending_requests;
        stats_.last_request_id = request_id;
        stats_.last_path_status = NavStatus::InProgress;
        worker_->submit(std::move(job));
      });
}

void NavigationSystem::applyCompletedPaths(ecs::World& world) {
  std::vector<PathResult> results = worker_->takeCompleted();
  for (PathResult& result : results) {
    if (!world.isAlive(result.agent_entity) ||
        !world.has<components::NavMeshAgentComponent>(result.agent_entity)) {
      continue;
    }

    auto& agent = world.get<components::NavMeshAgentComponent>(result.agent_entity);
    if (!agent.path_pending || agent.path_request_id != result.request_id) {
      ++stats_.stale_results;
      continue;
    }

    if (stats_.pending_requests > 0) {
      --stats_.pending_requests;
    }
    ++stats_.completed_requests;
    stats_.last_request_id = result.request_id;
    stats_.last_worker_queue_wait_ms = result.worker_queue_wait_ms;
    stats_.last_worker_solve_ms = result.worker_solve_ms;
    stats_.last_worker_cache_rebuilt = result.worker_cache_rebuilt;
    stats_.last_path_point_count = result.path_point_count;
    stats_.last_path_status = result.path.status;

    if (!world.isAlive(result.nav_mesh_entity) ||
        !world.has<components::NavMeshComponent>(result.nav_mesh_entity)) {
      failPathRequest(agent, NavStatus::NoNavMesh);
      ++stats_.failed_requests;
      stats_.last_path_status = NavStatus::NoNavMesh;
      continue;
    }

    const auto& nav_mesh = world.get<components::NavMeshComponent>(result.nav_mesh_entity);
    if (!navMeshUsable(nav_mesh) ||
        nav_mesh.build_version != result.nav_mesh_build_version) {
      failPathRequest(agent, NavStatus::QueryFailed);
      ++stats_.failed_requests;
      stats_.last_path_status = NavStatus::QueryFailed;
      continue;
    }

    agent.last_path_status = result.path.status;
    agent.path_pending = false;
    if (!result.path.success() ||
        result.path.points.empty() ||
        (result.path.partial && !agent.accept_partial_paths)) {
      failPathRequest(agent, result.path.status);
      ++stats_.failed_requests;
      continue;
    }

    agent.path = std::move(result.path.points);
    agent.path_point_flags = std::move(result.path.point_flags);
    agent.next_waypoint = agent.path.size() > 1u ? 1u : 0u;
    agent.current_path_partial = result.path.partial;
    agent.path_resolved = true;
    agent.status = components::NavMeshAgentStatus::PathResolved;
  }
}

NavigationSystem::NavigationSystem(const content::AssetRegistry* assets)
    : worker_(std::make_unique<WorkerState>()), assets_(assets) {}

NavigationSystem::~NavigationSystem() = default;

}  // namespace karma::navigation
