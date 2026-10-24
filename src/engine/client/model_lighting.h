#pragma once
#include "engine/server/commonmodel.h"
#include "engine/server/world_state.h"
#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace nolf {
// A dynamic light a model may receive: world space, colour 0..255.
struct ModelLightSource { LTVector position{0,0,0}; float radius=0; LTVector color{0,0,0}; };
// LithTech's per-vertex model lighting (src/modern/render/docs/objects.md
// II.5): the world's light grid as ambient, then point lights brought into the
// space of the bind-pose vertices, each weighted by the normal and by
// 1 - d^2/r^2. Dynamic lights come first, then the world's static lights that
// the light grid does not hold.
class ModelLighting {
public:
    static constexpr std::size_t max_lights=16;
    // frame places the bind pose in the world; ambient, scale and add are the
    // grid sample (0..255), the object colour (0..1) and the additive colour.
    ModelLighting(const LTMatrix& frame,LTVector ambient,LTVector scale,LTVector add,std::uint8_t alpha);
    // A FLAG_NOLIGHT model: full ambient, no lights.
    static ModelLighting unlit(LTVector scale,LTVector add,std::uint8_t alpha);
    // Each adds the light when it reaches a model of this radius at position
    // and fewer than limit lights are held.
    void add(const ModelLightSource&,LTVector position,float reach,std::size_t limit);
    void add(const WorldLight&,LTVector position,float reach,std::size_t limit);
    std::size_t lights() const { return count_; }
    // One RGBA colour per vertex.
    void light(std::span<const ModelVertex>,std::vector<std::array<std::uint8_t,4>>& colors) const;
    std::array<std::uint8_t,4> flat() const;
private:
    struct Light { LTVector position,direction,color; float squared_radius=0,inverse_squared=0; };
    ModelLighting(LTVector ambient,LTVector scale,LTVector add,std::uint8_t alpha);
    void push(LTVector world_position,float radius,LTVector color);
    LTMatrix to_model_{};
    LTVector ambient_,scale_,add_;
    std::uint8_t alpha_;
    std::array<Light,max_lights> lights_{};
    std::size_t count_=0;
    bool unlit_=false;
};
}
