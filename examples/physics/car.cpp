#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <string>
#include <vector>

#include "physics_example_common.h"
#include "karma/features/ui/imgui/imgui_layer.h"

#include <imgui.h>

namespace karma::demo::physics_examples {

namespace {

constexpr std::array<const char*, 3> kCollisionTesterNames = {"Ray", "Sphere cast", "Cylinder cast"};
constexpr std::array<const char*, 3> kDriveModeNames = {"Rear", "Front", "All wheel"};

float vecLength(const math::Vec3& value) {
  return std::sqrt(value.x * value.x + value.y * value.y + value.z * value.z);
}

math::Vec3 transformLocal(const math::Vec3& position,
                          const math::Quat& rotation,
                          const math::Vec3& local) {
  return vadd(position, rotated(rotation, local));
}

float smoothToward(float current, float target, float speed, float dt) {
  const float alpha = 1.0f - std::exp(-speed * dt);
  return current + (target - current) * alpha;
}

struct StaticVisual {
  physics::RigidBody body;
  physics::PhysicsShapeDesc shape;
  math::Color color{0.7f, 0.75f, 0.78f, 1.0f};
};

struct CarState {
  bool reset_requested = false;
  bool apply_setup = false;
  bool flip_requested = false;
  bool keyboard_drive = true;
  bool chase_camera = true;
  bool draw_contacts = true;
  bool draw_suspension = true;
  bool draw_chassis_shape = true;
  int collision_tester = 1;
  int drive_mode = 2;

  float gravity = 9.8f;
  float mass = 1150.0f;
  float max_torque = 850.0f;
  float max_steer_angle = 0.58f;
  float tire_friction = 1.25f;
  float brake_torque = 2200.0f;
  float handbrake_torque = 5200.0f;
  float suspension_min = 0.24f;
  float suspension_max = 0.68f;
  float suspension_frequency = 1.9f;
  float suspension_damping = 0.72f;
  float anti_roll_stiffness = 3200.0f;
  float differential_ratio = 3.62f;

  float ui_throttle = 0.0f;
  float ui_steering = 0.0f;
  float ui_brake = 0.0f;
  float ui_handbrake = 0.0f;
  float target_throttle = 0.0f;
  float target_steering = 0.0f;
  float target_brake = 0.0f;
  float target_handbrake = 0.0f;
  float live_steering = 0.0f;
  bool key_forward = false;
  bool key_reverse = false;
  bool key_left = false;
  bool key_right = false;

  physics::PhysicsVehicleState vehicle_state{};
  math::Vec3 position{};
  math::Vec3 velocity{};
  float speed_kph = 0.0f;
  bool chassis_active = false;
  bool vehicle_valid = false;
  std::uintptr_t chassis_handle = 0;
};

class CarUi final {
 public:
  explicit CarUi(std::shared_ptr<CarState> state) : state_(std::move(state)) {}

