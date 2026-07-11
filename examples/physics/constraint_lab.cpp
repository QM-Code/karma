#include <algorithm>
#include <array>
#include <cstdio>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include "demo_asset_paths.h"
#include "physics_example_common.h"
#if defined(KARMA_ENABLE_IMGUI)
#include "karma/ui_imgui.h"

#include <imgui.h>
#endif

namespace karma::demo::physics_examples {

namespace {

constexpr std::array<const char*, 8> kConstraintNames = {
    "Fixed", "Point", "Distance", "Hinge", "Slider", "Cone", "Swing twist", "Six DOF"};
constexpr std::array<const char*, 8> kConstraintValues = {
    "fixed", "point", "distance", "hinge", "slider", "cone", "swing-twist", "six-dof"};

components::PhysicsConstraintKind kindFromIndex(int index) {
  switch (index) {
    case 0: return components::PhysicsConstraintKind::Fixed;
    case 1: return components::PhysicsConstraintKind::Point;
    case 2: return components::PhysicsConstraintKind::Distance;
    case 3: return components::PhysicsConstraintKind::Hinge;
    case 4: return components::PhysicsConstraintKind::Slider;
    case 5: return components::PhysicsConstraintKind::Cone;
    case 6: return components::PhysicsConstraintKind::SwingTwist;
    default: return components::PhysicsConstraintKind::SixDof;
  }
}

struct ConstraintState {
  bool reset_requested = false;
  bool impulse_selected = false;
  bool enabled = true;
  bool local_space = false;
  int selected = 3;
  int priority = 0;
  int velocity_solver_steps = 0;
  int position_solver_steps = 0;
  float gravity = 9.8f;
  float impulse = 7.0f;
  float min_distance = 0.65f;
  float max_distance = 2.4f;
  float limits_min = -0.75f;
  float limits_max = 0.75f;
  float half_cone_angle = 0.55f;
  float normal_half_cone_angle = 0.55f;
  float plane_half_cone_angle = 0.42f;
  float twist_min_angle = -0.5f;
  float twist_max_angle = 0.5f;
  float max_friction_force = 4.0f;
  float max_friction_torque = 2.0f;
  float spring_frequency = 1.2f;
  float spring_damping = 0.35f;
  math::Vec3 selected_position{};
  math::Vec3 selected_velocity{};
  std::string selected_name;
};

#if defined(KARMA_ENABLE_IMGUI)
class ConstraintImGui final {
 public:
  explicit ConstraintImGui(std::shared_ptr<ConstraintState> state) : state_(std::move(state)) {}

