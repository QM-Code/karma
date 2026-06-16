#include "recast_navigation_sample_app.h"

#include "recast_navigation_demo_data.h"
#include "scene_helpers.h"

#include <algorithm>
#include <cstddef>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>
#include <imgui.h>
#include <spdlog/spdlog.h>

#include "karma/features/ui/imgui/imgui_layer.h"
#include "karma/karma.h"
#include "karma/rendering/renderer/camera_picking.h"
#include "karma/simulation/navigation/nav_geometry.h"
#include "karma/world/components/mesh.h"
#include "karma/world/components/nav_crowd.h"
#include "karma/world/components/nav_mesh.h"
#include "karma/world/components/nav_mesh_agent.h"
#include "karma/world/components/nav_tile_cache.h"
#include "karma/world/components/transform.h"

namespace karma::demo {
namespace {

constexpr float kGroundY = 0.0f;

enum class ToolKind {
  NavmeshTester,
  NavmeshPrune,
  TileEdit,
  TileHighlight,
  TempObstacle,
  OffMeshConnection,
  ConvexVolume,
  Crowd,
};

enum class NavmeshTesterMode {
  PathfindFollow,
  PathfindStraight,
  PathfindSliced,
  DistanceToWall,
  Raycast,
  FindPolysInCircle,
  FindPolysInShape,
  FindLocalNeighbourhood,
};

enum class CrowdToolMode {
  CreateAgents,
  MoveTarget,
  SelectAgent,
  TogglePolys,
};

struct ScreenSegment {
  math::Vec3 start{};
  math::Vec3 end{};
};

struct AgentTrail {
  static constexpr size_t kMaxPoints = 64;
  std::vector<math::Vec3> points;
};

struct OffMeshLinkRecord {
  ecs::Entity start{};
  ecs::Entity end{};
};

struct ConvexVolumeRecord {
  ecs::Entity entity{};
  std::vector<math::Vec3> vertices;
  float min_y = 0.0f;
  float max_y = 0.0f;
};

static constexpr unsigned char kSampleAreaGround = navigation::kNavAreaDefault;
static constexpr unsigned char kSampleAreaWater = 2;
static constexpr unsigned char kSampleAreaRoad = 3;
static constexpr unsigned char kSampleAreaDoor = 4;
static constexpr unsigned char kSampleAreaGrass = 5;
static constexpr unsigned char kSampleAreaJump = 6;

static constexpr uint16_t kSamplePolyFlagWalk = navigation::kNavPolyFlagWalk;
static constexpr uint16_t kSamplePolyFlagSwim = 1u << 1u;
static constexpr uint16_t kSamplePolyFlagDoor = 1u << 2u;
static constexpr uint16_t kSamplePolyFlagJump = 1u << 3u;
static constexpr uint16_t kSamplePolyFlagDisabled = 1u << 4u;
static constexpr uint16_t kSamplePolyFlagsAll = 0xffffu;

struct DebugModeOption {
  const char* label = "";
  navigation::NavMeshDebugDrawMode mode = navigation::NavMeshDebugDrawMode::NavMeshEdges;
  bool mesh_only = false;
};

const DebugModeOption kSoloDebugModes[] = {
    {"Input Mesh", navigation::NavMeshDebugDrawMode::NavMeshEdges, true},
    {"Navmesh", navigation::NavMeshDebugDrawMode::NavMesh},
    {"Navmesh Trans", navigation::NavMeshDebugDrawMode::NavMesh},
    {"Navmesh BVTree", navigation::NavMeshDebugDrawMode::NavMeshBVTree},
    {"Voxels", navigation::NavMeshDebugDrawMode::Voxels},
    {"Walkable Voxels", navigation::NavMeshDebugDrawMode::WalkableVoxels},
    {"Compact", navigation::NavMeshDebugDrawMode::Compact},
    {"Compact Distance", navigation::NavMeshDebugDrawMode::CompactDistance},
    {"Compact Regions", navigation::NavMeshDebugDrawMode::CompactRegions},
    {"Region Connections", navigation::NavMeshDebugDrawMode::RegionConnections},
    {"Raw Contours", navigation::NavMeshDebugDrawMode::RawContours},
    {"Both Contours", navigation::NavMeshDebugDrawMode::BothContours},
    {"Contours", navigation::NavMeshDebugDrawMode::Contours},
    {"Poly Mesh", navigation::NavMeshDebugDrawMode::PolyMesh},
    {"Poly Mesh Detail", navigation::NavMeshDebugDrawMode::PolyMeshDetail},
};

const DebugModeOption kTileDebugModes[] = {
    {"Input Mesh", navigation::NavMeshDebugDrawMode::NavMeshEdges, true},
    {"Navmesh", navigation::NavMeshDebugDrawMode::NavMesh},
    {"Navmesh Trans", navigation::NavMeshDebugDrawMode::NavMesh},
    {"Navmesh BVTree", navigation::NavMeshDebugDrawMode::NavMeshBVTree},
    {"Navmesh Portals", navigation::NavMeshDebugDrawMode::NavMeshPortals},
    {"Voxels", navigation::NavMeshDebugDrawMode::Voxels},
    {"Walkable Voxels", navigation::NavMeshDebugDrawMode::WalkableVoxels},
    {"Compact", navigation::NavMeshDebugDrawMode::Compact},
    {"Compact Distance", navigation::NavMeshDebugDrawMode::CompactDistance},
    {"Compact Regions", navigation::NavMeshDebugDrawMode::CompactRegions},
    {"Region Connections", navigation::NavMeshDebugDrawMode::RegionConnections},
    {"Raw Contours", navigation::NavMeshDebugDrawMode::RawContours},
    {"Both Contours", navigation::NavMeshDebugDrawMode::BothContours},
    {"Contours", navigation::NavMeshDebugDrawMode::Contours},
    {"Poly Mesh", navigation::NavMeshDebugDrawMode::PolyMesh},
    {"Poly Mesh Detail", navigation::NavMeshDebugDrawMode::PolyMeshDetail},
};

const DebugModeOption kTempObstacleDebugModes[] = {
    {"Input Mesh", navigation::NavMeshDebugDrawMode::NavMeshEdges, true},
    {"Navmesh", navigation::NavMeshDebugDrawMode::NavMesh},
    {"Navmesh Trans", navigation::NavMeshDebugDrawMode::NavMesh},
    {"Navmesh BVTree", navigation::NavMeshDebugDrawMode::NavMeshBVTree},
    {"Navmesh Portals", navigation::NavMeshDebugDrawMode::NavMeshPortals},
    {"Cache Bounds", navigation::NavMeshDebugDrawMode::NavMeshEdges},
};

const char* toolName(ToolKind tool) {
  switch (tool) {
    case ToolKind::NavmeshTester: return "Test Navmesh";
    case ToolKind::NavmeshPrune: return "Prune Navmesh";
    case ToolKind::TileEdit: return "Create Tiles";
    case ToolKind::TileHighlight: return "Highlight Tile Cache";
    case ToolKind::TempObstacle: return "Create Temp Obstacles";
    case ToolKind::OffMeshConnection: return "Create Off-Mesh Connections";
    case ToolKind::ConvexVolume: return "Create Convex Volumes";
    case ToolKind::Crowd: return "Create Crowds";
    default: return "Unknown";
  }
}

const char* testerModeName(NavmeshTesterMode mode) {
  switch (mode) {
    case NavmeshTesterMode::PathfindFollow: return "Pathfind Follow";
    case NavmeshTesterMode::PathfindStraight: return "Pathfind Straight";
    case NavmeshTesterMode::PathfindSliced: return "Pathfind Sliced";
    case NavmeshTesterMode::DistanceToWall: return "Distance to Wall";
    case NavmeshTesterMode::Raycast: return "Raycast";
    case NavmeshTesterMode::FindPolysInCircle: return "Find Polys in Circle";
    case NavmeshTesterMode::FindPolysInShape: return "Find Polys in Shape";
    case NavmeshTesterMode::FindLocalNeighbourhood: return "Find Local Neighbourhood";
    default: return "Tester";
  }
}

const char* crowdModeName(CrowdToolMode mode) {
  switch (mode) {
    case CrowdToolMode::CreateAgents: return "Create Agents";
    case CrowdToolMode::MoveTarget: return "Move Target";
    case CrowdToolMode::SelectAgent: return "Select Agent";
    case CrowdToolMode::TogglePolys: return "Toggle Polys";
    default: return "Crowd";
  }
}

const char* areaName(unsigned char area) {
  switch (area) {
    case kSampleAreaGround: return "Ground";
    case kSampleAreaWater: return "Water";
    case kSampleAreaRoad: return "Road";
    case kSampleAreaDoor: return "Door";
    case kSampleAreaGrass: return "Grass";
    case kSampleAreaJump: return "Jump";
    default: return "Custom";
  }
}

bool hasFlag(uint16_t flags, uint16_t flag) {
  return (flags & flag) != 0;
}

void toggleFlag(uint16_t& flags, uint16_t flag) {
  flags = static_cast<uint16_t>(flags ^ flag);
}

bool intersectSegmentAabb(const math::Vec3& start,
                          const math::Vec3& end,
                          const math::Vec3& bounds_min,
                          const math::Vec3& bounds_max,
                          float& out_t) {
  constexpr float kEpsilon = 1.0e-6f;
  float tmin = 0.0f;
  float tmax = 1.0f;
  const float s[3] = {start.x, start.y, start.z};
  const float e[3] = {end.x, end.y, end.z};
  const float mn[3] = {bounds_min.x, bounds_min.y, bounds_min.z};
  const float mx[3] = {bounds_max.x, bounds_max.y, bounds_max.z};
  for (int axis = 0; axis < 3; ++axis) {
    const float delta = e[axis] - s[axis];
    if (std::abs(delta) < kEpsilon) {
      if (s[axis] < mn[axis] || s[axis] > mx[axis]) {
        return false;
      }
      continue;
    }
    const float inv_delta = 1.0f / delta;
    float near_t = (mn[axis] - s[axis]) * inv_delta;
    float far_t = (mx[axis] - s[axis]) * inv_delta;
    if (near_t > far_t) {
      std::swap(near_t, far_t);
    }
    tmin = std::max(tmin, near_t);
    tmax = std::min(tmax, far_t);
    if (tmin > tmax) {
      return false;
    }
  }
  out_t = tmin;
  return true;
}

float horizontalDistanceSquared(const math::Vec3& a, const math::Vec3& b) {
  const float dx = a.x - b.x;
  const float dz = a.z - b.z;
  return dx * dx + dz * dz;
}

bool pointInPolyXZ(const std::vector<math::Vec3>& vertices, const math::Vec3& point) {
  bool inside = false;
  for (size_t i = 0, j = vertices.empty() ? 0 : vertices.size() - 1; i < vertices.size(); j = i++) {
    const math::Vec3& vi = vertices[i];
    const math::Vec3& vj = vertices[j];
    const bool crosses = (vi.z > point.z) != (vj.z > point.z);
    if (crosses) {
      const float x = (vj.x - vi.x) * (point.z - vi.z) / (vj.z - vi.z) + vi.x;
      if (point.x < x) {
        inside = !inside;
      }
    }
  }
  return inside;
}

std::vector<math::Vec3> convexHullXZ(const std::vector<math::Vec3>& points) {
  if (points.size() < 3) {
    return points;
  }
  size_t hull = 0;
  for (size_t i = 1; i < points.size(); ++i) {
    if (points[i].x < points[hull].x ||
        (points[i].x == points[hull].x && points[i].z < points[hull].z)) {
      hull = i;
    }
  }

  std::vector<math::Vec3> out;
  size_t current = hull;
  do {
    out.push_back(points[current]);
    size_t endpoint = 0;
    for (size_t i = 1; i < points.size(); ++i) {
      const math::Vec3& a = points[current];
      const math::Vec3& b = points[endpoint];
      const math::Vec3& c = points[i];
      const float cross = (b.x - a.x) * (c.z - a.z) - (b.z - a.z) * (c.x - a.x);
      if (endpoint == current || cross < 0.0f) {
        endpoint = i;
      }
    }
    current = endpoint;
  } while (current != hull && out.size() <= points.size());
  return out;
}

std::vector<math::Vec3> offsetPolyFromCentroid(const std::vector<math::Vec3>& vertices,
                                               float offset) {
  if (vertices.empty() || offset <= 0.01f) {
    return vertices;
  }
  math::Vec3 center{};
  for (const math::Vec3& point : vertices) {
    center = math::add(center, point);
  }
  center = math::scale(center, 1.0f / static_cast<float>(vertices.size()));

  std::vector<math::Vec3> out;
  out.reserve(vertices.size());
  for (const math::Vec3& point : vertices) {
    math::Vec3 dir{point.x - center.x, 0.0f, point.z - center.z};
    const float len = std::sqrt(dir.x * dir.x + dir.z * dir.z);
    if (len > 0.0001f) {
      dir.x /= len;
      dir.z /= len;
    }
    out.push_back({point.x + dir.x * offset, point.y, point.z + dir.z * offset});
  }
  return out;
}

void drawCircle(renderer::GraphicsDevice& graphics,
                const math::Vec3& center,
                float radius,
                const math::Color& color,
                bool depth_test,
                float thickness = 1.0f) {
  constexpr int kSegments = 36;
  math::Vec3 prev{center.x + radius, center.y, center.z};
  for (int i = 1; i <= kSegments; ++i) {
    const float a = static_cast<float>(i) * 6.28318530718f / static_cast<float>(kSegments);
    const math::Vec3 next{center.x + std::cos(a) * radius, center.y, center.z + std::sin(a) * radius};
    graphics.drawLine(prev, next, color, depth_test, thickness);
    prev = next;
  }
}

void drawCross(renderer::GraphicsDevice& graphics,
               const math::Vec3& center,
               float size,
               const math::Color& color,
               bool depth_test,
               float thickness = 1.0f) {
  graphics.drawLine({center.x - size, center.y, center.z},
                    {center.x + size, center.y, center.z},
                    color,
                    depth_test,
                    thickness);
  graphics.drawLine({center.x, center.y, center.z - size},
                    {center.x, center.y, center.z + size},
                    color,
                    depth_test,
                    thickness);
}

const char* meshForSample(RecastNavigationSampleKind kind) {
  switch (kind) {
    case RecastNavigationSampleKind::TempObstacles: return "dungeon.obj";
    case RecastNavigationSampleKind::Debug: return "undulating.obj";
    case RecastNavigationSampleKind::SoloMesh:
    case RecastNavigationSampleKind::TileMesh:
    default: return "nav_test.obj";
  }
}

math::Color tintForSample(RecastNavigationSampleKind kind) {
  switch (kind) {
    case RecastNavigationSampleKind::SoloMesh: return {0.56f, 0.68f, 0.82f, 1.0f};
    case RecastNavigationSampleKind::TileMesh: return {0.54f, 0.76f, 0.58f, 1.0f};
    case RecastNavigationSampleKind::TempObstacles: return {0.72f, 0.62f, 0.52f, 1.0f};
    case RecastNavigationSampleKind::Debug: return {0.68f, 0.58f, 0.82f, 1.0f};
    default: return {0.6f, 0.6f, 0.6f, 1.0f};
  }
}

std::string snapshotFileForSample(RecastNavigationSampleKind kind) {
  switch (kind) {
    case RecastNavigationSampleKind::SoloMesh: return "solo_navmesh.bin";
    case RecastNavigationSampleKind::TileMesh: return "all_tiles_navmesh.bin";
    case RecastNavigationSampleKind::TempObstacles: return "all_tiles_tilecache_state.txt";
    case RecastNavigationSampleKind::Debug: return "debug_navmesh.bin";
    default: return "navmesh.bin";
  }
}

navigation::NavMeshPartitionType partitionFromIndex(int index) {
  switch (index) {
    case 1: return navigation::NavMeshPartitionType::Monotone;
    case 2: return navigation::NavMeshPartitionType::Layers;
    case 0:
    default: return navigation::NavMeshPartitionType::Watershed;
  }
}

int partitionIndex(navigation::NavMeshPartitionType partition) {
  switch (partition) {
    case navigation::NavMeshPartitionType::Monotone: return 1;
    case navigation::NavMeshPartitionType::Layers: return 2;
    case navigation::NavMeshPartitionType::Watershed:
    default: return 0;
  }
}

navigation::NavQueryFilter filterFor(const QueryCase& query) {
  navigation::NavQueryFilter filter;
  filter.include_flags = query.include_flags;
  filter.exclude_flags = query.exclude_flags;
  return filter;
}

math::Vec3 yOffset(const math::Vec3& point, float y) {
  return {point.x, point.y + y, point.z};
}

uint64_t entityKey(ecs::Entity entity) {
  return (static_cast<uint64_t>(entity.index) << 32u) |
         static_cast<uint64_t>(entity.generation);
}

}  // namespace

const char* recastNavigationSampleName(RecastNavigationSampleKind kind) {
  switch (kind) {
    case RecastNavigationSampleKind::SoloMesh: return "Solo Mesh";
    case RecastNavigationSampleKind::TileMesh: return "Tile Mesh";
    case RecastNavigationSampleKind::TempObstacles: return "Temp Obstacles";
    case RecastNavigationSampleKind::Debug: return "Debug";
    default: return "Recast Navigation";
  }
}

class RecastNavigationSampleApp final : public app::GameInterface {
 public:
  explicit RecastNavigationSampleApp(RecastNavigationSampleKind kind)
      : kind_(kind) {}

