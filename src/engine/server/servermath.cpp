// *********************************************************************** //
//  MODULE:   SERVERMATH (.CPP)
//  PURPOSE:  ILTCSBase rotation/matrix helpers — validate pointer args, then
//            forward to the math implementation embedded at server+4.
//            Build /O1.
// *********************************************************************** //

#include "servermath.h"
#include "de_error.h"

LTRESULT CLTServerMath::GetRotationVectors(LTRotation* pRotation, LTVector* pUp, LTVector* pRight, LTVector* pForward)
{
	if (!pRotation || !pUp || !pRight || !pForward)
		RETURN_ERROR_PARAM("ILTCSBase::GetRotationVectors");

	ILTMath* pC = &m_common;
	return pC->GetRotationVectors(*pRotation, *pRight, *pUp, *pForward);
}

void CLTServerMath::GetRotationVectorsFromMatrix(LTMatrix* pMat, LTVector* pUp, LTVector* pRight, LTVector* pForward)
{
	if (!pMat || !pUp || !pRight || !pForward)
		RETURN_ERROR_VOID("ILTCSBase::GetRotationVectorsFromMatrix");

	ILTMath* pC = &m_common;
	pC->GetRotationVectorsFromMatrix(*pMat, *pRight, *pUp, *pForward);
}

void CLTServerMath::RotateAroundAxis(LTRotation* pRotation, LTVector* pAxis, float amount)
{
	if (!pRotation || !pAxis)
		RETURN_ERROR_VOID("ILTCSBase::RotateAroundAxis");

	ILTMath* pC = &m_common;
	pC->RotateAroundAxis(*pRotation, *pAxis, amount);
}

void CLTServerMath::EulerRotateX(LTRotation* pRotation, float amount)
{
	if (!pRotation)
		RETURN_ERROR_VOID("ILTCSBase::EulerRotateX");

	ILTMath* pC = &m_common;
	pC->EulerRotateX(*pRotation, amount);
}

void CLTServerMath::EulerRotateY(LTRotation* pRotation, float amount)
{
	if (!pRotation)
		RETURN_ERROR_VOID("ILTCSBase::EulerRotateY");

	ILTMath* pC = &m_common;
	pC->EulerRotateY(*pRotation, amount);
}

void CLTServerMath::EulerRotateZ(LTRotation* pRotation, float amount)
{
	if (!pRotation)
		RETURN_ERROR_VOID("ILTCSBase::EulerRotateZ");

	ILTMath* pC = &m_common;
	pC->EulerRotateZ(*pRotation, amount);
}

void CLTServerMath::AlignRotation(LTRotation* pRotation, LTVector* pVector, LTVector* pUp)
{
	// pUp is optional (not validated by the shipped code).
	if (!pRotation || !pVector)
		RETURN_ERROR_VOID("ILTCSBase::AlignRotation");

	rot_Align(pRotation, pVector, pUp);
}

LTRESULT CLTServerMath::SetupEuler(LTRotation* pRotation, float pitch, float yaw, float roll)
{
	if (!pRotation)
		RETURN_ERROR_PARAM("ILTCSBase::SetupEuler");

	ILTMath* pC = &m_common;
	return pC->SetupEuler(*pRotation, pitch, yaw, roll);
}

LTRESULT CLTServerMath::InterpolateRotation(LTRotation* pDest, LTRotation* pRot1, LTRotation* pRot2, float t)
{
	if (!pDest || !pRot1 || !pRot2)
		RETURN_ERROR_PARAM("ILTCSBase::InterpolateRotation");

	ILTMath* pC = &m_common;
	return pC->InterpolateRotation(*pDest, *pRot1, *pRot2, t);
}

void CLTServerMath::SetupTransformationMatrix(LTMatrix* pMat, LTVector* pTranslation, LTRotation* pRotation)
{
	if (!pMat || !pTranslation || !pRotation)
		RETURN_ERROR_VOID("ILTCSBase::SetupTransformationMatrix");

	ILTMath* pC = &m_common;
	pC->SetupTransformationMatrix(*pMat, *pTranslation, *pRotation);
}

