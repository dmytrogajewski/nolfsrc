// *********************************************************************** //
//
//  MODULE:   w12_438000 (.CPP)  -- lithtech.exe only, scratch reconstruction
//  BUILD:    MSVC 6 (cl 8447), /O2 = /Og /Oi /Ot /Oy /Ob1 (frameless, esp-rel)
//  ORACLE:   the retail lithtech.exe  (never one of our builds)
//
//  The nine enumerated rows are NOT one module.  They are five:
//
//    A.  0x0043a960 / 0x0043a9c0 / 0x0043a390       the warp blitter's
//        polygon clipper and edge setup -- src/engine/lithtech/diblit.cpp
//        already owns this cluster and already reconstructs 0x0043b270.
//    B.  0x0043a070                                 the blit rectangle
//        intersect, called only by ci_FillRect (0x0040ea30).  Same surface
//        module as A; src/engine/lithtech/cissurf.cpp / diblit.cpp.
//    C.  0x00434070                                 euler-angles -> basis
//        vectors, an ltmatrix.h helper in the ClientCommonLT run.
//    D.  0x004335d0 (NOT 0x004335c0 -- see the extent note)   a file-scope
//        object's constructor in ..\server\saveio.cpp.  SHARED TU: the
//        server twin is 0x10011137.
//    E.  0x004368c0 / 0x00436ae0                    the DirectInput layer,
//        src/engine/lithtech/clientinput.cpp (0x00436860..0x00439fbc).
//
//    0x00431ff0 is NOT WORK.  It is CSaveIO::WriteChecked and it is already
//    written in src/engine/server/saveio.cpp; see the note at the bottom.
//
//  The decoded object map attributes the 0x0043a* rows to netmgr.obj.
//  That is wrong for every one of them: 0x0043a070's only caller is
//  ci_FillRect, 0x0043a390's only caller is the function whose de_error
//  literal at 0x0040e308 is "InternalWarpSurfaceToSurface", and
//  0x0043a960/0x0043a9c0's only callers are inside 0x0043a4f0, the warp
//  polygon clipper.  There is no server.dll twin for any of them.
//
//  Every constant, offset and branch below is cited by the address it was
//  read at.  Locally introduced names carry a Q438 suffix.
//
// *********************************************************************** //

#include <stdlib.h>


// ----------------------------------------------------------------------- //
//  Minimal stand-ins for the SDK types.  The real declarations are
//  src/LT2/sdk/inc/ltbasedefs.h, ltrect.h, ltvector.h, ltmatrix.h and
//  ltbasetypes.h; they are restated here so this file compiles alone.  The
//  LAYOUTS are the SDK's, unchanged.
// ----------------------------------------------------------------------- //

typedef int            LTBOOL;
typedef unsigned int   uint32;
typedef signed int     int32;
typedef short          int16;
typedef unsigned char  uint8;

#define LTTRUE  1
#define LTFALSE 0
#define LTNULL  0

#define LTMIN(a,b) ((a) < (b) ? (a) : (b))          // ltbasedefs.h:1245
#define LTMAX(a,b) ((a) > (b) ? (a) : (b))          // ltbasedefs.h:1246

// ltrect.h -- four ints, in this order, with the SDK's defaulted
// constructor and Init (ltrect.h:8-15).  ci_FillRect (0x0040ea30) needs the
// constructor: its two LTRect locals are zeroed at entry (0x0040ea3b..
// 0x0040ea57) before anything else runs.
struct LTRectQ438
{
	LTRectQ438(int l=0, int t=0, int r=0, int b=0)
		: left(l), top(t), right(r), bottom(b) {}
	inline void Init(int inLeft, int inTop, int inRight, int inBottom)
	{
		left = inLeft;
		top = inTop;
		right = inRight;
		bottom = inBottom;
	}

	int left, top, right, bottom;
};

// ltvector.h -- three floats.  POD here on purpose: the by-value copy in
// 0x00434070 is three dword moves, which is the compiler-generated bitwise
// copy, not a user copy constructor.
struct LTVectorQ438
{
	float x, y, z;
};

// ltbasetypes.h's LTWarpPt, already pinned by diblit.cpp PART THREE:
// sixteen bytes, and the field at +0x08 is dest_x, at +0x0c dest_y.
struct LTWarpPtQ438
{
	float source_x;                          // 0x00
	float source_y;                          // 0x04
	float dest_x;                            // 0x08
	float dest_y;                            // 0x0c
};


// ======================================================================= //
//  PART A -- 0x0043a960 and 0x0043a9c0, the warp clipper's vertex
//            interpolator, ONE SOURCE STATEMENT BLOCK, TWO CLIP PLANES
//
//  THEY ARE A PAIR AND THEY ARE *NOT* IDENTICAL.  Both are 81 bytes and
//  0x60 apart, both are `ret $0xc` with %ecx dead, and instruction for
//  instruction they are the same function -- but 0x0043a960 clips on
//  dest_x against the global at 0x004b3e1c and 0x0043a9c0 clips on dest_y
//  against the global at 0x004b3690, so the two byte strings differ (the
//  x87 opcodes for +0x08 vs +0x0c, and two different relocated addresses).
//  They match separately.
//
//  WHERE THEY COME FROM.  0x0043a4f0 is the warp polygon clipper.  It
//  publishes its four plane arguments into
//
//      0x004b3e18 <- arg3 (left,   dest_x >= K)     0x0043a502
//      0x004b3690 <- arg4 (top,    dest_y >= K)     0x0043a512
//      0x004b3e1c <- arg5 (right,  dest_x <  K)     0x0043a521
//      0x004b3e24 <- arg6 (bottom, dest_y <  K)     0x0043a527
//
//  and then runs the SAME Sutherland-Hodgman pass four times.  Pass 4 is
//  the out-of-line call to 0x0043b270 that diblit.cpp PART THREE already
//  reconstructs.  Passes 1..3 are inlined -- but only pass 1 also inlines
//  the vertex INTERPOLATION (0x0043a611..0x0043a655).  Passes 2 and 3 CALL
//  it, at 0x0043a7a0 -> 0x0043a9c0 and 0x0043a8ed -> 0x0043a960.
//
//  ** CORRECTION TO src/engine/lithtech/diblit.cpp. **  Its "NOT WRITTEN"
//  block says of 0x0043a4f0 that "three of the four are inlined ... and the
//  fourth is the call to 0x0043b270".  That is true of the LOOP but not of
//  the interpolation: two of the three inlined passes still call out, and
//  those two calls are exactly these two enumerated rows.  Anyone writing
//  0x0043a4f0 has to reproduce a call here, not an expansion.
//
//  THE CALLING CONVENTION.  `ret $0xc` with three stack arguments, and the
//  call sites load %ecx (`lea 0x2c(%esp),%ecx` at 0x0043a79c and
//  0x0043a8e9) with an address the callee never reads -- 0x20(%esp) in the
//  master's frame, four bytes below its 50-dword bInside array.  That is a
//  `this` for a member the compiler kept out of line.  A `__stdcall` free
//  function produces BYTE-FOR-BYTE THE SAME callee, so the spelling below
//  is not load-bearing for matching; it is written as a member because the
//  %ecx load at the call site has no other explanation.
//
//  THE STATEMENT ORDER is read off the stores, and it is the same order
//  diblit.cpp PART THREE already reconstructs for 0x0043b270: the clipped
//  axis first (it is the plane constant, reloaded from the global rather
//  than reused, 0x0043a97d / 0x0043a9dd), then the OTHER dest axis, then
//  source_x, then source_y.  The interpolant fT never reaches memory -- it
//  lives in st(1) from the `fdivrp` at 0x0043a97b to the `fstp %st(0)` at
//  0x0043a9ac -- which is what a single local float does.
// ======================================================================= //

