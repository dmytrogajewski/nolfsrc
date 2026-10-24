#include "file_transfer.h"
// server.dll — the se_ family: giving an object its file (reconstruction).
// Build /O1.
#include "serverengine.h"
#include "commonphysics.h"
#include "worldbsp.h"
#include <new>
#include <stdio.h>
#include <stdlib.h>
#include "lthread.h"
#include <string.h>


// A byte-wide view of the file record's flag word, for the one operation VC6
// will not narrow on its own.
//
// The field is sixteen bits: se_LoadFile, which creates the record, stores
// its nFlags there as a word (0x1002bf0b `mov %ax,0xc(%esi)`) and reads it
// back as one (0x1002bf58 `mov 0xc(%esi),%cx`), and declaring it `uint8`
// costs both of those. But the shipped clear of bit 0 is `andb $0xfe`
// (0x1002b4cb) where a `uint16` field gives us `andw $0xfffe`, so the
// operation was narrowed and ours is not.
//
// The narrowing is not uniform in our build either, which is what makes this
// worth writing down: `|= 1` on the same `uint16` field DOES come out as
// `orb $0x1` and matches the shipped bytes at 0x1002b699 (sm_UnloadModel)
// and 0x1002b84f without any help. VC6 narrows an OR with a small positive
// immediate and declines to narrow the AND whose immediate has an all-ones
// high byte — even though both are equally safe. So this is an
// immediate-width difference, not a claim that the record has a byte there;
// a byte-typed view at the clear site is simply the only spelling found that
// reproduces it, and it is what took se_RegisterModel from 193 bytes to
// byte-identical.




// 0x1001cb33 — a request the reader can be handed straight away: no stream,
// no context, no child name, the do-nothing callback, and all four flags set.
//
// The store order is the shipped order and is not the declaration order: the
// child name goes first, then the stream, then the CALLBACK POINTER, then the
// context, then the four flags. Both constants live in one register — `xor
// ecx,ecx` serves the three zero stores and the `push $1` hoisted to the top
// of the function is popped back into the same register for the four ones.
//
// THE THIRD AND FOURTH STORES LOOK SWAPPED AT /O1 AND ARE NOT, and only
// lithtech.exe can tell you so. At /O1 the two constants share one register,
// so the three zero stores HAVE to be emitted together before the `pop %ecx`
// turns that register into the ones — which puts +0x08 third and the
// immediate store to +0x04 last (0x1001cb33), whatever the source said. At
// /O2 the caller already holds zero in %ebx and one in %edi, nothing is
// grouped, and the source order survives: se_LoadModelData's expansion of
// this constructor at 0x0046aadf stores $se_NoChildModel to +0x04 BEFORE the
// zero to +0x08. That one transposition was the whole of the 583-byte
// function's difference.
//
// AND IT IS `inline`, which is what se_LoadModelData below needs. lithtech.exe
// (/O2) does NOT have a standalone copy of this constructor anywhere — the
// eight stores appear in line at 0x0046aac0, where `xor %ebx,%ebx` and
// `mov $1,%edi` hold the two constants and then serve the rest of that
// function (`cmp %ebx,%eax` for its null tests, `push %edi` for its ones).
// server.dll (/O1, /Os) calls it out of line at BOTH its sites — 0x1002b238
// inside se_LoadModelData and 0x1001c615 in another translation unit — and
// should carry on doing so, because the expansion GROWS the caller (581 bytes
// against our 561), which is the one thing /Os refuses. Defined above its
// caller because VC6's inliner is forward-only.
// (The body now lives in serverengine.h, above every caller; see there.)


