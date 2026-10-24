// *********************************************************************** //
//
//  MODULE:   D3DBLIT (.CPP)
//
//  PURPOSE:  d3d.ren's surface and screen-blit family -- RenderStruct slots
//            +0xc4 .. +0xfc.  Everything the engine does to a CSurface that
//            is not "draw 3D" comes through here.
//
//  STATUS:   NONE OF THIS IS VERIFIABLE YET.  d3d.ren was built with a
//            compiler this project does not have (see d3ddraw.cpp), and
//            every function below carries at least one of its fingerprints.
//            The DECODE is compiler-independent and is what is being banked
//            here; the moment a candidate compiler exists these become
//            twenty-odd measuring sticks at once.
//
//            Per-function confidence is stated at each function.  DECODED
//            means every instruction of the body was read.  INFERRED means
//            a gap was filled from context and says which.
//
//  WHAT THIS FILE ESTABLISHED (all read off the shipped bytes):
//
//   1. THE SURFACE OBJECT.  d3d.ren's "render data" is a 0x8c-byte block
//      allocated by slot +0xc8 and freed by slot +0xcc:
//
//          +0x00  IDirectDrawSurface7*   the DirectDraw surface
//          +0x04  DDSURFACEDESC2         0x7c bytes, filled by
//                                        GetSurfaceDesc at creation
//          +0x80  uint32                 the colour key currently SET on
//                                        the surface (a cache, so the blit
//                                        path can skip SetColorKey)
//          +0x84  D3DOptSurface*         the optimized (texture) form, or
//                                        NULL
//          +0x88  uint32                 read by the warp path only
//
//      That settles d3dmisc.cpp's `d3d_GetSurfaceInfo`, which reads +0x10,
//      +0x0c and +0x14 of this object.  With the description at +0x04 those
//      are m_Desc.dwWidth (0x04+0x0c), m_Desc.dwHeight (0x04+0x08) and
//      m_Desc.lPitch (0x04+0x10) -- so the slot really is
//      GetSurfaceInfo(pData, &width, &height, &pitch), the out-parameters
//      are in the obvious order, and the "second output is the lower
//      offset" oddity noted there is just dwHeight sitting below dwWidth in
//      DDSURFACEDESC2.  (This does NOT flip its edx/ecx difference -- see
//      the note at the bottom of this file.)
//
//   2. THE BLIT REQUEST.  RenderBlitRequest is at least 0x1c bytes:
//
//          +0x00  D3DRenderData*  the NON-screen surface's render data
//          +0x04  uint32          flags; bit 0 = "use the source colour key"
//          +0x08  uint32          the colour key value
//          +0x0c  LTRect*         source rectangle
//          +0x10  LTRect*         destination rectangle
//          +0x14  float           warp path only (compared against a float
//                                 constant at 0x100a914c)
//          +0x18  uint32          warp path only
//
//      +0x0c is always the source and +0x10 always the destination, in both
//      directions: BlitToScreen passes +0x10 as Blt's lpDestRect with the
//      screen as `this`, BlitFromScreen passes +0x10 as lpDestRect with the
//      surface as `this` and the screen as lpDDSrcSurface.  That is the
//      client-side contract confirmed from the renderer's side.
//
//   3. THE OPTIMIZED SURFACE at +0x84 is a tile grid, allocated as one
//      block by OptimizeSurface:
//
//          +0x00  uint32   nCols   (ceil(width  / 64))
//          +0x04  uint32   nRows   (ceil(height / 64))
//          +0x08  Tile[nCols*nRows], stride 0x1c
//
//      and inside a tile, +0x10 = texture width, +0x14 = texture height,
//      +0x18 = IDirectDrawSurface7* (the texture).  The first 0x10 bytes of
//      a tile are never touched by any function in this file; they are
//      written by 0x10033fac, the pixel-copy step.
//      Tile index is row*nCols + col.  The allocation is
//      (nCols*nRows - 1) * 0x1c + 0x24, which is 0x08 of header plus
//      count * 0x1c -- that is what fixes the 0x08 header.
//
//   4. COM SLOTS PINNED (no DX7 headers exist in the toolchain; these come
//      from argument counts, adjacency and the surrounding code):
//
//        IDirectDraw7        +0x10 CreateClipper(dwFlags, ppClipper, pUnk)
//                            +0x18 CreateSurface(pDesc, ppSurface, pUnk)
//        IDirectDrawSurface7 +0x08 Release
//                            +0x14 Blt(pDstRect, pSrc, pSrcRect, flags, pFx)
//                            +0x2c Flip(pTarget, dwFlags)
//                            +0x58 GetSurfaceDesc(pDesc)
//                            +0x64 Lock(pRect, pDesc, dwFlags, hEvent)
//                            +0x70 SetClipper(pClipper)
//                            +0x74 SetColorKey(dwFlags, pColorKey)
//                            +0x80 Unlock(pRect)
//        IDirectDrawClipper  +0x20 SetHWnd(dwFlags, hWnd)
//        IDirect3DDevice7    +0x14 BeginScene  +0x18 EndScene
//                            (already pinned in d3ddraw.cpp; the blit path
//                             uses the pair the other way round -- it ENDS
//                             the scene, blits, and BEGINS it again)
//
//      The Lock/Blt/CreateSurface trio cross-check each other: all three
//      pass a 0x7c-byte structure whose first dword is 0x7c, which is
//      sizeof(DDSURFACEDESC2); Blt additionally passes a 0x64-byte block
//      whose first dword is 0x64, which is sizeof(DDBLTFX); LockSurface
//      reads the answer out of that block at +0x24, which is lpSurface, and
//      LockScreen reads +0x10, which is lPitch.  Nothing else lines up.
//
//   5. A COMPILER LEAD, and it is a better one than anything in d3ddraw.cpp.
//      See the block comment above d3d_BlitToScreen: this module calls some
//      of its own functions with the arguments IN REGISTERS (eax, then edx,
//      then ecx) while STILL reserving the stack slots for them, and emits
//      a four-byte "load the argument off the stack into eax" preamble in
//      front of such a function so that it can also be called normally.
//      0x10036bc0/0x10036bc4 is the clean example.  It also uses MSVC's
//      __thiscall (`this` in ecx, callee pops) for its C++ classes, and it
//      links the MSVC CRT.  A compiler that is MSVC-ABI-compatible for
//      classes but does interprocedural register-parameter passing for free
//      functions is a very short list, and Intel's Win32 C/C++ compiler is
//      at the top of it.  The image also carries two non-MSVC section
//      names, `.data1` and `_DATA`.
//
// *********************************************************************** //

#include "ltbasedefs.h"
#include "../../LT2/lithshared/stdlith/dynarray.h"
#include "ltrect.h"


// ---------------------------------------------------------------------- //
//  The DirectDraw 7 surface, called the way d3d.ren calls it: through an
//  explicit vtable pointer with `this` pushed as the first argument.  Only
//  the slots this file reaches are named; the rest hold their offsets.
// ---------------------------------------------------------------------- //

