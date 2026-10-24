#include "servermgr.h"
#include "servermsg.h"
#include "de_error.h"

// ======================================================================= //
//  PART B -- cmodelload.obj  (src/engine/lithtech/cmodelload.cpp)
//
//  These two are the OT_SPRITE and OT_CONTAINER entries of the create-hook
//  table cmodelload.cpp already documents at line 1064.  WHAT PLACES THEM
//  IS THE TABLE, not the string pool: the dispatcher at 0x00477d60 indexes
//  0x004ae340 by the object's +0xbc type byte with a stride of EIGHT and
//  calls slot 0; 0x00477d90 calls slot 1.  Reading .data at that stride:
//
//      type 1 (OT_MODEL)      slot0 0x004778a0   slot1 0x00477b60
//      type 2 (OT_WORLDMODEL) slot0 0x00477b80   slot1 0
//      type 3 (OT_SPRITE)     slot0 0x00477ce0   slot1 0        <-- B1
//      type 9 (OT_CONTAINER)  slot0 0x00477d20   slot1 0        <-- B2
//
//  (0x004ae358 and 0x004ae388 are exactly 0x30 apart, which is what the
//  enumeration's two `ptr` provenances are: slot 0 of types 3 and 9.  BOTH
//  ADDRESSES WERE READ BACK AS LITTLE-ENDIAN TEXT FIRST.
//  0x00477ce0 is "\xe0|G\0", which is not printable, so it cannot be a
//  string tail.  0x00477d20 IS printable -- " }G\0" -- so it had to be
//  cleared the other way: the eight bytes of .data BELOW 0x004ae388 are all
//  zero (0x004ae380..0x004ae387), so there is no string whose last three
//  characters those could be, and the dword sits four-byte aligned inside a
//  run the dispatcher at 0x00477d60 indexes by construction.  Neither row is
//  an ASCII accident.)
//
//  The four arguments and their ORDER are fixed by 0x00477d76..0x00477d85,
//  which forwards its own (a1,a2,a3,a4) unchanged: (pClientMgr, pObject,
//  pCreateRequest, dResult).  Both bodies below leave a4 unread except as
//  the guard, exactly as 0x00477b80 does.
// ======================================================================= //

// lithtech.exe only: cmodelload.obj is client code with no server.dll twin,
// so the server build does not compile Part B (its callees are client-only).
#ifndef LT_SERVER_BUILD

#ifndef CMODELLOAD_CPP_Q474    // <- stand-in only; delete on paste

class CClientMgrQ474;

class CFileIdentQ474
{
public:
	uint32 m_nFileType;
	char*  m_pFilename;
	uint16 m_nLocalID;
};

class ObjectCreateStructQ474
{
public:
	uint16 m_ObjectType;                                       // 0x00
	uint16 m_ContainerCode;                                    // 0x02
};

class CClientCreateRequestQ474
{
public:
	CFileIdentQ474          m_ObjectFile;                      // 0x00
	CFileIdentQ474          m_SkinFiles[2];                    // 0x0c
	ObjectCreateStructQ474* m_pStruct;                         // 0x24
};

class CSpriteQ474;

class CSpriteTrackerQ474
{
public:
	uint8 m_pad_00[0x14];
};

// 0x00476e80 -- cmodelload.cpp's own cm_LoadSprite.
LTRESULT cm_LoadSpriteQ474(CClientMgrQ474* pMgr, CFileIdentQ474* pIdent,
                           CSpriteQ474** ppSprite);
// 0x00483cc0 -- clientmgr.h line 540.
void sub_483cc0Q474(CSpriteTrackerQ474* pRec, void* pSprite);

class CClientModelInstQ474
{
public:
	uint8              m_pad_000[0x1a8];
	CSpriteTrackerQ474 m_SpriteTracker;                        // 0x1a8
	uint8              m_pad_1bc[0x270 - 0x1bc];
	uint16             m_nUnknown270;                          // 0x270
};

