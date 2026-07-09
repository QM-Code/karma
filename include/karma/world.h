#pragma once

#include "karma/core.h"
#include "karma/math.h"

namespace karma::assets { class AssetRegistry; }
namespace karma::rendering { class GraphicsDevice; }
namespace karma::components { struct DeformableMeshComponent; struct VertexSkinInfluence; }



#include <type_traits>

namespace karma::world {

/// \ingroup karma_world_ecs
/// Marker base for ECS component types.
///
/// Components are intentionally plain data contracts. Systems own behavior and
/// transient state unless a component explicitly documents otherwise.
struct ComponentTag {};

/// True when `T` can be stored as a Karma component.
template <typename T>
constexpr bool isComponentV = std::is_base_of_v<ComponentTag, T> ||
                              std::is_trivially_copyable_v<T>;

}  // namespace karma::world



namespace karma::world {

/// \ingroup karma_world_ecs
/// ECS entity handle.
using Entity = core::EntityId;

}  // namespace karma::world


#include <cstdint>
#include <limits>
#include <utility>
#include <vector>


namespace karma::world {

/// \ingroup karma_world_ecs
/// Sparse-set storage for one component type.
///
/// Components are stored densely for iteration while a sparse entity-index
/// table provides O(1) lookup. Removing a component swaps in the last dense
/// entry, so callers must not keep indices into `denseEntities()` across
/// mutations.
template <typename T>
class ComponentStorage {
 public:
  /// Returns true when `entity` currently owns a `T` component.
  bool has(Entity entity) const {
    if (entity.index >= sparse_.size()) {
      return false;
    }
    const size_t dense_index = sparse_[entity.index];
    return dense_index != kInvalidIndex &&
           dense_index < dense_.size() &&
           dense_[dense_index] == entity;
  }

  /// Returns the mutable component for `entity`.
  ///
  /// The caller must ensure the component exists.
  T& get(Entity entity) { return components_[sparse_[entity.index]]; }

  /// Returns the component for `entity`.
  ///
  /// The caller must ensure the component exists.
  const T& get(Entity entity) const { return components_[sparse_[entity.index]]; }

  /// Adds or replaces the component for `entity`.
  void add(Entity entity, T component) {
    if (has(entity)) {
      components_[sparse_[entity.index]] = std::move(component);
      return;
    }
    ensureSparse(entity.index);
    dense_.push_back(entity);
    components_.push_back(std::move(component));
    sparse_[entity.index] = dense_.size() - 1;
  }

  /// Removes the component if it exists.
  void remove(Entity entity) {
    if (!has(entity)) {
      return;
    }
    const size_t dense_index = sparse_[entity.index];
    const size_t last_index = dense_.size() - 1;
    if (dense_index != last_index) {
      const Entity last_entity = dense_[last_index];
      dense_[dense_index] = last_entity;
      components_[dense_index] = std::move(components_[last_index]);
      sparse_[last_entity.index] = dense_index;
    }
    dense_.pop_back();
    components_.pop_back();
    sparse_[entity.index] = kInvalidIndex;
  }

  /// Dense entity order matching the internal component array.
  const std::vector<Entity>& denseEntities() const { return dense_; }

 private:
  static constexpr size_t kInvalidIndex = std::numeric_limits<size_t>::max();

  void ensureSparse(uint32_t index) {
    if (index >= sparse_.size()) {
      sparse_.resize(index + 1, kInvalidIndex);
    }
  }

  std::vector<Entity> dense_;
  std::vector<size_t> sparse_;
  std::vector<T> components_;
};

}  // namespace karma::world


#include <cstdint>
#include <vector>


namespace karma::world {

/// \ingroup karma_world_ecs
/// Owns entity allocation, liveness, generation bumps, and entity-versioning.
class EntityRegistry {
 public:
  /// Creates an entity, reusing a free slot when possible.
  Entity create() {
    if (!free_list_.empty()) {
      const uint32_t index = free_list_.back();
      free_list_.pop_back();
      Entity entity{index, generations_[index]};
      occupied_[index] = true;
      alive_positions_[index] = alive_.size();
      alive_.push_back(entity);
      ++version_;
      return entity;
    }
    const uint32_t index = static_cast<uint32_t>(generations_.size());
    generations_.push_back(0);
    occupied_.push_back(true);
    alive_positions_.push_back(alive_.size());
    Entity entity{index, 0};
    alive_.push_back(entity);
    ++version_;
    return entity;
  }

  /// Destroys an entity if it is still alive.
  void destroy(Entity entity) {
    if (!isAlive(entity)) {
      return;
    }
    const std::size_t alive_index = alive_positions_[entity.index];
    const std::size_t last_index = alive_.size() - 1;
    if (alive_index != last_index) {
      const Entity last_entity = alive_[last_index];
      alive_[alive_index] = last_entity;
      alive_positions_[last_entity.index] = alive_index;
    }
    alive_.pop_back();
    alive_positions_[entity.index] = kInvalidAliveIndex;
    occupied_[entity.index] = false;
    ++generations_[entity.index];
    free_list_.push_back(entity.index);
    ++version_;
  }

  /// Returns true if the handle identifies an occupied slot at its current generation.
  bool isAlive(Entity entity) const {
    return entity.index < generations_.size() &&
           occupied_[entity.index] &&
           generations_[entity.index] == entity.generation;
  }

  /// Live entities in dense registry order.
  const std::vector<Entity>& entities() const { return alive_; }
  /// Monotonically increments whenever entity liveness changes.
  uint64_t version() const { return version_; }