  void onStart() override {
    bindInput();
    loadAssets();
    spawnLighting();
    spawnSampleScene();
    spawnCamera();
  }

  void onFixedUpdate(float dt) override { (void)dt; }

  void onUpdate(float dt) override {
    updateCamera(dt);
    handleCrowdHotkeys();
    handleClick();
    updateTesterSliced();
    updateAgentTrails();
    updateAfterBuild();
    drawDebug();
  }

  void onShutdown() override {}

  void drawUi(app::UIContext& ctx) {
    (void)ctx;
    ImGui::SetNextWindowSize(ImVec2(360.0f, 680.0f), ImGuiCond_FirstUseEver);
    ImGui::Begin(recastNavigationSampleName(kind_));
    drawSettingsUi();
    ImGui::Separator();
    drawToolsUi();
    ImGui::Separator();
    drawDebugUi();
    ImGui::Separator();
    drawStatusUi();
    ImGui::End();
  }

 private:
  navigation::NavigationSystem* navigationSystem() const {
    return systems != nullptr ? systems->findSystem<navigation::NavigationSystem>() : nullptr;
  }

  navigation::NavQueryFilter sampleQueryFilter() const {
    return navigation::makeQueryFilter(currentConfig(), query_include_flags_, query_exclude_flags_);
  }

  void bindInput() {
    input->bindKey("forward", platform::Key::W);
    input->bindKey("back", platform::Key::S);
    input->bindKey("left", platform::Key::A);
    input->bindKey("right", platform::Key::D);
    input->bindKey("up", platform::Key::E);
    input->bindKey("down", platform::Key::Q);
    input->bindKey("fast", platform::Key::LeftShift);
    input->bindKey("crowd_toggle", platform::Key::Space, input::Trigger::Pressed);
    input->bindKey("crowd_step", platform::Key::Num1, input::Trigger::Pressed);
    input->bindMouse("look", platform::MouseButton::Right);
    input->bindMouse("sample_click", platform::MouseButton::Left, input::Trigger::Pressed);
  }

  void loadAssets() {
    asset_ = loadMeshGeometry(meshForSample(kind_));
    nav_tests_ =
        loadTestCase(recastAssetPath("test_cases/nav_mesh_test.txt")).value_or(TestCaseFile{});
    ray_tests_ =
        loadTestCase(recastAssetPath("test_cases/raycast_test.txt")).value_or(TestCaseFile{});
  }

  navigation::NavMeshBuildConfig currentConfig() const {
    navigation::NavMeshBuildConfig config = recastBuildConfig(
        kind_ == RecastNavigationSampleKind::SoloMesh || kind_ == RecastNavigationSampleKind::Debug
            ? navigation::NavMeshBuildMode::Solo
            : navigation::NavMeshBuildMode::Tiled);
    config.cell_size = cell_size_;
    config.cell_height = cell_height_;
    config.agent_height = agent_height_;
    config.agent_radius = agent_radius_;
    config.agent_max_climb = agent_max_climb_;
    config.agent_max_slope_degrees = agent_max_slope_;
    config.region_min_size = region_min_size_;
    config.region_merge_size = region_merge_size_;
    config.edge_max_len = edge_max_len_;
    config.edge_max_error = edge_max_error_;
    config.verts_per_poly = verts_per_poly_;
    config.detail_sample_dist = detail_sample_dist_;
    config.detail_sample_max_error = detail_sample_max_error_;
    config.partition_type = partitionFromIndex(partition_index_);
    config.collect_build_debug_draw = keep_intermediate_results_ ||
                                      kind_ == RecastNavigationSampleKind::Debug;
    config.tile_size = tile_size_;
    config.default_poly_flags = kSamplePolyFlagWalk;
    config.off_mesh_poly_flags = kSamplePolyFlagJump;
    config.area_configs = {
        {kSampleAreaGround, kSamplePolyFlagWalk, 1.0f},
        {kSampleAreaWater, kSamplePolyFlagSwim, 10.0f},
        {kSampleAreaRoad, kSamplePolyFlagWalk, 1.0f},
        {kSampleAreaDoor, kSamplePolyFlagWalk | kSamplePolyFlagDoor, 1.0f},
        {kSampleAreaGrass, kSamplePolyFlagWalk, 2.0f},
        {kSampleAreaJump, kSamplePolyFlagJump, 1.5f},
    };
    return config;
  }

  void spawnSampleScene() {
    const Bounds bounds = computeBounds(asset_.geometry);
    mesh_offset_ = math::scale(centerOf(bounds), -1.0f);
    const std::string mesh_key = asset_.path.string();
    const std::string material_key =
        std::string("recast/") + recastNavigationSampleName(kind_) + "/mesh_tint";
    materials->registerFromMeshTint(material_key, mesh_key, tintForSample(kind_));

    mesh_entity_ = helpers::spawnMeshAsset(*world,
                                           std::string(recastNavigationSampleName(kind_)) + " Mesh",
                                           mesh_key,
                                           mesh_offset_);
    world->get<components::MeshComponent>(mesh_entity_).material_key = material_key;
    world->add(mesh_entity_,
               components::NavMeshSurfaceComponent{
                   .layer_mask = source_mask_,
                   .area = navigation::kNavAreaDefault,
                   .mesh_key = mesh_key,
               });

    nav_entity_ = world->createEntity();
    world->setName(nav_entity_, std::string(recastNavigationSampleName(kind_)) + " NavMesh");
    components::NavMeshComponent nav;
    nav.source_mask = source_mask_;
    nav.build_config = currentConfig();
    nav.debug_draw = true;
    nav.debug_draw_mode = selectedDebugMode().mode;
    world->add(nav_entity_, std::move(nav));

    if (kind_ == RecastNavigationSampleKind::TempObstacles) {
      components::NavTileCacheComponent cache;
      cache.build_config.expected_layers_per_tile = 4;
      cache.build_config.max_obstacles = 128;
      world->add(nav_entity_, std::move(cache));
      active_tool_ = ToolKind::TempObstacle;
    }
  }

  void spawnCamera() {
    const Bounds bounds = computeBounds(asset_.geometry);
    const math::Vec3 span = math::subtract(bounds.max, bounds.min);
    const float distance = std::max({span.x, span.z, 60.0f}) * 1.15f;
    camera_entity_ = helpers::spawnCamera(*world,
                                          "Camera",
                                          {0.0f, std::max(45.0f, span.y + 50.0f), distance},
                                          math::fromYawPitch(camera_yaw_, camera_pitch_),
                                          components::CameraComponent{
                                              .near_clip = 0.05f,
                                              .far_clip = 1000.0f,
                                              .is_primary = true,
                                          });
  }

  void spawnLighting() {
    helpers::spawnDirectionalLight(*world,
                                   "Recast Sun",
                                   {0.0f, 80.0f, 0.0f},
                                   math::fromYawPitch(0.45f, -0.95f),
                                   components::LightComponent{
                                       .type = components::LightComponent::Type::Directional,
                                       .color = {1.0f, 1.0f, 1.0f, 1.0f},
                                       .intensity = 0.85f,
                                       .casts_shadows = true,
                                       .shadow_extent = 460.0f,
                                   });
    helpers::spawnEnvironment(*world,
                              "Environment",
                              resolveExampleAssetPath("golden_gate_hills_4k.hdr").string(),
                              0.35f,
                              true);
  }

