#include "worldmodels.h"
#include "ltobject_internal.h"
#include <charconv>
#include <string_view>

int CServerWorld::AttachWorldModel(LTObject* object, char* name) {
    if (!object || !name) return 0;
    std::string_view value(name);
    if (!value.starts_with("#$#")) {
        auto* record = FindWorldModel(name);
        if (!record) return 0;
        object->SetWorldBsp(record->m_pWorldBsp, record->m_pOriginal); return 1;
    }
    value.remove_prefix(3);
    while (!value.empty() && value.front() == ' ') value.remove_prefix(1);
    uint32 index = 0;
    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), index);
    if (parsed.ec != std::errc{} || parsed.ptr == value.data() + value.size() || *parsed.ptr != ' ') return 0;
    value.remove_prefix(parsed.ptr - value.data());
    while (!value.empty() && value.front() == ' ') value.remove_prefix(1);
    auto* record = FindWorldModel(std::string(value).c_str());
    if (!record || index >= record->m_pWorldBsp->m_Array138.GetSize()) return 0;
    auto& terrain = record->m_pWorldBsp->m_Array138[index];
    const auto* geometry = Geometry();
    const auto* bounds = geometry ? w_FindLayoutNode(geometry->layout, terrain.m_Header48) : nullptr;
    if (!bounds) return 0;
    const auto dims = (bounds->max - bounds->min) * .5f;
    object->SetWorldBsp(&terrain, nullptr); object->SetDims(dims); object->SetPos(bounds->min + dims); return 1;
}