 private:
  static constexpr std::size_t kInvalidAliveIndex =
      std::numeric_limits<std::size_t>::max();

  std::vector<uint32_t> generations_;
  std::vector<bool> occupied_;
  std::vector<uint32_t> free_list_;
  std::vector<Entity> alive_;
  std::vector<std::size_t> alive_positions_;
  uint64_t version_ = 0;
};

}  // namespace karma::world


#include <string>


namespace karma::components {

/// \ingroup karma_components
/// Human-readable entity name used by examples, prefabs, and debug UI.
struct TagComponent : world::ComponentTag {
  std::string name;
};

}  // namespace karma::components


#include <cstdint>
#include <atomic>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>



namespace karma::world {

/// \ingroup karma_world_ecs
/// Primary ECS container for entities and typed component stores.
///
/// `World` owns entity lifetime and component data. It is deliberately small:
/// systems query and mutate components directly, while higher-level runtime
/// code decides update order. Component storage is allocated lazily per type.
class World {
 public:
  World() : instance_id_(allocateInstanceId()) {}
  World(const World&) = delete;
  World& operator=(const World&) = delete;

  /// Moves ECS state while preserving its stable world identity.
  ///
  /// The moved-from world remains usable with a newly allocated identity.
  World(World&& other) noexcept
      : registry_(std::move(other.registry_)),
        storages_(std::move(other.storages_)),
        instance_id_(std::exchange(other.instance_id_, allocateInstanceId())) {}

  /// Move assignment is disabled because replacing a live world's identity
  /// would invalidate external ownership records associated with it.
  World& operator=(World&&) = delete;

  /// Process-unique identity for the lifetime of this ECS state.
  ///
  /// The identity survives move construction and is never based on the
  /// `World` object's address. It is suitable for ownership/cache keys that
  /// also include an entity generation.
  uint64_t instanceId() const { return instance_id_; }

  /// Creates a live entity.
  Entity createEntity() { return registry_.create(); }

  /// Destroys an entity and removes all components stored for it.
  void destroyEntity(Entity entity) {
    if (!registry_.isAlive(entity)) {
      return;
    }
    for (auto& [id, storage] : storages_) {
      (void)id;
      storage->remove(entity);
    }
    registry_.destroy(entity);
  }

  /// Returns whether an entity handle is currently alive.
  bool isAlive(Entity entity) const { return registry_.isAlive(entity); }

  /// Dense list of live entities.
  const std::vector<Entity>& entities() const { return registry_.entities(); }
  /// Entity registry version, incremented on create/destroy.
  uint64_t entityVersion() const { return registry_.version(); }

  /// Adds or updates an entity name through `TagComponent`.
  void setName(Entity entity, std::string name) {
    if (has<components::TagComponent>(entity)) {
      auto& tag = get<components::TagComponent>(entity);
      tag.name = std::move(name);
    } else {
      add(entity, components::TagComponent{.name = std::move(name)});
    }
  }

  /// Adds or replaces component `T` for `entity`.
  ///
  /// If `T` exposes `static Validate(World&, Entity)`, validation runs before
  /// the component is inserted.
  template <typename T>
  void add(Entity entity, T component) {
    if (!entity.isValid() || !isAlive(entity)) {
      throw std::runtime_error("Cannot add component to a dead or invalid entity.");
    }
    if constexpr (HasValidate<T>::value) {
      T::Validate(*this, entity);
    }
    getStorage<T>().data.add(entity, std::move(component));
  }

  /// Returns true when `entity` has component `T`.
  template <typename T>
  bool has(Entity entity) const {
    return tryGet<T>(entity) != nullptr;
  }

  /// Returns mutable component `T`.
  ///
  /// Throws `std::out_of_range` when the entity is dead or does not own `T`.
  template <typename T>
  T& get(Entity entity) {
    T* component = tryGet<T>(entity);
    if (component == nullptr) {
      throw std::out_of_range("Entity does not own the requested component.");
    }
    return *component;
  }

  /// Returns component `T`.
  ///
  /// Throws `std::out_of_range` when the entity is dead or does not own `T`.
  template <typename T>
  const T& get(Entity entity) const {
    const T* component = tryGet<T>(entity);
    if (component == nullptr) {
      throw std::out_of_range("Entity does not own the requested component.");
    }
    return *component;
  }

  /// Returns mutable component `T`, or null when it is absent.
  template <typename T>
  T* tryGet(Entity entity) {
    if (!isAlive(entity)) {
      return nullptr;
    }
    Storage<T>* storage = findStorage<T>();
    if (storage == nullptr || !storage->data.has(entity)) {
      return nullptr;
    }
    return &storage->data.get(entity);
  }

  /// Returns component `T`, or null when it is absent.
  template <typename T>
  const T* tryGet(Entity entity) const {
    if (!isAlive(entity)) {
      return nullptr;
    }
    const Storage<T>* storage = findStorage<T>();
    if (storage == nullptr || !storage->data.has(entity)) {
      return nullptr;
    }
    return &storage->data.get(entity);
  }

  /// Removes component `T` if present.
  template <typename T>
  void remove(Entity entity) {
    if (Storage<T>* storage = findStorage<T>()) {
      storage->data.remove(entity);
    }
  }

  /// Returns storage for component `T`, creating it if needed.
  template <typename T>
  ComponentStorage<T>& storage() {
    return getStorage<T>().data;
  }

  /// Returns existing storage for component `T`, or an immutable empty storage.
  template <typename T>
  const ComponentStorage<T>& storage() const {
    if (const Storage<T>* storage = findStorage<T>()) {
      return storage->data;
    }
    static const ComponentStorage<T> empty;
    return empty;
  }

