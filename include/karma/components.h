#pragma once

#include "karma/math.h"
#include "karma/world.h"
#include "karma/rendering.h"
#include "karma/navigation.h"

namespace karma::physics { class PhysicsSystem; }




namespace karma::physics {
class PhysicsSystem;
}

namespace karma::world {
class World;
}

namespace karma::world {
class Scene;
void updateWorldTransforms(world::World& world, const Scene& scene);
}


#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>


namespace karma::components {

/// \ingroup karma_components
/// Runtime parameter storage kind for animator state machines.
enum class AnimatorParameterType : uint8_t {
  Bool,
  Int,
  Float,
  Trigger,
};

/// \ingroup karma_components
/// Comparison operation for animator transition conditions.
enum class AnimatorConditionOp : uint8_t {
  If,
  IfNot,
  Equals,
  NotEquals,
  Greater,
  GreaterOrEqual,
  Less,
  LessOrEqual,
};

/// \ingroup karma_components
/// Motion source used by an animator state.
enum class AnimatorMotionType : uint8_t {
  Clip,
  BlendTree1D,
};

/// \ingroup karma_components
/// Policy for whether active transitions can be interrupted.
enum class AnimatorInterruptPolicy : uint8_t {
  None,
  Source,
  Destination,
  SourceThenDestination,
  Any,
};

/// \ingroup karma_components
/// How sampled root-motion deltas are exposed or applied.
enum class RootMotionMode : uint8_t {
  Disabled,
  ApplyToLocalTransform,
  ExposeDelta,
};

/// \ingroup karma_components
/// Named runtime parameter used by animator transitions and blend trees.
struct AnimatorParameter {
  std::string name;
  AnimatorParameterType type = AnimatorParameterType::Float;
  bool bool_value = false;
  int int_value = 0;
  float float_value = 0.0f;
  bool trigger_value = false;
};

/// \ingroup karma_components
/// Single transition condition evaluated against animator parameters.
struct AnimatorCondition {
  std::string parameter;
  AnimatorConditionOp op = AnimatorConditionOp::If;
  bool bool_value = false;
  int int_value = 0;
  float float_value = 0.0f;
};

/// \ingroup karma_components
/// Child clip entry in a one-dimensional blend tree.
struct AnimatorBlendTree1DChild {
  uint32_t clip_index = world::kInvalidAnimationIndex;
  float threshold = 0.0f;
  float speed = 1.0f;
};

/// \ingroup karma_components
/// One-dimensional blend tree keyed by a float parameter.
struct AnimatorBlendTree1D {
  std::string parameter;
  std::vector<AnimatorBlendTree1DChild> children;
};

/// \ingroup karma_components
/// Transition from one animator state to another.
struct AnimatorTransition {
  uint32_t to_state_index = world::kInvalidAnimationIndex;
  std::vector<AnimatorCondition> conditions;
  float duration_seconds = 0.0f;
  bool has_exit_time = false;
  float exit_time_normalized = 1.0f;
  AnimatorInterruptPolicy interrupt_policy = AnimatorInterruptPolicy::None;
};

/// \ingroup karma_components
/// State-machine state backed by either a clip or a one-dimensional blend tree.
struct AnimatorState {
  std::string name;
  AnimatorMotionType motion_type = AnimatorMotionType::Clip;
  uint32_t clip_index = world::kInvalidAnimationIndex;
  AnimatorBlendTree1D blend_tree;
  float speed = 1.0f;
  bool loop = true;
  std::vector<AnimatorTransition> transitions;
};

/// \ingroup karma_components
/// Data-only animator graph stored on an entity.
struct AnimatorStateMachine {
  std::vector<AnimatorParameter> parameters;
  std::vector<AnimatorState> states;
  uint32_t entry_state_index = 0;
};

/// \ingroup karma_components
/// Runtime state for an active state-machine transition.
struct AnimatorTransitionRuntime {
  bool active = false;
  uint32_t from_state_index = world::kInvalidAnimationIndex;
  uint32_t to_state_index = world::kInvalidAnimationIndex;
  float elapsed_seconds = 0.0f;
  float duration_seconds = 0.0f;
  float from_time_seconds = 0.0f;
  float to_time_seconds = 0.0f;
  AnimatorInterruptPolicy interrupt_policy = AnimatorInterruptPolicy::None;
};

/// \ingroup karma_components
/// Event emitted while sampling an animation clip/state.
struct AnimatorEventRecord {
  std::string name;
  std::string payload;
  uint32_t clip_index = world::kInvalidAnimationIndex;
  uint32_t state_index = world::kInvalidAnimationIndex;
  float time_seconds = 0.0f;
};

/// \ingroup karma_components
/// Frame-stable buffer populated from an entity's `AnimatorComponent` events.
struct AnimationEventBufferComponent : world::ComponentTag {
  std::vector<AnimatorEventRecord> events;
  uint64_t sequence = 0;
};

/// \ingroup karma_components
/// Root-motion exposure/apply state for an entity's `AnimatorComponent`.
///
/// In `ExposeDelta` mode, `delta` accumulates sampled deltas until game code
/// calls `consumeRootMotionDelta(...)`.
struct RootMotionComponent : world::ComponentTag {
  RootMotionMode mode = RootMotionMode::Disabled;
  uint32_t root_motion_node_index = world::kInvalidAnimationIndex;
  world::SampledTransform delta;
  world::SampledTransform accumulated;
  bool has_unconsumed_delta = false;
};

/// \ingroup karma_components
/// Full animation state machine, blending, root motion, and skinning metadata.
///
/// `AnimationSystem` consumes this component. Clips and node maps normally come
/// from glTF import. Game code changes parameters or calls helper functions to
/// drive playback.
struct AnimatorComponent : world::ComponentTag {
  std::vector<world::AnimationClip> clips;
  std::vector<world::Entity> node_entities_by_index;
  /// Renderable morph primitive entities keyed by imported glTF node index.
  std::vector<std::vector<world::Entity>> morph_entities_by_node_index;
  std::vector<world::Skeleton> skeletons;
  std::vector<world::Skin> skins;

  size_t current_clip_index = 0;
  float time_seconds = 0.0f;
  float speed = 1.0f;
  bool loop = true;
  bool playing = false;

  bool blend_active = false;
  size_t blend_from_clip_index = 0;
  float blend_from_time_seconds = 0.0f;
  float blend_elapsed_seconds = 0.0f;
  float blend_duration_seconds = 0.0f;

  AnimatorStateMachine state_machine;
  uint32_t current_state_index = world::kInvalidAnimationIndex;
  float state_time_seconds = 0.0f;
  AnimatorTransitionRuntime transition;

  RootMotionMode root_motion_mode = RootMotionMode::Disabled;
  uint32_t root_motion_node_index = world::kInvalidAnimationIndex;
  world::SampledTransform root_motion_delta;
  world::SampledTransform root_motion_accumulated;

  std::vector<AnimatorEventRecord> event_queue;
};

/// Switches animator playback to a clip by index.
bool setAnimatorClip(AnimatorComponent& animator,
                     size_t clip_index,
                     bool reset_time = true,
                     float blend_duration_seconds = 0.0f);
/// Switches animator playback to a clip by name.
bool setAnimatorClip(AnimatorComponent& animator,
                     std::string_view clip_name,
                     bool reset_time = true,
                     float blend_duration_seconds = 0.0f);
/// Starts or resumes animator playback.
void playAnimator(AnimatorComponent& animator);
/// Pauses animator playback without rewinding.
void pauseAnimator(AnimatorComponent& animator);
/// Stops animator playback and resets time.
void stopAnimator(AnimatorComponent& animator);

/// Finds a mutable animator parameter by name.
AnimatorParameter* findAnimatorParameter(AnimatorComponent& animator, std::string_view name);
/// Finds an animator parameter by name.
const AnimatorParameter* findAnimatorParameter(const AnimatorComponent& animator,
                                               std::string_view name);
/// Sets a bool parameter.
bool setAnimatorBool(AnimatorComponent& animator, std::string_view name, bool value);
/// Sets an int parameter.
bool setAnimatorInt(AnimatorComponent& animator, std::string_view name, int value);
/// Sets a float parameter.
bool setAnimatorFloat(AnimatorComponent& animator, std::string_view name, float value);
/// Raises a trigger parameter for one transition evaluation.
bool setAnimatorTrigger(AnimatorComponent& animator, std::string_view name);
/// Clears a trigger parameter manually.
bool resetAnimatorTrigger(AnimatorComponent& animator, std::string_view name);

/// Returns and clears the currently exposed root-motion delta.
world::SampledTransform consumeRootMotionDelta(RootMotionComponent& root_motion);

}  // namespace karma::components


namespace karma::components {

/// \ingroup karma_components
/// Authored local transform plus cached world transform.
///
/// Scene hierarchy code composes local values into the cached world values.
/// Systems such as rendering, physics, audio, and particles read the world
/// accessors. Physics integration uses private friends to update world values
/// without treating them as authored local changes.
class TransformComponent : public world::ComponentTag {
 public:
  TransformComponent();
  TransformComponent(const math::Vec3& position, const math::Quat& rotation = {},
                     const math::Vec3& scale = {1.0f, 1.0f, 1.0f});

  /// Authored local position.
  const math::Vec3& localPosition() const { return local_position_; }
  /// Authored local rotation.
  const math::Quat& localRotation() const { return local_rotation_; }
  /// Authored local scale.
  const math::Vec3& localScale() const { return local_scale_; }
  /// Cached world position.
  const math::Vec3& worldPosition() const { return world_position_; }
  /// Cached world rotation.
  const math::Quat& worldRotation() const { return world_rotation_; }
  /// Cached world scale.
  const math::Vec3& worldScale() const { return world_scale_; }

