// *********************************************************************** //
//
//  MODULE:   CSBASE (.CPP)
//
//  PURPOSE:  The forty-four methods ILTCSBase implements itself.
//
//  iltcsbase.h declares most of the interface pure, but not these: the
//  message accessors and the rotation maths are declared without `= 0` and
//  defined here, in the engine, which is why every one of them reports
//  itself as "ILTCSBase::Something" rather than under a concrete class's
//  name. They are also all obsolete - the header says so twice - and every
//  one of them is a null check in front of a single forwarded call.
//
//  Two things run right through the set:
//
//    * The forwarding target follows the name mechanically. The message
//      accessors call the ILTMessage slot of the same name; the rotation
//      calls go through GetMathLT(), which is `&m_MathLT` - the member the
//      header puts immediately after the vptr, which is why every one of
//      them adds four to `this`.
//
//    * Which error macro a method uses follows from its return type, with
//      no exceptions in forty-four. LTRESULT methods can return the code,
//      so they use the ordinary RETURN_ERROR and report it as "60"; void
//      and value-returning methods cannot, and those report the code by
//      name and pass the engine's message buffer as the description.
//
//  Everything here is compiled into both server.dll and lithtech.exe.
//
// *********************************************************************** //

#include "servermath.h"
#include "iltcsbase.h"
#include "iltmessage.h"
#include "de_error.h"


// ---- writing ------------------------------------------------------------ //

LTRESULT ILTCSBase::WriteToMessageFloat(HMESSAGEWRITE hMessage, float val)
{
	if (!hMessage)
		RETURN_ERROR(2, "ILTCSBase::WriteToMessageFloat", LT_INVALIDPARAMS);

	return hMessage->WriteFloat(val);
}

LTRESULT ILTCSBase::WriteToMessageByte(HMESSAGEWRITE hMessage, uint8 val)
{
	if (!hMessage)
		RETURN_ERROR(2, "ILTCSBase::WriteToMessageByte", LT_INVALIDPARAMS);

	return hMessage->WriteByte(val);
}

LTRESULT ILTCSBase::WriteToMessageWord(HMESSAGEWRITE hMessage, uint16 val)
{
	if (!hMessage)
		RETURN_ERROR(2, "ILTCSBase::WriteToMessageWord", LT_INVALIDPARAMS);

	return hMessage->WriteWord(val);
}

LTRESULT ILTCSBase::WriteToMessageDWord(HMESSAGEWRITE hMessage, uint32 val)
{
	if (!hMessage)
		RETURN_ERROR(2, "ILTCSBase::WriteToMessageDWord", LT_INVALIDPARAMS);

	return hMessage->WriteDWord(val);
}

LTRESULT ILTCSBase::WriteToMessageString(HMESSAGEWRITE hMessage, char* pStr)
{
	if (!hMessage)
		RETURN_ERROR(2, "ILTCSBase::WriteToMessageString", LT_INVALIDPARAMS);

	return hMessage->WriteString(pStr);
}

LTRESULT ILTCSBase::WriteToMessageVector(HMESSAGEWRITE hMessage, LTVector* pVal)
{
	if (!hMessage || !pVal)
		RETURN_ERROR(2, "ILTCSBase::WriteToMessageVector", LT_INVALIDPARAMS);

	return hMessage->WriteVector(*pVal);
}

LTRESULT ILTCSBase::WriteToMessageCompVector(HMESSAGEWRITE hMessage, LTVector* pVal)
{
	if (!hMessage || !pVal)
		RETURN_ERROR(2, "ILTCSBase::WriteToMessageCompVector", LT_INVALIDPARAMS);

	return hMessage->WriteCompVector(*pVal);
}

LTRESULT ILTCSBase::WriteToMessageCompPosition(HMESSAGEWRITE hMessage, LTVector* pVal)
{
	if (!hMessage || !pVal)
		RETURN_ERROR(2, "ILTCSBase::WriteToMessageCompPosition", LT_INVALIDPARAMS);

	return hMessage->WriteCompPos(*pVal);
}

LTRESULT ILTCSBase::WriteToMessageRotation(HMESSAGEWRITE hMessage, LTRotation* pVal)
{
	if (!hMessage || !pVal)
		RETURN_ERROR(2, "ILTCSBase::WriteToMessageRotation", LT_INVALIDPARAMS);

	return hMessage->WriteRotation(*pVal);
}

LTRESULT ILTCSBase::WriteToMessageHString(HMESSAGEWRITE hMessage, HSTRING hString)
{
	if (!hMessage)
		RETURN_ERROR(2, "ILTCSBase::WriteToMessageHString", LT_INVALIDPARAMS);

	return hMessage->WriteHString(hString);
}

