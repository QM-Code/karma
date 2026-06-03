#pragma once

#include <type_traits>

namespace karma::ecs {

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

}  // namespace karma::ecs