  /// Current world-space position.
  const math::Vec3& getPosition() const { return worldPosition(); }
  /// Current world-space rotation.
  const math::Quat& getRotation() const { return worldRotation(); }
  /// Current world-space scale.
  const math::Vec3& getScale() const { return worldScale(); }
  /// Interpolated position between previous and current values.
  math::Vec3 getInterpolatedPosition(float alpha) const;
  /// Interpolated rotation between previous and current values.
  math::Quat getInterpolatedRotation(float alpha) const;

  /// Sets authored local position and mirrors it to world for unparented use.
  void setLocalPosition(const math::Vec3& position);
  /// Sets authored local rotation and mirrors it to world for unparented use.
  void setLocalRotation(const math::Quat& rotation);
  /// Sets authored local scale and mirrors it to world for unparented use.
  void setLocalScale(const math::Vec3& scale);

  /// Sets cached world position and records interpolation history.
  void setWorldPosition(const math::Vec3& position);
  /// Sets cached world rotation and records interpolation history.
  void setWorldRotation(const math::Quat& rotation);
  /// Sets cached world scale.
  void setWorldScale(const math::Vec3& scale);

  /// Convenience setter for authored local position.
  void setPosition(const math::Vec3& position) { setLocalPosition(position); }
  /// Convenience setter for authored local rotation.
  void setRotation(const math::Quat& rotation) { setLocalRotation(rotation); }
  /// Convenience setter for authored local scale.
  void setScale(const math::Vec3& scale) { setLocalScale(scale); }

  friend class world::World;
  friend class physics::PhysicsSystem;
  friend void world::updateWorldTransforms(world::World& world, const world::Scene& scene);

 private:
  void setPositionFromPhysics(const math::Vec3& position);
  void setRotationFromPhysics(const math::Quat& rotation);
  void setWorldFromHierarchy(const math::Vec3& position,
                             const math::Quat& rotation,
                             const math::Vec3& scale,
                             bool reset_history);

 private:
  math::Vec3 local_position_{};
  math::Quat local_rotation_{};
  math::Vec3 local_scale_{1.0f, 1.0f, 1.0f};
  math::Vec3 world_position_{};
  math::Quat world_rotation_{};
  math::Vec3 world_scale_{1.0f, 1.0f, 1.0f};
  math::Vec3 previous_position_{};
  math::Quat previous_rotation_{};
  bool position_dirty_ = false;
  bool rotation_dirty_ = false;
  bool hierarchy_initialized_ = false;
};

}  // namespace karma::components



namespace karma::components {

/// \ingroup karma_components
/// Marks an entity transform as the active audio listener.
struct AudioListenerComponent : world::ComponentTag {};

}  // namespace karma::components


#include <string>


namespace karma::components {

/// \ingroup karma_components
/// Audio emitter bound to a clip key loaded by `AudioSystem`.
///
/// `play_on_start` is consumed once by the audio system. Calling `play()`
/// records a transient request that is consumed on the next audio update.
class AudioSourceComponent : public world::ComponentTag {
 public:
  std::string clip_key;
  float gain = 1.0f;
  float pitch = 1.0f;
  float min_distance = 1.0f;
  float max_distance = 20.0f;
  bool looping = false;
  bool play_on_start = false;
  bool spatialized = true;
  int max_instances = 5;

  /// Requests one playback instance on the next audio-system update.
  void play() { play_requested_ = true; }

  /// Returns and clears a pending playback request.
  bool consumePlayRequest() {
    if (!play_requested_) {
      return false;
    }
    play_requested_ = false;
    return true;
  }

 private:
  bool play_requested_ = false;
};

}  // namespace karma::components


#include <filesystem>
#include <string>
#include <unordered_map>


namespace karma::components {

/// \ingroup karma_components
/// Camera authoring data extracted by `RenderSystem`.
///
/// A scene should usually have one primary camera. Cameras can render to the
/// default target or to named render targets, and may provide shader override
/// paths plus small color parameter payloads for custom camera effects.
/// `post_process_profile_key` selects a renderer post-process profile; empty
/// or missing profile names use the engine default profile. Cameras do not own
/// post-process passes, shader assets, render targets, or history resources.
struct CameraComponent : world::ComponentTag {
  bool perspective = true;
  bool render_shadows = true;
  float fov_y_degrees = 60.0f;
  float near_clip = 0.1f;
  float far_clip = 1000.0f;
  float ortho_left = -1.0f;
  float ortho_right = 1.0f;
  float ortho_top = 1.0f;
  float ortho_bottom = -1.0f;
  bool is_primary = false;
  bool render_to_texture = false;
  rendering::RenderTargetId render_target = rendering::kDefaultRenderTarget;
  std::string render_target_key;
  /// Name of the post-process profile resolved for this camera pass.
  std::string post_process_profile_key;
  std::filesystem::path shader_override_vertex_path;
  std::filesystem::path shader_override_fragment_path;
  std::unordered_map<std::string, math::Color> shader_user_params;
};

}  // namespace karma::components


#include <array>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>


namespace karma::components {

/// \ingroup karma_components
/// Collider shape kind exposed by public components and ECS query helpers.
enum class ColliderShapeType : uint8_t {
  Box,
  Sphere,
  Capsule,
  Cylinder,
  TaperedCapsule,
  ConvexHull,
  Triangle,
  HeightField,
  Mesh,
};

/// \ingroup karma_components
/// Axis-aligned or transform-oriented box collider.
struct BoxColliderShape {
  math::Vec3 center{};
  math::Vec3 half_extents{0.5f, 0.5f, 0.5f};
};

/// \ingroup karma_components
/// Sphere collider.
struct SphereColliderShape {
  math::Vec3 center{};
  float radius = 0.5f;
};

/// \ingroup karma_components
/// Capsule collider.
struct CapsuleColliderShape {
  math::Vec3 center{};
  float radius = 0.5f;
  float height = 1.0f;
};

/// \ingroup karma_components
/// Cylinder collider aligned to the entity's local Y axis.
struct CylinderColliderShape {
  math::Vec3 center{};
  float radius = 0.5f;
  float height = 1.0f;
  float convex_radius = 0.0f;
};

/// \ingroup karma_components
/// Tapered capsule collider aligned to the entity's local Y axis.
struct TaperedCapsuleColliderShape {
  math::Vec3 center{};
  float top_radius = 0.5f;
  float bottom_radius = 0.5f;
  float height = 1.0f;
};

/// \ingroup karma_components
/// Convex hull collider built from local-space points.
struct ConvexHullColliderShape {
  math::Vec3 center{};
  std::vector<math::Vec3> points;
  float convex_radius = 0.0f;
};

/// \ingroup karma_components
/// Single local-space triangle collider.
struct TriangleColliderShape {
  std::array<math::Vec3, 3> points{};
  float convex_radius = 0.0f;
};

/// \ingroup karma_components
/// Height-field collider. Samples are row-major and require `sample_count * sample_count` values.
struct HeightFieldColliderShape {
  std::vector<float> samples;
  uint32_t sample_count = 0;
  math::Vec3 offset{};
  math::Vec3 scale{1.0f, 1.0f, 1.0f};
  uint32_t block_size = 2;
  uint32_t bits_per_sample = 8;
};

/// \ingroup karma_components
/// Mesh collider using registered mesh asset geometry.
struct MeshColliderShape {
  std::string mesh_asset_key;
  std::vector<math::Vec3> vertices;
  std::vector<uint32_t> indices;
};

using ColliderShape = std::variant<BoxColliderShape,
                                   SphereColliderShape,
                                   CapsuleColliderShape,
                                   CylinderColliderShape,
                                   TaperedCapsuleColliderShape,
                                   ConvexHullColliderShape,
                                   TriangleColliderShape,
                                   HeightFieldColliderShape,
                                   MeshColliderShape>;

/// \ingroup karma_components
/// Single collider component tagged by shape variant.
struct ColliderComponent : world::ComponentTag {
  ColliderShapeType type = ColliderShapeType::Box;
  bool is_trigger = false;
  bool debug_draw = false;
  ColliderShape shape = BoxColliderShape{};

  static ColliderComponent box(BoxColliderShape shape = {},
                               bool is_trigger = false,
                               bool debug_draw = false) {
    return ColliderComponent{
        .type = ColliderShapeType::Box,
        .is_trigger = is_trigger,
        .debug_draw = debug_draw,
        .shape = std::move(shape),
    };
  }

  static ColliderComponent sphere(SphereColliderShape shape = {},
                                  bool is_trigger = false,
                                  bool debug_draw = false) {
    return ColliderComponent{
        .type = ColliderShapeType::Sphere,
        .is_trigger = is_trigger,
        .debug_draw = debug_draw,
        .shape = std::move(shape),
    };
  }

  static ColliderComponent capsule(CapsuleColliderShape shape = {},
                                   bool is_trigger = false,
                                   bool debug_draw = false) {
    return ColliderComponent{
        .type = ColliderShapeType::Capsule,
        .is_trigger = is_trigger,
        .debug_draw = debug_draw,
        .shape = std::move(shape),
    };
  }

  static ColliderComponent cylinder(CylinderColliderShape shape = {},
                                    bool is_trigger = false,
                                    bool debug_draw = false) {
    return ColliderComponent{
        .type = ColliderShapeType::Cylinder,
        .is_trigger = is_trigger,
        .debug_draw = debug_draw,
        .shape = std::move(shape),
    };
  }

  static ColliderComponent taperedCapsule(TaperedCapsuleColliderShape shape = {},
                                          bool is_trigger = false,
                                          bool debug_draw = false) {
    return ColliderComponent{
        .type = ColliderShapeType::TaperedCapsule,
        .is_trigger = is_trigger,
        .debug_draw = debug_draw,
        .shape = std::move(shape),
    };
  }

