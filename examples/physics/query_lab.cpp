#include <algorithm>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "physics_example_common.h"
#include "karma/ui_imgui.h"

#include <imgui.h>

namespace karma::demo::physics_examples {

namespace {

constexpr const char* kAllShapeNames[] = {
    "Box", "Sphere", "Capsule", "Cylinder", "Tapered capsule",
    "Convex hull", "Triangle", "Mesh wedge", "Height field", "Compound"};
constexpr const char* kCastShapeNames[] = {
    "Box", "Sphere", "Capsule", "Cylinder", "Tapered capsule", "Convex hull",
    "Triangle", "Compound"};
constexpr const char* kBackFaceNames[] = {"Ignore", "Collide"};

bool dragVec3(const char* label, math::Vec3& value, float speed = 0.05f) {
  float data[3] = {value.x, value.y, value.z};
  if (!ImGui::DragFloat3(label, data, speed)) {
    return false;
  }
  value = {data[0], data[1], data[2]};
  return true;
}

physics::PhysicsBackFaceMode backFaceMode(int mode) {
  return mode == 1 ? physics::PhysicsBackFaceMode::Collide : physics::PhysicsBackFaceMode::Ignore;
}

physics::PhysicsShapeDesc shapeForIndex(int index) {
  switch (index) {
    case 0: return makeBoxShape({0.65f, 0.65f, 0.65f});
    case 1: return makeSphereShape(0.75f);
    case 2: return makeCapsuleShape(0.35f, 1.55f);
    case 3: return makeCylinderShape(0.55f, 1.35f);
    case 4: return makeTaperedCapsuleShape(0.25f, 0.58f, 1.65f);
    case 5: return makeConvexHullShape(0.8f);
    case 6: return makeTriangleShape(0.9f);
    case 7: return makeMeshWedgeShape(0.85f);
    case 8: return makeHeightFieldShape(5, 0.42f, 0.55f);
    default: return makeCompoundShape();
  }
}

uint32_t queryMask(bool ground, bool dynamics, bool ramps, bool sensors) {
  uint32_t mask = 0u;
  if (ground) mask |= 1u;
  if (dynamics) mask |= 2u;
  if (ramps) mask |= 4u;
  if (sensors) mask |= 16u;
  return mask;
}

struct QueryState {
  bool reset_requested = false;
  bool include_sensors = true;
  bool treat_convex_as_solid = true;
  bool cast_shrunken = false;
  bool cast_deepest = false;
  bool mask_ground = true;
  bool mask_dynamics = true;
  bool mask_ramps = true;
  bool mask_sensors = true;
  int ray_back_face = 0;
  int shape_back_face = 0;
  int cast_triangle_back_face = 0;
  int cast_convex_back_face = 0;
  int overlap_shape = 0;
  int cast_shape = 1;
  std::uintptr_t ignored_body = 0;
  math::Vec3 ray_from{-11.0f, 6.0f, 6.0f};
  math::Vec3 ray_to{8.0f, 0.4f, -4.0f};
  math::Vec3 point{0.0f, 1.0f, 0.0f};
  math::Vec3 overlap_position{0.0f, 1.1f, 0.0f};
  math::Vec3 shape_scale{1.0f, 1.0f, 1.0f};
  math::Vec3 cast_from{-9.0f, 3.2f, 3.0f};
  math::Vec3 cast_translation{17.0f, -2.0f, -4.0f};
  float max_separation = 0.02f;
  bool ray_hit = false;
  physics::PhysicsQueryHit closest_ray{};
  std::vector<physics::PhysicsQueryHit> ray_hits;
  std::vector<physics::PhysicsQueryHit> point_hits;
  std::vector<physics::PhysicsQueryHit> overlap_hits;
  std::vector<physics::PhysicsQueryHit> cast_hits;
};

class QueryUi final {
 public:
  explicit QueryUi(std::shared_ptr<QueryState> state) : state_(std::move(state)) {}

