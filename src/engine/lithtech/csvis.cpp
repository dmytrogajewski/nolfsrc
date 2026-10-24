// *********************************************************************** //
//
//  MODULE:   Q414000 (.CPP)   (file name ours)
//
//  PURPOSE:  lithtech.exe 0x004144e0 .. 0x00415ec0 -- the parts of the
//            CLIENT SHELL's world-load run that nothing under src/engine/
//            has reached yet: the vis-container world models DoLoadWorld
//            builds, the two object-create constructors that run emits, the
//            server-inherited-model check, and the loader thread's model
//            entry point, plus the client message helper's
//            WriteFormattedString, five per-frame update walks and the
//            sprite tracker update 0x00483d00 they call.
//
//  WHICH MODULE THIS IS, AND WHERE THE SEAM FALLS
//  ----------------------------------------------
//  The .data literal pool read in ADDRESS ORDER splits the cluster
//  (0x00414000..0x00418000) in exactly one place:
//
//      0x004a85b0 "LT_INVALIDWORLDFILE"                  0x004141d1
//      0x004a85c4 "CreateVisContainerObjects failed"     0x0041413c
//      0x004a85ec "Entering world %s"                    0x0041402e
//      0x004a8600 "CClientShell::DoLoadWorld"            0x004140cb
//      0x004a867c "textures\default_texture.dtx"         0x00414358
//      0x004a869c "Unable to find world texture %s"      0x00414338
//      0x004a86bc "Client inherited model %s from server!" 0x004149ce
//      0x004a86fc "CLoaderThread::LoadModel(%s)"         0x004156f8
//      0x004a871c "CLIENT-OBJECT"                        0x00415800  <-- last
//      ---------------------------------------------------------------------
//      0x004a8750 "Error: (point) physics recursed infinitely on a %s"
//                                                        0x00417d83  <-- first
//      0x004a8784 "Error: physics recursed infinitely on a %s"  0x0041810b
//      0x004a87b0 "World %s doesn't have PBlocks!"       0x00417eee
//
//  Everything above the rule is the client shell -- the same run
//  src/engine/lithtech/clientmgr2.cpp already documents from 0x0040e9d0.
//  Everything below it is src/engine/server/worldsim.cpp, which the lithtech
//  build ALREADY COMPILES (build/engine.sh's lithtech line ends with
//  ..\server\worldsim.cpp).  The seam is therefore between 0x00415800 and
//  0x00417d83, and matching the built worldsim.obj against the image puts it
//  exactly: that object's matched functions in the cluster run
//  0x004162e0 / 0x00416380 / 0x004163e0 / 0x00416440 / 0x004164a0 /
//  0x00416500 / 0x00416560 / 0x00416060 / 0x00416ba0 / 0x00416da0 /
//  0x00417c10 / 0x00417ce0 / 0x00418410, and the client shell's own matched
//  and near functions run 0x00414370 / 0x004144b0 / 0x004148d0 / 0x004157c0.
//  The two objects are INTERLEAVED in the image, so the seam is not a single
//  address -- which is why the enumerated rows below had to be attributed one
//  at a time.
//
//  ENUMERATED ROWS ALREADY WRITTEN ELSEWHERE  (evidence, not opinion)
//  ------------------------------------------------------------------
//  Five of the seven rows handed to this file belong to worldsim.cpp and are
//  already written there, as NEARS rather than matches; a scan that
//  drops a row only when its ADDRESS is mentioned under src/engine/
//  could not see them.  worldsim.cpp carries server.dll addresses
//  (0x1000xxxx) in its comments, never lithtech.exe's.  Sizes below are
//  from matching src/engine/lithtech/Release/worldsim.obj against the
//  retail lithtech.exe:
//
//    0x004152f0  621 bytes   ?w_IsPolyBlocking@@YAHPAVWorldPolyOwner@@@Z,
//                621 bytes in worldsim.obj -- EXACT size agreement, and the
//                shape agrees too: one cdecl argument, `pOwner->+0x28`'s
//                +0x38 bit 0 as the entry gate, a sphere-sphere rejection
//                against g_fBoxRadius, then a clip of the owner's vertex run
//                (+0x50 count, +0x54 stride-0x18 run) against the six
//                16-byte box planes at 0x004b27d4..0x004b2834.  The decoded
//                map's `bridged-callsite` row for this address is RIGHT.
//    0x004165c0  the row's 430 bytes CUT AN INSTRUCTION IN HALF
//                (0x0041676d ends at 0x00416770).  The real
//                extent is 0x004165c0..0x004169d9, 1,049 bytes with seven
//                bytes of lea-nop padding after it -- and worldsim.obj's
//                ?ws_ShiftBox@@YAHXZ is 1,055 bytes.
//    0x004169e0  436 bytes = ?Setup@CSimContext@@QAEXXZ, 435 bytes in
//                worldsim.obj; already a documented near.
//    0x00417da0  994 bytes = ?ws_MoveObject@@YAXPAVCMoveState@@PAVCBounceState@@@Z,
//                1,025 bytes in worldsim.obj.  It is the only function in
//                the image that raises BOTH "World %s doesn't have PBlocks!"
//                (0x00417eee) and "Error: physics recursed infinitely on a
//                %s" (0x0041810b), and worldsim.cpp's ws_MoveObject is the
//                only function in our source that raises both.
//
//  And two rows ARE NOT FUNCTIONS AT ALL.  Both were enumerated as `ptr`
//  rows -- an address found as a dword somewhere in the image -- and both
//  dwords are ACCIDENTS OF ASCII in the PE import name table:
//
//    0x00416f66  the dword at .rdata 0x004a5e7c.  The bytes around it are
//                "\x50\x01GetStartupInfoA\0\xca\0GetCom", so the dword read
//                at 0x004a5e7c is 'f','o','A',0 -- the tail of
//                "GetStartupInfoA" -- which is 0x00416f66 little-endian.
//    0x0041726f  the dword at .rdata 0x004a5190.  "\x9a\x01LoadCursorA\0
//                \x9e\x01LoadIc" -- 'o','r','A',0 = 0x0041726f.
//
//  Neither address is the target of any call in the image, and there are
//  branches LEAVING both rows (0x00417008 -> 0x00417437 and
//  0x0041708b -> 0x004172a7 out of the first; 0x00417428 -> 0x00417018 back
//  into it from the second).  Both are interior blocks of the function that
//  starts at the call target 0x00416de0 and runs to 0x004176bf -- 2,272
//  bytes against the 2,175 of worldsim.obj's
//  ?TestPoly@CSimContext@@QAEHPAVWorldPolyOwner@@PAVWorldPoly@@@Z.
//
//  So ONE of the seven rows was new work, and this file takes it plus the
//  four neighbouring client-shell rows still uncovered in the same run
//  (0x004147f0, 0x00414930, 0x00414a00, 0x004156d0).
//
//  EVERY symbol defined here carries a Q414 suffix so this TU cannot collide
//  with src/engine/lithtech/clientmgr2.cpp or clientmgr.h at link time.
//  ObjectCreateStruct and LTRotation are the SDK's own
//  (src/LT2/sdk/inc/ltbasedefs.h, ltrotation.h) and are NOT renamed: this file
//  cannot reproduce 0x004147f0 or the two inlined Clear() blocks in
//  0x004144e0 with a stand-in view, because it is the header's own field
//  order and its inline LTRotation::Init that the shipped bytes lay out.
//
//  Build /O2 /Gy (build/engine.sh's lithtech line, cl 8447).
//
// *********************************************************************** //

#include <string.h>
#include <stdarg.h>

#include "ltbasedefs.h"
#include "ltlink.h"
#include "..\..\modern\engine\p1_frametime.h"   // M-P1-23


// ======================================================================= //
//  WHAT THIS TU BORROWS
//
//  Stand-in declarations only.  Every offset is the displacement the shipped
//  code actually uses and the address it was read from is in the comment.
// ======================================================================= //

// One file the object creator has to resolve.  This is
// src/engine/lithtech/clientmgr.h's CClientFileRef restated -- and the
// restatement is load-bearing, because the three-store body its constructor
// compiles to IS the 14-byte function at 0x004147e0 that the array loop at
// 0x00414511 calls.  Two dwords and a WORD, field for field and width for
// width (0x004147e4 / 0x004147e6 / 0x004147e9).
class CClientFileRefQ414
{
public:
	CClientFileRefQ414() { m_nFileType = 0; m_pFilename = LTNULL; m_nLocalID = 0; }

	uint32 m_nFileType;                                        // 0x00
	char*  m_pFilename;                                        // 0x04
	uint16 m_nLocalID;                                         // 0x08
};

