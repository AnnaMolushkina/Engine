#pragma once
#include "Resource.h"
#include <vector>
#include <string>
#include <cstdint>
#include <memory>
#include "GPUMesh.h"
#include <glm/glm.hpp>

// Forward declarations Assimp
namespace Assimp { class Importer; }
struct aiScene;
struct aiMesh;
struct aiFace;

// ===== Vertex3D =====
// ВАЖНО: порядок полей совпадает с Input Layout:
//   POSITION offset  0 (12 байт, glm::vec3)
//   COLOR    offset 12 (16 байт, glm::vec4)  <- ДОЛЖЕН быть ДО normal!
//   TEXCOORD offset 28 ( 8 байт, glm::vec2)
//   normal   offset 36 (12 байт) — не в Input Layout, GPU игнорирует
struct Vertex3D {
    glm::vec3 position = glm::vec3(0.0f);              // offset  0
    glm::vec4 color    = glm::vec4(1.0f);              // offset 12
    glm::vec2 texCoord = glm::vec2(0.0f);              // offset 28
    glm::vec3 normal   = glm::vec3(0.0f, 1.0f, 0.0f); // offset 36 (за пределами layout)
};

// ===== SubMesh =====
struct SubMesh {
    std::vector<Vertex3D> vertices;
    std::vector<uint32_t> indices;
    std::string materialName;
};

// ===== MeshData =====
struct MeshData : public Resource {
    std::vector<SubMesh> subMeshes;
    std::string filePath;

    // GPU-�����
    GPUMesh gpuMesh;

    uint32_t GetTotalVertexCount() const;
    uint32_t GetTotalIndexCount() const;
    bool IsValid() const { return !subMeshes.empty(); }

    static std::shared_ptr<MeshData> LoadFromFile(const std::string& path);
};