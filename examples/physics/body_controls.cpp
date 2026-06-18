#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "physics_example_common.h"
#include "karma/features/ui/imgui/imgui_layer.h"

#include <imgui.h>

namespace karma::demo::physics_examples {

namespace {

constexpr const char* kBodyShapeNames[] = {
    "Box", "Sphere", "Capsule", "Cylinder", "Tapered capsule", "Convex hull", "Compound"};
constexpr const char* kDofNames[] = {"All", "Plane 2D", "No rotations", "Y only"};
constexpr const char* kQualityNames[] = {"Discrete", "Linear cast"};

bool dragVec3(const char* label, math::Vec3& value, float speed = 0.05f) {
  float data[3] = {value.x, value.y, value.z};
  if (!ImGui::DragFloat3(label, data, speed)) {
    return false;
  }
  value = {data[0], data[1], data[2]};
  return true;
}

physics::PhysicsShapeDesc bodyShape(int index) {
  switch (index) {
    case 0: return makeBoxShape({0.65f, 0.55f, 0.8f});
    case 1: return makeSphereShape(0.72f);
    case 2: return makeCapsuleShape(0.35f, 1.65f);
    case 3: return makeCylinderShape(0.55f, 1.35f);
    case 4: return makeTaperedCapsuleShape(0.28f, 0.58f, 1.75f);
    case 5: return makeConvexHullShape(0.82f);
    default: return makeCompoundShape();
  }
}

uint8_t allowedDofs(int mode) {
  switch (mode) {
    case 1:
      return physics::PhysicsDofPlane2D;
    case 2:
      return static_cast<uint8_t>(physics::PhysicsDofTranslationX |
                                  physics::PhysicsDofTranslationY |
                                  physics::PhysicsDofTranslationZ);
    case 3:
      return physics::PhysicsDofTranslationY;
    default:
      return physics::PhysicsDofAll;
  }
}

struct BodyState {
  bool recreate = false;
  bool replace_shape = false;
  bool launch = false;
  bool set_velocity = false;
  bool add_velocity = false;
  bool force = false;
  bool force_at_point = false;
  bool torque = false;
  bool impulse = false;
  bool impulse_at_point = false;
  bool angular_impulse = false;
  bool teleport = false;
  bool activate = false;
  bool deactivate = false;
  bool reset_sleep = false;
  bool buoyancy_enabled = false;
  bool use_gravity = true;
  bool trigger = false;
  bool kinematic = false;
  bool manifold_reduction = true;
  bool allow_sleeping = false;
  bool apply_gyroscopic_force = true;
  bool enhanced_internal_edge_removal = false;
  int shape = 0;
  int quality = 1;
  int dof = 0;
  uint64_t user_data = 0xB0D1u;
  float gravity = 9.8f;
  float mass = 1.5f;
  float friction = 0.55f;
  float restitution = 0.2f;
  float gravity_factor = 1.0f;
  float water_level = 1.0f;
  float buoyancy = 1.25f;
  float linear_drag = 0.35f;
  float angular_drag = 0.22f;
  math::Vec3 velocity{0.0f, 4.5f, -8.0f};
  math::Vec3 angular_velocity{0.0f, 5.0f, 0.0f};
  math::Vec3 force_value{0.0f, 28.0f, -18.0f};
  math::Vec3 torque_value{0.0f, 6.0f, 0.0f};
  math::Vec3 impulse_value{0.0f, 7.0f, -8.0f};
  math::Vec3 angular_impulse_value{3.0f, 3.0f, 0.0f};
  math::Vec3 point_offset{0.45f, 0.35f, 0.25f};
  math::Vec3 teleport_position{0.0f, 5.0f, 3.0f};
  math::Vec3 position{};
  math::Quat rotation{};
  math::Vec3 live_velocity{};
  math::Vec3 live_angular_velocity{};
  math::Vec3 point_velocity{};
  bool valid = false;
  bool active = false;
  bool grounded = false;
  float live_friction = 0.0f;
  float live_restitution = 0.0f;
  float live_gravity_factor = 0.0f;
  bool live_manifold_reduction = false;
  uint64_t live_user_data = 0;
  std::uintptr_t handle = 0;
};

class BodyUi final {
 public:
  explicit BodyUi(std::shared_ptr<BodyState> state) : state_(std::move(state)) {}

