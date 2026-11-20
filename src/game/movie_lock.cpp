#include <cstdint>

#include "xdk/hle.h"

using namespace kernel;

X86_EXTERN_FUNC(__imp__sub_00300460);

namespace {
constexpr uint32_t kSvmLock = 0x003002B0;    // SVM_Lock with id 1, as the V-sync thread
constexpr uint32_t kSvmUnlock = 0x003005A0;  // SVM_Unlock, id 1
}  // namespace

// int SVM_ExecSvrFuncIdle(void), cdecl: server type 6, from the idle thread only
X86_FUNC(sub_00300460) {
    CallGuest(ctx, kSvmLock, {});
    __imp__sub_00300460(ctx, base);  // pops its return address; eax: whether a server did anything
    const uint32_t result = ctx.eax;
    CallGuest(ctx, kSvmUnlock, {});
    ctx.eax = result;
}