// What the object creator is handed: the object's own file, one reference
// per skin, and the create struct.  clientmgr.h's CClientCreateRequest, and
// the shipped constructor run at 0x004144fe..0x0041451c proves the shape --
// the object's file is inlined (three stores) and exactly
// MAX_MODEL_TEXTURES (2, from ltbasedefs.h) more are built by a loop over a
// 12-byte stride, which puts m_pStruct at +0x24 where 0x00414526 stores it.
class CClientCreateRequestQ414
{
public:
	CClientFileRefQ414  m_ObjectFile;                          // 0x00
	CClientFileRefQ414  m_SkinFiles[MAX_MODEL_TEXTURES];       // 0x0c
	ObjectCreateStruct* m_pStruct;                             // 0x24
};

// The client's world, as far as this run reaches it.  Offsets read off
// 0x00414550 (+0x04 taken as an address, so an inline char run), 0x00414553
// (+0x48), 0x0041455f (+0x140) and 0x0041475e (+0xe8).  What fixes +0x13c /
// +0x140 as a run of 0xc0-byte vis-container records and not something else
// is the name PARSER at 0x00424f52, which splits a "#$#%d %s" name back
// apart, bounds the index it recovers against this same +0x140, and then
// indexes +0x13c by `(i + i*2) << 6` -- 192 bytes a record.  (names ours)
class CBlockerArrayQ414
{
public:
	uint32 GetSize() const { return m_nElements; }

	void*  m_pVTable;                                          // +0x00
	void*  m_pArray;                                           // +0x04 (0x13c)
	uint32 m_nElements;                                        // +0x08 (0x140)
};

class CClientWorldQ414
{
public:
	uint8    m_pad_00[0x04];
	char     m_Name[0x48 - 0x04];                              // 0x04
	// Bit 3 selects the per-blocker branch and bits 4-5 the whole-world
	// branch; the shipped test is on the low byte of a dword load
	// (0x00414556 / 0x00414685), so the field is at least a dword wide.
	uint32   m_Flags;                                          // 0x48
	uint8    m_pad_4c[0xe8 - 0x4c];
	LTVector m_vPos;                                           // 0xe8
	uint8    m_pad_f4[0x138 - 0xf4];
	CBlockerArrayQ414 m_Blockers;                              // 0x138
};

// One element of the run at CClientMgr+0x18c.  Only the two pointers this
// run follows are modelled.  +0x0c's own +0x04 is handed straight to
// strncpy as a `char*` source (0x00414741 computes the address rather than
// loading through it), so it is an inline name and not a pointer to one --
// the same shape CClientFileRefQ414 would have if its name were inline, and
// deliberately NOT called that here because nothing proves they are the same
// class.  (names ours)
class CClientWorldRecQ414
{
public:
	uint8              m_pad_00[0x04];
	CClientWorldQ414*  m_pWorld;                               // 0x04
	uint8              m_pad_08[0x0c - 0x08];
	class CWorldFileNameQ414* m_pFile;                         // 0x0c
};

class CWorldFileNameQ414
{
public:
	uint8 m_pad_00[0x04];
	char  m_Name[1];                                           // 0x04
};

// The CClientMgr, as far as this run reaches it.  The array pair at
// +0x18c/+0x190 is the one src/engine/lithtech/clientmgr2.cpp models as
// CArray18C; this file agrees with it offset for offset and only adds the
// element type.  (Worth recording against that file: its
// cs_ReleaseArrayItems takes the array as a SECOND argument typed
// CClientShell* / CArray18C*, and DoLoadWorld's call site at 0x004140fa
// pushes `0x78(%esi)` for BOTH -- two separate loads of the same
// CClientMgr*, so both parameters are CClientMgr*.)
class CWorldRecArrayQ414
{
public:
	CClientWorldRecQ414*& operator[](const uint32 index) const { return Get(index); }
	uint32 GetSize() const { return m_nElements; }
	CClientWorldRecQ414*& Get(uint32 index) const { return m_pArray[index]; }

	void*                 m_pVTable;                           // +0x00
	CClientWorldRecQ414** m_pArray;                            // +0x04
	uint32                m_nElements;                         // +0x08
};

class CClientMgrQ414
{
public:
	uint8                 m_pad_000[0x188];
	CWorldRecArrayQ414    m_WorldRecs;                         // 0x188
	uint8                 m_pad_194[0x41c - 0x194];
	// The client's object list, walked by cs_DirtyAllModelsQ414 below.
	// 0x00414a10 takes the ADDRESS of +0x420 as the walk's end marker and
	// 0x00414a0a reads +0x424 as its first element: that is the list's
	// m_Head LTLink (m_pPrev, m_pNext, m_pData) at +0x420, so ltlink.h's
	// LTList -- m_nElements (4) then m_Head -- starts at +0x41c.  (An
	// earlier revision put the LTList itself at +0x420; the walk then read
	// +0x428 / +0x424, masked away by a mnemonic-only diff.)
	// clientmgr2.cpp's CClientMgrView pads over this address.  (name ours)
	LTList                m_ObjectList;                        // 0x41c
	uint8                 m_pad_42c[0x46c - 0x42c];
	// Two more LTLists in the same run, each walked by one of the per-frame
	// updates below: 0x00415c15 reads +0x474 as the first element and
	// 0x00415c1b takes +0x470 as the end marker (the particle systems);
	// 0x00415e64 / 0x00415e5e do the same with +0x484 / +0x480 (the
	// sprites).  Contiguous with m_MoveList at +0x48c.  (names ours)
	LTList                m_ParticleList;                      // 0x46c
	LTList                m_SpriteList;                        // 0x47c
	// The list cs_ApplyPendingMoves walks: 0x00415ec7 reads +0x494 as the
	// first element and 0x00415ecd takes +0x490 as the end marker, so it is
	// an LTList at +0x48c exactly as m_ObjectList above.  (name ours)
	LTList                m_MoveList;                          // 0x48c
	uint8                 m_pad_49c[0x618 - 0x49c];
	// A string-resource module record -- a POINTER to a record whose first
	// dword is the HINSTANCE (cltnew.cpp's 0x610/0x614 pair, one further
	// on).  The only reader in this file is the message helper's
	// WriteFormattedString (0x00415602).  (name ours)
	void*                 m_pStringModule618;                  // 0x618
	uint8                 m_pad_61c[0x1258 - 0x61c];
	// The frame time the per-frame walks scale by 1000.0f (0x00415bf6,
	// 0x00415e47, 0x00415f1c), and 0x00415ca8 pushes it whole as the float
	// argument of the particle update 0x0045ca70.  (name ours)
	float                 m_fFrameTime;                        // 0x1258
	uint8                 m_pad_125c[0x1264 - 0x125c];
	// The sprite-instance chain cs_UpdateSprites walks (0x00415b74), null-
	// terminated through +0x04.  (name ours)
	class CSpriteInstQ414* m_pSpriteInsts;                     // 0x1264
	uint8                 m_pad_1268[0x150c - 0x1268];
	// clientmgr.h's m_pConnection, whose +0x1c is m_fFrameTime.
	class CClientConnectionQ414* m_pConnection;                // 0x150c
	uint8                 m_pad_1510[0x1534 - 0x1510];
	// clientmgr.h's own name and offset; cmodelload.cpp uses the same pair.
	class CFileMgrQ414*   m_pFileMgr;                          // 0x1534
};

// clientmgr.h / clientmgr2.cpp's g_pClientMgr.  Read by both the inherit
// check (0x0041495c, 0x004149b6) and the model walk (0x00414a00).
extern CClientMgrQ414* g_pClientMgrQ414;                       // 0x004b17e0

// src/engine/lithtech/clientmgr2.cpp's CClientShell, cut down to the two
// things this file needs.  The manager at +0x78 is that file's
// m_pClientMgr, and GetClientMgr (0x00414b10) is its four-byte accessor.
class CClientShellQ414
{
public:
	uint8            m_pad_00[0x78];
	CClientMgrQ414*  m_pClientMgr;                             // 0x078

	CClientMgrQ414*  GetClientMgr();                           // 0x00414b10

	LTBOOL CreateVisContainerObjects();                        // 0x004144e0
};


// ----------------------------------------------------------------------- //
//  Helpers this run calls but does not define
// ----------------------------------------------------------------------- //

// 0x004221f0 -- build the engine's internal name for one of a world's
// vis-container blockers.  `sprintf(pOut, "%s%d %s", "#$#", nIndex,
// pszWorldName)` behind two guards: it refuses a buffer under 8 bytes and
// refuses a world name longer than nMaxLen-8.  Returns non-zero on success.
// What names it is its inverse at 0x00424f52, which takes a name apart with
// the same "#$#" prefix and a "%d %s" tail.  (name ours)
int sub_4221f0(const char* pszWorldName, uint32 nIndex,
               char* pOut, uint32 nMaxLen);                    // 0x004221f0

