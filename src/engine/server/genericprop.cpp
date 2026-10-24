// server.dll — the GenericProp fillers (reconstruction). Build /O1.
//
// ILTServer::GetPropGeneric answers with whatever type the property happens
// to be, and these are what know the mapping: each one clears the record and
// then fills every field its type can reasonably produce, so a caller can
// read a string property as a vector, or a real as a bool, without asking
// what the world file actually said.
//
// The record itself needs no reconstruction — ltbasedefs.h defines it, and
// the clear below confirms it: the shipped code zeroes offsets 0/4/8 and
// 0x11c-0x128, which is m_Vec and m_Rotation exactly where the header puts
// them. The file name is ours.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "ltbasedefs.h"
#include "ltmatrix.h"
#include "engine/portable/string.h"
#include <algorithm>
#include <cmath>
#include <limits>

// 0x100117ae and 0x100117f2 — a 4x4 from three euler angles, and a matrix's
// rotation as a quaternion. Both are servermath.h's; declared here because
// this file does not include it (names ours for the first, servermath.h's for
// the second).
void mat_SetupEuler(LTMatrix* pMat, float x, float y, float z);
void quat_FromMatrix(LTMatrix* pMat, LTRotation* pRot);

// 0x100116ad — the matrix builder proper, which takes the angles by value the
// same way the rotation one does; the out-parameter is again LAST (name ours).
//
// The six sines and cosines are taken in the order y, x, z — COS before SIN
// in each pair — and each angle is widened to double once and reloaded for
// the second call (`fstl` into a scratch, `fldl` back), because sin and cos
// are the double CRT entry points at 0x1003f924 and 0x1003f5f4.
//
// SETTLED, and the reconstruction was once wrong. The
// question was which of the two CRT entry points is sin and which is cos,
// because a masked `call rel32` cannot tell them apart and an earlier
// comment had asserted the identification backwards. The image answers it
// twice over, and neither answer is an inference:
//
//   * 0x1003f5f4 executes `fcos` (0x1003f612 and again at 0x1003f649 on the
//     argument-reduction path) and its own domain-error arm loads the string
//     "cos" (0x10055c50) with _matherr code 0x12. 0x1003f924 executes `fsin`
//     (0x1003f942, 0x1003f979) and loads "sin" (0x10055c60) with code 0x1e.
//     The function's own name is inside the function.
//   * 0x100116ad calls 0x1003f5f4 first in every one of its three pairs
//     (0x100116be, 0x100116dd, 0x100116fc). So the shipped source takes the
//     COSINE first.
//
// The old source took the sine first, so every slot in the body held the
// other member of its pair and the reconstruction computed a DIFFERENT
// matrix from the shipped code while matching its bytes — the third of the
// three possibilities the previous comment listed, and the one it could not
// choose. The "rows are unit length only under sin=0x1003f5f4" argument that
// had settled it the wrong way is simply not an argument: swapping sin for
// cos throughout maps each angle to pi/2 minus itself, which is still a
// rotation, so BOTH readings give an orthonormal matrix and the test cannot
// separate them.
//
// The fix is a pure renaming — each variable trades places with its partner
// and every reference in the body follows — so the calls now go cos, sin per
// pair and the six results land in the same slots as before. It compiles to
// the same 257 bytes, still byte-identical, and our object's relocations now
// name `_cos` as the first call of each pair where they used to name `_sin`.
// The lesson: a masked relocation can hide a semantic error
// that no amount of byte-matching will surface, and the way out is to read
// the CALLEE — here its own `fcos`/`fsin` opcode and its own error string.
//
// sin(z) never reaches memory — it lives in %st(0) for the whole body — and
// `sz * sx` and `cz * sx` are each computed once and reused by two elements,
// which is what fixes the multiplication grouping below: written any other
// way the products do not common. The angle vector's z slot is recycled for
// cos(z) and the pMat parameter's slot for `cz * sx`, both dead by then.
//
// The last row is one chained assignment, which VC6 stores right to left with
// a fresh `fldz` each time (0x38, 0x34, 0x30) — the same idiom
// quat_ConvertToMatrix ends with. The per-row zeros above it are separate
// statements and land in row order.
//
// The two subtractions had to be written the way they are below, and getting
// them backwards is what the FPU stack traffic exposes. `de e9` is Intel
// FSUBP ST(1),ST — ST(1) = ST(1) - ST(0) — so the operand computed FIRST is
// the minuend, which means the shipped source's left operand is the product
// carrying the common subexpression. Written with the terms the other way
// round the two elements come out negated AND cost seven bytes more: the
// common product is stored-and-popped and then reloaded instead of being
// kept in %st(0) for its own element (`fstps`+`flds` where the shipped code
// has one `fsts`).
void mat_SetupEuler(LTVector vAngles, LTMatrix* pMat)
{
	float cy = (float)cos(vAngles.y);
	float sy = (float)sin(vAngles.y);
	float cx = (float)cos(vAngles.x);
	float sx = (float)sin(vAngles.x);
	float cz = (float)cos(vAngles.z);
	float sz = (float)sin(vAngles.z);

	pMat->m[0][0] = sz * sx * sy + cz * cy;
	pMat->m[0][1] = cz * sx * sy - sz * cy;
	pMat->m[0][2] = cx * sy;
	pMat->m[0][3] = 0.0f;

	pMat->m[1][0] = sz * cx;
	pMat->m[1][1] = cz * cx;
	pMat->m[1][2] = -sx;
	pMat->m[1][3] = 0.0f;

	pMat->m[2][0] = sz * sx * cy - cz * sy;
	pMat->m[2][1] = cz * sx * cy + sz * sy;
	pMat->m[2][2] = cx * cy;
	pMat->m[2][3] = 0.0f;

	pMat->m[3][0] = pMat->m[3][1] = pMat->m[3][2] = 0.0f;
	pMat->m[3][3] = 1.0f;
}

