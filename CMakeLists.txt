cmake_minimum_required(VERSION 3.21)
project(RLO_Lobby LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(RLO_LobbyServer
    src/lobby_main.cpp
    src/net/NetCommon.cpp
    src/net/LobbyServer.cpp
)

target_include_directories(RLO_LobbyServer PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

find_package(Threads REQUIRED)
find_package(GameNetworkingSockets CONFIG REQUIRED)

target_link_libraries(RLO_LobbyServer PRIVATE
    GameNetworkingSockets::GameNetworkingSockets
    Threads::Threads
)