#pragma once

#include <memory>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

#include "karma/core/type_id.h"
#include "karma/ecs/component_storage.h"
#include "karma/ecs/entity_registry.h"

#include "karma/components/rigidbody.h"
#include "karma/components/transform.h"

namespace karma::ecs {

class World {
 public:
  Entity createEntity() { return registry_.create(); }

  void destroyEntity(Entity entity) { registry_.destroy(entity); }

  bool isAlive(Entity entity) const { return registry_.isAlive(entity); }

  template <typename T>
  void add(Entity entity, T component) {
    if constexpr (HasValidate<T>::value) {
      T::Validate(*this, entity);
    }
    if constexpr (std::is_same_v<T, components::TransformComponent>) {
      if (has<components::RigidbodyComponent>(entity)) {
        const auto& body = get<components::RigidbodyComponent>(entity);
        component.setHasPhysics(true);
        component.setPhysicsWriteWarning(!body.is_kinematic);
      }
    }
    getStorage<T>().data.add(entity, std::move(component));
    if constexpr (std::is_same_v<T, components::RigidbodyComponent>) {
      if (has<components::TransformComponent>(entity)) {
        auto& transform = get<components::TransformComponent>(entity);
        transform.setHasPhysics(true);
        transform.setPhysicsWriteWarning(!component.is_kinematic);
      }
    }
  }

  template <typename T>
  bool has(Entity entity) const {
    return getStorage<T>().data.has(entity);
  }

  template <typename T>
  T& get(Entity entity) {
    return getStorage<T>().data.get(entity);
  }

  template <typename T>
  const T& get(Entity entity) const {
    return getStorage<T>().data.get(entity);
  }

  template <typename T>
  void remove(Entity entity) {
    getStorage<T>().data.remove(entity);
    if constexpr (std::is_same_v<T, components::RigidbodyComponent>) {
      if (has<components::TransformComponent>(entity)) {
        auto& transform = get<components::TransformComponent>(entity);
        transform.setHasPhysics(false);
        transform.setPhysicsWriteWarning(true);
      }
    }
  }

  template <typename T>
  ComponentStorage<T>& storage() {
    return getStorage<T>().data;
  }

  template <typename T>
  const ComponentStorage<T>& storage() const {
    return getStorage<T>().data;
  }

  template <typename... Ts>
  std::vector<Entity> view() const {
    std::vector<Entity> entities;
    const auto& base = storage<std::tuple_element_t<0, std::tuple<Ts...>>>();
    for (const Entity entity : base.denseEntities()) {
      if (!isAlive(entity)) {
        continue;
      }
      if ((has<Ts>(entity) && ...)) {
        entities.push_back(entity);
      }
    }
    return entities;
  }

 private:
  template <typename T, typename = void>
  struct HasValidate : std::false_type {};

  template <typename T>
  struct HasValidate<T, std::void_t<decltype(T::Validate(std::declval<World&>(),
                                                        std::declval<Entity>()))>>
      : std::true_type {};

  struct IStorage {
    virtual ~IStorage() = default;
  };

  template <typename T>
  struct Storage : IStorage {
    ComponentStorage<T> data;
  };

  template <typename T>
  Storage<T>& getStorage() const {
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

  EntityRegistry registry_;
  mutable std::unordered_map<core::TypeId, std::unique_ptr<IStorage>> storages_;
};

}  // namespace karma::ecs
