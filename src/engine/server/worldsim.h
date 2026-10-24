// *********************************************************************** //
//  MODULE:   WORLDSIM (.H)
//  PURPOSE:  The physics module at 0x100022ac-0x100054ff. Twenty-three
//            functions, and only three of them are reached from outside: a
//            box-versus-BSP test, the object mover, and one more. Everything
//            else is a private call tree under those, which is why nothing in
//            here carries a name from a vtable or an error tag — the two
//            error strings it does have ("physics recursed infinitely on a
//            %s", "World %s doesn't have PBlocks!") name the module's purpose
//            rather than its functions.
//
//            Class and function names are ours throughout.
// *********************************************************************** //
#ifndef __WORLDSIM_H__
#define __WORLDSIM_H__

#include "ltbasedefs.h"
#include "world_line_query.h"
#include "ltobject_internal.h"
#include "counter.h"
#include "world_move.h"
#include "de_error.h"

// One node of a world's BSP tree. The plane is two indirections away, the
// same walk WorldPoly::GetPlane makes; the flag byte at 0x16 marks a node
// with nothing below it, and both of its low bits do.
#define BSPNODE_LEAF 0x03

// The two leaf bits on their own. bsp_PointInside tests them one at a time
// and answers zero for 0x02 and one for 0x01; bsp_SphereInside answers a
// straddle whose back child carries 0x02 with two, and otherwise hands back
// the last leaf's 0x01 bit. Which bit is the solid side and which the empty
// one is not something these bytes say, so they are named by their value
// (names ours).
#define BSPNODE_LEAF_BIT0 0x01
#define BSPNODE_LEAF_BIT1 0x02

// The sphere walk's stack is five hundred nodes on the machine stack — the
// `sub $0x7dc,%esp` frame is that array plus the twelve bytes of the point
// copy and nothing else.
#define BSP_SPHERE_STACK 500

// What a sphere test is asked about: a radius and where it is.
class CSphereQuery
{
public:
	float    m_fRadius;                      // 0x00
	LTVector m_vPoint;                       // 0x04
};

// 0x10056ff8 — the walk's own stack. It is a file-scope array rather than
// recursion, which is what lets the walk run to any depth without touching
// the machine stack; a pointer equal to the base means nothing is left to
// come back to (name ours).



// 0x100032be — is any of the tree's geometry within the sphere? (name ours)
int bspq_TestSphere(BSPNode* pNode, CSphereQuery* pQuery);

// 0x100582a8 — the bounding sphere of whatever box is being tested. One
// global rather than a parameter, like everything else the module works on.
// The other two are the box at each end of a move, and the radius they all
// share is the box's own, worked out once by the caller (names ours).
extern thread_local CSphereQuery g_BoxSphere;
extern thread_local CSphereQuery g_SphereAtOffset;
extern thread_local CSphereQuery g_SphereAtDelta;
extern thread_local float        g_fBoxRadius;

// Below this, a move has not really moved and there is nothing to redo.
#define WS_MOVE_EPSILON 0.0001

// The world's own cheap containment tests, which answer before any of the
// box machinery is set up (0x10012c3a and 0x10012ba2; names ours).
int bsp_PointInside(BSPNode* pRoot, LTVector* pPoint);
int bsp_SphereInside(BSPNode* pRoot, LTVector* pPoint, float fRadius);

// 0x10002fff — does this box touch the world? One of the three ways into the
// module from outside it (name ours).
int ws_GetBoxStatus(BSPNode* pRoot, LTVector* pMin, LTVector* pMax);
int ws_BoxTouchesSolidSurface(BSPNode* root, LTVector minimum, LTVector maximum);

// How far a resting object is pushed back off the surface it hit, and how
// much of the impulse becomes momentum. Both are in the shipped constants
// rather than in any tunable (names ours).
#define WS_SEPARATION  -0.05f
#define WS_IMPULSE      0.01f

// 0x100051ec — what hitting a surface does to an object. Answers zero and
// touches nothing when the object is already moving away from the surface,
// which is what makes a glancing contact free (name ours).
int ws_Bounce(LTObject* pObj, LTVector* pVel, LTVector* pNormal,
              LTVector* pImpulse, LTVector* pMomentum);

