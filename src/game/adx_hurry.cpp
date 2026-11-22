// ADX servers tick fast while the title spins on a partition load.

#include <atomic>
#include <chrono>
#include <cstdint>
#include <thread>

#include "core/log.h"
#include "xdk/hle.h"

using namespace kernel;

X86_EXTERN_FUNC(__imp__sub_002FBAE0);
X86_EXTERN_FUNC(__imp__sub_002FC5A0);

namespace {
constexpr uint32_t kReadEnd = 3;              // ADXF_STAT_READEND
constexpr int64_t kSpinNs = 1'000'000;        // two askings this close are a loop
constexpr int64_t kHurryNs = 5'000'000;       // how long one asking keeps the servers fast
constexpr auto kFastWait = std::chrono::milliseconds(1);

std::atomic<int64_t> g_last_ask{0};
std::atomic<int64_t> g_hurry_until{0};

int64_t Now() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch())
        .count();
}
}  // namespace

// int ADXF_GetPtStat(int ptid), cdecl
X86_FUNC(sub_002FBAE0) {
    __imp__sub_002FBAE0(ctx, base);
    const int64_t now = Now();
    const int64_t last = g_last_ask.exchange(now);
    if (ctx.eax != kReadEnd && now - last < kSpinNs) {
        if (g_hurry_until.exchange(now + kHurryNs) < now)
            LOG_FIRST(8, "adx: the title waits on a partition load, the ADX servers run every millisecond");
    }
}

// the ADX server threads' wait for the next vertical blank, cdecl, no arguments
X86_FUNC(sub_002FC5A0) {
    if (Now() < g_hurry_until.load()) {
        std::this_thread::sleep_for(kFastWait);
        ctx.esp += 4;  // the return address the skipped call would have popped
        return;
    }
    __imp__sub_002FC5A0(ctx, base);
}
