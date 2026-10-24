// *********************************************************************** //
//
//  MODULE:   clu401770 (.CPP)
//
//  PURPOSE:  Reconstruction of the nine enumerated functions of
//            lithtech.exe's BINK VIDEO translation unit, 0x00401770 ..
//            0x004022c0.
//
//  HOW THE MODULE WAS IDENTIFIED (data, not code):
//
//    * The three ".rdata" addresses the cluster stores at offset 0 of the
//      objects it builds -- 0x004a1440, 0x004a1454 and 0x004a147c -- are
//      not a string block: they are three VTABLES.  0x004a1440 is written
//      by the constructor at 0x004017e0 and the destructor at 0x00401820;
//      0x004a1454 and 0x004a147c are both written by the constructor at
//      0x00401b90, base first then derived, which is single inheritance
//      with the base constructor inlined.
//
//    * The .data literal pool from 0x004a7114 to 0x004a7480 is this TU's
//      string pool, in source order, and it names every function outright:
//
//         004a7114  "LT ERROR: %s returned %s (%s)"     <- g_pErrorFormat
//         004a7134  "SetInstanceHandle"
//         004a7148  "bm_SetInstanceHandle"              <- 0x00401770
//         004a7160  "60"     004a7164  " "     004a7168  "73"
//         004a716c  "Invalid Binkw32.dll, video playback disabled."
//         004a719c .. 004a7288   the thirteen "_BinkXxx@n" export names
//         004a72a0  "BinkVideoMgr::Init"                <- 0x00401850
//         004a72b4  "22"
//         004a72b8  "Unable to find Binkw32.dll, video playback disabled."
//         004a72f0  "Binkw32.dll"
//         004a72fc  "BinkVideoMgr"                      <- 0x00401a50
//         004a730c  "67"
//         004a7310  "BinkVideoInst::Init"               <- 0x00401d30
//         004a7324  "LT_MISSINGFILE"   004a7334 "BinkOpen failed"
//         004a7344  "BinkVideoInst::InitScreen"         <- 0x00401dd0
//         004a7360  "LT_ERROR"
//         004a736c  "IDirectDraw::CreateSurface failed"
//         004a7390  "IDirectDraw::GetDisplayMode failed"
//         004a73b4  "BACKBUFFER"     004a73c0 "LPDIRECTDRAW"
//         004a73d0  "BinkVideoInst::InitTexture"        <- 0x00402040
//         004a73ec  "BinkVideoInst::BindToSurface"      <- 0x00402200
//         004a740c  "BinkVideoInst::UpdateOnScreen"     (0x004022e0)
//         004a742c  "LT_NOTINITIALIZED"
//         004a7440  "GetHook(BACKBUFFER) failed"
//         004a745c  "BinkVideoInst::UpdateTextureVideo" (0x00402540)
//
//      The next literal, "ObjectDLLSetup" at 0x004a7484, belongs to the
//      server-binary loader and is where this TU's pool ends.
//
//    * The two workhorse callees are the error-reporting pair every
//      LithTech interface method uses:
//         0x0042e6f0  con_ConsolePrint / dsi_ServerError  (12 calls here)
//         0x0042d760  dsi_OnReturnError                   (11 calls here)
//      i.e. 23 of the cluster's calls are two expansions of
//      src/engine/server/de_error.h's REPORT_ERROR.
//
//  NAMING: every type, global and macro in this file carries a K suffix or
//  a k_ prefix so it cannot collide with src/engine/lithtech/*.cpp, which
//  already declare SharedTexture, TextureData, LTLink, BinkVideoInst,
//  con_ConsolePrint and RETURN_ERROR.
//
// *********************************************************************** //

#include <windows.h>
#include <string.h>

typedef int             LTRESULT;
typedef int             LTBOOL;
typedef unsigned long   uint32;
typedef unsigned short  uint16;
typedef unsigned char   uint8;

#define LTNULL 0

#define LT_OK                0
#define LT_ERROR             1
#define LT_FINISHED          2
#define LT_MISSINGFILE      22
#define LT_INVALIDPARAMS    60
#define LT_OUTOFMEMORY      67
#define LT_NOTINITIALIZED   73


// ======================================================================= //
//  THE ERROR MACROS
//
//  src/engine/server/de_error.h's, renamed.  The expansion is fixed by the
//  bytes at every one of the eleven sites below, e.g. 0x00401778:
//
//      push $0x3c            ; dsi_OnReturnError(60)
//      call 0x0042d760
//      mov  0x004b0ab0,%eax  ; g_DebugLevel
//      add  $0x4,%esp
//      cmp  $0x1,%eax        ; >= level
//      jl   <skip>
//      mov  0x004a7110,%eax  ; g_pErrorFormat
//      push $0x004a7164      ; " "
//      push $0x004a7160      ; "60"
//      push $0x004a7148      ; "bm_SetInstanceHandle"
//      push %eax
//      call 0x0042e6f0
//      add  $0x10,%esp
//      mov  $0x3c,%eax       ; return 60
// ======================================================================= //

extern "C" void dsi_OnReturnErrorK(int code);           // 0x0042d760
extern int      g_DebugLevelK;                          // 0x004b0ab0
extern char*    g_pErrorFormatK;                        // 0x004a7110
void            dsi_ServerErrorK(char* pFormat, ...);   // 0x0042e6f0

#define K_ERRSTR_(x) #x
#define K_ERRSTR(x)  K_ERRSTR_(x)
#define K_ERRDESC    " "

#define K_REPORT_ERROR(level, fnName, code) \
	dsi_OnReturnErrorK(code); \
	if (g_DebugLevelK >= level) \
		dsi_ServerErrorK(g_pErrorFormatK, fnName, K_ERRSTR(code), K_ERRDESC);

#define K_RETURN_ERROR(level, fnName, code) \
	{ \
		K_REPORT_ERROR(level, fnName, code) \
		return code; \
	}

