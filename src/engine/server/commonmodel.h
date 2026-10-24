// *********************************************************************** //
//  MODULE:   COMMONMODEL (.H)
//  PURPOSE:  The shared half of ILTModel — the animation-tracker accessors the
//            engine implements once for both client and server, at
//            0x1001cff6-0x1001e055. The server-specific overrides that
//            broadcast a change to the clients live separately, at
//            0x1002dc22+, and report themselves as "ServerModelLT::".
//
//            The class name is ours. Only "MyServerInterface" is attested by a
//            surviving literal; everything in this file reports itself as
//            "ILTModel::", the interface.
//
//            LTAnimTracker's layout comes from the public ltanimtracker.h and
//            needs no reconstruction: m_Link 0x00, m_StringKeyCallback 0x0c,
//            m_pUser1 0x10, m_CurKey 0x14, m_Flags 0x18, m_InterpolationMS
//            0x1a, m_TimeRef 0x1c (m_pModel 0x1c, m_Prev 0x20, m_Cur 0x28,
//            m_Percent 0x30, m_iWeightSet 0x34). Every offset the accessors
//            use lands on a named field, which is the confirmation that the
//            shipped header and the shipped code agree.
// *********************************************************************** //
#ifndef __COMMONMODEL_H__
#define __COMMONMODEL_H__

#include <vector>
#include <array>
#include <atomic>
#include <memory>
#include <span>
#include "observer.h"
#include "ltanimtracker.h"
#include "iltmodel.h"
#include "ltobject_internal.h"
#include "collection.h"
#include "engine/portable/arena.h"


// LTAnimTracker::m_Flags. ltanimtracker.h refers to "AT_ flags above" but the
// SDK copy does not define them — they are engine-internal. Bit 0 is tested by
// GetPlaying/SetPlaying and bit 1 by GetLooping/SetLooping.
#define AT_PLAYING  (1 << 0)
#define AT_LOOPING  (1 << 1)

// One animation. Only its name is reached from here (name ours).
// One keyframe: the time it lands on and four dwords of payload.
class AnimKeyFrame
{
public:
	// 0x1001651d — the defaults: everything zero and the string on the
	// shared empty string. It exists because the keyframe collection's
	// allocator runs it over every element (see CUnknownD9E4 in
	// commonmodel.cpp).
	AnimKeyFrame();

	uint32 m_Time;                           // 0x00
	// The two payloads at_DoKeyFrame reaches, and the byte that says which
	// of them is live: type 0 is a string key (the string at +0x04, handed
	// to the tracker's own callback) and type 1 is a direct call (the
	// function at +0x0c, gated on bit 2 of the tracker's flags). Any other
	// type does nothing at all. The record's 0x14 bytes come from the
	// `lea (%edi,%edi,4)` / `lea (%ecx,%eax,4)` pair that indexes it and
	// from the `idiv $0x14` that turns a keyframe pointer back into an
	// index (names ours).
	char*  m_pString;                        // 0x04
	uint8  m_Type;                           // 0x08
	uint8  m_pad_09[0x0c - 0x09];
	StringKeyCallback m_pFn;                 // 0x0c
	// 0x10 — the fourth dword the constructor clears. Nothing reads it in
	// the reconstructed set; it exists because 0x1001651d writes it
	// (name ours).
	uint32 m_Unknown10;                      // 0x10
};

// 0x1c bytes of transform, and the record the animation's per-node tables are
// made of: a three-float position at +0x00 read by w_ReadVector and a
// four-float quaternion at +0x0c read by w_ReadRotation (name ours).
//
// IT HAS A USER-DECLARED, void-RETURNING ASSIGNMENT OPERATOR, and
// the shipped image says so at both optimisation levels:
//
//   * 0x10016723 (42 bytes) in server.dll and 0x004457e0 (55) in lithtech.exe
//     are the same function — a thiscall that copies twelve bytes and then
//     sixteen and ends `mov 0x18(%eax),%eax; ret $0x4`, leaving `this`
//     NOWHERE. A compiler-generated operator= returns `this`, so this one was
//     written by hand and returns void. stdlith spells assignments that way
//     elsewhere too (BaseGenListPos::operator= in genlist.h).
//   * server.dll's CMoArray<CModelXform,NoCache>::CopyArray2 (0x1001b6a3)
//     CALLS 0x10016723 from its element loop (0x1001b6fb, stride 0x1c), and
//     lithtech.exe's CUnknownD974::sub_100165e5 (0x4456f0), which is the same
//     copy inlined whole, CALLS 0x4457e0 (0x445794). Our raw-float spelling
//     gives both a `rep movsl` of seven dwords instead.
//   * 0x10016723 lies between sub_1001671c (0x1001671c) and CUnknownD9E4's
//     constructor (0x1001674d), i.e. inside this translation unit's range.
//     collection.cpp's CCollElem1CV was built as a stand-in for exactly this
//     address, and that class — a plain twelve-byte head plus a sixteen-byte
//     sub-object with an assignment operator of its own — reproduces BOTH
//     shipped bodies byte for byte today. This is that class, named.
//
// So the two halves are sub-objects, not raw arrays, and they differ: the head
// has no operator= of its own (the compiler's memberwise copy, which VC6
// renders as three `movsl` at /O1 and three dword move pairs at /O2 — see
// gp_SetRotation / gp_SetVector in genericprop.cpp, which match in both images
// doing exactly that to an LTVector), while the tail's four dwords are moved
// one at a time, which is an operator= of its own being expanded.
//
// That is precisely the shape of LTVector (no operator=) and LTRotation (an
// in-class operator= assigning m_Quat[0..3] and returning *this), and the
// `(LTVector*)` / `(LTRotation*)` casts in Model::CModelSub1D4::sub_1001bf0d
// say the real source used them -- for the OTHER 0x1c record, CChildNodeXform
// below, which is exactly that pair -- and for this one too (see the
// operator's own note).
//
// CModelXform DOES have an empty default constructor and an in-class copy
// constructor that assigns its two members. The earlier "no constructor"
// reading was wrong, and its matches were look-alike credits from modelnew's
// CAnimXformQ444:
//   * BaseNew<CModelXform> (0x1001b71a) keeps the placement-new loop, folded
//     to a `jne` onto the next instruction. Without a constructor ours
//     inlined it and CopyArray2/Insert2/Remove2/InternalNiceSetSize
//     (0x1001b6a3, 0x1001a2b4, 0x1001a371, 0x10019633) missed.
//   * GenGetNext/GenGetAt (0x10018124/0x10018148) CALL a copy (0x100195b8)
//     where an implicit copy constructor gives a `rep movsl`.
class CModelXform
{
public:
	CModelXform() : m_Pos(0,0,0) { m_Rot.Init(); }

	CModelXform(const CModelXform& other)
	{
		m_Pos = other.m_Pos;
		m_Rot = other.m_Rot;
	}

	// 0x10016723 (42 bytes) / 0x004457e0 (55). DEFINED IN CLASS, and the
	// members are the SDK's own LTVector and LTRotation, not look-alikes of
	// them. lithtech.exe's /O2 CMoArray<CModelXform, NoCache> (vtable
	// 0x004a2708: GenAppend 0x00447a20, GenRemoveAt 0x00447b80, GenCopyList
	// 0x00447d10, GenAppendList 0x00447e60) EXPANDS this body -- the head as
	// dword moves, then a CALL to LTRotation::operator= (0x0047c460) -- which
	// an out-of-class definition can never give (/Ob1). The out-of-line copy
	// is still emitted for the sites that decline it (0x00445794 in
	// lithtech.exe, every element loop of server.dll's /Os arrays), so both
	// addresses stay matched. With local stand-ins for the two member types
	// (a float triple and a four-float class with its own operator=)
	// GenRemoveAt came out 405 bytes against 387 -- a different loop shape
	// -- so the member types are load-bearing too. Those five /O2 functions
	// used to be credited to collection.cpp's CMoArray<CCollElem1CS,
	// NoCache>, an instantiation the shipped image does not have (server.dll
	// says the NoCache 0x1c array assigns through this VOID operator, not
	// through CCollElem1CS's `this`-returning 0x100195b8).
	void operator=(const CModelXform& other)
	{
		m_Pos = other.m_Pos;
		m_Rot = other.m_Rot;
	}

	LTVector   m_Pos;                        // 0x00
	LTRotation m_Rot;                        // 0x0c
};

// 0x1c bytes, and NOT CModelXform: the per-node record of a child-model
// record (Model::CModelSub1D4::m_Array00). Its DefaultCache array's element
// copies all reach 0x100195b8 -- as the assignment in GenCopyList
// (0x1001820a) and as the copy in GenGetNext/GenGetAt (0x1001813b,
// 0x10018159) -- which returns `this`, while the NoCache CModelXform array
// assigns through 0x10016723, which returns void. Two assignment operators
// means two element types. sub_1001bf0d reads it with w_ReadVector and
// w_ReadRotation, so it is the SDK pair itself, with every special member
// implicit (name ours).
class CChildNodeXform
{
public:
	LTVector   m_Pos;                        // 0x00
	LTRotation m_Rot;                        // 0x0c
};

