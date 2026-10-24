// World geometry loading and platform-independent host object creation.
#include "world_loader.h"
#include "worldbsp.h"
#include "world_object_record.h"
#include "servermgr.h"
#include "engine/portable/allocation.h"
#include <cstring>
#include <exception>
#include <memory>
#include <vector>

int fs_GetTableName(void* table, uint32 entry, char* name, uint32 capacity) {
    if (!table || !name) return 0;
    auto text = std::string("#$#") + std::to_string(entry) + " " + static_cast<const char*>(table);
    if (text.size() >= capacity) return 0;
    std::memcpy(name, text.c_str(), text.size() + 1); return 1;
}
