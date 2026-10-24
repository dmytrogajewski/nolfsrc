#include "platform/gl_context.h"
#include "platform/desktop.h"

NolfGLContext nolf_gl_context(void* host) {
    return {
        host,
        [](void* host) { return static_cast<nolf::platform::Desktop*>(host)->make_current(); },
        [](void* host) { static_cast<nolf::platform::Desktop*>(host)->release_context(); },
        [](void* host) { static_cast<nolf::platform::Desktop*>(host)->swap(); },
        [](void* host, const char* name) { return static_cast<nolf::platform::Desktop*>(host)->gl_symbol(name); },
        [](void* host, int interval) { static_cast<nolf::platform::Desktop*>(host)->swap_interval(interval); }
    };
}
