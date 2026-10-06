#include "MeshData.h"
#include "Logger.h"

#include <algorithm>
#include <cmath>

// Полные заголовки Assimp
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

uint32_t MeshData::GetTotalVertexCount() const {
    uint32_t n = 0;
    for (const auto& sm : subMeshes) n += static_cast<uint32_t>(sm.vertices.size());
    return n;
}

uint32_t MeshData::GetTotalIndexCount() const {
    uint32_t n = 0;
    for (const auto& sm : subMeshes) n += static_cast<uint32_t>(sm.indices.size());
    return n;
}

std::shared_ptr<MeshData> MeshData::LoadFromFile(const std::string& path) {
    auto mesh = std::make_shared<MeshData>();
    if (!LoadInto(*mesh, path)) return nullptr;
    return mesh;
}

bool MeshData::LoadInto(MeshData& mesh, const std::string& path) {
    mesh.filePath = path;
    mesh.path = path;
    mesh.subMeshes.clear();
    mesh.gpuMesh = GPUMesh{};
    mesh.boundingRadius = 1.0f;
    mesh.loaded = false;

    Assimp::Importer importer;
    const aiScene* scene = importer.ReadFile(path,
        aiProcess_Triangulate | aiProcess_GenSmoothNormals |
        aiProcess_FlipUVs | aiProcess_JoinIdenticalVertices);

    if (!scene || scene->mNumMeshes == 0) {
        Logger::Error("Assimp: Failed to load model: " + path);
        return false;
    }

    aiMesh* aimesh = scene->mMeshes[0];
    SubMesh subMesh;

    subMesh.vertices.reserve(aimesh->mNumVertices);
    for (unsigned int i = 0; i < aimesh->mNumVertices; ++i) {
        Vertex3D v;
        v.position = glm::vec3(aimesh->mVertices[i].x, aimesh->mVertices[i].y, aimesh->mVertices[i].z);
        if (aimesh->HasNormals())
            v.normal = glm::vec3(aimesh->mNormals[i].x, aimesh->mNormals[i].y, aimesh->mNormals[i].z);
        if (aimesh->HasTextureCoords(0))
            v.texCoord = glm::vec2(aimesh->mTextureCoords[0][i].x, aimesh->mTextureCoords[0][i].y);
        subMesh.vertices.push_back(v);
    }
    for (unsigned int i = 0; i < aimesh->mNumFaces; ++i) {
        aiFace face = aimesh->mFaces[i];
        for (unsigned int j = 0; j < face.mNumIndices; ++j)
            subMesh.indices.push_back(face.mIndices[j]);
    }

    float maxDistSq = 0.0f;
    for (const Vertex3D& v : subMesh.vertices)
        maxDistSq = std::max(maxDistSq, glm::dot(v.position, v.position));
    mesh.boundingRadius = std::sqrt(maxDistSq);

    mesh.subMeshes.push_back(std::move(subMesh));
    mesh.loaded = true;

    Logger::Info("Mesh loaded (CPU): " + path +
        " | V:" + std::to_string(mesh.GetTotalVertexCount()) +
        " | I:" + std::to_string(mesh.GetTotalIndexCount()));
    return true;
}