// The bank every dependency link comes out of, and the lock that guards the
// lists. Two separate globals, and the addresses prove it: the bank object is
// at 0x100585d8 with its own critical section embedded at +0x20 (0x100585f8),
// and the list lock is the standalone twenty-four-byte one at 0x100585c0
// immediately before it. 0x10013fab is the bank's global initialiser — vptr,
// the embedded lock's constructor, then sb_Init2(&m_Bank, 0xc, 0x20, 0x20),
// which is where the record size comes from.
//
// THE SHAPE IS FORWARDER-OVER-WORKER, and the two images together are what
// say so. server.dll's vftable for this class (0x1004d7d8) does NOT point at
// the allocate and free bodies; it points at two thunks that do nothing but
// forward to them:
//
//     0x1001409c   jmp 0x100140d3                       (slot 1, 5 bytes)
//     0x100140a1   push 0x4(%esp); call 0x10014107;     (slot 2, 12 bytes)
//                  ret $4
//
// while lithtech.exe's vftable for the same class (0x004a2438) points STRAIGHT
// at a 101-byte and a 38-byte body — the thunks with their targets expanded
// into them. So the class overrides its base's slots with tiny forwarders and
// keeps the typed allocate/free as ordinary members, which is also what the
// call sites want: dl_MakeLink and dl_UnlinkAndFree reach the typed pair
// directly (0x10014053, 0x10014045) and at /O2 expand it (0x0043c500 is
// dl_MakeLink with the whole of Allocate in line, 0x0043c4b0 the same for
// Free), which is only possible if the typed pair is an inline candidate —
// i.e. declared IN-CLASS, as below. /Os declines both expansions at 52 and 38
// bytes, which is why server.dll still calls them, and that is the shipped
// proof that writing them in-class cannot cost server.dll anything.
//
// The forwarders are defined OUT of the class on purpose: that is the one
// construct /Ob1 never expands, so both bodies
// are emitted in both builds whether or not this translation unit ever emits
// a vftable to hold their addresses. In lithtech.exe the typed pair has no
// out-of-line body at all — every call site expands it — and it is the
// forwarders that carry the 101 and 38 bytes at 0x0043c5c0 and 0x0043c630.

// The base whose three slots after the destructor this bank fills in. It is
// servermgr.h's CBankBase — same vftable, 0x1004d260, with _purecall in all
// three slots — but that declaration spells the two middle slots `void _v04()`
// and `void _v08()`, and the thunks above prove this bank's are `void*` with
// no argument and `void` with one. An override has to match exactly, so the
// shared declaration cannot serve here without changing servermgr.h and every
// other bank that derives from it. Declared locally with the right signatures
// until that is in scope; the vftable addresses are relocations either way, so
// the emitted bytes are the same class of thing.
class CDepBankBase
{
public:
	virtual ~CDepBankBase() {}

	virtual void* _v04() = 0;
	virtual void  _v08(void* pStruct) = 0;
	virtual void  Term() = 0;
};

class CDepLinkBank : public CDepBankBase
{
public:
	// ---- from here to the definition of g_DepLinkCS below is one separable
	// addition: the bank's own lifetime, and the two globals this file's
	// address run says are defined here rather than merely referenced. See
	// the note above the constructor. ----
	//
	// 0x10013fab (50 bytes) is NOT a standalone constructor: it is the
	// compiler's _$E initialiser for g_DepLinkBank with this body expanded
	// into it — every store goes to an absolute address (`movl $vft,
	// 0x100585d8`, `push $0x100585dc`, `mov $0x100585f8,%ecx` for the
	// embedded lock) with no `this` register — and 0x10013fa1 is the _$E
	// that calls it and chains to the atexit registration at 0x10013fdd.
	// `NOLF_FORCE_INLINE`, as fsstream.cpp's two stream banks (0x10006d88,
	// 0x10006df7, the same shape): plain `inline`, in class or out, leaves
	// a ten-byte `mov ecx; jmp` initialiser and a 45-byte constructor.
	// Twelve-byte records, thirty-two cached and thirty-two added at a time;
	// sb_Init2 and not sb_Init because the initialiser pushes four arguments
	// and cleans up sixteen bytes.
	NOLF_FORCE_INLINE CDepLinkBank()             // 0x10013fab (as _$E)
	{
		sb_Init2(&m_Bank, 0xc, 0x20, 0x20);
	}
	virtual ~CDepLinkBank();                 // 0x100140ad
	// ---- end of the separable addition's first half ----

	// 0x1001409c / 0x100140a1 — the vftable's own two entries. Defined below.
	virtual void* _v04();
	virtual void  _v08(void* pStruct);
	virtual void  Term();

	// 0x100140d3 — a zeroed link off the bank, under the bank's own lock. The
	// three clearing stores and the null test share one materialised zero
	// (`xor eax,eax; cmp eax,esi`), which is why the test is a compare against
	// a register rather than the `test esi,esi` an isolated `if (p)` would
	// give: the constant the stores need already exists.
	//
	// In-class, and above every caller, because lithtech.exe expands it into
	// dl_MakeLink and into the slot-1 forwarder; VC6's inliner is forward-only.
	CDepLink* Allocate()
	{
		CDepLink* pLink;

		m_Lock.Enter();

		pLink = (CDepLink*)sb_Allocate(&m_Bank);

		if (pLink)
		{
			pLink->m_pRecord = LTNULL;
			pLink->m_pData   = LTNULL;
			pLink->m_pNext   = LTNULL;
		}

		m_Lock.Leave();

		return pLink;
	}

