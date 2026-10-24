#include "Stdafx.h"
#include "WinUtil.h"
#include "cshell/p1_modern.h"
#include "engine/portable/profile.h"
#include "platform/logging.h"
#include <algorithm>
#include <cstring>

namespace {
template<class Operation> LTBOOL file_operation(Operation operation) {
    try { return operation(nolf::platform::UserFiles{}) ? LTTRUE : LTFALSE; }
    catch (const std::exception& error) { nolf::platform::log("game files",error.what()); return LTFALSE; }
}
}
LTBOOL CWinUtil::DirExist(const char* path) {
    return path && *path ? file_operation([&](const auto& files) { return files.directory_exists(path); }) : LTFALSE;
}
LTBOOL CWinUtil::CreateDir(const char* path) {
    return path && *path ? file_operation([&](const auto& files) { files.create_directory(path); return true; }) : LTFALSE;
}
LTBOOL CWinUtil::FileExist(const char* path) {
    return path && *path ? file_operation([&](const auto& files) { return files.file_exists(path); }) : LTFALSE;
}
uint32 CWinUtil::WinGetPrivateProfileString(const char* section,const char* key,const char* fallback,char* output,uint32 capacity,const char* file) {
    if (!output || !capacity) return 0;
    std::string value=fallback ? fallback : "";
    try {
        if (section && key && file) value=nolf::profile_value(nolf::platform::UserFiles{},file,section,key,value);
    } catch (const std::exception& error) { nolf::platform::log("game profile",error.what()); }
    const auto length=std::min<std::size_t>(value.size(),capacity-1);
    std::memcpy(output,value.data(),length); output[length]=0;
    return static_cast<uint32>(length);
}
LTBOOL CWinUtil::WinWritePrivateProfileString(const char* section,const char* key,const char* value,const char* file) {
    if (!section || !key || !file) return LTFALSE;
    return file_operation([&](const auto& files) {
        nolf::set_profile_value(files,file,section,key,value ? std::optional<std::string_view>(value) : std::nullopt); return true;
    });
}
void CWinUtil::DebugOut(const char* message) { if (message) nolf::platform::log("game",message); }
float CWinUtil::GetTime() { return P1Timing_GetTime(); }
