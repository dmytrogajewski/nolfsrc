#include "engine/server/file_transfer.h"
#include "engine/server/servermgr.h"
#include "engine/server/filemgr.h"
#include "engine/portable/memory_stream.h"
#include "platform/system.h"
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <map>
#include <stdexcept>
#include <thread>

namespace {
void require(bool value, const char* why) { if (!value) throw std::runtime_error(why); }
class Handler final : public CBaseMgr {
public:
    NetConnection* connection = nullptr;
    LTBOOL _v04(NetConnection* value, uint32) override { connection = value; return LTTRUE; }
    void _v08(NetConnection*) override { connection = nullptr; }
    void ProcessPacket(CPacket*, uint8*, uint16) override {}
};
struct Pair {
    Handler left_handler, right_handler;
    CNetMgr left, right;
    float time = 0;
    explicit Pair(bool udp = false) {
        left.SetNetName(const_cast<char*>("transfer-server")); right.SetNetName(const_cast<char*>("transfer-client"));
        left.m_pHandler = &left_handler; right.m_pHandler = &right_handler;
        auto* first = left.AddDriver(const_cast<char*>(udp ? "internet" : "local"));
        auto* second = right.AddDriver(const_cast<char*>(udp ? "internet" : "local"));
        require(first && second, "native transfer transport drivers");
        if (udp) {
            NetHost info{};
            { nolf::platform::UdpSocket unused; info.m_Port = unused.port(); }
            info.m_dwMaxPlayers = 4;
            std::strcpy(info.m_sName,"Native file transfer");
            require(first->HostSession(&info) == LT_OK,"host actual UDP transfer session");
            std::jthread accept([&](std::stop_token stop) {
                ServerMessage message;
                while (!stop.stop_requested()) {
                    left.GetPacket(&message);
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }
            });
            const auto address = "127.0.0.1:" + std::to_string(info.m_Port);
            const auto result = second->ConnectTCP(address.c_str());
            accept.request_stop(); accept.join();
            require(result == LT_OK,"join actual UDP transfer session");
        } else first->Connect(second);
        update();
        require(left_handler.connection && right_handler.connection, "native transfer transport endpoints");
    }
    void update() {
        time += .02f;
        left.Update(const_cast<char*>("transfer server: "),time,0);
        right.Update(const_cast<char*>("transfer client: "),time,0);
    }
    bool receive(CNetMgr& manager, ServerMessage& message) {
        for (unsigned attempt = 0; attempt < 1000; ++attempt) {
            update();
            if (manager.GetPacket(&message)) return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return false;
    }
    void reply(CClientFiles& state, std::initializer_list<uint16> words) {
        ServerMessage message; message.m_pData[0] = 0x36;
        for (auto word : words) message.WriteWord(word);
        require(right.SendToConnection(&message,right_handler.connection,0x80), "send actual file reply packet");
        ServerMessage incoming;
        require(receive(left,incoming),"client file reply reaches server");
        require(state.Handle(incoming), "server accepts actual client file reply");
    }
    void grant(CClientFiles& state) {
        ServerMessage message; message.m_pData[0] = 0x38;
        require(right.SendToConnection(&message,right_handler.connection,0x80), "send actual chunk grant");
        ServerMessage incoming;
        require(receive(left,incoming),"client chunk grant reaches server");
        require(state.Handle(incoming), "server accepts actual chunk grant");
    }
};
struct Files {
    std::map<std::string,std::vector<std::byte>> bytes;
    unsigned opened = 0, released = 0;
    class Stream final : public CBaseStream {
    public:
        Stream(const std::vector<std::byte>& bytes, unsigned& released, bool fail)
            : data_(bytes), released_(released), fail_(fail) {}
        void Release() override { ++released_; delete this; }
        LTRESULT Read(void* output, uint32 size) override {
            if (fail_) { std::memset(output,0,size); return LT_ERROR; }
            return data_.Read(output,size);
        }
        LTRESULT Write(const void*, uint32) override { return LT_ERROR; }
        LTRESULT SeekTo(uint32 pos) override { return data_.SeekTo(pos); }
        LTRESULT GetPos(uint32* pos) override { return data_.GetPos(pos); }
        LTRESULT GetLen(uint32* size) override { return data_.GetLen(size); }
        LTRESULT ErrorStatus() override { return data_.ErrorStatus(); }
    private:
        nolf::MemoryStream data_;
        unsigned& released_;
        bool fail_;
    };
    static ILTStream* open(void* context, const char* name) {
        auto& self = *static_cast<Files*>(context);
        auto it = self.bytes.find(name);
        if (it == self.bytes.end()) return nullptr;
        ++self.opened;
        return new Stream(it->second,self.released,std::string_view(name) == "read-error.bin");
    }
};
CPacketHolderDesc descriptor(Pair& pair, Files& files) {
    return {Files::open,&files,&pair.left,pair.left_handler.connection};
}
void announcements_and_validation() {
    Pair pair; Files files;
    CClientFiles state(descriptor(pair,files),0);
    char owned_name[] = "MODELS\\Owned.ABC";
    require(state.Add(owned_name,123,42,3), "queue owned counted filename");
    std::memset(owned_name,'x',sizeof(owned_name)-1);
    for (unsigned id = 0; id < 150; ++id) {
        if (id == 42) continue;
        const auto name = "models/long_resource_name_" + std::to_string(id) + ".abc";
        require(state.Add(name.c_str(),id,id,2),"queue independent transient filename");
    }
    require(state.file_count() == 150 && state.counted_count() == 1,"native file counts");
    require(!state.Add("../private",0,151,2) && !state.Add("/absolute",0,151,2) &&
            !state.Add("C:\\private",0,151,2) && !state.Add(nullptr,0,151,2),"reject invalid transfer asset names");
    require(!state.Add("valid",0,0x8000,2) && !state.Add("different",123,42,2),"wire IDs and duplicate identity");
    require(state.Add("models/owned.abc",123,42,2) && state.file_count() == 150,"duplicate addition is idempotent");
    require(state.Announce(),"batch file announcements"); pair.update();
    std::map<uint16,std::pair<uint32,std::string>> announced;
    unsigned packets = 0;
    ServerMessage message;
    while (pair.right.GetPacket(&message)) {
        require((message.m_pData[0] & 0x3f) == 0x32 && message.m_DataLen <= 0x1000,"bounded file announcement framing");
        ++packets;
        while (message.m_Pos < message.m_DataLen) {
            const auto id = message.ReadWord(nullptr); const auto size = message.ReadDWord(nullptr);
            auto* name = message.ReadStringInPlace();
            require(name && announced.emplace(id,std::pair(size,std::string(name))).second,"terminated unique announcement entries");
        }
    }
    require(packets > 1 && announced.size() == 150 && announced[42] == std::pair(uint32(123),std::string("models/owned.abc")),"all filenames survive batching and caller destruction");
    ServerMessage malformed; malformed.m_pData[0] = 0x36; malformed.WriteWord(0x8000 | 42); malformed.WriteByte(1); malformed.m_Pos = 1;
    require(!state.Handle(malformed) && state.file_count() == 150,"truncated replies apply no partial mutations");
    malformed.m_DataLen = static_cast<uint16>(malformed.m_Data.GetSize() + 1);
    require(!state.Handle(malformed),"reject payload extent outside packet allocation");
    pair.reply(state,{uint16(0x8000 | 42),uint16(0x8000 | 149)});
    require(state.file_count() == 148 && state.counted_count() == 0,"already-owned replies remove actual queue records");
    require(state.Announce(),"repeat announcement is empty"); pair.update();
    require(!pair.right.GetPacket(&message),"announcements sent once");
    state.Update(std::numeric_limits<float>::quiet_NaN()); state.Update(-1);
    require(!state.transferring(),"invalid frame times leave transfer idle");
}
void complete_transfers(bool udp = false) {
    Pair pair(udp); Files files;
    auto& payload = files.bytes["large.bin"]; payload.resize(80000);
    for (std::size_t i = 0; i < payload.size(); ++i) payload[i] = std::byte((i*17 + i/193) & 255);
    files.bytes["empty.bin"] = {};
    CClientFiles state(descriptor(pair,files),0);
    require(state.Add("large.bin",uint32(payload.size()),1,2) && state.Add("empty.bin",0,2,2),"queue transfer payloads");
    pair.reply(state,{1});
    state.Update(.1f); require(state.transferring() && files.opened == 1,"open requested file");
    std::vector<std::byte> received;
    unsigned chunks = 0;
    auto drain = [&](bool empty = false) {
        const auto before = chunks;
        for (unsigned attempt = 0; attempt < 1000; ++attempt) {
            pair.update(); ServerMessage packet;
            while (pair.right.GetPacket(&packet)) {
                require((packet.m_pData[0] & 0x3f) == 0x35,"raw file block packet ID");
                ++chunks;
                for (uint32 i = 1; i < packet.m_DataLen; ++i) received.push_back(std::byte(packet.m_pData[i]));
            }
            if (received.size() == state.bytes_sent() && (!empty || chunks > before)) return;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        throw std::runtime_error("sent file blocks reach native client");
    };
    for (unsigned i = 0; i < 10; ++i) { state.Update(.1f); drain(); }
    require(chunks == 6 && state.transferring(),"six-block window waits for client grant");
    for (unsigned step = 0; state.transferring() && step < 30; ++step) {
        if (step % 6 == 0) pair.grant(state);
        state.Update(.1f); drain();
    }
    require(!state.transferring() && received == payload && state.bytes_sent() == payload.size() && state.file_count() == 1,
            "large file sends exact bytes and reaches completion");
    pair.reply(state,{2}); state.Update(.1f); state.Update(.1f); drain(true);
    require(!state.transferring() && state.file_count() == 0 && received == payload && chunks == 21,"empty file sends one empty block and completes");
    require(files.opened == 2 && files.released == 2,"completed streams released exactly once");
}
void failures_and_lifetime() {
    Pair pair; Files files;
    files.bytes["mismatch.bin"].resize(1);
    CClientFiles state(descriptor(pair,files),0);
    require(state.Add("missing.bin",2,1,3) && state.Add("mismatch.bin",2,2,2),"queue unavailable resources");
    pair.reply(state,{1,2}); state.Update(.1f); state.Update(.1f);
    require(!state.file_count() && !state.counted_count() && !state.transferring(),"open and size failures remove records and counters");
    require(files.opened == files.released,"rejected stream released");
    files.bytes["read-error.bin"].resize(10);
    require(state.Add("read-error.bin",10,4,2),"queue read failure");
    pair.reply(state,{4}); state.Update(.1f); state.Update(.1f); pair.update();
    ServerMessage failed;
    require(pair.right.GetPacket(&failed) && (failed.m_pData[0] & 0x3f) == 0x34 &&
            !state.file_count() && files.opened == files.released,"failed read sends cancellation and releases without transmitting garbage");
    files.bytes["cancel.bin"].resize(9000);
    require(state.Add("cancel.bin",9000,3,2),"queue cancelled resource");
    pair.reply(state,{3}); state.Update(.1f); state.Cancel(); pair.update();
    ServerMessage message;
    require(pair.right.GetPacket(&message) && (message.m_pData[0] & 0x3f) == 0x34 && !state.transferring(),"cancel sends CFT and releases active stream");
    pair.reply(state,{3}); state.Update(.1f);
    pair.left.Disconnect(pair.left_handler.connection,1,0); pair.update();
    state.Update(.1f); state.Cancel();
    require(!state.transferring(),"disconnected connection pointer is never dereferenced by transfer");
    require(files.opened == files.released,"cancelled streams released exactly once");
    auto desc = descriptor(pair,files);
    require(!ph_CreateHolder(nullptr,0) && !ph_CreateHolder(&desc,0),"invalid transfer descriptors fail cleanly");
}
void mounted_asset_transfer() {
    struct TemporaryTree {
        std::filesystem::path root = std::filesystem::temp_directory_path() /
            ("nolf-transfer-" + std::to_string(nolf::platform::monotonic_nanoseconds()));
        TemporaryTree() { std::filesystem::create_directory(root); }
        ~TemporaryTree() { std::error_code error; std::filesystem::remove_all(root,error); }
    } tree;
    std::vector<std::byte> expected(17003);
    for (std::size_t i = 0; i < expected.size(); ++i) expected[i] = std::byte(i % 251);
    nolf::platform::atomic_write(tree.root / "OriginalCase.BIN",expected);
    Pair pair;
    CFileMgr manager; manager.mount(tree.root);
    CPacketHolderDesc desc;
    desc.open = [](void* context, const char* name) { return fs_OpenFile(static_cast<CFileMgr*>(context),const_cast<char*>(name)); };
    desc.context = &manager; desc.manager = &pair.left; desc.connection = pair.left_handler.connection;
    auto* state = ph_CreateHolder(&desc,0);
    require(state && xfer_AddFile(state,"ORIGINALCASE.bin",uint32(expected.size()),17,2),"native mounted file adapter");
    pair.reply(*state,{17}); xfer_Update(state,.1f);
    // Stream ownership must remain valid after its catalogue is closed.
    fs_Term(&manager);
    std::vector<std::byte> received;
    for (unsigned step = 0; state->transferring() && step < 10; ++step) {
        xfer_Update(state,.1f); pair.update(); ServerMessage message;
        while (pair.right.GetPacket(&message)) {
            require((message.m_pData[0] & 0x3f) == 0x35,"mounted resource block framing");
            for (uint32 i = 1; i < message.m_DataLen; ++i) received.push_back(std::byte(message.m_pData[i]));
        }
    }
    require(!state->transferring() && !state->file_count() && received == expected,"mounted asset transfer outlives catalogue and preserves final partial block");
    xfer_Term(state);
}
}
int main() {
    try { announcements_and_validation(); complete_transfers(); complete_transfers(true); failures_and_lifetime(); mounted_asset_transfer(); std::cout << "native file transfer tests passed\n"; }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