  void updateCamera(float dt) {
    if (!world->isAlive(camera_entity_)) {
      return;
    }
    if (input->actionDown("look")) {
      camera_yaw_ -= input->mouseDeltaX() * 0.003f;
      camera_pitch_ =
          std::clamp(camera_pitch_ - input->mouseDeltaY() * 0.003f, -1.35f, -0.08f);
    }

    auto& transform = world->get<components::TransformComponent>(camera_entity_);
    const math::Quat rotation = math::fromYawPitch(camera_yaw_, camera_pitch_);
    const math::Vec3 forward = math::rotateVec(rotation, {0.0f, 0.0f, -1.0f});
    const math::Vec3 right = math::rotateVec(rotation, {1.0f, 0.0f, 0.0f});
    math::Vec3 move{};
    if (input->actionDown("forward")) move = math::add(move, forward);
    if (input->actionDown("back")) move = math::subtract(move, forward);
    if (input->actionDown("right")) move = math::add(move, right);
    if (input->actionDown("left")) move = math::subtract(move, right);
    if (input->actionDown("up")) move.y += 1.0f;
    if (input->actionDown("down")) move.y -= 1.0f;

    if (math::lengthSquared(move) > 0.0001f) {
      const float speed = input->actionDown("fast") ? 95.0f : 38.0f;
      transform.setPosition(
          math::add(transform.getPosition(), math::scale(math::normalize(move), speed * dt)));
    }
    transform.setRotation(rotation);
  }

  bool mouseScreenRay(renderer::ScreenRay& out_ray) const {
    if (graphics == nullptr || !world->isAlive(camera_entity_)) {
      return false;
    }
    double mouse_x = 0.0;
    double mouse_y = 0.0;
    if (!input->mousePosition(mouse_x, mouse_y)) {
      return false;
    }
    int width = 0;
    int height = 0;
    graphics->getFramebufferSize(width, height);
    const auto& camera_transform = world->get<components::TransformComponent>(camera_entity_);
    const auto& camera = world->get<components::CameraComponent>(camera_entity_);
    return renderer::screenPointToWorldRay(mouse_x,
                                           mouse_y,
                                           width,
                                           height,
                                           camera_transform.getPosition(),
                                           camera_transform.getRotation(),
                                           camera.fov_y_degrees,
                                           out_ray);
  }

  bool screenToGround(math::Vec3& out_point) const {
    renderer::ScreenRay ray;
    if (!mouseScreenRay(ray) || std::abs(ray.direction.y) < 0.0001f) {
      return false;
    }
    const float t = (kGroundY - ray.origin.y) / ray.direction.y;
    if (t < 0.0f) {
      return false;
    }
    out_point = math::add(ray.origin, math::scale(ray.direction, t));
    return true;
  }

  std::optional<ScreenSegment> clickSegment(float length = 1000.0f) const {
    renderer::ScreenRay ray;
    if (!mouseScreenRay(ray)) {
      return std::nullopt;
    }
    return ScreenSegment{ray.origin, math::add(ray.origin, math::scale(ray.direction, length))};
  }

  void handleClick() {
    if (!input->actionPressed("sample_click") || !world->isAlive(nav_entity_)) {
      return;
    }
    if (ImGui::GetCurrentContext() != nullptr && ImGui::GetIO().WantCaptureMouse) {
      return;
    }
    math::Vec3 point;
    if (!screenToGround(point)) {
      return;
    }
    const std::optional<ScreenSegment> segment = clickSegment();

    switch (active_tool_) {
      case ToolKind::NavmeshTester: handleTesterClick(point); break;
      case ToolKind::NavmeshPrune: pruneFrom(point); break;
      case ToolKind::TileEdit: editTile(point); break;
      case ToolKind::TileHighlight: highlightTile(point); break;
      case ToolKind::TempObstacle: handleTempObstacleClick(point, segment); break;
      case ToolKind::OffMeshConnection: addOffMeshClick(point); break;
      case ToolKind::ConvexVolume: addConvexVolume(point); break;
      case ToolKind::Crowd: handleCrowdClick(point, segment); break;
    }
  }

  void updateAfterBuild() {
    if (!world->isAlive(nav_entity_) || !world->has<components::NavMeshComponent>(nav_entity_)) {
      return;
    }
    auto& nav = world->get<components::NavMeshComponent>(nav_entity_);
    if (!nav.built || !nav.nav_mesh.isValid()) {
      return;
    }
    if (observed_build_version_ == nav.build_version) {
      return;
    }
    observed_build_version_ = nav.build_version;
    if (reported_build_result_ &&
        last_reported_status_ == nav.last_build_result.status &&
        last_reported_polygons_ == nav.last_build_result.polygon_count &&
        last_reported_triangles_ == nav.last_build_result.triangle_count) {
      return;
    }
    reported_build_result_ = true;
    last_reported_status_ = nav.last_build_result.status;
    last_reported_polygons_ = nav.last_build_result.polygon_count;
    last_reported_triangles_ = nav.last_build_result.triangle_count;
    debug_paths_.clear();
    spdlog::info("{} baked {} polygons from {} triangles",
                 recastNavigationSampleName(kind_),
                 nav.last_build_result.polygon_count,
                 nav.last_build_result.triangle_count);
    if (tester_has_start_ || tester_has_end_) {
      recalculateTester();
    } else {
      seedSampleQueries(nav);
    }
  }

  void seedSampleQueries(const components::NavMeshComponent& nav) {
    navigation::NavQuery query(nav.nav_mesh);
    if (kind_ == RecastNavigationSampleKind::SoloMesh && !nav_tests_.queries.empty()) {
      int count = 0;
      for (const QueryCase& test : nav_tests_.queries) {
        if (test.kind != "pf" || count >= 4) {
          continue;
        }
        const navigation::NavPath path =
            query.findSmoothPath(localToWorld(test.start),
                                 localToWorld(test.end),
                                 {2.0f, 4.0f, 2.0f},
                                 {},
                                 1024,
                                 filterFor(test));
        if (path.success()) {
          debug_paths_.push_back(path);
          ++count;
        }
      }
    }
    if (kind_ == RecastNavigationSampleKind::TileMesh && !ray_tests_.queries.empty()) {
      for (const QueryCase& test : ray_tests_.queries) {
        if (test.kind != "rc") {
          continue;
        }
        const navigation::NavPath ray =
            query.raycast(localToWorld(test.start),
                          localToWorld(test.end),
                          {2.0f, 4.0f, 2.0f},
                          256,
                          filterFor(test));
        if (ray.success()) {
          debug_paths_.push_back(ray);
        }
      }
    }
  }

  math::Vec3 localToWorld(const math::Vec3& point) const {
    return offsetPoint(point, mesh_offset_);
  }

  void handleTesterClick(const math::Vec3& point) {
    if (!navUsable()) {
      return;
    }
    if (input->actionDown("fast")) {
      tester_start_ = point;
      tester_has_start_ = true;
      marker(point, "Tester Start", {0.15f, 0.75f, 0.95f, 1.0f});
    } else {
      tester_end_ = point;
      tester_has_end_ = true;
      marker(point, "Tester End", {0.95f, 0.90f, 0.20f, 1.0f});
    }
    recalculateTester();
  }

  void recalculateTester() {
    debug_paths_.clear();
    tester_polys_.clear();
    tester_parent_polys_.clear();
    tester_shape_.clear();
    tester_wall_segments_.clear();
    tester_distance_to_wall_ = 0.0f;
    tester_has_wall_hit_ = false;
    sliced_query_.reset();
    sliced_active_ = false;
    const auto& nav = world->get<components::NavMeshComponent>(nav_entity_);
    navigation::NavQuery query(nav.nav_mesh);
    const navigation::NavQueryFilter filter = sampleQueryFilter();
    if (!query.isValid()) {
      return;
    }

    if (tester_has_start_) {
      uint64_t start_ref = 0;
      query.findNearestPoly(tester_start_, start_ref, nullptr, {2.0f, 4.0f, 2.0f}, filter);
      if (start_ref != 0) {
        tester_polys_.push_back(start_ref);
      }
    }
    if (tester_has_end_) {
      uint64_t end_ref = 0;
      query.findNearestPoly(tester_end_, end_ref, nullptr, {2.0f, 4.0f, 2.0f}, filter);
      if (end_ref != 0) {
        tester_polys_.push_back(end_ref);
      }
    }

    switch (tester_mode_) {
      case NavmeshTesterMode::PathfindFollow: {
        if (!tester_has_start_ || !tester_has_end_) {
          return;
        }
        const navigation::NavPath path =
            query.findSmoothPath(tester_start_, tester_end_, {2.0f, 4.0f, 2.0f}, {}, 1024, filter);
        if (path.success()) {
          debug_paths_.push_back(path);
        }
        break;
      }
      case NavmeshTesterMode::PathfindStraight: {
        if (!tester_has_start_ || !tester_has_end_) {
          return;
        }
        const navigation::NavPath path =
            query.findPath(tester_start_,
                           tester_end_,
                           {2.0f, 4.0f, 2.0f},
                           256,
                           filter,
                           straight_path_options_);
        if (path.success()) {
          debug_paths_.push_back(path);
        }
        break;
      }
      case NavmeshTesterMode::PathfindSliced:
        if (!tester_has_start_ || !tester_has_end_) {
          return;
        }
        sliced_query_ = std::make_unique<navigation::NavQuery>(nav.nav_mesh);
        if (sliced_query_ != nullptr &&
            sliced_query_->beginSlicedPath(tester_start_, tester_end_, {2.0f, 4.0f, 2.0f}, filter) ==
                navigation::NavStatus::InProgress) {
          sliced_active_ = true;
        }
        break;
      case NavmeshTesterMode::Raycast: {
        if (!tester_has_start_ || !tester_has_end_) {
          return;
        }
        const navigation::NavPath ray =
            query.raycast(tester_start_, tester_end_, {2.0f, 4.0f, 2.0f}, 256, filter);
        if (ray.success()) {
          debug_paths_.push_back(ray);
          tester_has_wall_hit_ = ray.points.size() >= 2 &&
                                 horizontalDistanceSquared(ray.points.back(), tester_end_) > 0.04f;
          tester_wall_hit_ = ray.points.back();
        }
        break;
      }
      case NavmeshTesterMode::DistanceToWall:
        if (!tester_has_start_) {
          return;
        }
        tester_has_wall_hit_ = query.findDistanceToWall(tester_start_,
                                                        100.0f,
                                                        tester_distance_to_wall_,
                                                        &tester_wall_hit_,
                                                        &tester_wall_normal_,
                                                        {2.0f, 4.0f, 2.0f},
                                                        filter);
        break;
      case NavmeshTesterMode::FindPolysInCircle:
        if (!tester_has_start_ || !tester_has_end_) {
          return;
        } else {
          tester_circle_radius_ = std::sqrt(horizontalDistanceSquared(tester_start_, tester_end_));
          const navigation::NavPolyQueryResult result =
              query.findPolysAroundCircle(tester_start_,
                                          tester_circle_radius_,
                                          {2.0f, 4.0f, 2.0f},
                                          256,
                                          filter);
          if (result.success()) {
            tester_polys_ = result.polys;
            tester_parent_polys_ = result.parents;
          }
        }
        break;
      case NavmeshTesterMode::FindPolysInShape:
        if (!tester_has_start_ || !tester_has_end_) {
          return;
        } else {
          const float nx = (tester_end_.z - tester_start_.z) * 0.25f;
          const float nz = -(tester_end_.x - tester_start_.x) * 0.25f;
          tester_shape_ = {
              {tester_start_.x + nx * 1.2f, tester_start_.y + agent_height_ * 0.5f, tester_start_.z + nz * 1.2f},
              {tester_start_.x - nx * 1.3f, tester_start_.y + agent_height_ * 0.5f, tester_start_.z - nz * 1.3f},
              {tester_end_.x - nx * 0.8f, tester_end_.y + agent_height_ * 0.5f, tester_end_.z - nz * 0.8f},
              {tester_end_.x + nx, tester_end_.y + agent_height_ * 0.5f, tester_end_.z + nz},
          };
          const navigation::NavPolyQueryResult result =
              query.findPolysAroundShape(tester_start_,
                                         tester_shape_,
                                         {2.0f, 4.0f, 2.0f},
                                         256,
                                         filter);
          if (result.success()) {
            tester_polys_ = result.polys;
            tester_parent_polys_ = result.parents;
          }
        }
        break;
      case NavmeshTesterMode::FindLocalNeighbourhood: {
        if (!tester_has_start_) {
          return;
        }
        const navigation::NavPolyQueryResult result =
            query.findLocalNeighbourhood(tester_start_,
                                         tester_neighbourhood_radius_,
                                         {2.0f, 4.0f, 2.0f},
                                         256,
                                         filter);
        if (result.success()) {
          tester_polys_ = result.polys;
          tester_parent_polys_ = result.parents;
        }
        tester_wall_segments_ =
            query.getPolyWallSegments(tester_start_, {2.0f, 4.0f, 2.0f}, 256, filter).segments;
        break;
      }
    }
  }