void CLTServerMath::SetupTranslationMatrix(LTMatrix* pMat, LTVector* pTranslation)
{
	if (!pMat || !pTranslation)
		RETURN_ERROR_VOID("ILTCSBase::SetupTranslationMatrix");

	ILTMath* pC = &m_common;
	pC->SetupTranslationMatrix(*pMat, *pTranslation);
}

void CLTServerMath::SetupRotationMatrix(LTMatrix* pMat, LTRotation* pRot)
{
	if (!pMat || !pRot)
		RETURN_ERROR_VOID("ILTCSBase::SetupRotationMatrix");

	ILTMath* pC = &m_common;
	pC->SetupRotationMatrix(*pMat, *pRot);
}

void CLTServerMath::SetupTranslationFromMatrix(LTVector* pTranslation, LTMatrix* pMat)
{
	if (!pTranslation || !pMat)
		RETURN_ERROR_VOID("ILTCSBase::SetupTranslationFromMatrix");

	ILTMath* pC = &m_common;
	pC->SetupTranslationFromMatrix(*pTranslation, *pMat);
}

void CLTServerMath::SetupRotationFromMatrix(LTRotation* pRot, LTMatrix* pMat)
{
	if (!pRot || !pMat)
		RETURN_ERROR_VOID("ILTCSBase::SetupRotationFromMatrix");

	ILTMath* pC = &m_common;
	pC->SetupRotationFromMatrix(*pRot, *pMat);
}

void CLTServerMath::SetupRotationAroundPoint(LTMatrix* pMat, LTRotation* pRot, LTVector* pPoint)
{
	if (!pMat || !pRot || !pPoint)
		RETURN_ERROR_VOID("ILTCSBase::SetupRotationAroundPoint");

	ILTMath* pC = &m_common;
	pC->SetupRotationAroundPoint(*pMat, *pRot, *pPoint);
}


// ---- ILTMath: the implementation the wrappers above forward to ------- //
//
// The whole class is arithmetic with the checks stripped: every method here
// is the wrapper's callee, so it trusts what it is handed. Three of the
// matrix builders are one function between them — mat_SetupTransform takes a
// position, a rotation and a scale and skips whichever it is handed a null
// for, so a translation-only matrix is that call with two nulls.

// These are ILTMath's own methods, not a separate class's: ILTCSBase embeds
// an ILTMath at +4 and CLTServer's constructor (0x1002d385) stores ILTMath's
// vtable 0x1004e86c there, whose fifteen slots are exactly these bodies in
// iltmath.h's declaration order (0x10015ff1, 0x10016037, 0x1001600e, ...
// 0x100162e6).

LTRESULT ILTMath::GetRotationVectors(LTRotation& rot, LTVector& right,
                                     LTVector& up, LTVector& forward)
{
	quat_GetVectors(&rot, &right, &up, &forward);

	return LT_OK;
}

LTRESULT ILTMath::SetupEuler(LTRotation& rot, float pitch, float yaw,
                             float roll)
{
	rot_SetupEuler(pitch, yaw, roll, &rot);

	return LT_OK;
}

LTRESULT ILTMath::InterpolateRotation(LTRotation& dest, LTRotation& rot1,
                                      LTRotation& rot2, float t)
{
	quat_Slerp(&dest, &rot1, &rot2, t);

	return LT_OK;
}

LTRESULT ILTMath::SetupTransformationMatrix(LTMatrix& mat,
                                            LTVector& translation,
                                            LTRotation& rot)
{
	mat_SetupTransform(&translation, &rot, LTNULL, &mat);

	return LT_OK;
}

LTRESULT ILTMath::SetupTranslationMatrix(LTMatrix& mat,
                                         LTVector& translation)
{
	mat_SetupTransform(&translation, LTNULL, LTNULL, &mat);

	return LT_OK;
}

