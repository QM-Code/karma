#include <cstdio>
#include <cmath>
#include <deque>
#include <string>
#include <vector>

#include "demo_asset_paths.h"
#include "karma/karma.h"
#include "karma/components.h"
#include "karma/ui_imgui.h"

namespace karma::demo {

namespace {

struct CollisionUiState {
  bool grounded = false;
  bool grounded_entered = false;
  bool grounded_exited = false;
  std::string support_label;
  math::Vec3 ground_normal{0.0f, 1.0f, 0.0f};
  std::vector<std::string> active_contacts;
  std::vector<std::string> entered_contacts;
  std::vector<std::string> exited_contacts;
  std::vector<std::string> solid_contacts;
  std::deque<std::string> recent_messages;
};

std::unique_ptr<app::UiLayer> createCollisionUiLayer(CollisionUiState& state) {
  return ui::imgui::createUiLayer(
      [&state](app::UIContext&) {
        ImGui::SetNextWindowBgAlpha(0.88f);
        ImGui::SetNextWindowPos(ImVec2(20.0f, 20.0f), ImGuiCond_Always);
        ImGui::Begin("Collision Events", nullptr,
                     ImGuiWindowFlags_NoCollapse |
                         ImGuiWindowFlags_NoSavedSettings |
                         ImGuiWindowFlags_NoInputs |
                         ImGuiWindowFlags_AlwaysAutoResize);
        ImGui::TextUnformatted("Drive with WASD / arrows");
        ImGui::TextUnformatted("Jump with Space");
        ImGui::TextUnformatted("Reset with R");
        ImGui::Text("Grounded: %s", state.grounded ? "Yes" : "No");
        ImGui::Text(
            "Ground Event: %s",
            state.grounded_entered ? "Just Landed"
                                   : state.grounded_exited ? "Just Left Ground"
                                                           : "None");
        ImGui::Text("Support: %s",
                    state.support_label.empty() ? "None"
                                                : state.support_label.c_str());
        ImGui::Text("Ground Normal: (%.2f, %.2f, %.2f)",
                    state.ground_normal.x, state.ground_normal.y,
                    state.ground_normal.z);
        ImGui::Separator();

        ImGui::Text("Active Contacts: %d",
                    static_cast<int>(state.active_contacts.size()));
        if (state.active_contacts.empty()) {
          ImGui::TextDisabled("None");
        } else {
          for (const std::string& label : state.active_contacts) {
            ImGui::BulletText("%s", label.c_str());
          }
        }

        ImGui::Separator();
        ImGui::TextUnformatted("Entered This Tick");
        if (state.entered_contacts.empty()) {
          ImGui::TextDisabled("None");
        } else {
          for (const std::string& label : state.entered_contacts) {
            ImGui::BulletText("%s", label.c_str());
          }
        }

        ImGui::TextUnformatted("Exited This Tick");
        if (state.exited_contacts.empty()) {
          ImGui::TextDisabled("None");
        } else {
          for (const std::string& label : state.exited_contacts) {
            ImGui::BulletText("%s", label.c_str());
          }
        }

        ImGui::Separator();
        ImGui::TextUnformatted("Solid Contacts");
        if (state.solid_contacts.empty()) {
          ImGui::TextDisabled("None");
        } else {
          for (const std::string& label : state.solid_contacts) {
            ImGui::BulletText("%s", label.c_str());
          }
        }

        ImGui::Separator();
        ImGui::TextUnformatted("Recent Messages");
        if (state.recent_messages.empty()) {
          ImGui::TextDisabled("No events yet");
        } else {
          for (const std::string& line : state.recent_messages) {
            ImGui::TextWrapped("%s", line.c_str());
          }
        }
        ImGui::End();
      });
}

class CollisionEventsGame final : public app::GameInterface {
 public:
  explicit CollisionEventsGame(CollisionUiState& ui_state) : ui_state_(ui_state) {}