  void updateTesterSliced() {
    if (!sliced_active_ || sliced_query_ == nullptr) {
      return;
    }
    bool done = false;
    const navigation::NavStatus status = sliced_query_->updateSlicedPath(1, done);
    if (status != navigation::NavStatus::InProgress || done) {
      navigation::NavPath path = sliced_query_->finalizeSlicedPath(256);
      if (path.success()) {
        debug_paths_.clear();
        debug_paths_.push_back(std::move(path));
      }
      sliced_active_ = false;
      sliced_query_.reset();
    }
  }

  void pruneFrom(const math::Vec3& point) {
    if (!navUsable()) {
      return;
    }
    auto& nav = world->get<components::NavMeshComponent>(nav_entity_);
    const uint32_t pruned = nav.nav_mesh.pruneUnreachable(point, 1u << 4u);
    ++nav.build_version;
    marker(point, "Prune Start", {0.95f, 0.25f, 0.25f, 1.0f});
    spdlog::info("{} pruned {} polygons", recastNavigationSampleName(kind_), pruned);
  }

  void editTile(const math::Vec3& point) {
    if (!navUsable()) {
      return;
    }
    auto& nav = world->get<components::NavMeshComponent>(nav_entity_);
    if (!input->actionDown("fast")) {
      const navigation::NavMeshInputGeometry geometry =
          navigation::collectNavMeshGeometry(*world, source_mask_);
      navigation::NavMeshBuildResult result;
      if (nav.nav_mesh.rebuildTile(geometry, point, &result)) {
        ++nav.build_version;
        marker(point, "Rebuilt Tile", {0.2f, 0.8f, 0.35f, 1.0f});
      }
    } else if (nav.nav_mesh.removeTile(point)) {
      ++nav.build_version;
      marker(point, "Removed Tile", {0.95f, 0.35f, 0.15f, 1.0f});
    }
  }

  void highlightTile(const math::Vec3& point) {
    highlighted_tile_point_ = point;
    has_highlighted_tile_ = true;
    marker(point, "Tile Highlight", {0.96f, 0.78f, 0.18f, 1.0f});
  }

  void handleTempObstacleClick(const math::Vec3& point,
                               const std::optional<ScreenSegment>& segment) {
    if (kind_ != RecastNavigationSampleKind::TempObstacles) {
      return;
    }
    if (input->actionDown("fast")) {
      if (segment) {
        removeTempObstacleHit(*segment);
      }
      return;
    }
    createObstacle(point,
                   obstacle_shape_,
                   {obstacle_half_extents_[0], obstacle_half_extents_[1], obstacle_half_extents_[2]},
                   obstacle_radius_,
                   obstacle_height_,
                   obstacle_yaw_);
  }

  void removeTempObstacleHit(const ScreenSegment& segment) {
    ecs::Entity best{};
    float best_t = std::numeric_limits<float>::max();
    for (const ecs::Entity entity : obstacles_) {
      if (!world->isAlive(entity) ||
          !world->has<components::TransformComponent>(entity) ||
          !world->has<components::NavTileCacheObstacleComponent>(entity)) {
        continue;
      }
      const auto& transform = world->get<components::TransformComponent>(entity);
      const auto& obstacle = world->get<components::NavTileCacheObstacleComponent>(entity);
      const math::Vec3 center = transform.getPosition();
      const math::Vec3 half = obstacle.shape == navigation::NavTileCacheObstacleShape::Cylinder
          ? math::Vec3{obstacle.radius, obstacle.height * 0.5f, obstacle.radius}
          : obstacle.half_extents;
      float t = 0.0f;
      if (intersectSegmentAabb(segment.start,
                               segment.end,
                               math::subtract(center, half),
                               math::add(center, half),
                               t) &&
          t < best_t) {
        best = entity;
        best_t = t;
      }
    }
    if (!best.isValid()) {
      return;
    }
    if (world->has<components::NavTileCacheObstacleComponent>(best)) {
      world->get<components::NavTileCacheObstacleComponent>(best).remove_requested = true;
    }
    if (world->has<components::MeshComponent>(best)) {
      auto& mesh = world->get<components::MeshComponent>(best);
      mesh.visible = false;
      mesh.shadow_visible = false;
    }
    obstacles_.erase(std::remove(obstacles_.begin(), obstacles_.end(), best), obstacles_.end());
  }

  void addOffMeshClick(const math::Vec3& point) {
    if (input->actionDown("fast")) {
      deleteOffMeshNear(point);
      return;
    }
    if (!off_mesh_has_start_) {
      off_mesh_start_ = point;
      off_mesh_start_entity_ =
          marker(point, "OffMesh Start", {0.15f, 0.75f, 0.95f, 1.0f});
      off_mesh_has_start_ = true;
      return;
    }
    const ecs::Entity end =
        marker(point, "OffMesh End", {0.95f, 0.90f, 0.20f, 1.0f});
    if (world->isAlive(off_mesh_start_entity_)) {
      world->add(off_mesh_start_entity_,
                 components::NavOffMeshLinkComponent{
                     .layer_mask = source_mask_,
                     .end_entity = end,
                     .radius = off_mesh_radius_,
                     .area = kSampleAreaJump,
                     .flags = kSamplePolyFlagJump,
                     .bidirectional = off_mesh_bidirectional_,
                     .user_id = ++off_mesh_user_id_,
                 });
      off_mesh_links_.push_back({off_mesh_start_entity_, end});
      requestRebuild();
    }
    off_mesh_has_start_ = false;
  }

  void deleteOffMeshNear(const math::Vec3& point) {
    float best_distance = off_mesh_radius_ * off_mesh_radius_;
    size_t best_index = off_mesh_links_.size();
    for (size_t i = 0; i < off_mesh_links_.size(); ++i) {
      const OffMeshLinkRecord& link = off_mesh_links_[i];
      if (world->isAlive(link.start) && world->has<components::TransformComponent>(link.start)) {
        const auto& transform = world->get<components::TransformComponent>(link.start);
        const float d = horizontalDistanceSquared(transform.getPosition(), point);
        if (d < best_distance) {
          best_distance = d;
          best_index = i;
        }
      }
      if (world->isAlive(link.end) && world->has<components::TransformComponent>(link.end)) {
        const auto& transform = world->get<components::TransformComponent>(link.end);
        const float d = horizontalDistanceSquared(transform.getPosition(), point);
        if (d < best_distance) {
          best_distance = d;
          best_index = i;
        }
      }
    }
    if (best_index == off_mesh_links_.size()) {
      return;
    }
    const OffMeshLinkRecord link = off_mesh_links_[best_index];
    if (world->isAlive(link.start)) {
      world->destroyEntity(link.start);
    }
    if (world->isAlive(link.end)) {
      world->destroyEntity(link.end);
    }
    off_mesh_links_.erase(off_mesh_links_.begin() + static_cast<std::ptrdiff_t>(best_index));
    off_mesh_has_start_ = false;
    requestRebuild();
  }

  void addConvexVolume(const math::Vec3& point) {
    if (input->actionDown("fast")) {
      deleteConvexVolumeAt(point);
      return;
    }
    if (!convex_draft_points_.empty() &&
        horizontalDistanceSquared(point, convex_draft_points_.back()) < 0.04f) {
      commitConvexVolume();
      return;
    }
    if (convex_draft_points_.size() >= 12) {
      return;
    }
    convex_draft_points_.push_back(point);
    convex_draft_hull_ = convexHullXZ(convex_draft_points_);
    convex_draft_markers_.push_back(
        marker(point, "Convex Point", {0.74f, 0.30f, 0.90f, 1.0f}, {0.18f, 0.18f, 0.18f}));
  }

  void commitConvexVolume() {
    if (convex_draft_hull_.size() < 3) {
      clearConvexDraft();
      return;
    }
    std::vector<math::Vec3> vertices = offsetPolyFromCentroid(convex_draft_hull_, convex_poly_offset_);
    float min_y = std::numeric_limits<float>::max();
    for (const math::Vec3& point : vertices) {
      min_y = std::min(min_y, point.y);
    }
    min_y -= convex_volume_descent_;
    const float max_y = min_y + convex_volume_height_;

    const ecs::Entity volume = world->createEntity();
    world->setName(volume, "Recast Convex Volume");
    world->add(volume, components::TransformComponent{});
    world->add(volume,
               components::NavConvexVolumeComponent{
                   .layer_mask = source_mask_,
                   .vertices = vertices,
                   .min_y = min_y,
                   .max_y = max_y,
                   .area = convex_area_type_,
               });
    convex_volumes_.push_back({volume, vertices, min_y, max_y});
    clearConvexDraft();
    requestRebuild();
  }

  void deleteConvexVolumeAt(const math::Vec3& point) {
    for (auto it = convex_volumes_.begin(); it != convex_volumes_.end(); ++it) {
      if (point.y >= it->min_y &&
          point.y <= it->max_y &&
          pointInPolyXZ(it->vertices, point)) {
        if (world->isAlive(it->entity)) {
          world->destroyEntity(it->entity);
        }
        convex_volumes_.erase(it);
        requestRebuild();
        return;
      }
    }
  }

  void clearConvexDraft() {
    for (const ecs::Entity entity : convex_draft_markers_) {
      if (world->isAlive(entity)) {
        world->destroyEntity(entity);
      }
    }
    convex_draft_markers_.clear();
    convex_draft_points_.clear();
    convex_draft_hull_.clear();
  }

  void handleCrowdClick(const math::Vec3& point, const std::optional<ScreenSegment>& segment) {
    ensureCrowdComponent();
    if (!world->has<components::NavCrowdComponent>(nav_entity_)) {
      return;
    }

    switch (crowd_mode_) {
      case CrowdToolMode::CreateAgents:
        if (input->actionDown("fast")) {
          if (segment) {
            removeCrowdAgentHit(*segment);
          }
        } else {
          createCrowdAgent(point);
        }
        break;
      case CrowdToolMode::MoveTarget:
        setCrowdMoveTarget(point, input->actionDown("fast"));
        break;
      case CrowdToolMode::SelectAgent:
        selected_crowd_agent_ = segment ? hitTestCrowdAgent(*segment) : ecs::Entity{};
        break;
      case CrowdToolMode::TogglePolys:
        toggleCrowdPoly(point);
        break;
    }
  }

  navigation::NavCrowdAgentParams currentCrowdAgentParams() const {
    navigation::NavCrowdAgentParams params;
    params.radius = crowd_agent_radius_;
    params.height = crowd_agent_height_;
    params.max_acceleration = 8.0f;
    params.max_speed = crowd_agent_speed_;
    params.collision_query_range = params.radius * 12.0f;
    params.path_optimization_range = params.radius * 30.0f;
    params.separation_weight = crowd_separation_weight_;
    params.obstacle_avoidance_type = static_cast<uint8_t>(crowd_obstacle_avoidance_type_);
    params.update_flags = 0;
    if (crowd_anticipate_turns_) {
      params.update_flags =
          static_cast<uint8_t>(params.update_flags | navigation::NavCrowdUpdateFlagAnticipateTurns);
    }
    if (crowd_optimize_visibility_) {
      params.update_flags =
          static_cast<uint8_t>(params.update_flags | navigation::NavCrowdUpdateFlagOptimizeVisibility);
    }
    if (crowd_optimize_topology_) {
      params.update_flags =
          static_cast<uint8_t>(params.update_flags | navigation::NavCrowdUpdateFlagOptimizeTopology);
    }
    if (crowd_obstacle_avoidance_) {
      params.update_flags =
          static_cast<uint8_t>(params.update_flags | navigation::NavCrowdUpdateFlagObstacleAvoidance);
    }
    if (crowd_separation_) {
      params.update_flags =
          static_cast<uint8_t>(params.update_flags | navigation::NavCrowdUpdateFlagSeparation);
    }
    return params;
  }

  void createCrowdAgent(const math::Vec3& point) {
    const navigation::NavCrowdAgentParams params = currentCrowdAgentParams();
    const glm::vec3 half_extents{params.radius, params.height * 0.5f, params.radius};
    const ecs::Entity entity = marker(point,
                                      "Crowd Agent",
                                      {0.2f, 0.65f, 1.0f, 1.0f},
                                      half_extents);
    auto& transform = world->get<components::TransformComponent>(entity);
    transform.setPosition(yOffset(point, half_extents.y));

    components::NavCrowdAgentComponent agent;
    agent.crowd_entity = nav_entity_;
    agent.params = params;
    agent.height_offset = params.height * 0.5f;
    agent.stopping_distance = 0.2f;
    world->add(entity, agent);
    crowd_agents_.push_back(entity);
    agent_trails_[entityKey(entity)] = AgentTrail{{transform.getPosition()}};
    if (crowd_target_set_) {
      navigation::NavigationSystem::requestCrowdMoveTo(*world, entity, crowd_target_);
    }
  }

