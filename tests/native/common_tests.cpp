#include "engine/server/sharedcommon.h"
#include "iltmath.h"
#include "ilttransform.h"
#include <cmath>
#include <iostream>
#include <stdexcept>

LTBOOL ci_UpperStrcmp(char*, char*);

namespace {
void require(bool value, const char* why) { if (!value) throw std::runtime_error(why); }
bool near(float first, float second) { return std::abs(first-second) < .001f; }
void same(ILTTransform& api, LTransform first, LTransform second) {
    LTMatrix a, b;
    api.ToMatrix(first, a); api.ToMatrix(second, b);
    for (unsigned row = 0; row < 4; ++row) for (unsigned col = 0; col < 4; ++col)
        require(near(a.m[row][col], b.m[row][col]), "transform matrix differs");
}
LTransform identity() { LTransform value; value.m_Pos.Init(); value.m_Rot.Init(); return value; }
class CommonFixture final : public SharedCommonLT {
public:
    void bind(ILTTransform* transform, ILTModel* model, ILTMath* math) {
        m_pTransformLT = transform; m_pModelLT = model; SetMath(math);
    }
    HOBJECT parent = nullptr, child = nullptr;
    LTRESULT SetObjectFilenames(HOBJECT, ObjectCreateStruct*) override { return LT_ERROR; }
    LTRESULT GetAttachmentObjects(HATTACHMENT handle, HOBJECT& out_parent, HOBJECT& out_child) override {
        out_parent = parent; out_child = child;
        return handle && parent && child ? LT_OK : LT_NOTINITIALIZED;
    }
    LTRESULT GetPointStatus(LTVector*) override { return LT_NOTINWORLD; }
    LTRESULT GetPointShade(LTVector*, LTVector*) override { return LT_NOTINWORLD; }
    LTRESULT GetPolyTextureFlags(HPOLY, uint32*) override { return LT_NOTINWORLD; }
};
void transforms() {
    ILTTransform implementation;
    ILTTransform* api = &implementation;
    ILTMath math;
    auto a = identity(), b = identity();
    a.m_Pos.Init(12, -3, 7); b.m_Pos.Init(-2, 6, 11);
    math.SetupEuler(a.m_Rot, .4f, -.7f, .2f);
    math.SetupEuler(b.m_Rot, -.1f, .8f, -.3f);
    LTransform inverse, result, difference;
    require(api->Inverse(inverse, a) == LT_OK, "inverse SDK call");
    api->Multiply(result, a, inverse); same(*api, result, identity());
    api->Multiply(result, inverse, a); same(*api, result, identity());
    api->Inverse(a, a); same(*api, a, inverse);
    api->Inverse(a, a);
    api->Difference(difference, a, b);
    api->Multiply(result, b, difference); same(*api, result, a);
    api->Multiply(result, a, b);
    auto alias = a; api->Multiply(alias, alias, b); same(*api, alias, result);
    alias = b; api->Multiply(alias, a, alias); same(*api, alias, result);
    LTMatrix matrix; api->ToMatrix(result, matrix);
    api->FromMatrix(alias, matrix); same(*api, alias, result);
    LTVector pos; LTRotation rot;
    api->Get(result, pos, rot); api->Set(alias, pos, rot); same(*api, alias, result);
    api->GetPos(result, pos); api->GetRot(result, rot);
    require(near(pos.x, result.m_Pos.x) && near(rot.m_Quat[0], result.m_Rot.m_Quat[0]), "SDK getters");
}
void model_fixture(Model& model) {
    require(model.sub_1001728c(1) && model.sub_100172e3(1), "index root node");
    require(model.m_Sub1d4.m_Array00.SetSize(1) && model.m_AnimArray.SetSize(1), "model arrays");
    model.m_Sub1d4.m_Array00[0].m_Pos.Init(); model.m_Sub1d4.m_Array00[0].m_Rot.Init();
    auto* anim = BaseNew(&g_DefAlloc, static_cast<ModelAnim*>(nullptr), 1, &model);
    model.m_AnimArray[0].m_pAnim = anim; model.m_AnimArray[0].m_pChildModel = &model.m_Sub1d4;
    model.m_AnimArray[0].m_vDims.Init(4,5,6);
    require(anim->m_Array08.SetSize(1) && anim->m_Sub28.m_Array08.SetSize(1), "animation frames");
    anim->m_Array08[0].m_Time = 0;
    anim->m_Sub28.m_pSub20c04 = model.m_pSub20c;
    anim->m_Sub28.m_Array08[0].m_Pos.Init(3,0,0);
    anim->m_Sub28.m_Array08[0].m_Rot.Init();
    require(anim->sub_100168a3(1), "index animation");
    require(model.m_Sockets.SetSize(1), "socket slot");
    auto* socket = BaseNew(&g_DefAlloc, static_cast<ModelSocket*>(nullptr), 1);
    model.m_Sockets[0] = socket; socket->m_iNode = 0; socket->m_vPos.Init(0,5,0);
    require(model.m_Pieces.SetSize(32), "piece slots");
    for (uint32 i = 0; i < 32; ++i) model.m_Pieces[i] = BaseNew(&g_DefAlloc, static_cast<ModelPiece*>(nullptr), 1, &model);
}
void common_interface() {
    CommonFixture common;
    ILTCommon* api = &common;
    ILTMath math; ILTTransform transform; CommonModelLT models;
    require(!api->GetTransformLT() && !api->GetModelLT() && !common.m_pMath, "all interface dependencies start null");
    LTRotation rot; LTVector up, right, forward;
    require(api->SetupEuler(rot, 0,0,0) == LT_NOTINITIALIZED, "unbound math is safe");
    LTransform unbound_out;
    require(api->GetAttachmentTransform(reinterpret_cast<HATTACHMENT>(1), unbound_out, 0) == LT_NOTINITIALIZED, "unbound transform is safe");
    common.bind(&transform, &models, &math);
    require(api->SetupEuler(rot,0,0,0) == LT_OK && api->GetRotationVectors(rot,up,right,forward) == LT_OK,
            "SDK math forwards");
    require(near(up.y,1) && near(right.x,1) && near(forward.z,1), "rotation vector ordering");
    LTObject normal;
    uint32 flags = 99, type = 99;
    require(api->GetObjectFlags(nullptr,OFT_Flags,flags) == LT_INVALIDPARAMS && flags == 0, "null flags");
    require(api->SetObjectFlags(&normal,OFT_Flags,17) == LT_OK && api->SetObjectFlags(&normal,OFT_Flags2,23) == LT_OK,
            "independent flag words");
    require(api->GetObjectFlags(&normal,OFT_Flags,flags) == LT_OK && flags == 17 && normal.m_Flags2 == 23, "flag dispatch");
    require(api->GetObjectType(&normal,&type) == LT_OK && type == OT_NORMAL &&
            api->GetObjectType(&normal,nullptr) == LT_INVALIDPARAMS, "object type validation");
    Model model(&g_DefAlloc,&g_DefAlloc); model_fixture(model);
    ModelInstance parent, child;
    at_Init(&parent.m_AnimTracker,&model,0); at_Init(&child.m_AnimTracker,&model,0);
    LTBOOL hidden = LTFALSE;
    require(models.SetPieceHideStatus(&parent,31,LTTRUE) == LT_OK &&
            models.GetPieceHideStatus(&parent,31,hidden) == LT_OK && hidden, "highest representable hidden-piece bit");
    require(models.SetPieceHideStatus(&parent,32,LTTRUE) == LT_INVALIDPARAMS &&
            models.GetPieceHideStatus(&parent,UINT32_MAX,hidden) == LT_INVALIDPARAMS, "piece handles checked before shift");
    parent.m_Pos.Init(10,20,30);
    common.parent = &parent; common.child = &child;
    Attachment attachment{}; attachment.m_Offset = identity(); attachment.m_Offset.m_Pos.Init(0,0,7);
    attachment.m_iNode = 0;
    const auto handle = reinterpret_cast<HATTACHMENT>(&attachment);
    LTransform out;
    require(api->GetAttachmentTransform(handle,out,LTTRUE) == LT_OK, "attachment uses actual SDK model dispatch");
    require(near(out.m_Pos.x,13) && near(out.m_Pos.y,25) && near(out.m_Pos.z,37), "attachment socket offset in world space");
    require(api->GetAttachmentTransform(handle,out,LTFALSE) == LT_OK && near(out.m_Pos.x,3) && near(out.m_Pos.y,5), "local socket transform");
    attachment.m_iNode = 1;
    require(api->GetAttachmentTransform(handle,out,LTTRUE) == LT_OK && near(out.m_Pos.y,20), "biased node handle");
    attachment.m_iNode = -1;
    require(api->GetAttachedModelNodeTransform(handle,0,out) == LT_OK && near(out.m_Pos.x,13) && near(out.m_Pos.y,20), "attached child node transform");
    require(api->GetAttachedModelSocketTransform(handle,0,out) == LT_OK && near(out.m_Pos.y,25), "attached child socket transform");
    require(api->GetModelAnimUserDims(&parent,&up,0) == LT_OK && near(up.y,5) &&
            api->GetModelAnimUserDims(&parent,nullptr,0) == LT_INVALIDPARAMS, "animation dimensions");
    uint32 size, total;
    parent.m_pAttachments = &attachment;
    require(api->GetAttachments(&parent,nullptr,0,size,total) == LT_OK && size == 0 && total == 1, "attachment count without buffer");
    HLOCALOBJ list[1];
    require(api->GetAttachments(&parent,list,1,size,total) == LT_OK && size == 1 && total == 1 && list[0] == &child, "attachment enumeration");
    parent.m_pAttachments = nullptr;
    ConParse parse; parse.Init("one two; three");
    require(api->Parse(&parse) == LT_OK && parse.m_nArgs == 2 && api->Parse(&parse) == LT_OK &&
            parse.m_nArgs == 1 && api->Parse(&parse) == LT_FINISHED, "SDK parser forward");
    char lower[] = "file.abc", upper[] = "FILE.ABC", other[] = "other";
    require(ci_UpperStrcmp(lower,upper) && !ci_UpperStrcmp(lower,other), "legacy comparison is boolean equality");
}
}
int main() {
    try { transforms(); common_interface(); std::cout << "native common interface tests passed\n"; }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