  void draw(app::UIContext&) {
    ImGui::SetNextWindowPos(ImVec2(18.0f, 18.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(390.0f, 0.0f), ImGuiCond_FirstUseEver);
    ImGui::Begin("Physics Car");

    if (ImGui::Button("Reset")) state_->reset_requested = true;
    ImGui::SameLine();
    if (ImGui::Button("Apply setup")) state_->apply_setup = true;
    ImGui::SameLine();
    if (ImGui::Button("Upright")) state_->flip_requested = true;

    ImGui::Checkbox("Keyboard drive", &state_->keyboard_drive);
    ImGui::SameLine();
    ImGui::Checkbox("Chase camera", &state_->chase_camera);
    ImGui::Checkbox("Chassis shape", &state_->draw_chassis_shape);
    ImGui::SameLine();
    ImGui::Checkbox("Suspension", &state_->draw_suspension);
    ImGui::SameLine();
    ImGui::Checkbox("Contacts", &state_->draw_contacts);

    ImGui::SeparatorText("Driver");
    ImGui::SliderFloat("Throttle", &state_->ui_throttle, -1.0f, 1.0f, "%.2f");
    ImGui::SliderFloat("Steering", &state_->ui_steering, -1.0f, 1.0f, "%.2f");
    ImGui::SliderFloat("Brake", &state_->ui_brake, 0.0f, 1.0f, "%.2f");
    ImGui::SliderFloat("Handbrake", &state_->ui_handbrake, 0.0f, 1.0f, "%.2f");

    ImGui::SeparatorText("Setup");
    ImGui::Combo("Collision tester", &state_->collision_tester, kCollisionTesterNames.data(),
                 static_cast<int>(kCollisionTesterNames.size()));
    ImGui::Combo("Drive mode", &state_->drive_mode, kDriveModeNames.data(),
                 static_cast<int>(kDriveModeNames.size()));
    ImGui::SliderFloat("Gravity", &state_->gravity, 0.0f, 24.0f, "%.2f");
    ImGui::SliderFloat("Mass", &state_->mass, 500.0f, 2200.0f, "%.0f kg");
    ImGui::SliderFloat("Max torque", &state_->max_torque, 200.0f, 1800.0f, "%.0f Nm");
    ImGui::SliderFloat("Steer angle", &state_->max_steer_angle, 0.2f, 0.9f, "%.2f rad");
    ImGui::SliderFloat("Tire friction", &state_->tire_friction, 0.4f, 2.2f, "%.2f");
    ImGui::SliderFloat("Brake torque", &state_->brake_torque, 400.0f, 6000.0f, "%.0f Nm");
    ImGui::SliderFloat("Handbrake torque", &state_->handbrake_torque, 1000.0f, 9000.0f,
                       "%.0f Nm");
    ImGui::SliderFloat("Suspension min", &state_->suspension_min, 0.05f, 0.55f, "%.2f m");
    ImGui::SliderFloat("Suspension max", &state_->suspension_max, 0.25f, 1.1f, "%.2f m");
    ImGui::SliderFloat("Spring frequency", &state_->suspension_frequency, 0.5f, 5.0f, "%.2f");
    ImGui::SliderFloat("Spring damping", &state_->suspension_damping, 0.05f, 2.0f, "%.2f");
    ImGui::SliderFloat("Anti-roll", &state_->anti_roll_stiffness, 0.0f, 9000.0f, "%.0f");
    ImGui::SliderFloat("Diff ratio", &state_->differential_ratio, 1.5f, 7.0f, "%.2f");

    ImGui::SeparatorText("Telemetry");
    ImGui::Text("Speed %.1f km/h", state_->speed_kph);
    ImGui::Text("RPM %.0f  Gear %d  Clutch %.2f",
                state_->vehicle_state.engine_rpm,
                state_->vehicle_state.current_gear,
                state_->vehicle_state.clutch_friction);
    ImGui::Text("Input T %.2f  S %.2f  B %.2f  H %.2f",
                state_->target_throttle,
                state_->live_steering,
                state_->target_brake,
                state_->target_handbrake);
    ImGui::Text("Keys F%d R%d L%d R%d",
                state_->key_forward ? 1 : 0,
                state_->key_reverse ? 1 : 0,
                state_->key_left ? 1 : 0,
                state_->key_right ? 1 : 0);
    ImGui::Text("Chassis %s  Vehicle %s/%s",
                state_->chassis_active ? "active" : "sleeping",
                state_->vehicle_valid ? "valid" : "missing",
                state_->vehicle_state.active ? "active" : "inactive");
    ImGui::Text("Body: %s", handleLabel(state_->chassis_handle).c_str());
    int contacts = 0;
    for (const auto& wheel : state_->vehicle_state.wheels) {
      if (wheel.has_contact) ++contacts;
    }
    ImGui::Text("Wheel contacts %d / %d", contacts,
                static_cast<int>(state_->vehicle_state.wheels.size()));
    ImGui::End();
  }

 private:
  std::shared_ptr<CarState> state_;
};

class CarGame final : public app::GameInterface {
 public:
  explicit CarGame(std::shared_ptr<CarState> state) : state_(std::move(state)) {}