	// 0x10014107 — and back again. The free is servermgr.h's inline sb_Free,
	// so there is no call: the record's own first dword becomes the next-free
	// link.
	void Free(void* pLink)
	{
		m_Lock.Enter();

		sb_Free(&m_Bank, pLink);

		m_Lock.Leave();
	}

	StructBank       m_Bank;                 // 0x04
	LCriticalSection m_Lock;                 // 0x20
};

// 0x1001409c and 0x100140a1 at /O1; 0x0043c5c0 (101 bytes) and 0x0043c630
// (38 bytes) at /O2, where the body each one forwards to has been expanded
// into it. The zero-argument slot tail-jumps and the one-argument slot cannot,
// which is exactly the pair of shapes the shipped thunks have.
//
// DO NOT SCORE THEM AT /O1. The first is five bytes of which four are the
// jump displacement, so a byte matcher's eight-unmasked-byte floor drops it before
// it is searched; the second has exactly eight and will be searched, but a
// `push 0x4(%esp); call; ret $4` thunk is a shape the image carries many
// copies of, so it can be sited on the wrong one. Neither is the reason
// these two exist. They exist because at /O2 they are the ONLY thing holding
// 0x0043c5c0 and 0x0043c630: every call of the typed pair is expanded there,
// so without a body that must be emitted whatever the inliner does, both of
// those addresses would go unclaimed.
void* CDepLinkBank::_v04()
{
	return Allocate();
}

void CDepLinkBank::_v08(void* pStruct)
{
	Free(pStruct);
}

// 0x1001412d — slot 3, the one the base calls Term. Empty the bank's chunks
// and set it up again from scratch, under the bank's own lock; the record
// size and cache depth are the constructor's, minus the grow count, so this
// is sb_Init and not sb_Init2. The two argument pushes are cleaned up
// together (`add $0x14,%esp`), which is why there is no stack adjust between
// the calls.
void CDepLinkBank::Term()
{
	m_Lock.Enter();

	sb_FreeChunks(&m_Bank, 1);
	sb_Init(&m_Bank, 0xc, 0x20);

	m_Lock.Leave();
}

// The list itself is one pointer and nothing else — dl_AddLink writes through
// it and dl_UnlinkAndFree hands its address to 0x1002446d as a `this`, which
// walks the chain through CDepLink::m_pNext at +0x08 and unlinks the node it
// was given. Declared, not defined; it lives outside the run this file covers
// (names ours).
class CDepList
{
public:
	void RemoveLink(void* pLink);            // 0x1002446d

	CDepLink* m_pHead;                       // 0x00
};

// Twelve-byte records, thirty-two of them cached and thirty-two added at a
// time. sb_Init2 and not sb_Init because the initialiser pushes four
// arguments and cleans up sixteen bytes.
//
// INLINE, and defined above the global it constructs. server.dll has no
// standalone constructor: 0x10013fab (50 bytes) is the compiler's _$E
// initialiser for g_DepLinkBank with this body expanded into it — every
// store is to an absolute address (`movl $vft,0x100585d8`, `push $0x100585dc`,
// `mov $0x100585f8,%ecx` for the embedded lock) rather than through a `this`
// register — and 0x10013fa1 is the _$E that calls it and then chains to the
// atexit registration at 0x10013fdd. Written out of line, VC6 emits a
// ten-byte `mov ecx; jmp ctor` initialiser plus a 45-byte constructor, and
// neither pairs.
// ---- second half of the separable addition ----
//
// DEFINED here, not declared: the run this file covers begins at 0x10013fa0
// and the first three things in it are this object's dynamic initialiser
// (0x10013fa1 calls the constructor at 0x10013fab and then registers the
// destructor thunk at 0x10013fe9, which tail-jumps to 0x100140ad) and the
// list lock's (0x10013ffd / 0x10014013). A global with a constructor is what
// puts them there, in this declaration order, and it is also what makes VC6
// emit the vftable that holds the two forwarders above.
//
// The constructor's bytes are the ordinary C++ order and confirm the base:
// the base vftable goes in first (0x1004d260, CBankBase's), then the embedded
// lock is constructed, then this class's own vftable (0x1004d7d8), then the
// body's single sb_Init2. The destructor unwinds exactly that.
// 0x100140ad — hand the chunks back and let the embedded lock and the base
// take themselves down. A standalone body at /O1 (the atexit thunk jumps to
// it and the compiler's deleting destructor at 0x1001415d calls it), but
// `inline`, and defined above the global: lithtech.exe's atexit thunk
// (0x0043c440) carries the whole teardown at the global's own addresses --
// the vptr store, sb_FreeChunks, the lock's destructor, the base vptr --
// with no `this` register and no destructor of its own.
inline CDepLinkBank::~CDepLinkBank()
{
	sb_FreeChunks(&m_Bank, 1);
}

