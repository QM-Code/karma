#include <algorithm>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "physics_example_common.h"
#include "karma/ui.h"

#include <imgui.h>

namespace karma::demo::physics_examples {

namespace {

struct GalleryState {
  bool reset_requested = false;
  bool kick_selected = false;
  bool scatter_all = false;
  bool continuous_forces = false;
  bool use_ccd = true;
  bool allow_sleeping = false;
  bool apply_gyroscopic_force = true;
  bool use_manifold_reduction = true;
  bool selected_is_trigger = false;
  int selected = 0;
  int dof_mode = 0;
  int layer_mode = 0;
  float gravity = 9.8f;
  float mass = 1.0f;
  float friction = 0.55f;
  float restitution = 0.2f;
  float impulse = 8.0f;
  math::Vec3 selected_position{};
  math::Vec3 selected_velocity{};
  bool selected_grounded = false;
  int selected_active_contacts = 0;
  int selected_entered_contacts = 0;
  std::string selected_name;
};

class GalleryUi final {
 public:
  explicit GalleryUi(std::shared_ptr<GalleryState> state) : state_(std::move(state)) {}

  void draw(app::UIContext&) {
    ImGui::SetNextWindowPos(ImVec2(18.0f, 18.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(360.0f, 0.0f), ImGuiCond_FirstUseEver);
    ImGui::Begin("Physics Shape Gallery");
    ImGui::TextUnformatted("WASD + QE fly, hold RMB to look");
    ImGui::Separator();
    if (ImGui::Button("Reset")) {
      state_->reset_requested = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Kick")) {
      state_->kick_selected = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Scatter")) {
      state_->scatter_all = true;
    }
    ImGui::SliderFloat("Gravity", &state_->gravity, 0.0f, 30.0f, "%.2f");
    ImGui::SliderFloat("Mass", &state_->mass, 0.1f, 12.0f, "%.2f");
    ImGui::SliderFloat("Friction", &state_->friction, 0.0f, 1.0f, "%.2f");
    ImGui::SliderFloat("Restitution", &state_->restitution, 0.0f, 1.0f, "%.2f");
    ImGui::SliderFloat("Impulse", &state_->impulse, 0.0f, 35.0f, "%.1f");
    ImGui::Checkbox("Continuous forces", &state_->continuous_forces);
    ImGui::Checkbox("Linear cast CCD", &state_->use_ccd);
    ImGui::Checkbox("Allow sleeping", &state_->allow_sleeping);
    ImGui::Checkbox("Gyroscopic force", &state_->apply_gyroscopic_force);
    ImGui::Checkbox("Manifold reduction", &state_->use_manifold_reduction);
    ImGui::Checkbox("Selected trigger", &state_->selected_is_trigger);
    const char* dof_items[] = {"All", "Plane 2D", "No rotations", "Y only"};
    ImGui::Combo("Allowed DOF", &state_->dof_mode, dof_items, IM_ARRAYSIZE(dof_items));
    const char* layer_items[] = {"All collide", "Odd/even lanes", "Selected isolated"};
    ImGui::Combo("Layer mode", &state_->layer_mode, layer_items, IM_ARRAYSIZE(layer_items));
    ImGui::Separator();
    ImGui::SliderInt("Selected", &state_->selected, 0, 6);
    ImGui::Text("Name: %s", state_->selected_name.c_str());
    ImGui::Text("Position: %.2f %.2f %.2f", state_->selected_position.x,
                state_->selected_position.y, state_->selected_position.z);
    ImGui::Text("Velocity: %.2f %.2f %.2f", state_->selected_velocity.x,
                state_->selected_velocity.y, state_->selected_velocity.z);
    ImGui::Text("Grounded: %s", state_->selected_grounded ? "yes" : "no");
    ImGui::Text("Contacts: %d active, %d entered", state_->selected_active_contacts,
                state_->selected_entered_contacts);
    ImGui::End();
  }

 private:
  std::shared_ptr<GalleryState> state_;
};

class ShapeGalleryGame final : public app::GameInterface {
 public:
  explicit ShapeGalleryGame(std::shared_ptr<GalleryState> state) : state_(std::move(state)) {}