  void onStart() override {
    bindFlyCameraControls(*input);
    bindCarControls();
    addDefaultLighting(*world);
    createFlyCamera(*world, camera_, {0.0f, 7.0f, -16.0f}, 0.0f, -0.22f);
    createTrack();
    resetCar({0.0f, 1.25f, 0.0f}, {});
  }

  void onFixedUpdate(float dt) override {
    physics->setGravity(-state_->gravity);
    sampleDriverInput();
    updateDriverInput(dt);
    if (vehicle_.isValid()) {
      vehicle_.setInput(current_input_);
    }
    if (chassis_.isValid() && hasDriverInput()) {
      chassis_.activate();
    }
  }

  void onUpdate(float dt) override {
    sampleDriverInput();

    if (input->actionPressed("car_reset")) {
      state_->reset_requested = true;
    }
    if (input->actionPressed("car_upright")) {
      state_->flip_requested = true;
    }

    if (state_->reset_requested) {
      state_->reset_requested = false;
      resetCar({0.0f, 1.25f, 0.0f}, {});
    }
    if (state_->apply_setup) {
      state_->apply_setup = false;
      const math::Vec3 current = chassis_.isValid() ? math::fromGlm(chassis_.getPosition())
                                                    : math::Vec3{0.0f, 1.25f, 0.0f};
      resetCar({current.x, std::max(current.y, 1.25f), current.z}, {});
    }
    if (state_->flip_requested) {
      state_->flip_requested = false;
      uprightCar();
    }

    updateTelemetry();
    if (state_->chase_camera && chassis_.isValid()) {
      updateChaseCamera(dt);
    } else {
      updateFlyCamera(*world, *input, camera_, dt);
    }
    if (graphics) {
      drawScene();
    }
  }

  void onShutdown() override {
    vehicle_.destroy();
    chassis_.destroy();
    for (auto& visual : statics_) {
      visual.body.destroy();
    }
    statics_.clear();
  }

 private:
  void bindCarControls() {
    input->bindKey("car_forward", platform::Key::Up);
    input->bindKey("car_forward", platform::Key::W);
    input->bindKey("car_reverse", platform::Key::Down);
    input->bindKey("car_reverse", platform::Key::S);
    input->bindKey("car_left", platform::Key::Left);
    input->bindKey("car_left", platform::Key::A);
    input->bindKey("car_right", platform::Key::Right);
    input->bindKey("car_right", platform::Key::D);
    input->bindKey("car_brake", platform::Key::LeftShift);
    input->bindKey("car_brake", platform::Key::RightShift);
    input->bindKey("car_handbrake", platform::Key::Space);
    input->bindKey("car_reset", platform::Key::R, input::Trigger::Pressed);
    input->bindKey("car_upright", platform::Key::F, input::Trigger::Pressed);
  }

  physics::PhysicsShapeDesc chassisShape() const {
    physics::PhysicsShapeDesc compound{};
    compound.type = physics::PhysicsShapeType::Compound;

    auto base = makeBoxShape({1.05f, 0.28f, 1.85f});
    base.center = {0.0f, 0.08f, 0.0f};
    compound.children.push_back(base);

    auto cabin = makeBoxShape({0.68f, 0.38f, 0.58f});
    cabin.center = {0.0f, 0.62f, -0.35f};
    compound.children.push_back(cabin);

    auto nose = makeBoxShape({0.82f, 0.16f, 0.72f});
    nose.center = {0.0f, 0.28f, 1.18f};
    compound.children.push_back(nose);
    return compound;
  }

  physics::PhysicsVehicleCollisionTesterType collisionTester() const {
    switch (state_->collision_tester) {
      case 0:
        return physics::PhysicsVehicleCollisionTesterType::Ray;
      case 2:
        return physics::PhysicsVehicleCollisionTesterType::CylinderCast;
      default:
        return physics::PhysicsVehicleCollisionTesterType::SphereCast;
    }
  }

