#pragma once
#include <string>

class Resource {
public:
    virtual ~Resource() = default;
    std::string path = "";
    bool loaded = false;
};