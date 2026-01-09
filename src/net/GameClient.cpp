#include "GameClient.hpp"
#include <iostream>
#include <cstring>
#include <algorithm>

bool GameClient::connect(ISteamNetworkingSockets* iface, const std::string& hostAddr) {
    m_iface = iface;

    SteamNetworkingIPAddr addr;
    addr.Clear();
    if (!addr.ParseString(hostAddr.c_str())) {
        std::cerr << "[Client] Bad host address: " << hostAddr << "\n";
        return false;
    }

    m_conn = m_iface->ConnectByIPAddress(addr, 0, nullptr);
    if (m_conn == k_HSteamNetConnection_Invalid) {
        std::cerr << "[Client] ConnectByIPAddress failed\n";
        return false;
    }

    return true;
}

void GameClient::disconnect(const char* reason) {
    if (!m_iface) return;
    if (m_conn != k_HSteamNetConnection_Invalid) {
        m_iface->CloseConnection(m_conn, 0, reason, false);
        m_conn = k_HSteamNetConnection_Invalid;
    }
    m_connected = false;
    m_myId = 255;
    m_hasSnap = false;
}

void GameClient::onConnStatusChanged(SteamNetConnectionStatusChangedCallback_t* info) {
    if (info->m_hConn != m_conn) return;

    const auto st = info->m_info.m_eState;

    if (st == k_ESteamNetworkingConnectionState_Connected) {
        m_connected = true;

        game::Hello h{};
        h.type = game::Type::Hello;
        h.protocol = game::kProtocol;
        m_iface->SendMessageToConnection(m_conn, &h, sizeof(h), k_nSteamNetworkingSend_Reliable, nullptr);

        std::cout << "[Client] Connected\n";
        return;
    }

    if (st == k_ESteamNetworkingConnectionState_ClosedByPeer ||
        st == k_ESteamNetworkingConnectionState_ProblemDetectedLocally) {
        m_connected = false;
        m_myId = 255;
        if (m_conn != k_HSteamNetConnection_Invalid) {
            m_iface->CloseConnection(m_conn, 0, "cleanup", false);
            m_conn = k_HSteamNetConnection_Invalid;
        }
        std::cout << "[Client] Disconnected\n";
        return;
    }
}

void GameClient::pumpNetwork() {
    if (!m_iface) return;
    if (m_conn == k_HSteamNetConnection_Invalid) return;

    SteamNetworkingMessage_t* msgs[64];
    for (;;) {
        int n = m_iface->ReceiveMessagesOnConnection(m_conn, msgs, 64);
        if (n <= 0) break;

        for (int i = 0; i < n; ++i) {
            handleMessage(msgs[i]->m_pData, (uint32_t)msgs[i]->m_cbSize);
            msgs[i]->Release();
        }
    }
}

void GameClient::handleMessage(const void* data, uint32_t size) {
    if (size < 1) return;

    const auto type = *(const game::Type*)data;

    if (type == game::Type::Welcome) {
        if (size < sizeof(game::Welcome)) return;
        game::Welcome w{};
        std::memcpy(&w, data, sizeof(w));
        m_myId = w.yourId;
        m_worldSeed = w.worldSeed;
        std::cout << "[Client] Welcome: myId=" << (int)m_myId << " seed=" << w.worldSeed << "\n";
        return;
    }

    if (type == game::Type::Snap) {
        if (size < sizeof(game::Snap)) return;
        std::memcpy(&m_latest, data, sizeof(game::Snap));
        m_hasSnap = true;
        return;
    }
    if (type == game::Type::StartGame) {
        if (size < sizeof(game::StartGame)) return;

        game::StartGame m{};
        std::memcpy(&m, data, sizeof(m));

        m_worldSeed = m.worldSeed;
        m_gameStarted = true;

        std::cout << "[Client] StartGame seed=" << m_worldSeed << "\n";
        return;
    }
}

void GameClient::sendInput(int8_t mx, int8_t my) {
    if (!m_connected) return;
    if (m_myId > 2) return;
    if (!m_gameStarted) return; // NEW: wait for host StartGame

    game::Input in{};
    in.type = game::Type::Input;
    in.clientTick = ++m_clientTick;
    in.playerId = m_myId;
    in.moveX = (int8_t)std::clamp<int>(mx, -1, 1);
    in.moveY = (int8_t)std::clamp<int>(my, -1, 1);

    m_iface->SendMessageToConnection(m_conn, &in, sizeof(in), k_nSteamNetworkingSend_Unreliable, nullptr);
}

bool GameClient::popLatestSnap(game::Snap& out) {
    if (!m_hasSnap) return false;
    out = m_latest;
    m_hasSnap = false;
    return true;
}