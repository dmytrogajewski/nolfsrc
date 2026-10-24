#pragma once
#include "ltbasedefs.h"
#include <array>
#include <cstdint>

namespace nolf {
// Column-major device matrices, +Z forward, pixel rectangles from the top left.
// The same view supplies SDK pixel rays and rendering/culling calculations.
class CameraView {
public:
    CameraView(LTVector,LTRotation,LTRect,float fov_x,float fov_y,
        float near_distance=7,float far_distance=10000,bool horizontal_plus=true);
    const std::array<float,16>& projection() const { return projection_; }
    const std::array<float,16>& view() const { return view_; }
    LTRect rectangle() const { return rectangle_; }
    LTVector position() const { return position_; }
    LTVector right() const { return axes_[0]; }
    LTVector up() const { return axes_[1]; }
    LTVector forward() const { return axes_[2]; }
    // Point on the plane one unit ahead of the camera, matching Get3DCameraPt.
    LTVector pixel_point(int x,int y) const;
    bool visible(LTVector center,float radius) const;
private:
    std::array<float,16> projection_{},view_{};
    std::array<LTVector,3> axes_;
    std::array<LTPlane,6> planes_;
    LTVector position_;
    LTRect rectangle_;
    float tangent_x_=0,tangent_y_=0;
};
}