  void draw(app::UIContext&) {
    ImGui::SetNextWindowPos(ImVec2(18.0f, 18.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(405.0f, 0.0f), ImGuiCond_FirstUseEver);
    ImGui::Begin("Physics Query Lab");
    ImGui::TextUnformatted("WASD + QE fly, hold RMB to look");
    ImGui::Separator();
    if (ImGui::Button("Reset Scene")) {
      state_->reset_requested = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Ignore closest") && state_->ray_hit) {
      state_->ignored_body = state_->closest_ray.body;
    }
    ImGui::SameLine();
    if (ImGui::Button("Clear ignore")) {
      state_->ignored_body = 0;
    }
    ImGui::Checkbox("Include sensors", &state_->include_sensors);
    ImGui::Checkbox("Treat convex as solid", &state_->treat_convex_as_solid);
    ImGui::Checkbox("Ground mask", &state_->mask_ground);
    ImGui::SameLine();
    ImGui::Checkbox("Dynamics", &state_->mask_dynamics);
    ImGui::Checkbox("Ramps/terrain", &state_->mask_ramps);
    ImGui::SameLine();
    ImGui::Checkbox("Sensors", &state_->mask_sensors);
    ImGui::Text("Ignored body: %s", handleLabel(state_->ignored_body).c_str());
    ImGui::SeparatorText("Ray");
    dragVec3("Ray from", state_->ray_from);
    dragVec3("Ray to", state_->ray_to);
    ImGui::Combo("Ray back faces", &state_->ray_back_face, kBackFaceNames,
                 IM_ARRAYSIZE(kBackFaceNames));
    ImGui::Text("Ray hits: %zu closest: %s", state_->ray_hits.size(),
                state_->ray_hit ? handleLabel(state_->closest_ray.body).c_str() : "none");
    ImGui::SeparatorText("Point / Overlap");
    dragVec3("Point", state_->point);
    ImGui::Combo("Overlap shape", &state_->overlap_shape, kAllShapeNames,
                 IM_ARRAYSIZE(kAllShapeNames));
    dragVec3("Overlap pos", state_->overlap_position);
    dragVec3("Shape scale", state_->shape_scale, 0.025f);
    ImGui::SliderFloat("Max separation", &state_->max_separation, 0.0f, 0.5f, "%.3f");
    ImGui::Combo("Shape back faces", &state_->shape_back_face, kBackFaceNames,
                 IM_ARRAYSIZE(kBackFaceNames));
    ImGui::Text("Point hits: %zu overlap hits: %zu", state_->point_hits.size(),
                state_->overlap_hits.size());
    ImGui::SeparatorText("Shape Cast");
    ImGui::Combo("Cast shape", &state_->cast_shape, kCastShapeNames,
                 IM_ARRAYSIZE(kCastShapeNames));
    dragVec3("Cast from", state_->cast_from);
    dragVec3("Cast translation", state_->cast_translation);
    ImGui::Checkbox("Shrunken shape", &state_->cast_shrunken);
    ImGui::SameLine();
    ImGui::Checkbox("Deepest point", &state_->cast_deepest);
    ImGui::Combo("Cast triangle faces", &state_->cast_triangle_back_face, kBackFaceNames,
                 IM_ARRAYSIZE(kBackFaceNames));
    ImGui::Combo("Cast convex faces", &state_->cast_convex_back_face, kBackFaceNames,
                 IM_ARRAYSIZE(kBackFaceNames));
    ImGui::Text("Cast hits: %zu", state_->cast_hits.size());
    ImGui::End();
  }

 private:
  std::shared_ptr<QueryState> state_;
};

class QueryLabGame final : public app::GameInterface {
 public:
  explicit QueryLabGame(std::shared_ptr<QueryState> state) : state_(std::move(state)) {}

  void onStart() override {
    bindFlyCameraControls(*input);
    addDefaultLighting(*world, assets);
    createFlyCamera(*world, camera_, {0.0f, 7.5f, 20.0f}, 3.14159f, -0.34f);
    resetScene();
  }

