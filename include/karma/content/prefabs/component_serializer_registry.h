#pragma once

#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>

#include "karma/world/ecs/entity.h"
#include "karma/world/ecs/world.h"

namespace karma::prefabs {

struct ComponentSerializer {
  std::string type_name;
  std::function<bool(const ecs::World&, ecs::Entity)> has;
  std::function<nlohmann::json(const ecs::World&, ecs::Entity)> serialize;
  std::function<bool(ecs::World&, ecs::Entity, const nlohmann::json&)> deserialize;
};

class ComponentSerializerRegistry {
 public:
  bool registerSerializer(ComponentSerializer serializer);
  void clear();

  const ComponentSerializer* find(std::string_view type_name) const;
  const std::vector<ComponentSerializer>& serializers() const { return serializers_; }

 private:
  std::vector<ComponentSerializer> serializers_;
  std::unordered_map<std::string, size_t> indices_;
};

ComponentSerializerRegistry& componentSerializerRegistry();
void registerBuiltinComponentSerializers(ComponentSerializerRegistry& registry);
void ensureBuiltinComponentSerializers();

}  // namespace karma::prefabs