  static ColliderComponent convexHull(ConvexHullColliderShape shape = {},
                                      bool is_trigger = false,
                                      bool debug_draw = false) {
    return ColliderComponent{
        .type = ColliderShapeType::ConvexHull,
        .is_trigger = is_trigger,
        .debug_draw = debug_draw,
        .shape = std::move(shape),
    };
  }

  static ColliderComponent triangle(TriangleColliderShape shape = {},
                                    bool is_trigger = false,
                                    bool debug_draw = false) {
    return ColliderComponent{
        .type = ColliderShapeType::Triangle,
        .is_trigger = is_trigger,
        .debug_draw = debug_draw,
        .shape = std::move(shape),
    };
  }

  static ColliderComponent heightField(HeightFieldColliderShape shape = {},
                                       bool is_trigger = false,
                                       bool debug_draw = false) {
    return ColliderComponent{
        .type = ColliderShapeType::HeightField,
        .is_trigger = is_trigger,
        .debug_draw = debug_draw,
        .shape = std::move(shape),
    };
  }

  static ColliderComponent mesh(MeshColliderShape shape = {},
                                bool is_trigger = false,
                                bool debug_draw = false) {
    return ColliderComponent{
        .type = ColliderShapeType::Mesh,
        .is_trigger = is_trigger,
        .debug_draw = debug_draw,
        .shape = std::move(shape),
    };
  }

  static void Validate(world::World& world, world::Entity entity) {
    if (!world.has<TransformComponent>(entity)) {
      throw std::runtime_error("ColliderComponent requires TransformComponent on the same entity.");
    }
  }
};

inline ColliderShapeType colliderShapeType(const ColliderShape& shape) {
  return std::visit(
      [](const auto& value) {
        using Shape = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<Shape, BoxColliderShape>) {
          return ColliderShapeType::Box;
        } else if constexpr (std::is_same_v<Shape, SphereColliderShape>) {
          return ColliderShapeType::Sphere;
        } else if constexpr (std::is_same_v<Shape, CapsuleColliderShape>) {
          return ColliderShapeType::Capsule;
        } else if constexpr (std::is_same_v<Shape, CylinderColliderShape>) {
          return ColliderShapeType::Cylinder;
        } else if constexpr (std::is_same_v<Shape, TaperedCapsuleColliderShape>) {
          return ColliderShapeType::TaperedCapsule;
        } else if constexpr (std::is_same_v<Shape, ConvexHullColliderShape>) {
          return ColliderShapeType::ConvexHull;
        } else if constexpr (std::is_same_v<Shape, TriangleColliderShape>) {
          return ColliderShapeType::Triangle;
        } else if constexpr (std::is_same_v<Shape, HeightFieldColliderShape>) {
          return ColliderShapeType::HeightField;
        } else {
          return ColliderShapeType::Mesh;
        }
      },
      shape);
}

inline bool colliderTypeMatchesShape(const ColliderComponent& collider) {
  return collider.type == colliderShapeType(collider.shape);
}

inline bool isCharacterControllerShape(ColliderShapeType type) {
  return type == ColliderShapeType::Box;
}

}  // namespace karma::components


#include <cstdint>
#include <vector>


namespace karma::components {

/// \ingroup karma_components
/// Which collider classes a collision listener records.
enum class CollisionListenMode : uint8_t {
  All = 0,
  TriggersOnly = 1,
  SolidsOnly = 2,
};

/// \ingroup karma_components
/// Overlap/trigger contact against another entity.
struct CollisionContact {
  world::Entity other{};
  ColliderShapeType other_shape = ColliderShapeType::Box;
  bool other_is_trigger = false;
};

/// \ingroup karma_components
/// Opt-in listener for ECS overlap and trigger events.
struct CollisionListenerComponent : world::ComponentTag {
  bool enabled = true;
  CollisionListenMode mode = CollisionListenMode::All;
  bool emit_stay = false;
  uint32_t collision_layer_mask = 0xFFFFFFFFu;
};

/// \ingroup karma_components
/// Per-frame overlap event buffers written by `CollisionEventSystem`.
struct CollisionEventsComponent : world::ComponentTag {
  std::vector<CollisionContact> entered;
  std::vector<CollisionContact> stayed;
  std::vector<CollisionContact> exited;
  std::vector<CollisionContact> active;

  /// Clears one-frame event buffers.
  void clearTransient() {
    entered.clear();
    stayed.clear();
    exited.clear();
    active.clear();
  }

  /// Returns true when no collision contacts are recorded.
  bool empty() const {
    return entered.empty() && stayed.empty() && exited.empty() && active.empty();
  }
};

}  // namespace karma::components


#include <cstdint>
#include <vector>


namespace karma::components {

/// \ingroup karma_components
/// Solid contact point produced by the physics backend.
struct ContactEvent {
  world::Entity other{};
  ColliderShapeType other_shape = ColliderShapeType::Box;
  math::Vec3 point{};
  math::Vec3 normal{0.0f, 1.0f, 0.0f};
};

/// \ingroup karma_components
/// Opt-in listener for physics contact enter/stay/exit events.
struct ContactListenerComponent : world::ComponentTag {
  bool enabled = true;
  bool emit_stay = false;
  uint32_t collision_layer_mask = 0xFFFFFFFFu;
};

/// \ingroup karma_components
/// Per-frame solid-contact buffers written by `PhysicsSystem`.
struct ContactEventsComponent : world::ComponentTag {
  std::vector<ContactEvent> entered;
  std::vector<ContactEvent> stayed;
  std::vector<ContactEvent> exited;
  std::vector<ContactEvent> active;

  /// Clears one-frame event buffers.
  void clearTransient() {
    entered.clear();
    stayed.clear();
    exited.clear();
    active.clear();
  }
};

}  // namespace karma::components



namespace karma::components {

/// \ingroup karma_components
/// Ground/support state written by physics/character-controller integration.
///
/// `entered` and `exited` are one-frame flags. `support_entity`, point, and
/// normal describe the current support surface when available.
struct GroundContactComponent : world::ComponentTag {
  bool grounded = false;
  bool entered = false;
  bool exited = false;
  bool has_support = false;
  world::Entity support_entity{};
  math::Vec3 point{};
  math::Vec3 normal{0.0f, 1.0f, 0.0f};

  /// Clears one-frame enter/exit flags.
  void clearTransient() {
    entered = false;
    exited = false;
  }
};

}  // namespace karma::components


#include <stdexcept>


namespace karma::components {

/// \ingroup karma_components
/// Game-input bridge for the physics character controller.
///
/// The component requires `TransformComponent` and a `ColliderComponent` with a
/// box shape on the same entity.
struct CharacterControllerComponent : world::ComponentTag {
  bool enabled = true;
  /// System-written current controller velocity.
  math::Vec3 velocity{};
  /// System-written current controller angular velocity.
  math::Vec3 angular_velocity{};
  /// System-written controller forward vector.
  math::Vec3 forward{0.0f, 0.0f, -1.0f};
  /// System-written grounded state.
  bool grounded = false;

  /// Sets continuous desired movement velocity.
  void setDesiredVelocity(const math::Vec3& velocity) { desired_velocity_ = velocity; }
  /// Sets continuous desired angular velocity.
  void setDesiredAngularVelocity(const math::Vec3& velocity) {
    desired_angular_velocity_ = velocity;
  }
  /// Adds a one-shot velocity impulse.
  void addImpulse(const math::Vec3& velocity) { add_velocity_ = velocity; }
  /// Replaces the pending additive velocity.
  void setAddVelocity(const math::Vec3& velocity) { add_velocity_ = velocity; }
  /// Returns the desired movement velocity.
  const math::Vec3& desiredVelocity() const { return desired_velocity_; }
  /// Returns the desired angular velocity.
  const math::Vec3& desiredAngularVelocity() const { return desired_angular_velocity_; }
  /// Returns the pending additive velocity.
  const math::Vec3& addVelocity() const { return add_velocity_; }
  /// Clears the pending additive velocity.
  void clearImpulse() { add_velocity_ = {}; }

  static void Validate(world::World& world, world::Entity entity) {
    if (!world.has<TransformComponent>(entity)) {
      throw std::runtime_error(
          "CharacterControllerComponent requires TransformComponent on the same entity.");
    }
    if (!world.has<ColliderComponent>(entity)) {
      throw std::runtime_error(
          "CharacterControllerComponent requires ColliderComponent on the same entity.");
    }
    const ColliderComponent& collider = world.get<ColliderComponent>(entity);
    if (!colliderTypeMatchesShape(collider)) {
      throw std::runtime_error(
          "CharacterControllerComponent requires ColliderComponent type to match its shape.");
    }
    if (!isCharacterControllerShape(collider.type)) {
      throw std::runtime_error(
          "CharacterControllerComponent requires a box ColliderComponent.");
    }
  }

 private:
  math::Vec3 desired_velocity_{};
  math::Vec3 desired_angular_velocity_{};
  math::Vec3 add_velocity_{};
};

}  // namespace karma::components


#include <cstdint>
#include <string>
#include <vector>

#include <glm/glm.hpp>


namespace karma::components {

/// \ingroup karma_components
/// Selected deformation execution path.
enum class DeformationPath {
  Gpu,
  CpuReference,
};

/// \ingroup karma_components
/// Four-joint skin influence payload for one vertex.
struct VertexSkinInfluence {
  glm::uvec4 joints{0u, 0u, 0u, 0u};
  glm::vec4 weights{0.0f, 0.0f, 0.0f, 0.0f};
};

/// \ingroup karma_components
/// Unified runtime deformation state for one renderable mesh.
///
/// glTF import fills bind mesh, skin binding, morph weights, joint entities, and
/// inverse bind matrices. `DeformationSystem` builds joint palettes, updates the
/// renderer-owned deformation resource, and only uploads CPU-deformed meshes for
/// explicit reference/diagnostic paths.
struct DeformableMeshComponent : world::ComponentTag {
  world::MeshData bind_mesh;
  world::MeshData cpu_deformed_mesh;

