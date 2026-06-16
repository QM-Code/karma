#pragma once

#include "demo_asset_paths.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "karma/content/importers/mesh_import.h"
#include "karma/simulation/navigation/nav_geometry.h"

namespace karma::demo {

struct QueryCase {
  std::string kind;
  math::Vec3 start{};
  math::Vec3 end{};
  uint16_t include_flags = navigation::kNavPolyFlagAll;
  uint16_t exclude_flags = 0;
};

struct TestCaseFile {
  std::string sample_name;
  std::string mesh_file;
  std::vector<QueryCase> queries;
};

struct MeshGeometry {
  std::filesystem::path path;
  std::vector<geometry::MeshData> meshes;
  navigation::NavMeshInputGeometry geometry;
};

struct Bounds {
  math::Vec3 min{};
  math::Vec3 max{};
};

inline std::filesystem::path recastAssetPath(std::string_view relative) {
  const std::filesystem::path path =
      std::filesystem::path("navigation") / "recast" / std::filesystem::path{relative};
  return resolveExampleAssetPath(path.generic_string());
}

inline uint16_t parseFlags(const std::string& text) {
  return static_cast<uint16_t>(std::stoul(text, nullptr, 0));
}

inline std::optional<TestCaseFile> loadTestCase(const std::filesystem::path& path) {
  std::ifstream in(path);
  if (!in) {
    return std::nullopt;
  }

  TestCaseFile file;
  std::string op;
  while (in >> op) {
    if (op == "s") {
      std::getline(in, file.sample_name);
      if (!file.sample_name.empty() && file.sample_name.front() == ' ') {
        file.sample_name.erase(file.sample_name.begin());
      }
    } else if (op == "f") {
      in >> file.mesh_file;
    } else if (op == "pf" || op == "rc") {
      QueryCase query;
      query.kind = op;
      std::string include_text;
      std::string exclude_text;
      in >> query.start.x >> query.start.y >> query.start.z;
      in >> query.end.x >> query.end.y >> query.end.z;
      in >> include_text >> exclude_text;
      query.include_flags = parseFlags(include_text);
      query.exclude_flags = parseFlags(exclude_text);
      file.queries.push_back(query);
    } else {
      std::string ignored;
      std::getline(in, ignored);
    }
  }
  return file;
}

inline MeshGeometry loadMeshGeometry(std::string_view mesh_file) {
  MeshGeometry out;
  const std::filesystem::path mesh_path =
      std::filesystem::path("meshes") / std::filesystem::path{mesh_file};
  out.path = recastAssetPath(mesh_path.generic_string());
  out.meshes = content::importMeshes(out.path);
  for (const geometry::MeshData& mesh : out.meshes) {
    navigation::appendGeometry(out.geometry, mesh);
  }
  return out;
}

inline Bounds computeBounds(const navigation::NavMeshInputGeometry& geometry) {
  Bounds bounds;
  if (geometry.vertices.empty()) {
    return bounds;
  }
  bounds.min = geometry.vertices.front();
  bounds.max = geometry.vertices.front();
  for (const math::Vec3& vertex : geometry.vertices) {
    bounds.min.x = std::min(bounds.min.x, vertex.x);
    bounds.min.y = std::min(bounds.min.y, vertex.y);
    bounds.min.z = std::min(bounds.min.z, vertex.z);
    bounds.max.x = std::max(bounds.max.x, vertex.x);
    bounds.max.y = std::max(bounds.max.y, vertex.y);
    bounds.max.z = std::max(bounds.max.z, vertex.z);
  }
  return bounds;
}

inline math::Vec3 centerOf(const Bounds& bounds) {
  return {
      (bounds.min.x + bounds.max.x) * 0.5f,
      (bounds.min.y + bounds.max.y) * 0.5f,
      (bounds.min.z + bounds.max.z) * 0.5f,
  };
}

inline math::Vec3 midpoint(const math::Vec3& a, const math::Vec3& b) {
  return {
      (a.x + b.x) * 0.5f,
      (a.y + b.y) * 0.5f,
      (a.z + b.z) * 0.5f,
  };
}

inline math::Vec3 offsetPoint(const math::Vec3& point, const math::Vec3& offset) {
  return {point.x + offset.x, point.y + offset.y, point.z + offset.z};
}

inline navigation::NavMeshBuildConfig recastBuildConfig(navigation::NavMeshBuildMode mode) {
  navigation::NavMeshBuildConfig config;
  config.build_mode = mode;
  config.cell_size = 0.3f;
  config.cell_height = 0.2f;
  config.agent_height = 2.0f;
  config.agent_radius = 0.6f;
  config.agent_max_climb = 0.9f;
  config.agent_max_slope_degrees = 45.0f;
  config.edge_max_len = 12.0f;
  config.edge_max_error = 1.3f;
  config.region_min_size = 8.0f;
  config.region_merge_size = 20.0f;
  config.verts_per_poly = 6;
  config.detail_sample_dist = 6.0f;
  config.detail_sample_max_error = 1.0f;
  config.tile_size = 32;
  config.area_configs = {
      {.area = navigation::kNavAreaDefault, .flags = navigation::kNavPolyFlagWalk, .cost = 1.0f},
      {.area = 2, .flags = static_cast<uint16_t>(1u << 4u), .cost = 1.8f},
  };
  return config;
}

}  // namespace karma::demo
