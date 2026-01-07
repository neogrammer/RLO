#include "LobbyServer.hpp"
#include <cstring>
#include <iostream>
#include <algorithm>

bool LobbyServer::start(ISteamNetworkingSockets* iface, uint16_t port) {
    m_iface = iface;

    SteamNetworkingIPAddr addr;
    addr.Clear();
    addr.m_port = port;

    m_listen = m_iface->CreateListenSocketIP(addr, 0, nullptr);
    if (m_listen == k_HSteamListenSocket_Invalid) {
        std::cerr << "[Lobby] CreateListenSocketIP failed\n";
        return false;
    }

    m_poll = m_iface->CreatePollGroup();
    if (m_poll == k_HSteamNetPollGroup_Invalid) {
        std::cerr << "[Lobby] CreatePollGroup failed\n";
        return false;
    }

    std::cout << "[Lobby] Listening on UDP port " << port << "\n";
    return true;
}

void LobbyServer::stop() {
    if (!m_iface) return;

    for (auto& kv : m_connToSession) {
        m_iface->CloseConnection(kv.first, 0, "lobby stop", false);
    }

    m_connToSession.clear();
    m_sessions.clear();

    if (m_poll != k_HSteamNetPollGroup_Invalid) {
        m_iface->DestroyPollGroup(m_poll);
        m_poll = k_HSteamNetPollGroup_Invalid;
    }
    if (m_listen != k_HSteamListenSocket_Invalid) {
        m_iface->CloseListenSocket(m_listen);
        m_listen = k_HSteamListenSocket_Invalid;
    }
}

bool LobbyServer::fillRemoteIPv4(HSteamNetConnection from, uint32_t& outIpHostOrder) {
    SteamNetConnectionInfo_t ci{};
    if (!m_iface->GetConnectionInfo(from, &ci)) return false;

    const uint32_t ip = ci.m_addrRemote.GetIPv4();
    if (ip == 0) return false; // ipv6-only not handled in this prototype

    outIpHostOrder = ip; // host order
    return true;
}

void LobbyServer::markMigrating(uint64_t sessionKey) {
    auto it = m_sessions.find(sessionKey);
    if (it == m_sessions.end()) return;

    auto& s = it->second;
    s.state = lobby::SessionState::Migrating;
    s.ownerConn = k_HSteamNetConnection_Invalid;
    s.migratingSince = Clock::now();
}

void LobbyServer::onConnStatusChanged(SteamNetConnectionStatusChangedCallback_t* info) {
    const auto state = info->m_info.m_eState;
    const auto conn = info->m_hConn;

    // Only care about conns on our listen socket
    if (info->m_info.m_hListenSocket != m_listen) return;

    if (state == k_ESteamNetworkingConnectionState_Connecting) {
        // Accept quickly
        if (m_iface->AcceptConnection(conn) != k_EResultOK) {
            m_iface->CloseConnection(conn, 0, "AcceptConnection failed", false);
            return;
        }
        m_iface->SetConnectionPollGroup(conn, m_poll);
        return;
    }

    if (state == k_ESteamNetworkingConnectionState_ClosedByPeer ||
        state == k_ESteamNetworkingConnectionState_ProblemDetectedLocally) {

        // If this connection owned a session, mark it migrating (grace period)
        auto it = m_connToSession.find(conn);
        if (it != m_connToSession.end()) {
            const uint64_t key = it->second;
            m_connToSession.erase(it);
            markMigrating(key);
        }

        m_iface->CloseConnection(conn, 0, "cleanup", false);
        return;
    }
}

