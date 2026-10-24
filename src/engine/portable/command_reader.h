#pragma once
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace nolf {
// LithTech statements: semicolon/control separators, quoted or parenthesized
// arguments and percent escapes. No host shell or locale participates.
class CommandReader {
public:
    explicit CommandReader(std::string_view input, std::size_t arguments = 30, std::size_t token_bytes = 256)
        : input_(input), max_arguments_(arguments), max_token_(token_bytes) {}
    std::optional<std::vector<std::string>> next();
    std::size_t position() const { return position_; }
    bool ended_statement() const { return ended_; }
    bool overflowed() const { return overflowed_; }
    const std::vector<bool>& literals() const { return literals_; }
private:
    std::string_view input_;
    std::size_t position_ = 0;
    std::size_t max_arguments_, max_token_;
    bool ended_ = false, overflowed_ = false;
    std::vector<bool> literals_;
};
}
