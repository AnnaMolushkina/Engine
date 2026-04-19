#pragma once
#include "Entity.h"
#include "Component.h"
#include "Transform.h"
#include "MeshRenderer.h"
#include "Tag.h"
#include "Camera.h"
#include "SpatialGrid.h"
#include <unordered_map>
#include <vector>
#include <algorithm>
#include <string>

class World {
public:
    Entity CreateEntity() {
        Entity e = nextId++;
        entities.push_back(e);
        return e;
    }

    void DestroyEntity(Entity e) {
        transforms.erase(e);
        meshes.erase(e);
        tags.erase(e);
        cameras.erase(e);
        m_spatialGrid.RemoveEntity(e);
        entities.erase(std::remove(entities.begin(), entities.end(), e), entities.end());
    }

    // Transform
    Transform& AddTransform(Entity e) {
        Transform& t = transforms[e];
        m_spatialGrid.UpdateEntity(e, t.position);
        return t;
    }

    Transform* GetTransform(Entity e) {
        auto it = transforms.find(e);
        return (it != transforms.end()) ? &it->second : nullptr;
    }

    // MeshRenderer
    MeshRenderer& AddMeshRenderer(Entity e) {
        return meshes[e];
    }

    MeshRenderer* GetMeshRenderer(Entity e) {
        auto it = meshes.find(e);
        return (it != meshes.end()) ? &it->second : nullptr;
    }

    // Tag
    Tag& AddTag(Entity e, const std::string& name = "") {
        tags[e] = Tag{ name };
        return tags[e];
    }

    Tag* GetTag(Entity e) {
        auto it = tags.find(e);
        return (it != tags.end()) ? &it->second : nullptr;
    }

    // Camera
    Camera& AddCamera(Entity e) {
        return cameras[e];
    }

    Camera* GetCamera(Entity e) {
        auto it = cameras.find(e);
        return (it != cameras.end()) ? &it->second : nullptr;
    }

    // Ïîëó÷èòü âñå ñóùíîñòè ó êîòîğûõ åñòü Transform è MeshRenderer
    std::vector<Entity> GetRenderableEntities() const {
        std::vector<Entity> result;
        for (Entity e : entities) {
            if (transforms.find(e) != transforms.end() &&
                meshes.find(e) != meshes.end()) {
                result.push_back(e);
            }
        }
        return result;
    }

    std::vector<Entity> GetRenderableEntitiesForWindow(const std::string& windowName) const {
        std::vector<Entity> result;
        for (Entity e : entities) {
            if (transforms.count(e) && meshes.count(e)) {
                Tag* tag = const_cast<World*>(this)->GetTag(e);
                if (tag && tag->name == windowName) {
                    result.push_back(e);
                }
            }
        }
        return result;
    }

    // ========== ÌÅÒÎÄÛ ÄËß ÏĞÎÑÒĞÀÍÑÒÂÅÍÍÎÉ ÑÅÒÊÈ ==========
    void UpdateSpatialGrid() {
        m_spatialGrid.Clear();
        auto ents = GetRenderableEntities();
        for (Entity e : ents) {
            Transform* t = GetTransform(e);
            if (t) {
                m_spatialGrid.AddEntity(e, t->position);
            }
        }
    }

    std::vector<Entity> GetEntitiesInRange(const glm::vec3& center, float radius) const {
        return m_spatialGrid.GetEntitiesInRange(center, radius);
    }

    std::vector<Entity> GetVisibleEntities(const glm::vec3& cameraPos, float viewDistance) const {
        return m_spatialGrid.GetVisibleEntities(cameraPos, viewDistance);
    }

    SpatialGrid& GetSpatialGrid() {
        return m_spatialGrid;
    }
    // ======================================================

    Entity GetNextId() const { return nextId; }

private:
    Entity nextId = 1;
    std::vector<Entity> entities;

    std::unordered_map<Entity, Transform> transforms;
    std::unordered_map<Entity, MeshRenderer> meshes;
    std::unordered_map<Entity, Tag> tags;
    std::unordered_map<Entity, Camera> cameras;

    SpatialGrid m_spatialGrid;
};