  void draw(app::UIContext&) {
    ImGui::SetNextWindowPos(ImVec2(18.0f, 18.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(390.0f, 0.0f), ImGuiCond_FirstUseEver);
    ImGui::Begin("Physics Constraint Lab");
    ImGui::TextUnformatted("WASD + QE fly, hold RMB to look");
    ImGui::Separator();
    if (ImGui::Button("Reset")) {
      state_->reset_requested = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Impulse")) {
      state_->impulse_selected = true;
    }
    ImGui::Checkbox("Enabled", &state_->enabled);
    ImGui::Checkbox("Local body frames", &state_->local_space);
    ImGui::SliderFloat("Gravity", &state_->gravity, 0.0f, 30.0f, "%.2f");
    ImGui::SliderFloat("Impulse", &state_->impulse, 0.0f, 35.0f, "%.1f");
    ImGui::Combo("Constraint", &state_->selected, kConstraintNames.data(),
                 static_cast<int>(kConstraintNames.size()));
    ImGui::SliderInt("Priority", &state_->priority, 0, 8);
    ImGui::SliderInt("Velocity solver steps", &state_->velocity_solver_steps, 0, 16);
    ImGui::SliderInt("Position solver steps", &state_->position_solver_steps, 0, 16);
    ImGui::SeparatorText("Distance / Linear");
    ImGui::SliderFloat("Min distance", &state_->min_distance, 0.0f, 3.0f, "%.2f");
    ImGui::SliderFloat("Max distance", &state_->max_distance, 0.0f, 5.0f, "%.2f");
    ImGui::SliderFloat("Limit min", &state_->limits_min, -3.14f, 0.0f, "%.2f");
    ImGui::SliderFloat("Limit max", &state_->limits_max, 0.0f, 3.14f, "%.2f");
    ImGui::SliderFloat("Max friction force", &state_->max_friction_force, 0.0f, 30.0f, "%.1f");
    ImGui::SeparatorText("Angular");
    ImGui::SliderFloat("Cone angle", &state_->half_cone_angle, 0.0f, 1.55f, "%.2f");
    ImGui::SliderFloat("Normal cone", &state_->normal_half_cone_angle, 0.0f, 1.55f, "%.2f");
    ImGui::SliderFloat("Plane cone", &state_->plane_half_cone_angle, 0.0f, 1.55f, "%.2f");
    ImGui::SliderFloat("Twist min", &state_->twist_min_angle, -3.14f, 0.0f, "%.2f");
    ImGui::SliderFloat("Twist max", &state_->twist_max_angle, 0.0f, 3.14f, "%.2f");
    ImGui::SliderFloat("Max friction torque", &state_->max_friction_torque, 0.0f, 30.0f,
                       "%.1f");
    ImGui::SeparatorText("Spring");
    ImGui::SliderFloat("Frequency/stiffness", &state_->spring_frequency, 0.0f, 10.0f, "%.2f");
    ImGui::SliderFloat("Damping", &state_->spring_damping, 0.0f, 2.0f, "%.2f");
    ImGui::Separator();
    ImGui::Text("Editing: %s", state_->selected_name.c_str());
    ImGui::Text("Body: %.2f %.2f %.2f", state_->selected_position.x,
                state_->selected_position.y, state_->selected_position.z);
    ImGui::Text("Velocity: %.2f %.2f %.2f", state_->selected_velocity.x,
                state_->selected_velocity.y, state_->selected_velocity.z);
    ImGui::End();
  }

 private:
  std::shared_ptr<ConstraintState> state_;
};
#endif

struct Pair {
  world::Entity anchor{};
  world::Entity body{};
  world::Entity constraint{};
  math::Vec3 anchor_position{};
  math::Vec3 body_position{};
};

class ConstraintLabGame final : public app::GameInterface {
 public:
  explicit ConstraintLabGame(std::shared_ptr<ConstraintState> state) : state_(std::move(state)) {}

  void onStart() override {
    bindFlyCameraControls(*input);
    addDefaultLighting(*world, assets);
    createFlyCamera(*world, camera_, {0.0f, 8.0f, 24.0f}, 3.14159f, -0.32f);
#if defined(KARMA_ENABLE_NATIVE_UI)
    openNativeControls();
#endif
    resetScene();
  }

  void onFixedUpdate(float) override {
#if defined(KARMA_ENABLE_NATIVE_UI)
    pullNativeControlValues();
#endif
    physics->setGravity(-state_->gravity);
    applySelectedSettings();
    if (state_->impulse_selected) {
      kickSelectedBody();
      state_->impulse_selected = false;
    }
  }

  void onUpdate(float dt) override {
    if (state_->reset_requested) {
      state_->reset_requested = false;
      resetScene();
    }
    updateFlyCamera(*world, *input, camera_, dt);
    updateStats();
#if defined(KARMA_ENABLE_NATIVE_UI)
    publishNativeStats();
#endif
    if (graphics) {
      drawScene();
    }
  }

  void onShutdown() override {
#if defined(KARMA_ENABLE_NATIVE_UI)
    closeNativeControls();
#endif
    destroyEntities(*world, entities_);
    pairs_.clear();
  }

