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

/// \ingroup karma_prefabs
/// Serialization hooks for one ECS component type.
struct ComponentSerializer {
  std::string type_name;
  std::function<bool(const ecs::World&, ecs::Entity)> has;
  std::function<nlohmann::json(const ecs::World&, ecs::Entity)> serialize;
  std::function<bool(ecs::World&, ecs::Entity, const nlohmann::json&)> deserialize;
};

/// \ingroup karma_prefabs
/// Registry mapping component type names to JSON serializers.
///
/// The current global registry is process-wide. Prefer scoped ownership if
/// future tools need multiple independent engine instances in one process.
class ComponentSerializerRegistry {
 public:
  /// Registers a serializer by type name.
  bool registerSerializer(ComponentSerializer serializer);
  /// Clears all registered serializers.
  void clear();

  /// Finds a serializer by component type name.
  const ComponentSerializer* find(std::string_view type_name) const;
  /// Registered serializers in insertion order.
  const std::vector<ComponentSerializer>& serializers() const { return serializers_; }

 private:
  std::vector<ComponentSerializer> serializers_;
  std::unordered_map<std::string, size_t> indices_;
};

/// Process-global component serializer registry.
ComponentSerializerRegistry& componentSerializerRegistry();
/// Registers built-in component serializers into `registry`.
void registerBuiltinComponentSerializers(ComponentSerializerRegistry& registry);
/// Ensures built-in serializers are present in the global registry.
void ensureBuiltinComponentSerializers();

}  // namespace karma::prefabs
