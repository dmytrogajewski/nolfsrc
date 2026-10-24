#pragma once
#include "engine/server/commonmodel.h"
#include "engine/server/world_bsp_interface.h"
#include <span>

namespace nolf {
// A BSP a shadow may fall on: world space, or a world model's through its
// placement and its inverse.
struct ShadowReceiver { IWorldBsp* bsp=nullptr; const LTMatrix* to_world=nullptr; const LTMatrix* to_local=nullptr; };
// MaxModelShadows, ModelShadowAlpha (0..255) and ModelShadowOffset.
struct ModelShadowSettings { int max=1; float alpha=230,offset=.05f; };
// LithTech's projected model shadow (d3d.ren 0x10048180; reference port
// src/modern/render/mr_shadows.cpp): a model whose command string enables
// shadows casts a soft blob straight down from a point light ShadowLightDist
// above it. The receiving polygons near that segment that face the camera
// are drawn again, lifted off their planes and clipped to the shadow's
// volume, black under the blob. Uses the caller's camera matrices.
void model_DrawShadow(const Model&,LTVector position,std::span<const ShadowReceiver>,LTVector camera,const ModelShadowSettings&);
}
