// *********************************************************************** //
//
//  MODULE:   W4_1001EB90_OS (.CPP)  -- /Os
//
//  PURPOSE:  0x1001eb90, the per-frame scene setup big2d1c0.cpp calls as
//            `if (sub_1001eb90(pScene, buf, 0x5000))`, which the d3d.ren
//            link referenced by address and nobody had written.
//
//  Flag set: /Os -- pushed callee-saved registers behind an %ebp frame,
//  `xor %eax,%eax ; inc %eax` for TRUE, and plain 0x90 padding after it.
//
//  A NEAR, and knowingly so, in three places:
//   * (closed) the shipped body opens with an inline x87
//     control-word sequence -- `fstcw ; andw $0xfcff ; fldcw`, precision
//     control to 24 bits -- which only inline assembly spells: it is
//     SetFPUPrecision24, one __asm block in an inline helper (RoundF's
//     precedent).  As _controlfp(_PC_24, _MCW_PC) it was a call; as three
//     separate __asm statements icl schedules the argument loads between
//     them; as C `&=` it is a movzwl/and/mov.  With it, the second light
//     vector as its own temp (vTemp2: the shipped frame holds two 12-byte
//     temps, -0x40 and -0x34) and the four global stores in reverse
//     source order (icl emits a run of global stores backwards), the
//     0x88 frame is exact and the row pairs as a near (586 -> 554 off);
//   * (closed) the three colour bytes are `fstps ; flds ;
//     fistpl` conversions: RoundF, the inlined __asm helper whose
//     out-of-line copy is 0x10015190 -- 706 -> 598 differing bytes and
//     1015 -> 921 bytes against the shipped 916;
//   * (closed) 0x10021e50, read the console variables, is
//     w15_10021e50_os.cpp.
//   * (closed) the second light vector is `(scale * 1.0f) * (1.0f /
//     255.0f)` through a by-value vector-times-scalar inline (FVecScale):
//     the shipped code keeps both multiplies (six separate constants), and
//     written componentwise icl folds the 1.0f away;
//   * (closed) the 0xff pair is stored BEFORE the seventeen counter
//     zeroes (icl then emits the zeroes first, through %eax, 5-byte
//     stores), the world node is read before the draw-mode test, and the
//     world before the node is published.
//  LEFT (920 of 925 bytes, one block): the world-model loop's entry test
//  loads the count into %eax and the shipped code reloads 0x1008beac
//  before the body; ours tests in %ecx and keeps the world in %eax.  No
//  effect: `k, i` / int32 counters, `count > i`, a guarded do/while, the
//  loop as an inline or static helper, the frame code set after the loop.
//
// *********************************************************************** //

#include "ltbasedefs.h"

// Round to nearest in the current FPU mode (0x10015190 out of line).
inline int32 RoundF(float f)
{
	int32 n;
	__asm fld f
	__asm fistp n
	return n;
}

// Precision control to 24 bits: the shipped `fstcw ; andw $0xfcff ;
// fldcw` (the bits _controlfp(_PC_24, _MCW_PC) sets, without the call).
// An __asm helper like RoundF; icl inlines it.
inline void SetFPUPrecision24()
{
	uint16 nCW;
	__asm
	{
		fstcw nCW
		and   nCW, 0xfcff
		fldcw nCW
	}
}


// The engine's SceneDesc, only as far as this function reads it.
struct SceneWorldNode;

struct SceneDesc
{
	int32           m_DrawMode;                         // +0x00  1 = world
	uint8           m_pad_04[0x34 - 0x04];              // +0x04
	SceneWorldNode* m_pWorldNode;                       // +0x34
	float           m_vGlobalLightColor[3];             // +0x38
	float           m_vGlobalLightDir[3];               // +0x44
	float           m_vGlobalLightScale[3];             // +0x50
	uint8           m_pad_5c[0xa4 - 0x5c];              // +0x5c
	int32           m_Rect[4];                          // +0xa4  l, t, r, b
	float           m_fFovX;                            // +0xb4
	float           m_fFovY;                            // +0xb8
	float           m_fFarZ;                            // +0xbc
	float           m_vPos[3];                          // +0xc0
	float           m_rRot[4];                          // +0xcc
};