// 0x004b3e1c -- the RIGHT plane; 0x0043a521 stores 0x0043a4f0's fifth
// argument into it.
extern float g_fWarpClipRightQ438;                         // 0x004b3e1c

// 0x004b3690 -- the TOP plane; 0x0043a512 stores the fourth argument.
extern float g_fWarpClipTopQ438;                           // 0x004b3690

class CWarpClipRightQ438
{
public:
	// Out-of-class on purpose: /Ob1 never expands a non-template member
	// declared out of class, which is what the shipped image shows at both
	// call sites.
	void InterpolateQ438(LTWarpPtQ438* pPrev, LTWarpPtQ438* pCur,
	                     LTWarpPtQ438* pOut);
};

class CWarpClipTopQ438
{
public:
	void InterpolateQ438(LTWarpPtQ438* pPrev, LTWarpPtQ438* pCur,
	                     LTWarpPtQ438* pOut);
};


// 0x0043a960, 81 bytes.  0x0043a960..0x0043a9b0 inclusive; the only branch
// target in the range is the function itself and its one caller is
// 0x0043a8ed.  No interior labels at all -- straight-line x87.
void CWarpClipRightQ438::InterpolateQ438(LTWarpPtQ438* pPrev,
                                         LTWarpPtQ438* pCur,
                                         LTWarpPtQ438* pOut)
{
	float fT;

	// 0x0043a968 flds 0x004b3e1c / 0x0043a96e fsubs 0x08(%eax)
	// 0x0043a971 flds 0x08(%ecx) / 0x0043a974 fsubs 0x08(%eax)
	// 0x0043a97b fdivrp -- numerator first, divisor second.
	fT = (g_fWarpClipRightQ438 - pPrev->dest_x) /
	     (pCur->dest_x - pPrev->dest_x);

	// 0x0043a97d flds 0x004b3e1c ; 0x0043a983 fstps 0x08(%edx)
	pOut->dest_x   = g_fWarpClipRightQ438;

	// 0x0043a986..0x0043a991
	pOut->dest_y   = (pCur->dest_y - pPrev->dest_y) * fT + pPrev->dest_y;

	// 0x0043a994..0x0043a99c
	pOut->source_x = (pCur->source_x - pPrev->source_x) * fT +
	                 pPrev->source_x;

	// 0x0043a99e..0x0043a9a9
	pOut->source_y = (pCur->source_y - pPrev->source_y) * fT +
	                 pPrev->source_y;
}


// 0x0043a9c0, 81 bytes.  0x0043a9c0..0x0043aa10 inclusive; one caller,
// 0x0043a7a0.  Identical statement shape, clipping dest_y instead.
void CWarpClipTopQ438::InterpolateQ438(LTWarpPtQ438* pPrev,
                                       LTWarpPtQ438* pCur,
                                       LTWarpPtQ438* pOut)
{
	float fT;

	// 0x0043a9c8 / 0x0043a9ce / 0x0043a9d1 / 0x0043a9d4 / 0x0043a9db
	fT = (g_fWarpClipTopQ438 - pPrev->dest_y) /
	     (pCur->dest_y - pPrev->dest_y);

	// 0x0043a9dd flds 0x004b3690 ; 0x0043a9e3 fstps 0x0c(%edx)
	pOut->dest_y   = g_fWarpClipTopQ438;

	// 0x0043a9e6..0x0043a9f1
	pOut->dest_x   = (pCur->dest_x - pPrev->dest_x) * fT + pPrev->dest_x;

	// 0x0043a9f4..0x0043a9fc
	pOut->source_x = (pCur->source_x - pPrev->source_x) * fT +
	                 pPrev->source_x;

	// 0x0043a9fe..0x0043aa09
	pOut->source_y = (pCur->source_y - pPrev->source_y) * fT +
	                 pPrev->source_y;
}


// ======================================================================= //
//  PART B -- 0x0043a070, the blit rectangle intersect
//
//  WHAT IDENTIFIES IT.  Exactly one caller in the image, 0x0040eb19, inside
//  0x0040ea30 -- which clientinterface.h:363 already names ci_FillRect and
//  which pushes the de_error literal "FillRect" (0x004a8334) at 0x0040ea8c,
//  0x0040ead4 and 0x0040ec57.  The caller builds the second argument on its
//  own stack as {0, 0, pSurface->m_Width (+0x10), pSurface->m_Height
//  (+0x14)} at 0x0040eaff..0x0040eb15 and passes the caller's LTRect* as the
//  third, so the two inputs are "the whole surface" and "the user's rect".
//
//  SIGNATURE.  __cdecl, three dwords (`add $0xc,%esp` at 0x0040eb1e).  The
//  OUT parameter is the FIRST argument even though it is read last
//  (0x0043a088, after the first LTMAX is already in %ebp) -- arg1 is
//  0x14(%esp) behind four pushes, arg2 is 0x08(%esp) at entry, arg3 is
//  0x0c(%esp) at entry.
//
//  THE COMPARISONS.  `jg` keeps the first operand on the two top-left
//  fields and `jl` keeps it on the two bottom-right ones, which is
//  ltbasedefs.h:1245/1246's LTMIN/LTMAX with the SURFACE rect first:
//      0x0043a080  cmp %esi,%eax / 0x0043a084 jg   -> LTMAX(a->left , b->left )
//      0x0043a096  cmp %edi,%esi / 0x0043a098 jg   -> LTMAX(a->top  , b->top  )
//      0x0043a0a5  cmp %edi,%esi / 0x0043a0a7 jl   -> LTMIN(a->right, b->right)
//      0x0043a0b4  cmp %edx,%ecx / 0x0043a0b6 jl   -> LTMIN(a->bottom,b->bottom)
//  Signed throughout, so the fields are `int`, which is what ltrect.h says.
//
//  THE RETURN IS ONE `&&`, AND THE EPILOGUE IS DUPLICATED.  0x0043a0bf
//  `jl 0x0043a0c8` leaves the left<right test to a block that does
//  `xor %eax,%eax ; ... ; setl %al` on top<bottom, while the fall-through
//  at 0x0043a0c1 pops and returns a zero it materialises itself.  Two
//  epilogues, which is /O2 preserving the short circuit -- not two source
//  returns.
//
//  WHERE IT BELONGS: with ci_FillRect, i.e. the client surface module
//  (src/engine/lithtech/cissurf.cpp, or diblit.cpp which already owns
//  0x0043a0e0, the scaled-blit clipper 0x70 bytes below this one).
// ======================================================================= //

