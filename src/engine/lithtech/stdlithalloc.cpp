// *********************************************************************** //
//
//  MODULE:   NET (.CPP)  -- scratch reconstruction, lithtech.exe only
//
//  PURPOSE:  What the survey of 0x4870f0..0x4920ba actually found, and the
//            part of it that can be built.
//
//  The run is NOT one subsystem. Reading the string references and the
//  vtables that point into it splits it cleanly into five blocks:
//
//    0x4870f0..0x488260  the tail of the client's TCP driver translation
//                        unit. Slot 0x54 of the driver vtable at 0x4a37a4
//                        is 0x4870f0, and that function is the one that
//                        pushes "ConnectTCP" -- see the note on the driver
//                        base below. Everything else in the block is
//                        template code the driver instantiates.
//    0x488280..0x4888c0  the concrete ILTVideoMgr (six slots, vtable
//                        0x4a3848, every one reporting LT_INVALIDPARAMS).
//    0x4888c0..0x48c000  the video managers/instances (SmackVideoMgr,
//                        SmackVideoInst -- five are already named).
//    0x48c270..0x48f900  sound ("...nd3D with Creative Labs EAX(TM)") and
//                        the file/directory walker ("*.*", "..").
//    0x48f9b0..0x4920ba  stdlith: a hash/bank block, then ONE translation
//                        unit's worth of the LT2-era stdlith library at
//                        0x491740..0x4919e0.
//
//  That last block is this file. It is the only part of the run that is
//  both small and completely unsourced today: src/lithshared/stdlith is
//  built into StdLith.lib, which lithtech.exe does not link -- the engine
//  compiles its own copy of these sources, with the engine allocator in
//  place of malloc/free, and that copy is what sits at 0x491740.
//
//  EVERY declaration below is transcribed from headers that are in this
//  repository -- src/LT2/lithshared/stdlith/l_allocator.h, dynarray.h,
//  goodlinklist.h -- and the reference bodies are in
//  src/lithshared/stdlith/{l_allocator,dynarray,goodlinklist}.cpp. The
//  shipped bytes contribute the confirmation, member by member, and the
//  substitution of the engine allocator for DefStdlithAlloc/DefStdlithFree.
//
//  PLACEMENT, and it matters:
//
//    * `LAlloc` is declared (and never defined) in src/engine/server/
//      collection.h, which is compiled into BOTH binaries. Defining its
//      three members HERE puts them in a lithtech-only object, which is
//      what the shipped image wants: server.dll's copies live above its
//      app-region cut. Nothing in src/engine/server changes.
//    * the three .text blocks below are three different shipped objects
//      (0x491740.., 0x491950, 0x491990, 0x4871e0). If the goal is one
//      object per shipped object, this file should be split into
//      l_allocator.cpp / dynarray.cpp / goodlinklist.cpp under
//      src/engine/lithtech, and tcp_Create() should go to
//      src/engine/server/tcpdriver.cpp (it is
//      parked at the bottom here with that warning attached).
//
// *********************************************************************** //

#include <string.h>

#include "collection.h"      // LAlloc, GenList, CMoArray, MoArray_FindElementMemcmp
#include "engine/portable/allocation.h"
#include <cstddef>
#include <limits>

// collection.h reaches ltbasetypes.h only, so LTTRUE/LTFALSE are in scope and
// LTNULL is not -- exactly the position src/engine/server/collection.cpp
// compiles from. Null is spelled 0 below for that reason, not by preference.

// The engine's allocator front ends. servermgr.h declares both under these
// names; the addresses here are lithtech.exe's. Both are cdecl one-argument
// forwarders (0x4290b0 -> 0x428fd0, 0x4290c0 -> 0x429080) and both are
// reached from a couple of hundred call sites across .text, which is what
// says they are the engine-wide allocator and not a stdlith-private shim:
// the period header spells LAlloc's bodies `DefStdlithAlloc(size)` and
// `DefStdlithFree(ptr)`, and in this build those two names resolve to
// exactly these, because 0x491790's call goes straight to 0x4290b0 with no
// intervening hop.