  void draw(app::UIContext&) {
    ImGui::SetNextWindowPos(ImVec2(18.0f, 18.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(410.0f, 0.0f), ImGuiCond_FirstUseEver);
    ImGui::Begin("Physics Body Controls");
    ImGui::TextUnformatted("WASD + QE fly, hold RMB to look");
    ImGui::Separator();
    if (ImGui::Button("Recreate")) state_->recreate = true;
    ImGui::SameLine();
    if (ImGui::Button("Replace shape")) state_->replace_shape = true;
    ImGui::SameLine();
    if (ImGui::Button("Launch")) state_->launch = true;
    if (ImGui::Button("Activate")) state_->activate = true;
    ImGui::SameLine();
    if (ImGui::Button("Deactivate")) state_->deactivate = true;
    ImGui::SameLine();
    if (ImGui::Button("Reset sleep")) state_->reset_sleep = true;

    ImGui::Combo("Shape", &state_->shape, kBodyShapeNames, IM_ARRAYSIZE(kBodyShapeNames));
    ImGui::Combo("Quality", &state_->quality, kQualityNames, IM_ARRAYSIZE(kQualityNames));
    ImGui::Combo("Allowed DOF", &state_->dof, kDofNames, IM_ARRAYSIZE(kDofNames));
    ImGui::Checkbox("Kinematic", &state_->kinematic);
    ImGui::SameLine();
    ImGui::Checkbox("Use gravity", &state_->use_gravity);
    ImGui::Checkbox("Trigger", &state_->trigger);
    ImGui::SameLine();
    ImGui::Checkbox("Manifold reduction", &state_->manifold_reduction);
    ImGui::Checkbox("Allow sleeping", &state_->allow_sleeping);
    ImGui::SameLine();
    ImGui::Checkbox("Gyroscopic force", &state_->apply_gyroscopic_force);
    ImGui::Checkbox("Enhanced edge removal", &state_->enhanced_internal_edge_removal);
    ImGui::SliderFloat("World gravity", &state_->gravity, 0.0f, 30.0f, "%.2f");
    ImGui::SliderFloat("Mass", &state_->mass, 0.1f, 20.0f, "%.2f");
    ImGui::SliderFloat("Friction", &state_->friction, 0.0f, 1.0f, "%.2f");
    ImGui::SliderFloat("Restitution", &state_->restitution, 0.0f, 1.0f, "%.2f");
    ImGui::SliderFloat("Gravity factor", &state_->gravity_factor, -2.0f, 3.0f, "%.2f");
    ImGui::InputScalar("User data", ImGuiDataType_U64, &state_->user_data);

    ImGui::SeparatorText("Velocity / Forces");
    dragVec3("Set velocity", state_->velocity);
    dragVec3("Set angular velocity", state_->angular_velocity);
    if (ImGui::Button("Set linear/angular")) state_->set_velocity = true;
    ImGui::SameLine();
    if (ImGui::Button("Add linear/angular")) state_->add_velocity = true;
    dragVec3("Force", state_->force_value);
    dragVec3("Torque", state_->torque_value);
    dragVec3("Impulse", state_->impulse_value);
    dragVec3("Angular impulse", state_->angular_impulse_value);
    dragVec3("Point offset", state_->point_offset);
    if (ImGui::Button("Force")) state_->force = true;
    ImGui::SameLine();
    if (ImGui::Button("Force at point")) state_->force_at_point = true;
    ImGui::SameLine();
    if (ImGui::Button("Torque")) state_->torque = true;
    if (ImGui::Button("Impulse")) state_->impulse = true;
    ImGui::SameLine();
    if (ImGui::Button("Impulse at point")) state_->impulse_at_point = true;
    ImGui::SameLine();
    if (ImGui::Button("Angular impulse")) state_->angular_impulse = true;

    ImGui::SeparatorText("Teleport / Buoyancy");
    dragVec3("Teleport position", state_->teleport_position);
    if (ImGui::Button("Teleport")) state_->teleport = true;
    ImGui::Checkbox("Buoyancy impulse", &state_->buoyancy_enabled);
    ImGui::SliderFloat("Water level", &state_->water_level, -2.0f, 6.0f, "%.2f");
    ImGui::SliderFloat("Buoyancy", &state_->buoyancy, 0.0f, 4.0f, "%.2f");
    ImGui::SliderFloat("Linear drag", &state_->linear_drag, 0.0f, 2.0f, "%.2f");
    ImGui::SliderFloat("Angular drag", &state_->angular_drag, 0.0f, 2.0f, "%.2f");

    ImGui::SeparatorText("Live body");
    ImGui::Text("Handle: %s", handleLabel(state_->handle).c_str());
    ImGui::Text("Valid/active/grounded: %s / %s / %s", state_->valid ? "yes" : "no",
                state_->active ? "yes" : "no", state_->grounded ? "yes" : "no");
    ImGui::Text("Position: %.2f %.2f %.2f", state_->position.x, state_->position.y,
                state_->position.z);
    ImGui::Text("Velocity: %.2f %.2f %.2f", state_->live_velocity.x,
                state_->live_velocity.y, state_->live_velocity.z);
    ImGui::Text("Angular: %.2f %.2f %.2f", state_->live_angular_velocity.x,
                state_->live_angular_velocity.y, state_->live_angular_velocity.z);
    ImGui::Text("Point velocity: %.2f %.2f %.2f", state_->point_velocity.x,
                state_->point_velocity.y, state_->point_velocity.z);
    ImGui::Text("Material: friction %.2f restitution %.2f", state_->live_friction,
                state_->live_restitution);
    ImGui::Text("Gravity factor %.2f manifold %s user data 0x%llx", state_->live_gravity_factor,
                state_->live_manifold_reduction ? "on" : "off",
                static_cast<unsigned long long>(state_->live_user_data));
    ImGui::End();
  }

 private:
  std::shared_ptr<BodyState> state_;
};

struct DirectVisual {
  physics::RigidBody body;
  physics::PhysicsShapeDesc shape;
  math::Color color{1.0f, 1.0f, 1.0f, 1.0f};
};

class BodyControlsGame final : public app::GameInterface {
 public:
  explicit BodyControlsGame(std::shared_ptr<BodyState> state) : state_(std::move(state)) {}