LTRESULT ILTMath::SetupRotationMatrix(LTMatrix& mat, LTRotation& rot)
{
	mat_SetupTransform(LTNULL, &rot, LTNULL, &mat);

	return LT_OK;
}

// The translation is the matrix's fourth column, read out one element at a
// time; only the first goes through the FPU, because only the first is
// stored where the compiler knows a float belongs.
LTRESULT ILTMath::SetupTranslationFromMatrix(LTVector& translation,
                                             LTMatrix& mat)
{
	translation.x = mat.m[0][3];
	translation.y = mat.m[1][3];
	translation.z = mat.m[2][3];

	return LT_OK;
}

LTRESULT ILTMath::SetupRotationFromMatrix(LTRotation& rot, LTMatrix& mat)
{
	quat_FromMatrix(&mat, &rot);

	return LT_OK;
}

// The three euler rotations are one shape: take the rotation's own axes, and
// turn about whichever of them the name says. Each hands its answer straight
// back from the rotate, so none of them has a return of its own.
LTRESULT ILTMath::EulerRotateX(LTRotation& rot, float amount)
{
	LTVector v[3];

	quat_GetVectors(&rot, &v[0], &v[1], &v[2]);

	return RotateAroundAxis(rot, v[0], amount);
}

LTRESULT ILTMath::EulerRotateY(LTRotation& rot, float amount)
{
	LTVector v[3];

	quat_GetVectors(&rot, &v[0], &v[1], &v[2]);

	return RotateAroundAxis(rot, v[1], amount);
}

LTRESULT ILTMath::EulerRotateZ(LTRotation& rot, float amount)
{
	LTVector v[3];

	quat_GetVectors(&rot, &v[0], &v[1], &v[2]);

	return RotateAroundAxis(rot, v[2], amount);
}

// Turning about an arbitrary axis goes through matrices rather than through
// the quaternion: the rotation becomes a matrix, the axis becomes another,
// and the product becomes a rotation again.
LTRESULT ILTMath::RotateAroundAxis(LTRotation& rot, LTVector& axis,
                                   float amount)
{
	LTMatrix mRot;
	LTMatrix mAxis;
	LTMatrix mOut;

	quat_ToMatrix(&rot, &mRot);

	mat_SetupRotationAboutAxis(&mAxis, axis, amount);

	mat_Mul(&mOut, &mAxis, &mRot);

	mRot = mOut;

	quat_FromMatrix(&mRot, &rot);

	return LT_OK;
}

// A direction and an up hint become three orthogonal axes, the axes become a
// matrix, and the matrix becomes the rotation.
void rot_Align(LTRotation* pRotation, LTVector* pVector, LTVector* pUp)
{
	LTVector vRight, vUp, vForward;
	LTMatrix mat;

	vec_AlignVectors(pVector, pUp, &vRight, &vUp, &vForward);

	mat.SetBasisVectors(&vRight, &vUp, &vForward);

	quat_FromMatrix(&mat, pRotation);
}

LTRESULT ILTMath::AlignRotation(LTRotation& rot, LTVector& vector,
                                LTVector& up)
{
	rot_Align(&rot, &vector, &up);
	return LT_OK;
}

LTRESULT ILTMath::GetRotationVectorsFromMatrix(LTMatrix& mat,
                                               LTVector& right,
                                               LTVector& up,
                                               LTVector& forward)
{
	mat.GetBasisVectors(&right, &up, &forward);

	return LT_OK;
}

