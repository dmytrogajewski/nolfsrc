// *********************************************************************** //
//
//  MODULE:   LTHREAD (.H)
//
//  PURPOSE:  LThread / LThreadQueue — the engine's worker-thread wrapper and
//            its message queues, at 0x10015587-0x100159ed. Both class names
//            are attested by check literals ("LThread::Start",
//            "LThreadQueue::Init", "LThreadQueue::PostMessage").
//
//            An LThread owns two queues (incoming at +0x04, outgoing at
//            +0x2c). A queue is a critical section, a small polymorphic
//            message list, and a manual-reset event that wakes the thread.
//            Messages are fixed 32-byte records drawn from one global bank
//            under one global lock.
//
// *********************************************************************** //

#ifndef __LTHREAD_H__
#define __LTHREAD_H__

#include "platform/sync.h"
#include <atomic>
#include <thread>
#include "ltbasedefs.h"
// GenList<> and GenListPos: the message queues below implement that
// interface, which is what their twelve-slot vtables are.
#include "collection.h"

// windows.h renames these methods out from under us otherwise.
#undef PostMessage
#undef GetMessage
#undef PeekMessage
#undef CopyFile
#undef PlaySound
#undef GetClassName


// Thin wrapper over a CRITICAL_SECTION (it is the class's only member, which
// is why Enter and Leave compile to `push ecx`). IsValid answers LTTRUE
// unconditionally in this build (0x100355db).
class LCriticalSection
{
public:
	LCriticalSection();                      // 0x10015587
	~LCriticalSection();                     // 0x100155a2

	void   Enter();                          // 0x100155aa
	void   Leave();                          // 0x100155b2
	LTBOOL IsValid();
    void lock() { Enter(); }
    void unlock() { Leave(); }                        // 0x100355db

public:
	nolf::platform::RecursiveMutex m_CS;
};


// One queued message: eight dwords, copied whole (`rep movsd` with ecx=8 in
// PostMessage). The field breakdown is not visible from here.
class LThreadMessage
{
public:
	LThreadMessage();                        // 0x100155ba — only the first
	                                         // two payload dwords are cleared

	// The list threads through the messages themselves; the payload follows.
	LThreadMessage* m_pNext{};                 // +0x00
	LThreadMessage* m_pPrev{};                 // +0x04
	uintptr_t       m_Data[6]{};               // +0x08
};


// The bank the messages live in (one global instance at 0x10058700) and the
// lock that guards it (0x100586e8). Its layout is the same vptr-over-
// StructBank shape as CMessageBank and CNetBank, so it is declared alongside
// them in servermgr.h — Allocate reaches the bank at this+4.
class LThreadMessageBank;

extern LThreadMessageBank g_ThreadMessageBank;
extern LCriticalSection   g_ThreadCS;

// 0x10015b65 — hand every message in the list back to the bank.



// The queue's message list. Polymorphic — LThreadQueue's constructor stores
// its vtable pointer inline at +0x18.
// 0x1003e050 — walk a circular list of these records from one node, through
// +0x04, comparing nBytes of each against a value; the matching node or null.
// Not a CRT function despite living up in that range, and the `repz cmpsb` is
// the memcmp collection.h's GenFindElement comment promises (name ours).
void* tm_FindInList(void* pStart, const void* pValue, uint32 nBytes);


// Ten of the twelve GenList<> slots have ONE body between every message
// queue in the image -- four vtables (0x1004d8ec, 0x1004dcfc, 0x1004dd2c and
// 0x1004dd5c) name the same ten addresses -- so one base implements them and
// each queue overrides only GenAppend and GenAppendList. /OPT:ICF is off in
// server.dll, so that sharing is a shared BASE, not four bodies the linker
// folded; and the two it does not share sit at a different address per queue,
// which is what says they are per-class members (name ours).
//
// There is no virtual destructor: slot 0 of every one of those vtables is
// GenBegin, so the shipped classes never had one.
class CMessageQueueBase : public GenList<LThreadMessage*>
{
public:
	// Empty: each concrete queue zeroes the two fields in its OWN constructor.
	// NetConnection's constructor (0x10021200) stores each queue's vptr
	// BEFORE its zeros; a base that zeroed would run first and put the zeros
	// ahead of the derived vptr store.
	CMessageQueueBase() {}

