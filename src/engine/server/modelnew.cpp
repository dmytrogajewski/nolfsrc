// Native model records shared by loading, animation and object instances.
#include "commonmodel.h"
#include <limits>

void ModelPieceData::SetModel(Model* model) { m_pModel = model; }
ModelPieceData::ModelPieceData(const ModelPieceData& other) : m_pModel(other.m_pModel) {
    auto* alloc = m_pModel ? m_pModel->GetAlloc() : &g_DefAlloc;
    if (!m_Array00.CopyArray2(other.m_Array00,alloc) || !m_Array10.CopyArray2(other.m_Array10,alloc)) {
        m_Array00.Term(alloc); m_Array10.Term(alloc);
        throw std::bad_alloc();
    }
}
ModelPieceData& ModelPieceData::operator=(const ModelPieceData& other) {
    if (this == &other) return *this;
    ModelPieceData staged(other);
    m_Array00.Swap(staged.m_Array00); m_Array10.Swap(staged.m_Array10);
    std::swap(m_pModel,staged.m_pModel);
    return *this;
}
ModelPieceData::~ModelPieceData() {
    auto* alloc = m_pModel ? m_pModel->GetAlloc() : &g_DefAlloc;
    m_Array00.Term(alloc);
    m_Array10.Term(alloc);
}
ModelPiece::ModelPiece(Model* model) : ModelPieceData(model), m_pModel68(model) {
    m_Unknown24 = m_nOffset = m_Unknown44 = 0;
    m_f2c = 5; m_f30 = 1;
    m_Name[0] = 0;
}
ModelPiece::~ModelPiece() {
    m_Array34.Term(m_pModel ? m_pModel->GetAlloc() : &g_DefAlloc);
}
ModelWeightSet::ModelWeightSet(Model* model) : m_pModel(model) { m_Name[0] = 0; }
char* Model::sub_100173fa(char* name) { return m_DataRef.sub_1001638c(name); }

LTBOOL ModelNode::sub_10016ac3(uint32* index) {
    if (!index || !m_pModel || *index >= m_pModel->m_Nodes.GetSize() || *index > 65535) return LTFALSE;
    m_n10 = static_cast<uint16>(*index);
    m_pModel->m_Nodes[(*index)++] = this;
    for (uint32 i = 0; i < m_Array18.GetSize(); ++i)
        if (!m_Array18[i] || !m_Array18[i]->sub_10016ac3(index)) return LTFALSE;
    return LTTRUE;
}
void ModelNode::sub_10016b11(uint32 parent) {
    m_iParent = parent;
    for (uint32 i = 0; i < m_Array18.GetSize(); ++i)
        if (m_Array18[i]) m_Array18[i]->sub_10016b11(m_n10);
}
uint32 Model::sub_1001761d(LTBOOL lod) {
    uint32 total = 0;
    for (uint32 i = 0; i < m_Pieces.GetSize(); ++i)
        if (auto* data = m_Pieces[i]->GetPieceData(static_cast<uint32>(lod))) total += data->m_Array10.GetSize();
    return total;
}
uint32 Model::sub_10017660() {
    uint32 total = 0;
    for (uint32 i = 0; i < m_Pieces.GetSize(); ++i) total += m_Pieces[i]->m_Array00.GetSize();
    return total;
}
uint32 Model::OwnedAnimationCount() const {
    uint32 total = 0;
    for (uint32 i = 0; i < m_AnimArray.GetSize(); ++i)
        if (m_AnimArray[i].m_pAnim && m_AnimArray[i].m_pAnim->m_pModel == this) ++total;
    return total;
}
LTBOOL Model::sub_10017b8a(uint32 slot, CModelSub1D4* record, Model* model, char* name) {
    if (slot >= 16 || !record) return LTFALSE;
    record->m_pName = name;
    record->m_pOwner24 = this;
    record->m_pOwner28 = model;
    record->m_Unknown18 = 0;
    if (model && model != this) ++model->m_CacheCount;
    m_pChildModels[slot] = record;
    return LTTRUE;
}
