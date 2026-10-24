#include "model_geometry.h"
#include "model_callback.h"
#include <cmath>

namespace {
bool finite(const LTVector& value) { return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z); }
}
std::optional<ModelPose> model_EvaluatePose(ModelInstance& instance) {
    ModelPose pose; pose.model=instance.m_ModelOwner;
    if (!pose.model || pose.model.get()!=instance.m_AnimTracker.GetModel()) return {};
    ModelCallbackGuard guard(instance.m_AnimTracker);
    instance.GetTransformMatrix(&pose.object); pose.hidden_pieces=instance.m_HiddenPieces;
    pose.nodes.resize(pose.model->m_Nodes.GetSize()); ModelWalker walker; instance.SetupModelWalker(&walker);
    walker.m_pObjTransform=&pose.object; walker.m_Unknown_e8=pose.nodes.data();
    if (!walker.sub_100386c6() || !guard.unchanged()) return {};
    for (const auto& matrix : pose.nodes) for (const auto& row : matrix.m) for (const auto value : row)
        if (!std::isfinite(value)) return {};
    return pose;
}
std::vector<LTVector> model_SkinVertices(const ModelPose& pose,const ModelPieceData& mesh) {
    if (!pose.model || pose.nodes.size()!=pose.model->m_Nodes.GetSize() || mesh.m_pModel!=pose.model.get())
        throw std::invalid_argument("mesh does not belong to evaluated model pose");
    std::vector<LTVector> result; result.reserve(mesh.vertices().size());
    for (const auto& vertex : mesh.vertices()) {
        LTVector point{0,0,0}; float homogeneous=0;
        if (!vertex.weight_count) {
            for (unsigned row=0;row<3;++row) {
                point[row]=pose.object.m[row][3];
                for (unsigned column=0;column<3;++column) point[row]+=pose.object.m[row][column]*vertex.position[column];
            }
        } else {
            if (!vertex.weights) throw std::invalid_argument("missing model vertex weights");
            for (const auto& weight : std::span(vertex.weights,vertex.weight_count)) {
                if (weight.m_n10>=pose.nodes.size()) throw std::invalid_argument("model vertex bone is outside pose");
                const auto& matrix=pose.nodes[weight.m_n10];
                for (unsigned row=0;row<3;++row) for (unsigned column=0;column<4;++column) point[row]+=matrix.m[row][column]*weight.m_f[column];
                for (unsigned column=0;column<4;++column) homogeneous+=matrix.m[3][column]*weight.m_f[column];
            }
            if (homogeneous!=0) point/=homogeneous;
        }
        if (!finite(point)) throw std::runtime_error("non-finite skinned model vertex");
        result.push_back(point);
    }
    return result;
}