	// 0x10023ea9 — the tail, as a position. The one-argument GenListPos
	// constructor, exactly as CMoArray::GenBegin (0x1000ba8f) builds its
	// `GenListPos((uint32)0)`: the shipped body stores m_pTail through the
	// return pointer and copies the temporary's UNINITIALISED m_SubIndex out
	// of an eight-byte frame (`push %ecx` twice is the reservation, and
	// `mov -0x4(%ebp),%ecx` reads it). No `this` is stored anywhere; the old
	// reading of that frame as a spill was wrong. The old body (a named
	// local, plus `this` stored into its m_SubIndex) came out 15 bytes with
	// no frame at all.
	virtual GenListPos GenBegin() const
	{
		return GenListPos((void*)m_pTail);
	}

	// 0x100240cd
	virtual LTBOOL GenIsValid(const GenListPos& pos) const
	{
		return pos.m_Pos != LTNULL;
	}

	// 0x10023f55 — hand out the node the position is on and step BACKWARDS
	// through +0x04, stopping when the step lands on the tail again. The
	// branchless `neg`/`sbb`/`and` is the `?:`, not an if.
	virtual LThreadMessage* GenGetNext(GenListPos& pos) const
	{
		LThreadMessage* pCur  = (LThreadMessage*)pos.m_Pos;
		LThreadMessage* pNext = pCur->m_pPrev;

		pos.m_Pos = (pNext == m_pTail) ? LTNULL : pNext;

		return pCur;
	}

	// 0x10023ec0
	virtual LThreadMessage* GenGetAt(GenListPos& pos) const
	{
		return (LThreadMessage*)pos.m_Pos;
	}

	// 0x100240ec — by value, hence the `ret $0x8`, and only the node half of
	// the position is passed on.
	virtual void GenRemoveAt(GenListPos pos)
	{
		Remove((LThreadMessage*)pos.m_Pos);
	}

	// 0x10023eda — the tail first, then the count. Nothing is handed back to
	// the bank: this forgets the list, it does not free it.
	virtual void GenRemoveAll()
	{
		m_pTail     = LTNULL;
		m_nMessages = 0;
	}

	// 0x10023f83
	virtual uint32 GenGetSize() const { return m_nMessages; }

	// 0x10015aa8 — GenRemoveAll is expanded here rather than called, and the
	// append goes through the vtable, which is what makes this one body serve
	// every queue.
	virtual LTBOOL GenCopyList(const GenList<LThreadMessage*>& other)
	{
		// GenRemoveAll's body written out, not called: it is virtual, so the
		// call would go through the vtable and the shipped code has the two
		// stores inline.
		m_pTail     = LTNULL;
		m_nMessages = 0;

		return GenAppendList(other);
	}

	// 0x10023f32
	virtual LTBOOL GenFindElement(LThreadMessage* const& toFind,
	                              GenListPos& thePos) const
	{
		thePos.m_Pos = tm_FindInList(m_pTail, &toFind,
		                             sizeof(LThreadMessage*));

		return thePos.m_Pos != LTNULL;
	}



	// The two splices are inline: the net manager's ordered insert carries a
	// copy of InsertAfter rather than calling anything, and Append is that
	// same copy with the tail as the anchor. The m_pTail test inside
	// InsertAfter is what makes it general — the splice does not otherwise
	// know whether it landed at the end (names ours).
	void InsertAfter(LThreadMessage* pAt, LThreadMessage* pMsg)
	{
		pAt->m_pNext->m_pPrev = pMsg;
		pMsg->m_pNext = pAt->m_pNext;
		pMsg->m_pPrev = pAt;
		pAt->m_pNext = pMsg;

		m_nMessages++;

		if (pMsg->m_pPrev == m_pTail)
			m_pTail = pMsg;
	}

