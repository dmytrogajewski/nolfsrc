#pragma once
#include "engine/client/world_lightmaps.h"
#include "engine/portable/binary.h"
#include "engine/portable/memory_stream.h"
#include <stdexcept>
namespace nolf_test {
inline std::vector<std::byte> lightmap_fixture(bool broken=false) {
    nolf::BinaryWriter file;
    for (std::uint32_t value : {3u,4u,broken ? 8u : 11u,4u,3u}) file.integer(value);
    auto name=[&](std::string_view value) { file.integer(static_cast<std::uint16_t>(value.size())); file.bytes(std::as_bytes(std::span(value.data(),value.size()))); };
    auto header=[&](std::string_view value,unsigned shadow,unsigned frames) { name(value); file.integer<std::uint32_t>(shadow); file.integer<std::uint32_t>(frames); file.integer<std::uint32_t>(1); file.integer<std::uint16_t>(0); file.integer<std::uint16_t>(0); };
    auto frame=[&](std::uint16_t color) { file.integer<std::uint32_t>(3); file.integer<std::uint16_t>(color|0x8000); file.integer<std::uint8_t>(2); };
    header("LightAnim_BASE",0,1); frame(31);
    header("Blink",0,2); if (broken) file.integer<std::uint32_t>(0); else frame(31<<10); frame(31<<5);
    header("Shadow",1,1); file.integer<std::uint32_t>(2); file.integer<std::uint8_t>(0); file.integer<std::uint8_t>(2);
    return file.data();
}
inline void load_lightmap_fixture(CWorldData& data,const std::vector<std::byte>& bytes) {
    nolf::MemoryStream stream(bytes); if (data.LoadObjects(&stream)!=LT_OK) throw std::runtime_error("lightmap fixture pools failed");
}
inline std::vector<nolf::LightmapTile> lightmap_geometry() {
    nolf::LightmapTile first; first.key=7; first.width=2; first.height=1; first.layers={{0,0},{1,0},{2,0}};
    auto second=first; second.key=9;
    return {std::move(first),std::move(second)};
}
}