// Both of these write a whole message into another one, and both reach the
// same slot: a data message is an ILTMessage whichever way it was opened.
LTRESULT ILTCSBase::WriteToMessageHMessageWrite(HMESSAGEWRITE hMessage, HMESSAGEWRITE hDataMessage)
{
	if (!hMessage)
		RETURN_ERROR(2, "ILTCSBase::WriteToMessageHMessageWrite", LT_INVALIDPARAMS);

	return hDataMessage ? hMessage->WriteMessage(*hDataMessage) : hMessage->WriteWord(0xffff);
}

LTRESULT ILTCSBase::WriteToMessageHMessageRead(HMESSAGEWRITE hMessage, HMESSAGEREAD hDataMessage)
{
	if (!hMessage)
		RETURN_ERROR(2, "ILTCSBase::WriteToMessageHMessageRead", LT_INVALIDPARAMS);

	// A player may have no client save payload (e.g. a server autosave).
	// Nested messages reserve 0xffff for this absent-message value.
	return hDataMessage ? hMessage->WriteMessage(*hDataMessage) : hMessage->WriteWord(0xffff);
}

// The one vararg in the set, so the one that is not a thiscall: `this` comes
// in on the stack and the function cleans nothing up. Its answer is LT_OK
// whatever the write said.
LTRESULT ILTCSBase::WriteToMessageFormattedHString(HMESSAGEWRITE hMessage, int messageCode, ...)
{
	va_list marker;

	if (!hMessage)
		RETURN_ERROR(2, "ILTCSBase::WriteToMessageFormattedHString", LT_INVALIDPARAMS);

	va_start(marker, messageCode);
	const LTRESULT result = hMessage->WriteHStringArgList(messageCode, &marker);
	va_end(marker);
	return result;
}

LTRESULT ILTCSBase::WriteToMessageObject(HMESSAGEWRITE hMessage, HOBJECT hObj)
{
	if (!hMessage)
		RETURN_ERROR(2, "ILTCSBase::WriteToMessageObject", LT_INVALIDPARAMS);

	return hMessage->WriteObject(hObj);
}

LTRESULT ILTCSBase::WriteToLoadSaveMessageObject(HMESSAGEWRITE message, HOBJECT object)
{
    return WriteToMessageObject(message, object);
}


// ---- reading ------------------------------------------------------------ //

// The readers hand the message a local to fill in and then return it. VC6
// puts that local in the slot the message-handle argument occupies, which is
// why none of them reserves any stack.
float ILTCSBase::ReadFromMessageFloat(HMESSAGEREAD hMessage)
{
	float val;

	if (!hMessage)
		RETURN_ERROR_MSG_VALUE(2, "ILTCSBase::ReadFromMessageFloat", LT_INVALIDPARAMS, 0.0f);

	hMessage->ReadFloatFL(val);
	return val;
}

uint8 ILTCSBase::ReadFromMessageByte(HMESSAGEREAD hMessage)
{
	uint8 val;

	if (!hMessage)
		RETURN_ERROR_MSG_VALUE(2, "ILTCSBase::ReadFromMessageByte", LT_INVALIDPARAMS, 0);

	hMessage->ReadByteFL(val);
	return val;
}

uint16 ILTCSBase::ReadFromMessageWord(HMESSAGEREAD hMessage)
{
	uint16 val;

	if (!hMessage)
		RETURN_ERROR_MSG_VALUE(2, "ILTCSBase::ReadFromMessageWord", LT_INVALIDPARAMS, 0);

	hMessage->ReadWordFL(val);
	return val;
}

uint32 ILTCSBase::ReadFromMessageDWord(HMESSAGEREAD hMessage)
{
	uint32 val;

	if (!hMessage)
		RETURN_ERROR_MSG_VALUE(2, "ILTCSBase::ReadFromMessageDWord", LT_INVALIDPARAMS, 0);

	hMessage->ReadDWordFL(val);
	return val;
}

void ILTCSBase::ReadFromMessageVector(HMESSAGEREAD hMessage, LTVector* pVal)
{
	if (!hMessage || !pVal)
		RETURN_ERROR_MSG_VOID(2, "ILTCSBase::ReadFromMessageVector", LT_INVALIDPARAMS);

	hMessage->ReadVectorFL(*pVal);
}

void ILTCSBase::ReadFromMessageCompVector(HMESSAGEREAD hMessage, LTVector* pVal)
{
	if (!hMessage || !pVal)
		RETURN_ERROR_MSG_VOID(2, "ILTCSBase::ReadFromMessageCompVector", LT_INVALIDPARAMS);

	hMessage->ReadCompVectorFL(*pVal);
}