  void onStart() override {
    bindFlyCameraControls(*input);
    addDefaultLighting(*world, assets);
    createFlyCamera(*world, camera_, {0.0f, 9.0f, 22.0f}, 3.14159f, -0.34f);
    resetScene();
  }

  void onFixedUpdate(float) override {
    physics->setGravity(-state_->gravity);
    applyAuthoringState();
    applyForces();
  }

  void onUpdate(float dt) override {
    if (state_->reset_requested) {
      state_->reset_requested = false;
      resetScene();
    }
    updateFlyCamera(*world, *input, camera_, dt);
    updateSelectedStats();
    if (graphics) {
      drawScene();
    }
  }

  void onShutdown() override {
    destroyEntities(*world, entities_);
    dynamic_entities_.clear();
    dynamic_names_.clear();
  }

 private:
  void resetScene() {
    destroyEntities(*world, entities_);
    dynamic_entities_.clear();
    dynamic_names_.clear();

    entities_.push_back(addStaticBox(*world, {0.0f, -0.35f, 0.0f}, {23.0f, 0.35f, 18.0f}));
    addTriangleRamp({-8.0f, 0.2f, -5.0f});
    addMeshWedge({0.0f, 0.25f, -6.5f});
    addHeightField({8.0f, 0.0f, -4.5f});

    addDynamicBox("Box", {-8.0f, 6.0f, 2.0f});
    addDynamicSphere("Sphere", {-5.2f, 7.0f, 2.0f});
    addDynamicCapsule("Capsule", {-2.4f, 8.0f, 2.0f});
    addDynamicCylinder("Cylinder", {0.4f, 9.0f, 2.0f});
    addDynamicTaperedCapsule("Tapered capsule", {3.2f, 10.0f, 2.0f});
    addDynamicConvexHull("Convex hull", {6.0f, 11.0f, 2.0f});
    addDynamicCompound("Compound", {8.8f, 12.0f, 2.0f});

    state_->selected = std::clamp(state_->selected, 0, static_cast<int>(dynamic_entities_.size() - 1u));
    applyAuthoringState();
  }

  world::Entity createDynamicBase(const char* name, const math::Vec3& position) {
    auto entity = world->createEntity();
    world->setName(entity, name);
    setTransform(*world, entity, position, axisAngle({0.0f, 1.0f, 0.0f}, position.x * 0.08f));
    components::ContactListenerComponent listener{};
    listener.enabled = true;
    listener.emit_stay = true;
    world->add(entity, listener);
    world->add(entity, components::GroundContactComponent{});
    components::PhysicsMaterialComponent material{};
    material.friction = state_->friction;
    material.restitution = state_->restitution;
    world->add(entity, material);
    components::PhysicsCollisionFilterComponent filter{};
    filter.layers = 2u;
    filter.collides_with = 0xFFFFFFFFu;
    world->add(entity, filter);

    entities_.push_back(entity);
    dynamic_entities_.push_back(entity);
    dynamic_names_.push_back(name);
    return entity;
  }

  void addDynamicBody(world::Entity entity, const math::Vec3& position) {
    components::RigidbodyComponent body{};
    body.mass = state_->mass;
    body.motion_quality = components::RigidbodyMotionQuality::LinearCast;
    body.allow_sleeping = false;
    body.apply_gyroscopic_force = true;
    body.velocity = {-position.x * 0.08f, 0.0f, -1.5f};
    world->add(entity, body);
    world->add(entity, components::PhysicsBodyForcesComponent{});
  }

  void addDynamicBox(const char* name, const math::Vec3& position) {
    const auto entity = createDynamicBase(name, position);
    world->add(entity,
               components::ColliderComponent::box(
                   components::BoxColliderShape{.half_extents = {0.65f, 0.45f, 0.85f}},
                   false,
                   true));
    addDynamicBody(entity, position);
  }

  void addDynamicSphere(const char* name, const math::Vec3& position) {
    const auto entity = createDynamicBase(name, position);
    world->add(entity,
               components::ColliderComponent::sphere(
                   components::SphereColliderShape{.radius = 0.68f},
                   false,
                   true));
    addDynamicBody(entity, position);
  }

