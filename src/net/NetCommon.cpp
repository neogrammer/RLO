#include "NetCommon.hpp"
#include <iostream>

static NetRuntime* g_rt = nullptr;

void NetRuntime::s_debugOutput(ESteamNetworkingSocketsDebugOutputType, const char* msg) {
    // msg already includes newline usually
    std::cerr << "[GNS] " << msg;
}

void NetRuntime::s_onConnStatusChanged(SteamNetConnectionStatusChangedCallback_t* info) {
    if (g_rt && g_rt->m_router) g_rt->m_router(info);
}

bool NetRuntime::init(const NetRuntimeConfig& cfg) {
    g_rt = this;

    SteamDatagramErrMsg errMsg{};
    if (!GameNetworkingSockets_Init(nullptr, errMsg)) {
        std::cerr << "GameNetworkingSockets_Init failed: " << errMsg << "\n";
        return false;
    }

    m_iface = SteamNetworkingSockets();
    if (!m_iface) {
        std::cerr << "SteamNetworkingSockets() returned null\n";
        return false;
    }

    SteamNetworkingUtils()->SetDebugOutputFunction(cfg.debugLevel, &NetRuntime::s_debugOutput);
    SteamNetworkingUtils()->SetGlobalCallback_SteamNetConnectionStatusChanged(&NetRuntime::s_onConnStatusChanged);

    return true;
}

void NetRuntime::shutdown() {
    m_iface = nullptr;
    GameNetworkingSockets_Kill();
    g_rt = nullptr;
}

void NetRuntime::pumpCallbacks() {
    if (m_iface) m_iface->RunCallbacks();
}