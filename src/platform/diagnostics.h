#pragma once
#include <string_view>

namespace nolf::platform {
[[noreturn]] void assertion_failed(std::string_view expression, std::string_view file, unsigned line) noexcept;
}
