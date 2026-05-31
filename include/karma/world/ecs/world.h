#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

#include "karma/core/type_id.h"
#include "karma/world/ecs/component_storage.h"
#include "karma/world/ecs/entity_registry.h"

#include "karma/world/components/rigidbody.h"
#include "karma/world/components/transform.h"
#include "karma/world/components/tag.h"

namespace karma::ecs {

class World {
 public:
  Entity createEntity() { return registry_.create(); }

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

  bool isAlive(Entity entity) const { return registry_.isAlive(entity); }

  const std::vector<Entity>& entities() const { return registry_.entities(); }
  uint64_t entityVersion() const { return registry_.version(); }

  void setName(Entity entity, std::string name) {
    if (has<components::TagComponent>(entity)) {
      auto& tag = get<components::TagComponent>(entity);
      tag.name = std::move(name);
    } else {
      add(entity, components::TagComponent{.name = std::move(name)});
    }
  }

  template <typename T>
  void add(Entity entity, T component) {
    if constexpr (HasValidate<T>::value) {
      T::Validate(*this, entity);
    }
    getStorage<T>().data.add(entity, std::move(component));
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
  }

  template <typename T>
  ComponentStorage<T>& storage() {
    return getStorage<T>().data;
  }

  template <typename T>
  const ComponentStorage<T>& storage() const {
    return getStorage<T>().data;
  }

  template <typename T0, typename... Ts>
  std::vector<Entity> view() const {
    std::vector<Entity> entities;
    const auto& base = storage<T0>();
    for (const Entity entity : base.denseEntities()) {
      if (!isAlive(entity)) {
        continue;
      }
      if (!(has<Ts>(entity) && ...)) {
        continue;
      }
      entities.push_back(entity);
    }
    return entities;
  }

  template <typename T0, typename... Ts, typename Fn>
  void forEach(Fn&& fn) const {
    const auto& base = storage<T0>();
    for (const Entity entity : base.denseEntities()) {
      if (!isAlive(entity)) {
        continue;
      }
      if (!(has<Ts>(entity) && ...)) {
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
