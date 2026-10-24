#pragma once
#include "engine/client/surface_display.h"
#include "engine/portable/assets.h"
#include "iltclient.h"

namespace nolf {
// Exactly typed subset of the SDK table, also usable independently of the
// other client subsystems. install() copies these entries into ILTClient.
struct SurfaceFunctions {
#define NOLF_SURFACE_API(name) decltype(ILTClient::name) name=nullptr;
#include "surface_api_members.inc"
#undef NOLF_SURFACE_API
};
class ClientSurfaces {
    struct State;
public:
    // Assets are retained. The display must outlive this service and all its
    // active scopes. It owns the framebuffer; offscreen surfaces are CPU data.
    ClientSurfaces(std::shared_ptr<const Assets>,std::uint32_t width,std::uint32_t height,
        SurfaceDisplay& display,std::size_t byte_budget=256*1024*1024);
    ~ClientSurfaces();
    ClientSurfaces(const ClientSurfaces&)=delete;
    ClientSurfaces& operator=(const ClientSurfaces&)=delete;
    class Scope {
    public:
        explicit Scope(const ClientSurfaces&);
        ~Scope();
        Scope(const Scope&)=delete;
        Scope& operator=(const Scope&)=delete;
    private:
        std::shared_ptr<State> previous_,current_;
    };
    // The SDK function pointers have no context parameter. Hosts establish a
    // scope around game callbacks, allowing nested calls and separate clients
    // on independent threads without a process-wide engine singleton.
    Scope activate() const { return Scope(*this); }
    static const SurfaceFunctions& functions();
    void install(ILTClient&) const;
    void resize(std::uint32_t width,std::uint32_t height);
    Image screenshot() const;
    std::size_t live_surfaces() const;
    std::size_t allocated_bytes() const;
    HSURFACE create_image(Image);
    void draw_image(HSURFACE,const std::shared_ptr<const Surface>&,int x,int y,std::optional<PixelRect> clip = {});
private:
    std::shared_ptr<State> state_;
};
}