struct DDSurface;
struct DDSurfaceDesc2;
struct DDBltFX;
struct DDColorKey;
struct DDClipper;


// sizeof == 0x7c, and the code proves it: every user stores 0x7c into the
// first dword before handing it to DirectDraw.
struct DDPixelFormat                                    // 0x20 bytes
{
	uint32 dwSize;                                      // +0x00
	uint32 dwFlags;                                     // +0x04
	uint32 dwFourCC;                                    // +0x08
	uint32 dwRGBBitCount;                               // +0x0c
	uint32 dwRBitMask;                                  // +0x10
	uint32 dwGBitMask;                                  // +0x14
	uint32 dwBBitMask;                                  // +0x18
	uint32 dwRGBAlphaBitMask;                           // +0x1c
};

struct DDSurfaceCaps                                    // 0x10 bytes
{
	uint32 dwCaps;                                      // +0x00
	uint32 dwCaps2;                                     // +0x04
	uint32 dwCaps3;                                     // +0x08
	uint32 dwCaps4;                                     // +0x0c
};

struct DDSurfaceDesc2
{
	uint32          dwSize;                             // +0x00
	uint32          dwFlags;                            // +0x04
	uint32          dwHeight;                           // +0x08
	uint32          dwWidth;                            // +0x0c
	int32           lPitch;                             // +0x10
	uint32          dwBackBufferCount;                  // +0x14
	uint32          dwMipMapCount;                      // +0x18
	uint32          dwAlphaBitDepth;                    // +0x1c
	uint32          dwReserved;                         // +0x20
	void*           lpSurface;                          // +0x24
	uint32          ddckCKDestOverlay[2];               // +0x28
	uint32          ddckCKDestBlt[2];                   // +0x30
	uint32          ddckCKSrcOverlay[2];                // +0x38
	uint32          ddckCKSrcBlt[2];                    // +0x40
	DDPixelFormat   ddpfPixelFormat;                    // +0x48
	DDSurfaceCaps   ddsCaps;                            // +0x68
	uint32          dwTextureStage;                     // +0x78
};                                                      // == 0x7c

struct DDColorKey
{
	uint32 dwColorSpaceLowValue;                        // +0x00
	uint32 dwColorSpaceHighValue;                       // +0x04
};

// Only the size matters here: every construction of one is memset to zero
// and then given dwSize = 0x64.  Nothing in this file reads a field of it.
// A Win32 RECT, which is what Blt takes.  The two blits build theirs
// member by member from the request's LTRects: a 16-byte struct copy is one
// `rep movsl` (sub_10036df0 below), and an LTRect local carries its
// constructor's four zero stores, and the shipped code has neither.
struct DDRect
{
	int32 left, top, right, bottom;
};

struct DDBltFX
{
	uint32 dwSize;                                      // +0x00
	uint8  m_Rest[0x60];                                // +0x04
};                                                      // == 0x64

struct DDSurfaceVtbl
{
	void*  _QueryInterface;                                             // +0x00
	void*  _AddRef;                                                     // +0x04
	uint32 (__stdcall* Release)(DDSurface*);                            // +0x08
	void*  _pad_0c[2];                                                  // +0x0c
	long   (__stdcall* Blt)(DDSurface*, DDRect*, DDSurface*,
	                        DDRect*, uint32, DDBltFX*);                 // +0x14
	void*  _pad_18[5];                                                  // +0x18
	long   (__stdcall* Flip)(DDSurface*, DDSurface*, uint32);           // +0x2c
	void*  _pad_30[10];                                                 // +0x30
	long   (__stdcall* GetSurfaceDesc)(DDSurface*, DDSurfaceDesc2*);    // +0x58
	void*  _pad_5c[2];                                                  // +0x5c
	long   (__stdcall* Lock)(DDSurface*, LTRect*, DDSurfaceDesc2*,
	                         uint32, void*);                            // +0x64
	void*  _pad_68[2];                                                  // +0x68
	long   (__stdcall* SetClipper)(DDSurface*, DDClipper*);             // +0x70
	long   (__stdcall* SetColorKey)(DDSurface*, uint32, DDColorKey*);   // +0x74
	void*  _pad_78[2];                                                  // +0x78
	long   (__stdcall* Unlock)(DDSurface*, LTRect*);                    // +0x80
};

struct DDSurface
{
	DDSurfaceVtbl* lpVtbl;
};

struct DDrawVtbl
{
	void*  _pad_00[4];                                                  // +0x00
	long   (__stdcall* CreateClipper)(void*, uint32,
	                                  DDClipper**, void*);              // +0x10
	void*  _pad_14;                                                     // +0x14
	long   (__stdcall* CreateSurface)(void*, DDSurfaceDesc2*,
	                                  DDSurface**, void*);              // +0x18
};

struct DDraw
{
	DDrawVtbl* lpVtbl;
};

struct DDClipperVtbl
{
	void*  _pad_00[8];                                                  // +0x00
	long   (__stdcall* SetHWnd)(DDClipper*, uint32, void*);             // +0x20
};

struct DDClipper
{
	DDClipperVtbl* lpVtbl;
};

// DirectDraw constants, spelled out because the toolchain's ddraw.h is the
// DirectX 5 one.  Each is here because the shipped code contains the
// literal; nothing is guessed.
#define DDSD_CAPS                       0x00000001
#define DDSD_HEIGHT                     0x00000002
#define DDSD_WIDTH                      0x00000004
#define DDSD_PIXELFORMAT                0x00001000

#define DDSCAPS_OFFSCREENPLAIN          0x00000040
#define DDSCAPS_SYSTEMMEMORY            0x00000800
#define DDSCAPS_TEXTURE                 0x00001000
#define DDSCAPS2_TEXTUREMANAGE          0x00000010

#define DDCKEY_SRCBLT                   0x00000008
#define DDBLT_KEYSRC                    0x00008000
#define DDBLT_WAIT                      0x01000000
#define DDLOCK_WAIT                     0x00000001
#define DDFLIP_WAIT                     0x00000001


// ---------------------------------------------------------------------- //
//  d3d.ren's own objects
// ---------------------------------------------------------------------- //

// One tile of an optimized surface: a texture holding up to 64x64 pixels of
// the source.  Stride 0x1c, and only the last three dwords are written by
// anything in this file.
struct D3DOptTile
{
	uint8      m_pad_00[0x10];                          // +0x00
	uint32     m_nWidth;                                // +0x10
	uint32     m_nHeight;                               // +0x14
	DDSurface* m_pTexture;                              // +0x18
};                                                      // == 0x1c

// The tile grid hanging off a surface's +0x84.  Allocated as one block; the
// declared [1] is the shipped allocation's `(count - 1) * 0x1c + 0x24`.
struct D3DOptSurface
{
	uint32     m_nCols;                                 // +0x00
	uint32     m_nRows;                                 // +0x04
	D3DOptTile m_Tiles[1];                              // +0x08
};