  void removeCrowdAgentHit(const ScreenSegment& segment) {
    const ecs::Entity hit = hitTestCrowdAgent(segment);
    if (!hit.isValid() || !world->isAlive(hit)) {
      return;
    }
    if (world->has<components::NavCrowdAgentComponent>(hit)) {
      auto& agent = world->get<components::NavCrowdAgentComponent>(hit);
      agent.remove_requested = true;
      agent.enabled = false;
    }
    if (world->has<components::MeshComponent>(hit)) {
      auto& mesh = world->get<components::MeshComponent>(hit);
      mesh.visible = false;
      mesh.shadow_visible = false;
    }
    crowd_agents_.erase(std::remove(crowd_agents_.begin(), crowd_agents_.end(), hit),
                        crowd_agents_.end());
    agent_trails_.erase(entityKey(hit));
    if (selected_crowd_agent_ == hit) {
      selected_crowd_agent_ = {};
    }
  }

  ecs::Entity hitTestCrowdAgent(const ScreenSegment& segment) const {
    ecs::Entity best{};
    float best_t = std::numeric_limits<float>::max();
    for (const ecs::Entity entity : crowd_agents_) {
      if (!world->isAlive(entity) ||
          !world->has<components::TransformComponent>(entity) ||
          !world->has<components::NavCrowdAgentComponent>(entity)) {
        continue;
      }
      const auto& transform = world->get<components::TransformComponent>(entity);
      const auto& agent = world->get<components::NavCrowdAgentComponent>(entity);
      if (!agent.enabled) {
        continue;
      }
      const math::Vec3 center = transform.getPosition();
      const math::Vec3 half{agent.params.radius, agent.params.height * 0.5f, agent.params.radius};
      float t = 0.0f;
      if (intersectSegmentAabb(segment.start,
                               segment.end,
                               math::subtract(center, half),
                               math::add(center, half),
                               t) &&
          t < best_t) {
        best = entity;
        best_t = t;
      }
    }
    return best;
  }

  void setCrowdMoveTarget(const math::Vec3& point, bool velocity_mode) {
    crowd_target_ = point;
    crowd_target_set_ = true;
    marker(point,
           velocity_mode ? "Crowd Velocity Target" : "Crowd Target",
           velocity_mode ? math::Color{0.45f, 1.0f, 0.2f, 1.0f}
                         : math::Color{0.95f, 0.95f, 0.15f, 1.0f});

    std::vector<ecs::Entity> targets;
    if (selected_crowd_agent_.isValid() && world->isAlive(selected_crowd_agent_)) {
      targets.push_back(selected_crowd_agent_);
    } else {
      targets = crowd_agents_;
    }

    for (const ecs::Entity agent_entity : targets) {
      if (!world->isAlive(agent_entity) ||
          !world->has<components::TransformComponent>(agent_entity) ||
          !world->has<components::NavCrowdAgentComponent>(agent_entity)) {
        continue;
      }
      if (velocity_mode) {
        const auto& transform = world->get<components::TransformComponent>(agent_entity);
        const auto& agent = world->get<components::NavCrowdAgentComponent>(agent_entity);
        math::Vec3 delta = math::subtract(point, transform.getPosition());
        delta.y = 0.0f;
        if (math::lengthSquared(delta) > 0.0001f) {
          const math::Vec3 velocity =
              math::scale(math::normalize(delta), std::max(agent.params.max_speed, 0.0f));
          navigation::NavigationSystem::requestCrowdVelocity(*world, agent_entity, velocity);
        }
      } else {
        navigation::NavigationSystem::requestCrowdMoveTo(*world, agent_entity, point);
      }
    }
  }

  void toggleCrowdPoly(const math::Vec3& point) {
    if (!navUsable()) {
      return;
    }
    auto& nav = world->get<components::NavMeshComponent>(nav_entity_);
    navigation::NavQuery query(nav.nav_mesh);
    uint64_t poly_ref = 0;
    if (!query.findNearestPoly(point, poly_ref, nullptr, {2.0f, 4.0f, 2.0f}, sampleQueryFilter()) ||
        poly_ref == 0) {
      return;
    }
    uint16_t flags = 0;
    if (!nav.nav_mesh.getPolyFlags(poly_ref, flags)) {
      return;
    }
    flags = static_cast<uint16_t>(flags ^ kSamplePolyFlagDisabled);
    if (nav.nav_mesh.setPolyFlags(poly_ref, flags)) {
      toggled_polys_.push_back(poly_ref);
    }
  }

  void updateCrowdAgentParams() {
    const navigation::NavCrowdAgentParams params = currentCrowdAgentParams();
    for (const ecs::Entity entity : crowd_agents_) {
      if (!world->isAlive(entity) || !world->has<components::NavCrowdAgentComponent>(entity)) {
        continue;
      }
      auto& agent = world->get<components::NavCrowdAgentComponent>(entity);
      agent.params = params;
      agent.height_offset = params.height * 0.5f;
      agent.params_dirty = true;
    }
  }

  void handleCrowdHotkeys() {
    if (active_tool_ != ToolKind::Crowd ||
        !world->isAlive(nav_entity_) ||
        !world->has<components::NavCrowdComponent>(nav_entity_)) {
      return;
    }
    auto& crowd = world->get<components::NavCrowdComponent>(nav_entity_);
    if (input->actionPressed("crowd_toggle")) {
      crowd.simulation_paused = !crowd.simulation_paused;
    }
    if (input->actionPressed("crowd_step")) {
      crowd.simulation_paused = true;
      crowd.step_requested = true;
    }
  }

  void updateAgentTrails() {
    if (!world->isAlive(nav_entity_) || !world->has<components::NavCrowdComponent>(nav_entity_)) {
      return;
    }
    for (const ecs::Entity entity : crowd_agents_) {
      if (!world->isAlive(entity) ||
          !world->has<components::TransformComponent>(entity) ||
          !world->has<components::NavCrowdAgentComponent>(entity)) {
        continue;
      }
      const auto& agent = world->get<components::NavCrowdAgentComponent>(entity);
      if (!agent.enabled || agent.agent_id < 0) {
        continue;
      }
      AgentTrail& trail = agent_trails_[entityKey(entity)];
      trail.points.push_back(world->get<components::TransformComponent>(entity).getPosition());
      if (trail.points.size() > AgentTrail::kMaxPoints) {
        trail.points.erase(trail.points.begin());
      }
    }
  }

  void ensureCrowdComponent() {
    if (!world->isAlive(nav_entity_)) {
      return;
    }
    const navigation::NavQueryFilter crowd_filter =
        navigation::makeQueryFilter(currentConfig(),
                                    kSamplePolyFlagsAll,
                                    kSamplePolyFlagDisabled);
    if (world->has<components::NavCrowdComponent>(nav_entity_)) {
      auto& crowd = world->get<components::NavCrowdComponent>(nav_entity_);
      if (crowd.config.query_filters.empty() || crowd.config.query_filters.front().exclude_flags !=
                                                    kSamplePolyFlagDisabled) {
        crowd.config.query_filters = {crowd_filter};
        crowd.rebuild_requested = true;
      }
      return;
    }
    components::NavCrowdComponent crowd;
    crowd.config.max_agents = 64;
    crowd.config.max_agent_radius = std::max(crowd_agent_radius_, agent_radius_);
    crowd.config.query_filters = {crowd_filter};
    world->add(nav_entity_, std::move(crowd));
  }

  ecs::Entity marker(const math::Vec3& point,
                     std::string_view name,
                     const math::Color& color,
                     const glm::vec3& half_extents = {0.32f, 0.32f, 0.32f}) {
    const ecs::Entity entity =
        helpers::createDebugBoxMarker(*world,
                                      graphics,
                                      materials,
                                      std::string(recastNavigationSampleName(kind_)) + " " +
                                          std::string(name),
                                      color,
                                      yOffset(point, half_extents.y),
                                      half_extents);
    helper_entities_.push_back(entity);
    return entity;
  }

  ecs::Entity createObstacle(const math::Vec3& point,
                             navigation::NavTileCacheObstacleShape shape,
                             const math::Vec3& half_extents,
                             float radius,
                             float height,
                             float yaw) {
    const math::Vec3 visual_half = shape == navigation::NavTileCacheObstacleShape::Cylinder
        ? math::Vec3{radius, height * 0.5f, radius}
        : half_extents;
    const math::Vec3 nav_position = shape == navigation::NavTileCacheObstacleShape::Cylinder
        ? math::Vec3{point.x, point.y - 0.5f, point.z}
        : yOffset(point, visual_half.y);
    const math::Vec3 visual_position = yOffset(point, visual_half.y);
    const ecs::Entity entity =
        helpers::createDebugBoxMarker(*world,
                                      graphics,
                                      materials,
                                      "Temp Obstacle",
                                      {0.96f, 0.45f, 0.12f, 1.0f},
                                      visual_position,
                                      {visual_half.x, visual_half.y, visual_half.z});
    components::NavTileCacheObstacleComponent obstacle;
    obstacle.nav_mesh_entity = nav_entity_;
    obstacle.shape = shape;
    obstacle.offset = math::subtract(nav_position, visual_position);
    obstacle.half_extents = half_extents;
    obstacle.bounds_min = {-half_extents.x, -half_extents.y, -half_extents.z};
    obstacle.bounds_max = {half_extents.x, half_extents.y, half_extents.z};
    obstacle.radius = radius;
    obstacle.height = height;
    obstacle.yaw_radians = yaw;
    world->add(entity, obstacle);
    obstacles_.push_back(entity);
    return entity;
  }

  bool navUsable() const {
    if (!world->isAlive(nav_entity_) || !world->has<components::NavMeshComponent>(nav_entity_)) {
      return false;
    }
    const auto& nav = world->get<components::NavMeshComponent>(nav_entity_);
    return nav.built && nav.nav_mesh.isValid();
  }

  void requestRebuild() {
    if (!world->isAlive(nav_entity_) || !world->has<components::NavMeshComponent>(nav_entity_)) {
      return;
    }
    auto& nav = world->get<components::NavMeshComponent>(nav_entity_);
    nav.build_config = currentConfig();
    nav.debug_draw_mode = selectedDebugMode().mode;
    nav.rebuild_requested = true;
    if (world->has<components::NavTileCacheComponent>(nav_entity_)) {
      auto& cache = world->get<components::NavTileCacheComponent>(nav_entity_);
      cache.rebuild_requested = true;
    }
    if (world->has<components::NavCrowdComponent>(nav_entity_)) {
      auto& crowd = world->get<components::NavCrowdComponent>(nav_entity_);
      crowd.rebuild_requested = true;
    }
    observed_build_version_ = 0;
    reported_build_result_ = false;
    toggled_polys_.clear();
    sliced_query_.reset();
    sliced_active_ = false;
  }

  void saveSnapshot() {
    if (!navUsable()) {
      return;
    }
    const auto& nav = world->get<components::NavMeshComponent>(nav_entity_);
    const std::shared_ptr<const navigation::NavMeshSnapshot> snapshot = nav.nav_mesh.snapshot();
    if (snapshot == nullptr || !snapshot->valid()) {
      return;
    }
    std::ofstream out(snapshotFileForSample(kind_), std::ios::binary);
    out.write(reinterpret_cast<const char*>(snapshot->data.data()),
              static_cast<std::streamsize>(snapshot->data.size()));
  }

  void loadSnapshot() {
    std::ifstream in(snapshotFileForSample(kind_), std::ios::binary);
    if (!in || !world->has<components::NavMeshComponent>(nav_entity_)) {
      return;
    }
    navigation::NavMeshSnapshot snapshot;
    snapshot.data.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    auto& nav = world->get<components::NavMeshComponent>(nav_entity_);
    navigation::NavMeshBuildResult result;
    if (nav.nav_mesh.loadSnapshot(snapshot, &result)) {
      nav.built = true;
      nav.last_build_result = result;
      ++nav.build_version;
    }
  }

  void clearRuntimeMarkers() {
    auto is_runtime_entity = [&](ecs::Entity entity) {
      if (std::find(obstacles_.begin(), obstacles_.end(), entity) != obstacles_.end() ||
          std::find(crowd_agents_.begin(), crowd_agents_.end(), entity) != crowd_agents_.end() ||
          std::find(convex_draft_markers_.begin(), convex_draft_markers_.end(), entity) !=
              convex_draft_markers_.end()) {
        return true;
      }
      for (const OffMeshLinkRecord& link : off_mesh_links_) {
        if (link.start == entity || link.end == entity) {
          return true;
        }
      }
      return false;
    };
    std::vector<ecs::Entity> kept;
    for (const ecs::Entity entity : helper_entities_) {
      if (is_runtime_entity(entity)) {
        kept.push_back(entity);
      } else if (world->isAlive(entity)) {
        world->destroyEntity(entity);
      }
    }
    helper_entities_ = std::move(kept);
    tester_has_start_ = false;
    tester_has_end_ = false;
    tester_polys_.clear();
    tester_parent_polys_.clear();
    tester_shape_.clear();
    tester_wall_segments_.clear();
    tester_random_points_.clear();
    debug_paths_.clear();
  }