// StdLith's own code in this image -- struct_bank.obj, linked from
// StdLith.lib -- calls l_allocator.h's DefStdlithAlloc/DefStdlithFree, and
// its calls land on 0x4290b0/0x4290c0 too. So the engine defines the two
// names itself, as the same one-line forwarders operator new and delete are
// (dalloc.cpp); /OPT:ICF folds each onto its twin, and nothing pulls
// StdLith.lib's l_allocator.obj (the malloc versions, and a second LAlloc)
// into the exe.
void* DefStdlithAlloc(uint32 size)
{
	return dalloc(size);
}

void DefStdlithFree(void* ptr)
{
	dfree(ptr);
}


// ======================================================================= //
//  l_allocator.cpp
//
//  The client's LAlloc vtable is at 0x4a3c54 and holds exactly three slots
//  -- { 0x4917b0, 0x491790, 0x4917a0 } -- which is collection.h's declared
//  order (destructor, Alloc, Free) with nothing before or after it. That
//  is the whole confirmation of the class: no data members, three virtuals.
// ======================================================================= //

// 0x004b4e88 -- the one allocator instance in lithtech.exe (collection.h
// declares it `extern`; server.dll's is 0x10058c7c). Four compiler-emitted
// functions come with it and no source line spells any of them:
//
//   0x00491740  10 bytes  `call 0x491750; jmp 0x491760` -- the initializer
//                         pair the enumeration reports as source `ptr`.
//   0x00491750  11 bytes  `movl $0x4a3c54,0x4b4e88; ret` -- the whole of
//                         constructing it. An LAlloc is a vptr and nothing
//                         else, so this store IS the constructor, and that
//                         is a second, independent proof that the class has
//                         no data.
//   0x00491760  12 bytes  `push 0x491770; call 0x49307e; pop ecx; ret` --
//                         registering the destructor with atexit.
//   0x00491770  11 bytes  `movl $0x4a3c54,0x4b4e88; ret` -- the destructor
//                         thunk. Same store again: ~LAlloc's body is empty,
//                         so all that is left is VC6's re-store of the
//                         vptr.
LAlloc g_DefAlloc;

// 0x00491780 -- `movl $0x4a3c54,(%ecx); ret`. The vptr store and nothing
// else, and no return value, which is what separates a destructor from a
// constructor here. Nothing in the image references it: every use is either
// inlined or goes through the scalar-deleting form below, so this is the
// COMDAT the linker kept because slot 0 needed the class emitted.
LAlloc::~LAlloc()
{
}

// 0x00491790 -- vtable 0x4a3c54 slot 1 (+0x04), the slot every allocation
// in the image reaches as `call *0x4(%eax)`. Four instructions: load the
// argument, push it, call the cdecl allocator, `add esp,4`, `ret $4`. No
// null test and no zero-size test, which is why the period header's note
// ("LAllocs MUST return NULL for allocation sizes of 0") is enforced by the
// callers and by LAllocSimpleBlock::Alloc below, not here.
void* LAlloc::Alloc(uint32 size)
{
	return dalloc(size);
}

// 0x004917a0 -- slot 2 (+0x08), reached as `call *0x8(%edx)`. Identical
// shape with `pop ecx` for the cleanup instead of `add esp,4`, which is
// VC6's choice when the return value is discarded.
void LAlloc::Free(void* ptr)
{
	dfree(ptr);
}

// 0x004917b0 -- 32 bytes, slot 0. The scalar-deleting destructor VC6 emits
// for a class with a virtual destructor: re-store the vptr, then
// `if (flags & 1) dfree(this)`, then return `this`. It tests the byte at
// [esp+4] rather than the dword, and it inlines ~LAlloc's vptr store rather
// than calling 0x491780. No source line produces it; it exists because
// `virtual ~LAlloc()` was declared.


