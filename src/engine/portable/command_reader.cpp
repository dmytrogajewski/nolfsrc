#include "command_reader.h"

namespace nolf {
std::optional<std::vector<std::string>> CommandReader::next() {
    ended_ = overflowed_ = false; literals_.clear();
    if (position_ >= input_.size() || !input_[position_]) return std::nullopt;
    std::vector<std::string> arguments;
    const auto append = [&](std::string token, bool started, bool literal) {
        if (!started) return;
        if (arguments.size() < max_arguments_) { arguments.push_back(std::move(token)); literals_.push_back(literal); }
        else overflowed_ = true;
    };
    while (position_ < input_.size() && input_[position_]) {
        while (position_ < input_.size() && input_[position_] == ' ') ++position_;
        std::string token; unsigned depth = 0; bool started = false, literal = false;
        while (position_ < input_.size() && input_[position_]) {
            auto c = static_cast<unsigned char>(input_[position_]);
            if (c == '%') {
                literal = true;
                if (++position_ == input_.size() || !input_[position_]) break;
                c = static_cast<unsigned char>(input_[position_]);
            } else if (c == ';' || c < 32 || c == 127) {
                ++position_; ended_ = true;
                append(std::move(token),started,literal);
                return arguments;
            } else if (c == '(') {
                started = literal = true;
                if (++depth == 1) { ++position_; continue; }
            } else if (c == ')') {
                if (depth && --depth == 0) { ++position_; break; }
            } else if (c == '"') {
                started = literal = true;
                ++position_;
                if (depth) break;
                depth = 1; continue;
            } else if (c == ' ' && !depth) break;
            started = true;
            if (token.size() < max_token_) token.push_back(static_cast<char>(c));
            else overflowed_ = true;
            ++position_;
        }
        append(std::move(token),started,literal);
        // Consume the entire statement even when its argument limit was hit.
        // Its remainder must never become an unintended second command.
    }
    return arguments;
}
}
