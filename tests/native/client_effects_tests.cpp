#include "engine/client/client_scene.h"
#include "engine/client/client_effects.h"
#include "engine/client/effect_geometry.h"
#include "engine/server/objtypes.h"
#include <cmath>
#include <future>
#include <iostream>
#include <limits>

namespace {
void require(bool v,const char* why) { if (!v) throw std::runtime_error(why); }
void near(float a,float b) { require(std::fabs(a-b)<1e-5f,"effect simulation differs"); }
template<class F> void rejects(F&& f) { bool failed=false; try { f(); } catch (const std::exception&) { failed=true; } require(failed,"invalid effect request accepted"); }
HOBJECT create(nolf::ClientScene& scene,uint16 type) { ObjectCreateStruct input; input.m_ObjectType=type; HOBJECT object=nullptr; require(scene.create(input,object)==LT_OK,"effect object creation failed"); return object; }
void checks() {
    nolf::ClientScene scene,other; const auto active=scene.activate(); const auto& api=nolf::ClientScene::functions(); auto& effects=scene.effects();
    const auto system=create(scene,OT_PARTICLESYSTEM),second=create(scene,OT_PARTICLESYSTEM),lines=create(scene,OT_LINESYSTEM),foreign=create(other,OT_PARTICLESYSTEM);
    char empty[]=""; require(api.SetupParticleSystem(system,empty,-8,0,2)==LT_OK,"particle system configuration failed");
    LTParticle *head=nullptr,*tail=nullptr; require(api.GetParticles(system,&head,&tail) && head==tail && tail,"empty sentinel contract differs");
    auto* first=effects.add_particle(system,{1,2,3},{2,4,6},{255,128,0},1); auto* last=effects.add_particle(system,{0,0,0},{0,0,0},{0,255,0},2);
    require(api.GetParticles(system,&head,&tail) && head==first && head->m_pNext==last && last->m_pNext==tail,"particle append order/sentinel differs");
    float lifetime=0; require(api.GetParticleTotalLifetime(system,first,lifetime)==LT_OK,"particle total lifetime SDK failed"); near(lifetime,1); near(first->m_Size,2);
    require(api.GetParticleLifetime(second,first,lifetime)==LT_INVALIDPARAMS && lifetime==0,"foreign-system particle was dereferenced");
    require(api.GetParticleLifetime(foreign,first,lifetime)==LT_INVALIDPARAMS,"foreign-scene particle was dereferenced");
    require(scene.update(.5f)==LT_OK,"particle timestep failed"); auto position=effects.particle_position(system,first); near(position.x,2); near(position.y,4); near(position.z,6); near(first->m_Velocity.y,0); near(effects.particle_lifetime(system,first,false),.5f); near(effects.particle_lifetime(system,first,true),1);
    require(scene.update(.5f)==LT_OK && effects.snapshot_particles(system).particles.size()==2,"zero remaining lifetime was removed too early");
    require(scene.update(.01f)==LT_OK && effects.snapshot_particles(system).particles.size()==1,"expired particle retained"); require(api.GetParticleLifetime(system,first,lifetime)==LT_INVALIDPARAMS,"removed particle dereferenced");
    api.RemoveParticle(second,last); require(effects.snapshot_particles(system).particles.size()==1,"foreign system removed particle"); api.RemoveParticle(system,last); require(api.GetParticles(system,&head,&tail) && head==tail,"last particle removal corrupted sentinel");
    require(api.SetupParticleSystem(system,empty,-10,PS_NEVERDIE,3)==LT_OK,"never-die configuration failed"); first=effects.add_particle(system,{0,0,0},{0,-20,0},{255,255,255},0);
    effects.update(.5f,500,{}); near(effects.particle_lifetime(system,first,false),0); near(effects.particle_position(system,first).y,-10);
    require(api.SetupParticleSystem(system,empty,-10,PS_DUMB,3)==LT_OK,"dumb configuration failed"); effects.update(.5f,500,{}); near(effects.particle_position(system,first).y,-10); near(first->m_Velocity.y,-25);
    require(api.SetupParticleSystem(system,empty,0,PS_NEVERDIE|PS_BOUNCE,3)==LT_OK,"bounce configuration failed"); effects.update(.1f,100,[](LTVector) { return std::optional<float>(-5); }); near(effects.particle_position(system,first).y,-5); near(first->m_Velocity.y,12.5f);
    effects.add_particles(system,200,{-1,-2,-3},{1,2,3},{-4,-5,-6},{4,5,6},{10,20,30},{40,50,60},1,2);
    require(effects.snapshot_particles(system).particles.size()==201,"batch particle generation lost records"); api.OptimizeParticles(system); auto* native=static_cast<CObjParticleSystem*>(system); require(native->m_vBoxMin.y<=-5 && native->m_vBoxMax.y<=2,"optimized particle bounds incorrect");
    rejects([&] { effects.add_particles(system,262145,{0,0,0},{0,0,0},{0,0,0},{0,0,0},{0,0,0},{0,0,0},0,1); });
    require(api.SetupParticleSystem(system,empty,std::numeric_limits<float>::quiet_NaN(),0,1)==LT_INVALIDPARAMS,"nonfinite gravity accepted");
    LTLine line{}; line.m_Points[0]={{1,2,3},1,0,0,.5f}; line.m_Points[1]={{4,5,6},0,1,0,1};
    const auto a=api.AddLine(lines,&line),b=api.AddLine(lines,&line); require(a && b && a!=b && api.GetNextLine(lines,nullptr)==a && api.GetNextLine(lines,a)==b && !api.GetNextLine(lines,b),"line insertion/iteration differs");
    LTLine copy{}; api.GetLineInfo(a,&copy); near(copy.m_Points[0].m_Pos.y,2); copy.m_Points[0].a=.25f; api.SetLineInfo(a,&copy); near(effects.snapshot_lines(lines)[0].m_Points[0].a,.25f);
    api.RemoveLine(system,a); require(api.GetNextLine(lines,nullptr)==a,"wrong object removed a line"); api.RemoveLine(lines,a); require(api.GetNextLine(lines,nullptr)==b,"line head removal corrupted iteration"); require(!api.GetNextLine(lines,a),"removed line used for iteration");
    const auto snapshot=effects.snapshot_lines(lines); require(scene.remove(lines)==LT_OK,"line-system removal failed"); rejects([&] { effects.line(b); }); near(snapshot[0].m_Points[0].m_Pos.x,1);
    auto wrong_thread=std::async(std::launch::async,[&] { effects.snapshot_particles(system); }); rejects([&] { wrong_thread.get(); });
    const auto retained=effects.snapshot_particles(system); require(scene.remove(system)==LT_OK,"particle system removal failed"); rejects([&] { effects.particle_position(system,first); }); require(retained.particles.size()==201,"owned particle snapshot did not survive destruction");
    require(!api.AddLine(reinterpret_cast<HOBJECT>(1),&line),"unknown object handle dereferenced");
}
void geometry() {
    CObjParticleSystem object; object.m_Pos.Init(10,20,30); object.m_Scale.Init(2,3,4); object.m_Rotation.Init(0,0,std::sqrt(.5f),std::sqrt(.5f));
    object.m_ColorR=128; object.m_ColorA=128;
    nolf::CameraView camera({4,22,0},{0,0,0,1},{0,0,1920,1080},1.5707964f,1.5707964f);
    nolf::ParticleSnapshot source; source.particles.push_back({{1,2,3},{255,128,64},.5f,3});
    const auto vertices=nolf::particle_vertices(source,object,camera,16,32,7,{.5f,1,1}); require(vertices.size()==4,"particle geometry lost quad");
    near((vertices[0].position.x+vertices[2].position.x)*.5f,0); near((vertices[0].position.y+vertices[2].position.y)*.5f,0); near(vertices[0].position.z,42); near(vertices[0].position.x,-6); near(vertices[0].u,.125f); near(vertices[0].v,.0625f);
    require(vertices[0].color==std::array<uint8,4>{64,128,64,64},"particle color/alpha/global scale differs");
    const auto dx=vertices[1].position.x-vertices[0].position.x,dy=vertices[0].position.y-vertices[3].position.y;
    near(dx*camera.projection()[0]*1920,dy*camera.projection()[5]*1080);
    source.particles[0].size=0; require(nolf::particle_vertices(source,object,camera,0,0,7,{1,1,1}).empty(),"zero-size particle generated geometry"); source.particles[0].size=3;
    require(nolf::particle_vertices(source,object,camera,0,0,43,{1,1,1}).empty(),"near plane did not reject particle center");
    LTLine line{}; line.m_Points[0]={{1,2,3},1,.5f,0,.5f}; line.m_Points[1]={{0,0,0},0,1,0,1}; const auto lines=nolf::line_vertices(std::span(&line,1),object,camera,{1,1,1});
    require(lines.size()==2 && lines[0].color==std::array<uint8,4>{255,127,0,64},"line endpoint color inherited object RGB"); near(lines[0].position.x,0); near(lines[0].position.y,0); near(lines[0].position.z,42); near(lines[1].position.x,6); near(lines[1].position.y,-2); near(lines[1].position.z,30);
    source.particles[0].size=std::numeric_limits<float>::infinity(); rejects([&] { nolf::particle_vertices(source,object,camera,0,0,7,{1,1,1}); });
}
}
int main() { try { checks(); geometry(); std::cout<<"Native particle simulation, lifetimes, sentinel iteration, bounds, lines, geometry and owned effect snapshots passed\n"; return 0; } catch (const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; } }