// ----------------------------------------------------------------------- //
//  LAllocSimpleBlock (l_allocator.h). One big block, handed out in pieces,
//  never given back. collection.h has no such class, so it is redeclared
//  here as a layout-identical view -- the four members are the header's, in
//  the header's order, and the shipped offsets confirm every one:
//
//     +0x00  vptr                (vtable 0x4a3c60)
//     +0x04  m_pDelegate         tested by Term (0x4918e0), stored by Init
//     +0x08  m_pBlock            the base the bump allocation adds to
//     +0x0c  m_CurBlockPos       read, advanced and stored by Alloc
//     +0x10  m_BlockSize         the ceiling Alloc compares against
//
//  Its vtable 0x4a3c60 is { 0x491820, 0x491900, 0x48faa0 } -- three slots,
//  same shape as the base's, with Alloc overridden and Free folded onto the
//  image's shared `ret $4` at 0x48faa0 (nine vtable slots point at that
//  three-byte body; it is every empty one-argument virtual in the build).
// ----------------------------------------------------------------------- //

class LAllocSimpleBlock : public LAlloc
{
public:
	                LAllocSimpleBlock();     // 0x004917d0
	virtual         ~LAllocSimpleBlock();    // 0x00491840

	LTBOOL          Init(LAlloc* pDelegate, uint32 blockSize);  // 0x00491890
	void            Term();                                     // 0x004918e0

	virtual void*   Alloc(uint32 size);      // 0x00491900
	virtual void    Free(void* ptr);         // 0x0048faa0

	uint32          GetBlockSize() const { return m_BlockSize; }

private:
	void            Clear();                 // 0x00491940

private:
	LAlloc* m_pDelegate;                     // +0x04
	uint8*  m_pBlock;                        // +0x08
	uint32  m_CurBlockPos;                   // +0x0c
	uint32  m_BlockSize;                     // +0x10
};

// 0x004917d0 -- 66 bytes, kept because
// the class is unusable without it and because it is proven twice over: it
// stores 0x4a3c60 and then calls 0x491940 (Clear), and it carries an
// __try/__except frame (`push $-1; push $0x4a0a28; fs:0`) that only the
// base subobject's virtual destructor can be asking for.
// NOT DEFINED, deliberately, and this is the interesting part.  Our body
// compiles to 18 bytes; the shipped one is 66, and the whole difference is
// the EH registration.  /GX is already on for this target, and the base
// subobject with its virtual destructor is present, so the frame is not a
// flag we are missing -- something about the shipped TU makes VC6 register
// an unwind for a ctor body that ours proves cannot throw.
//
// Leaving the definition in was actively harmful: the destructor's 23-byte
// body matched at 0x0045ad00 -- an
// unrelated function that also sets a vptr and makes two calls, with the
// relocations masked -- so it was credited with a function we had not
// reconstructed.  A shy count is the project's rule; an inflated one is
// fabrication.  Both bodies were commented out for that reason until the
// whole-exe link needed them: they are real functions of this image, and
// without them the link pulls StdLith.lib's l_allocator.obj in for them, a
// second LAlloc and g_DefAlloc.  Defined now; the scorer must not credit the
// destructor at 0x0045ad00 (checked when this was restored).
//
LAllocSimpleBlock::LAllocSimpleBlock() { Clear(); }

// 0x00491840 -- 70 bytes, also over the cut. Stores 0x4a3c60, calls Term
// (0x4918e0), then stores the BASE vtable 0x4a3c54 on the way out, which is
// VC6 walking the class back down to LAlloc. Its scalar-deleting form is
// 0x00491820 (30 bytes: call 0x491840, `if (flags & 1) dfree(this)`, return
// this) and fills slot 0 of 0x4a3c60.
LAllocSimpleBlock::~LAllocSimpleBlock() { Term(); }

// 0x00491890 -- 71 bytes, over the cut, included for the same reason. The
// rounding is `lea 0x3(%eax),%edi; and $0xfffffffc,%edi`, and the branch it
// feeds is `jbe` on the flags that `and` left -- CF is clear after `and`,
// so that is a test for zero, i.e. the header's `if (blockSize > 0)`. The
// allocation goes through the delegate's slot 1 (`call *0x4(%edx)`) and its
// result is stored to +0x08 BEFORE being tested, which is why m_pBlock is
// written on the failure path too.
LTBOOL LAllocSimpleBlock::Init(LAlloc* pDelegate, uint32 blockSize)
{
	Term();

	blockSize = (blockSize + 3) & ~3;

	if (blockSize > 0)
	{
		m_pBlock = (uint8*)pDelegate->Alloc(blockSize);
		if (!m_pBlock)
			return LTFALSE;
	}

	m_pDelegate = pDelegate;
	m_BlockSize = blockSize;
	m_CurBlockPos = 0;

	return LTTRUE;
}