// src/engine/lithtech/clientmgr.h declares this one already, at this
// address and with this signature; it is restated because that header is
// shared with work in flight.  The last two arguments are the two integers
// the two branches below disagree about (0 and 0 from the per-blocker
// branch, 1 and 0 from the whole-world branch).
LTRESULT sub_411a70(CClientMgrQ414* pClientMgr, uint32 nServerID,
                    CClientCreateRequestQ414* pRequest, HLOCALOBJ* phObject,
                    int nUnknown1, int nUnknown2);             // 0x00411a70


// ======================================================================= //
//  CClientShell::CreateVisContainerObjects -- 0x004144e0
//
//  WHAT NAMES IT.  DoLoadWorld calls this at 0x00414112 and reports
//  "CreateVisContainerObjects failed" (0x004a85c4, pushed at 0x0041413c)
//  when it comes back zero.  That is the whole of the naming evidence and it
//  is enough: the string is the function's own name plus " failed".
//
//  WHAT IT DOES.  Every world the client has loaded gets client-side
//  WorldModel objects made for the parts of it the server treats as
//  geometry: either one object per blocker in the world's +0x140 run, named
//  through 0x004221f0, or -- if the world carries bits 4 or 5 instead -- one
//  object for the whole world, named after the world file and placed at the
//  world's own +0xe8 point.
//
//  THE INLINE BUDGET IS THE SOURCE (measured; this used to say "nothing in
//  the source can ask for either").  Shipped CALLS ObjectCreateStruct::
//  Clear() inside the inlined constructor (0x004144f2 -> 0x004147f0), CALLS
//  CClientFileRef's constructor in the skin-array loop (0x00414513 ->
//  0x004147e0), expands the first Clear() whole, and expands the second
//  with LTRotation::Init left as the call (0x004146e7 -> 0x00402ea0).  That
//  is /O2's per-function inline budget running out, and three inline
//  expansions the old source did not have spend exactly enough of it:
//    * the world list at CClientMgr+0x188 as a CMoArray-shaped view with
//      in-class GetSize() and operator[] -> Get() (the CArray32c/CArray188
//      pairing in clientmgr3.cpp's header: vptr, m_pArray +0x18c,
//      m_nElements +0x190);
//    * the blocker run at world+0x138 the same way (GetSize() on +0x140);
//    * SAFE_STRCPY (ltbasedefs.h) for the file name -- LTStrCpy's
//      `strncpy(dest, src, 127); dest[127] = 0` at 0x00414744 / 0x00414754.
//  With the first alone the function is 765 bytes, with the first two 762;
//  all three give 758, identical, and emit 0x004147e0 / 0x004147f0 too.

//  ALLOCATION, NOT SOURCE.  %ebp holds the constant 2 from 0x004144f9 to the
//  end and serves as the skin-array count, the Clear() skin-loop count AND
//  the OT_WORLDMODEL stored at 0x00414615 / 0x00414731.
// ======================================================================= //

LTBOOL CClientShellQ414::CreateVisContainerObjects()
{
	// Declaration order is fixed by construction order: the shipped code
	// runs ObjectCreateStruct's constructor (0x004144f2) BEFORE the request's
	// three-store-plus-loop one (0x004144fe..0x0041451c).
	ObjectCreateStruct       ocs;
	CClientCreateRequestQ414 request;
	CClientMgrQ414*          pClientMgr;
	CClientWorldRecQ414*     pRec;
	HLOCALOBJ                hObject;
	uint32                   i, j;

	request.m_pStruct = &ocs;

	pClientMgr = GetClientMgr();

	// The bound is re-read every iteration (0x004147a6 reloads it after the
	// body), so it is written out here rather than cached.
	for (i = 0; i < pClientMgr->m_WorldRecs.GetSize(); i++)
	{
		pRec = pClientMgr->m_WorldRecs[i];

		if (pRec->m_pWorld->m_Flags & 0x8)
		{
			// Same rule for the inner bound: 0x00414674 re-reads
			// pRec->m_pWorld->m_nBlockers through a fresh load of
			// pRec->m_pWorld.
			for (j = 0; j < pRec->m_pWorld->m_Blockers.GetSize(); j++)
			{
				ocs.Clear();
				ocs.m_ObjectType = OT_WORLDMODEL;
				ocs.m_Flags      = FLAG_RAYHIT | FLAG_SOLID;

				// 0x0041462c pushes 0x80 -- the whole buffer, not
				// MAX_CS_FILENAME_LEN.
				if (!sub_4221f0(pRec->m_pWorld->m_Name, j,
				                ocs.m_Filename, MAX_CS_FILENAME_LEN + 1))
				{
					return LTFALSE;
				}

				if (sub_411a70(m_pClientMgr, 0xffff, &request, &hObject,
				               0, 0))
				{
					return LTFALSE;
				}
			}
		}
		else if (pRec->m_pWorld->m_Flags & 0x30)
		{
			ocs.Clear();
			ocs.m_ObjectType = OT_WORLDMODEL;
			ocs.m_Flags      = FLAG_VISIBLE | FLAG_RAYHIT | FLAG_SOLID;

			// 0x00414744 pushes 0x7f and 0x00414754 writes the terminator
			// at ocs.m_Filename[127] -- strncpy with the length one short of
			// the buffer, then a hand-written NUL.
			SAFE_STRCPY(ocs.m_Filename, pRec->m_pFile->m_Name)

			ocs.m_Pos = pRec->m_pWorld->m_vPos;

			if (sub_411a70(m_pClientMgr, 0xffff, &request, &hObject, 1, 0))
				return LTFALSE;
		}
	}

	return LTTRUE;
}


// ======================================================================= //
//  CClientShell::GetClientMgr -- 0x00414b10
//
//  Not new work -- src/engine/lithtech/clientmgr2.cpp already declares it at
//  this address ("four bytes: the back pointer, straight out").  It is
//  defined here only because CreateVisContainerObjects CALLS it
//  (0x0041452a) rather than reading the member, while the two object-create
//  sites inside the loop read `m_pClientMgr` straight (0x00414656,
//  0x0041478e).  That split is in the source, not in the optimiser: an
//  out-of-line accessor cannot be inlined at one site and called at another
//  in the same function.
//
//  IF clientmgr2.cpp DEFINES THIS TOO, DELETE ONE OF THEM.  Two definitions
//  of the same three bytes are two COMDATs; /Gy folds them, but only one TU
//  should own it.
// ======================================================================= //

CClientMgrQ414* CClientShellQ414::GetClientMgr()
{
	return m_pClientMgr;
}


// ======================================================================= //
//  MORE OF THE SAME RUN
//
//  The three functions below are the client-shell rows still uncovered
//  beside 0x004144e0 in 0x414000..0x418200.
//
//  0x004147e0 and 0x004147f0 are not written as functions at all: they are
//  the two CONSTRUCTOR COMDATs 0x004144e0 above forces this TU to emit.
//      0x004147e0  CClientFileRefQ414::CClientFileRefQ414(), 14 bytes, the
//                  out-of-line copy the array-of-two loop at 0x00414511
//                  calls.
//      0x004147f0  ObjectCreateStruct::Clear(), 109 bytes, out of line
//                  (no `mov eax,ecx` this-return, so NOT the constructor;
//                  cmgsurf.cpp's note agrees), LTRotation::Init expanded in
//                  it (0x00414814..0x00414827).  This TU now emits and
//                  matches both.
// ======================================================================= //

// ltnew1.cpp's spelling: one object, one name.
class CDepRecord;                                          // ltnew1.cpp's
extern CDepRecord         g_Unknown4a8380;                     // 0x004a8380
extern int                g_DebugLevelQ414;                    // 0x004b0ab0

// The .bss flag the inherit check opens with.  Not in any initialised
// section, read once
// and tested against zero -- nothing here says what it holds.  (name ours)
extern uint32             g_Unknown4b329cQ414;                 // 0x004b329c

// cmodelload.cpp's cm_GetFileNameQ474 / cm_FindFileQ474, restated so this TU
// stands alone.  m_nFileType == 2 is the lookup-by-local-id form: 0x004046a9
// compares the identity's +0x00 against 2 and only then reads its +0x08 word
// and hands it to 0x00404410.
char* cm_GetFileNameQ414(CFileMgrQ414* pFileMgr,
                         CClientFileRefQ414* pIdent);          // 0x004045d0
class CFileRecQ414* cm_FindFileQ414(CFileMgrQ414* pFileMgr,
                                    CClientFileRefQ414* pIdent,
                                    uint8 nType);              // 0x00404680

class CFileRecQ414
{
public:
	void* m_pData;                                             // 0x00
	// cmodelload.cpp's CFileRecQ474::m_pName, the interned path the loader
	// thread prints and loads by (0x004156f4, 0x00415705).
	uint8 m_pad_04[0x1c - 0x04];
	char* m_pName;                                             // 0x1c
};

