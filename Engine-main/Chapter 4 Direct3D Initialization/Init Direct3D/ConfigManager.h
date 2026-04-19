#pragma once

#include <string>
#include <fstream>
#include <vector>
#include <iostream>
#include <DirectXColors.h>

class ConfigManager {
public:
    static ConfigManager& Get() {
        static ConfigManager instance;
        return instance;
    }

    void Load(const std::string& filename) {
        std::ifstream file(filename);
        if (!file.is_open()) return;

        std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        
        size_t pos = content.find("\"background_color\"");
        if (pos != std::string::npos) {
            size_t start = content.find("[", pos);
            size_t end = content.find("]", start);
            if (start != std::string::npos && end != std::string::npos) {
                std::string arrayStr = content.substr(start + 1, end - start - 1);
                std::vector<float> values;
                size_t commaPos = 0;
                while ((commaPos = arrayStr.find(",")) != std::string::npos) {
                    values.push_back(std::stof(arrayStr.substr(0, commaPos)));
                    arrayStr.erase(0, commaPos + 1);
                }
                values.push_back(std::stof(arrayStr));

                if (values.size() >= 4) {
                    mBackgroundColor[0] = values[0];
                    mBackgroundColor[1] = values[1];
                    mBackgroundColor[2] = values[2];
                    mBackgroundColor[3] = values[3];
                }
            }
        }
    }

    const float* GetBackgroundColor() const { return mBackgroundColor; }

private:
    ConfigManager() {
        // Дефолтный цвет (LightSteelBlue)
        mBackgroundColor[0] = 0.69f;
        mBackgroundColor[1] = 0.77f;
        mBackgroundColor[2] = 0.87f;
        mBackgroundColor[3] = 1.0f;
    }
    float mBackgroundColor[4];
};
