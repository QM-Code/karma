#include "karma/features/visual/lights/light_pulse_system.h"

#include <algorithm>
#include <cmath>

#include "karma/world/components/light.h"
#include "karma/world/components/light_pulse.h"
#include "karma/world/components/visibility.h"

namespace karma::visual {

namespace {

float smoothStep01(float value) {
  const float t = std::clamp(value, 0.0f, 1.0f);
  return t * t * (3.0f - 2.0f * t);
}

void setVisibility(ecs::World& world, ecs::Entity entity, bool visible) {
  if (world.has<components::VisibilityComponent>(entity)) {
    world.get<components::VisibilityComponent>(entity).visible = visible;
  }
}

}  // namespace

void LightPulseSystem::update(ecs::World& world, float dt) {
  const float clamped_dt = std::max(dt, 0.0f);
  world.forEach<components::LightPulseComponent, components::LightComponent>(
      [&](ecs::Entity entity) {
        auto& pulse = world.get<components::LightPulseComponent>(entity);
        auto& light = world.get<components::LightComponent>(entity);
        if (!pulse.enabled) {
          return;
        }

        if (pulse.peak_intensity <= 0.0f) {
          pulse.peak_intensity = std::max(light.intensity, 0.0f);
        }
        if (pulse.peak_range <= 0.0f) {
          pulse.peak_range = std::max(light.range, pulse.off_range);
        }

        if (!pulse.active) {
          light.intensity = pulse.off_intensity;
          light.range = pulse.off_range;
          if (pulse.hide_after_completion) {
            setVisibility(world, entity, false);
          }
          return;
        }

        pulse.elapsed += clamped_dt;
        const float local_time = pulse.elapsed - std::max(pulse.start_delay, 0.0f);
        if (local_time < 0.0f) {
          light.intensity = pulse.off_intensity;
          light.range = pulse.off_range;
          if (pulse.hide_after_completion) {
            setVisibility(world, entity, false);
          }
          return;
        }

        const float duration = std::max(pulse.duration, 0.001f);
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
        const float intensity_power = std::max(pulse.intensity_power, 0.001f);
        const float range_power = std::max(pulse.range_power, 0.001f);
        const float range_floor = std::clamp(pulse.range_floor_factor, 0.0f, 1.0f);
        light.intensity = pulse.peak_intensity * std::pow(fade, intensity_power);
        light.range = std::max(
            pulse.off_range,
            pulse.peak_range *
                (range_floor + (1.0f - range_floor) * std::pow(fade, range_power)));
      });
}

}  // namespace karma::visual