  physics::PhysicsVehicleDesc vehicleDesc() const {
    physics::PhysicsVehicleDesc desc{};
    desc.controller = physics::PhysicsVehicleControllerType::Wheeled;
    desc.collision_tester = collisionTester();
    desc.collision_test_sphere_radius = 0.34f;
    desc.collision_test_cylinder_convex_radius_fraction = 0.18f;
    desc.collision_test_max_slope_angle = 1.32f;
    desc.num_steps_between_collision_test_active = 1;
    desc.num_steps_between_collision_test_inactive = 4;
    desc.velocity_solver_steps = 10;
    desc.position_solver_steps = 4;

    desc.engine.max_torque = state_->max_torque;
    desc.engine.min_rpm = 850.0f;
    desc.engine.max_rpm = 6800.0f;
    desc.engine.inertia = 0.65f;
    desc.engine.angular_damping = 0.18f;
    desc.engine.normalized_torque = {
        {0.00f, 0.72f},
        {0.32f, 1.00f},
        {0.66f, 0.92f},
        {1.00f, 0.35f},
    };
    desc.transmission.mode = physics::PhysicsVehicleTransmissionMode::Automatic;
    desc.transmission.gear_ratios = {3.20f, 2.10f, 1.45f, 1.08f, 0.84f};
    desc.transmission.reverse_gear_ratios = {-3.00f};
    desc.transmission.shift_up_rpm = 5200.0f;
    desc.transmission.shift_down_rpm = 2200.0f;
    desc.transmission.clutch_strength = 11.0f;
    desc.differential_limited_slip_ratio = 1.32f;

    const std::array<math::Vec3, 4> wheel_positions{{
        {-0.98f, -0.30f, 1.28f},
        {0.98f, -0.30f, 1.28f},
        {-0.98f, -0.30f, -1.28f},
        {0.98f, -0.30f, -1.28f},
    }};

    desc.wheels.resize(wheel_positions.size());
    for (size_t i = 0; i < desc.wheels.size(); ++i) {
      auto& wheel = desc.wheels[i];
      wheel.position = math::toGlm(wheel_positions[i]);
      wheel.suspension_force_point = math::toGlm(wheel_positions[i]);
      wheel.suspension_direction = {0.0f, -1.0f, 0.0f};
      wheel.steering_axis = {0.0f, 1.0f, 0.0f};
      wheel.wheel_up = {0.0f, 1.0f, 0.0f};
      wheel.wheel_forward = {0.0f, 0.0f, 1.0f};
      wheel.suspension_min_length = state_->suspension_min;
      wheel.suspension_max_length = std::max(state_->suspension_max, state_->suspension_min + 0.05f);
      wheel.suspension_preload_length = 0.03f;
      wheel.suspension_spring.mode = physics::PhysicsSpringMode::FrequencyAndDamping;
      wheel.suspension_spring.frequency_or_stiffness = state_->suspension_frequency;
      wheel.suspension_spring.damping = state_->suspension_damping;
      wheel.radius = 0.36f;
      wheel.width = 0.26f;
      wheel.inertia = 1.15f;
      wheel.angular_damping = 0.16f;
      wheel.max_steer_angle = i < 2 ? state_->max_steer_angle : 0.0f;
      wheel.max_brake_torque = state_->brake_torque;
      wheel.max_hand_brake_torque = i >= 2 ? state_->handbrake_torque : 0.0f;
      wheel.longitudinal_friction = {
          {0.00f, state_->tire_friction},
          {0.07f, state_->tire_friction * 1.05f},
          {0.35f, state_->tire_friction * 0.92f},
          {1.00f, state_->tire_friction * 0.78f},
      };
      wheel.lateral_friction = {
          {0.0f, state_->tire_friction},
          {4.0f, state_->tire_friction * 1.08f},
          {15.0f, state_->tire_friction * 0.82f},
          {45.0f, state_->tire_friction * 0.56f},
      };
    }

    if (state_->drive_mode == 1 || state_->drive_mode == 2) {
      desc.differentials.push_back({
          .left_wheel = 0,
          .right_wheel = 1,
          .differential_ratio = state_->differential_ratio,
          .left_right_split = 0.5f,
          .limited_slip_ratio = 1.35f,
          .engine_torque_ratio = state_->drive_mode == 2 ? 0.42f : 1.0f,
      });
    }
    if (state_->drive_mode == 0 || state_->drive_mode == 2) {
      desc.differentials.push_back({
          .left_wheel = 2,
          .right_wheel = 3,
          .differential_ratio = state_->differential_ratio,
          .left_right_split = 0.5f,
          .limited_slip_ratio = 1.35f,
          .engine_torque_ratio = state_->drive_mode == 2 ? 0.58f : 1.0f,
      });
    }

    desc.anti_roll_bars.push_back({0, 1, state_->anti_roll_stiffness});
    desc.anti_roll_bars.push_back({2, 3, state_->anti_roll_stiffness});
    return desc;
  }

