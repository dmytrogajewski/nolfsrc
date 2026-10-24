// GameSpy v1 query responder. Transport and name resolution are supplied by
// the native platform/engine services; protocol state belongs to each manager.
#include "GameSpyMgr.h"
#include "gs_protocol.h"
#include "platform/sockets.h"
#include "platform/system.h"
#include <charconv>
#include <optional>
#include <string>
#include <utility>

namespace {
constexpr std::size_t packet_limit = 1300;
constexpr std::uint64_t heartbeat_period = 60ull * 1000000000;
constexpr std::uint64_t resolve_period = 30ull * 60 * 1000000000;
struct Response {
    std::string address, text, query;
    DWORD port = 0, id = 0;
    unsigned packet = 1;
    std::size_t cursor = 0;
};
std::string next_token(Response& response) {
    auto& pos = response.cursor;
    while (pos < response.query.size() && response.query[pos] == '\\') ++pos;
    const auto start = pos;
    while (pos < response.query.size() && response.query[pos] != '\\') ++pos;
    return response.query.substr(start, pos - start);
}
}
struct CGameSpyMgr::State {
    bool alive = true;
    std::shared_ptr<Response> response;
    std::optional<nolf::platform::Endpoint> endpoint;
    std::optional<std::uint64_t> heartbeat;
    std::uint64_t resolved_at = 0;
    std::string requested_master;
    std::optional<nolf::platform::Endpoint> master(const char* configured) {
        const std::string requested = configured && *configured ? configured : "master.openspy.net";
        if (nolf::ascii_compare(requested.c_str(), "none") == 0) { endpoint.reset(); return {}; }
        const auto now = nolf::platform::monotonic_nanoseconds();
        if (endpoint && requested_master == requested && now - resolved_at < resolve_period) return endpoint;
        requested_master = requested; resolved_at = now; endpoint.reset();
        nolf::platform::Endpoint host{requested, 27900};
        if (const auto colon = requested.find(':'); colon != requested.npos) {
            host.address = requested.substr(0, colon);
            unsigned port = 0;
            const auto part = std::string_view(requested).substr(colon + 1);
            const auto [end, error] = std::from_chars(part.data(), part.data() + part.size(), port);
            if (error != std::errc{} || end != part.data() + part.size() || !port || port > 65535) return {};
            host.port = static_cast<std::uint16_t>(port);
        }
        try { endpoint = nolf::platform::resolve_ipv4(host); }
        catch (const std::exception& error) { nolf::platform::log("master", error.what()); }
        return endpoint;
    }
};
CGameSpyMgr::CGameSpyMgr() : state_(std::make_shared<State>()) { Clear(); }
CGameSpyMgr::~CGameSpyMgr() {
    state_->alive = false;
    try { Term(); } catch (...) {}
}
void CGameSpyMgr::Clear() {
    m_sGameName.Empty(); m_sGameVer.Empty(); m_sGameKey.Empty(); m_sGameLoc.Empty();
    m_sGameSpyAddr.Empty(); m_sResponseAddr.Empty(); m_sResponse[0] = 0;
    m_dwQueryID = 1; m_nPacket = 0; m_timerHeartbeat = 0;
    m_dwGamePort = m_dwSpyPort = m_dwResponsePort = m_dwFlags = 0;
    m_pSender = nullptr; m_bInit = FALSE;
    state_->response.reset(); state_->endpoint.reset(); state_->heartbeat.reset();
}
BOOL CGameSpyMgr::Init(const char* name, const char* version, const char* key,
                       int location, int game_port, int query_port, DWORD flags) {
    const auto state = state_;
    Term();
    const bool query_port_valid = query_port >= 1 && query_port <= 65535;
    if (!state->alive || !name || !*name || game_port < 1 || game_port > 65535 ||
        (!query_port_valid && !(flags & GSMF_USEGAMEPORTFORHEARTBEAT))) return FALSE;
    m_sGameName = name; m_sGameVer = version ? version : ""; m_sGameKey = key ? key : "";
    m_sGameLoc.Format("%d", location);
    m_dwGamePort = game_port; m_dwSpyPort = query_port_valid ? query_port : game_port; m_dwFlags = flags; m_bInit = TRUE;
    return TRUE;
}
void CGameSpyMgr::Term() {
    const auto state = state_;
    const bool announce = m_bInit && m_pSender && state->heartbeat && state->endpoint;
    auto* sender = m_pSender;
    const auto endpoint = state->endpoint;
    const auto text = "\\heartbeat\\" + std::to_string(m_dwGamePort) + "\\gamename\\" +
        std::string(static_cast<const char*>(m_sGameName)) + "\\statechanged\\2";
    m_bInit = FALSE; m_timerHeartbeat = 0; state->heartbeat.reset();
    if (announce) sender->SendTo(text.data(), text.size(), endpoint->address.c_str(), endpoint->port);
}
void CGameSpyMgr::Update() { if (m_bInit) UpdateHeartbeat(); }
BOOL CGameSpyMgr::UpdateHeartbeat() {
    const auto state = state_;
    if (!m_pSender) return FALSE;
    const auto now = nolf::platform::monotonic_nanoseconds();
    if (state->heartbeat && now - *state->heartbeat < heartbeat_period) return FALSE;
    state->heartbeat = now;
    const auto endpoint = state->master(m_sGameSpyAddr);
    if (!endpoint) return FALSE;
    const auto text = "\\heartbeat\\" + std::to_string(m_dwGamePort) + "\\gamename\\" +
        std::string(static_cast<const char*>(m_sGameName));
    return SendSocketString(endpoint->address.c_str(), endpoint->port, text.c_str());
}
BOOL CGameSpyMgr::OnQuery(CGameSpyQueryInfo* info) {
    if (!info || !info->IsValid()) return FALSE;
    auto data = info->GetData(), address = info->GetAddr();
    return OnQuery(address.GetBuffer(0), info->GetPort(), data.GetBuffer(0), data.GetLength());
}
BOOL CGameSpyMgr::OnQuery(char* address, DWORD port, char* data, int length) {
    if (!m_bInit || !address || !data || length <= 0 || length >= MAX_RESPONSE || !port || port > 65535) return FALSE;
    const auto state = state_;
    const auto previous = state->response;
    auto response = std::make_shared<Response>();
    response->address = address; response->port = port; response->id = ++m_dwQueryID;
    response->query.assign(data, length);
    if (response->query.find('\0') != response->query.npos) response->query.resize(response->query.find('\0'));
    state->response = response;
    struct Restore { std::shared_ptr<State> state; std::shared_ptr<Response> previous; ~Restore() { state->response = previous; } } restore{state, previous};
    BOOL answered = FALSE;
    char challenge[80];
    if (nolf::gamespy::find_key(response->query.c_str(), "secure", challenge, sizeof(challenge))) answered |= OnSecureQuery(challenge);
    for (;;) {
        const auto token = next_token(*response);
        if (token.empty()) break;
        if (nolf::ascii_compare(token.c_str(), "basic") == 0) answered |= OnBasicQuery();
        else if (nolf::ascii_compare(token.c_str(), "info") == 0) answered |= OnInfoQuery();
        else if (nolf::ascii_compare(token.c_str(), "rules") == 0) answered |= OnRulesQuery();
        else if (nolf::ascii_compare(token.c_str(), "players") == 0) answered |= OnPlayersQuery();
        else if (nolf::ascii_compare(token.c_str(), "status") == 0) answered |= OnStatusQuery();
        else if (nolf::ascii_compare(token.c_str(), "echo") == 0) answered |= OnEchoQuery(next_token(*response).c_str());
        else if (nolf::ascii_compare(token.c_str(), "secure") == 0) next_token(*response);
        if (!state->alive) return answered;
    }
    if (answered) SendResponse();
    return answered;
}
BOOL CGameSpyMgr::OnEchoQuery(const char* query) { SendResponseInfo("echo", query ? query : ""); return TRUE; }
BOOL CGameSpyMgr::OnSecureQuery(const char* value) {
    if (!value || !*value || m_sGameKey.IsEmpty()) return FALSE;
    char response[96];
    nolf::gamespy::validate(value, m_sGameKey, response);
    SendResponseInfo("validate", response);
    return TRUE;
}
BOOL CGameSpyMgr::OnBasicQuery() {
    const auto state = state_;
    const auto name = m_sGameName, version = m_sGameVer, location = m_sGameLoc;
    SendResponseInfo("gamename", name);
    if (!state->alive) return TRUE;
    SendResponseInfo("gamever", version);
    if (!state->alive) return TRUE;
    SendResponseInfo("location", location);
    return TRUE;
}
BOOL CGameSpyMgr::OnStatusQuery() {
    const auto state = state_;
    OnBasicQuery(); if (!state->alive) return TRUE;
    OnInfoQuery(); if (!state->alive) return TRUE;
    OnRulesQuery(); if (!state->alive) return TRUE;
    OnPlayersQuery(); return TRUE;
}
void CGameSpyMgr::SendResponseInfo(const char* key, const char* value) {
    const auto state = state_;
    const auto response = state->response;
    if (!response || !key) return;
    const auto text = '\\' + std::string(key) + '\\' + (value ? value : "");
    if (text.size() > MAX_RESPONSE) return;
    if (response->text.size() + text.size() > packet_limit) {
        FlushResponse();
        if (!state->alive) return;
    }
    response->text += text;
}
void CGameSpyMgr::SendResponseInfo(const char* key, int value) { SendResponseInfo(key, std::to_string(value).c_str()); }
void CGameSpyMgr::AddResponseQueryID() {
    const auto response = state_->response;
    if (response) response->text += "\\queryid\\" + std::to_string(response->id) + '.' + std::to_string(response->packet++);
}
void CGameSpyMgr::FlushResponse() {
    const auto response = state_->response;
    if (!response || response->text.empty()) return;
    AddResponseQueryID();
    const auto text = std::exchange(response->text, {});
    SendSocketString(response->address.c_str(), response->port, text.c_str());
}
void CGameSpyMgr::SendResponse() {
    const auto response = state_->response;
    if (!response) return;
    response->text += "\\final\\";
    FlushResponse();
}
BOOL CGameSpyMgr::SendSocketString(const char* address, DWORD port, const char* text, int length) {
    if (!m_pSender || !address || !text) return FALSE;
    const std::string data(text, length < 0 ? std::strlen(text) : static_cast<std::size_t>(length));
    const std::string peer(address);
    auto* sender = m_pSender;
    sender->SendTo(data.data(), data.size(), peer.c_str(), port);
    return TRUE;
}