  void addDynamicCapsule(const char* name, const math::Vec3& position) {
    const auto entity = createDynamicBase(name, position);
    world->add(entity,
               components::ColliderComponent::capsule(
                   components::CapsuleColliderShape{
                       .radius = 0.38f,
                       .height = 1.75f},
                   false,
                   true));
    addDynamicBody(entity, position);
  }

  void addDynamicCylinder(const char* name, const math::Vec3& position) {
    const auto entity = createDynamicBase(name, position);
    world->add(entity,
               components::ColliderComponent::cylinder(
                   components::CylinderColliderShape{
                       .radius = 0.55f,
                       .height = 1.35f,
                       .convex_radius = 0.02f},
                   false,
                   true));
    addDynamicBody(entity, position);
  }

  void addDynamicTaperedCapsule(const char* name, const math::Vec3& position) {
    const auto entity = createDynamicBase(name, position);
    world->add(entity,
               components::ColliderComponent::taperedCapsule(
                   components::TaperedCapsuleColliderShape{
                       .top_radius = 0.28f,
                       .bottom_radius = 0.58f,
                       .height = 1.8f},
                   false,
                   true));
    addDynamicBody(entity, position);
  }

  void addDynamicConvexHull(const char* name, const math::Vec3& position) {
    const auto entity = createDynamicBase(name, position);
    components::ConvexHullColliderShape collider{};
    collider.convex_radius = 0.02f;
    const auto shape = makeConvexHullShape(0.85f);
    for (const glm::vec3& point : shape.points) {
      collider.points.push_back(math::fromGlm(point));
    }
    world->add(entity, components::ColliderComponent::convexHull(std::move(collider), false, true));
    addDynamicBody(entity, position);
  }

  void addDynamicCompound(const char* name, const math::Vec3& position) {
    const auto entity = createDynamicBase(name, position);
    world->add(entity,
               components::ColliderComponent::box(
                   components::BoxColliderShape{
                       .center = {-0.15f, 0.05f, 0.1f},
                       .half_extents = {0.95f, 0.45f, 0.65f}},
                   false,
                   true));
    addDynamicBody(entity, position);
  }

  void addTriangleRamp(const math::Vec3& position) {
    auto entity = world->createEntity();
    world->setName(entity, "Triangle ramp");
    setTransform(*world, entity, position, axisAngle({1.0f, 0.0f, 0.0f}, -0.32f));
    components::TriangleColliderShape triangle{};
    triangle.points = {{{-2.2f, 0.0f, -1.6f}, {2.2f, 0.0f, -1.6f}, {0.0f, 0.0f, 2.0f}}};
    triangle.convex_radius = 0.02f;
    world->add(entity, components::ColliderComponent::triangle(triangle, false, true));
    entities_.push_back(entity);
  }

  void addMeshWedge(const math::Vec3& position) {
    auto entity = world->createEntity();
    world->setName(entity, "Mesh wedge");
    setTransform(*world, entity, position);
    components::MeshColliderShape mesh{};
    const auto shape = makeMeshWedgeShape(1.8f);
    for (const glm::vec3& vertex : shape.mesh_vertices) {
      mesh.vertices.push_back(math::fromGlm(vertex));
    }
    mesh.indices = shape.mesh_indices;
    world->add(entity, components::ColliderComponent::mesh(std::move(mesh), false, true));
    entities_.push_back(entity);
  }

  void addHeightField(const math::Vec3& position) {
    auto entity = world->createEntity();
    world->setName(entity, "Height field");
    setTransform(*world, entity, position);
    components::HeightFieldColliderShape height{};
    const auto shape = makeHeightFieldShape(9, 0.65f, 1.0f);
    height.samples = shape.height_samples;
    height.sample_count = shape.height_sample_count;
    height.offset = math::fromGlm(shape.height_offset);
    height.scale = math::fromGlm(shape.height_scale);
    height.block_size = shape.height_block_size;
    height.bits_per_sample = shape.height_bits_per_sample;
    world->add(entity, components::ColliderComponent::heightField(std::move(height), false, true));
    entities_.push_back(entity);
  }