// 0x0043a070, 100 bytes.  0x0043a070..0x0043a0d3 inclusive.  EXTENT CLEAN:
// the e8/e9/0f8x sweep of the whole .text finds exactly one displacement
// into the range, the call at 0x0040eb19 to the entry; every other branch
// is an internal short jump and the last instruction (`ret` at 0x0043a0d3)
// ends on the boundary.
LTBOOL rect_IntersectQ438(LTRectQ438* pOut, LTRectQ438* pRect1,
                          LTRectQ438* pRect2)
{
	pOut->left   = LTMAX(pRect1->left,   pRect2->left);      // 0x0043a08c
	pOut->top    = LTMAX(pRect1->top,    pRect2->top);       // 0x0043a09c
	pOut->right  = LTMIN(pRect1->right,  pRect2->right);     // 0x0043a0ab
	pOut->bottom = LTMIN(pRect1->bottom, pRect2->bottom);    // 0x0043a0bc

	// 0x0043a0ba / 0x0043a0cb -- both read back from the registers the
	// stores above left behind, which is store-to-load forwarding on one
	// pointer, not a source difference.  COIN-FLIP: four locals assigned
	// first and stored afterwards give the same four comparisons; the
	// interleaving of `mov 0x14(%esp),%eax` at 0x0043a088 with the FIRST
	// LTMAX is what argues for the field form written here.
	// NOT one `&&`. 0x0043a0bf `jl` jumps FORWARD over an inline
	// `return LTFALSE`, so the first test is an EARLY RETURN with its own
	// epilogue and the second is a trailing relational return -- which is
	// why 0x0043a0d0 is `setl %al` and not a branch. The `&&` form costs
	// two bytes: two conditional jumps plus `mov $1,%eax` instead of one
	// jump plus `setl`.
	if (pOut->left >= pOut->right)
		return LTFALSE;

	return pOut->top < pOut->bottom;
}


// ======================================================================= //
//  PART C -- 0x00434070, euler angles to the three basis vectors
//
//  WHAT IT IS.  It reserves a 0x40-byte local (`sub $0x40,%esp`, an
//  LTMatrix), hands that local and a twelve-byte by-value copy of its own
//  first argument to 0x00433e40, and then reads NINE floats out of the
//  local into three LTVector out-parameters:
//
//      arg4 <- local +0x00, +0x10, +0x20     (m[0][0], m[1][0], m[2][0])
//      arg5 <- local +0x04, +0x14, +0x24     (m[0][1], m[1][1], m[2][1])
//      arg6 <- local +0x08, +0x18, +0x28     (m[0][2], m[1][2], m[2][2])
//
//  read at 0x00434096..0x004340dd.  That is src/LT2/sdk/inc/ltmatrix.h:663
//  LTMatrix::GetBasisVectors(pRight, pUp, pForward) verbatim, column by
//  column, which also fixes the out-parameter ORDER.
//
//  0x00433e40 (210 bytes, NOT among these rows) is the euler builder: it
//  takes three floats and an LTMatrix*, `fcos`/`fsin`s each of them
//  (0x00433e47, 0x00433e6c, 0x00433e72, 0x00433e7c, 0x00433e82,
//  0x00433e8c), zeroes m[0][3], m[1][3], m[2][3] and the whole bottom row
//  and writes 1.0f (0x3f800000) to m[3][3] at 0x00433e61.  Its other caller,
//  0x00433f20, assembles the same twelve-byte block from three of ITS
//  arguments (0x00433f4d..0x00433f56) -- two independent sites building a
//  12-byte block with `sub $0xc,%esp` plus three `mov`s is a struct passed
//  BY VALUE, not three separate float pushes, which is why the parameter
//  below is an LTVector and not three floats.
//
//  MODULE.  This is the ltmatrix.h-helper run that sits under
//  ClientCommonLT: src/engine/lithtech/cltapi.cpp BLOCK B already maps
//  0x004347c0..0x00434a30 to iltcommon.h's vtable 0x004a17c8, and
//  src/engine/lithtech/transform.cpp already names 0x00434580 MatMul and
//  0x00431650 MatVMul_3x3 out of the same window.  CLIENT-ONLY: the
//  distinctive thirteen bytes at 0x00433e5b occur once in lithtech.exe and
//  ZERO times in the retail server.dll, so there is no /O1 oracle for it.
//
//  THE ONE CALLER is 0x00424312, inside 0x00424242, which builds the
//  by-value vector out of three consecutive dwords and keeps only the THIRD
//  out-parameter afterwards (0x00424317..0x00424325).
// ======================================================================= //

// 0x00433e40 -- declared, not defined: it is a 210-byte function of its own
// and not among these rows.  The call at 0x00434091 is __cdecl (its
// arguments are folded into the single `add $0x50,%esp` at 0x004340de).
void mat_SetupEulerQ438(LTVectorQ438 vAngles, struct LTMatrixQ438* pMat);

struct LTMatrixQ438
{
	float m[4][4];

	// In-class, so /Ob1 may expand it -- which is what 0x00434096 shows.
	void GetBasisVectorsQ438(LTVectorQ438* pRight, LTVectorQ438* pUp,
	                         LTVectorQ438* pForward)
	{
		pRight->x   = m[0][0];               // 0x00434096 flds 0x10(%esp)
		pRight->y   = m[1][0];               // 0x0043409e
		pRight->z   = m[2][0];               // 0x004340a2

		pUp->x      = m[0][1];               // 0x004340a8
		pUp->y      = m[1][1];               // 0x004340af
		pUp->z      = m[2][1];               // 0x004340ba

		pForward->x = m[0][2];               // 0x004340c0
		pForward->y = m[1][2];               // 0x004340c7
		pForward->z = m[2][2];               // 0x004340d2
	}
};

// 0x00434070, 114 bytes.  0x00434070..0x004340e1 inclusive.  EXTENT CLEAN:
// one inbound displacement in the whole image (0x00424312), no internal
// branches at all, `ret` at 0x004340e1 on the boundary.
void mat_GetEulerBasisVectorsQ438(LTVectorQ438 vAngles,
                                  LTVectorQ438* pRight,
                                  LTVectorQ438* pUp,
                                  LTVectorQ438* pForward)
{
	LTMatrixQ438 mat;

	mat_SetupEulerQ438(vAngles, &mat);       // 0x00434091

	mat.GetBasisVectorsQ438(pRight, pUp, pForward);
}


// ======================================================================= //
//  PART D -- 0x004335d0, the save translation table's constructor: MERGED
//  into src/engine/server/saveio.cpp (CSaveXlatInit, g_SaveXlatInit), whose
//  client compile is the shipped 0x00433590..0x0043360a quartet and
//  constructor.  The copy that stood here defined the object a second time.
// ======================================================================= //