void LobbyServer::handleMessage(HSteamNetConnection from, const void* data, uint32_t size) {
    if (size < 1) return;
    const auto type = *(const lobby::Type*)data;

    if (type == lobby::Type::Hello) {
        // optional; ignore
        return;
    }

    if (type == lobby::Type::Announce || type == lobby::Type::Claim) {
        if (size < sizeof(lobby::Announce)) return;
        const auto* a = (const lobby::Announce*)data;
        if (a->protocol != lobby::kProtocol) return;
        if (a->sessionKey == 0) return;

        uint32_t ipHostOrder{};
        if (!fillRemoteIPv4(from, ipHostOrder)) return;

        const auto now = Clock::now();

        auto sit = m_sessions.find(a->sessionKey);
        const bool exists = (sit != m_sessions.end());

        // Claim rules:
        // - if session exists and is Migrating: accept first claim, replace ownerConn
        // - if session exists and is Open/Full: ignore Claim (prevents hijack)
        // - Announce always creates/updates (host normal behavior)
        if (type == lobby::Type::Claim) {
            if (!exists) return;
            if (sit->second.state != lobby::SessionState::Migrating) return;
        }

        Session s{};
        if (exists) s = sit->second;

        s.sessionKey = a->sessionKey;
        s.ownerConn = from;
        s.ipv4_host_order = ipHostOrder;
        s.gamePort = a->gamePort;
        s.maxPlayers = a->maxPlayers ? a->maxPlayers : 3;
        s.worldSeed = a->worldSeed;
        std::memcpy(s.name, a->name, sizeof(s.name));

        // On announce/claim, reset timers
        s.lastSeen = now;
        s.migratingSince = Clock::time_point{};

        // State computed from curPlayers unless migrating
        if (s.curPlayers >= s.maxPlayers) s.state = lobby::SessionState::Full;
        else s.state = lobby::SessionState::Open;

        m_sessions[a->sessionKey] = s;
        m_connToSession[from] = a->sessionKey;
        return;
    }

    if (type == lobby::Type::Heartbeat) {
        if (size < sizeof(lobby::Heartbeat)) return;
        const auto* hb = (const lobby::Heartbeat*)data;
        if (hb->sessionKey == 0) return;

        // Only accept heartbeat from the current owner conn
        auto sit = m_sessions.find(hb->sessionKey);
        if (sit == m_sessions.end()) return;

        auto& s = sit->second;
        if (s.ownerConn != from) return;
        if (s.state == lobby::SessionState::Migrating) return;

        s.curPlayers = (uint8_t)std::clamp<uint16_t>(hb->curPlayers, 1, s.maxPlayers);
        s.lastSeen = Clock::now();
        s.state = (s.curPlayers >= s.maxPlayers) ? lobby::SessionState::Full : lobby::SessionState::Open;
        return;
    }

    if (type == lobby::Type::ListReq) {
        if (size < sizeof(lobby::ListReq)) return;
        const auto* lr = (const lobby::ListReq*)data;
        if (lr->protocol != lobby::kProtocol) return;
        sendList(from);
        return;
    }
}

void LobbyServer::cleanupExpired() {
    const auto now = Clock::now();

    for (auto it = m_sessions.begin(); it != m_sessions.end(); ) {
        auto& s = it->second;

        // Active sessions: if TTL exceeded, mark migrating
        if (s.state != lobby::SessionState::Migrating) {
            if (now - s.lastSeen > kActiveTTL) {
                markMigrating(s.sessionKey);
                // refresh iterator reference after potential overwrite
                it = m_sessions.find(s.sessionKey);
                if (it == m_sessions.end()) continue;
                ++it;
                continue;
            }
        }
        else {
            // Migrating: if grace exceeded, delete
            if (s.migratingSince != Clock::time_point{} && now - s.migratingSince > kGraceTTL) {
                it = m_sessions.erase(it);
                continue;
            }
        }

        ++it;
    }
}

void LobbyServer::sendList(HSteamNetConnection to) {
    cleanupExpired();

    std::vector<lobby::SessionEntry> entries;
    entries.reserve(m_sessions.size());

    for (auto& kv : m_sessions) {
        const auto& s = kv.second;

        lobby::SessionEntry e{};
        e.sessionKey = s.sessionKey;
        e.ipv4_host_order = s.ipv4_host_order;
        e.gamePort = s.gamePort;
        e.curPlayers = s.curPlayers;
        e.maxPlayers = s.maxPlayers;
        e.worldSeed = s.worldSeed;
        e.state = s.state;
        std::memcpy(e.name, s.name, sizeof(e.name));

        entries.push_back(e);
    }

    lobby::ListRespHdr hdr{};
    hdr.type = lobby::Type::ListResp;
    hdr.count = (uint16_t)std::min<size_t>(entries.size(), 512);

    const size_t bytes = sizeof(hdr) + (size_t)hdr.count * sizeof(lobby::SessionEntry);
    std::vector<uint8_t> buf(bytes);

    std::memcpy(buf.data(), &hdr, sizeof(hdr));
    if (hdr.count) {
        std::memcpy(buf.data() + sizeof(hdr), entries.data(), (size_t)hdr.count * sizeof(lobby::SessionEntry));
    }

    m_iface->SendMessageToConnection(
        to, buf.data(), (uint32)buf.size(),
        k_nSteamNetworkingSend_Reliable, nullptr
    );
}

void LobbyServer::pump() {
    if (!m_iface || m_poll == k_HSteamNetPollGroup_Invalid) return;

    cleanupExpired();

    SteamNetworkingMessage_t* msgs[32];
    for (;;) {
        const int n = m_iface->ReceiveMessagesOnPollGroup(m_poll, msgs, 32);
        if (n <= 0) break;

        for (int i = 0; i < n; ++i) {
            handleMessage(msgs[i]->m_conn, msgs[i]->m_pData, (uint32_t)msgs[i]->m_cbSize);
            msgs[i]->Release();
        }
    }
}