  /// Returns entities that have all requested component types.
  template <typename T0, typename... Ts>
  std::vector<Entity> view() const {
    std::vector<Entity> entities;
    const std::vector<Entity>* candidates = &storage<T0>().denseEntities();
    bool candidates_are_t0 = true;
    auto choose_smaller_storage = [&]<typename T>() {
      const auto& dense = storage<T>().denseEntities();
      if (dense.size() < candidates->size()) {
        candidates = &dense;
        candidates_are_t0 = false;
      }
    };
    (choose_smaller_storage.template operator()<Ts>(), ...);
    entities.reserve(candidates->size());
    for (const Entity entity : *candidates) {
      if (!isAlive(entity)) {
        continue;
      }
      if ((!candidates_are_t0 && !has<T0>(entity)) ||
          !(has<Ts>(entity) && ...)) {
        continue;
      }
      entities.push_back(entity);
    }
    return entities;
  }

  /// Invokes `fn(entity)` for entities with all requested component types.
  ///
  /// If the callback returns `bool`, iteration stops when it returns false.
  /// The callback must not add or remove any queried component type because
  /// structural mutation invalidates the selected dense storage iteration.
  template <typename T0, typename... Ts, typename Fn>
  void forEach(Fn&& fn) const {
    const std::vector<Entity>* candidates = &storage<T0>().denseEntities();
    bool candidates_are_t0 = true;
    auto choose_smaller_storage = [&]<typename T>() {
      const auto& dense = storage<T>().denseEntities();
      if (dense.size() < candidates->size()) {
        candidates = &dense;
        candidates_are_t0 = false;
      }
    };
    (choose_smaller_storage.template operator()<Ts>(), ...);
    for (const Entity entity : *candidates) {
      if (!isAlive(entity)) {
        continue;
      }
      if ((!candidates_are_t0 && !has<T0>(entity)) ||
          !(has<Ts>(entity) && ...)) {
        continue;
      }
      if constexpr (std::is_same_v<std::invoke_result_t<Fn, Entity>, bool>) {
        if (!std::invoke(fn, entity)) {
          break;
        }
      } else {
        std::invoke(fn, entity);
      }
    }
  }

 private:
  static uint64_t allocateInstanceId() {
    static std::atomic<uint64_t> next_id{1u};
    uint64_t id = next_id.fetch_add(1u, std::memory_order_relaxed);
    if (id == 0u) {
      id = next_id.fetch_add(1u, std::memory_order_relaxed);
    }
    return id;
  }

  template <typename T, typename = void>
  struct HasValidate : std::false_type {};

  template <typename T>
  struct HasValidate<T, std::void_t<decltype(T::Validate(std::declval<World&>(),
                                                        std::declval<Entity>()))>>
      : std::true_type {};

  struct IStorage {
    virtual ~IStorage() = default;
    virtual void remove(Entity entity) = 0;
  };

  template <typename T>
  struct Storage : IStorage {
    ComponentStorage<T> data;

    void remove(Entity entity) override { data.remove(entity); }
  };

  template <typename T>
  Storage<T>& getStorage() {
    const core::TypeId id = core::typeId<T>();
    auto it = storages_.find(id);
    if (it == storages_.end()) {
      auto storage = std::make_unique<Storage<T>>();
      auto* storage_ptr = storage.get();
      storages_[id] = std::move(storage);
      return *storage_ptr;
    }
    return *static_cast<Storage<T>*>(it->second.get());
  }

  template <typename T>
  Storage<T>* findStorage() {
    const core::TypeId id = core::typeId<T>();
    const auto it = storages_.find(id);
    return it == storages_.end() ? nullptr
                                 : static_cast<Storage<T>*>(it->second.get());
  }

  template <typename T>
  const Storage<T>* findStorage() const {
    const core::TypeId id = core::typeId<T>();
    const auto it = storages_.find(id);
    return it == storages_.end() ? nullptr
                                 : static_cast<const Storage<T>*>(it->second.get());
  }

  EntityRegistry registry_;
  std::unordered_map<core::TypeId, std::unique_ptr<IStorage>> storages_;
  uint64_t instance_id_ = 0u;
};

}  // namespace karma::world


#include <cstdint>
#include <string>
#include <vector>

#include <glm/glm.hpp>

namespace karma::world {

/// \ingroup karma_world
/// Index range for one draw subset of a mesh.
struct MeshSubmesh {
  uint32_t index_offset = 0;
  uint32_t index_count = 0;
  uint32_t material_slot = 0;
};

/// \ingroup karma_world
/// Material slot authored by a mesh asset.
struct MeshMaterialSlot {
  std::string name;
  std::string default_material_key;
};

/// \ingroup karma_world
/// Shared CPU-side mesh geometry used by importers, simulation, and rendering uploads.
struct MeshData {
  std::vector<glm::vec3> vertices;
  std::vector<glm::vec3> normals;
  std::vector<glm::vec2> uvs;
  std::vector<glm::vec2> uvs1;
  std::vector<glm::vec4> tangents;
  std::vector<glm::uvec4> joint_indices;
  std::vector<glm::vec4> joint_weights;
  std::vector<uint32_t> indices;

  /// Per-target morph delta payload.
  struct MorphTarget {
    std::vector<glm::vec3> position_deltas;
    std::vector<glm::vec3> normal_deltas;
    std::vector<glm::vec3> tangent_deltas;
  };
  std::vector<MorphTarget> morph_targets;