// ======================================================================= //
//  PART E -- 0x00436ae0 and 0x004368c0, the DirectInput layer's
//            binding-record teardown
//
//  MODULE.  src/engine/lithtech/clientinput.cpp already owns
//  0x00436860..0x00439fbc and neither row is written there; its slot map
//  lists 0x00436940 (Term) as "not written" and does not mention either of
//  these.  The tie is the global list head at 0x004b3678, which
//  0x004381a0 -- the entry that prints "Bound %s (on %s) to action %s" --
//  pushes onto at 0x0043829b.
//
//  THE RECORD IS TWELVE BYTES, read straight off its allocation:
//  0x00438273 `push $0xc ; call 0x004290b0` (operator new), then
//  `mov %ebx,(%eax) ; mov %ebx,0x4(%eax)` with %ebx == 0 -- two zeroed
//  dwords -- and then `mov 0x4b3678,%ecx ; mov %ecx,0x8(%eax) ;
//  mov %eax,0x4b3678`, a push onto a singly linked list whose link is at
//  +0x08.  There is NO vptr: +0x00 receives 0, not a table address, and
//  0x00436ae0 appears in no vtable anywhere in the image (a whole-image
//  dword scan finds zero data references to it).
//
//  THE OWNING NODE IS 0x18 BYTES: 0x004382a0 `push $0x18 ; call
//  0x004290b0`.  Only three of its six dwords are reached by the teardown
//  -- +0x08 (a plain block, freed), +0x10 (next) and +0x14 (the record).
//
//  0x00436ae0 IS THE SCALAR-DELETING-DESTRUCTOR SHAPE: thiscall, `ret $0x4`,
//  `testb $0x1,0x8(%esp)` at 0x00436b1c gating one `operator delete`, and
//  `mov %esi,%eax` returning `this`.  Its one call site, 0x004369dd inside
//  Term, is a DIRECT call (`push $0x1 ; call`), and the SAME body is
//  INLINED without any flag at 0x004368ed (below) and at 0x004380ab -- the
//  per-call-site /Ob1 split.  It is written below as an
//  explicit member because the class is not polymorphic and it is not
//  settled whether VC6 emits `??_G` for it; if a build does
//  emit `??_G` for CBindRecQ438, the stand-in below is the one to drop, not
//  the destructor.
// ======================================================================= //

// 0x004b3678 -- the head of the binding-record list.
extern struct CBindRecQ438* g_pBindRecHeadQ438;

struct CBindRecQ438
{
	// The destructor is IN CLASS so that /Ob1 can expand it, which is what
	// 0x004368ed and 0x004380ab show.
	~CBindRecQ438()
	{
		CBindRecQ438* pPrev;

		m_n00 = 0;                           // 0x00436ae3
		m_n04 = 0;                           // 0x00436ae9

		// 0x00436af0 / 0x00436af5 -- head first, `this` second.
		if (g_pBindRecHeadQ438 == this)
		{
			g_pBindRecHeadQ438 = m_pNext;    // 0x00436af9
		}
		else
		{
			pPrev = g_pBindRecHeadQ438;

			// 0x00436b03 guard, 0x00436b07 loop top, 0x00436b12 `je` out
			// and 0x00436b14 an unconditional `jmp` back.  The found arm
			// ENDS THE LOOP BY NULLING pPrev, not by `break`: with `break`
			// (or return, goto, for(;;), an else arm) VC6 rotates the test
			// to `jne top ; jmp exit`; the null store is folded away and
			// leaves shipped's `je exit ; jmp top` (0x00436b12, 0x00436914,
			// 0x004380d3 -- all three copies).
			while (pPrev)
			{
				if (pPrev->m_pNext == this)  // 0x00436b0a
				{
					pPrev->m_pNext = m_pNext;  // 0x00436b16/19
					pPrev = LTNULL;
				}
				else
				{
					pPrev = pPrev->m_pNext;  // 0x00436b0e
				}
			}
		}
	}

	void* DeleteQ438(uint32 nFlags);

	uint32        m_n00;                     // 0x00
	uint32        m_n04;                     // 0x04
	CBindRecQ438* m_pNext;                   // 0x08
};

// 0x00436ae0, 82 bytes.  0x00436ae0..0x00436b31 inclusive.  EXTENT CLEAN:
// one inbound displacement in the whole image (0x004369dd), every other
// branch internal, `ret $0x4` at 0x00436b2f..0x00436b31 on the boundary.
void* CBindRecQ438::DeleteQ438(uint32 nFlags)
{
	this->~CBindRecQ438();

	if (nFlags & 1)                          // 0x00436b1c
		::operator delete(this);             // 0x00436b24 -> 0x004290c0

	return this;                             // 0x00436b2c
}


// One node of the list 0x004368c0 walks.  0x18 bytes; the three fields
// below are the only ones the teardown reaches.
struct SBindNodeQ438
{
	uint32         m_n00;                    // 0x00
	uint32         m_n04;                    // 0x04
	uint8*         m_pData;                  // 0x08  freed at 0x004368de
	uint32         m_n0c;                    // 0x0c
	SBindNodeQ438* m_pNext;                  // 0x10  0x004368cf
	CBindRecQ438*  m_pBind;                  // 0x14  0x004368e6
};

// 0x004b34d8 -- the head the atexit handler frees (CBindNodeOwnerQ438::
// m_pList below).  A four-byte-window scan of the whole .text finds exactly
// ONE reference to it, 0x004368c3, so nothing in the image ever links
// anything onto it; the list is walked defensively and is normally empty.

// The body the three sites share.  0x004368d6..0x00436932 here,
// 0x004369c4..0x004369ed in Term and 0x00438094..0x004380f1 in 0x00438000
// are the same twelve statements; only the inner `delete` differs (a call
// to 0x00436ae0 in Term, expanded in the other two).  `inline` and the
// hoist above the caller are both required -- the
// inliner is forward-only.
inline void di_FreeBindNodeQ438(SBindNodeQ438* pNode)
{
	// 0x004368d2 -- the redundant second null test, four bytes after the
	// caller's loop condition tested the same register.  It is what says
	// this is a separate function inlined, and not one loop body.
	if (!pNode)
		return;

	// Explicit guard: `delete` alone emits no null test.
	if (pNode->m_pData)                      // 0x004368d9
		delete pNode->m_pData;               // 0x004368de

	if (pNode->m_pBind)                      // 0x004368e9
	{
		delete pNode->m_pBind;               // 0x004368ed..0x00436926
		pNode->m_pBind = LTNULL;             // 0x00436927
	}

	delete pNode;                            // 0x0043692b
}