struct LTVec { float x, y, z; };
struct FVec3 { float x, y, z; };                       // big2d1c0.cpp's spelling

// A vector times a scalar, the SDK's `operator*(float)` shape: a fresh
// temporary per product, so `(v * 1.0f) * (1.0f / 255.0f)` keeps both
// multiplies (written componentwise icl folds the 1.0f away).
inline FVec3 FVecScale(const FVec3& v, float f)
{
	FVec3 r;
	r.x = v.x * f;
	r.y = v.y * f;
	r.z = v.z * f;
	return r;
}

// A world model's poly and leaf records carry a frame code each.
struct WorldPoly  { uint8 m_pad[0x2c]; uint16 m_wFrameCode; uint8 m_pad2e[2]; };
struct WorldLeaf  { uint8 m_pad[0x42]; uint16 m_wFrameCode; };

struct WorldBsp
{
	uint8       m_pad_00[0x88];                         // +0x00
	WorldPoly*  m_pPolies;                              // +0x88
	uint32      m_nPolies;                              // +0x8c
	uint8       m_pad_90[0xa0 - 0x90];                  // +0x90
	WorldLeaf** m_ppLeaves;                             // +0xa0
	uint32      m_nLeaves;                              // +0xa4
};

struct WorldModelRef
{
	uint32      m_n00;                                  // +0x00
	WorldBsp*   m_pBsp;                                 // +0x04
};

struct S1001e520Root
{
	uint8           m_pad_00[0x18c];                    // +0x00
	WorldModelRef** m_ppModels;                         // +0x18c
	uint32          m_nModels;                          // +0x190
};

struct SceneWorldNode
{
	uint32          m_n00;                              // +0x00
	uint32          m_n04;                              // +0x04
	S1001e520Root*  m_pWorld;                           // +0x08
	uint16          m_wFrameCode;                       // +0x0c
};

// The engine's RenderStruct: +0x28 / +0x30 answer the two values kept at
// 0x1008bea4 / 0x1008b870, and +0x108 is the block 0x1008beb8 points at.
struct Counters10059590
{
	uint8   m_pad_00[0x28];                             // +0x00
	uint32  (*m_pfn28)();                               // +0x28
	uint8   m_pad_2c[4];                                // +0x2c
	uint32  (*m_pfn30)();                               // +0x30
	uint8   m_pad_34[0x108 - 0x34];                     // +0x34
	uint8   m_Block108[4];                              // +0x108
};

struct C1008beb0Thing;
struct C1000da50Data;
struct ClipVert;
struct Vec10020c00;
struct Rot10020c00;

extern Counters10059590* g_1008cbcc;                    // 0x1008cbcc
extern uint32            g_1008cbac;                    // 0x1008cbac  frame count
extern C1008beb0Thing*   g_1008beb0;                    // 0x1008beb0  the scene
extern uint32            g_1008b878;                    // 0x1008b878  buffer size
extern C1000da50Data*    g_1008beb8;                    // 0x1008beb8
extern ClipVert*         g_1008b87c;                    // 0x1008b87c  buffer
extern S1001e520Root*    g_1008beac;                    // 0x1008beac  the world
extern SceneWorldNode*   g_1008beb4;                    // 0x1008beb4  its node
extern uint16            g_1008bea8;                    // 0x1008bea8  frame code
extern uint32            g_1008bea4;                    // 0x1008bea4
extern uint16            g_w1008b870;                   // 0x1008b870
extern uint32            g_1008b874;                    // 0x1008b874  clip flags
extern LTVec             g_v1008be98;                   // 0x1008be98  light colour
extern LTVec             g_v1008be8c;                   // 0x1008be8c  light dir
extern FVec3             g_vLightScale;                 // 0x1008be80  light scale
extern LTVec             g_v1008be74;                   // 0x1008be74  scale * 255
extern LTVec             g_v1008be68;                   // 0x1008be68  scale / 255
extern uint8             g_1008be64;                    // 0x1008be64  scale b
extern uint8             g_1008be65;                    // 0x1008be65  scale g
extern uint8             g_1008be66;                    // 0x1008be66  scale r
extern uint8             g_1008be67;                    // 0x1008be67  scale a
extern uint8             g_1008be60;                    // 0x1008be60
extern uint8             g_1008b880[];                  // 0x1008b880  the xform