CDepLinkBank    g_DepLinkBank;               // 0x100585d8
LCriticalSection g_DepLinkCS;                // 0x100585c0

// ---- end of the separable addition ----

// 0x1002446d — take one link back out of the list. The cursor is a pointer
// *to* the link pointer, which is what makes the head need no special case
// (`this` is the first cursor, at offset zero) and what makes the walk
// advance into the node with `lea 0x8(%eax),%ecx` instead of keeping a
// previous-node register.
void CDepList::RemoveLink(void* pLink)
{
	CDepLink** ppCur = &m_pHead;
	CDepLink*  pCur;

	while ((pCur = *ppCur) != LTNULL)
	{
		if (pCur == (CDepLink*)pLink)
		{
			*ppCur = pCur->m_pNext;
			return;
		}

		ppCur = &pCur->m_pNext;
	}
}

// 0x1001404e — a link with its two payload words filled in. The record comes
// back already zeroed, so the next pointer needs no store here; an allocation
// that fails is returned as it is, which is why the null test guards only the
// two stores and not the return.
//
// This and dl_AddLink sit in a contiguous run of their own in the shipped
// image (0x10013fa0-0x10014107, the list module) rather than beside the se_
// family — they are here because these are the files this reconstruction
// owns, and the byte comparison scans the whole image regardless of which object a
// function is compiled into.
void* dl_MakeLink(void* pRecord, void* pData)
{
	CDepLink* pLink = g_DepLinkBank.Allocate();

	if (pLink)
	{
		pLink->m_pRecord = pRecord;
		pLink->m_pData   = pData;
	}

	return pLink;
}

// 0x1001401d — the callback of the second dependency record (0x10052df8):
// take the link back out of its list and, if the fourth argument says so,
// give the record back to the bank. Name ours, and it is what the bytes do
// rather than a guess at intent.
//
// The signature is four wide, which is the evidence serverengine.h's
// CDepRecord comment refers to: the gate is the third argument
// (`cmpl $0x0,0x10(%ebp)` before anything else is read), the first is the
// list — it goes straight into %ecx as a `this` — the second is the link,
// and the fourth is a pointer whose first dword decides whether the link is
// freed. se_ModelHashCleanup, reached the same way off the other record,
// reads only the second and third, so one declaration serves both and the
// extra parameter costs nothing there.
//
// Unlike every other function in this file the shipped code carries a real
// `push ebp; mov esp,ebp` frame with no locals at all — the argument pushes
// happen between the two uses of each argument and VC6 declined FPO for it.
LTRESULT dl_UnlinkAndFree(void* pList, CDepLink* pLink, int nArg3, int* pnFreeIt)
{
	if (nArg3 == 0)
	{
		((CDepList*)pList)->RemoveLink(pLink);

		if (pnFreeIt && *pnFreeIt)
			g_DepLinkBank.Free(pLink);
	}

	return LT_OK;
}

// 0x1001406a — put a link on a list, under the list lock. The list is one
// pointer and nothing else, so this is a head insertion; see serverengine.h
// on the name. A null link is refused rather than crashing, and that refusal
// is the only thing the return value ever says.
LTRESULT dl_AddLink(void* pList, void* pLink)
{
	if (pLink)
	{
		g_DepLinkCS.Enter();

		((CDepLink*)pLink)->m_pNext = *(CDepLink**)pList;
		*(CDepLink**)pList = (CDepLink*)pLink;

		g_DepLinkCS.Leave();

		return LT_OK;
	}

	return LT_ERROR;
}


// 0x1002bd75 — a stream on the file a file record names. The record already
// knows which mounted tree it came out of, so nothing is searched: the tree
// is followed through +0x10 and the name is asked of the file manager, and
// the two are handed to the tree's own opener. The third argument is a flag
// both this and fs_OpenFile pass zero for.
//
// Both cdecl cleanups batch into the one `add $0x14,%esp`, which is what says
// both callees have fixed prototypes.
//
// Like dl_MakeLink above, the shipped address sits in the file manager's run
// (0x1002bc12-0x1002bfcd) rather than beside the se_ family at 0x1002af75;
// it is here because these are the files this reconstruction owns.