// 0x004368c0, 125 bytes.  0x004368c0..0x0043693c inclusive.  EXTENT CLEAN
// in the branch sense -- no e8/e9/0f8x in the image lands inside it -- and
// its whole `cb` provenance is the atexit push at 0x004368b0:
//     0x004368b0  push $0x004368c0 ; call 0x0049307e ; pop %ecx ; ret
// which is preceded by the matching __xc pair at 0x00436880
//     0x00436880  call 0x00436890 ; jmp 0x004368b0
// and the initialiser at 0x00436890 (`g_004b348c = 0 ; g_004b3490 = 1`).
// So this IS the `??__F` of a file-scope object in clientinput.cpp -- but
// unlike saveio.cpp's (PART D) it carries NO `mov $obj,%ecx ; jmp`,
// because the destructor is inline and has been EXPANDED into the thunk.
// The matching `??__E` is 0x00436890, expanded the same way (`g_004b348c =
// 0 ; g_004b3490 = 1`, both absolute, no %ecx), and 0x00436880 is the
// __xc entry `call 0x00436890 ; jmp 0x004368b0`.
//
// WRITTEN AS THAT OBJECT.  The four COMDATs are the quartet VC6 emits for a
// file-scope object whose constructor and destructor are both in class and
// both EXPANDED into their thunks at /Ob1:
//     0x00436880  call 0x00436890 ; jmp 0x004368b0     the __xc entry
//     0x00436890  movl $0,0x4b348c ; movl $1,0x4b3490  ??__E, ctor expanded
//     0x004368b0  push $0x004368c0 ; call atexit       registers ??__F
//     0x004368c0  the list teardown                    ??__F, dtor expanded
// Every store is to an absolute address because the object is static.  The
// two words the constructor sets (+0x00, +0x04) and the list head the
// destructor walks (0x004b34d8, +0x4c) are the only bytes of the object any
// code in the image names -- a four-byte-window scan finds no other
// reference to 0x004b348c..0x004b34db -- so the 0x44 bytes between are
// unnamed padding and the names are placeholders.  Whether +0x4c is a member
// or a neighbouring global is not decidable from the bytes (both are masked
// relocations); one object is the reading that needs no second declaration.
class CBindNodeOwnerQ438
{
public:
	CBindNodeOwnerQ438()
	{
		m_n00 = 0;                           // 0x00436890
		m_n04 = 1;                           // 0x0043689a
	}

	~CBindNodeOwnerQ438()
	{
		SBindNodeQ438* pNode;
		SBindNodeQ438* pNext;

		pNode = m_pList;                     // 0x004368c1

		while (pNode)                        // 0x004368ca / 0x00436937
		{
			pNext = pNode->m_pNext;          // 0x004368cf

			di_FreeBindNodeQ438(pNode);

			pNode = pNext;                   // 0x00436935
		}
	}

	uint32         m_n00;                    // 0x00  0x004b348c
	uint32         m_n04;                    // 0x04  0x004b3490
	uint8          m_pad08[0x4c - 0x08];
	SBindNodeQ438* m_pList;                  // 0x4c  0x004b34d8
};

// 0x004b348c.
static CBindNodeOwnerQ438 g_BindNodeOwnerQ438;


// ======================================================================= //
//  PART F -- 0x0043a390, the warp blitter's edge setup
//
//  WHAT IDENTIFIES IT.  Exactly one caller, 0x0040e2b3, inside the function
//  that names itself "InternalWarpSurfaceToSurface" at 0x0040e308 (a
//  de_error.h literal, 0x004a8260).  That caller has a 0x6018-byte frame
//  holding TWO 0x3000-byte arrays -- 0x3000 == 1024 * 12 -- and hands the
//  SAME two arrays, in the SAME order, to the span filler at 0x0040e2d1,
//  whose third and fourth parameters diblit.cpp PART FOUR already calls
//  pLeft and pRight.  So argument 1 is pLeft and argument 2 is pRight, and
//  the record is diblit.cpp's SWarpEdgeQ438 {int16 x; int16 pad; int32 u;
//  int32 v;}.
//
//  WHICH ARRAY GETS WHICH EDGE.  At 0x0043a3de the two scanline indices are
//  compared with `jae` -- UNSIGNED, so they are uint32 -- and the taken arm
//  (pPrev is at or below pCur on screen, i.e. the edge runs UP) loads
//  argument 1 at 0x0043a3f7, while the fall-through (the edge runs DOWN)
//  loads argument 2 at 0x0043a3eb.  Upward edges go to pLeft, downward to
//  pRight.
//
//  THE FIXED POINT.  0x004a23c0 holds 0x47800000 == 65536.0f, so u and v
//  are 16.16 -- which is exactly how diblit.cpp's span fillers read them
//  back (`v >> 16`, `u >> 16`).
//
//  THE OUTER LOOP IS A COUNTDOWN, not a counting `for`: one signed entry
//  test (`test %eax,%eax ; jle 0x0043a4de` at 0x0043a3b0/0x0043a3bc) and a
//  bare `dec %eax ; ... ; jne` close at 0x0043a4cd/0x0043a4d6 with no
//  compare -- the countdown rule.  `pPrev` is set to the LAST vertex before
//  the guard (0x0043a3b8 `lea -0x10(%edx,%ebp,1),%ebx`), which is the
//  wrap-around every polygon edge walk starts with.
//
//  CONFIDENCE.  Lower than the rest of this file: the body is 340 bytes of
//  x87 with four `__ftol` calls (0x004928c0) and an `%st` value (fDY) held
//  live ACROSS three of them, so register and x87-stack allocation carry
//  more of the shape here than anywhere else in the file.  The statement
//  set and its order are firm; the spill layout is not.
// ======================================================================= //

// Same layout as diblit.cpp PART FOUR's SWarpEdgeQ438: `mov %ax,(%esi)` at
// 0x0043a49e is a 16-bit store and +0x02 is never touched.
struct SWarpEdgeW12Q438
{
	int16 x;                                 // 0x00
	int16 m_pad02;                           // 0x02
	int32 u;                                 // 0x04  16.16
	int32 v;                                 // 0x08  16.16
};

