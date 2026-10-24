// *********************************************************************** //
//  MODULE:   WORLDBSP (.H)
//  PURPOSE:  IWorldBsp — the world geometry a level is made of, and the
//            object every LTObject points at through m_pWorldBsp. The class
//            was already declared from the outside in ltobject_internal.h
//            (three virtuals and the bounding box); this is the same class
//            from the inside, at 0x1000abae-0x1000afa0.
//
//            The layout is not guessed. Init (0x1000ac21) zeroes every
//            scalar field one at a time, which gives each offset and width;
//            Term (0x1000ad12) releases exactly the ones that are owned
//            pointers, which says which of them are allocations; and the
//            constructor builds the three members that are objects of their
//            own. Everything Init does not touch is a constructed member.
//
//            Field names are ours except the bounding box.
//
//            DECLARATION ORDER. CWorldSimple and everything it is built out
//            of now come FIRST, because the BSP world's array at +0x138 is
//            an array OF CWorldSimple and a template instantiation needs its
//            element type complete. The shipped .rdata agrees: CWorldSimple's
//            vtable is 0x1004d380 and the BSP world's 0x1004d428, with the
//            two arrays' tables (0x1004d3a4, 0x1004d3d4) in between.
// *********************************************************************** //
#ifndef __WORLDBSP_H__
#define __WORLDBSP_H__

#include "ltbasedefs.h"
#include "collection.h"
#include "world_node.h"
#include "world_polygon.h"
#include "world_grid.h"
#include "world_bsp_interface.h"
#include "world_visibility.h"
#include "world_records.h"
#include <string>
#include <vector>

struct WorldSurfaceProperties {
    uint32 surface;
    std::string class_name, properties;
};

// 0x1000d398 — the allocator's release, which every owned pointer here goes
// back through.
void dfree(void* pMem);

// 0x10058360 — how many bytes of world geometry are live. Term takes its own
// contribution back off it (name ours).
extern uint32 g_WorldBytes;


// ======================================================================= //
//  The four arrays.
//
//  All four are stdlith's growable array — `CMoArray<T, DefaultCache>`,
//  collection.h — instantiated once per element type, and every fact below
//  is off the shipped bytes rather than guessed.
//
//   * Layout. `CMoArray<T, DefaultCache>` is 0x14 bytes: the `GenList<T>`
//     vptr, m_pArray, m_nElements, and `DefaultCache`'s {m_CacheSize,
//     m_WantedCache} — which is exactly what the hand-rolled classes here
//     used to spell {vptr, m_pItems, m_nItems, m_nSpare, m_nGrowBy}. The
//     old m_nSpare IS m_CacheSize and the old m_nGrowBy IS m_WantedCache.
//
//   * INSTANTIATION, NOT DERIVATION, and the destructors settle it on their
//     own. server.dll's ~CWorldSimple (0x1000a849) and lithtech.exe's
//     (0x00425cf0) each tear an array down as ONE vftable store followed by
//     `SetSize2(0, pAlloc)` — `~CMoArray() { Term(); }`. A derived class
//     over a destructor-bearing base emits TWO vptr stores; VC6 does not
//     fold them (CNetBank/CBankBase at 0x10021449 shows it doing exactly
//     that). The constructors say the same from the other side: all four
//     (0x1000b15c, 0x1000b295, 0x1000b4db, 0x1000b591) are the same 34
//     bytes as every other `CMoArray<T, DefaultCache>::CMoArray()` in the
//     image (0x1000af9a) — one vftable store at the TOP, then Clear()'s
//     four zeroes, then `call Init` — where a base constructor would put
//     ITS store first and the derived store LAST.
//
//   * Why the change had to happen: a non-template member declared
//     out-of-class is the one construct /Ob1 never expands, so the
//     hand-rolled spelling could match server.dll forever and could never
//     match lithtech.exe, where the constructors, Init and SetSize are all
//     EXPANDED into the two world objects' constructors and destructors.
//
//   * The bodies, all of them already matched from collection.cpp:
//     0x1000c38c and 0x1000bf58 are `Init(size, cacheSize)` (53 bytes);
//     0x1000c470 (95), 0x1000bf8d (98) and 0x1000c0e7 (112) are `SetSize2`;
//     0x1000d181 and 0x1000d1b5 — the old `wa138_FreeItems` and
//     `wa138_AllocItems` — are `BaseDelete<CWorldSimple>` and
//     `BaseNew<CWorldSimple>`, the free function templates LDelete_Array
//     and LNew_Array expand to. That is why both took an allocator and a
//     raw block rather than a `this`, and why the resize had to null the
//     fields itself: `_DeleteAndDestroyArray` does that, not BaseDelete.
//
//   * `g_MoAlloc` is collection.h's `g_DefAlloc` — the same object at
//     0x10058c7c — so every `SetSize(0, &g_MoAlloc)` below is now
//     `Term(&g_DefAlloc)`.
// ======================================================================= //


