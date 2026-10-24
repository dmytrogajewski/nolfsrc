#include "engine/portable/profile.h"
#include "engine/portable/string.h"
#include <mutex>
#include <stdexcept>
#include <vector>

namespace nolf {
namespace {
std::mutex profile_mutex;
constexpr std::size_t maximum=1024*1024;
std::string_view trim(std::string_view value) {
    const auto first=value.find_first_not_of(" \t\r");
    if (first == value.npos) return {};
    return value.substr(first,value.find_last_not_of(" \t\r")-first+1);
}
bool equal(std::string_view a,std::string_view b) { return ascii_compare(a,b)==0; }
void field(std::string_view value,bool key) {
    if (value.empty() || value.size()>4096 || trim(value)!=value || value.find_first_of("\r\n[]\0",0,5)!=value.npos ||
        (key && value.find('=')!=value.npos)) throw std::invalid_argument("invalid profile field");
}
std::vector<std::string> lines(const platform::UserFiles& files,std::string_view file) {
    if (!files.file_exists(file)) return {};
    const auto bytes=files.read(file,maximum);
    const std::string text(reinterpret_cast<const char*>(bytes.data()),bytes.size());
    if (text.find('\0') != text.npos) throw std::runtime_error("profile contains NUL bytes");
    std::vector<std::string> result;
    std::size_t start=0;
    while (start<text.size()) {
        const auto end=text.find('\n',start);
        result.emplace_back(text.substr(start,end==text.npos ? text.size()-start : end-start));
        if (!result.back().empty() && result.back().back()=='\r') result.back().pop_back();
        if (end==text.npos) break;
        start=end+1;
    }
    return result;
}
std::string_view section_name(std::string_view line) {
    line=trim(line);
    return line.size()>=2 && line.front()=='[' && line.back()==']' ? trim(line.substr(1,line.size()-2)) : std::string_view{};
}
bool entry(std::string_view line,std::string_view key) {
    line=trim(line);
    if (line.empty() || line.front()==';' || line.front()=='#') return false;
    const auto delimiter=line.find('=');
    return delimiter!=line.npos && equal(trim(line.substr(0,delimiter)),key);
}
}
std::string profile_value(const platform::UserFiles& files,std::string_view file,std::string_view section,std::string_view key,std::string_view fallback) {
    field(section,false); field(key,true);
    std::lock_guard lock(profile_mutex);
    bool matching=false;
    for (const auto& line : lines(files,file)) {
        const auto name=section_name(line);
        if (!name.empty()) { matching=equal(name,section); continue; }
        if (matching && entry(line,key)) {
            auto value=trim(std::string_view(line).substr(line.find('=')+1));
            if (value.size()>=2 && ((value.front()=='"' && value.back()=='"') || (value.front()=='\'' && value.back()=='\''))) value=value.substr(1,value.size()-2);
            return std::string(value);
        }
    }
    return std::string(trim(fallback));
}
void set_profile_value(const platform::UserFiles& files,std::string_view file,std::string_view section,std::string_view key,std::optional<std::string_view> value) {
    field(section,false); field(key,true);
    if (value && (value->size()>maximum || value->find_first_of("\r\n\0",0,3)!=value->npos)) throw std::invalid_argument("invalid profile value");
    std::lock_guard lock(profile_mutex);
    auto document=lines(files,file);
    bool matching=false,found_section=false,inserted=false;
    std::size_t insertion=document.size();
    std::vector<std::string> output;
    for (const auto& line : document) {
        const auto name=section_name(line);
        if (!name.empty()) {
            if (matching) insertion=output.size();
            matching=equal(name,section); found_section |= matching;
        }
        if (matching && entry(line,key)) {
            if (value && !inserted) { output.emplace_back(std::string(key)+'='+std::string(*value)); inserted=true; }
        } else output.push_back(line);
    }
    if (matching) insertion=output.size();
    if (value && !inserted) {
        if (!found_section) {
            if (!output.empty() && !output.back().empty()) output.emplace_back();
            output.emplace_back('['+std::string(section)+']'); insertion=output.size();
        }
        output.insert(output.begin()+static_cast<std::ptrdiff_t>(insertion),std::string(key)+'='+std::string(*value));
    }
    std::string text;
    for (const auto& line : output) {
        if (line.size()+2 > maximum-text.size()) throw std::length_error("profile is too large");
        text+=line; text+="\r\n";
    }
    files.write(file,std::as_bytes(std::span(text.data(),text.size())));
}
}