  void resetCar(const math::Vec3& position, const math::Quat& rotation) {
    vehicle_.destroy();
    chassis_.destroy();

    chassis_shape_ = chassisShape();
    physics::PhysicsBodyDesc body{};
    body.shape = chassis_shape_;
    body.motion = physics::PhysicsMotionType::Dynamic;
    body.motion_quality = physics::PhysicsMotionQuality::LinearCast;
    body.position = math::toGlm(position);
    body.rotation = math::toGlm(rotation);
    body.mass = state_->mass;
    body.inertia_multiplier = 1.15f;
    body.gravity_factor = 1.0f;
    body.linear_damping = 0.035f;
    body.angular_damping = 0.08f;
    body.max_linear_velocity = 140.0f;
    body.max_angular_velocity = 90.0f;
    body.velocity_solver_steps = 10;
    body.position_solver_steps = 4;
    body.material.friction = 0.72f;
    body.material.restitution = 0.05f;
    body.collision_filter.layers = 2u;
    body.collision_filter.collides_with = 0xFFFFFFFFu;
    body.allow_sleeping = false;
    body.apply_gyroscopic_force = true;
    body.user_data = 0xCAAu;
    chassis_ = physics->createBody(body);
    vehicle_desc_ = vehicleDesc();
    if (chassis_.isValid()) {
      vehicle_ = physics->createVehicle(vehicle_desc_, chassis_.nativeHandle());
      vehicle_.setEnabled(true);
    }
    state_->vehicle_valid = vehicle_.isValid();
    state_->live_steering = 0.0f;
    state_->target_throttle = 0.0f;
    state_->target_steering = 0.0f;
    state_->target_brake = 0.0f;
    state_->target_handbrake = 0.0f;
    current_input_ = {};
  }

  void uprightCar() {
    if (!chassis_.isValid()) {
      return;
    }
    const math::Vec3 pos = math::fromGlm(chassis_.getPosition());
    chassis_.setPositionAndRotation({pos.x, std::max(pos.y + 0.8f, 1.4f), pos.z},
                                    glm::quat(1.0f, 0.0f, 0.0f, 0.0f));
    chassis_.setLinearAndAngularVelocity({0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f});
    chassis_.activate();
  }

