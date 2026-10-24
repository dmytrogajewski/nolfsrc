#include "engine/portable/profile.h"
#include "platform/system.h"
#include <iostream>
#include <stdexcept>

namespace fs=std::filesystem;
static void require(bool value,const char* message) { if (!value) throw std::runtime_error(message); }
template<class F> void rejects(F action) { bool rejected=false; try { action(); } catch (const std::exception&) { rejected=true; } require(rejected,"invalid profile/path accepted"); }
int main() {
    try {
        const auto root=fs::temp_directory_path()/("nolf-profile-"+std::to_string(nolf::platform::monotonic_nanoseconds()));
        struct Cleanup { fs::path path; ~Cleanup() { std::error_code error; fs::remove_all(path,error); } } cleanup{root};
        const nolf::platform::UserFiles files(root);
        require(!files.file_exists("Save/Save1001.ini") && !fs::exists(root),"read-only lookup created a profile");
        require(nolf::profile_value(files,"Save\\Save1001.ini","NOLF","Continue"," fallback ")=="fallback","missing profile default");
        files.create_directory("Save");
        const std::string retail="; retained comment\r\n[No One Lives Forever]\r\nContinue=\"Worlds\\M01S01\"\r\nSaveGame00=old\r\n[Other]\r\nKey=untouched\r\n";
        nolf::platform::atomic_write(root/"save/Save1001.INI",std::as_bytes(std::span(retail.data(),retail.size())));
        require(nolf::profile_value(files,"SAVE\\save1001.ini","no one lives forever","CONTINUE")=="Worlds\\M01S01","retail CRLF/quotes/case matching");
        nolf::set_profile_value(files,"Save\\Save1001.ini","No One Lives Forever","SaveGame00","Cate — Приключение|Worlds\\M01S01");
        require(nolf::profile_value(files,"save/save1001.ini","No One Lives Forever","SaveGame00")=="Cate — Приключение|Worlds\\M01S01","Unicode save label did not persist");
        nolf::set_profile_value(files,"Save/Save1001.ini","No One Lives Forever","Reload","reload.sav");
        nolf::set_profile_value(files,"Save/Save1001.ini","New Section","Key","new");
        nolf::set_profile_value(files,"Save/Save1001.ini","No One Lives Forever","Continue",std::nullopt);
        require(nolf::profile_value(files,"Save/Save1001.ini","No One Lives Forever","Continue","gone")=="gone","profile deletion");
        require(nolf::profile_value(files,"Save/Save1001.ini","No One Lives Forever","Reload")=="reload.sav","inserted value crossed section boundary");
        require(nolf::profile_value(files,"Save/Save1001.ini","Other","Key")=="untouched" && nolf::profile_value(files,"Save/Save1001.ini","New Section","Key")=="new","unrelated section changed");
        require(files.path("Save/Save1001.ini")==root/"save/Save1001.INI","case matching created a duplicate writable file");
        const auto before=files.read("Save/Save1001.ini");
        rejects([&] { nolf::set_profile_value(files,"Save/Save1001.ini","Invalid\nSection","key","value"); });
        rejects([&] { nolf::set_profile_value(files,"Save/Save1001.ini","NOLF","key","value\nInjected=bad"); });
        rejects([&] { files.path("../escape.sav"); }); rejects([&] { files.path("C:\\escape.sav"); });
        rejects([&] { files.read("Save/Save1001.ini",2); });
        require(files.read("Save/Save1001.ini")==before,"failed profile update changed valid data");
        std::error_code error; fs::create_directory_symlink(root/"save",root/"link",error);
        if (!error) rejects([&] { files.path("link/escape.sav"); });
#ifndef _WIN32
        nolf::platform::atomic_write(root/"save/save1001.ini",{});
        rejects([&] { files.path("SAVE/Save1001.INI"); });
#endif
        std::cout << "Native writable profile checks passed\n"; return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
