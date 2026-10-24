#include "engine/client/camera_view.h"
#include <cmath>
#include <limits>
#include <numbers>
#include <stdexcept>

namespace nolf {
namespace {
bool finite(LTVector value) { return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z); }
}
CameraView::CameraView(LTVector position,LTRotation rotation,LTRect rectangle,float fov_x,float fov_y,
    float near_distance,float far_distance,bool horizontal_plus) : position_(position),rectangle_(rectangle) {
    const auto width=std::int64_t(rectangle.right)-rectangle.left,height=std::int64_t(rectangle.bottom)-rectangle.top;
    if (!finite(position) || width<=0 || height<=0 || width>std::numeric_limits<int>::max() || height>std::numeric_limits<int>::max() ||
        !std::isfinite(fov_x) || !std::isfinite(fov_y) || fov_x<=0 || fov_y<=0 || fov_x>=std::numbers::pi_v<float> || fov_y>=std::numbers::pi_v<float> ||
        !std::isfinite(near_distance) || !std::isfinite(far_distance) || near_distance<=0 || far_distance<=near_distance)
        throw std::invalid_argument("invalid native camera projection");
    double norm=0;
    for (float value : rotation.m_Quat) { if (!std::isfinite(value)) throw std::invalid_argument("nonfinite camera rotation"); norm+=double(value)*value; }
    if (!norm) throw std::invalid_argument("zero camera rotation");
    const double scale=2/norm,x=rotation.m_Quat[0],y=rotation.m_Quat[1],z=rotation.m_Quat[2],w=rotation.m_Quat[3];
    const double xx=x*x*scale,yy=y*y*scale,zz=z*z*scale,xy=x*y*scale,xz=x*z*scale,yz=y*z*scale,wx=w*x*scale,wy=w*y*scale,wz=w*z*scale;
    axes_[0].Init(float(1-yy-zz),float(xy+wz),float(xz-wy));
    axes_[1].Init(float(xy-wz),float(1-xx-zz),float(yz+wx));
    axes_[2].Init(float(xz+wy),float(yz-wx),float(1-xx-yy));
    tangent_x_=std::tan(fov_x*.5f); tangent_y_=std::tan(fov_y*.5f);
    if (horizontal_plus) tangent_x_*=float(double(width)/height/(4./3.));
    const double n=near_distance,f=far_distance;
    projection_[0]=1/tangent_x_; projection_[5]=1/tangent_y_;
    projection_[10]=float((f+n)/(f-n)); projection_[11]=1; projection_[14]=float(-2*f*n/(f-n));
    for (unsigned row=0;row<3;++row) {
        for (unsigned column=0;column<3;++column) view_[column*4+row]=axes_[row][column];
        view_[12+row]=-axes_[row].Dot(position);
    }
    view_[15]=1;
    const std::array<LTVector,6> normals{{{0,0,1},{0,0,-1},{1,0,tangent_x_},{-1,0,tangent_x_},{0,1,tangent_y_},{0,-1,tangent_y_}}};
    for (unsigned i=0;i<planes_.size();++i) {
        const float length=normals[i].Mag(); const auto normal=normals[i]/length;
        auto& plane=planes_[i]; plane.m_Normal=axes_[0]*normal.x+axes_[1]*normal.y+axes_[2]*normal.z;
        plane.m_Dist=plane.m_Normal.Dot(position)+(i==0 ? near_distance : i==1 ? -far_distance : 0)/length;
    }
    for (float value : projection_) if (!std::isfinite(value)) throw std::invalid_argument("camera projection overflow");
    for (float value : view_) if (!std::isfinite(value)) throw std::invalid_argument("camera view overflow");
}
LTVector CameraView::pixel_point(int x,int y) const {
    if (x<rectangle_.left || x>=rectangle_.right || y<rectangle_.top || y>=rectangle_.bottom) throw std::out_of_range("pixel outside camera");
    const auto half_width=(double(rectangle_.right)-rectangle_.left)*.5,half_height=(double(rectangle_.bottom)-rectangle_.top)*.5;
    const auto horizontal=float((double(x)-rectangle_.left-half_width)/half_width)*tangent_x_;
    const auto vertical=-float((double(y)-rectangle_.top-half_height)/half_height)*tangent_y_;
    return position_+axes_[2]+axes_[0]*horizontal+axes_[1]*vertical;
}
bool CameraView::visible(LTVector center,float radius) const {
    if (!finite(center) || !std::isfinite(radius) || radius<0) throw std::invalid_argument("invalid camera culling bounds");
    for (const auto& plane : planes_) if (plane.m_Normal.Dot(center)-plane.m_Dist < -radius) return false;
    return true;
}
}
