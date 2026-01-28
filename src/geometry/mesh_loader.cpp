#include "karma/geometry/mesh_loader.h"

#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

namespace karma::geometry {

std::vector<MeshData> loadGLB(const std::string& filename) {
    std::vector<MeshData> meshes;

    Assimp::Importer importer;
    const aiScene* scene = importer.ReadFile(
        filename,
        aiProcess_Triangulate |
        aiProcess_JoinIdenticalVertices);

    if (!scene) {
        return meshes;
    }

    for (unsigned int m = 0; m < scene->mNumMeshes; ++m) {
        aiMesh* mesh = scene->mMeshes[m];
        MeshData data;

        for (unsigned int v = 0; v < mesh->mNumVertices; ++v) {
            auto& vert = mesh->mVertices[v];
            data.vertices.emplace_back(vert.x, vert.y, vert.z);
        }

        for (unsigned int f = 0; f < mesh->mNumFaces; ++f) {
            aiFace& face = mesh->mFaces[f];
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

} // namespace karma::geometry
