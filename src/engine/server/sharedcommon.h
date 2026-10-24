#pragma once
#include "iltcommon.h"
#include "commonmodel.h"

// Common SDK behavior shared by the client and server implementations.
// World queries, attachment identity, filenames and messages are supplied
// by the corresponding owner; native calls use the SDK virtual interfaces.
class SharedCommonLT : public ILTCommon {
public:
    SharedCommonLT();
    LTRESULT GetObjectFlags(HOBJECT, ObjFlagType, uint32&) override;
    LTRESULT SetObjectFlags(HOBJECT, ObjFlagType, uint32) override;
    LTRESULT GetAttachments(HLOCALOBJ, HLOCALOBJ*, uint32 capacity, uint32& size, uint32& total) override;
    LTRESULT GetObjectType(HOBJECT, uint32*) override;
    LTRESULT GetModelAnimUserDims(HOBJECT, LTVector*, HMODELANIM) override;
    LTRESULT GetAttachmentTransform(HATTACHMENT, LTransform&, LTBOOL world) override;
    LTRESULT GetAttachedModelNodeTransform(HATTACHMENT, HMODELNODE, LTransform&) override;
    LTRESULT GetAttachedModelSocketTransform(HATTACHMENT, HMODELSOCKET, LTransform&) override;
    LTRESULT Parse(ConParse*) override;
    LTRESULT GetRotationVectors(LTRotation&, LTVector& up, LTVector& right, LTVector& forward) override;
    LTRESULT SetupEuler(LTRotation&, float pitch, float yaw, float roll) override;
    LTRESULT CreateMessage(ILTMessage*&) override;
    LTRESULT GetAttachedTransform(HATTACHMENT, uint32 piece, LTransform&, LTBOOL node);
    void SetMath(class ILTMath*);
    class ILTMath* m_pMath;
};

void quat_ConvertToMatrix(LTRotation*, LTMatrix*);
void mat_Multiply(LTMatrix*, LTMatrix*, LTMatrix*);
