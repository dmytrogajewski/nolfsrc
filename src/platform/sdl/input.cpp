#include "platform/input.h"
#include <SDL3/SDL.h>
#include <array>

namespace nolf::platform {
namespace {
struct Key { SDL_Scancode scan; unsigned binding,logical; };
constexpr Key keys[]={
    {SDL_SCANCODE_ESCAPE,1,0x1b},{SDL_SCANCODE_1,2,'1'},{SDL_SCANCODE_2,3,'2'},{SDL_SCANCODE_3,4,'3'},{SDL_SCANCODE_4,5,'4'},{SDL_SCANCODE_5,6,'5'},{SDL_SCANCODE_6,7,'6'},{SDL_SCANCODE_7,8,'7'},{SDL_SCANCODE_8,9,'8'},{SDL_SCANCODE_9,10,'9'},{SDL_SCANCODE_0,11,'0'},
    {SDL_SCANCODE_MINUS,12,0xbd},{SDL_SCANCODE_EQUALS,13,0xbb},{SDL_SCANCODE_BACKSPACE,14,8},{SDL_SCANCODE_TAB,15,9},
    {SDL_SCANCODE_Q,16,'Q'},{SDL_SCANCODE_W,17,'W'},{SDL_SCANCODE_E,18,'E'},{SDL_SCANCODE_R,19,'R'},{SDL_SCANCODE_T,20,'T'},{SDL_SCANCODE_Y,21,'Y'},{SDL_SCANCODE_U,22,'U'},{SDL_SCANCODE_I,23,'I'},{SDL_SCANCODE_O,24,'O'},{SDL_SCANCODE_P,25,'P'},
    {SDL_SCANCODE_LEFTBRACKET,26,0xdb},{SDL_SCANCODE_RIGHTBRACKET,27,0xdd},{SDL_SCANCODE_RETURN,28,13},{SDL_SCANCODE_LCTRL,29,0x11},
    {SDL_SCANCODE_A,30,'A'},{SDL_SCANCODE_S,31,'S'},{SDL_SCANCODE_D,32,'D'},{SDL_SCANCODE_F,33,'F'},{SDL_SCANCODE_G,34,'G'},{SDL_SCANCODE_H,35,'H'},{SDL_SCANCODE_J,36,'J'},{SDL_SCANCODE_K,37,'K'},{SDL_SCANCODE_L,38,'L'},
    {SDL_SCANCODE_SEMICOLON,39,0xba},{SDL_SCANCODE_APOSTROPHE,40,0xde},{SDL_SCANCODE_GRAVE,41,0xc0},{SDL_SCANCODE_LSHIFT,42,0x10},{SDL_SCANCODE_BACKSLASH,43,0xdc},
    {SDL_SCANCODE_Z,44,'Z'},{SDL_SCANCODE_X,45,'X'},{SDL_SCANCODE_C,46,'C'},{SDL_SCANCODE_V,47,'V'},{SDL_SCANCODE_B,48,'B'},{SDL_SCANCODE_N,49,'N'},{SDL_SCANCODE_M,50,'M'},
    {SDL_SCANCODE_COMMA,51,0xbc},{SDL_SCANCODE_PERIOD,52,0xbe},{SDL_SCANCODE_SLASH,53,0xbf},{SDL_SCANCODE_RSHIFT,54,0x10},{SDL_SCANCODE_KP_MULTIPLY,55,0x6a},{SDL_SCANCODE_LALT,56,0x12},{SDL_SCANCODE_SPACE,57,32},{SDL_SCANCODE_CAPSLOCK,58,0x14},
    {SDL_SCANCODE_F1,59,0x70},{SDL_SCANCODE_F2,60,0x71},{SDL_SCANCODE_F3,61,0x72},{SDL_SCANCODE_F4,62,0x73},{SDL_SCANCODE_F5,63,0x74},{SDL_SCANCODE_F6,64,0x75},{SDL_SCANCODE_F7,65,0x76},{SDL_SCANCODE_F8,66,0x77},{SDL_SCANCODE_F9,67,0x78},{SDL_SCANCODE_F10,68,0x79},
    {SDL_SCANCODE_NUMLOCKCLEAR,69,0x90},{SDL_SCANCODE_SCROLLLOCK,70,0x91},{SDL_SCANCODE_KP_7,71,0x67},{SDL_SCANCODE_KP_8,72,0x68},{SDL_SCANCODE_KP_9,73,0x69},{SDL_SCANCODE_KP_MINUS,74,0x6d},{SDL_SCANCODE_KP_4,75,0x64},{SDL_SCANCODE_KP_5,76,0x65},{SDL_SCANCODE_KP_6,77,0x66},{SDL_SCANCODE_KP_PLUS,78,0x6b},{SDL_SCANCODE_KP_1,79,0x61},{SDL_SCANCODE_KP_2,80,0x62},{SDL_SCANCODE_KP_3,81,0x63},{SDL_SCANCODE_KP_0,82,0x60},{SDL_SCANCODE_KP_PERIOD,83,0x6e},
    {SDL_SCANCODE_NONUSBACKSLASH,86,0xe2},{SDL_SCANCODE_F11,87,0x7a},{SDL_SCANCODE_F12,88,0x7b},{SDL_SCANCODE_F13,100,0x7c},{SDL_SCANCODE_F14,101,0x7d},{SDL_SCANCODE_F15,102,0x7e},{SDL_SCANCODE_INTERNATIONAL1,115,0xe2},
    {SDL_SCANCODE_KP_ENTER,156,13},{SDL_SCANCODE_RCTRL,157,0x11},{SDL_SCANCODE_KP_DIVIDE,181,0x6f},{SDL_SCANCODE_PRINTSCREEN,183,0x2c},{SDL_SCANCODE_RALT,184,0x12},{SDL_SCANCODE_PAUSE,197,0x13},
    {SDL_SCANCODE_HOME,199,0x24},{SDL_SCANCODE_UP,200,0x26},{SDL_SCANCODE_PAGEUP,201,0x21},{SDL_SCANCODE_LEFT,203,0x25},{SDL_SCANCODE_RIGHT,205,0x27},{SDL_SCANCODE_END,207,0x23},{SDL_SCANCODE_DOWN,208,0x28},{SDL_SCANCODE_PAGEDOWN,209,0x22},{SDL_SCANCODE_INSERT,210,0x2d},{SDL_SCANCODE_DELETE,211,0x2e},{SDL_SCANCODE_LGUI,219,0x5b},{SDL_SCANCODE_RGUI,220,0x5c},{SDL_SCANCODE_APPLICATION,221,0x5d}
};
}
std::uint32_t desktop_key_binding(int scan) { for (const auto& key : keys) if (key.scan==scan) return key.binding; return 0; }
std::vector<InputControl> desktop_input_controls() {
    std::vector<InputControl> result; result.reserve(std::size(keys)+30);
    for (const auto& key : keys) {
        auto label=std::string(SDL_GetKeyName(SDL_GetKeyFromScancode(key.scan,SDL_KMOD_NONE,false)));
        if (label.empty()) label=SDL_GetScancodeName(key.scan);
        result.push_back({1,9,key.binding,key.logical,"Keyboard",std::to_string(key.binding),std::move(label),0,255,false});
    }
    for (unsigned axis=0;axis<3;++axis) result.push_back({2,axis+1,256+axis,0,"Mouse",std::string(1,char('x'+axis))+"-axis",std::string(1,char('X'+axis))+" axis",-32768,32767,true});
    for (unsigned button=0;button<5;++button) result.push_back({2,8,button,0,"Mouse","button "+std::to_string(button),"Button "+std::to_string(button+1),0,255,false});
    // The Window service already normalizes controller axes and buttons.
    constexpr const char* axes[]={"x-axis","y-axis","z-axis","rx-axis","ry-axis","rz-axis"};
    for (unsigned axis=0;axis<6;++axis) result.push_back({4,axis+1,256+axis,0,"Joystick 1",axes[axis],axes[axis],-32768,32767,false});
    for (unsigned button=0;button<16;++button) result.push_back({4,8,button,0,"Joystick 1","button "+std::to_string(button),"Button "+std::to_string(button+1),0,255,false});
    return result;
}
}