// A sprite that will not load is not fatal: the engine has one of its own to
// put there instead, and only says so once. Failing that there is nothing to
// draw and the object cannot be made.
LTRESULT se_InitSprite(CServerMgr* pMgr, LTObject* pObj,
                       ObjectCreateStruct* pStruct)
{
	char* pFilename = pStruct->m_Filename;

	static_cast<CObjSprite*>(pObj)->m_ClipPoly = 0xffffffff;

	if (!se_LoadFile(&pMgr->m_FileMgr, pFilename, 0, &pObj->m_pSData->m_pModelFile))
	{
		if (g_DebugLevel >= 1)
			dsi_ServerError("Couldn't find sprite file %s, using sprites/default.spr.",
			                pFilename);

		if (!se_LoadFile(&pMgr->m_FileMgr, "sprites\\default.spr", 0,
		                 &pObj->m_pSData->m_pModelFile))
		{
			RETURN_ERROR_SYM_DESC(1, "se_InitSprite", LT_MISSINGFILE,
			                      "sprites\\default.spr");
		}
	}

	return LT_OK;
}


// Instances release their owned model after clearing every tracker.
LTRESULT se_TermModel(CServerMgr*, LTObject* object) {
    if (!object || object->m_ObjectType != OT_MODEL) return LT_INVALIDPARAMS;
    static_cast<ModelInstance*>(object)->BindModel({},false,false);
    return LT_OK;
}


// A container is a world model, and is made the same way; there is nothing
// of its own to set up here.
LTRESULT se_InitContainer(CServerMgr* pMgr, LTObject* pObj,
                          ObjectCreateStruct* pStruct)
{
	return se_InitWorldModel(pMgr, pObj, pStruct);
}

// Paths arrive written either way round and the file manager only knows one
// of them, so every name is folded to backslashes before it is looked up.
// The walk is destructive — the caller's own buffer is fixed in place — and
// what comes back is where it started (name ours).
char* se_FixSlashes(char* pPath)
{
	char* pRet;

	if (!pPath)
		return LTNULL;

	pRet = pPath;

	while (*pPath)
	{
		if (*pPath == '/')
			*pPath = '\\';

		pPath++;
	}

	return pRet;
}

// A world model is geometry that came in with the level rather than a file of
// its own, so this is a lookup and not a load: the create struct's filename
// is the name it went in under.
//
// Once it is attached, the object is given the geometry's own idea of where
// it is — the BSP's centre — and sized to half the span of its box, which is
// what a half-extent is. A world model whose BSP reports itself busy is left
// alone rather than re-placed.
LTRESULT se_InitWorldModel(CServerMgr* pMgr, LTObject* pObj,
                           ObjectCreateStruct* pStruct)
{
	WMMoveState state;
	LTVector    vHalfDims;
	CWorldBsp*  pBsp;

	state.m_pContainer  = LTNULL;
	state.m_pUnknown04  = LTNULL;
	state.m_pObj        = LTNULL;
	state.m_nUnknown64  = 0;

	if (pMgr->m_World.AttachWorldModel(pObj, pStruct->m_Filename))
	{
		// POSITIVE GUARD. 0x0046b330's `jne 0x0046b46d` sends the BUSY case
		// forward past the whole body to the single `xor %eax,%eax` exit, so
		// `return LT_OK` is a trailing statement and the test is negated.
		// Written as an early `if (IsBusy()) return LT_OK;` the epilogue is
		// duplicated -- worth 8 bytes in lithtech.exe's /O2 build, and
		// invisible in server.dll's /O1 build, which tail-merges the two.
		if (!pObj->m_pWorldBsp->IsBusy())
		{
		pBsp = (CWorldBsp*)pObj->m_pWorldBsp;

		vHalfDims = (pBsp->m_vBoxMax - pBsp->m_vBoxMin) * 0.5f;

		pObj->SetPos(pBsp->m_ve8);

		state.m_pContainer = &pMgr->m_Unknown210;
		state.m_pUnknown04 = pMgr->m_pUnknown6a8;
		state.m_pObj       = pObj;
		state.m_nUnknown0C = pObj->m_BlockingPriority;
		state.m_pPushList  = LTNULL;
		state.m_nPushCount = 0;

		wm_SetObjectDims(&state, &vHalfDims, 0, 1);
		}

		return LT_OK;
	}

	RETURN_ERROR(1, "se_InitWorldModel", LT_MISSINGWORLDMODEL);
}