  void createTrack() {
    for (auto& visual : statics_) {
      visual.body.destroy();
    }
    statics_.clear();
    addStatic(makeBoxShape({14.0f, 0.25f, 54.0f}), {0.0f, -0.25f, 28.0f},
              {0.42f, 0.47f, 0.50f, 0.86f}, {}, 1.1f);
    addStatic(makeBoxShape({0.22f, 0.45f, 36.0f}), {-6.2f, 0.2f, 24.0f},
              {0.86f, 0.75f, 0.22f, 0.9f}, {}, 0.9f);
    addStatic(makeBoxShape({0.22f, 0.45f, 36.0f}), {6.2f, 0.2f, 24.0f},
              {0.86f, 0.75f, 0.22f, 0.9f}, {}, 0.9f);
    addStatic(makeBoxShape({2.5f, 0.22f, 1.1f}), {-2.0f, 0.22f, 13.0f},
              {0.32f, 0.82f, 0.52f, 0.9f}, axisAngle({1.0f, 0.0f, 0.0f}, -0.34f), 1.0f);
    addStatic(makeBoxShape({2.5f, 0.22f, 1.1f}), {2.3f, 0.22f, 24.0f},
              {0.32f, 0.82f, 0.52f, 0.9f}, axisAngle({1.0f, 0.0f, 0.0f}, -0.30f), 1.0f);
    addStatic(makeMeshWedgeShape(2.2f), {0.0f, 0.2f, 37.5f},
              {0.88f, 0.44f, 0.24f, 0.9f}, {}, 0.95f);

    for (int i = 0; i < 9; ++i) {
      const float z = 7.0f + static_cast<float>(i) * 4.1f;
      const float x = (i % 2 == 0) ? -2.35f : 2.35f;
      addStatic(makeCylinderShape(0.28f, 0.65f, 0.02f), {x, 0.33f, z},
                {1.0f, 0.45f, 0.15f, 0.9f}, {}, 0.75f);
    }
  }

  void addStatic(const physics::PhysicsShapeDesc& shape,
                 const math::Vec3& position,
                 const math::Color& color,
                 const math::Quat& rotation,
                 float friction) {
    physics::PhysicsBodyDesc desc{};
    desc.shape = shape;
    desc.motion = physics::PhysicsMotionType::Static;
    desc.position = math::toGlm(position);
    desc.rotation = math::toGlm(rotation);
    desc.mass = 0.0f;
    desc.material.friction = friction;
    desc.material.restitution = 0.05f;
    desc.collision_filter.layers = 1u;
    desc.collision_filter.collides_with = 0xFFFFFFFFu;

    StaticVisual visual{};
    visual.shape = shape;
    visual.color = color;
    visual.body = physics->createBody(desc);
    statics_.push_back(std::move(visual));
  }

  void sampleDriverInput() {
    state_->key_forward = input->actionDown("car_forward");
    state_->key_reverse = input->actionDown("car_reverse");
    state_->key_left = input->actionDown("car_left");
    state_->key_right = input->actionDown("car_right");

    float target_throttle = state_->ui_throttle;
    float target_steering = state_->ui_steering;
    float target_brake = state_->ui_brake;
    float target_handbrake = state_->ui_handbrake;
    if (state_->keyboard_drive) {
      target_throttle = 0.0f;
      if (state_->key_forward) target_throttle += 1.0f;
      if (state_->key_reverse) target_throttle -= 0.75f;
      target_steering = 0.0f;
      if (state_->key_left) target_steering -= 1.0f;
      if (state_->key_right) target_steering += 1.0f;
      target_brake = input->actionDown("car_brake") ? 1.0f : 0.0f;
      target_handbrake = input->actionDown("car_handbrake") ? 1.0f : 0.0f;
    }

    state_->target_throttle = std::clamp(target_throttle, -1.0f, 1.0f);
    state_->target_steering = std::clamp(target_steering, -1.0f, 1.0f);
    state_->target_brake = std::clamp(target_brake, 0.0f, 1.0f);
    state_->target_handbrake = std::clamp(target_handbrake, 0.0f, 1.0f);
  }

  void updateDriverInput(float dt) {
    state_->live_steering = smoothToward(state_->live_steering, state_->target_steering, 9.0f, dt);
    current_input_.forward = state_->target_throttle;
    current_input_.right = std::clamp(state_->live_steering, -1.0f, 1.0f);
    current_input_.brake = state_->target_brake;
    current_input_.hand_brake = state_->target_handbrake;
  }

  bool hasDriverInput() const {
    return std::abs(current_input_.forward) > 0.01f ||
           std::abs(current_input_.right) > 0.01f ||
           current_input_.brake > 0.01f ||
           current_input_.hand_brake > 0.01f;
  }

