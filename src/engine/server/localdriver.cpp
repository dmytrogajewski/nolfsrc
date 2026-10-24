// In-process transport. Each end owns its connection and receives through
// a shared mailbox; callbacks run on that end's engine thread.
#include "net_driver.h"
#include "message.h"
#include <cstring>
#include <deque>
#include <memory>
#include <mutex>

extern int g_LocalNoFlag;
namespace {
class LocalDriver final : public CBaseDriver {
    struct Mailbox {
        std::mutex mutex;
        std::deque<std::vector<uint8>> packets;
        std::size_t bytes = 0;
        bool open = true;
        bool peer_closed = false;
    };
    struct Pending {
        std::shared_ptr<Mailbox> inbox;
        std::weak_ptr<Mailbox> outbox;
        uint32 flags = 0;
    };
    struct Control {
        std::mutex mutex;
        bool paired = false;
        Pending pending;
    };
    static void close_mailboxes(const std::shared_ptr<Mailbox>& inbox, const std::weak_ptr<Mailbox>& outbox) {
        if (inbox) {
            std::lock_guard lock(inbox->mutex);
            inbox->open = false;
            inbox->packets.clear();
            inbox->bytes = 0;
        }
        if (const auto peer = outbox.lock()) {
            std::lock_guard lock(peer->mutex);
            peer->peer_closed = true;
        }
    }
public:
    LocalDriver() { std::strcpy(m_Name, "local"); }
    ~LocalDriver() override { Term(); }
    LTBOOL Init() override { return LTTRUE; }
    void Term() override {
        if (accepting_) { deferred_stop_ = true; return; }
        deferred_stop_ = false;
        auto connection = std::move(connection_);
        const bool accepted = accepted_;
        accepted_ = pending_ = false;
        auto mailbox = std::move(mailbox_);
        auto peer = std::move(peer_);
        Pending unclaimed;
        {
            std::lock_guard lock(control_->mutex);
            control_->paired = false;
            unclaimed = std::move(control_->pending);
            control_->pending = {};
        }
        close_mailboxes(mailbox, peer);
        close_mailboxes(unclaimed.inbox, unclaimed.outbox);
        if (connection && accepted && host()) host()->close_connection(*connection);
    }
    void Connect(CBaseDriver* driver) override {
        auto* peer = dynamic_cast<LocalDriver*>(driver);
        if (!peer || peer == this || !host() || !peer->host()) return;
        auto left = std::make_shared<Mailbox>();
        auto right = std::make_shared<Mailbox>();
        const uint32 flags = g_LocalNoFlag ? 0 : 1;
        // Connect can be called while the other endpoint is ticking. Only
        // publish mailboxes here; each Update owns its connection and policy.
        std::scoped_lock lock(control_->mutex, peer->control_->mutex);
        if (control_->paired || peer->control_->paired) return;
        control_->pending = Pending{left, right, flags};
        peer->control_->pending = Pending{right, left, flags};
        control_->paired = peer->control_->paired = true;
    }
    void Update() override {
        if (!connection_) {
            Pending incoming;
            {
                std::lock_guard lock(control_->mutex);
                if (!control_->pending.inbox) return;
                incoming = std::move(control_->pending);
                control_->pending = {};
            }
            mailbox_ = std::move(incoming.inbox);
            peer_ = std::move(incoming.outbox);
            try { connection_ = std::make_unique<NetConnection>(); }
            catch (...) { Term(); throw; }
            connection_->m_pDriver = this;
            connection_->m_Flags = incoming.flags;
            pending_ = true;
        }
        bool closed;
        {
            std::lock_guard lock(mailbox_->mutex);
            closed = mailbox_->peer_closed;
        }
        if (closed) { Term(); return; }
        if (pending_) {
            pending_ = false;
            auto* connection = connection_.get();
            accepting_ = true;
            bool accepted;
            try { accepted = host()->accept_connection(*connection); }
            catch (...) { accepting_ = false; Term(); throw; }
            accepting_ = false;
            accepted_ = accepted;
            if (!accepted || deferred_stop_) { deferred_stop_ = false; Term(); }
        }
    }
    void Disconnect(NetConnection* connection, uint32, uint32) override {
        if (connection_.get() == connection) Term();
    }
    LTBOOL SendData(void* data, uint32 size, uint32, NetConnection* connection) override {
        if (connection != connection_.get() || !connection || (!data && size) || !size || size > 65535) return LTFALSE;
        const auto peer = peer_.lock();
        if (!peer) return LTFALSE;
        std::lock_guard lock(peer->mutex);
        if (!peer->open || peer->peer_closed || peer->packets.size() >= 16384 || size > 16 * 1024 * 1024 - peer->bytes) return LTFALSE;
        const auto* bytes = static_cast<const uint8*>(data);
        peer->packets.emplace_back(bytes, bytes + size);
        peer->bytes += size;
        return LTTRUE;
    }
    LTBOOL ReceivePacket(ServerMessage* message) override {
        if (!message || !connection_ || pending_ || !accepted_) return LTFALSE;
        std::lock_guard lock(mailbox_->mutex);
        if (mailbox_->packets.empty()) return LTFALSE;
        const auto& bytes = mailbox_->packets.front();
        if (!message->Reserve(static_cast<uint32>(bytes.size()))) return LTFALSE;
        std::memcpy(message->m_pData, bytes.data(), bytes.size());
        message->m_DataLen = static_cast<uint16>(bytes.size());
        message->m_Pos = 1;
        message->m_Flags = 0;
        message->m_pConnection = connection_.get();
        mailbox_->bytes -= bytes.size();
        mailbox_->packets.pop_front();
        return LTTRUE;
    }
    uint32 GetRecvSize() override { return 0; }
private:
    const std::shared_ptr<Control> control_ = std::make_shared<Control>();
    std::unique_ptr<NetConnection> connection_;
    std::shared_ptr<Mailbox> mailbox_;
    std::weak_ptr<Mailbox> peer_;
    bool pending_ = false, accepted_ = false, accepting_ = false, deferred_stop_ = false;
};
}
CBaseDriver* ldrv_Create() { return new LocalDriver; }
