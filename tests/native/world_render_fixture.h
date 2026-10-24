#pragma once
#include "engine/portable/binary.h"
#include "engine/portable/wire.h"
#include "world_lightmap_fixture.h"
namespace nolf_test {
inline std::vector<std::byte> render_world_fixture(bool with_brush=false) {
    const auto build_model=[](std::string_view name,uint32 flags,bool lightmapped) {
    nolf::BinaryWriter model;
    auto word=[&](uint32 value) { model.integer(value); };
    auto vec=[&](LTVector value) { model.real(value.x); model.real(value.y); model.real(value.z); };
    auto text=[&](std::string_view value) { model.integer(uint16(value.size())); model.bytes(std::as_bytes(std::span(value.data(),value.size()))); };
    for (unsigned i=0;i<8;++i) word(0);
    word(flags); word(0); text(name);
    for (unsigned value : {4u,1u,1u,0u,1u,1u,0u,1u,1u,1u,1u,lightmapped ? 3u : 0u}) word(value);
    vec({-10,-10,10}); vec({10,10,10}); vec({0,0,10});
    const std::string texture="color.dtx"; word(texture.size()+1); word(1);
    model.bytes(std::as_bytes(std::span(texture.c_str(),texture.size()+1)));
    model.integer(uint8(4)); model.integer(uint8(0));
    model.integer(uint16(1)); model.integer(uint16(0)); model.integer(uint16(1)); model.integer(uint8(1)); // PVS includes group zero.
    word(1); model.integer(uint16(0)); model.integer(uint16(0)); word(0);
    vec({0,0,-1}); model.real(-10);
    vec({0,0,10}); vec({1,0,0}); vec({0,1,0}); model.integer(uint16(0)); word(0); word(lightmapped ? 0x80 : 0);
    for (unsigned i=0;i<5;++i) model.integer(uint8(0));
    model.integer(uint16(0));
    vec({-10,-10,10}); model.integer(uint16(2)); model.integer(uint16(1)); model.integer(uint16(lightmapped ? 3 : 0));
    for (uint16 animation=0;lightmapped && animation<3;++animation) { model.integer(animation); model.integer(uint16(0)); }
    word(0);
    for (uint16 i=0;i<4;++i) { model.integer(i); model.integer(uint8(128)); model.integer(uint8(255)); model.integer(uint8(64)); }
    word(0); model.integer(uint16(0)); word(0xffffffff); word(0xfffffffe);
    for (auto point : {LTVector(-10,-10,10),LTVector(10,-10,10),LTVector(10,10,10),LTVector(-10,10,10)}) { vec(point); vec({0,0,0}); }
    word(0); word(0); word(0); vec({1,1,1}); vec({0,0,0}); word(0); word(0);
    return model.data();
    };
    nolf::BinaryWriter file;
    file.integer(uint32(66)); for (unsigned i=0;i<10;++i) file.integer(uint32(0));
    file.integer(uint32(0)); file.real(20);
    for (unsigned i=0;i<2;++i) for (float value : {-12.f,-12.f,0.f,12.f,12.f,20.f}) file.real(value);
    file.integer(uint32(1)); file.integer(uint32(0)); file.integer(uint8(0)); file.integer(uint32(with_brush ? 2 : 1));
    auto bytes=file.data();
    const auto append_model=[&](const std::vector<std::byte>& model) {
        const auto next=bytes.size(); bytes.resize(next+4); bytes.insert(bytes.end(),model.begin(),model.end());
        nolf::wire::store(bytes.data()+next,uint32(bytes.size()));
    };
    append_model(build_model("Main",4|0x10|0x20,true));
    if (with_brush) append_model(build_model("Door",2,false));
    nolf::wire::store(bytes.data()+4,uint32(bytes.size()));
    const auto objects=bytes.size(); bytes.resize(objects+4); nolf::wire::store(bytes.data()+objects,uint32(0));
    nolf::wire::store(bytes.data()+8,uint32(bytes.size()));
    const auto lights=lightmap_fixture(); bytes.insert(bytes.end(),lights.begin(),lights.end()); return bytes;
}
}
