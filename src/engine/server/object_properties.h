#pragma once
#include "ltbasedefs.h"
#include <span>
#include <string>
#include <vector>

struct WorldProperty;
class CServerMgr;
struct ObjectProperty {
    std::string name, text;
    int type = 0;
    GenericProp value{};
};
// Runtime values own their storage. Serialized lengths and float encodings
// are decoded at the boundary and never overlaid with native structures.
class ObjectProperties {
public:
    explicit ObjectProperties(std::span<const WorldProperty>);
    explicit ObjectProperties(std::string_view statements);
    const ObjectProperty* find(std::string_view) const;
private:
    std::vector<ObjectProperty> values_;
};