  /// Draw ranges and material-slot indices. Empty means the whole index buffer is one submesh.
  std::vector<MeshSubmesh> submeshes;
  /// Authored material slots and their default material asset keys.
  std::vector<MeshMaterialSlot> material_slots;
};

/// \ingroup karma_world
/// Sphere mesh generation settings.
struct SphereMeshDesc {
  float radius = 0.5f;
  uint32_t segments = 32u;
  uint32_t rings = 16u;
  std::string material_key;
};

/// \ingroup karma_world
/// Capsule mesh generation settings. The capsule is aligned to the Y axis.
struct CapsuleMeshDesc {
  float radius = 0.5f;
  float cylinder_height = 1.0f;
  uint32_t segments = 32u;
  uint32_t hemisphere_rings = 8u;
  std::string material_key;
};

/// Creates a centered XZ plane with upward normals.
MeshData createPlaneMesh(float width = 1.0f,
                         float depth = 1.0f,
                         std::string material_key = {});

/// Creates a centered box from half extents.
MeshData createBoxMesh(const math::Vec3& half_extents,
                       std::string material_key = {});

/// Creates a centered cube from full edge size.
MeshData createCubeMesh(float size = 1.0f, std::string material_key = {});

/// Creates a UV sphere aligned to the Y axis.
MeshData createSphereMesh(const SphereMeshDesc& desc = {});

/// Creates a UV sphere aligned to the Y axis.
MeshData createSphereMesh(float radius,
                          uint32_t segments = 32u,
                          uint32_t rings = 16u,
                          std::string material_key = {});

/// Creates a capsule aligned to the Y axis.
MeshData createCapsuleMesh(const CapsuleMeshDesc& desc = {});

/// Creates a capsule aligned to the Y axis.
MeshData createCapsuleMesh(float radius,
                           float cylinder_height,
                           uint32_t segments = 32u,
                           uint32_t hemisphere_rings = 8u,
                           std::string material_key = {});

}  // namespace karma::world


#include <cstdint>
#include <vector>


namespace karma::world {

/// \ingroup karma_scene
/// Index into `Scene`'s node array.
using NodeId = uint32_t;

/// \ingroup karma_scene
/// Scene hierarchy node optionally bound to an ECS entity.
struct Node {
  static constexpr NodeId kInvalidId = 0xFFFFFFFFu;

  NodeId id = kInvalidId;
  NodeId parent = kInvalidId;
  std::vector<NodeId> children;
  core::EntityId entity;
};

}  // namespace karma::world


#include <algorithm>
#include <unordered_map>
#include <vector>


namespace karma::world {

/// \ingroup karma_scene
/// Lightweight scene graph that maps ECS entities to parent/child nodes.
///
/// The scene graph owns hierarchy relationships only. Authored local transforms
/// and final world transforms live in `TransformComponent` and are composed by
/// `updateWorldTransforms(...)`.
class Scene {
 public:
  /// Creates a node, reusing an existing node for `entity` when one exists.
  NodeId createNode(core::EntityId entity = {}) {
    if (entity.isValid()) {
      const NodeId existing = findNode(entity);
      if (isAlive(existing)) {
        return existing;
      }
    }

    if (!free_list_.empty()) {
      const NodeId id = free_list_.back();
      free_list_.pop_back();
      nodes_[id] = Node{.id = id, .parent = Node::kInvalidId, .children = {}, .entity = entity};
      registerEntityNode(entity, id);
      return id;
    }
    const NodeId id = static_cast<NodeId>(nodes_.size());
    nodes_.push_back(Node{.id = id, .parent = Node::kInvalidId, .children = {}, .entity = entity});
    registerEntityNode(entity, id);
    return id;
  }

  /// Ensures there is a node for `entity`.
  NodeId ensureNode(core::EntityId entity) {
    return createNode(entity);
  }

  /// Finds the node bound to `entity`, or `Node::kInvalidId`.
  NodeId findNode(core::EntityId entity) const {
    if (!entity.isValid()) {
      return Node::kInvalidId;
    }
    const auto it = entity_to_node_.find(entityKey(entity));
    if (it == entity_to_node_.end() || !isAlive(it->second)) {
      return Node::kInvalidId;
    }
    return it->second;
  }

  /// Destroys a node and detaches its children.
  void destroyNode(NodeId id) {
    if (!isAlive(id)) {
      return;
    }
    unregisterEntityNode(nodes_[id].entity, id);
    detachFromParent(id);
    for (NodeId child : nodes_[id].children) {
      if (isAlive(child)) {
        nodes_[child].parent = Node::kInvalidId;
      }
    }
    nodes_[id].children.clear();
    nodes_[id].id = Node::kInvalidId;
    free_list_.push_back(id);
  }

  /// Reparents `child` under `new_parent`.
  ///
  /// Pass `Node::kInvalidId` to detach the child. Returns false for dead nodes,
  /// self-parenting, or relationships that would create a hierarchy cycle.
  bool reparent(NodeId child, NodeId new_parent) {
    if (!isAlive(child)) {
      return false;
    }
    if (new_parent != Node::kInvalidId && !isAlive(new_parent)) {
      return false;
    }
    if (child == new_parent) {
      return false;
    }

    NodeId ancestor = new_parent;
    std::size_t remaining = nodes_.size();
    while (isAlive(ancestor) && remaining-- > 0) {
      if (ancestor == child) {
        return false;
      }
      ancestor = nodes_[ancestor].parent;
    }
    if (isAlive(ancestor)) {
      return false;
    }
    if (nodes_[child].parent == new_parent) {
      return true;
    }

    detachFromParent(child);
    nodes_[child].parent = new_parent;
    if (isAlive(new_parent)) {
      nodes_[new_parent].children.push_back(child);
    }
    return true;
  }

