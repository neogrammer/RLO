#include <iostream>
#include <thread>
#include <chrono>
#include <string>

#include "net/NetCommon.hpp"
#include "net/LobbyServer.hpp"

struct LobbyApp
{
    NetRuntime rt;
    LobbyServer lobby;
    HSteamListenSocket listenSock{ k_HSteamListenSocket_Invalid };

    static LobbyApp* self;

    static void onConnStatus(SteamNetConnectionStatusChangedCallback_t* info)
    {
        if (!self) return;
        if (info->m_info.m_hListenSocket == self->listenSock) {
            self->lobby.onConnStatusChanged(info);
        }
    }
};

LobbyApp* LobbyApp::self = nullptr;

int main(int argc, char** argv)
{
    uint16_t port = 27010;
    if (argc >= 2) {
        try { port = static_cast<uint16_t>(std::stoi(argv[1])); }
        catch (...) {
            std::cerr << "Usage: RLO_LobbyServer [port]\n";
            return 2;
        }
    }

    LobbyApp app;
    LobbyApp::self = &app;

    if (!app.rt.init()) {
        std::cerr << "NetRuntime init failed\n";
        return 1;
    }

    app.rt.setConnStatusRouter(&LobbyApp::onConnStatus);

    if (!app.lobby.start(app.rt.iface(), port)) {
        std::cerr << "Failed to start lobby server on UDP " << port << "\n";
        app.rt.shutdown();
        return 3;
    }

    app.listenSock = app.lobby.listenSocket();

    std::cout << "[LobbyServer] Running on UDP " << port << "\n";

    for (;;) {
        app.rt.pumpCallbacks();
        app.lobby.pump();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}