  void onStart() override {
    bindFlyCameraControls(*input);
    addDefaultLighting(*world);
    createFlyCamera(*world, camera_, {0.0f, 8.0f, 22.0f}, 3.14159f, -0.34f);
    recreateStatics();
    recreatePrimary();
  }

  void onFixedUpdate(float dt) override {
    physics->setGravity(-state_->gravity);
    moveKinematicPlatform(dt);
    applyLiveSettings();
    consumeActions();
    if (state_->buoyancy_enabled && primary_.isValid()) {
      physics::PhysicsBuoyancyDesc buoyancy{};
      buoyancy.surface_position = {0.0f, state_->water_level, 0.0f};
      buoyancy.surface_normal = {0.0f, 1.0f, 0.0f};
      buoyancy.buoyancy = state_->buoyancy;
      buoyancy.linear_drag = state_->linear_drag;
      buoyancy.angular_drag = state_->angular_drag;
      buoyancy.fluid_velocity = {0.0f, 0.0f, 0.0f};
      buoyancy.gravity = {0.0f, -state_->gravity, 0.0f};
      buoyancy.delta_time = dt;
      primary_.applyBuoyancyImpulse(buoyancy);
    }
  }

  void onUpdate(float dt) override {
    if (state_->recreate) {
      state_->recreate = false;
      recreatePrimary();
    }
    updateFlyCamera(*world, *input, camera_, dt);
    updateStats();
    if (graphics) {
      drawScene();
    }
  }

  void onShutdown() override {
    primary_.destroy();
    kinematic_platform_.body.destroy();
    for (auto& visual : statics_) {
      visual.body.destroy();
    }
    statics_.clear();
  }