void ILTCSBase::ReadFromMessageCompPosition(HMESSAGEREAD hMessage, LTVector* pVal)
{
	if (!hMessage || !pVal)
		RETURN_ERROR_MSG_VOID(2, "ILTCSBase::ReadFromMessageCompPosition", LT_INVALIDPARAMS);

	hMessage->ReadCompPosFL(*pVal);
}

void ILTCSBase::ReadFromMessageRotation(HMESSAGEREAD hMessage, LTRotation* pVal)
{
	if (!hMessage || !pVal)
		RETURN_ERROR_MSG_VOID(2, "ILTCSBase::ReadFromMessageRotation", LT_INVALIDPARAMS);

	hMessage->ReadRotationFL(*pVal);
}

HOBJECT ILTCSBase::ReadFromMessageObject(HMESSAGEREAD hMessage)
{
	HOBJECT hObj;

	if (!hMessage)
		RETURN_ERROR_MSG_VALUE(2, "ILTCSBase::ReadFromMessageObject", LT_INVALIDPARAMS, LTNULL);

	hMessage->ReadObjectFL(hObj);
	return hObj;
}

HSTRING ILTCSBase::ReadFromMessageHString(HMESSAGEREAD hMessage)
{
	HSTRING hString;

	if (!hMessage)
		RETURN_ERROR_MSG_VALUE(2, "ILTCSBase::ReadFromMessageHString", LT_INVALIDPARAMS, LTNULL);

	hMessage->ReadHStringFL(hString);
	return hString;
}

// The same slot ReadFromMessageObject uses, and the only object reader that
// answers an LTRESULT - so the only one that reports its code as a number.
LTRESULT ILTCSBase::ReadFromLoadSaveMessageObject(HMESSAGEREAD hMessage, HOBJECT* hObject)
{
	if (!hMessage || !hObject)
		RETURN_ERROR(2, "ILTCSBase::ReadFromLoadSaveMessageObject", LT_INVALIDPARAMS);

	return hMessage->ReadObjectFL(*hObject);
}

HMESSAGEREAD ILTCSBase::ReadFromMessageHMessageRead(HMESSAGEREAD hMessage)
{
	HMESSAGEREAD hRead;

	if (!hMessage)
		RETURN_ERROR_MSG_VALUE(2, "ILTCSBase::ReadFromMessageHMessageRead", LT_INVALIDPARAMS, LTNULL);

	hRead = LTNULL;
	hMessage->ReadMessageFL(hRead);
	return hRead;
}

// Ending a message is releasing it, whichever way it was opened.
void ILTCSBase::EndHMessageRead(HMESSAGEREAD hMessage)
{
	if (!hMessage)
		RETURN_ERROR_MSG_VOID(2, "ILTCSBase::EndHMessageRead", LT_INVALIDPARAMS);

	hMessage->Release();
}

void ILTCSBase::EndHMessageWrite(HMESSAGEWRITE hMessage)
{
	if (!hMessage)
		RETURN_ERROR_MSG_VOID(2, "ILTCSBase::EndHMessageWrite", LT_INVALIDPARAMS);

	hMessage->Release();
}

void ILTCSBase::ResetRead(HMESSAGEREAD hRead)
{
	if (!hRead)
		RETURN_ERROR_MSG_VOID(2, "ILTCSBase::ResetRead", LT_INVALIDPARAMS);

	hRead->ResetPos();
}


// ---- the rotation maths ------------------------------------------------- //

// The header warns that "PARAMETER ORDERINGS ARE DIFFERENT FOR SOME OF THE
// FUNCTIONS", and this is the one it means: up and right change places on
// the way through.
LTRESULT ILTCSBase::GetRotationVectors(LTRotation* pRotation, LTVector* pUp,
                                       LTVector* pRight, LTVector* pForward)
{
	if (!pRotation || !pUp || !pRight || !pForward)
		RETURN_ERROR(2, "ILTCSBase::GetRotationVectors", LT_INVALIDPARAMS);

	return GetMathLT()->GetRotationVectors(*pRotation, *pRight, *pUp, *pForward);
}

void ILTCSBase::GetRotationVectorsFromMatrix(LTMatrix* pMat, LTVector* pUp,
                                             LTVector* pRight, LTVector* pForward)
{
	if (!pMat || !pUp || !pRight || !pForward)
		RETURN_ERROR_MSG_VOID(2, "ILTCSBase::GetRotationVectorsFromMatrix", LT_INVALIDPARAMS);

	GetMathLT()->GetRotationVectorsFromMatrix(*pMat, *pRight, *pUp, *pForward);
}

void ILTCSBase::RotateAroundAxis(LTRotation* pRotation, LTVector* pAxis, float amount)
{
	if (!pRotation || !pAxis)
		RETURN_ERROR_MSG_VOID(2, "ILTCSBase::RotateAroundAxis", LT_INVALIDPARAMS);

	GetMathLT()->RotateAroundAxis(*pRotation, *pAxis, amount);
}

