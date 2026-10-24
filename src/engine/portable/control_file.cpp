#include "engine/portable/control_file.h"
#include "engine/portable/string.h"
#include <algorithm>
#include <cstdlib>
#include <limits>
#include <stdexcept>

namespace nolf {
namespace {
constexpr std::string_view whitespace=" =,\t\r";
constexpr std::size_t maximum_word=2047,maximum_nesting=1024;
bool space(char c) { return whitespace.find(c)!=whitespace.npos; }
bool same(std::string_view a,std::string_view b) { return ascii_compare(a,b)==0; }
class Reader {
    std::string_view text_;
    std::size_t at_=0;
public:
    explicit Reader(std::string_view text) : text_(text) {}
    bool done() const { return at_>=text_.size(); }
    char peek() const { return text_[at_]; }
    char take() { return text_[at_++]; }
    void line() { while (!done() && take()!='\n') {} }
    void blanks() { while (!done() && space(peek())) ++at_; }
    // Ends before whitespace, a line end or end of input.
    std::string word() {
        std::string result; while (!done() && !space(peek()) && peek()!='\n') { const auto c=take(); if (result.size()<maximum_word) result.push_back(c); }
        return result;
    }
};
}
std::optional<int> ControlFile::Key::integer(std::size_t word) const {
    if (word>=words.size()) return {};
    // atoi: a leading number or zero, as the engine reads these values.
    return int(std::clamp(std::strtol(words[word].c_str(),nullptr,10),long(std::numeric_limits<int>::min()),long(std::numeric_limits<int>::max())));
}
std::optional<double> ControlFile::Key::number(std::size_t word) const {
    if (word>=words.size()) return {};
    return std::strtod(words[word].c_str(),nullptr);
}
ControlFile ControlFile::parse(std::span<const std::byte> bytes,std::span<const std::string> defines) {
    if (std::find(bytes.begin(),bytes.end(),std::byte{0})!=bytes.end()) throw std::runtime_error("control file contains binary data");
    Reader input(std::string_view(reinterpret_cast<const char*>(bytes.data()),bytes.size()));
    ControlFile file; file.sections_.emplace_back(); std::size_t section=0;
    // Each open conditional records whether it is skipping.
    std::vector<bool> conditions; std::size_t skipping=0;
    const auto defined=[&](std::string_view name) { return std::any_of(defines.begin(),defines.end(),[&](const auto& d) { return same(d,name); }); };
    while (!input.done()) {
        const auto c=input.peek();
        if (space(c) || c=='\n') { input.take(); continue; }
        if (c==';') { input.line(); continue; }
        if (c=='#') {
            input.take(); const auto command=input.word(); input.blanks(); const auto argument=input.word(); input.line();
            if (same(command,"ifdef") || same(command,"ifndef")) {
                if (conditions.size()>=maximum_nesting) throw std::runtime_error("control file conditionals nested too deeply");
                const bool skip=defined(argument)!=same(command,"ifdef"); conditions.push_back(skip); skipping+=skip;
            } else if (same(command,"else") && !conditions.empty()) {
                skipping-=conditions.back(); conditions.back()=!conditions.back(); skipping+=conditions.back();
            } else if (same(command,"endif") && !conditions.empty()) {
                skipping-=conditions.back(); conditions.pop_back();
            }
            continue;
        }
        if (skipping) { input.line(); continue; }
        if (c=='[') {
            input.take(); std::string name;
            while (!input.done() && input.peek()!=']' && input.peek()!='\n') { const auto ch=input.take(); if (name.size()<maximum_word) name.push_back(ch); }
            input.line();
            const auto found=std::find_if(file.sections_.begin(),file.sections_.end(),[&](const Section& s) { return same(s.name,name); });
            if (found==file.sections_.end()) { file.sections_.push_back({name,{}}); section=file.sections_.size()-1; }
            else section=std::size_t(found-file.sections_.begin());
            continue;
        }
        Key key; key.name=input.word();
        while (!input.done()) {
            const auto next=input.peek();
            if (next=='\n') { input.take(); break; }
            if (space(next)) { input.take(); continue; }
            if (next==';') { input.line(); break; }
            key.words.push_back(input.word());
        }
        file.sections_[section].keys.push_back(std::move(key));
    }
    return file;
}
std::vector<const ControlFile::Key*> ControlFile::keys(std::string_view name,std::string_view section) const {
    std::vector<const Key*> result;
    const auto found=std::find_if(sections_.begin(),sections_.end(),[&](const Section& s) { return same(s.name,section); });
    if (found!=sections_.end()) for (const auto& key : found->keys) if (same(key.name,name)) result.push_back(&key);
    return result;
}
const ControlFile::Key* ControlFile::key(std::string_view name,std::string_view section) const {
    const auto all=keys(name,section); return all.empty() ? nullptr : all.front();
}
}