// Resolve a resident model or reserve an owned worker ticket.
LTRESULT se_LoadModel(CServerMgr* manager, char* filename, Model** output,
                      void** file_output, int want_load, int load_here) {
    if (output) *output = nullptr;
    if (file_output) *file_output = nullptr;
    if (!manager || !filename || !output || !file_output) return LT_INVALIDPARAMS;
    try {
        auto key = nolf::platform::asset_key(filename);
        if (!se_LoadFile(&manager->m_FileMgr,key.data(),0,file_output)) return LT_MISSINGFILE;
        if (auto model = manager->m_Models.Find(key)) { *output = model.get(); return LT_OK; }
        if (!want_load) return LT_INPROGRESS;
        if (!load_here) {
            if (manager->m_Models.HasPending(key)) return LT_INPROGRESS;
            const auto ticket = manager->m_Models.Reserve(key);
            LThreadMessage request;
            request.m_Data[3] = reinterpret_cast<uintptr_t>(*file_output);
            request.m_Data[5] = ticket;
            const auto posted = manager->m_Thread.m_IncomingQueue.PostMessage(&request);
            if (posted != LT_OK) { manager->m_Models.Discard(ticket); return posted; }
            return LT_INPROGRESS;
        }
        ModelService::Ticket ticket = 0;
        const auto status = se_LoadModelData(manager,key.data(),*file_output,output,&ticket);
        LThreadMessage done;
        done.m_Data[0] = status;
        done.m_Data[3] = reinterpret_cast<uintptr_t>(*file_output);
        done.m_Data[5] = ticket;
        manager->OnModelLoaded(&done);
        auto model = manager->m_Models.Find(key);
        *output = model.get();
        return done.m_Data[0] != LT_OK ? static_cast<LTRESULT>(done.m_Data[0]) : model ? LT_OK : LT_NOTFOUND;
    } catch (const std::bad_alloc&) { return LT_OUTOFMEMORY; }
      catch (const std::exception& error) { dsi_ServerError("Model load failed: %s",error.what()); return LT_INVALIDMODELFILE; }
}

LTRESULT sm_LoadModel(CServerMgr* manager, char* filename, Model** output,
                      CFileIdentifier** file_output, int want_load, int load_here) {
    if (!file_output) return LT_INVALIDPARAMS;
    void* file = nullptr;
    const auto status = se_LoadModel(manager,filename,output,&file,want_load,load_here);
    *file_output = static_cast<CFileIdentifier*>(file);
    return status;
}


// Stage file identifiers and bind the complete model or pending placeholder.
LTRESULT se_InitModel(CServerMgr* manager, LTObject* object, ObjectCreateStruct* create) {
    if (!manager || !object || object->m_ObjectType != OT_MODEL || !object->m_pSData || !create) return LT_INVALIDPARAMS;
    auto* instance = static_cast<ModelInstance*>(object);
    char* filename = create->m_Filename;
    void* model_file = nullptr;
    std::array<void*,MAX_MODEL_TEXTURES> skins{};
    if (!se_LoadFile(&manager->m_FileMgr,filename,0,&model_file)) {
        filename = const_cast<char*>("models/default.abc");
        if (!se_LoadFile(&manager->m_FileMgr,filename,0,&model_file)) return LT_MISSINGFILE;
    }
    for (uint32 i = 0; i < MAX_MODEL_TEXTURES; ++i) {
        auto& file = skins[i];
        file = nullptr;
        if (create->m_SkinNames[i][0] && !se_LoadFile(&manager->m_FileMgr,create->m_SkinNames[i],0,&file))
            se_LoadFile(&manager->m_FileMgr,const_cast<char*>("skins/default.dtx"),0,&file);
    }
    Model* model = nullptr;
    void* file = nullptr;
    const auto autoload = create->m_CreateFlags & OCS_AUTOLOAD;
    const auto status = se_LoadModel(manager,filename,&model,&file,autoload,autoload);
    if (status == LT_OK) instance->BindModel(manager->m_Models.Retain(model),false,false);
    else if (status == LT_INPROGRESS) {
        instance->BindModel(manager->m_PlaceholderModelOwner,true,false);
        static_cast<CFileRef*>(model_file)->m_Flags |= 1;
    } else return status;
    object->m_pSData->m_pModelFile = model_file;
    for (uint32 i = 0; i < MAX_MODEL_TEXTURES; ++i) object->m_pSData->m_SkinFiles[i] = skins[i];
    instance->m_AnimTracker.m_Flags |= 4;
    instance->m_AnimTracker.m_pUser1 = instance;
    return LT_OK;
}