// 0x100117ae — and its loose-float front end: pack and forward, byte for byte
// the same shape as rot_SetupEuler below. Same evidence too — the components
// reach the vector as plain integer moves, so it is a component assignment
// and not LTVector::Init.
void mat_SetupEuler(LTMatrix* pMat, float x, float y, float z)
{
	LTVector vAngles;

	vAngles.x = x;
	vAngles.y = y;
	vAngles.z = z;

	mat_SetupEuler(vAngles, pMat);
}

// 0x10011838 — build a rotation from three euler angles already packed into
// a vector: make a 4x4 out of them and read the quaternion back out of it.
// The vector is taken BY VALUE, which is what the twelve-byte `movsl` copy at
// every call site says (name ours).
//
// The three components are forwarded as loose floats through the /O1 float
// idiom — `push %ecx` to reserve four bytes and `fstps (%esp)` to fill it —
// and both cleanups are batched into one `add $0x18,%esp`, so the matrix
// builder's four arguments and the quaternion extractor's two are popped
// together.
void rot_SetupEuler(LTVector vAngles, LTRotation* pRot)
{
	LTMatrix mat;

	mat_SetupEuler(&mat, vAngles.x, vAngles.y, vAngles.z);

	quat_FromMatrix(&mat, pRot);
}

// 0x1001186d — the same from three loose angles: pack and forward. The
// rotation is the last argument, not the first (name ours).
//
// It belongs to the servermath run (0x100117ae-0x100118d5) rather than to
// this file's own; it is defined here rather than in servermath.cpp,
// and the address it matches at is unaffected.
//
// The three angles reach the vector as plain integer moves — a component
// assignment, not LTVector::Init, which would route each one through the FPU.
void rot_SetupEuler(float x, float y, float z, LTRotation* pRot)
{
	LTVector vAngles;

	vAngles.x = x;
	vAngles.y = y;
	vAngles.z = z;

	rot_SetupEuler(vAngles, pRot);
}

// 0x100118a1 — the three basis vectors three euler angles stand for, and the
// last function of the 0x100117ae-0x100118d5 run this file already carries
// the rest of. commonmodel.cpp declares it; nothing defined it.
//
// The whole body is two calls and a 0x40-byte local. Read off the bytes:
//
//  * the frame is `sub $0x40,%esp` and nothing else -- one LTMatrix, no
//    other local. The matrix's address is pushed FIRST and the angles are
//    then copied into twelve bytes made with `sub $0xc,%esp`, so on the
//    stack the vector is BELOW the pointer: `mat_SetupEuler(vAngles,
//    &mMat)`, the by-value overload this file already defines above, and
//    not the loose-float front end.
//  * `add $0x10,%esp` cleans 4 + 12 in one go.
//  * the second call is a THISCALL on the matrix (`lea -0x40(%ebp),%ecx`)
//    with the three out-parameters pushed 0x1c, 0x18, 0x14 -- right to
//    left, so right/up/forward in the header's own order. It is a CALL and
//    not an expansion even though ltmatrix.h marks GetBasisVectors inline:
//    nine float stores is past what /Os will take.
void mat_GetEulerVectors(LTVector vAngles, LTVector* pRight, LTVector* pUp,
                         LTVector* pForward)
{
	LTMatrix mMat;

	mat_SetupEuler(vAngles, &mMat);

	mMat.GetBasisVectors(pRight, pUp, pForward);
}


