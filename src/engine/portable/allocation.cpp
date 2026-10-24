#include "engine/portable/allocation.h"
#include <cstring>
#include <new>

void* dalloc(uint32 size) {
    return size ? ::operator new(size, std::nothrow) : nullptr;
}
void* dalloc_z(uint32 size) {
    auto* memory = dalloc(size);
    if (memory) std::memset(memory, 0, size);
    return memory;
}
void dfree(void* memory) { ::operator delete(memory); }