// ltnew1.cpp's CKeyedListView / cmodelload.cpp's CKeyedNodeQ474, restated.
class CKeyedNodeQ414
{
public:
	void*           m_pKey;                                    // 0x00
	void*           m_pData;                                   // 0x04
	CKeyedNodeQ414* m_pNext;                                   // 0x08
};

class CKeyedListQ414
{
public:
	CKeyedNodeQ414* m_pHead;                                   // 0x00
};

CKeyedNodeQ414* cb_MakeNodeQ414(void* pKey, void* pData);      // 0x0043c500
LTRESULT        cb_AddNodeQ414(CKeyedListQ414* pList,
                               CKeyedNodeQ414* pNode);         // 0x0043c580

void con_ConsolePrintQ414(char* pFormat, ...);                 // 0x0042e6f0


// ======================================================================= //
//  cs_InheritFileFromServer -- 0x00414930
//
//  WHAT NAMES IT.  The one string it raises, "Client inherited model %s from
//  server!" (0x004a86bc, pushed at 0x004149ce).  The name is ours; the word
//  "inherited" is the shipped string's.
//
//  WHAT THE ARGUMENT IS.  Not settled, and this file does not pretend it is.
//  What the bytes give: a uint16 at +0x1c that is the file manager's LOCAL
//  ID (0x0041496e loads a word and 0x00414974 stores it into the identity's
//  +0x08), and a four-byte keyed-list head at +0x14 (0x0041499f computes
//  +0x14 and hands it to cb_AddNode, whose only field is the head pointer at
//  +0x00).  cmodelload.cpp's ModelQ474 has a keyed list at exactly +0x14 --
//  its own note says so in as many words -- and the string says "model", so
//  a Model is the likely class; but ModelQ474 also spells +0x14..+0x1f as
//  one LTLink, which would put this uint16 inside that link's m_pData, and
//  nothing here settles which spelling is right.  So the view below claims
//  only the two offsets it reads.
//
//  TWO CALL SITES, both outside this run: 0x00463c89 and 0x0046add3.
// ======================================================================= //

class CInheritOwnerQ414
{
public:
	uint8          m_pad_00[0x14];
	CKeyedListQ414 m_List14;                                   // 0x14
	uint8          m_pad_18[0x1c - 0x18];
	uint16         m_nLocalID;                                 // 0x1c
};

void cs_InheritFileFromServerQ414(CInheritOwnerQ414* pOwner)
{
	// Declared before the guard, and it shows: the constructor's three
	// stores are emitted at 0x0041493b..0x0041494b, ABOVE the branch at
	// 0x00414952, so the m_nFileType = 0 the constructor writes cannot be
	// folded into the 2 written below it -- the two stores are in different
	// blocks.
	CClientFileRefQ414 ident;
	CFileRecQ414*      pRec;

	if (!g_Unknown4b329cQ414)
	{
		ident.m_nFileType = 2;
		ident.m_nLocalID  = pOwner->m_nLocalID;

		pRec = cm_FindFileQ414(g_pClientMgrQ414->m_pFileMgr, &ident, 1);

		if (pRec && !pRec->m_pData)
		{
			pRec->m_pData = pOwner;

			cb_AddNodeQ414(&pOwner->m_List14,
			               cb_MakeNodeQ414(&g_Unknown4a8380, pRec));

			if (g_DebugLevelQ414 >= 1)
			{
				con_ConsolePrintQ414(
				    "Client inherited model %s from server!",
				    cm_GetFileNameQ414(g_pClientMgrQ414->m_pFileMgr,
				                       &ident));
			}
		}
	}
}


// ======================================================================= //
//  cs_DirtyAllModels -- 0x00414a00
//
//  WHAT NAMES IT.  Its only call site, 0x0047558d, is the tail of the
//  SERVER's copy of the same walk: 0x00475560 runs the identical loop over
//  the server manager's own list at +0x5c4, sets the same bit on the same
//  +0x1dc model, and then calls this to do the client's half.  So this is
//  the client side of one operation that touches both managers' object
//  lists.  Both names below are ours.
//
//  WHAT THE TWO SIDES DO NOT SHARE, and it is in the source, not the
//  optimiser: the server's loop tests ONLY `pObj->m_pModel` (0x0047557e),
//  while the client's tests `!pObj->m_Unknown188 && pObj->m_pModel`
//  (0x00414a2a / 0x00414a2e).  Two copies of one walk that were edited
//  apart, the way clientmgr2.cpp's note on the two LTList constructors
//  describes.
//
//  The list is an LTLink used as its own end marker -- 0x00414a10 takes the
//  ADDRESS of +0x420 and compares every m_pNext against it -- so the walk
//  stops on the head and not on null, which is ltlink.h's LTList exactly.
//  The head's address is held in %esi for the whole walk (never re-read
//  from g_pClientMgr), which is the named pListHead local: without it the
//  store through the model may alias the global and VC6 reloads it every
//  iteration (80 bytes against 68).
// ======================================================================= //

// The model's flag word, at the offset the read-modify-write at
// 0x00414a32..0x00414a37 uses.  Bit 0 is all this run sets.  cmodelload.cpp's
// ModelQ474 leaves +0x20 inside m_pad_20 and names nothing there, so the
// name is ours.  (The server's copy of this same statement compiles to
// `orl $0x1,0x20(%ecx)` at 0x00475582 and the client's to a load, `or
// $0x1,%al`, and a store.  That is SOURCE, not allocation: the client reads
// +0x188 and +0x1dc into two locals before testing either -- both loads
// sit above both tests at 0x00414a1e / 0x00414a24 -- and the store through
// the model local takes the load/or/store form.  Written as one `&&` over
// the members it came out `orl` with the loads interleaved.)
class ModelQ414
{
public:
	uint8  m_pad_00[0x20];
	uint32 m_Flags20;                                          // 0x20
};

// The client object, as far as this walk reaches it.  +0x1dc is
// clientmgr.h's CClientModel::m_pModel and cmodelload.cpp's
// CClientObjectQ474::m_pModel, at the same offset in both.
class CClientObjectQ414
{
public:
	uint8      m_pad_000[0x188];
	uint32     m_Unknown188;                                   // 0x188
	uint8      m_pad_18c[0x1dc - 0x18c];
	ModelQ414* m_pModel;                                       // 0x1dc
};

void cs_DirtyAllModelsQ414()
{
	LTLink*            pCur;
	LTLink*            pListHead;
	CClientObjectQ414* pObj;
	uint32             bSkip;
	ModelQ414*         pModel;

	if (g_pClientMgrQ414)
	{
		pListHead = &g_pClientMgrQ414->m_ObjectList.m_Head;
		for (pCur = pListHead->m_pNext; pCur != pListHead; pCur = pCur->m_pNext)
		{
			pObj   = (CClientObjectQ414*)pCur->m_pData;
			bSkip  = pObj->m_Unknown188;
			pModel = pObj->m_pModel;

			if (!bSkip && pModel)
				pModel->m_Flags20 |= 1;
		}
	}
}


// ======================================================================= //
//  THE CLIENT'S MESSAGE-OBJECT HELPER -- vtable 0x004a1dcc
//
//  Slot 0 0x00415580 (2 args), slot 1
//  0x004155c0 (2), slot 2 0x004155f0 (3), slot 3 0x0043ec90.  That is
//  src/engine/server/servermessage.cpp's CMessageObjectHelper, the client's
//  copy: ReadObjectID / WriteObjectID / WriteFormattedString / GetWorld.
//  0x004155c0 is WriteObjectID without the server's Verify() split (one
//  `mov 0xb8(%eax),%ax`, else 0xffff), and 0x004155f0 below is the server's
//  0x100350bc with the client manager at +0x04 in place of the server
//  manager, and the module record read from the manager's +0x618 in place
//  of CClassMgr::m_pUnknown08.  Only slot 2 is written here (it is the one
//  handed to this file); the helper's home TU is not established -- its
//  neighbours 0x00415580 / 0x004155c0 are listed as unwritten in
//  clientmgr2.cpp.  (Q414 names ours; the method name is the server twin's.)
// ======================================================================= //

// servermsg.h's IMessageWrite, cut to the two slots this reaches.
class IMessageWriteQ414
{
public:
	virtual void _v00()=0; virtual void _v04()=0; virtual void _v08()=0;
	virtual void _v0c()=0; virtual void _v10()=0; virtual void _v14()=0;
	virtual void _v18()=0; virtual void _v1c()=0; virtual void _v20()=0;
	virtual void _v24()=0; virtual void _v28()=0; virtual void _v2c()=0;
	virtual void _v30()=0; virtual void _v34()=0; virtual void _v38()=0;
	virtual void _v3c()=0; virtual void _v40()=0;
	virtual LTRESULT WriteWord(uint16 val)=0;                  // 0x44
	virtual void _v48()=0; virtual void _v4c()=0; virtual void _v50()=0;
	virtual void _v54()=0; virtual void _v58()=0; virtual void _v5c()=0;
	virtual void _v60()=0; virtual void _v64()=0;
	virtual LTRESULT WriteRaw(void* pData, uint32 len)=0;      // 0x68
};

