#pragma once
#include <string>

class Texture {
public:
    Texture() = default;
    ~Texture() = default;

    bool Load(const std::string& filename) {
        m_filename = filename;
        return true;
    }

    void Unload() {}

    unsigned int GetID() const { return 0; }
    int GetWidth() const { return 0; }
    int GetHeight() const { return 0; }

private:
    std::string m_filename;
};