  void onStart() override {
    input->bindKey("player_forward", platform::Key::W);
    input->bindKey("player_forward", platform::Key::Up);
    input->bindKey("player_backward", platform::Key::S);
    input->bindKey("player_backward", platform::Key::Down);
    input->bindKey("player_turn_left", platform::Key::A);
    input->bindKey("player_turn_left", platform::Key::Left);
    input->bindKey("player_turn_right", platform::Key::D);
    input->bindKey("player_turn_right", platform::Key::Right);
    input->bindKey("player_jump", platform::Key::Space);
    input->bindKey("player_reset", platform::Key::R);

    auto world_entity = world->createEntity();
    world->setName(world_entity, "World");
    world->add(world_entity, components::TransformComponent{});
    world->add(world_entity, components::MeshComponent{
        .mesh_asset_key = importExampleMeshAsset(assets, "world.glb")});
    world->add(world_entity, components::ColliderComponent::mesh());

    player_entity_ = world->createEntity();
    world->setName(player_entity_, "Player");
    world->add(player_entity_, components::TransformComponent{});
    world->add(player_entity_, components::MeshComponent{
        .mesh_asset_key = importExampleMeshAsset(assets, "tank_final.glb")});
    world->add(player_entity_,
               components::ColliderComponent::box(
                   components::BoxColliderShape{
                       .center = {0.0f, 1.0f, 0.0f},
                       .half_extents = {1.0f, 1.0f, 1.0f}}));
    world->add(player_entity_, components::CharacterControllerComponent{});
    world->add(player_entity_,
               components::CollisionListenerComponent{
                   .enabled = true,
                   .mode = components::CollisionListenMode::TriggersOnly,
                   .emit_stay = true,
               });
    world->add(player_entity_, components::CollisionEventsComponent{});
    world->add(player_entity_, components::ContactListenerComponent{
        .enabled = true,
        .emit_stay = true,
    });
    world->add(player_entity_, components::ContactEventsComponent{});
    world->add(player_entity_, components::GroundContactComponent{});

    camera_entity_ = world->createEntity();
    world->setName(camera_entity_, "Camera");
    components::TransformComponent camera_xform{};
    camera_xform.setPosition(camera_follow_offset_);
    camera_xform.setRotation(math::fromYawPitch(0.0f, camera_pitch_));
    world->add(camera_entity_, camera_xform);
    world->add(camera_entity_, components::CameraComponent{.is_primary = true});
    world->add(camera_entity_, components::AudioListenerComponent{});

    auto light = world->createEntity();
    world->setName(light, "Sun Light");
    components::TransformComponent light_xform{};
    light_xform.setPosition({0.0f, 50.0f, 0.0f});
    light_xform.setRotation(math::fromYawPitch(0.45f, -0.9f));
    world->add(light, light_xform);
    world->add(light, components::LightComponent{
        .type = components::LightComponent::Type::Directional,
        .color = {1.0f, 1.0f, 1.0f, 1.0f},
        .intensity = 0.9f,
        .shadow_extent = 60.0f});

    auto environment = world->createEntity();
    world->setName(environment, "Environment");
    world->add(environment, components::EnvironmentComponent{
        .environment_map_asset_key = registerExampleEnvironmentMap(assets, "golden_gate_hills_4k.hdr"),
        .intensity = 0.5f,
        .draw_skybox = true});

    createTriggerZone("Repair Field", {-12.0f, 2.5f, -8.0f}, {0.15f, 0.85f, 1.0f, 0.18f}, 2.5f);
    createTriggerZone("Checkpoint Alpha", {10.0f, 2.5f, -6.0f}, {0.95f, 0.35f, 0.25f, 0.18f}, 2.7f);
    createTriggerZone("Boost Ring", {0.0f, 2.5f, 12.0f}, {0.65f, 0.25f, 1.0f, 0.18f}, 3.0f);
    createTriggerZone("Cooling Zone", {18.0f, 2.5f, 11.0f}, {0.25f, 1.0f, 0.35f, 0.18f}, 2.4f);
  }

