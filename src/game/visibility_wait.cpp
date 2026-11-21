// D3DDevice_GetVisibilityTestResult (0x344450) waits for the GPU when the title polls it.

#include <cstdint>

#include "core/log.h"
#include "xdk/d3d8.h"
#include "xdk/hle.h"

using namespace kernel;

X86_EXTERN_FUNC(__imp__sub_00344450);

namespace {
constexpr uint32_t kTestIncomplete = 0x88760828;  // D3DERR_TESTINCOMPLETE
}

// HRESULT GetVisibilityTestResult(DWORD index, UINT* result, ULONGLONG* timestamp), stdcall
X86_FUNC(sub_00344450) {
    const uint32_t ret = R32(ctx.esp), index = R32(ctx.esp + 4), result = R32(ctx.esp + 8), stamp = R32(ctx.esp + 12);
    __imp__sub_00344450(ctx, base);
    if (ctx.eax != kTestIncomplete) return;
    d3d::FinishReports();
    // the call popped its arguments: put them back for the second asking
    ctx.esp -= 16;
    W32(ctx.esp, ret);
    W32(ctx.esp + 4, index);
    W32(ctx.esp + 8, result);
    W32(ctx.esp + 12, stamp);
    __imp__sub_00344450(ctx, base);
    if (ctx.eax != kTestIncomplete) return;
    LOG_FIRST(8, "visibility: test %X has no report coming, read as hidden", index);
    if (result) W32(result, 0);
    ctx.eax = 0;
}
