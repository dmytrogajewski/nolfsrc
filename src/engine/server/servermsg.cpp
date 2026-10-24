// Windows-compatible position, vector, and rotation compression.
#include "message.h"
#include "servermath.h"
#include <cmath>
void lmsg_CompressPos(uint8* pOut, LTVector* pVec, CPosRef* pRef)
{
	uint32 nX = (uint32)(int)((pVec->x - pRef->m_vMin.x) * pRef->m_vScale.x * 65536.0f);
	uint32 nY = (uint32)(int)((pVec->y - pRef->m_vMin.y) * pRef->m_vScale.y * 65536.0f);
	uint32 nZ = (uint32)(int)((pVec->z - pRef->m_vMin.z) * pRef->m_vScale.z * 65536.0f);

	nolf::wire::store(pOut, uint16(nX));
	nolf::wire::store(pOut + 2, uint16(nY));
	nolf::wire::store(pOut + 4, uint16(nZ));

	pOut[6] = (uint8)((nZ >> 16) | (nY >> 16) | (nX >> 16));
}


inline void sub_100131c1(uint8* pData, LTVector* pUp, LTVector* pForward)
{
		float    fScale;

	pForward->x = ((int8)pData[1] * (1.0f / 127.0f) - 0.5f) * 2.0f;
	pForward->y = ((int8)pData[0] * (1.0f / 127.0f) - 0.5f) * 2.0f;
	pForward->z = (int8)pData[2] * (1.0f / 127.0f);

	pUp->Init(pForward->x, pForward->y + 50.0f, pForward->z);

fScale = pForward->Dot(*pUp);

	fScale = -(fScale / pForward->MagSqr());

	*pUp += *pForward * fScale;
}


void sub_1001307f(LTRotation* pRot, uint8* pOut)
{
	LTMatrix mat;
	LTVector vOther;
	LTVector vUp;
	LTVector vCheck;
	float    fMagSqr;
	float    fInv;
	float    fDot;

	quat_ToMatrix(pRot, &mat);

	vUp.x = mat.m[0][1];
	vUp.y = mat.m[1][1];
	vUp.z = mat.m[2][1];

	pOut[0] = (uint8)(int)((mat.m[1][2] + 1.0f) * 63.9f);
	pOut[1] = (uint8)(int)((mat.m[0][2] + 1.0f) * 63.9f);
	pOut[2] = (uint8)(int)(mat.m[2][2] * 127.9f);
	pOut[3] = (uint8)(int)(mat.m[0][1] * 127.9f);
	pOut[4] = (uint8)(int)(mat.m[1][1] * 127.9f);
	pOut[5] = (uint8)(int)(mat.m[2][1] * 127.9f);

	sub_100131c1(pOut, &vCheck, &vOther);

	fMagSqr = vCheck.x * vCheck.x + vCheck.y * vCheck.y + vCheck.z * vCheck.z;

	if (fMagSqr > 0.1f)
	{
		fInv = 1.0f / (float)sqrt(fMagSqr);

		vCheck.x *= fInv;
		vCheck.y *= fInv;
		vCheck.z *= fInv;

		fDot = vCheck.z * vUp.z + vCheck.y * vUp.y + vCheck.x * vUp.x;

		if (fDot <= 0.999f)
		{
			if (fDot >= -0.999f)
				return;

			pOut[1] = (uint8)(pOut[1] == 0 ? -1 : -(int8)pOut[1]);
		}

		pOut[0] = (uint8)(pOut[0] == 0 ? -1 : -(int8)pOut[0]);
	}
}


void lmsg_WriteCompRotationBytes(IMessageWrite* pMsg, uint8* pData)
{
	pMsg->WriteByte(pData[0]);
	pMsg->WriteByte(pData[1]);
	pMsg->WriteByte(pData[2]);

	if ((int8)pData[0] >= 0)
	{
		pMsg->WriteByte(pData[3]);
		pMsg->WriteByte(pData[4]);
		pMsg->WriteByte(pData[5]);
	}
}


void lmsg_WriteCompPosWords(IMessageWrite* pMsg, uint8* pData)
{
	pMsg->WriteWord(nolf::wire::load<uint16>(pData + 0));
	pMsg->WriteWord(nolf::wire::load<uint16>(pData + 2));
	pMsg->WriteWord(nolf::wire::load<uint16>(pData + 4));
}


void lmsg_WriteCompPos(LMessageImpl* pMsg, LTVector* pVec, CPosRef* pRef)
{
	uint8 data[7];

	lmsg_CompressPos(data, pVec, pRef);

	lmsg_WriteCompPosWords((IMessageWrite*)pMsg, data);
}


void lmsg_WriteCompRotation(LMessageImpl* pMsg, LTRotation* pRot)
{
	uint8 data[6];

	sub_1001307f(pRot, data);

	lmsg_WriteCompRotationBytes((IMessageWrite*)pMsg, data);
}