// A regular grid over the world: a cell size, an origin, three counts, and a
// run of eight-byte records — one per cell — that it owns. Both world objects
// keep one. What each record holds is not reached from here (names ours).
// One piece of the world the radius is measured over: a vertex count and a
// run of records, each of which points at a position (names ours).
// 0x1000aaee — the furthest any of those positions gets from a point, as a
// squared distance (name ours).
float wb_MaxDistSqr(CWorldPart** ppParts, uint32 nParts, LTVector* pPoint);


// What the world sits on: a vptr and the world's NAME, inline. The name is
// 0x41 bytes and it belongs HERE rather than to either derived class, and
// the evidence is CWorldSimple's implicit assignment (0x1000bd94):
//
//   * it copies [0x04, 0x45) as ONE byte-at-a-time loop counted against
//     `cmp $0x41` — a single char-array member, not a byte at +0x04 plus a
//     separate run at +0x08. Written the old way (uint8 at +0x04 in the
//     base, uint8 m_pad_08[0x40] in the derived class) VC6 emits the base's
//     byte on its own and then a loop counted against 0x40, which is the
//     six bytes ours was long: 150 against the shipped 144;
//   * 0x45..0x47 are copied by nothing at all, so they are alignment
//     padding and the next member really is at 0x48;
//   * the byte loop is how VC6 spells a char array whose size is NOT a
//     multiple of four in an implicit operator= (0x41 = 65). The same array
//     in the implicit COPY CONSTRUCTOR (0x1000bcdc) comes out as
//     `rep movsl $0x10` plus one `movsb` instead.
//
// ltobject_internal.h's IWorldBsp carries the same field at the same offset
// (the physics passes &m_Name to a %s) and savobj.cpp reads it there; both
// pad it out to the bounding box at 0xc4 because neither of them needs the
// end of it. 0x41 is where the copy says it really stops.
//
// The constructor is separate (0x1000aba1), which is what puts the base's
// vtable store before the derived one; it empties the name, which is the
// one byte it writes at +0x04.
// The element of the simple world's second array. It used to be a size and a
// constructor declared and deliberately NOT defined, on the reading that a
// body would be expanded into BaseNew and the shipped `call` would go. The
// /O2 client says the opposite, and it gives the whole class away:
//
//   * server.dll's BaseNew for it (0x1000d145, 60 bytes) steps 0x18 bytes at
//     a time and its placement-new loop does `push $0x0; call 0x10009250` —
//     a call, one defaulted argument;
//   * lithtech.exe's SAME BaseNew (0x00428dd0, 84 bytes) has that callee
//     EXPANDED, and the expansion is the constructor's whole body in the
//     clear. Both builds are /Ob1, so the body has to be an inline candidate
//     in the true source and /Os simply declines it at thirty-seven bytes —
//     the ordinary /O1-vs-/O2 split, not a body that does not exist.
//
// So the constructor is in the class and its one statement is `Init(bInit)`,
// which 0x10009250 is: that function leaves %eax holding &m_head rather than
// `this`, and a VC6 constructor always hands `this` back, so what BaseNew's
// placement new reaches is a void member and the constructor around it is
// one line. Reading 0x00428dd0's stores off the image (%ebx is the zero,
// %ecx runs at this+0x16):
//
//   mov %bl,(%ecx)          0x16 = bInit      mov %ebx,-0x16(%ecx)   0x00 = 0
//   mov %bl,0x1(%ecx)       0x17 = 0          movw $0xffff,-0x2(%ecx) 0x14
//   mov %ebx,-0xe(%ecx)     0x08 = 0          mov %ebx,-0x12(%ecx)   0x04 = 0
//   lea -0xa(%ecx),%edx     edx = &m_head (0x0c)
//   mov %edx,-0x6(%ecx)     0x10 = &m_head    mov %edx,(%edx)        0x0c = it
//
// — three dwords, a self-linked circular list head at 0x0c, a 16-bit -1, the
// argument at 0x16 and a last byte at 0x17. `mov 0x4(%esp),%al` in the /O1
// body says the argument is a byte. This is collection.cpp's CCollElem18L,
// read there off 0x10009250 alone; the body below is its body.
//
// WHY THE SPELLING IS SAFE for everything that already matches. The rest of
// this instantiation copies the element, and a copy does not care how the
// constructor is written — it cares about the LAYOUT. Six contiguous dwords
// of scalar members fold into the same `rep movsl $0x6` the old
// `uint8 m_pad[0x18]` gave (0x18 is a multiple of four, which is the whole
// condition — the 0x41-byte name in CWorldBspBase above is not, and comes
// out as a byte loop instead). The shipped image proves it from the other
// side: CopyArray2 for this element (0x1000d0ad) is `rep movsl $0x6` and it
// calls 0x1000d145, whose callee is 0x10009250, so the run of shipped code
// that does the `rep movsl` was built from THIS class and not from a blob.
// lithtech.exe says the same: 0x00426d36, 0x00426e86 and 0x00426fec — inside
// GenAppend, GenRemoveAt and GenCopyList, all three of which we already
// match — all call the constructor-expanding 0x00428dd0.
//
// The constructor is also why this array's SetSize2 is the 98-byte shape
// (0x1000bf8d) where the four-byte arrays' is 95 (0x1000c470): an element
// with a constructor gives BaseNew a loop too big to fold into
// _AllocateTArray, so _AllocateTArray stays a pure forwarder and VC6 inlines
// THAT into SetSize2 instead, leaving the three-argument cdecl call.
// The simple world's two arrays. vtable 0x1004d3a4 / Init 0x1000c38c /
// SetSize2 0x1000c470, and vtable 0x1004d3d4 / Init 0x1000bf58 / SetSize2
// 0x1000bf8d. The first holds world parts — ComputeRadius hands its storage
// and count straight to wb_MaxDistSqr, which takes CWorldPart** — and the
// second holds the 0x18 element above.
typedef CMoArray<CWorldPart*>  CWorldArray50;    // vtable 0x1004d3a4
typedef CMoArray<CWorldItem18> CWorldArray68;    // vtable 0x1004d3d4


