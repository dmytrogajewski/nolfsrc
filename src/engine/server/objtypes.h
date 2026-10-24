// Native concrete object types shared by allocation, SDK accessors and rendering.
#pragma once
#include "ltobject_internal.h"
#include "ltparticle.h"
#include "iltspritecontrol.h"
#include "sprite_data.h"

void mat_SetupTransform(LTVector*, LTRotation*, LTVector*, LTMatrix*);
void quat_ToMatrix2(LTRotation*, LTMatrix*);

class CObjCanvas : public CObjBase
{
public:
	CObjCanvas();

	virtual float _v2c();
	virtual float _v38();

	CanvasDrawFn m_DrawFn=nullptr;
	void* m_DrawUser=nullptr;
	float m_VisRadius=1;
};

class CObjCamera : public CObjBase
{
public:
	virtual ~CObjCamera();
	CObjCamera();
	float    m_fFovY;
	float    m_fFovX;
	uint32   m_Unknown1c0;
	LTVector m_v1c4;
};

struct CLineSegment {
    LTLine m_Line{};
    CLineSegment* m_pPrev = nullptr;
    CLineSegment* m_pNext = nullptr;
};

class CObjLineSystem : public CObjBase
{
public:
	CObjLineSystem();

	virtual ~CObjLineSystem();

	virtual void _v20(CUnknown390* pAttachMgr,
	                  struct ObjectCreateStruct* pStruct);

	void*    m_pUnknown1a8;
	void*    m_pUnknown1ac;
	CLineSegment m_Head;
	CLineSegment* m_pCursorA;
	CLineSegment* m_pCursorB;
	LTVector m_v1f4;
	LTVector m_v200;
	LTVector m_v20c;
	float    m_f218;
};

class CObjLight : public CObjBase
{
public:
	virtual ~CObjLight();
	CObjLight();

	virtual float GetLightRadius(void* pUnused);

	float& m_fLightRadius = m_LightRadius;
};

class CObjSprite;
class SpriteControl final : public ILTSpriteControl {
public:
    LTRESULT GetNumAnims(uint32&) override;
    LTRESULT GetNumFrames(uint32, uint32&) override;
    LTRESULT GetCurPos(uint32&, uint32&) override;
    LTRESULT SetCurPos(uint32, uint32) override;
    LTRESULT GetFlags(uint32&) override;
    LTRESULT SetFlags(uint32) override;
    CObjSprite* m_pInstance = nullptr;
};
class CObjSprite : public LTObject {
public:
    CObjSprite();
    ~CObjSprite() override;
    float GetScaledRadius() override;
    void UpdateAnimation(uint32 milliseconds);
    Sprite* m_pSprite = nullptr;
    SpriteAnim* m_pCurAnim = nullptr;
    uint32* m_pCurFrame = nullptr;
    uint32 m_FrameTime = 0;
    uint32 m_Unknown1b8 = SC_PLAY | SC_LOOP;
    HPOLY m_ClipPoly = INVALID_HPOLY;
    SpriteControl m_Control;
};
using SpriteInstance = CObjSprite;

struct CParticle : LTParticle {
    CParticle() { m_Velocity.Init(); m_Color.Init(); m_Alpha = m_Size = 1; m_pNext = nullptr; }
    LTVector m_Pos{0,0,0};
    float m_Lifetime = 0;
    float m_TotalLifetime = 0;
    CParticle* m_pPrev = nullptr;
};

class CObjParticleSystem : public CObjBase
{
public:
	CObjParticleSystem();

	virtual void _v20(CUnknown390* pAttachMgr,
	                  struct ObjectCreateStruct* pStruct);

	virtual ~CObjParticleSystem();

public:
	CParticle m_Head;
	CParticle* m_p1c8;
	CParticle* m_p1e0;
	void*  m_p1e4;
	uint32 m_u1e8;
	uint8  m_u1ec;
	uint8  m_u1ed;
	uint8  m_u1ee;
	uint32 m_u1f0;
	float  m_f208;
	float  m_f20c;
	float  m_f210;
	float  m_f214;
	float  m_f218;
	float  m_f21c;
	float  m_f220;
	float  m_f224;
	uint32 m_u228;
	uint32 m_u22c;

	LTVector m_vBoxMin;
	LTVector m_vBoxMax;
	float  m_f248;
	float  m_f24c;
	int    m_n250;
	int    m_n254;
	uint32 m_u258;
};
