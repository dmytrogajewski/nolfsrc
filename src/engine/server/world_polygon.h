#pragma once
#include "ltbasedefs.h"
#include "observer.h"
#include <cstddef>
#include <span>

class CWorldSurface {
public:
    CWorldSurface() { m_Sub28.Init(this); }
    virtual void GetBasisVectors(LTVector* right, LTVector* up);
    LTVector m_vO{0,0,0}, m_vP{0,0,0}, m_vQ{0,0,0};
    CModelSub14 m_Sub28;
    uint32 m_n30 = 0;
    LTPlane* m_pPlane = nullptr;
    uint32 m_nFlags : 29 = 0;
    uint32 m_nAxis : 3 = 0;
    uint16 m_nTextureFlags = 0, m_n3e = 0;
    uint16 m_nFirstPoly = 0, m_nLink42 = 0x7fff;
    uint8 m_n44 = 0;
};
using WorldBspData = CWorldSurface;
using WorldSurface = CWorldSurface;

struct WorldPolyVertex {
    LTVector* m_pPos;
    float m_fU, m_fV, m_fUnknown0c, m_fUnknown10;
    uint8 m_b, m_g, m_r, m_a;
};
struct WorldLightmapReference {
    uint16 animation = 0;
    uint16 polygon = 0; // Entry within that animation's polygon/frame arrays.
};
static_assert(sizeof(WorldLightmapReference)==4);
using CWorldEdgeQT1=WorldLightmapReference;
class CWorldPart {
public:
    LTPlane* GetPlane() { return m_pParent->m_pPlane; }
    uint32 GetVertexCount() const {
        return m_pVerts2 != m_Verts ? uint32(m_nVerts) + m_nVerts2 : m_nVerts;
    }
    std::span<const WorldPolyVertex> render_vertices(bool fix_tjunctions=true) const {
        return fix_tjunctions && m_pVerts2 && m_pVerts2!=m_Verts ?
            std::span<const WorldPolyVertex>{m_pVerts2,m_nVerts2} : std::span<const WorldPolyVertex>{m_Verts,m_nVerts};
    }
    std::span<const WorldLightmapReference> lightmap_references() const { return {m_pEdges,m_nEdges}; }
    uint32 m_Unknown00, m_Unknown04, m_Unknown08;
    WorldLightmapReference* m_pEdges;
    uint32 m_nEdges;
    uint16 m_Unknown14, m_nIndex;
    LTVector m_vCentre;
    float m_fRadius;
    union { CWorldSurface* m_pSurface; CWorldSurface* m_pParent; };
    uint32 m_Unknown2c, m_Unknown30;
    LTVector m_v34;
    uint16 m_nNextOnSurface, m_Unknown42;
    uint32 m_Unknown44;
    uint8 m_n48, m_n49;
    uint16 m_Unknown4a;
    union { WorldPolyVertex* m_pVerts2; WorldPolyVertex* m_pVertexRun; };
    union { uint16 m_nVerts; uint16 m_nVertices; };
    union { uint16 m_nVerts2; uint16 m_nExtraVertices; };
    union { WorldPolyVertex* m_Verts; WorldPolyVertex* m_Vertices; };
};
inline constexpr std::size_t world_polygon_bytes(unsigned vertices) {
    return sizeof(CWorldPart) + vertices * sizeof(WorldPolyVertex);
}
