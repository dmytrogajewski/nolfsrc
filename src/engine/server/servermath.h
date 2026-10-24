// *********************************************************************** //
//  MODULE:   SERVERMATH (.H)
//  PURPOSE:  server.dll's ILTCSBase rotation/matrix helpers. These validate
//            their pointer arguments and forward to an engine-internal math
//            implementation EMBEDDED IN the server object at offset +4
//            (recovered from `mov eax,[ecx+4]; add ecx,4; call [eax+slot]`).
//            Slot numbers below are the observed forwarding offsets.
// *********************************************************************** //
#ifndef __SERVERMATH_H__
#define __SERVERMATH_H__

#include "ltbasedefs.h"
#include "iltmath.h"

// Shared by the geometry and math modules; no reconstructed duplicate views.
struct SAxisBasis {
    LTVector m_vRight, m_vUp, m_vNormal;
};
class CAxisBasis : public SAxisBasis {
public:
    CAxisBasis(LTVector right, LTVector up, LTVector normal);
};
extern SAxisBasis g_AxisBases[6];

class ICommonMath
{
public:
	virtual LTRESULT GetRotationVectors(LTRotation*, LTVector*, LTVector*, LTVector*)=0;   // 0x00
	virtual LTRESULT InterpolateRotation(LTRotation*, LTRotation*, LTRotation*, float)=0;  // 0x04
	virtual LTRESULT SetupEuler(LTRotation*, float, float, float)=0;                       // 0x08
	virtual LTRESULT SetupTransformationMatrix(LTMatrix*, LTVector*, LTRotation*)=0;           // 0x0c
	virtual LTRESULT SetupTranslationMatrix(LTMatrix*, LTVector*)=0;                           // 0x10
	virtual LTRESULT SetupRotationMatrix(LTMatrix*, LTRotation*)=0;                            // 0x14
	virtual LTRESULT SetupTranslationFromMatrix(LTVector*, LTMatrix*)=0;                       // 0x18
	virtual LTRESULT SetupRotationFromMatrix(LTRotation*, LTMatrix*)=0;                        // 0x1c
	virtual LTRESULT SetupRotationAroundPoint(LTMatrix*, LTRotation*, LTVector*)=0;            // 0x20
	virtual LTRESULT AlignRotation(LTRotation*, LTVector*, LTVector*)=0;                       // 0x24
	virtual LTRESULT EulerRotateX(LTRotation*, float)=0;                                       // 0x28
	virtual LTRESULT EulerRotateY(LTRotation*, float)=0;                                       // 0x2c
	virtual LTRESULT EulerRotateZ(LTRotation*, float)=0;                                       // 0x30
	virtual LTRESULT RotateAroundAxis(LTRotation*, LTVector*, float)=0;                        // 0x34
	virtual LTRESULT GetRotationVectorsFromMatrix(LTMatrix*, LTVector*, LTVector*, LTVector*)=0;// 0x38
};

// The implementation ICommonMath views is the SDK's own ILTMath
// (iltmath.h): ILTCSBase embeds one at +4 (`ILTMath m_MathLT`), and the
// vtable CLTServer's constructor stores there, 0x1004e86c, is ILTMath's own
// with its fifteen slots at 0x10015ff1..0x100162e6 -- the bodies
// servermath.cpp defines as ILTMath:: (references in the SDK signatures,
// which pass exactly as the pointers ICommonMath spells them).

// The helpers ILTMath is built out of. Nothing here validates anything —
// the interface wrapper above did that — so these are the arithmetic itself
// (names ours except where noted).

// 0x10011802 — a rotation's three axis vectors.
void quat_GetVectors(LTRotation* pRot, LTVector* pRight, LTVector* pUp,
                     LTVector* pForward);

// 0x1001181b — the shortest path between two rotations at a fraction.
void quat_Slerp(LTRotation* pDest, LTRotation* pRot1, LTRotation* pRot2,
                float t);

// 0x1001186d — a rotation from three euler angles (the rotation is the LAST
// argument, not the first).
void rot_SetupEuler(float x, float y, float z, LTRotation* pRot);

// 0x100117f2 — a matrix's rotation, as a quaternion.
void quat_FromMatrix(LTMatrix* pMat, LTRotation* pRot);

