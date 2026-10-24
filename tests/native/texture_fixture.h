#pragma once
#include "engine/portable/wire.h"
#include <array>
#include <vector>
namespace nolf_test {
inline std::vector<std::byte> tiny_texture() {
    std::vector<std::byte> file(164+20);
    nolf::wire::store(file.data()+4,std::int32_t(-5));
    nolf::wire::store(file.data()+8,std::uint16_t(2)); nolf::wire::store(file.data()+10,std::uint16_t(2));
    nolf::wire::store(file.data()+12,std::uint16_t(2)); file[26]=std::byte{3};
    const std::array<std::uint32_t,5> values{0x10203040,0x50607080,0x90a0b0c0,0xd0e0f000,0x12345678};
    for (std::size_t i=0;i<values.size();++i) nolf::wire::store(file.data()+164+i*4,values[i]);
    return file;
}
}