// Where a coordinate falls against one axis of the region being tested.
// Callers classify both ends of a segment and compare: two ends with the
// same non-zero answer are both outside the same face, and the segment
// cannot cross the region at all (names ours).
#define WS_INSIDE  0
#define WS_ABOVE   1
#define WS_BELOW   2

// What a plane test answers. Behind and in-front are the cheap cases; only a
// straddle makes the caller do any more work.
// The eight corners of the swept box projected onto one plane: how far the
// nearest of them falls in front of it and how far the furthest does. The six
// axis-aligned cases have their own tests, which read the box's own bounds
// straight off instead of touching the corners at all (names ours).
class CBoxProject
{
public:
	LTPlane*      m_pPlane;                  // 0x00
	LTVector*     m_pPoints;                 // 0x04  the eight corners
	CSphereQuery* m_pSphere;                 // 0x08  the box's bounding sphere
	float         m_fMin;                    // 0x0c
	float         m_fMax;                    // 0x10
	uint32*       m_pProjected;              // 0x14  cleared when the cheap
	                                         //       sphere test answered and
	                                         //       the interval was never
	                                         //       filled in
	LTVector      m_vMin;                    // 0x18  the box's own bounds
	LTVector      m_vMax;                    // 0x24
};

// How far past a plane a corner has to be before it counts as being on the
// far side of it. Negative, so a corner exactly on the plane is in front.
#define WS_PLANE_SLOP -0.001f

int ws_ProjectBox(CBoxProject* pProject);                          // 0x10001fd1
int ws_ProjectBoxCheap(CBoxProject* pProject);                     // 0x10002069

int ws_ProjectBoxXPos(CBoxProject* pProject);                      // 0x100020d8
int ws_ProjectBoxXNeg(CBoxProject* pProject);                      // 0x10002124
int ws_ProjectBoxYPos(CBoxProject* pProject);                      // 0x10002174
int ws_ProjectBoxYNeg(CBoxProject* pProject);                      // 0x100021c0
int ws_ProjectBoxZPos(CBoxProject* pProject);                      // 0x10002210
int ws_ProjectBoxZNeg(CBoxProject* pProject);                      // 0x1000225c

// 0x1005118c — the six above in the same order as g_BoxPlanes, with the
// general projection last, so the byte a polygon carries picks the cheapest
// test that will do for its plane.
typedef int (*WSProjectFn)(CBoxProject* pProject);

extern WSProjectFn g_BoxProjectFns[7];

// The move the module is working on, as the caller described it. Only the
// fields the physics touches are named here (name ours).
// Collision callbacks use the same native mover as the enclosing move.
void ws_ObjectsCollided(IObjectMover*, LTObject*, LTObject*, LTVector*, float);

// The world's physics block for the region a move starts in. All the module
// wants from it is the index the sweeps look their geometry up by (name ours).
// One node of a physics block's tree: the polygon on its plane, and the two
// children either side of it. Everything is a sixteen-bit index into the
// block, with two reserved values standing in for the two kinds of leaf
// (names ours).
#define WS_NODE_EMPTY 0xfffe
#define WS_NODE_SOLID 0xffff

// Deep enough for any world the game ships.
#define WS_NODE_STACK 1024

extern thread_local CPhysicsBlock* g_pPBlock;

class CMoveState
{
public:
	IObjectMover*   m_pHost;                  // 0x00
	CollisionInfo* m_pInfo;                  // 0x04  filled in for the caller
	bool           m_pUnknown08;             // 0x08  copied out of the world
	                                         //       model move state's +0x18
	                                         //       (0x1001f9ac); nothing in
	                                         //       this module reads it
	IWorldBsp*     m_pWorldBsp;              // 0x0c  names polygons; null for
	                                         //       an object-vs-object hit
	HOBJECT        m_hObject;                // 0x10
	LTVector       m_vStart;                 // 0x14
	LTVector       m_vEnd;                   // 0x20
	LTVector       m_vDims;                  // 0x2c  half-extents of the box
	uint32         m_n38;                    // 0x38  set means try the step-up
	LTObject*      m_pObject;                // 0x3c  the thing being moved
	uint32         m_bPushOut;               // 0x40  push clear of a contact
	                                         //       rather than stopping at it
	uint32         m_n44;                    // 0x44
};