// Rotating about a point is three matrices in a row: out to the point, the
// rotation, and back again. The middle product has to be copied out before
// it can be an argument to the second multiply, because the multiply writes
// its answer over one of the two it is reading.
LTRESULT ILTMath::SetupRotationAroundPoint(LTMatrix& mat, LTRotation& rot,
                                           LTVector& point)
{
	LTVector vBack;
	LTMatrix mOut;
	LTMatrix mBack;
	LTMatrix mRot;
	LTMatrix mA;
	LTMatrix mB;
	LTMatrix mResult;

	vBack = -point;

	SetupTranslationMatrix(mOut, point);
	SetupTranslationMatrix(mBack, vBack);
	SetupRotationMatrix(mRot, rot);

	mat_Mul(&mA, &mOut, &mRot);

	mB = mA;

	mat_Mul(&mResult, &mB, &mBack);

	mat = mResult;

	return LT_OK;
}


// ---- the math module's own helpers -------------------------------------- //
//
// These five sit in the engine's math block rather than beside the ILTMath methods
// (0x100114xx-0x100119xx against 0x10015ff1-0x100162e6), so they are not
// really this translation unit's; they are reconstructed here because this is
// where their declarations already were. Verification is per function with
// relocations masked, so which object they land in does not change their
// bytes.
//
// Four of the five are pure forwards, and the only thing to read off them is
// the argument order — which for quat_FromMatrix is *not* the order it was
// handed (see below).

// 0x10015bcd — sharedcommon.h's quaternion-to-matrix arithmetic, declared
// here because this file does not include that header.
void quat_ConvertToMatrix(LTRotation* pRot, LTMatrix* pMat);

// 0x100117e2 — the other direction, and the one servermath.h declared
// without a body. Both arguments straight through in their own order
// (`push 8(%esp)` twice), sixteen bytes, two `pop %ecx`.
void quat_ToMatrix(LTRotation* pRot, LTMatrix* pMat)
{
	quat_ConvertToMatrix(pRot->m_Quat, pMat->m);
}

// The wrapper pushes its first argument and then its second, so the callee
// receives them the other way round: the matrix comes in second. Sixteen
// bytes, two `pop %ecx` for the cleanup.
void quat_FromMatrix(LTMatrix* pMat, LTRotation* pRot)
{
	quat_ConvertFromMatrix(pRot->m_Quat, pMat->m);
}

// Four arguments straight through, so one batched `add $0x10,%esp`.
void quat_GetVectors(LTRotation* pRot, LTVector* pRight, LTVector* pUp,
                     LTVector* pForward)
{
	quat_GetVectors(pRot->m_Quat, &pRight->x, &pUp->x, &pForward->x);
}

// The same forward with a float last: `flds` the argument, `push %ecx` to
// grab four bytes for it, `fstps (%esp)`, then the three pointers.
void quat_Slerp(LTRotation* pDest, LTRotation* pRot1, LTRotation* pRot2,
                float t)
{
	quat_Slerp(pDest->m_Quat, pRot1->m_Quat, pRot2->m_Quat, t);
}

// Not a free function underneath at all: the axis rotation is LTMatrix's own
// SetupRot, reached as a thiscall on the destination matrix (`mov 0x10(%esp),
// %ecx` for `this`, the twelve-byte axis copied into a `sub $0xc,%esp` slot,
// the angle in the four bytes above it). The callee cleans its sixteen bytes,
// which is why the wrapper's own epilogue only pops the two saved registers.
void mat_SetupRotationAboutAxis(LTMatrix* pMat, LTVector vAxis, float amount)
{
	pMat->SetupRot(vAxis, amount);
}

// The one helper here with a body. The forward direction is the input
// normalised into the output; the up hint, if there is one, is normalised into
// a local first and the orthogonal up is computed from the pair; and the right
// vector falls out of the cross product. The two calls to sub_1001149a are
// one call in the shipped code — both arms push the last two arguments and
// jump to a shared `push` of the forward vector — which is VC6 cross-jumping
// the tails, not a conditional expression in the source.
//
// The frame is 0x18: twelve bytes for the normalised up hint and twelve for
// Cross's return temporary, which the assignment then copies out.
void vec_AlignVectors(LTVector* pVector, LTVector* pUp, LTVector* pRight,
                      LTVector* pUpOut, LTVector* pForward)
{
	LTVector vUp;

	*pForward = *pVector;
	pForward->Norm();

	if (pUp)
	{
		vUp = *pUp;
		vUp.Norm();

		sub_1001149a(pForward, &vUp, pUpOut);
	}
	else
	{
		sub_1001149a(pForward, LTNULL, pUpOut);
	}

	*pRight = pForward->Cross(*pUpOut);
}