// The renderer's per-surface object.  0x8c bytes, zero-filled at creation.
struct D3DRenderData
{
	DDSurface*      m_pSurface;                         // +0x00
	DDSurfaceDesc2  m_Desc;                             // +0x04
	uint32          m_nColorKey;                        // +0x80
	D3DOptSurface*  m_pOptimized;                       // +0x84
	uint32          m_nUnknown88;                       // +0x88
};

// The engine's blit request.  See the header note; +0x14 and +0x18 are read
// only by the warp path (0x10035450) and are not named here because nothing
// in this family tells you what they are.
struct RenderBlitRequest
{
	D3DRenderData* m_pRenderData;                       // +0x00
	uint32         m_nFlags;                            // +0x04
	uint32         m_nColorKey;                         // +0x08
	LTRect*        m_pSrcRect;                          // +0x0c
	LTRect*        m_pDestRect;                         // +0x10
	float          m_fUnknown14;                        // +0x14
	uint32         m_nUnknown18;                        // +0x18
};

// The struct slot +0xc4 fills.  Only the members it writes are named; +0x00
// is left alone by that function, so it is not part of what it copies.
struct D3DBlock16 { uint32 m_d[4]; };
struct D3DRenderInfo
{
	uint32   m_nUnknown00;                              // +0x00
	uint32*  m_pUnknown04;                              // +0x04
	D3DBlock16 m_Unknown08;                             // +0x08
	D3DBlock16 m_Unknown18;                             // +0x18
	D3DBlock16 m_Unknown28;                             // +0x28
};


// ---------------------------------------------------------------------- //
//  Globals this family reaches
// ---------------------------------------------------------------------- //

extern DDraw*     g_pDirectDraw;                        // 0x1008bf74
extern DDSurface* g_pOffscreen;                         // 0x1008bf68
extern DDSurface* g_pPrimary;                           // 0x1008bf6c
extern DDSurface* g_pWindowedPrimary;                   // 0x1008bf70
extern DDClipper* g_pClipper;                           // 0x1008d97c

extern LTBOOL     g_bInOptimized2D;                     // 0x1008d7c4
extern LTBOOL     g_bIn3D;                              // 0x1008d7c8
struct  D3DDevice;
extern D3DDevice* g_pD3DDevice;                         // 0x1008d988

// 0x1008da40 and the RECT at 0x1008da44 -- the state LockScreen keeps so
// that UnlockScreen can hand DirectDraw the same rectangle back.
LTBOOL  g_bScreenLocked;                                // 0x1008da40
LTRect  g_ScreenLockRect;                               // 0x1008da44

// The screen geometry FlipScreen presents with.  Addresses only; this file
// does not establish what sets them.
extern uint32 g_nWindowWidth;                           // 0x1008cbb4
extern uint32 g_nWindowHeight;                          // 0x1008cbb0
extern void*  g_hWnd;                                   // 0x1008cbc0
extern LTBOOL g_bWindowed;                              // 0x1008cbbc

// Helpers outside this family, named after their addresses.
extern void*  d3d_Alloc(uint32 nSize);                  // 0x10021c50 -- allocates
                                                        //   AND zero-fills nSize
extern void   d3d_Free(void* pMem);                     // 0x10021cb0
extern void sub_10021d60(uint32 nLevel, const char* pszMsg, ...);  // 0x10021d60 AddDebugMessage (was d3d_Warning*; icl treats a callee named *Warn*/*Error* as cold)
extern void   sub_10033f30(D3DRenderData* pData);       // 0x10033f30
extern LTBOOL d3d_GetOptimized2DBlend(uint32* pBlend);  // 0x10034ed0 (slot +0xa4)
extern void   sub_10034f10(RenderBlitRequest* pRequest);// 0x10034f10
extern void   sub_10035450(RenderBlitRequest* pRequest);// 0x10035450
extern void   d3d_AddDirtyRect(LTRect* pRect);          // 0x1003b3d0
extern void   sub_1003b3a0();                           // 0x1003b3a0
extern void   sub_1003b240();                           // 0x1003b240
extern void*  memset(void* p, int c, uint32 n);         // 0x100809a0

// The Direct3D device, as d3ddraw.cpp models it.  Repeated here rather than
// shared because that file declares only BeginScene/EndScene and this file
// needs exactly the same two.
struct D3DDeviceVtbl
{
	void* _pad_00[5];                                   // +0x00
	long (__stdcall* BeginScene)(D3DDevice*);           // +0x14
	long (__stdcall* EndScene)(D3DDevice*);             // +0x18
};

struct D3DDevice
{
	D3DDeviceVtbl* lpVtbl;
};


// ---------------------------------------------------------------------- //
//  SLOT +0xc8 -- create a surface
// ---------------------------------------------------------------------- //
//
// 0x100367b0, 295 bytes.  DECODED.
//
// Not named by the client side; the body names it.  It builds a
// DDSURFACEDESC2 with DDSD_CAPS|DDSD_HEIGHT|DDSD_WIDTH, caps
// OFFSCREENPLAIN|SYSTEMMEMORY, calls IDirectDraw7::CreateSurface, and wraps
// the result in the 0x8c-byte render-data block that every other function in
// this file takes.  Slot +0xcc is its exact inverse.  Argument order is
// pinned by the description: the first argument lands in dwWidth (desc+0x0c)
// and the second in dwHeight (desc+0x08).
//
// The zero-fill of the block is free: 0x10021c50 memsets what it allocates,
// which is why +0x84 and +0x88 are never written here yet are safe to read.
// The redundant `memset(&m_Desc, 0, 0x7c)` that follows is in the shipped
// code all the same.
D3DRenderData* d3d_CreateSurface(uint32 nWidth, uint32 nHeight)
{
	DDSurfaceDesc2 desc;
	DDColorKey     key;
	DDSurface*     pSurface;
	D3DRenderData* pData;

	if (!g_pDirectDraw)
		return LTNULL;

	if (!nWidth || !nHeight)
		return LTNULL;

	memset(&desc, 0, sizeof(desc));
	desc.dwSize          = sizeof(desc);
	desc.dwFlags         = DDSD_CAPS | DDSD_HEIGHT | DDSD_WIDTH;
	desc.dwHeight        = nHeight;
	desc.dwWidth         = nWidth;
	desc.ddsCaps.dwCaps  = DDSCAPS_OFFSCREENPLAIN | DDSCAPS_SYSTEMMEMORY;

	if (g_pDirectDraw->lpVtbl->CreateSurface(g_pDirectDraw, &desc,
	                                         &pSurface, LTNULL))
	{
		return LTNULL;
	}

	pData = (D3DRenderData*)d3d_Alloc(sizeof(D3DRenderData));

	if (!pData)
	{
		pSurface->lpVtbl->Release(pSurface);
		return LTNULL;
	}

	pData->m_pSurface = pSurface;

	key.dwColorSpaceLowValue  = 0;
	key.dwColorSpaceHighValue = 0;
	pSurface->lpVtbl->SetColorKey(pSurface, DDCKEY_SRCBLT, &key);

	pData->m_nColorKey = 0;

	memset(&pData->m_Desc, 0, sizeof(pData->m_Desc));
	pData->m_Desc.dwSize = sizeof(pData->m_Desc);
	pSurface->lpVtbl->GetSurfaceDesc(pSurface, &pData->m_Desc);

	return pData;
}