// Model::m_Unknown054's element, 0x14 bytes: four floats and a dword, with
// an out-of-line constructor at 0x10016478 that zeroes all five (the dword
// first). BaseNew for it (0x1001b97b, `lea (%esi,%esi,4); shl $2`) calls
// that constructor per element, which is what ~Model's release of the
// collection (0x10019d2e) reaches. Fields by offset (names ours).
class CModelVertRec
{
public:
	CModelVertRec();

	float  m_f[4];                           // 0x00
	uint32 m_n10;                            // 0x10  read first
};

// Model::m_Unknown08c's element: one float, zeroed by an in-class
// constructor -- BaseNew for it (0x1001ba2b) is the `fldz; fstps` loop, the
// shape a four-byte element with a constructor gives and a bare float or
// pointer does not (name ours).
class CModelFloatRec
{
public:
	CModelFloatRec() { m_f = 0.0f; }

	float m_f;
};

class ModelAnim;
class ModelAnimationNode;

class ModelNode
	{
	public:
		// 0x1001bd54 — the "Nodes" chunk: the node tree out of the file
		// (name ours).
		LTBOOL sub_1001bd54(class ILTStream* pStream);

		ModelNode();
        ModelNode(const ModelNode&) = delete;
        ModelNode& operator=(const ModelNode&) = delete;                      // 0x1001694e
		// 0x10016990 — the same object built against a model: identical to
		// the default constructor except that the model goes into +0xac
		// instead of a zero. It is what slot 0x08 news up.
		ModelNode(Model* pModel);         // 0x10016990
		virtual ~ModelNode();
		// 0x100169dd — the destructor's whole body, and the model's own
		// release (0x10017016) calls it too, which is what says it is a
		// member rather than the destructor written inline: it hands the
		// collection at +0x18 back to the model's data block (the model is
		// at +0xac and its block at +0x174) and then runs the reset below
		// (names ours).
		void   sub_100169dd();               // 0x100169dd
		// 0x10016a00 — the reset the constructor ends with and the release
		// runs again: three floats, two counters and a name pointer back to
		// their defaults, plus two sub-records at +0x2c and +0x6c cleared
		// through 0x10011e2b. Declared only — its fields are not named yet
		// (name ours).
		void   sub_10016a00();               // 0x10016a00
		// Slot 0x04 is `ret $0xc` — three dwords of arguments and no body,
		// which is all that fixes its shape. Slot 0x08 is a factory: it
		// asks the engine allocator for 0xb4 bytes — this class's own size,
		// which is the independent confirmation of the extent — and runs
		// the model-taking constructor on it. Slot 0x0c is the one Model's
		// constructor calls (names ours).
		virtual void _v04(uint32 a, uint32 b, uint32 c);   // 0x10016971
		virtual ModelNode* Create(Model* pModel);       // 0x10016a37
		virtual void SetModel(Model* pModel);              // 0x10016a58

		// 0x10016a9b — how many nodes this subtree is: itself plus every
		// child's answer, recursively. Forty bytes, and the one that says
		// the array at +0x18 is the child list (name ours).
		uint32 sub_10016a9b();
		// 0x10016ac3 — walk the subtree into the owning model's node array,
		// taking the next index from the caller's counter. Declared only
		// (name ours).
		LTBOOL sub_10016ac3(uint32* pnIndex);
		// 0x10016b11 — set this subtree's own index, recursively.
		// Declared only (name ours).
		void   sub_10016b11(uint32 nIndex);

		// 0x10016304 — do two node trees have the same SHAPE? A free
		// function rather than a member: it compares child counts level by
		// level and recurses pairwise, and the first node whose count
		// disagrees goes out through the third argument. Deeper mismatches
		// are reported by the recursion that found them, which is why this
		// level only stores on its own test (name ours).
		friend LTBOOL mod_SameNodeTree(ModelNode* pA, ModelNode* pB,
		                               ModelNode** ppMismatch);

		// 0x04 .. 0x12 — what the reset at 0x10016a00 writes: three floats
		// zeroed through `fldz`/`fstps`, a word and a byte cleared with
		// `andw`/`andb` (which is what fixes their widths), and the name
		// pointer at 0xb0 put on the shared empty string. Named for their
		// offsets; nothing reads them in the reconstructed set.
		float  m_f04;                        // 0x04
		float  m_f08;                        // 0x08
		float  m_f0c;                        // 0x0c
		uint16 m_n10;                        // 0x10
		uint8  m_b12;                        // 0x12

		// The elements are ModelNode* and the walk in SetModel says so:
		// it calls slot 0x0c on each of them, with the same model, which is
		// this class's own SetModel and no other (name ours).
		CMoArray<ModelNode*, NoCache> m_Array18;   // 0x18
		uint32 m_iParent = 0xffffffff;
		// 0x2c and 0x6c — two 4x4s, and the pair is what closes this class:
		// the reset runs LTMatrix::Identity (0x10011e2b, the same call
		// GetNodeTransform makes) over +0x2c and then over +0x6c, and
		// 0x6c + 0x40 lands exactly on the model back-pointer below.
		LTMatrix m_Mat2c;                    // 0x2c
		LTMatrix m_Mat6c;                    // 0x6c
		// 0xac — the model this belongs to, and not merely a pointer: the
		// release at 0x100169dd loads it and then reads +0x174 off it,
		// which is Model::m_pDataBlock. SetModel (slot 0x0c) is what fills
		// it; Model's constructor calls that with `this`.
		Model* m_pModel;                     // 0xac
		char*  m_pName;                      // 0xb0  the shared empty string
	};


// EvalSocket reads every field of a socket: the offset the attachment sits
// at, the rotation it sits at, and the node it hangs off. The name's extent
// is fixed by the node index that follows it (names ours).
class ModelSocket
{
public:
	// 0x10016c95 — the origin, an identity rotation, no name and no node.
	// The 1.0f it puts at +0x18 is the quaternion's w, which is what fixes
	// the four floats at +0x0c as a rotation and not another vector.
	ModelSocket();

	LTVector   m_vPos;                       // 0x00
	LTRotation m_rRot;                       // 0x0c
	char       m_Name[0x2c - 0x1c];          // 0x1c
	uint32     m_iNode;                      // 0x2c
	// 0x30 — the constructor zeroes it and nothing else here reads it. The
	// record is 0x34 bytes, which its own allocator pins: LoadSockets goes
	// through the BaseNew instantiation at 0x1001ccaa, and collection.cpp
	// records that one's element size as 0x34 (name ours).
	uint32     m_Unknown30;                  // 0x30
};

// The two remaining named tables; both keep the name inline (names ours).
// One piece: 0x6c bytes, which its own release pins (BaseDelete at
// 0x1001b641 strides by 0x6c and runs a destructor at 0x10016c14) and its
// reader confirms (0x1001c1a5 fills 0x24 through 0x68 and reads the name
// with a limit of 0x20 into +0x48, and 0x48 + 0x20 + the model pointer lands
// exactly on 0x6c). It is NOT a POD: the destructor the release runs is a
// real one, which is why it is declared here.
//
// The first 0x24 bytes are a record of their own — the vertex-data record
// 0x1001bf85 reads, which is called both on the piece itself and on each
// element of the 0x24-strided collection at +0x34, so the piece opens with
// one of them and carries a list of the rest (names ours).
class ModelVertex
{
public:
    CModelVertRec* weights=nullptr;
    uint16 weight_count=0,replacement=0;
    LTVector position{0,0,0},normal{0,0,0};
};
using ModelPieceLod=ModelVertex;

class ModelTriangle
{
public:
    uint16 indices[3]{};
    float uv[3][2]{};
};
using ModelPieceDimension=ModelTriangle;

class ModelPieceData
{
public:
	// 0x1001bf85 / 0x0044d3f0.
	LTBOOL ReadVertexData(class ILTStream* pStream, uint32* pnBase);
	// 0x00446090 -- `mov 0x4(%esp),%eax; mov %eax,0x20(%ecx); ret $0x4`.
	void   SetModel(class Model* pModel);
	ModelPieceData() : m_pModel(nullptr) {}
    ModelPieceData(const ModelPieceData& other);
    ModelPieceData& operator=(const ModelPieceData& other);
    explicit ModelPieceData(Model* model) : m_pModel(model) {}                        // through BaseNew at 0x0044cae0
	~ModelPieceData();

	CMoArray<ModelPieceLod, NoCache> m_Array00;   // 0x00
	CMoArray<ModelPieceDimension, NoCache> m_Array10;   // 0x10
    std::span<const ModelVertex> vertices() const { return {m_Array00.GetArray(),m_Array00.GetSize()}; }
    std::span<const ModelTriangle> triangles() const { return {m_Array10.GetArray(),m_Array10.GetSize()}; }
	class Model* m_pModel;                        // 0x20
};

