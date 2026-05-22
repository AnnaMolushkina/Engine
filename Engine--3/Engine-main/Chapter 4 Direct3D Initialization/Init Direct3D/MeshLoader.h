//#pragma once
//// ===================================================================
//// MeshLoader — загрузка .obj-файлов через tinyobjloader
//// Использует условную компиляцию: если tiny_obj_loader.h недоступен,
//// возвращает куб-заглушку.
//// ===================================================================
//#include "MeshData.h"
//#include "Logger.h"
//#include <string>
//#include <memory>
//
//// TINYOBJLOADER_IMPLEMENTATION должен быть определён ровно в одном
//// translation unit. Мы делаем это здесь через MESH_LOADER_IMPL.
//#ifdef MESH_LOADER_IMPL
//#  define TINYOBJLOADER_IMPLEMENTATION
//#endif
//#include "tiny_obj_loader.h"
//
//class MeshLoader {
//public:
//    // ---------- Основной метод загрузки ----------
//    static std::shared_ptr<MeshData> Load(const std::string& filePath) {
//        tinyobj::attrib_t                attrib;
//        std::vector<tinyobj::shape_t>    shapes;
//        std::vector<tinyobj::material_t> materials;
//        std::string warn, err;
//
//        // Базовая директория для поиска .mtl
//        std::string baseDir;
//        auto sep = filePath.find_last_of("/\\");
//        if (sep != std::string::npos) baseDir = filePath.substr(0, sep + 1);
//
//        bool ok = tinyobj::LoadObj(&attrib, &shapes, &materials,
//                                   &warn, &err,
//                                   filePath.c_str(), baseDir.c_str());
//
//        if (!warn.empty()) Logger::Info("[MeshLoader] Warn: " + warn);
//        if (!err.empty())  Logger::Error("[MeshLoader] Error: " + err);
//
//        if (!ok || shapes.empty()) {
//            Logger::Error("[MeshLoader] Failed to load: " + filePath + " — using placeholder cube");
//            return CreatePlaceholderCube();
//        }
//
//        auto meshData      = std::make_shared<MeshData>();
//        meshData->filePath = filePath;
//
//        for (const auto& shape : shapes) {
//            SubMesh sub;
//            sub.materialName = shape.name;
//
//            size_t indexOffset = 0;
//            for (size_t f = 0; f < shape.mesh.num_face_vertices.size(); f++) {
//                int fv = shape.mesh.num_face_vertices[f];
//
//                for (int v = 0; v < fv; v++) {
//                    tinyobj::index_t idx = shape.mesh.indices[indexOffset + v];
//
//                    Vertex3D vert;
//                    vert.position = {
//                        attrib.vertices[3 * idx.vertex_index + 0],
//                        attrib.vertices[3 * idx.vertex_index + 1],
//                        attrib.vertices[3 * idx.vertex_index + 2]
//                    };
//
//                    if (idx.normal_index >= 0 && !attrib.normals.empty()) {
//                        vert.normal = {
//                            attrib.normals[3 * idx.normal_index + 0],
//                            attrib.normals[3 * idx.normal_index + 1],
//                            attrib.normals[3 * idx.normal_index + 2]
//                        };
//                    }
//
//                    if (idx.texcoord_index >= 0 && !attrib.texcoords.empty()) {
//                        vert.texCoord = {
//                            attrib.texcoords[2 * idx.texcoord_index + 0],
//                            1.0f - attrib.texcoords[2 * idx.texcoord_index + 1]  // flip V
//                        };
//                    }
//
//                    sub.indices.push_back(static_cast<uint32_t>(sub.vertices.size()));
//                    sub.vertices.push_back(vert);
//                }
//                indexOffset += fv;
//            }
//
//            if (!sub.vertices.empty())
//                meshData->subMeshes.push_back(std::move(sub));
//        }
//
//        Logger::Info("[MeshLoader] Loaded: \"" + filePath + "\""
//            + "  shapes=" + std::to_string(meshData->subMeshes.size())
//            + "  vertices=" + std::to_string(meshData->GetTotalVertexCount())
//            + "  triangles=" + std::to_string(meshData->GetTotalIndexCount() / 3));
//
//        return meshData;
//    }
//
//    // ---------- Куб-заглушка (всегда работает) ----------
//    static std::shared_ptr<MeshData> CreatePlaceholderCube() {
//        auto data = std::make_shared<MeshData>();
//        data->filePath = "__placeholder_cube__";
//
//        SubMesh sub;
//        sub.materialName = "placeholder";
//
//        const float h = 0.5f;
//        // 6 граней, каждая — 4 вершины
//        struct FaceInfo { glm::vec3 normal; glm::vec3 v[4]; };
//        const FaceInfo faces[6] = {
//            { { 0, 0, 1}, { {{-h,-h,h}},{{h,-h,h}},{{h,h,h}},{{-h,h,h}} } },   // Front
//            { { 0, 0,-1}, { {{h,-h,-h}},{{-h,-h,-h}},{{-h,h,-h}},{{h,h,-h}} } }, // Back
//            { {-1, 0, 0}, { {{-h,-h,-h}},{{-h,-h,h}},{{-h,h,h}},{{-h,h,-h}} } }, // Left
//            { { 1, 0, 0}, { {{h,-h,h}},{{h,-h,-h}},{{h,h,-h}},{{h,h,h}} } },     // Right
//            { { 0, 1, 0}, { {{-h,h,h}},{{h,h,h}},{{h,h,-h}},{{-h,h,-h}} } },     // Top
//            { { 0,-1, 0}, { {{-h,-h,-h}},{{h,-h,-h}},{{h,-h,h}},{{-h,-h,h}} } }, // Bottom
//        };
//        const glm::vec2 uvs[4] = { {0,0},{1,0},{1,1},{0,1} };
//
//        for (int f = 0; f < 6; f++) {
//            uint32_t base = static_cast<uint32_t>(sub.vertices.size());
//            for (int i = 0; i < 4; i++) {
//                Vertex3D v;
//                v.position = faces[f].v[i];
//                v.normal   = faces[f].normal;
//                v.texCoord = uvs[i];
//                sub.vertices.push_back(v);
//            }
//            // Два треугольника
//            sub.indices.insert(sub.indices.end(),
//                { base, base+1, base+2, base, base+2, base+3 });
//        }
//
//        data->subMeshes.push_back(std::move(sub));
//        Logger::Info("[MeshLoader] Created placeholder cube: 24 vertices, 12 triangles");
//        return data;
//    }
//};
