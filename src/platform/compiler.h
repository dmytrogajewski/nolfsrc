#pragma once

// Keep compiler and binary-interface spelling out of engine and SDK code.
#if defined(_WIN32)
#define NOLF_EXPORT __declspec(dllexport)
#define NOLF_IMPORT __declspec(dllimport)
#else
#define NOLF_EXPORT __attribute__((visibility("default")))
#define NOLF_IMPORT
#endif

#if defined(_WIN32) && !defined(_WIN64)
#define NOLF_STDCALL __stdcall
#define NOLF_CDECL __cdecl
#define NOLF_THISCALL __thiscall
#else
#define NOLF_STDCALL
#define NOLF_CDECL
#define NOLF_THISCALL
#endif

#if defined(_MSC_VER)
#define NOLF_FORCE_INLINE __forceinline
#define NOLF_CPP_ABI_ID 0x4d534356u
#elif defined(__GNUC__) || defined(__clang__)
#define NOLF_FORCE_INLINE inline __attribute__((always_inline))
#define NOLF_CPP_ABI_ID 0x4954414eu
#else
#define NOLF_FORCE_INLINE inline
#define NOLF_CPP_ABI_ID 0u
#endif