  void clearObstacles() {
    for (const ecs::Entity entity : obstacles_) {
      if (world->isAlive(entity) && world->has<components::NavTileCacheObstacleComponent>(entity)) {
        world->get<components::NavTileCacheObstacleComponent>(entity).remove_requested = true;
      }
      if (world->isAlive(entity) && world->has<components::MeshComponent>(entity)) {
        auto& mesh = world->get<components::MeshComponent>(entity);
        mesh.visible = false;
        mesh.shadow_visible = false;
      }
    }
    obstacles_.clear();
  }

  void saveTempObstacleState() {
    if (kind_ != RecastNavigationSampleKind::TempObstacles) {
      saveSnapshot();
      return;
    }
    std::ofstream out(snapshotFileForSample(kind_));
    for (const ecs::Entity entity : obstacles_) {
      if (!world->isAlive(entity) || !world->has<components::TransformComponent>(entity) ||
          !world->has<components::NavTileCacheObstacleComponent>(entity)) {
        continue;
      }
      const auto& transform = world->get<components::TransformComponent>(entity);
      const auto& obstacle = world->get<components::NavTileCacheObstacleComponent>(entity);
      const math::Vec3 visual_half =
          obstacle.shape == navigation::NavTileCacheObstacleShape::Cylinder
              ? math::Vec3{obstacle.radius, obstacle.height * 0.5f, obstacle.radius}
              : obstacle.half_extents;
      const math::Vec3 p = math::subtract(transform.getPosition(), {0.0f, visual_half.y, 0.0f});
      out << static_cast<int>(obstacle.shape) << " " << p.x << " " << p.y << " " << p.z << " "
          << obstacle.half_extents.x << " " << obstacle.half_extents.y << " "
          << obstacle.half_extents.z << " " << obstacle.radius << " " << obstacle.height << " "
          << obstacle.yaw_radians << "\n";
    }
  }

  void loadTempObstacleState() {
    if (kind_ != RecastNavigationSampleKind::TempObstacles) {
      loadSnapshot();
      return;
    }
    clearObstacles();
    std::ifstream in(snapshotFileForSample(kind_));
    int shape = 0;
    math::Vec3 p{};
    math::Vec3 half{};
    float radius = 0.0f;
    float height = 0.0f;
    float yaw = 0.0f;
    while (in >> shape >> p.x >> p.y >> p.z >> half.x >> half.y >> half.z >> radius >> height >> yaw) {
      createObstacle(p,
                     static_cast<navigation::NavTileCacheObstacleShape>(shape),
                     half,
                     radius,
                     height,
                     yaw);
    }
  }

  const DebugModeOption& selectedDebugMode() const {
    const DebugModeOption* modes = kSoloDebugModes;
    size_t count = sizeof(kSoloDebugModes) / sizeof(kSoloDebugModes[0]);
    if (kind_ == RecastNavigationSampleKind::TileMesh) {
      modes = kTileDebugModes;
      count = sizeof(kTileDebugModes) / sizeof(kTileDebugModes[0]);
    } else if (kind_ == RecastNavigationSampleKind::TempObstacles) {
      modes = kTempObstacleDebugModes;
      count = sizeof(kTempObstacleDebugModes) / sizeof(kTempObstacleDebugModes[0]);
    }
    const int clamped = std::clamp(debug_mode_index_, 0, static_cast<int>(count) - 1);
    return modes[clamped];
  }

  void drawSettingsUi() {
    if (ImGui::CollapsingHeader("Rasterization", ImGuiTreeNodeFlags_DefaultOpen)) {
      ImGui::SliderFloat("Cell Size", &cell_size_, 0.1f, 1.0f, "%.2f");
      ImGui::SliderFloat("Cell Height", &cell_height_, 0.1f, 1.0f, "%.2f");
      ImGui::SliderFloat("Agent Height", &agent_height_, 0.1f, 5.0f, "%.1f");
      ImGui::SliderFloat("Agent Radius", &agent_radius_, 0.0f, 5.0f, "%.1f");
      ImGui::SliderFloat("Agent Max Climb", &agent_max_climb_, 0.1f, 5.0f, "%.1f");
      ImGui::SliderFloat("Agent Max Slope", &agent_max_slope_, 0.0f, 90.0f, "%.0f");
      ImGui::SliderFloat("Min Region Size", &region_min_size_, 0.0f, 150.0f, "%.0f");
      ImGui::SliderFloat("Merged Region Size", &region_merge_size_, 0.0f, 150.0f, "%.0f");
      ImGui::SliderFloat("Max Edge Length", &edge_max_len_, 0.0f, 50.0f, "%.0f");
      ImGui::SliderFloat("Max Edge Error", &edge_max_error_, 0.1f, 3.0f, "%.1f");
      ImGui::SliderInt("Verts Per Poly", &verts_per_poly_, 3, 12);
      ImGui::SliderFloat("Sample Distance", &detail_sample_dist_, 0.0f, 16.0f, "%.0f");
      ImGui::SliderFloat("Max Sample Error", &detail_sample_max_error_, 0.0f, 16.0f, "%.0f");
      ImGui::Combo("Partition", &partition_index_, "Watershed\0Monotone\0Layers\0");
      if (kind_ == RecastNavigationSampleKind::TileMesh ||
          kind_ == RecastNavigationSampleKind::TempObstacles) {
        ImGui::SliderInt("Tile Size", &tile_size_, 16, kind_ == RecastNavigationSampleKind::TileMesh ? 1024 : 128);
      }
      ImGui::Checkbox("Keep Intermediate Results", &keep_intermediate_results_);
      if (ImGui::Button("Build")) {
        requestRebuild();
      }
      ImGui::SameLine();
      if (ImGui::Button("Save")) {
        if (kind_ == RecastNavigationSampleKind::TempObstacles) {
          saveTempObstacleState();
        } else {
          saveSnapshot();
        }
      }
      ImGui::SameLine();
      if (ImGui::Button("Load")) {
        if (kind_ == RecastNavigationSampleKind::TempObstacles) {
          loadTempObstacleState();
        } else {
          loadSnapshot();
        }
      }
    }
  }

  void drawToolsUi() {
    if (!ImGui::CollapsingHeader("Tools", ImGuiTreeNodeFlags_DefaultOpen)) {
      return;
    }
    drawToolRadio(ToolKind::NavmeshTester);
    drawToolRadio(ToolKind::NavmeshPrune);
    if (kind_ == RecastNavigationSampleKind::TileMesh) {
      drawToolRadio(ToolKind::TileEdit);
    }
    if (kind_ == RecastNavigationSampleKind::TempObstacles) {
      drawToolRadio(ToolKind::TileHighlight);
      drawToolRadio(ToolKind::TempObstacle);
    }
    if (kind_ != RecastNavigationSampleKind::Debug) {
      drawToolRadio(ToolKind::OffMeshConnection);
      drawToolRadio(ToolKind::ConvexVolume);
      drawToolRadio(ToolKind::Crowd);
    }

    if (active_tool_ == ToolKind::TileEdit && kind_ == RecastNavigationSampleKind::TileMesh) {
      if (ImGui::Button("Remove All Tiles") && navUsable()) {
        auto& nav = world->get<components::NavMeshComponent>(nav_entity_);
        if (nav.nav_mesh.removeAllTiles()) {
          ++nav.build_version;
        }
      }
      ImGui::SameLine();
      if (ImGui::Button("Build All Tiles")) {
        requestRebuild();
      }
    }
    if (active_tool_ == ToolKind::TempObstacle && kind_ == RecastNavigationSampleKind::TempObstacles) {
      int shape = static_cast<int>(obstacle_shape_);
      if (ImGui::Combo("Shape", &shape, "Cylinder\0Box\0Oriented Box\0")) {
        obstacle_shape_ = static_cast<navigation::NavTileCacheObstacleShape>(shape);
      }
      ImGui::SliderFloat3("Half Extents", obstacle_half_extents_, 0.2f, 3.0f);
      ImGui::SliderFloat("Radius", &obstacle_radius_, 0.2f, 3.0f);
      ImGui::SliderFloat("Height", &obstacle_height_, 0.5f, 5.0f);
      ImGui::SliderFloat("Yaw", &obstacle_yaw_, -3.14159f, 3.14159f);
      if (ImGui::Button("Clear Obstacles")) {
        clearObstacles();
      }
    }
    if (active_tool_ == ToolKind::NavmeshTester) {
      drawTesterToolUi();
    }
    if (active_tool_ == ToolKind::ConvexVolume) {
      ImGui::SliderFloat("Shape Height", &convex_volume_height_, 0.1f, 20.0f, "%.1f");
      ImGui::SliderFloat("Shape Descent", &convex_volume_descent_, 0.1f, 20.0f, "%.1f");
      ImGui::SliderFloat("Poly Offset", &convex_poly_offset_, 0.0f, 10.0f, "%.1f");
      ImGui::Text("Area Type");
      drawAreaRadio(kSampleAreaGround);
      drawAreaRadio(kSampleAreaWater);
      drawAreaRadio(kSampleAreaRoad);
      drawAreaRadio(kSampleAreaDoor);
      drawAreaRadio(kSampleAreaGrass);
      drawAreaRadio(kSampleAreaJump);
      if (ImGui::Button("Clear Shape")) {
        clearConvexDraft();
      }
      ImGui::Text("Draft Points: %u", static_cast<unsigned int>(convex_draft_points_.size()));
    }
    if (active_tool_ == ToolKind::OffMeshConnection) {
      if (ImGui::RadioButton("One Way", !off_mesh_bidirectional_)) {
        off_mesh_bidirectional_ = false;
      }
      if (ImGui::RadioButton("Bidirectional", off_mesh_bidirectional_)) {
        off_mesh_bidirectional_ = true;
      }
      ImGui::SliderFloat("Link Radius", &off_mesh_radius_, 0.1f, 2.0f);
    }
    if (active_tool_ == ToolKind::Crowd) {
      ensureCrowdComponent();
      drawCrowdToolUi();
    }
    if (ImGui::Button("Clear Markers")) {
      clearRuntimeMarkers();
      debug_paths_.clear();
    }
  }

  void drawAreaRadio(unsigned char area) {
    if (ImGui::RadioButton(areaName(area), convex_area_type_ == area)) {
      convex_area_type_ = area;
    }
  }

  void drawTesterModeRadio(NavmeshTesterMode mode) {
    if (ImGui::RadioButton(testerModeName(mode), tester_mode_ == mode)) {
      tester_mode_ = mode;
      recalculateTester();
    }
  }

  void drawTesterToolUi() {
    ImGui::Separator();
    drawTesterModeRadio(NavmeshTesterMode::PathfindFollow);
    drawTesterModeRadio(NavmeshTesterMode::PathfindStraight);
    if (tester_mode_ == NavmeshTesterMode::PathfindStraight) {
      ImGui::Indent();
      bool changed = false;
      changed |= ImGui::RadioButton("None", &straight_path_options_, navigation::NavStraightPathOptionNone);
      changed |= ImGui::RadioButton("Area", &straight_path_options_, navigation::NavStraightPathOptionAreaCrossings);
      changed |= ImGui::RadioButton("All", &straight_path_options_, navigation::NavStraightPathOptionAllCrossings);
      if (changed) {
        recalculateTester();
      }
      ImGui::Unindent();
    }
    drawTesterModeRadio(NavmeshTesterMode::PathfindSliced);
    drawTesterModeRadio(NavmeshTesterMode::DistanceToWall);
    drawTesterModeRadio(NavmeshTesterMode::Raycast);
    drawTesterModeRadio(NavmeshTesterMode::FindPolysInCircle);
    drawTesterModeRadio(NavmeshTesterMode::FindPolysInShape);
    drawTesterModeRadio(NavmeshTesterMode::FindLocalNeighbourhood);

    if (ImGui::Button("Set Random Start")) {
      setRandomTesterStart();
    }
    ImGui::SameLine();
    if (ImGui::Button("Set Random End")) {
      setRandomTesterEnd();
    }
    if (ImGui::Button("Make Random Points")) {
      makeRandomTesterPoints(false);
    }
    ImGui::SameLine();
    if (ImGui::Button("Make Random Points Around")) {
      makeRandomTesterPoints(true);
    }

    if (ImGui::TreeNode("Include Flags")) {
      drawFlagCheckbox("Walk##include", query_include_flags_, kSamplePolyFlagWalk);
      drawFlagCheckbox("Swim##include", query_include_flags_, kSamplePolyFlagSwim);
      drawFlagCheckbox("Door##include", query_include_flags_, kSamplePolyFlagDoor);
      drawFlagCheckbox("Jump##include", query_include_flags_, kSamplePolyFlagJump);
      ImGui::TreePop();
    }
    if (ImGui::TreeNode("Exclude Flags")) {
      drawFlagCheckbox("Walk##exclude", query_exclude_flags_, kSamplePolyFlagWalk);
      drawFlagCheckbox("Swim##exclude", query_exclude_flags_, kSamplePolyFlagSwim);
      drawFlagCheckbox("Door##exclude", query_exclude_flags_, kSamplePolyFlagDoor);
      drawFlagCheckbox("Jump##exclude", query_exclude_flags_, kSamplePolyFlagJump);
      drawFlagCheckbox("Disabled##exclude", query_exclude_flags_, kSamplePolyFlagDisabled);
      ImGui::TreePop();
    }
  }

