#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cmath>

#include <DetourNavMeshQuery.h>
#include <DetourStatus.h>

#include "karma/math.h"
#include "karma/navigation.h"

namespace karma::navigation::detail {

inline constexpr int kMaxPathPolys = 1024;

inline const float* ptr(const math::Vec3& v) {
  return &v.x;
}

inline math::Vec3 toVec3(const float* v) {
  return {v[0], v[1], v[2]};
}

inline bool succeeded(dtStatus status) {
  return dtStatusSucceed(status) != 0;
}

inline bool failed(dtStatus status) {
  return dtStatusFailed(status) != 0;
}

inline float randomUnit() {
  static std::atomic<uint64_t> next_seed{0x9e3779b97f4a7c15ull};
  thread_local uint64_t state = [] {
    uint64_t seed = next_seed.fetch_add(0x9e3779b97f4a7c15ull,
                                        std::memory_order_relaxed);
    return seed != 0 ? seed : 0xd1b54a32d192ed03ull;
  }();

  // xorshift64*: each query thread owns its state; the top 24 bits map exactly
  // into the mantissa range required by Detour's [0, 1) callback.
  state ^= state >> 12u;
  state ^= state << 25u;
  state ^= state >> 27u;
  const uint64_t value = state * 0x2545f4914f6cdd1dull;
  return static_cast<float>(value >> 40u) * (1.0f / 16777216.0f);
}

inline void applyDetourFilter(dtQueryFilter& out, const NavQueryFilter& filter) {
  out.setIncludeFlags(filter.include_flags);
  out.setExcludeFlags(filter.exclude_flags);
  for (size_t i = 0; i < filter.area_costs.size(); ++i) {
    out.setAreaCost(static_cast<int>(i), filter.area_costs[i]);
  }
}

inline dtQueryFilter makeDetourFilter(const NavQueryFilter& filter) {
  dtQueryFilter out;
  applyDetourFilter(out, filter);
  return out;
}

class NavDetourQueryFilter final : public dtQueryFilter {
 public:
  explicit NavDetourQueryFilter(const NavQueryFilter& filter,
                                const NavTraversalCostProvider* provider = nullptr)
      : filter_(filter), provider_(provider) {
    applyDetourFilter(*this, filter_);
  }

#ifdef DT_VIRTUAL_QUERYFILTER
  float getCost(const float* pa,
                const float* pb,
                const dtPolyRef prevRef,
                const dtMeshTile* prevTile,
                const dtPoly* prevPoly,
                const dtPolyRef curRef,
                const dtMeshTile* curTile,
                const dtPoly* curPoly,
                const dtPolyRef nextRef,
                const dtMeshTile* nextTile,
                const dtPoly* nextPoly) const override {
    (void)prevTile;
    (void)prevPoly;
    (void)curTile;
    (void)nextTile;
    (void)nextPoly;
    const float dx = pb[0] - pa[0];
    const float dy = pb[1] - pa[1];
    const float dz = pb[2] - pa[2];
    const float base_distance = std::sqrt((dx * dx) + (dy * dy) + (dz * dz));
    const unsigned char area =
        curPoly != nullptr ? curPoly->getArea() : kNavAreaDefault;
    const float area_cost = filter_.areaCost(area);
    const float base_cost = base_distance * area_cost;
    if (provider_ == nullptr) {
      return base_cost;
    }

    const NavTraversalContext context{
        .previous_poly_ref = static_cast<uint64_t>(prevRef),
        .current_poly_ref = static_cast<uint64_t>(curRef),
        .next_poly_ref = static_cast<uint64_t>(nextRef),
        .from = toVec3(pa),
        .to = toVec3(pb),
        .base_distance = base_distance,
        .area = area,
        .area_cost = area_cost,
        .base_cost = base_cost,
        .filter = &filter_,
    };
    const float cost = provider_->traversalCost(context);
    return std::isfinite(cost) && cost >= 0.0f ? cost : base_cost;
  }
#else
#error "Karma dynamic navigation costs require Detour built with DT_VIRTUAL_QUERYFILTER."
#endif

 private:
  NavQueryFilter filter_{};
  const NavTraversalCostProvider* provider_ = nullptr;
};

inline NavDetourQueryFilter makeTraversalDetourFilter(
    const NavQueryFilter& filter,
    const NavTraversalCostProvider* provider = nullptr) {
  return NavDetourQueryFilter(filter, provider);
}

inline uint8_t mapStraightPathFlags(unsigned char flags) {
  uint8_t out = NavPathPointFlagNone;
  if ((flags & DT_STRAIGHTPATH_START) != 0) {
    out |= NavPathPointFlagStart;
  }
  if ((flags & DT_STRAIGHTPATH_END) != 0) {
    out |= NavPathPointFlagEnd;
  }
  if ((flags & DT_STRAIGHTPATH_OFFMESH_CONNECTION) != 0) {
    out |= NavPathPointFlagOffMeshConnection;
  }
  return out;
}

inline unsigned char sanitizeArea(unsigned char area) {
  if (area > kNavAreaMax) {
    return kNavAreaDefault;
  }
  return area;
}

inline uint16_t flagsForArea(const NavMeshBuildConfig& config, unsigned char area) {
  if (area == kNavAreaNull) {
    return 0;
  }
  for (const NavAreaConfig& area_config : config.area_configs) {
    if (area_config.area == area) {
      return area_config.flags;
    }
  }
  return config.default_poly_flags;
}

}  // namespace karma::navigation::detail