// 0x10011172 — everything except m_Color, which only the vector setter ever
// writes.
//
// The float store sits between m_Long and m_Bool. server.dll's /O1 build
// (0x10011172) matches with it anywhere, since its x87 zero is scheduled
// first regardless; lithtech.exe's /O2 twin (0x00433610) turns it into an
// integer store and keeps it where the source puts it -- after the string
// byte and m_Long (+0x12c), before m_Bool (+0x134).
void gp_Clear(GenericProp* pProp)
{
	pProp->m_Vec.Init();
	pProp->m_Color.Init();
	pProp->m_Rotation.Init();
	pProp->m_String[0] = 0;
	pProp->m_Long = 0;
	pProp->m_Float = 0.0f;
	pProp->m_Bool = 0;
}

// 0x1001122d — the numeric fields and their text, without clearing first;
// the string setter reaches this one directly once it has a value.
void gp_SetFloatValue(GenericProp* pProp, float fValue)
{
	if (!std::isfinite(fValue)) fValue = 0;
	snprintf(pProp->m_String, sizeof(pProp->m_String), "%f", fValue);

	pProp->m_Float = fValue;
	const auto integer = std::clamp(static_cast<double>(fValue),
	    static_cast<double>(std::numeric_limits<int32>::min()), static_cast<double>(std::numeric_limits<int32>::max()));
	pProp->m_Long = static_cast<int32>(integer);
	pProp->m_Bool = static_cast<int32>(integer);
}

// One setter call per arm, cross-jumped by the compiler into a single call:
// that is why shipped 0x100111b7 reuses atof's argument slot as the float
// slot and pops the setter's arguments on their own. A float local set on
// each arm and passed once cleans atof's slot, reserves a fresh one and
// batches the setter's cleanup with strncpy's (117 bytes).
//
// 0x100111b7 — a string property fills everything it can be read as: three
// floats if it looks like a vector, and a number if it looks like one, with
// "true" and "false" spelled out.
void gp_SetString(GenericProp* pProp, char* pStr)
{
	gp_Clear(pProp);

	sscanf(pStr, "%f %f %f", &pProp->m_Vec.x, &pProp->m_Vec.y, &pProp->m_Vec.z);
	for (int axis = 0; axis < 3; ++axis) if (!std::isfinite(pProp->m_Vec[axis])) pProp->m_Vec[axis] = 0;

	if (nolf::ascii_compare(pStr, "true") == 0)
		gp_SetFloatValue(pProp, 1.0f);
	else if (nolf::ascii_compare(pStr, "false") == 0)
		gp_SetFloatValue(pProp, 0.0f);
	else
		gp_SetFloatValue(pProp, (float)atof(pStr));

	strncpy(pProp->m_String, pStr, MAX_GP_STRING_LEN);
	pProp->m_String[MAX_GP_STRING_LEN] = 0;
}

// 0x1001126d — a vector fills both vector fields and its own text.
void gp_SetVector(GenericProp* pProp, LTVector* pVec)
{
	gp_Clear(pProp);

	snprintf(pProp->m_String, sizeof(pProp->m_String), "%f %f %f", pVec->x, pVec->y, pVec->z);

	pProp->m_Vec = *pVec;
	pProp->m_Color = *pVec;
}

// 0x100112ba — the clearing form of the float setter.
void gp_SetFloat(GenericProp* pProp, float fValue)
{
	gp_Clear(pProp);
	gp_SetFloatValue(pProp, fValue);
}

// 0x100112d6 — a rotation arrives as euler angles, and the quaternion is
// built from the copy just stored rather than from the caller's vector.
void gp_SetRotation(GenericProp* pProp, LTVector* pAngles)
{
	gp_Clear(pProp);

	pProp->m_Vec = *pAngles;

	rot_SetupEuler(pProp->m_Vec.x, pProp->m_Vec.y, pProp->m_Vec.z,
	               &pProp->m_Rotation);
}
