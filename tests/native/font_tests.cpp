#include "font_offset_cache.h"
#include "lithbitmapfont.h"
#include "lithfontmgr.h"
#include "engine/portable/checksum.h"
#include "platform/filesystem.h"
#include "platform/system.h"
#include <algorithm>
#include <cstring>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool value,const char* why) { if (!value) throw std::runtime_error(why); }
struct TemporaryTree {
    std::filesystem::path path=std::filesystem::temp_directory_path()/
        ("nolf-font-cache-"+std::to_string(nolf::platform::monotonic_nanoseconds()));
    TemporaryTree() { std::filesystem::create_directories(path); }
    ~TemporaryTree() { std::error_code error; std::filesystem::remove_all(path,error); }
};
void cache() {
    TemporaryTree tree;
    const auto path=tree.path/"offsets.bin";
    nolf::FontOffsetCache cache(path);
    nolf::FontOffsetKey key{"interface/fonts/large.pcx",200,32,127,0,0x12345678};
    LITHFONTOFFSETDATA offsets;
    for (unsigned i=0; i<LTF_NUM_TOTAL_CHARS; ++i) {
        offsets.pOffsets[i+1]=offsets.pOffsets[i]+2;
        offsets.pWidths[i]=2;
    }
    require(!cache.load(key,offsets),"missing optional cache does not fabricate measurements");
    require(cache.store(key,offsets),"font offsets commit atomically");
    LITHFONTOFFSETDATA loaded; std::strcpy(loaded.szRefName,"CALLER");
    require(cache.load(key,loaded) && std::equal(std::begin(offsets.pOffsets),std::end(offsets.pOffsets),loaded.pOffsets) &&
        std::equal(std::begin(offsets.pWidths),std::end(offsets.pWidths),loaded.pWidths) && std::strcmp(loaded.szRefName,"CALLER") == 0,
        "cached widths preserve native values and caller-owned reference name");
    for (unsigned field=0; field<5; ++field) {
        auto changed=key;
        switch (field) {
            case 0: changed.width++; break;
            case 1: changed.height++; break;
            case 2: changed.groups++; break;
            case 3: changed.pitch++; break;
            case 4: changed.crc32++; break;
        }
        require(!cache.load(changed,loaded),"different atlas dimensions, grouping, pitch or content cannot reuse measurements");
    }
    const auto bytes=nolf::platform::read_file(path);
    loaded.pOffsets[5]=9999;
    for (std::size_t size=0; size<bytes.size(); ++size) {
        nolf::platform::atomic_write(path,std::span(bytes).first(size));
        require(!cache.load(key,loaded) && loaded.pOffsets[5] == 9999,"every truncated cache leaves output unchanged");
    }
    auto invalid=bytes; invalid[8]=std::byte{129};
    nolf::platform::atomic_write(path,invalid);
    require(!cache.load(key,loaded),"cache entry count is bounded before allocation");
    require(cache.store(key,offsets) && cache.load(key,loaded),"successful measurement replaces corrupt advisory cache");
    auto bad=offsets; bad.pOffsets[10]=0;
    require(!cache.store(key,bad),"nonmonotonic glyph positions cannot be persisted");
    auto foreign=key; foreign.asset="../outside";
    require(!cache.store(foreign,offsets),"cache identities use normalized asset names");
    require(cache.load(key,loaded) && loaded.pOffsets[5] == offsets.pOffsets[5],"invalid writes preserve previous valid cache");
}
class BitmapLayoutProbe : public LithBitmapFont {
public:
    const char* prepare(const char* text,int index=-1) { return PrepareText(text,index); }
    bool update(LITHFONTSAVEDATA& saved,const LITHFONTDRAWDATA& draw) { return UpdateLayoutIdentity(saved,draw,draw.dwFlags); }
    const LITHFONTFORMATDATA& layout(std::string text,uint32 width) {
        m_szString=PrepareText(text.c_str(),-1); m_lfFD=&format_; m_lfDD=&draw_;
        m_dwFlags=0; m_nAvgWidth=2; m_nExtraLetterSpace=0;
        std::fill(std::begin(m_lfOD.pWidths),std::end(m_lfOD.pWidths),2);
        SetFormatData(width); return format_;
    }
private:
    LITHFONTFORMATDATA format_;
    LITHFONTDRAWDATA draw_;
};
class FontLifetimeProbe final : public BitmapLayoutProbe {
public:
    explicit FontLifetimeProbe(bool& destroyed) : destroyed_(destroyed) {}
    ~FontLifetimeProbe() override { destroyed_=true; }
private:
    bool& destroyed_;
};
void layout() {
    BitmapLayoutProbe font;
    const char* literal="A\xc3\xa9\xe2\x82\xac";
    require(std::string(font.prepare(literal,2)) == "A\xc3\xa9" && std::strlen(literal) == 6,
        "indexed text uses owned UTF-8 characters without modifying caller storage");
    LITHFONTSAVEDATA saved;
    LITHFONTDRAWDATA draw;
    require(font.update(saved,draw) && !font.update(saved,draw),"unchanged text reuses its measured layout");
    font.prepare("new value"); require(font.update(saved,draw),"reused draw storage cannot hide changed text");
    auto copied=saved; saved.previousText.clear();
    require(copied.previousText == "new value","copied saved formatting owns its string snapshot");
    draw.dwFormatWidth=100; require(font.update(copied,draw),"wrapping changes invalidate cached line measurements");
    draw.fLetterDelay=.01f; require(font.update(copied,draw),"timing changes invalidate cached letter durations");
    BitmapLayoutProbe replacement; replacement.prepare("new value");
    require(replacement.update(copied,draw),"a replacement font cannot reuse old glyph measurements");
    const auto& wrapped=font.layout("AB CD\nEF",5);
    require(wrapped.nNumLines == 3 && wrapped.pIndex[1] == 3 && wrapped.pIndex[2] == 6 && wrapped.pIndex[3] == 8 &&
        wrapped.pWidths[0] == 4 && wrapped.pWidths[1] == 4 && wrapped.pWidths[2] == 4,"actual bitmap formatter preserves word wrapping and explicit newlines");
    const auto& long_lines=font.layout(std::string(2000,'\n'),80);
    require(long_lines.nNumLines == LTF_MAX_FORMAT_LINES-1 && long_lines.pIndex[long_lines.nNumLines] == long_lines.nNumLines,
        "bitmap layout leaves room for its terminal index at the documented line budget");
    require(font.layout(std::string(4096,'A'),0).pWidths[0] == 8192,"long unwrapped text is measured without a fixed string buffer");
    require(font.layout("",0).nNumLines == 0,"empty bitmap text has no drawable lines");
    font.Draw_Method_Timed();
    bool destroyed=false;
    FreeLithFont(new FontLifetimeProbe(destroyed));
    require(destroyed,"factory release destroys the actual derived font and its owned strings");
    FreeLithCursor(new LithCursor);
    require(font.SetRefName("valid") && !font.SetRefName(std::string(100,'x').c_str()) && std::strcmp(font.GetRefName(),"valid") == 0,
        "oversized reference names cannot overwrite or truncate an existing font identity");
}
}
int main() {
    try {
        nolf::Crc32 crc;
        const std::string first="1234", second="56789";
        crc.update(std::as_bytes(std::span(first))); crc.update(std::as_bytes(std::span(second)));
        require(crc.value() == 0xcbf43926u,"incremental CRC matches independent standard vector");
        cache();
        layout();
        std::cout << "native bounded font cache, atlas identity, owned text/layout, line budgets and derived font release passed\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