	// lithtech 0x45a210, the one out-of-line copy (netmgr's SendPacketData
	// declines the expansion; src/engine/lithtech/nettrace.cpp emits it).
	// It RETURNS pMsg, as Append below does: the shipped copy keeps pMsg in
	// eax from its first load to the `ret` and counts in edx. As a void
	// function VC6 put pAt in eax and the count after it (36/36, an eax/edx
	// transposition). Every caller discards the result.
	LThreadMessage* InsertBefore(LThreadMessage* pAt, LThreadMessage* pMsg)
	{
		pAt->m_pPrev->m_pNext = pMsg;
		pMsg->m_pPrev = pAt->m_pPrev;
		pMsg->m_pNext = pAt;
		pAt->m_pPrev = pMsg;

		m_nMessages++;

		return pMsg;
	}

	// 0x100242e2 / lithtech 0x45a1c0. Splicing in behind the tail; the
	// m_pTail update is written as a test because the general splice does
	// not otherwise know it landed at the end.
	//
	// INLINE, and it has to be visible here rather than in a .cpp: lithtech
	// 0x41d550 (GenAppend), 0x43a03a (inside GenAppendList) and 0x444384
	// (inside PostMessage) all carry the empty-list arm of this body with no
	// call, and each of those reaches it through an expanded Prepend, so it
	// is expanded two levels deep. /Ob1 never expands a member defined
	// out-of-class, so the keyword was on the definition in the header.
	//
	// The out-of-line copy survives in BOTH images anyway, which is why this
	// is safe: at /Os every call declines (server.dll calls 0x100242e2 from
	// 0x10015af8 and 0x100234fb) and at /Ot three big netmgr callers still
	// decline (lithtech calls 0x45a1c0 from 0x457bce, 0x45840a and
	// 0x459227), so netmgr.obj keeps emitting the COMDAT.
	//
	// It RETURNS pMsg. Both images leave pMsg in eax at every `ret` of the
	// out-of-line copy, and lithtech 0x45a1fb loads the count into edx, not
	// eax, before the empty arm's tail store. As a void function VC6 reused
	// eax for the count and put the tail store first (72/72, 4 insns off).
	// Callers all discard the result, so the inline expansions are unchanged.
	LThreadMessage* Append(LThreadMessage* pMsg)
	{
		LThreadMessage* pAt = m_pTail;

		if (pAt)
		{
			InsertAfter(pAt, pMsg);
		}
		else
		{
			pMsg->m_pPrev = pMsg;
			pMsg->m_pNext = pMsg;

			// The tail BEFORE the count. lithtech 0x41d585 stores m_pTail
			// and only then loads m_nMessages, and 0x43a03f ends this arm
			// on the tail store so the increment merges with the splice
			// arm's; server 0x10024308 keeps the two tail stores apart
			// rather than cross-jumping them. The count-first order closed
			// none of the three (GenAppend, GenAppendList, Append).
			m_pTail = pMsg;
			m_nMessages++;
		}

		return pMsg;
	}

	// 0x10015ad0. Splice in front of whatever m_pTail names the head to be.
	// With no head to splice in front of there is nothing to do here, so the
	// empty case goes to Append, which knows how to start a list.
	//
	// INLINE, and lithtech.exe has no out-of-line copy at all to prove it:
	// nothing in the image calls a Prepend body, and GenAppend (0x41d550),
	// GenAppendList (0x439fe0), PostMessage (0x4442c0) and three netmgr
	// sites (0x457bae, 0x4583f9, 0x459206) each carry the `if (m_pTail)`
	// test with the splice on one arm and Append on the other. The last of
	// those three is the tell that the two halves are separate inline
	// functions rather than one written-out body: at 0x4583f9 the budget had
	// run out and BOTH arms are calls (`call 0x45a210` to InsertBefore,
	// `call 0x45a1c0` to Append) while the same source at 0x457bae expands
	// InsertBefore and only calls Append.
	//
	// It cannot be marked inline down in lthread.cpp: netmgr.cpp calls it
	// five times, and once /Ot expanded the last in-file call VC6 would emit
	// no COMDAT at all and netmgr.obj's externals would not resolve.
	//
	// server.dll is unaffected by the move — /Os declines the expansion at
	// this size, and the shipped image proves it: twelve call sites reach
	// 0x10015ad0, including GenAppend, GenAppendList, PostMessage and six in
	// netmgr's block.
	//
	// It RETURNS pMsg, passing on what InsertBefore and Append return, as
	// those two do. server.dll 0x10015ad0 holds pMsg in eax through the
	// splice and the splice point in edx; as a void function VC6 swapped
	// the two (48/48, an eax/edx transposition that naming the splice point
	// or the tail did not move). Callers all discard the result.
	LThreadMessage* Prepend(LThreadMessage* pMsg)
	{
		if (m_pTail)
			return InsertBefore(m_pTail->m_pNext, pMsg);
		else
			return Append(pMsg);
	}

