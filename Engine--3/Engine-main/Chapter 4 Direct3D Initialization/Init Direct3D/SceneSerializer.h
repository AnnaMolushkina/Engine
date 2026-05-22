#pragma once
#include "World.h"
#include "Transform.h"
#include "MeshRenderer.h"
#include "Tag.h"
#include "json.hpp"
#include <string>
#include <fstream>
#include <vector>
#include <algorithm>

using json = nlohmann::json;

class SceneSerializer {
public:
    // Сохранить сцену в JSON файл (с состоянием анимаций)
    static bool SaveScene(const World& world, const std::string& filename,
        float rotationAngle, float jumpPhase, float circleY) {
        json sceneJson;

        std::vector<Entity> entities = const_cast<World&>(world).GetRenderableEntities();

        for (Entity e : entities) {
            json entityJson;
            entityJson["id"] = e;

            const Transform* transform = const_cast<World&>(world).GetTransform(e);
            if (transform) {
                entityJson["transform"]["position"]["x"] = transform->position.x;
                entityJson["transform"]["position"]["y"] = transform->position.y;
                entityJson["transform"]["position"]["z"] = transform->position.z;

                entityJson["transform"]["rotation"]["x"] = transform->rotation.x;
                entityJson["transform"]["rotation"]["y"] = transform->rotation.y;
                entityJson["transform"]["rotation"]["z"] = transform->rotation.z;
                entityJson["transform"]["rotation"]["w"] = transform->rotation.w;

                entityJson["transform"]["scale"]["x"] = transform->scale.x;
                entityJson["transform"]["scale"]["y"] = transform->scale.y;
                entityJson["transform"]["scale"]["z"] = transform->scale.z;
            }

            const MeshRenderer* meshRenderer = const_cast<World&>(world).GetMeshRenderer(e);
            if (meshRenderer) {
                entityJson["meshRenderer"]["type"] = static_cast<int>(meshRenderer->type);
                entityJson["meshRenderer"]["color"]["r"] = meshRenderer->color.r;
                entityJson["meshRenderer"]["color"]["g"] = meshRenderer->color.g;
                entityJson["meshRenderer"]["color"]["b"] = meshRenderer->color.b;
                entityJson["meshRenderer"]["color"]["a"] = meshRenderer->color.a;
            }

            const Tag* tag = const_cast<World&>(world).GetTag(e);
            if (tag) {
                entityJson["tag"]["name"] = tag->name;
            }

            sceneJson["entities"].push_back(entityJson);
        }

        // Сохраняем состояние анимаций
        sceneJson["animationState"]["rotationAngle"] = rotationAngle;
        sceneJson["animationState"]["jumpPhase"] = jumpPhase;
        sceneJson["animationState"]["circleY"] = circleY;

        sceneJson["version"] = 1;
        sceneJson["entityCount"] = entities.size();

        std::ofstream file(filename);
        if (!file.is_open()) return false;

        file << sceneJson.dump(4);
        file.close();
        return true;
    }

    // Загрузить сцену из JSON файла (с восстановлением состояния анимаций)
    static bool LoadScene(World& world, const std::string& filename,
        Entity& outTriangle, Entity& outCircle, Entity& outSquare,
        float& outRotationAngle, float& outJumpPhase, float& outCircleY) {
        std::ifstream file(filename);
        if (!file.is_open()) return false;

        json sceneJson;
        file >> sceneJson;
        file.close();

        if (sceneJson.contains("version") && sceneJson["version"].get<int>() != 1) {
            return false;
        }

        // Загружаем состояние анимаций
        if (sceneJson.contains("animationState")) {
            outRotationAngle = sceneJson["animationState"]["rotationAngle"].get<float>();
            outJumpPhase = sceneJson["animationState"]["jumpPhase"].get<float>();
            outCircleY = sceneJson["animationState"]["circleY"].get<float>();
        }
        else {
            outRotationAngle = 0.0f;
            outJumpPhase = 0.0f;
            outCircleY = 0.0f;
        }

        // Очищаем старую сцену
        std::vector<Entity> oldEntities = world.GetRenderableEntities();
        for (Entity e : oldEntities) {
            world.DestroyEntity(e);
        }
        Logger::Info("Cleared " + std::to_string(oldEntities.size()) + " old entities");

        outTriangle = 0;
        outCircle = 0;
        outSquare = 0;

        // Загружаем сущности
        for (const auto& entityJson : sceneJson["entities"]) {
            Entity e = world.CreateEntity();

            // Загружаем Transform
            if (entityJson.contains("transform")) {
                Transform& t = world.AddTransform(e);

                t.position.x = entityJson["transform"]["position"]["x"].get<float>();
                t.position.y = entityJson["transform"]["position"]["y"].get<float>();
                t.position.z = entityJson["transform"]["position"]["z"].get<float>();

                t.rotation.x = entityJson["transform"]["rotation"]["x"].get<float>();
                t.rotation.y = entityJson["transform"]["rotation"]["y"].get<float>();
                t.rotation.z = entityJson["transform"]["rotation"]["z"].get<float>();
                t.rotation.w = entityJson["transform"]["rotation"]["w"].get<float>();

                t.scale.x = entityJson["transform"]["scale"]["x"].get<float>();
                t.scale.y = entityJson["transform"]["scale"]["y"].get<float>();
                t.scale.z = entityJson["transform"]["scale"]["z"].get<float>();
            }

            // Загружаем MeshRenderer
            if (entityJson.contains("meshRenderer")) {
                MeshRenderer& mr = world.AddMeshRenderer(e);

                int typeInt = entityJson["meshRenderer"]["type"].get<int>();
                mr.type = static_cast<PrimitiveType>(typeInt);

                mr.color.r = entityJson["meshRenderer"]["color"]["r"].get<float>();
                mr.color.g = entityJson["meshRenderer"]["color"]["g"].get<float>();
                mr.color.b = entityJson["meshRenderer"]["color"]["b"].get<float>();
                mr.color.a = entityJson["meshRenderer"]["color"]["a"].get<float>();
            }

            // Загружаем Tag
            if (entityJson.contains("tag")) {
                std::string name = entityJson["tag"]["name"].get<std::string>();
                world.AddTag(e, name);

                // Восстанавливаем ссылки по тегам
                if (name == "MainWindow") {
                    MeshRenderer* mr = world.GetMeshRenderer(e);
                    if (mr && mr->type == PrimitiveType::Triangle) {
                        outTriangle = e;
                        Logger::Info("Restored triangle entity: " + std::to_string(e));
                    }
                    else if (mr && (mr->type == PrimitiveType::Square || mr->type == PrimitiveType::Quad)) {
                        outCircle = e;
                        Logger::Info("Restored circle/rhombus entity: " + std::to_string(e));
                    }
                }
                else if (name == "SecondaryWindow") {
                    outSquare = e;
                    Logger::Info("Restored square entity: " + std::to_string(e));
                }
            }
        }

        Logger::Info("Loaded " + std::to_string(sceneJson["entities"].size()) + " entities");
        return true;
    }
};