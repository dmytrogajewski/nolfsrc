#pragma once
#include "ltintegertypes.h"

// Engine raw allocations use the same scalar allocation family as placement
// construction. There is no process-wide operator new/delete override.
void* dalloc(uint32 size);
void* dalloc_z(uint32 size);
void dfree(void* memory);
