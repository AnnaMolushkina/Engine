#pragma once
#include "Entity.h"
#include "Transform.h"
#include <unordered_map>
#include <vector>
#include <cmath>
#include <string>
#include <algorithm>
#include "Logger.h"

struct SpatialGrid {
    float cellSize = 10.0f;
    std::unordered_map<std::string, std::vector<Entity>> grid;
    std::unordered_map<Entity, std::string> entityCell;

    std::string GetCellKey(float x, float z) {
        int cellX = static_cast<int>(std::floor(x / cellSize));
        int cellZ = static_cast<int>(std::floor(z / cellSize));
        return std::to_string(cellX) + "," + std::to_string(cellZ);
    }

    void UpdateEntity(Entity e, const glm::vec3& position) {
        std::string newKey = GetCellKey(position.x, position.z);

        auto it = entityCell.find(e);
        if (it != entityCell.end()) {
            if (it->second == newKey) return;
            std::string oldKey = it->second;
            auto gridIt = grid.find(oldKey);
            if (gridIt != grid.end()) {
                auto& vec = gridIt->second;
                vec.erase(std::remove(vec.begin(), vec.end(), e), vec.end());
                if (vec.empty()) grid.erase(gridIt);
            }
        }

        grid[newKey].push_back(e);
        entityCell[e] = newKey;
    }

    void RemoveEntity(Entity e) {
        auto it = entityCell.find(e);
        if (it != entityCell.end()) {
            std::string key = it->second;
            auto gridIt = grid.find(key);
            if (gridIt != grid.end()) {
                auto& vec = gridIt->second;
                vec.erase(std::remove(vec.begin(), vec.end(), e), vec.end());
                if (vec.empty()) grid.erase(gridIt);
            }
            entityCell.erase(it);
        }
    }

    // Метод для очистки сетки
    void Clear() {
        grid.clear();
        entityCell.clear();
    }

    // Метод для добавления сущности напрямую
    void AddEntity(Entity e, const glm::vec3& position) {
        std::string key = GetCellKey(position.x, position.z);
        grid[key].push_back(e);
        entityCell[e] = key;
    }

    std::vector<Entity> GetEntitiesInRange(const glm::vec3& center, float radius) const {
        std::vector<Entity> result;
        std::unordered_map<Entity, bool> added;

        int minX = static_cast<int>(std::floor((center.x - radius) / cellSize));
        int maxX = static_cast<int>(std::floor((center.x + radius) / cellSize));
        int minZ = static_cast<int>(std::floor((center.z - radius) / cellSize));
        int maxZ = static_cast<int>(std::floor((center.z + radius) / cellSize));

        for (int x = minX; x <= maxX; ++x) {
            for (int z = minZ; z <= maxZ; ++z) {
                std::string key = std::to_string(x) + "," + std::to_string(z);
                auto it = grid.find(key);
                if (it != grid.end()) {
                    for (Entity e : it->second) {
                        if (!added[e]) {
                            added[e] = true;
                            result.push_back(e);
                        }
                    }
                }
            }
        }

        return result;
    }

    std::vector<Entity> GetVisibleEntities(const glm::vec3& cameraPos, float viewDistance) const {
        return GetEntitiesInRange(cameraPos, viewDistance);
    }
};