// 0x0043a390, 340 bytes.  0x0043a390..0x0043a4e3 inclusive.  EXTENT CLEAN:
// the only inbound displacement in the whole .text is the call at
// 0x0040e2b3; every other branch in the range is internal and the `ret` at
// 0x0043a4e3 lands on the boundary.
void cis_SetupWarpEdgesQ438(SWarpEdgeW12Q438* pLeft, SWarpEdgeW12Q438* pRight,
                            LTWarpPtQ438* pPts, int nPts,
                            uint32* pMinY, uint32* pMaxY)
{
	LTWarpPtQ438*     pPrev;
	LTWarpPtQ438*     pCur;
	LTWarpPtQ438*     pTop;
	LTWarpPtQ438*     pBottom;
	SWarpEdgeW12Q438* pEdge;
	SWarpEdgeW12Q438* pEnd;

	uint32 nYPrev, nYCur, nYTop, nYBottom;
	int    n;
	int32  nU, nV, nUStep, nVStep;
	float  fX, fXStep, fDY;

	*pMinY = 5000;                           // 0x0043a39b  movl $0x1388
	*pMaxY = 0;                              // 0x0043a3b2

	pPrev = &pPts[nPts - 1];                 // 0x0043a3ad / 0x0043a3b8

	if (nPts > 0)                            // 0x0043a3bc  jle
	{
		// The countdown is its own local (0x10(%esp)) and pCur homes in
		// nPts's dead slot, both stored after the esi/edi pushes -- so
		// both are set inside the guard.
		pCur = pPts;
		n    = nPts;

		do
		{
			nYPrev = (uint32)pPrev->dest_y;  // 0x0043a3cc -> __ftol
			nYCur  = (uint32)pCur->dest_y;   // 0x0043a3d4 -> __ftol

			if (nYPrev < nYCur)              // 0x0043a3de  jae
			{
				nYTop    = nYPrev;
				nYBottom = nYCur;
				pTop     = pPrev;
				pBottom  = pCur;
				pEdge    = &pRight[nYTop];   // 0x0043a3eb
			}
			else
			{
				nYTop    = nYCur;
				nYBottom = nYPrev;
				pTop     = pCur;
				pBottom  = pPrev;
				pEdge    = &pLeft[nYTop];    // 0x0043a3f7
			}

			if (nYTop < *pMinY)              // 0x0043a407  jae
				*pMinY = nYTop;

			if (nYBottom > *pMaxY)           // 0x0043a411  jbe
				*pMaxY = nYBottom;

			// No count local: named, its live range puts nYTop in edx
			// and nYBottom in ecx (the reverse of shipped); written out
			// at both uses, VC6 CSEs it into edx's spill at 0x14(%esp).
			if (nYBottom - nYTop)            // 0x0043a41d  je 0x0043a4c4
			{
				fX  = pTop->dest_x;                          // 0x0043a423
				fDY = pBottom->dest_y - pTop->dest_y;        // 0x0043a426

				fXStep = (pBottom->dest_x - pTop->dest_x) / fDY;  // 0x0043a42c

				nU = (int32)(pTop->source_x * 65536.0f);     // 0x0043a438
				nV = (int32)(pTop->source_y * 65536.0f);     // 0x0043a445

				nUStep = (int32)((pBottom->source_x - pTop->source_x) /
				                 fDY * 65536.0f);            // 0x0043a455
				nVStep = (int32)((pBottom->source_y - pTop->source_y) /
				                 fDY * 65536.0f);            // 0x0043a46a

				pEnd = pEdge + (nYBottom - nYTop);           // 0x0043a489

				while (pEdge < pEnd)                         // 0x0043a48f
				{
					pEdge->x = (int16)fX;                    // 0x0043a495
					pEdge->u = nU;                           // 0x0043a4a5
					pEdge->v = nV;                           // 0x0043a4ac

					fX += fXStep;                            // 0x0043a4a8
					pEdge++;                                 // 0x0043a4af
					nU += nUStep;                            // 0x0043a4b2
					nV += nVStep;                            // 0x0043a4b4
				}
			}

			pPrev = pCur;                    // 0x0043a4c8
			pCur++;                          // 0x0043a4ca
		}
		while (--n);                         // 0x0043a4cd  dec / jne
	}
}


// ======================================================================= //
//  PART G -- 0x0040ea30, ci_FillRect (576 bytes)
//
//  HOME: src/engine/lithtech/cissurf.cpp -- the cis_ surface TU
//  (0x0040c000..0x0040f2ff).  Written HERE because it calls PART B's
//  rect_IntersectQ438 (0x0043a070, its only caller: 0x0040eb19) and this
//  is the file that has it.
//  clientinterface.h:363 names it; the de_error literal "FillRect"
//  (0x004a8334) is pushed at 0x0040ea8c / 0x0040ead4 / 0x0040ec57.
//
//  THE REQUEST.  The 0x58-byte object at 0x2c(%esp) is constructed by
//  0x0045f340 (lt2new2.cpp section 18's CPFormatHolderView): +0x00 a
//  PFormat* pointed at its own embedded PFormat at +0x20, +0x04 / +0x08
//  zeroed, +0x0c..+0x18 zeroed (an LTRect member), +0x1c zeroed.  The body
//  fills +0x04 with the locked bits (0x0040eba9), +0x08 with the pitch
//  (0x0040eb56 / 0x0040eb92), +0x0c..+0x18 with the clipped rect
//  (0x0040ebbf..0x0040ebdb), +0x1c with hColor (0x0040ebe3) and +0x00 with
//  &g_ScreenFormat (0x0040ebee), then hands it to 0x0045f430, a __thiscall
//  on g_pClientMgr+0x1618 (0x0040ebd6 / 0x0040ebe8) -- the blitter.
//
//  THE LOCK / UNLOCK are diblit.cpp's cis_LockSurface (0x0043acb0) and
//  cis_UnlockSurface (0x0043afc0), expanded: the `*pPitch = 0` store is
//  0x0040eb56, the screen test 0x0040eb50, the `neg/sbb/and` 0x0040eb83,
//  and the `bWrite && pBits && pSurface` test folds to the one `cmp %edi,
//  %eax` at 0x0040eb9f because bWrite is a constant and pSurface was
//  already null-checked.
// ======================================================================= //

typedef unsigned int HLTCOLORQ40E;
typedef unsigned int LTRESULTQ40E;

// cissurf.cpp's CSurfaceQ40C, the fields reached here.
struct CSurfaceQ40E
{
	void*   m_pFreeNext;                     // 0x000
	void*   m_pNext;                         // 0x004
	void*   m_pSysMem;                       // 0x008
	void*   m_pRenderData;                   // 0x00c  0x0040eb8b
	uint32  m_nWidth;                        // 0x010  0x0040eaf8
	uint32  m_nHeight;                       // 0x014  0x0040eafb
	uint32  m_nPitch;                        // 0x018  0x0040eb8e
	uint32  m_nFlags;                        // 0x01c  0x0040eba3
};

// cissurf.cpp's RenderStructQ40C, the slots reached here.
struct RenderStructQ40E
{
	uint8   m_pad_00[0xd4];
	void*  (*LockSurface)(void* pRenderData);                       // 0x0d4
	void   (*UnlockSurface)(void* pRenderData);                     // 0x0d8
	LTBOOL (*OptimizeSurface)(void* pRenderData, HLTCOLORQ40E h);   // 0x0dc
	void   (*UnoptimizeSurface)(void* pRenderData);                 // 0x0e0
	LTBOOL (*LockScreen)(int nLeft, int nTop, int nRight, int nBottom,
	                     void** ppPixels, uint32* pPitch);          // 0x0e4
	void   (*UnlockScreen)();                                       // 0x0e8
};

// lttexmod.cpp's PFormat: a one-slot vtable (0x004a1ab4), 0x38 bytes.
class PFormatQ40E
{
public:
	virtual void Init(uint32 nBitDepth, uint32 aMask, uint32 rMask,
	                  uint32 gMask, uint32 bMask);             // 0x0045f0e0

	uint32 m_nBitDepth;                      // 0x04
	uint32 m_Masks[4];                       // 0x08
	uint8  m_pad_18[0x38 - 0x18];
};

// 0x58 bytes, constructor 0x0045f340 (out of line, so declared only).
class FillRequestQ40E
{
public:
	FillRequestQ40E();                       // 0x0045f340

	PFormatQ40E* m_pDestFormat;              // 0x00
	void*        m_pDest;                    // 0x04
	uint32       m_nDestPitch;               // 0x08
	LTRectQ438   m_Rect;                     // 0x0c
	HLTCOLORQ40E m_Color;                    // 0x1c
	PFormatQ40E  m_DestFormat;               // 0x20
};

