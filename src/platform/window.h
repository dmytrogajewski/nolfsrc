#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace nolf::platform {
struct DisplayMode { int width, height; float refresh_rate; };
enum class WindowMode { Fullscreen, Windowed, Borderless };
enum class MouseButton { Left=1, Middle=2, Right=3, Extra1=4, Extra2=5 };
enum class KeyModifier : std::uint32_t { Shift=1, Control=2, Alt=4, Meta=8, CapsLock=16, NumLock=32 };
inline bool has_modifier(std::uint32_t flags,KeyModifier modifier) { return (flags & static_cast<std::uint32_t>(modifier)) != 0; }
struct CursorRectangle { int x, y, width, height; };
struct PointerPosition { float x, y; };
// Stable game-facing buttons; axes use [-1,1], Y points up, triggers [0,1].
enum class GamepadButton : std::uint16_t {
    Up=0x0001, Down=0x0002, Left=0x0004, Right=0x0008,
    Start=0x0010, Back=0x0020, LeftStick=0x0040, RightStick=0x0080,
    LeftShoulder=0x0100, RightShoulder=0x0200,
    South=0x1000, East=0x2000, West=0x4000, North=0x8000
};
struct GamepadState {
    std::uint32_t id{};
    std::uint16_t buttons{};
    float left_x{}, left_y{}, right_x{}, right_y{}, left_trigger{}, right_trigger{};
};
enum class InputType { quit, key, text, mouse_motion, mouse_button, wheel, focus, resize, gamepad_added, gamepad_removed, gamepad_axis, gamepad_button };
struct InputEvent {
    InputType type;
    std::uint32_t device{};
    int code{};
    bool pressed{}, repeat{};
    float x{}, y{};
    // Pointer positions use drawable pixels; motion x/y remain relative input.
    float pointer_x{}, pointer_y{};
    std::uint8_t clicks{};
    std::string text;
};

// The engine owns this service and outlives callbacks into the game module.
// Window/input operations run on the window's owner thread. No native handles
// or toolkit types cross the game-module boundary.
class Window {
public:
    virtual ~Window() = default;
    virtual std::vector<DisplayMode> display_modes() const = 0;
    virtual DisplayMode desktop_mode() const = 0;
    virtual void window_mode(WindowMode, int width, int height) = 0;
    virtual WindowMode window_mode() const = 0;
    virtual bool focused() const = 0;
    virtual std::uint32_t key_modifiers() const = 0;
    virtual std::optional<GamepadState> gamepad_state() const = 0;
    virtual std::string title() const = 0;
    virtual void relative_mouse(bool enabled) = 0;
    virtual void cursor_visible(bool enabled) = 0;
    virtual void text_input(bool enabled) = 0;
    virtual bool confine_cursor(std::optional<CursorRectangle>) = 0;
    virtual PointerPosition pointer_position() const = 0;
    virtual void pointer_position(PointerPosition) = 0;
    virtual std::string clipboard() const = 0;
    virtual void clipboard(const std::string&) = 0;
    virtual void drawable_size(int& width, int& height) const = 0;
};
}