 private:
#if defined(KARMA_ENABLE_NATIVE_UI)
  void openNativeControls() {
    if (ui == nullptr) return;
    const auto opened = ui->open("ui/pilots/constraint-lab", {.layer = 100});
    controls_ = opened.document;
    if (!controls_) return;

    ui->setMany(
        controls_,
        {{"controls.enabled", state_->enabled},
         {"controls.local_space", state_->local_space},
         {"controls.gravity", state_->gravity},
         {"controls.impulse", state_->impulse},
         {"controls.constraint", kConstraintValues[state_->selected]},
         {"controls.priority", state_->priority},
         {"controls.velocity_steps", state_->velocity_solver_steps},
         {"controls.position_steps", state_->position_solver_steps},
         {"controls.min_distance", state_->min_distance},
         {"controls.max_distance", state_->max_distance},
         {"controls.spring_frequency", state_->spring_frequency},
         {"controls.spring_damping", state_->spring_damping},
         {"stats.name", kConstraintNames[state_->selected]},
         {"stats.position", "Body: --"},
         {"stats.velocity", "Velocity: --"}});

    reset_listener_ = ui->onAction(controls_, "reset", [state = state_](const ui::ActionEvent&) {
      state->reset_requested = true;
    });
    impulse_listener_ =
        ui->onAction(controls_, "impulse", [state = state_](const ui::ActionEvent&) {
          state->impulse_selected = true;
        });
  }

  void closeNativeControls() {
    if (ui == nullptr || !controls_) return;
    ui->removeListener(reset_listener_);
    ui->removeListener(impulse_listener_);
    ui->close(controls_);
    controls_ = {};
    reset_listener_ = {};
    impulse_listener_ = {};
  }

  void pullNativeControlValues() {
    if (ui == nullptr || !controls_) return;
    auto number = [&](std::string_view path, auto& target) {
      if (const auto value = ui->get(controls_, path)) {
        if (const auto numeric = value->asNumber()) {
          target = static_cast<std::remove_reference_t<decltype(target)>>(*numeric);
        }
      }
    };
    auto boolean = [&](std::string_view path, bool& target) {
      if (const auto value = ui->get(controls_, path)) {
        if (const auto enabled = value->asBoolean()) target = *enabled;
      }
    };

    boolean("controls.enabled", state_->enabled);
    boolean("controls.local_space", state_->local_space);
    number("controls.gravity", state_->gravity);
    number("controls.impulse", state_->impulse);
    number("controls.priority", state_->priority);
    number("controls.velocity_steps", state_->velocity_solver_steps);
    number("controls.position_steps", state_->position_solver_steps);
    number("controls.min_distance", state_->min_distance);
    number("controls.max_distance", state_->max_distance);
    number("controls.spring_frequency", state_->spring_frequency);
    number("controls.spring_damping", state_->spring_damping);
    if (const auto value = ui->get(controls_, "controls.constraint")) {
      if (const std::string* selected = value->asString()) {
        const auto found = std::find(kConstraintValues.begin(), kConstraintValues.end(), *selected);
        if (found != kConstraintValues.end()) {
          state_->selected = static_cast<int>(found - kConstraintValues.begin());
        }
      }
    }
  }

  static std::string vectorText(const char* label, const math::Vec3& value) {
    std::array<char, 128> text{};
    std::snprintf(text.data(), text.size(), "%s: %.2f  %.2f  %.2f", label,
                  value.x, value.y, value.z);
    return text.data();
  }

  void publishNativeStats() {
    if (ui == nullptr || !controls_) return;
    ui->setMany(
        controls_,
        {{"stats.name", state_->selected_name},
         {"stats.position", vectorText("Body", state_->selected_position)},
         {"stats.velocity", vectorText("Velocity", state_->selected_velocity)}});
  }
#endif

  void resetScene() {
    destroyEntities(*world, entities_);
    pairs_.clear();
    entities_.push_back(addStaticBox(*world, {0.0f, -0.25f, 0.0f}, {25.0f, 0.25f, 14.0f}));

    for (int i = 0; i < static_cast<int>(kConstraintNames.size()); ++i) {
      const float x = (static_cast<float>(i) - 3.5f) * 3.0f;
      addPair(i, {x, 4.6f, 0.0f}, {x, 2.55f, 0.0f});
    }
    applySelectedSettings();
  }

