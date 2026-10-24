#pragma once
// Engine objects have one native layout. Serialized object IDs retain their
// fixed widths; neither platform code nor wire code depends on member offsets.
#include "ltbasedefs.h"
#include "message.h"
#include "world_bsp_interface.h"
#include "world_spatial.h"
#include "polygrid_record.h"
#include <atomic>
#include <memory>
#include <array>
#include <vector>

#define WBSP_NOSTEP 0x400000
#define WORLDFLAG_MAINWORLD 0x04
#define CHANGEFLAG_MOTION 0x40

class LTAnimTracker;
class ModelWalker;
class CUnknown390;
class StructBank;
class CObjVisitor;
class CWorldBsp;
using CWorldBspView = CWorldBsp;
using CAttachMgrView = CUnknown390;

class SObjData
{
public:
	LTLink m_Links;                          // +0x00  the inter-object links this
	                                         //        object is an end of; both
	                                         //        container walks run it
	void*  m_hIdentifier;                    // +0x0c
	float  m_NextUpdate;                     // +0x10  SetNextUpdate stores it raw
	float  m_DeactivationTime;               // +0x14  SetDeactivationTime writes this
	                                         //        and the countdown below together
	float  m_UpdateTimer;                    // +0x18  seconds until the next update
	class Client* m_pOwningClient;           // +0x1c  set at world entry; nonnull makes
	                                         //        removal LT_TRIEDTOREMOVECLIENTOBJECT
	class BaseClass* m_pBaseClass;           // +0x20  the game object (m_hObject at
	                                         //        its +8 points back here)
	struct ClassDef* m_pClass;              // SDK definition of the game class
	void*  m_Unknown28;                      // +0x28  freed by removal when set
	CMessageRef m_SFXMessage;                 // owns the late-join effect message
	void*  m_pModelFile;                     // +0x30  file identifiers, not names:
	void*  m_SkinFiles[2]{};                  // two skin file identifiers
	LTLink m_StateLink;                      // +0x3c  in the 0x8dc list: actives are
	                                         //        walked forward, inactives from
	                                         //        the tail (GetNextObject pair)
	class LTObject* m_pNextChanged;          // +0x48  the changed-objects list at 0x910
	uint32 m_CreateFlags = 0;
	uint16 m_ClientChangeFlags;              // +0x50  ORed into each client's table
	uint16 m_NetFlags;                       // +0x52  NETFLAG_* (ltcodes.h); SetNetFlags
	                                         //        strips ANIMUNGUARANTEED off non-models
	bool m_Removing = false;
    std::shared_ptr<const void> m_GameModuleLease;
    inline static std::atomic<uint64> s_Generation{1};
    uint64 m_Generation = s_Generation.fetch_add(1,std::memory_order_relaxed);
};

// One attachment of a child object to a parent. Only the list link is reached
// from here; the objects themselves come back from GetAttachmentObjects.
class Attachment
{
public:
	LTransform  m_Offset;                    // 0x00  position + rotation offset
	uint16      m_nParentID;                 // 0x1c
	uint16      m_nChildID;                  // 0x1e
	int         m_iNode;                     // 0x20  socket/node index; -1 = none
	Attachment* m_pNext;                     // 0x24
    LTObject* m_pParent = nullptr;
    LTObject* m_pChild = nullptr;
    inline static std::atomic<uint64> s_Generation{1};
    uint64 m_Generation = s_Generation.fetch_add(1,std::memory_order_relaxed);
};

class LTObject {
public:
    explicit LTObject(uint8 type = OT_NORMAL);
    virtual ~LTObject();
    LTObject(const LTObject&) = delete;
    LTObject& operator=(const LTObject&) = delete;
    virtual LTBOOL _v04(CObjVisitor*);
    virtual void _v08();
    virtual void GetDims(LTVector* min, LTVector* max);
    virtual LTBOOL _v10() { return LTFALSE; }
    virtual LTLink* _v14(uint32) { return nullptr; }
    virtual void _v18(LTObject*) {}
    virtual void _v1c(void*) {}
    virtual void _v20(CUnknown390*, ObjectCreateStruct*);
    virtual LTBOOL IsInWorld();
    virtual void GetTransformMatrix(LTMatrix*);
    virtual float _v2c();
    virtual LTBOOL _v30();
    virtual LTBOOL _v34() { return LTFALSE; }
    virtual void SetupModelWalker(ModelWalker*);
    virtual void SetupModelWalkerRefsOnly(ModelWalker*);
    virtual LTBOOL FindTracker(LTAnimTracker*, LTAnimTracker***);
    virtual float GetScaledRadius();
    void BaseTerm();
    void Unlink();
    HPOLY MakeHPoly(WorldPoly*);
    void SetPos(LTVector);
    void UpdateBounds();
    void SetDims(LTVector);
    void SetWorldBsp(IWorldBsp* current, IWorldBsp* original) {
        m_pWorldBsp = current; m_pOriginalBsp = original;
        m_pWMIface = original ? original : current;
    }

