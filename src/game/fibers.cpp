#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <mutex>
#include <unordered_map>

#include "xdk/hle.h"

using namespace kernel;

X86_EXTERN_FUNC(__imp__sub_002BB7F6);  // DeleteFiber 0x2BB7F6: frees the guest stack, which holds the record

namespace {

constexpr uint32_t kTlsIndexVar = 0x00F2A4C0;  // guest variable: TLS slot of the per-thread fiber data
constexpr uint32_t kFiberStartup = 0x002BB725;  // XapiFiberStartup(start routine)
constexpr uint32_t kCurrentFiber = 0xC;         // [thread data + 0xC]: the current fiber's record
constexpr uint32_t kRecordSave = 0xC;           // [F+C]: esp saved by SwitchToFiber

struct HostFiber {
    void* handle = nullptr;  // the Windows fiber
    bool saved_valid = false;
    X86Context saved{};      // guest registers while switched out
    uint32_t saved_fs0 = 0;  // guest exception list
};

struct StartInfo {
    X86Context* ctx;
    uint32_t record;
};

std::mutex g_lock;
std::unordered_map<uint32_t, HostFiber> g_fibers;  // by guest record; node based, references stay valid

HostFiber& FiberFor(uint32_t record) {
    std::lock_guard<std::mutex> lock(g_lock);
    return g_fibers[record];
}

// the first run of a guest fiber, on its own host fiber and stack
VOID CALLBACK FiberMain(void* param) {
    StartInfo si = *static_cast<StartInfo*>(param);
    X86Context& ctx = *si.ctx;
    ctx.esp = si.record;  // the stack grows down from the record, as CreateFiber laid it out
    ctx.ebp = ctx.esi = ctx.edi = ctx.ebx = 0;
    W32(ctx.fs_base, 0xFFFFFFFFu);  // empty exception list
    uint32_t start = R32(si.record - 4);
    LOGI("fiber %08X: first run, start routine %08X", si.record, start);
    CallGuest(ctx, kFiberStartup, {start});
    // XapiFiberStartup ends in ExitThread; a Windows fiber function must not return
    LOGE("fiber %08X: the start routine returned", si.record);
    kernel::ExitThread(0);
}

}  // namespace

// SwitchToFiber(PVOID fiber), stdcall, esp -> [return][fiber]
X86_FUNC(sub_002BB809) {
    const uint32_t ret_esp = ctx.esp + 8;  // after ret 4
    const uint32_t target = R32(ctx.esp + 4);

    // the thread must be a fiber itself before it can switch to another
    if (!IsThreadAFiber()) ConvertThreadToFiber(nullptr);

    uint32_t slot = R32(R32(ctx.fs_base + 4) + R32(kTlsIndexVar) * 4);  // per-thread fiber data
    uint32_t cur = R32(slot + kCurrentFiber);                                        // current fiber's record

    HostFiber& me = FiberFor(cur);
    me.handle = GetCurrentFiber();
    me.saved = ctx;
    me.saved.esp = ret_esp;
    me.saved_fs0 = R32(ctx.fs_base);
    me.saved_valid = true;
    W32(cur + kRecordSave, ret_esp);

    HostFiber& to = FiberFor(target);
    W32(slot + kCurrentFiber, target);  // the target is the current fiber from here on

    StartInfo start{&ctx, target};
    if (!to.handle) {
        // first switch into a fiber made by CreateFiber
        to.handle = CreateFiberEx(0, 1u << 20, 0, FiberMain, &start);
        if (!to.handle) {
            LOGE("fiber %08X: CreateFiberEx failed (%lu)", target, GetLastError());
            kernel::ExitThread(0);
        }
    } else if (to.saved_valid) {
        // resuming one that switched out earlier: its registers come back when it runs again
        W32(ctx.fs_base, to.saved_fs0);
    }
    SwitchToFiber(to.handle);  // `start` is only read by FiberMain before anyone switches back here

    // somebody switched back to this fiber: continue after the guest call as its ret 4 would
    ctx = me.saved;
    W32(ctx.fs_base, me.saved_fs0);
}

// DeleteFiber(PVOID fiber): free the host fiber with the guest one
X86_FUNC(sub_002BB7F6) {
    uint32_t record = R32(ctx.esp + 4);
    {
        std::lock_guard<std::mutex> lock(g_lock);
        auto it = g_fibers.find(record);
        if (it != g_fibers.end()) {
            if (it->second.handle && it->second.handle != GetCurrentFiber()) DeleteFiber(it->second.handle);
            g_fibers.erase(it);
        }
    }
    __imp__sub_002BB7F6(ctx, base);
}