  std::vector<VertexSkinInfluence> vertex_influences;
  std::vector<world::Entity> joint_entities;
  std::vector<glm::mat4> inverse_bind_matrices;
  std::vector<glm::mat4> joint_palette;

  std::vector<float> base_morph_weights;
  std::vector<float> morph_weights;

  world::Entity render_transform_entity{};
  rendering::DeformationId deformation = rendering::kInvalidDeformation;
  uint32_t skin_index = 0;
  DeformationPath path = DeformationPath::Gpu;
  std::string diagnostic;

  bool palette_valid = false;
  bool morph_weights_dirty = true;
  bool override_render_transform = false;
  bool renderer_mesh_is_cpu_deformed = false;
  bool enabled = true;

  bool skinned() const {
    return !joint_entities.empty() &&
           vertex_influences.size() == bind_mesh.vertices.size();
  }

  bool morphable() const {
    return !bind_mesh.morph_targets.empty();
  }
};

}  // namespace karma::components


#include <string>


namespace karma::components {

/// \ingroup karma_components
/// Environment map and skybox settings extracted by `RenderSystem`.
struct EnvironmentComponent : world::ComponentTag {
  std::string environment_map_asset_key;
  float intensity = 1.0f;
  bool draw_skybox = true;
  bool enabled = true;
};

}  // namespace karma::components


#include <cstdint>
#include <string>
#include <vector>


namespace karma::components {

/// Assigned material asset or variant for one mesh material slot.
struct MeshMaterialAssignment {
  uint32_t slot = 0;
  std::string material_key;

  bool operator==(const MeshMaterialAssignment&) const = default;
};

/// \ingroup karma_components
/// Mesh/material assignment data extracted by `RenderSystem`.
///
/// Mesh assets may provide default material slots. Entries in `materials`
/// assign replacement material keys to individual slots for this object.
/// Key fields refer to normalized assets registered in `assets::AssetRegistry`.
struct MeshComponent : world::ComponentTag {
  std::string mesh_asset_key;
  std::vector<MeshMaterialAssignment> materials;
  bool visible = true;
  bool shadow_visible = true;
};

}  // namespace karma::components

#include <array>
#include <cstdint>
#include <string>
#include <vector>


namespace karma::components {

/// One authored instance inside an `InstancedMeshComponent`.
struct MeshInstance {
  math::Vec3 position{};
  math::Quat rotation{};
  math::Vec3 scale{1.0f, 1.0f, 1.0f};
  std::array<float, 4> params{0.0f, 0.0f, 0.0f, 0.0f};
};

/// One planar/yaw-only authored instance for compact GPU instancing.
struct PlanarMeshInstance {
  math::Vec3 position{};
  float yaw_radians = 0.0f;
  math::Vec3 scale{1.0f, 1.0f, 1.0f};
  std::array<float, 4> params{0.0f, 0.0f, 0.0f, 0.0f};
};

inline constexpr size_t kMaxInstancedMeshLodLevels = 3u;

/// Optional alternate mesh/material used after an instance reaches a distance.
struct InstancedMeshLodLevel {
  float start_distance = 0.0f;
  std::string mesh_asset_key;
  std::vector<MeshMaterialAssignment> materials;
  rendering::InstanceLodRenderMode render_mode = rendering::InstanceLodRenderMode::Mesh;
  bool shadow_visible = false;
};

/// \ingroup karma_components
/// Shared mesh/material binding plus many per-instance transforms.
///
/// Instance data is authored in world-layer types. `RenderSystem` translates it
/// into renderer instance buffers and keeps material slot fallback behavior the
/// same as `MeshComponent`.
struct InstancedMeshComponent : world::ComponentTag {
  std::string mesh_asset_key;
  std::vector<MeshMaterialAssignment> materials;
  std::vector<InstancedMeshLodLevel> lods;
  rendering::InstanceGpuLayout gpu_layout = rendering::InstanceGpuLayout::Matrix4x4Params;
  std::vector<MeshInstance> instances;
  std::vector<PlanarMeshInstance> planar_instances;
  uint64_t instance_revision = 0;
  bool dynamic = false;
  bool visible = true;
  bool shadow_visible = true;
};

}  // namespace karma::components


#include <cstdint>

namespace karma::components {

/// \ingroup karma_components
/// Coarse render-layer labels used to build visibility masks.
enum class RenderLayer : uint8_t {
  Default = 0,
  World = 1,
  UI = 2,
  Skybox = 3,
  Effects = 4,
  Debug = 5
};

/// \ingroup karma_components
/// Coarse collision-layer labels used to build collision masks.
enum class CollisionLayer : uint8_t {
  Default = 0,
  Static = 1,
  Dynamic = 2,
  Character = 3,
  Trigger = 4,
  Projectile = 5
};

/// Returns a bit mask for a render layer.
constexpr uint32_t layerBit(RenderLayer layer) {
  return 1u << static_cast<uint32_t>(layer);
}

/// Returns a bit mask for a collision layer.
constexpr uint32_t layerBit(CollisionLayer layer) {
  return 1u << static_cast<uint32_t>(layer);
}

}  // namespace karma::components



namespace karma::components {

/// \ingroup karma_components
/// Light data extracted by `RenderSystem`.
///
/// Directional, point, and spot lights share one component. Point lights can
/// request shadows when the renderer has point-shadow budget available.
struct LightComponent : world::ComponentTag {
  /// Light shape/type consumed by the renderer.
  enum class Type {
    Directional,
    Point,
    Spot
  };

  Type type = Type::Point;
  math::Color color{1.0f, 1.0f, 1.0f, 1.0f};
  float intensity = 1.0f;
  float range = 10.0f;
  float inner_cone_degrees = 15.0f;
  float outer_cone_degrees = 30.0f;
  bool casts_shadows = false;
  float shadow_extent = 0.0f;
};

}  // namespace karma::components



namespace karma::components {

/// \ingroup karma_components
/// Time-based light intensity/range envelope.
///
/// `LightPulseSystem` updates the paired `LightComponent` and can hide the
/// entity when the pulse completes.
struct LightPulseComponent : world::ComponentTag {
  bool enabled = true;
  bool active = true;
  float start_delay = 0.0f;
  float duration = 0.64f;
  float peak_intensity = 0.0f;
  float peak_range = 0.0f;
  float off_intensity = 0.0f;
  float off_range = 0.1f;
  float intensity_power = 2.0f;
  float range_power = 0.5f;
  float range_floor_factor = 0.35f;
  bool hide_after_completion = true;
  float elapsed = 0.0f;
};

}  // namespace karma::components



#include <cstdint>


namespace karma::components {

/// \ingroup karma_components
/// How `NavigationSystem` applies DetourCrowd movement to an entity.
enum class NavCrowdMovementMode {
  Transform,
  CharacterControllerVelocity,
};

/// \ingroup karma_components
/// Owns a DetourCrowd instance for a navmesh entity.
struct NavCrowdComponent : world::ComponentTag {
  bool enabled = true;
  bool build_on_start = true;
  bool rebuild_requested = true;
  bool built = false;
  bool simulation_paused = false;
  bool step_requested = false;
  float step_dt = 1.0f / 20.0f;
  float time_scale = 1.0f;
  uint64_t nav_mesh_build_version = 0;
  navigation::NavCrowdConfig config{};
  navigation::NavCrowdBuildResult last_build_result{};
  navigation::NavCrowdDebugRequest debug_request{};
  navigation::NavCrowdDebugSnapshot debug_snapshot{};
  navigation::NavCrowd crowd{};
};

/// \ingroup karma_components
/// ECS-authored DetourCrowd agent.
struct NavCrowdAgentComponent : world::ComponentTag {
  bool enabled = true;
  world::Entity crowd_entity{};
  world::Entity cached_crowd_entity{};
  navigation::NavCrowdAgentParams params{};
  math::Vec3 destination{};
  math::Vec3 requested_velocity{};
  math::Vec3 current_velocity{};
  math::Vec3 search_extents{2.0f, 4.0f, 2.0f};
  float height_offset = 0.0f;
  float stopping_distance = 0.2f;
  int agent_id = -1;
  bool has_destination = false;
  bool destination_requested = false;
  bool velocity_requested = false;
  bool params_dirty = false;
  bool remove_requested = false;
  NavCrowdMovementMode movement_mode = NavCrowdMovementMode::Transform;
  navigation::NavCrowdAgentState state = navigation::NavCrowdAgentState::Invalid;
  navigation::NavCrowdTargetState target_state = navigation::NavCrowdTargetState::None;
  navigation::NavStatus last_request_status = navigation::NavStatus::QueryFailed;
  bool partial = false;
  bool reached_destination = false;
};

}  // namespace karma::components


#include <cstdint>
#include <memory>
#include <string>
#include <vector>


