// Engine worker queues over the consolidated platform synchronization API.
#include "lthread.h"
#include "ltcodes.h"
#include "platform/system.h"
#include <chrono>
#include <cstring>
#include <new>
#include <stdexcept>

using namespace std::chrono_literals;
LCriticalSection g_ThreadCS;
LCriticalSection::LCriticalSection() = default;
LCriticalSection::~LCriticalSection() = default;
void LCriticalSection::Enter() { m_CS.lock(); }
void LCriticalSection::Leave() { m_CS.unlock(); }
LTBOOL LCriticalSection::IsValid() { return LTTRUE; }
LThreadMessage::LThreadMessage() = default;

void* tm_FindInList(void* start, const void* value, uint32 bytes) {
    if (!start || !value || bytes != sizeof(LThreadMessage*)) return nullptr;
    LThreadMessage* wanted = nullptr;
    std::memcpy(&wanted, value, sizeof(wanted));
    auto* first = static_cast<LThreadMessage*>(start);
    auto* current = first;
    do {
        if (current == wanted) return current;
        current = current->m_pPrev;
    } while (current && current != first);
    return nullptr;
}

LThreadQueue::LThreadQueue() = default;
LThreadQueue::~LThreadQueue() { Flush(); }
LTRESULT LThreadQueue::Init() { Flush(); return LT_OK; }
void LThreadQueue::Flush() {
    std::lock_guard lock(m_CS);
    while (auto* message = m_MessageList.m_pTail) {
        m_MessageList.Remove(message);
        delete message;
    }
    m_hEvent.reset();
}
LTRESULT LThreadQueue::PostMessage(LThreadMessage* message) {
    if (!message) return LT_INVALIDPARAMS;
    auto* copy = new (std::nothrow) LThreadMessage(*message);
    if (!copy) return LT_OUTOFMEMORY;
    std::lock_guard lock(m_CS);
    m_MessageList.Prepend(copy);
    m_hEvent.set();
    return LT_OK;
}
LTRESULT LThreadQueue::GetMessage(LThreadMessage* message, LTBOOL wait) {
    if (wait ? !m_hEvent.wait() : !m_hEvent.wait_for(0ms)) return LT_NOTFOUND;
    return TakeMessage(message) ? LT_OK : LT_NOTFOUND;
}
LTBOOL LThreadQueue::TakeMessage(LThreadMessage* destination) {
    std::lock_guard lock(m_CS);
    auto* message = m_MessageList.m_pTail;
    if (!message) { m_hEvent.reset(); return LTFALSE; }
    if (destination) {
        *destination = *message;
        destination->m_pNext = destination->m_pPrev = nullptr;
    }
    m_MessageList.Remove(message);
    delete message;
    if (!m_MessageList.m_pTail) m_hEvent.reset();
    return LTTRUE;
}
LTRESULT LThreadQueue::PeekMessage(LThreadMessage* destination) {
    if (!destination) return LT_INVALIDPARAMS;
    std::lock_guard lock(m_CS);
    if (!m_MessageList.m_pTail) return LT_NOTFOUND;
    *destination = *m_MessageList.m_pTail;
    destination->m_pNext = destination->m_pPrev = nullptr;
    return LT_OK;
}

LThread::LThread() = default;
LThread::~LThread() { Term(LTTRUE); }
LTRESULT LThread::Start(int priority) {
    if (priority < 0 || priority > 5) return LT_INVALIDPARAMS;
    if (m_hThread.joinable()) return LT_ERROR;
    m_IncomingQueue.Init();
    m_OutgoingQueue.Init();
    m_Stop = std::stop_source{};
    m_hEvent.reset();
    m_Failed = false;
    try {
        // Historical priority values are advisory; worker correctness never
        // depends on scheduler privileges or a platform-specific priority scale.
        m_hThread = std::jthread([this] {
            try { ThreadRun(); }
            catch (const std::exception& error) { m_Failed = true; nolf::platform::log("worker", error.what()); }
            catch (...) { m_Failed = true; nolf::platform::log("worker", "unknown worker exception"); }
            try { ThreadTerm(); }
            catch (...) { m_Failed = true; nolf::platform::log("worker", "worker cleanup failed"); }
        });
        return LT_OK;
    } catch (const std::system_error& error) {
        nolf::platform::log("worker", error.what());
        return LT_ERROR;
    }
}
LTRESULT LThread::Term(LTBOOL) {
    m_Stop.request_stop();
    m_hEvent.set();
    if (m_hThread.joinable()) {
        if (m_hThread.get_id() == std::this_thread::get_id()) return LT_ERROR;
        m_hThread.join();
    }
    return m_Failed ? LT_ERROR : LT_OK;
}
void LThread::ThreadRun() {
    const auto stop = m_Stop.get_token();
    while (!stop.stop_requested() && m_IncomingQueue.m_hEvent.wait(stop)) {
        if (stop.stop_requested()) break;
        LThreadMessage message;
        if (m_IncomingQueue.PeekMessage(&message) == LT_OK) {
            ProcessMessage(&message);
            m_IncomingQueue.TakeMessage(nullptr);
        }
    }
}
void LThread::ThreadTerm() { m_IncomingQueue.Flush(); m_OutgoingQueue.Flush(); }
void LThread::ProcessMessage(LThreadMessage*) {}