  /// Returns true when a node id is alive.
  bool isAlive(NodeId id) const {
    return id < nodes_.size() && nodes_[id].id != Node::kInvalidId;
  }

  /// Returns mutable node data. Caller must ensure `id` is alive.
  Node& get(NodeId id) { return nodes_[id]; }

  /// Returns node data. Caller must ensure `id` is alive.
  const Node& get(NodeId id) const { return nodes_[id]; }

  /// Raw node array, including dead slots.
  const std::vector<Node>& nodes() const { return nodes_; }

 private:
  static uint64_t entityKey(core::EntityId entity) {
    return (static_cast<uint64_t>(entity.index) << 32) |
           static_cast<uint64_t>(entity.generation);
  }

  void registerEntityNode(core::EntityId entity, NodeId id) {
    if (!entity.isValid()) {
      return;
    }
    entity_to_node_[entityKey(entity)] = id;
  }

  void unregisterEntityNode(core::EntityId entity, NodeId id) {
    if (!entity.isValid()) {
      return;
    }
    const auto it = entity_to_node_.find(entityKey(entity));
    if (it != entity_to_node_.end() && it->second == id) {
      entity_to_node_.erase(it);
    }
  }

  void detachFromParent(NodeId id) {
    const NodeId parent = nodes_[id].parent;
    if (!isAlive(parent)) {
      nodes_[id].parent = Node::kInvalidId;
      return;
    }
    auto& siblings = nodes_[parent].children;
    siblings.erase(std::remove(siblings.begin(), siblings.end(), id), siblings.end());
    nodes_[id].parent = Node::kInvalidId;
  }

  std::vector<Node> nodes_;
  std::vector<NodeId> free_list_;
  std::unordered_map<uint64_t, NodeId> entity_to_node_;
};

}  // namespace karma::world



namespace karma::world {

/// \ingroup karma_scene
/// Composes local scene transforms into world-space `TransformComponent` values.
///
/// Entities without a scene node keep their authored world transform. Entities
/// with a scene node inherit their parent node transform when the scene
/// hierarchy is updated.
void updateWorldTransforms(world::World& world, const Scene& scene);

}  // namespace karma::world


#include <string_view>


namespace karma::world {

/// \ingroup karma_systems
/// Minimal ECS system interface used by `SystemGraph`.
class ISystem {
 public:
  virtual ~ISystem() = default;

  /// Human-readable system name used in diagnostics/debug UI.
  virtual std::string_view name() const = 0;
  /// Updates system-owned behavior for one frame or fixed step.
  virtual void update(world::World& world, float dt) = 0;
};

}  // namespace karma::world


#include <cstdint>
#include <memory>
#include <queue>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>


namespace karma::world {

/// \ingroup karma_systems
/// Opaque id assigned to systems registered in a `SystemGraph`.
using SystemId = uint32_t;
/// Sentinel returned or accepted when no system is present.
inline constexpr SystemId kInvalidSystemId = 0;

/// \ingroup karma_systems
/// Runtime-owned collection of ECS systems with dependency ordering.
///
/// Dependencies are topologically sorted before update. Invalid dependency ids,
/// duplicate edges, and dependency cycles are rejected when the edge is added.
class SystemGraph {
 public:
  /// Introspection record for debug UI and diagnostics.
  struct SystemInfo {
    SystemId id = 0;
    std::string name;
    std::vector<SystemId> depends_on;
  };

  /// Adds a system and returns its graph id.
  SystemId addSystem(std::unique_ptr<ISystem> system) {
    if (!system) {
      throw std::invalid_argument("Cannot add a null system to SystemGraph.");
    }
    const SystemId id = next_id_++;
    nodes_[id] = Node{.id = id, .system = std::move(system), .depends_on = {}};
    insertion_order_.push_back(id);
    order_dirty_ = true;
    return id;
  }

  /// Returns true when `system` is registered.
  bool contains(SystemId system) const {
    return nodes_.find(system) != nodes_.end();
  }

  /// Declares that `system` must run after `depends_on`.
  ///
  /// Returns false when either id is unknown, the edge already exists, or the
  /// edge would create a dependency cycle.
  bool addDependency(SystemId system, SystemId depends_on) {
    auto system_it = nodes_.find(system);
    if (system_it == nodes_.end() || !contains(depends_on) ||
        wouldCreateCycle(system, depends_on)) {
      return false;
    }
    auto& dependencies = system_it->second.depends_on;
    if (std::find(dependencies.begin(), dependencies.end(), depends_on) !=
        dependencies.end()) {
      return false;
    }
    dependencies.push_back(depends_on);
    order_dirty_ = true;
    return true;
  }

  /// Updates all registered systems in dependency order.
  void update(world::World& world, float dt) {
    if (order_dirty_) {
      cached_order_ = buildOrder();
      order_dirty_ = false;
    }
    const auto& order = cached_order_;
    for (SystemId id : order) {
      const auto it = nodes_.find(id);
      if (it != nodes_.end() && it->second.system) {
        it->second.system->update(world, dt);
      }
    }
  }

  /// Returns system metadata for tooling and debug UI.
  std::vector<SystemInfo> systems() const {
    std::vector<SystemInfo> out;
    out.reserve(nodes_.size());
    for (const SystemId id : insertion_order_) {
      const auto it = nodes_.find(id);
      if (it == nodes_.end()) {
        continue;
      }
      const Node& node = it->second;
      SystemInfo info{};
      info.id = id;
      if (node.system) {
        info.name = std::string(node.system->name());
      }
      info.depends_on = node.depends_on;
      out.push_back(std::move(info));
    }
    return out;
  }

