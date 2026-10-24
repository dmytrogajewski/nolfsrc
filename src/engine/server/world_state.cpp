#include "world_state.h"
#include "engine/portable/memory_stream.h"
#include "engine/portable/binary.h"
#include "engine/portable/string.h"
#include "engine/portable/wire.h"
#include "engine/portable/command_reader.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <sstream>
#include <locale>
#include <stdexcept>

void mat_GetEulerVectors(LTVector, LTVector*, LTVector*, LTVector*);
namespace {
bool finite(LTVector v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }
std::string string(nolf::BinaryReader& reader) {
    const auto bytes = reader.bytes(reader.integer<uint16>());
    return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}
std::vector<WorldObjectDefinition> read_objects(std::span<const std::byte> bytes, uint32 offset) {
    if (offset > bytes.size()) throw std::runtime_error("invalid world object section offset");
    nolf::BinaryReader reader(bytes.subspan(offset));
    const auto count = reader.integer<uint32>();
    if (count > reader.remaining() / 8) throw std::runtime_error("invalid world object count");
    std::vector<WorldObjectDefinition> result; result.reserve(count);
    for (uint32 i = 0; i < count; ++i) {
        nolf::BinaryReader record(reader.bytes(reader.integer<uint16>()));
        WorldObjectDefinition object; object.class_name = string(record);
        const auto properties = record.integer<uint32>();
        if (properties > record.remaining() / 9) throw std::runtime_error("invalid world property count");
        object.properties.reserve(properties);
        for (uint32 j = 0; j < properties; ++j) {
            WorldProperty property; property.name = string(record); property.type = record.integer<uint8>();
            property.flags = record.integer<uint32>();
            auto data = record.bytes(record.integer<uint16>()); property.data.assign(data.begin(), data.end());
            object.properties.push_back(std::move(property));
        }
        result.push_back(std::move(object));
    }
    return result;
}
LTVector vector(const WorldProperty& property) {
    // Rotation properties store three Euler angles in a four-float record.
    const std::size_t size = property.type == 7 ? 16 : 12;
    if (property.data.size() != size) throw std::runtime_error("invalid world vector property " + property.name + ": " + std::to_string(property.data.size()));
    LTVector value;
    for (uint32 i = 0; i < 3; ++i) value[i] = nolf::wire::load<float>(property.data.data() + i * 4);
    if (!finite(value)) throw std::runtime_error("nonfinite world vector property");
    return value;
}
float real(const WorldProperty& property) {
    if (property.data.size() != 4) throw std::runtime_error("invalid world real property");
    const auto value = nolf::wire::load<float>(property.data.data());
    if (!std::isfinite(value)) throw std::runtime_error("nonfinite world real property");
    return value;
}
bool boolean(const WorldProperty& property) {
    if (property.data.size() != 1) throw std::runtime_error("invalid world boolean property");
    return property.data.front() != std::byte{};
}
std::vector<WorldLight> read_lights(const std::vector<WorldObjectDefinition>& objects) {
    std::vector<WorldLight> lights;
    for (const auto& object : objects) {
        if (object.class_name != "Light" && object.class_name != "DirLight" && object.class_name != "ObjectLight") continue;
        WorldLight light; bool enabled = true;
        for (const auto& property : object.properties) {
            if (property.name == "LightRadius" && property.type == 3) light.radius = real(property);
            else if ((property.name == "LightColor" || property.name == "InnerColor") && property.type == 2) light.color = vector(property);
            else if (property.name == "OuterColor" && property.type == 2) light.outer_color = vector(property);
            else if (property.name == "Pos" && property.type == 1) light.position = vector(property);
            else if (property.name == "LightObjects" && property.type == 5) enabled = boolean(property);
            else if (property.name == "FastLightObjects" && property.type == 5) light.fast = boolean(property);
            else if (property.name == "Rotation" && property.type == 7) {
                LTVector right, up; mat_GetEulerVectors(vector(property), &right, &up, &light.direction);
            } else if (property.name == "FOV" && property.type == 3) light.fov_cos = std::cos(real(property) * 3.14159265358979323846f / 360.f);
        }
        if (enabled && light.radius > 0) lights.push_back(light);
    }
    return lights;
}
std::pair<LTVector,float> settings(std::string information) {
    nolf::CommandReader input(information);
    LTVector ambient(0,0,0); float cell = 250;
    bool got_ambient = false, got_cell = false;
    const auto number = [](const std::string& text) {
        std::istringstream stream(text); stream.imbue(std::locale::classic()); float value = 0;
        if (!(stream >> value) || !std::isfinite(value)) throw std::runtime_error("invalid world lighting number");
        return value;
    };
    while (auto statement = input.next()) {
        if (statement->empty()) continue;
        if (!got_ambient && statement->size() >= 4 && nolf::ascii_compare((*statement)[0], "AmbientLight") == 0) {
            ambient.Init(number((*statement)[1]),number((*statement)[2]),number((*statement)[3])); got_ambient = true;
        } else if (!got_cell && statement->size() >= 2 && nolf::ascii_compare((*statement)[0], "LightTableRes") == 0) {
            cell = number((*statement)[1]); got_cell = true;
        }
    }
    if (!finite(ambient) || !std::isfinite(cell) || cell <= 0) throw std::runtime_error("invalid world lighting settings");
    return {ambient,cell};
}
}
CServerWorld::~CServerWorld() { Term(); }
void CServerWorld::TermObjects() { auto* stream = m_pObjectStream; m_pObjectStream = nullptr; if (stream) stream->Release(); }
void CServerWorld::Term() {
    if (terminating_) return;
    terminating_ = true;
    m_bLoaded = 0; m_Tree.Term(); TermObjects();
    auto objects = std::move(world_objects_); objects.clear();
    TermArrays(); m_Shade.Clear(); lights_.clear(); definitions_.clear();
    m_Polies.Term(); m_Worlds.Term(); geometry_.reset(); source_.reset();
    m_vMin.Init(); m_vMax.Init(); m_v15c.Init(); m_vBoxMin.Init(); m_vBoxMax.Init();
    m_Flags180 = 0; m_nObjectsOffset = 0; terminating_ = false;
    position_reference_ = {};
}
int CServerWorld::Load(WorldLoadRequest* request) {
    last_error_.clear();
    if (!request || !request->m_pStream) return 3;
    if (terminating_ || request->m_pStream == m_pObjectStream) return 2;
    bool committed = false;
    try {
        uint32 length = 0;
        if (request->m_pStream->GetLen(&length) != LT_OK || length > 512u * 1024 * 1024 || request->m_pStream->SeekTo(0) != LT_OK) return 2;
        auto bytes = std::make_shared<std::vector<std::byte>>(length);
        if (request->m_pStream->Read(bytes->data(), length) != LT_OK) return 2;
        nolf::MemoryStream stream(*bytes);
        auto geometry = w_ReadWorldGeometry(stream);
        for (const auto& model : geometry->models)
            for (uint32 i = 0; i < model->m_pWorldBsp->m_Array138.GetSize(); ++i)
                if (!w_FindLayoutNode(geometry->layout, model->m_pWorldBsp->m_Array138[i].m_Header48))
                    throw std::runtime_error("invalid terrain spatial path");
        auto definitions = read_objects(*bytes, geometry->header[1]);
        auto lights = read_lights(definitions);
        const auto [ambient, cell] = settings(geometry->information);
        WorldShadeGrid shade; shade.Setup(geometry->min, geometry->max, cell, ambient);
        for (const auto& light : lights) if (light.fast) shade.AddLight(light);
        CMoArray<CServerWorldRec*> models;
        if (!models.SetSize(static_cast<uint32>(geometry->models.size()))) throw std::bad_alloc();
        for (uint32 i = 0; i < models.GetSize(); ++i) models[i] = geometry->models[i].get();
        CMoArray<CWorldPart*> polies;
        uint64 count = 0;
        for (const auto& record : geometry->models) for (uint32 i = 0; i < record->m_pWorldBsp->m_nParts; ++i)
            if (record->m_pWorldBsp->m_ppParts[i]->m_pSurface->m_nFlags & 0x10) ++count;
        if (count > std::numeric_limits<uint32>::max() || !polies.SetSize(static_cast<uint32>(count))) throw std::bad_alloc();
        uint32 index = 0;
        for (const auto& record : geometry->models) for (uint32 i = 0; i < record->m_pWorldBsp->m_nParts; ++i)
            if (record->m_pWorldBsp->m_ppParts[i]->m_pSurface->m_nFlags & 0x10) polies[index++] = record->m_pWorldBsp->m_ppParts[i];
        // Validate the new layout before releasing a live level.
        WorldSpatialTree layout; if (!layout.LoadLayout(geometry->layout)) return 2;
        Term(); committed = true;
        m_Tree.Swap(layout);
        m_Worlds.Swap(models); m_Polies.Swap(polies);
        geometry_ = std::move(geometry); source_ = bytes;
        definitions_ = std::move(definitions); lights_ = std::move(lights); m_Shade = std::move(shade);
        m_nObjectsOffset = geometry_->header[2]; m_vMin = geometry_->min; m_vMax = geometry_->max;
        m_vBoxMin = m_vMin - 100.f; m_vBoxMax = m_vMax + 100.f;
        for (uint32 axis = 0; axis < 3; ++axis) m_v15c[axis] = m_vMax[axis] != m_vMin[axis] ? 1.f / (m_vMax[axis] - m_vMin[axis]) : 0;
        position_reference_.m_vMin = m_vMin; position_reference_.m_vMax = m_vMax; position_reference_.m_vScale = m_v15c;
        if (auto* visibility = FindFlagged20()) {
            if (!m_Tree.BindVisibility(visibility)) throw std::runtime_error("invalid visibility binding");
            m_Flags180 |= 2;
        }
        for (const auto& model : geometry_->models) for (const auto& properties : model->m_pWorldBsp->m_SurfaceProperties) {
            WorldClassQT1* klass = static_cast<WorldClassQT1*>(request->m_pUserData);
            while (klass && nolf::ascii_compare(klass->m_Name, properties.class_name) != 0) klass = klass->m_pNext;
            if (!klass || !klass->m_pMake || !klass->m_pFree) continue;
            auto* surface = model->m_pWorldBsp->m_pTree + properties.surface;
            WorldObjectInfoQT1 info{surface->m_vO,surface->m_vP,surface->m_vQ,this,model->m_pWorldBsp,surface};
            nolf::CommandReader input(properties.properties);
            auto arguments = input.next().value_or(std::vector<std::string>{});
            std::vector<char*> pointers; for (auto& value : arguments) pointers.push_back(value.data());
            auto object = std::make_unique<WorldObjectRecQT1>(); object->m_pBsp = model->m_pWorldBsp; object->m_pSurface = surface; object->m_pClass = klass;
            object->m_pObject = klass->m_pMake(&info, static_cast<int>(pointers.size()), pointers.data());
            if (object->m_pObject) world_objects_.push_back(std::move(object));
        }
        m_bLoaded = 1;
        if (request->m_LoadHook) request->m_LoadHook(request->m_Unknown0c);
        return 0;
    } catch (const std::bad_alloc&) { last_error_ = "world allocation failed"; if (committed) Term(); return 4; }
      catch (const std::exception& error) { last_error_ = error.what(); if (committed) Term(); return 2; }
}
LTBOOL CServerWorld::InheritWorld(CServerWorld* source) {
    if (!source || !source->source_ || source == this) return LTFALSE;
    nolf::MemoryStream stream(*source->source_); WorldLoadRequest request; request.m_pStream = &stream;
    return Load(&request) == 0 && LoadObjects(&stream) == LT_OK;
}
WorldPolyOwner* CServerWorld::FindPoly(HPOLY handle) {
    const uint32 model = handle >> 16, poly = handle & 0xffff;
    return model < m_Worlds.GetSize() && poly < m_Worlds[model]->m_pWorldBsp->m_nParts ? m_Worlds[model]->m_pWorldBsp->m_ppParts[poly] : nullptr;
}
const WorldPolyOwner* CServerWorld::FindPoly(HPOLY handle) const {
    const uint32 model=handle>>16,poly=handle&0xffff;
    return model<m_Worlds.GetSize() && poly<m_Worlds[model]->m_pWorldBsp->m_nParts ? m_Worlds[model]->m_pWorldBsp->m_ppParts[poly] : nullptr;
}
LTRESULT CServerWorld::NextPoly(HPOLY* handle) {
    if (!handle) return LT_INVALIDPARAMS;
    if (!m_bLoaded) return LT_NOTINITIALIZED;
    uint32 model = *handle == INVALID_HPOLY ? 0 : *handle >> 16;
    uint32 polygon = *handle == INVALID_HPOLY ? 0 : (*handle & 0xffff) + 1;
    while (model < m_Worlds.GetSize() && polygon >= m_Worlds[model]->m_pWorldBsp->m_nParts) { ++model; polygon = 0; }
    if (model >= m_Worlds.GetSize()) return LT_FINISHED;
    *handle = (model << 16) | polygon; return LT_OK;
}
void* CServerWorld::FindPoly(CWorldPolyKey* key) { return key ? FindPoly((uint32(key->m_iModel) << 16) | key->m_iPoly) : nullptr; }
CWorldBsp* CServerWorld::FindFlagged20() { for (auto* record : std::span(m_Worlds.GetArray(),m_Worlds.GetSize())) if (record->m_pWorldBsp->m_nUnknown48 & 0x20) return record->m_pWorldBsp; return nullptr; }
CWorldBsp* CServerWorld::FindFlagged10() { for (auto* record : std::span(m_Worlds.GetArray(),m_Worlds.GetSize())) if (record->m_pWorldBsp->m_nUnknown48 & 0x10) return record->m_pWorldBsp; return nullptr; }
void CServerWorld::ResetAll() { for (auto* record : std::span(m_Worlds.GetArray(),m_Worlds.GetSize())) { wm_ResetGeometry(record->m_pWorldBsp); if (record->m_pOriginal) wm_ResetGeometry(record->m_pOriginal); } }
CServerWorldRec* CServerWorld::FindWorldModel(const char* name) { if (name) for (auto* record : std::span(m_Worlds.GetArray(),m_Worlds.GetSize())) if (nolf::ascii_compare(record->m_pWorldBsp->m_Name,name) == 0) return record; return nullptr; }
const std::string& CServerWorld::Information() const { static const std::string empty; return geometry_ ? geometry_->information : empty; }
void CServerWorld::AddWorldObject(WorldObjectRecQT1* object) { std::unique_ptr<WorldObjectRecQT1> owner(object); world_objects_.push_back(std::move(owner)); }
void w_AddWorldObjectQT1(CServerWorld* world, WorldObjectRecQT1* object) { if (world) world->AddWorldObject(object); else delete object; }
