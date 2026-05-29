#include "MeshData.h"
#include "Logger.h"

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

std::shared_ptr<MeshData> MeshData::LoadFromFile(const std::string& path)
{
    auto mesh = std::make_shared<MeshData>();
    mesh->filePath = path;
    mesh->path = path;

    Assimp::Importer importer;
    const aiScene* scene = importer.ReadFile(path,
        aiProcess_Triangulate |
        aiProcess_GenSmoothNormals |
        aiProcess_FlipUVs |
        aiProcess_JoinIdenticalVertices);

    if (!scene || scene->mNumMeshes == 0) {
        Logger::Error("Assimp: Failed to load model: " + path);
        return nullptr;
    }

    aiMesh* aimesh = scene->mMeshes[0];
    SubMesh subMesh;

    // === Загрузка вершин ===
    subMesh.vertices.reserve(aimesh->mNumVertices);
    for (unsigned int i = 0; i < aimesh->mNumVertices; ++i) {
        Vertex3D v;

        v.position = glm::vec3(aimesh->mVertices[i].x,
            aimesh->mVertices[i].y,
            aimesh->mVertices[i].z);

        if (aimesh->HasNormals()) {
            v.normal = glm::vec3(aimesh->mNormals[i].x,
                aimesh->mNormals[i].y,
                aimesh->mNormals[i].z);
        }

        if (aimesh->HasTextureCoords(0)) {
            v.texCoord = glm::vec2(aimesh->mTextureCoords[0][i].x,
                aimesh->mTextureCoords[0][i].y);
        }
        else {
            // Планарные UV по умолчанию
            v.texCoord = glm::vec2((v.position.x + 1.0f) * 0.5f,
                (v.position.y + 1.0f) * 0.5f);
        }

        subMesh.vertices.push_back(v);
    }

    // === Загрузка индексов ===
    subMesh.indices.reserve(aimesh->mNumFaces * 3);

    for (unsigned int i = 0; i < aimesh->mNumFaces; ++i) {
        aiFace face = aimesh->mFaces[i];

        if (face.mNumIndices < 3) continue;  // защита от некорректных граней

        for (unsigned int j = 0; j < face.mNumIndices; ++j) {
            subMesh.indices.push_back(face.mIndices[j]);
        }
    }

    // Если индексов нет (модель без индексации) — генерируем их
    if (subMesh.indices.empty()) {
        Logger::Warning("Mesh has no indices. Generating sequential indices for: " + path);
        subMesh.indices.reserve(subMesh.vertices.size());
        for (unsigned int i = 0; i < subMesh.vertices.size(); ++i) {
            subMesh.indices.push_back(i);
        }
    }

    mesh->subMeshes.push_back(std::move(subMesh));
    mesh->loaded = true;

    Logger::Info("Mesh loaded successfully: " + path +
        " | Vertices: " + std::to_string(mesh->GetTotalVertexCount()) +
        " | Indices: " + std::to_string(mesh->GetTotalIndexCount()));

    return mesh;
}