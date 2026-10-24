#include "world_polygon_geometry.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace {
bool finite(LTVector value) { return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z); }
void affine(const LTMatrix& matrix) {
    for (unsigned row=0;row<4;++row) for (unsigned column=0;column<4;++column)
        if (!std::isfinite(matrix.m[row][column])) throw std::invalid_argument("nonfinite polygon transform");
    if (matrix.m[3][0]!=0 || matrix.m[3][1]!=0 || matrix.m[3][2]!=0 || matrix.m[3][3]!=1) throw std::invalid_argument("polygon transform is not affine");
}
}
LTVector world_TransformPoint(const LTMatrix& matrix,LTVector source) {
    LTVector result{0,0,0};
    for (unsigned row=0;row<3;++row) {
        double value=matrix.m[row][3]; for (unsigned column=0;column<3;++column) value+=double(matrix.m[row][column])*source[column];
        if (!std::isfinite(value) || std::abs(value)>std::numeric_limits<float>::max()) throw std::invalid_argument("world polygon transform overflow");
        result[row]=static_cast<float>(value);
    }
    return result;
}
WorldPolygonGeometry world_PolygonGeometry(const CWorldPart& polygon,const LTMatrix& forward,const LTMatrix& inverse) {
    affine(forward); affine(inverse);
    const auto* plane=polygon.m_pSurface ? polygon.m_pSurface->m_pPlane : nullptr; const auto vertices=polygon.render_vertices();
    if (!plane || !finite(plane->m_Normal) || !std::isfinite(plane->m_Dist) || vertices.size()<3 || vertices.size()>512) throw std::invalid_argument("invalid world polygon snapshot");
    WorldPolygonGeometry output; std::array<double,3> normal{}; double length=0,source_length=0;
    for (unsigned row=0;row<3;++row) {
        for (unsigned column=0;column<3;++column) normal[row]+=double(inverse.m[column][row])*plane->m_Normal[column];
        length+=normal[row]*normal[row]; source_length+=double(plane->m_Normal[row])*plane->m_Normal[row];
    }
    if (length<1e-30 || source_length<1e-30 || !std::isfinite(length)) throw std::invalid_argument("degenerate polygon plane transform");
    const auto magnitude=std::sqrt(length);
    for (unsigned row=0;row<3;++row) output.plane.m_Normal[row]=static_cast<float>(normal[row]/magnitude);
    LTVector source_point{0,0,0}; for (unsigned row=0;row<3;++row) source_point[row]=static_cast<float>(double(plane->m_Normal[row])*plane->m_Dist/source_length);
    const auto point=world_TransformPoint(forward,source_point); double distance=0;
    for (unsigned row=0;row<3;++row) distance+=double(output.plane.m_Normal[row])*point[row];
    if (!std::isfinite(distance) || std::abs(distance)>std::numeric_limits<float>::max()) throw std::invalid_argument("polygon plane distance overflow");
    output.plane.m_Dist=static_cast<float>(distance); output.points.reserve(vertices.size());
    for (const auto& vertex : vertices) { if (!vertex.m_pPos) throw std::invalid_argument("missing polygon vertex"); output.points.push_back(world_TransformPoint(forward,*vertex.m_pPos)); }
    return output;
}
double world_PolygonDistanceSquared(const WorldPolygonGeometry& polygon,LTVector point) {
    if (!finite(point) || !finite(polygon.plane.m_Normal) || !std::isfinite(polygon.plane.m_Dist) || polygon.points.size()<3 || polygon.points.size()>512) throw std::invalid_argument("invalid polygon distance query");
    std::array<double,3> center{};
    for (const auto& vertex : polygon.points) {
        if (!finite(vertex)) throw std::invalid_argument("nonfinite polygon distance vertex");
        for (unsigned axis=0;axis<3;++axis) center[axis]+=double(vertex[axis])/polygon.points.size();
    }
    double distance=-polygon.plane.m_Dist,normal_length=0;
    for (unsigned axis=0;axis<3;++axis) { distance+=double(polygon.plane.m_Normal[axis])*point[axis]; normal_length+=double(polygon.plane.m_Normal[axis])*polygon.plane.m_Normal[axis]; }
    if (normal_length<1e-30) throw std::invalid_argument("degenerate distance plane");
    double nearest=std::numeric_limits<double>::infinity(); bool inside=true,area=false;
    for (std::size_t edge=0;edge<polygon.points.size();++edge) {
        const auto a=polygon.points[edge],b=polygon.points[(edge+1)%polygon.points.size()]; const auto n=polygon.plane.m_Normal;
        std::array<double,3> d{},relative{}; double squared=0,along=0;
        for (unsigned axis=0;axis<3;++axis) { d[axis]=double(b[axis])-a[axis]; relative[axis]=double(point[axis])-a[axis]; squared+=d[axis]*d[axis]; along+=relative[axis]*d[axis]; }
        const auto fraction=squared ? std::clamp(along/squared,0.,1.) : 0.; double separation=0;
        for (unsigned axis=0;axis<3;++axis) { const double delta=relative[axis]-fraction*d[axis]; separation+=delta*delta; }
        nearest=std::min(nearest,separation);
        const std::array<double,3> cross{d[1]*n.z-d[2]*n.y,d[2]*n.x-d[0]*n.z,d[0]*n.y-d[1]*n.x}; double reference=0,side=0;
        for (unsigned axis=0;axis<3;++axis) { reference+=cross[axis]*(center[axis]-a[axis]); side+=cross[axis]*relative[axis]; }
        area=area || std::abs(reference)>1e-20;
        const auto length=std::hypot(cross[0],cross[1],cross[2]);
        if ((reference<0 ? -side : side)<-1e-6*length) inside=false;
    }
    return inside && area ? distance*distance/normal_length : nearest;
}