// servermessage.cpp's clm_GetRecordModule (0x00477f30, byte-identical in
// both images) and clientstreams.cpp's str_FormatMessage / str_FreeMessage.
void* clm_GetRecordModuleQ414(void* pRecord);                  // 0x00477f30
void* str_FormatMessageQ414(void* pModule, int messageCode,
                            va_list* pMarker, uint32* pLen);   // 0x00484780
void  str_FreeMessageQ414(void* pRaw);                         // 0x00484840

class CMessageObjectHelperQ414
{
public:
	LTRESULT WriteFormattedString(IMessageWriteQ414* pMsg,
	                              uint32 messageCode,
	                              va_list* pMarker);           // 0x004155f0

	void*           m_pVtable;                                 // 0x00
	CClientMgrQ414* m_pClientMgr;                              // 0x04
};

// 0x004155f0 -- the server twin's body word for word, down to the length
// written over the pMarker PARAMETER slot (`lea 0xc(%esp),%eax` pushed as
// pLen, then `mov 0x10(%esp),%ecx` reading it back as the length).
LTRESULT CMessageObjectHelperQ414::WriteFormattedString(IMessageWriteQ414* pMsg,
                                                        uint32 messageCode,
                                                        va_list* pMarker)
{
	void* pRaw = str_FormatMessageQ414(
		clm_GetRecordModuleQ414(m_pClientMgr->m_pStringModule618),
		messageCode, pMarker, (uint32*)&pMarker);

	if (pRaw)
	{
		pMsg->WriteWord((uint16)(uint32)pMarker);
		pMsg->WriteRaw(pRaw, (uint32)pMarker);
		str_FreeMessageQ414(pRaw);
	}
	else
	{
		pMsg->WriteWord(0);
	}

	return LT_OK;
}


// ======================================================================= //
//  CLoaderThread's constructor -- 0x00415660, 25 bytes
//
//  LThread's constructor (0x00444570), the vtable store (0x004a1ddc), and
//  +0x60 (the client manager ProcessMessage gates on) cleared; `this`
//  returned.  Byte for byte the shape of smgr.cpp's CServerThread
//  constructor (lithtech.exe 0x0047bec0, vtable 0x004a341c), and with the
//  vtable masked the two are indistinguishable to the scorer: while this one
//  was unwritten, ??0CServerThread was credited HERE and 0x0047bec0 showed
//  as unwritten.  The scalar-deleting destructor after it, 0x00415680, is
//  shared by both vtables (slot 0 of 0x004a1ddc and of 0x004a341c) -- the
//  linker folded the two identical ??_G bodies -- so it comes with this
//  class and with CServerThread alike.  The base and the slots are restated
//  from src/engine/server/lthread.h rather than included, so as to add
//  nothing else to this TU.  (Stand-in names; the class is CLoaderThread.)
// ======================================================================= //
class LThreadQ415
{
public:
	LThreadQ415();                                             // 0x00444570
	virtual ~LThreadQ415();                                    // 0x004445a0
	virtual void ThreadTerm();                                 // 0x004445c0
	virtual void ThreadRun();                                  // 0x00444760
	virtual void ProcessMessage(void* pMsg);

	uint8 m_pad_04[0x60 - 0x04];
};

class CLoaderThreadQ415 : public LThreadQ415
{
public:
	CLoaderThreadQ415();                                       // 0x00415660
	virtual void ProcessMessage(void* pMsg);                   // 0x004156a0

	CClientMgrQ414* m_pClientMgr;                              // 0x60
};

CLoaderThreadQ415::CLoaderThreadQ415()
{
	m_pClientMgr = LTNULL;
}


// ======================================================================= //
//  CLoaderThread::LoadModel -- 0x004156d0
//
//  THE NAME IS SHIPPED: "CLoaderThread::LoadModel(%s)" (0x004a86fc, pushed
//  at 0x004156f8).  The class is an LThread (src/engine/server/lthread.h):
//  its vtable 0x004a1ddc is slot 0 0x00415680 (scalar-deleting destructor,
//  calls 0x004445a0 ~LThread), slot 1 0x004445c0 ThreadTerm, slot 2
//  0x00444760 ThreadRun, slot 3 0x004156a0 ProcessMessage -- which gates on
//  +0x60, requires message words +0x08 and +0x10 zero and calls this with
//  word +0x14 (0x004156bd).  0x00415660 is the constructor (LThread ctor
//  0x00444570, vtable store, `+0x60 = 0`); clientmgr3.cpp declares it as
//  CCmgThread at the manager's +0x15a8.  The request this answers is the
//  one cmodelload.cpp's cm_LoadModel posts (0x0047706f: +0x08 = +0x10 = 0,
//  +0x14 = the file record) and the reply goes out on m_OutgoingQueue
//  (`lea 0x2c(%edi),%ecx`, 0x004442c0 LThreadQueue::PostMessage) carrying
//  the failure flag, the record and the model.
// ======================================================================= //

// lthread.h's LThreadMessage / LThreadQueue, restated.
class LThreadMessageQ414
{
public:
	LThreadMessageQ414();                                      // 0x004441a0

	LThreadMessageQ414* m_pNext;                               // 0x00
	LThreadMessageQ414* m_pPrev;                               // 0x04
	uint32              m_Data[6];                             // 0x08
};

class LThreadQueueQ414
{
public:
	LTRESULT PostMessage(LThreadMessageQ414* pMsg);            // 0x004442c0

	uint8 m_Opaque[0x28];
};

// cmodelload.cpp's cm_LoadModelDataQ474 (0x00477440).
class ModelQ414;
LTRESULT cm_LoadModelDataQ414(CClientMgrQ414* pMgr, char* pFilename,
                              CFileRecQ414* pFileRec,
                              ModelQ414** ppModel);            // 0x00477440

// The .bss word LoadModel tests before its trace line: the "DebugLoaders"
// console variable (lithtech's binding at 0x004aa0b0 points here), under
// dsi.cpp's name.
extern int g_CV_DebugLoaders;                                  // 0x004b3248

class CLoaderThreadQ414
{
public:
	void LoadModel(CFileRecQ414* pRec);                        // 0x004156d0

	void*            m_pVtable;                                // 0x00
	LThreadQueueQ414 m_IncomingQueue;                          // 0x04
	LThreadQueueQ414 m_OutgoingQueue;                          // 0x2c
	uint8            m_pad_54[0x60 - 0x54];
	CClientMgrQ414*  m_pClientMgr;                              // 0x60
};

void CLoaderThreadQ414::LoadModel(CFileRecQ414* pRec)
{
	LThreadMessageQ414 msg;
	ModelQ414*         pModel;
	LTRESULT           dResult;

	if (!m_pClientMgr)
		return;

	if (g_CV_DebugLoaders)
		con_ConsolePrintQ414("CLoaderThread::LoadModel(%s)", pRec->m_pName);

	dResult = cm_LoadModelDataQ414(m_pClientMgr, pRec->m_pName, pRec, &pModel);

	msg.m_Data[0] = dResult != LT_OK;
	msg.m_Data[2] = 0;
	msg.m_Data[3] = (uint32)pRec;
	msg.m_Data[4] = (uint32)pModel;
	m_OutgoingQueue.PostMessage(&msg);
}


// ======================================================================= //
//  THE PER-FRAME OBJECT UPDATES -- 0x00415bb0's callees
//
//  0x00415bb0 (__thiscall on the manager, unwritten; clientmgr3.cpp calls
//  it as Sub415bb0 between the client shell's Update and PostUpdate) zeroes
//  0x004b1800 and then passes the manager to six __cdecl walks in turn:
//  0x00415b50, 0x00415d00, 0x00415bf0, 0x00415e40, 0x00415ec0, 0x00415f10.
//  All but 0x00415d00 are written here, with the sprite tracker update
//  0x00483d00 they share; all names are ours.
// ======================================================================= //

class CClientConnectionQ414
{
public:
	uint8 m_pad_00[0x1c];
	float m_fFrameTime;                                        // 0x1c
};

// A sprite frame: the texture it shows first.
class CSpriteFrameQ414
{
public:
	uint32 m_pTexture;                                         // 0x00
};

// What a sprite instance draws with; +0x30 takes the current frame's
// texture (0x00415b96).
class CSpriteObjQ414
{
public:
	uint8  m_pad_00[0x30];
	uint32 m_pCurTexture;                                      // 0x30
};

