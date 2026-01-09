#include "GameHost.hpp"
#include <iostream>
#include <cstring>
#include <algorithm>

static float clampf(float v, float lo, float hi) {
    return (v < lo) ? lo : (v > hi) ? hi : v;
}

bool GameHost::start(ISteamNetworkingSockets* iface, uint16_t port, uint32_t worldSeed) {
    m_iface = iface;
    m_port = port;
    m_worldSeed = worldSeed;

    // init state (host is player 0)
    for (uint8_t i = 0; i < game::kMaxPlayers; ++i) {
        m_state[i].id = i;
        m_state[i].x = 200.f + 90.f * i;
        m_state[i].y = 200.f;
        m_inputX[i] = 0;
        m_inputY[i] = 0;
    }

    SteamNetworkingIPAddr addr;
    addr.Clear();
    addr.m_port = port;

    m_listen = m_iface->CreateListenSocketIP(addr, 0, nullptr);
    if (m_listen == k_HSteamListenSocket_Invalid) {
        std::cerr << "[Host] CreateListenSocketIP failed\n";
        return false;
    }

    m_poll = m_iface->CreatePollGroup();
    if (m_poll == k_HSteamNetPollGroup_Invalid) {
        std::cerr << "[Host] CreatePollGroup failed\n";
        return false;
    }

    std::cout << "[Host] Listening on port " << port << " (worldSeed=" << worldSeed << ")\n";
    return true;
}

void GameHost::stop() {
    if (!m_iface) return;

    for (auto c : m_clients) {
        m_iface->CloseConnection(c, 0, "host stop", false);
    }
    m_clients.clear();
    m_connToId.clear();

    if (m_poll != k_HSteamNetPollGroup_Invalid) {
        m_iface->DestroyPollGroup(m_poll);
        m_poll = k_HSteamNetPollGroup_Invalid;
    }
    if (m_listen != k_HSteamListenSocket_Invalid) {
        m_iface->CloseListenSocket(m_listen);
        m_listen = k_HSteamListenSocket_Invalid;
    }
}

uint8_t GameHost::pickFreeClientSlot() const {
    // client slots are 1 and 2
    bool used1 = false, used2 = false;
    for (auto& kv : m_connToId) {
        if (kv.second == 1) used1 = true;
        if (kv.second == 2) used2 = true;
    }
    if (!used1) return 1;
    if (!used2) return 2;
    return 255;
}

void GameHost::onConnStatusChanged(SteamNetConnectionStatusChangedCallback_t* info) {
    // Only care about connects targeting our listen socket
    if (info->m_info.m_hListenSocket != m_listen) return;

    const auto st = info->m_info.m_eState;
    const auto conn = info->m_hConn;

    if (st == k_ESteamNetworkingConnectionState_Connecting) {
        if ((int)m_clients.size() >= 2) {
            m_iface->CloseConnection(conn, 0, "Server full", false);
            return;
        }
        if (m_iface->AcceptConnection(conn) != k_EResultOK) {
            m_iface->CloseConnection(conn, 0, "AcceptConnection failed", false);
            return;
        }
        m_iface->SetConnectionPollGroup(conn, m_poll);
        return;
    }

    if (st == k_ESteamNetworkingConnectionState_Connected) {
        uint8_t slot = pickFreeClientSlot();
        if (slot > 2) {
            m_iface->CloseConnection(conn, 0, "No slot", false);
            return;
        }

        m_clients.push_back(conn);
        m_connToId[conn] = slot;

        sendWelcome(conn, slot);
        // Push an immediate snapshot so the client sees something right away
        sendSnap(conn, true);

        // If game already started, bring this late joiner in immediately.
        if (m_gameStarted) {
            sendStartGame(conn);
        }

        std::cout << "[Host] Client connected -> id=" << (int)slot << "\n";
        return;
    }

    if (st == k_ESteamNetworkingConnectionState_ClosedByPeer ||
        st == k_ESteamNetworkingConnectionState_ProblemDetectedLocally) {
        auto it = m_connToId.find(conn);
        if (it != m_connToId.end()) {
            uint8_t id = it->second;
            m_inputX[id] = 0;
            m_inputY[id] = 0;
            m_connToId.erase(it);
        }
        m_clients.erase(std::remove(m_clients.begin(), m_clients.end(), conn), m_clients.end());
        m_iface->CloseConnection(conn, 0, "cleanup", false);
        std::cout << "[Host] Client disconnected\n";
        return;
    }
}

