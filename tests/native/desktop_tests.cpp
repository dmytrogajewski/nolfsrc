#include "platform/desktop.h"
#include <SDL3/SDL.h>
#include <cmath>
#include <cstdlib>
#include <string_view>
#include "rdev.h"
#include <array>
#include <iostream>
#include <stdexcept>
#include <future>
#include <limits>

int main() {
    try {
        nolf::platform::DesktopConfig config;
        config.width = 128; config.height = 128; config.vsync = false;
        nolf::platform::Desktop desktop(config);
        if (const auto* expected=std::getenv("NOLF_EXPECT_VIDEO_DRIVER"); expected && std::string_view(desktop.driver())!=expected)
            throw std::runtime_error("SDL selected the wrong video backend");
        auto invalid_config=config; invalid_config.width=0;
        bool rejected=false;
        try { nolf::platform::Desktop invalid(invalid_config); } catch (const std::invalid_argument&) { rejected=true; }
        if (!rejected || desktop.title()!=config.title) throw std::runtime_error("failed desktop construction shut down an existing window");
        nolf::platform::Window& window=desktop;
        if (window.title() != config.title || window.desktop_mode().width <= 0 || window.display_modes().empty())
            throw std::runtime_error("native window service did not expose its actual title/display");
        window.window_mode(nolf::platform::WindowMode::Windowed,config.width,config.height);
        if (window.window_mode() != nolf::platform::WindowMode::Windowed) throw std::runtime_error("window placement was not retained");
        auto wrong_thread=std::async(std::launch::async,[&] {
            try { window.focused(); } catch (const std::logic_error&) { return true; }
            return false;
        });
        if (!wrong_thread.get()) throw std::runtime_error("window service accepted a foreign thread");
        window.confine_cursor(nolf::platform::CursorRectangle{10,12,40,30});
        const auto confined=window.pointer_position();
        if (confined.x < 10 || confined.x > 49 || confined.y < 12 || confined.y > 41)
            throw std::runtime_error("pointer events did not honor drawable clipping");
        window.confine_cursor(std::nullopt);
        // Exercise SDL's real virtual device path, including hotplug and mapping.
        // This is a fixture device, not a replacement gamepad provider.
        SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS,"1");
        SDL_VirtualJoystickDesc pad_desc{}; SDL_INIT_INTERFACE(&pad_desc);
        pad_desc.type=SDL_JOYSTICK_TYPE_GAMEPAD; pad_desc.name="NOLF adapter test";
        pad_desc.naxes=SDL_GAMEPAD_AXIS_COUNT; pad_desc.nbuttons=SDL_GAMEPAD_BUTTON_COUNT;
        pad_desc.axis_mask=(1u<<SDL_GAMEPAD_AXIS_COUNT)-1;
        pad_desc.button_mask=(1u<<SDL_GAMEPAD_BUTTON_COUNT)-1;
        const auto pad_id=SDL_AttachVirtualJoystick(&pad_desc);
        if (!pad_id) throw std::runtime_error(SDL_GetError());
        struct PadFixture {
            SDL_JoystickID id; SDL_Joystick* joystick;
            ~PadFixture() { if (joystick) SDL_CloseJoystick(joystick); if (id) SDL_DetachVirtualJoystick(id); }
        } pad{pad_id,SDL_OpenJoystick(pad_id)};
        if (!pad.joystick) throw std::runtime_error(SDL_GetError());
        desktop.poll();
        if (!SDL_SetJoystickVirtualAxis(pad.joystick,SDL_GAMEPAD_AXIS_LEFTX,16384) ||
            !SDL_SetJoystickVirtualAxis(pad.joystick,SDL_GAMEPAD_AXIS_LEFTY,-32768) ||
            !SDL_SetJoystickVirtualAxis(pad.joystick,SDL_GAMEPAD_AXIS_RIGHTY,32767) ||
            !SDL_SetJoystickVirtualAxis(pad.joystick,SDL_GAMEPAD_AXIS_LEFT_TRIGGER,32767) ||
            !SDL_SetJoystickVirtualAxis(pad.joystick,SDL_GAMEPAD_AXIS_RIGHT_TRIGGER,-32768) ||
            !SDL_SetJoystickVirtualButton(pad.joystick,SDL_GAMEPAD_BUTTON_SOUTH,true) ||
            !SDL_SetJoystickVirtualButton(pad.joystick,SDL_GAMEPAD_BUTTON_DPAD_LEFT,true)) throw std::runtime_error(SDL_GetError());
        SDL_UpdateJoysticks(); desktop.poll();
        const auto state=window.gamepad_state();
        const auto buttons=static_cast<std::uint16_t>(nolf::platform::GamepadButton::South)|static_cast<std::uint16_t>(nolf::platform::GamepadButton::Left);
        if (!state || state->id!=pad_id || state->buttons!=buttons || std::abs(state->left_x-.5f)>.001f ||
            state->left_y!=1 || state->right_y!=-1 || state->left_trigger!=1 || state->right_trigger!=0)
        {
            if (state) std::cerr << "gamepad=" << state->id << " expected=" << pad_id << " buttons=" << state->buttons << " axes=" << state->left_x << ',' << state->left_y << ',' << state->right_y << " triggers=" << state->left_trigger << ',' << state->right_trigger << '\n';
            throw std::runtime_error("SDL gamepad axes/buttons did not reach the native service");
        }
        if (!SDL_DetachVirtualJoystick(pad.id)) throw std::runtime_error(SDL_GetError());
        pad.id=0; SDL_CloseJoystick(pad.joystick); pad.joystick=nullptr; desktop.poll();
        if (const auto state=window.gamepad_state(); state && state->id==pad_id)
            throw std::runtime_error("removed SDL gamepad remained active");

        auto get_string = reinterpret_cast<const unsigned char* (*)(unsigned)>(desktop.gl_symbol("glGetString"));
        auto clear_color = reinterpret_cast<void (*)(float, float, float, float)>(desktop.gl_symbol("glClearColor"));
        auto clear = reinterpret_cast<void (*)(unsigned)>(desktop.gl_symbol("glClear"));
        auto read_pixels = reinterpret_cast<void (*)(int, int, int, int, unsigned, unsigned, void*)>(desktop.gl_symbol("glReadPixels"));
        if (!get_string || !clear_color || !clear || !read_pixels) throw std::runtime_error("missing core OpenGL entry points");
        if (!get_string(0x1f02)) throw std::runtime_error("OpenGL context is not current");
        std::cout << "desktop=" << desktop.driver() << " OpenGL=" << get_string(0x1f02) << '\n';
        int width = 0, height = 0;
        desktop.drawable_size(width, height);
        if (width < 128 || height < 128) throw std::runtime_error("invalid framebuffer size");
        if (!rd_CreateDevice(&desktop, width, height, true, 0, 4)) throw std::runtime_error("renderer device creation failed");
        std::cout << "renderer=" << rd_Describe() << " samples=" << g_RDCaps.nSamples << '\n';
        rd_ClearColor(0.25f, 0.5f, 0.75f, 1.0f);
        rd_Clear(RD_CLEAR_COLOR | RD_CLEAR_DEPTH);
        std::array<unsigned char, 4> rendered{};
        rd_ReadPixels(0, 0, 1, 1, RD_PIX_RGBA, rendered.data());
        if (rendered[0] < 62 || rendered[0] > 65 || rendered[1] < 126 || rendered[1] > 129)
            throw std::runtime_error("renderer readback or multisample resolve failed");
        rd_MatrixMode(RD_PROJECTION); rd_LoadIdentity();
        rd_MatrixMode(RD_MODELVIEW); rd_LoadIdentity();
        rd_Color4f(1, 0, 0, 1);
        rd_Begin(RD_TRIANGLES);
        rd_Vertex3f(-1, -1, 0); rd_Vertex3f(1, -1, 0); rd_Vertex3f(0, 1, 0);
        rd_End();
        rd_ReadPixels(width / 2, height / 2, 1, 1, RD_PIX_RGBA, rendered.data());
        if (rendered[0] < 250 || rendered[1] > 5 || rendered[2] > 5) throw std::runtime_error("renderer triangle did not draw");
        rd_Present();
        rd_DestroyDevice();
        if (!rd_CreateDevice(&desktop, width, height, true, 0, 1)) throw std::runtime_error("renderer restart failed");
        rd_DestroyDevice();
        if (!desktop.make_current()) throw std::runtime_error("context restore failed");
        clear_color(0.25f, 0.5f, 0.75f, 1.0f);
        clear(0x4000);
        std::array<unsigned char, 4> pixel{};
        read_pixels(0, 0, 1, 1, 0x1908, 0x1401, pixel.data());
        if (pixel[0] < 62 || pixel[0] > 65 || pixel[1] < 126 || pixel[1] > 129 || pixel[2] < 190 || pixel[2] > 193)
            throw std::runtime_error("OpenGL framebuffer contents differ from clear color");
        desktop.swap();
        desktop.relative_mouse(true);
        desktop.relative_mouse(false);
        desktop.text_input(true);
        desktop.text_input(false);
        desktop.poll();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n'; return 1;
    }
}