// ---------------------------------------------------------------------- //
//  SLOT +0xcc -- destroy a surface
// ---------------------------------------------------------------------- //
//
// 0x100368e0, 52 bytes.  DECODED.
//
// Drops the optimized form (0x10033f30 is the same tile-release loop
// UnoptimizeSurface open-codes), releases the DirectDraw surface, frees the
// block.  Note it does NOT null m_pOptimized afterwards -- 0x10033f30 does
// not either; the block is gone by then.
// NEAR (same length, 5 instructions): the shipped code keeps the dead pad
// slot of the first call's argument area for Release's argument
// (`add $0x4; ...; mov %eax,(%esp)`) and loads the vtable into %edx before
// that store; this spelling pops both slots and `push`es.  Measured without
// the two locals: the slot is kept but the vtable goes to %eax after the
// store (still 5 off).  Also tried: the locals before the call, a function
// pointer local, a C++ COM interface, if-wrapped body, 0x10033f30's body in
// this TU, /G4 /G5, all no closer.
// 19 -> 7 bytes.  ONE surface local and the slot read into a
// function-pointer local: the surface is now loaded before the stack
// adjust and the vtable into %edx before the argument store, as shipped;
// what is left is the call itself -- shipped `mov %eax,(%esp) ; call
// *0x8(%edx)` (the dead slot reused), ours `mov 0x8(%edx),%edx ; push %eax
// ; call *%edx` after `add $8`.  Every spelling that keeps the slot
// (no locals, a pSurface local alone, an inline Release helper, a C++
// interface, `(*p->lpVtbl->Release)(p)`) loads the vtable after the store
// (11 off); every one that orders the loads (a vtable local, a vtable
// reference, a function-pointer local) pushes.
// Closed: Release through a C++ interface that DERIVES from an
// IUnknown-like base. A virtual call to an override makes icl load the
// vtable before storing the argument, as shipped; without the base it
// stores first.
struct IUnkB { virtual long __stdcall QueryInterface(void*, void**) = 0; virtual uint32 __stdcall AddRef() = 0; virtual uint32 __stdcall Release() = 0; };
struct ISurfB : public IUnkB { virtual long __stdcall QueryInterface(void*, void**) = 0; virtual uint32 __stdcall AddRef() = 0; virtual uint32 __stdcall Release() = 0; };
void d3d_DeleteSurface(D3DRenderData* pData)
{
	if (!pData)
		return;

	sub_10033f30(pData);

	ISurfB* pSurface = (ISurfB*)pData->m_pSurface;
	pSurface->Release();

	d3d_Free(pData);
}


// ---------------------------------------------------------------------- //
//  SLOT +0xd4 -- lock a surface
// ---------------------------------------------------------------------- //
//
// 0x10036950, 74 bytes.  DECODED.
//
// Returns the pixels or NULL.  Nothing else: the pitch comes from
// GetSurfaceInfo, which is why this needs no out-parameters where
// LockScreen has two.  What pins the return value is the read at
// desc + 0x24, which is DDSURFACEDESC2::lpSurface.
//
// COMPILER: the null-argument path shares the failure path's
// `xor %eax,%eax` through a two-byte `jmp`, and the frame is opened with
// `sub $0x90` for a 0x7c-byte local.
void* d3d_LockSurface(D3DRenderData* pData)
{
	DDSurfaceDesc2 desc;

	if (!pData)
		return LTNULL;

	desc.dwSize = sizeof(desc);

	// Success on the fall-through of the test, the NULL last: written the
	// other way round (`if (Lock(...)) return NULL; return lpSurface;`) the
	// same 74 bytes come out with the null-argument `jmp` hosted after the
	// success block instead of before it (8 bytes differ).
	if (pData->m_pSurface->lpVtbl->Lock(pData->m_pSurface, LTNULL, &desc,
	                                    0, LTNULL) == 0)
	{
		return desc.lpSurface;
	}

	return LTNULL;
}


// ---------------------------------------------------------------------- //
//  SLOT +0xd8 -- unlock a surface
// ---------------------------------------------------------------------- //
//
// 0x100369a0, 43 bytes.  DECODED.  Returns nothing; %eax is never set.
void d3d_UnlockSurface(D3DRenderData* pData)
{
	if (!pData)
		return;

	pData->m_pSurface->lpVtbl->Unlock(pData->m_pSurface, LTNULL);
}


// ---------------------------------------------------------------------- //
//  SLOT +0xe4 -- lock the screen
// ---------------------------------------------------------------------- //
//
// 0x100369d0, 188 bytes.  DECODED.
//
// The rectangle is copied into a GLOBAL, not a local, because Unlock has to
// be handed the identical rectangle and UnlockScreen takes no arguments.
// g_bScreenLocked is the re-entry guard: a second lock refuses rather than
// nesting.
//
// Both outputs are separately null-checked, exactly like GetSurfaceInfo, and
// come out of the description at +0x24 (lpSurface) and +0x10 (lPitch).
//
// COMPILER: `xor %eax,%eax; inc %eax` for the success return, and %esi/%edi
// are saved into frame locals rather than pushed.
LTBOOL d3d_LockScreen(int32 nLeft, int32 nTop, int32 nRight, int32 nBottom,
                      void** ppPixels, uint32* pPitch)
{
	DDSurfaceDesc2 desc;

	// One test: as two `if ... return` statements the null-screen branch
	// gets a `jmp` trampoline of its own at the end (197 bytes, not 188).
	if (!g_pOffscreen || g_bScreenLocked)
		return LTFALSE;

	desc.dwSize = sizeof(desc);

	g_ScreenLockRect.left   = nLeft;
	g_ScreenLockRect.top    = nTop;
	g_ScreenLockRect.right  = nRight;
	g_ScreenLockRect.bottom = nBottom;

	if (g_pOffscreen->lpVtbl->Lock(g_pOffscreen, &g_ScreenLockRect, &desc,
	                               DDLOCK_WAIT, LTNULL))
	{
		return LTFALSE;
	}

	if (ppPixels)
		*ppPixels = desc.lpSurface;

	if (pPitch)
		*pPitch = desc.lPitch;

	g_bScreenLocked = LTTRUE;

	return LTTRUE;
}


// ---------------------------------------------------------------------- //
//  SLOT +0xe8 -- unlock the screen
// ---------------------------------------------------------------------- //
//
// 0x10036a90, 74 bytes.  DECODED.
//
// The trailing d3d_AddDirtyRect is what makes the locked region reach the
// display: 0x1003b3d0 accumulates rectangles into a 0x63-entry array at
// 0x1008e734 with its count at 0x1008e730, and FlipScreen flushes them.
// The BlitToScreen path calls it with its destination rectangle for the same
// reason, which is the cross-check that it is a dirty-rect list and not
// something else.
void d3d_UnlockScreen()
{
	if (!g_pOffscreen || !g_bScreenLocked)
		return;

	g_pOffscreen->lpVtbl->Unlock(g_pOffscreen, &g_ScreenLockRect);

	g_bScreenLocked = LTFALSE;

	d3d_AddDirtyRect(&g_ScreenLockRect);
}