  void applyAuthoringState() {
    if (dynamic_entities_.empty()) {
      return;
    }
    state_->selected =
        std::clamp(state_->selected, 0, static_cast<int>(dynamic_entities_.size() - 1u));
    for (size_t i = 0; i < dynamic_entities_.size(); ++i) {
      const world::Entity entity = dynamic_entities_[i];
      if (!world->isAlive(entity)) {
        continue;
      }
      auto& body = world->get<components::RigidbodyComponent>(entity);
      body.mass = state_->mass;
      body.motion_quality = state_->use_ccd ? components::RigidbodyMotionQuality::LinearCast
                                            : components::RigidbodyMotionQuality::Discrete;
      body.allow_sleeping = state_->allow_sleeping;
      body.apply_gyroscopic_force = state_->apply_gyroscopic_force;
      body.use_manifold_reduction = state_->use_manifold_reduction;
      body.is_trigger = state_->selected_is_trigger && static_cast<int>(i) == state_->selected;
      switch (state_->dof_mode) {
        case 1:
          body.allowed_dofs = components::RigidbodyDofPlane2D;
          break;
        case 2:
          body.allowed_dofs = static_cast<uint8_t>(components::RigidbodyDofTranslationX |
                                                   components::RigidbodyDofTranslationY |
                                                   components::RigidbodyDofTranslationZ);
          break;
        case 3:
          body.allowed_dofs = components::RigidbodyDofTranslationY;
          break;
        default:
          body.allowed_dofs = components::RigidbodyDofAll;
          break;
      }

      auto& material = world->get<components::PhysicsMaterialComponent>(entity);
      material.friction = state_->friction;
      material.restitution = state_->restitution;

      auto& filter = world->get<components::PhysicsCollisionFilterComponent>(entity);
      if (state_->layer_mode == 1) {
        filter.layers = (i % 2u == 0u) ? 2u : 4u;
        filter.collides_with = 1u | filter.layers;
      } else if (state_->layer_mode == 2 && static_cast<int>(i) == state_->selected) {
        filter.layers = 8u;
        filter.collides_with = 1u;
      } else {
        filter.layers = 2u;
        filter.collides_with = 0xFFFFFFFFu;
      }
    }
  }

  void applyForces() {
    if (dynamic_entities_.empty()) {
      return;
    }
    const int selected = std::clamp(state_->selected, 0, static_cast<int>(dynamic_entities_.size() - 1u));
    for (size_t i = 0; i < dynamic_entities_.size(); ++i) {
      const world::Entity entity = dynamic_entities_[i];
      if (!world->isAlive(entity)) {
        continue;
      }
      auto& forces = world->get<components::PhysicsBodyForcesComponent>(entity);
      if (state_->continuous_forces) {
        const float direction = (i % 2u == 0u) ? 1.0f : -1.0f;
        forces.force = {direction * 18.0f, 0.0f, -8.0f};
        forces.force_at_position = true;
        forces.force_position =
            vadd(world->get<components::TransformComponent>(entity).getPosition(),
                 {0.0f, 0.55f, 0.25f});
        forces.torque = {0.0f, direction * 4.0f, 0.0f};
      }
      if (state_->kick_selected && static_cast<int>(i) == selected) {
        forces.impulse = {0.0f, state_->impulse * 0.7f, -state_->impulse};
        forces.angular_impulse = {state_->impulse * 0.35f, state_->impulse * 0.15f, 0.0f};
      }
      if (state_->scatter_all) {
        const float side = static_cast<float>(static_cast<int>(i) - 3);
        forces.impulse = {side * state_->impulse * 0.35f, state_->impulse * 0.55f,
                          -state_->impulse * 0.6f};
        forces.impulse_at_position = true;
        forces.impulse_position =
            vadd(world->get<components::TransformComponent>(entity).getPosition(),
                 {0.25f, 0.35f, 0.1f});
      }
    }
    state_->kick_selected = false;
    state_->scatter_all = false;
  }

