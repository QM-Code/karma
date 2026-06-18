#include "gltf_scene_mesh_import.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace karma::scene {

namespace {

bool readVec3Attribute(const GltfDocument& doc,
                       const Json& attributes,
                       const char* name,
                       std::vector<glm::vec3>& out) {
  out.clear();
  if (!attributes.contains(name) || !attributes[name].is_number_unsigned()) {
    return false;
  }
  std::vector<float> values;
  size_t count = 0;
  if (!readFloatAccessor(doc, attributes[name].get<uint32_t>(), 3, values, &count)) {
    return false;
  }
  out.reserve(count);
  for (size_t i = 0; i < count; ++i) {
    const size_t base = i * 3;
    out.emplace_back(values[base], values[base + 1], values[base + 2]);
  }
  return true;
}

bool readVec2Attribute(const GltfDocument& doc,
                       const Json& attributes,
                       const char* name,
                       std::vector<glm::vec2>& out) {
  out.clear();
  if (!attributes.contains(name) || !attributes[name].is_number_unsigned()) {
    return false;
  }
  std::vector<float> values;
  size_t count = 0;
  if (!readFloatAccessor(doc, attributes[name].get<uint32_t>(), 2, values, &count)) {
    return false;
  }
  out.reserve(count);
  for (size_t i = 0; i < count; ++i) {
    const size_t base = i * 2;
    out.emplace_back(values[base], values[base + 1]);
  }
  return true;
}

bool readVec4Attribute(const GltfDocument& doc,
                       const Json& attributes,
                       const char* name,
                       std::vector<glm::vec4>& out) {
  out.clear();
  if (!attributes.contains(name) || !attributes[name].is_number_unsigned()) {
    return false;
  }
  std::vector<float> values;
  size_t count = 0;
  if (!readFloatAccessor(doc, attributes[name].get<uint32_t>(), 4, values, &count)) {
    return false;
  }
  out.reserve(count);
  for (size_t i = 0; i < count; ++i) {
    const size_t base = i * 4;
    out.emplace_back(values[base], values[base + 1], values[base + 2], values[base + 3]);
  }
  return true;
}

std::vector<glm::vec4> generateTangents(const geometry::MeshData& mesh) {
  std::vector<glm::vec4> tangents(mesh.vertices.size(), glm::vec4(1.0f, 0.0f, 0.0f, 1.0f));
  if (mesh.vertices.empty() ||
      mesh.normals.size() != mesh.vertices.size() ||
      mesh.uvs.size() != mesh.vertices.size()) {
    return tangents;
  }

  std::vector<glm::vec3> tangent_accum(mesh.vertices.size(), glm::vec3(0.0f));
  std::vector<glm::vec3> bitangent_accum(mesh.vertices.size(), glm::vec3(0.0f));
  auto accumulate_triangle = [&](uint32_t i0, uint32_t i1, uint32_t i2) {
    if (i0 >= mesh.vertices.size() || i1 >= mesh.vertices.size() || i2 >= mesh.vertices.size()) {
      return;
    }
    const glm::vec3 edge1 = mesh.vertices[i1] - mesh.vertices[i0];
    const glm::vec3 edge2 = mesh.vertices[i2] - mesh.vertices[i0];
    const glm::vec2 duv1 = mesh.uvs[i1] - mesh.uvs[i0];
    const glm::vec2 duv2 = mesh.uvs[i2] - mesh.uvs[i0];
    const float determinant = duv1.x * duv2.y - duv1.y * duv2.x;
    if (std::abs(determinant) <= 1.0e-8f) {
      return;
    }

    const float inv_det = 1.0f / determinant;
    const glm::vec3 tangent = (edge1 * duv2.y - edge2 * duv1.y) * inv_det;
    const glm::vec3 bitangent = (edge2 * duv1.x - edge1 * duv2.x) * inv_det;
    tangent_accum[i0] += tangent;
    tangent_accum[i1] += tangent;
    tangent_accum[i2] += tangent;
    bitangent_accum[i0] += bitangent;
    bitangent_accum[i1] += bitangent;
    bitangent_accum[i2] += bitangent;
  };

  if (!mesh.indices.empty()) {
    for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
      accumulate_triangle(mesh.indices[i], mesh.indices[i + 1], mesh.indices[i + 2]);
    }
  } else {
    for (size_t i = 0; i + 2 < mesh.vertices.size(); i += 3) {
      accumulate_triangle(static_cast<uint32_t>(i),
                          static_cast<uint32_t>(i + 1),
                          static_cast<uint32_t>(i + 2));
    }
  }