// ---------------------------------------------------------------------- //
//  M-P0-03: fast FillRect onto the screen
// ---------------------------------------------------------------------- //
//
// The engine's FillRect fills the screen by locking the whole back buffer
// (LockScreen above) and writing the pixels itself, which makes the video
// card hand the frame back and take it again -- for every rectangle.  This
// is the same fill done by DirectDraw: a colour-fill Blt onto the back
// buffer, ended and restarted around the scene exactly as the DirectDraw
// blit path does it, and the rectangle goes to the dirty list as an unlocked
// screen's would.  nPixel is already in the screen's pixel format.  The
// engine finds it through GetRenderObject("FillScreenRect") (slot +0xb8,
// w26_10020fd0_osoiqip.cpp), so a retail d3d.ren simply does not offer it
// and the engine keeps its own path.  Returns FALSE when it cannot fill.
#define DDBLT_COLORFILL                 0x00000400

struct DDBltFXFill
{
	uint32 dwSize;                                      // +0x00
	uint8  m_pad_04[0x50 - 0x04];                       // +0x04
	uint32 dwFillColor;                                 // +0x50
	uint8  m_pad_54[0x64 - 0x54];                       // +0x54
};                                                      // == DDBltFX

extern "C" LTBOOL __cdecl d3d_FillScreenRect(LTRect* pRect, uint32 nPixel)
{
	DDRect      rcDest;
	DDBltFXFill fx;
	long        hResult;

	if (!g_pOffscreen || !pRect || g_bScreenLocked)
		return LTFALSE;

	rcDest.left   = pRect->left;
	rcDest.top    = pRect->top;
	rcDest.right  = pRect->right;
	rcDest.bottom = pRect->bottom;

	if (rcDest.left >= rcDest.right || rcDest.top >= rcDest.bottom)
		return LTTRUE;

	memset(&fx, 0, sizeof(fx));
	fx.dwSize      = sizeof(fx);
	fx.dwFillColor = nPixel;

	if (g_bIn3D && g_pD3DDevice)
		g_pD3DDevice->lpVtbl->EndScene(g_pD3DDevice);

	hResult = g_pOffscreen->lpVtbl->Blt(g_pOffscreen, &rcDest, LTNULL, LTNULL,
	                                    DDBLT_COLORFILL | DDBLT_WAIT,
	                                    (DDBltFX*)&fx);

	if (g_bIn3D && g_pD3DDevice)
		g_pD3DDevice->lpVtbl->BeginScene(g_pD3DDevice);

	if (hResult != 0)
		return LTFALSE;

	d3d_AddDirtyRect((LTRect*)&rcDest);
	return LTTRUE;
}
// ---------------------------------------------------------------------- //
//  SLOTS +0xe0 / +0xdc -- UnoptimizeSurface (0x10034510) and
//  OptimizeSurface (0x10034580) are in w81_10034510_os.cpp: this file is
//  /Os /Oi- /Qip and they are not (see that file's header).
// ---------------------------------------------------------------------- //
extern LTBOOL d3d_OptimizeSurface(D3DRenderData* pData, uint32 nColorKey); // 0x10034580


// ---------------------------------------------------------------------- //
//  The DirectDraw blit onto the screen -- 0x10036bc0 / 0x10036bc4
// ---------------------------------------------------------------------- //
//
// 300 bytes.  DECODED.  Not a RenderStruct slot; BlitToScreen's fallback
// when the optimized path is unavailable.
//
// READ THIS -- IT IS THE BEST COMPILER EVIDENCE IN THE MODULE.
//
// The function has TWO entry points four bytes apart:
//
//     10036bc0: 8b 44 24 04    mov 0x4(%esp),%eax     <- nothing calls this
//     10036bc4: 55             push %ebp              <- the argument is
//     10036bc5: 8b ec          mov %esp,%ebp             ALREADY in %eax
//
// and its one caller (BlitToScreen at 0x10036d73) does `mov %edx,%eax;
// call 0x10036bc4`, jumping PAST the stack load.  A scan of the whole .text
// finds zero references to 0x10036bc0.  The same shape appears at
// 0x10036df0/0x10036df4 -- and there BOTH entries are live: 0x10033ffd
// calls 0x10036df0 with a pushed argument, 0x10036f21 calls 0x10036df4 with
// the argument in %eax, and 0x10036df0 is the address stored in the
// RenderStruct, because the ENGINE has to call it normally.
//
// So this compiler passes arguments to its own functions in registers --
// %eax first, then %edx, then %ecx (0x10034580's call to 0x10033fac uses all
// three) -- while STILL reserving the stack slots the arguments would have
// occupied (`add $0xfffffff0,%esp` around that call, with nothing written
// into it), and it emits a four-byte stack-loading preamble in front of such
// a function so the address can still be handed out.  That is
// interprocedural register-parameter passing, not any documented __fastcall:
// MSVC's is %ecx,%edx and Borland's does not reserve.  Meanwhile the
// module's C++ classes use plain MSVC __thiscall -- 0x1007a150 is
// `ret $0x4` with `this` in %ecx -- and it links the MSVC CRT.
//
// MSVC-ABI-compatible for classes, register-parameter for free functions,
// frame pointers everywhere, GAS-style lea-nop padding, `.data1` and `_DATA`
// section names.  Intel's Win32 C/C++ compiler of that era fits all of it.
//
// The body itself: end the 3D scene if one is open, Blt the surface onto the
// screen with the source colour key if the request asks for one, restart the
// scene, and add the destination rectangle to the dirty list.  The colour
// key is only re-set when it differs from the one cached at +0x80, which is
// the whole reason that field exists.
void d3d_BlitToScreen_DD(RenderBlitRequest* pRequest)
{
	D3DRenderData* pData = pRequest->m_pRenderData;
	DDRect         rcSrc;
	DDRect         rcDest;
	LTRect*        pSrc;
	LTRect*        pDest;
	DDBltFX        fx;
	uint32         nFlags;

	if (g_bIn3D)
	{
		sub_10021d60(0x14, "Warning: drawing a nonoptimized surface while in "
		                  "3D mode.");

		if (g_pD3DDevice)
			g_pD3DDevice->lpVtbl->EndScene(g_pD3DDevice);
	}

	pSrc = pRequest->m_pSrcRect;
	rcSrc.left    = pSrc->left;
	rcSrc.top     = pSrc->top;
	rcSrc.right   = pSrc->right;
	rcSrc.bottom  = pSrc->bottom;

	pDest = pRequest->m_pDestRect;
	rcDest.left   = pDest->left;
	rcDest.top    = pDest->top;
	rcDest.right  = pDest->right;
	rcDest.bottom = pDest->bottom;

	fx.dwSize = sizeof(fx);

	nFlags = 0;

	if (pRequest->m_nFlags & 1)
	{
		if (pData->m_nColorKey != pRequest->m_nColorKey)
		{
			DDColorKey key;

			key.dwColorSpaceLowValue  = pRequest->m_nColorKey;
			key.dwColorSpaceHighValue = pRequest->m_nColorKey;

			pData->m_pSurface->lpVtbl->SetColorKey(pData->m_pSurface,
			                                       DDCKEY_SRCBLT, &key);

			pData->m_nColorKey = pRequest->m_nColorKey;
		}

		nFlags = DDBLT_KEYSRC;
	}

	g_pOffscreen->lpVtbl->Blt(g_pOffscreen, &rcDest, pData->m_pSurface,
	                          &rcSrc, nFlags, &fx);

	if (g_bIn3D)
	{
		if (g_pD3DDevice)
			g_pD3DDevice->lpVtbl->BeginScene(g_pD3DDevice);
	}

	d3d_AddDirtyRect((LTRect*)&rcDest);
}