  void updateSelectedStats() {
    state_->selected_name.clear();
    state_->selected_position = {};
    state_->selected_velocity = {};
    state_->selected_grounded = false;
    state_->selected_active_contacts = 0;
    state_->selected_entered_contacts = 0;
    if (dynamic_entities_.empty()) {
      return;
    }
    state_->selected =
        std::clamp(state_->selected, 0, static_cast<int>(dynamic_entities_.size() - 1u));
    const world::Entity entity = dynamic_entities_[state_->selected];
    if (!world->isAlive(entity)) {
      return;
    }
    state_->selected_name = dynamic_names_[state_->selected];
    state_->selected_position = world->get<components::TransformComponent>(entity).getPosition();
    state_->selected_velocity = world->get<components::RigidbodyComponent>(entity).velocity;
    if (world->has<components::GroundContactComponent>(entity)) {
      state_->selected_grounded = world->get<components::GroundContactComponent>(entity).grounded;
    }
    if (world->has<components::ContactEventsComponent>(entity)) {
      const auto& events = world->get<components::ContactEventsComponent>(entity);
      state_->selected_active_contacts = static_cast<int>(events.active.size());
      state_->selected_entered_contacts = static_cast<int>(events.entered.size());
    }
  }

  void drawScene() {
    drawReference(*graphics, 22.0f);
    const int selected =
        dynamic_entities_.empty()
            ? -1
            : std::clamp(state_->selected, 0, static_cast<int>(dynamic_entities_.size() - 1u));

    for (world::Entity entity : entities_) {
      if (!world->isAlive(entity)) {
        continue;
      }
      const bool dynamic = world->has<components::RigidbodyComponent>(entity);
      const bool highlighted = selected >= 0 && entity == dynamic_entities_[selected];
      const math::Color color =
          highlighted ? math::Color{1.0f, 0.92f, 0.25f, 1.0f}
                      : dynamic ? math::Color{0.25f, 0.9f, 1.0f, 0.95f}
                                : math::Color{0.75f, 0.78f, 0.82f, 0.8f};
      drawEntityColliders(*graphics, *world, entity, color, true, highlighted ? 2.5f : 1.3f);
      if (dynamic) {
        const auto& transform = world->get<components::TransformComponent>(entity);
        const auto& body = world->get<components::RigidbodyComponent>(entity);
        const math::Vec3 start = transform.getPosition();
        graphics->drawLine(start, vadd(start, vscale(body.velocity, 0.18f)),
                           {1.0f, 0.35f, 0.1f, 1.0f}, false, 2.0f);
      }
    }

    if (selected >= 0) {
      const world::Entity entity = dynamic_entities_[selected];
      if (world->isAlive(entity) && world->has<components::ContactEventsComponent>(entity)) {
        const auto& events = world->get<components::ContactEventsComponent>(entity);
        for (const components::ContactEvent& event : events.active) {
          graphics->drawLine(event.point, vadd(event.point, vscale(event.normal, 0.85f)),
                             {1.0f, 0.2f, 0.1f, 1.0f}, false, 3.0f);
        }
      }
      if (world->isAlive(entity) && world->has<components::GroundContactComponent>(entity)) {
        const auto& ground = world->get<components::GroundContactComponent>(entity);
        if (ground.grounded) {
          graphics->drawLine(ground.point, vadd(ground.point, vscale(ground.normal, 1.1f)),
                             {0.2f, 1.0f, 0.45f, 1.0f}, false, 3.0f);
        }
      }
    }
  }

  std::shared_ptr<GalleryState> state_;
  CameraRig camera_{};
  std::vector<world::Entity> entities_;
  std::vector<world::Entity> dynamic_entities_;
  std::vector<std::string> dynamic_names_;
};

}  // namespace

}  // namespace karma::demo::physics_examples

int main() {
  karma::app::EngineApp engine;
  auto state = std::make_shared<karma::demo::physics_examples::GalleryState>();
  karma::demo::physics_examples::ShapeGalleryGame game(state);
  auto ui = std::make_shared<karma::demo::physics_examples::GalleryUi>(state);
  engine.setUi(karma::ui::imgui::createUiLayer(
      [ui](karma::app::UIContext& ctx) { ui->draw(ctx); }));

  karma::app::EngineConfig config;
  config.window.title = "Physics Shape Gallery";
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