// One polygon's worth of test state: the box projected onto that polygon's
// plane at each end of the move. The two halves share one "was it actually
// projected" flag, so the cheap sphere test only has to clear it once.
class CPolyTest
{
public:
	CBoxProject m_Start;                     // 0x00
	CBoxProject m_End;                       // 0x30
};

// A plane the moving box has been found inside, remembered with whatever
// object put it there so the same object cannot contribute twice. Twenty is
// all the room there is; past that the box is stuck and the extra planes are
// dropped (names ours).
#define WS_MAX_CONTACTS 20

struct WSContact
{
	LTPlane m_Plane;                         // 0x00
	void*   m_pObject;                       // 0x10
};

// How far past a contact plane to push the box, and how far inside one it has
// to be before the push is worth making.
#define WS_PUSH_BIAS    0.1f
#define WS_PUSH_SLOP    0.09f

// Giving up: ten rounds of pushing the box out of its contacts is as far as
// the solver will go before it leaves the box where it is.
#define WS_PUSH_TRIES   10

// What a run of bounces has added up to. The velocity change is what the
// object's own velocity will become, and the momentum is the stopping
// velocity the touch notification reports; the count is how many surfaces
// have contributed, which is what tells the caller a single contact from a
// pile-up (names ours).
class CBounceState
{
public:
	LTVector   m_vPoint;                     // 0x00  where the move got to
	uint32     m_nHits;                      // 0x0c
	WorldPoly* m_pStandingPoly;              // 0x10  what it came to rest on
	LTVector   m_vMomentum;                  // 0x14
	LTVector   m_vVelocityAdd;               // 0x20
};

extern thread_local int       g_nContacts;
extern thread_local WSContact g_Contacts[WS_MAX_CONTACTS];

extern thread_local LTVector   g_vMoveBase;
extern thread_local LTVector   g_vMoveStart;
extern thread_local LTVector   g_vMoveEnd;
extern thread_local CMoveState*   g_pMoveState;
extern thread_local CBounceState* g_pMoveResult;

extern thread_local uint32 g_nPhysContactTime;

// How upward a polygon's normal has to point before the box can be said to
// be standing on it.
#define WS_FLOOR_NORMAL_Y 0.01f

int  ws_IsBoxOutsidePoly(WorldPolyOwner* pPoly, LTVector* pPoints1,
                         LTVector* pPoints2);                      // 0x100045b7

void ws_HandleImpact(CollisionInfo* pInfo, CBounceState* pState,
                     LTObject* pObj, HOBJECT hObject, IWorldBsp* pWorldBsp,
                     WorldPoly* pPoly, int bWorld);              // 0x10005103

// A point move takes at most forty steps. Needing more means the point is
// wedged between surfaces that keep bouncing it back at each other, and the
// module says so rather than spinning.
#define WS_MAX_POINT_STEPS 40

// 0x1000372c — the bounding box of the whole move, and the geometry that goes
// with it (name ours).
void ws_SetupSweptBox(LTVector* pMin, LTVector* pMax);

// Forty attempts at one move. Each one that changes the hit count earns
// another, because pushing clear of one surface can bury the box in the next.
#define WS_MAX_MOVE_STEPS 40

// 0x10001cad — will the physics collide with this polygon at all? Every
// caller in the module goes through this one address: the sphere walk's
// per-node geometry test and both of the box sweep's blocking tests are the
// same function (name ours).
int  w_IsPolyBlocking(WorldPolyOwner* pOwner);

// The polygon-versus-box clip's three working arrays, sized off the frame:
// `sub $0xd58,%esp` is 200 clipped vertices, 200 pointers of output list, 40
// pointers of input list, the twelve-byte CountAdder and eleven dwords of
// locals, and nothing else. Forty is therefore the most vertices a world
// polygon may have (names ours).
#define WS_MAX_POLY_VERTS 40
#define WS_MAX_CLIP_VERTS 200

// How far in front of a clip plane a vertex has to be to count as inside it.
#define WS_CLIP_SLOP 0.001f