namespace karma::components {

/// \ingroup karma_components
/// Marks mesh geometry as a source surface for navmesh builds.
struct NavMeshSurfaceComponent : world::ComponentTag {
  bool enabled = true;
  uint32_t layer_mask = 0xffffffffu;
  unsigned char area = navigation::kNavAreaDefault;
  bool walkable = true;
  std::shared_ptr<const world::MeshData> mesh_data;
  std::string mesh_asset_key;
};

/// \ingroup karma_components
/// Authored off-mesh navigation connection.
struct NavOffMeshLinkComponent : world::ComponentTag {
  bool enabled = true;
  uint32_t layer_mask = 0xffffffffu;
  world::Entity end_entity{};
  math::Vec3 start_offset{};
  math::Vec3 end_offset{};
  float radius = 0.4f;
  unsigned char area = navigation::kNavAreaDefault;
  uint16_t flags = navigation::kNavPolyFlagWalk | navigation::kNavPolyFlagOffMesh;
  bool bidirectional = true;
  uint32_t user_id = 0;
};

/// \ingroup karma_components
/// Marks a vertical convex volume with a navigation area during navmesh builds.
struct NavConvexVolumeComponent : world::ComponentTag {
  bool enabled = true;
  uint32_t layer_mask = 0xffffffffu;
  std::vector<math::Vec3> vertices;
  float min_y = 0.0f;
  float max_y = 2.0f;
  unsigned char area = navigation::kNavAreaDefault;
};

/// \ingroup karma_components
/// Controls persistent navigation cache use for a navmesh build.
struct NavMeshCacheSettings {
  bool enabled = false;
  bool read = true;
  bool write = true;
};

/// \ingroup karma_components
/// Owns a baked navigation mesh and build settings for an entity.
struct NavMeshComponent : world::ComponentTag {
  bool enabled = true;
  bool build_on_start = true;
  bool rebuild_requested = true;
  bool built = false;
  bool debug_draw = true;
  navigation::NavMeshDebugDrawMode debug_draw_mode = navigation::NavMeshDebugDrawMode::NavMeshEdges;
  uint64_t build_version = 0;
  uint32_t source_mask = 0xffffffffu;
  NavMeshCacheSettings cache{};
  navigation::NavMeshBuildConfig build_config{};
  navigation::NavMeshBuildResult last_build_result{};
  navigation::NavMesh nav_mesh{};
  bool build_debug_draw_requested = false;
};

}  // namespace karma::components


#include <cstddef>
#include <cstdint>
#include <vector>


namespace karma::components {

/// \ingroup karma_components
/// High-level path-following state for `NavigationSystem`.
enum class NavMeshAgentStatus {
  Idle,
  Requested,
  PathPending,
  PathResolved,
  Moving,
  Arrived,
  Failed,
  PartialPath,
};

/// \ingroup karma_components
/// Navigation agent request/result data.
///
/// Game code normally calls `NavigationSystem::requestMoveTo(...)` rather than
/// mutating request flags directly. The system writes path, status, velocity,
/// and bookkeeping fields.
struct NavMeshAgentComponent : world::ComponentTag {
  bool enabled = true;
  float speed = 3.0f;
  float stopping_distance = 0.15f;
  float height_offset = 0.0f;
  bool update_vertical_position = true;
  bool accept_partial_paths = true;
  math::Vec3 destination{};
  math::Vec3 search_extents{2.0f, 4.0f, 2.0f};
  math::Vec3 current_velocity{};
  world::Entity nav_mesh_entity{};
  navigation::NavQueryFilter query_filter{};
  navigation::NavStatus last_path_status = navigation::NavStatus::QueryFailed;
  NavMeshAgentStatus status = NavMeshAgentStatus::Idle;
  bool has_destination = false;
  bool path_requested = false;
  bool path_pending = false;
  bool path_resolved = false;
  bool current_path_partial = false;
  uint64_t path_request_id = 0;
  std::vector<math::Vec3> path;
  std::vector<uint8_t> path_point_flags;
  size_t next_waypoint = 0;
};

}  // namespace karma::components


#include <cstdint>


namespace karma::components {

/// \ingroup karma_components
/// Builds a Detour tile cache for a `NavMeshComponent` so obstacle entities can update tiles incrementally.
struct NavTileCacheComponent : world::ComponentTag {
  bool enabled = true;
  bool build_on_start = true;
  bool rebuild_requested = true;
  bool built = false;
  navigation::NavTileCacheBuildConfig build_config{};
  navigation::NavTileCacheBuildResult last_build_result{};
  navigation::NavTileCache tile_cache{};
  bool updates_pending = false;
};

/// \ingroup karma_components
/// Temporary navigation obstacle authored in ECS and synchronized into a `NavTileCacheComponent`.
struct NavTileCacheObstacleComponent : world::ComponentTag {
  bool enabled = true;
  world::Entity nav_mesh_entity{};
  navigation::NavTileCacheObstacleShape shape = navigation::NavTileCacheObstacleShape::Cylinder;
  math::Vec3 offset{};
  math::Vec3 half_extents{0.5f, 0.5f, 0.5f};
  math::Vec3 bounds_min{-0.5f, -0.5f, -0.5f};
  math::Vec3 bounds_max{0.5f, 0.5f, 0.5f};
  float radius = 0.5f;
  float height = 2.0f;
  float yaw_radians = 0.0f;
  uint64_t obstacle_ref = 0;
  world::Entity cached_nav_mesh_entity{};
  bool dirty = true;
  bool remove_requested = false;
};

}  // namespace karma::components


#include <cstdint>
#include <vector>


namespace karma::components {

/// \ingroup karma_components
/// Stable network entity id used on the wire.
using NetworkEntityId = uint64_t;
/// \ingroup karma_components
/// Stable network peer id as stored in ECS data contracts.
using NetworkPeerId = uint32_t;

inline constexpr NetworkEntityId kInvalidNetworkEntityId = 0;
inline constexpr NetworkPeerId kInvalidNetworkPeerId = 0;

/// \ingroup karma_components
/// Stable identity for an entity that participates in network replication.
struct NetworkIdentityComponent : world::ComponentTag {
  NetworkEntityId id = kInvalidNetworkEntityId;
};

/// \ingroup karma_components
/// Explicit authority mode for networked component state.
enum class AuthorityMode : uint8_t {
  Server,
  Owner,
  Client,
  Custom
};

/// \ingroup karma_components
/// Ownership and override contract for networked entities.
struct NetworkAuthorityComponent : world::ComponentTag {
  AuthorityMode mode = AuthorityMode::Server;
  NetworkPeerId owner_peer = kInvalidNetworkPeerId;
  bool server_can_override = true;
};

/// \ingroup karma_components
/// Per-component replication policy.
enum class ReplicationPolicy : uint8_t {
  Snapshot,
  Delta,
  OwnerInput
};

/// \ingroup karma_components
/// One replicated component entry with a stable wire component type id.
struct ReplicatedComponentEntry {
  uint32_t component_type = 0;
  ReplicationPolicy policy = ReplicationPolicy::Snapshot;
};

/// \ingroup karma_components
/// Metadata listing component types replicated for an entity.
struct NetworkReplicatedComponent : world::ComponentTag {
  std::vector<ReplicatedComponentEntry> components;
  bool visible_by_default = true;
};

}  // namespace karma::components


#include <cstdint>
#include <string>
#include <vector>


namespace karma::components {

/// \ingroup karma_components
/// Particle blend path selected by particle emitters.
enum class ParticleBlendMode : uint32_t {
  Additive = 0,
  Alpha = 1,
  Distortion = 2,
};

/// \ingroup karma_components
/// Particle orientation mode.
enum class ParticleAlignment : uint32_t {
  Billboard = 0,
  Ground = 1,
};

/// \ingroup karma_components
/// Particle shader family.
enum class ParticleShadingMode : uint32_t {
  Standard = 0,
  Shell = 1,
};

/// \ingroup karma_components
/// Particle source shape used by `ParticleEmitterComponent`.
enum class ParticleSourceShape : uint32_t {
  Box = 0,
  Sphere = 1,
  SphereSurface = 2,
  Disc = 3,
  Ring = 4,
  Cylinder = 5,
  Capsule = 6,
  Cone = 7,
  Line = 8,
  Path = 9,
  TrailPath = 10,
  MeshSurface = 11,
};

/// \ingroup karma_components
/// Particle source sampling policy.
enum class ParticleSourceSamplingMode : uint32_t {
  Random = 0,
  Sequential = 1,
  Vertices = 2,
};

/// \ingroup karma_components
/// Particle source emission distribution.
enum class ParticleSourceDistribution : uint32_t {
  Uniform = 0,
  Surface = 1,
  Edge = 2,
};

/// \ingroup karma_components
/// Runtime particle emitter template and state contract.
///
/// `.kpeffect` files deserialize into this component. `ParticleSystem` owns the
/// live particles and treats this component as authoring/playback input.
struct ParticleEmitterComponent : world::ComponentTag {
  bool enabled = true;
  bool playing = true;
  bool loop = true;
  bool emit_burst_on_start = true;
  bool local_space = false;
  uint32_t layer = 0;
  bool depth_test = true;
  ParticleBlendMode blend_mode = ParticleBlendMode::Additive;
  ParticleAlignment alignment = ParticleAlignment::Billboard;
  ParticleShadingMode shading_mode = ParticleShadingMode::Standard;
  bool use_soft_mask = true;
  float soft_particle_distance = 0.0f;
  float distortion_strength = 0.0f;
  float fresnel_power = 4.0f;
  float fresnel_strength = 1.0f;
  float refraction_strength = 0.0f;
  float interior_glow = 0.0f;
  std::string texture_key;
  uint32_t atlas_columns = 1;
  uint32_t atlas_rows = 1;
  uint32_t atlas_frame_count = 0;
  uint32_t atlas_frame_width = 0;
  uint32_t atlas_frame_height = 0;
  uint32_t atlas_border_x = 0;
  uint32_t atlas_border_y = 0;
  uint32_t atlas_spacing_x = 0;
  uint32_t atlas_spacing_y = 0;
  float animation_fps = 0.0f;
  bool animate_over_lifetime = false;
  bool random_start_frame = false;
  uint32_t max_particles = 256;
  uint32_t burst_count = 0;
  uint32_t seed = 0;
  float time_scale = 1.0f;
  float start_delay = 0.0f;
  float duration = 0.0f;
  float spawn_rate = 32.0f;
  float particle_lifetime_min = 0.65f;
  float particle_lifetime_max = 1.15f;
  float start_size_min = 0.18f;
  float start_size_max = 0.32f;
  float end_size_min = 0.03f;
  float end_size_max = 0.10f;
  float size_curve_exponent = 1.0f;
  float alpha_curve_exponent = 1.0f;
  float initial_rotation_min = 0.0f;
  float initial_rotation_max = 6.2831853f;
  float angular_velocity_min = -1.5f;
  float angular_velocity_max = 1.5f;
  ParticleSourceShape source_shape = ParticleSourceShape::Box;
  math::Vec3 source_box_extents{0.0f, 0.0f, 0.0f};
  math::Vec3 source_dimensions{0.0f, 0.0f, 0.0f};
  float source_radius_min = 0.0f;
  float source_radius_max = 0.0f;
  float source_inner_radius = 0.0f;
  float source_outer_radius = 0.0f;
  float source_height = 0.0f;
  float source_angle = 0.0f;
  std::vector<math::Vec3> source_path_points;
  bool source_closed_loop = false;
  ParticleSourceSamplingMode source_sampling = ParticleSourceSamplingMode::Random;
  float source_jitter_radius = 0.0f;
  std::string source_mesh_asset_key;
  ParticleSourceDistribution source_distribution = ParticleSourceDistribution::Uniform;
  float radial_speed_min = 0.0f;
  float radial_speed_max = 0.0f;
  math::Vec3 velocity_min{-0.6f, 2.5f, -0.6f};
  math::Vec3 velocity_max{0.6f, 4.5f, 0.6f};
  math::Vec3 acceleration{0.0f, -3.5f, 0.0f};
  float drag = 0.0f;
  math::Vec3 orbit_axis{0.0f, 1.0f, 0.0f};
  float orbit_speed = 0.0f;
  bool collide_with_ground = false;
  float ground_height = 0.0f;
  float bounce_damping = 0.35f;
  float collision_friction = 0.25f;
  float rest_speed_threshold = 0.35f;
  math::Color start_color{1.0f, 0.8f, 0.35f, 0.9f};
  math::Color end_color{1.0f, 0.15f, 0.05f, 0.0f};
};

/// \ingroup karma_components
/// Runtime textured particle beam/ribbon authored on prefabs.
///
/// Beam geometry is expanded by the renderer from local path points. Use
/// particle effects for secondary sparks, smoke, heat, and impacts.
struct ParticleBeamComponent : world::ComponentTag {
  bool enabled = true;
  bool visible = true;
  uint32_t layer = 0;
  bool depth_test = true;
  ParticleBlendMode blend_mode = ParticleBlendMode::Additive;
  std::string texture_key;
  std::vector<math::Vec3> local_path_points;
  float start_width = 0.2f;
  float end_width = 0.2f;
  math::Color start_color{1.0f, 1.0f, 1.0f, 1.0f};
  math::Color end_color{1.0f, 1.0f, 1.0f, 1.0f};
  float edge_softness = 0.0f;
  float uv_repeat = 1.0f;
  float uv_scroll_speed = 0.0f;
  float time_scale = 1.0f;
  uint32_t restart_count = 0u;
};

}  // namespace karma::components