  void onFixedUpdate(float /*dt*/) override {
    if (!world->isAlive(player_entity_)) {
      return;
    }

    auto& player_input = world->get<components::CharacterControllerComponent>(player_entity_);

    float forward_input = 0.0f;
    if (input->actionDown("player_forward")) {
      forward_input += 1.0f;
    }
    if (input->actionDown("player_backward")) {
      forward_input -= 1.0f;
    }

    float turn_input = 0.0f;
    if (input->actionDown("player_turn_left")) {
      turn_input += 1.0f;
    }
    if (input->actionDown("player_turn_right")) {
      turn_input -= 1.0f;
    }

    math::Vec3 move_forward = math::normalize(
        math::Vec3{player_input.forward.x, 0.0f, player_input.forward.z});
    if (math::lengthSquared(move_forward) <= 0.0001f) {
      move_forward = {0.0f, 0.0f, -1.0f};
    }
    float vertical_velocity = player_input.velocity.y;
    const bool grounded = player_input.grounded;
    player_input.setDesiredAngularVelocity({0.0f, turn_input * turn_speed_rad_, 0.0f});

    const bool jump_down = input->actionDown("player_jump");
    if (jump_down && !jump_down_prev_ && grounded) {
      vertical_velocity = jump_speed_;
    }
    jump_down_prev_ = jump_down;

    player_input.setDesiredVelocity({
        move_forward.x * forward_input * move_speed_,
        vertical_velocity,
        move_forward.z * forward_input * move_speed_,
    });

    const bool reset_down = input->actionDown("player_reset");
    if (reset_down && !reset_down_prev_) {
      resetPlayer();
    }
    reset_down_prev_ = reset_down;
  }

  void onPostFixedUpdate(float /*dt*/) override {
    ui_state_.grounded = false;
    ui_state_.grounded_entered = false;
    ui_state_.grounded_exited = false;
    ui_state_.support_label.clear();
    ui_state_.ground_normal = {0.0f, 1.0f, 0.0f};
    ui_state_.active_contacts.clear();
    ui_state_.entered_contacts.clear();
    ui_state_.exited_contacts.clear();
    ui_state_.solid_contacts.clear();

    if (!world->isAlive(player_entity_) || !world->has<components::CollisionEventsComponent>(player_entity_)) {
      return;
    }

    if (world->has<components::GroundContactComponent>(player_entity_)) {
      const auto& ground = world->get<components::GroundContactComponent>(player_entity_);
      ui_state_.grounded = ground.grounded;
      ui_state_.grounded_entered = ground.entered;
      ui_state_.grounded_exited = ground.exited;
      ui_state_.ground_normal = ground.normal;
      if (ground.has_support && world->isAlive(ground.support_entity)) {
        ui_state_.support_label = entityLabel(ground.support_entity);
      }
      if (ground.entered) {
        pushRecent("Ground contact: entered");
      }
      if (ground.exited) {
        pushRecent("Ground contact: exited");
      }
    }

    const auto& events = world->get<components::CollisionEventsComponent>(player_entity_);
    for (const auto& contact : events.active) {
      ui_state_.active_contacts.push_back(labelForContact(contact));
    }
    for (const auto& contact : events.entered) {
      const std::string label = labelForContact(contact);
      ui_state_.entered_contacts.push_back(label);
      pushRecent("Enter: " + label);
    }
    for (const auto& contact : events.exited) {
      const std::string label = labelForContact(contact);
      ui_state_.exited_contacts.push_back(label);
      pushRecent("Exit: " + label);
    }

    if (world->has<components::ContactEventsComponent>(player_entity_)) {
      const auto& contacts = world->get<components::ContactEventsComponent>(player_entity_);
      for (const auto& contact : contacts.active) {
        ui_state_.solid_contacts.push_back(
            entityLabel(contact.other) + formatContactNormal(contact.normal));
      }
    }
  }

  void onUpdate(float /*dt*/) override {
    if (!world->isAlive(camera_entity_) || !world->isAlive(player_entity_)) {
      return;
    }

    auto& camera_xform = world->get<components::TransformComponent>(camera_entity_);
    const auto& player_xform = world->get<components::TransformComponent>(player_entity_);
    const math::Vec3 player_pos =
        player_xform.getInterpolatedPosition(renderInterpolationAlpha());
    camera_xform.setPosition({player_pos.x + camera_follow_offset_.x,
                              player_pos.y + camera_follow_offset_.y,
                              player_pos.z + camera_follow_offset_.z});
    camera_xform.setRotation(math::fromYawPitch(0.0f, camera_pitch_));

    if (graphics != nullptr) {
      const float axis_len = 4.0f;
      graphics->drawLine({0.0f, 0.0f, 0.0f}, {axis_len, 0.0f, 0.0f},
                         {1.0f, 0.0f, 0.0f, 1.0f});
      graphics->drawLine({0.0f, 0.0f, 0.0f}, {0.0f, axis_len, 0.0f},
                         {0.0f, 1.0f, 0.0f, 1.0f});
      graphics->drawLine({0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, axis_len},
                         {0.0f, 0.0f, 1.0f, 1.0f});
    }
  }

  void onShutdown() override {}

