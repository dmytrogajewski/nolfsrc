#pragma once
#include "ltbasedefs.h"
#include "world_node.h"
#include "world_polygon.h"
#include "world_grid.h"
#include "world_lifetime.h"
#include <memory>

// Loader, physics, server objects and client geometry use this same C++
// interface. Serialized world indices never depend on its native layout.
class IWorldBsp {
public:
    IWorldBsp();
    virtual ~IWorldBsp() { m_GeometryLifetime->alive = m_GeometryLifetime->available = false; ++m_GeometryLifetime->generation; }
    virtual uint32 IsBusy() = 0;
    virtual WorldPolyRef* GetRoot() = 0;
    virtual HPOLY MakeHPoly(WorldPolyRef*) = 0;
    virtual CWorldPart* FindPoly(HPOLY) = 0;
    virtual CSubRecord* GetPointStatus(LTVector) = 0;
    virtual WorldPolyRef* GetPolyRefs() = 0;
    virtual uint32 GetWorldFlags() = 0;
    virtual uint32 IsFlagSet() = 0;
    virtual float GetRadiusSqr() = 0;
    virtual const CWorldSub108& PhysicsGrid() const = 0;
    virtual std::span<WorldPolyRef> PolygonReferences() = 0;
    CPhysicsBlock* GetPBlock(LTVector point) { return GetPointStatus(point); }
    float GetBoxDiagonalSqr() const { return m_vBoxMin.DistSqr(m_vBoxMax); }
    char m_Name[65]{};
    LTVector m_vBoxMin{0,0,0}, m_vBoxMax{0,0,0};
    std::shared_ptr<WorldGeometryLifetime> m_GeometryLifetime = std::make_shared<WorldGeometryLifetime>();
};
using CWorldBspBase = IWorldBsp;
