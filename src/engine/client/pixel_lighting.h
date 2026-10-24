#pragma once
#include "engine/client/gl_programs.h"
#include "engine/server/model_geometry.h"
#include <span>
#include <vector>

namespace nolf {
// A light lighting a model per pixel, world space, colour 0..255. A light
// with the original's model falloff weighs N.L by 1 - d^2/r^2; a light taken
// out of the light grid keeps the grid's (1 - d/r)^2 and lights the side
// turned from it less (wrapped), so the model is as bright as the grid made
// it where it faces the light.
struct PixelLight { LTVector position{0,0,0}; float radius=0; LTVector color{0,0,0}; bool grid=false; };
// Enhanced lighting for one model: the original's terms -- light grid
// ambient, the sun's lerp, point lights, object colour scale and add --
// worked out per pixel from world-space normals.
struct PixelLighting {
    LTVector ambient{0,0,0};          // 0..255
    LTVector to_sun{0,1,0},sun_color{0,0,0};
    float sun_amount=0;
    std::vector<PixelLight> lights;
    LTVector scale{1.f/255,1.f/255,1.f/255},add{0,0,0};   // colour = c*scale + add/255
    float alpha=1;
    static constexpr std::size_t max_lights=16;
};
// Smooth world-space normals from a mesh's skinned positions: each
// triangle's area-weighted face normal, turned the way its bind-pose
// normals face, summed at its vertices.
std::vector<LTVector> model_SmoothNormals(const ModelPieceData&,std::span<const LTVector> positions,const LTMatrix& object);
class PixelLightingProgram {
public:
    PixelLightingProgram();
    // Puts the program in use with this lighting; false without the device's
    // programmable extension. The model's skin goes on unit 0.
    bool use(const PixelLighting&,bool textured,bool fog);
private:
    GlProgram program_;
};
}