  /// Finds the first registered system with runtime type `T`.
  template <typename T>
  T* findSystem() {
    for (const SystemId id : insertion_order_) {
      auto it = nodes_.find(id);
      if (it != nodes_.end()) {
        if (auto* system = dynamic_cast<T*>(it->second.system.get())) {
          return system;
        }
      }
    }
    return nullptr;
  }

  /// Finds the first registered system with runtime type `T`.
  template <typename T>
  const T* findSystem() const {
    for (const SystemId id : insertion_order_) {
      const auto it = nodes_.find(id);
      if (it != nodes_.end()) {
        if (const auto* system = dynamic_cast<const T*>(it->second.system.get())) {
          return system;
        }
      }
    }
    return nullptr;
  }

 private:
  struct Node {
    SystemId id = 0;
    std::unique_ptr<ISystem> system;
    std::vector<SystemId> depends_on;
  };

  bool wouldCreateCycle(SystemId system, SystemId depends_on) const {
    if (system == depends_on) {
      return true;
    }

    std::vector<SystemId> pending{depends_on};
    std::unordered_set<SystemId> visited;
    while (!pending.empty()) {
      const SystemId current = pending.back();
      pending.pop_back();
      if (!visited.insert(current).second) {
        continue;
      }
      if (current == system) {
        return true;
      }
      const auto it = nodes_.find(current);
      if (it == nodes_.end()) {
        continue;
      }
      pending.insert(pending.end(),
                     it->second.depends_on.begin(),
                     it->second.depends_on.end());
    }
    return false;
  }

  std::vector<SystemId> buildOrder() const {
    std::unordered_map<SystemId, uint32_t> indegree;
    std::unordered_map<SystemId, std::vector<SystemId>> dependents;
    for (const SystemId id : insertion_order_) {
      indegree[id] = 0;
    }
    for (const SystemId id : insertion_order_) {
      const auto node_it = nodes_.find(id);
      if (node_it == nodes_.end()) {
        continue;
      }
      const Node& node = node_it->second;
      for (SystemId dep : node.depends_on) {
        if (contains(dep)) {
          indegree[id]++;
          dependents[dep].push_back(id);
        }
      }
    }

    std::queue<SystemId> ready;
    for (const SystemId id : insertion_order_) {
      if (indegree[id] == 0) {
        ready.push(id);
      }
    }

    std::vector<SystemId> order;
    order.reserve(nodes_.size());
    while (!ready.empty()) {
      const SystemId id = ready.front();
      ready.pop();
      order.push_back(id);
      const auto dependents_it = dependents.find(id);
      if (dependents_it != dependents.end()) {
        for (SystemId dependent : dependents_it->second) {
          if (--indegree[dependent] == 0) {
            ready.push(dependent);
          }
        }
      }
    }

    if (order.size() != nodes_.size()) {
      return insertion_order_;
    }
    return order;
  }

  SystemId next_id_ = 1;
  std::unordered_map<SystemId, Node> nodes_;
  std::vector<SystemId> insertion_order_;
  mutable std::vector<SystemId> cached_order_;
  mutable bool order_dirty_ = true;
};

}  // namespace karma::world


#include <cstdint>
#include <functional>
#include <limits>
#include <optional>
#include <string>
#include <vector>


#include <glm/glm.hpp>

