#pragma once
#include "platform/window.h"
#include <memory>

namespace nolf::platform {
class CursorProvider;
struct DesktopConfig {
    std::string title{"No One Lives Forever"};
    int width{1280}, height{720};
    bool fullscreen{}, vsync{true};
};
// No SDL or native window handles cross this interface. All calls are on the main thread.
class Desktop final : public Window {
public:
    explicit Desktop(const DesktopConfig& config);
    ~Desktop() override;
    Desktop(const Desktop&) = delete;
    Desktop& operator=(const Desktop&) = delete;
    std::vector<InputEvent> poll();
    std::vector<DisplayMode> display_modes() const override;
    DisplayMode desktop_mode() const override;
    void window_mode(WindowMode, int width, int height) override;
    WindowMode window_mode() const override;
    bool focused() const override;
    std::uint32_t key_modifiers() const override;
    std::optional<GamepadState> gamepad_state() const override;
    std::string title() const override;
    void relative_mouse(bool enabled) override;
    void cursor_visible(bool enabled) override;
    void text_input(bool enabled) override;
    bool confine_cursor(std::optional<CursorRectangle>) override;
    PointerPosition pointer_position() const override;
    void pointer_position(PointerPosition) override;
    void fullscreen(bool enabled);
    void swap();
    bool make_current();
    void release_context();
    void swap_interval(int interval);
    void* gl_symbol(const char* name) const;
    std::string clipboard() const override;
    void clipboard(const std::string& text) override;
    const char* driver() const;
    std::shared_ptr<CursorProvider> cursor_service();
    void drawable_size(int& width, int& height) const override;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
