#include "engine/client/camera_view.h"
#include "engine/client/client_scene.h"
#include <cmath>
#include <iostream>
#include <limits>
#include <numbers>
#include <stdexcept>

namespace {
void require(bool value,const char* why) { if (!value) throw std::runtime_error(why); }
template<class F> void rejects(F&& function) { bool failed=false; try { function(); } catch (const std::exception&) { failed=true; } require(failed,"invalid camera request accepted"); }
bool near(LTVector a,LTVector b) { return a.DistSqr(b)<.00001f; }
std::array<double,4> multiply(const std::array<float,16>& matrix,std::array<double,4> point) {
    std::array<double,4> result{};
    for (unsigned row=0;row<4;++row) for (unsigned column=0;column<4;++column) result[row]+=matrix[column*4+row]*point[column];
    return result;
}
void projection() {
    LTRotation identity; identity.Init(); constexpr auto fov=std::numbers::pi_v<float>/2;
    for (const LTRect rect : {LTRect{0,0,640,480},LTRect{13,27,1293,747},LTRect{5,7,485,647},LTRect{0,0,1,1}}) {
        nolf::CameraView camera({10,20,30},identity,rect,fov,fov);
        const auto width=rect.right-rect.left,height=rect.bottom-rect.top;
        require(near(camera.forward(),{0,0,1}) && near(camera.right(),{1,0,0}) && near(camera.up(),{0,1,0}),"camera identity axes");
        const auto edge=camera.pixel_point(rect.left,rect.top);
        require(near(edge,{10-float(double(width)/height*.75),21,31}),"Hor+ aspect ratio/pixel origin differs");
        for (const auto pixel : {std::array{rect.left,rect.top},std::array{rect.left+width/2,rect.top+height/2},std::array{rect.right-1,rect.bottom-1}}) {
            const auto point=camera.position()+(camera.pixel_point(pixel[0],pixel[1])-camera.position())*100;
            const auto eye=multiply(camera.view(),{point.x,point.y,point.z,1}); const auto clip=multiply(camera.projection(),eye);
            const auto x=rect.left+(clip[0]/clip[3]+1)*width/2,y=rect.top+(1-clip[1]/clip[3])*height/2;
            require(std::abs(x-pixel[0])<.001 && std::abs(y-pixel[1])<.001,"camera pixel ray and rendered projection disagree");
        }
        rejects([&] { camera.pixel_point(rect.right,rect.top); }); rejects([&] { camera.pixel_point(rect.left,rect.bottom); });
    }
    nolf::CameraView camera({0,0,0},identity,{0,0,640,480},fov,fov,7,1000);
    require(camera.visible({0,0,7},0) && !camera.visible({0,0,6},0) && camera.visible({0,0,6},2) && camera.visible({0,0,1000},0) && !camera.visible({0,0,1001},0),"camera near/far sphere clipping");
    require(camera.visible({19,0,20},0) && !camera.visible({21,0,20},0) && camera.visible({21,0,20},2) && !camera.visible({0,21,20},0),"camera side sphere clipping");
    require(!camera.visible({0,0,-20},10),"camera accepted sphere wholly behind it");
    auto rotation=identity; rotation.m_Quat[1]=std::sqrt(.5f); rotation.m_Quat[3]=std::sqrt(.5f);
    nolf::CameraView rotated({10,20,30},rotation,{0,0,640,480},fov,fov,7,1000);
    require(near(rotated.pixel_point(320,240),{11,20,30}) && rotated.visible({30,20,30},0) && !rotated.visible({10,20,50},0),"rotated camera ray/frustum");
    for (float& value : rotation.m_Quat) value*=7;
    nolf::CameraView scaled({10,20,30},rotation,{0,0,640,480},fov,fov);
    require(near(scaled.pixel_point(0,0),rotated.pixel_point(0,0)),"nonunit quaternion changed camera orientation");
    for (float bad : {0.f,-1.f,std::numbers::pi_v<float>,std::numeric_limits<float>::quiet_NaN()}) rejects([&] { nolf::CameraView invalid({0,0,0},identity,{0,0,640,480},bad,fov); });
    rejects([&] { nolf::CameraView invalid({0,0,0},identity,{0,0,0,480},fov,fov); });
    rejects([&] { nolf::CameraView invalid({0,0,0},identity,{std::numeric_limits<int>::min(),0,std::numeric_limits<int>::max(),1},fov,fov); });
    rejects([&] { nolf::CameraView invalid({0,0,0},identity,{0,0,640,480},fov,fov,7,7); });
    rejects([&] { camera.visible({0,0,0},-1); });
    rejects([&] { camera.visible({std::numeric_limits<float>::quiet_NaN(),0,0},1); });
}
void sdk() {
    nolf::ClientScene scene; ObjectCreateStruct input; input.m_ObjectType=OT_CAMERA; HOBJECT camera=nullptr;
    require(scene.create(input,camera)==LT_OK && scene.viewport(1280,720)==LT_OK,"SDK camera setup");
    auto settings=scene.camera(camera); settings->fov_x=settings->fov_y=std::numbers::pi_v<float>/2;
    require(scene.camera(camera,*settings)==LT_OK,"SDK camera FOV update");
    auto active=scene.activate(); const auto& api=nolf::ClientScene::functions(); LTVector point;
    require(api.Get3DCameraPt(camera,0,0,&point)==LT_OK && near(point,{-4.f/3,1,1}),"SDK widescreen ray disagrees with renderer camera");
    LTRotation rotation; rotation.Init(); rotation.m_Quat[1]=std::sqrt(.5f); rotation.m_Quat[3]=std::sqrt(.5f);
    require(scene.rotation(camera,rotation)==LT_OK && scene.position(camera,{10,20,30},true)==LT_OK && api.Get3DCameraPt(camera,640,360,&point)==LT_OK && near(point,{11,20,30}),"SDK transformed camera ray");
    require(api.Get3DCameraPt(camera,1280,360,&point)==LT_OUTSIDE && near(point,{0,0,0}),"SDK camera half-open ray bounds");
}
}
int main() { try { projection(); sdk(); std::cout<<"Native camera projection, frustum and SDK pixel rays passed\n"; return 0; } catch (const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; } }