// Bit 0 of the world data's flags. Geometry without it is not collided with
// at all, which is the first thing w_IsPolyBlocking checks (name ours).
#define WBSP_SOLID 0x01

// 0x10051174 — the six box planes' normals, one number each: the plane's axis
// is its index halved and the sign alternates, so a dot product against an
// axis-aligned normal is one multiply. Sits immediately before
// g_BoxProjectFns in .data (name ours).
extern float g_BoxSigns[6];

// The two physics statistics this module keeps that are not times: one is the
// running total a CountAdder adds to and the other a plain count. Named by
// their addresses because nothing here says what either measures.
extern thread_local uint32 g_nPhysStatA4;
extern thread_local uint32 g_nPhysStatA8;

// How upward a surface has to face before stepping onto it is even
// considered, and how upward before it counts as flat rather than a slope.
// Both live in writable data, so both are meant to be tunable.
extern float g_fStepMinNormalY;
extern float g_fStepSlopeLimit;

// Nothing is this far away, so the first face always wins.
#define WS_STEP_FAR 5000.0f

int  ws_StairStep(uint16 nBlock, CBounceState* pState);            // 0x10004ccb
int  ws_MoveCylinder(uint16 nBlock, CBounceState* pState,
                     int bRetry);                                  // 0x1000470b
int  ws_MoveBox(uint16 nBlock, CBounceState* pState, int bRetry);  // 0x10003911

// Fifty pushes back along the move is as far as the box sweep will go before
// it abandons the move entirely.
#define WS_MAX_BOX_PUSHES 50

// 0x100033fd — the move, from the outside. Everything else in the module is
// under this (name ours).
void ws_MoveObject(CMoveState* pIn, CBounceState* pState);

int  ws_ShiftBox();                                                // 0x100022ac
void ws_MovePoint(CMoveState* pMove, CBounceState* pState);        // 0x10003366

// The world's line query, as its caller fills it in and as the recursive walk
// answers it. The two dwords in the middle are never read by anything
// reconstructed here, so they keep their offsets for names. Twenty-four bytes,
// which is exactly the `sub $0x18,%esp` bsp_IntersectLine reserves, so there
// is nothing else in that frame.
//
// The default constructor lives at 0x1000f08d, in some other translation unit,
// and zeroes all six fields in the order 0x00, 0x04, 0x08, 0x14, 0x0c, 0x10 —
// not offset order, so the source wrote the result pointer's clear before the
// two unknown dwords (names ours).
// 0x10013f28 — the walk itself, which takes the segment's two ends by value
// as well as the query (its own 43 bytes copy them off the query and forward
// them to 0x10013bf5). Nothing here needs more than its answer (name ours).
int bspq_IntersectLine(BSPNode* pRoot, CLineQuery* pQuery);

// 0x10013bf5 — the recursion under it. The two ends arrive by value because
// the walk splits the segment as it descends and needs its own copy of each
// half; the query object comes along beside them for the crossing point and
// the result (name ours).
int bspq_IntersectLineRec(BSPNode* pNode, CLineQuery* pQuery,
                          LTVector vStart, LTVector vEnd);

// 0x10013f53 — the world's line-versus-BSP query: the first polygon the
// segment crosses, where it crossed it and the plane it crossed (name and
// signature ours).
WorldPoly* bsp_IntersectLine(BSPNode* pRoot, LTVector* pStart,
                             LTVector* pEnd, LTVector* pHit,
                             LTPlane* pPlane);
int  ws_HitPoly(WorldPoly* pPoly, CBounceState* pState,
                CPolyTest* pTest);                                 // 0x10004167

void ws_AddContact(LTPlane* pPlane, void* pObject, float fDepth);  // 0x10004213
void ws_AddClippedContact(LTPlane* pPlane, void* pObject, float fDist);
                                                                   // 0x100044b4
void ws_PushBoxOutOfContacts();                                    // 0x100042db

#define WS_BEHIND   0
#define WS_INFRONT  1
#define WS_STRADDLE 2