  for (size_t i = 0; i < mesh.vertices.size(); ++i) {
    const glm::vec3 normal = mesh.normals[i];
    glm::vec3 tangent = tangent_accum[i] - normal * glm::dot(normal, tangent_accum[i]);
    if (glm::dot(tangent, tangent) <= 1.0e-8f) {
      tangent = glm::vec3(1.0f, 0.0f, 0.0f);
    } else {
      tangent = glm::normalize(tangent);
    }
    const float sign = glm::dot(glm::cross(normal, tangent), bitangent_accum[i]) < 0.0f
                           ? -1.0f
                           : 1.0f;
    tangents[i] = glm::vec4(tangent, sign);
  }
  return tangents;
}

bool buildMeshDataFromGltfPrimitive(const GltfDocument& doc,
                                    const Json& source_primitive,
                                    geometry::MeshData& out) {
  if (!source_primitive.contains("attributes") ||
      !source_primitive["attributes"].is_object()) {
    return false;
  }
  const Json& attributes = source_primitive["attributes"];

  geometry::MeshData mesh{};
  if (!readVec3Attribute(doc, attributes, "POSITION", mesh.vertices) ||
      mesh.vertices.empty()) {
    return false;
  }

  if (!readVec3Attribute(doc, attributes, "NORMAL", mesh.normals) ||
      mesh.normals.size() != mesh.vertices.size()) {
    mesh.normals.assign(mesh.vertices.size(), glm::vec3(0.0f, 1.0f, 0.0f));
  }
  if (!readVec2Attribute(doc, attributes, "TEXCOORD_0", mesh.uvs) ||
      mesh.uvs.size() != mesh.vertices.size()) {
    mesh.uvs.assign(mesh.vertices.size(), glm::vec2(0.0f));
  } else {
    for (glm::vec2& uv : mesh.uvs) {
      uv.y = 1.0f - uv.y;
    }
  }
  if (!readVec2Attribute(doc, attributes, "TEXCOORD_1", mesh.uvs1) ||
      mesh.uvs1.size() != mesh.vertices.size()) {
    mesh.uvs1 = mesh.uvs;
  } else {
    for (glm::vec2& uv : mesh.uvs1) {
      uv.y = 1.0f - uv.y;
    }
  }
  bool has_tangents =
      readVec4Attribute(doc, attributes, "TANGENT", mesh.tangents) &&
      mesh.tangents.size() == mesh.vertices.size();
  if (has_tangents) {
    for (glm::vec4& tangent : mesh.tangents) {
      tangent.w = -tangent.w;
    }
  }

  if (source_primitive.contains("indices") && source_primitive["indices"].is_number_unsigned()) {
    if (!readIndexAccessor(doc, source_primitive["indices"].get<uint32_t>(), mesh.indices)) {
      return false;
    }
  } else {
    mesh.indices.reserve(mesh.vertices.size());
    for (size_t i = 0; i < mesh.vertices.size(); ++i) {
      mesh.indices.push_back(static_cast<uint32_t>(i));
    }
  }

  if (!has_tangents) {
    mesh.tangents = generateTangents(mesh);
  }

  if (source_primitive.contains("targets") && source_primitive["targets"].is_array()) {
    mesh.morph_targets.reserve(source_primitive["targets"].size());
    for (const Json& target_json : source_primitive["targets"]) {
      if (!target_json.is_object()) {
        continue;
      }
      geometry::MeshData::MorphTarget target{};
      bool has_target_data = false;
      if (readVec3Attribute(doc, target_json, "POSITION", target.position_deltas) &&
          target.position_deltas.size() == mesh.vertices.size()) {
        has_target_data = true;
      } else {
        target.position_deltas.clear();
      }
      if (readVec3Attribute(doc, target_json, "NORMAL", target.normal_deltas) &&
          target.normal_deltas.size() == mesh.vertices.size()) {
        has_target_data = true;
      } else {
        target.normal_deltas.clear();
      }
      if (readVec3Attribute(doc, target_json, "TANGENT", target.tangent_deltas) &&
          target.tangent_deltas.size() == mesh.vertices.size()) {
        has_target_data = true;
      } else {
        target.tangent_deltas.clear();
      }
      if (has_target_data) {
        mesh.morph_targets.push_back(std::move(target));
      }
    }
  }

  out = std::move(mesh);
  return true;
}

