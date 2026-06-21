#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdlib>

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
  return static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX);
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
