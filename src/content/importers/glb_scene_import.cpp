#include "karma/content/importers/glb_scene_import.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <limits>
#include <memory>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <assimp/Importer.hpp>
#include <assimp/material.h>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

#include "gltf_document.h"
#include "glb_scene_animation_import.h"
#include "glb_scene_mesh_import.h"
#include "glb_scene_skinning.h"
#include "karma/world/components/animator.h"
#include "karma/world/components/mesh.h"
#include "karma/world/components/morph_target.h"
#include "karma/world/components/skinned_mesh.h"
#include "karma/world/components/tag.h"
#include "karma/world/components/transform.h"
#include "karma/world/scene/transform_hierarchy.h"

namespace karma::scene {

namespace {

math::Vec3 toVec3(const aiVector3D& v) {
  return {v.x, v.y, v.z};
}

math::Quat toQuat(const aiQuaternion& q) {
  return {q.x, q.y, q.z, q.w};
}

std::string safeName(std::string_view base, std::string_view fallback) {
  return base.empty() ? std::string(fallback) : std::string(base);
}

geometry::MeshData buildMeshData(const aiMesh& mesh) {
  geometry::MeshData out{};
  out.vertices.reserve(mesh.mNumVertices);
  out.normals.reserve(mesh.mNumVertices);
  out.uvs.reserve(mesh.mNumVertices);
  out.uvs1.reserve(mesh.mNumVertices);
  out.tangents.reserve(mesh.mNumVertices);

  for (unsigned int v = 0; v < mesh.mNumVertices; ++v) {
    const auto& pos = mesh.mVertices[v];
    out.vertices.emplace_back(pos.x, pos.y, pos.z);

    if (mesh.HasNormals()) {
      const auto& n = mesh.mNormals[v];
      out.normals.emplace_back(n.x, n.y, n.z);
    } else {
      out.normals.emplace_back(0.0f, 1.0f, 0.0f);
    }

    if (mesh.HasTextureCoords(0)) {
      const auto& uv = mesh.mTextureCoords[0][v];
      out.uvs.emplace_back(uv.x, uv.y);
    } else {
      out.uvs.emplace_back(0.0f, 0.0f);
    }

    if (mesh.HasTextureCoords(1)) {
      const auto& uv = mesh.mTextureCoords[1][v];
      out.uvs1.emplace_back(uv.x, uv.y);
    } else {
      out.uvs1.emplace_back(out.uvs.back());
    }

    if (mesh.HasTangentsAndBitangents()) {
      const auto& t = mesh.mTangents[v];
      const auto& b = mesh.mBitangents[v];
      const auto& n = mesh.mNormals[v];
      const glm::vec3 tangent{t.x, t.y, t.z};
      const glm::vec3 bitangent{b.x, b.y, b.z};
      const glm::vec3 normal{n.x, n.y, n.z};
      const float sign =
          glm::dot(glm::cross(normal, tangent), bitangent) < 0.0f ? -1.0f : 1.0f;
      out.tangents.emplace_back(tangent.x, tangent.y, tangent.z, sign);
    } else {
      out.tangents.emplace_back(1.0f, 0.0f, 0.0f, 1.0f);
    }
  }

  for (unsigned int f = 0; f < mesh.mNumFaces; ++f) {
    const aiFace& face = mesh.mFaces[f];
    if (face.mNumIndices != 3) {
      continue;
    }
    out.indices.push_back(face.mIndices[0]);
    out.indices.push_back(face.mIndices[1]);
    out.indices.push_back(face.mIndices[2]);
  }

  return out;
}

renderer::MaterialDesc buildMaterialDesc(const aiMaterial& material) {
  renderer::MaterialDesc desc{};
  desc.base_color = {1.0f, 1.0f, 1.0f, 1.0f};

  aiColor4D base_color(1.0f, 1.0f, 1.0f, 1.0f);
  if (material.Get(AI_MATKEY_BASE_COLOR, base_color) == AI_SUCCESS) {
    desc.base_color = {base_color.r, base_color.g, base_color.b, base_color.a};
  } else {
    aiColor3D diffuse(1.0f, 1.0f, 1.0f);
    if (material.Get(AI_MATKEY_COLOR_DIFFUSE, diffuse) == AI_SUCCESS) {
      desc.base_color = {diffuse.r, diffuse.g, diffuse.b, 1.0f};
    }
  }

  float opacity = desc.base_color.a;
  if (material.Get(AI_MATKEY_OPACITY, opacity) == AI_SUCCESS) {
    desc.base_color.a = opacity;
  }

  int two_sided = 0;
  if (material.Get(AI_MATKEY_TWOSIDED, two_sided) == AI_SUCCESS) {
    desc.double_sided = two_sided != 0;
  }

  desc.transparent = desc.base_color.a < 0.999f;
  return desc;
}

renderer::MaterialDesc buildMaterialDesc(const aiScene& scene, const aiMesh& mesh) {
  if (mesh.mMaterialIndex >= scene.mNumMaterials ||
      scene.mMaterials[mesh.mMaterialIndex] == nullptr) {
    return {};
  }
  return buildMaterialDesc(*scene.mMaterials[mesh.mMaterialIndex]);
}

enum ImportedTextureCoordSlot : size_t {
  kTexCoordBaseColor = 0,
  kTexCoordNormal = 1,
  kTexCoordMetallicRoughness = 2,
  kTexCoordOcclusion = 3,
  kTexCoordEmissive = 4,
  kTexCoordClearcoat = 5,
  kTexCoordClearcoatRoughness = 6,
  kTexCoordClearcoatNormal = 7,
  kTexCoordSheenColor = 8,
  kTexCoordSheenRoughness = 9,
  kTexCoordTransmission = 10,
  kTexCoordThickness = 11,
};

int embeddedTextureIndex(const std::string& raw_key) {
  if (raw_key.size() < 2 || raw_key[0] != '*') {
    return -1;
  }
  char* end = nullptr;
  const long parsed = std::strtol(raw_key.c_str() + 1, &end, 10);
  if (end == nullptr || *end != '\0' || parsed < 0 ||
      parsed > static_cast<long>(std::numeric_limits<int>::max())) {
    return -1;
  }
  return static_cast<int>(parsed);
}

void setImportedTextureCoordTransform(renderer::ImportedMaterialData& data,
                                      const aiMaterial& material,
                                      unsigned int texture_type,
                                      unsigned int texture_index,
                                      unsigned int uv_index,
                                      size_t slot) {
  if (slot >= renderer::kImportedMaterialTextureCoordSlotCount) {
    return;
  }

  const auto type = static_cast<aiTextureType>(texture_type);
  aiUVTransform transform;
  transform.mTranslation = aiVector2D(0.0f, 0.0f);
  transform.mScaling = aiVector2D(1.0f, 1.0f);
  transform.mRotation = 0.0f;
  material.Get(AI_MATKEY_UVTRANSFORM(type, texture_index), transform);

  const float sx = transform.mScaling.x;
  const float sy = transform.mScaling.y;
  const float c = std::cos(transform.mRotation);
  const float s = std::sin(transform.mRotation);
  const float tx = transform.mTranslation.x;
  const float ty = transform.mTranslation.y;

  data.texcoord_row0[slot] =
      glm::vec4(c * sx, -s * sy, -0.5f * c + 0.5f * s + 0.5f + tx,
                uv_index > 0u ? 1.0f : 0.0f);
  data.texcoord_row1[slot] =
      glm::vec4(s * sx, c * sy, -0.5f * s - 0.5f * c + 0.5f + ty, 0.0f);
}

bool appendImportedTexture(renderer::ImportedMaterialData& data,
                           const aiScene& scene,
                           const aiMaterial& material,
                           const std::filesystem::path& asset_path,
                           aiTextureType type,
                           unsigned int texture_index,
                           renderer::ImportedMaterialTextureSemantic semantic,
                           bool srgb,
                           const char* label,
                           size_t texcoord_slot) {
  aiString tex_path;
  aiTextureMapping mapping = aiTextureMapping_UV;
  unsigned int uv_index = 0;
  float blend = 1.0f;
  aiTextureOp op = aiTextureOp_Multiply;
  aiTextureMapMode mapmode[2] = {aiTextureMapMode_Wrap, aiTextureMapMode_Wrap};
  if (material.GetTexture(type, texture_index, &tex_path, &mapping, &uv_index, &blend, &op,
                          mapmode) != AI_SUCCESS ||
      tex_path.length == 0) {
    return false;
  }

  const std::string raw_key = tex_path.C_Str();
  const bool embedded = !raw_key.empty() && raw_key[0] == '*';
  renderer::ImportedMaterialTexture texture{};
  texture.semantic = semantic;
  texture.raw_name = raw_key;
  texture.label = label ? label : "importedTexture";
  texture.embedded = embedded;
  texture.srgb = srgb;

  if (embedded) {
    const int texture_idx = embeddedTextureIndex(raw_key);
    if (texture_idx < 0 || texture_idx >= static_cast<int>(scene.mNumTextures) ||
        scene.mTextures[texture_idx] == nullptr) {
      return false;
    }
    texture.source_key = asset_path.string() + ":" + raw_key;
    const aiTexture& embedded_texture = *scene.mTextures[texture_idx];
    if (embedded_texture.mHeight == 0) {
      texture.compressed = true;
      texture.source_bytes.resize(static_cast<size_t>(embedded_texture.mWidth));
      if (!texture.source_bytes.empty()) {
        std::memcpy(texture.source_bytes.data(),
                    embedded_texture.pcData,
                    texture.source_bytes.size());
      }
    } else {
      texture.compressed = false;
      texture.width = embedded_texture.mWidth;
      texture.height = embedded_texture.mHeight;
      const uint64_t byte_count = static_cast<uint64_t>(texture.width) *
                                  static_cast<uint64_t>(texture.height) * 4u;
      if (byte_count > static_cast<uint64_t>(std::numeric_limits<size_t>::max())) {
        return false;
      }
      texture.source_bytes.resize(static_cast<size_t>(byte_count));
      if (!texture.source_bytes.empty()) {
        std::memcpy(texture.source_bytes.data(),
                    embedded_texture.pcData,
                    texture.source_bytes.size());
      }
    }
  } else {
    texture.resolved_path = asset_path.parent_path() / raw_key;
    texture.source_key = texture.resolved_path.string();
  }

  setImportedTextureCoordTransform(data, material, type, texture_index, uv_index, texcoord_slot);
  data.textures.push_back(std::move(texture));
  return true;
}

renderer::ImportedMaterialData buildImportedMaterialData(const aiScene& scene,
                                                         const aiMaterial& material,
                                                         const std::filesystem::path& asset_path) {
  renderer::ImportedMaterialData data{};
  data.material = buildMaterialDesc(material);

  aiColor3D emissive(0.0f, 0.0f, 0.0f);
  if (material.Get(AI_MATKEY_COLOR_EMISSIVE, emissive) == AI_SUCCESS) {
    data.material.emissive_color = {emissive.r, emissive.g, emissive.b, 1.0f};
  }
  if (float emissive_strength = 1.0f;
      material.Get(AI_MATKEY_EMISSIVE_INTENSITY, emissive_strength) == AI_SUCCESS) {
    data.material.emissive_strength = emissive_strength;
  }
  if (float metallic = 1.0f;
      material.Get(AI_MATKEY_METALLIC_FACTOR, metallic) == AI_SUCCESS) {
    data.material.metallic = metallic;
  }
  if (float roughness = 1.0f;
      material.Get(AI_MATKEY_ROUGHNESS_FACTOR, roughness) == AI_SUCCESS) {
    data.material.roughness = roughness;
  }
  if (float normal_scale = 1.0f;
      material.Get(AI_MATKEY_TEXBLEND_NORMALS(0), normal_scale) == AI_SUCCESS) {
    data.material.normal_scale = normal_scale;
  }
  float occlusion_strength = 1.0f;
  if (material.Get(AI_MATKEY_TEXBLEND(aiTextureType_AMBIENT_OCCLUSION, 0),
                   occlusion_strength) == AI_SUCCESS ||
      material.Get(AI_MATKEY_TEXBLEND_LIGHTMAP(0), occlusion_strength) == AI_SUCCESS) {
    data.material.occlusion_strength = occlusion_strength;
  }
  if (float clearcoat = 0.0f;
      material.Get(AI_MATKEY_CLEARCOAT_FACTOR, clearcoat) == AI_SUCCESS) {
    data.material.clearcoat = clearcoat;
  }
  if (float clearcoat_roughness = 0.0f;
      material.Get(AI_MATKEY_CLEARCOAT_ROUGHNESS_FACTOR, clearcoat_roughness) == AI_SUCCESS) {
    data.material.clearcoat_roughness = clearcoat_roughness;
  }
  if (aiColor3D sheen_color(0.0f, 0.0f, 0.0f);
      material.Get(AI_MATKEY_SHEEN_COLOR_FACTOR, sheen_color) == AI_SUCCESS) {
    data.material.sheen_color = {sheen_color.r, sheen_color.g, sheen_color.b, 1.0f};
  }
  if (float sheen_roughness = 0.0f;
      material.Get(AI_MATKEY_SHEEN_ROUGHNESS_FACTOR, sheen_roughness) == AI_SUCCESS) {
    data.material.sheen_roughness = sheen_roughness;
  }
  if (float anisotropy = 0.0f;
      material.Get(AI_MATKEY_ANISOTROPY_FACTOR, anisotropy) == AI_SUCCESS) {
    data.material.anisotropy = anisotropy;
  }
  if (float transmission = 0.0f;
      material.Get(AI_MATKEY_TRANSMISSION_FACTOR, transmission) == AI_SUCCESS) {
    data.material.transmission = transmission;
    if (transmission > 0.001f) {
      data.material.transparent = true;
    }
  }
  if (float ior = 1.5f; material.Get(AI_MATKEY_REFRACTI, ior) == AI_SUCCESS) {
    data.material.ior = ior;
  }
  if (float thickness = 0.0f;
      material.Get(AI_MATKEY_VOLUME_THICKNESS_FACTOR, thickness) == AI_SUCCESS) {
    data.material.thickness = thickness;
  }
  if (float attenuation_distance = std::numeric_limits<float>::infinity();
      material.Get(AI_MATKEY_VOLUME_ATTENUATION_DISTANCE, attenuation_distance) == AI_SUCCESS) {
    data.material.attenuation_distance = attenuation_distance;
  }
  if (aiColor3D attenuation_color(1.0f, 1.0f, 1.0f);
      material.Get(AI_MATKEY_VOLUME_ATTENUATION_COLOR, attenuation_color) == AI_SUCCESS) {
    data.material.attenuation_color =
        {attenuation_color.r, attenuation_color.g, attenuation_color.b, 1.0f};
  }

  using Semantic = renderer::ImportedMaterialTextureSemantic;
  if (!appendImportedTexture(data, scene, material, asset_path, aiTextureType_BASE_COLOR, 0,
                             Semantic::BaseColor, true, "baseColor", kTexCoordBaseColor)) {
    appendImportedTexture(data, scene, material, asset_path, aiTextureType_DIFFUSE, 0,
                          Semantic::BaseColor, true, "baseColor", kTexCoordBaseColor);
  }
  appendImportedTexture(data, scene, material, asset_path, aiTextureType_NORMALS, 0,
                        Semantic::Normal, false, "normal", kTexCoordNormal);
  if (!appendImportedTexture(data, scene, material, asset_path, aiTextureType_METALNESS, 0,
                             Semantic::MetallicRoughness, false, "metallicRoughness",
                             kTexCoordMetallicRoughness)) {
    appendImportedTexture(data, scene, material, asset_path, aiTextureType_DIFFUSE_ROUGHNESS, 0,
                          Semantic::MetallicRoughness, false, "metallicRoughness",
                          kTexCoordMetallicRoughness);
  }
  if (!appendImportedTexture(data, scene, material, asset_path, aiTextureType_AMBIENT_OCCLUSION, 0,
                             Semantic::Occlusion, false, "occlusion", kTexCoordOcclusion)) {
    appendImportedTexture(data, scene, material, asset_path, aiTextureType_LIGHTMAP, 0,
                          Semantic::Occlusion, false, "occlusion", kTexCoordOcclusion);
  }
  appendImportedTexture(data, scene, material, asset_path, aiTextureType_EMISSIVE, 0,
                        Semantic::Emissive, true, "emissive", kTexCoordEmissive);
  appendImportedTexture(data, scene, material, asset_path, aiTextureType_CLEARCOAT, 0,
                        Semantic::Clearcoat, false, "clearcoat", kTexCoordClearcoat);
  appendImportedTexture(data, scene, material, asset_path, aiTextureType_CLEARCOAT, 1,
                        Semantic::ClearcoatRoughness, false, "clearcoatRoughness",
                        kTexCoordClearcoatRoughness);
  appendImportedTexture(data, scene, material, asset_path, aiTextureType_CLEARCOAT, 2,
                        Semantic::ClearcoatNormal, false, "clearcoatNormal",
                        kTexCoordClearcoatNormal);
  appendImportedTexture(data, scene, material, asset_path, aiTextureType_SHEEN, 0,
                        Semantic::SheenColor, true, "sheenColor", kTexCoordSheenColor);
  appendImportedTexture(data, scene, material, asset_path, aiTextureType_SHEEN, 1,
                        Semantic::SheenRoughness, false, "sheenRoughness",
                        kTexCoordSheenRoughness);
  appendImportedTexture(data, scene, material, asset_path, aiTextureType_TRANSMISSION, 0,
                        Semantic::Transmission, false, "transmission", kTexCoordTransmission);
  appendImportedTexture(data, scene, material, asset_path, aiTextureType_TRANSMISSION, 1,
                        Semantic::Thickness, false, "thickness", kTexCoordThickness);
  return data;
}

float estimateLightRange(const aiLight& light) {
  if (light.mAttenuationLinear > 1e-5f) {
    return std::max(1.0f, 1.0f / light.mAttenuationLinear);
  }
  if (light.mAttenuationQuadratic > 1e-5f &&
      (light.mAttenuationConstant > 1e-5f || light.mAttenuationLinear > 1e-5f ||
       std::abs(light.mAttenuationQuadratic - 1.0f) > 1e-5f)) {
    return std::max(1.0f, std::sqrt(1.0f / light.mAttenuationQuadratic));
  }
  return 10.0f;
}

components::LightComponent buildLightComponent(const aiLight& light) {
  constexpr float kRadiansToDegrees = 57.29577951308232f;
  constexpr float kDirectionalIntensityScale = 1.0f / 700.0f;
  constexpr float kLocalLightIntensityScale = 1.0f / 50.0f;
  constexpr float kLocalLightCutoffIntensity = 0.05f;
  components::LightComponent out{};
  float intensity_scale = kLocalLightIntensityScale;

  switch (light.mType) {
    case aiLightSource_DIRECTIONAL:
      out.type = components::LightComponent::Type::Directional;
      intensity_scale = kDirectionalIntensityScale;
      break;
    case aiLightSource_SPOT:
      out.type = components::LightComponent::Type::Spot;
      out.casts_shadows = true;
      out.inner_cone_degrees = light.mAngleInnerCone * kRadiansToDegrees;
      out.outer_cone_degrees = light.mAngleOuterCone * kRadiansToDegrees;
      break;
    case aiLightSource_POINT:
    default:
      out.type = components::LightComponent::Type::Point;
      out.casts_shadows = true;
      break;
  }

  math::Color diffuse{
      light.mColorDiffuse.r,
      light.mColorDiffuse.g,
      light.mColorDiffuse.b,
      1.0f};
  const float max_channel =
      std::max(diffuse.r, std::max(diffuse.g, std::max(diffuse.b, 0.0f)));
  if (max_channel > 1e-5f) {
    out.color = {diffuse.r / max_channel, diffuse.g / max_channel, diffuse.b / max_channel, 1.0f};
    out.intensity = max_channel * intensity_scale;
  } else {
    out.color = {1.0f, 1.0f, 1.0f, 1.0f};
    out.intensity = 1.0f;
  }

  if (out.type == components::LightComponent::Type::Point ||
      out.type == components::LightComponent::Type::Spot) {
    const bool gltf_default_quadratic =
        light.mAttenuationQuadratic > 1e-5f &&
        light.mAttenuationConstant <= 1e-5f &&
        light.mAttenuationLinear <= 1e-5f &&
        std::abs(light.mAttenuationQuadratic - 1.0f) <= 1e-5f;
    if (gltf_default_quadratic) {
      out.range = std::clamp(std::sqrt(std::max(out.intensity, 0.0f) / kLocalLightCutoffIntensity),
                             4.0f,
                             40.0f);
    } else {
      out.range = estimateLightRange(light);
    }
  }

  return out;
}

void decomposeTransform(const aiMatrix4x4& transform,
                        math::Vec3& position,
                        math::Quat& rotation,
                        math::Vec3& scale) {
  aiVector3D ai_scale;
  aiQuaternion ai_rotation;
  aiVector3D ai_position;
  transform.Decompose(ai_scale, ai_rotation, ai_position);
  position = toVec3(ai_position);
  rotation = toQuat(ai_rotation);
  scale = toVec3(ai_scale);
}

uint32_t loadNodePrefab(const aiScene& scene,
                        const aiNode& node,
                        const aiMatrix4x4& parent_world,
                        const std::unordered_map<std::string, const aiLight*>& lights_by_name,
                        const GlbSceneLoadOptions& options,
                        GlbScenePrefab& prefab,
                        std::unordered_map<std::string, uint32_t>& node_indices_by_name) {
  const aiMatrix4x4 world_transform = parent_world * node.mTransformation;

  GlbScenePrefabNode prefab_node{};
  prefab_node.name = node.mName.C_Str();
  decomposeTransform(node.mTransformation,
                     prefab_node.local_position,
                     prefab_node.local_rotation,
                     prefab_node.local_scale);
  decomposeTransform(world_transform,
                     prefab_node.world_position,
                     prefab_node.world_rotation,
                     prefab_node.world_scale);

  if (options.import_meshes) {
    prefab_node.primitives.reserve(node.mNumMeshes);
    for (unsigned int mesh_index = 0; mesh_index < node.mNumMeshes; ++mesh_index) {
      const unsigned int scene_mesh_index = node.mMeshes[mesh_index];
      if (scene_mesh_index >= scene.mNumMeshes || scene.mMeshes[scene_mesh_index] == nullptr) {
        continue;
      }
      const aiMesh& mesh = *scene.mMeshes[scene_mesh_index];
      GlbScenePrefabPrimitive primitive{};
      primitive.name = safeName(mesh.mName.C_Str(), "Primitive");
      primitive.mesh = buildMeshData(mesh);
      primitive.source_material_index =
          mesh.mMaterialIndex < scene.mNumMaterials ? mesh.mMaterialIndex : kInvalidGlbSceneMaterial;
      if (primitive.source_material_index < prefab.imported_materials.size() &&
          prefab.imported_materials[primitive.source_material_index]) {
        primitive.material = prefab.imported_materials[primitive.source_material_index]->material;
      } else {
        primitive.material = buildMaterialDesc(scene, mesh);
      }
      primitive.source_mesh_index = scene_mesh_index;
      prefab_node.primitives.push_back(std::move(primitive));
    }
  }

  if (options.import_lights) {
    const auto light_it = lights_by_name.find(node.mName.C_Str());
    if (light_it != lights_by_name.end() && light_it->second != nullptr) {
      prefab_node.has_light = true;
      prefab_node.light = buildLightComponent(*light_it->second);
    }
  }

  const uint32_t node_index = static_cast<uint32_t>(prefab.nodes.size());
  prefab.nodes.push_back(std::move(prefab_node));
  node_indices_by_name.try_emplace(node.mName.C_Str(), node_index);

  prefab.nodes[node_index].children.reserve(node.mNumChildren);
  for (unsigned int child_index = 0; child_index < node.mNumChildren; ++child_index) {
    const aiNode* child = node.mChildren[child_index];
    if (child == nullptr) {
      continue;
    }
    const uint32_t imported_child =
        loadNodePrefab(scene,
                       *child,
                       world_transform,
                       lights_by_name,
                       options,
                       prefab,
                       node_indices_by_name);
    if (imported_child != kInvalidGlbSceneNode) {
      prefab.nodes[node_index].children.push_back(imported_child);
    }
  }

  return node_index;
}

std::string nodeDisplayName(const GlbScenePrefabNode& node, uint32_t index) {
  if (!node.name.empty()) {
    return node.name;
  }
  return "GLB Node " + std::to_string(index);
}

std::string primitiveDisplayName(const GlbScenePrefabNode& node,
                                 uint32_t node_index,
                                 const GlbScenePrefabPrimitive& primitive,
                                 size_t primitive_index) {
  if (!primitive.name.empty()) {
    return primitive.name;
  }
  return nodeDisplayName(node, node_index) + " Primitive " + std::to_string(primitive_index);
}

std::string prefabResourceKey(const GlbScenePrefab& prefab,
                              uint32_t node_index,
                              size_t primitive_index,
                              std::string_view suffix) {
  std::string key = prefab.source_path.empty() ? std::string("imported_glb")
                                               : prefab.source_path.string();
  key.append("#node=");
  key.append(std::to_string(node_index));
  key.append("/primitive=");
  key.append(std::to_string(primitive_index));
  key.push_back('/');
  key.append(suffix);
  return key;
}

std::string materialResourceKey(const GlbScenePrefab& prefab, uint32_t material_index) {
  std::string key = prefab.source_path.empty() ? std::string("imported_glb")
                                               : prefab.source_path.string();
  key.append("#material=");
  key.append(std::to_string(material_index));
  return key;
}

std::string fallbackPrimitiveMaterialKey(const GlbScenePrefab& prefab,
                                         uint32_t node_index,
                                         size_t primitive_index) {
  return prefabResourceKey(prefab, node_index, primitive_index, "material");
}

std::string primitiveMaterialKey(const GlbScenePrefab& prefab,
                                 uint32_t node_index,
                                 size_t primitive_index,
                                 const GlbScenePrefabPrimitive& primitive) {
  if (primitive.source_material_index != kInvalidGlbSceneMaterial) {
    return materialResourceKey(prefab, primitive.source_material_index);
  }
  return fallbackPrimitiveMaterialKey(prefab, node_index, primitive_index);
}

void registerPrimitiveMaterial(const GlbScenePrefab& prefab,
                               uint32_t node_index,
                               size_t primitive_index,
                               const GlbScenePrefabPrimitive& primitive,
                               const std::string& material_key,
                               renderer::MaterialLibrary* materials,
                               std::unordered_set<std::string>& registered_materials) {
  if (materials == nullptr || material_key.empty() ||
      !registered_materials.insert(material_key).second) {
    return;
  }

  if (primitive.source_material_index != kInvalidGlbSceneMaterial &&
      !prefab.source_path.empty()) {
    std::shared_ptr<const renderer::ImportedMaterialData> imported_material;
    if (primitive.source_material_index < prefab.imported_materials.size()) {
      imported_material = prefab.imported_materials[primitive.source_material_index];
    }
    materials->registerImportedAssetMaterial(material_key,
                                             prefab.source_path,
                                             primitive.source_material_index,
                                             primitive.material,
                                             std::move(imported_material));
  } else {
    (void)node_index;
    (void)primitive_index;
    materials->registerMaterialDesc(material_key, primitive.material);
  }
}

void appendPrimitiveMesh(geometry::MeshData& out,
                         const geometry::MeshData& primitive,
                         uint32_t material_slot) {
  const uint32_t base_vertex = static_cast<uint32_t>(out.vertices.size());
  out.vertices.insert(out.vertices.end(), primitive.vertices.begin(), primitive.vertices.end());
  out.normals.insert(out.normals.end(), primitive.normals.begin(), primitive.normals.end());
  out.uvs.insert(out.uvs.end(), primitive.uvs.begin(), primitive.uvs.end());
  out.uvs1.insert(out.uvs1.end(), primitive.uvs1.begin(), primitive.uvs1.end());
  out.tangents.insert(out.tangents.end(), primitive.tangents.begin(), primitive.tangents.end());

  const uint32_t index_offset = static_cast<uint32_t>(out.indices.size());
  for (const uint32_t index : primitive.indices) {
    out.indices.push_back(base_vertex + index);
  }
  const uint32_t index_count = static_cast<uint32_t>(out.indices.size()) - index_offset;
  if (index_count > 0) {
    out.submeshes.push_back(geometry::MeshSubmesh{
        .index_offset = index_offset,
        .index_count = index_count,
        .material_slot = material_slot,
    });
  }
}

geometry::MeshData buildCombinedNodeMesh(const GlbScenePrefab& prefab,
                                         uint32_t node_index,
                                         const GlbScenePrefabNode& node,
                                         const std::vector<size_t>& primitive_indices,
                                         renderer::MaterialLibrary* materials,
                                         std::unordered_set<std::string>& registered_materials) {
  geometry::MeshData combined{};
  std::unordered_map<std::string, uint32_t> slots_by_material_key;
  slots_by_material_key.reserve(primitive_indices.size());

  for (const size_t primitive_index : primitive_indices) {
    const auto& primitive = node.primitives[primitive_index];
    const std::string material_key =
        primitiveMaterialKey(prefab, node_index, primitive_index, primitive);
    registerPrimitiveMaterial(prefab,
                              node_index,
                              primitive_index,
                              primitive,
                              material_key,
                              materials,
                              registered_materials);

    auto slot_it = slots_by_material_key.find(material_key);
    if (slot_it == slots_by_material_key.end()) {
      const uint32_t slot = static_cast<uint32_t>(combined.material_slots.size());
      std::string slot_name =
          primitive.name.empty() ? ("Slot " + std::to_string(slot)) : primitive.name;
      combined.material_slots.push_back(geometry::MeshMaterialSlot{
          .name = std::move(slot_name),
          .default_material_key = materials != nullptr ? material_key : std::string{},
      });
      slot_it = slots_by_material_key.emplace(material_key, slot).first;
    }
    appendPrimitiveMesh(combined, primitive.mesh, slot_it->second);
  }

  return combined;
}
}  // namespace

GlbScenePrefab loadGlbScenePrefab(const std::filesystem::path& path,
                                  const GlbSceneLoadOptions& options) {
  GlbScenePrefab prefab{};
  prefab.source_path = path;

  Assimp::Importer importer;
  const aiScene* scene = importer.ReadFile(path.string(),
                                           aiProcess_Triangulate |
                                           aiProcess_GenNormals |
                                           aiProcess_CalcTangentSpace);
  if (scene == nullptr || scene->mRootNode == nullptr) {
    return prefab;
  }

  prefab.imported_materials.reserve(scene->mNumMaterials);
  for (unsigned int i = 0; i < scene->mNumMaterials; ++i) {
    if (scene->mMaterials[i] == nullptr) {
      prefab.imported_materials.push_back({});
      continue;
    }
    prefab.imported_materials.push_back(std::make_shared<renderer::ImportedMaterialData>(
        buildImportedMaterialData(*scene, *scene->mMaterials[i], path)));
  }

  std::unordered_map<std::string, const aiLight*> lights_by_name;
  lights_by_name.reserve(scene->mNumLights);
  for (unsigned int i = 0; i < scene->mNumLights; ++i) {
    const aiLight* light = scene->mLights[i];
    if (light == nullptr) {
      continue;
    }
    lights_by_name[light->mName.C_Str()] = light;
  }

  std::unordered_map<std::string, uint32_t> node_indices_by_name;
  node_indices_by_name.reserve(128);
  prefab.root_node = loadNodePrefab(*scene,
                                    *scene->mRootNode,
                                    aiMatrix4x4{},
                                    lights_by_name,
                                    options,
                                    prefab,
                                    node_indices_by_name);
  const GltfDocument gltf = loadGltfDocument(path);
  populateGltfMeshData(gltf, node_indices_by_name, prefab);
  populateGltfSkins(gltf, node_indices_by_name, prefab);
  populatePrimitiveSkinning(*scene, node_indices_by_name, prefab);
  prefab.animations = loadGltfAnimationClips(gltf, node_indices_by_name, prefab);
  if (prefab.animations.empty()) {
    prefab.animations = loadAnimationClips(*scene, node_indices_by_name);
  }
  return prefab;
}

GlbSceneImportResult instantiateGlbScenePrefab(
    ecs::World& world,
    scene::Scene& scene,
    renderer::GraphicsDevice& device,
    const GlbScenePrefab& prefab,
    const GlbSceneInstantiateOptions& options,
    renderer::MaterialLibrary* materials) {
  GlbSceneImportResult result{};
  if (!prefab.valid()) {
    return result;
  }

  result.node_entities_by_index.resize(prefab.nodes.size());
  result.morph_entities_by_node_index.resize(prefab.nodes.size());

  struct PendingSkin {
    ecs::Entity entity{};
    const GlbScenePrefabPrimitive* primitive = nullptr;
  };
  std::vector<PendingSkin> pending_skins;
  ecs::Entity skin_render_transform_entity{};
  std::unordered_set<std::string> registered_materials;

  auto attach_pending_skins = [&]() {
    for (const PendingSkin& pending : pending_skins) {
      if (!world.isAlive(pending.entity) || pending.primitive == nullptr ||
          !pending.primitive->skinned()) {
        continue;
      }
      std::vector<ecs::Entity> joint_entities;
      joint_entities.reserve(pending.primitive->joint_node_indices.size());
      for (const uint32_t joint_node_index : pending.primitive->joint_node_indices) {
        if (joint_node_index < result.node_entities_by_index.size()) {
          joint_entities.push_back(result.node_entities_by_index[joint_node_index]);
        } else {
          joint_entities.push_back({});
        }
      }
      world.add(pending.entity, components::SkinnedMeshComponent{
                                    .bind_mesh = pending.primitive->mesh,
                                    .skinned_mesh = pending.primitive->mesh,
                                    .vertex_influences = pending.primitive->vertex_influences,
                                    .joint_entities = std::move(joint_entities),
                                    .inverse_bind_matrices = pending.primitive->inverse_bind_matrices,
                                    .render_transform_entity = skin_render_transform_entity,
                                    .skin_index = pending.primitive->skin_index,
                                    .skinning_path = components::SkinningPath::Gpu,
                                    .diagnostic = "Waiting for first skinning palette update",
                                    .override_render_transform = true,
                                    .enabled = true});
    }
  };

  std::function<std::pair<ecs::Entity, scene::NodeId>(uint32_t, scene::NodeId)> instantiate_node;
  instantiate_node = [&](uint32_t prefab_node_index,
                         scene::NodeId parent_node) -> std::pair<ecs::Entity, scene::NodeId> {
    const auto& prefab_node = prefab.nodes[prefab_node_index];
    const ecs::Entity entity = world.createEntity();
    world.setName(entity, nodeDisplayName(prefab_node, prefab_node_index));
    world.add(entity, components::TransformComponent{
                           prefab_node.local_position,
                           prefab_node.local_rotation,
                           prefab_node.local_scale});
    if (prefab_node.has_light) {
      world.add(entity, prefab_node.light);
    }

    const scene::NodeId node_id = scene.createNode(entity);
    if (parent_node != scene::Node::kInvalidId) {
      scene.reparent(node_id, parent_node);
    }
    result.entities.push_back(entity);
    if (prefab_node_index < result.node_entities_by_index.size()) {
      result.node_entities_by_index[prefab_node_index] = entity;
    }

    std::vector<size_t> combined_primitive_indices;
    combined_primitive_indices.reserve(prefab_node.primitives.size());
    for (size_t primitive_index = 0; primitive_index < prefab_node.primitives.size(); ++primitive_index) {
      const auto& primitive = prefab_node.primitives[primitive_index];
      if (!primitive.skinned() && !primitive.morphable()) {
        combined_primitive_indices.push_back(primitive_index);
        continue;
      }

      const ecs::Entity primitive_entity = world.createEntity();
      world.setName(primitive_entity,
                    primitiveDisplayName(prefab_node, prefab_node_index, primitive, primitive_index));
      world.add(primitive_entity, components::TransformComponent{
                                     {},
                                     {},
                                     {1.0f, 1.0f, 1.0f}});

      const std::string mesh_key =
          prefabResourceKey(prefab, prefab_node_index, primitive_index, "mesh");
      const std::string material_key =
          primitiveMaterialKey(prefab, prefab_node_index, primitive_index, primitive);
      registerPrimitiveMaterial(prefab,
                                prefab_node_index,
                                primitive_index,
                                primitive,
                                material_key,
                                materials,
                                registered_materials);
      geometry::MeshData mesh_asset = primitive.mesh;
      mesh_asset.material_slots = {geometry::MeshMaterialSlot{
          .name = primitive.name.empty() ? std::string("Slot 0") : primitive.name,
          .default_material_key = materials != nullptr ? material_key : std::string{},
      }};
      if (mesh_asset.submeshes.empty() && !mesh_asset.indices.empty()) {
        mesh_asset.submeshes.push_back(geometry::MeshSubmesh{
            .index_offset = 0,
            .index_count = static_cast<uint32_t>(mesh_asset.indices.size()),
            .material_slot = 0,
        });
      } else {
        for (auto& submesh : mesh_asset.submeshes) {
          submesh.material_slot = 0;
        }
      }
      const renderer::MeshId mesh_id = device.registerRuntimeMesh(mesh_key, mesh_asset);
      world.add(primitive_entity, components::MeshComponent{
                                     .mesh_key = mesh_id != renderer::kInvalidMesh ? mesh_key : "",
                                     .visible = true});
      if (primitive.morphable()) {
        std::vector<float> morph_weights = primitive.morph_weights;
        morph_weights.resize(mesh_asset.morph_targets.size(), 0.0f);
        world.add(primitive_entity, components::MorphTargetComponent{
                                        .bind_mesh = mesh_asset,
                                        .deformed_mesh = mesh_asset,
                                        .base_weights = morph_weights,
                                        .weights = morph_weights,
                                        .weights_dirty = true,
                                        .enabled = true});
        if (prefab_node_index < result.morph_entities_by_node_index.size()) {
          result.morph_entities_by_node_index[prefab_node_index].push_back(primitive_entity);
        }
      }
      if (primitive.skinned()) {
        pending_skins.push_back(PendingSkin{.entity = primitive_entity, .primitive = &primitive});
      }

      const scene::NodeId primitive_node = scene.createNode(primitive_entity);
      scene.reparent(primitive_node, node_id);
      result.entities.push_back(primitive_entity);
    }

    if (!combined_primitive_indices.empty()) {
      const ecs::Entity mesh_entity = world.createEntity();
      world.setName(mesh_entity, nodeDisplayName(prefab_node, prefab_node_index) + " Mesh");
      world.add(mesh_entity, components::TransformComponent{
                               {},
                               {},
                               {1.0f, 1.0f, 1.0f}});
      const std::string mesh_key =
          prefabResourceKey(prefab, prefab_node_index, 0, "mesh");
      geometry::MeshData combined_mesh = buildCombinedNodeMesh(prefab,
                                                               prefab_node_index,
                                                               prefab_node,
                                                               combined_primitive_indices,
                                                               materials,
                                                               registered_materials);
      const renderer::MeshId mesh_id = device.registerRuntimeMesh(mesh_key, combined_mesh);
      world.add(mesh_entity, components::MeshComponent{
                                 .mesh_key = mesh_id != renderer::kInvalidMesh ? mesh_key : "",
                                 .visible = true});

      const scene::NodeId mesh_node = scene.createNode(mesh_entity);
      scene.reparent(mesh_node, node_id);
      result.entities.push_back(mesh_entity);
    }

    for (const uint32_t child_index : prefab_node.children) {
      instantiate_node(child_index, node_id);
    }

    return {entity, node_id};
  };

  if (options.create_synthetic_root) {
    const ecs::Entity root_entity = world.createEntity();
    const std::string root_name =
        prefab.source_path.stem().empty() ? std::string("Imported GLB") : prefab.source_path.stem().string();
    world.setName(root_entity, root_name);
    world.add(root_entity, components::TransformComponent{});
    const scene::NodeId root_node = scene.createNode(root_entity);
    result.entities.push_back(root_entity);
    skin_render_transform_entity = root_entity;
    instantiate_node(prefab.root_node, root_node);
    result.root_entity = root_entity;
    result.root_node = root_node;
    attach_pending_skins();
    if (!prefab.animations.empty()) {
      world.add(root_entity, components::AnimatorComponent{
                                 .clips = prefab.animations,
                                 .node_entities_by_index = result.node_entities_by_index,
                                 .morph_entities_by_node_index =
                                     result.morph_entities_by_node_index,
                                 .skeletons = prefab.skeletons,
                                 .skins = prefab.skins,
                                 .current_clip_index = 0,
                                 .time_seconds = 0.0f,
                                 .speed = 1.0f,
                                 .loop = true,
                                 .playing = options.autoplay_animations});
    }
    updateWorldTransforms(world, scene);
    return result;
  }

  const auto [root_entity, root_node] = instantiate_node(prefab.root_node, scene::Node::kInvalidId);
  result.root_entity = root_entity;
  result.root_node = root_node;
  skin_render_transform_entity = root_entity;
  attach_pending_skins();
  if (!prefab.animations.empty()) {
    world.add(root_entity, components::AnimatorComponent{
                               .clips = prefab.animations,
                               .node_entities_by_index = result.node_entities_by_index,
                               .morph_entities_by_node_index =
                                   result.morph_entities_by_node_index,
                               .skeletons = prefab.skeletons,
                               .skins = prefab.skins,
                               .current_clip_index = 0,
                               .time_seconds = 0.0f,
                               .speed = 1.0f,
                               .loop = true,
                               .playing = options.autoplay_animations});
  }
  updateWorldTransforms(world, scene);
  return result;
}

GlbSceneImportResult importGlbScene(ecs::World& world,
                                    scene::Scene& scene,
                                    renderer::GraphicsDevice& device,
                                    const std::filesystem::path& path,
                                    const GlbSceneImportOptions& options,
                                    renderer::MaterialLibrary* materials) {
  const GlbScenePrefab prefab = loadGlbScenePrefab(path, options.load);
  return instantiateGlbScenePrefab(world, scene, device, prefab, options.instantiate, materials);
}

}  // namespace karma::scene
