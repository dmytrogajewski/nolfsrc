#pragma once
#include <array>
#include <bit>
#include <cstdint>
#include <cstddef>
#include <string_view>
#include <vector>
namespace nolf_test {
struct Bytes : std::vector<std::byte> {
    void integer(std::uint32_t value,unsigned width = 4) { for (unsigned i = 0; i < width; ++i) push_back(std::byte((value >> (i*8)) & 255)); }
    void scalar(float value) { integer(std::bit_cast<std::uint32_t>(value)); }
    void string(std::string_view value) { integer(static_cast<std::uint32_t>(value.size()),2); for (auto c : value) push_back(std::byte(c)); }
    void pose(float x) { scalar(x); scalar(0); scalar(0); scalar(0); scalar(0); scalar(0); scalar(1); }
};
inline Bytes tiny_model(std::string_view child = {},std::string_view start_key = "start",bool mesh=false) {
    std::array<Bytes,7> chunks;
    auto& header = chunks[0]; header.integer(12);
    for (unsigned i = 0; i < 13; ++i) header.integer(0);
    header.string(""); header.scalar(50); header.integer(0,2); header.resize(header.size()+62);
    auto& pieces=chunks[1]; pieces.integer(mesh ? 3 : 0); pieces.integer(mesh ? 1 : 0);
    if (mesh) {
        pieces.integer(0,2); pieces.scalar(5); pieces.scalar(1); pieces.integer(0); pieces.integer(0,2); pieces.string("triangle");
        pieces.integer(1);
        for (unsigned i=0;i<3;++i) { pieces.scalar(i==1 ? 1 : 0); pieces.scalar(i==2 ? 1 : 0); pieces.integer(i,2); }
        pieces.integer(3);
        constexpr float positions[3][3]{{-1,0,10},{1,0,10},{0,1,10}};
        for (unsigned i=0;i<3;++i) {
            pieces.integer(i==2 ? 0 : i+1,2); pieces.integer(0xffff,2);
            for (unsigned j=0;i!=2 && j<=i;++j) {
                const float weight=i==0 ? 1 : j==0 ? .25f : .75f;
                pieces.integer(0); for (float coordinate : positions[i]) pieces.scalar(coordinate*weight); pieces.scalar(weight);
            }
            for (float coordinate : positions[i]) pieces.scalar(coordinate);
            pieces.scalar(0); pieces.scalar(0); pieces.scalar(1);
        }
    }
    auto& nodes = chunks[2]; nodes.string("root"); nodes.integer(0,2); nodes.integer(0,1);
    for (unsigned i = 0; i < 16; ++i) nodes.scalar(i % 5 == 0 ? 1.f : 0.f);
    nodes.integer(0); nodes.integer(0);
    auto& children = chunks[3]; children.integer(child.empty() ? 1 : 2); children.integer(0); children.pose(0);
    if (!child.empty()) { children.string(child); children.integer(0); children.pose(0); }
    auto& anim = chunks[4]; anim.integer(1); anim.scalar(1); anim.scalar(1); anim.scalar(1);
    anim.string("idle"); anim.integer(UINT32_MAX); anim.integer(200); anim.integer(2);
    anim.integer(0); anim.string(start_key); anim.integer(100); anim.string("end"); anim.pose(1); anim.pose(2);
    auto& bindings = chunks[5]; bindings.integer(1); bindings.string("idle");
    for (unsigned i = 0; i < 6; ++i) bindings.scalar(0);
    if (!child.empty()) { bindings.integer(1); bindings.string("idle"); for (unsigned i = 0; i < 6; ++i) bindings.scalar(0); }
    chunks[6].integer(0);
    constexpr std::string_view names[] = {"Header","Pieces","Nodes","ChildModels","Animation","AnimBindings","Sockets"};
    Bytes file;
    for (unsigned i = 0; i < 7; ++i) {
        file.string(names[i]);
        file.integer(i == 6 ? UINT32_MAX : static_cast<std::uint32_t>(file.size()+4+chunks[i].size()));
        file.insert(file.end(),chunks[i].begin(),chunks[i].end());
    }
    return file;
}
}
