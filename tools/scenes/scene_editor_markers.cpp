#include "scene_editor_markers.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <string_view>
#include <utility>

namespace karma::tools::scene_editor {
namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kMinimumGeometrySize = 1.0e-6f;

constexpr math::Color kEmptyMarkerColor{0.78f, 0.82f, 0.88f, 1.0f};
constexpr math::Color kPrefabMarkerColor{0.25f, 0.72f, 1.0f, 1.0f};
constexpr math::Color kEnvironmentMarkerColor{0.35f, 0.88f, 0.78f, 1.0f};

bool sameSelection(const Selection& a, const Selection& b) {
  return a.kind == b.kind && a.id == b.id;
}

int markerPickPriority(SceneMarkerKind kind) {
  switch (kind) {
    case SceneMarkerKind::PointLight:
    case SceneMarkerKind::SpotLight:
    case SceneMarkerKind::DirectionalLight:
      return 3;
    case SceneMarkerKind::EnvironmentAnchor:
      return 2;
    case SceneMarkerKind::PrefabRoot:
      return 1;
    case SceneMarkerKind::EmptyEntity:
      return 0;
  }
  return 0;
}

float finiteNonNegative(float value) {
  return std::isfinite(value) ? std::max(value, 0.0f) : 0.0f;
}

math::Color visibleLightColor(const math::Color& color) {
  if (!math::isFinite(color)) return {1.0f, 0.85f, 0.35f, 1.0f};
  // Keep black and very dim authored lights visible as editor controls while
  // preserving the useful hue of ordinary light colors.
  return {
      0.2f + 0.8f * math::clamp01(color.r),
      0.2f + 0.8f * math::clamp01(color.g),
      0.2f + 0.8f * math::clamp01(color.b),
      1.0f,
  };
}

math::Color selectedColor(const SceneMarker& marker) {
  if (!marker.selected) return marker.color;
  constexpr math::Color kSelection{1.0f, 0.78f, 0.18f, 1.0f};
  return {
      math::lerp(marker.color.r, kSelection.r, 0.35f),
      math::lerp(marker.color.g, kSelection.g, 0.35f),
      math::lerp(marker.color.b, kSelection.b, 0.35f),
      1.0f,
  };
}

bool componentEnabledForRendering(const nlohmann::json& component) {
  if (!component.is_object()) return true;
  const auto visible = component.find("visible");
  if (visible != component.end() && visible->is_boolean() &&
      !visible->get<bool>()) {
    return false;
  }
  const auto enabled = component.find("enabled");
  return enabled == component.end() || !enabled->is_boolean() ||
         enabled->get<bool>();
}

class MarkerGeometryBuilder {
 public:
  MarkerGeometryBuilder(const SceneMarker& marker,
                        float icon_world_radius,
                        uint32_t circle_segments)
      : marker_(marker),
        icon_world_radius_(icon_world_radius),
        circle_segments_(circle_segments),
        color_(selectedColor(marker)) {
    geometry_.pivot = marker.world_transform.position;
    geometry_.icon_world_radius = icon_world_radius;
  }

  SceneMarkerGeometry finish() { return std::move(geometry_); }

  void localLine(const math::Vec3& from,
                 const math::Vec3& to,
                 SceneMarkerLineLayer layer = SceneMarkerLineLayer::Overlay) {
    worldLine(localPoint(from), localPoint(to), layer, color_);
  }

  void localCircle(const math::Vec3& center,
                   const math::Vec3& axis_u,
                   const math::Vec3& axis_v,
                   float radius,
                   SceneMarkerLineLayer layer = SceneMarkerLineLayer::Overlay) {
    for (uint32_t index = 0u; index < circle_segments_; ++index) {
      const float angle_a =
          2.0f * kPi * static_cast<float>(index) /
          static_cast<float>(circle_segments_);
      const float angle_b =
          2.0f * kPi * static_cast<float>(index + 1u) /
          static_cast<float>(circle_segments_);
      localLine(circlePoint(center, axis_u, axis_v, radius, angle_a),
                circlePoint(center, axis_u, axis_v, radius, angle_b),
                layer);
    }
  }