    int m_Unknown54 = 0, m_Unknown58 = -1;
    CUnknown390* m_pAttachMgr = nullptr;
    StructBank* m_pAttachmentBank = nullptr;
    LTLink m_Link60, m_Link7c;
    BspObjectLink m_Link6c;
    WorldPolyRef*& m_pTreeNode78 = m_Link6c.m_pNode;
    uint32 m_Flags = 0, m_Flags2 = 0, m_UserFlags = 0;
    uint8 m_ColorR = 255, m_ColorG = 255, m_ColorB = 255, m_ColorA = 255;
    Attachment* m_pAttachments = nullptr;
    LTRotation m_Rotation;
    LTVector m_Scale{1,1,1};
    uint16 m_ObjectID = 0xffff, m_ObjectID2 = 0xffff;
    uint8 m_ObjectType = OT_NORMAL, m_BlockingPriority = 0;
    uint32 m_Unknownc0 = 0;
    LTVector m_Velocity{0,0,0}, m_Acceleration{0,0,0};
    float m_FrictionCoefficient = 5, m_Mass = 30, m_ForceIgnoreLimitSqr = 1000000;
    LTObject* m_pStandingOn = nullptr;
    WorldPoly* m_pStandingOnPoly = nullptr;
    LTVector m_BBoxMin{0,0,0}, m_BBoxMax{0,0,0};
    LTLink m_StandingOnMe, m_StandingLink;
    uint32 m_ChangeFlags = 0;
    float m_fLink150Key = 0, m_f128 = 0;
    LTVector m_v12c{0,0,0}, m_v138{0,0,0};
    LTLink m_Link150;
    float m_fLink170Key = 0;
    LTLink m_Link170;
    void* m_pUnknown17c = nullptr;
    uint16 m_n180 = 0;
    SObjData* m_pSData = nullptr;
    LTVector m_Pos{0,0,0}, m_Dims{1,1,1};
    float m_Radius = 0;
    IWorldBsp* m_pWorldBsp = nullptr;
    IWorldBsp* m_pOriginalBsp = nullptr;
    IWorldBsp* m_pWMIface = nullptr;
    float m_LightRadius = 100;
    SpatialMembership m_SpatialMembership;
    // Independent callback token survives storage recycling without retaining
    // the object. Model instances use this same lifetime token.
    std::shared_ptr<bool> m_Lifetime = std::make_shared<bool>(true);
};
using CObjBase = LTObject;

class CObjVisitor {
public:
    virtual ~CObjVisitor() = default;
    virtual void Visit(LTObject*) = 0;
};

class CObjWorldModel : public LTObject {
public:
    CObjWorldModel();
    explicit CObjWorldModel(char type);
    ~CObjWorldModel() override;
    void _v08() override { sub_10024d41(); }
    LTBOOL _v10() override { return m_pWorldBsp ? m_pWorldBsp->IsFlagSet() : 0; }
    LTLink* _v14(uint32 index) override { return GetLink(index); }
    void _v18(LTObject* object) override { LinkIntoTree(object); }
    LTBOOL _v30() override { return HasOriginalBsp(); }
    LTBOOL _v34() override { return GetWorldFlag4(); }
    void _v20(CUnknown390*, ObjectCreateStruct*) override;
    void sub_10024d41();
    void InitFields();
    void LinkIntoTree(LTObject*);
    void ForwardToWorldBsp();
    LTLink* GetLink(uint32);
    LTBOOL HasOriginalBsp();
    uint32 GetWorldFlag4();
    LTBOOL IsWorldBusy();
    LTBOOL IsWorldIdle();
    LTLink m_Links1b4[5];
    LTMatrix m_Transform1f0, m_Transform230;
};
using WorldModelObject = CObjWorldModel;

class CObjContainer : public CObjWorldModel {
public:
    CObjContainer();
    ~CObjContainer() override;
    void _v20(CUnknown390*, ObjectCreateStruct*) override;
    uint16 m_ContainerCode = 0xffff;
};
using ContainerInstance = CObjContainer;

class CObjPolyGrid : public LTObject {
public:
    CObjPolyGrid();
    ~CObjPolyGrid() override;
    virtual float GetBoundingRadius();
    std::vector<int8> m_Heights;
    std::vector<uint16> m_Indices;
    float m_XPan=0,m_YPan=0,m_XScale=1,m_YScale=1;
    LTLink m_Records;
    uint32 m_Width=0,m_Height=0;
    std::array<PGColor,256> m_Colors{};
};
using PolyGridObject = CObjPolyGrid;

void* obj_LinkIntoWorldTree(CWorldBspView*, LTObject*);
void obj_ReleaseMemory(LTObject*);
void pg_FreeData(LTObject*);
void obj_UnlinkTrackersA(LTObject*);
void obj_UnlinkTrackersB(LTObject*);
void obj_SetStandingOn(LTObject*, LTObject*, WorldPoly*);
void obj_DetachAll(CUnknown390*, LTObject*);
void obj_RetransformWorldModel(LTObject*);
void obj_UpdateTransform(LTObject*);
void wm_BuildTransforms(LTVector*, LTVector*, LTRotation*, LTMatrix*, LTMatrix*);
void wm_Transform(LTObject*, LTMatrix*, int);
int ws_GetBoxStatus(WorldPolyRef*, LTVector*, LTVector*);
