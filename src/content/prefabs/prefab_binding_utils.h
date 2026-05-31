#pragma once

#include <algorithm>
#include <string>
#include <string_view>
#include <unordered_map>

#include "karma/content/prefabs/prefab.h"

namespace karma::prefabs::detail {

inline math::Color lerpColor(const math::Color& a, const math::Color& b, float t) {
  const float s = std::clamp(t, 0.0f, 1.0f);
  return {
      a.r + (b.r - a.r) * s,
      a.g + (b.g - a.g) * s,
      a.b + (b.b - a.b) * s,
      a.a + (b.a - a.a) * s,
  };
}

inline math::Color multiplyColor(const math::Color& a, const math::Color& b) {
  return {a.r * b.r, a.g * b.g, a.b * b.b, a.a * b.a};
}

inline bool tryGetParamColor(const std::unordered_map<std::string, PrefabParamValue>& params,
                             std::string_view name,
                             math::Color& out_color) {
  const auto it = params.find(std::string(name));
  if (it == params.end()) {
    return false;
  }
  if (const auto* color = std::get_if<math::Color>(&it->second)) {
    out_color = *color;
    return true;
  }
  if (const auto* vec = std::get_if<math::Vec3>(&it->second)) {
    out_color = {vec->x, vec->y, vec->z, 1.0f};
    return true;
  }
  return false;
}

inline bool tryGetParamFloat(const std::unordered_map<std::string, PrefabParamValue>& params,
                             std::string_view name,
                             float& out_value) {
  const auto it = params.find(std::string(name));
  if (it == params.end()) {
    return false;
  }
  if (const auto* value = std::get_if<float>(&it->second)) {
    out_value = *value;
    return true;
  }
  return false;
}

inline math::Color resolveColorBinding(
    const PrefabColorBinding& binding,
    const std::unordered_map<std::string, PrefabParamValue>& params,
    const math::Color& fallback) {
  math::Color color = binding.value.value_or(fallback);
  if (!binding.param.empty()) {
    math::Color param_color{};
    if (tryGetParamColor(params, binding.param, param_color)) {
      color = param_color;
    }
  }
  color = multiplyColor(color, binding.scale);
  if (binding.mix_color.has_value() && binding.mix_factor > 0.0f) {
    color = lerpColor(color, *binding.mix_color, binding.mix_factor);
  }
  return color;
}

inline float resolveFloatBinding(const PrefabFloatBinding& binding,
                                 const std::unordered_map<std::string, PrefabParamValue>& params,
                                 float fallback) {
  float value = binding.value.value_or(fallback);
  if (!binding.param.empty()) {
    float param_value = 0.0f;
    if (tryGetParamFloat(params, binding.param, param_value)) {
      value = param_value;
    }
  }
  return value * binding.scale + binding.bias;
}

}  // namespace karma::prefabs::detail