#include <cstdint>
#include <string>


namespace karma::components {

/// \ingroup karma_components
/// Binds an entity's emitter to a named `assets::AssetRegistry` particle effect.
///
/// The particle system reapplies the effect when the asset registry version, override
/// hash, effect key, or restart counter changes.
struct ParticleEffectComponent : world::ComponentTag {
  std::string effect_key;
  bool auto_apply = true;
  bool preserve_enabled = true;
  bool preserve_playing = true;
  bool preserve_start_delay = false;
  uint32_t restart_count = 0;
  uint64_t applied_version = 0;
  uint64_t applied_override_hash = 0;
  uint32_t applied_restart_count = 0;
  std::string applied_effect_key;
};

}  // namespace karma::components


#include <optional>
#include <string>
#include <vector>


namespace karma::components {

/// \ingroup karma_components
/// Per-instance multipliers and replacements applied to a particle effect.
struct ParticleEffectOverrideComponent : world::ComponentTag {
  bool active = true;
  float time_scale = 1.0f;
  float spawn_rate_scale = 1.0f;
  float lifetime_scale = 1.0f;
  float size_scale = 1.0f;
  float radius_scale = 1.0f;
  float velocity_scale = 1.0f;
  float angular_velocity_scale = 1.0f;
  float alpha_scale = 1.0f;
  std::optional<math::Color> start_color;
  std::optional<math::Color> end_color;
  std::optional<std::string> texture_key;
  std::optional<ParticleSourceShape> source_shape;
  std::optional<math::Vec3> source_box_extents;
  std::optional<math::Vec3> source_dimensions;
  std::optional<float> source_radius_min;
  std::optional<float> source_radius_max;
  std::optional<float> source_inner_radius;
  std::optional<float> source_outer_radius;
  std::optional<float> source_height;
  std::optional<float> source_angle;
  std::optional<std::vector<math::Vec3>> source_path_points;
  std::optional<bool> source_closed_loop;
  std::optional<ParticleSourceSamplingMode> source_sampling;
  std::optional<float> source_jitter_radius;
  std::optional<std::string> source_mesh_asset_key;
  std::optional<ParticleSourceDistribution> source_distribution;
};

}  // namespace karma::components


#include <cstdint>


namespace karma::components {

/// \ingroup karma_components
/// Physics collision filtering consumed by `PhysicsSystem`.
///
/// `layers` are the collision categories this body belongs to. `collides_with`
/// is the set of categories this body accepts contacts from. A pair collides
/// only when both masks accept the other body's layers.
struct PhysicsCollisionFilterComponent : world::ComponentTag {
  uint32_t layers = 1u;
  uint32_t collides_with = 0xFFFFFFFFu;
};

}  // namespace karma::components


#include <array>
#include <cstdint>


namespace karma::components {

enum class PhysicsConstraintKind : uint8_t {
  Fixed,
  Point,
  Distance,
  Hinge,
  Slider,
  Cone,
  SwingTwist,
  SixDof,
};

enum class PhysicsConstraintFrameSpace : uint8_t {
  World,
  LocalToBodyCenterOfMass,
};

enum class PhysicsConstraintSpringMode : uint8_t {
  FrequencyAndDamping,
  StiffnessAndDamping,
};

struct PhysicsConstraintSpring {
  PhysicsConstraintSpringMode mode = PhysicsConstraintSpringMode::FrequencyAndDamping;
  float frequency_or_stiffness = 0.0f;
  float damping = 0.0f;
};

/// \ingroup karma_components
/// Two-body constraint authored on a constraint entity.
struct PhysicsConstraintComponent : world::ComponentTag {
  world::Entity body_a{};
  world::Entity body_b{};
  PhysicsConstraintKind kind = PhysicsConstraintKind::Fixed;
  PhysicsConstraintFrameSpace space = PhysicsConstraintFrameSpace::World;
  bool enabled = true;
  uint32_t priority = 0;
  uint32_t velocity_solver_steps = 0;
  uint32_t position_solver_steps = 0;
  float draw_size = 1.0f;
  uint64_t user_data = 0;

  bool auto_detect_point = false;
  math::Vec3 point1{};
  math::Vec3 point2{};
  math::Vec3 axis1{0.0f, 1.0f, 0.0f};
  math::Vec3 axis2{0.0f, 1.0f, 0.0f};
  math::Vec3 normal1{1.0f, 0.0f, 0.0f};
  math::Vec3 normal2{1.0f, 0.0f, 0.0f};
  math::Vec3 plane_axis1{0.0f, 1.0f, 0.0f};
  math::Vec3 plane_axis2{0.0f, 1.0f, 0.0f};

  float min_distance = -1.0f;
  float max_distance = -1.0f;
  float limits_min = -3.14159265358979323846f;
  float limits_max = 3.14159265358979323846f;
  float half_cone_angle = 0.0f;
  float normal_half_cone_angle = 0.0f;
  float plane_half_cone_angle = 0.0f;
  float twist_min_angle = 0.0f;
  float twist_max_angle = 0.0f;
  float max_friction_force = 0.0f;
  float max_friction_torque = 0.0f;
  PhysicsConstraintSpring limit_spring{};

  std::array<float, 6> six_dof_min_limits{{-3.402823466e+38F, -3.402823466e+38F,
                                           -3.402823466e+38F, -3.402823466e+38F,
                                           -3.402823466e+38F, -3.402823466e+38F}};
  std::array<float, 6> six_dof_max_limits{{3.402823466e+38F, 3.402823466e+38F,
                                           3.402823466e+38F, 3.402823466e+38F,
                                           3.402823466e+38F, 3.402823466e+38F}};
  std::array<float, 6> six_dof_max_friction{{0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f}};
};

}  // namespace karma::components



namespace karma::components {

/// \ingroup karma_components
/// Physics material authoring data consumed by `PhysicsSystem`.
struct PhysicsMaterialComponent : world::ComponentTag {
  float friction = 0.2f;
  float restitution = 0.0f;
};

}  // namespace karma::components


#include <cstdint>
#include <vector>


namespace karma::components {

enum class PhysicsSoftBodyPresetKind : uint8_t {
  Custom,
  Cloth,
  Cube,
  Sphere,
};

enum class PhysicsSoftBodyBendKind : uint8_t {
  None,
  Distance,
  Dihedral,
};

enum class PhysicsSoftBodyLraKind : uint8_t {
  None,
  EuclideanDistance,
  GeodesicDistance,
};

struct PhysicsSoftBodyVertex {
  math::Vec3 position{};
  math::Vec3 velocity{};
  float inverse_mass = 1.0f;
};

struct PhysicsSoftBodyFace {
  uint32_t vertex0 = 0;
  uint32_t vertex1 = 0;
  uint32_t vertex2 = 0;
  uint32_t material_index = 0;
};

struct PhysicsSoftBodyEdge {
  uint32_t vertex0 = 0;
  uint32_t vertex1 = 0;
  float compliance = 0.0f;
};

struct PhysicsSoftBodyVolume {
  uint32_t vertex0 = 0;
  uint32_t vertex1 = 0;
  uint32_t vertex2 = 0;
  uint32_t vertex3 = 0;
  float compliance = 0.0f;
};

struct PhysicsSoftBodyVertexAttributes {
  float compliance = 0.0f;
  float shear_compliance = 0.0f;
  float bend_compliance = 3.402823466e+38F;
  PhysicsSoftBodyLraKind lra_type = PhysicsSoftBodyLraKind::None;
  float lra_max_distance_multiplier = 1.0f;
};

/// \ingroup karma_components
/// Soft body authored through ECS.
struct PhysicsSoftBodyComponent : world::ComponentTag {
  bool enabled = true;
  bool recreate = false;
  PhysicsSoftBodyPresetKind preset = PhysicsSoftBodyPresetKind::Custom;
  uint64_t user_data = 0;