// One animation of a loaded sprite, at the offsets 0x00483d00 reads:
// the frame array (+0x20, 4-byte entries: `lea (%eax,%edx,4)`), the frame
// count (+0x24), the animation's length in ms (+0x28, the wrap bound) and
// its frame rate (+0x2c, divided into 1000).  clientstreams.cpp's note on
// the loader 0x00483a60 records +0x28 = (1000 / msPerFrame) * nFrames.
class CSpriteAnimQ414
{
public:
	uint8             m_pad_00[0x20];
	CSpriteFrameQ414* m_Frames;                                // 0x20
	uint32            m_nFrames;                               // 0x24
	uint32            m_MsAnimLength;                          // 0x28
	uint32            m_MsFrameRate;                           // 0x2c
	uint8             m_pad_30[0x3c - 0x30];   // 0x3c records: 0x00483c91
};

// The sprite tracker, 20 bytes (cmodelload.cpp's 0x00477af4 indexes the
// model's pair of them by `iSkin * 20`): the sprite, the current
// animation, the frame it landed on, the time into the animation and the
// play/loop flags.  (names ours)
#define SC_PLAYQ414  1
#define SC_LOOPQ414  2

class CSpriteTrackerQ414
{
public:
	void*             m_pSprite;                               // 0x00
	CSpriteAnimQ414*  m_pCurAnim;                              // 0x04
	CSpriteFrameQ414* m_pCurFrame;                             // 0x08
	uint32            m_MsCurTime;                             // 0x0c
	uint32            m_Flags;                                 // 0x10
};

// One sprite instance: its object, the chain, and at +0x0c the tracker
// 0x00483d00 advances.

class CSpriteInstQ414
{
public:
	CSpriteObjQ414*    m_pObject;                              // 0x00
	CSpriteInstQ414*   m_pNext;                                // 0x04
	uint8              m_pad_08[0x0c - 0x08];
	CSpriteTrackerQ414 m_Tracker;                              // 0x0c
};

void spr_UpdateTrackerQ414(CSpriteTrackerQ414* pTracker,
                           uint32 msDelta);                    // 0x00483d00

// 0x00415b50 -- advance every sprite instance by this frame's time in
// milliseconds (`fmuls 1000.0f` at 0x00415b63, __ftol, unsigned `jbe`).
void cs_UpdateSpritesQ414(CClientMgrQ414* pMgr)
{
	CSpriteInstQ414* pInst;
	uint32           msDelta;

	if (pMgr->m_pConnection)
	{
		// M-P1-23: the fraction of a millisecond carried to the next frame
		static float s_fP1Carry;
		msDelta = (uint32)P1_FrameMs(pMgr->m_pConnection->m_fFrameTime, &s_fP1Carry);

		if (msDelta > 0)
		{
			for (pInst = pMgr->m_pSpriteInsts; pInst; pInst = pInst->m_pNext)
			{
				spr_UpdateTrackerQ414(&pInst->m_Tracker, msDelta);

				if (pInst->m_Tracker.m_pCurFrame)
					pInst->m_pObject->m_pCurTexture =
					    pInst->m_Tracker.m_pCurFrame->m_pTexture;
			}
		}
	}
}

// The client object's flag word at +0x88, as the two object walks below
// use it: bits 3 and 30 each make the object live this frame, and bits 4
// and 30 are cleared on every pass with bit 4 set again for a live one
// (`and $0xbfffffef` at 0x00415c36 / 0x00415e79, `or $0x10,%al` at
// 0x00415c5c / 0x00415e91).  The test reads the flags as they were BEFORE
// the clear -- bit 30 is one of the two cleared -- so the source holds
// them in a local.  (names ours)
#define CF_LIVE08Q414      0x00000008
#define CF_UPDATED10Q414   0x00000010
#define CF_LIVE40MQ414     0x40000000

// A client particle system, at src/engine/lithtech/partfm.cpp's offsets
// (its CClientParticleSystemQ45C view cites each): +0x18c m_Pos, +0x1e8
// m_pTexture, +0x1f0 m_pSprite, +0x1f4 m_SpriteTracker, +0x22c
// m_nDirtyCount (signed: `test %edi,%edi ; jg` at 0x00415cbe).
class CParticleObjQ414
{
public:
	uint8              m_pad_000[0x88];
	uint32             m_Flags;                                // 0x088
	uint8              m_pad_08c[0x18c - 0x8c];
	LTVector           m_Pos;                                  // 0x18c
	uint8              m_pad_198[0x1e8 - 0x198];
	uint32             m_pTexture;                             // 0x1e8
	uint8              m_pad_1ec[0x1f0 - 0x1ec];
	void*              m_pSprite;                              // 0x1f0
	CSpriteTrackerQ414 m_SpriteTracker;                        // 0x1f4
	uint8              m_pad_208[0x22c - 0x208];
	int                m_nDirtyCount;                          // 0x22c
};

// partfm.cpp's three particle entries: save the old bounding sphere
// (0x0045cf10), the per-frame update ps_UpdateParticles (0x0045ca70), and
// "recompute the sphere; has it moved?" (0x0045cf40).
void   ps_SavePrevBoundsQ414(CParticleObjQ414* pSys);          // 0x0045cf10
void   ps_UpdateParticlesQ414(CParticleObjQ414* pSys,
                              float fFrameTime);               // 0x0045ca70
LTBOOL ps_BoundsChangedQ414(CParticleObjQ414* pSys);           // 0x0045cf40

class CMoveObjQ414;
void cs_SetObjectPosQ414(CClientMgrQ414* pMgr, CMoveObjQ414* pObj,
                         LTVector* pPos, int bForce);          // 0x004207a0

// 0x00415bf0 -- the third walk: update every particle system on the list
// at +0x46c.  The ms delta is SIGNED here (`test ; jle` at 0x00415c09),
// unlike the two sprite walks.
void cs_UpdateParticleSystemsQ414(CClientMgrQ414* pMgr)
{
	LTLink*           pCur;
	CParticleObjQ414* pSys;
	uint32            nFlags;
	int               nDirty;
	LTBOOL            bMoved;
	int               msDelta;

	// M-P1-23: the fraction of a millisecond carried to the next frame
	static float s_fP1Carry;
	msDelta = (int)P1_FrameMs(pMgr->m_fFrameTime, &s_fP1Carry);

	if (msDelta > 0)
	{
		for (pCur = pMgr->m_ParticleList.m_Head.m_pNext;
		     pCur != &pMgr->m_ParticleList.m_Head;
		     pCur = pCur->m_pNext)
		{
			pSys   = (CParticleObjQ414*)pCur->m_pData;
			nFlags = pSys->m_Flags;
			pSys->m_Flags = nFlags & ~(CF_UPDATED10Q414 | CF_LIVE40MQ414);

			if ((nFlags & CF_LIVE08Q414) || (nFlags & CF_LIVE40MQ414) ||
			    pSys->m_nDirtyCount)
			{
				pSys->m_Flags |= CF_UPDATED10Q414;

				if (pSys->m_pSprite)
				{
					spr_UpdateTrackerQ414(&pSys->m_SpriteTracker, msDelta);

					if (pSys->m_SpriteTracker.m_pCurFrame)
						pSys->m_pTexture = pSys->m_SpriteTracker.m_pCurFrame->m_pTexture;
					else
						pSys->m_pTexture = 0;
				}

				nDirty = pSys->m_nDirtyCount;
				ps_SavePrevBoundsQ414(pSys);
				ps_UpdateParticlesQ414(pSys, pMgr->m_fFrameTime);

				bMoved = ps_BoundsChangedQ414(pSys);

				if (nDirty > 0 || bMoved)
					cs_SetObjectPosQ414(pMgr, (CMoveObjQ414*)pSys, &pSys->m_Pos, 1);

				pSys->m_nDirtyCount = 0;
			}
		}
	}
}

// A client sprite object: its sprite at +0x1b0 and tracker at +0x1b4
// (0x00415e99 / 0x00415ea3).
class CSpriteObjectQ414
{
public:
	uint8              m_pad_000[0x88];
	uint32             m_Flags;                                // 0x088
	uint8              m_pad_08c[0x1b0 - 0x8c];
	void*              m_pSprite;                              // 0x1b0
	CSpriteTrackerQ414 m_SpriteTracker;                        // 0x1b4
};

// 0x00415e40 -- the fourth walk: advance every sprite object on the list
// at +0x47c.
void cs_UpdateSpriteObjectsQ414(CClientMgrQ414* pMgr)
{
	LTLink*            pCur;
	LTLink*            pListHead;
	CSpriteObjectQ414* pObj;
	uint32             nFlags;
	uint32             msDelta;

	// M-P1-23: the fraction of a millisecond carried to the next frame
	static float s_fP1Carry;
	msDelta = (uint32)P1_FrameMs(pMgr->m_fFrameTime, &s_fP1Carry);

	if (msDelta)
	{
		pListHead = &pMgr->m_SpriteList.m_Head;
		for (pCur = pListHead->m_pNext; pCur != pListHead; pCur = pCur->m_pNext)
		{
			pObj   = (CSpriteObjectQ414*)pCur->m_pData;
			nFlags = pObj->m_Flags;
			pObj->m_Flags = nFlags & ~(CF_UPDATED10Q414 | CF_LIVE40MQ414);

			if ((nFlags & CF_LIVE08Q414) || (nFlags & CF_LIVE40MQ414))
			{
				pObj->m_Flags |= CF_UPDATED10Q414;

				if (pObj->m_pSprite)
					spr_UpdateTrackerQ414(&pObj->m_SpriteTracker, msDelta);
			}
		}
	}
}

