#include "ilttransform.h"
#include "ltquatbase.h"

namespace {
void from_matrix(LTransform& transform, const LTMatrix& matrix) {
    transform.m_Pos.Init(matrix.m[0][3], matrix.m[1][3], matrix.m[2][3]);
    quat_ConvertFromMatrix(transform.m_Rot.m_Quat, matrix.m);
}
void to_matrix(const LTransform& transform, LTMatrix& matrix) {
    quat_ConvertToMatrix(transform.m_Rot.m_Quat, matrix.m);
    matrix.SetTranslation(transform.m_Pos);
}
}

LTRESULT ILTTransform::Get(LTransform& transform, LTVector& pos, LTRotation& rot) {
    pos = transform.m_Pos;
    rot = transform.m_Rot;
    return LT_OK;
}
LTRESULT ILTTransform::Set(LTransform& transform, LTVector& pos, LTRotation& rot) {
    transform.m_Pos = pos;
    transform.m_Rot = rot;
    return LT_OK;
}
LTRESULT ILTTransform::GetPos(LTransform& transform, LTVector& pos) {
    pos = transform.m_Pos;
    return LT_OK;
}
LTRESULT ILTTransform::GetRot(LTransform& transform, LTRotation& rot) {
    rot = transform.m_Rot;
    return LT_OK;
}
LTRESULT ILTTransform::ToMatrix(LTransform& transform, LTMatrix& matrix) {
    to_matrix(transform, matrix);
    return LT_OK;
}
LTRESULT ILTTransform::FromMatrix(LTransform& transform, LTMatrix& matrix) {
    from_matrix(transform, matrix);
    return LT_OK;
}
LTRESULT ILTTransform::Inverse(LTransform& inverse, LTransform& transform) {
    LTMatrix matrix, result;
    to_matrix(transform, matrix);
    Mat_InverseTransformation(&matrix, &result);
    from_matrix(inverse, result);
    return LT_OK;
}
LTRESULT ILTTransform::Multiply(LTransform& out, LTransform& first, LTransform& second) {
    LTMatrix a, b, result;
    to_matrix(first, a);
    to_matrix(second, b);
    MatMul(&result, &a, &b);
    from_matrix(out, result);
    return LT_OK;
}
LTRESULT ILTTransform::Difference(LTransform& out, LTransform& first, LTransform& second) {
    LTMatrix a, b, inverse, result;
    to_matrix(first, a);
    to_matrix(second, b);
    Mat_InverseTransformation(&b, &inverse);
    MatMul(&result, &inverse, &a);
    from_matrix(out, result);
    return LT_OK;
}
