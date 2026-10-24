#pragma once
// Renderer-facing context operations. The host owns the window. The adapter
// defines whether release destroys a context or detaches a host-owned one.
struct NolfGLContext {
    void* user;
    bool (*make_current)(void*);
    void (*release)(void*);
    void (*present)(void*);
    void* (*symbol)(void*, const char*);
    void (*swap_interval)(void*, int);
};
// The host token is interpreted only by the selected platform adapter.
NolfGLContext nolf_gl_context(void* host);
