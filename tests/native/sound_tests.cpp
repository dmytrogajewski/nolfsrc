#include "sound_cache.h"
#include "sound_instance.h"
#include "sound_wire.h"
#include "inter_object_link.h"
#include "engine/portable/record_pool.h"
#include "engine/portable/binary.h"
#include "engine/portable/wire.h"
#include "platform/system.h"
#include <array>
#include <cmath>
#include <cstring>
#include <iostream>
#include <random>
#include <stdexcept>

using Bytes = std::vector<std::byte>;
namespace {
void require(bool value, const char* why) { if (!value) throw std::runtime_error(why); }
template<class F> void rejects(F&& fn) {
    bool failed = false;
    try { fn(); } catch (const std::exception&) { failed = true; }
    require(failed, "malformed WAVE accepted");
}
void append(Bytes& a, const Bytes& b) { a.insert(a.end(), b.begin(), b.end()); }
Bytes chunk(std::string_view id, Bytes payload) {
    nolf::BinaryWriter out;
    out.bytes(std::as_bytes(std::span(id))); out.integer<uint32>(payload.size()); out.bytes(payload);
    if (payload.size() & 1) out.integer<uint8>(0);
    return out.data();
}
Bytes words(std::initializer_list<uint32> values) {
    nolf::BinaryWriter out; for (auto v : values) out.integer(v); return out.data();
}
Bytes fourcc(std::string_view text) { return Bytes(std::as_bytes(std::span(text)).begin(), std::as_bytes(std::span(text)).end()); }
Bytes list(std::string_view form, Bytes chunks) { auto body = fourcc(form); append(body, chunks); return chunk("LIST", body); }
Bytes riff(Bytes chunks) { auto body = fourcc("WAVE"); append(body, chunks); return chunk("RIFF", body); }
Bytes fmt(uint16 codec, uint16 channels, uint32 rate, uint32 byteRate, uint16 align, uint16 bits, Bytes extra = {}) {
    nolf::BinaryWriter out;
    out.integer(codec); out.integer(channels); out.integer(rate); out.integer(byteRate); out.integer(align); out.integer(bits);
    if (!extra.empty()) { out.integer<uint16>(extra.size()); out.bytes(extra); }
    return chunk("fmt ", out.data());
}
Bytes pcm() {
    auto chunks = fmt(1, 2, 8000, 32000, 4, 16);
    append(chunks, chunk("data", Bytes(320, std::byte{0}))); return riff(chunks);
}
Bytes ima() {
    nolf::BinaryWriter extra; extra.integer<uint16>(505);
    auto chunks = fmt(0x11, 1, 22050, 11178, 256, 4, extra.data());
    append(chunks, chunk("fact", words({500})));
    append(chunks, chunk("data", Bytes(256, std::byte{0})));
    return riff(chunks);
}
void parser_checks() {
    const auto original = pcm();
    const auto p = nolf::read_wave(original);
    require(p.format == 1 && p.channels == 2 && p.frames == 80 && p.data_offset == 44 && p.data_size == 320,
        "PCM metadata");
    require(std::abs(p.duration() - .01) < 1e-12, "PCM duration");
    for (std::size_t n = 0; n < original.size(); ++n) rejects([&] { nolf::read_wave(std::span(original).first(n)); });
    const auto compressed = ima();
    for (std::size_t n = 0; n < compressed.size(); ++n) rejects([&] { nolf::read_wave(std::span(compressed).first(n)); });
    require(nolf::read_wave(ima()).frames == 500, "IMA fact before data remains authoritative");
    auto chunks = chunk("fact", words({500}));
    append(chunks, chunk("data", Bytes(256, std::byte{0})));
    nolf::BinaryWriter extra; extra.integer<uint16>(505);
    append(chunks, fmt(0x11, 1, 22050, 11178, 256, 4, extra.data()));
    require(nolf::read_wave(riff(chunks)).frames == 500, "format/data/fact independent of chunk order");
    auto noFact = fmt(0x11, 1, 22050, 11178, 256, 4, extra.data());
    append(noFact, chunk("data", Bytes(260, std::byte{0})));
    require(nolf::read_wave(riff(noFact)).frames == 506, "partial IMA block frame capacity");
    auto invalidFact = ima(); nolf::wire::store<uint32>(invalidFact.data() + 48, 506);
    rejects([&] { nolf::read_wave(invalidFact); });
    auto mp3 = fmt(0x55, 2, 44100, 16000, 1, 0);
    append(mp3, chunk("data", Bytes(16000, std::byte{0})));
    require(!nolf::read_wave(riff(mp3)).frames && nolf::read_wave(riff(mp3)).duration() == 1, "MP3 byte-rate duration");
    auto loops = fmt(1, 1, 8000, 8000, 1, 8);
    auto label = words({7, 20, 0, 0, 0});
    append(loops, list("adtl", chunk("ltxt", label)));
    auto cue = words({1, 7, 10}); append(cue, fourcc("data")); append(cue, words({0, 0, 999}));
    append(loops, chunk("cue ", cue));
    append(loops, list("INFO", chunk("lith", words({3, 123}))));
    append(loops, chunk("JUNK", Bytes(3)));
    append(loops, chunk("data", Bytes(80)));
    auto l = nolf::read_wave(riff(loops));
    require(l.loop && l.loop->cue == 7 && l.loop->start == 10 && l.loop->length == 20 && l.lith_flags == 3 && l.lith_metadata == 123,
        "LithTech loop and metadata, label preceding cue");
    auto deep = chunk("JUNK", {});
    for (int i = 0; i < 18; ++i) deep = list("INFO", deep);
    rejects([&] { nolf::read_wave(riff(deep)); });
    auto duplicate = fmt(1, 1, 8000, 8000, 1, 8); append(duplicate, duplicate); append(duplicate, chunk("data", Bytes(10)));
    rejects([&] { nolf::read_wave(riff(duplicate)); });
    auto overflow = original; nolf::wire::store<uint32>(overflow.data() + 40, UINT32_MAX);
    rejects([&] { nolf::read_wave(overflow); });
    auto hugeCue = fmt(1, 1, 8000, 8000, 1, 8);
    append(hugeCue, chunk("cue ",words({UINT32_MAX}))); append(hugeCue,chunk("data",Bytes(80)));
    rejects([&] { nolf::read_wave(riff(hugeCue)); });
    auto opaque = fmt(1, 1, 8000, 8000, 1, 8);
    append(opaque,list("lith",words({8,UINT32_MAX,123})));
    append(opaque,chunk("data",Bytes(3)));
    auto odd = riff(opaque); odd.pop_back(); nolf::wire::store<uint32>(odd.data() + 4,static_cast<uint32>(odd.size() - 8));
    require(nolf::read_wave(odd).frames == 3,"opaque original vendor LIST and omitted final odd data pad");
    nolf::BinaryWriter stereo_extra; stereo_extra.integer<uint16>(505);
    auto stereo = fmt(0x11, 2, 22050, 22356, 512, 4, stereo_extra.data());
    append(stereo,chunk("data",Bytes(520)));
    require(nolf::read_wave(riff(stereo)).frames == 506,"stereo IMA interleaved blocks and partial header capacity");
    auto many_chunks = fmt(1, 1, 8000, 8000, 1, 8);
    for (unsigned i = 0; i < 65536; ++i) append(many_chunks,chunk("JUNK",{}));
    append(many_chunks,chunk("data",Bytes(1)));
    rejects([&] { nolf::read_wave(riff(many_chunks)); });
    // Mutations stress independent RIFF boundaries and format arithmetic.
    std::mt19937 random(0x534f554e);
    for (unsigned i = 0; i < 12000; ++i) {
        auto mutated = i & 1 ? original : ima();
        for (unsigned j = 0; j < 3; ++j) mutated[random() % mutated.size()] = std::byte(random() & 255);
        try { (void)nolf::read_wave(mutated); } catch (const std::runtime_error&) {}
    }
}
void cache_checks() {
    const auto root = std::filesystem::temp_directory_path() / ("nolf-sounds-" + std::to_string(nolf::platform::monotonic_nanoseconds()));
    std::filesystem::create_directories(root);
    struct Cleanup { std::filesystem::path path; ~Cleanup() { std::error_code e; std::filesystem::remove_all(path, e); } } cleanup{root};
    nolf::platform::atomic_write(root / "a.wav", pcm());
    nolf::platform::atomic_write(root / "b.wav", ima());
    nolf::platform::atomic_write(root / "bad.wav", Bytes(8));
    CFileMgr files; files.mount(root);
    SoundCache cache(files);
    CFileRef *a = nullptr, *b = nullptr, *bad = nullptr;
    require(files.identify("a.wav", 0, &a) && files.identify("b.wav", 0, &b) && files.identify("bad.wav", 0, &bad), "sound file IDs");
    const auto retained = cache.Load(*a);
    require(retained == cache.Load(*a) && cache.Size() == 1 && std::abs(retained->server_duration - .11f) < 1e-6,
        "shared sound cache and original server duration policy");
    cache.Load(*b); cache.MarkWanted(*b);
    require(cache.WantedFileIDs() == std::vector<uint16>{b->m_nFileID}, "cache pass announcement IDs");
    cache.Collect(); require(cache.Size() == 1 && retained->wave.frames == 80, "collected track remains valid for holder");
    rejects([&] { cache.MarkWanted(*bad); });
    require(cache.Size() == 1, "failed metadata load publishes no cache entry");
    nolf::platform::atomic_write(root / "bad.wav", pcm());
    require(cache.MarkWanted(*bad) && cache.Size() == 2, "failed metadata load can retry after repair");
    cache.BeginPass(); require(cache.WantedFileIDs().empty(), "new cache pass clears wanted state");
    cache.Collect(); require(cache.Size() == 0, "unwanted metadata collected");
    cache.MarkWanted(*a); auto final = cache.Load(*a);
    cache.Clear(); fs_Term(&files);
    require(final->wave.frames == 80 && final->name == "a.wav", "track owns names/metadata across file manager shutdown");
}
void requests_and_records() {
    PlaySoundInfo info{};
    std::strcpy(info.m_szSoundName,"sound.wav");
    require(sound_NormalizeRequest(info) == LT_OK && info.m_fPitchShift == 1 && info.m_nVolume == 100 && info.m_fOuterRadius == 0,
        "SDK zero-initialized request receives ignored-field defaults");
    const auto nan = std::numeric_limits<float>::quiet_NaN();
    info.m_fPitchShift = nan; info.m_fOuterRadius = nan; info.m_vPosition.x = nan;
    require(sound_NormalizeRequest(info) == LT_OK && info.m_fPitchShift == 1 && info.m_vPosition.x == 0,
        "ignored request fields do not reject local sounds");
    info.m_dwFlags = PLAYSOUND_3D;
    info.m_fOuterRadius = nan;
    require(sound_NormalizeRequest(info) == LT_INVALIDPARAMS,"used spatial fields reject non-finite values");
    info.m_fOuterRadius = 100000; info.m_fInnerRadius = -1; info.m_vPosition.Init(1,2,3);
    require(sound_NormalizeRequest(info) == LT_OK && info.m_fOuterRadius == 65000 && info.m_fInnerRadius == 1,"spatial radii retain engine limits");
    info.m_dwFlags |= PLAYSOUND_CTRL_PITCH; info.m_fPitchShift = nan;
    require(sound_NormalizeRequest(info) == LT_INVALIDPARAMS,"used pitch rejects non-finite value");
    info.m_fPitchShift = 0;
    require(sound_NormalizeRequest(info) == LT_OK,"explicit zero pitch remains supported by SDK contract");
    std::memset(info.m_szSoundName,'x',sizeof(info.m_szSoundName));
    require(sound_NormalizeRequest(info) == LT_INVALIDPARAMS,"unterminated sound name bounded");
    require(sound_QuantizeRange(0,100) == std::pair<uint16,uint8>{0,0} && sound_QuantizeRange(nan,1).first == 0,
        "nonspatial/invalid ranges cannot divide by zero or cast NaN");
    require(sound_QuantizeRange(100,50) == std::pair<uint16,uint8>{100,127} && sound_QuantizeRange(100,500).second == 255 &&
        sound_QuantizeRange(100,-5).second == 0 && sound_QuantizeRange(100000,50).first == 65535,"wire range and ratio saturate before conversion");
    require(sound_ElapsedMilliseconds(100,0) == 34464 && sound_ElapsedMilliseconds(0,100) == 0 && sound_ElapsedMilliseconds(nan,0) == 0,
        "original 16-bit elapsed-time wrap is explicit and bounded");
    nolf::RecordPool<SoundInstance> instances;
    auto track = std::make_shared<SoundTrack>();
    std::weak_ptr<const SoundTrack> lifetime = track;
    auto* sound = instances.Allocate(); require(sound,"native sound instance allocation");
    sound->m_Track = track; track.reset();
    require(sound->m_Link.IsTiedOff() && !sound->m_pObject && !sound->m_pUnknown0c && sound->m_fPitchShift == 1,"full native sound record constructed");
    sound->m_PlaySoundFlags = PLAYSOUND_TIME;
    sound->m_fTimer = 10; sound->AddHold(); sound->AddHold(); uint8 flags = 0;
    sound->DropListener(&flags);
    require(sound->m_ListenerCount == 1 && sound->m_fTimer == 10 && flags == 1,"sound listener holds maintain timer");
    sound->DropListener(&flags); sound->DropListener(&flags);
    require(!sound->m_ListenerCount && !sound->m_fTimer,"last listener retires timer without count underflow");
    sound->m_PlaySoundFlags |= PLAYSOUND_LOOP; sound->AddHold();
    require(!sound->m_ListenerCount,"looping sound does not acquire timed listener holds");
    require(instances.Free(sound) && lifetime.expired() && !instances.Free(sound),"native sound destruction releases track exactly once");
}
void links() {
    LTObject a,b,c;
    SObjData ad{},bd{},cd{};
    a.m_pSData = &ad; b.m_pSData = &bd; c.m_pSData = &cd;
    ad.m_Links.TieOff(); bd.m_Links.TieOff(); cd.m_Links.TieOff();
    InterObjectLinks links;
    require(!links.Create(&a,&a,0) && !links.Create(&a,&b,2),"typed link endpoints reject self and sound-as-object");
    require(links.Create(&a,&b,0) && links.Create(&a,&b,0) && links.Create(&a,&b,1),"native dual link creation");
    require(links.Size() == 2,"duplicate SDK link request is idempotent");
    links.Break(&a,&b,false); require(links.Size() == 1,"break first matching game link");
    links.Break(&a,&b,true); require(links.Size() == 1,"game unlink preserves container relation");
    links.BreakFor(&b,1); require(!links.Size() && ad.m_Links.IsTiedOff() && bd.m_Links.IsTiedOff(),"unlink from far endpoint retires both memberships");
    SoundInstance sound, held;
    sound.m_PlaySoundFlags = held.m_PlaySoundFlags = PLAYSOUND_ATTACHED;
    held.m_PlaySoundFlags |= PLAYSOUND_GETHANDLE;
    sound.m_fTimer = held.m_fTimer = 10;
    require(links.Attach(&a,&sound) && links.Attach(&a,&held) && !links.Attach(&b,&sound),"one typed attachment per sound");
    links.BreakFor(&a,2);
    require(!links.Size() && !sound.m_pObject && sound.m_bKill && !sound.m_fTimer && !held.m_pObject && !held.m_bKill && held.m_fTimer == 10,
        "object teardown kills automatic sounds and preserves game-held sounds");
    require(links.Attach(&a,&held),"detached sound can be reattached"); links.Detach(&a,&held,true);
    require(!links.Size() && !held.m_pObject && !held.m_bKill,"sound termination detaches without changing game policy");
    require(links.Create(&a,&b,0) && links.Create(&a,&c,0),"callback mutation setup");
    struct Context { InterObjectLinks& links; LTObject& a; LTObject& b; unsigned calls = 0; } context{links,a,b};
    links.BreakFor(&a,0,[](void* p,LTObject*,LTObject*) {
        auto& ctx = *static_cast<Context*>(p); ++ctx.calls;
        ctx.links.Clear(); ctx.links.Create(&ctx.a,&ctx.b,0);
    },&context);
    require(context.calls == 1 && links.Size() == 1,"link callback can clear other records and add a new generation without stale iteration");
    links.Clear();
    require(links.Create(&b,&a,1) && links.Create(&c,&a,1),"container visitor setup");
    context.calls = 0;
    require(links.VisitContainers(&a,[](void* p,LTObject*) {
        auto& ctx = *static_cast<Context*>(p); ++ctx.calls;
        ctx.links.Clear(); ctx.links.Create(&ctx.b,&ctx.a,1); return true;
    },&context) && context.calls == 1 && links.Size() == 1,"container visitor tolerates callback retirement and skips newly created links");
    links.Clear();
    ad.m_Removing = true;
    require(!links.Create(&a,&b,0) && !links.Create(&b,&a,1) && !links.Attach(&a,&sound),"teardown rejects new links to either endpoint");
    require(ad.m_Links.IsTiedOff() && bd.m_Links.IsTiedOff() && cd.m_Links.IsTiedOff(),"all native endpoint links drain");
}
void wire_records() {
    auto check = [](const ServerMessage& packet, std::initializer_list<uint8> expected) {
        require(packet.m_DataLen == expected.size() + 1 && std::equal(expected.begin(),expected.end(),packet.m_pData + 1),"sound wire bytes match fixed-width fixture");
    };
    CPosRef reference; reference.m_vScale.Init(.01f,.01f,.01f);
    SoundRecord queued; queued.file_name = "sound.wav"; queued.file_id = 0x1234;
    queued.m_Info.m_nPriority = 5; queued.m_Info.m_UserData = 0x11223344;
    Client client;
    ServerMessage first;
    require(sound_WriteQueued(queued,client,first,&reference) == LT_OK,"queue local sound wire record");
    check(first,{0,0,0x34,0x12,7,0,0,5,0x44,0x33,0x22,0x11});
    ServerMessage repeat;
    require(sound_WriteQueued(queued,client,repeat,&reference) == LT_OK,"queue repeated local sound");
    check(repeat,{0,0,0x34,0x12,4,0x44,0x33,0x22,0x11});
    Client spatial_client;
    queued.m_Info.m_dwFlags = PLAYSOUND_3D | PLAYSOUND_CTRL_VOL | PLAYSOUND_CTRL_PITCH;
    queued.m_Info.m_fOuterRadius = 100; queued.m_Info.m_fInnerRadius = 50;
    queued.m_Info.m_nVolume = 50; queued.m_Info.m_fPitchShift = 1.5f;
    queued.m_Info.m_vPosition.Init(50,25,0);
    ServerMessage spatial;
    require(sound_WriteQueued(queued,spatial_client,spatial,&reference) == LT_OK,"queue full spatial sound wire record");
    check(spatial,{0,0,0x34,0x12,7,0xc2,0,5,100,0,127,50,0,0,0xc0,0x3f,0,0x80,0,0x40,0,0,0x44,0x33,0x22,0x11});
    require(!spatial_client.m_SoundStates.at(0x1234).m_ChangeFlags,"successful spatial send commits all state fields");
    Client failed_client;
    ServerMessage failed; failed.Init(4,4);
    require(sound_WriteQueued(queued,failed_client,failed,&reference) == LT_OUTOFMEMORY && failed.m_Pos == 1 && failed.m_DataLen == 1 &&
        failed_client.m_SoundStates.at(0x1234).m_ChangeFlags == 7,"failed append preserves queue delta state and packet extent");
    ServerMessage invalid;
    require(sound_WriteQueued(queued,failed_client,invalid,nullptr) == LT_INVALIDPARAMS && invalid.m_Pos == 1,"missing world position reference rejects complete record");
    queued.m_Info.m_vPosition.x = 100000;
    ServerMessage culled;
    require(sound_WriteQueued(queued,client,culled,&reference) == LT_OK && culled.m_Pos == 1,"inaudible queued sound emits no wire record");
    SoundInstance sound;
    CFileRef file; file.m_nFileID = 0x1234;
    CObjectIDRec id; id.m_nIndex = 42;
    sound.m_hFile = &file; sound.m_pUnknown0c = &id;
    sound.m_PlaySoundFlags = PLAYSOUND_TIME | PLAYSOUND_GETHANDLE | PLAYSOUND_TIMESYNC;
    sound.m_nPriority = 5; sound.m_UserData = 0x11223344;
    CObjInfo info; info.m_ChangeFlags = 1;
    Client kept_client; ServerMessage kept;
    require(sound_WriteInstance(sound,info,kept_client,kept,&reference,1) == LT_OK && sound.m_ListenerCount == 1,"kept sound adds listener hold after append");
    check(kept,{0,1,1,42,0,0x34,0x12,7,0x30,4,5,0xff,0xe8,3,0,0,0x44,0x33,0x22,0x11});
    info.m_ChangeFlags = 0x20;
    ServerMessage killed;
    require(sound_WriteInstance(sound,info,kept_client,killed,&reference,1) == LT_OK && sound.m_ListenerCount == 1,"kill update acquires no extra hold");
    check(killed,{0,1,0x20,42,0});
    info.m_ChangeFlags = 3;
    ServerMessage short_kept; short_kept.Init(3,3);
    require(sound_WriteInstance(sound,info,kept_client,short_kept,&reference,1) == LT_OUTOFMEMORY && sound.m_ListenerCount == 1 && info.m_ChangeFlags == 3,
        "failed kept sound append preserves listener count and change flags");
    LTObject actor;
    sound.m_PlaySoundFlags = PLAYSOUND_ATTACHED;
    sound.m_pObject = &actor; kept_client.m_pUnknown3AC = &actor;
    info.m_ChangeFlags = 3;
    ServerMessage attached;
    require(sound_WriteInstance(sound,info,kept_client,attached,nullptr,1) == LT_OK && info.m_ChangeFlags == 1,
        "client-owned attachment uses local playback without position reference");
    sound.m_pObject = nullptr; kept_client.m_pUnknown3AC = nullptr;
    sound.m_PlaySoundFlags = 0; sound.m_vPosition.x = std::numeric_limits<float>::max();
    info.m_ChangeFlags = 2;
    ServerMessage oversized;
    require(sound_WriteInstance(sound,info,kept_client,oversized,&reference,1) == LT_INVALIDPARAMS && oversized.m_Pos == 1,
        "out-of-range compressed position cannot trigger float-to-integer overflow");
}
void real_data(const std::filesystem::path& path) {
    nolf::Assets assets; assets.mount_game(path);
    CFileMgr files; files.mount_game(path); SoundCache cache(files);
    std::map<uint16, unsigned> formats;
    std::size_t loops = 0, total = 0;
    for (const auto& name : assets.names()) {
        if (!name.ends_with(".wav")) continue;
        CFileRef* file = nullptr; require(files.identify(name, 0, &file), "corpus sound identifier");
        try {
            auto track = cache.Load(*file);
            require(track->wave.duration() >= 0 && track->server_duration > 0, "corpus sound duration");
            ++formats[track->wave.format]; loops += track->wave.loop.has_value(); ++total;
        } catch (const std::exception& e) { throw std::runtime_error(name + ": " + e.what()); }
    }
    require(total > 0, "missing corpus sounds");
    std::cout << "Native sound metadata: " << total << " files, " << loops << " loops; codecs";
    for (const auto& [codec, count] : formats) std::cout << ' ' << codec << '=' << count;
    std::cout << '\n';
}
}
int main(int argc, char** argv) {
    try {
        parser_checks(); cache_checks(); requests_and_records(); links(); wire_records();
        if (argc == 3 && std::string_view(argv[1]) == "--data") real_data(argv[2]);
        else require(argc == 1, "usage: nolf-sound-tests [--data directory]");
        std::cout << "native sound metadata/cache tests passed\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
