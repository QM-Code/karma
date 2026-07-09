#include "karma/visual.h"

#include <algorithm>
#include <cmath>

#include "karma/components.h"

namespace karma::visual {

namespace {

float smoothStep01(float value) {
  const float t = std::clamp(value, 0.0f, 1.0f);
  return t * t * (3.0f - 2.0f * t);
}

float finiteOr(float value, float fallback) {
  return std::isfinite(value) ? value : fallback;
}

void setVisibility(world::World& world, world::Entity entity, bool visible) {
  if (world.has<components::VisibilityComponent>(entity)) {
    world.get<components::VisibilityComponent>(entity).visible = visible;
  }
}

}  // namespace

void LightPulseSystem::update(world::World& world, float dt) {
  const float clamped_dt = std::isfinite(dt) ? std::max(dt, 0.0f) : 0.0f;
  world.forEach<components::LightPulseComponent, components::LightComponent>(
      [&](world::Entity entity) {
        auto& pulse = world.get<components::LightPulseComponent>(entity);
        auto& light = world.get<components::LightComponent>(entity);
        if (!pulse.enabled) {
          return;
        }

        pulse.off_intensity = std::max(finiteOr(pulse.off_intensity, 0.0f), 0.0f);
        pulse.off_range = std::max(finiteOr(pulse.off_range, 0.0f), 0.0f);
        if (!std::isfinite(pulse.peak_intensity) || pulse.peak_intensity <= 0.0f) {
          pulse.peak_intensity = std::max(finiteOr(light.intensity, 0.0f), 0.0f);
        }
        if (!std::isfinite(pulse.peak_range) || pulse.peak_range <= 0.0f) {
          pulse.peak_range = std::max(finiteOr(light.range, pulse.off_range), pulse.off_range);
        }

        if (!pulse.active) {
          light.intensity = pulse.off_intensity;
          light.range = pulse.off_range;
          if (pulse.hide_after_completion) {
            setVisibility(world, entity, false);
          }
          return;
        }

        pulse.elapsed = std::max(finiteOr(pulse.elapsed, 0.0f), 0.0f) + clamped_dt;
        const float local_time =
            pulse.elapsed - std::max(finiteOr(pulse.start_delay, 0.0f), 0.0f);
        if (local_time < 0.0f) {
          light.intensity = pulse.off_intensity;
          light.range = pulse.off_range;
          if (pulse.hide_after_completion) {
            setVisibility(world, entity, false);
          }
          return;
        }

        const float duration = std::max(finiteOr(pulse.duration, 0.001f), 0.001f);
        if (local_time >= duration) {
          pulse.active = false;
          light.intensity = pulse.off_intensity;
          light.range = pulse.off_range;
          if (pulse.hide_after_completion) {
            setVisibility(world, entity, false);
          }
          return;
        }

        setVisibility(world, entity, true);
        const float t = std::clamp(local_time / duration, 0.0f, 1.0f);
        const float fade = 1.0f - smoothStep01(t);
        const float intensity_power =
            std::max(finiteOr(pulse.intensity_power, 1.0f), 0.001f);
        const float range_power = std::max(finiteOr(pulse.range_power, 1.0f), 0.001f);
        const float range_floor =
            std::clamp(finiteOr(pulse.range_floor_factor, 0.0f), 0.0f, 1.0f);
        light.intensity = pulse.peak_intensity * std::pow(fade, intensity_power);
        light.range = std::max(
            pulse.off_range,
            pulse.peak_range *
                (range_floor + (1.0f - range_floor) * std::pow(fade, range_power)));
      });
}

}  // namespace karma::visual
