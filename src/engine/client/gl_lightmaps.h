#pragma once
#include "engine/client/world_lightmaps.h"

namespace nolf {
// Device residency for one CPU atlas. Lighting updates remain in WorldLightmaps;
// context recreation and GL calls remain in the device adapter.
class GlLightmaps {
public:
    explicit GlLightmaps(std::shared_ptr<const WorldLightmaps>,std::size_t budget=64*1024*1024);
    ~GlLightmaps();
    GlLightmaps(const GlLightmaps&)=delete;
    GlLightmaps& operator=(const GlLightmaps&)=delete;
    void bind(std::size_t page,bool linear=true);
    void clear();
    std::size_t bytes() const;
    std::size_t size() const;
private:
    struct State;
    std::unique_ptr<State> state_;
};
}
