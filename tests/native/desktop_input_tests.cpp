#include "input_fixture.h"
#include "platform/desktop.h"
#include "platform/filesystem.h"
#include <SDL3/SDL.h>
#include <cmath>
#include <iostream>

namespace {
using input_fixture::require;
void checks(const char* data) {
    nolf::platform::DesktopConfig config; config.width=config.height=128; config.vsync=false; nolf::platform::Desktop desktop(config); desktop.poll(); std::cout<<"desktop="<<desktop.driver()<<'\n';
    const auto catalog=nolf::platform::desktop_input_controls(); nolf::ClientInput input(catalog); const auto scope=input.activate(); const auto& api=nolf::ClientInput::functions();
    input_fixture::configure(input,"AddAction Forward 0; AddAction Fire 8; AddAction Axis1 -1; rangebind ##keyboard ##17 0 0 Forward; rangebind ##mouse ##3 0 0 Fire; rangebind ##mouse ##x-axis 0 0 Axis1; scale ##mouse ##x-axis 0.01");
    auto events=desktop.poll(); nolf::platform::InputEvent focus{}; focus.type=nolf::platform::InputType::focus; focus.pressed=true; input.frame(std::span(&focus,1));
    SDL_Event event{}; event.type=SDL_EVENT_KEY_DOWN; event.key.scancode=SDL_SCANCODE_W; event.key.down=true; require(SDL_PushEvent(&event),"SDL keyboard fixture enqueue failed");
    event={}; event.type=SDL_EVENT_MOUSE_BUTTON_DOWN; event.button.button=SDL_BUTTON_LEFT; event.button.down=true; require(SDL_PushEvent(&event),"SDL button fixture enqueue failed");
    event={}; event.type=SDL_EVENT_MOUSE_MOTION; event.motion.xrel=50; require(SDL_PushEvent(&event),"SDL motion fixture enqueue failed");
    events=desktop.poll(); input.frame(events); float offsets[3]{}; api.GetAxisOffsets(offsets);
    require(api.IsCommandOn(0) && api.IsCommandOn(8) && std::abs(offsets[0]-.5f)<.0001f && input.logical_key(17)=='W',"SDL event normalization did not reach SDK command/mouse bindings");
    for (const auto [scan,binding,logical] : {std::array<unsigned,3>{SDL_SCANCODE_ESCAPE,1,27},std::array<unsigned,3>{SDL_SCANCODE_F5,63,0x74},std::array<unsigned,3>{SDL_SCANCODE_LEFT,203,0x25},std::array<unsigned,3>{SDL_SCANCODE_RCTRL,157,0x11}}) require(nolf::platform::desktop_key_binding(int(scan))==binding && input.logical_key(binding)==logical,"SDL extended/menu physical/logical key differs");
    const auto saved=input.config(); nolf::ClientInput restored(catalog); input_fixture::configure(restored,saved); require(restored.config()==saved,"SDL catalog binding config did not round trip");
    if (data) {
        const auto bytes=nolf::platform::read_file(std::filesystem::path(data)/"defaults.cfg"); const std::string text(reinterpret_cast<const char*>(bytes.data()),bytes.size());
        nolf::ClientInput original(catalog); const auto count=input_fixture::configure(original,text,true); require(count>100,"original default input commands missing"); const auto original_scope=original.activate();
        std::array original_events{input_fixture::key(17,true),input_fixture::key(57,true)}; original.frame(original_events); require(api.IsCommandOn(0) && api.IsCommandOn(6),"original W/space bindings did not drive forward/jump");
        nolf::platform::InputEvent button{}; button.type=nolf::platform::InputType::mouse_button; button.code=int(nolf::platform::MouseButton::Right); button.pressed=true; original.frame(std::span(&button,1)); require(api.IsCommandOn(2) && !api.IsCommandOn(8),"original Button 1 confused canonical binding with display label");
        button.code=int(nolf::platform::MouseButton::Middle); original.frame(std::span(&button,1)); require(api.IsCommandOn(3),"original middle button did not drive reload");
        nolf::ClientInput roundtrip(catalog); input_fixture::configure(roundtrip,original.config()); require(roundtrip.config()==original.config(),"original input graph did not round trip"); std::cout<<"Original defaults.cfg: "<<count<<" input commands loaded, W/space drive forward/jump\n";
    }
}
}
int main(int argc,char** argv) { try { checks(argc>1 ? argv[1] : nullptr); std::cout<<"Native SDL keyboard/mouse translation and persisted SDK bindings passed\n"; return 0; } catch (const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; } }