// An object on the move list: a pending position at +0x18c and its flag
// at +0x1ac (0x00415edb / 0x00415ee5 / 0x00415ef8).
class CMoveObjQ414
{
public:
	uint8    m_pad_000[0x18c];
	LTVector m_vPendingPos;                                    // 0x18c
	uint8    m_pad_198[0x1ac - 0x198];
	uint32   m_bPendingMove;                                   // 0x1ac
};

// clientmgr.h's sub_4207a0: set an object's position, the last argument
// the "force it however small" flag.
void cs_SetObjectPosQ414(CClientMgrQ414* pMgr, CMoveObjQ414* pObj,
                         LTVector* pPos, int bForce);          // 0x004207a0

// 0x00415ec0 -- apply every pending move on the list at +0x48c and clear
// the flag, set or not (the store at 0x00415ef8 is below the join).
void cs_ApplyPendingMovesQ414(CClientMgrQ414* pMgr)
{
	LTLink*       pCur;
	CMoveObjQ414* pObj;

	for (pCur = pMgr->m_MoveList.m_Head.m_pNext;
	     pCur != &pMgr->m_MoveList.m_Head;
	     pCur = pCur->m_pNext)
	{
		pObj = (CMoveObjQ414*)pCur->m_pData;

		if (pObj->m_bPendingMove)
			cs_SetObjectPosQ414(pMgr, pObj, &pObj->m_vPendingPos, 1);

		pObj->m_bPendingMove = 0;
	}
}


// The client model instance, at cmodelload.cpp's CClientModelInstQ474
// offsets: two skin textures at +0x1b8, two skin sprites at +0x208 and
// their trackers at +0x210 (20 bytes each: esi steps by 0x14 at 0x00415faf).
class CTexRefQ414
{
public:
	uint8 m_pad_00[0x1c];
	char* m_pName;                                             // 0x1c
};

class CSpriteDataQ414
{
public:
	uint8        m_pad_00[0x24];
	CTexRefQ414* m_pTexRef;                                    // 0x24
};

class CModelInstQ414
{
public:
	uint8              m_pad_000[0x1b8];
	void*              m_pSkins[MAX_MODEL_TEXTURES];           // 0x1b8
	uint8              m_pad_1c0[0x208 - 0x1c0];
	void*              m_pSprites[MAX_MODEL_TEXTURES];         // 0x208
	CSpriteTrackerQ414 m_SpriteRecs[MAX_MODEL_TEXTURES];       // 0x210
};

// 0x0041ff50 -- lttexmod.cpp's texture resolver (cmodelload.cpp's
// cm_LoadTextureQ474).
LTRESULT cm_LoadTextureQ414(CClientMgrQ414* pMgr, CClientFileRefQ414* pRef,
                            void** ppTexture);                 // 0x0041ff50

// 0x00415f10 -- the sixth walk: for every object on the main list, advance
// each sprite skin and re-resolve its texture from the frame it landed on.
// The file reference is built once, at the top (its three zeroing stores
// sit between the fld and __ftol at 0x00415f28..0x00415f30), and the
// texture is resolved through g_pClientMgr, not the argument (0x00415f8e).
void cs_UpdateModelSkinsQ414(CClientMgrQ414* pMgr)
{
	CClientFileRefQ414 ref;
	LTLink*            pCur;
	CModelInstQ414*    pObj;
	uint32             i;
	uint32             msDelta;

	// M-P1-23: the fraction of a millisecond carried to the next frame
	static float s_fP1Carry;
	msDelta = (uint32)P1_FrameMs(pMgr->m_fFrameTime, &s_fP1Carry);

	if (msDelta)
	{
		for (pCur = pMgr->m_ObjectList.m_Head.m_pNext;
		     pCur != &pMgr->m_ObjectList.m_Head;
		     pCur = pCur->m_pNext)
		{
			pObj = (CModelInstQ414*)pCur->m_pData;

			for (i = 0; i < MAX_MODEL_TEXTURES; i++)
			{
				if (pObj->m_pSprites[i])
				{
					spr_UpdateTrackerQ414(&pObj->m_SpriteRecs[i], msDelta);

					ref.m_pFilename = ((CSpriteDataQ414*)pObj->m_SpriteRecs[i].m_pCurFrame->m_pTexture)->m_pTexRef->m_pName;
					ref.m_nFileType = 1;
					cm_LoadTextureQ414(g_pClientMgrQ414, &ref, &pObj->m_pSkins[i]);
				}
			}
		}
	}
}


// ======================================================================= //
//  spr_Destroy -- 0x00483c60, spr_InitTracker -- 0x00483cc0
//
//  The two sprite-module functions just ahead of spr_UpdateTracker in the
//  image, written here beside it for the same reason (the sprite module has
//  no TU of its own).  clientmgr2.cpp calls the first as sub_483c60 and
//  clientmgr.h / cmodelload.cpp / partfm.cpp declare the second as
//  sub_483cc0.  The sprite header is 0x14 bytes (0x00483a60 allocates it):
//  a list link, then the animation array (+0x0c, 0x3c-byte records) and its
//  count (+0x10).  Every free goes to dfree (0x00429080).  (names ours,
//  after the SDK's spr_ prefix)
// ======================================================================= //

void dfree(void* ptr);                                         // 0x00429080

class CSpriteQ414
{
public:
	LTLink           m_Link;                                   // 0x00
	CSpriteAnimQ414* m_Anims;                                  // 0x0c
	uint32           m_nAnims;                                 // 0x10
};

// 0x00483c60 -- each animation's frame array (+0x20, `mov 0x20(%edi,%eax)`
// with a 0x3c stride), then the animation array, then the sprite.
void spr_DestroyQ414(CSpriteQ414* pSprite)
{
	uint32 i;

	if (pSprite->m_Anims)
	{
		for (i = 0; i < pSprite->m_nAnims; i++)
		{
			if (pSprite->m_Anims[i].m_Frames)
				dfree(pSprite->m_Anims[i].m_Frames);
		}

		dfree(pSprite->m_Anims);
	}

	dfree(pSprite);
}

// 0x00483cc0 -- point the tracker at the sprite's first animation and its
// first frame (none when the animation is empty), time zero, playing and
// looping (the `movl $0x3,0x10(%eax)` on both arms).
void spr_InitTrackerQ414(CSpriteTrackerQ414* pTracker, CSpriteQ414* pSprite)
{
	pTracker->m_pSprite = pSprite;
	pTracker->m_pCurAnim = &pSprite->m_Anims[0];

	if (pTracker->m_pCurAnim->m_nFrames > 0)
		pTracker->m_pCurFrame = &pTracker->m_pCurAnim->m_Frames[0];
	else
		pTracker->m_pCurFrame = LTNULL;

	pTracker->m_MsCurTime = 0;
	pTracker->m_Flags = SC_PLAYQ414 | SC_LOOPQ414;
}


// ======================================================================= //
//  spr_UpdateTracker -- 0x00483d00
//
//  HOME: the client's sprite module (0x00483a60 loader .. 0x00483f30
//  SpriteControl), which has no TU under src/engine/ yet; clientstreams.cpp
//  lists the run as unwritten.  Written here because every caller in this
//  file needs it.  __cdecl, two arguments.  Advance the time by the delta
//  when playing (bit 0), clamp to the last millisecond when past the end and
//  NOT looping (bit 1) -- any other case, in range or looping, takes the
//  modulo (0x00483d3d) -- then pick the frame as
//  (time / (1000 / frame rate)) % frame count.  A zero-length animation
//  clears the frame.  (name ours)
// ======================================================================= //

void spr_UpdateTrackerQ414(CSpriteTrackerQ414* pTracker, uint32 msDelta)
{
	CSpriteAnimQ414* pAnim;
	uint32           iFrame;

	pAnim = pTracker->m_pCurAnim;
	if (!pAnim)
		return;

	if (pAnim->m_MsAnimLength)
	{
		if (pTracker->m_Flags & SC_PLAYQ414)
			pTracker->m_MsCurTime += msDelta;

		if (pTracker->m_MsCurTime >= pAnim->m_MsAnimLength &&
		    !(pTracker->m_Flags & SC_LOOPQ414))
			pTracker->m_MsCurTime = pAnim->m_MsAnimLength - 1;
		else
			pTracker->m_MsCurTime %= pAnim->m_MsAnimLength;

		iFrame = (pTracker->m_MsCurTime / (1000 / pAnim->m_MsFrameRate)) % pAnim->m_nFrames;
		pTracker->m_pCurFrame = &pAnim->m_Frames[iFrame];
	}
	else
	{
		pTracker->m_pCurFrame = LTNULL;
	}
}


