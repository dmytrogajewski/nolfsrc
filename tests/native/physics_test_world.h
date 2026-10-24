#pragma once
#include "engine/server/world_bsp_interface.h"
#include "engine/server/worldsim.h"
#include <array>
#include <cstring>

// Two real native polygon records, with an optional cell boundary between
// the starting point and floor. No serialized layout or private vtable view.
class TestPhysicsWorld final : public IWorldBsp {
public:
    explicit TestPhysicsWorld(bool split_cells) {
        std::strcpy(m_Name,"PhysicsRegression");
        m_vBoxMin.Init(-1000,-1024,-1000); m_vBoxMax.Init(1000,1024,1000);
        for (unsigned i = 0; i < 2; ++i) {
            const float height = i ? 300.f : split_cells ? -300.f : 0.f;
            planes_[i].m_Normal.Init(0,1,0); planes_[i].m_Dist = height;
            surfaces_[i].m_pPlane = &planes_[i]; surfaces_[i].m_nFlags = WBSP_SOLID;
            positions_[i] = {LTVector(-100,height,-100),LTVector(-100,height,100),LTVector(100,height,100),LTVector(100,height,-100)};
            auto& polygon = polygons_[i]; polygon.m_nIndex = i;
            polygon.m_vCentre.Init(0,height,0); polygon.m_fRadius = 142;
            polygon.m_pParent = &surfaces_[i]; polygon.m_nVertices = polygon.m_nExtraVertices = 4;
            polygon.m_Vertices = polygon.m_pVertexRun = vertices_[i].data();
            for (unsigned j = 0; j < 4; ++j) vertices_[i][j].m_pPos = &positions_[i][j];
            refs_[i].m_pOwner = &polygon; refs_[i].m_nProject = 2;
            refs_[i].m_Children[0] = static_cast<WorldPolyRef*>(g_pWorldTreeBackTerm);
            refs_[i].m_Children[1] = static_cast<WorldPolyRef*>(g_pWorldTreeFrontTerm);
        }
        refs_[0].m_Children[1] = &refs_[1];
        grid_.m_nCountX = grid_.m_nCountZ = 1; grid_.m_nCountY = split_cells ? 2 : 1;
        grid_.m_nSliceStride = grid_.m_nCountY; grid_.m_nRecords = grid_.m_nCountY;
        grid_.m_vCell.Init(0,split_cells ? 1024.f : 2048.f,0); grid_.m_vOrigin.Init(0,-1024,0);
        grid_.m_pRecords = new CSubRecord[grid_.m_nRecords]{};
        if (split_cells) {
            for (uint16 i = 0; i < 2; ++i) {
                auto& cell = grid_.m_pRecords[i]; cell.m_nCount = 1; cell.m_nIndex = 0;
                cell.m_pNodes = new WSNode[1]{{i,{WS_NODE_SOLID,WS_NODE_EMPTY}}};
            }
        } else {
            auto& cell = grid_.m_pRecords[0]; cell.m_nCount = 2; cell.m_nIndex = 0;
            cell.m_pNodes = new WSNode[2]{{0,{WS_NODE_SOLID,1}},{1,{WS_NODE_EMPTY,WS_NODE_EMPTY}}};
        }
    }
    uint32 IsBusy() override { return 0; }
    WorldPolyRef* GetRoot() override { return refs_.data(); }
    HPOLY MakeHPoly(WorldPolyRef* ref) override {
        for (uint32 i = 0; i < refs_.size(); ++i) if (ref == &refs_[i]) return (1u<<16)|i;
        return INVALID_HPOLY;
    }
    CWorldPart* FindPoly(HPOLY handle) override { return handle >= (1u<<16) && handle < (1u<<16)+2 ? &polygons_[handle&0xffff] : nullptr; }
    CSubRecord* GetPointStatus(LTVector point) override { return grid_.GetPointStatus(point); }
    WorldPolyRef* GetPolyRefs() override { return refs_.data(); }
    uint32 GetWorldFlags() override { return WORLDFLAG_MAINWORLD; }
    uint32 IsFlagSet() override { return 0; }
    float GetRadiusSqr() override { return GetBoxDiagonalSqr()*.25f; }
    const CWorldSub108& PhysicsGrid() const override { return grid_; }
    std::span<WorldPolyRef> PolygonReferences() override { return expose_polygons ? std::span<WorldPolyRef>(refs_) : std::span<WorldPolyRef>(); }
    bool expose_polygons = true;
    void ClearPhysics() { grid_.Term(); }
private:
    std::array<LTPlane,2> planes_{};
    std::array<CWorldSurface,2> surfaces_{};
    std::array<std::array<LTVector,4>,2> positions_{};
    std::array<std::array<WorldPolyVertex,4>,2> vertices_{};
    std::array<CWorldPart,2> polygons_{};
    std::array<WorldPolyRef,2> refs_;
    CWorldSub108 grid_;
};