class CBlitterQ40E
{
public:
	LTRESULTQ40E FillRect(FillRequestQ40E* pRequest);          // 0x0045f430
};

struct CClientMgrQ40E
{
	uint8        m_pad_00[0x1618];
	CBlitterQ40E m_Blitter;                  // 0x1618
};

extern RenderStructQ40E* g_pRenderQ40E;                    // 0x004b17a4
extern CSurfaceQ40E      g_ScreenSurfaceQ40E;              // 0x004b1620
// The screen size (0x004b1630 / 0x004b1634) is the screen surface's own
// m_nWidth / m_nHeight (+0x10 / +0x14), not a global of its own.
extern PFormatQ40E       g_ScreenFormatQ40E;               // 0x004b1758
extern CClientMgrQ40E*   g_pClientMgrQ40E;                 // 0x004b17e0

extern "C" void dsi_OnReturnErrorQ40E(int code);           // 0x0042d760
extern int      g_DebugLevelQ40E;                          // 0x004b0ab0
extern char*    g_pErrorFormatQ40E;                        // 0x004a7110
void            con_ConsolePrintQ40E(char* pFormat, ...);  // 0x0042e6f0

#define Q40E_ERRSTR_(x) #x
#define Q40E_ERRSTR(x)  Q40E_ERRSTR_(x)

#define Q40E_RETURN_ERROR(level, fnName, code) \
	{ \
		dsi_OnReturnErrorQ40E(code); \
		if (g_DebugLevelQ40E >= level) \
			con_ConsolePrintQ40E(g_pErrorFormatQ40E, fnName, \
			                     Q40E_ERRSTR(code), " "); \
		return code; \
	}

// diblit.cpp's cis_LockSurfaceQ438 / cis_UnlockSurfaceQ438, inline here.
inline void* cis_LockSurfaceQ40E(CSurfaceQ40E* pSurface, uint32* pPitch,
                                 LTBOOL bWrite)
{
	void* pBits;

	if (!g_pRenderQ40E)
		return LTNULL;

	*pPitch = 0;

	if (pSurface == &g_ScreenSurfaceQ40E)
	{
		if (!g_pRenderQ40E->LockScreen(0, 0, g_ScreenSurfaceQ40E.m_nWidth,
		                               g_ScreenSurfaceQ40E.m_nHeight, &pBits, pPitch))
		{
			return LTNULL;
		}

		return pBits;
	}

	*pPitch = pSurface->m_nPitch;

	pBits = g_pRenderQ40E->LockSurface(pSurface->m_pRenderData);

	if (bWrite && pBits && pSurface)
		pSurface->m_nFlags |= 4;

	return pBits;
}

inline void cis_UnlockSurfaceQ40E(CSurfaceQ40E* pSurface)
{
	if (pSurface == &g_ScreenSurfaceQ40E)
		g_pRenderQ40E->UnlockScreen();
	else
		g_pRenderQ40E->UnlockSurface(pSurface->m_pRenderData);
}

// M-P0-03: fast FillRect.  A fill of the SCREEN locks the whole back buffer
// and writes the pixels in software, which costs the video card a round
// trip of the frame per rectangle -- menus, sliders, the scope and the
// letterbox fill many each frame.  A renderer that offers a colour fill
// (the modern d3d.ren: GetRenderObject("FillScreenRect"), d3dblit.cpp)
// does it there instead.  The colour is converted to the screen format the
// same way the software fill converts it, so the pixels are the same.
// Returns FALSE (and the caller does the retail fill) when the renderer has
// no such fill or it fails.
class FMFormat;

class FMConvertMgrAPI                                // fmconvert.cpp
{
public:
	unsigned long PValueToFormatColor(FMFormat* pFormat, unsigned long nValue,
	                                  unsigned long* pOut);   // 0x0045f5e0
};

struct RenderHookQ40E
{
	uint8   m_pad_00[0xb8];
	void*  (*GetRenderObject)(const char* pName);                   // 0x0b8
};

typedef LTBOOL (__cdecl *FillScreenRectFnQ40E)(LTRectQ438* pRect, uint32 nPixel);

static LTBOOL ci_ModernFillScreen(LTRectQ438* pRect, HLTCOLORQ40E hColor)
{
	FillScreenRectFnQ40E fnFill;
	unsigned long        nPixel;

	if (g_ScreenFormatQ40E.m_nBitDepth != 2 && g_ScreenFormatQ40E.m_nBitDepth != 3)
		return LTFALSE;

	fnFill = (FillScreenRectFnQ40E)
		((RenderHookQ40E*)g_pRenderQ40E)->GetRenderObject("FillScreenRect");
	if (!fnFill)
		return LTFALSE;

	nPixel = 0;
	((FMConvertMgrAPI*)&g_pClientMgrQ40E->m_Blitter)->PValueToFormatColor(
		(FMFormat*)&g_ScreenFormatQ40E, hColor, &nPixel);

	// 16-bit screens take the low word, as the software fill writes it.
	if (g_ScreenFormatQ40E.m_nBitDepth == 2)
		nPixel &= 0xffff;

	return fnFill(pRect, nPixel);
}
LTRESULTQ40E ci_FillRectQ40E(CSurfaceQ40E* hDest, LTRectQ438* pRect,
                             HLTCOLORQ40E hColor)
{
	LTRectQ438 rect, rSurf;
	FillRequestQ40E request;
	CSurfaceQ40E* pSurface;
	LTRESULTQ40E dResult;

	if (!g_pRenderQ40E)
		Q40E_RETURN_ERROR(1, "FillRect", 73);

	pSurface = hDest;
	if (!pSurface)
		Q40E_RETURN_ERROR(1, "FillRect", 60);

	if (pRect)
	{
		rSurf.left = rSurf.top = 0;
		rSurf.right = pSurface->m_nWidth;
		rSurf.bottom = pSurface->m_nHeight;
		if (!rect_IntersectQ438(&rect, &rSurf, pRect))
			return 0;
	}
	else
	{
		rect.left = rect.top = 0;
		rect.right = pSurface->m_nWidth;
		rect.bottom = pSurface->m_nHeight;
	}
	// M-P0-03: fast FillRect
	if (pSurface == &g_ScreenSurfaceQ40E && ci_ModernFillScreen(&rect, hColor))
		return 0;

	request.m_pDest = cis_LockSurfaceQ40E(pSurface, &request.m_nDestPitch,
	                                      LTTRUE);
	if (request.m_pDest)
	{
		request.m_Rect = rect;
		request.m_Color = hColor;
		request.m_pDestFormat = &g_ScreenFormatQ40E;

		dResult = g_pClientMgrQ40E->m_Blitter.FillRect(&request);

		cis_UnlockSurfaceQ40E(pSurface);

		return dResult;
	}

	Q40E_RETURN_ERROR(1, "FillRect", 1);
}