std::vector<float> readMorphWeights(const Json& source_mesh, size_t target_count) {
  std::vector<float> weights(target_count, 0.0f);
  if (!source_mesh.contains("weights") || !source_mesh["weights"].is_array()) {
    return weights;
  }
  const size_t count = std::min(target_count, source_mesh["weights"].size());
  for (size_t i = 0; i < count; ++i) {
    if (source_mesh["weights"][i].is_number()) {
      weights[i] = source_mesh["weights"][i].get<float>();
    }
  }
  return weights;
}

}  // namespace

void populateGltfMeshData(const GltfDocument& doc,
                          const std::unordered_map<std::string, uint32_t>& node_indices_by_name,
                          GltfScenePrefab& prefab) {
  if (!doc.valid() ||
      !doc.json.contains("nodes") ||
      !doc.json["nodes"].is_array() ||
      !doc.json.contains("meshes") ||
      !doc.json["meshes"].is_array()) {
    return;
  }

  const auto gltf_node_to_prefab = buildGltfNodeToPrefabIndex(doc, node_indices_by_name);
  for (size_t gltf_node_index = 0; gltf_node_index < doc.json["nodes"].size(); ++gltf_node_index) {
    const Json& node = doc.json["nodes"][gltf_node_index];
    if (!node.contains("mesh") || !node["mesh"].is_number_unsigned()) {
      continue;
    }
    const uint32_t gltf_mesh_index = node["mesh"].get<uint32_t>();
    if (gltf_mesh_index >= doc.json["meshes"].size()) {
      prefab.diagnostics.push_back("GLTF node references a mesh outside the mesh array");
      continue;
    }
    const auto prefab_node_it = gltf_node_to_prefab.find(static_cast<uint32_t>(gltf_node_index));
    if (prefab_node_it == gltf_node_to_prefab.end() ||
        prefab_node_it->second >= prefab.nodes.size()) {
      prefab.diagnostics.push_back("GLTF mesh node could not be mapped to prefab node");
      continue;
    }

    const Json& mesh = doc.json["meshes"][gltf_mesh_index];
    if (!mesh.contains("primitives") || !mesh["primitives"].is_array()) {
      continue;
    }
    auto& primitives = prefab.nodes[prefab_node_it->second].primitives;
    const size_t primitive_count = std::min(primitives.size(), mesh["primitives"].size());
    if (primitives.size() != mesh["primitives"].size()) {
      prefab.diagnostics.push_back("GLTF/Assimp primitive count mismatch; partial mesh replacement");
    }
    for (size_t primitive_index = 0; primitive_index < primitive_count; ++primitive_index) {
      const Json& source_primitive = mesh["primitives"][primitive_index];
      geometry::MeshData mesh_data{};
      if (buildMeshDataFromGltfPrimitive(doc, source_primitive, mesh_data)) {
        primitives[primitive_index].morph_weights =
            readMorphWeights(mesh, mesh_data.morph_targets.size());
        primitives[primitive_index].mesh = std::move(mesh_data);
        if (source_primitive.contains("material") &&
            source_primitive["material"].is_number_unsigned()) {
          primitives[primitive_index].source_gltf_material_index =
              source_primitive["material"].get<uint32_t>();
        }
      } else {
        prefab.diagnostics.push_back("Failed to replace primitive mesh data from GLTF accessors");
      }
    }
  }
}

}  // namespace karma::scene
