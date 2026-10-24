#pragma once
#include "platform/user_files.h"
#include <optional>
#include <string>
#include <string_view>

namespace nolf {
// Retail Save1001.ini document codec. Section/key matching is ASCII insensitive;
// values remain byte-for-byte UTF-8 and the document uses CRLF on save.
std::string profile_value(const platform::UserFiles&, std::string_view file,
    std::string_view section, std::string_view key, std::string_view fallback = {});
void set_profile_value(const platform::UserFiles&, std::string_view file,
    std::string_view section, std::string_view key, std::optional<std::string_view> value);
}
