#include "commonmodel.h"
#include "model_callback.h"
#include "ltquatbase.h"
#include <algorithm>
#include <cmath>
#include <numeric>
#include <optional>

LTBOOL ModelWalker::IsValid() {
    if (!m_Unknown_e0 || m_Unknown_e0 > 8) return LTFALSE;
    auto* model = m_TimeRefs[0].m_pModel;
    if (!model || model->m_NodeTransforms.GetSize() != model->m_Nodes.GetSize()) return LTFALSE;
    for (uint32 i = 0; i < m_Unknown_e0; ++i) {
        auto& ref = m_TimeRefs[i];
        if (!ref.IsValid() || ref.m_pModel != model) return LTFALSE;
        if (i && (ref.m_iWeightSet >= model->m_WeightSets.GetSize() ||
                  !model->m_WeightSets[ref.m_iWeightSet] ||
                  model->m_WeightSets[ref.m_iWeightSet]->m_Weights.GetSize() != model->m_Nodes.GetSize())) return LTFALSE;
    }
    return LTTRUE;
}
bool ModelWalker::NodePose(uint32 reference, uint32 node, bool relative, LTRotation& rotation, LTVector& position) {
    auto& ref = m_TimeRefs[reference];
    auto* model = ref.m_pModel;
    auto* prev = ref.GetAnim1(); auto* cur = ref.GetAnim2();
    if (!prev || !cur || !prev->m_ppNodeRecords || !cur->m_ppNodeRecords ||
        !prev->m_ppNodeRecords[node] || !cur->m_ppNodeRecords[node]) return false;
    auto& first = prev->m_ppNodeRecords[node]->m_Array08;
    auto& second = cur->m_ppNodeRecords[node]->m_Array08;
    if (ref.m_Prev.m_iFrame >= first.GetSize() || ref.m_Cur.m_iFrame >= second.GetSize()) return false;
    auto& a = first[ref.m_Prev.m_iFrame]; auto& b = second[ref.m_Cur.m_iFrame];
    quat_Slerp(rotation.m_Quat, a.m_Rot.m_Quat, b.m_Rot.m_Quat, ref.m_Percent);
    position = a.m_Pos + (b.m_Pos - a.m_Pos) * ref.m_Percent;
    if (relative) {
        rotation = ~first[0].m_Rot * rotation;
        position -= first[0].m_Pos;
    } else if (!node) {
        const auto& a_binding = model->m_AnimArray[ref.m_Prev.m_iAnim].m_vAnimBinding;
        const auto& b_binding = model->m_AnimArray[ref.m_Cur.m_iAnim].m_vAnimBinding;
        position += a_binding + (b_binding - a_binding) * ref.m_Percent;
    }
    return true;
}
LTBOOL ModelWalker::EvaluateNodes(const std::vector<uint32>& nodes) {
    if (!IsValid()) return LTFALSE;
    std::optional<ModelCallbackGuard> callback_guard;
    std::shared_ptr<Model> model_owner;
    if (m_Unknown_ec && m_Unknown_f4) {
        auto& instance=*static_cast<ModelInstance*>(m_Unknown_f4);
        callback_guard.emplace(instance.m_AnimTracker);
        model_owner=instance.m_ModelOwner;
    }
    auto* model = m_TimeRefs[0].m_pModel;
    auto* transforms = m_Unknown_e8 ? m_Unknown_e8 : model->m_NodeTransforms.m_pArray;
    LTMatrix identity; identity.Identity();
    auto* base = m_pObjTransform ? m_pObjTransform : &identity;
    auto* owner = static_cast<Model::CModelSub1D4*>(model->m_AnimArray[m_TimeRefs[0].m_Prev.m_iAnim].m_pChildModel);
    if (owner && owner->m_Array00.GetSize() != model->m_Nodes.GetSize()) return LTFALSE;
    for (auto index : nodes) {
        auto* node = model->m_Nodes[index];
        if (!node || (node->m_iParent != 0xffffffff && node->m_iParent >= index)) return LTFALSE;
        LTRotation rotation; LTVector position;
        if (!NodePose(0,index,false,rotation,position)) return LTFALSE;
        for (uint32 i = 1; i < m_Unknown_e0; ++i) {
            const auto weight = model->m_WeightSets[m_TimeRefs[i].m_iWeightSet]->m_Weights[index];
            if (!std::isfinite(weight)) return LTFALSE;
            if (!weight) continue;
            LTRotation other; LTVector translation;
            if (!NodePose(i,index,weight == 2,other,translation)) return LTFALSE;
            if (weight == 2) { rotation = rotation * other; position += translation; }
            else {
                const auto old = rotation;
                quat_Slerp(rotation.m_Quat,old.m_Quat,other.m_Quat,weight);
                position += (translation - position) * weight;
            }
        }
        LTMatrix local, combined;
        rotation.ConvertToMatrix(local);
        local.SetTranslation(node->m_b12 & 2 ? LTVector(node->m_f04,node->m_f08,node->m_f0c) : position);
        auto* parent = node->m_iParent == 0xffffffff ? base : &transforms[node->m_iParent];
        MatMul(&combined,parent,&local);
        if (owner) {
            LTMatrix offset;
            owner->m_Array00[index].m_Rot.ConvertToMatrix(offset);
            offset.SetTranslation(owner->m_Array00[index].m_Pos);
            MatMul(&transforms[index],&combined,&offset);
        } else transforms[index] = combined;
        if (m_Unknown_ec) {
            m_Unknown_ec(m_Unknown_f4,index,&transforms[index],m_Unknown_f0);
            if (callback_guard && !callback_guard->unchanged()) return LTFALSE;
        }
    }
    return LTTRUE;
}
LTBOOL ModelWalker::sub_100386c6() {
    if (!IsValid()) return LTFALSE;
    std::vector<uint32> nodes(m_TimeRefs[0].m_pModel->m_Nodes.GetSize());
    std::iota(nodes.begin(),nodes.end(),0u);
    return EvaluateNodes(nodes);
}
LTBOOL ModelWalker::Evaluate(HMODELNODE index) {
    if (!IsValid()) return LTFALSE;
    auto* model = m_TimeRefs[0].m_pModel;
    if (index >= model->m_Nodes.GetSize()) return LTFALSE;
    std::vector<uint32> path;
    for (;;) {
        path.push_back(index);
        auto* node = model->m_Nodes[index];
        if (!node) return LTFALSE;
        auto parent = node->m_iParent;
        if (parent == 0xffffffff) break;
        if (parent >= index) return LTFALSE;
        index = parent;
    }
    std::reverse(path.begin(),path.end());
    return EvaluateNodes(path);
}
