#include "sharedcommon.h"
#include "engine/portable/string.h"
#include "iltmath.h"
#include "ilttransform.h"

SharedCommonLT::SharedCommonLT() : m_pMath(nullptr) {
    m_pTransformLT = nullptr;
    m_pModelLT = nullptr;
}

LTRESULT SharedCommonLT::GetObjectFlags(HOBJECT object, ObjFlagType type, uint32& flags) {
    flags = 0;
    if (!object || (type != OFT_Flags && type != OFT_Flags2)) return LT_INVALIDPARAMS;
    flags = type == OFT_Flags ? object->m_Flags : object->m_Flags2;
    return LT_OK;
}
LTRESULT SharedCommonLT::SetObjectFlags(HOBJECT object, ObjFlagType type, uint32 flags) {
    if (!object || (type != OFT_Flags && type != OFT_Flags2)) return LT_INVALIDPARAMS;
    (type == OFT_Flags ? object->m_Flags : object->m_Flags2) = flags;
    return LT_OK;
}
LTRESULT SharedCommonLT::GetObjectType(HOBJECT object, uint32* type) {
    if (!object || !type) return LT_INVALIDPARAMS;
    *type = object->m_ObjectType;
    return LT_OK;
}
LTRESULT SharedCommonLT::GetModelAnimUserDims(HOBJECT object, LTVector* dims, HMODELANIM anim) {
    if (!object || object->m_ObjectType != OT_MODEL || !dims) return LT_INVALIDPARAMS;
    auto* model = static_cast<ModelInstance*>(object)->m_AnimTracker.GetModel();
    if (!model) return LT_NOTINITIALIZED;
    if (anim >= model->m_AnimArray.GetSize()) return LT_INVALIDPARAMS;
    *dims = model->m_AnimArray[anim].m_vDims;
    return LT_OK;
}
LTRESULT SharedCommonLT::GetAttachments(HLOCALOBJ object, HLOCALOBJ* list, uint32 capacity,
                                      uint32& size, uint32& total) {
    size = total = 0;
    if (!object || (!list && capacity)) return LT_INVALIDPARAMS;
    for (auto* attachment = object->m_pAttachments; attachment; attachment = attachment->m_pNext) {
        HOBJECT parent, child;
        if (GetAttachmentObjects(reinterpret_cast<HATTACHMENT>(attachment), parent, child) == LT_OK) {
            if (size < capacity) list[size++] = child;
            ++total;
        }
    }
    return LT_OK;
}
LTRESULT SharedCommonLT::GetAttachmentTransform(HATTACHMENT handle, LTransform& out, LTBOOL world) {
    if (!handle) return LT_INVALIDPARAMS;
    if (!m_pTransformLT) return LT_NOTINITIALIZED;
    HOBJECT parent, child;
    auto result = GetAttachmentObjects(handle, parent, child);
    if (result != LT_OK) return result;
    if (!parent || !child) return LT_NOTINITIALIZED;
    auto* attachment = reinterpret_cast<Attachment*>(handle);
    LTransform parent_transform;
    if (parent->m_ObjectType == OT_MODEL && attachment->m_iNode != -1) {
        if (!m_pModelLT) return LT_NOTINITIALIZED;
        // Attachments store socket handles; node handles are biased past
        // the socket table by CreateAttachment and model replacement.
        result = m_pModelLT->GetSocketTransform(parent, attachment->m_iNode, parent_transform, world);
        if (result != LT_OK) return result;
    } else if (world) {
        parent_transform.m_Pos = parent->m_Pos;
        parent_transform.m_Rot = parent->m_Rotation;
    } else {
        parent_transform.m_Pos.Init();
        parent_transform.m_Rot.Init();
    }
    return m_pTransformLT->Multiply(out, parent_transform, attachment->m_Offset);
}
LTRESULT SharedCommonLT::GetAttachedTransform(HATTACHMENT handle, uint32 piece, LTransform& out, LTBOOL node) {
    if (!handle) return LT_INVALIDPARAMS;
    if (!m_pTransformLT || !m_pModelLT) return LT_NOTINITIALIZED;
    HOBJECT parent, child;
    auto result = GetAttachmentObjects(handle, parent, child);
    if (result != LT_OK) return result;
    if (!parent || !child) return LT_NOTINITIALIZED;
    LTransform attachment, local;
    result = GetAttachmentTransform(handle, attachment, LTTRUE);
    if (result != LT_OK) return result;
    result = node ? m_pModelLT->GetNodeTransform(child, piece, local, LTFALSE)
                  : m_pModelLT->GetSocketTransform(child, piece, local, LTFALSE);
    if (result != LT_OK) return result;
    return m_pTransformLT->Multiply(out, attachment, local);
}
LTRESULT SharedCommonLT::GetAttachedModelNodeTransform(HATTACHMENT handle, HMODELNODE node, LTransform& out) {
    return GetAttachedTransform(handle, node, out, LTTRUE);
}
LTRESULT SharedCommonLT::GetAttachedModelSocketTransform(HATTACHMENT handle, HMODELSOCKET socket, LTransform& out) {
    return GetAttachedTransform(handle, socket, out, LTFALSE);
}
LTRESULT SharedCommonLT::Parse(ConParse* parser) {
    if (!parser) return LT_INVALIDPARAMS;
    return parser->Parse() ? LT_OK : LT_FINISHED;
}
LTRESULT SharedCommonLT::GetRotationVectors(LTRotation& rotation, LTVector& up, LTVector& right, LTVector& forward) {
    return m_pMath ? m_pMath->GetRotationVectors(rotation, right, up, forward) : LT_NOTINITIALIZED;
}
LTRESULT SharedCommonLT::SetupEuler(LTRotation& rotation, float pitch, float yaw, float roll) {
    return m_pMath ? m_pMath->SetupEuler(rotation, pitch, yaw, roll) : LT_NOTINITIALIZED;
}
void SharedCommonLT::SetMath(ILTMath* math) { m_pMath = math; }
LTRESULT SharedCommonLT::CreateMessage(ILTMessage*& message) {
    message = nullptr;
    return LT_ERROR;
}
LTBOOL ci_UpperStrcmp(char* first, char* second) {
    return first && second ? nolf::ascii_compare(first, second) == 0 : first == second;
}