	// 0x10015b00 (name ours). INLINE for the same reason and on the same
	// evidence: lithtech.exe contains exactly one instance of this unlink
	// with these offsets outside an expansion, and it is GenRemoveAt
	// (0x41d5a0, `ret $0x8`), whose whole body is this one instruction for
	// instruction; TakeMessage carries it a second time at 0x4444f8. There
	// is no `ret $0x4` copy for netmgr.cpp's six calls to reach, so the
	// definition was visible to netmgr.cpp — it was here.
	//
	// server.dll keeps the out-of-line copy and calls it from eight sites
	// (TakeMessage at 0x10015835, GenRemoveAt at 0x100240f0 and six in
	// netmgr's block), so /Os declines this one too and the move costs
	// server.dll nothing.
	void Remove(LThreadMessage* pMsg)
	{
		pMsg->m_pNext->m_pPrev = pMsg->m_pPrev;
		pMsg->m_pPrev->m_pNext = pMsg->m_pNext;

		if (pMsg == m_pTail)
			m_pTail = m_pTail->m_pPrev;

		m_nMessages--;
		if (m_nMessages == 0)
			m_pTail = LTNULL;
	}

public:
	// Not a head and a tail: the list is circular through the messages
	// themselves, so one pointer into it is enough and the other field
	// counts. Append increments it and Remove decrements it, which is what
	// says which is which.
	uint32          m_nMessages;             // +0x04
	LThreadMessage* m_pTail;                 // +0x08
};


class LMessageList : public CMessageQueueBase
{
public:
	// The zeroing lives here, not in the base: see CMessageQueueBase().
	LMessageList() { m_nMessages = 0; m_pTail = LTNULL; }

	// 0x10023ec9 — Prepend, not Append, and the answer is always LTTRUE.
	virtual LTBOOL GenAppend(LThreadMessage*& toAppend)
	{
		Prepend(toAppend);

		return LTTRUE;
	}

	// 0x10023ee3
	virtual LTBOOL GenAppendList(const GenList<LThreadMessage*>& other)
	{
		GenListPos pos = other.GenBegin();

		while (other.GenIsValid(pos))
			Prepend(other.GenGetNext(pos));

		return LTTRUE;
	}

};


// The thread queue's list is NOT an LMessageList: its vtable (0x1004d8ec)
// names LMessageList's GenAppendList at 0x10023ee3 but its own GenAppend at
// 0x10015a97, so it derives from LMessageList and overrides that one slot and
// nothing else. Two identical GenAppend bodies at two addresses is what says
// they are two classes -- COMDATs are deduplicated by name, so one class
// cannot have two (name ours).
class LThreadMsgList : public LMessageList
{
public:
	// 0x10015a97 — byte-for-byte the base's.
	virtual LTBOOL GenAppend(LThreadMessage*& toAppend)
	{
		Prepend(toAppend);

		return LTTRUE;
	}

