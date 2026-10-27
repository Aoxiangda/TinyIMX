#pragma once
#include <cstdlib>

namespace tinyimx {
// Independent experiment switch. Native SQL itself is still default OFF.
inline bool NativeSqlPacketEnabled() noexcept {
    static const bool enabled = [] {
        const char* value = std::getenv("TINYIMX_PRIVATE_NATIVE_SQL_PACKET_ENABLE");
        return value && value[0] == '1' && value[1] == '\0';
    }();
    return enabled;
}
}