// Decode a reserved result without publishing it from the worker.
LTRESULT se_LoadModelData(CServerMgr* manager, char* filename, void*, Model** output,
                          ModelService::Ticket* ticket) {
    if (output) *output = nullptr;
    if (!manager || !filename || !output || !ticket) return LT_INVALIDPARAMS;
    try {
        const auto load = *ticket ? manager->m_Models.LoadReserved(*ticket) : manager->m_Models.BeginLoad(filename);
        *ticket = load.ticket;
        *output = load.model.get();
        return load.status;
    } catch (const std::bad_alloc&) { return LT_OUTOFMEMORY; }
      catch (const std::exception& error) { dsi_ServerError("Model load failed: %s",error.what()); return LT_INVALIDMODELFILE; }
}


// Publish only owned load tickets. Cancelled or stale completion messages
// cannot resurrect a model, including after service termination/restart.
void CServerMgr::OnModelLoaded(LThreadMessage* message) {
    ServerActivity activity(m_Lifetime);
    if (!activity.available() || m_Terminating) return;
    ServerCallbackGuard operation(m_Lifetime);
    if (!message || message->m_Data[2] != 0) return;
    auto* file = reinterpret_cast<CFileRef*>(message->m_Data[3]);
    auto status = static_cast<LTRESULT>(message->m_Data[0]);
    std::shared_ptr<Model> model;
    if (message->m_Data[5]) {
        ModelService::Completion completed;
        try { completed = m_Models.Finish(message->m_Data[5]); }
        catch (const std::bad_alloc&) { message->m_Data[0] = LT_INPROGRESS; return; }
        if (!completed.accepted) return;
        model = std::move(completed.model);
        status = completed.status;
    }
    if (status == LT_OK) {
        if (!model || !file) status = LT_INVALIDPARAMS;
        else status = se_RegisterModel(this,model.get(),file,1);
        if (!operation.unchanged()) return;
    }
    message->m_Data[0] = status;
    if (status != LT_OK && model) m_Models.Unload(model->GetFilename());
    if (file && m_ClassMgr.m_pServerShell) {
        const std::string name = file->m_Name;
        m_ClassMgr.shell([&](IServerShell& shell) { return shell.FileLoadNotify(name.c_str(),status); });
    }
}

// THE SHARED SERVER SOURCE HAS A CLIENT-ONLY STATEMENT, and se_RegisterModel
// below is where it is. lithtech.exe's copy of this function (0x0046ad10) ends
//
//     push %esi ; call 0x00414930 ; add $0x4,%esp
//
// immediately before its `xor %eax,%eax ; test %ebp,%ebp ; sete %al`, with
// %esi holding pModel. server.dll's copy (0x1002b41b, which our source matches
// byte for byte) has nothing there. 0x00414930 is client code —
// src/engine/lithtech/csvis.cpp already reconstructs it as
// cs_InheritFileFromServer, named for the one string it raises, "Client
// inherited model %s from server!", and that file's own note lists 0x0046add3
// as one of its two call sites. So the shipped serverengine.cpp really does
// call into the client when a client is present, under some preprocessor
// guard, and server.dll is simply the build where that guard is false.
//
// THE MACRO'S REAL NAME IS NOT KNOWN. build/engine.sh now defines
// LT_SERVER_BUILD for the server target only, purely as a named
// discriminator for cases like this one -- do NOT overload
// LT_SERVER_TIMERS, which means something narrower (the scoped
// CountAdder the client genuinely lacks). The original symbol
// build/engine.sh defines for server.dll and not for lithtech.exe (worldsim.cpp
// and four other files already turn on server-only code with it), so its
// negation is used here as the stand-in for "a client is in this process".
// If a properly named discriminator is ever added to build/engine.sh, this is
// the site to move onto it.
//
// The symbol below cannot be linked against csvis.cpp's (the mangling differs
// and the argument class is spelled there, not here), the byte comparison masks every
// relocated byte and the whole link joins the two through link/aliases.rsp,
// so the declaration only has to give the right calling
// convention and argument count.
#ifndef LT_SERVER_BUILD
void cs_InheritFileFromServer(Model* pModel);      // 0x00414930 (client only)
#endif


// Index published ownership and rebind waiting instances through native types.
LTRESULT se_RegisterModel(CServerMgr* manager, Model* model, void* file_data, int rebind) {
    if (!manager || !model || !file_data || !manager->m_hModelHash) return LT_INVALIDPARAMS;
    auto owner = manager->m_Models.Retain(model);
    if (!owner) return LT_NOTFOUND;
    auto* file = static_cast<CFileRef*>(file_data);
    const auto* name = model->GetFilename();
    auto* element = dhash_FindElement(manager->m_hModelHash,name,static_cast<uint32>(strlen(name)));
    if (!element) element = dhash_AddElement(manager->m_hModelHash,name,static_cast<uint32>(strlen(name)));
    if (!element) return LT_OUTOFMEMORY;
    dhash_SetData(element,model);
    if (rebind && (file->m_Flags & 1)) {
        auto* head = manager->ObjectListHead();
        for (auto* link = head->m_pNext; link != head; link = link->m_pNext) {
            auto* instance = static_cast<ModelInstance*>(link->m_pData);
            if (instance->m_pSData && instance->m_pSData->m_pModelFile == file)
                instance->BindModel(owner,false,true);
        }
        file->m_Flags &= static_cast<uint16>(~1u);
    }
    return LT_OK;
}