  void updateTelemetry() {
    state_->vehicle_valid = vehicle_.isValid();
    state_->vehicle_state = state_->vehicle_valid ? vehicle_.getState() : physics::PhysicsVehicleState{};
    if (!chassis_.isValid()) {
      state_->position = {};
      state_->velocity = {};
      state_->speed_kph = 0.0f;
      state_->chassis_active = false;
      state_->vehicle_valid = false;
      state_->chassis_handle = 0;
      return;
    }
    state_->position = math::fromGlm(chassis_.getPosition());
    state_->velocity = math::fromGlm(chassis_.getVelocity());
    state_->speed_kph = vecLength(state_->velocity) * 3.6f;
    state_->chassis_active = chassis_.isActive();
    state_->chassis_handle = chassis_.nativeHandle();
  }

  void updateChaseCamera(float dt) {
    if (!world->isAlive(camera_.entity) || !chassis_.isValid()) {
      return;
    }
    auto& transform = world->get<components::TransformComponent>(camera_.entity);
    const math::Vec3 car_pos = math::fromGlm(chassis_.getPosition());
    const math::Quat car_rot = math::fromGlm(chassis_.getRotation());
    const math::Vec3 behind = rotated(car_rot, {0.0f, 0.0f, -9.0f});
    const math::Vec3 high{0.0f, 4.4f, 0.0f};
    const math::Vec3 desired = vadd(vadd(car_pos, behind), high);
    const math::Vec3 current = transform.getPosition();
    const float alpha = 1.0f - std::exp(-6.5f * dt);
    transform.setPosition({
        current.x + (desired.x - current.x) * alpha,
        current.y + (desired.y - current.y) * alpha,
        current.z + (desired.z - current.z) * alpha,
    });

    const math::Vec3 to_car = math::normalize(vsub(vadd(car_pos, {0.0f, 1.0f, 0.0f}),
                                                  transform.getPosition()));
    const float yaw = std::atan2(-to_car.x, -to_car.z);
    const float pitch = std::asin(std::clamp(to_car.y, -0.95f, 0.95f));
    const math::Quat desired_rot = math::fromYawPitch(yaw, pitch);
    transform.setRotation(math::slerp(transform.getRotation(), desired_rot, alpha));
    camera_.yaw = yaw;
    camera_.pitch = pitch;
    camera_.target_yaw = yaw;
    camera_.target_pitch = pitch;
  }

  void drawRoadLines() {
    const math::Color lane{0.9f, 0.86f, 0.62f, 0.72f};
    for (int i = 0; i < 16; ++i) {
      const float z0 = static_cast<float>(i) * 5.5f - 4.0f;
      graphics->drawLine({0.0f, 0.018f, z0}, {0.0f, 0.018f, z0 + 2.2f}, lane, true, 2.0f);
    }
    const math::Color edge{0.96f, 0.96f, 0.9f, 0.42f};
    graphics->drawLine({-4.5f, 0.02f, -7.0f}, {-4.5f, 0.02f, 78.0f}, edge, true, 1.5f);
    graphics->drawLine({4.5f, 0.02f, -7.0f}, {4.5f, 0.02f, 78.0f}, edge, true, 1.5f);
  }

