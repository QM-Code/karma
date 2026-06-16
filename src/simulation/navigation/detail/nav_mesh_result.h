#pragma once

#include <cstdint>
#include <string>
#include <utility>

#include "karma/simulation/navigation/nav_mesh.h"

namespace karma::navigation::detail {

inline void setResult(NavMeshBuildResult* result,
                      NavStatus status,
                      std::string message,
                      uint32_t vertex_count = 0,
                      uint32_t triangle_count = 0,
                      uint32_t polygon_count = 0) {
  if (result == nullptr) {
    return;
  }
  result->status = status;
  result->message = std::move(message);
  result->vertex_count = vertex_count;
  result->triangle_count = triangle_count;
  result->polygon_count = polygon_count;
}

}  // namespace karma::navigation::detail