 private:
  physics::PhysicsBodyDesc baseDesc(const physics::PhysicsShapeDesc& shape,
                                    physics::PhysicsMotionType motion,
                                    const math::Vec3& position) const {
    physics::PhysicsBodyDesc desc{};
    desc.shape = shape;
    desc.motion = motion;
    desc.motion_quality = state_->quality == 1 ? physics::PhysicsMotionQuality::LinearCast
                                               : physics::PhysicsMotionQuality::Discrete;
    desc.allowed_dofs = allowedDofs(state_->dof);
    desc.position = math::toGlm(position);
    desc.rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    desc.mass = state_->mass;
    desc.gravity_factor = state_->use_gravity ? state_->gravity_factor : 0.0f;
    desc.material.friction = state_->friction;
    desc.material.restitution = state_->restitution;
    desc.collision_filter.layers = 2u;
    desc.collision_filter.collides_with = 0xFFFFFFFFu;
    desc.sensor = state_->trigger;
    desc.allow_sleeping = state_->allow_sleeping;
    desc.use_manifold_reduction = state_->manifold_reduction;
    desc.apply_gyroscopic_force = state_->apply_gyroscopic_force;
    desc.enhanced_internal_edge_removal = state_->enhanced_internal_edge_removal;
    desc.user_data = state_->user_data;
    return desc;
  }

  void recreateStatics() {
    for (auto& visual : statics_) {
      visual.body.destroy();
    }
    statics_.clear();

    addStatic(makeBoxShape({14.0f, 0.25f, 14.0f}), {0.0f, -0.25f, 0.0f},
              {0.64f, 0.68f, 0.72f, 0.85f});
    addStatic(makeTriangleShape(2.0f), {-5.5f, 0.3f, -5.5f},
              {0.82f, 0.72f, 0.38f, 0.9f}, axisAngle({1.0f, 0.0f, 0.0f}, -0.32f));
    addStatic(makeMeshWedgeShape(1.8f), {0.5f, 0.25f, -6.0f}, {0.82f, 0.72f, 0.38f, 0.9f});
    addStatic(makeHeightFieldShape(9, 0.65f, 0.9f), {6.5f, 0.0f, -4.8f},
              {0.72f, 0.85f, 0.46f, 0.9f});

    physics::PhysicsBodyDesc platform_desc{};
    platform_shape_ = makeBoxShape({1.35f, 0.25f, 1.35f});
    platform_desc.shape = platform_shape_;
    platform_desc.motion = physics::PhysicsMotionType::Kinematic;
    platform_desc.position = {0.0f, 1.2f, -3.8f};
    platform_desc.material.friction = 0.85f;
    platform_desc.collision_filter.layers = 4u;
    platform_desc.collision_filter.collides_with = 0xFFFFFFFFu;
    platform_desc.allow_sleeping = false;
    kinematic_platform_.shape = platform_shape_;
    kinematic_platform_.color = {0.25f, 0.95f, 0.8f, 0.95f};
    kinematic_platform_.body = physics->createBody(platform_desc);
  }

  void addStatic(const physics::PhysicsShapeDesc& shape,
                 const math::Vec3& position,
                 const math::Color& color,
                 const math::Quat& rotation = {}) {
    physics::PhysicsBodyDesc desc{};
    desc.shape = shape;
    desc.motion = physics::PhysicsMotionType::Static;
    desc.position = math::toGlm(position);
    desc.rotation = math::toGlm(rotation);
    desc.mass = 0.0f;
    desc.material.friction = 0.8f;
    desc.collision_filter.layers = 1u;
    desc.collision_filter.collides_with = 0xFFFFFFFFu;
    DirectVisual visual{};
    visual.shape = shape;
    visual.color = color;
    visual.body = physics->createBody(desc);
    statics_.push_back(std::move(visual));
  }

  void recreatePrimary() {
    primary_.destroy();
    primary_shape_ = bodyShape(state_->shape);
    auto desc = baseDesc(primary_shape_,
                         state_->kinematic ? physics::PhysicsMotionType::Kinematic
                                           : physics::PhysicsMotionType::Dynamic,
                         {0.0f, 5.0f, 3.0f});
    desc.linear_velocity = math::toGlm(state_->velocity);
    desc.angular_velocity = math::toGlm(state_->angular_velocity);
    primary_ = physics->createBody(desc);
  }

