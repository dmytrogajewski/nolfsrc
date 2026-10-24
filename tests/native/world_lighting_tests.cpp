#include "engine/server/world_lighting.h"
#include "engine/portable/assets.h"
#include "engine/portable/asset_stream.h"
#include "engine/portable/memory_stream.h"
#include "engine/portable/wire.h"
#include <array>
#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

namespace {
using Bytes = std::vector<std::byte>;
void require(bool value, const char* text) { if (!value) throw std::runtime_error(text); }
template<class T> void append(Bytes& bytes, T value) {
    auto offset = bytes.size(); bytes.resize(offset + sizeof(T));
    nolf::wire::store(bytes.data() + offset, value);
}
void name(Bytes& bytes, const char* text) {
    append(bytes, static_cast<uint16>(std::strlen(text)));
    for (; *text; ++text) bytes.push_back(std::byte(*text));
}
void entry(Bytes& bytes, uint32 size) {
    append(bytes, size);
    for (uint32 i = 0; i < size; ++i) append(bytes, uint8(i + 1));
}
Bytes fixture() {
    Bytes bytes;
    for (uint32 count : {3u,3u,7u,5u,2u}) append(bytes, count);
    name(bytes, "LightAnim_BASE");
    for (uint32 count : {1u,2u,2u}) append(bytes, count);
    for (uint16 value : {uint16(2),uint16(400),uint16(5),uint16(600)}) append(bytes, value);
    entry(bytes, 1); entry(bytes, 2); entry(bytes, 0); entry(bytes, 3);
    name(bytes, "Door");
    for (uint32 count : {0u,1u,1u}) append(bytes, count);
    append(bytes, uint16(9)); append(bytes, uint16(1000)); entry(bytes, 1);
    return bytes;
}
LTRESULT load(CWorldData& data, Bytes bytes) {
    nolf::MemoryStream stream(std::move(bytes));
    return data.LoadObjects(&stream);
}
void checks() {
    CWorldData data;
    const auto bytes = fixture();
    require(load(data, bytes) == LT_OK, "load native light-animation pools");
    uint32 index = 99;
    auto* base = data.FindLightAnim("LIGHTANIM_base", &index);
    auto* door = data.FindLightAnim("door");
    require(base && index == 0 && door && (data.m_Flags180 & WD_HAS_BASEANIM), "case-insensitive animation lookup and base flag");
    require(base->m_bShadowMap && base->m_nFrames == 2 && base->m_nLights == 2 &&
        base->m_iFrames[0] == 0 && base->m_iFrames[1] == 0 && base->m_fBlendPercent == 1 &&
        door->m_iFrames[0] == std::numeric_limits<uint32>::max(), "runtime state uses complete native records");
    require(base->m_pLights[1].m_n02 == 600 && base->m_ppFrames[0][1].m_nSize == 2 &&
        base->m_ppFrames[1][0].m_nSize == 0 && !base->m_ppFrames[1][0].m_pData &&
        static_cast<uint8*>(door->m_ppFrames[0][0].m_pData)[0] == 1, "pooled frame and light spans point to decoded data");
    for (std::size_t size = 0; size < bytes.size(); ++size) {
        require(load(data, Bytes(bytes.begin(), bytes.begin() + size)) != LT_OK &&
            data.FindLightAnim("Door") == door && door->m_pLights[0].m_n02 == 1000,
            "truncated blocks preserve all live lighting storage");
    }
    for (std::size_t offset : {0u,4u,8u,12u,16u,39u,43u}) {
        auto corrupt = bytes;
        nolf::wire::store(corrupt.data() + offset, std::numeric_limits<uint32>::max());
        require(load(data, std::move(corrupt)) != LT_OK && data.FindLightAnim("Door") == door,
            "overflowing declared pools and animation spans are rejected before replacing live data");
    }
    data.m_nObjectsOffset = 10000;
    require(load(data, bytes) == LT_ERROR && data.FindLightAnim("Door") == door, "invalid light block offset preserves storage");
    data.m_nObjectsOffset = 9;
    auto prefixed = Bytes(9); prefixed.insert(prefixed.end(), bytes.begin(), bytes.end());
    require(load(data, std::move(prefixed)) == LT_OK, "world header's absolute light offset is honored");
    data.m_nObjectsOffset = 0;
    Bytes empty(20);
    require(load(data, empty) == LT_OK && !data.FindLightAnim("Door") && !(data.m_Flags180 & WD_HAS_BASEANIM), "empty lighting replaces and releases previous pools");
    data.TermArrays(); data.TermArrays();
    require(data.LoadObjects(nullptr) == LT_INVALIDPARAMS, "missing lighting stream reports invalid parameters");
}
void real_worlds(const std::filesystem::path& path) {
    nolf::Assets assets; assets.mount_game(path);
    std::size_t worlds = 0, anims = 0, frames = 0;
    for (const auto& filename : assets.names()) {
        if (!filename.starts_with("worlds/") || !filename.ends_with(".dat")) continue;
        nolf::AssetStream stream(assets.locate(filename));
        std::array<std::byte, 12> header{};
        require(stream.Read(header.data(), header.size()) == LT_OK && nolf::wire::load<uint32>(header.data()) == 66,
            "real world has supported version 66 header");
        CWorldData lighting;
        lighting.m_nObjectsOffset = nolf::wire::load<uint32>(header.data() + 8);
        if (lighting.LoadObjects(&stream) != LT_OK) throw std::runtime_error("light-animation block failed: " + filename);
        ++worlds; anims += lighting.m_Anims.GetSize(); frames += lighting.m_Frames.GetSize();
    }
    require(worlds != 0, "no real world assets were validated");
    std::cout << "decoded lighting in " << worlds << " original worlds: " << anims << " animations, " << frames << " frames\n";
}
}
int main(int argc, char** argv) {
    try {
        checks();
        if (argc == 3 && std::string_view(argv[1]) == "--data") real_worlds(argv[2]);
        std::cout << "native world lighting decoding, bounds, ownership and reload passed\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
