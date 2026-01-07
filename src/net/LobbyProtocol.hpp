#pragma once
#include <cstdint>

namespace lobby {

    static constexpr uint32_t kProtocol = 1;

    enum class Type : uint8_t {
        Hello = 1,
        Announce = 2,  // host -> lobby (create/update)
        Heartbeat = 3,  // host -> lobby (keep alive, players count)
        ListReq = 4,  // client -> lobby
        ListResp = 5,  // lobby -> client
        Claim = 6,  // new host -> lobby (take over existing sessionKey during grace)
    };

    enum class SessionState : uint8_t {
        Open = 1,
        Full = 2,
        Migrating = 3,
    };

#pragma pack(push, 1)

    struct Hello {
        Type     type;      // Hello
        uint32_t protocol;  // kProtocol
        uint8_t  role;      // 0=client/browser, 1=host/announcer
    };

    struct Announce {
        Type     type;        // Announce or Claim (same payload)
        uint32_t protocol;    // kProtocol
        uint64_t sessionKey;  // stable id for this run
        uint16_t gamePort;    // public forwarded UDP port for the game host
        uint8_t  maxPlayers;  // 3 for you
        uint8_t  reserved0;
        uint32_t worldSeed;   // roguelike seed (or 0 for now)
        char     name[32];    // null-terminated if shorter
    };

    struct Heartbeat {
        Type     type;        // Heartbeat
        uint64_t sessionKey;
        uint16_t curPlayers;  // 1..maxPlayers
        uint16_t reserved0;
    };

    struct ListReq {
        Type     type;      // ListReq
        uint32_t protocol;  // kProtocol
    };

    struct ListRespHdr {
        Type     type;   // ListResp
        uint16_t count;  // number of entries
        uint16_t reserved0;
    };

    // IPv4-only for prototype
    struct SessionEntry {
        uint64_t sessionKey;

        uint32_t ipv4_host_order; // host byte order (0 if not IPv4)
        uint16_t gamePort;

        uint8_t  curPlayers;
        uint8_t  maxPlayers;

        uint32_t worldSeed;

        SessionState state;
        uint8_t reserved1[3];

        char name[32];
    };

#pragma pack(pop)

} // namespace lobby