  void drawWheel(const physics::PhysicsVehicleWheelDesc& desc,
                 const physics::PhysicsVehicleWheelState* state,
                 const math::Vec3& body_pos,
                 const math::Quat& body_rot,
                 size_t index) {
    const math::Vec3 hardpoint = math::fromGlm(desc.position);
    const float suspension = state != nullptr ? state->suspension_length : desc.suspension_max_length;
    const math::Vec3 local_center =
        vadd(hardpoint, vscale(math::fromGlm(desc.suspension_direction), suspension));
    const math::Vec3 center = transformLocal(body_pos, body_rot, local_center);
    const math::Vec3 anchor = transformLocal(body_pos, body_rot, hardpoint);
    const float steer = state != nullptr ? state->steer_angle : 0.0f;
    const float spin = state != nullptr ? state->rotation_angle : 0.0f;
    math::Quat wheel_rot = math::mul(body_rot, axisAngle({0.0f, 1.0f, 0.0f}, steer));
    wheel_rot = math::mul(wheel_rot, axisAngle({0.0f, 0.0f, 1.0f}, kPi * 0.5f));
    wheel_rot = math::mul(wheel_rot, axisAngle({0.0f, 1.0f, 0.0f}, spin));
    const bool front = index < 2;
    const math::Color color = front ? math::Color{0.18f, 0.58f, 1.0f, 1.0f}
                                    : math::Color{0.12f, 0.95f, 0.62f, 1.0f};
    drawWireCylinder(*graphics, center, wheel_rot, desc.radius, desc.width, color, true, 2.0f);
    if (state_->draw_suspension) {
      graphics->drawLine(anchor, center, {1.0f, 0.86f, 0.25f, 0.95f}, true, 2.0f);
    }
    if (state_->draw_contacts && state != nullptr && state->has_contact) {
      const math::Vec3 contact = math::fromGlm(state->contact_position);
      graphics->drawLine(contact, vadd(contact, vscale(math::fromGlm(state->contact_normal), 0.75f)),
                         {1.0f, 0.2f, 0.18f, 1.0f}, false, 2.2f);
    }
  }

  void drawCar() {
    if (!chassis_.isValid()) {
      return;
    }
    const math::Vec3 position = math::fromGlm(chassis_.getPosition());
    const math::Quat rotation = math::fromGlm(chassis_.getRotation());
    if (state_->draw_chassis_shape) {
      drawShapeDesc(*graphics, chassis_shape_, position, rotation,
                    {0.18f, 0.74f, 1.0f, 1.0f}, true, 2.5f);
    }
    drawWireBox(*graphics, transformLocal(position, rotation, {0.0f, 0.62f, -0.35f}),
                rotation, {0.70f, 0.40f, 0.60f}, {0.98f, 0.92f, 0.35f, 1.0f}, true, 2.0f);
    graphics->drawLine(position, transformLocal(position, rotation, {0.0f, 0.25f, 2.25f}),
                       {1.0f, 0.22f, 0.12f, 1.0f}, false, 3.0f);

    for (size_t i = 0; i < vehicle_desc_.wheels.size(); ++i) {
      const physics::PhysicsVehicleWheelState* wheel_state =
          i < state_->vehicle_state.wheels.size() ? &state_->vehicle_state.wheels[i] : nullptr;
      drawWheel(vehicle_desc_.wheels[i], wheel_state, position, rotation, i);
    }
  }

  void drawScene() {
    drawReference(*graphics, 18.0f);
    drawRoadLines();
    for (const auto& visual : statics_) {
      if (visual.body.isValid()) {
        drawShapeDesc(*graphics, visual.shape, math::fromGlm(visual.body.getPosition()),
                      math::fromGlm(visual.body.getRotation()), visual.color, true, 1.6f);
      }
    }
    drawCar();
  }

  std::shared_ptr<CarState> state_;
  CameraRig camera_{};
  physics::RigidBody chassis_;
  physics::Vehicle vehicle_;
  physics::PhysicsShapeDesc chassis_shape_{};
  physics::PhysicsVehicleDesc vehicle_desc_{};
  physics::PhysicsVehicleInput current_input_{};
  std::vector<StaticVisual> statics_;
};

}  // namespace

}  // namespace karma::demo::physics_examples

int main() {
  karma::app::EngineApp engine;
  auto state = std::make_shared<karma::demo::physics_examples::CarState>();
  karma::demo::physics_examples::CarGame game(state);
  auto ui = std::make_shared<karma::demo::physics_examples::CarUi>(state);
  engine.setUi(karma::imgui::createUiLayer(
      [ui](karma::app::UIContext& ctx) { ui->draw(ctx); }));

  karma::app::EngineConfig config;
  config.window.title = "Physics Car";
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
