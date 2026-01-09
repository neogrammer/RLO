#include <SFML/Graphics.hpp>
#include <iostream>
#include <string>
#include <vector>
#include <thread>
#include <chrono>
#include <random>

#include "net/NetCommon.hpp"
#include "net/LobbyServer.hpp"
#include "net/LobbyClient.hpp"
#include "net/GameHost.hpp"
#include "net/GameClient.hpp"
#include <steam/isteamnetworkingutils.h>

struct Args {
    bool lobbyServer = false;
    uint16_t lobbyPort = 27010;

    bool host = false;
    uint16_t gamePort = 27020;

    bool client = false;
    bool browseOnly = false;
    int pickIndex = 0;

    std::string lobbyAddr;     // e.g. "1.2.3.4:27010"
    std::string name = "Run #1";
};

static Args parseArgs(int argc, char** argv) {
    Args a;
    for (int i = 1; i < argc; ++i) {
        std::string s = argv[i];

        if (s == "--lobby-server" && i + 1 < argc) { a.lobbyServer = true; a.lobbyPort = (uint16_t)std::stoi(argv[++i]); }
        else if (s == "--host" && i + 1 < argc) { a.host = true; a.gamePort = (uint16_t)std::stoi(argv[++i]); }
        else if (s == "--client") { a.client = true; }
        else if (s == "--browse") { a.browseOnly = true; }
        else if (s == "--pick" && i + 1 < argc) { a.pickIndex = std::stoi(argv[++i]); }
        else if (s == "--lobby" && i + 1 < argc) { a.lobbyAddr = argv[++i]; }
        else if (s == "--name" && i + 1 < argc) { a.name = argv[++i]; }
    }
    return a;
}

struct App {
    NetRuntime rt;

    LobbyServer lobbyServer;
    LobbyClient lobbyClient;
    GameHost gameHost;
    GameClient gameClient;

    bool hasLobbyServer = false;
    bool hasLobbyClient = false;
    bool hasGameHost = false;
    bool hasGameClient = false;


    // Callback router
    static App* self;
    static void routeConnStatus(SteamNetConnectionStatusChangedCallback_t* info) {
        if (!self) return;

        // Listen-socket-targeted callbacks
        if (self->hasLobbyServer && info->m_info.m_hListenSocket == self->lobbyServerListen()) {
            self->lobbyServer.onConnStatusChanged(info);
            return;
        }
        if (self->hasGameHost && info->m_info.m_hListenSocket == self->gameHostListen()) {
            self->gameHost.onConnStatusChanged(info);
            return;
        }

        // Connection-targeted callbacks
        if (self->hasLobbyClient && info->m_hConn == self->lobbyClient.conn()) {
            self->lobbyClient.onConnStatusChanged(info);
            return;
        }
        if (self->hasGameClient && info->m_hConn == self->gameClient.conn()) {
            self->gameClient.onConnStatusChanged(info);
            return;
        }
    }

    HSteamListenSocket lobbyServerListen() const { return hasLobbyServer ? lobbyServerListenSock : k_HSteamListenSocket_Invalid; }
    HSteamListenSocket gameHostListen() const { return hasGameHost ? gameHost.listenSocket() : k_HSteamListenSocket_Invalid; }

    // stash because LobbyServer keeps listen private; we’ll track it here via start success
    HSteamListenSocket lobbyServerListenSock{ k_HSteamListenSocket_Invalid };
};

App* App::self = nullptr;
//
//
//static LobbyServer* g_lobby = nullptr;
//static HSteamListenSocket g_lobbyListen = k_HSteamListenSocket_Invalid;
//
//static void Route(SteamNetConnectionStatusChangedCallback_t* info)
//{
//    if (!g_lobby) return;
//    if (info->m_info.m_hListenSocket == g_lobbyListen) {
//        g_lobby->onConnStatusChanged(info);
//    }
//}