// commonmodel.h's ModelPiece with the first 0x24 bytes and m_Array34 given
// their real types.  Same offsets, same size (0x6c).
class ModelPiece : public ModelPieceData
{
public:
    explicit ModelPiece(Model* model);
    ~ModelPiece();
    ModelPiece(const ModelPiece&) = delete;
    ModelPiece& operator=(const ModelPiece&) = delete;
    ModelPieceData* GetPieceData(uint32 i) { return i == 0 ? this : i <= m_Array34.GetSize() ? &m_Array34[i - 1] : nullptr; }
    const ModelPieceData* GetPieceData(uint32 i) const { return i == 0 ? this : i <= m_Array34.GetSize() ? &m_Array34[i - 1] : nullptr; }
    uint32& m_nSize = m_Array00.m_nElements;
	// 0x1001c1a5 / 0x0044d6e0.
	LTBOOL sub_1001c1a5(class ILTStream* pStream, uint32* pnBase);

	uint32 m_Unknown24;                      // 0x24
	uint32 m_nOffset;                        // 0x28
	float  m_f2c;                            // 0x2c
	float  m_f30;                            // 0x30
	CMoArray<ModelPieceData, NoCache> m_Array34;  // 0x34
	uint32 m_Unknown44;                      // 0x44
	char   m_Name[0x68 - 0x48];              // 0x48
	class Model* m_pModel68;                 // 0x68
};



// One weight set: a sixteen-byte name and one four-byte weight per node.
// 0x28 bytes, which is what its own allocator bakes in (0x1001cce3), and its
// constructor takes the model (0x10016c4e) — which is where the collection at
// +0x10 gets the allocator it grows against (names ours).
class ModelWeightSet
{
public:
	// 0x10016c4e — build one against a model. Declared only (name ours).
	ModelWeightSet(class Model* pModel);
	// 0x10016c69 / 0x004462c0 -- the weights back to the model's allocator.
	// Out of line: /O2's BaseDelete calls it (0x0044dd6e) rather than
	// expanding it.
	~ModelWeightSet();
	// 0x1001c2bc — the set off the wire: the name, the count, then one
	// weight per node (name ours).
	LTBOOL sub_1001c2bc(class ILTStream* pStream);

	// The name is read with ILTStream::ReadString straight into the record
	// with a limit of 0x10, which is what fixes its size.
	char   m_Name[0x10];                     // +0x00
	CMoArray<float, DefaultCache> m_Weights;// +0x10 .. 0x24
	class Model* m_pModel;                   // +0x24
};

// One entry per animation in the model's table; the animation itself is the
// first field, which is why the accessors index by 0x20 and then dereference.
class AnimInfo
{
public:
	// 0x10016922 — out of line: every element BaseNew makes for the
	// model's animation table (0x1001ba5b, stride 0x20) calls it.
	AnimInfo();

	// 0x1001bec9 — this entry off the wire: the dimensions, then the
	// animation record itself (name ours).
	LTBOOL sub_1001bec9(class ILTStream* pStream);

	ModelAnim* m_pAnim;                      // 0x00
	// 0x04 — the child-model record the animation belongs to. Model's own
	// reader fills it (`mov %ecx,0x4(%edi,%edx,1)` with `lea 0x1d4(%ebx)`
	// in ecx) for every entry it allocates, which is the model's own
	// record, and the animation reader then re-homes it onto whatever
	// child the file names (name ours).
	// (typed void* because CModelSub1D4 is nested inside Model, which is
	// declared below; nothing here dereferences it.)
	void*      m_pChildModel;                // 0x04
	LTVector   m_vDims;                      // 0x08  what ModelEdit set
	// 0x14 — what the "AnimBindings" chunk writes (0x0044dad1-0x0044dade).
	LTVector   m_vAnimBinding;               // 0x14
};

using IMoAllocator = LAlloc;

// 0x100163fa — a slot that can be filled once: a second call is refused with
// LTFALSE and changes nothing. Model builds one at +0x84 with the first of
// its two allocators (names ours).
// 0x1001638c is what the class turns out to be: not a "data reference" but
// the model's own STRING STORE. Every name a model file carries — the model's
// own, each animation's, each keyframe's script key — is handed to it and
// comes back as a pointer the model owns, and an identical name handed to it
// twice comes back as the SAME pointer, because the store searches before it
// allocates. One block per distinct string, each carrying its own length and
// the next block's address ahead of the bytes.
class CMoDataLink
{
public:
	// 0x00, 0x04 and 0x08, read off 0x1001638c: the block's total size
	// (strlen + 0xc, so twelve bytes of overhead for two dwords and the
	// terminator's alignment), the next block, and the string itself
	// (names ours).
	uint32       m_nSize;                    // 0x00
	CMoDataLink* m_pNext;                    // 0x04
	char         m_Name[1];                  // 0x08
};

class CMoDataRef
{
public:
	// 0x10016352 — build one: the flag word cleared (`andl $0x0` — /O1's
	// four-byte store of zero) and the pointer taken from the one argument,
	// with `mov %ecx,%eax` hoisted to the top of the body because loading
	// that argument is what clobbers `this`. It hands `this` back in eax,
	// which is what says it is a constructor rather than an initialiser.
	// Model builds the one at its +0x84 with its first allocator, so what
	// the pointer means is the caller's business; from here it is only the
	// same slot Set fills.
	CMoDataRef(void* pData);                 // 0x10016352
	// 0x10016361 — five bytes, a tail `jmp` into the release below, which
	// is the shape VC6 gives a destructor whose whole body is one call.
	~CMoDataRef();                           // 0x10016361
	// 0x10016366 — walk the block list and hand every block back to the
	// allocator, then clear the head. Reached from the destructor and from
	// Model's own release, so it is a member in its own right and not the
	// destructor body. (The earlier reading of the link layout here — next
	// at +0x00, an allocator at +0x04 — was wrong: 0x1001638c builds the
	// blocks with the SIZE at +0x00 and the next at +0x04, and the
	// allocator is the store's, not the block's.)
	void   sub_10016366();                   // 0x10016366
	LTBOOL Set(void* pData);                 // 0x100163fa
	// 0x1001638c — a name into the store, and the copy the store owns back
	// out. A null or empty name, and a refused allocation, all answer the
	// shared empty string rather than null, which is why every reader can
	// strlen what it gets back (name ours).
	char*  sub_1001638c(char* pName);

	// 0x00 — the head of the block list, and the same word Set treats as
	// "already claimed": a store with anything in it will not take a new
	// allocator.
	CMoDataLink* m_pLinks;                   // 0x00
	// 0x04 — where the blocks come from. Model builds the store at its
	// +0x84 with its first allocator, and 0x1001638c reaches slot 1 of it,
	// which is what says the field is the allocator and not payload.
	class LAlloc* m_pAlloc;                  // 0x04
};

// The thirteen dwords the model file's "Header" chunk is: 0x1001ce0a reads
// them one at a time through the stream's slot 8 (four bytes each, the count
// held in ebx across all thirteen) into this+0x00 .. this+0x30 and then asks
// the stream whether anything went wrong, so the record is exactly 0x34 bytes
// and every field of it is a dword off the wire. Which count is which is not
// readable from here — only 0x1001cec1's arithmetic over them is — so they
// stay an array (names ours).
//
// 0x1001cde1 clears it: `xor eax,eax` and thirteen stores. It is NOT the
// constructor, even though CModelLoadState's constructor is the only thing
// that calls it — a VC6 constructor hands `this` back in eax and this one
// leaves the zero register there, so it is an ordinary void member and the
// load state's constructor body calls it (name ours). It also has to stay out
// of line: /O1 implies /Ob1, so a body written in the class declaration would
// be expanded into the caller and the call at 0x1001cdd8 would vanish.
class CModelFileCounts
{
public:
	void   Clear();                          // 0x1001cde1
	LTBOOL Read(class ILTStream* pStream);   // 0x1001ce0a

	uint32 m_n[13];                          // 0x00 .. 0x30
};

// 0x1001cdd5 — the scratch the model reader keeps while it walks a file:
// 0x34 bytes on Model::Load's own stack (the frame is 0x38 and the byte
// count sits in the last dword), and a constructor that builds one
// sub-object at offset zero and does nothing else. Only the one method
// Load reaches is declared, and it is named for its address because what it
// answers is not readable from the call site alone — it hands back a byte
// count and a flag for whether there is anything to allocate at all
// (names ours).
class CModelLoadState
{
public:
	CModelLoadState();                       // 0x1001cdd5
	LTBOOL sub_1001cec1(uint32* pnBytes);    // 0x1001cec1

	CModelFileCounts m_Counts;               // 0x00 .. 0x34
};

// Shared native model state. Instance construction and tracker initialization
// need these records independently of the file loader's polymorphic lifetime.
struct ModelRuntimeData {
    char* m_pFilename = nullptr;
    float m_f0a8 = 50;
    CMoArray<AnimInfo, NoCache> m_AnimArray;
};

