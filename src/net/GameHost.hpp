#pragma once
#include <vector>
#include <unordered_map>
#include <cstdint>

#include <steam/steamnetworkingsockets.h>
#include <steam/isteamnetworkingsockets.h>
#include <steam/steamnetworkingtypes.h>

#include "GameProtocol.hpp"

class GameHost {
public:
    bool start(ISteamNetworkingSockets* iface, uint16_t port, uint32_t worldSeed);
    void stop();

    void onConnStatusChanged(SteamNetConnectionStatusChangedCallback_t* info);
    void pumpNetwork();

    // Call each frame: applies stored inputs and moves players, broadcasts snapshots at fixed rate.
    void updateSim(float dt, int8_t hostMoveX, int8_t hostMoveY);

    uint16_t port() const { return m_port; }
    HSteamListenSocket listenSocket() const { return m_listen; }

    uint8_t curPlayers() const { return (uint8_t)(1 + m_clients.size()); }
    uint32_t worldSeed() const { return m_worldSeed; }

    // For rendering on host
    const game::PlayerState* states() const { return m_state; }

private:
    void handleMessage(HSteamNetConnection from, const void* data, uint32_t size);
    void sendWelcome(HSteamNetConnection to, uint8_t assignedId);
    void sendSnap(HSteamNetConnection to, bool reliable);
    void broadcastSnap();

    uint8_t pickFreeClientSlot() const;

private:
    ISteamNetworkingSockets* m_iface{ nullptr };
    uint16_t m_port{ 0 };

    HSteamListenSocket m_listen{ k_HSteamListenSocket_Invalid };
    HSteamNetPollGroup m_poll{ k_HSteamNetPollGroup_Invalid };

    std::vector<HSteamNetConnection> m_clients; // up to 2
    std::unordered_map<HSteamNetConnection, uint8_t> m_connToId;

    uint32_t m_worldSeed{ 0 };

    // authoritative sim state
    game::PlayerState m_state[game::kMaxPlayers]{};
    int8_t m_inputX[game::kMaxPlayers]{};
    int8_t m_inputY[game::kMaxPlayers]{};

    uint32_t m_serverTick{ 0 };
    float m_snapAccum{ 0.f };
};