// The form 0x00401d30 and 0x00401dd0 use: the code reported SYMBOLICALLY
// ("LT_MISSINGFILE" at 0x004a7324, "LT_ERROR" at 0x004a7360) and a real
// description instead of the lone space.
#define K_RETURN_ERROR_INFO(level, fnName, code, info) \
	{ \
		dsi_OnReturnErrorK(code); \
		if (g_DebugLevelK >= level) \
			dsi_ServerErrorK(g_pErrorFormatK, fnName, #code, info); \
		return code; \
	}

// 0x0042e6f0 again, called with ONE argument at 0x0040186a and 0x004019f2.
// Same function as dsi_ServerErrorK; declared twice only so the one-argument
// sites read as a console print, which is what clientconsole.cpp calls it.
#define con_ConsolePrintK dsi_ServerErrorK


// ======================================================================= //
//  THE ENGINE THIS FILE REACHES
// ======================================================================= //

void* k_operator_new(unsigned int n);            // 0x004290b0  operator new
// 0x004290c0 is operator delete; reached through `delete` below.

// 0x0040f7f0 -- `g_pClientMgr + 0x774`.  clientmgr.h names it; +0x124 is
// clientsound.h's m_pSoundSys ("null until the sound system is up") and
// +0x130 is the handle handed to Bink alongside _BinkOpenMiles@4.
class ILTClientSoundMgrK
{
public:
	uint8 m_pad_00[0x124];
	void* m_pSoundSys;                       // 0x124
	uint8 m_pad_128[0x130 - 0x128];
	void* m_hDigDriver;                      // 0x130  (name ours)
};

ILTClientSoundMgrK* ci_GetSoundMgrK();           // 0x0040f7f0

HWND dsi_GetMainWindow();                        // 0x0042e740  clientdsi.cpp

// 0x004b439c -- the render-up flag, g_RenderStruct's +0x44 (RenderStructK
// below).

// lttexmod.cpp's TextureData, plus the one field this TU also sets.
class TextureDataK
{
public:
	uint8  m_pad_00[0x18];
	uint32 m_Flags;                          // 0x18   bit 0x40 = TDFLAG_TEXMOD
	uint8  m_pad_1c[0xc4 - 0x1c];
	uint32 m_FlagsC4;                        // 0xc4   bit 0x04 (name ours)
	uint8  m_pad_c8[0xd4 - 0xc8];
	void*  m_pPixels;                        // 0xd4   (name ours; 0x0040258e)
	uint32 m_nPitch;                         // 0xd8   (name ours; 0x00402588)
};

#define K_TDFLAG_TEXMOD   0x40
#define K_TDFLAG_C4_BIT2  0x04

// 0x0042e8e0 / 0x0042ee30 -- lttexmod.cpp names both.  Seven cdecl
// arguments and one, respectively.
TextureDataK* tex_CreateTextureDataK(uint32 nBitDepth, uint32 nWidth, uint32 nHeight,
                                     uint32 nMipmaps, uint32 nUnknown5,
                                     uint32 nUnknown6, uint32 bBumpmap);
void          tex_FreeTextureDataK(TextureDataK* pData);

// clienttexmgr.cpp's SharedTextureView, field for field: 0x34 bytes, which
// is exactly what 0x00401b90 initialises and what makes BinkVideoInstK
// 0x68 bytes -- the size 0x00401a50 passes to operator new.
//
// +0x00 is an eight-byte sub-object with its own constructor (0x0045a350,
// "push $0; call 0x45a370") and a one-argument method (0x0045a370,
// "{ m_p00 = 0; m_p04 = arg; return 1; }").  0x00401b90 runs BOTH: the
// default construction, then the method with the SharedTexture itself as
// the argument.  lt2new3.cpp already models the same pair.
class CBankLinkK
{
public:
	CBankLinkK();                            // 0x0045a350
	LTBOOL Init(void* pOwner);               // 0x0045a370
	void   Release();                        // 0x0045a360

public:
	void* m_pNextFree;                       // 0x00
	void* m_pOwner;                          // 0x04
};

class LTLinkK
{
public:
	LTLinkK* m_pPrev;                        // 0x00
	LTLinkK* m_pNext;                        // 0x04
	void*    m_pData;                        // 0x08
};

class SharedTextureK
{
public:
	SharedTextureK();

public:
	CBankLinkK    m_Bank;                    // 0x00
	TextureDataK* m_pEngineData;             // 0x08
	void*         m_pRenderData;             // 0x0c
	LTLinkK       m_Link;                    // 0x10
	uint32        m_Unknown1c[2];            // 0x1c, 0x20
	void*         m_pFileRef;                // 0x24
	uint32        m_Unknown28;               // 0x28
	uint32        m_Unknown2c;               // 0x2c
	uint16        m_nFrameCode;              // 0x30
	uint16        m_nRefCount : 15;          // 0x32
	uint16        m_bMarked   : 1;           // 0x32 bit 15
};

// 0x00401b90 / 0x0048aba0 expand this inline, identically but for the
// sub-object's offset (+0x28 in Bink, +0x68 in Smack).  Neither +0x10 nor
// +0x14 nor +0x2c is touched, so they are not in the initialiser list.
//   * +0x1c/+0x20 are cleared through their OWN pointer and their OWN zero
//     (`lea 0x1c(%esi),%ecx; xor %eax,%eax; mov %eax,(%ecx); mov
//     %eax,0x4(%ecx)` at 0x00401bad..0x00401bbe) while every other field
//     shares %ebx: an inline /Oi memset of 8 bytes, not two assignments.
//   * +0x32 is stored as a whole word (`mov %bx,0x32(%esi)`, 0x00401bd2):
//     BOTH bitfields are cleared; clearing only the count gives `andw
//     $0x8000`.
inline SharedTextureK::SharedTextureK()
{
	m_pEngineData = LTNULL;
	m_pRenderData = LTNULL;
	m_Link.m_pData = this;
	memset(&m_Unknown1c, 0, sizeof(m_Unknown1c));
	m_pFileRef = LTNULL;
	m_Bank.Init(this);
	m_Unknown28 = 0;
	m_nRefCount = 0;
	m_bMarked = 0;
	m_nFrameCode = 0;
}

// 0x004628e0 / 0x00462900 -- clientrender.cpp names both.
void rif_BindTextureK(SharedTextureK* pTexture, LTBOOL bChanged);
void tex_FreeTextureDataForK(SharedTextureK* pTexture);

// The render DLL's dispatch table at 0x004b4358 (clientrender.cpp's
// g_RenderStruct); the screen size and render-up flag at +0x3c..+0x44 and
// the hook getter at +0xb8 (0x004b4410) are reached from here.
// clientworldinfo.cpp's slot map lists 0x00401dff and 0x00401e33 -- two of
// this file's three call sites -- against exactly that slot.
struct RenderStructK
{
	uint8  m_pad_00[0x3c];
	int    m_Width;                          // 0x3c  -> 0x004b4394
	int    m_Height;                         // 0x40  -> 0x004b4398
	int    m_bInited;                        // 0x44  -> 0x004b439c
	uint8  m_pad_48[0xb8 - 0x48];
	void*  (*GetHook)(char* pName);          // 0xb8  -> 0x004b4410
};

class RenderStruct;
extern RenderStruct g_RenderStruct;          // 0x004b4358
#define g_RenderStructK (*(RenderStructK*)&g_RenderStruct)


// ======================================================================= //
//  THE DIRECTDRAW SLOTS THIS FILE USES
//
//  Declared here rather than pulled out of <ddraw.h> so that every offset
//  below is the one read off the shipped bytes: CreateSurface at +0x18 and
//  GetDisplayMode at +0x30 of IDirectDraw's vtable, Blt at +0x14 of
//  IDirectDrawSurface's.  The descriptor's dwSize is 0x7c and the blit
//  effects block's is 0x64, which is DDSURFACEDESC2 and DDBLTFX.
// ======================================================================= //

struct DDPIXELFORMATK
{
	DWORD dwSize;                            // 0x00
	DWORD dwFlags;                           // 0x04
	DWORD dwFourCC;                          // 0x08
	DWORD dwRGBBitCount;                     // 0x0c
	DWORD dwRBitMask;                        // 0x10
	DWORD dwGBitMask;                        // 0x14
	DWORD dwBBitMask;                        // 0x18
	DWORD dwRGBAlphaBitMask;                 // 0x1c
};

struct DDSCAPS2K
{
	DWORD dwCaps;                            // 0x00
	DWORD dwCaps2;
	DWORD dwCaps3;
	DWORD dwCaps4;
};

struct DDSURFACEDESC2K                       // 0x7c bytes
{
	DWORD          dwSize;                   // 0x00
	DWORD          dwFlags;                  // 0x04
	DWORD          dwHeight;                 // 0x08
	DWORD          dwWidth;                  // 0x0c
	LONG           lPitch;                   // 0x10
	DWORD          dwBackBufferCount;        // 0x14
	DWORD          dwMipMapCount;            // 0x18
	DWORD          dwAlphaBitDepth;          // 0x1c
	DWORD          dwReserved;               // 0x20
	LPVOID         lpSurface;                // 0x24
	DWORD          ddckCKDestOverlay[2];     // 0x28
	DWORD          ddckCKDestBlt[2];         // 0x30
	DWORD          ddckCKSrcOverlay[2];      // 0x38
	DWORD          ddckCKSrcBlt[2];          // 0x40
	DDPIXELFORMATK ddpfPixelFormat;          // 0x48
	DDSCAPS2K      ddsCaps;                  // 0x68
	DWORD          dwTextureStage;           // 0x78
};

struct DDBLTFXK                              // 0x64 bytes
{
	DWORD dwSize;                            // 0x00
	DWORD dwPad[24];
};

#define K_DDSD_CAPS              0x00000001
#define K_DDSD_HEIGHT            0x00000002
#define K_DDSD_WIDTH             0x00000004
#define K_DDSCAPS_OFFSCREENPLAIN 0x00000040
#define K_DDSCAPS_VIDEOMEMORY    0x00004000
#define K_DDBLT_COLORFILL        0x00000400
#define K_DDBLT_DEPTHFILL        0x00000800
#define K_DDBLT_WAIT             0x01000000

class IDirectDrawSurfaceK
{
public:
	virtual HRESULT __stdcall QueryInterface(REFIID, LPVOID*);      // 0x00
	virtual ULONG   __stdcall AddRef();                             // 0x04
	virtual ULONG   __stdcall Release();                            // 0x08
	virtual HRESULT __stdcall AddAttachedSurface(IDirectDrawSurfaceK*);
	virtual HRESULT __stdcall AddOverlayDirtyRect(LPRECT);          // 0x10
	virtual HRESULT __stdcall Blt(LPRECT, IDirectDrawSurfaceK*,
	                              LPRECT, DWORD, DDBLTFXK*);        // 0x14
	// The rest of IDirectDrawSurface7 up to Unlock, for UpdateOnScreen
	// (BltFast +0x1c, Lock +0x64, Unlock +0x80).
	virtual HRESULT __stdcall BltBatch(LPVOID, DWORD, DWORD);       // 0x18
	virtual HRESULT __stdcall BltFast(DWORD, DWORD, IDirectDrawSurfaceK*,
	                                  LPRECT, DWORD);               // 0x1c
	virtual HRESULT __stdcall DeleteAttachedSurface(DWORD, IDirectDrawSurfaceK*);
	virtual HRESULT __stdcall EnumAttachedSurfaces(LPVOID, LPVOID);
	virtual HRESULT __stdcall EnumOverlayZOrders(DWORD, LPVOID, LPVOID);
	virtual HRESULT __stdcall Flip(IDirectDrawSurfaceK*, DWORD);    // 0x2c
	virtual HRESULT __stdcall GetAttachedSurface(LPVOID, IDirectDrawSurfaceK**);
	virtual HRESULT __stdcall GetBltStatus(DWORD);
	virtual HRESULT __stdcall GetCaps(LPVOID);
	virtual HRESULT __stdcall GetClipper(LPVOID*);                  // 0x3c
	virtual HRESULT __stdcall GetColorKey(DWORD, LPVOID);
	virtual HRESULT __stdcall GetDC(HDC*);
	virtual HRESULT __stdcall GetFlipStatus(DWORD);
	virtual HRESULT __stdcall GetOverlayPosition(LPLONG, LPLONG);   // 0x4c
	virtual HRESULT __stdcall GetPalette(LPVOID*);
	virtual HRESULT __stdcall GetPixelFormat(DDPIXELFORMATK*);
	virtual HRESULT __stdcall GetSurfaceDesc(DDSURFACEDESC2K*);
	virtual HRESULT __stdcall Initialize(LPVOID, DDSURFACEDESC2K*); // 0x5c
	virtual HRESULT __stdcall IsLost();
	virtual HRESULT __stdcall Lock(LPRECT, DDSURFACEDESC2K*, DWORD,
	                               HANDLE);                         // 0x64
	virtual HRESULT __stdcall ReleaseDC(HDC);
	virtual HRESULT __stdcall Restore();
	virtual HRESULT __stdcall SetClipper(LPVOID);                   // 0x70
	virtual HRESULT __stdcall SetColorKey(DWORD, LPVOID);
	virtual HRESULT __stdcall SetOverlayPosition(LONG, LONG);
	virtual HRESULT __stdcall SetPalette(LPVOID);
	virtual HRESULT __stdcall Unlock(LPRECT);                       // 0x80
};

#define K_DDLOCK_WAIT            0x00000001
#define K_DDLOCK_WRITEONLY       0x00000020
#define K_DDBLTFAST_WAIT         0x00000010

class IDirectDrawK
{
public:
	virtual HRESULT __stdcall QueryInterface(REFIID, LPVOID*);      // 0x00
	virtual ULONG   __stdcall AddRef();                             // 0x04
	virtual ULONG   __stdcall Release();                            // 0x08
	virtual HRESULT __stdcall Compact();                            // 0x0c
	virtual HRESULT __stdcall CreateClipper(DWORD, LPVOID*, IUnknown*);
	virtual HRESULT __stdcall CreatePalette(DWORD, LPVOID, LPVOID*, IUnknown*);
	virtual HRESULT __stdcall CreateSurface(DDSURFACEDESC2K*,
	                                        IDirectDrawSurfaceK**,
	                                        IUnknown*);             // 0x18
	virtual HRESULT __stdcall DuplicateSurface(IDirectDrawSurfaceK*,
	                                           IDirectDrawSurfaceK**);
	virtual HRESULT __stdcall EnumDisplayModes(DWORD, DDSURFACEDESC2K*,
	                                           LPVOID, LPVOID);     // 0x20
	virtual HRESULT __stdcall EnumSurfaces(DWORD, DDSURFACEDESC2K*,
	                                       LPVOID, LPVOID);         // 0x24
	virtual HRESULT __stdcall FlipToGDISurface();                   // 0x28
	virtual HRESULT __stdcall GetCaps(LPVOID, LPVOID);              // 0x2c
	virtual HRESULT __stdcall GetDisplayMode(DDSURFACEDESC2K*);     // 0x30
};


// ======================================================================= //
//  BINK
//
//  Every entry is looked up by its DECORATED name ("_BinkOpen@8"), so every
//  pointer is __stdcall and the argument count is in the name.  The four
//  surface constants the format cascade in InitScreen stores -- 3, 4, 9 and
//  10 -- are BINKSURFACE32, BINKSURFACE32R, BINKSURFACE555 and
//  BINKSURFACE565 of RAD's bink.h, in exactly the mask order those four
//  formats have.
// ======================================================================= //

struct BINKK
{
	uint32 Width;                            // 0x00
	uint32 Height;                           // 0x04
	uint32 Frames;                           // 0x08
	uint32 FrameNum;                         // 0x0c
};

#define K_BINKSURFACE32     3
#define K_BINKSURFACE32R    4
#define K_BINKSURFACE555    9
#define K_BINKSURFACE565   10

// Bink open flags, read off 0x00401d30 as literals.
#define K_BINKSNDTRACK      0x00004000
#define K_BINKOPENFLAG_2    0x20000000
#define K_BINKOPENFLAG_4    0x00800000
#define K_BINKOPENFLAG_8    0x04000000

// _BinkSetSoundSystem@8 takes the open routine itself as its first
// argument -- at 0x004019d9 and 0x004019e6 the value pushed under it is
// +0x18 or +0x1c, i.e. one of the two "open" entries.
typedef void   (__stdcall *TBinkSoundOpenK)(void* pParam);
typedef void   (__stdcall *TBinkSetSoundSystemK)(TBinkSoundOpenK pOpenProc, void* pParam);
typedef TBinkSoundOpenK TBinkOpenMilesK;
typedef TBinkSoundOpenK TBinkOpenDirectSoundK;
typedef void   (__stdcall *TBinkSetSoundTrackK)(uint32 nTrack);
typedef BINKK* (__stdcall *TBinkOpenK)(char* pFilename, uint32 nFlags);
typedef void   (__stdcall *TBinkCloseK)(BINKK* pBink);
typedef int    (__stdcall *TBinkWaitK)(BINKK* pBink);
typedef int    (__stdcall *TBinkCopyToBufferK)(BINKK* pBink, void* pDest, int nPitch,
                                               uint32 nDestHeight, uint32 nDestX,
                                               uint32 nDestY, uint32 nFlags);
typedef int    (__stdcall *TBinkDoFrameK)(BINKK* pBink);
typedef void   (__stdcall *TBinkNextFrameK)(BINKK* pBink);
typedef int    (__stdcall *TBinkIsSoftwareCursorK)(void* pHook, HCURSOR hCursor);
typedef int    (__stdcall *TBinkCheckCursorK)(HWND hWnd, int x, int y, int w, int h);
typedef void   (__stdcall *TBinkRestoreCursorK)(int bForce);


// ======================================================================= //
//  THE LIST
//
//  Not the SDK's ltlink.h list: the head is a bare LTLink* and the count
//  sits BEFORE it (mgr +0x04/+0x08, inst +0x10/+0x14).  The insert is
//  hand-rolled, and the bytes say so -- 0x00401b1a and 0x00401b1f RELOAD
//  `pAfter->m_pNext` across the store through pAfter->m_pNext->m_pPrev, so
//  the expression really is written twice.
// ======================================================================= //

struct LTListK
{
	void Init() { m_pHead = LTNULL; m_nElements = 0; }

	uint32   m_nElements;                    // 0x00
	LTLinkK* m_pHead;                        // 0x04
};

// The manager's list clears itself before the manager's vptr goes in
// (0x004017e8/0x004017eb), so it is constructed, not assigned.
struct LTListInitK : public LTListK
{
	LTListInitK() { m_nElements = 0; m_pHead = LTNULL; }
};

inline void k_AddToList(LTListK* pList, LTLinkK* pLink, void* pObj)
{
	if (pList->m_pHead)
	{
		LTLinkK* pAfter = pList->m_pHead->m_pPrev;

		pAfter->m_pNext->m_pPrev = pLink;
		pLink->m_pNext = pAfter->m_pNext;
		pLink->m_pPrev = pAfter;
		pAfter->m_pNext = pLink;
		pLink->m_pData = pObj;
	}
	else
	{
		// TieOff: `m_pPrev = m_pNext = this`, which is why the +0x04 store
		// comes out before the +0x00 one at 0x00401b2f.
		pLink->m_pPrev = pLink->m_pNext = pLink;
		pLink->m_pData = pObj;
		pList->m_pHead = pLink;
	}

	++pList->m_nElements;
}

// 0x0048b390 inlines this: the index goes out through a pointer that is
// null-tested (`lea 0x8(%esp); test`), and -1 means "not in the list".
inline LTLinkK* k_FindInList(LTListK* pList, void* pObj, uint32* pIndex)
{
	LTLinkK* pLink;
	uint32   i;

	i = 0;
	if (pList->m_pHead)
	{
		pLink = pList->m_pHead;
		do
		{
			if (pLink->m_pData == pObj)
			{
				if (pIndex)
					*pIndex = i;
				return pLink;
			}

			pLink = pLink->m_pNext;
			++i;
		}
		while (pLink != pList->m_pHead);
	}

	if (pIndex)
		*pIndex = (uint32)-1;
	return LTNULL;
}

// 0x0048b1b0 / 0x0048b390: unlink, move the head off the link, and clear
// the head when the count reaches zero.
inline void k_RemoveFromList(LTListK* pList, LTLinkK* pLink)
{
	pLink->m_pPrev->m_pNext = pLink->m_pNext;
	pLink->m_pNext->m_pPrev = pLink->m_pPrev;

	if (pLink == pList->m_pHead)
		pList->m_pHead = pList->m_pHead->m_pNext;

	if (--pList->m_nElements == 0)
		pList->m_pHead = LTNULL;
}


// ======================================================================= //
//  THE CLASSES
// ======================================================================= //

class BinkVideoInstK;

// The thing BindToSurface binds a video to: only its +0x30, a
// SharedTexture*, is reached.  0x00401c70's first walk puts the saved one
// back, which is iltvideomgr.h's "for a texture video, this restores the
// original textures on any polies it's bound to".
class CVideoPolyK
{
public:
	uint8           m_pad_00[0x30];
	SharedTextureK* m_pTexture;              // 0x30
};

// The twelve-byte notifier each hooked pointer carries at its +0x04: a
// static descriptor (0x004af4d8 / 0x004af4e0 -- lt2new3.cpp's section H,
// {callback, 0x004ab398}), the owner, and a third dword the constructor
// clears.  0x0048a640 / 0x0048a680 hand &node to 0x0045a400 / 0x0043c580,
// the unhook / hook pair.  Names ours.
//
// Both descriptors are DEFINED here, once (lt2new4.cpp's 0x0048a5f0 reaches
// them as an extern): the .data words are {0x0048a590, 0x004ab398} and
// {0x0048a5c0, 0x004ab398}, and they sit directly ahead of this TU's
// "%d videos, %d active" (0x004af4e8).  The callbacks are lt2new3.cpp's
// section-H twins; the shared second word is serverengine.cpp's
// g_DepUnlinkRecord.
class SUnknown48A590Arg;
class CDepRecord;
typedef unsigned long (*CVideoHookFnK)(SUnknown48A590Arg* pSrc, SUnknown48A590Arg* pDest,
                                       unsigned long nUnknown, unsigned long* pOut);
unsigned long sub_0048a590(SUnknown48A590Arg* pSrc, SUnknown48A590Arg* pDest,
                           unsigned long nUnknown, unsigned long* pOut);   // 0x0048a590
unsigned long sub_0048a5c0(SUnknown48A590Arg* pSrc, SUnknown48A590Arg* pDest,
                           unsigned long nUnknown, unsigned long* pOut);   // 0x0048a5c0
extern CDepRecord g_DepUnlinkRecord;                 // 0x004ab398

struct CVideoHookDescK
{
	CVideoHookFnK m_pfnNotify;               // 0x00
	CDepRecord*   m_pBase;                   // 0x04
};

CVideoHookDescK k_PolyHookDescK    = { sub_0048a590, &g_DepUnlinkRecord };  // 0x004af4d8
CVideoHookDescK k_TextureHookDescK = { sub_0048a5c0, &g_DepUnlinkRecord };  // 0x004af4e0

struct CVideoHookK
{
	CVideoHookDescK* m_pDesc;                // 0x00
	BinkVideoInstK*  m_pOwner;               // 0x04
	uint32           m_nUnknown;             // 0x08
};

// 0x2c bytes: two sixteen-byte hooked pointers at +0x00 and +0x10 (the
// pointer, then its notifier) and an LTLink at +0x20.
class CVideoBindingK
{
public:
	CVideoBindingK(BinkVideoInstK* pInst);             // 0x0048a5f0
	~CVideoBindingK();                                 // 0x0048a620
	void SetPoly(CVideoPolyK* pPoly);                  // 0x0048a640
	void SetOldTexture(SharedTextureK* pTexture);      // 0x0048a680

public:
	CVideoPolyK*    m_pPoly;                 // 0x00
	CVideoHookK     m_PolyHook;              // 0x04
	SharedTextureK* m_pOldTexture;           // 0x10
	CVideoHookK     m_TextureHook;           // 0x14
	LTLinkK         m_Link;                  // 0x20
};

// The abstract base behind vtable 0x004a147c (lt2new3.cpp's
// CUnknown4A147C).  Ten slots: an inline empty destructor, one non-pure
// body that is a bare `ret` at 0x00491b40, and eight _purecall.  Its size
// is 0x18, which is where BinkVideoInstK's own first field sits.
class VideoInstK
{
public:
	VideoInstK() { m_Bindings.m_nElements = 0; m_Bindings.m_pHead = LTNULL; }
	virtual ~VideoInstK() {}                                   // slot 0
	virtual void     Release() {}                              // slot 1
	virtual void     Restore() = 0;                            // slot 2
	virtual void     FreeSurfaces() = 0;                       // slot 3
	virtual LTRESULT UpdateScreen() = 0;                       // slot 4
	virtual LTRESULT Update() = 0;                             // slot 5
	virtual LTRESULT GetStatus() = 0;                          // slot 6
	virtual LTRESULT BindToSurface(CVideoPolyK* pPoly) = 0;    // slot 7
	virtual void     UnbindFromSurface(CVideoPolyK* pPoly) = 0;  // slot 8
	virtual void     OnTextureFreed(SharedTextureK* pTexture) = 0;  // slot 9

public:
	LTLinkK  m_Link;                         // 0x04  the manager's list
	LTListK  m_Bindings;                     // 0x10 count, 0x14 head
};

class BinkVideoMgrK;

// 0x68 bytes -- the size 0x00401a50 hands operator new.
class BinkVideoInstK : public VideoInstK
{
public:
	BinkVideoInstK(BinkVideoMgrK* pMgr);                       // 0x00401b90
	virtual ~BinkVideoInstK();                                 // 0x00401c40

	virtual void     Release();                                // 0x0048b390
	virtual void     Restore();                                // 0x00402120
	virtual LTRESULT UpdateScreen();                           // 0x004021a0
	virtual LTRESULT Update();                                 // 0x004021c0
	virtual LTRESULT GetStatus();                              // 0x004021e0
	virtual void     UnbindFromSurface(CVideoPolyK* pPoly);    // 0x0048b1b0
	virtual void     OnTextureFreed(SharedTextureK* pTexture); // 0x0048b230

	void     Term();                                           // 0x00401c70
	LTRESULT Init(char* pFilename, uint32 nFlags, uint32 nMode);  // 0x00401d30
	LTRESULT InitScreen();                                     // 0x00401dd0
	LTRESULT InitTexture();                                    // 0x00402040
	virtual void     FreeSurfaces();                           // 0x00402150
	virtual LTRESULT BindToSurface(CVideoPolyK* pPoly);        // 0x00402200
	LTBOOL   IsDone();                                         // 0x004022c0
	LTRESULT UpdateOnScreen();                                 // 0x004022e0
	LTRESULT UpdateTextureVideo();                             // 0x00402540

public:
	BINKK*                m_pBink;           // 0x18
	BinkVideoMgrK*        m_pMgr;            // 0x1c
	IDirectDrawSurfaceK*  m_pSurface;        // 0x20
	TextureDataK*         m_pTextureData;    // 0x24
	SharedTextureK        m_Texture;         // 0x28 .. 0x5b
	uint32                m_nFlags;          // 0x5c
	uint32                m_nMode;           // 0x60
	uint32                m_nSurfaceType;    // 0x64
};

// Vtable 0x004a1440, five slots: StartOnScreenVideo (0x00401b50),
// StartTextureVideo (0x00401b70), then three bodies shared with the Smack
// manager by the linker's identical-COMDAT folding.
class BinkVideoMgrK
{
public:
	BinkVideoMgrK(void* pClientMgr);                           // 0x004017e0
	~BinkVideoMgrK();                                          // 0x00401820
	virtual LTRESULT StartOnScreenVideo(char* pFilename, uint32 nFlags,
	                                    BinkVideoInstK*& hVideo);   // 0x00401b50
	virtual LTRESULT StartTextureVideo(char* pFilename, uint32 nFlags,
	                                   BinkVideoInstK*& hVideo);    // 0x00401b70
	virtual void     UpdateVideos();                           // 0x0048a6b0
	virtual void     OnRenderInit();                           // 0x0048a710
	virtual void     OnRenderTerm();                           // 0x0048a740

	LTRESULT Init();                                           // 0x00401850
	LTRESULT StartVideo(char* pFilename, uint32 nFlags,
	                    BinkVideoInstK*& hVideo, uint32 bTexture);  // 0x00401a50

public:
	LTListInitK            m_Videos;                 // 0x04 count, 0x08 head
	void*                  m_pClientMgr;             // 0x0c
	uint32                 m_Unknown10;              // 0x10
	TBinkSetSoundSystemK   m_pBinkSetSoundSystem;    // 0x14
	TBinkOpenMilesK        m_pBinkOpenMiles;         // 0x18
	TBinkOpenDirectSoundK  m_pBinkOpenDirectSound;   // 0x1c
	TBinkSetSoundTrackK    m_pBinkSetSoundTrack;     // 0x20
	TBinkOpenK             m_pBinkOpen;              // 0x24
	TBinkCloseK            m_pBinkClose;             // 0x28
	TBinkWaitK             m_pBinkWait;              // 0x2c
	TBinkCopyToBufferK     m_pBinkCopyToBuffer;      // 0x30
	TBinkDoFrameK          m_pBinkDoFrame;           // 0x34
	TBinkNextFrameK        m_pBinkNextFrame;         // 0x38
	TBinkIsSoftwareCursorK m_pBinkIsSoftwareCursor;  // 0x3c
	TBinkCheckCursorK      m_pBinkCheckCursor;       // 0x40
	TBinkRestoreCursorK    m_pBinkRestoreCursor;     // 0x44
	HINSTANCE              m_hBinkDLL;               // 0x48
	uint32                 m_pad_4c;                 // 0x4c
	uint32                 m_bSoftwareCursor;        // 0x50
};

// The DLL-host record 0x00401770 reads: the loaded module's HINSTANCE is
// its first dword (0x00401710, the sibling that shares the error literal,
// passes the same (%esi) to GetProcAddress as the module handle).
class CBinaryModuleK
{
public:
	HINSTANCE m_hInst;                       // 0x00
};


// ======================================================================= //
//  1.  0x00401770, 74 bytes -- bm_GetInstanceHandle
//
//  WHAT PINS IT
//    * `mov 0x4(%esp),%eax` with NO prologue push before it -> __cdecl,
//      and the slot read is argument one.  The final `ret` cleans nothing.
//    * `test %eax,%eax; jne 0x004017af` with the report block FALLING
//      THROUGH -> a leading `if (!pModule)` guard, the error inline, the
//      body hosted after it.
//    * `mov 0x8(%esp),%edx` is read only on the success path, so argument
//      two is touched only there: it is the out pointer, not a second
//      thing to validate.
//    * `mov (%eax),%ecx; mov %ecx,(%edx)` -- one dword out of the module
//      record's offset 0 into *pOut, and `xor %eax,%eax` is the only exit
//      value on that path: `return LT_OK;`.
//    * the name literal is 0x004a7148, "bm_SetInstanceHandle", which is
//      also what the SETTER at 0x00401710 pushes.  The two share the string
//      because Monolith copied the check and did not retype the name; this
//      file reproduces the literal, not the intent.
// ======================================================================= //

LTRESULT bm_GetInstanceHandleK(CBinaryModuleK* pModule, HINSTANCE* phInst)
{
	if (!pModule)
		K_RETURN_ERROR(1, "bm_SetInstanceHandle", LT_INVALIDPARAMS);

	*phInst = pModule->m_hInst;

	return LT_OK;
}


// ======================================================================= //
//  1a. 0x004017e0, 59 bytes -- BinkVideoMgr::BinkVideoMgr
//
//  `ret $0x4`, returns `this`: the constructor, one argument.  The list
//  (+0x04/+0x08) and the client-manager pointer (+0x0c) are stored BEFORE
//  the vptr, so they are the initialiser list; the body then clears the
//  ten entry points +0x14..+0x38 and the module handle +0x48.  The three
//  cursor entries (+0x3c..+0x44), +0x10, +0x4c and +0x50 are left alone.
// ======================================================================= //

BinkVideoMgrK::BinkVideoMgrK(void* pClientMgr)
	: m_pClientMgr(pClientMgr)
{
	m_pBinkSetSoundSystem = LTNULL;
	m_pBinkOpenMiles = LTNULL;
	m_pBinkOpenDirectSound = LTNULL;
	m_pBinkSetSoundTrack = LTNULL;
	m_pBinkOpen = LTNULL;
	m_pBinkClose = LTNULL;
	m_pBinkWait = LTNULL;
	m_pBinkCopyToBuffer = LTNULL;
	m_pBinkDoFrame = LTNULL;
	m_pBinkNextFrame = LTNULL;
	m_hBinkDLL = LTNULL;
}


// ======================================================================= //
//  1b. 0x00401820, 46 bytes -- BinkVideoMgr::~BinkVideoMgr
//
//  NOT virtual: the factory 0x0048a770 calls it directly and then operator
//  delete (0x0048a7c2, 0x0048a819), and the table 0x004a1440 has no
//  destructor slot.  The vptr store is the class's own table and nothing
//  else, so no base with a destructor.  lt2new4.cpp's block 12 also carries
//  this body as CUnknown401820 (a stand-in written before this TU existed).
//    * the clear of +0x48 is inside the `if`; +0x08 then +0x04 after it.
//    * all three clears are `movl $0` immediates.  Two plain assignments
//      make /O2 hoist a zero into %edi (and `cmp %edi` the handle); the
//      list reset as an inline LTListK::Init() is what the bytes have.
// ======================================================================= //

BinkVideoMgrK::~BinkVideoMgrK()
{
	if (m_hBinkDLL)
	{
		FreeLibrary(m_hBinkDLL);
		m_hBinkDLL = LTNULL;
	}

	m_Videos.Init();
}


// ======================================================================= //
//  2.  0x00401850 .. 0x00401a43, 500 bytes -- BinkVideoMgr::Init
//
//  THE ENUMERATION'S DEFECT.  The enumeration carried this as two rows,
//  0x00401850 (87) and 0x004018a7 (413).  0x004018a7 is NOT a function: it
//  is the `push %edi` that opens the SUCCESS arm of the `jne` at 0x00401863
//  -- the failure arm (the "Unable to find Binkw32.dll" report) is the
//  fall-through and ends at 0x004018a6 with `mov $0x16,%eax; pop %esi;
//  ret`, so the next byte looks like a fresh prologue.  It is reached only
//  by that one intra-function branch; the 413-byte
//  row has three `ret`s and no out-of-range branch target, which is the
//  signature of a tail rather than a function.  True extent:
//  0x00401850 .. 0x00401a43 inclusive, 500 bytes, one function.
//
//  WHAT PINS THE REST
//    * `push %esi; mov %ecx,%esi` and a bare `ret` -> __thiscall, no
//      arguments, LTRESULT.
//    * the FIRST GetProcAddress uses the %eax LoadLibrary left behind while
//      every later one re-reads 0x48(%esi): the store to m_hBinkDLL and the
//      first lookup are adjacent with no call between them, and the
//      thirteen lookups are thirteen separate statements.
//    * `mov 0x4a10e8,%edi` hoists the import thunk into a register and
//      calls `*%edi` thirteen times -- <windows.h>'s __declspec(dllimport)
//      GetProcAddress CSE'd, not a cached function pointer of ours.
//    * the twelve validity tests all `je` ONE block that sits AFTER both
//      success returns, and the value tested first is +0x14, not +0x18:
//      the order below is the source's, not the assignment order.  +0x44
//      (BinkRestoreCursor) is NOT tested.
//    * `call 0x0040f7f0` appears TWICE around one `if`, once for the test
//      and once for the argument.  ci_GetSoundMgr() is re-called rather
//      than cached, which is the shipped engine's habit wherever a call
//      sits between two uses of the same expression.  The second call's
//      driver is NAMED (hDig) before the pushes: written inline as the
//      argument, /O2 picks ecx/edx for the whole sound arm and the error
//      tail (10 insns off); the named local gives shipped's eax/ecx.
//    * the sound call cleans nothing after `call *0x14(%esi)`, so the
//      pointer is __stdcall -- which "_BinkSetSoundSystem@8" already says.
// ======================================================================= //

LTRESULT BinkVideoMgrK::Init()
{
	m_hBinkDLL = LoadLibrary("Binkw32.dll");
	if (!m_hBinkDLL)
	{
		con_ConsolePrintK("Unable to find Binkw32.dll, video playback disabled.");
		K_RETURN_ERROR(1, "BinkVideoMgr::Init", LT_MISSINGFILE);
	}

	m_pBinkSetSoundSystem   = (TBinkSetSoundSystemK)  GetProcAddress(m_hBinkDLL, "_BinkSetSoundSystem@8");
	m_pBinkOpenMiles        = (TBinkOpenMilesK)       GetProcAddress(m_hBinkDLL, "_BinkOpenMiles@4");
	m_pBinkOpenDirectSound  = (TBinkOpenDirectSoundK) GetProcAddress(m_hBinkDLL, "_BinkOpenDirectSound@4");
	m_pBinkSetSoundTrack    = (TBinkSetSoundTrackK)   GetProcAddress(m_hBinkDLL, "_BinkSetSoundTrack@4");
	m_pBinkOpen             = (TBinkOpenK)            GetProcAddress(m_hBinkDLL, "_BinkOpen@8");
	m_pBinkClose            = (TBinkCloseK)           GetProcAddress(m_hBinkDLL, "_BinkClose@4");
	m_pBinkWait             = (TBinkWaitK)            GetProcAddress(m_hBinkDLL, "_BinkWait@4");
	m_pBinkCopyToBuffer     = (TBinkCopyToBufferK)    GetProcAddress(m_hBinkDLL, "_BinkCopyToBuffer@28");
	m_pBinkDoFrame          = (TBinkDoFrameK)         GetProcAddress(m_hBinkDLL, "_BinkDoFrame@4");
	m_pBinkNextFrame        = (TBinkNextFrameK)       GetProcAddress(m_hBinkDLL, "_BinkNextFrame@4");
	m_pBinkIsSoftwareCursor = (TBinkIsSoftwareCursorK)GetProcAddress(m_hBinkDLL, "_BinkIsSoftwareCursor@8");
	m_pBinkCheckCursor      = (TBinkCheckCursorK)     GetProcAddress(m_hBinkDLL, "_BinkCheckCursor@20");
	m_pBinkRestoreCursor    = (TBinkRestoreCursorK)   GetProcAddress(m_hBinkDLL, "_BinkRestoreCursor@4");

	if (m_pBinkSetSoundSystem && m_pBinkOpen && m_pBinkClose && m_pBinkWait &&
	    m_pBinkCopyToBuffer && m_pBinkDoFrame && m_pBinkNextFrame &&
	    m_pBinkOpenMiles && m_pBinkOpenDirectSound && m_pBinkSetSoundTrack &&
	    m_pBinkIsSoftwareCursor && m_pBinkCheckCursor)
	{
		if (ci_GetSoundMgrK()->m_pSoundSys)
		{
			void* hDig = ci_GetSoundMgrK()->m_hDigDriver;
			m_pBinkSetSoundSystem(m_pBinkOpenMiles, hDig);
		}
		else
			m_pBinkSetSoundSystem(m_pBinkOpenDirectSound, LTNULL);

		return LT_OK;
	}

	con_ConsolePrintK("Invalid Binkw32.dll, video playback disabled.");

	FreeLibrary(m_hBinkDLL);
	m_hBinkDLL = LTNULL;

	K_RETURN_ERROR(1, "BinkVideoMgr::Init", LT_NOTINITIALIZED);
}


// ======================================================================= //
//  3.  0x00401a50, 256 bytes -- BinkVideoMgr::StartVideo
//
//  WHAT PINS THE SIGNATURE.  `ret $0x10` with `this` out of %ecx ->
//  __thiscall, four stack arguments.  Its two callers are vtable slots 0
//  and 1 of 0x004a1440 -- 0x00401b50 forwards three arguments plus a
//  literal 0 and 0x00401b70 the same three plus a literal 1, both with
//  `ret $0xc`.  That is iltvideomgr.h's StartOnScreenVideo /
//  StartTextureVideo pair, so arguments one to three are (pFilename,
//  flags, HVIDEO&) and the fourth is the on-screen/texture selector.
//
//  WHAT PINS THE BODY
//    * `movl $0x0,(%eax)` on the third argument before anything else ->
//      `hVideo = LTNULL;` is the first statement.
//    * `push $0x68; call 0x004290b0; add $4; test; je; mov %eax,%ecx; call
//      0x00401b90` is VC6's `new BinkVideoInstK(this)` -- the object is
//      0x68 bytes, which is exactly what the layout above adds up to.
//    * the LT_OUTOFMEMORY block falls THROUGH the `jne 0x00401aba` -> the
//      null check is an inline early return.
//    * `call *0x4(%edx)` on failure is slot 1, not slot 0: the cleanup is
//      Release(), not a deleting destructor.  Its body, 0x0048b390, walks
//      `m_pMgr->m_Videos` for the link whose m_pData is `this` -- shared
//      with the Smack instance by COMDAT folding, which is why one address
//      appears in both tables.
//    * `test %ebp,%ebp; jne <InitTexture>` with InitScreen as the
//      FALL-THROUGH -> the source tests the NEGATIVE, `if (!bTexture)`.
//    * `mov %eax,%ebx` sits at the join, so the two arms assign one local.
//    * the list insert reloads `pAfter->m_pNext` at 0x00401b1f after
//      storing through it at 0x00401b1d: the expression is written twice.
// ======================================================================= //

LTRESULT BinkVideoMgrK::StartVideo(char* pFilename, uint32 nFlags,
                                   BinkVideoInstK*& hVideo, uint32 bTexture)
{
	BinkVideoInstK* pInst;
	LTRESULT        dResult;

	hVideo = LTNULL;

	pInst = new BinkVideoInstK(this);
	if (!pInst)
		K_RETURN_ERROR(1, "BinkVideoMgr", LT_OUTOFMEMORY);

	dResult = pInst->Init(pFilename, nFlags, bTexture);
	if (dResult != LT_OK)
	{
		pInst->Release();
		return dResult;
	}

	if (!bTexture)
		dResult = pInst->InitScreen();
	else
		dResult = pInst->InitTexture();

	if (dResult != LT_OK)
	{
		pInst->Release();
		return dResult;
	}

	k_AddToList(&m_Videos, &pInst->m_Link, pInst);

	hVideo = pInst;

	return LT_OK;
}


// 0x00401b50 / 0x00401b70, 25 bytes each -- vtable 0x004a1440 slots 0 and
// 1. Three arguments forwarded plus the selector (push $0x0 / push $0x1),
// `this` riding through %ecx untouched. smackvideo.cpp's 0x0048ab60 /
// 0x0048ab80 are the same two bodies over the Smack manager, and
// sharedcommon.cpp's GetAttachedModel{Node,Socket}Transform (0x00434c90 /
// 0x00434cb0) the same shape again: all six must be written for the
// shape-only oracle to hand each its own address.
LTRESULT BinkVideoMgrK::StartOnScreenVideo(char* pFilename, uint32 nFlags,
                                           BinkVideoInstK*& hVideo)
{
	return StartVideo(pFilename, nFlags, hVideo, 0);
}

LTRESULT BinkVideoMgrK::StartTextureVideo(char* pFilename, uint32 nFlags,
                                          BinkVideoInstK*& hVideo)
{
	return StartVideo(pFilename, nFlags, hVideo, 1);
}


// ======================================================================= //
//  4.  0x00401b90, 103 bytes -- BinkVideoInst::BinkVideoInst
//
//  WHAT PINS IT
//    * `ret $0x4` and `mov %edi,%eax` on the way out -> __thiscall, one
//      argument, returning `this`: a constructor.
//    * TWO vptr constants at offset 0 -- 0x004a147c first at 0x00401b9a and
//      0x004a1454 last at 0x00401bda -- so single inheritance with the base
//      constructor INLINED, base table then derived.
//    * the twin at 0x0048aba0 (the Smack instance) is the same source
//      instruction for instruction, differing only in the sub-object's
//      offset (+0x68 for +0x28), one extra member vptr at +0x2c and one
//      extra zero at +0x28.  That is what says the 0x34-byte sub-object is
//      a class of its own and not inline fields of this one; it is
//      clienttexmgr.cpp's SharedTexture, field for field.
//    * `call 0x0045a350` with %ecx = this+0x28 is the sub-object's member
//      at its own +0x00 being default-constructed; `call 0x0045a370` with
//      %ecx = this+0x28 AND this+0x28 pushed is that member's one-argument
//      Init taking the sub-object itself.  Both are inside the inline
//      SharedTextureK constructor above.
//    * the derived stores are +0x1c (the argument) first, then +0x18,
//      +0x20, +0x24, +0x64 from one zeroed register.
// ======================================================================= //

BinkVideoInstK::BinkVideoInstK(BinkVideoMgrK* pMgr)
{
	m_pMgr = pMgr;
	m_pBink = LTNULL;
	m_pSurface = LTNULL;
	m_pTextureData = LTNULL;
	m_nSurfaceType = 0;
}


// ======================================================================= //
//  4a. 0x00401c40, 38 bytes -- BinkVideoInst::~BinkVideoInst
//
//  Derived vptr, a DIRECT call to Term (0x00401c70), the SharedTexture
//  member's destructor (0x0045a360 on this+0x28), then the inlined base
//  destructor: +0x14/+0x10 cleared from one zero, base vptr last.
// ======================================================================= //

BinkVideoInstK::~BinkVideoInstK()
{
	Term();
	m_Texture.m_Bank.Release();
	m_Bindings.Init();
}


// ======================================================================= //
//  5.  0x00401c70, 182 bytes -- BinkVideoInst::Term
//
//  WHAT PINS IT
//    * bare `ret`, `this` from %ecx, %eax never set on the way out ->
//      __thiscall, no arguments, void.  Its one caller is the destructor
//      at 0x00401c40, which calls it DIRECTLY (%ecx untouched), so it is a
//      non-virtual member and not a slot.
//    * `call *0x2c(%ecx)` on the manager is _BinkWait@4 and `call
//      *0x28(%ecx)` is _BinkClose@4, from the offsets Init stored them at.
//      Neither call is followed by a stack adjustment: __stdcall.
//    * the wait is a test at the top and a bottom-tested copy below it,
//      both re-reading m_pBink and m_pMgr -- VC6's rotation of a
//      `while (cond) ;` with an empty body, not a do/while (a do/while
//      would have no leading test).
//    * `mov (%esi),%eax; call *0xc(%eax)` is slot 3 through `this`.  VC6
//      does not devirtualise a call on `this`, so this really is the
//      virtual FreeSurfaces and not a direct call to 0x00402150.
//    * both list walks compute the next link with `sub; neg; sbb; and`,
//      which is VC6's branchless select: the source is a conditional
//      EXPRESSION, `(p->m_pNext == head) ? LTNULL : p->m_pNext`, and the
//      head is re-read inside the loop each time.
//    * `mov %ebx,%ecx; call 0x0048a620; push %ebx; call 0x004290c0` guarded
//      by a null test is `delete pBind` on a class whose destructor is NOT
//      virtual -- a virtual one would go through a slot.
//    * the two final stores are +0x14 then +0x10, DESCENDING, where the
//      constructor's pair is ascending: the source order really differs.
// ======================================================================= //

void BinkVideoInstK::Term()
{
	LTLinkK*        pLink;
	CVideoBindingK* pBind;

	if (m_pBink)
	{
		while (m_pMgr->m_pBinkWait(m_pBink))
			;

		m_pMgr->m_pBinkClose(m_pBink);
		m_pBink = LTNULL;
	}

	FreeSurfaces();

	if (m_pTextureData)
	{
		tex_FreeTextureDataK(m_pTextureData);
		m_pTextureData = LTNULL;
	}

	pLink = m_Bindings.m_pHead;
	while (pLink)
	{
		pBind = (CVideoBindingK*)pLink->m_pData;
		pLink = (pLink->m_pNext == m_Bindings.m_pHead) ? LTNULL : pLink->m_pNext;

		if (pBind->m_pPoly)
			pBind->m_pPoly->m_pTexture = pBind->m_pOldTexture;
	}

	pLink = m_Bindings.m_pHead;
	while (pLink)
	{
		pBind = (CVideoBindingK*)pLink->m_pData;
		pLink = (pLink->m_pNext == m_Bindings.m_pHead) ? LTNULL : pLink->m_pNext;

		delete pBind;
	}

	m_Bindings.m_pHead = LTNULL;
	m_Bindings.m_nElements = 0;
}


// ======================================================================= //
//  6.  0x00401d30, 156 bytes -- BinkVideoInst::Init
//
//  WHAT PINS IT
//    * `ret $0xc` -> __thiscall, three stack arguments.  0x00401a50 passes
//      (its argument 1, its argument 2, its argument 4), so they are
//      (pFilename, flags, mode).
//    * `xor %eax,%eax` before the test and `mov $0x4000,%eax` after the
//      call -> the open flags start at 0 and are ASSIGNED 0x4000 inside the
//      `if`, not or'd.  0x4000 is RAD's BINKSNDTRACK, which is exactly what
//      the _BinkSetSoundTrack@4 call beside it is for.
//    * `cmp $0x1,%edi`, not `test %edi,%edi` -> the source compares the
//      mode against 1, where 0x00401a50's own selector test is a plain
//      truth test.  Two different source lines.
//    * `test $0x2,%bl` / `$0x4` / `$0x8` on the LOW BYTE of the flags word
//      are three independent `if (nFlags & n)` statements, in that order.
//    * the report block is the FALL-THROUGH of `jne 0x00401dbe` -> an
//      inline early return.  Its third argument is 0x004a7324,
//      "LT_MISSINGFILE", the code spelled SYMBOLICALLY, and its fourth is a
//      real description rather than the lone space -- de_error.h's
//      RETURN_ERROR_INFO form, not RETURN_ERROR.  The code returned is
//      0x16 = 22 = LT_MISSINGFILE, which is what makes the two agree.
//    * the two trailing stores are +0x60 then +0x5c, descending.
// ======================================================================= //

LTRESULT BinkVideoInstK::Init(char* pFilename, uint32 nFlags, uint32 nMode)
{
	uint32 nOpenFlags = 0;

	if (nMode == 1)
	{
		m_pMgr->m_pBinkSetSoundTrack(0xFFFFFFFF);
		nOpenFlags = K_BINKSNDTRACK;
	}

	if (nFlags & 2)
		nOpenFlags |= K_BINKOPENFLAG_2;

	if (nFlags & 4)
		nOpenFlags |= K_BINKOPENFLAG_4;

	if (nFlags & 8)
		nOpenFlags |= K_BINKOPENFLAG_8;

	m_pBink = m_pMgr->m_pBinkOpen(pFilename, nOpenFlags);
	if (!m_pBink)
		K_RETURN_ERROR_INFO(1, "BinkVideoInst::Init", LT_MISSINGFILE,
		                    "BinkOpen failed");

	m_nMode = nMode;
	m_nFlags = nFlags;

	return LT_OK;
}


// ======================================================================= //
//  7.  0x00401dd0 .. 0x00402032, 611 bytes -- BinkVideoInst::InitScreen
//
//  The enumeration marks this row "contains an interior ret then a
//  prologue ... start missing at 0x00401ff2".  It is NOT two functions:
//  0x00401ff2 is the shared LT_NOTINITIALIZED report, the target of the
//  three `je`s at 0x00401de5, 0x00401df2 and 0x00401e0a, and the frame it
//  tears down (`add $0xe0,%esp`) is this function's.  611 bytes, one
//  function, and the last instruction ends exactly at the stated end.
//
//  THE FRAME.  `sub $0xe0,%esp` plus three pushes puts the locals at
//  +0x0c..+0xec off the settled %esp: a 0x7c-byte block at +0x0c whose
//  dwSize is stored 0x7c and a 0x64-byte block at +0x88 whose dwSize is
//  stored 0x64.  Those are DDSURFACEDESC2 (124) and DDBLTFX (100), and
//  0xc + 0x7c + 0x64 = 0xe0 exactly.  Both are cleared by an inline
//  `rep stos` of the whole structure before the size goes in, which is
//  memset with a constant length under /Oi.
//
//  WHAT PINS THE SHAPE
//    * the three guards `je` ONE block that sits after both success
//      returns.  Written as three early returns VC6 cross-jumps them into
//      the FIRST site, early in the function; the block only lands at the
//      end when the success path is the body of an `if` and the report is
//      the function's last statement.
//    * `call *0x4b4410` is slot +0xb8 of the render dispatch table --
//      clientworldinfo.cpp's slot map lists 0x00401dff and 0x00401e33
//      against exactly that slot.  One cdecl argument (`add $4` after the
//      second call), so the `push %eax` that survives it is the cursor
//      argument of the __stdcall _BinkIsSoftwareCursor@8 underneath, and
//      the hook is that call's FIRST argument.
//    * `push $0xfffffff4` into GetClassLongA is GCL_HCURSOR (-12).
//    * `call *0x30(%eax)` / `call *0x18(%eax)` on the object the hook hands
//      back are IDirectDraw::GetDisplayMode and ::CreateSurface; `call
//      *0x14(%eax)` on the surface is ::Blt.  All three take `this` as the
//      last push, i.e. real COM calls.
//    * the format cascade's dead loads at 0x00401ed8 and 0x00401f1c -- a
//      blue mask read and never compared -- are the THIRD comparand of each
//      first condition, kept because both of its outcomes assign the same
//      constant.  That is the only spelling that leaves the load behind;
//      two-term conditions would not have read it at all.
//    * the two LT_ERROR reports share everything from the description push
//      onward (0x00401f88): two RETURN_ERROR_INFO expansions cross-jumped.
//    * `movl $0x0,(%esi)` with %esi = this+0x20 inside the CreateSurface
//      failure is `m_pSurface = LTNULL;` written before the report.
//
//    * `call dsi_GetMainWindow` comes BEFORE `push $0xfffffff4`
//      (0x00401e1a/0x00401e1f) and m_pMgr is reloaded only after
//      GetClassLongA: the window and the cursor are each their own
//      statement, not nested arguments.
//    * the descriptor stores follow source order: dwWidth, dwHeight,
//      dwFlags, ddsCaps.dwCaps (0x00401e97..0x00401eb1).  Flags-first
//      schedules differently.
//    * `mov $0xa,%ecx; ...; mov %ecx,0x64(%esi)` at 0x00401ebf/0x00401ec9,
//      BEFORE the first 16-bit mask compare, is a default 565 assignment
//      at the top of the 16-bit arm; the 32-bit arm has none.
// ======================================================================= //

LTRESULT BinkVideoInstK::InitScreen()
{
	DDSURFACEDESC2K ddsd;
	DDBLTFXK        bltfx;
	IDirectDrawK*   pDD;
	HWND            hWnd;
	HCURSOR         hCursor;

	FreeSurfaces();

	if (m_pBink && g_RenderStructK.m_bInited &&
	    (pDD = (IDirectDrawK*)g_RenderStructK.GetHook("LPDIRECTDRAW")) != LTNULL)
	{
		m_pMgr->m_bSoftwareCursor = 0;

		hWnd = dsi_GetMainWindow();
		hCursor = (HCURSOR)GetClassLong(hWnd, GCL_HCURSOR);
		if (m_pMgr->m_pBinkIsSoftwareCursor(
		        g_RenderStructK.GetHook("BACKBUFFER"), hCursor))
		{
			m_pMgr->m_bSoftwareCursor = 1;
		}

		memset(&ddsd, 0, sizeof(ddsd));
		ddsd.dwSize = sizeof(ddsd);

		if (pDD->GetDisplayMode(&ddsd) != 0)
			K_RETURN_ERROR_INFO(1, "BinkVideoInst::InitScreen", LT_ERROR,
			                    "IDirectDraw::GetDisplayMode failed");

		ddsd.dwWidth = m_pBink->Width;
		ddsd.dwHeight = m_pBink->Height;
		ddsd.dwFlags = K_DDSD_CAPS | K_DDSD_HEIGHT | K_DDSD_WIDTH;
		ddsd.ddsCaps.dwCaps = K_DDSCAPS_VIDEOMEMORY | K_DDSCAPS_OFFSCREENPLAIN;

		if (ddsd.ddpfPixelFormat.dwRGBBitCount == 16)
		{
			m_nSurfaceType = K_BINKSURFACE565;

			if (ddsd.ddpfPixelFormat.dwRBitMask == 0xF800 &&
			    ddsd.ddpfPixelFormat.dwGBitMask == 0x07E0 &&
			    ddsd.ddpfPixelFormat.dwBBitMask == 0x001F)
			{
				m_nSurfaceType = K_BINKSURFACE565;
			}
			else if (ddsd.ddpfPixelFormat.dwRBitMask == 0x7C00 &&
			         ddsd.ddpfPixelFormat.dwGBitMask == 0x03E0 &&
			         ddsd.ddpfPixelFormat.dwBBitMask == 0x001F)
			{
				m_nSurfaceType = K_BINKSURFACE555;
			}
			else
			{
				m_nSurfaceType = K_BINKSURFACE565;
			}
		}
		else if (ddsd.ddpfPixelFormat.dwRGBBitCount == 32)
		{
			if (ddsd.ddpfPixelFormat.dwRBitMask == 0x00FF0000 &&
			    ddsd.ddpfPixelFormat.dwGBitMask == 0x0000FF00 &&
			    ddsd.ddpfPixelFormat.dwBBitMask == 0x000000FF)
			{
				m_nSurfaceType = K_BINKSURFACE32;
			}
			else if (ddsd.ddpfPixelFormat.dwRBitMask == 0x000000FF &&
			         ddsd.ddpfPixelFormat.dwGBitMask == 0x0000FF00 &&
			         ddsd.ddpfPixelFormat.dwBBitMask == 0x00FF0000)
			{
				m_nSurfaceType = K_BINKSURFACE32R;
			}
			else
			{
				m_nSurfaceType = K_BINKSURFACE32;
			}
		}
		else
		{
			m_nSurfaceType = K_BINKSURFACE565;
		}

		if (pDD->CreateSurface(&ddsd, &m_pSurface, LTNULL) != 0)
		{
			m_pSurface = LTNULL;
			K_RETURN_ERROR_INFO(1, "BinkVideoInst::InitScreen", LT_ERROR,
			                    "IDirectDraw::CreateSurface failed");
		}

		memset(&bltfx, 0, sizeof(bltfx));
		bltfx.dwSize = sizeof(bltfx);

		m_pSurface->Blt(LTNULL, LTNULL, LTNULL,
		                K_DDBLT_COLORFILL | K_DDBLT_DEPTHFILL | K_DDBLT_WAIT,
		                &bltfx);

		return LT_OK;
	}

	K_RETURN_ERROR(1, "BinkVideoInst::InitScreen", LT_NOTINITIALIZED);
}


// ======================================================================= //
//  8.  0x00402040, 212 bytes -- BinkVideoInst::InitTexture
//
//  WHAT PINS IT
//    * `push %esi; mov %ecx,%esi`, bare `ret` -> __thiscall, no arguments.
//    * BOTH report blocks are fall-throughs of a `jne` over them, unlike
//      InitScreen's: here the guards really are inline early returns and
//      the codes are reported NUMERICALLY ("73" at 0x004a7168, "67" at
//      0x004a730c), i.e. plain RETURN_ERROR.
//    * `add $0x1c,%esp` after 0x0042e8e0 -> seven cdecl arguments, and they
//      are pushed (0,0,0,1,Height,Width,3): lttexmod.cpp's GetTextureHandle
//      passes the same seven in the same shape.
//    * the three statements after it each RE-READ m_pTextureData
//      (`mov -0x4(%esi)` twice, with %esi biased to this+0x28): the field
//      is spelled three times in the source, not held in a local.
//    * `mov %edx,0x8(%esi)` with %esi = this+0x28 is
//      m_Texture.m_pEngineData, which is what makes the embedded 0x34-byte
//      object a SharedTexture -- and 0x004628e0, which takes a
//      SharedTexture*, is handed that same address.
// ======================================================================= //

LTRESULT BinkVideoInstK::InitTexture()
{
	FreeSurfaces();

	if (!m_pBink)
		K_RETURN_ERROR(1, "BinkVideoInst::InitTexture", LT_NOTINITIALIZED);

	m_pTextureData = tex_CreateTextureDataK(3, m_pBink->Width, m_pBink->Height,
	                                        1, 0, 0, 0);
	if (!m_pTextureData)
		K_RETURN_ERROR(1, "BinkVideoInst::InitTexture", LT_OUTOFMEMORY);

	m_pTextureData->m_FlagsC4 |= K_TDFLAG_C4_BIT2;
	m_pTextureData->m_Flags |= K_TDFLAG_TEXMOD;
	m_Texture.m_pEngineData = m_pTextureData;

	rif_BindTextureK(&m_Texture, 1);

	return LT_OK;
}


// ======================================================================= //
//  8b. 0x00402120, 46 bytes -- BinkVideoInst::Restore (slot 2)
//
//  The render device came back: rebuild the surface the mode needs, and
//  shut the video down if that fails.  +0x60 non-zero is a texture video.
//  Neither arm sets %eax after its call and the only caller (0x0048a730)
//  discards the answer: void.  Each arm has its own Term() call and its
//  own `pop %esi; ret`.
// ======================================================================= //

void BinkVideoInstK::Restore()
{
	if (!m_nMode)
	{
		if (InitScreen() != LT_OK)
			Term();
	}
	else
	{
		if (InitTexture() != LT_OK)
			Term();
	}
}


// ======================================================================= //
//  8c. 0x00402150, 67 bytes -- BinkVideoInst::FreeSurfaces (slot 3)
//
//  cmgsurf.cpp's SmackVideoInst::FreeSurfaces (0x0048b160) without the
//  second surface.  Release the DirectDraw surface through its COM slot 2,
//  then unbind and free the texture data; the two cdecl calls share one
//  `add $0x8,%esp`, and m_pTextureData is re-read after the first call.
// ======================================================================= //

void BinkVideoInstK::FreeSurfaces()
{
	if (m_pSurface)
	{
		m_pSurface->Release();
		m_pSurface = LTNULL;
	}

	if (m_pTextureData)
	{
		tex_FreeTextureDataForK(&m_Texture);

		m_Texture.m_pEngineData = LTNULL;

		tex_FreeTextureDataK(m_pTextureData);

		m_pTextureData = LTNULL;
	}
}


// ======================================================================= //
//  0x004021a0 / 0x004021c0 / 0x004021e0 -- slots 4, 5 and 6: UpdateScreen,
//  Update, GetStatus (24, 25 and 26 bytes).  Written first in ltnew1.cpp
//  over a same-offset view (BinkVideoInst); they are this class's, and the
//  view's DrawScreen / DrawFrame / IsDone are UpdateTextureVideo
//  (0x00402540), UpdateOnScreen (0x004022e0) and IsDone (0x004022c0).
//
//  WHAT PINS THEM (from ltnew1.cpp's notes)
//    * UpdateScreen: two tests jumping FORWARD to one shared `mov $0x2,%eax;
//      ret` and the body a tail `jmp` -- one `if` with `&&`, the failure
//      return last.
//    * Update: the same shape over +0x18 and +0x20; Smack's 0x0048b290 is
//      the same source with a different jump target.
//    * GetStatus: `call; neg %eax; sbb %eax,%eax; and $0x2,%eax` is VC6's
//      branchless select of 2-or-0 -- the conditional expression, not an
//      if/return pair.
// ======================================================================= //

LTRESULT BinkVideoInstK::UpdateScreen()
{
	if (m_pBink && m_nMode == 1)
		return UpdateTextureVideo();

	return LT_FINISHED;
}

LTRESULT BinkVideoInstK::Update()
{
	if (m_pBink && m_pSurface)
		return UpdateOnScreen();

	return LT_FINISHED;
}

LTRESULT BinkVideoInstK::GetStatus()
{
	if (m_pBink)
		return IsDone() ? LT_FINISHED : LT_OK;

	return LT_FINISHED;
}


// ======================================================================= //
//  9.  0x00402200, 192 bytes -- BinkVideoInst::BindToSurface
//
//  Slot 7 of vtable 0x004a1454; the enumeration's "start missing at
//  0x00402285" is again the shared report block, reached by the two `je`s
//  at 0x00402210 and 0x0040221e.  192 bytes, one function.
//
//  WHAT PINS IT
//    * `ret $0x4` -> __thiscall, one argument.
//    * `push $0x2c; call 0x004290b0; add $4; test; je; push %esi; mov
//      %eax,%ecx; call 0x0048a5f0` is `new CVideoBindingK(this)`: 0x2c
//      bytes, one constructor argument, and the two `je`s share a target
//      because /O2 folds the "operator new returned null" arm into the
//      later null check.
//    * `mov 0x30(%ebx),%eax` is read BEFORE `mov %ecx,0x30(%ebx)` is
//      written, so the old texture is captured first and the two calls are
//      two statements in that order.
//    * `lea 0x28(%esi),%ecx` stored into the poly's +0x30 is &m_Texture --
//      the same embedded SharedTexture InitTexture fills in.
//    * the insert is the same hand-rolled sequence as 0x00401b0e over the
//      list at +0x10/+0x14, link at the binding's +0x20.
//    * the report block sits AFTER the success return here (where
//      0x00401a50's equivalent is inline), so the allocation test is
//      written positively with the report as the last statement.
// ======================================================================= //

LTRESULT BinkVideoInstK::BindToSurface(CVideoPolyK* pPoly)
{
	CVideoBindingK* pBind;

	pBind = new CVideoBindingK(this);
	if (pBind)
	{
		pBind->SetPoly(pPoly);
		pBind->SetOldTexture(pPoly->m_pTexture);

		pPoly->m_pTexture = &m_Texture;

		k_AddToList(&m_Bindings, &pBind->m_Link, pBind);

		return LT_OK;
	}

	K_RETURN_ERROR(1, "BinkVideoInst::BindToSurface", LT_OUTOFMEMORY);
}


// ======================================================================= //
//  9a. 0x004022c0, 32 bytes -- BinkVideoInst::IsDone (name ours;
//  ltnew1.cpp's GetStatus calls it).  No BINK counts as done; otherwise
//  done on the last frame: `dec` of +0x08 (Frames) against +0x0c.
// ======================================================================= //

LTBOOL BinkVideoInstK::IsDone()
{
	if (m_pBink)
		return m_pBink->FrameNum == m_pBink->Frames - 1;

	return TRUE;
}


// ======================================================================= //
//  9c. 0x004022e0, 595 bytes -- BinkVideoInst::UpdateOnScreen (the name is
//  the literal at 0x004a740c; ltnew1.cpp's Update reaches it by a tail jmp
//  at 0x004021ce).  The on-screen twin of UpdateTextureVideo:
//    * the BACKBUFFER hook is fetched FIRST, before IsDone; its refusal is
//      an inline early return (the report falls through the `jne`) at
//      level 2 with the code reported symbolically.
//    * the frame is decoded only when _BinkWait@4 says 0: the surface is
//      locked (+0x64, flags 0x21 -- ddsd gets only dwSize, no memset),
//      _BinkCopyToBuffer@28 writes into it in m_nSurfaceType's format,
//      _BinkDoFrame@4, unlock (+0x80).  With a software cursor,
//      _BinkCheckCursor@20's answer lands in the manager's +0x4c first.
//    * m_nFlags bit 0 stretches the frame over the whole back buffer
//      (Blt, DDBLT_WAIT); otherwise it is centred and clipped to the
//      screen and BltFast'ed (flags 0x10).  The centring is `shr`: the
//      difference is unsigned (g_ScreenWidth - Width).
//    * _BinkNextFrame@4 only when the wait said 0 and it is not the last
//      frame.
// ======================================================================= //

// The screen size (0x004b4394 / 0x004b4398) is g_RenderStructK's
// m_Width / m_Height.

LTRESULT BinkVideoInstK::UpdateOnScreen()
{
	RECT                 srcRect, destRect;
	IDirectDrawSurfaceK* pBackBuffer;
	int                  nWait;
	DDSURFACEDESC2K      ddsd;

	pBackBuffer = (IDirectDrawSurfaceK*)g_RenderStructK.GetHook("BACKBUFFER");
	if (!pBackBuffer)
		K_RETURN_ERROR_INFO(2, "BinkVideoInst::UpdateOnScreen", LT_NOTINITIALIZED,
		                    "GetHook(BACKBUFFER) failed");

	if (IsDone())
		return LT_OK;

	nWait = m_pMgr->m_pBinkWait(m_pBink);
	if (!nWait)
	{
		ddsd.dwSize = sizeof(ddsd);

		if (m_pMgr->m_bSoftwareCursor)
			m_pMgr->m_pad_4c = m_pMgr->m_pBinkCheckCursor(dsi_GetMainWindow(), 0, 0,
			                                              m_pBink->Width, m_pBink->Height);

		if (m_pSurface->Lock(LTNULL, &ddsd, K_DDLOCK_WAIT | K_DDLOCK_WRITEONLY, LTNULL) == 0)
		{
			m_pMgr->m_pBinkCopyToBuffer(m_pBink, ddsd.lpSurface, ddsd.lPitch,
			                            m_pBink->Height, 0, 0, m_nSurfaceType);
			m_pMgr->m_pBinkDoFrame(m_pBink);
			m_pSurface->Unlock(LTNULL);
		}
	}

	if (m_nFlags & 1)
	{
		srcRect.left = srcRect.top = 0;
		srcRect.right = m_pBink->Width;
		srcRect.bottom = m_pBink->Height;

		destRect.left = destRect.top = 0;
		destRect.right = g_RenderStructK.m_Width;
		destRect.bottom = g_RenderStructK.m_Height;

		// M-P1-27: the movie keeps its shape -- the largest rectangle of
		// its proportions that fits the screen, centred. Retail stretched
		// it over the whole screen, so on a wide one the 4:3 intro movies
		// came out wide.
		if (m_pBink->Width && m_pBink->Height)
		{
			int nW = g_RenderStructK.m_Width;
			int nH = g_RenderStructK.m_Height;

			if ((__int64)nW * m_pBink->Height > (__int64)nH * m_pBink->Width)
			{
				int nFit = (int)((__int64)nH * m_pBink->Width / m_pBink->Height);
				destRect.left = (nW - nFit) / 2;
				destRect.right = destRect.left + nFit;
			}
			else
			{
				int nFit = (int)((__int64)nW * m_pBink->Height / m_pBink->Width);
				destRect.top = (nH - nFit) / 2;
				destRect.bottom = destRect.top + nFit;
			}

			// The margins black on every frame: nothing else draws there
			// while a movie plays, so a skipped movie of another shape
			// stayed visible beside the next one.
			if (destRect.left > 0 || destRect.top > 0)
			{
				DDBLTFXK bltfx;
				RECT     bars[2];

				memset(&bltfx, 0, sizeof(bltfx));
				bltfx.dwSize = sizeof(bltfx);       // fill colour 0: black
				if (destRect.left > 0)
				{
					SetRect(&bars[0], 0, 0, destRect.left, nH);
					SetRect(&bars[1], destRect.right, 0, nW, nH);
				}
				else
				{
					SetRect(&bars[0], 0, 0, nW, destRect.top);
					SetRect(&bars[1], 0, destRect.bottom, nW, nH);
				}
				pBackBuffer->Blt(&bars[0], LTNULL, LTNULL, K_DDBLT_COLORFILL | K_DDBLT_WAIT, &bltfx);
				pBackBuffer->Blt(&bars[1], LTNULL, LTNULL, K_DDBLT_COLORFILL | K_DDBLT_WAIT, &bltfx);
			}
		}

		pBackBuffer->Blt(&destRect, m_pSurface, &srcRect, K_DDBLT_WAIT, LTNULL);
	}
	else
	{
		srcRect.left = srcRect.top = 0;
		srcRect.right = m_pBink->Width;
		srcRect.bottom = m_pBink->Height;

		destRect.left = (g_RenderStructK.m_Width - m_pBink->Width) >> 1;
		destRect.top = (g_RenderStructK.m_Height - m_pBink->Height) >> 1;

		if (destRect.left < 0)
		{
			srcRect.left -= destRect.left;
			srcRect.right -= destRect.left;
			destRect.left = 0;
		}

		if (destRect.top < 0)
		{
			srcRect.top -= destRect.top;
			srcRect.bottom -= destRect.top;
			destRect.top = 0;
		}

		destRect.right = destRect.left + (srcRect.right - srcRect.left);
		if (destRect.right > g_RenderStructK.m_Width)
		{
			srcRect.right += g_RenderStructK.m_Width - destRect.right;
			destRect.right = g_RenderStructK.m_Width;
		}

		destRect.bottom = destRect.top + (srcRect.bottom - srcRect.top);
		if (destRect.bottom > g_RenderStructK.m_Height)
		{
			srcRect.bottom += g_RenderStructK.m_Height - destRect.bottom;
			destRect.bottom = g_RenderStructK.m_Height;
		}

		pBackBuffer->BltFast(destRect.left, destRect.top, m_pSurface, &srcRect,
		                     K_DDBLTFAST_WAIT);
	}

	if (!nWait)
	{
		if (!IsDone())
			m_pMgr->m_pBinkNextFrame(m_pBink);
	}

	return LT_OK;
}


// ======================================================================= //
//  9b. 0x00402540, 218 bytes -- BinkVideoInst::UpdateTextureVideo (the
//  name is the literal at 0x004a745c; ltnew1.cpp's Update/GetStatus pair
//  reaches it as slot 5's texture arm).
//    * both guards jump to the report block hosted LAST (0x004025e1), so
//      they are one positive `if (m_pBink && m_pTextureData)` with the
//      LT_FINISHED report after it.
//    * _BinkWait@4 (mgr +0x2c) answering nonzero skips the frame.
//    * _BinkCopyToBuffer@28 gets the texture's +0xd4 pixels and +0xd8
//      pitch, the BINK's Height, 0, 0 and flags 10.
//    * after the frame, IsDone() again: on the last frame the manager's
//      +0x44 entry is called with +0x4c when +0x50 is set (the fields
//      BinkVideoMgrK names RestoreCursor / m_pad_4c / m_bSoftwareCursor),
//      otherwise _BinkNextFrame@4 (+0x38) advances.
// ======================================================================= //

LTRESULT BinkVideoInstK::UpdateTextureVideo()
{
	if (m_pBink && m_pTextureData)
	{
		if (IsDone())
			return LT_OK;

		if (!m_pMgr->m_pBinkWait(m_pBink))
		{
			m_pMgr->m_pBinkCopyToBuffer(m_pBink, m_pTextureData->m_pPixels,
				m_pTextureData->m_nPitch, m_pBink->Height, 0, 0, 10);
			m_pMgr->m_pBinkDoFrame(m_pBink);
			rif_BindTextureK(&m_Texture, TRUE);

			if (IsDone())
			{
				if (m_pMgr->m_bSoftwareCursor)
					m_pMgr->m_pBinkRestoreCursor(m_pMgr->m_pad_4c);
			}
			else
			{
				m_pMgr->m_pBinkNextFrame(m_pBink);
			}
		}

		return LT_OK;
	}

	K_RETURN_ERROR(1, "BinkVideoInst::UpdateTextureVideo", LT_FINISHED);
}


// ======================================================================= //
//  THE SHARED VIDEO-MANAGER TU (0x0048a5f0 ..)
//
//  HOME: not this file.  These bodies sit at 0x0048a5f0.., ahead of the
//  factory 0x0048a770, and the manager bodies are shared by the Bink table
//  0x004a1440 and the Smack table 0x004a3890 at one address -- the common
//  video-manager TU (LithTech's videomgr.cpp).  The repo has no such file,
//  so they are written here against this file's classes.
// ======================================================================= //

// 0x0048a5f0, 42 bytes -- CVideoBinding::CVideoBinding
//   * the two notifiers are filled first (+0x04/+0x08/+0x0c, then
//     +0x14/+0x18/+0x1c), the two pointers cleared last (+0x00, +0x10).
CVideoBindingK::CVideoBindingK(BinkVideoInstK* pInst)
{
	m_PolyHook.m_pDesc = &k_PolyHookDescK;
	m_PolyHook.m_pOwner = pInst;
	m_PolyHook.m_nUnknown = 0;
	m_TextureHook.m_pDesc = &k_TextureHookDescK;
	m_TextureHook.m_pOwner = pInst;
	m_TextureHook.m_nUnknown = 0;
	m_pPoly = LTNULL;
	m_pOldTexture = LTNULL;
}

// 0x0048a6b0, 89 bytes -- VideoMgr::UpdateVideos, manager slot 2 (name
// ours; the old `UpdateVideo(hVideo)` declaration was wrong -- bare `ret`,
// no arguments).  Walk the list, calling instance slot 4 (UpdateScreen)
// then slot 6 (GetStatus), counting the LT_OK answers; if the console
// variable at 0x004b3244 is set, print "%d videos, %d active" (0x004af4e8).
extern int g_CV_VideoDebug;                          // 0x004b3244  dsi.cpp

void BinkVideoMgrK::UpdateVideos()
{
	LTLinkK*        pLink;
	BinkVideoInstK* pInst;
	uint32          nActive;

	nActive = 0;

	pLink = m_Videos.m_pHead;
	while (pLink)
	{
		pInst = (BinkVideoInstK*)pLink->m_pData;
		pLink = (pLink->m_pNext == m_Videos.m_pHead) ? LTNULL : pLink->m_pNext;

		pInst->UpdateScreen();
		if (pInst->GetStatus() == LT_OK)
			++nActive;
	}

	if (g_CV_VideoDebug)
		con_ConsolePrintK("%d videos, %d active", m_Videos.m_nElements, nActive);
}

// 0x0048a710 / 0x0048a740, 42 bytes each -- VideoMgr::OnRenderInit /
// OnRenderTerm (names ours).  No arguments (bare `ret`); walk the video
// list and call instance slot 2 (Restore) / slot 3 (FreeSurfaces).  The
// next link is taken before the call, with the same branchless
// `(next == head) ? LTNULL : next` select Term uses.
void BinkVideoMgrK::OnRenderInit()
{
	LTLinkK*        pLink;
	BinkVideoInstK* pInst;

	pLink = m_Videos.m_pHead;
	while (pLink)
	{
		pInst = (BinkVideoInstK*)pLink->m_pData;
		pLink = (pLink->m_pNext == m_Videos.m_pHead) ? LTNULL : pLink->m_pNext;

		pInst->Restore();
	}
}

void BinkVideoMgrK::OnRenderTerm()
{
	LTLinkK*        pLink;
	BinkVideoInstK* pInst;

	pLink = m_Videos.m_pHead;
	while (pLink)
	{
		pInst = (BinkVideoInstK*)pLink->m_pData;
		pLink = (pLink->m_pNext == m_Videos.m_pHead) ? LTNULL : pLink->m_pNext;

		pInst->FreeSurfaces();
	}
}

// 0x0048b1b0, 123 bytes -- VideoInst slot 8, shared by the Bink and Smack
// instance tables (name ours).  `ret $0x4`, %eax never set: void, one
// argument.  Every binding whose poly (+0x00) is the argument is unlinked
// from m_Bindings and deleted (0x0048a620 then operator delete).
void BinkVideoInstK::UnbindFromSurface(CVideoPolyK* pPoly)
{
	LTLinkK*        pLink;
	CVideoBindingK* pBind;

	pLink = m_Bindings.m_pHead;
	while (pLink)
	{
		pBind = (CVideoBindingK*)pLink->m_pData;
		pLink = (pLink->m_pNext == m_Bindings.m_pHead) ? LTNULL : pLink->m_pNext;

		if (pBind->m_pPoly == pPoly)
		{
			k_RemoveFromList(&m_Bindings, &pBind->m_Link);
			delete pBind;
		}
	}
}

// 0x0048b230, 59 bytes -- VideoInst slot 9, shared by the Bink (0x004a1454)
// and Smack (0x004a38a4) instance tables (name ours).  A texture is going
// away: every binding that remembered it as the poly's old texture forgets
// it (0x0048a680, SetOldTexture(LTNULL)).  `ret $0x4`, %eax never set:
// void, one argument.  Home: the Smack/common video TU, like the rest here.
void BinkVideoInstK::OnTextureFreed(SharedTextureK* pTexture)
{
	LTLinkK*        pLink;
	CVideoBindingK* pBind;

	pLink = m_Bindings.m_pHead;
	while (pLink)
	{
		pBind = (CVideoBindingK*)pLink->m_pData;
		pLink = (pLink->m_pNext == m_Bindings.m_pHead) ? LTNULL : pLink->m_pNext;

		if (pBind->m_pOldTexture == pTexture)
			pBind->SetOldTexture(LTNULL);
	}
}


// ======================================================================= //
//  WHAT THIS FILE DELIBERATELY DOES NOT CONTAIN
//
//  The cluster's nine rows are the nine bodies above.  The other twelve
//  functions in 0x00401770..0x004022c0 belong to the same TU but were not
//  among its rows; three of them -- 0x004021a0, 0x004021c0 and
//  0x004021e0, UpdateScreen / Update / GetStatus -- were written first in
//  src/engine/lithtech/ltnew1.cpp and now live above, as this class's
//  (the vtable's slots are what the link needs).  0x00401820 was carried by src/engine/lithtech/
//  lt2new4.cpp as CUnknown401820::~CUnknown401820 (a near there); it is
//  now written here as BinkVideoMgrK's destructor, where it belongs.
//
//  UNDECODED, and marked rather than invented:
//    * BinkVideoMgrK::m_Unknown10 (+0x10) and m_pad_4c (+0x4c) are never
//      touched by anything in this cluster; the constructor at 0x004017e0
//      does not clear either.  They are placeholders for the layout, not
//      claims.
//    * SharedTextureK::m_Link's m_pPrev/m_pNext (+0x10/+0x14) and
//      m_Unknown2c are not written by the inline constructor above; the
//      shipped bytes leave them alone too.
//    * the three Bink open-flag bits 0x20000000, 0x00800000 and 0x04000000
//      are read off the immediates; RAD's names for them are not pinned
//      from this image, so they are spelled numerically.
//    * CVideoBindingK's two sixteen-byte hook records are modelled only by
//      the two fields this cluster reaches (+0x00 and +0x10); lt2new3.cpp
//      has the descriptors.
// ======================================================================= //


// ======================================================================= //
//  0x0048b390, 124 bytes -- VideoInst::Release, slot 1 (shared by the Bink
//  and Smack tables).  Find this instance in the manager's list (inlined,
//  index out through a pointer), unlink it if found, then `delete this`
//  through slot 0 with the deleting flag 1.  Home: the common video TU.
// ======================================================================= //

void BinkVideoInstK::Release()
{
	uint32 iIndex;

	k_FindInList(&m_pMgr->m_Videos, this, &iIndex);
	if (iIndex != (uint32)-1)
		k_RemoveFromList(&m_pMgr->m_Videos, &m_Link);

	delete this;
}
