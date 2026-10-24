#include "mfcstub.h"
#include "butemgr.h"
#include "blowfish.h"
#include "GameSpyMgr.h"
#include "engine/portable/assets.h"
#include "platform/filesystem.h"
#include "engine/portable/string.h"
#include "game/ObjectDLL/CDynArray.h"
#include <array>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <functional>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

static_assert(sizeof(DWORD) == 4 && sizeof(SDWORD) == 4);
static_assert(std::is_same_v<CMoDWordArray, CMoArray<uint32>>);
static_assert(sizeof(POINT) == 8 && sizeof(RECT) == 16);
namespace {
void require(bool value, const char* why) { if (!value) throw std::runtime_error(why); }
void same(const CString& value, const char* expected) { require(value.Compare(expected) == 0, "CString differs"); }
void format_twice(CString& first, CString& second, const char* format, ...) {
    va_list args;
    va_start(args, format);
    first.FormatV(format, args); second.FormatV(format, args);
    va_end(args);
}
void strings() {
    CDynArray<std::string> indexed(1,3);
    indexed[0] = "owned"; indexed[4] = "last";
    auto copied = indexed; indexed.Flush();
    require(indexed.ArraySize() == 0 && copied.ArraySize() == 6 && copied[0] == "owned" && copied[4] == "last",
            "indexed game array preserves block growth and independent typed ownership");
    for (long invalid : {0L,-1L}) {
        bool rejected = false; try { copied.SetGrowBy(invalid); } catch (const std::invalid_argument&) { rejected = true; }
        require(rejected,"invalid game array growth cannot cause an infinite allocation loop");
    }
    bool rejected_index = false;
    try { copied[-1]; } catch (const std::out_of_range&) { rejected_index = true; }
    require(rejected_index,"game array rejects negative indices in every build mode");
    require(nolf::ascii_compare_n("a", "A", 800) == 0 && nolf::ascii_compare_n("a", "ab", 2) < 0,
            "bounded ASCII comparison must stop at terminators");
    CString empty;
    same(empty, ""); require(empty.GetLength() == 0 && empty.CompareNoCase("") == 0, "empty string");
    empty.ReleaseBuffer(); require(empty.Replace("x", "y") == 0, "empty replace");
    CString text("abcdef"); text = text; same(text, "abcdef");
    text = static_cast<const char*>(text) + 2; same(text, "cdef");
    text += text; same(text, "cdefcdef"); require(text.GetLength() == 8, "concat length");
    require(text.Replace("cd", "12345") == 2, "expanded replacement count"); same(text, "12345ef12345ef");
    require(text.Replace("12345", "x") == 2, "shrunk replacement count"); same(text, "xefxef");
    same(text.Mid(1, 2), "ef"); same(text.Left(2), "xe"); same(text.Right(2), "ef");
    same(text.Mid(4000, 100), ""); same(text.Mid(2, 0xffffffffu), "fxef");
    require(text.Mid(1, 2).GetLength() == 2 && text.Left(2).GetLength() == 2, "slice lengths");
    require(text.Find("ef", 2) == 4 && text.Find('x', 1) == 3 && empty.Find('x') == -1, "find offsets");
    text.Format("%s/%d", static_cast<const char*>(text), 42); same(text, "xefxef/42");
    text = "%s:%c:%.*f";
    text.Format(static_cast<const char*>(text), "alias", 'A', 2, 3.5); same(text, "alias:A:3.50");
    CString second;
    format_twice(text, second, "%*s/%d/%.450f", 2048, "large", 31, 1.25);
    require(text.GetLength() > 2400 && text.Compare(second) == 0, "independent va_list passes");
    char* buffer = text.GetBuffer(8192);
    std::strcpy(buffer, "new"); text.ReleaseBuffer(); same(text, "new");
    bool rejected = false;
    try { text.GetBuffer(0xffffffffu); } catch (const std::length_error&) { rejected = true; }
    require(rejected, "capacity overflow accepted");
    for (unsigned i = 0; i < 300; ++i) {
        CStringArray array;
        for (unsigned j = 0; j < 60; ++j) { CString item; item.Format("%u/%u", i, j); require(array.Add(item), "string array add"); }
        array.Remove(4);
        require(array.GetSize() == 59 && array[4].GetLength() > 0, "native string array removal");
    }
    CRect rect(10, 20, 30, 40);
    const auto point = rect.TopLeft(); require(point.x == 10 && point.y == 20, "rectangle value point");
    CPtrList pointers;
    int values[3]{};
    for (auto& value : values) pointers.AddTail(&value);
    for (unsigned i = 0; i < 3; ++i) {
        auto position = pointers.FindIndex(i);
        require(position && pointers.GetAt(position) == &values[i], "native list index");
    }
    GenListPos position;
    require(pointers.GenFindElement(&values[2], position), "native list typed search");
    require(pointers.GenGetAt(position) == &values[2], "native list search result");
    require(!pointers.FindIndex(3) && pointers.RemoveHead() == &values[0], "native list removal");
}
void cryptography() {
    std::array<std::uint8_t, 8> zero{};
    nolf::attributes::Blowfish cipher(zero);
    std::uint32_t left = 0, right = 0;
    cipher.encipher(left, right);
    require(left == 0x4ef99745 && right == 0x6198dd78, "Blowfish zero key/block vector");
    cipher.decipher(left, right); require(left == 0 && right == 0, "Blowfish inverse vector");
    CCryptMgr first("test-first"), second("next-second"), same_key("test-other");
    for (std::size_t size = 0; size < 100; ++size) {
        std::string clear;
        for (std::size_t i = 0; i < size; ++i) clear += static_cast<char>(i * 37);
        std::istringstream input(clear), input_again(clear), alternate_input(clear);
        std::ostringstream encoded, encoded_again, alternate;
        first.Encrypt(input, encoded); second.Encrypt(alternate_input, alternate); same_key.Encrypt(input_again, encoded_again);
        require(encoded.str() == encoded_again.str(), "four byte retail key compatibility");
        require(encoded.str() != alternate.str(), "cipher instances interfere");
        require(encoded.str().size() == (size / 8 + 1) * 8 + 1, "retail padded frame length");
        std::istringstream encrypted(encoded.str()); std::ostringstream decoded;
        first.Decrypt(encrypted, decoded); require(decoded.str() == clear, "encrypted stream round trip");
        for (std::size_t cut = 0; cut < encoded.str().size(); ++cut) {
            // A prefix ending at a valid tail can form another valid message;
            // the codec is unframed and cannot authenticate such a prefix.
            if (cut % 8 == 1 && cut >= 9) continue;
            std::istringstream truncated(encoded.str().substr(0, cut)); std::ostringstream result;
            bool refused = false;
            try { first.Decrypt(truncated, result); } catch (const std::runtime_error&) { refused = true; }
            require(refused, "truncated crypt frame accepted");
        }
    }
}
class QueryFixture : public CGameSpyMgr {
public:
    std::function<void()> info_hook;
    bool large_rules = false;
    BOOL OnInfoQuery() override {
        SendResponseInfo("hostname", "Native NOLF");
        const auto hook = info_hook;
        if (hook) hook();
        return TRUE;
    }
    BOOL OnRulesQuery() override {
        if (large_rules) for (unsigned i = 0; i < 60; ++i)
            SendResponseInfo(("rule" + std::to_string(i)).c_str(), std::string(80, 'a').c_str());
        return TRUE;
    }
    BOOL OnPlayersQuery() override { SendResponseInfo("player_0", "Cate Archer"); return TRUE; }
};
class QuerySender : public CGameSpySender {
public:
    struct Packet { std::string data, address; unsigned long port; };
    std::vector<Packet> packets;
    std::function<void()> hook;
    void SendTo(const void* data, unsigned long size, const char* address, unsigned long port) override {
        packets.push_back({std::string(static_cast<const char*>(data), size), address, port});
        const auto callback = hook;
        if (callback) callback();
    }
};
void queries() {
    QuerySender sender; QueryFixture manager;
    require(manager.Init("nolf", "1.003", "Jn3Ab4", 0, 27888, 28054), "native query manager init");
    manager.SetSendHandler(&sender); manager.SetGameSpyAddr("none");
    char address[] = "127.0.0.1";
    auto query = [&](const char* text) {
        std::string owned(text);
        return manager.OnQuery(address, 30000, owned.data(), owned.size());
    };
    require(query("\\BaSiC\\") && sender.packets.size() == 1, "case insensitive LAN basic query");
    require(sender.packets[0].data == "\\gamename\\nolf\\gamever\\1.003\\location\\0\\final\\\\queryid\\2.1" &&
        sender.packets[0].address == address && sender.packets[0].port == 30000, "GameSpy v1 response fixture");
    manager.info_hook = [&] { query("\\echo\\nested"); };
    require(query("\\info\\\\echo\\outer-tail"), "nested LAN queries");
    require(sender.packets.back().data.find("\\echo\\outer-tail") != std::string::npos &&
        sender.packets[sender.packets.size() - 2].data.find("\\echo\\nested") != std::string::npos,
        "nested query retains outer cursor and response");
    manager.info_hook = {};
    sender.packets.clear(); manager.large_rules = true;
    require(query("\\status\\") && sender.packets.size() > 3, "multipart LAN status response");
    for (const auto& packet : sender.packets) require(packet.data.size() <= 1400 && packet.data.find("\\queryid\\") != std::string::npos, "bounded response datagrams");
    require(sender.packets.back().data.find("\\player_0\\Cate Archer") != std::string::npos &&
        sender.packets.back().data.find("\\final\\") != std::string::npos, "multipart response terminates");
    sender.packets.clear(); manager.SetGameSpyAddr("127.0.0.1:27900");
    manager.Update(); manager.Update();
    require(sender.packets.size() == 1 && sender.packets[0].port == 27900 &&
        sender.packets[0].data == "\\heartbeat\\27888\\gamename\\nolf", "heartbeat uses native resolved endpoint and cadence");
    manager.Term();
    require(sender.packets.size() == 2 && sender.packets.back().data.ends_with("\\statechanged\\2"), "shutdown sends departure through game transport");
    auto destroyed = std::make_unique<QueryFixture>();
    require(destroyed->Init("nolf", "1.003", "Jn3Ab4", 0, 27888, 28054), "query lifetime fixture");
    destroyed->SetSendHandler(&sender); destroyed->SetGameSpyAddr("none");
    sender.hook = [&] { destroyed.reset(); };
    std::string basic = "\\basic\\";
    require(destroyed->OnQuery(address, 30000, basic.data(), basic.size()) && !destroyed, "query manager can be destroyed during send");
    sender.hook = {};
    QueryFixture highest_port;
    require(highest_port.Init("nolf", "1.003", "Jn3Ab4", 0, 65535, 65701, GSMF_USEGAMEPORTFORHEARTBEAT) &&
        highest_port.GetGamePort() == 65535, "query responder accepts the highest game port when sharing its transport");
}
std::string fixture() {
    return "// original attributes\r\n[Example]\r\n"
           "Integer = -45\r\nUnsigned = (DWORD)4294967295\r\nByte = (BYTE)255\r\n"
           "Boolean = TRUE\r\nFloat = 1.5f\r\nDouble = 2.125\r\nText = \"hello\"\r\n"
           "Rectangle = (1, 2, 3, 4)\r\nPoint = (5, 6)\r\nVector = <7.0, 8.0, 9.0>\r\nRange = [10.0, 11.0]\r\n";
}
void verify(CButeMgr& parser) {
    require(parser.GetInt("Example", "Integer") == -45, "attribute int");
    require(parser.GetDword("Example", "Unsigned") == 0xffffffffu, "attribute dword");
    require(parser.GetByte("Example", "Byte") == 255 && parser.GetBool("Example", "Boolean"), "attribute byte/bool");
    require(parser.GetFloat("Example", "Float") == 1.5f && parser.GetDouble("Example", "Double") == 2.125, "attribute floats");
    same(parser.GetString("Example", "Text"), "hello");
    require(parser.GetRect("Example", "Rectangle").right == 3 && parser.GetPoint("Example", "Point").y == 6, "attribute rect/point");
    require(parser.GetVector("Example", "Vector").Getk() == 9 && parser.GetRange("Example", "Range").GetMax() == 11, "attribute vector/range");
}
void attributes() {
    auto data = fixture(); CButeMgr parser;
    require(parser.Parse(data.data(), data.size()), "attribute fixture parse"); verify(parser);
    unsigned keys = 0;
    parser.GetKeys("Example", [](const char*, CButeMgr::CSymTabItem* item, void* context) {
        CButeMgr::CSymTabItem copy(*item);
        CButeMgr::CSymTabItem assigned(CButeMgr::IntType, 1);
        assigned = copy;
        require(assigned.SymType == item->SymType, "attribute value deep copy");
        ++*static_cast<unsigned*>(context);
    }, &keys);
    require(keys == 11, "attribute key count");
    const auto temporary = std::filesystem::temp_directory_path() /
        ("nolf-bute-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directory(temporary);
    struct Cleanup { std::filesystem::path path; ~Cleanup() { std::filesystem::remove_all(path); } } cleanup{temporary};
    const auto filename = temporary / "attributes.txt";
    parser.SetInt("Example", "Added", 91); parser.SetInt("NewTag", "Added", 92);
    require(parser.Save(filename.string().c_str()), "save memory attributes");
    const auto saved = nolf::platform::read_file(filename);
    require(std::string(reinterpret_cast<const char*>(saved.data()), saved.size()).find("// original attributes") != std::string::npos, "attribute comments preserved");
    CButeMgr loaded; require(loaded.Parse(filename.string().c_str()), "load saved attributes"); verify(loaded);
    require(loaded.GetInt("Example", "Added") == 91 && loaded.GetInt("NewTag", "Added") == 92, "saved new attributes");
    auto oversized = std::string("[Example]\nText = \"") + std::string(9000, 'x') + "\"\n";
    require(!parser.Parse(oversized.data(), oversized.size()), "oversized attribute token accepted");
    auto duplicate = std::string("[Example]\nValue = 1\n[Example]\nValue = 2\n");
    require(!parser.Parse(duplicate.data(), duplicate.size()), "duplicate tag accepted");
    require(parser.Parse(data.data(), data.size()), "parser failed to recover"); verify(parser);
    for (std::size_t length = 0; length < data.size(); ++length) {
        CButeMgr partial;
        partial.Parse(data.data(), length);
    }
    auto destroyed = std::make_unique<CButeMgr>();
    require(destroyed->Parse(data.data(), data.size()), "callback fixture parse");
    struct Visit { std::unique_ptr<CButeMgr>* owner; unsigned count = 0; } visit{&destroyed};
    destroyed->GetKeys("Example", [](const char* name, CButeMgr::CSymTabItem* item, void* context) {
        auto& visit = *static_cast<Visit*>(context);
        visit.owner->reset();
        require(*name && item->SymType != CButeMgr::NullType, "callback snapshot ownership");
        ++visit.count;
    }, &visit);
    require(!destroyed && visit.count == 11, "parser destroyed during key traversal");
    const auto first_run=temporary/"first-run.sav";
    CButeMgr fresh;
    require(!fresh.Parse(first_run.string().c_str(),"first-run-key"),"missing encrypted profile unexpectedly parsed");
    fresh.SetInt("Campaign","Mission",7);
    require(fresh.Save(),"first-run encrypted profile could not be created");
    CButeMgr fresh_reloaded;
    require(fresh_reloaded.Parse(first_run.string().c_str(),"first-run-key") && fresh_reloaded.GetInt("Campaign","Mission")==7,
        "first-run profile lost encryption settings after missing-file read");
        CCryptMgr encryption("test-key"); std::istringstream clear(data); std::ostringstream encrypted;
    encryption.Encrypt(clear, encrypted); auto encoded = encrypted.str();
    require(parser.Parse(encoded.data(), encoded.size(), "test-key"), "encrypted attribute parse"); verify(parser);
    require(parser.Save(filename.string().c_str()), "encrypted attribute save");
    require(loaded.Parse(filename.string().c_str(), "test-key"), "reload encrypted attributes"); verify(loaded);
}
void corpus(const char* directory) {
    nolf::Assets assets; assets.mount_game(directory);
    unsigned files = 0, tags = 0, keys = 0;
    for (const auto& name : assets.names()) {
        if (!name.starts_with("attributes/") || !name.ends_with(".txt") || name == "attributes/animationshuman.txt") continue;
        auto bytes = assets.read(name); CButeMgr parser;
        if (!parser.Parse(bytes.data(), bytes.size())) throw std::runtime_error(name + ": " + static_cast<const char*>(parser.GetErrorString()));
        struct Count { CButeMgr* parser; unsigned* tags; unsigned* keys; } count{&parser, &tags, &keys};
        parser.GetTags([](const char* tag, void* context) {
            auto& count = *static_cast<Count*>(context); ++*count.tags;
            count.parser->GetKeys(tag, [](const char*, CButeMgr::CSymTabItem*, void* total) { ++*static_cast<unsigned*>(total); }, count.keys);
        }, &count);
        ++files;
    }
    require(files > 10 && keys > 1000, "original attribute corpus missing");
    std::cout << "Original attributes: " << files << " files, " << tags << " tags, " << keys << " values\n";
}
}
int main(int argc, char** argv) {
    try { strings(); cryptography(); queries(); attributes(); if (argc > 1) corpus(argv[1]); std::cout << "Native game support passed\n"; return 0; }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