class Model : public ModelRuntimeData
{
public:
	// Two allocators, and the same two at both call sites: the manager's
	// placeholder is built with the packet bank's, and a model loaded from
	// file with the collection allocator's.
	//
	// NEAR (546 of 560 bytes, and the fourteen are one inline decision).
	// The body is reconstructed in commonmodel.cpp and every statement of
	// it is accounted for; what does not reproduce is that the shipped
	// build expanded `CMoArray<T, NoCache>::CMoArray(uint32)` inline at the
	// first of the two collection temporaries, and no VC6 in toolchains/
	// will do that at /Ob1. The full finding is on the definition.
	//
	// Fourteen sub-objects run before the body, in offset order, which is
	// member declaration order: 0x14 (0x100243f6), 0x24 (0x1001831d),
	// 0x34 (0x10018b5e), 0x44 (0x10018c1b), 0x54 (0x10018c92),
	// 0x70 (0x10018e8f), 0x84 (0x10016352, one argument — pAlloc1),
	// 0x8c (0x10019104), 0xa0 (no call at all — the one member the
	// constructor initialises from its list, `fldz`/`fstps` between the
	// 0x8c and 0x154 constructors, which is where declaration order puts
	// it and nowhere the body could be scheduled to), 0x154 (0x1001927c),
	// 0x164 (0x10019364), 0x17c (0x1003d330 — CMoMemBlock), 0x1d4
	// (0x100164bb) and 0x20c (0x1001694e).
	//
	// The two temporaries are both whole `CMoArray<T, NoCache>` locals and
	// they share the single 0x10-byte stack slot the frame reserves, which
	// is what says their scopes are disjoint. Each is `CopyArray2`'d into a
	// member (0x1001b76a for the one at 0x24, 0x1001b9b5 for the one at
	// 0x70, both with &g_DefAlloc, i.e. operator= -> CopyArray) and then
	// destroyed where its scope ends: the vptr re-stored and Term() run
	// (SetSize2 0x1001a03d and 0x10019e14). Both are NAMED locals rather
	// than temporaries, and the second one proves it — its constructor is
	// an out-of-line call that hands `this` back in eax, and the shipped
	// code still recomputes the address with `lea -0x10(%ebp),%eax` instead
	// of using that return value, which is what a named local does and a
	// temporary does not.
	Model(LAlloc* pAlloc1, LAlloc* pAlloc2);   // 0x10016cc5

	// Polymorphic: FreeUnusedModels deletes through vtable slot 0.
	// 0x10016f26 — the body is three statements (the release call below and
	// two subtractions off the running data total); the thirteen sub-object
	// destructors after it are the compiler's, in reverse declaration
	// order, and they are what confirms the member list end to end.
	virtual ~Model();

	// 0x10017016 — give back everything the model's data block owns: the
	// sub-objects at +0x14 and +0x20c, the collections at 0x24, 0x54, 0x70,
	// 0x44 and 0x154, the data reference at 0x84 and the filename. Reached
	// only from the destructor in the reconstructed set, with a literal 1,
	// so what the flag selects is not readable and the name is its address.
	void   sub_10017016(LTBOOL bFlag);
	// 0x100170f2 — the animation table given back: every record whose
	// animation says it belongs to THIS model is released, and the table
	// itself is then emptied against the model's own allocator (+0x178)
	// rather than its data block, which is the one place in the release
	// that uses the other one. An animation that came in with a child model
	// belongs to that model and is left alone (name ours).
	void   sub_100170f2();
	// 0x10017145 — the child-model table reset to just this model's own
	// record. Every other record is unhooked: the model it stands for loses
	// a reference (m_CacheCount) and, if the flag is set and that was the
	// last one, is deleted outright; then the record itself is reset and,
	// unless it is one of the two the model owns, handed back. The flag is
	// what the whole release takes as its argument (name ours).
	void   sub_10017145(LTBOOL bFlag);
	// 0x100171f4 — everything the model has to recompute once its node
	// tree and its data are in place, and the one function the reader calls
	// twice (once mid-file with 1, once at the end with 0). It resizes the
	// two node-indexed tables (0x1001728c, 0x100172e3), lays the pieces out
	// end to end in the vertex data, hands every animation of its own the
	// same flag, and finally fills the two counters at +0x64/+0x68. Any
	// step failing stops it (name ours).
	LTBOOL sub_100171f4(LTBOOL bFlag);
	// 0x1001728c — the node-transform array sized to the node count. With
	// the flag clear and the array already the right size it is left alone,
	// which is what makes the flag "rebuild even if it looks current"
	// (name ours).
	LTBOOL sub_1001728c(LTBOOL bFlag);
	// 0x100172e3 — the node array itself sized to the node count, cleared,
	// and then filled by the tree walking itself into it; the node count
	// divided by four (rounded up) is cached at +0x6c, and the tree's own
	// index is reset to all-ones afterwards. Same "already current" test as
	// its neighbour (name ours).
	LTBOOL sub_100172e3(LTBOOL bFlag);
	// 0x10017517 — every node's offset from its parent, recomputed: the
	// parent's transform inverted, times the node's own, and the
	// translation of the product kept. A node whose parent index is out of
	// range is a root and gets the origin. Three matrix-sized slots on the
	// stack, which is one named local and the two expression temporaries
	// `~a * b` needs (name ours).
	void   sub_10017517();
	// 0x1001c560 — the weight-set table off the wire: the old table given
	// back, a count read, one record allocated per entry and each read in
	// turn. Any failure gives the whole table back again (name ours).
	LTBOOL sub_1001c560(class ILTStream* pStream);
	// 0x10017660 and 0x1001761d — the two totals sub_100171f4 caches.
	// Declared only (names ours).
	uint32 sub_10017660();
	uint32 sub_1001761d(LTBOOL bFlag);

	char* GetFilename();                     // 0x10017b0e
	// 0x10017b12 — take a copy of the name the model was loaded from;
	// nonzero if the copy could be made (name ours).
	LTBOOL SetFilename(char* pFilename);
	// 0x10017b4a — give the copy back and go back to the shared sentinel
	// (name ours).
	void   FreeFilename();
	// 0x1001cbae — read the model out of a stream, answering an LTRESULT.
	// The allocator is an IMoAllocator (declared above): serverengine.cpp's
	// two call sites hand it the collection allocator bank.
	LTRESULT Load(class ILTStream* pStream, IMoAllocator* pAlloc);
	// 0x1001cb56 — the file's "Header" chunk: it opens the chunk by that
	// name, reads four bytes of version into m_nFileVersion, accepts only
	// 9, 10, 11 and 12, and then hands the stream to the load state. The
	// name is the chunk's, which is the only naming evidence there is.
	LTBOOL LoadHeader(class ILTStream* pStream, CModelLoadState* pState);

	// 0x1001c47b — the socket table off the wire.
	LTBOOL LoadSockets(class ILTStream* pStream);
	// 0x1001c604 — the whole model-data reader, and the child models are one
	// of the things it finds. Reconstructed in commonmodel.cpp; a near, one
	// instruction over 438 at the SIB base/index order, the commuting of two
	// address additions and the placement of the cold failure block. The name
	// is ours; `ret 4` and the 0x3c (LT_INVALIDPARAMS) answer for a request
	// with no stream are read off the binary.
	//
	// It walks four named chunks in order -- "Pieces", "Nodes", "ChildModels",
	// "Animation" -- through the same 0x1001bac6 chunk-finder LoadHeader uses,
	// sizing a collection off the wire before each. Its child-model half is
	// what named m_pChildModels/m_nChildModels below, and the walk over
	// `this + 0x34` at 0x1001c758 is what proved m_Pieces is a collection
	// rather than the flat m_pPieces/m_nPieces this header once named.
	LTRESULT LoadChildModels(class CModelLoadRequest* pRequest);

	// (declared below, inside this class)
	class CModelSub1D4;

	// LoadChildModels' remaining callees, named for their addresses and
	// declared from its call sites (names ours):
	//
	// 0x10017b8a — put one child-model record in a slot: the slot index, the
	// record, the model it stands for and the name the file gave it. The
	// model's own record goes in slot zero under the name "SELF".
	LTBOOL sub_10017b8a(uint32 iSlot, class CModelSub1D4* pRec,
	                    Model* pModel, char* pName);
	// 0x10017b69 — whatever a child record's +0x2c holds, derived from the
	// model at its +0x28. Reached only when that model is set.
	uint32 sub_10017b69(Model* pModel, ModelNode** pOut);
	// 0x1001c36c — the "AnimBindings" chunk (271 bytes, not reconstructed).
	LTBOOL sub_1001c36c(class CModelLoadRequest* pRequest,
	                    class ILTStream* pStream, int nFlag);
	// 0x100176dc — the property table (1001 bytes, not reconstructed).
	void   sub_100176dc();

