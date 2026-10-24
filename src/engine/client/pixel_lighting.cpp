#include "engine/client/pixel_lighting.h"
#include "rdev.h"
#include <algorithm>
#include <array>
#include <cmath>

namespace nolf {
namespace {
constexpr const char* vertex_source=R"(#version 120
varying vec3 v_world;
varying vec3 v_normal;
void main() {
    v_world=gl_Vertex.xyz; v_normal=gl_Normal;
    gl_TexCoord[0]=gl_MultiTexCoord0;
    vec4 eye=gl_ModelViewMatrix*gl_Vertex;
    gl_FogFragCoord=abs(eye.z);
    gl_Position=gl_ProjectionMatrix*eye;
}
)";
constexpr const char* fragment_source=R"(#version 120
uniform sampler2D u_texture;
uniform float u_textured;
uniform vec3 u_ambient;
uniform vec4 u_sun;
uniform vec3 u_sun_color;
uniform int u_count;
uniform vec4 u_lights[16];
uniform vec4 u_colors[16];
uniform vec3 u_scale;
uniform vec3 u_add;
uniform float u_alpha;
uniform float u_fog;
varying vec3 v_world;
varying vec3 v_normal;
void main() {
    vec3 n=normalize(v_normal);
    vec3 c=u_ambient;
    if (u_sun.w>0.0) { float d=dot(n,u_sun.xyz)*u_sun.w; if (d>0.0) c+=(u_sun_color-c)*d; }
    for (int i=0;i<16;++i) {
        if (i>=u_count) break;
        vec3 l=u_lights[i].xyz-v_world; float d2=dot(l,l); float r=u_lights[i].w;
        if (d2>=r*r) continue;
        float facing=dot(n,l*inversesqrt(max(d2,0.0001)));
        if (u_colors[i].a>0.5) {
            float f=1.0-sqrt(d2)/r; float w=0.5*facing+0.5;
            c+=u_colors[i].rgb*(f*f*(0.5+0.5*w*w));
        } else if (facing>0.0) c+=u_colors[i].rgb*(facing*(1.0-d2/(r*r)));
    }
    vec4 color=vec4(clamp(c*u_scale+u_add,0.0,1.0),u_alpha);
    if (u_textured>0.5) color*=texture2D(u_texture,gl_TexCoord[0].st);
    if (u_fog>0.5) color.rgb=mix(gl_Fog.color.rgb,color.rgb,clamp((gl_Fog.end-gl_FogFragCoord)*gl_Fog.scale,0.0,1.0));
    gl_FragColor=color;
}
)";
}
std::vector<LTVector> model_SmoothNormals(const ModelPieceData& mesh,std::span<const LTVector> positions,const LTMatrix& object) {
    std::vector<LTVector> normals(positions.size(),LTVector(0,0,0));
    const auto vertices=mesh.vertices();
    const auto rotate=[&](LTVector v) {
        return LTVector(object.m[0][0]*v.x+object.m[0][1]*v.y+object.m[0][2]*v.z,object.m[1][0]*v.x+object.m[1][1]*v.y+object.m[1][2]*v.z,object.m[2][0]*v.x+object.m[2][1]*v.y+object.m[2][2]*v.z);
    };
    for (const auto& triangle : mesh.triangles()) {
        const auto a=triangle.indices[0],b=triangle.indices[1],c=triangle.indices[2];
        if (a>=positions.size() || b>=positions.size() || c>=positions.size()) continue;
        const auto u=positions[b]-positions[a],v=positions[c]-positions[a];
        auto face=LTVector(u.y*v.z-u.z*v.y,u.z*v.x-u.x*v.z,u.x*v.y-u.y*v.x);
        if (a<vertices.size() && b<vertices.size() && c<vertices.size()) {
            const auto reference=rotate(vertices[a].normal+vertices[b].normal+vertices[c].normal);
            if (face.Dot(reference)<0) face=face*-1.f;
        }
        normals[a]+=face; normals[b]+=face; normals[c]+=face;
    }
    for (std::size_t i=0;i<normals.size();++i) {
        const auto length=normals[i].Mag();
        if (length>0) normals[i]/=length;
        else if (i<vertices.size()) { normals[i]=rotate(vertices[i].normal); const auto l=normals[i].Mag(); if (l>0) normals[i]/=l; }
    }
    return normals;
}
PixelLightingProgram::PixelLightingProgram() : program_("pixel lighting",vertex_source,fragment_source) {}
bool PixelLightingProgram::use(const PixelLighting& lighting,bool textured,bool fog) {
    if (!program_.use()) return false;
    auto& p=program_;
    rd_Uniform1i(p.uniform("u_texture"),0);
    rd_Uniform1f(p.uniform("u_textured"),textured ? 1.f : 0.f);
    rd_Uniform3f(p.uniform("u_ambient"),lighting.ambient.x,lighting.ambient.y,lighting.ambient.z);
    rd_Uniform4f(p.uniform("u_sun"),lighting.to_sun.x,lighting.to_sun.y,lighting.to_sun.z,lighting.sun_amount);
    rd_Uniform3f(p.uniform("u_sun_color"),lighting.sun_color.x,lighting.sun_color.y,lighting.sun_color.z);
    const auto count=std::min(lighting.lights.size(),PixelLighting::max_lights);
    std::array<float,PixelLighting::max_lights*4> positions{},colors{};
    for (std::size_t i=0;i<count;++i) {
        const auto& light=lighting.lights[i];
        positions[i*4]=light.position.x; positions[i*4+1]=light.position.y; positions[i*4+2]=light.position.z; positions[i*4+3]=light.radius;
        colors[i*4]=light.color.x; colors[i*4+1]=light.color.y; colors[i*4+2]=light.color.z; colors[i*4+3]=light.grid ? 1.f : 0.f;
    }
    rd_Uniform1i(p.uniform("u_count"),int(count));
    if (count) { rd_Uniform4fv(p.uniform("u_lights"),int(count),positions.data()); rd_Uniform4fv(p.uniform("u_colors"),int(count),colors.data()); }
    rd_Uniform3f(p.uniform("u_scale"),lighting.scale.x,lighting.scale.y,lighting.scale.z);
    rd_Uniform3f(p.uniform("u_add"),lighting.add.x/255,lighting.add.y/255,lighting.add.z/255);
    rd_Uniform1f(p.uniform("u_alpha"),lighting.alpha);
    rd_Uniform1f(p.uniform("u_fog"),fog ? 1.f : 0.f);
    return true;
}
}