  void onFixedUpdate(float) override {
    physics->setGravity(-9.8f);
  }

  void onUpdate(float dt) override {
    if (state_->reset_requested) {
      state_->reset_requested = false;
      resetScene();
    }
    updateFlyCamera(*world, *input, camera_, dt);
    runQueries();
    if (graphics) {
      drawScene();
    }
  }

  void onShutdown() override {
    destroyEntities(*world, entities_);
  }

 private:
  void resetScene() {
    destroyEntities(*world, entities_);
    entities_.push_back(addStaticBox(*world, {0.0f, -0.35f, 0.0f}, {24.0f, 0.35f, 16.0f}, 1u,
                                     0xFFFFFFFFu));
    addLayeredBox("Layer 2 box", {-5.5f, 1.2f, 0.0f}, {0.85f, 1.2f, 0.85f}, 2u, false);
    addLayeredBox("Layer 2 tall", {-2.0f, 1.8f, -2.0f}, {0.7f, 1.8f, 0.7f}, 2u, false);
    addLayeredSphere("Layer 4 sphere", {2.2f, 1.0f, 0.8f}, 0.95f, 4u, false);
    addLayeredSphere("Sensor sphere", {5.8f, 1.1f, -0.8f}, 1.0f, 16u, true);
    addTriangleRamp({-6.0f, 0.15f, -6.0f});
    addMeshWedge({0.5f, 0.25f, -6.5f});
    addHeightField({7.0f, 0.0f, -5.0f});
  }

  void addLayeredBox(const char* name,
                     const math::Vec3& position,
                     const math::Vec3& half_extents,
                     uint32_t layer,
                     bool sensor) {
    auto entity = world->createEntity();
    world->setName(entity, name);
    setTransform(*world, entity, position);
    world->add(entity,
               components::ColliderComponent::box(
                   components::BoxColliderShape{.half_extents = half_extents},
                   sensor,
                   true));
    components::PhysicsCollisionFilterComponent filter{};
    filter.layers = layer;
    filter.collides_with = 0xFFFFFFFFu;
    world->add(entity, filter);
    entities_.push_back(entity);
  }

  void addLayeredSphere(const char* name,
                        const math::Vec3& position,
                        float radius,
                        uint32_t layer,
                        bool sensor) {
    auto entity = world->createEntity();
    world->setName(entity, name);
    setTransform(*world, entity, position);
    world->add(entity,
               components::ColliderComponent::sphere(
                   components::SphereColliderShape{.radius = radius},
                   sensor,
                   true));
    components::PhysicsCollisionFilterComponent filter{};
    filter.layers = layer;
    filter.collides_with = 0xFFFFFFFFu;
    world->add(entity, filter);
    entities_.push_back(entity);
  }

  void addTriangleRamp(const math::Vec3& position) {
    auto entity = world->createEntity();
    setTransform(*world, entity, position, axisAngle({1.0f, 0.0f, 0.0f}, -0.28f));
    components::TriangleColliderShape triangle{};
    triangle.points = {{{-2.4f, 0.0f, -1.6f}, {2.4f, 0.0f, -1.6f}, {0.0f, 0.0f, 2.2f}}};
    triangle.convex_radius = 0.02f;
    world->add(entity, components::ColliderComponent::triangle(triangle, false, true));
    addFilter(entity, 4u);
    entities_.push_back(entity);
  }

  void addMeshWedge(const math::Vec3& position) {
    auto entity = world->createEntity();
    setTransform(*world, entity, position);
    components::MeshColliderShape mesh{};
    const auto shape = makeMeshWedgeShape(1.75f);
    for (const glm::vec3& vertex : shape.mesh_vertices) {
      mesh.vertices.push_back(math::fromGlm(vertex));
    }
    mesh.indices = shape.mesh_indices;
    world->add(entity, components::ColliderComponent::mesh(std::move(mesh), false, true));
    addFilter(entity, 4u);
    entities_.push_back(entity);
  }