	// Things to run when this model goes; a child model puts the callback
	// that unhooks it from the manager's hash in here (name ours).
	uint8      m_Cleanups[0x10 - 0x008];     // 0x008 .. a list head at 0x014
	// 0x010 — the model's own address, stored by the constructor and read
	// by nothing else that is reconstructed (name ours).
	Model*     m_pSelf010;                   // 0x010
	// 0x014 — the first of the thirteen sub-objects, and NOT a collection.
	// Its constructor (0x100243f6) is two instructions of setup around one
	// call to 0x10024409, and that callee is the whole object: `andl $0,(%ecx)`
	// then `mov %eax,0x4(%ecx)`, with the argument the constructor passes as
	// zero, answering 1. Two dwords, and the first is zeroed rather than given
	// a vtable, so there is no vptr and nothing polymorphic here.
	//
	// That settles what was the one unresolved offset in this layout. Eight
	// bytes means it ends at 0x1c, so m_FileFlags and m_Flags below are real
	// fields rather than this object's array and count -- and the reading of
	// m_Flags as a flag word whose bit 0 means "referenced by an object"
	// stands. Had it been a 0x10 collection like its siblings, those two
	// would have had to be an element pointer and a count instead.
	//
	// The setter takes its second field by argument and returns LTBOOL, which
	// is an Init rather than a constructor body; the names here are ours.
	using CModelSub14 = ::CModelSub14;

	CModelSub14 m_Sub014;                    // 0x014

	// The last three sub-objects are not collections, and their extents are
	// now read off their own constructors rather than guessed:
	//
	//   0x17c  CMoMemBlock, at most 0x14 bytes (Model has fields at 0x190 and
	//          0x1d0 before the next sub-object). Its constructor 0x1003d330
	//          stores a vptr (0x1004eb00) and sets up an SEH frame -- `push
	//          $-1; push handler; mov %fs:0x0` -- so it owns something whose
	//          construction can throw and needs an unwind funclet. That is a
	//          problem for reconstructing 0x1003d330 itself and NOT for
	//          Model::Model, which merely calls it: 0x10016cc5 opens with a
	//          plain frame and no SEH, and the call is a masked relocation, so
	//          CMoMemBlock only has to be DECLARED with a constructor here.
	//
	//   0x1d4  0x38 of span, of which 0x30 is confirmed touched: constructor
	//          0x100164bb calls a base at 0x10018102 (covering 0 to 0x14),
	//          then writes 0x14, 0x18, 0x24 and 0x28 as zero, 0x1c as the
	//          shared empty string, and 0x2c as 1. Model stores its own
	//          `this` into that object's 0x24 and 0x28 afterwards, which is
	//          what two of those zeroed fields are for. Whether the last
	//          eight bytes of the span belong to it or are unnamed Model
	//          fields is the one thing still open here.
	//
	//   0x20c  0xb4 bytes, ending at 0x2c0 exactly where Model stores a
	//          pointer back to it. Constructor 0x1001694e: a CMoArray member
	//          at +0x18 built first (0x1001831d, the NoCache constructor),
	//          then this object's own vptr (0x1004da24), then a body call
	//          (0x10016a00), then 0xac zeroed. Its destructor is 0x100169b8 --
	//          same vptr, and it tears down the +0x18 collection, which
	//          confirms the member.
	// Whatever the file manager knew about the file this came out of,
	// carried over verbatim when the model is loaded (name ours).
	uint32     m_FileFlags;                  // 0x01c
	uint32     m_Flags;                      // 0x020  bit 0 = referenced by an object
	CMoArray<class ModelNode*, NoCache> m_Nodes;           // 0x024
	CMoArray<ModelPiece*, NoCache>      m_Pieces;          // 0x034
	CMoArray<ModelWeightSet*, NoCache>  m_WeightSets;      // 0x044
	// 0x054 — a fifth collection (constructor 0x10018c92, the NoCache
	// CMoArray()). Its array and count at 0x058/0x05c are not named by
	// anything yet; the two dwords the constructor's body zeroes at 0x064 and
	// 0x06c are past its end.
	CMoArray<CModelVertRec, NoCache>    m_Unknown054;      // 0x054
	// 0x064, 0x068 and 0x06c — three counters the reader computes once the
	// model's data is in place: 0x064 and 0x068 from the two members at
	// 0x10017660 and 0x1001761d (0x100171f4), and 0x06c as the node count
	// divided by four, rounded up (0x100172e3). All three are zeroed by the
	// constructor and 0x064 again by the release (names ours).
	uint32     m_Unknown064;                 // 0x064
	uint32     m_Unknown068;                 // 0x068
	uint32     m_Unknown06c;                 // 0x06c
	CMoArray<LTMatrix, NoCache>         m_NodeTransforms;  // 0x070
	char*      m_pCommandString;             // 0x080  GetModelCommandString copies it
	CMoDataRef m_DataRef;                    // 0x084
	// 0x08c — the one DefaultCache collection of the thirteen (constructor
	// 0x10019104), so 0x14 bytes rather than 0x10: the policy carries a cache
	// size at +0x0c and a wanted-cache at +0x10 where NoCache carries one
	// padded byte.
	CMoArray<CModelFloatRec, DefaultCache> m_Unknown08c;   // 0x08c
	// 0x0a0 is the one field the constructor sets from its initialiser list
	// rather than its body: the `fldz`/`fstps` lands between the collection
	// constructor at 0x08c and the one at 0x154, which is where declaration
	// order puts it and nowhere the body could be scheduled to.
	float      m_f0a0;                       // 0x0a0  0.0f
	float      m_f0a4;                       // 0x0a4  96.0f

	uint32     m_Unknown0ac;                 // 0x0ac  zeroed
	// Two lengths and each one's square, set together by the member below
	// and by nothing else that is reconstructed; the constructor's default
	// for both is 20000.0f. What they measure is not readable from either
	// side, so they are named for their offsets.
	float      m_f0b0;                       // 0x0b0
	float      m_f0b4;                       // 0x0b4  m_f0b0 * m_f0b0
	float      m_f0b8;                       // 0x0b8
	float      m_f0bc;                       // 0x0bc  m_f0b8 * m_f0b8
	// 0x154 and 0x164 — the last two collections of the thirteen. Their
	// interiors are what this header used to name flat: the one at 0x154 puts
	// its array on 0x158 and count on 0x15c, the one at 0x164 on 0x168 and
	// 0x16c. Renaming those four reached 27 call sites across six files --
	// and deliberately NOT the seven in spritecontrol.cpp and worldmodels.cpp,
	// where m_nAnims is a sprite's own field and m_Anims is a CWDArray04 on
	// another class entirely.
	uint32     m_Unknown0c0;                 // 0x0c0  zeroed
	// 0x0c4 .. 0x0e4 — five pairs of floats, each pair written as one
	// chained assignment (100, 200 and 50) or as a lone constant (0.25 and
	// 0.75). Named for their offsets: the constructor is the only
	// reconstructed writer and nothing reads them yet.
	float      m_f0c4;                       // 0x0c4  100.0f
	float      m_f0c8;                       // 0x0c8  100.0f
	float      m_f0cc;                       // 0x0cc  0.25f
	float      m_f0d0;                       // 0x0d0  0.75f
	uint32     m_Unknown0d4;                 // 0x0d4  zeroed
	float      m_f0d8;                       // 0x0d8  200.0f
	float      m_f0dc;                       // 0x0dc  200.0f
	float      m_f0e0;                       // 0x0e0  50.0f
	float      m_f0e4;                       // 0x0e4  50.0f
	LTVector   m_Vec0e8;                     // 0x0e8  Init()ed to the origin
	int32      m_n0f4;                       // 0x0f4  -1
	int32      m_n0f8;                       // 0x0f8  -1
	// 0x0fc — an LTMatrix, exactly 0x40 bytes: sub_100176dc copies it out of
	// m_NodeTransforms.m_pArray[m_n0f4] with one `rep movsl $0x10`
	// (0x10017a66). Named, not resized; nothing else reads this pad.
	LTMatrix   m_mNormalRef0fc;              // 0x0fc
	uint32     m_Unknown13c;                 // 0x13c  zeroed
	uint32     m_Unknown140;                 // 0x140  zeroed
	// 0x144 / 0x148 — FovX and FovY offsets in radians. sub_100176dc writes
	// both from atof through MATH_DEGREES_TO_RADIANS, whose constant at
	// 0x1004dc80 matches ltbasedefs.h's bit for bit. Two floats, same eight
	// bytes the pad covered.
	float      m_f144;                       // 0x144
	float      m_f148;                       // 0x148
	uint32     m_Unknown14c;                 // 0x14c  zeroed
	uint32     m_Unknown150;                 // 0x150  zeroed
	CMoArray<class ModelSocket*, NoCache> m_Sockets;    // 0x154