void ILTCSBase::EulerRotateX(LTRotation* pRotation, float amount)
{
	if (!pRotation)
		RETURN_ERROR_MSG_VOID(2, "ILTCSBase::EulerRotateX", LT_INVALIDPARAMS);

	GetMathLT()->EulerRotateX(*pRotation, amount);
}

void ILTCSBase::EulerRotateY(LTRotation* pRotation, float amount)
{
	if (!pRotation)
		RETURN_ERROR_MSG_VOID(2, "ILTCSBase::EulerRotateY", LT_INVALIDPARAMS);

	GetMathLT()->EulerRotateY(*pRotation, amount);
}

void ILTCSBase::EulerRotateZ(LTRotation* pRotation, float amount)
{
	if (!pRotation)
		RETURN_ERROR_MSG_VOID(2, "ILTCSBase::EulerRotateZ", LT_INVALIDPARAMS);

	GetMathLT()->EulerRotateZ(*pRotation, amount);
}

void ILTCSBase::AlignRotation(LTRotation* pRotation, LTVector* pVector, LTVector* pUp)
{
	if (!pRotation || !pVector)
		RETURN_ERROR_MSG_VOID(2, "ILTCSBase::AlignRotation", LT_INVALIDPARAMS);

	rot_Align(pRotation, pVector, pUp);
}

LTRESULT ILTCSBase::SetupEuler(LTRotation* pRotation, float pitch, float yaw, float roll)
{
	if (!pRotation)
		RETURN_ERROR(2, "ILTCSBase::SetupEuler", LT_INVALIDPARAMS);

	return GetMathLT()->SetupEuler(*pRotation, pitch, yaw, roll);
}

LTRESULT ILTCSBase::InterpolateRotation(LTRotation* pDest, LTRotation* pRot1,
                                        LTRotation* pRot2, float t)
{
	if (!pDest || !pRot1 || !pRot2)
		RETURN_ERROR(2, "ILTCSBase::InterpolateRotation", LT_INVALIDPARAMS);

	return GetMathLT()->InterpolateRotation(*pDest, *pRot1, *pRot2, t);
}

void ILTCSBase::SetupTransformationMatrix(LTMatrix* pMat, LTVector* pTranslation,
                                          LTRotation* pRotation)
{
	if (!pMat || !pTranslation || !pRotation)
		RETURN_ERROR_MSG_VOID(2, "ILTCSBase::SetupTransformationMatrix", LT_INVALIDPARAMS);

	GetMathLT()->SetupTransformationMatrix(*pMat, *pTranslation, *pRotation);
}

void ILTCSBase::SetupTranslationMatrix(LTMatrix* pMat, LTVector* pTranslation)
{
	if (!pMat || !pTranslation)
		RETURN_ERROR_MSG_VOID(2, "ILTCSBase::SetupTranslationMatrix", LT_INVALIDPARAMS);

	GetMathLT()->SetupTranslationMatrix(*pMat, *pTranslation);
}

void ILTCSBase::SetupRotationMatrix(LTMatrix* pMat, LTRotation* pRot)
{
	if (!pMat || !pRot)
		RETURN_ERROR_MSG_VOID(2, "ILTCSBase::SetupRotationMatrix", LT_INVALIDPARAMS);

	GetMathLT()->SetupRotationMatrix(*pMat, *pRot);
}

void ILTCSBase::SetupTranslationFromMatrix(LTVector* pTranslation, LTMatrix* pMat)
{
	if (!pTranslation || !pMat)
		RETURN_ERROR_MSG_VOID(2, "ILTCSBase::SetupTranslationFromMatrix", LT_INVALIDPARAMS);

	GetMathLT()->SetupTranslationFromMatrix(*pTranslation, *pMat);
}

void ILTCSBase::SetupRotationFromMatrix(LTRotation* pRot, LTMatrix* pMat)
{
	if (!pRot || !pMat)
		RETURN_ERROR_MSG_VOID(2, "ILTCSBase::SetupRotationFromMatrix", LT_INVALIDPARAMS);

	GetMathLT()->SetupRotationFromMatrix(*pRot, *pMat);
}

void ILTCSBase::SetupRotationAroundPoint(LTMatrix* pMat, LTRotation* pRot, LTVector* pPoint)
{
	if (!pMat || !pRot || !pPoint)
		RETURN_ERROR_MSG_VOID(2, "ILTCSBase::SetupRotationAroundPoint", LT_INVALIDPARAMS);

	GetMathLT()->SetupRotationAroundPoint(*pMat, *pRot, *pPoint);
}
