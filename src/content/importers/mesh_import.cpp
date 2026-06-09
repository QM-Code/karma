#include "karma/content/importers/mesh_import.h"

#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

#include <utility>

namespace karma::content {

std::vector<geometry::MeshData> importMeshes(const std::filesystem::path& path) {
  std::vector<geometry::MeshData> meshes;

  Assimp::Importer importer;
  const aiScene* scene = importer.ReadFile(path.string(),
                                           aiProcess_Triangulate |
                                           aiProcess_GenNormals |
                                           aiProcess_CalcTangentSpace |
                                           aiProcess_JoinIdenticalVertices);
  if (scene == nullptr) {
    return meshes;
  }

  meshes.reserve(scene->mNumMeshes);
  for (unsigned int m = 0; m < scene->mNumMeshes; ++m) {
    const aiMesh* mesh = scene->mMeshes[m];
    if (mesh == nullptr) {
      continue;
    }

    geometry::MeshData data;
    data.vertices.reserve(mesh->mNumVertices);
    data.normals.reserve(mesh->mNumVertices);
    data.uvs.reserve(mesh->mNumVertices);
    data.tangents.reserve(mesh->mNumVertices);

    for (unsigned int v = 0; v < mesh->mNumVertices; ++v) {
      const aiVector3D& vert = mesh->mVertices[v];
      data.vertices.emplace_back(vert.x, vert.y, vert.z);

      if (mesh->HasNormals()) {
        const aiVector3D& normal = mesh->mNormals[v];
        data.normals.emplace_back(normal.x, normal.y, normal.z);
      } else {
        data.normals.emplace_back(0.0f, 1.0f, 0.0f);
      }

      if (mesh->HasTextureCoords(0)) {
        const aiVector3D& uv = mesh->mTextureCoords[0][v];
        data.uvs.emplace_back(uv.x, uv.y);
      } else {
        data.uvs.emplace_back(0.0f, 0.0f);
      }

      if (mesh->HasTangentsAndBitangents()) {
        const aiVector3D& tangent = mesh->mTangents[v];
        data.tangents.emplace_back(tangent.x, tangent.y, tangent.z, 1.0f);
      } else {
        data.tangents.emplace_back(1.0f, 0.0f, 0.0f, 1.0f);
      }
    }

    for (unsigned int f = 0; f < mesh->mNumFaces; ++f) {
      const aiFace& face = mesh->mFaces[f];
      if (face.mNumIndices != 3) {
        continue;
      }
      data.indices.push_back(face.mIndices[0]);
      data.indices.push_back(face.mIndices[1]);
      data.indices.push_back(face.mIndices[2]);
    }

    meshes.push_back(std::move(data));
  }

  return meshes;
}

}  // namespace karma::content
