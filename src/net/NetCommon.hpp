#pragma once
#include <string>

#include <steam/steamnetworkingsockets.h>
#include <steam/isteamnetworkingsockets.h>
#include <steam/steamnetworkingtypes.h>
#include <steam/isteamnetworkingutils.h>
#include <steam/steam_api_common.h>

struct NetRuntimeConfig {
    ESteamNetworkingSocketsDebugOutputType debugLevel = k_ESteamNetworkingSocketsDebugOutputType_Msg;
};

class NetRuntime {
public:
    using ConnStatusRouterFn = void(*)(SteamNetConnectionStatusChangedCallback_t*);

    bool init(const NetRuntimeConfig& cfg = {});
    void shutdown();

    ISteamNetworkingSockets* iface() const { return m_iface; }

    // Call once per frame/tick. Required so connection state callbacks get delivered.
    void pumpCallbacks();

    // Your app provides a function that routes callbacks to LobbyServer/GameHost/LobbyClient/GameClient.
    void setConnStatusRouter(ConnStatusRouterFn fn) { m_router = fn; }

private:
    static void s_onConnStatusChanged(SteamNetConnectionStatusChangedCallback_t* info);
    static void s_debugOutput(ESteamNetworkingSocketsDebugOutputType type, const char* msg);

private:
    ISteamNetworkingSockets* m_iface{ nullptr };
    ConnStatusRouterFn m_router{ nullptr };
};