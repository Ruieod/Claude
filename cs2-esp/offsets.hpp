#pragma once
#include <cstddef>

// regen: https://github.com/a2x/cs2-dumper (output/offsets.hpp + client_dll.hpp)
// every value here moves per patch — refresh after each CS2 update
namespace off {
    // offsets.hpp -> client_dll
    constexpr std::ptrdiff_t dwEntityList      = 0x0; // FILL
    constexpr std::ptrdiff_t dwLocalPlayerPawn = 0x0; // FILL
    constexpr std::ptrdiff_t dwViewMatrix      = 0x0; // FILL

    // client_dll.hpp schema
    constexpr std::ptrdiff_t m_hPlayerPawn     = 0x0; // FILL CCSPlayerController
    constexpr std::ptrdiff_t m_iszPlayerName   = 0x0; // FILL CBasePlayerController
    constexpr std::ptrdiff_t m_iHealth         = 0x0; // FILL C_BaseEntity
    constexpr std::ptrdiff_t m_iTeamNum        = 0x0; // FILL C_BaseEntity
    constexpr std::ptrdiff_t m_lifeState       = 0x0; // FILL C_BaseEntity
    constexpr std::ptrdiff_t m_vOldOrigin      = 0x0; // FILL C_BasePlayerPawn

    // entity list layout: 64 chunks * 512 entries, chunk ptrs start at +0x10
    constexpr std::ptrdiff_t entityChunkBase   = 0x10;
    constexpr std::ptrdiff_t entityStride      = 0x70; // was 0x78 before late-2025 builds — verify in ReClass if ESP is empty
}
