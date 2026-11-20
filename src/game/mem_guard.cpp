// XMemFree (0x2BBCC7) refuses memory the runtime made rather than the title's heap, and the heap's frees (0x2BD69B)
// refuse blocks it doesn't have out.
#include <cstdint>
#include <cstdio>

#include "kernel/memory.h"
#include "xdk/hle.h"

using namespace kernel;

X86_EXTERN_FUNC(__imp__sub_002BBCC7);

// void XMemFree(void* address, DWORD attributes), stdcall
X86_FUNC(sub_002BBCC7) {
    const uint32_t va = R32(ctx.esp + 4);
    const uint32_t attributes = R32(ctx.esp + 8);
    const bool heap = !(attributes & 0x80000000u);  // the physical ones go to MmFreeContiguousMemory
    if (heap && va >= mem::kSystemBase && va - mem::kSystemBase < mem::kSystemSize) {
        static uint32_t logged = 0;
        if (logged < 16) {
            logged++;
            LOGE("XMemFree: %08X is the runtime's memory, not the heap's; kept (attributes %08X, first dwords %08X %08X %08X)",
                 va, attributes, R32(va), R32(va + 4), R32(va + 8));
        }
        ctx.esp += 4 + 8;
        return;
    }
    __imp__sub_002BBCC7(ctx, base);
}

X86_EXTERN_FUNC(__imp__sub_002BD69B);

namespace {
constexpr uint32_t kProcessHeap = 0x00F45F94;  // the heap XMemAlloc and malloc use
constexpr uint32_t kHeapSegments = 0x60;       // 16 segment pointers
constexpr uint32_t kEntrySize = 0x10;          // a block's header, before it (RtlFreeHeap: block - 0x10)
}  // namespace

// BOOL-ish LocalFree(void* block), stdcall: 0 when freed
X86_FUNC(sub_002BD69B) {
    const uint32_t va = R32(ctx.esp + 4);
    const uint32_t heap = R32(kProcessHeap);
    if (va && heap && va >= kEntrySize && mem::IsCommitted(va - kEntrySize)) {
        const uint32_t entry = va - kEntrySize;
        const uint8_t flags = G<uint8_t>(entry + 5);
        const uint8_t segment = G<uint8_t>(entry + 4);
        const bool busy = flags & 0x01;
        const bool own_pages = flags & 0x08;
        const bool segment_ok = own_pages || (segment < 16 && R32(heap + kHeapSegments + segment * 4u) != 0);
        // its size in 16-byte units at +0, and the next block's previous size at +2 the same (unless it's the last,
        // flag 0x10): a pointer into the middle of something else hardly ever has both
        // (read only where memory is committed: a nonsense size points anywhere)
        const uint16_t units = G<uint16_t>(entry);
        const bool last = flags & 0x10;
        const uint32_t next = entry + units * 16u;
        const bool chain_ok =
            own_pages || (units && (last || (mem::IsCommitted(next) && mem::IsCommitted(next + 3) &&
                                             G<uint16_t>(next + 2) == units)));
        if (!busy || !segment_ok || !chain_ok) {
            static uint32_t logged = 0;
            if (logged < 16) {
                logged++;
                LOGE("heap: free of %08X kept, not a block the heap has out (flags %02X, segment %u, size %u)", va,
                     flags, segment, uint32_t(units) * 16);
                // DIAG: what the block holds, and the code addresses on the guest stack: who freed it
                LOGE("heap:   %08X: %08X %08X %08X %08X %08X %08X %08X %08X", va, R32(va), R32(va + 4), R32(va + 8),
                     R32(va + 12), R32(va + 16), R32(va + 20), R32(va + 24), R32(va + 28));
                char calls[256] = {};
                int len = 0, found = 0;
                for (uint32_t at = ctx.esp; at < ctx.esp + 0x400 && found < 16; at += 4) {
                    if (!mem::IsCommitted(at)) break;
                    const uint32_t v = R32(at);
                    if (v >= 0x00011000 && v < 0x003C0000) {
                        len += std::snprintf(calls + len, sizeof(calls) - len, " +%03X:%08X", at - ctx.esp, v);
                        found++;
                    }
                }
                LOGE("heap:   stack%s", calls);
            }
            ctx.eax = 0;
            ctx.esp += 4 + 4;
            return;
        }
    }
    __imp__sub_002BD69B(ctx, base);
}