  void addHeightField(const math::Vec3& position) {
    auto entity = world->createEntity();
    setTransform(*world, entity, position);
    const auto shape = makeHeightFieldShape(9, 0.65f, 1.0f);
    components::HeightFieldColliderShape height{};
    height.samples = shape.height_samples;
    height.sample_count = shape.height_sample_count;
    height.offset = math::fromGlm(shape.height_offset);
    height.scale = math::fromGlm(shape.height_scale);
    height.block_size = shape.height_block_size;
    height.bits_per_sample = shape.height_bits_per_sample;
    world->add(entity, components::ColliderComponent::heightField(std::move(height), false, true));
    addFilter(entity, 4u);
    entities_.push_back(entity);
  }

  void addFilter(world::Entity entity, uint32_t layer) {
    components::PhysicsCollisionFilterComponent filter{};
    filter.layers = layer;
    filter.collides_with = 0xFFFFFFFFu;
    world->add(entity, filter);
  }

  physics::PhysicsQueryFilter filter() const {
    physics::PhysicsQueryFilter result{};
    result.collision_mask = queryMask(state_->mask_ground, state_->mask_dynamics, state_->mask_ramps,
                                      state_->mask_sensors);
    result.include_sensors = state_->include_sensors;
    result.ignored_body = state_->ignored_body;
    return result;
  }

  void runQueries() {
    state_->ray_hits.clear();
    state_->point_hits.clear();
    state_->overlap_hits.clear();
    state_->cast_hits.clear();

    physics::PhysicsRaycastDesc ray{};
    ray.from = math::toGlm(state_->ray_from);
    ray.to = math::toGlm(state_->ray_to);
    ray.filter = filter();
    ray.back_face_mode = backFaceMode(state_->ray_back_face);
    ray.treat_convex_as_solid = state_->treat_convex_as_solid;
    state_->ray_hit = physics->castRay(ray, state_->closest_ray);
    physics->castRayAll(ray, state_->ray_hits);

    physics->collidePoint(math::toGlm(state_->point), filter(), state_->point_hits);

    physics::PhysicsShapeQueryDesc overlap{};
    overlap.shape = shapeForIndex(state_->overlap_shape);
    overlap.position = math::toGlm(state_->overlap_position);
    overlap.scale = math::toGlm(state_->shape_scale);
    overlap.filter = filter();
    overlap.back_face_mode = backFaceMode(state_->shape_back_face);
    overlap.max_separation_distance = state_->max_separation;
    physics->collideShape(overlap, state_->overlap_hits);

    physics::PhysicsShapeCastDesc cast{};
    cast.shape = shapeForIndex(state_->cast_shape == 7 ? 9 : state_->cast_shape);
    cast.from = math::toGlm(state_->cast_from);
    cast.translation = math::toGlm(state_->cast_translation);
    cast.scale = math::toGlm(state_->shape_scale);
    cast.filter = filter();
    cast.back_face_mode_triangles = backFaceMode(state_->cast_triangle_back_face);
    cast.back_face_mode_convex = backFaceMode(state_->cast_convex_back_face);
    cast.use_shrunken_shape_and_convex_radius = state_->cast_shrunken;
    cast.return_deepest_point = state_->cast_deepest;
    physics->castShape(cast, state_->cast_hits);
  }

  void drawHits(const std::vector<physics::PhysicsQueryHit>& hits,
                const math::Color& point_color,
                const math::Color& normal_color) {
    for (const physics::PhysicsQueryHit& hit : hits) {
      const math::Vec3 point = math::fromGlm(hit.point);
      drawWireSphere(*graphics, point, {}, 0.08f, point_color, false, 2.0f);
      graphics->drawLine(point, vadd(point, vscale(math::fromGlm(hit.normal), 0.55f)),
                         normal_color, false, 2.0f);
    }
  }

