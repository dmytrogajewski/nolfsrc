#pragma once
#include "ltbasetypes.h"
#include "ltlink.h"
#include <cassert>

class CWorldPart;
using WorldPolyOwner = CWorldPart;
using CWorldItem18Link = LTLink;
struct WorldPolyRef;
class BspObjectLink : public LTLink {
public:
    BspObjectLink() { Init2(nullptr); }
    ~BspObjectLink() { Detach(); }
    BspObjectLink(const BspObjectLink&) = delete;
    BspObjectLink& operator=(const BspObjectLink&) = delete;
    void Bind(WorldPolyRef&);
    void Detach();
    WorldPolyRef* m_pNode = nullptr;
};

// The on-disk node stores indices; this record stores resolved native
// pointers. Never read or write it as a serialized 24-byte block.
struct WorldPolyRef {
    explicit WorldPolyRef(uint8 flags = 0) { Init(flags); }
    ~WorldPolyRef();
    WorldPolyRef(const WorldPolyRef& other) : WorldPolyRef() { *this = other; }
    WorldPolyRef& operator=(const WorldPolyRef& other) {
        if (this == &other) return *this;
        assert(m_head.m_pNext == &m_head && m_head.m_pPrev == &m_head);
        m_pOwner = other.m_pOwner;
        m_Children[0] = other.m_Children[0];
        m_Children[1] = other.m_Children[1];
        m_x = other.m_x;
        m_flag = other.m_flag;
        m_nProject = other.m_nProject;
        m_head.Init2(nullptr);
        return *this;
    }
    void Init(uint8 flags) {
        m_pOwner = nullptr;
        m_Children[0] = m_Children[1] = nullptr;
        m_head.Init2(nullptr);
        m_x = 0xffff;
        m_flag = flags;
        m_nProject = 0;
    }
    LTPlane* GetPlane();
    WorldPolyOwner* m_pOwner;
    union { WorldPolyRef* m_Children[2]; WorldPolyRef* m_pChild[2]; };
    CWorldItem18Link m_head;
    uint16 m_x;
    union { uint8 m_flag; uint8 m_Flags; };
    union { uint8 m_nProject; uint8 m_y; };
};
using CWorldItem18 = WorldPolyRef;
using BSPNode = WorldPolyRef;
using WorldPoly = WorldPolyRef;

extern void* g_pWorldTreeFrontTerm;
extern void* g_pWorldTreeBackTerm;
WorldPolyRef* w_GetTreeNodeQT1(WorldPolyRef* nodes, uint32 count, uint32 index);