// *********************************************************************** //
//  NOT WRITTEN HERE, AND WHY
//  -------------------------
//  Still untouched in this cluster and still CLIENT-SHELL work, i.e. the
//  right next targets for a file like this one:
//
//    (0x00414a50 is now written at the foot of this file.)
//    0x004155c0 / 0x00415580 -- the client message helper's WriteObjectID
//                and ReadObjectID (vtable 0x004a1dcc; see the helper above).
//    0x00415660 / 0x00415680 / 0x004156a0 -- CLoaderThread's constructor,
//                scalar-deleting destructor and ProcessMessage.
//    0x00415bb0 / 0x00415d00 -- the per-frame update driver and its
//                second walk (the object list's model trackers at +0x1f8
//                with callback 0x00415da0, then the list at +0x43c through
//                0x00483d00 at +0x1a8).
//
//  And NOT client-shell work at all, so not here and not anyone's new work:
//  0x004152f0, 0x004165c0, 0x004169e0, 0x00416210, 0x00416de0, 0x004176c0,
//  0x00417da0 and 0x00418190 are src/engine/server/worldsim.cpp functions --
//  w_IsPolyBlocking, ws_ShiftBox, CSimContext::Setup, ws_ProjectBox,
//  CSimContext::TestPoly, CSimContext::DeflectOffHit, ws_MoveObject and
//  ws_SetupSweptBox -- all already written there and all NEAR rather than
//  matched in this image.  An address scan cannot find them because worldsim.cpp
//  carries server.dll's addresses in its comments and never lithtech.exe's;
//  the header of this file sets out the evidence for each.  0x00416f66 and
//  0x0041726f are not functions.
// *********************************************************************** //


// ======================================================================= //
//  0x0046a280 (146 bytes) -- sm_RemoveObject, lithtech.exe's copy.
//
//  HOME: src/engine/server/smgr.cpp's sm_RemoveObject (server 0x1002ac20),
//  which is written and IDENTICAL in server.dll but near here (110 vs 146):
//  smgr.cpp declares the queued-object bank's Allocate (server 0x1000ed6f)
//  without a body, so it stays a call, while this image expands it two
//  levels -- the free-list head 0x004b17c4, the grow 0x00490c20 with the
//  bank 0x004b17ac and its cache size 0x004b17b4, and the unlink.  /Os
//  declines the same inline body, which is why server.dll calls it.  When
//  smgr.cpp's CObjQueueBank::Allocate gets the inline body below, this
//  stand-in can go.  (Q414 stand-ins, as elsewhere in this file.)
// ======================================================================= //

// stdlith's StructBank: the cache size +0x08, the free list +0x18.
class StructBankQ414
{
public:
	uint32 m_StructSize;                     // +0x00
	uint32 m_AlignedStructSize;              // +0x04
	uint32 m_CacheSize;                      // +0x08
	uint32 m_nPages;                         // +0x0c
	uint32 m_nTotalObjects;                  // +0x10
	void*  m_pPageHead;                      // +0x14
	void*  m_pFreeList;                      // +0x18
};

// 0x00490c20 -- sb_AllocateNewStructPage.
void sb_GrowQ414(StructBankQ414* pBank, uint32 nCount);

// servermgr.h's sb_Allocate.
inline void* sb_AllocateQ414(StructBankQ414* pBank)
{
	void* pStruct = pBank->m_pFreeList;

	if (!pStruct)
	{
		sb_GrowQ414(pBank, pBank->m_CacheSize);

		pStruct = pBank->m_pFreeList;
		if (!pStruct)
			return LTNULL;
	}

	pBank->m_pFreeList = *(void**)pStruct;
	return pStruct;
}

class CObjQueueBankQ414
{
public:
	LTLink* Allocate() { return (LTLink*)sb_AllocateQ414(&m_Bank); }

	uint8          m_vptr[4];                // 0x004b17a8
	StructBankQ414 m_Bank;                   // 0x004b17ac
};

extern CObjQueueBankQ414 g_ObjQueueBankQ414;                   // 0x004b17a8

// servermgr.h's CServerMgr / LTObject, cut to the fields read here.
class CServerMgrQ46A
{
public:
	uint8  m_pad_00[0x1a0];
	uint32 m_Unknown1a0;                     // 0x1a0  bit 0: sweeping objects
	uint8  m_pad_1a4[0x83c - 0x1a4];
	LTLink m_Link83c;                        // 0x83c  the queued-object list
};

class LTObjectQ46A
{
public:
	uint8  m_pad_00[0x120];
	uint32 m_ChangeFlags;                    // 0x120
};

// 0x00429740 -- obj_FreeData (server 0x1000d8a8).
void obj_FreeDataQ414(LTObjectQ46A* hObj);

void sm_RemoveObjectQ414(CServerMgrQ46A* pServerMgr, LTObjectQ46A* hObj)
{
	if (~pServerMgr->m_Unknown1a0 & 1)
	{
		if (~hObj->m_ChangeFlags & 1)
		{
			LTLink* pLink = g_ObjQueueBankQ414.Allocate();

			pLink->m_pData = hObj;
			dl_Insert(&pServerMgr->m_Link83c, pLink);

			hObj->m_ChangeFlags |= 1;

			obj_FreeDataQ414(hObj);

			hObj->m_ChangeFlags &= ~4;
		}
	}
}


// ======================================================================= //
//  0x00414a50 (187 bytes) -- the client shell's record for one server
//  object id, or a new, zeroed one.
//
//  The same body as ccobj.cpp's snd_FindSentStateQ464 (0x00466550) without
//  its flag store: the key is the uint16 PARAMETER's own address (`lea
//  0x10(%esp)`, length 2) in the hash at CClientShell+0x8c, the record comes
//  from the struct bank at CClientMgr+0x1120 -- sb_AllocateCleared expanded
//  with the memset intrinsic (0x00414ab1..0x00414ac3) -- and the failure
//  path gives it back with dfree (0x00429080), not sb_Free, as the sound
//  table does.  clientshell.cpp's Term walks this same +0x8c hash and frees
//  its records onto +0x1120.  Callers 0x004798e9 / 0x00479d84.  (Q414
//  stand-ins, names ours.)
// ======================================================================= //

// g_pClientMgrQ414 again, viewed at the bank (CClientMgrQ414 is declared
// above StructBankQ414 and pads over +0x1120).
class CClientMgrBankQ414
{
public:
	uint8          m_pad_0000[0x1120];
	StructBankQ414 m_Bank1120;                              // 0x1120
};

class CClientShellObjMapQ414
{
public:
	uint8  m_pad_00[0x8c];
	void*  m_hObjHash;                                       // 0x08c

	void*  GetObjRecord(uint16 nID);                         // 0x00414a50
};

void* dhash_FindElementQ414(void* hTable, void* pKey, uint32 nLen);   // 0x0042cf70
void* dhash_AddElementQ414(void* hTable, void* pKey, uint32 nLen);    // 0x0042ce30
void* dhash_GetDataQ414(void* hElement);                              // 0x0042d080
void  dhash_SetDataQ414(void* hElement, void* pData);                 // 0x0042d090
void  dfreeQ414(void* p);                                             // 0x00429080

inline void* sb_AllocateClearedQ414(StructBankQ414* pBank)
{
	void* pStruct = pBank->m_pFreeList;

	if (!pStruct)
	{
		sb_GrowQ414(pBank, pBank->m_CacheSize);

		pStruct = pBank->m_pFreeList;
		if (!pStruct)
			return LTNULL;
	}

	pBank->m_pFreeList = *(void**)pStruct;

	memset(pStruct, 0, pBank->m_StructSize);

	return pStruct;
}

void* CClientShellObjMapQ414::GetObjRecord(uint16 nID)
{
	void* hElement;
	void* pRec;

	hElement = dhash_FindElementQ414(m_hObjHash, &nID, sizeof(nID));

	if (hElement)
	{
		pRec = dhash_GetDataQ414(hElement);

		if (pRec)
			return pRec;
	}

	pRec = sb_AllocateClearedQ414(&((CClientMgrBankQ414*)g_pClientMgrQ414)->m_Bank1120);

	if (!pRec)
		return LTNULL;

	hElement = dhash_AddElementQ414(m_hObjHash, &nID, sizeof(nID));

	if (!hElement)
	{
		dfreeQ414(pRec);
		return LTNULL;
	}

	dhash_SetDataQ414(hElement, pRec);

	return pRec;
}