// 0x00477b80 -- cmodelload.cpp's cm_WorldModelExtraInitQ474.
LTRESULT cm_WorldModelExtraInitQ474(CClientMgrQ474* pMgr,
                                    CClientModelInstQ474* pObject,
                                    CClientCreateRequestQ474* pRequest,
                                    LTRESULT dResult);

#endif  // CMODELLOAD_CPP_Q474


// ======================================================================= //
//  B1.  0x00477ce0 .. 0x00477d15, 54 bytes -- the SPRITE type's create hook
//
//  It has NO prologue at all and still passes a pointer into its own frame:
//  `lea 0xc(%esp),%eax` at 0x00477ce8 takes the address of the THIRD
//  ARGUMENT'S HOME SLOT, which the instruction two before it has just read
//  into %ecx and which nothing reads again.  That is the dead-
//  parameter-slot local (`ci_GetTcpIpAddress` is the worked example), so the
//  sprite pointer is an ordinary local and no `sub %esp` is needed.
//
//  ONE `ret`, TWO RETURNS.  The `jne 0x477d15` at 0x00477cf9 goes to a bare
//  `ret` with %eax already carrying the result, and the fall-through ends
//  `xor %eax,%eax` immediately above it: two `return` statements whose
//  epilogues (nothing to restore) tail-merge.  Written as a positive guard
//  the store of zero would be the ONLY producer and the early value would
//  have to be materialised somewhere; it is not.
//
//  The second argument to cm_LoadSprite is the request's FIRST field, so the
//  `&pRequest->m_ObjectFile` of the source is a bare register move at
//  0x00477ce0 with no `add` -- the same thing cm_InitSkin's caller does.
// ======================================================================= //

LTRESULT cm_SpriteExtraInitQ474(CClientMgrQ474* pMgr,
                                CClientModelInstQ474* pObject,
                                CClientCreateRequestQ474* pRequest,
                                LTRESULT dResult)
{
	CSpriteQ474* pSprite;
	LTRESULT     dRes;

	dRes = cm_LoadSpriteQ474(pMgr, &pRequest->m_ObjectFile, &pSprite);

	if (dRes != LT_OK)                                   // 0x00477cf7
		return dRes;

	sub_483cc0Q474(&pObject->m_SpriteTracker, pSprite);  // 0x00477d0b

	return LT_OK;                                        // 0x00477d13
}


// ======================================================================= //
//  B2.  0x00477d20 .. 0x00477d59, 58 bytes -- the CONTAINER type's create hook
//
//  A container IS a world model plus a code, so the body forwards to the
//  WORLDMODEL hook (0x00477d3b -> 0x00477b80) with a LITERAL LT_OK as its
//  fourth argument -- `push $0x0` at 0x00477d36, not `push %eax`, even
//  though %eax has just been proved zero -- and then copies the create
//  struct's m_ContainerCode (ltbasedefs.h's uint16 at ObjectCreateStruct+0x02,
//  read at 0x00477d4a) onto the object at +0x270.
//
//  THE GUARD IS POSITIVE.  0x00477d28 `jne` skips FORWARD to 0x00477d55, a
//  block past the body's own `return dRes` exit at 0x00477d57 --
//  a conditional jump forward to a block sitting past every other
//  return means that block is a trailing statement and the guard above it is
//  positive.  Its sibling 0x00477b80 is the other spelling and shows the
//  contrast directly: there the early return FALLS THROUGH (0x00477b8b `je`
//  skips it) and carries a duplicated epilogue.
// ======================================================================= //

LTRESULT cm_ContainerExtraInitQ474(CClientMgrQ474* pMgr,
                                   CClientModelInstQ474* pObject,
                                   CClientCreateRequestQ474* pRequest,
                                   LTRESULT dResult)
{
	if (dResult == LT_OK)                                // 0x00477d25
	{
		LTRESULT dRes = cm_WorldModelExtraInitQ474(pMgr, pObject,
		                                           pRequest, LT_OK);

		if (dRes != LT_OK)                               // 0x00477d43
			return dRes;

		pObject->m_nUnknown270 = pRequest->m_pStruct->m_ContainerCode;
	}

	return LT_OK;                                        // 0x00477d55
}

#endif  // !LT_SERVER_BUILD