// 0x1001149a — the one substantial function of the five: make an up vector
// that is orthogonal to a forward direction. The up hint may be null, in
// which case world up (0,1,0) stands in — three separate component stores of
// `fldz`/`fld1`/`fldz` rather than one Init, and the `lea` of the local is
// scheduled in between them.
//
// The hint is copied into the output and then tested against the forward
// direction: if the two are within 0.99 of parallel in either direction the
// hint is useless, so it is nudged and re-tested, up to three times over,
// each nudge different (z += 5, then x += 5 and y += 5, then x += 5, y -= 2
// and z -= 5) and each followed by a re-normalise. All four dots are
// compared against 0.99 and -0.99 with one `fcoms`/`fcomps` pair apiece — the
// value stays in st0 across both compares, so there is one dot per test and
// not two.
//
// Whatever survives that, the component of the up vector along the forward
// direction is subtracted off, which is what actually makes it orthogonal,
// and the result normalised.
//
// Two things the bytes say and no reading of the algorithm would:
//
//  - the frame is 0x18 and holds nothing the source named except the default
//    up. The other twelve bytes are the by-value argument slot `Dot` needs —
//    `Dot(_CVector<T> v)` takes its operand by value, so three of the four
//    dots copy twelve bytes into that slot first;
//  - the *second* dot has no copy at all: it multiplies the two vectors
//    straight out of memory, which is VEC_DOT rather than Dot. Three uses of
//    the member and one of the macro in one function is what shipped.
void sub_1001149a(LTVector* pForward, LTVector* pUp, LTVector* pUpOut)
{
	LTVector vDefUp;
	float    fDot;

	if (!pUp)
	{
		vDefUp.x = 0.0f;
		vDefUp.y = 1.0f;
		vDefUp.z = 0.0f;

		pUp = &vDefUp;
	}

	*pUpOut = *pUp;

	fDot = pForward->Dot(*pUp);

	if (fDot > 0.99f || fDot < -0.99f)
	{
		pUpOut->z += 5.0f;
		pUpOut->Norm();

		fDot = VEC_DOT(*pForward, *pUpOut);

		if (fDot > 0.99f || fDot < -0.99f)
		{
			pUpOut->x += 5.0f;
			pUpOut->y += 5.0f;
			pUpOut->Norm();

			fDot = pForward->Dot(*pUpOut);

			if (fDot > 0.99f || fDot < -0.99f)
			{
				pUpOut->x += 5.0f;
				pUpOut->y -= 2.0f;
				pUpOut->z -= 5.0f;
				pUpOut->Norm();
			}
		}
	}

	fDot = -pForward->Dot(*pUpOut);

	*pUpOut += *pForward * fDot;
	pUpOut->Norm();
}

