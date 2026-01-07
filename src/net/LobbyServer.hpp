#pragma once
#include <unordered_map>
#include <vector>
#include <cstdint>
#include <chrono>

#include <steam/steamnetworkingsockets.h>
#include <steam/isteamnetworkingsockets.h>
#include <steam/steamnetworkingtypes.h>
#include <steam/isteamnetworkingutils.h>

#include "LobbyProtocol.hpp"

class LobbyServer {
public:
    bool start(ISteamNetworkingSockets* iface, uint16_t port);
    void stop();

    void onConnStatusChanged(SteamNetConnectionStatusChangedCallback_t* info);
    void pump(); // call frequently

    HSteamListenSocket listenSocket() const { return m_listen; }

private:
    using Clock = std::chrono::steady_clock;

    struct Session {
        uint64_t sessionKey{};

        // current lobby connection that "owns" the session (host's lobby conn)
        HSteamNetConnection ownerConn{ k_HSteamNetConnection_Invalid };

        uint32_t ipv4_host_order{};
        uint16_t gamePort{};
        uint8_t  curPlayers{ 1 };
        uint8_t  maxPlayers{ 3 };

        uint32_t worldSeed{};
        char     name[32]{};

        lobby::SessionState state{ lobby::SessionState::Open };

        Clock::time_point lastSeen{};
        Clock::time_point migratingSince{};
    };

    void handleMessage(HSteamNetConnection from, const void* data, uint32_t size);
    void sendList(HSteamNetConnection to);

    void cleanupExpired(); // TTL + grace cleanup
    void markMigrating(uint64_t sessionKey);

    bool fillRemoteIPv4(HSteamNetConnection from, uint32_t& outIpHostOrder);

private:
    ISteamNetworkingSockets* m_iface{ nullptr };
    HSteamListenSocket m_listen{ k_HSteamListenSocket_Invalid };
    HSteamNetPollGroup m_poll{ k_HSteamNetPollGroup_Invalid };

    // host lobby connection -> sessionKey (only for current owner conns)
    std::unordered_map<HSteamNetConnection, uint64_t> m_connToSession;

    // sessionKey -> session record
    std::unordered_map<uint64_t, Session> m_sessions;

private:
    // Tunables (keep lobby dumb but resilient)
    static constexpr std::chrono::seconds kActiveTTL{ 12 };    // if no heartbeat, consider host gone
    static constexpr std::chrono::seconds kGraceTTL{ 25 };     // time allowed for Claim after host loss
};