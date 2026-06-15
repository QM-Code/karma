#pragma once

#include "karma/core/math/types.h"
#include "karma/world/ecs/component.h"

namespace karma::components {

/// \ingroup karma_components
/// Analytic volumetric primitive shape consumed by `VolumetricComponent`.
enum class VolumetricShape {
  Sphere,
  Capsule,
};

/// \ingroup karma_components
/// Analytic volumetric solid visual effect consumed by `VolumeRuntimeModule`.
struct VolumetricComponent : ecs::ComponentTag {
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