	// Name lookups, each returning LTFALSE when the name is not in the model.
	// Three out-parameters' worth of one lookup: the index, the whole
	// table entry, and the animation itself as the return value. Both
	// pointers are optional and each call site passes a different pair,
	// which is why all three exist (the SDK has no declaration for this).
	// 0x1001767a — the total animation count over this model and its
	// children: it walks m_pChildModels, follows each record's owner back to
	// a Model and adds that model's animation-table size. The flag decides
	// whether entry ZERO counts, and entry zero is this model itself (the
	// constructor puts &m_Sub1d4 there), so a false flag means "the children
	// only". Records whose owner is null are skipped (name ours).
	uint32 GetTotalAnimCount(LTBOOL bIncludeSelf);
    uint32 OwnedAnimationCount() const;
	// 0x100174b9 — FindAnim narrowed to one model: the same walk over
	// m_AnimArray, but an entry only qualifies when its animation's m_pModel
	// is the one asked for. It answers the ENTRY rather than a flag, and the
	// index goes out through the optional third argument (name ours).
	AnimInfo* FindAnimInModel(char* pName, class Model* pModel,
	                          uint32* pIndex);
	LTBOOL FindAnim(char* pName, uint32* pIndex, AnimInfo** ppInfo);// 0x10017452
	LTBOOL FindSocket(char* pName, HMODELSOCKET* pSocket);         // 0x10017bcd
	LTBOOL FindNode(char* pName, HMODELNODE* pNode);               // 0x1001740c
	LTBOOL FindPiece(char* pName, HMODELPIECE* pPiece);            // 0x1001737b
	LTBOOL FindWeightSet(char* pName, HMODELWEIGHTSET* pSet);      // 0x100173bc

	// 0x10017add — store two lengths and cache each one's square. Named for
	// its address: the four fields it writes are reached from nowhere else in
	// the reconstructed set, so nothing says what they are.
	void   sub_10017add(float f1, float f2);

	// 0x100173fa — take a name and answer the copy of it this model owns.
	// Both callers hand it the name they already hold and store the answer
	// straight back over it, so it re-homes a string onto the model rather
	// than duplicating it. Named for its address (name ours).
	char*  sub_100173fa(char* pName);

	// 0x1001c325 — read one name off a stream (a kilobyte of stack, filled
	// by ILTStream::ReadString) and hand back the copy this model owns. It
	// is a Model member rather than a free function because the copy comes
	// off the model's own store — every name in a model file is read
	// through it (name ours).
	LTBOOL sub_1001c325(class ILTStream* pStream, char** ppName);

	// 0x10017c17 — run one socket through the walker and hand back its 4x4
	// (name ours).
	LTBOOL EvalSocket(class ModelWalker* pWalker, HMODELSOCKET hSocket, LTMatrix* pOut);

public:
	// Load points this at the block below once the block owns memory, and
	// records the allocator beside it (names ours).
	// 0x174 — the data block, and the bytes want it reached through an
	// accessor rather than read as a member: every call that hands it to a
	// collection materializes it in a REGISTER before pushing it, where a
	// plain member read pushes the memory operand instead (0x1001bf0d,
	// 0x1001bb37). An inlined member function is what puts the value in a
	// register — the same fingerprint seen for inlined
	// accessors materializing `this`.
	class LAlloc* GetAlloc() { return (class LAlloc*)m_pDataBlock; }

	LAlloc* m_pDataBlock;               // 0x174
	IMoAllocator* m_pAlloc;                  // 0x178
	CMoMemBlock m_DataBlock;                 // 0x17c .. 0x190
	// 0x1d4 — the twelfth sub-object, and its size is pinned rather than
	// bounded: 0x1d4 + 0x30 lands exactly on m_nFileVersion, so it is 0x30
	// bytes, not the 0x38 the gap to the next sub-object suggested. Its
	// constructor (0x100164bb) runs a base at 0x10018102 over the first 0x14,
	// then zeroes 0x14, 0x18, 0x24 and 0x28, points 0x1c at the shared empty
	// string and puts 1 at 0x2c. Model stores its own `this` into that
	// object's 0x24 and 0x28 afterwards (names ours).
	class CModelSub1D4
	{
	public:
		CModelSub1D4();                      // 0x100164bb
		// 0x100164e3 — a body call at 0x10016501, then the 0x14-byte
		// sub-object at offset zero torn down inline (its vtable restored
		// and Term()'d through 0x100197e6). Declared here for Model's own
		// destructor, which calls it at +0x1d4.
		~CModelSub1D4();                     // 0x100164e3
		// 0x10016501 — the reset the destructor's body is: the owner
		// pointer at +0x28 cleared, the name put back on the shared empty
		// string, and the collection at offset zero handed back to the
		// owning model's data block. Named for its address; nothing says
		// what it is beyond that (the destructor is its only caller here).
		void   sub_10016501();               // 0x10016501

		// 0x00 — a 0x14-byte collection, not opaque padding: the reset at
		// 0x10016501 and the destructor at 0x100164e3 both Term() it
		// through `this` itself (`mov %esi,%ecx`, i.e. offset zero), and
		// the SetSize2 they reach (0x100197e6) is the DefaultCache body —
		// it reads +0x0c and +0x10 as the cache words — over four-byte
		// elements (its release is 0x1000d020, the `PAXVDefaultCache`
		// _DeleteAndDestroyArray). Its constructor is 0x10018102.
		// The elements are 0x1c bytes, not four: the reader at 0x1001bf0d
		// walks this array with a stride of 0x1c, filling each record with
		// one w_ReadVector and one w_ReadRotation, and the SetSize2 it
		// shares with the destructor (0x100197e6) reaches BaseNew at
		// 0x1001b71a, whose `imul $0x1c` is the size in the clear. One
		// record per node of the model, and the count comes from the
		// model's own node-transform array.
		CMoArray<CChildNodeXform, DefaultCache> m_Array00; // 0x00 .. 0x14
		uint32 m_Unknown14;                  // 0x14
		uint32 m_Unknown18;                  // 0x18
		char*  m_pName;                      // 0x1c  the shared ""
		// 0x20 — one dword off the wire, read by 0x1001bf0d before the
		// transforms (name ours).
		uint32 m_Unknown20;                  // 0x20
		Model* m_pOwner24;                   // 0x24  Model's `this`
		// 0x28 — the model this record stands for, which is NOT always the
		// model that holds it: 0x10017145 reads it off every child record,
		// decrements that model's cache count, and deletes the model when
		// the count reaches zero. For the record embedded at Model+0x1d4 it
		// is the holder itself, which is why that case is tested for and
		// skipped (name ours).
		Model* m_pOwner28;                   // 0x28
		uint32 m_Unknown2c;                  // 0x2c  set to 1

		// 0x1001bf0d — this record's half of the model file: one dword and
		// then one position-and-rotation per node of the owning model. A
		// record with no owner cannot be sized and answers LTFALSE
		// (name ours).
		LTBOOL sub_1001bf0d(class ILTStream* pStream);
	};

	// 0x190 .. 0x1d0 — sixteen child-model slots and, at 0x1d0, how many of
	// them are filled. Not reconstructed here, but read straight off
	// LoadChildModels (0x1001c604) and confirmed from four directions:
	//
	//   * it refuses a "ChildModels" count of zero or more than 0x10
	//     (`cmpl $0x10,-0x10(%ebp); ja` onto the failure path), and 0x190
	//     plus sixteen dwords lands exactly on 0x1d0;
	//   * it stores each new child with `mov %edi,0x190(%ebx,%eax,4)` and
	//     then `incl 0x1d0(%ebx)`;
	//   * a later loop walks `lea 0x190(%ebx)` forward four bytes at a time
	//     against `cmp 0x1d0(%ebx),%eax`;
	//   * and the constructor's `m_pChildModels[0] = &m_Sub1d4` /
	//     `m_nChildModels = 1` below is the model's own entry — the same
	//     one LoadChildModels reaches as `lea 0x1d4(%ebx),%edi` beside the
	//     literal "SELF". So CModelSub1D4 is a child-model record and the
	//     one embedded at 0x1d4 is the model itself (names ours).
	class CModelSub1D4* m_pChildModels[16];  // 0x190 .. 0x1d0
	uint32     m_nChildModels;               // 0x1d0
	CModelSub1D4 m_Sub1d4;                   // 0x1d4 .. 0x204
	uint32     m_nFileVersion;               // 0x204  9, 10, 11 or 12
	std::atomic_int m_CacheCount;                 // 0x208  FreeUnusedModels' scratch counter
	// The class runs to 0x2c4, which is what both places that build one
	// allocate; the rest is not reached from the reconstructed set.
	// 0x20c — the thirteenth, 0xb4 bytes, ending at 0x2c0 where Model stores
	// a pointer back to it. Its constructor (0x1001694e) builds a collection
	// member at +0x18 first, then its own vptr, then a body call, then zeroes
	// 0xac; its destructor (0x100169b8) stores the same vptr and tears that
	// member down, which confirms the member from the other end (names ours).
	using CModelSub20C = ModelNode;

	CModelSub20C m_Sub20c;                   // 0x20c .. 0x2c0
    std::array<std::shared_ptr<Model>,16> m_ChildOwners;
	CModelSub20C* m_pSub20c;           // 0x2c0
};

class ModelAnim;

class ModelAnimationNode
{
public:
	ModelAnimationNode();
    ModelAnimationNode(const ModelAnimationNode&) = delete;
    ModelAnimationNode& operator=(const ModelAnimationNode&) = delete;                          // 0x10016534
	// 0x10016578 — the same record with its owner and its 0x18 word given
	// by argument, which is what slot 0x0c news up.
	ModelAnimationNode(ModelAnim* pOwner, ModelAnimationNode* parent);