extern int32  g_nWorldPoliesProcessed;                  // 0x1008be58
extern uint32 g_1008be5c;                               // 0x1008be5c
extern int32  g_nParticlesDrawn;                        // 0x1008be4c
extern int32  g_nModelTriangles;                        // 0x1008be50
extern uint32 g_1008be54;                               // 0x1008be54
extern uint32 g_1008be48;                               // 0x1008be48
extern uint32 g_1008be44;                               // 0x1008be44
extern int32  g_nRejectedLights;                        // 0x1008bd78
extern uint32 g_n1008be3c;                              // 0x1008be3c
extern int32  g_nSkyPolies;                             // 0x1008be38
extern uint32 g_1008bd84;                               // 0x1008bd84
extern uint32 g_n1008be34;                              // 0x1008be34
extern int32  g_nVisibleLeaves;                         // 0x1008be40
extern int32  g_nTextureUploads;                        // 0x1008bd7c
extern uint32 g_n1008bd80;                              // 0x1008bd80
extern int32  g_nLitPoliesUploaded;                     // 0x1008be28
extern int32  g_nTextureUploadSaves;                    // 0x1008be2c
extern float  g_1008be30;                               // 0x1008be30

void   sub_10021e50();                                  // 0x10021e50
LTBOOL sub_10020c00(void* pArg0, uint32 nArg1, uint32 nArg2, float fArg3,
                    uint32 nArg4, float fArg5, float fArg6, float fArg7,
                    float fArg8, Vec10020c00* pPos,
                    Rot10020c00* pRot);                 // 0x10020c00

// M-P0-01: widescreen (Hor+) field of view.  The game picks its horizontal
// field of view for a 4:3 screen (FovX 90, FovY 75 degrees), and the
// renderer uses it as given, so on a wider screen the picture is stretched
// sideways.  Keep the vertical view a 4:3 screen would have and widen the
// horizontal one to the scene rectangle's real aspect ratio:
//     fovX' = 2 * atan(tan(fovX / 2) * (aspect / (4/3)))
// At 4:3 this is fovX itself.
extern "C" double __cdecl tan(double x);
extern "C" double __cdecl atan(double x);

//
// Models with a field of view of their own (the player-view weapons) are
// drawn from the game's field of view, not the widened one, and narrowed by
// the same factor (w43_10017c10_osoiqipni.cpp); these two hold, for the
// scene being drawn, the field of view the game asked for and that factor,
// tan(fovX'/2) / tan(fovX/2).
float g_fModernFovX4x3   = 1.5707964f;
float g_fModernFovXScale = 1.0f;