// ---------------------------------------------------------------------- //
//  SLOT +0xec -- blit a surface onto the screen
// ---------------------------------------------------------------------- //
//
// 0x10036cf0, 164 bytes.  DECODED.
//
// Returns nothing, which matches the client side ignoring the answer.
//
// Three refusals (no screen, no request, no render data), then the choice
// between the two blit paths.  Both conditions read the SAME three things --
// in Optimized2D, in 3D, and a non-zero blend -- so the shape is one test
// with the optimize attempt hanging off it:
//
//     if (in optimized 2D && in 3D && blend && !optimized)
//         OptimizeSurface(pData, request colour key);
//     if (optimized && in optimized 2D && in 3D)
//         textured path;
//     else
//         DirectDraw path;
//
// The second test re-reads both globals rather than reusing the first
// test's values, which is why they are written out twice here.
//
// The blend is fetched through slot +0xa4's implementation (0x10034ed0),
// which d3d.cpp calls dead because nothing in lithtech.exe reaches the slot.
// It is not dead: the renderer calls it internally.
void d3d_BlitToScreen(RenderBlitRequest* pRequest)
{
	D3DRenderData* pData;
	uint32         nBlend;

	if (!g_pOffscreen)
		return;

	if (!pRequest)
		return;

	pData = pRequest->m_pRenderData;

	if (!pData)
		return;

	d3d_GetOptimized2DBlend(&nBlend);

	if (g_bInOptimized2D && g_bIn3D && nBlend && !pData->m_pOptimized)
		d3d_OptimizeSurface(pRequest->m_pRenderData, pRequest->m_nColorKey);

	if (pData->m_pOptimized && g_bInOptimized2D && g_bIn3D)
		sub_10034f10(pRequest);
	else
		d3d_BlitToScreen_DD(pRequest);
}


// ---------------------------------------------------------------------- //
//  SLOT +0xf0 -- blit a surface onto the screen through four corner points
// ---------------------------------------------------------------------- //
//
// 0x10036da0, 80 bytes.  DECODED.
//
// The only blit whose answer is checked, and the body says exactly why: it
// has no DirectDraw fallback.  Six refusals, every one of them returning
// zero, and the caller drops to its software warp path on any of them.  A
// warped blit needs the textured path, so an unoptimized surface simply
// cannot be served.
//
// NOTE FOR THE CLIENT SIDE: the shipped body reads ONLY its first argument.
// The point array and the literal 4 that the call site pushes are never
// touched -- 0x10035450 is called with the request alone.  So either the
// corner points travel inside the request (+0x14 and +0x18 are read by
// 0x10035450 and are unaccounted for) or the renderer ignores them.  Written
// with all three parameters because the call site pushes three and this is
// __cdecl; dropping them would still link but would misstate the contract.
//
// COMPILER: `xor %eax,%eax; inc %eax` for the success return, and it is one
// of the few functions here with no frame pointer.
LTBOOL d3d_BlitWarpedToScreen(RenderBlitRequest* pRequest, void* pPoints,
                              uint32 nPoints)
{
	D3DRenderData* pData;

	if (g_pOffscreen && pRequest &&
	    (pData = pRequest->m_pRenderData) != LTNULL &&
	    pData->m_pOptimized && g_bInOptimized2D && g_bIn3D)
	{
		sub_10035450(pRequest);
		return LTTRUE;
	}

	return LTFALSE;
}


// ---------------------------------------------------------------------- //
//  SLOT +0xfc -- blit the screen into a surface
// ---------------------------------------------------------------------- //
//
// 0x10036ae0, 216 bytes.  DECODED.
//
// The mirror of the DirectDraw path above, and the confirmation of the rect
// convention: `this` is the request's own surface, the screen is
// lpDDSrcSurface, +0x0c is lpSrcRect and +0x10 is lpDestRect -- the same two
// members in the same two roles as the other direction.
//
// No colour key, no dirty rect (nothing on screen changed), and no null
// checks at all: the render data is dereferenced before the very first
// branch.  Returns nothing.
void d3d_BlitFromScreen(RenderBlitRequest* pRequest)
{
	D3DRenderData* pData = pRequest->m_pRenderData;
	DDRect         rcSrc;
	DDRect         rcDest;
	LTRect*        pSrc;
	LTRect*        pDest;
	DDBltFX        fx;

	if (g_bIn3D)
	{
		sub_10021d60(0x14, "Warning: drawing a nonoptimized surface while in "
		                  "3D mode.");

		if (g_pD3DDevice)
			g_pD3DDevice->lpVtbl->EndScene(g_pD3DDevice);
	}

	pSrc = pRequest->m_pSrcRect;
	rcSrc.left    = pSrc->left;
	rcSrc.top     = pSrc->top;
	rcSrc.right   = pSrc->right;
	rcSrc.bottom  = pSrc->bottom;

	pDest = pRequest->m_pDestRect;
	rcDest.left   = pDest->left;
	rcDest.top    = pDest->top;
	rcDest.right  = pDest->right;
	rcDest.bottom = pDest->bottom;

	fx.dwSize = sizeof(fx);

	pData->m_pSurface->lpVtbl->Blt(pData->m_pSurface, &rcDest, g_pOffscreen,
	                               &rcSrc, 0, &fx);

	if (g_bIn3D)
	{
		if (g_pD3DDevice)
			g_pD3DDevice->lpVtbl->BeginScene(g_pD3DDevice);
	}
}


