#pragma once
#include "engine/client/camera_view.h"
#include "engine/server/world_polygon_geometry.h"
#include <vector>

namespace nolf {
struct SpriteVertex { LTVector position; float u,v; };
struct SpriteGeometryRequest {
    LTVector position{0,0,0},scale{1,1,1};
    LTRotation rotation{0,0,0,1};
    uint32 flags=0,width=0,height=0;
    // Borrowed only for this call; its world owner must remain alive.
    const CWorldPart* clip=nullptr;
    const WorldPolygonGeometry* clip_geometry=nullptr;
};
// Produces camera-space geometry with native half extents, UV inset and
// depth bias. Rotatable decals clip against every convex polygon edge.
std::vector<SpriteVertex> sprite_vertices(const SpriteGeometryRequest&,const CameraView&);
}