 private:
  void createTriggerZone(std::string name,
                         const math::Vec3& position,
                         const math::Color& color,
                         float radius) {
    auto entity = world->createEntity();
    const std::string material_key = "runtime/collision_trigger/" + name + "/material";
    world->setName(entity, std::move(name));

    components::TransformComponent transform{};
    transform.setPosition(position);
    transform.setScale({radius, radius, radius});
    world->add(entity, transform);

    if (assets != nullptr) {
      rendering::MaterialDesc material{};
      material.base_color = color;
      material.emissive_color = {color.r * 1.8f, color.g * 1.8f, color.b * 1.8f, 1.0f};
      material.unlit = true;
      material.transparent = true;
      material.depth_write = false;
      material.double_sided = true;
      material.roughness = 1.0f;
      material.metallic = 0.0f;
      assets->registerMaterialAsset(material_key, material);
    }

    world->add(entity, components::MeshComponent{
        .mesh_asset_key = importExampleMeshAsset(assets, "wave.glb"),
        .materials = {components::MeshMaterialAssignment{
            .slot = 0,
            .material_key = material_key,
        }},
        .visible = true,
        .shadow_visible = false,
    });
    components::SphereColliderShape shape{};
    shape.center = {0.0f, 0.0f, 0.0f};
    // Sphere overlap queries already apply transform scale to collider radius.
    shape.radius = 1.0f;
    world->add(entity, components::ColliderComponent::sphere(shape, true));
  }

  void resetPlayer() {
    if (!world->isAlive(player_entity_)) {
      return;
    }

    auto& player_input = world->get<components::CharacterControllerComponent>(player_entity_);
    player_input.setDesiredVelocity({});
    player_input.setDesiredAngularVelocity({});
    player_input.setAddVelocity({});

    auto& player_xform = world->get<components::TransformComponent>(player_entity_);
    player_xform.setPosition({0.0f, 8.0f, 0.0f});
    player_xform.setRotation({});
  }

  std::string entityLabel(world::Entity entity) const {
    if (!world->isAlive(entity)) {
      return "Destroyed Entity";
    }
    if (world->has<components::TagComponent>(entity)) {
      const auto& tag = world->get<components::TagComponent>(entity);
      if (!tag.name.empty()) {
        return tag.name;
      }
    }
    return "Entity " + std::to_string(entity.index);
  }

  std::string labelForContact(const components::CollisionContact& contact) const {
    std::string label = entityLabel(contact.other);
    if (contact.other_is_trigger) {
      label += " (trigger)";
    }
    return label;
  }

  std::string formatContactNormal(const math::Vec3& normal) const {
    char buffer[96];
    std::snprintf(buffer, sizeof(buffer), " [n=(%.2f, %.2f, %.2f)]",
                  normal.x, normal.y, normal.z);
    return std::string(buffer);
  }

  void pushRecent(std::string message) {
    ui_state_.recent_messages.push_front(std::move(message));
    while (ui_state_.recent_messages.size() > 8u) {
      ui_state_.recent_messages.pop_back();
    }
  }

  CollisionUiState& ui_state_;
  world::Entity player_entity_{};
  world::Entity camera_entity_{};
  math::Vec3 camera_follow_offset_{0.0f, 8.0f, 12.0f};
  float camera_pitch_ = -0.55f;
  float move_speed_ = 12.0f;
  float jump_speed_ = 7.5f;
  float turn_speed_rad_ = 1.9f;
  bool jump_down_prev_ = false;
  bool reset_down_prev_ = false;
};

}  // namespace

}  // namespace karma::demo

int main() {
  karma::demo::CollisionUiState ui_state;
  karma::app::EngineApp engine;
  engine.setUi(karma::demo::createCollisionUiLayer(ui_state));

  karma::demo::CollisionEventsGame game(ui_state);

  karma::app::EngineConfig config;
  config.window.title = "Karma Collision Events Example";
  config.window.width = 1600;
  config.window.height = 900;
  config.window.samples = 8;
  config.window.icon_path = karma::demo::resolveExampleAssetPath("demo_icon.svg").string();
  config.shadow_map_size = 2048;
  config.shadow_bias = 0.0010f;
  config.shadow_pcf_radius = 1;
  config.local_light_distance_damping = 0.06f;
  config.lighting_exposure = 1.0f;

  engine.start(game, config);
  while (engine.isRunning()) {
    engine.tick();
  }
  return 0;
}
