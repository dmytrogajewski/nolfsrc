#pragma once
#include "platform/cursors.h"
#include <cstddef>
#include <span>
namespace nolf {
struct CursorEntry { std::uint16_t width=0,height=0,planes=0,depth=0,id=0; std::uint32_t bytes=0; };
std::vector<CursorEntry> decode_cursor_group(std::span<const std::byte>);
platform::CursorBitmap decode_cursor(std::span<const std::byte>);
}