namespace karma::world {

/// \ingroup karma_animation
/// Sentinel index used for absent animation nodes, clips, skins, and joints.
constexpr uint32_t kInvalidAnimationIndex = std::numeric_limits<uint32_t>::max();

/// \ingroup karma_animation
/// glTF-compatible keyframe interpolation mode.
enum class InterpolationMode : uint8_t {
  Step,
  Linear,
  CubicSpline,
};

/// \ingroup karma_animation
/// Animation target channel path.
enum class AnimationTargetPath : uint8_t {
  Translation,
  Rotation,
  Scale,
  MorphWeights,
};

/// \ingroup karma_animation
/// Vec3 animation keyframe, including optional cubic-spline tangents.
struct Vec3Keyframe {
  float time_seconds = 0.0f;
  math::Vec3 value{};
  math::Vec3 in_tangent{};
  math::Vec3 out_tangent{};
};

/// \ingroup karma_animation
/// Quaternion animation keyframe, including optional cubic-spline tangents.
struct QuatKeyframe {
  float time_seconds = 0.0f;
  math::Quat value{};
  math::Quat in_tangent{};
  math::Quat out_tangent{};
};

/// \ingroup karma_animation
/// Morph target weight keyframe.
struct MorphWeightKeyframe {
  float time_seconds = 0.0f;
  std::vector<float> values;
  std::vector<float> in_tangents;
  std::vector<float> out_tangents;
};

/// \ingroup karma_animation
/// Generic target description for imported animation data.
struct AnimationTrack {
  uint32_t target_node_index = kInvalidAnimationIndex;
  uint32_t target_skin_index = kInvalidAnimationIndex;
  uint32_t target_joint_index = kInvalidAnimationIndex;
  uint32_t target_morph_target_index = kInvalidAnimationIndex;
  AnimationTargetPath path = AnimationTargetPath::Translation;
  InterpolationMode interpolation = InterpolationMode::Linear;
};

/// \ingroup karma_animation
/// Node transform animation channel.
struct AnimationChannel {
  uint32_t target_node_index = 0;
  uint32_t target_skin_index = kInvalidAnimationIndex;
  uint32_t target_joint_index = kInvalidAnimationIndex;
  InterpolationMode position_interpolation = InterpolationMode::Linear;
  InterpolationMode rotation_interpolation = InterpolationMode::Linear;
  InterpolationMode scale_interpolation = InterpolationMode::Linear;
  std::vector<Vec3Keyframe> position_keys;
  std::vector<QuatKeyframe> rotation_keys;
  std::vector<Vec3Keyframe> scale_keys;
};

/// \ingroup karma_animation
/// Morph weight animation track.
struct MorphTargetTrack {
  uint32_t target_node_index = kInvalidAnimationIndex;
  uint32_t target_mesh_index = kInvalidAnimationIndex;
  InterpolationMode interpolation = InterpolationMode::Linear;
  std::vector<MorphWeightKeyframe> weight_keys;
};

/// \ingroup karma_animation
/// Authored event emitted while sampling a clip.
struct AnimationEvent {
  std::string name;
  float time_seconds = 0.0f;
  std::string payload;
};

/// \ingroup karma_animation
/// Optional root-motion track extracted from animation data.
struct RootMotionTrack {
  uint32_t target_node_index = kInvalidAnimationIndex;
  InterpolationMode position_interpolation = InterpolationMode::Linear;
  InterpolationMode rotation_interpolation = InterpolationMode::Linear;
  std::vector<Vec3Keyframe> position_keys;
  std::vector<QuatKeyframe> rotation_keys;
};

/// \ingroup karma_animation
/// One skeleton joint mapped back to an imported scene node.
struct Joint {
  std::string name;
  uint32_t parent_joint_index = kInvalidAnimationIndex;
  uint32_t node_index = kInvalidAnimationIndex;
  glm::mat4 inverse_bind_matrix{1.0f};
};

/// \ingroup karma_animation
/// Imported skeleton topology.
struct Skeleton {
  std::string name;
  std::vector<Joint> joints;
  std::vector<uint32_t> root_joint_indices;
};

/// \ingroup karma_animation
/// Imported skin data used by skinned meshes.
struct Skin {
  std::string name;
  uint32_t skeleton_index = kInvalidAnimationIndex;
  std::vector<uint32_t> joint_node_indices;
  std::vector<glm::mat4> inverse_bind_matrices;
};

/// \ingroup karma_animation
/// Imported animation clip.
struct AnimationClip {
  std::string name;
  float duration_seconds = 0.0f;
  float ticks_per_second = 1.0f;
  uint32_t source_index = kInvalidAnimationIndex;
  std::vector<AnimationChannel> channels;
  std::vector<MorphTargetTrack> morph_target_tracks;
  std::vector<AnimationEvent> events;
  std::optional<RootMotionTrack> root_motion;
};

/// \ingroup karma_animation
/// Optional transform sample for one target node.
struct SampledTransform {
  std::optional<math::Vec3> position;
  std::optional<math::Quat> rotation;
  std::optional<math::Vec3> scale;
};

/// Normalizes `time_seconds` into clip duration, respecting loop mode.
float normalizeAnimationTime(const AnimationClip& clip, float time_seconds, bool loop);
/// Samples Vec3 keyframes at a time.
std::optional<math::Vec3> sampleVec3Keyframes(const std::vector<Vec3Keyframe>& keys,
                                              float time_seconds,
                                              InterpolationMode interpolation =
                                                  InterpolationMode::Linear);
/// Samples quaternion keyframes at a time.
std::optional<math::Quat> sampleQuatKeyframes(const std::vector<QuatKeyframe>& keys,
                                              float time_seconds,
                                              InterpolationMode interpolation =
                                                  InterpolationMode::Linear);
/// Samples morph target weights at a time.
std::optional<std::vector<float>> sampleMorphWeightKeyframes(
    const std::vector<MorphWeightKeyframe>& keys,
    float time_seconds,
    InterpolationMode interpolation = InterpolationMode::Linear);
/// Samples a clip and invokes `on_sample` for each transform target node.
void sampleAnimationClip(
    const AnimationClip& clip,
    float time_seconds,
    bool loop,
    const std::function<void(uint32_t target_node_index, const SampledTransform& transform)>&
        on_sample);
/// Samples a clip and invokes callbacks for transform and morph-weight targets.
///
/// `on_sample` receives node transform channels. `on_morph_weights` receives
/// glTF `weights` channels keyed by the target node index.
void sampleAnimationClip(
    const AnimationClip& clip,
    float time_seconds,
    bool loop,
    const std::function<void(uint32_t target_node_index, const SampledTransform& transform)>&
        on_sample,
    const std::function<void(uint32_t target_node_index, const std::vector<float>& weights)>&
        on_morph_weights);

}  // namespace karma::world


#include <cstdint>
#include <string>
#include <vector>

#include <glm/glm.hpp>