// 0x004918e0 -- 28 bytes. Reads +0x04, and only if it is non-null pushes
// +0x08 and dispatches the delegate's slot 2 (`call *0x8(%eax)`); then tail
// calls 0x491940. Note what it does NOT do: it never nulls m_pBlock itself,
// because Clear does.
void LAllocSimpleBlock::Term()
{
	if (m_pDelegate)
	{
		m_pDelegate->Free(m_pBlock);
	}

	Clear();
}

// 0x00491900 -- 49 bytes, slot 1 of 0x4a3c60. The zero-size test is first
// and returns null, which is the header's stated LAlloc contract. Then the
// round-to-four, then the sum `m_CurBlockPos + size` is formed in a
// register and compared against m_BlockSize with `jbe` -- unsigned, so the
// members are uint32 and not int. The returned pointer is m_pBlock plus the
// OLD position (`lea (%edx,%eax,1)` on the pre-increment value), and the
// new position is stored before the return value is computed.
void* LAllocSimpleBlock::Alloc(uint32 size)
{
	uint8* pRet;

	if (size == 0)
		return 0;

	// A native record can contain pointers, doubles and vector fields.
	const uint32 alignment = alignof(std::max_align_t);
	if (size > std::numeric_limits<uint32>::max() - (alignment - 1)) return 0;
	size = (size + alignment - 1) & ~(alignment - 1);

	if (m_CurBlockPos > m_BlockSize || size > m_BlockSize - m_CurBlockPos)
		return 0;

	pRet = &m_pBlock[m_CurBlockPos];
	m_CurBlockPos += size;
	return pRet;
}

// 0x0048faa0 -- three bytes, `ret $4`. Nine vtable slots in the image point
// at it, so it is not this class's body in any meaningful sense: it is the
// one empty one-argument virtual the whole build shares. Slot 2 of
// 0x4a3c60 is one of the nine.
void LAllocSimpleBlock::Free(void* ptr)
{
}

// 0x00491940 -- 15 bytes: one zeroed register stored to +0x04, +0x08,
// +0x0c, +0x10 in that order. Declaration order, four members, no gaps and
// nothing at +0x14 -- which is what fixes sizeof at 0x14 and rules out a
// fifth field.
void LAllocSimpleBlock::Clear()
{
	m_pDelegate = 0;
	m_pBlock = 0;
	m_CurBlockPos = 0;
	m_BlockSize = 0;
}


// ======================================================================= //
//  dynarray.cpp
// ======================================================================= //

// 0x00491950 -- 55 bytes. collection.h already declares this and explains
// why it exists ("called by the virtual GenFindElement and we don't want to
// generate a bunch of template code for every type of object we use in a
// list"); nothing in the tree defines it, because server.dll's copy is
// above its app-region cut. lithtech.exe's is here, and eight shipped
// GenFindElement bodies call it -- each one pushing its own sizeof:
// 0x487a90 pushes 0xa4 (TCPQueryResponse, the TCP driver's query array),
// 0x4915c0 pushes 0x08, 0x4895b0 is a third size.
//
// The shipped bytes prove the period source is reproduced BUG INCLUDED.
// `pCur` is initialised from pToFind, not from pArray, and the memcmp then
// compares the walking pointer against pToFind as well -- so the shipped
// loop reloads argument 1 as its fixed operand and NEVER READS argument 2
// at all ([esp+8] is not touched anywhere in the function). Every call
// therefore reported index 0. Native collections search the actual array.
uint32 MoArray_FindElementMemcmp(
	const void* pToFind,
	const void* pArray,
	uint32 nElements,
	uint32 elementSize)
{
	const char* pCur = (const char*)pArray;
	uint32 i;

	for (i = 0; i < nElements; i++)
	{
		if (memcmp(pCur, pToFind, elementSize) == 0)
			return i;

		pCur += elementSize;
	}

	return BAD_INDEX;
}


