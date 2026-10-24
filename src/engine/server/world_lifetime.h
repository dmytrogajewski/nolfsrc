#pragma once
#include "ltintegertypes.h"

// Independent token for geometry readers across callbacks and replacement.
struct WorldGeometryLifetime {
    uint64 generation = 0;
    bool alive = true, available = true;
    bool Matches(uint64 value) const { return alive && available && generation == value; }
};
