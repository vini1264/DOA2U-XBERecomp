#include <cstdint>

#include "../common/keyboard.h"
#include "xdk/hle.h"

using namespace kernel;

X86_EXTERN_FUNC(__imp__sub_0001F4C0);

namespace {
constexpr uint32_t kStickActions = 0x007CAC38;  // per port, 8 bytes: held, pressed, released, previous
uint16_t g_previous[4] = {};
}  // namespace

// void StickActions(int port), cdecl
X86_FUNC(sub_0001F4C0) {
    const uint32_t port = R32(ctx.esp + 4);
    __imp__sub_0001F4C0(ctx, base);  // pops its return address
    if (port >= 4) return;
    const uint16_t now = keyboard::FightActions(int(port));
    const uint16_t before = g_previous[port];
    g_previous[port] = now;
    if (!now && !before) return;
    const uint32_t at = kStickActions + port * 8;
    W16(at + 0, uint16_t(R16(at + 0) | now));
    W16(at + 2, uint16_t(R16(at + 2) | (now & ~before)));
    W16(at + 4, uint16_t(R16(at + 4) | (before & ~now)));
}