// ---- the world-model transform pair (0x10011986) ----------------------- //
//
// PARKED HERE. Like the five helpers above, this one lives in the engine's own
// math module rather than beside the ILTMath methods — it sits at 0x10011986, between
// mat_SetupRotationAboutAxis (0x10011964) and mat_Mul (0x10011b1e) — and it is
// declared, not defined, in ltobject_internal.h. It is kept in this file
// because that module has no translation unit of its own and this is where the
// rest of the block's declarations already are.
//
// 0x10011986 — the pair of 4x4s a world model keeps at +0x1f0 and +0x230,
// rebuilt whenever the object moves. obj_UpdateTransform (0x1000d8e1) calls it
// with the world's own origin (pWorldBsp+0xe8), the object's m_Pos, its
// m_Rotation, and the two matrices in that order, which settles what
// ltobject_internal.h and objtypes.h both left open: +0x1f0 is the transform
// and +0x230 is its inverse.
//
// The whole body is three matrix expressions, and it has to be written that
// way. Six matrix-sized slots fill the 0x198 frame exactly, but they are not
// six named locals: a named local never shares a slot with another (probed —
// splitting the multiply's result and the inverse's destination into two
// locals gives a 0x1d8 frame and 414 bytes), while the compiler's own
// expression temporaries do. Writing the product into a named matrix and
// inverting out of it is size-exact and instruction-exact but rotates every
// frame displacement; writing it as
//
//     *pMat1 = mOut * mRot * mBack;
//     *pMat2 = ~*pMat1;
//
// gives the shipped bytes. `operator*` and `operator~` are ltmatrix.h's own
// inlines: each expands to a call to the out-of-line mat_Mul or
// Mat_InverseTransformation plus a whole-matrix copy of its return value, and
// the two operators of the chained multiply plus the inverse reuse one slot
// between them — which is the only way to reach the shipped layout. Twelve
// source shapes and three compiler revisions were tried before this one.
void wm_BuildTransforms(LTVector* pOffset, LTVector* pPos, LTRotation* pRot,
                        LTMatrix* pMat1, LTMatrix* pMat2)
{
	LTVector vDelta;
	LTMatrix mBack;
	LTMatrix mOut;
	LTMatrix mRot;

	vDelta = *pPos - *pOffset;

	// Out to the world's origin and back, the same "out, rotate, back" the
	// SetupRotationAroundPoint above is. The translation is spelled
	// `vDelta + *pOffset` rather than `*pPos`, which is what the two
	// separate `flds`/`fadds` pairs per component say.
	mBack.Init(1.0f, 0.0f, 0.0f, -pOffset->x,
	           0.0f, 1.0f, 0.0f, -pOffset->y,
	           0.0f, 0.0f, 1.0f, -pOffset->z,
	           0.0f, 0.0f, 0.0f, 1.0f);

	mOut.Init(1.0f, 0.0f, 0.0f, vDelta.x + pOffset->x,
	          0.0f, 1.0f, 0.0f, vDelta.y + pOffset->y,
	          0.0f, 0.0f, 1.0f, vDelta.z + pOffset->z,
	          0.0f, 0.0f, 0.0f, 1.0f);

	quat_ToMatrix(pRot, &mRot);

	*pMat1 = mOut * mRot * mBack;

	*pMat2 = ~*pMat1;
}


// *********************************************************************** //
//  The axis-basis table at 0x10058610 and the three helpers that read it.
//
//  Six records of three LTVectors (0x24 each), filled at startup by
//  0x100142a9 — which is a straight run of six `g_AxisBases[k] =
//  CAxisBasis(a, b, c);` statements, each building its three vectors in
//  locals and copying the constructor's temporary over the global with a
//  `rep movsl` of nine dwords. Reading the constants out of it gives the six
//  bases in the order +Y, -Y, +Z, -Z, +X, -X, each `(right, up, normal)`
//  with `right x up == normal`:
//
//     0x10058610  ( 1, 0, 0) ( 0, 0,-1) ( 0, 1, 0)
//     0x10058634  ( 1, 0, 0) ( 0, 0, 1) ( 0,-1, 0)
//     0x10058658  ( 1, 0, 0) ( 0, 1, 0) ( 0, 0, 1)
//     0x1005867c  ( 1, 0, 0) ( 0,-1, 0) ( 0, 0,-1)
//     0x100586a0  ( 0, 0, 1) ( 0,-1, 0) ( 1, 0, 0)
//     0x100586c4  ( 0, 0,-1) ( 0,-1, 0) (-1, 0, 0)
//
//  The table is in the uninitialised half of `.data` (the section's raw size
//  stops at 0x10057000), so the values are not in the file — they come off
//  the initialiser's `fldz`/`fld1`/`flds -1.0` sequence. Names ours; the
//  member names are what the three functions below use them for.
// *********************************************************************** //

