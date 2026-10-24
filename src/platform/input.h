#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace nolf::platform {
// Device/type values are the engine's persisted binding format. Keyboard
// InputEvent::code is the physical legacy binding number, independent of SDL.
struct InputControl {
    std::uint32_t device=0,type=0,code=0,logical_key=0;
    std::string device_name,name,label;
    float minimum=0,maximum=255;
    bool relative=false;
};
std::vector<InputControl> desktop_input_controls();
// Used only by the SDL event adapter to normalize its physical scan code.
std::uint32_t desktop_key_binding(int native_scancode);
}