  void addPair(int index, const math::Vec3& anchor_position, const math::Vec3& body_position) {
    Pair pair{};
    pair.anchor_position = anchor_position;
    pair.body_position = body_position;

    pair.anchor = world->createEntity();
    world->setName(pair.anchor, std::string(kConstraintNames[index]) + " anchor");
    setTransform(*world, pair.anchor, anchor_position);
    world->add(pair.anchor,
               components::ColliderComponent::box(
                   components::BoxColliderShape{.half_extents = {0.42f, 0.42f, 0.42f}},
                   false,
                   true));
    entities_.push_back(pair.anchor);

    pair.body = world->createEntity();
    world->setName(pair.body, std::string(kConstraintNames[index]) + " body");
    setTransform(*world, pair.body, body_position,
                 axisAngle({0.0f, 1.0f, 0.0f}, static_cast<float>(index) * 0.25f));
    world->add(pair.body,
               components::ColliderComponent::box(
                   components::BoxColliderShape{.half_extents = {0.55f, 0.45f, 0.55f}},
                   false,
                   true));
    components::RigidbodyComponent body{};
    body.mass = 1.2f;
    body.allow_sleeping = false;
    body.apply_gyroscopic_force = true;
    body.velocity = {0.0f, 0.0f, -0.5f};
    world->add(pair.body, body);
    world->add(pair.body, components::PhysicsBodyForcesComponent{});
    components::PhysicsMaterialComponent material{};
    material.friction = 0.45f;
    material.restitution = 0.05f;
    world->add(pair.body, material);
    entities_.push_back(pair.body);

    pair.constraint = world->createEntity();
    world->setName(pair.constraint, kConstraintNames[index]);
    world->add(pair.constraint, makeDefaultConstraint(index, pair));
    entities_.push_back(pair.constraint);
    pairs_.push_back(pair);
  }

  components::PhysicsConstraintComponent makeDefaultConstraint(int index, const Pair& pair) const {
    components::PhysicsConstraintComponent constraint{};
    constraint.body_a = pair.anchor;
    constraint.body_b = pair.body;
    constraint.kind = kindFromIndex(index);
    constraint.space = components::PhysicsConstraintFrameSpace::World;
    setFramePoints(constraint, pair, false);
    constraint.axis1 = {1.0f, 0.0f, 0.0f};
    constraint.axis2 = {1.0f, 0.0f, 0.0f};
    constraint.normal1 = {0.0f, 1.0f, 0.0f};
    constraint.normal2 = {0.0f, 1.0f, 0.0f};
    constraint.plane_axis1 = {0.0f, 1.0f, 0.0f};
    constraint.plane_axis2 = {0.0f, 1.0f, 0.0f};
    constraint.min_distance = 0.75f;
    constraint.max_distance = 2.4f;
    constraint.limits_min = -0.65f;
    constraint.limits_max = 0.65f;
    constraint.half_cone_angle = 0.5f;
    constraint.normal_half_cone_angle = 0.55f;
    constraint.plane_half_cone_angle = 0.42f;
    constraint.twist_min_angle = -0.5f;
    constraint.twist_max_angle = 0.5f;
    constraint.max_friction_force = 4.0f;
    constraint.max_friction_torque = 2.0f;
    constraint.limit_spring.frequency_or_stiffness = 1.2f;
    constraint.limit_spring.damping = 0.35f;
    constraint.draw_size = 1.0f;
    constraint.user_data = static_cast<uint64_t>(index + 1);
    for (size_t axis = 0; axis < constraint.six_dof_min_limits.size(); ++axis) {
      constraint.six_dof_min_limits[axis] = 0.0f;
      constraint.six_dof_max_limits[axis] = 0.0f;
      constraint.six_dof_max_friction[axis] = 0.0f;
    }
    constraint.six_dof_min_limits[0] = -0.7f;
    constraint.six_dof_max_limits[0] = 0.7f;
    constraint.six_dof_min_limits[4] = -0.55f;
    constraint.six_dof_max_limits[4] = 0.55f;
    constraint.six_dof_max_friction[0] = 2.0f;
    constraint.six_dof_max_friction[4] = 1.0f;
    return constraint;
  }

