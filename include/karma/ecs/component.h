#pragma once

#include <type_traits>

namespace karma::ecs {

struct ComponentTag {};

template <typename T>
constexpr bool isComponentV = std::is_base_of_v<ComponentTag, T> ||
                              std::is_trivially_copyable_v<T>;

}  // namespace karma::ecs