// ======================================================================= //
//  goodlinklist.cpp
// ======================================================================= //

// The node of a CGLinkedList, transcribed from
// src/LT2/lithshared/stdlith/goodlinklist.h. The order of the two links is
// load-bearing and the shipped walk fixes it: 0x491990 advances through
// `[pCur+4]`, and the header's second member is m_pGNext.
class CGLLNode
{
public:
	CGLLNode* m_pGPrev;                      // +0x00
	CGLLNode* m_pGNext;                      // +0x04
};

typedef CGLLNode* GPOS;

// 0x00491990 -- 65 bytes, one over the size cut, kept because it is the
// same three-line shape as the array version above and equally certain: the
// null test on pHead, one `repz cmpsb` whose fixed operand is argument 2
// and whose length is argument 3, the advance through +0x04, and the
// `cmp pCur, pHead` that ends a CIRCULAR list -- there is no null
// terminator anywhere in the loop, which is what says the list closes on
// itself. Returns the node, or null.
//
// Unlike the array version this one is correct: the walking pointer is
// compared against pToFind, which is what the source says.
GPOS GLinkedList_FindElementMemcmp(
	CGLLNode* pHead,
	const void* pToFind,
	uint32 elementSize)
{
	CGLLNode* pCur;

	if (pHead)
	{
		pCur = pHead;
		do
		{
			if (memcmp(pCur, pToFind, elementSize) == 0)
				return pCur;

			pCur = pCur->m_pGNext;
		} while (pCur != pHead);
	}

	return 0;
}


// ======================================================================= //
//  THIS ONE IS IN THE WRONG FILE ON PURPOSE
//
//  tcp_Create() belongs in src/engine/server/tcpdriver.cpp, next to
//  localdriver.cpp's ldrv_Create(). servermgr.h already declares both
//  (0x10014d65 and 0x10037c32 for server.dll) and localdriver.cpp defines
//  one of them; nothing in the tree defines the other, because server.dll's
//  copy is above the app-region cut. lithtech.exe's copy is NOT above any
//  cut -- it sits at 0x4871e0, inside the TCP driver's own .text block --
//  so this is a genuine gap and a one-line fix, but putting the line here would place the code in
//  the wrong object. Move it before relying on it.
// ======================================================================= //

// The client's TCPDriver, declared only so the `new` below has a size and a
// constructor to name. Both are read straight off 0x4871e0 and 0x484c10:
//
//   * `push $0xb0` -- sizeof(TCPDriver) is 0xb0, which is exactly where
//     tcpdriver.cpp's layout ends (m_Service, a TCPNetService, at 0xa8 plus
//     its eight bytes). The client's class is the server's class.
//   * `mov ecx,eax; jmp 0x484c10` -- the constructor is tail called and
//     hands `this` back in eax. 0x484c10 stores CBaseDriver's vptr
//     0x4a223c, sets m_Timeout (+0x4c) to 120.0f and m_DriverFlags (+0x44)
//     to 0 -- CTCPDriverBase's in-class constructor, expanded inline, just
//     as server.dll's 0x10036060 does -- then stores 0x4a3810 at +0x50 and
//     zeroes +0x54/+0x58/+0x5c, which is the TCPQueryArray base's own
//     constructor and confirms the second base is still at +0x50.
//   * the null test between the two (`test eax,eax; je` -> return 0) is
//     VC6's own guard on `new`, not source.
class TCPDriverView;                         // (name ours: 0xb0 bytes, ctor 0x484c10)

// 0x004871e0 -- 27 bytes. Reached by one direct call, from 0x45609b, which
// is the client's CNetMgr::AddDriver immediately after its inline strcmp
// against "internet" -- the same source netmgr.cpp already carries as
// `pDriver = tcp_Create();`.
//
//   CBaseDriver* tcp_Create()
//   {
//       return (CBaseDriver*)new TCPDriver;
//   }
//
// Left commented out rather than compiled, because TCPDriver is defined in
// tcpdriver.cpp and this file must not redeclare it.