  std::vector<PhysicsSoftBodyVertex> vertices;
  std::vector<PhysicsSoftBodyFace> faces;
  std::vector<PhysicsSoftBodyEdge> edges;
  std::vector<PhysicsSoftBodyVolume> volumes;
  std::vector<uint32_t> pinned_vertices;

  uint32_t grid_size_x = 12;
  uint32_t grid_size_y = 12;
  uint32_t grid_size_z = 4;
  float grid_spacing = 0.5f;
  float radius = 1.0f;
  uint32_t sphere_theta = 16;
  uint32_t sphere_phi = 8;
  bool pin_cloth_corners = true;

  bool create_constraints = true;
  bool optimize = true;
  PhysicsSoftBodyBendKind bend_type = PhysicsSoftBodyBendKind::Distance;
  PhysicsSoftBodyVertexAttributes vertex_attributes{};
  float angle_tolerance = 0.13962634f;
  float vertex_radius = 0.0f;

  float friction = 0.2f;
  float restitution = 0.0f;
  uint32_t collision_layers = 1u;
  uint32_t collides_with = 0xFFFFFFFFu;
  uint32_t solver_iterations = 5;
  float linear_damping = 0.1f;
  float max_linear_velocity = 500.0f;
  float pressure = 0.0f;
  float gravity_factor = 1.0f;
  bool update_position = true;
  bool make_rotation_identity = true;
  bool allow_sleeping = true;
  bool activate = true;
};

}  // namespace karma::components


#include <array>
#include <cstdint>
#include <vector>


namespace karma::components {

enum class PhysicsVehicleControllerKind : uint8_t {
  Wheeled,
  Motorcycle,
  Tracked,
};

enum class PhysicsVehicleCollisionTesterKind : uint8_t {
  Ray,
  SphereCast,
  CylinderCast,
};

enum class PhysicsVehicleTransmissionKind : uint8_t {
  Automatic,
  Manual,
};

enum class PhysicsVehicleSpringKind : uint8_t {
  FrequencyAndDamping,
  StiffnessAndDamping,
};

struct PhysicsVehicleCurvePoint {
  float x = 0.0f;
  float y = 0.0f;
};

struct PhysicsVehicleSpring {
  PhysicsVehicleSpringKind mode = PhysicsVehicleSpringKind::FrequencyAndDamping;
  float frequency_or_stiffness = 0.0f;
  float damping = 0.0f;
};

struct PhysicsVehicleInputState {
  float forward = 0.0f;
  float right = 0.0f;
  float brake = 0.0f;
  float hand_brake = 0.0f;
  float left_ratio = 1.0f;
  float right_ratio = 1.0f;
  int current_gear = 0;
  float clutch_friction = 1.0f;
};

struct PhysicsVehicleEngine {
  float max_torque = 500.0f;
  float min_rpm = 1000.0f;
  float max_rpm = 6000.0f;
  float inertia = 0.5f;
  float angular_damping = 0.2f;
  std::vector<PhysicsVehicleCurvePoint> normalized_torque;
};

struct PhysicsVehicleTransmission {
  PhysicsVehicleTransmissionKind mode = PhysicsVehicleTransmissionKind::Automatic;
  std::vector<float> gear_ratios{2.66f, 1.78f, 1.3f, 1.0f, 0.74f};
  std::vector<float> reverse_gear_ratios{-2.9f};
  float switch_time = 0.5f;
  float clutch_release_time = 0.3f;
  float switch_latency = 0.5f;
  float shift_up_rpm = 4000.0f;
  float shift_down_rpm = 2000.0f;
  float clutch_strength = 10.0f;
};

struct PhysicsVehicleDifferential {
  int left_wheel = -1;
  int right_wheel = -1;
  float differential_ratio = 3.42f;
  float left_right_split = 0.5f;
  float limited_slip_ratio = 1.4f;
  float engine_torque_ratio = 1.0f;
};

struct PhysicsVehicleAntiRollBar {
  int left_wheel = 0;
  int right_wheel = 1;
  float stiffness = 1000.0f;
};

struct PhysicsVehicleTrack {
  uint32_t driven_wheel = 0;
  std::vector<uint32_t> wheels;
  float inertia = 10.0f;
  float angular_damping = 0.5f;
  float max_brake_torque = 15000.0f;
  float differential_ratio = 6.0f;
};

struct PhysicsVehicleWheel {
  math::Vec3 position{};
  math::Vec3 suspension_force_point{};
  math::Vec3 suspension_direction{0.0f, -1.0f, 0.0f};
  math::Vec3 steering_axis{0.0f, 1.0f, 0.0f};
  math::Vec3 wheel_up{0.0f, 1.0f, 0.0f};
  math::Vec3 wheel_forward{0.0f, 0.0f, 1.0f};
  float suspension_min_length = 0.3f;
  float suspension_max_length = 0.5f;
  float suspension_preload_length = 0.0f;
  PhysicsVehicleSpring suspension_spring{PhysicsVehicleSpringKind::FrequencyAndDamping, 1.5f, 0.5f};
  float radius = 0.3f;
  float width = 0.1f;
  bool enable_suspension_force_point = false;

  float inertia = 0.9f;
  float angular_damping = 0.2f;
  float max_steer_angle = 1.22173048f;
  std::vector<PhysicsVehicleCurvePoint> longitudinal_friction;
  std::vector<PhysicsVehicleCurvePoint> lateral_friction;
  float max_brake_torque = 1500.0f;
  float max_hand_brake_torque = 4000.0f;

  float tracked_longitudinal_friction = 4.0f;
  float tracked_lateral_friction = 2.0f;
};

struct PhysicsMotorcycleSettings {
  float max_lean_angle = 0.785398163f;
  float lean_spring_constant = 5000.0f;
  float lean_spring_damping = 1000.0f;
  float lean_spring_integration_coefficient = 0.0f;
  float lean_spring_integration_decay = 4.0f;
  float lean_smoothing_factor = 0.8f;
  bool enable_lean_controller = true;
  bool enable_lean_steering_limit = true;
};

/// \ingroup karma_components
/// Vehicle constraint authored on an ECS rigid body.
struct PhysicsVehicleComponent : world::ComponentTag {
  bool enabled = true;
  PhysicsVehicleControllerKind controller = PhysicsVehicleControllerKind::Wheeled;
  PhysicsVehicleCollisionTesterKind collision_tester = PhysicsVehicleCollisionTesterKind::Ray;
  math::Vec3 up{0.0f, 1.0f, 0.0f};
  math::Vec3 forward{0.0f, 0.0f, 1.0f};
  float max_pitch_roll_angle = 3.14159265358979323846f;
  float collision_test_sphere_radius = 0.3f;
  float collision_test_cylinder_convex_radius_fraction = 0.1f;
  float collision_test_max_slope_angle = 1.3962634f;
  uint32_t collision_test_layer = 1u;
  uint32_t num_steps_between_collision_test_active = 1;
  uint32_t num_steps_between_collision_test_inactive = 1;
  bool override_gravity = false;
  math::Vec3 gravity{0.0f, -9.8f, 0.0f};
  uint32_t priority = 0;
  uint32_t velocity_solver_steps = 0;
  uint32_t position_solver_steps = 0;
  float draw_size = 1.0f;
  uint64_t user_data = 0;

  std::vector<PhysicsVehicleWheel> wheels;
  std::vector<PhysicsVehicleAntiRollBar> anti_roll_bars;
  PhysicsVehicleEngine engine{};
  PhysicsVehicleTransmission transmission{};
  std::vector<PhysicsVehicleDifferential> differentials;
  float differential_limited_slip_ratio = 1.4f;
  PhysicsMotorcycleSettings motorcycle{};
  std::array<PhysicsVehicleTrack, 2> tracks{};
  PhysicsVehicleInputState input{};
};

}  // namespace karma::components


#include <cstdint>
#include <stdexcept>


