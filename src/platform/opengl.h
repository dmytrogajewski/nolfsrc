#pragma once
// Platform OpenGL header details are confined to this adapter header.
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#define NOLF_GL_CALL APIENTRY
#else
#define NOLF_GL_CALL
#endif
#include <GL/gl.h>
