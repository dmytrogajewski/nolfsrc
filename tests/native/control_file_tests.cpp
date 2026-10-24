#include "engine/portable/control_file.h"
#include "engine/portable/assets.h"
#include <iostream>

namespace {
void require(bool value,const char* why) { if (!value) throw std::runtime_error(why); }
nolf::ControlFile parse(std::string_view text,std::vector<std::string> defines = {}) { return nolf::ControlFile::parse(std::as_bytes(std::span(text.data(),text.size())),defines); }
void checks() {
    const auto file=parse("; header comment\r\nNUMINTENSITIES 14\r\nINTENSITY\t3\t-1\t0\t\tAmb1.sgt, Amb2.sgt ; trailing\r\n"
        "TRANSITION 1 5 IMMEDIATE MANUAL MedIntro.sgt\nTRANSITION 1 8 IMMEDIATE MANUAL\nintensity 4 0 13 a;b.sgt\nKEY=value\n[Other]\nNUMINTENSITIES 2\n[]\nLAST 1");
    require(file.key("numintensities")->integer(0)==14 && file.keys("NUMINTENSITIES").size()==1,"top-level key or section separation wrong");
    require(file.keys("NUMINTENSITIES","other").front()->integer(0)==2,"named section lookup wrong");
    const auto intensities=file.keys("INTENSITY"); require(intensities.size()==2,"repeated keys lost");
    require((intensities[0]->words==std::vector<std::string>{"3","-1","0","Amb1.sgt","Amb2.sgt"}),"comma/tab/CR splitting or trailing comment wrong");
    require(intensities[0]->integer(1)==-1 && !intensities[0]->integer(5),"signed or missing integer wrong");
    require(intensities[1]->words.back()=="a;b.sgt","semicolon inside a word must not start a comment");
    require(file.keys("TRANSITION")[1]->words.size()==4 && file.key("KEY")->words.front()=="value","optional word or '=' separator wrong");
    require(file.key("LAST") && file.key("LAST")->integer(0)==1,"reopened unnamed section or final unterminated line lost");
    const auto conditional=parse("#ifdef MULTI\nA 1\n#else\nA 2\n#endif\n#ifndef MULTI\nB 1\n#ifdef OTHER\nC 1\n#endif\n#endif\nD x\n",{"multi"});
    require(conditional.key("A")->integer(0)==1 && !conditional.key("B") && !conditional.key("C"),"defines or nesting wrong");
    require(conditional.key("D")->integer(0)==0 && conditional.key("D")->number(0)==0,"non-numeric word must read as zero");
    const auto plain=parse("#ifdef MULTI\nA 1\n#else\nA 2\n#endif\n#endif\n#else\nB 3\n");
    require(plain.key("A")->integer(0)==2 && plain.key("B")->integer(0)==3,"unbalanced conditionals must be ignored");
    bool rejected=false; try { parse(std::string_view("A\0B",3)); } catch (const std::runtime_error&) { rejected=true; } require(rejected,"binary control file accepted");
    std::string deep; for (int i=0;i<1100;++i) deep+="#ifdef X\n"; rejected=false; try { parse(deep); } catch (const std::runtime_error&) { rejected=true; } require(rejected,"unbounded nesting accepted");
}
void corpus(const char* data) {
    nolf::Assets assets; assets.mount_game(data); unsigned files=0,intensities=0,transitions=0;
    for (const auto& name : assets.names()) {
        if (!name.starts_with("music/") || !name.ends_with(".txt")) continue;
        const auto file=nolf::ControlFile::parse(assets.read(name)); const auto count=file.key("NUMINTENSITIES");
        require(count && count->integer(0)>0,"original control file without intensities");
        for (const auto* key : file.keys("INTENSITY")) { require(key->words.size()>=4 && key->integer(0)>0 && key->integer(0)<=*count->integer(0),"original intensity malformed"); ++intensities; }
        for (const auto* key : file.keys("TRANSITION")) { require(key->words.size()>=4,"original transition malformed"); ++transitions; }
        ++files;
    }
    require(files>0,"no original control files found");
    std::cout<<"Native control files: "<<files<<" original music files, "<<intensities<<" intensities, "<<transitions<<" transitions\n";
}
}
int main(int argc,char** argv) { try { checks(); if (argc>1) corpus(argv[1]); std::cout<<"Native control file grammar, sections, conditionals and limits passed\n"; return 0; } catch (const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; } }
