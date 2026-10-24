#ifndef __LTINTEGERTYPES_H__
#define __LTINTEGERTYPES_H__
#include <stdint.h>
#include "platform/compiler.h"

// Fixed field bound used by the original game formats and SDK records.
#define LT_MAX_PATH 260

typedef unsigned int uint;
typedef int8_t int8;
typedef uint8_t uint8;
typedef int16_t int16;
typedef uint16_t uint16;
typedef int32_t int32;
typedef uint32_t uint32;
typedef int64_t int64;
typedef uint64_t uint64;
typedef unsigned int LTBOOL;
typedef float LTFLOAT;
typedef double LTDFLOAT;
typedef uint32 LTRESULT;

#endif
