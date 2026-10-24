#include "platform/desktop.h"
#include "platform/logging.h"
#include "platform/cursors.h"
#include "platform/input.h"
#include <SDL3/SDL.h>
#ifdef NOLF_SYSTEM_EGL
#include <EGL/egl.h>
#endif
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <thread>
#include <unordered_map>

namespace nolf::platform {
static void check(bool result, const char* operation) {
    if (!result) throw std::runtime_error(std::string(operation) + ": " + SDL_GetError());
}
struct Desktop::Impl {
    SDL_Window* window{};
    SDL_GLContext context{};
    std::shared_ptr<CursorProvider> cursors;
    using ReleaseEglThread=unsigned int (SDLCALL*)();
    ReleaseEglThread release_egl_thread=nullptr;
    bool initialized=false;
    std::unordered_map<SDL_JoystickID, SDL_Gamepad*> gamepads;
    bool relative{};
    WindowMode mode = WindowMode::Windowed;
    bool placement_valid = false;
    int requested_width{}, requested_height{}, windowed_width{}, windowed_height{};
    std::optional<CursorRectangle> cursor_rectangle;
    std::thread::id owner = std::this_thread::get_id();
    void require_owner() const {
        if (owner != std::this_thread::get_id()) throw std::logic_error("window operation on a different thread");
    }
    void pixels(float& x,float& y) const {
        int logical_w=0, logical_h=0, pixel_w=0, pixel_h=0;
        check(SDL_GetWindowSize(window,&logical_w,&logical_h) && SDL_GetWindowSizeInPixels(window,&pixel_w,&pixel_h),"get pointer coordinate scale");
        if (logical_w > 0) x *= float(pixel_w)/logical_w;
        if (logical_h > 0) y *= float(pixel_h)/logical_h;
        if (cursor_rectangle) {
            x=std::clamp(x,float(cursor_rectangle->x),float(std::int64_t(cursor_rectangle->x)+cursor_rectangle->width-1));
            y=std::clamp(y,float(cursor_rectangle->y),float(std::int64_t(cursor_rectangle->y)+cursor_rectangle->height-1));
        }
    }
    ~Impl() {
        for (auto [id, gamepad] : gamepads) SDL_CloseGamepad(gamepad);
        if (context) SDL_GL_DestroyContext(context);
        // EGL owns per-thread resources in addition to the context. Release
        // them while SDL still retains the EGL library through the window.
        if (release_egl_thread && !release_egl_thread()) {
            try { log("desktop","failed to release EGL thread state"); } catch (...) {}
        }
        if (window) SDL_DestroyWindow(window);
        if (initialized) SDL_QuitSubSystem(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD);
    }
    void open_gamepad(SDL_JoystickID id) {
        if (gamepads.contains(id)) return;
        if (auto* pad = SDL_OpenGamepad(id)) gamepads.emplace(id, pad);
    }
};
Desktop::Desktop(const DesktopConfig& config) : impl_(std::make_unique<Impl>()) {
    if (config.width <= 0 || config.height <= 0 || config.width > 16384 || config.height > 16384)
        throw std::invalid_argument("invalid game window dimensions");
    impl_->windowed_width=config.width; impl_->windowed_height=config.height;
    SDL_SetAppMetadata("No One Lives Forever", "0.1.0", "org.nolfsrc.NOLF");
    check(SDL_InitSubSystem(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD), "initialize desktop");
    impl_->initialized=true;
    check(SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2), "OpenGL major version");
    check(SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1), "OpenGL minor version");
    check(SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1), "OpenGL double buffering");
    check(SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24), "OpenGL depth buffer");
    check(SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8), "OpenGL stencil buffer");
    SDL_WindowFlags flags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;
    impl_->window = SDL_CreateWindow(config.title.c_str(), config.width, config.height, flags);
    check(impl_->window != nullptr, "create game window");
    impl_->context = SDL_GL_CreateContext(impl_->window);
    check(impl_->context != nullptr, "create OpenGL compatibility context");
    check(SDL_GL_MakeCurrent(impl_->window, impl_->context), "activate OpenGL context");
    if (SDL_EGL_GetCurrentDisplay()) {
#ifdef NOLF_SYSTEM_EGL
        // The system EGL dispatch library is a native runtime dependency. Its
        // entry points and vendor dispatch tables outlive individual windows.
        impl_->release_egl_thread=eglReleaseThread;
#else
        impl_->release_egl_thread=reinterpret_cast<Impl::ReleaseEglThread>(SDL_EGL_GetProcAddress("eglReleaseThread"));
#endif
        check(impl_->release_egl_thread!=nullptr,"load EGL thread cleanup");
    }
    SDL_GL_SetSwapInterval(config.vsync ? 1 : 0); // unsupported swap control is optional
    int count = 0;
    auto* pads = SDL_GetGamepads(&count);
    for (int i = 0; i < count; ++i) impl_->open_gamepad(pads[i]);
    SDL_free(pads);
    window_mode(config.fullscreen ? WindowMode::Fullscreen : WindowMode::Windowed,config.width,config.height);
}
Desktop::~Desktop() = default;
std::shared_ptr<CursorProvider> Desktop::cursor_service() {
    impl_->require_owner();
    if (!impl_->cursors) impl_->cursors=make_sdl_cursors();
    return impl_->cursors;
}
std::vector<InputEvent> Desktop::poll() {
    impl_->require_owner();
    std::vector<InputEvent> result;
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        InputEvent input{};
        switch (event.type) {
        case SDL_EVENT_QUIT: case SDL_EVENT_WINDOW_CLOSE_REQUESTED: input.type = InputType::quit; break;
        case SDL_EVENT_KEY_DOWN: case SDL_EVENT_KEY_UP:
            input.type = InputType::key; input.code = static_cast<int>(desktop_key_binding(event.key.scancode));
            input.pressed = event.key.down; input.repeat=event.key.repeat; input.device = event.key.which; break;
        case SDL_EVENT_TEXT_INPUT: input.type = InputType::text; input.text = event.text.text; break;
        case SDL_EVENT_MOUSE_MOTION:
            input.type = InputType::mouse_motion; input.x = event.motion.xrel; input.y = event.motion.yrel;
            input.pointer_x=event.motion.x; input.pointer_y=event.motion.y;
            impl_->pixels(input.pointer_x,input.pointer_y); break;
        case SDL_EVENT_MOUSE_BUTTON_DOWN: case SDL_EVENT_MOUSE_BUTTON_UP:
            input.type = InputType::mouse_button; input.code = event.button.button;
            input.pressed = event.button.down; input.clicks=event.button.clicks;
            input.pointer_x=event.button.x; input.pointer_y=event.button.y;
            impl_->pixels(input.pointer_x,input.pointer_y); break;
        case SDL_EVENT_MOUSE_WHEEL: input.type = InputType::wheel; input.x = event.wheel.x; input.y = event.wheel.y; break;
        case SDL_EVENT_WINDOW_FOCUS_LOST: case SDL_EVENT_WINDOW_FOCUS_GAINED:
            input.type = InputType::focus; input.pressed = event.type == SDL_EVENT_WINDOW_FOCUS_GAINED;
            check(SDL_SetWindowRelativeMouseMode(impl_->window, input.pressed && impl_->relative), "update mouse capture"); break;
        case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
            input.type = InputType::resize; input.x = static_cast<float>(event.window.data1); input.y = static_cast<float>(event.window.data2); break;
        case SDL_EVENT_GAMEPAD_ADDED:
            impl_->open_gamepad(event.gdevice.which);
            input.type = InputType::gamepad_added; input.device = event.gdevice.which; break;
        case SDL_EVENT_GAMEPAD_REMOVED:
            if (auto it = impl_->gamepads.find(event.gdevice.which); it != impl_->gamepads.end()) {
                SDL_CloseGamepad(it->second); impl_->gamepads.erase(it);
            }
            input.type = InputType::gamepad_removed; input.device = event.gdevice.which; break;
        case SDL_EVENT_GAMEPAD_AXIS_MOTION:
            input.type = InputType::gamepad_axis; input.device = event.gaxis.which; input.code = event.gaxis.axis;
            input.x = static_cast<float>(event.gaxis.value) / (event.gaxis.value < 0 ? 32768.0f : 32767.0f); break;
        case SDL_EVENT_GAMEPAD_BUTTON_DOWN: case SDL_EVENT_GAMEPAD_BUTTON_UP:
            input.type = InputType::gamepad_button; input.device = event.gbutton.which;
            input.code = event.gbutton.button; input.pressed = event.gbutton.down; break;
        default: continue;
        }
        result.push_back(std::move(input));
    }
    return result;
}
std::vector<DisplayMode> Desktop::display_modes() const {
    impl_->require_owner();
    int count = 0;
    auto** modes = SDL_GetFullscreenDisplayModes(SDL_GetDisplayForWindow(impl_->window), &count);
    check(modes != nullptr, "enumerate display modes");
    std::vector<DisplayMode> result;
    for (int i = 0; i < count; ++i) result.push_back({modes[i]->w, modes[i]->h, modes[i]->refresh_rate});
    SDL_free(modes);
    return result;
}
std::optional<GamepadState> Desktop::gamepad_state() const {
    impl_->require_owner();
    SDL_Gamepad* selected=nullptr;
    SDL_JoystickID id=0;
    for (auto [candidate,pad] : impl_->gamepads) {
        if (SDL_GamepadConnected(pad) && (!selected || candidate < id)) { selected=pad; id=candidate; }
    }
    if (!selected) return std::nullopt;
    GamepadState result{}; result.id=id;
    constexpr std::pair<SDL_GamepadButton,GamepadButton> buttons[] = {
        {SDL_GAMEPAD_BUTTON_DPAD_UP,GamepadButton::Up}, {SDL_GAMEPAD_BUTTON_DPAD_DOWN,GamepadButton::Down},
        {SDL_GAMEPAD_BUTTON_DPAD_LEFT,GamepadButton::Left}, {SDL_GAMEPAD_BUTTON_DPAD_RIGHT,GamepadButton::Right},
        {SDL_GAMEPAD_BUTTON_START,GamepadButton::Start}, {SDL_GAMEPAD_BUTTON_BACK,GamepadButton::Back},
        {SDL_GAMEPAD_BUTTON_LEFT_STICK,GamepadButton::LeftStick}, {SDL_GAMEPAD_BUTTON_RIGHT_STICK,GamepadButton::RightStick},
        {SDL_GAMEPAD_BUTTON_LEFT_SHOULDER,GamepadButton::LeftShoulder}, {SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER,GamepadButton::RightShoulder},
        {SDL_GAMEPAD_BUTTON_SOUTH,GamepadButton::South}, {SDL_GAMEPAD_BUTTON_EAST,GamepadButton::East},
        {SDL_GAMEPAD_BUTTON_WEST,GamepadButton::West}, {SDL_GAMEPAD_BUTTON_NORTH,GamepadButton::North}
    };
    for (auto [native,button] : buttons) if (SDL_GetGamepadButton(selected,native)) result.buttons |= static_cast<std::uint16_t>(button);
    auto axis=[&](SDL_GamepadAxis axis) { const int value=SDL_GetGamepadAxis(selected,axis); return value / (value < 0 ? 32768.0f : 32767.0f); };
    result.left_x=axis(SDL_GAMEPAD_AXIS_LEFTX); result.left_y=-axis(SDL_GAMEPAD_AXIS_LEFTY);
    result.right_x=axis(SDL_GAMEPAD_AXIS_RIGHTX); result.right_y=-axis(SDL_GAMEPAD_AXIS_RIGHTY);
    result.left_trigger=std::max(0.0f,axis(SDL_GAMEPAD_AXIS_LEFT_TRIGGER));
    result.right_trigger=std::max(0.0f,axis(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER));
    return result;
}
void Desktop::relative_mouse(bool enabled) {
    impl_->require_owner();
    check(SDL_SetWindowRelativeMouseMode(impl_->window, enabled && focused()), "set relative mouse"); impl_->relative = enabled;
}
DisplayMode Desktop::desktop_mode() const {
    impl_->require_owner();
    const auto* mode=SDL_GetDesktopDisplayMode(SDL_GetDisplayForWindow(impl_->window));
    check(mode != nullptr,"get desktop display mode");
    return {mode->w,mode->h,mode->refresh_rate};
}
void Desktop::window_mode(WindowMode mode,int width,int height) {
    impl_->require_owner();
    if (width <= 0 || height <= 0 || width > 16384 || height > 16384 ||
        (mode != WindowMode::Windowed && mode != WindowMode::Fullscreen && mode != WindowMode::Borderless))
        throw std::invalid_argument("invalid window mode");
    if (impl_->placement_valid && impl_->mode == mode && impl_->requested_width == width && impl_->requested_height == height) return;
    impl_->placement_valid = false;
    if (mode == WindowMode::Windowed) {
        check(SDL_SetWindowFullscreen(impl_->window,false),"leave fullscreen");
        check(SDL_SetWindowBordered(impl_->window,true),"set window frame");
        check(SDL_SetWindowSize(impl_->window,width,height),"set window size");
        if (impl_->mode != mode) check(SDL_SetWindowPosition(impl_->window,SDL_WINDOWPOS_CENTERED,SDL_WINDOWPOS_CENTERED),"center game window");
        impl_->windowed_width=width; impl_->windowed_height=height;
    } else {
        SDL_DisplayMode closest{};
        const auto* selected=mode == WindowMode::Fullscreen && SDL_GetClosestFullscreenDisplayMode(
            SDL_GetDisplayForWindow(impl_->window),width,height,0,true,&closest) ? &closest : nullptr;
        check(SDL_SetWindowFullscreenMode(impl_->window,selected),"select fullscreen display mode");
        check(SDL_SetWindowFullscreen(impl_->window,true),"enter fullscreen");
    }
    impl_->mode=mode; impl_->requested_width=width; impl_->requested_height=height;
    impl_->placement_valid = true;
}
WindowMode Desktop::window_mode() const { impl_->require_owner(); return impl_->mode; }
bool Desktop::focused() const { impl_->require_owner(); return (SDL_GetWindowFlags(impl_->window)&SDL_WINDOW_INPUT_FOCUS) != 0; }
std::uint32_t Desktop::key_modifiers() const {
    impl_->require_owner();
    const auto state=SDL_GetModState();
    std::uint32_t result=0;
    for (const auto [native,portable] : {std::pair{SDL_KMOD_SHIFT,KeyModifier::Shift},std::pair{SDL_KMOD_CTRL,KeyModifier::Control},
        std::pair{SDL_KMOD_ALT,KeyModifier::Alt},std::pair{SDL_KMOD_GUI,KeyModifier::Meta},
        std::pair{SDL_KMOD_CAPS,KeyModifier::CapsLock},std::pair{SDL_KMOD_NUM,KeyModifier::NumLock}})
        if (state & native) result |= static_cast<std::uint32_t>(portable);
    return result;
}
std::string Desktop::title() const { impl_->require_owner(); return SDL_GetWindowTitle(impl_->window); }
void Desktop::cursor_visible(bool enabled) {
    impl_->require_owner(); check(enabled ? SDL_ShowCursor() : SDL_HideCursor(),"set cursor visibility");
}
bool Desktop::confine_cursor(std::optional<CursorRectangle> rectangle) {
    impl_->require_owner();
    if (!rectangle) { impl_->cursor_rectangle.reset(); return SDL_SetWindowMouseRect(impl_->window,nullptr); }
    if (rectangle->width <= 0 || rectangle->height <= 0 ||
        std::int64_t(rectangle->x)+rectangle->width > std::numeric_limits<int>::max() ||
        std::int64_t(rectangle->y)+rectangle->height > std::numeric_limits<int>::max())
        throw std::invalid_argument("invalid cursor rectangle");
    int logical_w=0,logical_h=0,pixel_w=0,pixel_h=0;
    check(SDL_GetWindowSize(impl_->window,&logical_w,&logical_h) && SDL_GetWindowSizeInPixels(impl_->window,&pixel_w,&pixel_h),"get cursor clipping scale");
    if (pixel_w <= 0 || pixel_h <= 0) return false;
    const auto left=std::clamp<std::int64_t>(rectangle->x,0,pixel_w), top=std::clamp<std::int64_t>(rectangle->y,0,pixel_h);
    const auto right=std::clamp<std::int64_t>(std::int64_t(rectangle->x)+rectangle->width,0,pixel_w);
    const auto bottom=std::clamp<std::int64_t>(std::int64_t(rectangle->y)+rectangle->height,0,pixel_h);
    if (right <= left || bottom <= top) {
        impl_->cursor_rectangle.reset(); SDL_SetWindowMouseRect(impl_->window,nullptr); return false;
    }
    rectangle=CursorRectangle{int(left),int(top),int(right-left),int(bottom-top)};
    const auto xscale=double(logical_w)/pixel_w, yscale=double(logical_h)/pixel_h;
    SDL_Rect clip{int(std::floor(rectangle->x*xscale)),int(std::floor(rectangle->y*yscale)),
        std::max(1,int(std::ceil(rectangle->width*xscale))),std::max(1,int(std::ceil(rectangle->height*yscale)))};
    impl_->cursor_rectangle=rectangle;
    // Logical pointer events remain clipped on backends without native
    // pointer confinement. The return value reports native support.
    return SDL_SetWindowMouseRect(impl_->window,&clip);
}
PointerPosition Desktop::pointer_position() const {
    impl_->require_owner(); PointerPosition result{};
    SDL_GetMouseState(&result.x,&result.y); impl_->pixels(result.x,result.y); return result;
}
void Desktop::pointer_position(PointerPosition position) {
    impl_->require_owner();
    if (!std::isfinite(position.x) || !std::isfinite(position.y)) throw std::invalid_argument("invalid pointer position");
    int logical_w=0,logical_h=0,pixel_w=0,pixel_h=0;
    check(SDL_GetWindowSize(impl_->window,&logical_w,&logical_h) && SDL_GetWindowSizeInPixels(impl_->window,&pixel_w,&pixel_h),"get cursor position scale");
    if (pixel_w > 0 && pixel_h > 0) SDL_WarpMouseInWindow(impl_->window,position.x*logical_w/pixel_w,position.y*logical_h/pixel_h);
}
void Desktop::text_input(bool enabled) {
    impl_->require_owner();
    check(enabled ? SDL_StartTextInput(impl_->window) : SDL_StopTextInput(impl_->window), "set text input");
}
void Desktop::fullscreen(bool enabled) { window_mode(enabled ? WindowMode::Fullscreen : WindowMode::Windowed,impl_->windowed_width,impl_->windowed_height); }
void Desktop::swap() { check(SDL_GL_SwapWindow(impl_->window), "swap buffers"); }
bool Desktop::make_current() { return SDL_GL_MakeCurrent(impl_->window, impl_->context); }
void Desktop::release_context() {
    check(SDL_GL_MakeCurrent(impl_->window,nullptr),"release OpenGL context");
    if (impl_->release_egl_thread && !impl_->release_egl_thread()) throw std::runtime_error("release EGL thread state failed");
}
void Desktop::swap_interval(int interval) { SDL_GL_SetSwapInterval(interval); }
void* Desktop::gl_symbol(const char* name) const { return reinterpret_cast<void*>(SDL_GL_GetProcAddress(name)); }
std::string Desktop::clipboard() const {
    impl_->require_owner();
    char* text = SDL_GetClipboardText();
    check(text != nullptr, "read clipboard"); std::string result(text); SDL_free(text); return result;
}
void Desktop::clipboard(const std::string& text) { impl_->require_owner(); check(SDL_SetClipboardText(text.c_str()), "write clipboard"); }
const char* Desktop::driver() const { return SDL_GetCurrentVideoDriver(); }
void Desktop::drawable_size(int& width, int& height) const {
    check(SDL_GetWindowSizeInPixels(impl_->window, &width, &height), "get drawable size");
}
}
