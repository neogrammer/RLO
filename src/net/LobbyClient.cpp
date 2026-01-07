#include "LobbyClient.hpp"
#include <iostream>
#include <cstring>
#include <random>
#include <algorithm>

bool LobbyClient::connect(ISteamNetworkingSockets* iface, const std::string& lobbyAddr, Role role) {
    m_iface = iface;
    m_role = role;

    SteamNetworkingIPAddr addr;
    addr.Clear();
    if (!addr.ParseString(lobbyAddr.c_str())) {
        std::cerr << "[LobbyClient] Bad lobby address: " << lobbyAddr << "\n";
        return false;
    }

    m_conn = m_iface->ConnectByIPAddress(addr, 0, nullptr);
    if (m_conn == k_HSteamNetConnection_Invalid) {
        std::cerr << "[LobbyClient] ConnectByIPAddress failed\n";
        return false;
    }

    return true;
}

void LobbyClient::disconnect(const char* reason) {
    if (!m_iface) return;
    if (m_conn != k_HSteamNetConnection_Invalid) {
        m_iface->CloseConnection(m_conn, 0, reason, false);
        m_conn = k_HSteamNetConnection_Invalid;
    }

    m_connected = false;
    m_hasList = false;
    m_latestList.clear();
}

void LobbyClient::onConnStatusChanged(SteamNetConnectionStatusChangedCallback_t* info) {
    if (info->m_hConn != m_conn) return;

    const auto st = info->m_info.m_eState;

    if (st == k_ESteamNetworkingConnectionState_Connected) {
        m_connected = true;

        // optional hello
        lobby::Hello h{};
        h.type = lobby::Type::Hello;
        h.protocol = lobby::kProtocol;
        h.role = (m_role == Role::Announcer) ? 1 : 0;
        m_iface->SendMessageToConnection(m_conn, &h, sizeof(h), k_nSteamNetworkingSend_Reliable, nullptr);

        // auto-announce if we’re an announcer
        if (m_role == Role::Announcer && m_hasAnnounce) {
            sendAnnounceNow();
        }
        return;
    }

    if (st == k_ESteamNetworkingConnectionState_ClosedByPeer ||
        st == k_ESteamNetworkingConnectionState_ProblemDetectedLocally) {
        m_connected = false;
        if (m_conn != k_HSteamNetConnection_Invalid) {
            m_iface->CloseConnection(m_conn, 0, "cleanup", false);
            m_conn = k_HSteamNetConnection_Invalid;
        }
        return;
    }
}

void LobbyClient::pump() {
    if (!m_iface) return;
    if (m_conn == k_HSteamNetConnection_Invalid) return;

    SteamNetworkingMessage_t* msgs[32];
    for (;;) {
        const int n = m_iface->ReceiveMessagesOnConnection(m_conn, msgs, 32);
        if (n <= 0) break;

        for (int i = 0; i < n; ++i) {
            handleMessage(msgs[i]->m_pData, (uint32_t)msgs[i]->m_cbSize);
            msgs[i]->Release();
        }
    }
}

void LobbyClient::requestList() {
    if (!m_connected) return;

    lobby::ListReq r{};
    r.type = lobby::Type::ListReq;
    r.protocol = lobby::kProtocol;

    m_iface->SendMessageToConnection(m_conn, &r, sizeof(r), k_nSteamNetworkingSend_Reliable, nullptr);
}

bool LobbyClient::popLatestList(std::vector<lobby::SessionEntry>& out) {
    if (!m_hasList) return false;
    out = m_latestList;
    m_hasList = false;
    return true;
}

uint64_t LobbyClient::genSessionKey() {
    static std::mt19937_64 rng{ std::random_device{}() };
    uint64_t k = 0;
    while (k == 0) k = rng();
    return k;
}

void LobbyClient::setSessionKey(uint64_t key) {
    m_sessionKey = key ? key : genSessionKey();
    // if announce already prepared, keep it coherent
    if (m_hasAnnounce) {
        m_announce.sessionKey = m_sessionKey;
    }
}

void LobbyClient::setAnnounceInfo(uint16_t gamePort, uint8_t maxPlayers, uint32_t worldSeed, const std::string& name) {
    if (m_sessionKey == 0) m_sessionKey = genSessionKey();
    setAnnounceInfo(m_sessionKey, gamePort, maxPlayers, worldSeed, name);
}

void LobbyClient::setAnnounceInfo(uint64_t sessionKey, uint16_t gamePort, uint8_t maxPlayers, uint32_t worldSeed, const std::string& name) {
    m_sessionKey = sessionKey ? sessionKey : genSessionKey();

    std::memset(&m_announce, 0, sizeof(m_announce));
    m_announce.type = lobby::Type::Announce;
    m_announce.protocol = lobby::kProtocol;
    m_announce.sessionKey = m_sessionKey;
    m_announce.gamePort = gamePort;
    m_announce.maxPlayers = maxPlayers ? maxPlayers : 3;
    m_announce.worldSeed = worldSeed;

    std::snprintf(m_announce.name, sizeof(m_announce.name), "%s", name.c_str());
    m_hasAnnounce = true;
}

void LobbyClient::sendAnnounceNow() {
    if (!m_connected || !m_hasAnnounce) return;

    m_announce.type = lobby::Type::Announce;
    m_iface->SendMessageToConnection(m_conn, &m_announce, sizeof(m_announce),
        k_nSteamNetworkingSend_Reliable, nullptr);
}

void LobbyClient::sendClaimNow() {
    if (!m_connected || !m_hasAnnounce) return;

    m_announce.type = lobby::Type::Claim;
    m_iface->SendMessageToConnection(m_conn, &m_announce, sizeof(m_announce),
        k_nSteamNetworkingSend_Reliable, nullptr);

    // restore default type for future announce calls
    m_announce.type = lobby::Type::Announce;
}

void LobbyClient::sendHeartbeat(uint16_t curPlayers) {
    if (!m_connected) return;
    if (m_sessionKey == 0) return;

    lobby::Heartbeat hb{};
    hb.type = lobby::Type::Heartbeat;
    hb.sessionKey = m_sessionKey;
    hb.curPlayers = (uint16_t)std::clamp<int>((int)curPlayers, 1, 65535);

    m_iface->SendMessageToConnection(m_conn, &hb, sizeof(hb),
        k_nSteamNetworkingSend_Unreliable, nullptr);
}

void LobbyClient::handleMessage(const void* data, uint32_t size) {
    if (size < 1) return;

    const auto type = *(const lobby::Type*)data;

    if (type == lobby::Type::ListResp) {
        if (size < sizeof(lobby::ListRespHdr)) return;

        lobby::ListRespHdr hdr{};
        std::memcpy(&hdr, data, sizeof(hdr));

        const size_t expect = sizeof(lobby::ListRespHdr) + (size_t)hdr.count * sizeof(lobby::SessionEntry);
        if (size < expect) return;

        m_latestList.resize(hdr.count);
        if (hdr.count) {
            std::memcpy(m_latestList.data(),
                (const uint8_t*)data + sizeof(lobby::ListRespHdr),
                (size_t)hdr.count * sizeof(lobby::SessionEntry));
        }

        m_hasList = true;
        return;
    }

    // ignore everything else for now
}