// The eight bytes at CWorldSimple+0x48, read field by field so that the
// stream layout is one byte, one byte, one byte, one byte, one dword rather
// than a single eight-byte block (names ours).
class CWorldSimpleHeader
{
public:
	uint8  m_n00;                            // 0x00
	uint8  m_n01;                            // 0x01
	uint8  m_n02;                            // 0x02
	uint8  m_n03;                            // 0x03
	uint32 m_n04;                            // 0x04
};

// 0x1000aa81 — read that header. Five separate `ILTStream::Read` calls, and
// the last one is reached by advancing the record pointer rather than by a
// displacement, which is what says the dword is a field of the same record
// and not a separate variable (name ours).
void wb_ReadSimpleHeader(class ILTStream* pStream, CWorldSimpleHeader* pHeader);


// The other object on the world's interface, and the simpler of the two: two
// arrays, a sub-object and a surface pointer, with none of the geometry the
// BSP carries. Its vtable is 0x1004d380, which is the one the accessors
// below are slots of (name ours).
class CWorldSimple : public CWorldBspBase
{
public:
    const CWorldSub108& PhysicsGrid() const override { return m_Sub80; }
    std::span<WorldPolyRef> PolygonReferences() override { return {m_Array68.m_pArray,m_Array68.m_nElements}; }
	// 0x1000a8bb — __thiscall: %ecx is the element and the three pushes
	// are not cleaned by the caller at 0x10009154. Reconstructed at the
	// foot of worldbsp.cpp: 454 of 454 bytes, 164 of 164 instructions.
	LTBOOL Load(class CWorldBsp* pParent, class ILTStream* pStream,
	            uint32 nIndex);                         // 0x1000a8bb
    CWorldSimple(const CWorldSimple&);
    CWorldSimple& operator=(const CWorldSimple&);
    void Swap(CWorldSimple&) noexcept;
    CWorldSimple();                          // 0x1000a7de
	~CWorldSimple();                         // 0x1000a849