  void applyLiveSettings() {
    if (!primary_.isValid()) {
      return;
    }
    primary_.setKinematic(state_->kinematic);
    primary_.setMotionQuality(state_->quality == 1 ? physics::PhysicsMotionQuality::LinearCast
                                                   : physics::PhysicsMotionQuality::Discrete);
    primary_.setUseGravity(state_->use_gravity);
    primary_.setGravityFactor(state_->gravity_factor);
    primary_.setTrigger(state_->trigger);
    primary_.setFriction(state_->friction);
    primary_.setRestitution(state_->restitution);
    primary_.setUseManifoldReduction(state_->manifold_reduction);
    primary_.setUserData(state_->user_data);
  }

  void consumeActions() {
    if (!primary_.isValid()) {
      clearOneShotActions();
      return;
    }
    if (state_->replace_shape) {
      state_->replace_shape = false;
      primary_shape_ = bodyShape(state_->shape);
      primary_.setShape(primary_shape_, true, true);
    }
    if (state_->launch) {
      state_->launch = false;
      primary_.setPositionAndRotation({0.0f, 5.0f, 3.0f}, glm::quat(1.0f, 0.0f, 0.0f, 0.0f));
      primary_.setLinearAndAngularVelocity(math::toGlm(state_->velocity),
                                           math::toGlm(state_->angular_velocity));
      primary_.activate();
    }
    if (state_->set_velocity) {
      state_->set_velocity = false;
      primary_.setLinearAndAngularVelocity(math::toGlm(state_->velocity),
                                           math::toGlm(state_->angular_velocity));
    }
    if (state_->add_velocity) {
      state_->add_velocity = false;
      primary_.addLinearAndAngularVelocity(math::toGlm(state_->velocity),
                                           math::toGlm(state_->angular_velocity));
    }
    if (state_->force) {
      state_->force = false;
      primary_.addForce(math::toGlm(state_->force_value));
    }
    if (state_->force_at_point) {
      state_->force_at_point = false;
      primary_.addForceAtPosition(math::toGlm(state_->force_value),
                                  math::toGlm(vadd(state_->position, state_->point_offset)));
    }
    if (state_->torque) {
      state_->torque = false;
      primary_.addTorque(math::toGlm(state_->torque_value));
    }
    if (state_->impulse) {
      state_->impulse = false;
      primary_.addImpulse(math::toGlm(state_->impulse_value));
    }
    if (state_->impulse_at_point) {
      state_->impulse_at_point = false;
      primary_.addImpulseAtPosition(math::toGlm(state_->impulse_value),
                                    math::toGlm(vadd(state_->position, state_->point_offset)));
    }
    if (state_->angular_impulse) {
      state_->angular_impulse = false;
      primary_.addAngularImpulse(math::toGlm(state_->angular_impulse_value));
    }
    if (state_->teleport) {
      state_->teleport = false;
      primary_.setPositionAndRotation(math::toGlm(state_->teleport_position), primary_.getRotation());
      primary_.activate();
    }
    if (state_->activate) {
      state_->activate = false;
      primary_.activate();
    }
    if (state_->deactivate) {
      state_->deactivate = false;
      primary_.deactivate();
    }
    if (state_->reset_sleep) {
      state_->reset_sleep = false;
      primary_.resetSleepTimer();
    }
  }

  void clearOneShotActions() {
    state_->replace_shape = false;
    state_->launch = false;
    state_->set_velocity = false;
    state_->add_velocity = false;
    state_->force = false;
    state_->force_at_point = false;
    state_->torque = false;
    state_->impulse = false;
    state_->impulse_at_point = false;
    state_->angular_impulse = false;
    state_->teleport = false;
    state_->activate = false;
    state_->deactivate = false;
    state_->reset_sleep = false;
  }

  void moveKinematicPlatform(float dt) {
    if (!kinematic_platform_.body.isValid()) {
      return;
    }
    elapsed_ += dt;
    const math::Vec3 position{std::sin(elapsed_ * 0.8f) * 3.0f, 1.2f,
                              -3.8f + std::cos(elapsed_ * 0.6f) * 1.5f};
    const math::Quat rotation = axisAngle({0.0f, 1.0f, 0.0f}, std::sin(elapsed_) * 0.25f);
    kinematic_platform_.body.moveKinematic(math::toGlm(position), math::toGlm(rotation), dt);
  }