static float ModernHorPlusFovX(SceneDesc* pScene)
{
	float fWidth  = (float)(pScene->m_Rect[2] - pScene->m_Rect[0]);
	float fHeight = (float)(pScene->m_Rect[3] - pScene->m_Rect[1]);

	g_fModernFovX4x3   = pScene->m_fFovX;
	g_fModernFovXScale = 1.0f;

	if (fWidth <= 0.0f || fHeight <= 0.0f)
		return pScene->m_fFovX;

	g_fModernFovXScale = 0.75f * fWidth / fHeight;

	return 2.0f * (float)atan(tan(0.5 * pScene->m_fFovX)
	                          * g_fModernFovXScale);
}
unsigned int sub_1001eb90(SceneDesc* pScene, void* pMem, unsigned long nMemSize)
{
	uint16          nCW;
	SceneWorldNode* pNode;
	WorldBsp*       pBsp;
	LTVec           vTemp;
	FVec3           vTemp2;
	uint32          i, k;

	__asm
	{
		fstcw nCW
		and   nCW, 0xfcff
		fldcw nCW
	}

	sub_10021e50();

	++g_1008cbac;
	g_1008b87c = (ClipVert*)pMem;
	g_1008beb8 = (C1000da50Data*)g_1008cbcc->m_Block108;
	g_1008b878 = nMemSize;
	g_1008beb0 = (C1008beb0Thing*)pScene;

	pNode = pScene->m_pWorldNode;
	if (pScene->m_DrawMode == 1)
	{
		if (!pNode)
			return 0;

		g_1008beac = pNode->m_pWorld;
		g_1008beb4 = pNode;

		if (pNode->m_wFrameCode == 0xffff)
		{
			pNode->m_wFrameCode = 1;

			for (i = 0; i < g_1008beac->m_nModels; i++)
			{
				pBsp = g_1008beac->m_ppModels[i]->m_pBsp;

				for (k = 0; k < pBsp->m_nPolies; k++)
					pBsp->m_pPolies[k].m_wFrameCode = 0;

				for (k = 0; k < pBsp->m_nLeaves; k++)
					pBsp->m_ppLeaves[k]->m_wFrameCode = 0;

				for (k = 0; k < pBsp->m_nPolies; k++)
					pBsp->m_pPolies[k].m_wFrameCode = 0;
			}
		}

		g_1008bea8 = ++pNode->m_wFrameCode;
	}
	else
	{
		g_1008beac = LTNULL;
		g_1008beb4 = LTNULL;
	}

	g_1008bea4  = g_1008cbcc->m_pfn28();
	g_w1008b870 = (uint16)g_1008cbcc->m_pfn30();
	g_1008b874  = 0x3f;

	g_v1008be98 = *(LTVec*)pScene->m_vGlobalLightColor;
	g_v1008be8c = *(LTVec*)pScene->m_vGlobalLightDir;
	g_vLightScale = *(FVec3*)pScene->m_vGlobalLightScale;

	vTemp.x = 255.0f * g_vLightScale.x;
	vTemp.y = 255.0f * g_vLightScale.y;
	vTemp.z = 255.0f * g_vLightScale.z;
	g_v1008be74 = vTemp;

	vTemp2 = FVecScale(FVecScale(g_vLightScale, 1.0f), 1.0f / 255.0f);
	g_v1008be68 = *(LTVec*)&vTemp2;

	g_1008be66 = (uint8)RoundF(255.0f * g_vLightScale.x);
	g_1008be65 = (uint8)RoundF(255.0f * g_vLightScale.y);
	g_1008be64 = (uint8)RoundF(255.0f * g_vLightScale.z);
	g_1008be60              = 0xff;
	g_1008be67              = 0xff;
	g_nWorldPoliesProcessed = 0;
	g_1008be5c              = 0;
	g_nParticlesDrawn       = 0;
	g_nModelTriangles       = 0;
	g_1008be54              = 0;
	g_1008be48              = 0;
	g_1008be44              = 0;
	g_nRejectedLights       = 0;
	g_n1008be3c             = 0;
	g_nSkyPolies            = 0;
	g_1008bd84              = 0;
	g_n1008be34             = 0;
	g_nVisibleLeaves        = 0;
	g_nTextureUploads       = 0;
	g_n1008bd80             = 0;
	g_nLitPoliesUploaded    = 0;
	g_nTextureUploadSaves   = 0;
	g_1008be30              = 0.0f;

	// M-P0-01: widescreen (Hor+) field of view
	{
		float fFovX = ModernHorPlusFovX(pScene);

		sub_10020c00(g_1008b880,
		             *(uint32*)&fFovX, *(uint32*)&pScene->m_fFovY, 7.0f,
		             *(uint32*)&pScene->m_fFarZ,
		             (float)pScene->m_Rect[0], (float)pScene->m_Rect[1],
		             (float)pScene->m_Rect[2] - 0.1f, (float)pScene->m_Rect[3] - 0.1f,
		             (Vec10020c00*)pScene->m_vPos, (Rot10020c00*)pScene->m_rRot);
	}

	return 1;
}
