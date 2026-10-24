#pragma once
#include "engine/client/surface_api.h"
#include "platform/fonts.h"
#include <array>

namespace nolf {
struct FontFunctions {
#define NOLF_FONT_API(name) decltype(ILTClient::name) name=nullptr;
#include "client_font_members.inc"
#undef NOLF_FONT_API
};
// Font handles, text layout and raster cache belong to the client. The
// platform adapter supplies faces, and ClientSurfaces owns image presentation.
class ClientFonts {
    struct State;
public:
    ClientFonts(std::shared_ptr<platform::FontProvider>,std::shared_ptr<ClientSurfaces>,std::size_t byte_budget=32*1024*1024);
    ~ClientFonts();
    ClientFonts(const ClientFonts&)=delete;
    ClientFonts& operator=(const ClientFonts&)=delete;
    class Scope {
    public:
        explicit Scope(const ClientFonts&);
        ~Scope();
        Scope(const Scope&)=delete;
        Scope& operator=(const Scope&)=delete;
    private:
        std::shared_ptr<State> previous_,current_;
    };
    Scope activate() const { return Scope(*this); }
    static const FontFunctions& functions();
    static void install(ILTClient&);
    std::size_t live_fonts() const;
    std::size_t cached_bytes() const;
private:
    std::shared_ptr<State> state_;
};
}