// 0x100118d5 — a 4x4 from a position, a rotation and a scale, any of which
// may be null; the three matrix builders below are all this one function
// with the parts they do not want left out.
void mat_SetupTransform(LTVector* pPos, LTRotation* pRot, LTVector* pScale,
                        LTMatrix* pMat);

// 0x10011b1e — the product of two matrices, into a third.
void mat_Mul(LTMatrix* pOut, LTMatrix* pA, LTMatrix* pB);

// 0x100117e2 — a rotation as a matrix, the other direction from
// quat_FromMatrix.
void quat_ToMatrix(LTRotation* pRot, LTMatrix* pMat);

// 0x10011637 — a direction and an up hint turned into three orthogonal axes.
void vec_AlignVectors(LTVector* pVector, LTVector* pUp, LTVector* pRight,
                      LTVector* pUpOut, LTVector* pForward);

// ILTMath::AlignRotation with the up hint optional: the SDK passes a null one
// through, which a reference cannot carry.
void rot_Align(LTRotation* pRotation, LTVector* pVector, LTVector* pUp);

// 0x10011964 — a rotation of `amount` about an arbitrary axis, as a matrix.
// The axis goes by value, twelve bytes copied onto the stack at the call.
void mat_SetupRotationAboutAxis(LTMatrix* pMat, LTVector vAxis, float amount);

// ---- what the five helpers reconstructed below forward to ---------------- //
//
// Each of these lives in the engine's own math module (the 0x100114xx-
// 0x100135xx block that also holds the LTMatrix/LTVector method bodies), and
// none of them is named by anything in the image. All that is recoverable
// from this side is the argument count, the argument order and, for
// sub_10015ce2, the fact that the wrapper hands its two arguments over the
// other way round — so they are named by address rather than by guess.

// 0x10015ce2 — the matrix-to-quaternion arithmetic. Takes the rotation first;
// quat_FromMatrix, which takes the matrix first, swaps them on the way in.
void sub_10015ce2(LTRotation* pRot, LTMatrix* pMat);

// 0x10015e17 — the three axis vectors of a quaternion.
void sub_10015e17(LTRotation* pRot, LTVector* pRight, LTVector* pUp,
                  LTVector* pForward);

// 0x10015e69 — the interpolation itself.
void sub_10015e69(LTRotation* pDest, LTRotation* pRot1, LTRotation* pRot2,
                  float t);

// 0x1001149a — given a forward direction and an up hint (which may be null),
// produce the orthogonal up. 413 bytes of it, in the math module.
void sub_1001149a(LTVector* pForward, LTVector* pUp, LTVector* pUpOut);

// The server object: own vptr at +0, the math implementation embedded at +4.
class CLTServerMath
{
public:
	virtual void _ownvirtual();
	ILTMath m_common;

	LTRESULT GetRotationVectors(LTRotation* pRotation, LTVector* pUp, LTVector* pRight, LTVector* pForward);
	void GetRotationVectorsFromMatrix(LTMatrix* pMat, LTVector* pUp, LTVector* pRight, LTVector* pForward);
	void RotateAroundAxis(LTRotation* pRotation, LTVector* pAxis, float amount);
	void EulerRotateX(LTRotation* pRotation, float amount);
	void EulerRotateY(LTRotation* pRotation, float amount);
	void EulerRotateZ(LTRotation* pRotation, float amount);
	void AlignRotation(LTRotation* pRotation, LTVector* pVector, LTVector* pUp);
	LTRESULT SetupEuler(LTRotation* pRotation, float pitch, float yaw, float roll);
	LTRESULT InterpolateRotation(LTRotation* pDest, LTRotation* pRot1, LTRotation* pRot2, float t);
	void SetupTransformationMatrix(LTMatrix* pMat, LTVector* pTranslation, LTRotation* pRotation);
	void SetupTranslationMatrix(LTMatrix* pMat, LTVector* pTranslation);
	void SetupRotationMatrix(LTMatrix* pMat, LTRotation* pRot);
	void SetupTranslationFromMatrix(LTVector* pTranslation, LTMatrix* pMat);
	void SetupRotationFromMatrix(LTRotation* pRot, LTMatrix* pMat);
	void SetupRotationAroundPoint(LTMatrix* pMat, LTRotation* pRot, LTVector* pPoint);
};

#endif