  void drawFlagCheckbox(const char* label, uint16_t& flags, uint16_t flag) {
    bool enabled = hasFlag(flags, flag);
    if (ImGui::Checkbox(label, &enabled)) {
      toggleFlag(flags, flag);
      recalculateTester();
    }
  }

  void setRandomTesterStart() {
    if (!navUsable()) {
      return;
    }
    const auto& nav = world->get<components::NavMeshComponent>(nav_entity_);
    navigation::NavQuery query(nav.nav_mesh);
    math::Vec3 point{};
    if (query.findRandomPoint(point, sampleQueryFilter())) {
      tester_start_ = point;
      tester_has_start_ = true;
      marker(point, "Random Start", {0.15f, 0.75f, 0.95f, 1.0f});
      recalculateTester();
    }
  }

  void setRandomTesterEnd() {
    if (!navUsable()) {
      return;
    }
    const auto& nav = world->get<components::NavMeshComponent>(nav_entity_);
    navigation::NavQuery query(nav.nav_mesh);
    math::Vec3 point{};
    const bool found = tester_has_start_
        ? query.findRandomPointAroundCircle(tester_start_,
                                            tester_random_radius_,
                                            point,
                                            {2.0f, 4.0f, 2.0f},
                                            sampleQueryFilter())
        : query.findRandomPoint(point, sampleQueryFilter());
    if (found) {
      tester_end_ = point;
      tester_has_end_ = true;
      marker(point, "Random End", {0.95f, 0.90f, 0.20f, 1.0f});
      recalculateTester();
    }
  }

  void makeRandomTesterPoints(bool around_start) {
    tester_random_points_.clear();
    tester_random_in_circle_ = around_start;
    if (!navUsable() || (around_start && !tester_has_start_)) {
      return;
    }
    const auto& nav = world->get<components::NavMeshComponent>(nav_entity_);
    navigation::NavQuery query(nav.nav_mesh);
    for (int i = 0; i < 64; ++i) {
      math::Vec3 point{};
      const bool found = around_start
          ? query.findRandomPointAroundCircle(tester_start_,
                                              tester_random_radius_,
                                              point,
                                              {2.0f, 4.0f, 2.0f},
                                              sampleQueryFilter())
          : query.findRandomPoint(point, sampleQueryFilter());
      if (found) {
        tester_random_points_.push_back(point);
      }
    }
  }

  void drawCrowdModeRadio(CrowdToolMode mode) {
    if (ImGui::RadioButton(crowdModeName(mode), crowd_mode_ == mode)) {
      crowd_mode_ = mode;
    }
  }

  void drawCrowdToolUi() {
    ImGui::Separator();
    drawCrowdModeRadio(CrowdToolMode::CreateAgents);
    drawCrowdModeRadio(CrowdToolMode::MoveTarget);
    drawCrowdModeRadio(CrowdToolMode::SelectAgent);
    drawCrowdModeRadio(CrowdToolMode::TogglePolys);

    if (world->has<components::NavCrowdComponent>(nav_entity_)) {
      auto& crowd = world->get<components::NavCrowdComponent>(nav_entity_);
      if (ImGui::Button(crowd.simulation_paused ? "Run" : "Pause")) {
        crowd.simulation_paused = !crowd.simulation_paused;
      }
      ImGui::SameLine();
      if (ImGui::Button("Step")) {
        crowd.simulation_paused = true;
        crowd.step_requested = true;
      }
    }

    if (ImGui::TreeNode("Options")) {
      bool dirty = false;
      dirty |= ImGui::Checkbox("Optimize Visibility", &crowd_optimize_visibility_);
      dirty |= ImGui::Checkbox("Optimize Topology", &crowd_optimize_topology_);
      dirty |= ImGui::Checkbox("Anticipate Turns", &crowd_anticipate_turns_);
      dirty |= ImGui::Checkbox("Obstacle Avoidance", &crowd_obstacle_avoidance_);
      dirty |= ImGui::SliderInt("Avoidance Quality", &crowd_obstacle_avoidance_type_, 0, 3);
      dirty |= ImGui::Checkbox("Separation", &crowd_separation_);
      dirty |= ImGui::SliderFloat("Separation Weight", &crowd_separation_weight_, 0.0f, 20.0f, "%.2f");
      dirty |= ImGui::SliderFloat("Agent Radius", &crowd_agent_radius_, 0.1f, 2.0f, "%.2f");
      dirty |= ImGui::SliderFloat("Agent Height", &crowd_agent_height_, 0.5f, 4.0f, "%.2f");
      dirty |= ImGui::SliderFloat("Agent Speed", &crowd_agent_speed_, 0.1f, 8.0f, "%.2f");
      if (dirty) {
        updateCrowdAgentParams();
      }
      ImGui::TreePop();
    }
    if (ImGui::TreeNode("Selected Debug Draw")) {
      ImGui::Checkbox("Show Corners", &crowd_show_corners_);
      ImGui::Checkbox("Show Collision Segs", &crowd_show_collision_segments_);
      ImGui::Checkbox("Show Path", &crowd_show_path_);
      ImGui::Checkbox("Show VO", &crowd_show_vo_);
      ImGui::Checkbox("Show Path Optimization", &crowd_show_path_optimization_);
      ImGui::Checkbox("Show Neighbours", &crowd_show_neighbours_);
      ImGui::TreePop();
    }
    if (ImGui::TreeNode("Debug Draw")) {
      ImGui::Checkbox("Show Labels", &crowd_show_labels_);
      ImGui::Checkbox("Show Prox Grid", &crowd_show_grid_);
      ImGui::Checkbox("Show Nodes", &crowd_show_nodes_);
      ImGui::Checkbox("Show Perf Graph", &crowd_show_perf_graph_);
      ImGui::Checkbox("Show Detail All", &crowd_show_detail_all_);
      ImGui::TreePop();
    }
  }

  void drawToolRadio(ToolKind tool) {
    int current = static_cast<int>(active_tool_);
    const int value = static_cast<int>(tool);
    if (ImGui::RadioButton(toolName(tool), current == value)) {
      active_tool_ = tool;
    }
  }

  void drawDebugUi() {
    if (!ImGui::CollapsingHeader("Draw", ImGuiTreeNodeFlags_DefaultOpen)) {
      return;
    }
    const DebugModeOption* modes = kSoloDebugModes;
    size_t count = sizeof(kSoloDebugModes) / sizeof(kSoloDebugModes[0]);
    if (kind_ == RecastNavigationSampleKind::TileMesh) {
      modes = kTileDebugModes;
      count = sizeof(kTileDebugModes) / sizeof(kTileDebugModes[0]);
    } else if (kind_ == RecastNavigationSampleKind::TempObstacles) {
      modes = kTempObstacleDebugModes;
      count = sizeof(kTempObstacleDebugModes) / sizeof(kTempObstacleDebugModes[0]);
    }

    for (int i = 0; i < static_cast<int>(count); ++i) {
      if (ImGui::RadioButton(modes[i].label, debug_mode_index_ == i)) {
        debug_mode_index_ = i;
        applyDebugMode();
      }
    }
    ImGui::Checkbox("Show Input Mesh", &show_input_mesh_);
    applyMeshVisibility();
  }

  void drawStatusUi() {
    if (!ImGui::CollapsingHeader("Status", ImGuiTreeNodeFlags_DefaultOpen)) {
      return;
    }
    if (!world->isAlive(nav_entity_) || !world->has<components::NavMeshComponent>(nav_entity_)) {
      return;
    }
    const auto& nav = world->get<components::NavMeshComponent>(nav_entity_);
    ImGui::Text("Status: %s", navigation::navStatusName(nav.last_build_result.status));
    ImGui::Text("Polys: %u", nav.last_build_result.polygon_count);
    ImGui::Text("Triangles: %u", nav.last_build_result.triangle_count);
    ImGui::Text("Build Version: %llu", static_cast<unsigned long long>(nav.build_version));
    if (world->has<components::NavTileCacheComponent>(nav_entity_)) {
      const auto& cache = world->get<components::NavTileCacheComponent>(nav_entity_);
      ImGui::Text("Layers: %u", cache.last_build_result.layer_count);
      ImGui::Text("Cache Tiles: %u", cache.tile_cache.tileCount());
      ImGui::Text("Obstacles: %u", cache.tile_cache.obstacleCount());
      ImGui::Text("Compressed: %.1f kB", cache.last_build_result.compressed_bytes / 1024.0f);
      ImGui::Text("Raw: %.1f kB", cache.last_build_result.raw_bytes / 1024.0f);
    }
    if (world->has<components::NavCrowdComponent>(nav_entity_)) {
      const auto& crowd = world->get<components::NavCrowdComponent>(nav_entity_);
      ImGui::Text("Crowd Agents: %u", crowd.crowd.activeAgentCount());
    }
  }

  void applyDebugMode() {
    if (!world->has<components::NavMeshComponent>(nav_entity_)) {
      return;
    }
    auto& nav = world->get<components::NavMeshComponent>(nav_entity_);
    nav.debug_draw_mode = selectedDebugMode().mode;
    applyMeshVisibility();
  }

  void applyMeshVisibility() {
    if (!world->isAlive(mesh_entity_) || !world->has<components::MeshComponent>(mesh_entity_)) {
      return;
    }
    auto& mesh = world->get<components::MeshComponent>(mesh_entity_);
    const bool visible = show_input_mesh_ || selectedDebugMode().mesh_only;
    mesh.visible = visible;
    mesh.shadow_visible = visible;
  }