	virtual ~ModelAnimationNode();
	// Slots 1 and 2 are the getter and the setter for the owner at +0x2c —
	// four bytes and ten, and ModelAnim's own constructor calls the
	// setter on its embedded copy with itself, which is what says what the
	// field holds. Slot 3 is the factory: 0x30 bytes out of the engine
	// allocator, which pins this class's size, and then the two-argument
	// constructor on them (names ours).
	virtual ModelAnim* _v04();            // 0x10016558
	virtual void _v08(ModelAnim* pOwner); // 0x1001665e
	virtual ModelAnimationNode* _v0c(ModelAnim* pOwner, ModelAnimationNode* parent);  // 0x10016668

	// 0x100165e5 — three fields cleared and the 0x20-element collection
	// assigned from a fresh empty one, exactly the shape Model's own
	// constructor uses for two of its collections (name ours).
	void sub_100165e5();
	// 0x1001662c — hands both collections back to the allocator the model
	// below answers with (name ours).
	void sub_1001662c();
	// 0x1001671c — the model this hangs off, reached through the owner at
	// +0x2c and its own +0x20. Seven bytes (name ours).
	Model* sub_1001671c();
	// 0x1001b5c9 — the twin of 0x1001b605 for this collection: every
	// element back to the allocator, then the array emptied. Declared only
	// (name ours).
	// 0x100166de — reached from ModelAnim's slot 1 with the model's
	// +0x2c0 record: re-point this record, and every child under it, at
	// the matching node of a node tree (name ours).
	LTBOOL sub_100166de(Model::CModelSub20C* pSub20c);
	// 0x1001668a — flatten this record and everything under it into the
	// owner's per-node array, in depth-first order, advancing the caller's
	// counter. LTFALSE when the array is missing or the counter has reached
	// the model's node count, and a failure anywhere below aborts the whole
	// walk (name ours).
	LTBOOL sub_1001668a(uint32* pnCount);

	// 0x1001bb37 — this node's animation data, off the wire, and the whole
	// subtree under it. One CModelXform per keyframe of the owning
	// animation, then one child record per child of the node at +0x04 —
	// read recursively through this same function, which is what makes the
	// animation data a tree with the same shape as the node tree. A child
	// that fails to read takes the whole array with it: the child is handed
	// back to the allocator and every sibling already read is released
	// through 0x1001b5c9 (name ours).
	LTBOOL sub_1001bb37(class ILTStream* pStream);

	// 0x04 — the node this record animates. Its +0x1c/+0x20 are
	// CModelSub20C::m_Array18's array and count, which is how 0x1001bb37
	// knows how many children to read (name ours).
	Model::CModelSub20C* m_pSub20c04;        // 0x04
	CMoArray<CModelXform, NoCache> m_Array08;// 0x08 .. 0x18
	ModelAnimationNode* m_pParent;                 // parent animation node
	// The elements are ModelAnimationNode*, which the release names: the
	// BaseDelete the array's own teardown reaches (0x1001ba94) is this
	// class's own, and 0x1001bb37 fills the array with recursively-read
	// children of exactly this type.
	CMoArray<ModelAnimationNode*, NoCache> m_Array1c;  // 0x1c .. 0x2c
	ModelAnim* m_pOwner2c;                // 0x2c
};

class ModelAnim
{
public:
	// 0x1001674d — 0x5c bytes, which is exactly what slot 2 allocates and
	// exactly where the embedded ModelAnimationNode at +0x28 ends plus the
	// pointer that follows it. The two constants it plants are read off the
	// bytes: -1 into +0x18 (`orl $0xffffffff`, VC6's four-byte store of a
	// negative one) and 0xc8 into +0x1c.
	ModelAnim(Model* pModel = nullptr);             // 0x1001674d

	virtual ~ModelAnim();                 // 0x100167b4, vtable 0x1004d9e4
	virtual void _v04(Model* pModel);        // 0x1001682b
	virtual ModelAnim* _v08(Model* pModel);   // 0x10016856

	// 0x100167e1 — the release: the block at +0x04 given back through the
	// owning model's allocator, the collection at +0x08 emptied, the
	// sub-object at +0x28 released and then 0x10016874 run (name ours).
	void sub_100167e1();
	// 0x10016874 — put the +0x58 slot back on the embedded record, giving
	// back whatever else was in it first (name ours).
	void sub_10016874();
	// 0x10016897 — twelve bytes, `push 0x4(%esp); call 0x100168a3; ret $4`:
	// the out-of-line copy of a one-line forwarder onto the member below
	// it. The reader at 0x1001bc69 ends with its answer (name ours).
	LTBOOL sub_10016897(LTBOOL bFlag);
	// 0x100168a3 — (re)build the flat node array from the record tree
	// (name ours).
	LTBOOL sub_100168a3(LTBOOL bForce);
	// 0x1001bc69 — the animation, out of the model file: its name, two
	// version-gated dwords, the keyframe table, and then the whole node
	// tree's animation data through the record at +0x58. THIS CLASS IS
	// `ModelAnim` (see the note on ModelAnim in commonmodel.h): every
	// offset this function touches lands on a field ModelAnim already had
	// (name ours).
	LTBOOL sub_1001bc69(class ILTStream* pStream);

	// 0x04 — a FLAT array of the node records under this animation, one slot
	// per model node: 0x1001668a walks the record tree depth-first and
	// stores each record into it, bounded by the model's node-transform
	// count. The release frees it, which is all the old `void*` reading had
	// (name ours).
	ModelAnimationNode** m_ppNodeRecords;          // 0x04
	// The keyframe table: ModelAnim::m_pKeyFrames and m_nKeyFrames are this
	// collection's m_pArray and m_nElements. 0x1001bc69 sizes it off the
	// wire and then fills each record's m_Time and m_pString, and the
	// allocator it reaches (0x1001b730) bakes in both the 0x14 element size
	// and the constructor at 0x1001651d.
	CMoArray<AnimKeyFrame, NoCache> m_Array08;  // 0x08 .. 0x18
	int32  m_n18;                            // 0x18  the constructor's -1;
	                                         //       read off the wire for
	                                         //       file version >= 11
	uint32 m_Unknown1c;                      // 0x1c  the constructor's 0xc8;
	                                         //       read off the wire for
	                                         //       file version >= 12, and
	                                         //       ModelAnim's
	                                         //       m_nInterpolationTime
	Model* m_pModel;                         // 0x20
	char*  m_pName;                          // 0x24  the shared empty string
	ModelAnimationNode  m_Sub28;                   // 0x28 .. 0x58
	ModelAnimationNode* m_p58;                     // 0x58  normally &m_Sub28
    ModelAnim(const ModelAnim&) = delete;
    ModelAnim& operator=(const ModelAnim&) = delete;
    uint32 GetLength();
    char* GetName() { return m_pName; }
    AnimKeyFrame*& m_pKeyFrames = m_Array08.m_pArray;
    uint32& m_nKeyFrames = m_Array08.m_nElements;
    uint32& m_nInterpolationTime = m_Unknown1c;

};


extern std::atomic_size_t g_nModelDataBytes;

class ModelInstance : public CObjBase
{
public:
	ModelInstance();

	~ModelInstance() override;

	float _v2c() override;

	void   SetupModelWalker(class ModelWalker* pOut) override;
	void   SetupModelWalkerRefsOnly(class ModelWalker* pOut) override;
	LTBOOL FindTracker(LTAnimTracker* pTracker,
	                               LTAnimTracker*** pppLink) override;
	float  GetScaledRadius() override;

    void BindModel(std::shared_ptr<Model> model, bool pending = false, bool preserve = true);
    std::shared_ptr<Model> m_ModelOwner;

	uint32 GetTrackerCount();

	// Unlinks the trackers game code added; their storage is the game's and

	// goes with the game object. The instance keeps its own main tracker.

	void DetachExternalTrackers();
	char*  GetFilename();

public:
	void*             m_pUnknown1a8;
	void*             m_pUnknown1ac;
	void*             m_pUnknown1b0;
	ModelInstanceHookFn m_HookFn;
	void*             m_pUnknown1b8;
	void*             m_pUnknown1bc;
	LTAnimTracker m_AnimTracker;
	LTAnimTracker* m_pTrackerList;
	NodeControlFn     m_Unknown1fc;
	void*             m_Unknown200;
	uint32            m_HiddenPieces;
	void*             m_pUnknown208;
	void*             m_pUnknown20c;
	float             m_f244;
};


using CObjModel = ModelInstance;