//  The array's ELEMENT type is the plain record and the constructor lives on
//  a class derived from it: that is what makes the initializer build a
//  temporary and copy it (svlast.cpp, which defines the array, explains the
//  copy). Both TUs spell the two types identically.
// 0x1001450d — the constructor. It is a constructor and not an Init: the
// body returns `this` in %eax (`mov %ecx,%eax` on entry, `%eax` untouched at
// the `ret $0x24`), and the initialiser above relies on that return value as
// the source of the nine-dword copy into the global.
CAxisBasis::CAxisBasis(LTVector vRight, LTVector vUp, LTVector vNormal)
{
	m_vRight = vRight;
	m_vUp = vUp;
	m_vNormal = vNormal;
}

// 0x10014533 — which of the six bases points most nearly the way this
// direction does. Name ours.
//
// Read off the bytes:
//
//   * the running best starts at -2.0f (`movl $0xc0000000`), one less than
//     any unit dot product can be, so the first record always wins and the
//     answer is never the initial 0 by accident.
//   * the dot product is a FLAT three-term sum whose terms are written in
//     the order z, y, x — `LTVector::Dot`'s three terms exactly backwards.
//     The bytes emit them x, z, y, which is the flat sum's `c, a, b`
//     emission order (a fourteen-variant probe in this file
//     settled it, and only this one spelling matches). The emission order
//     alone is not the whole tell: it also fixes which component's address
//     VC6 uses as the strength-reduced induction variable, and the shipped
//     `-0x4/0x0/+0x4(%ecx)` triple says that base is `&m_vNormal.y`. The
//     accumulator spelling (`fDot = x*nx; fDot += z*nz; fDot += y*ny;`)
//     reaches the same x, z, y emission but bases the pointer on
//     `&m_vNormal.z` and is four bytes different for it.
//   * the loop counter is UNSIGNED. Its back edge is `jb`, not `jl`; an
//     `int i` gives the signed branch and is the one remaining byte.
//   * the index and the record address are separate registers because the
//     index is the return value; VC6 strength-reduced the subscript into the
//     `add $0x24` / `cmp $0x10058704` pointer walk on its own.
int math_FindClosestAxisBasis(LTVector vDir)
{
	int nBest = 0;
	float fBest = -2.0f;

	for (uint32 i = 0; i < 6; i++)
	{
		float fDot;

		fDot = vDir.z * g_AxisBases[i].m_vNormal.z +
		       vDir.y * g_AxisBases[i].m_vNormal.y +
		       vDir.x * g_AxisBases[i].m_vNormal.x;

		if (fDot > fBest)
		{
			fBest = fDot;
			nBest = i;
		}
	}

	return nBest;
}

// 0x1001457f — build a pair of vectors perpendicular to a direction, using
// the named basis's `up` as the seed. Name ours.
//
// Read off the bytes: the second Cross is called on *pOut1* (`%ecx` still
// holds the `0x10(%ebp)` it was loaded with for the store), so the shipped
// code reads back the vector it has just written rather than keeping the
// returned temporary — which is what writing the two statements against the
// out-parameters gives.
void math_GetAxisBasisVectors(uint32 iBasis, LTVector* pDir,
                              LTVector* pOut1, LTVector* pOut2)
{
	*pOut1 = pDir->Cross(g_AxisBases[iBasis].m_vUp);
	*pOut2 = pOut1->Cross(*pDir);
}



