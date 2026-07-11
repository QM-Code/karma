#pragma once

#include <cctype>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "karma/prefabs.h"

namespace karma::prefabs::component_serializer_detail {

using Json = nlohmann::json;

inline bool isPortableRelativePath(std::string_view value) {
  if (value.empty() || value.find('\\') != std::string_view::npos ||
      value.find('\0') != std::string_view::npos || value.front() == '/') {
    return false;
  }
  if (value.size() >= 2u &&
      std::isalpha(static_cast<unsigned char>(value[0])) != 0 &&
      value[1] == ':') {
    return false;
  }

  std::size_t start = 0u;
  while (start <= value.size()) {
    const std::size_t end = value.find('/', start);
    const std::string_view segment =
        end == std::string_view::npos ? value.substr(start)
                                      : value.substr(start, end - start);
    if (segment == "..") {
      return false;
    }
    if (end == std::string_view::npos) {
      break;
    }
    start = end + 1u;
  }
  return true;
}

inline Json toJson(const math::Vec3& value) {
  return Json::array({value.x, value.y, value.z});
}

inline Json toJson(const math::Quat& value) {
  return Json::array({value.x, value.y, value.z, value.w});
}

inline bool readFloatValue(const Json& value, float& out) {
  if (!value.is_number()) {
    return false;
  }
  const double scalar = value.get<double>();
  if (!std::isfinite(scalar) ||
      std::abs(scalar) >
          static_cast<double>(std::numeric_limits<float>::max())) {
    return false;
  }
  out = static_cast<float>(scalar);
  return true;
}

inline bool readBool(const Json& object, std::string_view key, bool& out) {
  const auto it = object.find(key);
  if (it == object.end()) {
    return true;
  }
  if (!it->is_boolean()) {
    return false;
  }
  out = it->get<bool>();
  return true;
}

inline bool readString(const Json& object,
                       std::string_view key,
                       std::string& out) {
  const auto it = object.find(key);
  if (it == object.end()) {
    return true;
  }
  if (!it->is_string()) {
    return false;
  }
  out = it->get<std::string>();
  return true;
}

inline bool readFloat(const Json& object, std::string_view key, float& out) {
  const auto it = object.find(key);
  if (it == object.end()) {
    return true;
  }
  return readFloatValue(*it, out);
}

inline bool readUint32(const Json& object,
                       std::string_view key,
                       uint32_t& out) {
  const auto it = object.find(key);
  if (it == object.end()) {
    return true;
  }
  if (!it->is_number_unsigned() && !it->is_number_integer()) {
    return false;
  }
  uint64_t value = 0u;
  if (it->is_number_unsigned()) {
    value = it->get<uint64_t>();
  } else {
    const int64_t signed_value = it->get<int64_t>();
    if (signed_value < 0) {
      return false;
    }
    value = static_cast<uint64_t>(signed_value);
  }
  if (value > static_cast<uint64_t>(UINT32_MAX)) {
    return false;
  }
  out = static_cast<uint32_t>(value);
  return true;
}

inline bool readUint64(const Json& object,
                       std::string_view key,
                       uint64_t& out) {
  const auto it = object.find(key);
  if (it == object.end()) {
    return true;
  }
  if (!it->is_number_unsigned() && !it->is_number_integer()) {
    return false;
  }
  if (it->is_number_unsigned()) {
    out = it->get<uint64_t>();
    return true;
  }
  const int64_t value = it->get<int64_t>();
  if (value < 0) {
    return false;
  }
  out = static_cast<uint64_t>(value);
  return true;
}

inline Json serializeEntityReference(
    world::Entity entity,
    const ComponentSerializationContext& context) {
  if (!entity.isValid()) {
    return nullptr;
  }
  if (!context.serialize_entity_reference) {
    // A legacy caller has no stable document identity. Never persist a raw ECS
    // index/generation pair because it is process-local and unsafe to reload.
    return nullptr;
  }
  const std::optional<Json> encoded =
      context.serialize_entity_reference(entity);
  return encoded.has_value() ? *encoded : Json(nullptr);
}

inline bool resolveEntityReferenceValue(
    const Json& value,
    world::Entity& out,
    const ComponentSerializationContext& context) {
  if (value.is_null()) {
    out = {};
    return true;
  }
  if (!context.resolve_entity_reference) {
    return false;
  }
  const std::optional<world::Entity> resolved =
      context.resolve_entity_reference(value);
  if (!resolved.has_value() || !resolved->isValid()) {
    return false;
  }
  out = *resolved;
  return true;
}

inline bool readEntityReference(
    const Json& object,
    std::string_view key,
    world::Entity& out,
    const ComponentSerializationContext& context) {
  const auto it = object.find(key);
  return it == object.end() ||
         resolveEntityReferenceValue(*it, out, context);
}

inline bool readVec3Value(const Json& value, math::Vec3& out) {
  if (!value.is_array() || value.size() != 3u) {
    return false;
  }
  return readFloatValue(value[0], out.x) &&
         readFloatValue(value[1], out.y) &&
         readFloatValue(value[2], out.z);
}

inline bool readQuatValue(const Json& value, math::Quat& out) {
  if (!value.is_array() || value.size() != 4u) {
    return false;
  }
  return readFloatValue(value[0], out.x) &&
         readFloatValue(value[1], out.y) &&
         readFloatValue(value[2], out.z) &&
         readFloatValue(value[3], out.w);
}

inline bool readVec3(const Json& object,
                     std::string_view key,
                     math::Vec3& out) {
  const auto it = object.find(key);
  if (it == object.end()) {
    return true;
  }
  return readVec3Value(*it, out);
}

inline bool readQuat(const Json& object,
                     std::string_view key,
                     math::Quat& out) {
  const auto it = object.find(key);
  if (it == object.end()) {
    return true;
  }
  return readQuatValue(*it, out);
}

template <typename Component, typename SerializeFn, typename DeserializeFn>
void registerComponent(ComponentSerializerRegistry& registry,
                       std::string type_name,
                       SerializeFn serialize,
                       DeserializeFn deserialize) {
  registry.registerSerializer(ComponentSerializer{
      .type_name = std::move(type_name),
      .has =
          [](const world::World& world, world::Entity entity) {
            return world.has<Component>(entity);
          },
      .serialize =
          [serialize = std::move(serialize)](const world::World& world,
                                             world::Entity entity) {
            return serialize(world.get<Component>(entity));
          },
      .deserialize =
          [deserialize = std::move(deserialize)](
              world::World& world, world::Entity entity, const Json& json) {
            std::optional<Component> component = deserialize(json);
            if (!component.has_value()) {
              return false;
            }
            world.add(entity, std::move(*component));
            return true;
          },
  });
}

template <typename Component, typename SerializeFn, typename DeserializeFn>
void registerContextualComponent(ComponentSerializerRegistry& registry,
                                 std::string type_name,
                                 SerializeFn serialize,
                                 DeserializeFn deserialize) {
  registry.registerSerializer(ComponentSerializer{
      .type_name = std::move(type_name),
      .has =
          [](const world::World& world, world::Entity entity) {
            return world.has<Component>(entity);
          },
      .serialize =
          [serialize](const world::World& world, world::Entity entity) {
            return serialize(world.get<Component>(entity),
                             ComponentSerializationContext{});
          },
      .deserialize =
          [deserialize](world::World& world,
                        world::Entity entity,
                        const Json& json) {
            std::optional<Component> component =
                deserialize(json, ComponentSerializationContext{});
            if (!component.has_value()) return false;
            world.add(entity, std::move(*component));
            return true;
          },
      .serialize_with_context =
          [serialize](const world::World& world,
                      world::Entity entity,
                      const ComponentSerializationContext& context) {
            return serialize(world.get<Component>(entity), context);
          },
      .deserialize_with_context =
          [deserialize](world::World& world,
                        world::Entity entity,
                        const Json& json,
                        const ComponentSerializationContext& context) {
            std::optional<Component> component = deserialize(json, context);
            if (!component.has_value()) return false;
            world.add(entity, std::move(*component));
            return true;
          },
  });
}

}  // namespace karma::prefabs::component_serializer_detail