// ---------------------------------------------------------------------- //
//  SLOT +0xc4 -- hand the engine a block of renderer settings
// ---------------------------------------------------------------------- //
//
// 0x10036df0 (stack entry) / 0x10036df4 (register entry), 68 bytes.
// DECODED as a byte copy; the MEANING is not established.
//
// Copies one pointer and three 16-byte blocks out of consecutive globals at
// 0x1008bf2c..0x1008bf5f into the caller's structure, starting at ITS +0x04
// -- +0x00 is left alone.  The three blocks are moved with `rep movsl` of 4
// dwords each, so they are aggregates, not four separate members.  Called
// once at renderer install with 0x004b1758, and once internally from the
// screenshot path (0x10036f21) with a stack local -- which is the second
// half of the dual-entry evidence above.
//
// Not named: nothing here says what the blocks are.
//
// The three blocks are structure assignments (one `rep movsl` of four each
// in the shipped bytes; an element loop emits a counted loop).  What stays
// different is the entry: the shipped function is a /Qip two-entry pair,
// 0x10036df0 `mov 0x4(%esp),%eax` falling into the register body at
// 0x10036df4, because the screenshot slot (0x10036e40, same TU) calls it
// with the argument in %eax.  That caller is now in this file (moved from
// w26_10036e40_osoi.cpp, below), and with it both the stack entry
// 0x10036df0 and the register body 0x10036df4 are byte-identical.
void sub_10036df0(D3DRenderInfo* pInfo)
{
	extern uint32* g_pUnknown_1008bf2c;                 // 0x1008bf2c
	extern uint32  g_Unknown_1008bf30[4];               // 0x1008bf30
	extern uint32  g_Unknown_1008bf40[4];               // 0x1008bf40
	extern uint32  g_Unknown_1008bf50[4];               // 0x1008bf50

	pInfo->m_pUnknown04 = g_pUnknown_1008bf2c;
	pInfo->m_Unknown08  = *(D3DBlock16*)g_Unknown_1008bf30;
	pInfo->m_Unknown18  = *(D3DBlock16*)g_Unknown_1008bf40;
	pInfo->m_Unknown28  = *(D3DBlock16*)g_Unknown_1008bf50;
}


// ---------------------------------------------------------------------- //
//  SLOT +0xf4 -- screenshot
// ---------------------------------------------------------------------- //
//
// The pixel buffer is dynarray.h's real CMoArray<uint32>
// (the real-container lever), declared AFTER req.Construct() so that it is constructed
// there: the shipped body clears its array pointer (`xor %esi,%esi`) right
// after the 0x10079fd0 call, and the 20-byte CMoArray (vptr, array,
// count, DefaultCache) is what makes the frame 0x178 (the 8-byte hand
// copy gave 0x170).  955 -> 910 bytes (shipped 913), 736 -> 432
// differing, 273 instructions on both sides, the first 24 bytes
// identical; what is left is register choice (the array in %ebx where
// shipped has %esi, the width*height product in %eax for %ebx, the file
// pointer kept in %esi where shipped spills it) and one sunk block.
//
// 0x10036e40, 913 bytes.  MakeScreenShot(pFilename) -- moved here from
// w26_10036e40_osoi.cpp, which wrote it from the decode that
// stood here.  It calls 0x10036df0 above through its /Qip REGISTER entry
// (`mov -0x164(%ebp),%eax ; call 0x10036df4`), so the shipped TU held both,
// and this file's /Os /Oi- /Qip is w26's /Os /Oi- plus that: measured,
// 0x10036df0 and its body 0x10036df4 become byte-identical (48 bytes off
// as a lone stack function here before) and 0x10036e40 goes from 761 to
// 736 bytes off.  The DirectDraw calls use this file's C vtable.
//

extern "C"
{
	struct _iobuf;
	typedef struct _iobuf FILE;

	FILE*        __cdecl fopen(const char* pszName, const char* pszMode);           // 0x1008107b
	unsigned int __cdecl fwrite(const void* pBuf, unsigned int nSize,
	                            unsigned int nCount, FILE* pFile);                // 0x10081230
	int          __cdecl fclose(FILE* pFile);                                     // 0x100811a5
}


// ---------------------------------------------------------------------- //
//  The pixel format and the conversion request (big3a080.cpp's shapes).
// ---------------------------------------------------------------------- //
class PFormat
{
public:
	virtual uint32  Slot00(uint32 nType, uint32 nAlphaMask, uint32 nRedMask,
	                       uint32 nGreenMask, uint32 nBlueMask);
};

struct ConvertRequest
{
	ConvertRequest* Construct();                        // 0x10079fd0

	PFormat* m_pSrcFormat;                              // +0x00
	void*    m_pSrcData;                                // +0x04
	uint32   m_nSrcPitch;                               // +0x08
	uint32   m_nUnknown0c;                              // +0x0c
	PFormat* m_pDstFormat;                              // +0x10
	void*    m_pDstData;                                // +0x14
	uint32   m_nDstPitch;                               // +0x18
	uint32   m_nWidth;                                  // +0x1c
	uint32   m_nHeight;                                 // +0x20
	uint32   m_nFlags;                                  // +0x24
	uint8    m_SrcFormat[0x38];                         // +0x28
	uint8    m_DstFormat[0x38];                         // +0x60
};                                                      // 0x98

class FormatMgr
{
public:
	uint32 ConvertPixels(ConvertRequest* pRequest);     // 0x1007a130
};
extern FormatMgr g_1008daa0;                            // 0x1008daa0


#define DDLOCK_WAIT_26      0x00000001


// ---------------------------------------------------------------------- //
//  The bitmap headers (wingdi.h's, packed to 2 as the file format is).
// ---------------------------------------------------------------------- //
#pragma pack(push, 2)
struct BmpFileHeader26
{
	uint16  bfType;                                     // +0x00
	uint32  bfSize;                                     // +0x02
	uint16  bfReserved1;                                // +0x06
	uint16  bfReserved2;                                // +0x08
	uint32  bfOffBits;                                  // +0x0a
};                                                      // 0x0e
#pragma pack(pop)

struct BmpInfoHeader26
{
	uint32  biSize;                                     // +0x00
	long    biWidth;                                    // +0x04
	long    biHeight;                                   // +0x08
	uint16  biPlanes;                                   // +0x0c
	uint16  biBitCount;                                 // +0x0e
	uint32  biCompression;                              // +0x10
	uint32  biSizeImage;                                // +0x14
	long    biXPelsPerMeter;                            // +0x18
	long    biYPelsPerMeter;                            // +0x1c
	uint32  biClrUsed;                                  // +0x20
	uint32  biClrImportant;                             // +0x24
};                                                      // 0x28


// The pixel buffer is StdLith's CMoArray<uint32> from dynarray.h (included
// at the top); its SetSize/Term expand inline on g_DefAlloc (0x10091df0).


void    sub_10021d10(const char* pszFmt, ...);          // console print