namespace karma::world {

/// \ingroup karma_animation
/// Transform sample with per-channel presence flags.
struct PoseTransform {
  math::Vec3 position{};
  math::Quat rotation{};
  math::Vec3 scale{1.0f, 1.0f, 1.0f};
  bool has_position = false;
  bool has_rotation = false;
  bool has_scale = false;
};

/// \ingroup karma_animation
/// Local pose indexed by imported node index.
struct LocalPose {
  std::vector<PoseTransform> nodes;
};

/// \ingroup karma_animation
/// Parent links and rest pose used for model-pose composition.
struct PoseHierarchy {
  std::vector<uint32_t> parent_indices;
  std::vector<PoseTransform> rest_local_transforms;
};

/// \ingroup karma_animation
/// Model-space matrix palette indexed by imported node index.
struct ModelPose {
  std::vector<glm::mat4> node_matrices;
};

/// \ingroup karma_animation
/// Final joint matrix palette for one skin.
struct SkinningPalette {
  uint32_t skin_index = kInvalidAnimationIndex;
  std::vector<glm::mat4> joint_matrices;
  bool valid = false;
  std::string diagnostic;
};

/// Converts a pose transform to a matrix.
glm::mat4 poseTransformToMatrix(const PoseTransform& transform);
/// Converts an optional sampled transform to a pose transform.
PoseTransform poseTransformFromSample(const SampledTransform& sample);

/// Creates a local pose from rest transforms.
LocalPose makeRestLocalPose(const PoseHierarchy& hierarchy);
/// Applies one sampled node transform to a local pose.
void applySampleToLocalPose(LocalPose& pose,
                            uint32_t target_node_index,
                            const SampledTransform& sample);
/// Composes local pose transforms through the hierarchy.
ModelPose composeModelPose(const PoseHierarchy& hierarchy, const LocalPose& local_pose);

/// Builds final skinning matrices from node model matrices and inverse binds.
SkinningPalette buildSkinningPalette(const std::vector<uint32_t>& joint_node_indices,
                                     const std::vector<glm::mat4>& inverse_bind_matrices,
                                     const std::vector<glm::mat4>& node_model_matrices,
                                     const glm::mat4& render_space_world,
                                     uint32_t skin_index = kInvalidAnimationIndex);

}  // namespace karma::world


#include <cstdint>
#include <string>
#include <vector>

#include <glm/mat4x4.hpp>


namespace karma::world {

/// \ingroup karma_animation
/// One explicit source-joint to target-joint retarget mapping.
struct SkeletonMapEntry {
  uint32_t source_joint_index = kInvalidAnimationIndex;
  uint32_t target_joint_index = kInvalidAnimationIndex;
  glm::mat4 rest_pose_correction{1.0f};
};

/// \ingroup karma_animation
/// Explicit skeleton mapping used for clip retargeting.
struct SkeletonMap {
  std::vector<SkeletonMapEntry> joints;
  uint32_t source_root_joint_index = kInvalidAnimationIndex;
  uint32_t target_root_joint_index = kInvalidAnimationIndex;
};

/// \ingroup karma_animation
/// Root translation scale policy used while retargeting clips.
enum class RetargetRootScalePolicy : uint8_t {
  None,
  ExplicitScale,
};

/// \ingroup karma_animation
/// Retargeting options for explicit skeleton maps.
struct RetargetOptions {
  RetargetRootScalePolicy root_scale_policy = RetargetRootScalePolicy::None;
  float root_translation_scale = 1.0f;
  bool copy_unmapped_channels = false;
};

/// Validates that all mapped joints exist on their source and target skeletons.
bool validateSkeletonMap(const Skeleton& source,
                         const Skeleton& target,
                         const SkeletonMap& map,
                         std::string* diagnostic = nullptr);

/// Retargets a clip from `source_skeleton` to `target_skeleton`.
AnimationClip retargetClip(const AnimationClip& source_clip,
                           const Skeleton& source_skeleton,
                           const Skeleton& target_skeleton,
                           const SkeletonMap& map,
                           const RetargetOptions& options = {});

}  // namespace karma::world


#include <string_view>


namespace karma::world {

/// \ingroup karma_animation
/// Samples animation components and writes local transforms.
///
/// The system consumes `AnimatorComponent`.
/// Scene hierarchy composition later writes final world transforms.
class AnimationSystem {
 public:
  std::string_view name() const { return "AnimationSystem"; }
  void update(world::World& world, world::Scene& scene, float dt);
};

}  // namespace karma::world


#include <vector>

#include <glm/glm.hpp>


namespace karma::world {
class Scene;
}

namespace karma::assets {
class AssetRegistry;
}  // namespace karma::assets

namespace karma::world {

/// \ingroup karma_animation
/// Skins `bind_mesh` on the CPU using final skin matrices.
world::MeshData skinMesh(const world::MeshData& bind_mesh,
                            const std::vector<components::VertexSkinInfluence>& influences,
                            const std::vector<glm::mat4>& skin_matrices);
/// Applies morph target weights to `bind_mesh` on the CPU.
///
/// Position, normal, and tangent deltas are accumulated from
/// `MeshData::morph_targets`; normals and tangents are renormalized.
world::MeshData morphMesh(const world::MeshData& bind_mesh,
                             const std::vector<float>& weights);

/// Builds a skinning palette from ECS world transforms.
SkinningPalette buildSkinningPaletteFromWorld(
    const components::DeformableMeshComponent& deformation,
    const world::World& world,
    const glm::mat4& mesh_world);

/// Builds a skinning palette from scene hierarchy and world transforms.
SkinningPalette buildSkinningPaletteFromScene(
    const components::DeformableMeshComponent& deformation,
    const world::World& world,
    const world::Scene& scene,
    const glm::mat4& mesh_world);

/// \ingroup karma_animation
/// Updates renderer-owned deformation resources for skinned/morphed meshes.
///
/// GPU mode updates joint palettes and morph weights without rewriting mesh
/// vertex buffers. CPU reference mode remains available for validation and
/// diagnostics.
class DeformationSystem {
 public:
  void update(world::World& world,
              const world::Scene& scene,
              rendering::GraphicsDevice& device,
              const assets::AssetRegistry* assets = nullptr);
};

}  // namespace karma::world