  static void setFramePoints(components::PhysicsConstraintComponent& constraint,
                             const Pair& pair,
                             bool local_space) {
    if (local_space) {
      constraint.point1 = {0.0f, -0.76f, 0.0f};
      constraint.point2 = {0.0f, 0.78f, 0.0f};
    } else {
      const math::Vec3 joint = {pair.anchor_position.x,
                                (pair.anchor_position.y + pair.body_position.y) * 0.5f,
                                pair.anchor_position.z};
      constraint.point1 = joint;
      constraint.point2 = joint;
    }
  }

  void applySelectedSettings() {
    if (pairs_.empty()) {
      return;
    }
    state_->selected = std::clamp(state_->selected, 0, static_cast<int>(pairs_.size() - 1u));
    const Pair& pair = pairs_[state_->selected];
    if (!world->isAlive(pair.constraint) ||
        !world->has<components::PhysicsConstraintComponent>(pair.constraint)) {
      return;
    }
    auto& constraint = world->get<components::PhysicsConstraintComponent>(pair.constraint);
    constraint.enabled = state_->enabled;
    constraint.priority = static_cast<uint32_t>(state_->priority);
    constraint.velocity_solver_steps = static_cast<uint32_t>(state_->velocity_solver_steps);
    constraint.position_solver_steps = static_cast<uint32_t>(state_->position_solver_steps);
    constraint.space = state_->local_space
                           ? components::PhysicsConstraintFrameSpace::LocalToBodyCenterOfMass
                           : components::PhysicsConstraintFrameSpace::World;
    setFramePoints(constraint, pair, state_->local_space);
    constraint.min_distance = std::min(state_->min_distance, state_->max_distance);
    constraint.max_distance = std::max(state_->min_distance, state_->max_distance);
    constraint.limits_min = std::min(state_->limits_min, state_->limits_max);
    constraint.limits_max = std::max(state_->limits_min, state_->limits_max);
    constraint.half_cone_angle = state_->half_cone_angle;
    constraint.normal_half_cone_angle = state_->normal_half_cone_angle;
    constraint.plane_half_cone_angle = state_->plane_half_cone_angle;
    constraint.twist_min_angle = std::min(state_->twist_min_angle, state_->twist_max_angle);
    constraint.twist_max_angle = std::max(state_->twist_min_angle, state_->twist_max_angle);
    constraint.max_friction_force = state_->max_friction_force;
    constraint.max_friction_torque = state_->max_friction_torque;
    constraint.limit_spring.frequency_or_stiffness = state_->spring_frequency;
    constraint.limit_spring.damping = state_->spring_damping;
    for (size_t axis = 0; axis < constraint.six_dof_max_friction.size(); ++axis) {
      constraint.six_dof_max_friction[axis] =
          axis < 3 ? state_->max_friction_force : state_->max_friction_torque;
    }
  }

  void kickSelectedBody() {
    if (pairs_.empty()) {
      return;
    }
    const Pair& pair = pairs_[std::clamp(state_->selected, 0, static_cast<int>(pairs_.size() - 1u))];
    if (!world->isAlive(pair.body) || !world->has<components::PhysicsBodyForcesComponent>(pair.body)) {
      return;
    }
    auto& forces = world->get<components::PhysicsBodyForcesComponent>(pair.body);
    forces.impulse = {state_->impulse * 0.4f, state_->impulse * 0.5f, -state_->impulse};
    forces.impulse_at_position = true;
    forces.impulse_position =
        vadd(world->get<components::TransformComponent>(pair.body).getPosition(),
             {0.3f, 0.25f, 0.2f});
    forces.angular_impulse = {state_->impulse * 0.2f, state_->impulse * 0.3f, 0.0f};
  }