	// ~LThreadQueue is where this is visible: 41 bytes in server.dll and 42
	// in lithtech against the 34 an empty member teardown costs, and the
	// extra seven are one `movl $0x1004d8ec,0x18(%esi)` — this class's own
	// vtable stored into the list at +0x18, which only a destructor emits.
	// The two stores that follow it are GenRemoveAll's, so the body is that,
	// and ~LThreadQueue keeps only the flush and the handle.
	//
	// It sits HERE rather than on CMessageQueueBase because only one table is
	// stored and it is the most derived one: a base-level destructor would
	// have the derived store dead-store-eliminated by the base's and leave
	// the wrong constant. Declaring it here also keeps LMessageList and
	// CNetSentQueue trivially destructible, so nothing in netmgr.cpp moves.
	//
	// Not virtual — slot 0 of all four queue vtables is GenBegin.
	~LThreadMsgList()
	{
		m_pTail     = LTNULL;
		m_nMessages = 0;
	}
};


class LThreadQueue
{
public:
	LThreadQueue();                          // 0x100155c5
	~LThreadQueue();                         // 0x100155ee

	LTRESULT Init();                         // 0x10015617
	void     Flush();                        // 0x10015671
	LTRESULT PostMessage(LThreadMessage* pMsg);  // 0x1001568f
	// 0x1001576a — take the next message off, copying it into the caller's
	// own storage rather than handing out the node. LT_OK when there was one,
	// so a drain loop runs while the answer is zero (name ours). The second
	// argument only picks the timeout the wait is given: the shipped code
	// turns it into 0 or -1 with the branchless `neg`/`sbb` pair and hands
	// that straight to WaitForSingleObject, so a false flag polls and a true
	// one blocks. The only reconstructed caller passes 0.
	LTRESULT GetMessage(LThreadMessage* pMsg, LTBOOL bWait);
	// 0x100157ec — the dequeue itself, and the half that takes both locks:
	// it waits on the event too, enters the queue's own critical section,
	// copies the tail's payload into the caller's storage when it was given
	// one, unlinks it and hands the record back to the global bank under the
	// global lock. LTTRUE when a message came off (name ours).
	LTBOOL   TakeMessage(LThreadMessage* pMsg);
	// 0x100157a3 — the same read WITHOUT the dequeue: wait with a zero
	// timeout, take the queue's lock, copy the tail's payload out and let go.
	// The record stays on the list, so the thread routine reads a message,
	// dispatches it and only then calls TakeMessage to retire it (name ours).
	LTRESULT PeekMessage(LThreadMessage* pMsg);

public:
	LCriticalSection m_CS;                   // 0x00
	LThreadMsgList   m_MessageList;          // 0x18
	nolf::platform::Event m_hEvent;               // 0x24  manual reset; set on post
};


// LThread's first three vtable slots. Only 4 and 8 are reached from the
// reconstructed code (the thread routine runs slot 8, then slot 4 when it
// returns); slot 0 is by convention the destructor. The slot names are ours.
class LThread
{
public:
	// 0x1001587d — out of line, so every derived constructor calls it rather
	// than inlining the two queue constructions. Only the handle and the
	// thread id are cleared; the event is left alone because Start creates it
	// before anything can look.
	LThread();

	virtual ~LThread();                      // slot 0x00 (not defined here)
	// Both have DEFAULT bodies here, which is what the server thread's
	// vtable says: its slots 0x04 and 0x08 point at 0x100158d9 and
	// 0x10015a2b, in this module's address block.
	virtual void ThreadTerm();               // slot 0x04  0x100158d9
	virtual void ThreadRun();                // slot 0x08  0x10015a2b
	// 0x0c — the only slot ThreadRun dispatches on, and the only one a
	// derived thread has to supply: one queued message, already copied out
	// of the queue and not yet retired (name ours).
	virtual void ProcessMessage(LThreadMessage* pMsg);

	LTRESULT Start(int priority);            // 0x100158ee
	LTRESULT Term(LTBOOL bWaitForThread);    // 0x100159ee

public:
	LThreadQueue m_IncomingQueue;            // 0x04
	LThreadQueue m_OutgoingQueue;            // 0x2c
	std::jthread m_hThread;                  // 0x54
	uint64 m_ThreadID{};                 // 0x58
	nolf::platform::Event m_hEvent;                   // cooperative quit signal
    std::atomic<bool> m_Failed{false};
    std::stop_source m_Stop;
};

#endif  // __LTHREAD_H__
