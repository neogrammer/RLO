#pragma once
#include <string>
#include <cstdint>
#include <optional>

#include <steam/steamnetworkingsockets.h>
#include <steam/isteamnetworkingsockets.h>
#include <steam/steamnetworkingtypes.h>
#include <steam/isteamnetworkingutils.h>

#include "GameProtocol.hpp"

class GameClient {
public:
    bool connect(ISteamNetworkingSockets* iface, const std::string& hostAddr);
    void disconnect(const char* reason = "bye");

    void onConnStatusChanged(SteamNetConnectionStatusChangedCallback_t* info);
    void pumpNetwork();

    bool isConnected() const { return m_connected; }
    HSteamNetConnection conn() const { return m_conn; }
    uint8_t myId() const { return m_myId; }

    void sendInput(int8_t mx, int8_t my);

    bool popLatestSnap(game::Snap& out);

private:
    void handleMessage(const void* data, uint32_t size);

private:
    ISteamNetworkingSockets* m_iface{ nullptr };
    HSteamNetConnection m_conn{ k_HSteamNetConnection_Invalid };
    bool m_connected{ false };

    uint8_t m_myId{ 255 };
    uint32_t m_clientTick{ 0 };

    bool m_hasSnap{ false };
    game::Snap m_latest{};
};