  void updateStats() {
    state_->valid = primary_.isValid();
    if (!primary_.isValid()) {
      state_->handle = 0;
      state_->active = false;
      state_->grounded = false;
      return;
    }
    state_->position = math::fromGlm(primary_.getPosition());
    state_->rotation = math::fromGlm(primary_.getRotation());
    state_->live_velocity = math::fromGlm(primary_.getVelocity());
    state_->live_angular_velocity = math::fromGlm(primary_.getAngularVelocity());
    state_->point_velocity =
        math::fromGlm(primary_.getPointVelocity(math::toGlm(vadd(state_->position, state_->point_offset))));
    state_->active = primary_.isActive();
    state_->grounded = primary_.isGrounded({1.5f, 1.5f, 1.5f});
    state_->live_friction = primary_.getFriction();
    state_->live_restitution = primary_.getRestitution();
    state_->live_gravity_factor = primary_.getGravityFactor();
    state_->live_manifold_reduction = primary_.getUseManifoldReduction();
    state_->live_user_data = primary_.getUserData();
    state_->handle = primary_.nativeHandle();
  }

  void drawWaterPlane() {
    const math::Color water{0.1f, 0.35f, 1.0f, 0.45f};
    for (int i = -12; i <= 12; ++i) {
      const float p = static_cast<float>(i);
      graphics->drawLine({-12.0f, state_->water_level, p}, {12.0f, state_->water_level, p},
                         water, false, 1.0f);
      graphics->drawLine({p, state_->water_level, -12.0f}, {p, state_->water_level, 12.0f},
                         water, false, 1.0f);
    }
  }

  void drawScene() {
    drawReference(*graphics, 16.0f);
    drawWaterPlane();
    for (const auto& visual : statics_) {
      if (visual.body.isValid()) {
        drawShapeDesc(*graphics, visual.shape, math::fromGlm(visual.body.getPosition()),
                      math::fromGlm(visual.body.getRotation()), visual.color, true, 1.4f);
      }
    }
    if (kinematic_platform_.body.isValid()) {
      drawShapeDesc(*graphics, platform_shape_, math::fromGlm(kinematic_platform_.body.getPosition()),
                    math::fromGlm(kinematic_platform_.body.getRotation()), kinematic_platform_.color,
                    true, 2.0f);
    }
    if (primary_.isValid()) {
      const math::Color color =
          state_->trigger ? math::Color{1.0f, 0.55f, 0.18f, 1.0f}
                          : math::Color{0.25f, 0.9f, 1.0f, 1.0f};
      const math::Vec3 position = math::fromGlm(primary_.getPosition());
      const math::Quat rotation = math::fromGlm(primary_.getRotation());
      drawShapeDesc(*graphics, primary_shape_, position, rotation, color, true, 2.5f);
      graphics->drawLine(position, vadd(position, vscale(state_->live_velocity, 0.2f)),
                         {1.0f, 0.35f, 0.1f, 1.0f}, false, 2.5f);
      graphics->drawLine(position, vadd(position, state_->point_offset), {1.0f, 1.0f, 0.2f, 1.0f},
                         false, 2.0f);
    }
  }

  std::shared_ptr<BodyState> state_;
  CameraRig camera_{};
  physics::RigidBody primary_;
  physics::PhysicsShapeDesc primary_shape_{};
  physics::PhysicsShapeDesc platform_shape_{};
  DirectVisual kinematic_platform_{};
  std::vector<DirectVisual> statics_;
  float elapsed_ = 0.0f;
};

}  // namespace

}  // namespace karma::demo::physics_examples

int main() {
  karma::app::EngineApp engine;
  auto state = std::make_shared<karma::demo::physics_examples::BodyState>();
  karma::demo::physics_examples::BodyControlsGame game(state);
  auto ui = std::make_shared<karma::demo::physics_examples::BodyUi>(state);
  engine.setUi(karma::imgui::createUiLayer(
      [ui](karma::app::UIContext& ctx) { ui->draw(ctx); }));

  karma::app::EngineConfig config;
  config.window.title = "Physics Body Controls";
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
