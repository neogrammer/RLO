#pragma once
#include <vector>
#include <string>
#include <cstdint>

#include <steam/steamnetworkingsockets.h>
#include <steam/isteamnetworkingsockets.h>
#include <steam/steamnetworkingtypes.h>
#include <steam/isteamnetworkingutils.h>

#include "LobbyProtocol.hpp"

class LobbyClient {
public:
    enum class Role { Browser, Announcer };

    bool connect(ISteamNetworkingSockets* iface, const std::string& lobbyAddr, Role role);
    void disconnect(const char* reason = "bye");

    void onConnStatusChanged(SteamNetConnectionStatusChangedCallback_t* info);
    void pump();

    bool isConnected() const { return m_connected; }
    HSteamNetConnection conn() const { return m_conn; }
    Role role() const { return m_role; }

    // Browser flow
    void requestList();
    bool popLatestList(std::vector<lobby::SessionEntry>& out);

    // Host announce flow (backward compatible overload auto-generates sessionKey)
    void setAnnounceInfo(uint16_t gamePort, uint8_t maxPlayers, uint32_t worldSeed, const std::string& name);
    void setAnnounceInfo(uint64_t sessionKey, uint16_t gamePort, uint8_t maxPlayers, uint32_t worldSeed, const std::string& name);

    uint64_t sessionKey() const { return m_sessionKey; }
    void setSessionKey(uint64_t key); // for migration: preserve existing sessionKey

    void sendAnnounceNow();            // reliable
    void sendHeartbeat(uint16_t curPlayers); // unreliable
    void sendClaimNow();               // reliable (type=Claim)

private:
    void handleMessage(const void* data, uint32_t size);
    uint64_t genSessionKey();

private:
    ISteamNetworkingSockets* m_iface{ nullptr };
    Role m_role{ Role::Browser };

    HSteamNetConnection m_conn{ k_HSteamNetConnection_Invalid };
    bool m_connected{ false };

    bool m_hasList{ false };
    std::vector<lobby::SessionEntry> m_latestList{};

    lobby::Announce m_announce{};
    bool m_hasAnnounce{ false };
    uint64_t m_sessionKey{ 0 };
};