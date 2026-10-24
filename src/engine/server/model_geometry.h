#pragma once
#include "commonmodel.h"
#include <optional>

struct ModelPose {
    std::shared_ptr<const Model> model;
    std::vector<LTMatrix> nodes;
    LTMatrix object;
    uint32 hidden_pieces=0;
};
// A renderer owns its pose matrices. Evaluating another instance or calling
// back into the engine cannot overwrite the preceding instance's result.
// Removal/rebinding during a node callback discards the incomplete pose.
std::optional<ModelPose> model_EvaluatePose(ModelInstance&);
std::vector<LTVector> model_SkinVertices(const ModelPose&,const ModelPieceData&);
