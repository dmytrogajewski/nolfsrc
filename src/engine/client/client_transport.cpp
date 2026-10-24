#include "client_transport.h"
#include "engine/server/net_manager.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>

namespace nolf {
struct ClientTransport::Impl {
    struct Handler : CBaseMgr {
        NetConnection* connection = nullptr;
        LTBOOL _v04(NetConnection* value,uint32) override {
            if (connection) return LTFALSE;
            connection = value; return LTTRUE;
        }
        void _v08(NetConnection* value) override { if (connection == value) connection = nullptr; }
        void ProcessPacket(CPacket*,uint8*,uint16) override {}
    } handler;
    CNetMgr manager;
    explicit Impl(LTGUID application) {
        manager.SetNetName(const_cast<char*>("NATIVE_CLIENT"));
        manager.m_pHandler = &handler;
        manager.SetGuid(&application);
        if (manager.Init() != LT_OK) throw std::runtime_error("native client network initialization failed");
    }
};
ClientTransport::ClientTransport(LTGUID application) : impl_(std::make_unique<Impl>(application)) {}
ClientTransport::~ClientTransport() = default;
LTRESULT ClientTransport::connect(std::string_view address) {
    if (address.empty() || address.size() > 255 || address.find('\0') != address.npos) return LT_INVALIDPARAMS;
    disconnect();
    auto* driver = impl_->manager.FindDriver(const_cast<char*>("internet"));
    if (!driver) return LT_NOTINITIALIZED;
    return driver->ConnectTCP(std::string(address).c_str());
}
LTRESULT ClientTransport::connect_local(CBaseDriver& server) {
    disconnect();
    auto* driver = impl_->manager.FindDriver(const_cast<char*>("local"));
    if (!driver) return LT_NOTINITIALIZED;
    driver->Connect(&server);
    return LT_OK;
}
bool ClientTransport::connected() const { return impl_->handler.connection != nullptr; }
void ClientTransport::disconnect(std::uint32_t reason) {
    if (auto* connection = impl_->handler.connection) impl_->manager.Disconnect(connection,reason,0);
}
void ClientTransport::send(std::span<const std::byte> packet, bool reliable) {
    if (!connected() || packet.empty() || packet.size() > (reliable ? 65529 : 65507)) throw std::runtime_error("invalid client packet or disconnected transport");
    ServerMessage message;
    message.Init(static_cast<uint16>(std::min<std::size_t>(65535,packet.size()+64)),0x1000);
    if (message.m_Flags & PACKETFLAG_WRITEOVERFLOW) throw std::bad_alloc();
    message.m_pData[0] = std::to_integer<uint8>(packet[0]);
    if (!message.WriteData(packet.data()+1,packet.size()-1) ||
        !impl_->manager.SendToConnection(&message,impl_->handler.connection,reliable ? 0x80 : 0))
        throw std::runtime_error("native client packet send failed");
}
std::vector<PacketBytes> ClientTransport::pump(float time) {
    if (!std::isfinite(time) || time < 0) throw std::invalid_argument("invalid client network clock");
    auto& manager = impl_->manager;
    manager.Update(const_cast<char*>("Client: "),time,1);
    manager.StartGettingPackets();
    struct End { CNetMgr& manager; ~End() { manager.EndGettingPackets(); } } end{manager};
    std::vector<PacketBytes> packets;
    std::size_t bytes = 0;
    ServerMessage message;
    for (unsigned count = 0; count < 4096 && manager.GetPacket(&message); ++count) {
        if (!message.m_DataLen || message.m_DataLen > message.m_Data.GetSize()) throw std::runtime_error("invalid native transport packet extent");
        if (message.m_DataLen > 4*1024*1024-bytes) throw std::runtime_error("client receive budget exceeded");
        const auto* first = reinterpret_cast<const std::byte*>(message.m_pData);
        packets.emplace_back(first,first+message.m_DataLen);
        bytes += message.m_DataLen;
    }
    return packets;
}
}