  void drawScene() {
    drawReference(*graphics, 22.0f);
    for (world::Entity entity : entities_) {
      if (!world->isAlive(entity)) {
        continue;
      }
      math::Color color{0.75f, 0.78f, 0.82f, 0.82f};
      if (world->has<components::PhysicsCollisionFilterComponent>(entity)) {
        const auto& filter_component = world->get<components::PhysicsCollisionFilterComponent>(entity);
        if ((filter_component.layers & 16u) != 0u) {
          color = {1.0f, 0.55f, 0.18f, 0.9f};
        } else if ((filter_component.layers & 2u) != 0u) {
          color = {0.25f, 0.9f, 1.0f, 0.9f};
        } else if ((filter_component.layers & 4u) != 0u) {
          color = {0.85f, 0.85f, 0.45f, 0.85f};
        }
      }
      drawEntityColliders(*graphics, *world, entity, color, true, 1.4f);
    }

    graphics->drawLine(state_->ray_from, state_->ray_to, {1.0f, 1.0f, 1.0f, 1.0f}, false, 2.5f);
    drawWireSphere(*graphics, state_->point, {}, 0.12f, {0.1f, 1.0f, 0.35f, 1.0f}, false, 2.0f);

    const auto overlap_shape = shapeForIndex(state_->overlap_shape);
    drawShapeDesc(*graphics, overlap_shape, state_->overlap_position, {}, {0.25f, 0.7f, 1.0f, 1.0f},
                  false, 2.0f);

    const int cast_shape_index = state_->cast_shape == 7 ? 9 : state_->cast_shape;
    const auto cast_shape = shapeForIndex(cast_shape_index);
    const math::Vec3 cast_to = vadd(state_->cast_from, state_->cast_translation);
    drawShapeDesc(*graphics, cast_shape, state_->cast_from, {}, {1.0f, 0.55f, 0.1f, 0.9f},
                  false, 1.8f);
    drawShapeDesc(*graphics, cast_shape, cast_to, {}, {1.0f, 0.18f, 0.12f, 0.9f}, false, 1.2f);
    graphics->drawLine(state_->cast_from, cast_to, {1.0f, 0.35f, 0.1f, 1.0f}, false, 2.5f);

    drawHits(state_->ray_hits, {1.0f, 1.0f, 0.1f, 1.0f}, {1.0f, 0.6f, 0.1f, 1.0f});
    drawHits(state_->point_hits, {0.15f, 1.0f, 0.35f, 1.0f}, {0.15f, 1.0f, 0.35f, 1.0f});
    drawHits(state_->overlap_hits, {0.25f, 0.7f, 1.0f, 1.0f}, {0.25f, 0.7f, 1.0f, 1.0f});
    drawHits(state_->cast_hits, {1.0f, 0.2f, 0.1f, 1.0f}, {1.0f, 0.2f, 0.1f, 1.0f});
  }

  std::shared_ptr<QueryState> state_;
  CameraRig camera_{};
  std::vector<world::Entity> entities_;
};

}  // namespace

}  // namespace karma::demo::physics_examples

int main() {
  karma::app::EngineApp engine;
  auto state = std::make_shared<karma::demo::physics_examples::QueryState>();
  karma::demo::physics_examples::QueryLabGame game(state);
  auto ui = std::make_shared<karma::demo::physics_examples::QueryUi>(state);
  engine.setUi(karma::ui::imgui::createUiLayer(
      [ui](karma::app::UIContext& ctx) { ui->draw(ctx); }));

  karma::app::EngineConfig config;
  config.window.title = "Physics Query Lab";
  config.window.samples = 1;
  config.cursor_visible = true;
  config.enable_anisotropy = true;
  config.anisotropy_level = 16;
  config.generate_mipmaps = true;
  config.shadow_map_size = 2048;
  config.shadow_pcf_radius = 1;

  engine.start(game, config);
  while (engine.isRunning()) {
    engine.tick();
  }
  return 0;
}