// ======================================================================= //
//  0x10036e40  913 bytes.  MakeScreenShot(pFilename).  IDENTICAL: the
//  pixel count is a named local, so the bitmap size reuses it
//  (`lea 0x36(%ebx,%ebx,2)`) and %ebx is taken for it all through.
// ======================================================================= //
void sub_10036e40(const char* pFilename)
{
	ConvertRequest      req;
	DDSurfaceDesc2      ddsd;
	BmpFileHeader26     bfh;
	BmpInfoHeader26     bih;
	uint32              nWidth;
	uint32              nHeight;
	uint32              nPixels;
	uint32              x;
	uint32              y;
	uint32*             pPixel;
	long                hr;
	FILE*               fp;

	req.Construct();
	CMoArray<uint32>    buffer;

	if (!g_pOffscreen)
		return;

	memset(&ddsd, 0, sizeof(ddsd));
	ddsd.dwSize = sizeof(ddsd);
	g_pOffscreen->lpVtbl->GetSurfaceDesc(g_pOffscreen, &ddsd);

	nWidth  = ddsd.dwWidth;
	nHeight = ddsd.dwHeight;

	nPixels = nWidth * nHeight;
	if (!buffer.SetSize(nPixels))
		return;

	ddsd.dwSize = sizeof(ddsd);
	hr = g_pOffscreen->lpVtbl->Lock(g_pOffscreen, LTNULL, &ddsd, DDLOCK_WAIT_26, LTNULL);
	if (hr != 0)
	{
		sub_10021d10("ScreenShot: g_pOffscreen->Lock returned %d.", hr);
		return;
	}

	sub_10036df0((D3DRenderInfo*)req.m_pSrcFormat);
	req.m_pSrcData  = ddsd.lpSurface;
	req.m_nSrcPitch = ddsd.lPitch;

	req.m_pDstFormat->Slot00(3, 0xff000000, 0x00ff0000, 0x0000ff00, 0x000000ff);
	req.m_pDstData  = buffer.GetArray();
	req.m_nDstPitch = nWidth * sizeof(uint32);
	req.m_nWidth    = nWidth;
	req.m_nHeight   = nHeight;
	req.m_nFlags    = 0;

	hr = g_1008daa0.ConvertPixels(&req);
	g_pOffscreen->lpVtbl->Unlock(g_pOffscreen, LTNULL);

	if (hr != 0)
	{
		sub_10021d10("ScreenShot: FormatMgr::ConvertPixels returned %d.", hr);
		return;
	}

	fp = fopen(pFilename, "wb");
	if (!fp)
	{
		sub_10021d10("ScreenShot: fopen(%s) failed.", pFilename);
		return;
	}

	memset(&bfh, 0, sizeof(bfh));
	memset(&bih, 0, sizeof(bih));

	bfh.bfType    = 0x4d42;                             // 'BM'
	bfh.bfSize    = nPixels * 3 + sizeof(bfh) + sizeof(bih);
	bfh.bfOffBits = sizeof(bfh) + sizeof(bih);

	bih.biSize        = sizeof(bih);
	bih.biWidth       = nWidth;
	bih.biHeight      = nHeight;
	bih.biPlanes      = 1;
	bih.biBitCount    = 24;
	bih.biCompression = 0;                              // BI_RGB

	fwrite(&bfh, sizeof(bfh), 1, fp);
	fwrite(&bih, sizeof(bih), 1, fp);

	for (y = 0; y < nHeight; y++)
	{
		pPixel = &buffer[(nHeight - y - 1) * nWidth];
		for (x = 0; x < nWidth; x++)
		{
			fwrite(pPixel, 3, 1, fp);
			pPixel++;
		}
	}

	fclose(fp);
	sub_10021d10("ScreenShot: Created %s successfully.", pFilename);
}


// ---------------------------------------------------------------------- //
//  SLOT +0xbc -- present
// ---------------------------------------------------------------------- //
//
// 0x100371e0, 651 bytes.  DECODED except for the two helpers it tail-calls.
//
// Three presentation paths, chosen by the flags argument and by whether
// there is a windowed primary:
//
//   dwFlags & 4                 -> 0x1003b240 and nothing else
//   windowed (0x1008bf70 != 0)  -> work out where the client area is on the
//                                  screen, make a clipper for it the first
//                                  time through, and Blt the primary onto
//                                  the windowed primary with DDBLT_WAIT
//   dwFlags & 2                 -> Blt g_pOffscreen onto the primary,
//                                  whole surface, DDBLT_WAIT
//   otherwise                   -> primary->Flip(NULL, DDFLIP_WAIT)
//
// then 0x1003b3a0, which is the dirty-rect list's other end.
//
// The clipper construction pins two more COM slots: IDirectDraw7 +0x10 is
// CreateClipper (four arguments, the third an out-pointer) and
// IDirectDrawClipper +0x20 is SetHWnd, and the new clipper is installed with
// surface +0x70, SetClipper.
//
// NOT WRITTEN OUT, and here is the honest reason: the windowed path computes
// its destination rectangle TWICE.  The first computation (0x1003729e) makes
// it {x, y, x + 0x1008cb78 * 0x1008cbb4, y + 0x1008cb74 * 0x1008cbb0} and
// the second (0x10037305) overwrites all four dwords with
// {x, y, x + 0x1008cbb4, y + 0x1008cbb0} before the Blt.  There is no branch
// between them.  Either the source really does assign the rectangle twice --
// a scale factor that was dropped -- or there is a control-flow edge not
// accounted for.  Writing either version would be writing an indefensible
// body, so it stays a decode until it is settled.
//
// The source rectangle IS settled: {0, 0, 0x1008cbb4, 0x1008cbb0}, the back
// buffer's own size, which is what identifies those two globals as the width
// and the height.
//
// void d3d_FlipScreen(uint32 dwFlags);


// ---------------------------------------------------------------------- //
//  ON d3dmisc.cpp's d3d_GetSurfaceInfo, and whether this closes it
// ---------------------------------------------------------------------- //
//
//  It does not, and the reason is worth recording so nobody re-runs it.
//
//  The eleven differing bytes are the out-pointer living in %edx with the
//  value in %ecx, where cl puts the pointer in %ecx and the value in %edx.
//  The struct layout recovered here changes the SOURCE -- the three reads
//  become pData->m_Desc.dwWidth, .dwHeight and .lPitch through a
//  D3DRenderData* instead of three raw offsets -- but it cannot change that
//  allocation: the shipped instruction ORDER is identical to ours (load the
//  pointer, test it, load the value, store), only the two register NAMES are
//  swapped, and no legal rearrangement of the source reorders a test of a
//  parameter against a load it guards.
//
//  What WOULD flip it is a compiler that allocates its first scratch
//  register as %edx rather than %ecx, which is exactly what every other
//  function in this file does too -- LockSurface, UnlockSurface, LockScreen
//  and BlitFromScreen all take their first pointer into %edx.  cl never
//  does.  So the eleven bytes are one more instance of the module-wide
//  compiler difference, not a source problem, and GetSurfaceInfo should be
//  read as a size-exact decode that is waiting on the same answer as
//  everything else here.
//
//  One thing to fix in d3dmisc.cpp when the tree is next touched: its local
//  `struct D3DSurface` with m_nUnknown0c/10/14 is this file's
//  D3DRenderData, and the out-parameters are (width, height, pitch) in that
//  order.  The names can come off the "Unknown" list.
// ---------------------------------------------------------------------- //