namespace karma::components {

/// \ingroup karma_components
/// ECS-facing rigid body motion type.
enum class RigidbodyMotionType : uint8_t {
  Dynamic,
  Kinematic,
  Static,
};

/// \ingroup karma_components
/// Continuous collision detection mode.
enum class RigidbodyMotionQuality : uint8_t {
  Discrete,
  LinearCast,
};

/// \ingroup karma_components
/// Bit mask values for unlocked rigid body degrees of freedom.
enum RigidbodyDof : uint8_t {
  RigidbodyDofNone = 0,
  RigidbodyDofTranslationX = 1u << 0u,
  RigidbodyDofTranslationY = 1u << 1u,
  RigidbodyDofTranslationZ = 1u << 2u,
  RigidbodyDofRotationX = 1u << 3u,
  RigidbodyDofRotationY = 1u << 4u,
  RigidbodyDofRotationZ = 1u << 5u,
  RigidbodyDofAll = RigidbodyDofTranslationX | RigidbodyDofTranslationY | RigidbodyDofTranslationZ |
                    RigidbodyDofRotationX | RigidbodyDofRotationY | RigidbodyDofRotationZ,
  RigidbodyDofPlane2D = RigidbodyDofTranslationX | RigidbodyDofTranslationY | RigidbodyDofRotationZ,
};

/// \ingroup karma_components
/// Dynamic rigid-body authoring data consumed by `PhysicsSystem`.
class RigidbodyComponent : public world::ComponentTag {
 public:
  RigidbodyMotionType motion_type = RigidbodyMotionType::Dynamic;
  RigidbodyMotionQuality motion_quality = RigidbodyMotionQuality::Discrete;
  uint8_t allowed_dofs = RigidbodyDofAll;
  float mass = 1.0f;
  math::Vec3 velocity{};
  math::Vec3 angular_velocity{};
  bool is_kinematic = false;
  bool use_gravity = true;
  bool is_trigger = false;
  float gravity_factor = 1.0f;
  float linear_damping = 0.05f;
  float angular_damping = 0.05f;
  float max_linear_velocity = 500.0f;
  float max_angular_velocity = 47.1238898f;
  float inertia_multiplier = 1.0f;
  uint32_t velocity_solver_steps = 0;
  uint32_t position_solver_steps = 0;
  bool allow_sleeping = true;
  bool allow_dynamic_or_kinematic = false;
  bool collide_kinematic_vs_non_dynamic = false;
  bool use_manifold_reduction = true;
  bool apply_gyroscopic_force = false;
  bool enhanced_internal_edge_removal = false;

  static void Validate(world::World& world, world::Entity entity) {
    if (!world.has<TransformComponent>(entity)) {
      throw std::runtime_error("RigidbodyComponent requires TransformComponent on the same entity.");
    }
    if (!world.has<ColliderComponent>(entity)) {
      throw std::runtime_error("RigidbodyComponent requires ColliderComponent on the same entity.");
    }
  }
};

/// \ingroup karma_components
/// Per-step force and impulse commands consumed by `PhysicsSystem`.
struct PhysicsBodyForcesComponent : world::ComponentTag {
  math::Vec3 force{};
  math::Vec3 force_position{};
  bool force_at_position = false;
  math::Vec3 torque{};
  math::Vec3 impulse{};
  math::Vec3 impulse_position{};
  bool impulse_at_position = false;
  math::Vec3 angular_impulse{};
  bool clear_after_step = true;

  void clearTransient() {
    force = {};
    force_position = {};
    force_at_position = false;
    torque = {};
    impulse = {};
    impulse_position = {};
    impulse_at_position = false;
    angular_impulse = {};
  }
};

}  // namespace karma::components


#include <string>


namespace karma::components {

/// \ingroup karma_components
/// Script binding placeholder for higher-level scripting integrations.
struct ScriptComponent : world::ComponentTag {
  std::string script_key;
  bool enabled = true;
};

}  // namespace karma::components


#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>


namespace karma::components {

/// Terrain tile data source.
enum class TerrainSourceType : uint8_t {
  Procedural = 0,
  ImageTileDirectory = 1,
  SingleImage = 2,
};

/// Height/data map file encoding.
enum class TerrainHeightFormat : uint8_t {
  Auto = 0,
  ImageFile = 1,
  Raw16Unsigned = 2,
  R32Float = 3,
};

/// Standard terrain auxiliary data map role.
enum class TerrainDataMapKind : uint8_t {
  Custom = 0,
  Flow = 1,
  Wear = 2,
  Deposit = 3,
  Slope = 4,
  Curvature = 5,
};

/// Repeated terrain material layer controlled by a packed weight/splat map.
struct TerrainMaterialLayer {
  std::string name;
  /// Preferred shared material key resolved through assets::AssetRegistry.
  /// Explicit image paths below are used as a direct texture fallback.
  std::string material_key;
  std::filesystem::path albedo_image;
  std::filesystem::path normal_image;
  std::filesystem::path roughness_image;
  float uv_scale = 16.0f;
  bool enabled = true;

  friend bool operator==(const TerrainMaterialLayer& lhs,
                         const TerrainMaterialLayer& rhs) = default;
};

/// Optional auxiliary terrain data map exported by terrain authoring tools.
struct TerrainDataMapBinding {
  std::string name;
  TerrainDataMapKind kind = TerrainDataMapKind::Custom;
  std::filesystem::path image;
  std::string pattern;
  TerrainHeightFormat format = TerrainHeightFormat::Auto;
  uint32_t raw_width = 0u;
  uint32_t raw_height = 0u;
  uint32_t channel = 0u;
  bool enabled = true;

  friend bool operator==(const TerrainDataMapBinding& lhs,
                         const TerrainDataMapBinding& rhs) = default;
};

/// \ingroup karma_components
/// Streamed height-field terrain authoring data.
///
/// Terrain uses Karma's Y-up convention: tiles cover the XZ plane and height
/// displaces along Y. File-backed terrain expects decoded image tiles under
/// `tile_directory` using `{x}` and `{z}` placeholders in the filename patterns.
/// `{y}` is accepted as an alias for `{z}` for tiled terrain-tool exports.
/// Single-image terrain renders one fixed-size tile using `terrain_size`.
struct TerrainComponent : world::ComponentTag {
  TerrainSourceType source = TerrainSourceType::Procedural;
  std::filesystem::path tile_directory;
  std::string height_pattern = "height_{x}_{z}.png";
  std::string color_pattern = "color_{x}_{z}.png";
  std::string control_pattern = "control_{x}_{z}.png";
  std::filesystem::path height_image;
  std::filesystem::path heatmap_image;
  std::filesystem::path color_image;
  std::filesystem::path control_image;
  TerrainHeightFormat height_format = TerrainHeightFormat::Auto;
  uint32_t raw_width = 0u;
  uint32_t raw_height = 0u;
  bool raw_little_endian = true;
  bool flip_y = false;
  float height_value_min = 0.0f;
  float height_value_max = 1.0f;
  int32_t tile_index_base = 0;
  std::vector<TerrainMaterialLayer> material_layers;
  std::vector<TerrainDataMapBinding> data_maps;
  float terrain_size = 1000.0f;
  float tile_size = 1000.0f;
  uint32_t tile_resolution = 257u;
  int32_t origin_tile_x = 0;
  int32_t origin_tile_z = 0;
  float height_scale = 120.0f;
  float height_offset = 0.0f;
  float view_distance = 4000.0f;
  uint32_t base_patch_size = 16u;
  float tessellation_factor = 16.0f;
  float target_tessellated_edge_size = 12.0f;
  uint32_t layer = 0u;
  bool visible = true;
  bool cpu_fallback_enabled = true;
};

}  // namespace karma::components


#include <cstdint>


namespace karma::components {

/// \ingroup karma_components
/// Shared visibility and layer-mask component.
///
/// Render and collision systems both honor the relevant masks when extracting
/// scene data or processing queries.
struct VisibilityComponent : world::ComponentTag {
  bool visible = true;
  uint32_t render_layer_mask = 0xFFFFFFFFu;
  uint32_t collision_layer_mask = 0xFFFFFFFFu;
};

}  // namespace karma::components



namespace karma::components {

/// \ingroup karma_components
/// Analytic volumetric primitive shape consumed by `VolumetricComponent`.
enum class VolumetricShape {
  Sphere,
  Capsule,
};

/// \ingroup karma_components
/// Analytic volumetric solid visual effect consumed by `VolumeRuntimeModule`.
struct VolumetricComponent : world::ComponentTag {
  VolumetricShape shape = VolumetricShape::Sphere;
  math::Color color{0.18f, 0.82f, 1.0f, 1.0f};
  math::Color emissive_color{0.0f, 0.0f, 0.0f, 1.0f};
  float density = 0.34657359f;
  float center_opacity = 0.5f;
  float scattering = 1.0f;
  float anisotropy = 0.0f;
  float absorption = 0.0f;
  float distortion_strength = 0.0f;
  float noise_strength = 1.0f;
  float radius = 1.0f;
  float capsule_half_length = 1.0f;
  bool scale_with_transform = true;
  bool visible = true;
  float overlay_depth = 0.12f;
};

}  // namespace karma::components


#include <cstdint>
#include <optional>
#include <vector>


namespace karma::world::queries {

/// Result from point containment tests.
struct PointContainmentHit {
  world::Entity entity{};
  components::ColliderShapeType shape = components::ColliderShapeType::Box;
};

/// Filter for point containment queries.
struct PointContainmentFilter {
  bool only_triggers = false;
  uint32_t collision_layer_mask = 0xFFFFFFFFu;
};

/// Result from overlap tests.
struct OverlapHit {
  world::Entity entity{};
  components::ColliderShapeType shape = components::ColliderShapeType::Box;
};

/// Filter for overlap queries.
struct OverlapFilter {
  bool only_triggers = false;
  uint32_t collision_layer_mask = 0xFFFFFFFFu;
  bool skip_self = true;
};

/// Returns true when `world_point` lies inside `entity`'s collider.
bool containsPoint(const world::World& world, world::Entity entity, const math::Vec3& world_point);

/// Returns true when two entities' colliders overlap.
bool overlaps(const world::World& world, world::Entity a, world::Entity b);

/// Finds the first collider containing `world_point`.
std::optional<PointContainmentHit> findContainingCollider(
    const world::World& world,
    const math::Vec3& world_point,
    const PointContainmentFilter& filter = {});

/// Finds all colliders containing `world_point`.
std::vector<PointContainmentHit> findContainingColliders(
    const world::World& world,
    const math::Vec3& world_point,
    const PointContainmentFilter& filter = {});

/// Finds the first collider overlapping `query_entity`.
std::optional<OverlapHit> findOverlappingCollider(
    const world::World& world,
    world::Entity query_entity,
    const OverlapFilter& filter = {});

/// Finds all colliders overlapping `query_entity`.
std::vector<OverlapHit> findOverlappingColliders(
    const world::World& world,
    world::Entity query_entity,
    const OverlapFilter& filter = {});

}  // namespace karma::world::queries