// 0x1002bde9 — walk the manager's mounted trees looking for a named file,
// answering nonzero when one has it. Two out-parameters: the record of the
// tree it was found in, and one dword the tree's own lookup reported about
// it (0x10006fb4 fills a 0x110-byte record and this is the dword at its
// +0x108). Both are optional. Its first parameter is the file manager,
// whose own first field is the tree list, so the callee reads it as a list
// head (name ours; the function lives in the file manager's run).
//
// Defined in smgrmisc.cpp under that file's views of the two records
// (0x1002bde9 is its placed address), so it is declared with them here and
// the call below casts to them.


// Cancel pending publication and detach instances before releasing the old model.
LTRESULT sm_UnloadModel(CServerMgr* manager, char* filename, CFileIdentifier* file) {
    if (!manager || !filename) return LT_INVALIDPARAMS;
    const auto key = nolf::platform::asset_key(filename);
    const bool pending = manager->m_Models.HasPending(key);
    auto old = manager->m_Models.Unload(key);
    auto* element = dhash_FindElement(manager->m_hModelHash,key.c_str(),static_cast<uint32>(key.size()));
    if (element) dhash_Remove(manager->m_hModelHash,element);
    if (!old && !pending && !element) return LT_NOTFOUND;
    auto* head = manager->ObjectListHead();
    for (auto* link = head->m_pNext; link != head; link = link->m_pNext) {
        auto* instance = static_cast<ModelInstance*>(link->m_pData);
        if ((old && instance->m_ModelOwner == old) || (file && instance->m_pSData && instance->m_pSData->m_pModelFile == file)) {
            instance->BindModel(manager->m_PlaceholderModelOwner,true,true);
            if (file) file->m_Flags |= 1;
        }
    }
    return LT_OK;
}



// 0x10054028 — the per-object-type hooks (servermgr.h's ObjTypeHandlers,
// indexed by LTObject::m_ObjectType), eleven pairs of { create, remove }.
// smgr.cpp's w_ApplyObjectFilenames (0x1002ba90) reads the first word of an
// entry (`mov 0x10054028(,%eax,8)`) and sm_RemoveFromWorldTree (0x1002bab8)
// the second (0x1005402c); both follow se_InitContainer in .text, and every
// hook in the table is one of this file's se_* functions, so the table is
// this file's. The words are the shipped .data: model 0x1002b6a4/0x1002b870,
// world model 0x1002b92e, sprite 0x1002b88b, container 0x1002ba7b, every
// other type (normal, light, camera, particles, poly grid, lines, canvas)
// nothing.
ObjTypeHandlers g_ObjTypeHandlers[11] =
{
	{ LTNULL,            LTNULL       },     // OT_NORMAL
	{ se_InitModel,      se_TermModel },     // OT_MODEL
	{ se_InitWorldModel, LTNULL       },     // OT_WORLDMODEL
	{ se_InitSprite,     LTNULL       },     // OT_SPRITE
	{ LTNULL,            LTNULL       },     // OT_LIGHT
	{ LTNULL,            LTNULL       },     // OT_CAMERA
	{ LTNULL,            LTNULL       },     // OT_PARTICLESYSTEM
	{ LTNULL,            LTNULL       },     // OT_POLYGRID
	{ LTNULL,            LTNULL       },     // OT_LINESYSTEM
	{ se_InitContainer,  LTNULL       },     // OT_CONTAINER
	{ LTNULL,            LTNULL       },     // OT_CANVAS
};

void se_NotifyFileRegistered(CServerMgr* manager, const CFileRef& file) {
    if (!manager) return;
    const auto* head = &manager->m_ClientList.m_Head;
    for (auto* current = head->m_pNext; current != head; current = current->m_pNext) {
        auto* client = static_cast<Client*>(current->m_pData);
        xfer_AddFile(client->m_FileTransfer.get(),
                     const_cast<char*>(file.m_Name.c_str()), file.m_Unknown04, file.m_nFileID, file.m_Flags);
    }
}