// *********************************************************************** //
//
//  PER-ADDRESS REPORT
//
// *********************************************************************** //
//
//  WRITTEN ABOVE, 8 rows (one of them at a corrected address):
//
//    0x0043a070  100  rect_IntersectQ438              surface / cissurf
//    0x00434070  114  mat_GetEulerBasisVectorsQ438    ltmatrix.h helper run
//    0x004335d0   59  CSaveXlatInit::ctor (saveio.cpp)
//                     (the enumerated row says 0x004335c0/75 -- see below)
//    0x00436ae0   82  CBindRecQ438::DeleteQ438        clientinput.cpp
//    0x004368c0  125  ~CBindNodeOwnerQ438 (??__F)     clientinput.cpp
//    0x0043a390  340  cis_SetupWarpEdgesQ438          diblit.cpp
//    0x0043a960   81  CWarpClipRightQ438::Interpolate diblit.cpp
//    0x0043a9c0   81  CWarpClipTopQ438::Interpolate   diblit.cpp
//
//  ALREADY WRITTEN IN THE REPO:
//
//    0x00431ff0   58  CSaveIO::WriteChecked, src/engine/server/saveio.cpp
//                     line 44, declared src/engine/server/saveio.h line 52.
//
//      [CLOSED on both images, together with ReadChecked 0x00431fa0 --
//      the sentinel is now a block-scoped local in saveio.cpp; the analysis
//      below is kept for the record.]
//
//      This is an o2gap row, not reconstruction.  The /O1 twin is
//      the retail server.dll 0x100100cd and our src/engine/server/Release/
//      saveio.obj reproduces it BYTE FOR BYTE, all 56 bytes, verified by
//      hand against a dump of the retail bytes.  The source is therefore
//      already proven correct and only the /O2 shape is wrong.
//
//      The /O2 miss is 2 bytes and one register:
//        shipped 0x00431ff0 keeps nLen in %edi across the call
//            mov 0x04(%esp),%edx   ; pData FIRST
//            push %esi ; mov %ecx,%esi ; push %edi
//            mov 0x10(%esp),%edi   ; nLen
//            ...  test %edi,%edi   ; no reload
//        ours (src/engine/lithtech/Release/saveio.obj, 60 bytes)
//            mov 0x08(%esp),%edx   ; nLen FIRST
//            push %esi ; mov %ecx,%esi ; push %edx
//            mov 0x0c(%esp),%edx   ; pData
//            ...  mov 0x0c(%esp),%ecx ; test %ecx,%ecx   ; RELOADS
//      i.e. +2 for the push/pop %edi we do not have, -4 for the reload we
//      do.  ReadChecked (0x00431fa0, 65 vs our 67) has the identical diff
//      from the identical cause, so ONE lever closes both.  Both the
//      guards and the `nLen = SAVE_SENTINEL` store into the PARAMETER's own
//      home slot are already right (shipped 0x00432011 writes 0x10(%esp),
//      ours writes 0x0c(%esp); same slot).  Untested candidates, in the
//      order to measure them: (a) load-order -- shipped materialises
//      pData before nLen, which "hoist a subexpression to fix
//      evaluation order" covers; (b) spelling the guard as the positive
//      nest `if (nLen && dResult == LT_OK) { ... }`.
//
//  EXTENT DEFECTS FOUND
//
//    0x004335c0 / 75 is TWO COMDATS plus padding.  0x004335c0..0x004335c9
//    is the compiler-generated `??__F` destructor thunk (`mov $0x4b3394,
//    %ecx ; jmp 0x00490200`), 0x004335ca..0x004335cf is alignment `nop`s,
//    and the source function is 0x004335d0..0x0043360a, 59 bytes.  Writing
//    the file-scope object reproduces all four COMDATs, so nothing is lost
//    -- but the START ADDRESS in the enumeration is wrong and a byte compare
//    anchored at 0x004335c0 will never succeed.
//
//    All eight other rows passed: for each one every e8 / e9 /
//    0f 8x in the whole .text was swept for a displacement landing inside
//    the range.
//    0x0043a070, 0x00434070, 0x0043a390, 0x0043a960, 0x0043a9c0 and
//    0x00436ae0 each have exactly ONE inbound edge and it is to the entry;
//    0x004368c0 has none at all (its only reference is the atexit push at
//    0x004368b1, which is a `push $imm32` in .text -- provenance sound but
//    it is DATA-IN-TEXT, the shape to distrust for `setup` rows,
//    so it was checked by disassembling 0x004368b0 rather than trusting it).
//    Every range ends on an instruction boundary.
//
//  CORRECTIONS TO EXISTING FILES (no file was edited)
//
//    src/engine/lithtech/diblit.cpp, the "NOT WRITTEN" block on 0x0043a4f0,
//    says three of the four clip passes are inlined and only 0x0043b270 is
//    called.  True of the LOOP, false of the vertex interpolation: passes 2
//    and 3 call 0x0043a9c0 (0x0043a7a0) and 0x0043a960 (0x0043a8ed).  Only
//    pass 1 inlines it (0x0043a611).  Whoever writes 0x0043a4f0 needs two
//    calls in the middle of two inlined loops, which is a harder shape than
//    that note implies.
//
//    The decoded object map attributes 0x0043a* to netmgr.obj.  All
//    four 0x0043a rows here are client surface code with no server.dll
//    twin.  netmgr.obj's real client rows are the seventeen already
//    decoded (0x0041d550..0x0045c020).
//
//  COIN-FLIPS LEFT OPEN, both spellings given
//
//    1. PART A's calling convention.  A member with an unused `this`
//       (written) and a `__stdcall` free function produce IDENTICAL callee
//       bytes; only the caller's `lea 0x2c(%esp),%ecx` distinguishes them,
//       and that caller is not among these rows.
//    2. PART B's return.  `return pOut->left < pOut->right && ...` (written)
//       versus four locals stored and then compared.  Same comparisons;
//       the interleaving of the pOut load with the first LTMAX is the only
//       evidence and it is weak.
//    3. PART D's layout (now saveio.cpp's).  A `uint8 [8]` beside a
//       one-byte object (written) versus a static member array of the
//       class.  All eight addresses are relocations, so byte matching
//       cannot tell.
//    4. PART E's 0x00436ae0.  Written as an explicit member; if VC6 emits
//       `??_GCBindRecQ438@@QAEPAXI@Z` for this class the stand-in is a
//       duplicate and should be deleted, keeping the destructor.
//    5. PART F's outer loop.  `if (n > 0) do {...} while (--n)` (written)
//       versus `for (i = 0; i < n; i++)`; VC6 sometimes strength-reduces
//       the second into the first when `i` is dead, which it is here.
//
//  WHAT BELONGS IN WHICH FILE WHEN THIS IS MERGED
//
//    PART A + PART F   -> src/engine/lithtech/diblit.cpp (it already owns
//                         0x0043a0e0, 0x0043b270 and 0x0043b410/6a0/940)
//    PART B            -> the same file, or cissurf.cpp with ci_FillRect
//    PART C            -> a new clientcommon.cpp / the ltmatrix.h helper run
//                         that transform.cpp already describes
//    PART D            -> src/engine/server/saveio.cpp (DONE)
//    PART E            -> src/engine/lithtech/clientinput.cpp
//
// *********************************************************************** //
