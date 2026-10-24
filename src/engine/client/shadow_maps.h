#pragma once
#include "engine/client/model_shadows.h"
#include "engine/server/model_geometry.h"
#include <memory>
#include <span>
#include <vector>

namespace nolf {
// The light a shadow is cast from: the sun (its direction of travel) or a
// point light (position, radius); a spot light casts as a point.
struct ShadowLight {
    bool sun=false;
    LTVector position{0,0,0},direction{0,-1,0};
    float radius=0;
    LTVector color{0,0,0};           // 0..255
};
// A model's drawn triangles, world space.
struct ShadowCasterMesh { const ModelPieceData* data=nullptr; std::span<const LTVector> positions; };
struct ShadowCaster {
    std::vector<ShadowCasterMesh> meshes;
    LTVector center{0,0,0};
    float radius=0;
    // How far past the caster its shadow may fall.
    float reach=200;
};
struct ShadowSettings {
    int tile=256;                    // texels across one shadow
    int tiles=16;                    // shadows a frame
    // Weighted: a shadow is as deep as its light is bright where it falls;
    // otherwise every shadow is `strength` deep (the original's projected
    // shadows, ModelShadowAlpha).
    bool weighted=false;
    float strength=.6f,softness=1.5f;
};
// Shadow maps for models: each caster drawn into a tile of a depth atlas from
// its light, then the world polygons in its shadow drawn again, darkened by
// how much of the tile's depth hides them from the light. Needs the device's
// programmable extension (available()).
class ShadowMaps {
public:
    ShadowMaps();
    ~ShadowMaps();
    ShadowMaps(const ShadowMaps&)=delete;
    ShadowMaps& operator=(const ShadowMaps&)=delete;
    static bool available();
    // A frame's shadows start empty; false when the device cannot draw them.
    bool begin(const ShadowSettings&);
    // The caster's depth from the light into the next tile. False when the
    // tiles are spent or the light cannot frame the caster (inside it).
    bool add(const ShadowCaster&,const ShadowLight&);
    std::size_t count() const;
    // Draws every shadow onto the receivers' polygons facing the camera, with
    // the camera's matrices as loaded; fog fades a shadow as it fades the
    // world. Answers the polygons drawn.
    std::size_t draw(std::span<const ShadowReceiver>,LTVector camera,bool fog);
private:
    struct State;
    std::unique_ptr<State> state_;
};
}