inline void sub_10013382(uint8* pData, LTRotation* pRot)
{
	LTMatrix mat;
	LTVector vRight;
	LTVector vForward;
	LTVector vUp;
	LTBOOL   bFlip;

	if ((int8)pData[0] < 0)
	{
		pData[0] = -pData[0];

		bFlip = LTFALSE;

		if ((int8)pData[1] < 0)
		{
			pData[1] = -pData[1];

			bFlip = LTTRUE;
		}

		sub_100131c1(pData, &vUp, &vForward);

		vUp.Norm();
		vForward.Norm();

		if (bFlip)
			vUp = -vUp;
	}
	else
	{
		vForward.y = ((int8)pData[0] * (1.0f / 127.0f) - 0.5f) * 2.0f;
		vForward.x = ((int8)pData[1] * (1.0f / 127.0f) - 0.5f) * 2.0f;
		vForward.z = (int8)pData[2] * (1.0f / 127.0f);

		vUp.x = (int8)pData[3] * (1.0f / 127.0f);
		vUp.y = (int8)pData[4] * (1.0f / 127.0f);
		vUp.z = (int8)pData[5] * (1.0f / 127.0f);
	}

	vRight   = vForward.Cross(vUp);
	vForward = vUp.Cross(vRight);

	vRight.Norm();
	vForward.Norm();

	vUp = vRight.Cross(vForward);

	mat.SetBasisVectors(&vRight, &vUp, &vForward);

	quat_FromMatrix(&mat, pRot);
}


void lmsg_ReadCompRotation(LMessageImpl* pMsg, LTRotation* pRot)
{
	int8 data[6]{};

	{ uint8 b; ((IMessageRead*)pMsg)->ReadByte(&b); data[0] = b; }
	{ uint8 b; ((IMessageRead*)pMsg)->ReadByte(&b); data[1] = b; }
	{ uint8 b; ((IMessageRead*)pMsg)->ReadByte(&b); data[2] = b; }

	if (data[0] >= 0)
	{
		{ uint8 b; ((IMessageRead*)pMsg)->ReadByte(&b); data[3] = b; }
		{ uint8 b; ((IMessageRead*)pMsg)->ReadByte(&b); data[4] = b; }
		{ uint8 b; ((IMessageRead*)pMsg)->ReadByte(&b); data[5] = b; }
	}

	sub_10013382((uint8*)data, pRot);
}


inline float lmsg_Get(const float* p) { return *p; }


void lmsg_ReadCompPos(LMessageImpl* pMsg, LTVector* pVec, CPosRef* pRef)
{
	uint32   n[3];
	LTVector vFrac;

	if (!pRef)
	{
		if (pVec)
		{
			pVec->x = 0.0f;
			pVec->y = 0.0f;
			pVec->z = 0.0f;
		}

		return;
	}

	{ uint16 w; ((IMessageRead*)pMsg)->ReadWord(&w); n[0] = w; }
	{ uint16 w; ((IMessageRead*)pMsg)->ReadWord(&w); n[1] = w; }
	{ uint16 w; ((IMessageRead*)pMsg)->ReadWord(&w); n[2] = w; }

	vFrac.x = (float)n[0] * (1.0f / 65536.0f);
	vFrac.y = (float)n[1] * (1.0f / 65536.0f);
	vFrac.z = (float)n[2] * (1.0f / 65536.0f);

	pVec->x = lmsg_Get(&pRef->m_vMin.x) + (lmsg_Get(&pRef->m_vMax.x) - lmsg_Get(&pRef->m_vMin.x)) * vFrac.x;
	pVec->y = lmsg_Get(&pRef->m_vMin.y) + (lmsg_Get(&pRef->m_vMax.y) - lmsg_Get(&pRef->m_vMin.y)) * vFrac.y;
	pVec->z = lmsg_Get(&pRef->m_vMin.z) + (lmsg_Get(&pRef->m_vMax.z) - lmsg_Get(&pRef->m_vMin.z)) * vFrac.z;
}


#define LMSG_SIGN(val)  (((val) < 0.0f) ? -1 : 1)

void lmsg_WriteCompVector(LMessageImpl* pMsg, LTVector* pVec)
{
	float  fMax;
	float  f1, f2, f3;
	uint32 n2, n3;
	float  d;
	uint8  nFlags;

	fMax   = (float)fabs(pVec->x);
	nFlags = 0;
	f1     = pVec->x;
	f2     = pVec->y;
	f3     = pVec->z;

	d = fabs(pVec->y);

	if (d > fMax)
	{
		nFlags = 0x40;
		fMax   = (float)d;
		f1     = pVec->y;
		f2     = pVec->x;
	}

	d = fabs(pVec->z);

	if (d > fMax)
	{
		nFlags = 0x80;
		fMax   = (float)d;
		f1     = pVec->z;
		f2     = pVec->x;
		f3     = pVec->y;
	}

	n2 = fMax == 0 ? 0 : (uint32)(fabs(f2) / fMax * 262143.0);

	nFlags |= ((n2 >> 16) & 3) << 3;

	if (LMSG_SIGN(f2) != LMSG_SIGN(f1))
		nFlags |= 0x20;

	n3 = fMax == 0 ? 0 : (uint32)(fabs(f3) / fMax * 262143.0);

	nFlags |= (n3 >> 16) & 3;

	if (LMSG_SIGN(f3) != LMSG_SIGN(f1))
		nFlags |= 0x04;

	((IMessageWrite*)pMsg)->WriteFloat(f1);
	((IMessageWrite*)pMsg)->WriteWord(n2);
	((IMessageWrite*)pMsg)->WriteWord(n3);
	((IMessageWrite*)pMsg)->WriteByte(nFlags);
}



// Yaw encoding used by the server update stream.
void sub_10013585(ServerMessage* msg, LTRotation* rotation) {
    LTMatrix matrix;
    quat_ToMatrix(rotation, &matrix);
    LTVector right, up, forward;
    matrix.GetBasisVectors(&right, &up, &forward);
    msg->WriteByte(uint8(int(std::atan2(forward.x, forward.z) * (127.0f / MATH_PI))));
}