	// 0x1000a882 — put it back the way the constructor left it, without
	// destroying the members (name ours).
	void Reset();

	// 0x1000aacc — how far the world reaches from its centre, kept squared
	// because nothing compares it against anything but another square.
	void ComputeRadius();

	// The nine slots of 0x1004d380, in order. Slots 0 and 7 are the
	// linker-shared `return 1` (0x100355db) and `return 0` (0x10014d2f)
	// stubs; the rest are a single load or a forward off an offset this
	// class owns (names ours).
	//
	// Slot 4 hands the point test to the sub-object at +0x80, exactly as the
	// BSP world's slot 4 hands it to the one at +0x108.
	virtual uint32 IsBusy();                 // 0x100355db
	virtual WorldPolyRef* GetRoot();                // 0x1000ab4f  (four bytes)
	virtual HPOLY  MakeHPoly(WorldPolyRef* pPoly);   // 0x1000ab53
	virtual CWorldPart* FindPoly(HPOLY hPoly);    // 0x1000ab62
	virtual class CSubRecord* GetPointStatus(LTVector vPoint);  // 0x1000ab71
	virtual WorldPolyRef* GetPolyRefs();            // 0x1000ab8f
	virtual uint32 GetWorldFlags();          // 0x1000ab93
	virtual uint32 IsFlagSet();              // 0x10014d2f
	virtual float  GetRadiusSqr();           // 0x1000ab9a

public:
	// 0x04..0x47 is the base: its name array and the three bytes of padding
	// after it. This class's own storage starts at 0x48.
	//
	// 0x48 — five fields the loader reads one at a time off the stream
	// (0x1000aa81): four bytes and a dword. Nothing else in the
	// reconstructed set reaches them, so they are named by their offsets.
	CWorldSimpleHeader m_Header48{};           // 0x48
	CWorldArray50  m_Array50;                // 0x50
	WorldPolyRef*  m_n64;                    // 0x64
	CWorldArray68  m_Array68;                // 0x68
	class CWorldBsp* m_p7c;             // 0x7c
	CWorldSub108   m_Sub80;                  // 0x80
	LTVector       m_vCenter{0,0,0};                // 0xb0
	float          m_fRadiusSqr = 0;             // 0xbc
};                                           //      = 0xc0