void GameHost::pumpNetwork() {
    if (!m_iface || m_poll == k_HSteamNetPollGroup_Invalid) return;

    SteamNetworkingMessage_t* msgs[64];
    for (;;) {
        int n = m_iface->ReceiveMessagesOnPollGroup(m_poll, msgs, 64);
        if (n <= 0) break;

        for (int i = 0; i < n; ++i) {
            handleMessage(msgs[i]->m_conn, msgs[i]->m_pData, (uint32_t)msgs[i]->m_cbSize);
            msgs[i]->Release();
        }
    }
}

void GameHost::handleMessage(HSteamNetConnection from, const void* data, uint32_t size) {
    if (size < 1) return;

    const auto type = *(const game::Type*)data;

    if (type == game::Type::Hello) {
        // nothing required; we already send Welcome on Connected
        return;
    }

    if (type == game::Type::Input) {
        if (size < sizeof(game::Input)) return;

        auto it = m_connToId.find(from);
        if (it == m_connToId.end()) return;

        const uint8_t assignedId = it->second;
        const auto* in = (const game::Input*)data;

        // Trust the connection->id mapping, not the packet's playerId
        m_inputX[assignedId] = (int8_t)std::clamp<int>(in->moveX, -1, 1);
        m_inputY[assignedId] = (int8_t)std::clamp<int>(in->moveY, -1, 1);
        return;
    }
}

void GameHost::sendWelcome(HSteamNetConnection to, uint8_t assignedId) {
    game::Welcome w{};
    w.type = game::Type::Welcome;
    w.yourId = assignedId;
    w.worldSeed = m_worldSeed;

    m_iface->SendMessageToConnection(to, &w, sizeof(w), k_nSteamNetworkingSend_Reliable, nullptr);
}

void GameHost::sendSnap(HSteamNetConnection to, bool reliable) {
    game::Snap s{};
    s.type = game::Type::Snap;
    s.serverTick = m_serverTick;
    s.count = game::kMaxPlayers;

    for (uint8_t i = 0; i < game::kMaxPlayers; ++i) s.players[i] = m_state[i];

    const int flags = reliable ? k_nSteamNetworkingSend_Reliable : k_nSteamNetworkingSend_Unreliable;
    m_iface->SendMessageToConnection(to, &s, sizeof(s), flags, nullptr);
}

void GameHost::broadcastSnap() {
    for (auto c : m_clients) {
        sendSnap(c, false);
    }
}

void GameHost::updateSim(float dt, int8_t hostMoveX, int8_t hostMoveY) {
    // host local input drives player 0
    m_inputX[0] = (int8_t)std::clamp<int>(hostMoveX, -1, 1);
    m_inputY[0] = (int8_t)std::clamp<int>(hostMoveY, -1, 1);

    const float speed = 240.f;
    for (uint8_t i = 0; i < game::kMaxPlayers; ++i) {
        m_state[i].x += (float)m_inputX[i] * speed * dt;
        m_state[i].y += (float)m_inputY[i] * speed * dt;

        // simple bounds for now (matches 1280x720 window)
        m_state[i].x = clampf(m_state[i].x, 0.f, 1280.f);
        m_state[i].y = clampf(m_state[i].y, 0.f, 720.f);
    }

    ++m_serverTick;

    // Snapshot at 20 Hz
    m_snapAccum += dt;
    const float snapDt = 1.f / 20.f;
    if (m_snapAccum >= snapDt) {
        m_snapAccum -= snapDt;
        broadcastSnap();
    }
}

void GameHost::sendStartGame(HSteamNetConnection to) {
    game::StartGame m{};
    m.type = game::Type::StartGame;
    m.worldSeed = m_worldSeed;

    m_iface->SendMessageToConnection(to, &m, sizeof(m), k_nSteamNetworkingSend_Reliable, nullptr);
}

void GameHost::startGame() {
    if (m_gameStarted) return;
    m_gameStarted = true;

    // reliable broadcast
    for (auto c : m_clients) {
        sendStartGame(c);
    }

    std::cout << "[Host] StartGame broadcast (seed=" << m_worldSeed << ")\n";
}