  void localSphere(const math::Vec3& center,
                   float radius,
                   SceneMarkerLineLayer layer = SceneMarkerLineLayer::Overlay) {
    localCircle(center, {1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, radius, layer);
    localCircle(center, {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, radius, layer);
    localCircle(center, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, radius, layer);
  }

  void worldLine(const math::Vec3& from,
                 const math::Vec3& to,
                 SceneMarkerLineLayer layer,
                 math::Color color) {
    if (!math::isFinite(from) || !math::isFinite(to) ||
        !math::isFinite(color)) {
      return;
    }
    geometry_.lines.push_back(
        SceneMarkerLine{.from = from,
                        .to = to,
                        .color = color,
                        .layer = layer});
  }

  void worldCircle(const math::Vec3& center,
                   const math::Vec3& axis_u,
                   const math::Vec3& axis_v,
                   float radius,
                   SceneMarkerLineLayer layer,
                   math::Color color) {
    if (!std::isfinite(radius) || radius <= kMinimumGeometrySize) return;
    for (uint32_t index = 0u; index < circle_segments_; ++index) {
      const float angle_a =
          2.0f * kPi * static_cast<float>(index) /
          static_cast<float>(circle_segments_);
      const float angle_b =
          2.0f * kPi * static_cast<float>(index + 1u) /
          static_cast<float>(circle_segments_);
      worldLine(circlePoint(center, axis_u, axis_v, radius, angle_a),
                circlePoint(center, axis_u, axis_v, radius, angle_b),
                layer,
                color);
    }
  }

  void worldSphere(const math::Vec3& center,
                   float radius,
                   SceneMarkerLineLayer layer,
                   math::Color color) {
    worldCircle(center, {1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, radius,
                layer, color);
    worldCircle(center, {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, radius,
                layer, color);
    worldCircle(center, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, radius,
                layer, color);
  }

  math::Vec3 localPoint(const math::Vec3& point) const {
    return math::add(
        marker_.world_transform.position,
        math::rotateVec(marker_.world_transform.rotation,
                        math::scale(point, icon_world_radius_)));
  }

 private:
  static math::Vec3 circlePoint(const math::Vec3& center,
                                const math::Vec3& axis_u,
                                const math::Vec3& axis_v,
                                float radius,
                                float angle) {
    return math::add(
        center,
        math::add(math::scale(axis_u, radius * std::cos(angle)),
                  math::scale(axis_v, radius * std::sin(angle))));
  }

  const SceneMarker& marker_;
  float icon_world_radius_ = 0.0f;
  uint32_t circle_segments_ = 24u;
  math::Color color_{};
  SceneMarkerGeometry geometry_{};
};

void appendPointLightGeometry(MarkerGeometryBuilder& builder,
                              const SceneMarker& marker) {
  builder.localSphere({0.0f, 0.12f, 0.0f}, 0.58f);
  builder.localLine({0.0f, -0.46f, 0.0f}, {0.0f, -0.88f, 0.0f});
  builder.localLine({-0.24f, -0.72f, 0.0f}, {0.24f, -0.72f, 0.0f});
  builder.localLine({-0.18f, -0.88f, 0.0f}, {0.18f, -0.88f, 0.0f});

  if (marker.selected && marker.range > kMinimumGeometrySize) {
    math::Color bounds = selectedColor(marker);
    bounds.a = 0.7f;
    builder.worldSphere(marker.world_transform.position,
                        marker.range,
                        SceneMarkerLineLayer::Bounds,
                        bounds);
  }
}

void appendSpotLightGeometry(MarkerGeometryBuilder& builder,
                             const SceneMarker& marker,
                             uint32_t circle_segments) {
  constexpr float kIconLength = 1.15f;
  constexpr float kIconRadius = 0.43f;
  builder.localCircle({0.0f, 0.0f, -kIconLength},
                      {1.0f, 0.0f, 0.0f},
                      {0.0f, 1.0f, 0.0f},
                      kIconRadius);
  for (const math::Vec3 rim :
       std::array<math::Vec3, 4>{{{kIconRadius, 0.0f, -kIconLength},
                                  {-kIconRadius, 0.0f, -kIconLength},
                                  {0.0f, kIconRadius, -kIconLength},
                                  {0.0f, -kIconRadius, -kIconLength}}}) {
    builder.localLine({}, rim);
  }
  builder.localLine({}, {0.0f, 0.0f, -1.42f});
  builder.localLine({0.0f, 0.0f, -1.42f}, {0.22f, 0.0f, -1.18f});
  builder.localLine({0.0f, 0.0f, -1.42f}, {-0.22f, 0.0f, -1.18f});

  if (!marker.selected || marker.range <= kMinimumGeometrySize) return;

  const float angle_degrees =
      std::clamp(marker.outer_cone_degrees, 0.0f, 89.0f);
  const float radius =
      marker.range * std::tan(angle_degrees * (kPi / 180.0f));
  if (!std::isfinite(radius)) return;

  const math::Vec3 direction = math::normalize(math::rotateVec(
      marker.world_transform.rotation, {0.0f, 0.0f, -1.0f}));
  const math::Vec3 right = math::normalize(math::rotateVec(
      marker.world_transform.rotation, {1.0f, 0.0f, 0.0f}));
  const math::Vec3 up = math::normalize(math::rotateVec(
      marker.world_transform.rotation, {0.0f, 1.0f, 0.0f}));
  const math::Vec3 end = math::add(
      marker.world_transform.position, math::scale(direction, marker.range));
  math::Color bounds = selectedColor(marker);
  bounds.a = 0.7f;
  builder.worldCircle(end, right, up, radius,
                      SceneMarkerLineLayer::Bounds, bounds);

  const uint32_t spoke_count = std::min<uint32_t>(8u, circle_segments);
  for (uint32_t index = 0u; index < spoke_count; ++index) {
    const float angle = 2.0f * kPi * static_cast<float>(index) /
                        static_cast<float>(spoke_count);
    const math::Vec3 rim = math::add(
        end,
        math::add(math::scale(right, radius * std::cos(angle)),
                  math::scale(up, radius * std::sin(angle))));
    builder.worldLine(marker.world_transform.position,
                      rim,
                      SceneMarkerLineLayer::Bounds,
                      bounds);
  }
}

void appendDirectionalLightGeometry(MarkerGeometryBuilder& builder) {
  builder.localSphere({}, 0.42f);
  for (const math::Vec3 direction :
       std::array<math::Vec3, 6>{{{1.0f, 0.0f, 0.0f},
                                  {-1.0f, 0.0f, 0.0f},
                                  {0.0f, 1.0f, 0.0f},
                                  {0.0f, -1.0f, 0.0f},
                                  {0.0f, 0.0f, 1.0f},
                                  {0.0f, 0.0f, -1.0f}}}) {
    builder.localLine(math::scale(direction, 0.6f), direction);
  }
  builder.localLine({}, {0.0f, 0.0f, -1.6f});
  builder.localLine({0.0f, 0.0f, -1.6f}, {0.24f, 0.0f, -1.3f});
  builder.localLine({0.0f, 0.0f, -1.6f}, {-0.24f, 0.0f, -1.3f});
  builder.localLine({0.0f, 0.0f, -1.6f}, {0.0f, 0.24f, -1.3f});
  builder.localLine({0.0f, 0.0f, -1.6f}, {0.0f, -0.24f, -1.3f});
}

void appendEmptyGeometry(MarkerGeometryBuilder& builder) {
  constexpr math::Vec3 top{0.0f, 1.0f, 0.0f};
  constexpr math::Vec3 bottom{0.0f, -1.0f, 0.0f};
  constexpr std::array<math::Vec3, 4> ring{{
      {0.72f, 0.0f, 0.0f},
      {0.0f, 0.0f, 0.72f},
      {-0.72f, 0.0f, 0.0f},
      {0.0f, 0.0f, -0.72f},
  }};
  for (const math::Vec3 point : ring) {
    builder.localLine(top, point);
    builder.localLine(bottom, point);
  }
}

void appendPrefabGeometry(MarkerGeometryBuilder& builder) {
  constexpr std::array<math::Vec3, 8> corners{{
      {-0.7f, -0.7f, -0.7f}, {0.7f, -0.7f, -0.7f},
      {0.7f, 0.7f, -0.7f},   {-0.7f, 0.7f, -0.7f},
      {-0.7f, -0.7f, 0.7f},  {0.7f, -0.7f, 0.7f},
      {0.7f, 0.7f, 0.7f},    {-0.7f, 0.7f, 0.7f},
  }};
  constexpr std::array<std::array<uint8_t, 2>, 12> edges{{
      {0u, 1u}, {1u, 2u}, {2u, 3u}, {3u, 0u},
      {4u, 5u}, {5u, 6u}, {6u, 7u}, {7u, 4u},
      {0u, 4u}, {1u, 5u}, {2u, 6u}, {3u, 7u},
  }};
  for (const auto edge : edges) {
    builder.localLine(corners[edge[0]], corners[edge[1]]);
  }
}

void appendEnvironmentGeometry(MarkerGeometryBuilder& builder) {
  builder.localSphere({}, 0.76f);
  constexpr float kLatitudeY = 0.38f;
  const float latitude_radius =
      std::sqrt(0.76f * 0.76f - kLatitudeY * kLatitudeY);
  builder.localCircle({0.0f, kLatitudeY, 0.0f},
                      {1.0f, 0.0f, 0.0f},
                      {0.0f, 0.0f, 1.0f},
                      latitude_radius);
  builder.localCircle({0.0f, -kLatitudeY, 0.0f},
                      {1.0f, 0.0f, 0.0f},
                      {0.0f, 0.0f, 1.0f},
                      latitude_radius);
}

}  // namespace

bool sceneEntityHasRenderableContent(const scenes::SceneDocument& document,
                                     const scenes::SceneEntity& entity) {
  const auto visibility = entity.components.find("VisibilityComponent");
  if (visibility != entity.components.end() && visibility->is_object()) {
    const auto visible = visibility->find("visible");
    if (visible != visibility->end() && visible->is_boolean() &&
        !visible->get<bool>()) {
      return false;
    }
  }

  if (std::any_of(document.static_components.begin(),
                  document.static_components.end(),
                  [&](const scenes::SceneStaticComponent& component) {
                    return component.entity_id == entity.id && component.render;
                  })) {
    return true;
  }

  constexpr std::array<std::string_view, 8> kRenderableComponents{
      "MeshComponent",
      "InstancedMeshComponent",
      "ParticleBeamComponent",
      "ParticleEffectComponent",
      "ParticleEmitterComponent",
      "TerrainComponent",
      "FoliageComponent",
      "VolumetricComponent",
  };
  return std::any_of(
      kRenderableComponents.begin(),
      kRenderableComponents.end(),
      [&](std::string_view name) {
        const auto component = entity.components.find(name);
        return component != entity.components.end() &&
               componentEnabledForRendering(*component);
      });
}

SceneMarkerClassificationResult classifySceneMarkers(
    const scenes::SceneDocument& document,
    const Selection& selected) {
  SceneMarkerClassificationResult result{};
  result.markers.reserve(document.entities.size() +
                         document.prefab_instances.size());

  for (const scenes::SceneEntity& entity : document.entities) {
    const auto light = std::find_if(
        document.lights.begin(),
        document.lights.end(),
        [&](const scenes::SceneLight& value) {
          return value.entity_id == entity.id;
        });
    const bool environment =
        document.environment.has_value() &&
        document.environment->entity_id == entity.id;
    if (light == document.lights.end() && !environment &&
        sceneEntityHasRenderableContent(document, entity)) {
      continue;
    }

    std::string diagnostic;
    const Selection selection{SelectionKind::Entity, entity.id};
    const auto transform = sceneWorldTransform(document, selection, &diagnostic);
    if (!transform.has_value()) {
      result.diagnostics.push_back(
          "could not resolve marker for entity '" + entity.id + "': " +
          (diagnostic.empty() ? "invalid world transform" : diagnostic));
      continue;
    }

    SceneMarker marker{};
    marker.selection = selection;
    marker.world_transform = *transform;
    marker.selected = sameSelection(selection, selected);
    if (light != document.lights.end()) {
      switch (light->component.type) {
        case components::LightComponent::Type::Point:
          marker.kind = SceneMarkerKind::PointLight;
          break;
        case components::LightComponent::Type::Spot:
          marker.kind = SceneMarkerKind::SpotLight;
          break;
        case components::LightComponent::Type::Directional:
          marker.kind = SceneMarkerKind::DirectionalLight;
          break;
      }
      marker.color = visibleLightColor(light->component.color);
      marker.range = finiteNonNegative(light->component.range);
      marker.outer_cone_degrees = finiteNonNegative(
          light->component.outer_cone_degrees);
    } else if (environment) {
      marker.kind = SceneMarkerKind::EnvironmentAnchor;
      marker.color = kEnvironmentMarkerColor;
    } else {
      marker.kind = SceneMarkerKind::EmptyEntity;
      marker.color = kEmptyMarkerColor;
    }
    result.markers.push_back(std::move(marker));
  }

  for (const scenes::ScenePrefabInstance& prefab : document.prefab_instances) {
    std::string diagnostic;
    const Selection selection{SelectionKind::Prefab, prefab.id};
    const auto transform = sceneWorldTransform(document, selection, &diagnostic);
    if (!transform.has_value()) {
      result.diagnostics.push_back(
          "could not resolve marker for prefab '" + prefab.id + "': " +
          (diagnostic.empty() ? "invalid world transform" : diagnostic));
      continue;
    }
    result.markers.push_back(SceneMarker{
        .selection = selection,
        .kind = SceneMarkerKind::PrefabRoot,
        .world_transform = *transform,
        .color = kPrefabMarkerColor,
        .selected = sameSelection(selection, selected),
    });
  }

  return result;
}

bool sceneMarkerVisible(const SceneMarker& marker, bool markers_visible) {
  return markers_visible || marker.selected;
}

SceneMarkerGeometry buildSceneMarkerGeometry(
    const SceneMarker& marker,
    const ViewportProjection& projection,
    const SceneMarkerGeometryOptions& options) {
  if (!std::isfinite(options.icon_radius_pixels) ||
      options.icon_radius_pixels <= 0.0f ||
      !math::isFinite(marker.world_transform.position)) {
    return {};
  }
  const float world_units_per_pixel =
      worldUnitsPerViewportPixel(projection, marker.world_transform.position);
  const float icon_world_radius =
      world_units_per_pixel * options.icon_radius_pixels;
  if (!std::isfinite(icon_world_radius) ||
      icon_world_radius <= kMinimumGeometrySize) {
    return {};
  }

  const uint32_t circle_segments =
      std::clamp(options.circle_segments, 8u, 128u);
  MarkerGeometryBuilder builder(marker, icon_world_radius, circle_segments);
  switch (marker.kind) {
    case SceneMarkerKind::PointLight:
      appendPointLightGeometry(builder, marker);
      break;
    case SceneMarkerKind::SpotLight:
      appendSpotLightGeometry(builder, marker, circle_segments);
      break;
    case SceneMarkerKind::DirectionalLight:
      appendDirectionalLightGeometry(builder);
      break;
    case SceneMarkerKind::EmptyEntity:
      appendEmptyGeometry(builder);
      break;
    case SceneMarkerKind::PrefabRoot:
      appendPrefabGeometry(builder);
      break;
    case SceneMarkerKind::EnvironmentAnchor:
      appendEnvironmentGeometry(builder);
      break;
  }
  return builder.finish();
}

std::optional<SceneMarkerHit> pickSceneMarker(
    std::span<const SceneMarker> markers,
    const ViewportProjection& projection,
    ViewportPoint cursor,
    bool markers_visible,
    float pick_radius_pixels) {
  if (!std::isfinite(cursor.x) || !std::isfinite(cursor.y) ||
      !std::isfinite(pick_radius_pixels) || pick_radius_pixels <= 0.0f) {
    return std::nullopt;
  }
  const float radius_squared = pick_radius_pixels * pick_radius_pixels;
  std::optional<SceneMarkerHit> best;
  float best_distance_squared = std::numeric_limits<float>::infinity();
  int best_priority = std::numeric_limits<int>::min();

  for (size_t index = 0u; index < markers.size(); ++index) {
    const SceneMarker& marker = markers[index];
    if (!sceneMarkerVisible(marker, markers_visible)) continue;
    const auto projected =
        projectWorldToViewport(projection, marker.world_transform.position);
    if (!projected.has_value() || !projected->inside_clip ||
        !projected->inside_viewport || projected->view_depth <= 0.0f) {
      continue;
    }
    const float delta_x = cursor.x - projected->screen.x;
    const float delta_y = cursor.y - projected->screen.y;
    const float distance_squared = delta_x * delta_x + delta_y * delta_y;
    if (!std::isfinite(distance_squared) ||
        distance_squared > radius_squared) {
      continue;
    }

    constexpr float kTieEpsilon = 1.0e-4f;
    const bool closer_on_screen =
        distance_squared + kTieEpsilon < best_distance_squared;
    const bool same_screen_distance =
        std::abs(distance_squared - best_distance_squared) <= kTieEpsilon;
    const bool closer_in_depth =
        same_screen_distance && best.has_value() &&
        projected->view_depth + kTieEpsilon < best->view_depth;
    const bool same_depth =
        same_screen_distance && best.has_value() &&
        std::abs(projected->view_depth - best->view_depth) <= kTieEpsilon;
    const int priority = markerPickPriority(marker.kind);
    const bool more_specific = same_depth && priority > best_priority;
    if (!best.has_value() || closer_on_screen || closer_in_depth ||
        more_specific) {
      best_distance_squared = distance_squared;
      best_priority = priority;
      best = SceneMarkerHit{
          .marker_index = index,
          .selection = marker.selection,
          .kind = marker.kind,
          .screen_distance_pixels = std::sqrt(distance_squared),
          .view_depth = projected->view_depth,
      };
    }
  }
  return best;
}

}  // namespace karma::tools::scene_editor