// The BSP world's two arrays. The one at +0x138 is an array of the *simple*
// worlds, and that is not an inference from the size alone: its BaseDelete
// (0x1000d181) runs 0x1000a849 — ~CWorldSimple — over every element stepping
// 0xc0 bytes at a time, and its BaseNew (0x1000d1b5) placement-constructs
// each slot with 0x1000a7de, CWorldSimple's own constructor. The one at
// +0x54 is a four-byte element whose type is not reached from here; it gets
// its own vtable (0x1004d450) because it is its own instantiation.
typedef CMoArray<CWorldSimple> CWorldArray138;   // vtable 0x1004d480
typedef CMoArray<CWorldEdgeQT1> CWorldArray54;    // vtable 0x1004d450


class CWorldBsp : public CWorldBspBase
{
public:
    const CWorldSub108& PhysicsGrid() const override { return m_Sub108; }
    std::span<WorldPolyRef> PolygonReferences() override { return {m_n70,m_n74}; }
    CWorldBsp(const CWorldBsp&) = delete;
    CWorldBsp& operator=(const CWorldBsp&) = delete;
    CWorldBsp();                             // 0x1000abae
	~CWorldBsp();                            // 0x1000abe5

	// Not the SDK's — the world is engine-private. The constructor stores
	// 0x1004d428, ten slots: CWorldBspBase's nine and DoVisQuery. In order
	// they are 0x10014d2f (the linker-shared `return 0` stub), 0x1000aebb,
	// 0x1000aec2, 0x1000aef8, 0x1000af19, 0x1000af37, 0x1000aea6,
	// 0x1000aeaa, 0x1000aeb4 and 0x1000ae99. The simple world's (0x1004d380)
	// are 0x100355db, 0x1000ab4f, 0x1000ab53, 0x1000ab62, 0x1000ab71,
	// 0x1000ab8f, 0x1000ab93, 0x10014d2f, 0x1000ab9a. The offsets agree:
	// slot 4 of the first adds 0x108 (this class's sub-object) and slot 4 of
	// the second adds 0x80 (the simple world's), and slot 8 of the first
	// reads +0x14c while the second's reads +0xbc (names ours).
	virtual uint32 IsBusy();                 // 0x10014d2f
	virtual WorldPolyRef* GetRoot();                // 0x1000aebb
	// Slot 2: a poly becomes a handle only if it lies in this world's run.
	virtual HPOLY  MakeHPoly(WorldPolyRef* pPoly);   // 0x1000aec2
	// Slot 3: one of the world parts by the low sixteen bits of its
	// argument, and null for anything the count does not reach. The mask is
	// in the shipped bytes (`and $0xffff`), so the handle it is handed
	// carries something else in its high half (name ours).
	virtual CWorldPart* FindPoly(HPOLY hPoly);    // 0x1000aef8
	virtual class CSubRecord* GetPointStatus(LTVector vPoint);  // 0x1000af19
	// Slot 5: four bytes, the pointer at +0x70 handed out raw — the same one
	// FreeTree releases (name ours).
	virtual WorldPolyRef* GetPolyRefs();            // 0x1000af37
	virtual uint32 GetWorldFlags();          // 0x1000aea6
	virtual uint32 IsFlagSet();              // 0x1000aeaa
	virtual float  GetRadiusSqr();           // 0x1000aeb4
	// Slot 9 (0x1000ae99, lithtech 0x426b50): the visibility query, handed
	// straight to the world-tree traversal without `this` (name ours).
	LTRESULT DoVisQuery(const CWorldVisQuery&);

