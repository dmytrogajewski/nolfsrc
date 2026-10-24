#pragma once
#include "world_loader.h"
#include "world_lighting.h"
#include "world_spatial.h"
#include "world_object_record.h"
#include "position_reference.h"

class LTObject;
class WorldLoadRequest {
public:
    ILTStream* m_pStream = nullptr;
    void* m_pUserData = nullptr;
    void (*m_LoadHook)(int) = nullptr;
    uint32 m_Unknown0c = 0;
};
struct WorldProperty {
    std::string name;
    uint8 type = 0;
    uint32 flags = 0;
    std::vector<std::byte> data;
};
struct WorldObjectDefinition {
    std::string class_name;
    std::vector<WorldProperty> properties;
};
struct WorldLight {
    LTVector position{0,0,0}, color{0,0,0}, outer_color{0,0,0}, direction{0,0,0};
    float radius = 0, fov_cos = -1;
    bool fast = true;
};
class WorldShadeGrid {
public:
    void Setup(LTVector min, LTVector max, float cell_size, LTVector ambient);
    void AddLight(const WorldLight&);
    std::array<uint8, 4> Sample(LTVector point) const;
    void Clear();
    std::size_t Size() const { return samples_.size(); }
private:
    LTVector origin_{0,0,0};
    float cell_ = 1;
    std::array<uint32, 3> dimensions_{};
    std::vector<std::array<uint8, 4>> samples_;
};
void w_GetPointShade(WorldShadeGrid*, LTVector*, uint8*);

// One owned world state. Historical manager offsets never define its extent.
class CServerWorld : public CWorldData {
public:
    CServerWorld() = default;
    ~CServerWorld() override;
    int Load(WorldLoadRequest*);
    void Term();
    void TermObjects();
    LTBOOL InheritWorld(CServerWorld*);
    WorldPolyOwner* FindPoly(HPOLY);
    const WorldPolyOwner* FindPoly(HPOLY) const;
    LTRESULT NextPoly(HPOLY*);
    void* FindPoly(CWorldPolyKey*);
    CWorldBsp* FindFlagged20();
    CWorldBsp* FindFlagged10();
    void ResetAll();
    int AttachWorldModel(LTObject*, char*);
    CServerWorldRec* FindWorldModel(const char*);
    const std::vector<WorldLight>& Lights() const { return lights_; }
    const std::vector<WorldObjectDefinition>& ObjectDefinitions() const { return definitions_; }
    const std::string& Information() const;
    const WorldGeometryFile* Geometry() const { return geometry_.get(); }
    const std::string& LastLoadError() const { return last_error_; }
    CPosRef* PositionReference() { return &position_reference_; }
    void AddWorldObject(WorldObjectRecQT1*);

    WorldSpatialTree m_Tree;
    WorldShadeGrid m_Shade;
    LTVector m_vMin{0,0,0}, m_vMax{0,0,0}, m_v15c{0,0,0};
    LTVector m_vBoxMin{0,0,0}, m_vBoxMax{0,0,0};
    CMoArray<CServerWorldRec*> m_Worlds;
    CMoArray<CWorldPart*> m_Polies;
    ILTStream* m_pObjectStream = nullptr; // SDK ownership transfers after successful server startup.
    uint32 m_bLoaded = 0;
private:
    std::unique_ptr<WorldGeometryFile> geometry_;
    std::vector<WorldObjectDefinition> definitions_;
    std::vector<WorldLight> lights_;
    std::shared_ptr<const std::vector<std::byte>> source_;
    std::vector<std::unique_ptr<WorldObjectRecQT1>> world_objects_;
    bool terminating_ = false;
    std::string last_error_;
    CPosRef position_reference_;
};
