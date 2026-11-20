// Switch the game's 4:3 and 16:9 modes in real-time when changing resolutions

#include <cstdint>

#include "app/app.h"
#include "xdk/hle.h"

using namespace kernel;

X86_EXTERN_FUNC(__imp__sub_00282940);

namespace {
constexpr uint32_t kDisplayFlags = 0x00E65A98;  // bit 0 widescreen, bit 1 480p
}

X86_FUNC(sub_00282940) {
    const uint32_t flags = R32(kDisplayFlags);
    const uint32_t want = app::Widescreen() ? (flags | 1u) : (flags & ~1u);
    if (want != flags) W32(kDisplayFlags, want);
    __imp__sub_00282940(ctx, base);
}