	void Init();                             // 0x1000ac21
	// Not a slot: 0x1000ae73 is reached only by direct call (name ours).
	void   Recalc();                         // 0x1000ae73
	void Term();                             // 0x1000ad12
	void FreeTree();                         // 0x1000ae5c

public:
	// The base runs to 0x48: a vptr and the 0x41-byte name it owns.
	uint32   m_nUnknown48;                   // 0x48  cleared last of all
	uint32   m_Flags4c;                      // 0x4c  bit 0 = do not release
	uint32   m_nBytes;                       // 0x50  this world's share of
	                                         //       the live-geometry total
	CWorldArray54 m_Array54;                 // 0x54
	void*    m_p68;                          // 0x68
	uint32   m_n6c;                          // 0x6c
	WorldPolyRef* m_n70;                          // 0x70
	uint32   m_n74;                          // 0x74
	class CWorldSurface* m_pTree;               // 0x78  released its own way
	uint32 m_nSurfaces;       // 0x7c  the surface table
	CWorldGroupEntryQT1* m_p80;                          // 0x80
	uint32   m_n84;                          // 0x84
	CWorldGroupQT1* m_p88;                          // 0x88
	uint32   m_n8c;                          // 0x8c
	CWorldPolyKey* m_p90;                          // 0x90
	uint32   m_n94;                          // 0x94
	uint32   m_n98;                          // 0x98
	WorldPolyRef* m_n9c;                          // 0x9c
	// The run of world parts and its count: what the diagonal is measured
	// over, and what slot 3 of the vtable (0x1000aef8) indexes by the low
	// sixteen bits of its argument.
	class CWorldPart** m_ppParts;            // 0xa0
	uint32   m_nParts;                       // 0xa4
	void*    m_pa8;                          // 0xa8
	uint32   m_nac;                          // 0xac
	// A run of 0x24-byte records, each owning one allocation of its own,
	// which the teardown walks before releasing the run itself.
	CWorldChunk* m_pChunks;            // 0xb0
	uint32   m_nChunks;                      // 0xb4
	void*    m_pb8;                          // 0xb8
	void*    m_pbc;                          // 0xbc
	uint32   m_nc0;                          // 0xc0
	uint32   m_ndc;                          // 0xdc
	uint32   m_ne0;                          // 0xe0
	uint32   m_ne4;                          // 0xe4
	LTVector m_ve8;                          // 0xe8
	void*    m_pf4;                          // 0xf4
	uint16   m_nf8;                          // 0xf8
	uint8    m_pad_fa[0xfc - 0xfa];
	void*    m_pfc;                          // 0xfc
	uint32   m_n100;                         // 0x100
	void*    m_p104;                         // 0x104
	CWorldSub108   m_Sub108;                 // 0x108
	CWorldArray138 m_Array138;               // 0x138
    float m_fDiagonal;
    std::vector<WorldSurfaceProperties> m_SurfaceProperties;
    std::shared_ptr<const WorldVisibility> m_Visibility;
private:
    bool m_Terminating = false;
};

// One record of the run at +0xb0, 0x24 bytes of it, owning a single
// allocation the teardown releases before the run itself goes (names ours).


// The world's "diagonal" is the same function: Recalc (0x1000ae73) calls
// 0x1000aaee too, so wb_MaxDistSqr above serves both, and what the BSP world
// keeps at +0x14c is the furthest its parts reach from the point at +0xe8.
// The two callers disagree about one thing worth keeping: Recalc pushes its
// two members straight (`push 0xa4(%esi)`), and the simple world's
// ComputeRadius loads both into registers first — same expression, two
// spellings, and each has to be written the way it shipped.

// 0x10028ace is the engine's `::operator delete` (dalloc.cpp); the tree at
// +0x70 goes back through it rather than the allocator's release.

// A poly, and the record that points at one. The index the handle carries is
// the poly's own, not the record's (names ours).
// 0x10051a2c and 0x10051a30 — the world tree's two terminators, and they are
// NOT interchangeable: the sphere descent at 0x1000d437 treats reaching the
// front one as "this node is the answer" and reaching the back one as "there
// is no node". 0x10051a30 does double duty as what a fresh world object
// starts its 0x64 field at, which is consistent -- that field starts out
// holding the same terminator (names ours).
//
// Repeated verbatim in ltobject.cpp, which reaches them from the descent and
// deliberately does not include this header. Same names, same types, so both
// TUs name one symbol each.
extern void* g_pWorldTreeFrontTerm;
extern void* g_pWorldTreeBackTerm;

#endif  // __WORLDBSP_H__

void wm_ResetGeometry(CWorldBsp* world);
