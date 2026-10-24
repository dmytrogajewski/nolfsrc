#include "engine/portable/sky.h"
#include "engine/portable/wire.h"
#include "engine/client/client_scene.h"
#include "engine/client/client_session.h"
#include <cmath>
#include <algorithm>
#include <iostream>
#include <limits>

namespace {
void require(bool value,const char* why) { if (!value) throw std::runtime_error(why); }
template<class F> void rejects(F&& function) { bool failed=false; try { function(); } catch (const std::exception&) { failed=true; } require(failed,"invalid sky operation accepted"); }
void near(float actual,float expected) { require(std::abs(actual-expected)<.0001f,"sky camera parallax differs"); }
nolf::SkyInfo fixture() {
    nolf::SkyInfo sky; sky.definition={{-10,-20,-30},{10,20,30},{1,2,3},{5,10,15}}; sky.objects[0]=0x1234; sky.objects[2]=42; return sky;
}
void codec() {
    auto sky=fixture(); const auto bytes=nolf::encode_sky(sky);
    require(bytes.size()==110 && bytes[0]==std::byte{0} && bytes[2]==std::byte{0x20} && bytes[3]==std::byte{0xc1} &&
        bytes[48]==std::byte{30} && bytes[49]==std::byte{0} && bytes[50]==std::byte{0x34} && bytes[51]==std::byte{0x12} &&
        bytes[52]==std::byte{0xff} && bytes[53]==std::byte{0xff},"sky packet no longer matches original float32/count/ID layout");
    const auto decoded=nolf::decode_sky(bytes);
    require(decoded.objects==sky.objects && decoded.definition.m_Min.z==-30 && decoded.definition.m_ViewMax.y==10,"sky fields or slot order changed");
    require(nolf::encode_sky(decoded)==bytes,"sky packet round trip changed original bytes");
    for (std::size_t size=0;size<bytes.size();++size) rejects([&] { nolf::decode_sky(std::span(bytes).first(size)); });
    auto bad=bytes; bad.push_back(std::byte{0}); rejects([&] { nolf::decode_sky(bad); });
    bad=bytes; nolf::wire::store(bad.data()+48,uint16(31)); rejects([&] { nolf::decode_sky(bad); });
    bad=bytes; nolf::wire::store(bad.data()+12,uint32(0x7fc00000)); rejects([&] { nolf::decode_sky(bad); });
    bad.assign(bytes.begin(),bytes.begin()+50); nolf::wire::store(bad.data()+48,uint16(0)); const auto empty=nolf::decode_sky(bad);
    require(std::all_of(empty.objects.begin(),empty.objects.end(),[](auto id) { return id==0xffff; }),"short sky ID list left uninitialized slots");
    sky.definition.m_ViewMin.x=std::numeric_limits<float>::infinity(); require(!nolf::valid_sky(sky.definition),"nonfinite sky definition valid"); rejects([&] { nolf::encode_sky(sky); });
    sky=fixture(); const auto a=nolf::sky_view_position(sky.definition,{-100,-200,-300},{100,200,300},{0,0,0}); near(a.x,3); near(a.y,6); near(a.z,9);
    const auto b=nolf::sky_view_position(sky.definition,{-100,-200,-300},{100,200,300},{100,-200,150}); near(b.x,5); near(b.y,2); near(b.z,12);
    const auto flat=nolf::sky_view_position(sky.definition,{-100,0,-300},{100,0,300},{100,0,150}); near(flat.x,3); near(flat.y,6); near(flat.z,9);
    rejects([&] { nolf::sky_view_position(sky.definition,{1,0,0},{0,1,1},{0,0,0}); });
}
void scene() {
    nolf::ClientScene scene; auto sky=fixture(); require(scene.sky(sky)==LT_OK && scene.sky_objects().empty(),"unresolved sky IDs were dereferenced or rejected");
    ObjectCreateStruct create; HOBJECT object=nullptr; require(scene.create(create,object)==LT_OK && scene.server_id(object,42)==LT_OK,"late sky object assignment failed");
    auto objects=scene.sky_objects(); require(objects.size()==1 && objects[0].object==object && objects[0].guard.unchanged(),"sky list did not resolve late server IDs");
    { const auto active=scene.activate(); SkyDef output; require(nolf::ClientScene::functions().GetSkyDef(&output)==LT_OK && output.m_Min.x==-10 && output.m_ViewMax.z==15,"SDK sky definition binding differs"); require(nolf::ClientScene::functions().GetSkyDef(nullptr)==LT_INVALIDPARAMS,"SDK sky definition accepted null output"); }
    auto invalid=sky; invalid.definition.m_Min.x=std::numeric_limits<float>::quiet_NaN(); require(scene.sky(invalid)==LT_INVALIDPARAMS && scene.sky().objects==sky.objects,"invalid sky update partially replaced scene state");
    require(scene.remove(object)==LT_OK && !objects[0].guard.alive() && scene.sky_objects().empty(),"removed sky object remained in a resolved list");
    HOBJECT replacement=nullptr; require(scene.create(create,replacement)==LT_OK && scene.server_id(replacement,42)==LT_OK && scene.sky_objects().empty(),"recycled server ID hijacked an old sky slot");
    require(scene.sky(sky)==LT_OK && scene.sky_objects().size()==1,"new sky message did not explicitly rebind a reused ID");
    require(scene.server_id(replacement,43)==LT_OK && scene.sky_objects().empty(),"changing an object's server ID retained its old sky slot");
    require(scene.unload_world()==LT_OK,"sky world unload failed"); const auto cleared=scene.sky();
    require(std::all_of(cleared.objects.begin(),cleared.objects.end(),[](auto id) { return id==0xffff; }),"world transition retained sky IDs");
}
void protocol() {
    auto assets=std::make_shared<nolf::Assets>(); nolf::ClientSession session(assets); session.begin({}); auto bytes=nolf::encode_sky(fixture()); bytes.insert(bytes.begin(),std::byte{0x11});
    rejects([&] { session.receive(bytes); });
    session.receive(std::array<std::byte,5>{std::byte{4},std::byte{6},std::byte{0},std::byte{0},std::byte{0}});
    session.receive(std::array<std::byte,4>{std::byte{12},std::byte{1},std::byte{0},std::byte{0}}); session.take_events();
    session.receive(bytes); const auto events=session.take_events();
    require(events.size()==1 && events[0].kind==nolf::ClientEvent::Kind::Sky && events[0].sky && events[0].sky->objects[0]==0x1234,"client session did not publish owned typed sky state");
    bytes.pop_back(); rejects([&] { session.receive(bytes); }); require(session.take_events().empty(),"malformed sky packet published a partial event");
}
}
int main() { try { codec(); scene(); protocol(); std::cout<<"Native sky wire layout, parallax, late object identities, SDK binding and typed session events passed\n"; return 0; } catch (const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; } }
