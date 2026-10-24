// *********************************************************************** //
//  MODULE:   SERVERENGINE (.H)
//  PURPOSE:  The se_ family at 0x1002af75-0x1002bab8: giving an object the
//            file it is made of. Every one of them is named by its own error
//            tag — se_InitModel, se_InitSprite, se_InitWorldModel,
//            se_LoadModel, se_LoadModelData, se_LoadChildModel — so the names
//            here are the engine's, not ours.
//
//            The shape is the same throughout: ask the file manager for the
//            file the create struct names, and where there is a sensible
//            stand-in (a default model, a default sprite) fall back to it
//            with a warning rather than failing outright.
// *********************************************************************** //
#ifndef __SERVERENGINE_H__
#define __SERVERENGINE_H__

#include "servermgr.h"
#include "ltobject_internal.h"
#include "de_error.h"
#include "commonmodel.h"
#include "objtypes.h"

#include "model_loading.h"

// What a file record's +0x10 points at: the record of the mounted tree the
// file came out of. Only one field is reached from here, and the same field
// is reached the same way from fs_OpenFile's tree walk (0x1002bd37), where
// the pointer comes off the manager's tree list instead — which is what says
// the two are the same kind of record (names ours).


// 0x100070e3 — open a stream on a named file inside a mounted tree. It picks
// the stream kind from the tree's own type word: a directory on disk gets a
// CTreeStream out of g_TreeStreamBank and prints "stream %p open file %s
// size = %u", a .rez member gets a CMemStream out of g_MemStreamBank and
// prints "stream %p open rez %s size = %u". Lives in another file's run
// (name ours).


// 0x1002bd75 — a stream over an already-open file (name ours).
class ILTStream* se_OpenStream(class CFileMgr* pFileMgr, void* pFile);

// 0x100074df — drain a stream into a named file on disk. Opens the file with
// the mode string at 0x100518a0, seeks the stream back to zero, asks it its
// length and copies it in 0x4000-byte blocks out of a temporary allocation,
// answering 1 only when every byte arrived (its own `sete` compares the
// running offset against the length). Lives in another file's run; declared
// here because fs_CopyFile is the only caller reconstructed so far (name
// ours).
int fs_CopyStreamToFile(class ILTStream* pStream, const char* pszDestFile);

// 0x1002bd97 — copy a file out of whichever mounted tree has it. The signature
// is servermgr.h's.
LTRESULT fs_CopyFile(class CFileMgr* pFileMgr, const char* pszSourceFile,
                     const char* pszDestFile);

// 0x1002b41b — announce a model that just finished loading and put it in the
// manager's hash. The last argument is a flag: se_LoadChildModel passes zero
// and CServerMgr::OnModelLoaded passes one (name ours).
LTRESULT se_RegisterModel(CServerMgr* pMgr, class Model* pModel, void* pFile,
                          int nFlag);

// A model keeps a list of things to run when it goes, and the static record
// at 0x10054020 is the one that takes it back out of the manager's hash.
//
// 0x10054020 — the record: a callback, a pointer to a second record of the
// same shape at 0x10052df8 (whose own callback is 0x1001401d), and two zero
// words. Nothing in .text reads the record — the only reference to it in the
// whole image is the dl_MakeLink call in se_LoadChildModel — so the runner
// that eventually calls the callback is not in the reconstructed region and
// the fields past the callback have no evidence behind their names.
//
// The callback's third argument is the gate (0x1002af75 does nothing unless
// it is zero) and its first is never read. 0x1001401d, reached the same way
// off the other record, reads a *fourth* argument, so the shared type is
// probably four wide with this one ignoring the last; both spellings compile
// to the same bytes here, so the three the code actually reads are what is
// declared.
class CDepRecord
{
public:
	LTRESULT (*m_pfnRun)(void* pArg1, class CDepLink* pLink, int nArg3);  // 0x00
	void*  m_pUnknown04;                     // 0x04
	uint32 m_Unknown08;                      // 0x08
	uint32 m_Unknown0c;                      // 0x0c
};



// One link: what to run, what to hand it, and the next one. Twelve bytes,
// and the bank's own initialiser says so — 0x10013fab calls sb_Init2 with a
// record size of 0xc (names ours).
class CDepLink
{
public:
	void*     m_pRecord;                     // 0x00
	void*     m_pData;                       // 0x04
	CDepLink* m_pNext;                       // 0x08
};

void* dl_MakeLink(void* pRecord, void* pData);      // 0x1001404e
// The name is read off the bytes and disagrees with the obvious guess: the
// new link's next is set to whatever the list's single pointer held and the
// pointer is then aimed at the new link, so this **prepends**. There is no
// tail pointer anywhere in the twelve-byte record for it to append to.
LTRESULT dl_AddLink(void* pList, void* pLink);      // 0x1001406a

// 0x1000d2ef — the engine's raw allocate. NOT 0x1000d25d, which this used
// to say: that address is the eleven-byte `operator new` forwarder in front
// of it (`push 0x4(%esp); call 0x1000d2ef; pop ecx; ret`). The two are
// indistinguishable to a masked comparison and only the call
// targets separate them.
void* dalloc(uint32 nBytes);

// 0x10058c7c is the collection allocator — collection.h's g_DefAlloc
// (stdlith's l_allocator.cpp defines it); a model loaded from file is built
// with it on both sides.

// 0x1002afcf — the callback the reader uses for each child model a file
// names (name ours).


// 0x1002b22c — turn an open model file into a Model (name ours).
LTRESULT se_LoadModelData(CServerMgr* pMgr, char* pFilename, void* pFile,
                          class Model** ppModel, ModelService::Ticket* ticket);

// 0x10032dcd — the manager's own hook for a model that finished loading here
// rather than on the worker thread (name ours).

// 0x1002be8c — load a sprite through the file manager, into the caller's
// pointer. Nonzero means it came back. What it fills is the same slot of the
// object data a model's file goes in — an object is made of one file and the
// slot does not care which kind (name ours).
int se_LoadFile(class CFileMgr* pFileMgr, char* pFilename, uint16 nFlags,
                void** ppFile);

// A sprite object keeps the frame it is showing past the end of LTObject; an
// uninitialised one is all-ones rather than zero, so "no frame yet" and
// "frame zero" stay apart (names ours).
// What the object data's file slot points at. Every field is pinned by
// se_LoadFile, which is the only thing that ever fills one in: the record is
// 0x14 bytes (CFileMgr's constructor sb_Init's the bank for 0x14) and
// se_LoadFile writes all five of them (names ours).


// A model instance holds the model it was made from, and the model counts
// the instances that hold it — commonmodel.h has Model, including the count
// at 0x208 (name ours).
LTRESULT se_InitWorldModel(CServerMgr* pMgr, LTObject* pObj,
                           ObjectCreateStruct* pStruct);   // 0x1002b92e

// 0x1002b4dc — a model by name: the one already loaded if the hash has it,
// otherwise the file, loaded here or queued for the worker thread.
LTRESULT se_LoadModel(CServerMgr* pMgr, char* pFilename, Model** ppModel,
                      void** ppFile, int bWantLoad, int bLoadHere);

void se_NotifyFileRegistered(CServerMgr*, const CFileRef&);

#endif  // __SERVERENGINE_H__