  void updateStats() {
    state_->selected_name.clear();
    state_->selected_position = {};
    state_->selected_velocity = {};
    if (pairs_.empty()) {
      return;
    }
    state_->selected = std::clamp(state_->selected, 0, static_cast<int>(pairs_.size() - 1u));
    const Pair& pair = pairs_[state_->selected];
    state_->selected_name = kConstraintNames[state_->selected];
    if (world->isAlive(pair.body)) {
      state_->selected_position = world->get<components::TransformComponent>(pair.body).getPosition();
      state_->selected_velocity = world->get<components::RigidbodyComponent>(pair.body).velocity;
    }
  }

  void drawScene() {
    drawReference(*graphics, 24.0f);
    const int selected = pairs_.empty()
                             ? -1
                             : std::clamp(state_->selected, 0, static_cast<int>(pairs_.size() - 1u));
    for (int i = 0; i < static_cast<int>(pairs_.size()); ++i) {
      const Pair& pair = pairs_[i];
      const bool highlighted = i == selected;
      const math::Color anchor_color{0.65f, 0.72f, 0.82f, 0.9f};
      const math::Color body_color =
          highlighted ? math::Color{1.0f, 0.92f, 0.22f, 1.0f}
                      : math::Color{0.25f, 0.85f, 1.0f, 0.95f};
      drawEntityColliders(*graphics, *world, pair.anchor, anchor_color, true, 1.2f);
      drawEntityColliders(*graphics, *world, pair.body, body_color, true, highlighted ? 2.5f : 1.4f);
      if (world->isAlive(pair.anchor) && world->isAlive(pair.body)) {
        const math::Vec3 a = world->get<components::TransformComponent>(pair.anchor).getPosition();
        const math::Vec3 b = world->get<components::TransformComponent>(pair.body).getPosition();
        graphics->drawLine(a, b, highlighted ? math::Color{1.0f, 0.55f, 0.1f, 1.0f}
                                             : math::Color{0.65f, 0.75f, 0.85f, 0.7f},
                           false, highlighted ? 3.0f : 1.4f);
        graphics->drawLine(a, vadd(a, {0.8f, 0.0f, 0.0f}), {1.0f, 0.15f, 0.1f, 1.0f}, false, 2.0f);
        graphics->drawLine(a, vadd(a, {0.0f, 0.8f, 0.0f}), {0.15f, 1.0f, 0.25f, 1.0f}, false, 2.0f);
        graphics->drawLine(a, vadd(a, {0.0f, 0.0f, 0.8f}), {0.2f, 0.45f, 1.0f, 1.0f}, false, 2.0f);
      }
    }
  }

  std::shared_ptr<ConstraintState> state_;
  CameraRig camera_{};
  std::vector<world::Entity> entities_;
  std::vector<Pair> pairs_;
#if defined(KARMA_ENABLE_NATIVE_UI)
  ui::DocumentHandle controls_{};
  ui::ListenerHandle reset_listener_{};
  ui::ListenerHandle impulse_listener_{};
#endif
};

}  // namespace

}  // namespace karma::demo::physics_examples

int main() {
  karma::app::EngineApp engine;
  auto state = std::make_shared<karma::demo::physics_examples::ConstraintState>();
  karma::demo::physics_examples::ConstraintLabGame game(state);
#if defined(KARMA_ENABLE_IMGUI) && !defined(KARMA_ENABLE_NATIVE_UI)
  auto imgui = std::make_shared<karma::demo::physics_examples::ConstraintImGui>(state);
  engine.setUi(karma::ui::imgui::createUiLayer(
      [imgui](karma::app::UIContext& ctx) { imgui->draw(ctx); }));
#endif

  karma::app::EngineConfig config;
  config.window.title = "Physics Constraint Lab";
  config.window.samples = 1;
  config.cursor_visible = true;
  config.enable_anisotropy = true;
  config.anisotropy_level = 16;
  config.generate_mipmaps = true;
  config.shadow_map_size = 2048;
  config.shadow_pcf_radius = 1;
#if defined(KARMA_ENABLE_NATIVE_UI)
  config.startup_asset_packages.push_back(karma::demo::resolveExamplePath(
      "examples/assets/ui/constraint_lab"));
#endif

  engine.start(game, config);
  while (engine.isRunning()) {
    engine.tick();
  }
  return 0;
}