// The physics query's own state: a swept volume, described twice over. The
// caller fills in where it starts and ends and how big it is, and Setup
// derives everything the walk needs — the direction and its length, the
// midpoint, the reach from that midpoint, and the squares of all of them,
// because the tests compare squares and never take a root.
//
// The vertical pair at the end is the slab ClassifyAxis reads: the whole
// sweep, opened out by the volume's own height (names ours).
class CSimContext
{
public:
	// 0x10002637 — derive the rest from what the caller filled in.
	void Setup();

	// 0x10002922
	int ClassifyAxis(float f);

	// 0x100027a7 — which side of a plane the swept volume is on, and by how
	// much. Answers WS_BEHIND, WS_INFRONT or WS_STRADDLE; the overlap, when
	// there is one, is left in m_fOverlap for the caller (name ours).
	int ClassifyPlane(LTPlane* pPlane, LTVector* pPoint, int bWantOverlap,
	                  int bCheap);

	// 0x10002948 — does the swept cylinder reach this polygon, and if so is
	// that the deepest thing it has reached? Everything the cylinder sweep
	// knows about geometry comes through here (name ours).
	int TestPoly(WorldPolyOwner* pPoly, WorldPoly* pHandle);

	// 0x10002eb4 — back the sweep's destination out of whatever it ran into
	// and start the sweep again from there (name ours).
	void DeflectOffHit(WorldPoly* pPoly, CBounceState* pState);

public:
	LTVector m_vStart;                       // 0x00
	LTVector m_vEnd;                         // 0x0c
	// Where the sweep is headed: ws_MoveCylinder stores its destination here
	// before the first step (server 0x10004954, lithtech 0x00419bdd) and
	// nothing in the module reads it back (name ours).
	LTVector m_vDest;                        // 0x18
	float    m_fRadius;                      // 0x24
	float    m_fHeight;                      // 0x28
	// Whether a hit is worth reporting to the game, or is only being used to
	// steer the sweep.
	uint32   m_bReport;                      // 0x2c
	// The volume's own half-diagonal: how far its corner reaches from its
	// axis, which is what a sweep has to be widened by.
	float    m_fCornerDist;                  // 0x30
	LTVector m_vDelta;                       // 0x34
	LTVector m_vDir;                         // 0x40  unit, or zero
	float    m_fLength;                      // 0x4c
	LTVector m_vMid;                         // 0x50
	float    m_fReach;                       // 0x5c
	float    m_fRadiusSqr;                   // 0x60
	float    m_fCornerSqr;                   // 0x64
	float    m_fReachSqr;                    // 0x68
	float    m_fLengthSqr;                   // 0x6c
	float    m_fMax;                         // 0x70
	float    m_fMin;                         // 0x74
	// What the last plane test found: how far the volume's centre was from
	// the plane, and how deeply it crossed it.
	float    m_fPlaneDist;                   // 0x78
	float    m_fOverlap;                     // 0x7c
	// Where on the volume the last hit happened.
	LTVector   m_vHitPoint;                  // 0x80
	// What the sweep last ran into: the surface's normal, how far the volume
	// ended up inside it, and the polygon itself.
	LTVector   m_vHitNormal;                 // 0x8c
	float      m_fHitDepth;                  // 0x98
	WorldPoly* m_pHitPoly;                   // 0x9c
};

// How far past a surface to push the volume: a hundred and one percent of
// however deep it got, so it ends up just clear rather than exactly touching.
#define WS_PUSHOUT_SCALE 1.01f

// The steepest surface an object will stand on -- forty-five degrees. A
// steeper one is reported as an impact instead.
#define WS_SLOPE_LIMIT 0.707f

// Sixteen points that bound whatever the module is testing, kept as two sets
// of eight because that is what the projections and the polygon-edge test
// take. For a box standing still each set holds one quad of the box, written
// twice; for a box in motion each set holds all eight corners of the box at
// one end of the move. Either way the union covers the volume, which is all
// the consumers need (names ours).
extern thread_local LTVector g_SweepPoints[2][8];
extern thread_local LTPlane  g_BoxPlanes[6];

// 0x10003140 (name ours).
void ws_SetupBoxGeometry(LTVector* pMin, LTVector* pMax);

#endif  // __WORLDSIM_H__