  void drawDebug() {
    if (graphics == nullptr) {
      return;
    }
    if (auto* nav_system = navigationSystem()) {
      nav_system->debugDraw(*world, *graphics, false);
    }
    if (navUsable()) {
      const auto& nav = world->get<components::NavMeshComponent>(nav_entity_);
      if (!tester_polys_.empty()) {
        nav.nav_mesh.debugDrawPolygons(*graphics,
                                       tester_polys_,
                                       {0.02f, 0.02f, 0.02f, 0.55f},
                                       false);
      }
      if (!toggled_polys_.empty()) {
        nav.nav_mesh.debugDrawPolygons(*graphics,
                                       toggled_polys_,
                                       {0.95f, 0.15f, 0.12f, 0.65f},
                                       false);
      }
      for (size_t i = 0; i < tester_polys_.size() && i < tester_parent_polys_.size(); ++i) {
        if (tester_parent_polys_[i] == 0) {
          continue;
        }
        math::Vec3 parent{};
        math::Vec3 child{};
        if (nav.nav_mesh.polyCenter(tester_parent_polys_[i], parent) &&
            nav.nav_mesh.polyCenter(tester_polys_[i], child)) {
          graphics->drawLine(yOffset(parent, 0.25f),
                             yOffset(child, 0.25f),
                             {0.0f, 0.0f, 0.0f, 0.55f},
                             false,
                             1.0f);
        }
      }
    }
    for (const navigation::NavPath& path : debug_paths_) {
      navigation::NavQuery::debugDrawPath(*graphics, path, {0.18f, 0.95f, 0.90f, 1.0f}, false);
    }
    if (tester_has_start_ &&
        (tester_mode_ == NavmeshTesterMode::FindPolysInCircle ||
         tester_mode_ == NavmeshTesterMode::FindLocalNeighbourhood)) {
      const float radius = tester_mode_ == NavmeshTesterMode::FindLocalNeighbourhood
          ? tester_neighbourhood_radius_
          : tester_circle_radius_;
      drawCircle(*graphics,
                 yOffset(tester_start_, agent_height_ * 0.5f),
                 radius,
                 {0.25f, 0.06f, 0.0f, 0.9f},
                 false,
                 2.0f);
    }
    if (tester_mode_ == NavmeshTesterMode::FindPolysInShape && tester_shape_.size() >= 2) {
      for (size_t i = 0; i < tester_shape_.size(); ++i) {
        const math::Vec3& a = tester_shape_[i];
        const math::Vec3& b = tester_shape_[(i + 1) % tester_shape_.size()];
        graphics->drawLine(a, b, {0.25f, 0.06f, 0.0f, 0.9f}, false, 2.0f);
      }
    }
    if (tester_has_wall_hit_ && tester_mode_ == NavmeshTesterMode::DistanceToWall) {
      drawCircle(*graphics,
                 yOffset(tester_start_, agent_height_ * 0.5f),
                 tester_distance_to_wall_,
                 {0.25f, 0.06f, 0.0f, 0.9f},
                 false,
                 2.0f);
      graphics->drawLine(yOffset(tester_wall_hit_, 0.02f),
                         yOffset(tester_wall_hit_, agent_height_),
                         {0.0f, 0.0f, 0.0f, 0.9f},
                         false,
                         3.0f);
      graphics->drawLine(yOffset(tester_wall_hit_, 0.4f),
                         yOffset(math::add(tester_wall_hit_,
                                           math::scale(tester_wall_normal_, agent_radius_)),
                                 0.4f),
                         {0.0f, 0.0f, 0.0f, 0.9f},
                         false,
                         2.0f);
    }
    if (tester_has_wall_hit_ && tester_mode_ == NavmeshTesterMode::Raycast) {
      drawCross(*graphics, yOffset(tester_wall_hit_, 0.35f), 0.35f, {0.0f, 0.0f, 0.0f, 0.9f}, false, 2.0f);
    }
    for (const navigation::NavWallSegment& segment : tester_wall_segments_) {
      const math::Color color = segment.neighbor_ref == 0
          ? math::Color{0.75f, 0.12f, 0.06f, 0.95f}
          : math::Color{1.0f, 1.0f, 1.0f, 0.35f};
      graphics->drawLine(yOffset(segment.start, agent_max_climb_),
                         yOffset(segment.end, agent_max_climb_),
                         color,
                         false,
                         2.0f);
    }
    for (const math::Vec3& point : tester_random_points_) {
      drawCross(*graphics, yOffset(point, 0.15f), 0.18f, {0.86f, 0.12f, 0.06f, 1.0f}, false, 2.0f);
    }
    if (tester_random_in_circle_ && tester_has_start_) {
      drawCircle(*graphics,
                 yOffset(tester_start_, agent_height_ * 0.5f),
                 tester_random_radius_,
                 {0.25f, 0.06f, 0.0f, 0.9f},
                 false,
                 1.0f);
    }
    drawConvexDraftDebug();
    drawCrowdExtraDebug();
    if (has_highlighted_tile_) {
      graphics->drawLine(yOffset(highlighted_tile_point_, 0.05f),
                         yOffset(highlighted_tile_point_, 4.0f),
                         {0.96f, 0.78f, 0.18f, 1.0f},
                         false,
                         2.0f);
    }
  }

  void drawConvexDraftDebug() {
    if (graphics == nullptr) {
      return;
    }
    if (convex_draft_hull_.size() >= 2) {
      for (size_t i = 0; i < convex_draft_hull_.size(); ++i) {
        const math::Vec3& a = convex_draft_hull_[i];
        const math::Vec3& b = convex_draft_hull_[(i + 1) % convex_draft_hull_.size()];
        graphics->drawLine(yOffset(a, 0.12f),
                           yOffset(b, 0.12f),
                           {0.74f, 0.30f, 0.90f, 1.0f},
                           false,
                           2.0f);
      }
    }
    for (const ConvexVolumeRecord& volume : convex_volumes_) {
      if (volume.vertices.size() < 2) {
        continue;
      }
      for (size_t i = 0; i < volume.vertices.size(); ++i) {
        const math::Vec3& a = volume.vertices[i];
        const math::Vec3& b = volume.vertices[(i + 1) % volume.vertices.size()];
        graphics->drawLine({a.x, volume.min_y, a.z},
                           {b.x, volume.min_y, b.z},
                           {0.74f, 0.30f, 0.90f, 0.85f},
                           false,
                           2.0f);
        graphics->drawLine({a.x, volume.max_y, a.z},
                           {b.x, volume.max_y, b.z},
                           {0.74f, 0.30f, 0.90f, 0.85f},
                           false,
                           2.0f);
        graphics->drawLine({a.x, volume.min_y, a.z},
                           {a.x, volume.max_y, a.z},
                           {0.74f, 0.30f, 0.90f, 0.65f},
                           false,
                           1.0f);
      }
    }
  }

  void drawCrowdExtraDebug() {
    if (graphics == nullptr || active_tool_ != ToolKind::Crowd) {
      return;
    }
    for (const auto& [key, trail] : agent_trails_) {
      (void)key;
      for (size_t i = 1; i < trail.points.size(); ++i) {
        const float alpha = static_cast<float>(i) / static_cast<float>(trail.points.size());
        graphics->drawLine(yOffset(trail.points[i - 1], 0.1f),
                           yOffset(trail.points[i], 0.1f),
                           {0.0f, 0.0f, 0.0f, 0.25f + alpha * 0.35f},
                           false,
                           2.0f);
      }
    }
    if (selected_crowd_agent_.isValid() &&
        world->isAlive(selected_crowd_agent_) &&
        world->has<components::TransformComponent>(selected_crowd_agent_) &&
        world->has<components::NavCrowdAgentComponent>(selected_crowd_agent_)) {
      const auto& transform = world->get<components::TransformComponent>(selected_crowd_agent_);
      const auto& agent = world->get<components::NavCrowdAgentComponent>(selected_crowd_agent_);
      drawCircle(*graphics,
                 yOffset(transform.getPosition(), -agent.height_offset + 0.03f),
                 agent.params.radius * 1.6f,
                 {1.0f, 0.1f, 0.05f, 1.0f},
                 false,
                 3.0f);
    }
  }

  RecastNavigationSampleKind kind_;
  MeshGeometry asset_;
  TestCaseFile nav_tests_;
  TestCaseFile ray_tests_;
  math::Vec3 mesh_offset_{};
  uint32_t source_mask_ = 1u;

  ecs::Entity mesh_entity_{};
  ecs::Entity nav_entity_{};
  ecs::Entity camera_entity_{};
  std::vector<ecs::Entity> helper_entities_;
  std::vector<ecs::Entity> obstacles_;
  std::vector<ecs::Entity> crowd_agents_;
  std::vector<OffMeshLinkRecord> off_mesh_links_;
  std::vector<ConvexVolumeRecord> convex_volumes_;
  std::vector<ecs::Entity> convex_draft_markers_;
  std::vector<navigation::NavPath> debug_paths_;
  std::unordered_map<uint64_t, AgentTrail> agent_trails_;

  ToolKind active_tool_ = ToolKind::NavmeshTester;
  uint64_t observed_build_version_ = 0;
  bool reported_build_result_ = false;
  navigation::NavStatus last_reported_status_ = navigation::NavStatus::BuildFailed;
  uint32_t last_reported_polygons_ = 0;
  uint32_t last_reported_triangles_ = 0;
  int debug_mode_index_ = 1;
  bool show_input_mesh_ = true;
  bool keep_intermediate_results_ = false;

  float cell_size_ = 0.3f;
  float cell_height_ = 0.2f;
  float agent_height_ = 2.0f;
  float agent_radius_ = 0.6f;
  float agent_max_climb_ = 0.9f;
  float agent_max_slope_ = 45.0f;
  float region_min_size_ = 8.0f;
  float region_merge_size_ = 20.0f;
  float edge_max_len_ = 12.0f;
  float edge_max_error_ = 1.3f;
  int verts_per_poly_ = 6;
  float detail_sample_dist_ = 6.0f;
  float detail_sample_max_error_ = 1.0f;
  int partition_index_ = 0;
  int tile_size_ = 32;

  bool tester_has_start_ = false;
  bool tester_has_end_ = false;
  math::Vec3 tester_start_{};
  math::Vec3 tester_end_{};
  NavmeshTesterMode tester_mode_ = NavmeshTesterMode::PathfindFollow;
  int straight_path_options_ = navigation::NavStraightPathOptionNone;
  uint16_t query_include_flags_ = static_cast<uint16_t>(kSamplePolyFlagsAll ^ kSamplePolyFlagDisabled);
  uint16_t query_exclude_flags_ = 0;
  float tester_neighbourhood_radius_ = 12.0f;
  float tester_random_radius_ = 18.0f;
  float tester_circle_radius_ = 0.0f;
  float tester_distance_to_wall_ = 0.0f;
  bool tester_has_wall_hit_ = false;
  bool tester_random_in_circle_ = false;
  math::Vec3 tester_wall_hit_{};
  math::Vec3 tester_wall_normal_{};
  std::vector<uint64_t> tester_polys_;
  std::vector<uint64_t> tester_parent_polys_;
  std::vector<math::Vec3> tester_shape_;
  std::vector<math::Vec3> tester_random_points_;
  std::vector<navigation::NavWallSegment> tester_wall_segments_;
  std::unique_ptr<navigation::NavQuery> sliced_query_;
  bool sliced_active_ = false;

  bool off_mesh_has_start_ = false;
  math::Vec3 off_mesh_start_{};
  ecs::Entity off_mesh_start_entity_{};
  float off_mesh_radius_ = 0.8f;
  bool off_mesh_bidirectional_ = true;
  uint32_t off_mesh_user_id_ = 1000;
  std::vector<math::Vec3> convex_draft_points_;
  std::vector<math::Vec3> convex_draft_hull_;
  float convex_volume_height_ = 6.0f;
  float convex_volume_descent_ = 1.0f;
  float convex_poly_offset_ = 0.0f;
  unsigned char convex_area_type_ = kSampleAreaGrass;

  navigation::NavTileCacheObstacleShape obstacle_shape_ =
      navigation::NavTileCacheObstacleShape::Cylinder;
  float obstacle_half_extents_[3] = {1.0f, 1.0f, 1.0f};
  float obstacle_radius_ = 1.0f;
  float obstacle_height_ = 2.0f;
  float obstacle_yaw_ = 0.0f;
  CrowdToolMode crowd_mode_ = CrowdToolMode::CreateAgents;
  ecs::Entity selected_crowd_agent_{};
  math::Vec3 crowd_target_{};
  bool crowd_target_set_ = false;
  float crowd_agent_radius_ = 0.6f;
  float crowd_agent_height_ = 2.0f;
  float crowd_agent_speed_ = 3.5f;
  bool crowd_optimize_visibility_ = true;
  bool crowd_optimize_topology_ = true;
  bool crowd_anticipate_turns_ = true;
  bool crowd_obstacle_avoidance_ = true;
  int crowd_obstacle_avoidance_type_ = 3;
  bool crowd_separation_ = false;
  float crowd_separation_weight_ = 2.0f;
  bool crowd_show_corners_ = false;
  bool crowd_show_collision_segments_ = false;
  bool crowd_show_path_ = false;
  bool crowd_show_vo_ = false;
  bool crowd_show_path_optimization_ = false;
  bool crowd_show_neighbours_ = false;
  bool crowd_show_labels_ = false;
  bool crowd_show_grid_ = false;
  bool crowd_show_nodes_ = false;
  bool crowd_show_perf_graph_ = false;
  bool crowd_show_detail_all_ = false;
  std::vector<uint64_t> toggled_polys_;

  bool has_highlighted_tile_ = false;
  math::Vec3 highlighted_tile_point_{};
  float camera_yaw_ = 0.0f;
  float camera_pitch_ = -0.55f;
};

int runRecastNavigationSample(RecastNavigationSampleKind kind) {
  app::EngineApp engine;
  RecastNavigationSampleApp game(kind);
  engine.setUi(imgui::createUiLayer([&game](app::UIContext& ctx) { game.drawUi(ctx); }));

  app::EngineConfig config;
  config.window.title = std::string("Karma Recast ") + recastNavigationSampleName(kind);
  config.window.width = 1440;
  config.window.height = 900;
  config.window.samples = 1;
  config.cursor_visible = true;
  config.enable_anisotropy = true;
  config.anisotropy_level = 16;
  config.generate_mipmaps = true;
  config.forward_plus_tile_size = 16;
  config.forward_plus_max_lights_per_tile = 128;
  config.shadow_map_size = 2048;
  config.shadow_pcf_radius = 1;
  config.lighting_exposure = 1.05f;

  engine.start(game, config);
  while (engine.isRunning()) {
    engine.tick();
  }
  return EXIT_SUCCESS;
}

}  // namespace karma::demo
