#pragma once
#include "Component.h"
#include <string>

struct Tag : public Component {
    std::string name;

    Tag() : name("") {}
    Tag(const std::string& n) : name(n) {}
};