// *********************************************************************** //
//  Two SDK inlines the shipped image carries out of line.
//
//  0x10035468 is `quat_Mul` and 0x100354f0 is `LTMatrix::Apply3x3(const
//  LTVector&, LTVector&)`, both straight out of src/LT2/sdk/inc (ltquatbase.h
//  line 96 and ltmatrix.h line 745). Neither name is ours and neither body
//  is reconstructed: referencing them is enough, and VC6's out-of-line
//  COMDAT for each is byte-identical to the shipped bytes — 136 and 81,
//  term order, operand order and the `fsubp` polarities included. That also
//  independently confirms the whole quaternion product and the 3x3 apply,
//  down to the asymmetry in Apply3x3's first row (its three terms emit
//  m01*y, m02*z, m00*x where the other two rows emit in field order).
//
//  Their real call site is 0x10035397 / 0x10035429, inside the attachment
//  follower at 0x1003530b — a translation unit that has no reconstruction
//  yet. When it gets one, that file's own calls emit these two and this
//  forcing driver should be DELETED. Like packet.cpp's
//  packet_ForceInstantiation, the driver itself exists in no shipped binary
//  and is not counted as reconstruction.
// *********************************************************************** //
#include "ltquatbase.h"

void servermath_ForceSDKInlines(LTRotation* pOut, LTRotation* pA, LTRotation* pB,
                                LTMatrix* pMat, LTVector* pIn, LTVector* pDest)
{
	quat_Mul((float*)pOut, (float*)pA, (float*)pB);

	pMat->Apply3x3(*pIn, *pDest);
}


SAxisBasis g_AxisBases[6] =
{
	CAxisBasis(LTVector(1.0f, 0.0f,  0.0f),
	           LTVector(0.0f, 0.0f, -1.0f),
	           LTVector(0.0f, 1.0f,  0.0f)),

	CAxisBasis(LTVector(1.0f,  0.0f, 0.0f),
	           LTVector(0.0f,  0.0f, 1.0f),
	           LTVector(0.0f, -1.0f, 0.0f)),

	CAxisBasis(LTVector(1.0f, 0.0f, 0.0f),
	           LTVector(0.0f, 1.0f, 0.0f),
	           LTVector(0.0f, 0.0f, 1.0f)),

	CAxisBasis(LTVector(1.0f,  0.0f,  0.0f),
	           LTVector(0.0f, -1.0f,  0.0f),
	           LTVector(0.0f,  0.0f, -1.0f)),

	CAxisBasis(LTVector(0.0f,  0.0f, 1.0f),
	           LTVector(0.0f, -1.0f, 0.0f),
	           LTVector(1.0f,  0.0f, 0.0f)),

	CAxisBasis(LTVector( 0.0f,  0.0f, -1.0f),
	           LTVector( 0.0f, -1.0f,  0.0f),
	           LTVector(-1.0f,  0.0f,  0.0f))
};

void mat_Mul(LTMatrix* out, LTMatrix* a, LTMatrix* b) { MatMul(out, a, b); }

void mat_SetupTransform(LTVector* pPos, LTRotation* pRot, LTVector* pScale,
                        LTMatrix* pMat)
{
	if (pRot)
		quat_ToMatrix(pRot, pMat);
	else
		pMat->Identity();

	if (pScale)
	{
		pMat->m[0][0] *= pScale->x;
		pMat->m[1][0] *= pScale->x;
		pMat->m[2][0] *= pScale->x;

		pMat->m[0][1] *= pScale->y;
		pMat->m[1][1] *= pScale->y;
		pMat->m[2][1] *= pScale->y;

		pMat->m[0][2] *= pScale->z;
		pMat->m[1][2] *= pScale->z;
		pMat->m[2][2] *= pScale->z;
	}

	if (pPos)
	{
		pMat->m[0][3] = pPos->x;
		pMat->m[1][3] = pPos->y;
		pMat->m[2][3] = pPos->z;
	}
}

// Typed native adapters for the SDK matrix arithmetic.
void quat_ConvertToMatrix(LTRotation* rotation, LTMatrix* matrix) {
    quat_ConvertToMatrix(rotation->m_Quat,matrix->m);
}
void mat_Multiply(LTMatrix* out, LTMatrix* a, LTMatrix* b) { MatMul(out,a,b); }
