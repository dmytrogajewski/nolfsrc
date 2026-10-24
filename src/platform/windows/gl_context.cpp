#include "platform/gl_context.h"
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <GL/gl.h>
#include <stdint.h>

namespace {
struct WglContext { HWND window; HDC dc; HGLRC context; };
WglContext context{};
void release(void* user) {
    auto& ctx = *static_cast<WglContext*>(user);
    if (ctx.context) { wglMakeCurrent(nullptr, nullptr); wglDeleteContext(ctx.context); ctx.context = nullptr; }
    if (ctx.dc) { ReleaseDC(ctx.window, ctx.dc); ctx.dc = nullptr; }
}
bool make_current(void* user) {
    auto& ctx = *static_cast<WglContext*>(user);
    if (ctx.context) return wglMakeCurrent(ctx.dc, ctx.context);
    ctx.dc = GetDC(ctx.window);
    if (!ctx.dc) return false;
    if (!GetPixelFormat(ctx.dc)) {
        PIXELFORMATDESCRIPTOR format{};
        format.nSize = sizeof(format); format.nVersion = 1;
        format.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
        format.iPixelType = PFD_TYPE_RGBA; format.cColorBits = 32; format.cAlphaBits = 8;
        format.cDepthBits = 24; format.cStencilBits = 8; format.iLayerType = PFD_MAIN_PLANE;
        const int index = ChoosePixelFormat(ctx.dc, &format);
        if (!index || !SetPixelFormat(ctx.dc, index, &format)) { release(user); return false; }
    }
    ctx.context = wglCreateContext(ctx.dc);
    if (!ctx.context || !wglMakeCurrent(ctx.dc, ctx.context)) { release(user); return false; }
    return true;
}
void* symbol(void*, const char* name) {
    auto address = wglGetProcAddress(name);
    const auto value = reinterpret_cast<intptr_t>(address);
    if (!address || value == 1 || value == 2 || value == 3 || value == -1) {
        const auto module = GetModuleHandleW(L"opengl32.dll");
        address = module ? GetProcAddress(module, name) : nullptr;
    }
    return reinterpret_cast<void*>(address);
}
void present(void* user) { SwapBuffers(static_cast<WglContext*>(user)->dc); }
void swap_interval(void* user, int interval) {
    typedef BOOL (APIENTRY* SwapInterval)(int);
    const auto fn = reinterpret_cast<SwapInterval>(symbol(user, "wglSwapIntervalEXT"));
    if (fn) fn(interval);
}
}
NolfGLContext nolf_gl_context(void* host) {
    context.window = static_cast<HWND>(host);
    return {&context, make_current, release, present, symbol, swap_interval};
}