int main(int argc, char** argv) {
    const Args args = parseArgs(argc, argv);

    App app;
    App::self = &app;

    if (!app.rt.init()) return 1;
    app.rt.setConnStatusRouter(&App::routeConnStatus);

    auto* iface = app.rt.iface();

    // Mode: Lobby server
    //if (args.lobbyServer) {

    //    uint16_t port = 27010;
    //    if (argc >= 2) port = (uint16_t)std::stoi(std::string(argv[1]));

    //    NetRuntime rt;
    //    if (!rt.init()) return 1;

    //    LobbyServer lobby;
    //    g_lobby = &lobby;
    //    rt.setConnStatusRouter(&Route);

    //    if (!lobby.start(rt.iface(), port)) {
    //        std::cerr << "Failed to start lobby server\n";
    //        return 2;
    //    }

    //    // requires you add: HSteamListenSocket LobbyServer::listenSocket() const { return m_listen; }
    //    g_lobbyListen = lobby.listenSocket();

    //    std::cout << "[LobbyServer] Running on UDP " << port << "\n";

    //    while (true) {
    //        rt.pumpCallbacks();
    //        lobby.pump();
    //        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    //    }
    //   
    //}


    // Migration state
    uint64_t savedSessionKey = 0;
    std::string savedSessionName;
    uint32_t savedWorldSeed = 0;
    game::Snap savedGameState{};
    sf::Clock migrationTimer;
    sf::Clock reconnectTimer;
    int migrationDelayMs = 0;
    int reconnectAttempts = 0;
    bool isMigratedHost = false;
    bool showMigrationFailedDialog = false;

    if (args.lobbyServer)
    {
        app.hasLobbyServer = true;

        if (!app.lobbyServer.start(iface, args.lobbyPort)) {
            std::cerr << "Failed to start lobby server\n";
            app.rt.shutdown();
            return 2;
        }

        app.lobbyServerListenSock = app.lobbyServer.listenSocket();

        std::cout << "[LobbyServer] Running on UDP " << args.lobbyPort << "\n";

        for (;;)
        {
            app.rt.pumpCallbacks();
            app.lobbyServer.pump();
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }


    // Mode: Host (with lobby announce)
    if (args.host) {
        app.hasGameHost = true;
        const uint32_t seed = 0xC0FFEEu; // placeholder; later: random per run
        if (!app.gameHost.start(iface, args.gamePort, seed)) return 4;

        // Optional: announce to lobby
        if (!args.lobbyAddr.empty()) {
            app.hasLobbyClient = true;
            if (!app.lobbyClient.connect(iface, args.lobbyAddr, LobbyClient::Role::Announcer)) return 5;
            app.lobbyClient.setAnnounceInfo(args.gamePort, 3, seed, args.name);
        }

        sf::RenderWindow window(sf::VideoMode({ 1280U, 720U }, 32U), "Host");
        window.setFramerateLimit(60);

        sf::Font font;
        auto tryFont = [&](const char* p) -> bool {
            if (font.openFromFile(p)) { std::cout << "[UI] Loaded font: " << p << "\n"; return true; }
            return false;
            };
        const bool hasFont =
            tryFont("assets/fonts/bubbly.ttf") ||
            tryFont("../assets/fonts/bubbly.ttf") ||
            tryFont("../../assets/fonts/bubbly.ttf") ||
            tryFont("bubbly.ttf");

        sf::Text hud(font);
        hud.setCharacterSize(18);
        hud.setPosition({ 20.f, 70.f });

        sf::RectangleShape startBtn({ 180.f, 44.f });
        startBtn.setPosition({ 20.f, 20.f });
        startBtn.setOutlineThickness(2.f);

        sf::Text startTxt(font);
        startTxt.setCharacterSize(18);
        startTxt.setString("Start Game");
        startTxt.setPosition({ 38.f, 30.f });

        auto hit = [](sf::Vector2f p, const sf::RectangleShape& r) {
            return r.getGlobalBounds().contains(p);
            };

        sf::CircleShape circles[3];
        for (int i = 0; i < 3; ++i) {
            circles[i] = sf::CircleShape(18.f);
            circles[i].setOrigin({ 18.f, 18.f });
        }

        float hbAccum = 0.f;

        sf::Clock clock;
        while (window.isOpen())
        {

            while (const std::optional event = window.pollEvent())
            {
                if (event->is<sf::Event::Closed>())
                    window.close();

                if (const auto* mb = event->getIf<sf::Event::MouseButtonPressed>()) {
                    if (mb->button == sf::Mouse::Button::Left && hasFont) {
                        const sf::Vector2f mp = window.mapPixelToCoords(mb->position);
                        if (hit(mp, startBtn)) {
                            app.gameHost.startGame();
                        }
                    }
                }
            }

            const float dt = clock.restart().asSeconds();

            int8_t mx = 0, my = 0;
            if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::A)) mx = -1;
            if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::D)) mx = +1;
            if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::W)) my = -1;
            if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::S)) my = +1;

            app.rt.pumpCallbacks();

            app.gameHost.pumpNetwork();
            app.gameHost.updateSim(dt, mx, my);

            if (app.hasLobbyClient) {


                app.lobbyClient.pump();
                hbAccum += dt;
                if (hbAccum >= 1.0f) {
                    hbAccum = 0.f;
                    //app.lobbyClient.sendHeartbeat(app.gameHost.curPlayers());
                    // Count host + connected clients
                    constexpr uint16_t kMaxPlayers = 3; // keep in sync with setAnnounceInfo(..., 3, ...)
                    // const uint16_t totalPlayers = (uint16_t)std::min<int>(kMaxPlayers, (int)app.gameHost.curPlayers());
                    const uint16_t totalPlayers =
                        (uint16_t)std::clamp((int)app.gameHost.curPlayers(), 1, (int)kMaxPlayers);
                    app.lobbyClient.sendHeartbeat(totalPlayers);
                }
            }

            window.clear(sf::Color(20, 20, 26));

            const auto* st = app.gameHost.states();
            for (int i = 0; i < 3; ++i) {
                circles[i].setPosition({ st[i].x, st[i].y });
                window.draw(circles[i]);
            }

            if (hasFont) {
                const int others = (int)app.gameHost.curPlayers() - 1;
                hud.setString("Players connected (besides you): " + std::to_string(others) +
                    "\nStarted: " + std::string(app.gameHost.gameStarted() ? "YES" : "NO"));

                // Button visual
                if (app.gameHost.gameStarted())
                    startBtn.setFillColor(sf::Color(70, 70, 75));
                else
                    startBtn.setFillColor(sf::Color(35, 90, 55));

                startBtn.setOutlineColor(sf::Color(220, 220, 255));

                window.draw(startBtn);
                window.draw(startTxt);
                window.draw(hud);
            }

            window.display();
        }

        app.gameHost.stop();
        app.lobbyClient.disconnect();
        app.rt.shutdown();
        return 0;
    }

    // Mode: Client (browse lobby, pick session, connect)
   // Mode: Client (SFML lobby browser -> click to join -> in-game)
    if (args.client) {
        if (args.lobbyAddr.empty()) {
            std::cerr << "Client mode requires --lobby <ip:port>\n";
            return 6;
        }

        app.hasLobbyClient = true;
        if (!app.lobbyClient.connect(iface, args.lobbyAddr, LobbyClient::Role::Browser)) return 7;

        // One window: lobby screen then gameplay
        sf::RenderWindow window(sf::VideoMode({ 1280U, 720U }, 32U), "RLO - Lobby");
        window.setFramerateLimit(60);

        // Font (you must provide one file; pick any .ttf)
        // Put it next to your .exe or in a relative folder you control.
        ///*sf::Font font;
        //const bool hasFont = font.openFromFile("assets/DejaVuSans.ttf") || font.openFromFile("DejaVuSans.ttf");*/

        sf::Font font;

        auto tryFont = [&](const char* p) -> bool {
            if (font.openFromFile(p)) { std::cout << "[UI] Loaded font: " << p << "\n"; return true; }
            return false;
            };

        // Try a few likely working directories
        const bool hasFont =
            tryFont("assets/fonts/bubbly.ttf") ||
            tryFont("../assets/fonts/bubbly.ttf") ||
            tryFont("../../assets/fonts/bubbly.ttf") ||
            tryFont("bubbly.ttf");

        bool showMigrationFailedDialog = false;
        sf::RectangleShape darkOverlay({ 1280.f, 720.f });
        sf::RectangleShape dialogBox({ 600.f, 300.f });
        sf::RectangleShape okButton({ 120.f, 40.f });
        bool okButtonClicked = false;


        darkOverlay.setFillColor(sf::Color(0, 0, 0, 180));
        dialogBox.setPosition({ 340.f, 210.f });
        dialogBox.setFillColor(sf::Color(40, 40, 50));
        dialogBox.setOutlineColor(sf::Color(100, 100, 120));
        dialogBox.setOutlineThickness(3.f);

        okButton.setPosition({ 580.f, 440.f });
        okButton.setFillColor(sf::Color(60, 120, 60));
        okButton.setOutlineColor(sf::Color(120, 180, 120));
        okButton.setOutlineThickness(2.f);

        auto mkText = [&](const std::string& s, unsigned size, sf::Vector2f pos) {
            sf::Text t(font);
            t.setString(s);
            t.setCharacterSize(size);
            t.setPosition(pos);
            return t;
            };

        auto entryAddrStr = [](const lobby::SessionEntry& e) -> std::string {
            SteamNetworkingIPAddr a;
            a.Clear();
            a.SetIPv4(e.ipv4_host_order, e.gamePort);
            char buf[SteamNetworkingIPAddr::k_cchMaxString]{};
            a.ToString(buf, sizeof(buf), true);
            return std::string(buf);
            };

        enum class Phase { LobbyBrowse, WaitingForStart, InGame, MigrationAttempt, MigrationReconnect };
        Phase phase = Phase::LobbyBrowse;

        std::string joinedHostStr;
        std::string joinedSessionName;
        int joinedIdx = -1;

        std::vector<lobby::SessionEntry> list;
        bool haveList = false;



        // Lobby UI layout
        const float left = 60.f;
        const float top = 90.f;
        const float rowH = 56.f;
        const float rowW = 1160.f;

        sf::RectangleShape rowRect({ rowW, rowH });
        rowRect.setOutlineThickness(2.f);

        sf::CircleShape circles[3];
        for (int i = 0; i < 3; ++i) {
            circles[i] = sf::CircleShape(18.f);
            circles[i].setOrigin({ 18.f, 18.f });
        }

        game::Snap snap{};
        bool hasSnap = false;

        float listReqAccum = 0.f;

        sf::Clock clock;

        int selectedIdx = -1;

        sf::Clock uiClock;
        float lastClickAt = -1000.f;
        int lastClickIdx = -1;

        auto tryJoinIndex = [&](int idx)
            {
                if (idx < 0 || idx >= (int)list.size()) return;

                const auto& e = list[idx];
                const bool open =
                    (e.state == lobby::SessionState::Open) &&
                    (e.curPlayers < e.maxPlayers);

                if (!open) return;

                const std::string hostStr = entryAddrStr(e);

                app.hasGameClient = true;
                if (!app.gameClient.connect(iface, hostStr.c_str())) {
                    std::cerr << "Failed to connect to host: " << hostStr << "\n";
                    app.hasGameClient = false;
                    return;
                }

                std::cout << "[Client] Joining " << hostStr << " name=\"" << e.name << "\"\n";

                joinedHostStr = hostStr;
                joinedSessionName = e.name;
                joinedIdx = idx;
                selectedIdx = idx;

                window.setTitle("RLO - Lobby (Waiting for host...)");
                phase = Phase::WaitingForStart;

                // IMPORTANT: stay connected to lobby so UI remains the lobby.
                // (You can disconnect later when game actually starts.)
            };


        while (window.isOpen()) {
            while (const std::optional ev = window.pollEvent())
            {
                if (ev->is<sf::Event::Closed>()) window.close();

                // Esc behavior
                if (const auto* kp = ev->getIf<sf::Event::KeyPressed>())
                {
                    if (kp->code == sf::Keyboard::Key::Escape)
                    {
                        if (phase == Phase::WaitingForStart)
                        {
                            if (app.hasGameClient) {
                                app.gameClient.disconnect("cancel wait");
                                app.hasGameClient = false;
                            }
                            window.setTitle("RLO - Lobby");
                            phase = Phase::LobbyBrowse;
                            joinedIdx = -1;
                            joinedHostStr.clear();
                            joinedSessionName.clear();
                        }
                        else
                        {
                            window.close();
                        }
                    }

                    if (phase == Phase::LobbyBrowse && kp->code == sf::Keyboard::Key::R)
                    {
                        if (app.lobbyClient.isConnected())
                            app.lobbyClient.requestList();
                    }

                    if (phase == Phase::LobbyBrowse && kp->code == sf::Keyboard::Key::Enter)
                    {
                        if (!args.browseOnly)
                            tryJoinIndex(selectedIdx);
                    }
                }

                // Mouse join only in LobbyBrowse
                if (phase == Phase::LobbyBrowse)
                {
                    if (const auto* mb = ev->getIf<sf::Event::MouseButtonPressed>())
                    {

                        if (showMigrationFailedDialog && mb->button == sf::Mouse::Button::Left) {
                            sf::Vector2f mp = window.mapPixelToCoords(mb->position);
                            if (okButton.getGlobalBounds().contains(mp)) {
                                showMigrationFailedDialog = false;
                                phase = Phase::LobbyBrowse;
                            }
                        }
                        if (mb->button == sf::Mouse::Button::Left && haveList)
                        {
                            const sf::Vector2f mp = window.mapPixelToCoords(mb->position);
                            const float relY = mp.y - top;

                            if (mp.x >= left && mp.x <= left + rowW && relY >= 0.f)
                            {
                                const int idx = (int)(relY / rowH);
                                if (idx >= 0 && idx < (int)list.size())
                                {
                                    if (args.browseOnly) { selectedIdx = idx; break; }

                                    const float now = uiClock.getElapsedTime().asSeconds();
                                    const bool sameRow = (idx == selectedIdx);
                                    const bool isDouble = (idx == lastClickIdx) && ((now - lastClickAt) < 0.35f);

                                    selectedIdx = idx;

                                    if (sameRow && isDouble)
                                        tryJoinIndex(idx);

                                    lastClickIdx = idx;
                                    lastClickAt = now;
                                }
                            }
                        }
                    }
                }
            }

            const float dt = clock.restart().asSeconds();

            // Always pump callbacks
            app.rt.pumpCallbacks();

            // Keep lobby updated while in lobby or waiting
            if (app.hasLobbyClient && (phase == Phase::LobbyBrowse || phase == Phase::WaitingForStart))
            {
                app.lobbyClient.pump();

                listReqAccum += dt;
                if (app.lobbyClient.isConnected() && listReqAccum >= 0.5f) {
                    listReqAccum = 0.f;
                    app.lobbyClient.requestList();
                }

                std::vector<lobby::SessionEntry> tmp;
                if (app.lobbyClient.popLatestList(tmp)) {
                    list = std::move(tmp);
                    haveList = true;
                }
            }

            // While waiting, pump the game connection and auto-transition on StartGame
            if (phase == Phase::WaitingForStart && app.hasGameClient)
            {
                app.gameClient.pumpNetwork();

                if (app.gameClient.gameStarted())
                {
                    window.setTitle("RLO - Client");
                    phase = Phase::InGame;

                    // Now drop lobby
                    app.lobbyClient.disconnect();
                    app.hasLobbyClient = false;
                }
            }

            if (app.hasGameClient) {
                app.gameClient.pumpNetwork();
            }

            // transition: only when host starts
            if (phase == Phase::WaitingForStart && app.hasGameClient && app.gameClient.gameStarted()) {
                std::cout << "[Client] StartGame detected -> entering InGame\n";
                window.setTitle("RLO - Client");
                phase = Phase::InGame;

                // optional: now drop lobby
                if (app.hasLobbyClient) {
                    app.lobbyClient.disconnect();
                    app.hasLobbyClient = false;
                }
            }

            if (phase != Phase::InGame) {
                // Render lobby list
                window.clear(sf::Color(16, 16, 22));

                if (hasFont) {
                    window.draw(mkText("Lobby (click an OPEN game to join)    R=Refresh   Esc=Quit", 20, { left, 30.f }));
                    window.draw(mkText(("Lobby: " + args.lobbyAddr), 16, { left, 55.f }));
                }

                // Hover detection
                int hoverIdx = -1;
                if (haveList) {
                    const sf::Vector2f mp = window.mapPixelToCoords(sf::Mouse::getPosition(window));
                    const float relY = mp.y - top;
                    if (mp.x >= left && mp.x <= left + rowW && relY >= 0.f) {
                        const int idx = (int)(relY / rowH);
                        if (idx >= 0 && idx < (int)list.size()) hoverIdx = idx;
                    }
                }



                if (!haveList) {
                    if (hasFont)
                    {
                        window.draw(mkText("Waiting for lobby list...", 18, { left, top }));
                    }
                }
                else if (list.empty()) {
                    if (hasFont)
                    {
                        window.draw(mkText("No sessions yet. Start a host.", 18, { left, top }));
                    }
                }
                else {
                    for (int i = 0; i < (int)list.size(); ++i) {
                        const auto& e = list[i];

                        const bool open = (e.state == lobby::SessionState::Open) && (e.curPlayers < e.maxPlayers);
                        const bool full = (e.state == lobby::SessionState::Full) || (e.curPlayers >= e.maxPlayers);
                        const bool mig = (e.state == lobby::SessionState::Migrating);

                        rowRect.setPosition({ left, top + i * rowH });


                        // color coding
                        if (mig) rowRect.setFillColor(sf::Color(70, 60, 25));
                        else if (full) rowRect.setFillColor(sf::Color(50, 50, 55));
                        else rowRect.setFillColor(sf::Color(25, 70, 35)); // open

                        // hover highlight (only for clickable rows)
                        if (i == hoverIdx && open) rowRect.setFillColor(sf::Color(35, 90, 55));

                        //rowRect.setOutlineColor(open ? sf::Color(120, 180, 140) : sf::Color(90, 90, 95));


                        if (i == selectedIdx) {
                            rowRect.setOutlineColor(sf::Color(220, 220, 255));
                            rowRect.setOutlineThickness(3.f);
                        }
                        else {
                            rowRect.setOutlineThickness(2.f);
                        }
                        window.draw(rowRect);

                        if (hasFont) {
                            const std::string addr = entryAddrStr(e);
                            const std::string line1 = std::string(e.name) + "    " +
                                std::to_string((int)e.curPlayers) + "/" + std::to_string((int)e.maxPlayers) +
                                (mig ? "   [MIGRATING]" : (full ? "   [FULL]" : "   [OPEN]"));

                            window.draw(mkText(line1, 18, { left + 14.f, top + i * rowH + 8.f }));
                            window.draw(mkText(addr, 14, { left + 14.f, top + i * rowH + 30.f }));
                        }
                    }
                }


                if (hasFont && app.hasGameClient && !app.gameClient.gameStarted()) {
                    auto t = mkText("Waiting for host to start...", 22, { 20.f, 20.f });
                    window.draw(t);
                }

                if (phase == Phase::WaitingForStart && hasFont) {
                    window.draw(mkText("Connected to: " + joinedSessionName, 18, { left, 660.f }));
                    window.draw(mkText("Host: " + joinedHostStr, 14, { left, 684.f }));
                    window.draw(mkText("Waiting for host to start...  (Esc = cancel)", 18, { left, 706.f }));
                }

                window.display();
                continue;
            }

            if (phase == Phase::InGame && app.hasGameClient && app.gameClient.hostDisconnected()) {
                app.gameClient.clearHostDisconnected();

                std::cout << "[Client] Host disconnected! Attempting migration...\n";

                // Reconnect to lobby if we dropped it during game start
                if (!app.hasLobbyClient) {
                    app.hasLobbyClient = true;
                    if (!app.lobbyClient.connect(iface, args.lobbyAddr, LobbyClient::Role::Browser)) {
                        std::cerr << "[Migration] Failed to reconnect to lobby for migration\n";
                    }
                }

                // Preserve game state
                game::Snap snap{};
                if (app.gameClient.popLatestSnap(snap)) {
                    savedGameState = snap;
                }
                savedSessionName = joinedSessionName;
                savedWorldSeed = app.gameClient.worldSeed();

                // Start random delay (0-1000ms) to avoid simultaneous claims
                std::mt19937 rng{ std::random_device{}() };
                std::uniform_int_distribution<int> dist(0, 1000);
                migrationDelayMs = dist(rng);
                migrationTimer.restart();

                phase = Phase::MigrationAttempt;
                break;
            }

            if (phase == Phase::MigrationAttempt && migrationTimer.getElapsedTime().asMilliseconds() >= migrationDelayMs) {

                // Try to start hosting on a dynamic port (OS assigns)
                uint16_t dynamicPort = 0; // 0 = OS picks available port

                if (app.gameHost.start(iface, dynamicPort, savedWorldSeed)) {

                    // Successfully hosting! Get the actual port assigned
                    uint16_t actualPort = app.gameHost.port();

                    std::cout << "[Migration] Became host on port " << actualPort << "\n";

                    app.hasGameHost = true;
                    app.hasGameClient = false;

                    // Send Claim to lobby (using saved sessionKey from original host)
                    if (app.hasLobbyClient) {
                        app.lobbyClient.setSessionKey(savedSessionKey);
                        app.lobbyClient.setAnnounceInfo(savedSessionKey, actualPort, 3, savedWorldSeed, savedSessionName);
                        app.lobbyClient.sendClaimNow();
                    }

                    // Restore game state to new host
                    app.gameHost.restoreState(savedGameState.players, savedGameState.serverTick);

                    phase = Phase::InGame;
                    isMigratedHost = true;

                    std::cout << "[Migration] Successfully claimed session. Now hosting.\n";

                }
                else {
                    // Failed to host (likely no port forwarding)
                    std::cout << "[Migration] Failed to become host. Checking for new host...\n";
                    phase = Phase::MigrationReconnect;
                    reconnectTimer.restart();
                }
            }

            if (phase == Phase::MigrationReconnect) {

                // Poll lobby every 500ms for updated session info
                if (reconnectTimer.getElapsedTime().asSeconds() >= 0.5f) {
                    reconnectTimer.restart();
                    reconnectAttempts++;

                    app.lobbyClient.requestList();
                }

                // Check if our old session now has a new host (state changed from Migrating to Open)
                std::vector<lobby::SessionEntry> list;
                if (app.lobbyClient.popLatestList(list)) {

                    for (const auto& e : list) {
                        if (e.sessionKey == savedSessionKey) {

                            if (e.state == lobby::SessionState::Open || e.state == lobby::SessionState::Full) {
                                // Session was successfully claimed!
                                std::cout << "[Migration] Found new host! Reconnecting...\n";

                                std::string newHostAddr = entryAddrStr(e);

                                app.hasGameClient = true;
                                if (app.gameClient.connect(iface, newHostAddr)) {
                                    phase = Phase::InGame;
                                    reconnectAttempts = 0;
                                    std::cout << "[Migration] Reconnected to new host.\n";
                                }

                            }
                            else if (e.state == lobby::SessionState::Migrating) {
                                // Still migrating, keep waiting
                                std::cout << "[Migration] Session still migrating... waiting\n";
                            }

                            break;
                        }
                    }
                }

                // THIS IS WHERE IT GOES - right here before the closing brace
                // Timeout after 10 attempts (~5 seconds)
                if (reconnectAttempts >= 10) {
                    std::cout << "[Migration] Failed. Returning to lobby.\n";
                    showMigrationFailedDialog = true;
                    phase = Phase::LobbyBrowse;
                    window.setTitle("RLO - Lobby");

                    if (app.hasGameClient) {
                        app.gameClient.disconnect();
                        app.hasGameClient = false;
                    }

                    // NEW: If we tried to host but failed, stop hosting
                    if (app.hasGameHost) {
                        app.gameHost.stop();
                        app.hasGameHost = false;
                    }

                    // CRITICAL: Reconnect to lobby fresh
    // The lobby connection may be stale after migration attempts
                    if (app.hasLobbyClient) {
                        app.lobbyClient.disconnect();
                        app.hasLobbyClient = false;
                    }

                    // Reconnect to lobby as Browser
                    app.hasLobbyClient = true;
                    if (!app.lobbyClient.connect(iface, args.lobbyAddr, LobbyClient::Role::Browser)) {
                        std::cerr << "[Migration] Failed to reconnect to lobby\n";
                    }

                    // Clear ALL state
                    savedSessionKey = 0;
                    savedSessionName.clear();
                    savedWorldSeed = 0;
                    reconnectAttempts = 0;
                    isMigratedHost = false;

                    // Clear UI state
                    joinedHostStr.clear();
                    joinedSessionName.clear();
                    joinedIdx = -1;
                    selectedIdx = -1;

                    // Force immediate list refresh
                    list.clear();
                    haveList = false;
                    listReqAccum = 999.f; // Will trigger immediate request on next frame

                    phase = Phase::LobbyBrowse;
                    window.setTitle("RLO - Lobby");
                    showMigrationFailedDialog = true;
                }
            }

            if (showMigrationFailedDialog) {
                // Render modal overlay
                window.draw(darkOverlay);
                window.draw(dialogBox);

                if (hasFont) {
                    window.draw(mkText("Migration Failed", 24, { 400, 250 }));
                    window.draw(mkText("No players could become the new host.", 16, { 350, 300 }));
                    window.draw(mkText("To host games, you need:", 16, { 350, 340 }));
                    window.draw(mkText("1. Port forwarding enabled on your router", 14, { 370, 370 }));
                    window.draw(mkText("2. Firewall configured to allow UDP traffic", 14, { 370, 395 }));
                    window.draw(mkText("Press OK to return to lobby", 16, { 380, 450 }));
                }

                // OK button click detection
                if (okButtonClicked) {
                    showMigrationFailedDialog = false;
                    phase = Phase::LobbyBrowse;
                }
            }

            // Phase: InGame
            int8_t mx = 0, my = 0;
            if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::A)) mx = -1;
            if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::D)) mx = +1;
            if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::W)) my = -1;
            if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::S)) my = +1;

            app.gameClient.pumpNetwork();
            app.gameClient.sendInput(mx, my);

            game::Snap s2{};
            if (app.gameClient.popLatestSnap(s2)) {
                snap = s2;
                hasSnap = true;
            }

            window.clear(sf::Color(20, 20, 26));

            if (hasSnap) {
                for (int i = 0; i < (int)snap.count && i < 3; ++i) {
                    const auto& ps = snap.players[i];
                    circles[ps.id].setPosition({ ps.x, ps.y });
                    window.draw(circles[ps.id]);
                }
            }

            if (showMigrationFailedDialog) {
                window.draw(darkOverlay);
                window.draw(dialogBox);
                window.draw(okButton);

                if (hasFont) {
                    window.draw(mkText("Migration Failed", 24, { 520.f, 250.f }));
                    window.draw(mkText("No players could become the new host.", 16, { 420.f, 300.f }));
                    window.draw(mkText("To host games, you need port forwarding enabled.", 14, { 400.f, 340.f }));
                    window.draw(mkText("OK", 18, { 625.f, 448.f }));
                }
            }

            window.display();
        }

        // cleanup
        if (app.hasGameClient) app.gameClient.disconnect();
        if (app.hasLobbyClient) app.lobbyClient.disconnect();
        app.rt.shutdown();
        return 0;
    }
}