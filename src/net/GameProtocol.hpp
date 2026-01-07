#pragma once
#include <cstdint>

namespace game {

    static constexpr uint32_t kProtocol = 1;
    static constexpr uint8_t  kMaxPlayers = 3;

    enum class Type : uint8_t {
        Hello = 1,
        Welcome = 2,
        Input = 3,
        Snap = 4,
    };

#pragma pack(push, 1)

    struct Hello {
        Type     type;      // Hello
        uint32_t protocol;  // kProtocol
    };

    struct Welcome {
        Type     type;        // Welcome
        uint8_t  yourId;      // 0..2
        uint32_t worldSeed;   // for roguelike determinism later
    };

    struct Input {
        Type     type;        // Input
        uint32_t clientTick;
        uint8_t  playerId;    // client-supplied; host will sanity-check mapping anyway
        int8_t   moveX;       // -1,0,1
        int8_t   moveY;       // -1,0,1
    };

    struct PlayerState {
        uint8_t id;
        float   x;
        float   y;
    };

    struct Snap {
        Type        type;        // Snap
        uint32_t    serverTick;
        uint8_t     count;
        PlayerState players[kMaxPlayers];
    };

#pragma pack(pop)

} // namespace game