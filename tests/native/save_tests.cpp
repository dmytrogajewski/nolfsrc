#include "engine/portable/saved_game.h"
#include "engine/portable/wire.h"
#include "engine/portable/binary.h"
#include "engine/portable/memory_stream.h"
#include "save_message.h"
#include "save_objects.h"
#include "client_reference.h"
#include <algorithm>
#include <bit>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
using namespace nolf;
void require(bool test,const char* why) { if (!test) throw std::runtime_error(why); }
template<class F> void rejects(F&& operation) {
    bool rejected = false; try { operation(); } catch (const std::exception&) { rejected = true; }
    require(rejected,"malformed saved game accepted");
}
// Independent transcription of sav_SaveObjects/sav_SaveObject's Windows
// field order. This fixture uses no native engine object or encoder types.
std::vector<std::byte> original_fixture() {
    BinaryWriter out;
    auto word = [&](std::uint16_t v) { out.integer(v); };
    auto dword = [&](std::uint32_t v) { out.integer(v); };
    auto byte = [&](std::uint8_t v) { out.integer(v); };
    auto text = [&](std::string_view v) { word(v.size()); out.bytes(std::as_bytes(std::span(v))); };
    dword(1113); dword(1); for (unsigned i = 0; i < 7; ++i) dword(0);
    dword(0xffffffff); byte(0);
    const auto record = out.data().size(); dword(0); dword(0); text("BaseClass"); byte(0);
    dword(0x12345678); dword(0x89abcdef);
    for (float v : {1.f,2.f,3.f,1.f,1.f,1.f,0.f,0.f,0.f,1.f}) out.real(v);
    dword(0x80000000); text("fixture"); text(""); text("");
    out.real(.25f); byte(4); byte(0);
    for (auto v : {10,20,30,40}) byte(v);
    for (float v : {4.f,5.f,6.f,0.f,-1.f,0.f,.5f,2.f,9.f,1.f,2.f,3.f}) out.real(v);
    dword(0x10); byte(0);
    const auto start = out.data().size();
    byte(42); dword(0xabcdefaf); dword(0xabcdefaf);
    const auto end = out.data().size(); dword(0xffffffff); word(0); word(0); byte(0);
    auto bytes = out.data(); wire::store(bytes.data()+record,static_cast<std::uint32_t>(end));
    wire::store(bytes.data()+record+4,static_cast<std::uint32_t>(start));
    return bytes;
}
SavedGame complete() {
    SavedGame game;
    game.timing_words = {17,std::bit_cast<std::uint32_t>(.01f),std::bit_cast<std::uint32_t>(50.f),0x80000000,0,0,0};
    game.portal_flags = {{{0,1,0xfffe},{},{4}}};
    game.console_variables = {{"difficulty","2"},{"empty",""},{"quoted","a; b \" c % d"}};
    for (unsigned i = 0; i < 11; ++i) {
        SavedObject object; object.class_name = "Class" + std::to_string(i); object.type = i;
        object.name = "Object" + std::to_string(i); object.filename = "models/path";
        object.skins[0] = "skins/texture.dtx";
        object.position = {float(i),-1,2}; object.dimensions = {1,2,3};
        object.velocity = {4,5,6}; object.acceleration = {0,-9,0};
        object.flags = 0x80000001; object.flags2 = 7; object.user_flags = 0xffffffff;
        object.friction = .5f; object.mass = 100; object.force_ignore_limit_squared = 4; object.next_update = .01f;
        if (i == 1) { object.animation = SavedAnimation{0x1234,500,0x81}; object.skins[1] = "second.dtx"; }
        if (i == 4) object.light_radius = 150;
        if (i == 9) object.container_code = 123;
        if (i == 2) { object.change_flags = 0x100; object.sky_slot = 29; }
        if (i == 3) object.sfx_message = std::vector<std::byte>{std::byte{0},std::byte{255}};
        if (i == 5) object.sfx_message.emplace();
        if (i == 0) object.client = SavedClient{0x80000000,"Player"};
        object.game_data = {std::byte(i),std::byte{0},std::byte{0xff}};
        if (i < 10) {
            object.attachments.push_back({{1,2,3},{0,0,0,1},static_cast<std::uint16_t>(i+1),-1});
            object.relations.push_back({static_cast<std::uint8_t>(i%2),static_cast<std::uint16_t>(i+1)});
        }
        game.objects.push_back(std::move(object));
    }
    return game;
}
void checks() {
    std::vector<std::byte> empty(46);
    wire::store(empty.data(),std::uint32_t{1113}); wire::store(empty.data()+36,std::uint32_t{0xffffffff});
    wire::store(empty.data()+41,std::uint32_t{0xffffffff});
    require(EncodeSavedGame(SavedGame{}) == empty && DecodeSavedGame(empty).objects.empty(),"empty save exact Windows header, timing, optional worlds and terminators");
    const auto fixture = original_fixture(); const auto decoded = DecodeSavedGame(fixture);
    require(decoded.objects.size() == 1 && decoded.objects[0].class_name == "BaseClass" && decoded.objects[0].flags == 0x12345678 &&
        decoded.objects[0].position == SavedVector{1,2,3} && decoded.objects[0].game_data.size() == 5,"independent original-layout fixture decodes");
    require(EncodeSavedGame(decoded) == fixture,"original-layout fixture is byte-identical after round trip");
    const auto document = complete(); const auto encoded = EncodeSavedGame(document);
    require(DecodeSavedGame(encoded) == document,"all 11 object types, portals, console variables, SFX, physics, client, sky and links round trip");
    require(DecodeSavedGame(EncodeSavedGame(document,12345),12345) == document,"absolute record offsets include outer stream origin");
    rejects([&] { DecodeSavedGame(encoded,1); });
    rejects([&] { EncodeSavedGame(document,UINT32_MAX-10); });
    for (std::size_t i = 0; i < encoded.size(); ++i) rejects([&] { DecodeSavedGame(std::span(encoded).first(i)); });
    auto damaged = fixture; wire::store(damaged.data()+4,std::uint32_t{0xffffffff});
    rejects([&] { DecodeSavedGame(damaged); });
    damaged = fixture; wire::store(damaged.data()+41,std::uint32_t{42}); rejects([&] { DecodeSavedGame(damaged); });
    damaged = fixture; wire::store(damaged.data()+45,std::uint32_t{0xffffffff}); rejects([&] { DecodeSavedGame(damaged); });
    damaged = empty; wire::store(damaged.data()+36,std::uint32_t{1}); wire::store(damaged.data()+40,std::uint32_t{0xffffffff});
    rejects([&] { DecodeSavedGame(damaged); });
    damaged = fixture; damaged[damaged.size()-10] ^= std::byte{1}; rejects([&] { DecodeSavedGame(damaged); });
    damaged = fixture; damaged.push_back(std::byte{0}); rejects([&] { DecodeSavedGame(damaged); });
    auto invalid = document; invalid.objects[0].attachments[0].child = 65535;
    rejects([&] { EncodeSavedGame(invalid); });
    invalid = document; invalid.objects[10].attachments.push_back({{},{0,0,0,1},0,-1});
    rejects([&] { EncodeSavedGame(invalid); });
    invalid = document; invalid.objects[0].relations[0].other = 0; rejects([&] { EncodeSavedGame(invalid); });
    invalid = document; invalid.objects[0].relations[0].type = 3; rejects([&] { EncodeSavedGame(invalid); });
    invalid = document; invalid.objects[0].position[0] = std::numeric_limits<float>::quiet_NaN(); rejects([&] { EncodeSavedGame(invalid); });
    invalid = document; invalid.objects[2].sky_slot = 30; rejects([&] { EncodeSavedGame(invalid); });
    invalid = document; invalid.objects[1].animation.reset(); rejects([&] { EncodeSavedGame(invalid); });
    invalid = document; invalid.objects[0].class_name = std::string("a\0b",3); rejects([&] { EncodeSavedGame(invalid); });
    invalid = document; invalid.objects[0].sfx_message.emplace(65536); rejects([&] { EncodeSavedGame(invalid); });
    invalid = document; invalid.objects[0].skins[1] = "would be lost"; rejects([&] { EncodeSavedGame(invalid); });
    std::uint32_t random = 0x6e6f6c66;
    for (unsigned i = 0; i < 10000; ++i) {
        damaged = encoded; random = random*1664525+1013904223; const auto index = random%damaged.size();
        random = random*1664525+1013904223; damaged[index] ^= std::byte((random>>24)|1);
        std::optional<SavedGame> valid;
        try { valid = DecodeSavedGame(damaged); } catch (const std::runtime_error&) {}
        if (valid) require(DecodeSavedGame(EncodeSavedGame(*valid)) == *valid,"mutated valid save is stable");
    }
}
void message_checks() {
    MemoryStream output({},true); SaveMessage writer(output,SaveMessageDirection::Write);
    require(writer.WriteByte(42) == LT_OK && writer.WriteDWord(0x12345678) == LT_OK,"save message scalar writes");
    const std::array<std::byte,13> scalar_fixture{std::byte{42},std::byte{0xaf},std::byte{0xef},std::byte{0xcd},std::byte{0xab},
        std::byte{0x78},std::byte{0x56},std::byte{0x34},std::byte{0x12},std::byte{0xaf},std::byte{0xef},std::byte{0xcd},std::byte{0xab}};
    require(std::ranges::equal(output.data(),scalar_fixture),"each save scalar has the original little-endian sentinel");
    char string[] = "abc"; LTVector vector(1,2,3); LTRotation rotation(0,0,0,1);
    require(writer.WriteString(string) == LT_OK && writer.WriteVector(vector) == LT_OK && writer.WriteRotation(rotation) == LT_OK,"save message strings and compound values use shared codecs");
    require(output.data().size() == 13+4*5+3*8+4*8,"string bytes and vector/rotation components have independent guards");
    require(writer.WriteStringAsHString(string) == LT_OK && writer.WriteHString(nullptr) == LT_OK,"save HSTRING length and null markers");
    CAbstractIO* storage = nullptr; msg_AllocPacket(&storage,4096,4096);
    require(storage,"nested save packet allocation");
    auto* packet = static_cast<ServerMessage*>(storage); packet->m_Write.WriteDWord(0x87654321);
    const auto nested_start = output.data().size();
    require(writer.WriteMessage(packet->m_Write) == LT_OK,"save embeds a native owned message"); storage->Release();
    require(output.data().size() == nested_start + 6 + 4*5,"nested save message guards each payload byte as in original reader/writer");
    require(writer.WriteCompVector(vector) == LT_OK && writer.WriteCompRotation(rotation) == LT_OK,"save compressed values use shared codecs");
    const auto size_before_zero = output.data().size();
    require(writer.WriteRaw(nullptr,0) == LT_OK && output.data().size() == size_before_zero,"zero length save field has no guard");
    const auto bytes = std::vector<std::byte>(output.data().begin(),output.data().end());
    auto read = [&](std::span<const std::byte> input) {
        MemoryStream source({input.begin(),input.end()}); SaveMessage reader(source,SaveMessageDirection::Read);
        ILTMessage& message = reader;
        uint8 byte = 0; uint32 dword = 0;
        if (message.ReadByteFL(byte) != LT_OK || message.ReadDWordFL(dword) != LT_OK) return false;
        require(byte == 42 && dword == 0x12345678,"save message virtual SDK scalar read");
        char truncated[2]; LTVector restored; LTRotation restored_rotation;
        if (message.ReadStringFL(truncated,sizeof(truncated)) != LT_OK || message.ReadVectorFL(restored) != LT_OK || message.ReadRotationFL(restored_rotation) != LT_OK) return false;
        require(std::string_view(truncated) == "a" && restored == vector,"truncated save string consumes remaining guarded characters");
        char text[8]; HSTRING null_string = nullptr;
        if (message.ReadHStringAsStringFL(text,sizeof(text)) != LT_OK || message.ReadHStringFL(null_string) != LT_OK) return false;
        require(std::string_view(text) == "abc" && !null_string,"guarded HSTRING values round trip");
        ILTMessage* nested = nullptr;
        if (message.ReadMessageFL(nested) != LT_OK) return false;
        require(nested && nested->ReadDWord() == 0x87654321,"restored nested message owns its ordinary packet data"); nested->Release();
        if (message.ReadCompVectorFL(restored) != LT_OK || message.ReadCompRotationFL(restored_rotation) != LT_OK) return false;
        uint32 position = 0, status = 99; source.GetPos(&position); message.GetStatus(status);
        require(position == input.size() && !status,"save payload consumed exactly without packet rewind");
        require(message.Release() == LT_OK && message.ResetPos() == LT_OK,"borrowed save message release/reset preserve owner");
        uint32 still = 0; source.GetPos(&still); require(still == position,"save ResetPos is the original no-op");
        return true;
    };
    require(read(bytes),"complete guarded save payload");
    for (std::size_t size = 0; size < bytes.size(); ++size) require(!read(std::span(bytes).first(size)),"truncated guarded payload fails");
    auto damaged = bytes; damaged[1] = std::byte{0};
    MemoryStream bad_source(std::move(damaged)); SaveMessage bad(bad_source,SaveMessageDirection::Read);
    uint8 value = 255; uint32 status = 0;
    require(bad.ReadByteFL(value) != LT_OK && value == 0,"bad field sentinel clears scalar output");
    bad.GetStatus(status); require(status & LMSTAT_READOVERFLOW,"bad sentinel persists in SDK message status");
    LTVector unused;
    require(writer.WriteCompPos(unused) != LT_OK && writer.Verify() != LT_OK,"missing position adapter marks ignored save error");
    SaveMessage wrong_direction(bad_source,SaveMessageDirection::Read);
    require(wrong_direction.WriteByte(1) != LT_OK,"read save message rejects writes");
}
void object_references() {
    auto first = std::make_unique<LTObject>(), second = std::make_unique<LTObject>();
    first->m_ObjectID = 300; second->m_ObjectID = 400;
    std::array<LTObject*,2> objects{first.get(),second.get()};
    CPosRef position; position.m_vMin.Init(1,2,3); position.m_vScale.Init(.25f,.25f,.25f);
    SaveObjectReferences references(objects,position,[](int code,va_list* arguments) {
        require(code == 7,"save formatting resource code");
        return std::string(va_arg(*arguments,char*));
    });
    CPosRef* retained = nullptr; references.GetPosRef(&retained); position.m_vMin.Init(99,99,99);
    require(retained && retained->m_vMin.x == 1,"save references snapshot world compression bounds");
    MemoryStream output({},true); SaveMessage writer(output,SaveMessageDirection::Write,&references);
    ILTMessage& sdk = writer;
    require(sdk.WriteObject(second.get()) == LT_OK && wire::load<std::uint16_t>(output.data().data()) == 1,"save object reference uses document index, not runtime ID");
    require(sdk.WriteObject(reinterpret_cast<HOBJECT>(std::uintptr_t(1))) == LT_OK && sdk.WriteObject(nullptr) == LT_OK,"unsaved and foreign handles serialize null without dereference");
    char formatted[] = "Resource text";
    require(sdk.WriteHStringFormatted(7,formatted) == LT_OK,"save formatted HSTRING uses supplied resource adapter");
    LTVector point(2,3,4); require(sdk.WriteCompPos(point) == LT_OK,"save position compression uses owned world reference");
    MemoryStream source({output.data().begin(),output.data().end()}); SaveMessage reader(source,SaveMessageDirection::Read,&references);
    ILTMessage& read_sdk = reader;
    require(read_sdk.ReadObject() == second.get() && !read_sdk.ReadObject() && !read_sdk.ReadObject(),"save object references round trip");
    char restored[40]; require(reader.ReadHStringAsStringFL(restored,sizeof(restored)) == LT_OK && std::string_view(restored) == formatted,"save resource string round trip");
    LTVector decoded; require(reader.ReadCompPosFL(decoded) == LT_OK,"guarded compressed position decodes");
    second.reset();
    require(source.SeekTo(0) == LT_OK && !read_sdk.ReadObject(),"deleted referenced object cannot escape restored message");
    require(sdk.WriteObject(objects[1]) == LT_OK && wire::load<std::uint16_t>(output.data().data()+output.data().size()-6) == 0xffff,"deleted saved object serializes null through retained lifetime token");
    std::array<LTObject*,2> duplicate{first.get(),first.get()}; rejects([&] { SaveObjectReferences bad(duplicate); });
    MemoryStream malformed({},true); SaveMessage malicious_writer(malformed,SaveMessageDirection::Write); malicious_writer.WriteWord(2);
    malformed.SeekTo(0); SaveMessage malicious_reader(malformed,SaveMessageDirection::Read,&references);
    HOBJECT invalid = first.get();
    require(malicious_reader.ReadObjectFL(invalid) != LT_OK && !invalid,"out-of-table save reference fails with defined null output");
}
void client_references() {
    ClientReferences references;
    auto first = std::make_unique<LTObject>(), second = std::make_unique<LTObject>();
    first->m_ObjectID = 7; second->m_ObjectID = 8;
    auto older = references.Add(*first,2,"First player");
    auto newer = references.Add(*second,0,"Second player");
    require(older && newer && references.Size() == 2 && references.Next(nullptr) == newer && references.Next(newer) == older,"restored player references own their enumeration order");
    char name[5]; require(references.CopyName(older,name,sizeof(name)) && std::string_view(name) == "Firs","client reference name terminates at caller capacity");
    const auto foreign = reinterpret_cast<HCLIENTREF>(std::uintptr_t(1));
    require(!references.Find(foreign) && !references.Next(foreign) && !references.CopyName(foreign,name,sizeof(name)) && !name[0],"foreign client reference is rejected before dereference");
    require(!references.CopyName(older,nullptr,5) && !references.CopyName(older,name,0),"client reference rejects invalid output");
    second.reset();
    require(!references.Find(newer) && references.Next(nullptr) == older,"removed player cannot be resolved through a recycled runtime ID");
    auto replacement = references.Add(*first,4,"Replacement");
    require(replacement && references.Size() == 2 && (first->m_ChangeFlags & 0x80),"replacing an object's client reference preserves owned pending state");
    references.Clear();
    require(references.Size() == 0 && !(first->m_ChangeFlags & 0x80),"client reference teardown clears live pending flags");
    MemoryStream bounded({},true,7); SaveMessage writer(bounded,SaveMessageDirection::Write);
    require(writer.WriteDWord(42) != LT_OK && writer.Verify() != LT_OK && bounded.data().size() <= 7,"game save payload cannot allocate past its extent budget");
}
}
int main() {
    try { checks(); message_checks(); object_references(); client_references(); std::cout << "native saved-game codec tests passed\n"; }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