// The scratch object the node evaluator walks a model's skeleton in: 0x248
// bytes on the caller's stack, set up from the object through LTObject's
// vtable and then handed the object's own transform. Names ours; only the
// three fields the two transform accessors reach are known.
// Native evaluator scratch. Callbacks and data retain their native types.
class ModelWalker {
public:
    ModelWalker() = default;
    LTBOOL sub_100386c6();
    LTBOOL Evaluate(HMODELNODE node);
    LTBOOL IsValid();
private:
    LTBOOL EvaluateNodes(const std::vector<uint32>& nodes);
    bool NodePose(uint32 reference, uint32 node, bool relative, LTRotation& rotation, LTVector& position);
public:
    AnimTimeRef m_TimeRefs[8];
    uint32 m_Unknown_e0 = 0;
    LTMatrix* m_pObjTransform = nullptr;
    LTMatrix* m_Unknown_e8 = nullptr;
    NodeControlFn m_Unknown_ec = nullptr;
    void* m_Unknown_f0 = nullptr;
    HOBJECT m_Unknown_f4 = nullptr;
};

// 0x10011b1e is the SDK's own MatMul from ltmatrix.h, emitted out of line —
// the same thing that happens to Identity, Inverse and Normalize: the header
// marks them inline, VC6 declines, and the COMDAT it leaves behind is the
// shipped function. So there is no mat_Multiply to declare here; call
// MatMul. (sharedcommon.h and servermath.h still declare a mat_Multiply,
// and their call sites byte-match either way because the call's operand is
// a masked relocation.)

// 0x10015bcd — the other direction: a quaternion laid out as the rotation
// half of a 4x4 (declared in sharedcommon.h too; both files reach it, and
// servermessage.cpp includes both headers, so the two declarations have to
// stay spelled the same). The era prototype in ltquatbase.h is
// `(const float* pQuat, float mat[4][4])`; the two pointers a call pushes
// are the same either way, so the call sites here byte-match as written.
void quat_ConvertToMatrix(LTRotation* pRot, LTMatrix* pMat);

// 0x1000d040 and 0x10026394 — a vector and a rotation off a stream, one
// component at a time. Declared here because the model reader uses them and
// they are defined further down commonmodel.cpp (names ours).
void w_ReadVector(class ILTStream* pStream, LTVector* pVec);
void w_ReadRotation(class ILTStream* pStream, LTRotation* pRot);

// 0x1001bac6 — seek the stream to a named chunk, answering LTFALSE when the
// file has no such chunk. Declared here for the same reason as the two above:
// the model readers use it and it is defined further down commonmodel.cpp.
LTBOOL mod_FindChunk(class ILTStream* pStream, char* pName);

// 0x10015ce2 — the rotation half of a 4x4, back out as a quaternion. Not a
// reconstruction at all: this is ltquatbase.cpp's own quat_ConvertFromMatrix,
// which the SDK ships as source, and the prototype is the SDK's.
void quat_ConvertFromMatrix(float* pQuat, const float mat[4][4]);

// Tracker helpers from the model module (0x100010a7, 0x100011c2, 0x10001153).
LTBOOL at_IsDone(LTAnimTracker* pTracker);
void   at_ResetAnim(LTAnimTracker* pTracker);
void   at_SetCurAnim(LTAnimTracker* pTracker, HMODELANIM hAnim, LTBOOL bTransition);
void   at_Init(LTAnimTracker* pTracker, Model* pModel, uint32 iAnim);   // 0x10001013

// 0x100136c2 — resolve an animation by name on the object's model (names
// ours; GetAnimIndex forwards to it, commonmodel.cpp defines it).
HMODELANIM ci_GetAnimIndex(LTObject* pObj, char* pAnimName);
LTRESULT ci_GetNextModelNode(HOBJECT,HMODELNODE,HMODELNODE*);
LTRESULT ci_GetModelNodeName(HOBJECT,HMODELNODE,char*,uint32);

// 0x10052edc — a char* holding the address of a zero byte at 0x10056ff4: the
// name a model with no file of its own carries, so that every reader can
// strlen it and FreeFilename can tell an allocated name from the shared one
// (name ours).
extern char* g_pEmptyFilename;

// 0x10058724 — the running total of the bytes every model's data block has
// taken. Load adds each block's rounded size to it and nothing here takes
// any back (name ours).

// Give every element of a pointer collection back to an allocator and then
// empty the collection. Sixty bytes, one instantiation per element type, and
// the image carries five of them — 0x1001a1a1, 0x1001a1dd, 0x1001a215,
// 0x1001b5c9 and 0x1001b605 — identical but for which BaseDelete they reach.
// (They all reach the SAME SetSize2 at 0x1001a03d, which is the observation
// that says the shipped link folds identical COMDATs: a pointer collection's
// SetSize2 does not depend on what the pointer points at.)
//
// LIKE THE BaseNew OVERLOADS ABOVE, THIS BELONGS IN collection.h with the
// rest of the collection's helpers; it is here only because that file is not
// ours to edit. It must NOT be marked `inline`: /O1 implies /Ob1, so an
// inline-marked template would be expanded into its callers and the five
// shipped functions would not exist (name ours).
template<class T>
void FreeArrayElements(CMoArray<T, NoCache>* pArray, LAlloc* pAlloc)
{
	uint32 i;
	T p;

	for (i = 0; i < pArray->m_nElements; i++)
	{
		p = pArray->m_pArray[i];

		if (p)
			BaseDelete(pAlloc, p, 1);
	}

	pArray->SetSize2(0, pAlloc);
}


// 0x1001690b — the animation's length in milliseconds.


// 0x1000108d — advance a tracker by a whole number of milliseconds; the
// interface takes seconds and this is where they stop being seconds
// (name ours).
void at_UpdateTracker(LTAnimTracker* pTracker, uint32 nMS);


// The vtable at 0x1004e5f4 is iltmodel.h's twenty-seven virtuals in the
// header's own declaration order — anchored by the twenty already matched,
// which fixes every slot without a guess. The order matters: UpdateMainTracker
// reaches GetMainTracker at 0x28 through `this`, which is index ten and
// nothing else.
class CommonModelLT : public ILTModel
{
public:
	virtual LTRESULT GetSocket(HOBJECT hObj, char* pSocketName, HMODELSOCKET& hSocket);
	virtual LTRESULT GetNode(HOBJECT hObj, char* pNodeName, HMODELNODE& hNode);
	virtual LTRESULT GetPiece(HOBJECT hObj, char* pPieceName, HMODELPIECE& hPiece);
	virtual LTRESULT FindWeightSet(HOBJECT hObj, char* pSetName, HMODELWEIGHTSET& hSet);

	virtual LTRESULT GetPieceHideStatus(HOBJECT hObj, HMODELPIECE hPiece, LTBOOL& bHidden);
	virtual LTRESULT SetPieceHideStatus(HOBJECT hObj, HMODELPIECE hPiece, LTBOOL bHidden);

	virtual LTRESULT GetNodeTransform(HOBJECT hObj, HMODELNODE hNode,
	                                  LTransform& transform, LTBOOL bWorldSpace);  // 0x1001d478
	virtual LTRESULT GetSocketTransform(HOBJECT hObj, HMODELSOCKET hSocket,
	                                    LTransform& transform, LTBOOL bWorldSpace);// 0x1001d77f

	virtual LTRESULT GetInstanceHookFn(HOBJECT hObj, ModelInstanceHookFn& fn);
	virtual LTRESULT SetInstanceHookFn(HOBJECT hObj, ModelInstanceHookFn fn);

	virtual LTRESULT GetMainTracker(HOBJECT hObj, LTAnimTracker*& pTracker);
	virtual LTRESULT UpdateMainTracker(HOBJECT hObj, float fUpdateDelta);          // 0x1001e056
	virtual LTRESULT GetPlaybackState(LTAnimTracker* pTracker, uint32& flags);

	virtual LTRESULT AddTracker(HOBJECT hObj, LTAnimTracker* pTracker);
	virtual LTRESULT RemoveTracker(HOBJECT hObj, LTAnimTracker* pTracker);

	virtual LTRESULT GetCurAnim(LTAnimTracker* pTracker, HMODELANIM& hAnim);
	virtual LTRESULT SetCurAnim(LTAnimTracker* pTracker, HMODELANIM hAnim);
	virtual LTRESULT ResetAnim(LTAnimTracker* pTracker);

	virtual LTRESULT GetLooping(LTAnimTracker* pTracker);
	virtual LTRESULT SetLooping(LTAnimTracker* pTracker, LTBOOL bLooping);
	virtual LTRESULT GetPlaying(LTAnimTracker* pTracker);
	virtual LTRESULT SetPlaying(LTAnimTracker* pTracker, LTBOOL bPlaying);

	virtual LTRESULT GetWeightSet(LTAnimTracker* pTracker, HMODELWEIGHTSET& hSet);
	virtual LTRESULT SetWeightSet(LTAnimTracker* pTracker, HMODELWEIGHTSET hSet);

	virtual LTRESULT GetCurAnimLength(LTAnimTracker* pTracker, uint32& length);
	virtual LTRESULT GetCurAnimTime(LTAnimTracker* pTracker, uint32& curTime);
	virtual LTRESULT SetCurAnimTime(LTAnimTracker* pTracker, uint32 curTime);
};

#endif
