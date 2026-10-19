// Native-VR-on-foot probe: the hooks, their installation and the frame thread's drain (native_vr_probe.h says what and why).
#include "native_vr_probe.h"
#include "native_vr_probe_core.h"
#include "../common/code_hook.h"
#include "../common/config.h"
#include "../common/log.h"
#include <windows.h>
#include <intrin.h>
#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace edvr {
namespace {

using Fn1 = void (*)(void*);
using Fn2 = void (*)(void*, void*);

std::atomic<uintptr_t> g_original[nvp::kPointCount];
std::atomic<bool> g_recording{false};
std::atomic<uint64_t> g_recorded{0};
nvp::Ring<1024> g_ring;
std::atomic<uintptr_t> g_base{0};
std::atomic<uintptr_t> g_imageSize{0};

// ---- the hook side: no lock, no allocation, no log -------------------------------------------------------------------------------

__declspec(noinline) uint8_t sehReadMode(const uint8_t* self, uint32_t* requested, uint64_t* owner) noexcept {
    __try {
        *owner = *reinterpret_cast<const uint64_t*>(self + 0x08);
        const uint8_t* data = *reinterpret_cast<const uint8_t* const*>(self + 0x10);
        *requested = *reinterpret_cast<const uint32_t*>(data + 0x08);
        return 1;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

__declspec(noinline) uint8_t sehReadCinema(const uint8_t* self, const void* arg, uint8_t* flag, uint64_t* argValue) noexcept {
    __try {
        *flag = *(self + 0x20);
        *argValue = arg ? *reinterpret_cast<const uint64_t*>(arg) : 0;
        return 1;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

// Inlined into each hook so frame 1 of the capture is the game's caller (frame 0 is the hook itself, skipped).
__forceinline void captureStack(nvp::Event& e) {
    void* frames[nvp::kStackFrames] = {};
    const USHORT n = RtlCaptureStackBackTrace(1, nvp::kStackFrames, frames, nullptr);
    e.frames = static_cast<uint8_t>(n);
    for (USHORT i = 0; i < n; ++i) e.stack[i] = reinterpret_cast<uint64_t>(frames[i]);
}

__declspec(noinline) void modeHook(void* self) {
    if (g_recording.load(std::memory_order_relaxed)) {
        nvp::Event e;
        e.point = nvp::kPointMode;
        e.self = reinterpret_cast<uint64_t>(self);
        e.readOk = sehReadMode(static_cast<const uint8_t*>(self), &e.requested, &e.other);
        e.thread = GetCurrentThreadId();
        captureStack(e);
        g_ring.push(e);
        g_recorded.fetch_add(1, std::memory_order_relaxed);
    }
    reinterpret_cast<Fn1>(g_original[nvp::kPointMode].load(std::memory_order_acquire))(self);
}

__declspec(noinline) void cinemaOnHook(void* self, void* arg) {
    if (g_recording.load(std::memory_order_relaxed)) {
        nvp::Event e;
        e.point = nvp::kPointCinemaOn;
        e.self = reinterpret_cast<uint64_t>(self);
        e.readOk = sehReadCinema(static_cast<const uint8_t*>(self), arg, &e.flagBefore, &e.other);
        e.thread = GetCurrentThreadId();
        captureStack(e);
        g_ring.push(e);
        g_recorded.fetch_add(1, std::memory_order_relaxed);
    }
    reinterpret_cast<Fn2>(g_original[nvp::kPointCinemaOn].load(std::memory_order_acquire))(self, arg);
}

__declspec(noinline) void cinemaOffHook(void* self) {
    if (g_recording.load(std::memory_order_relaxed)) {
        nvp::Event e;
        e.point = nvp::kPointCinemaOff;
        e.self = reinterpret_cast<uint64_t>(self);
        e.readOk = sehReadCinema(static_cast<const uint8_t*>(self), nullptr, &e.flagBefore, &e.other);
        e.thread = GetCurrentThreadId();
        captureStack(e);
        g_ring.push(e);
        g_recorded.fetch_add(1, std::memory_order_relaxed);
    }
    reinterpret_cast<Fn1>(g_original[nvp::kPointCinemaOff].load(std::memory_order_acquire))(self);
}

const void* hookFor(int point) {
    switch (point) {
        case nvp::kPointMode: return reinterpret_cast<const void*>(&modeHook);
        case nvp::kPointCinemaOn: return reinterpret_cast<const void*>(&cinemaOnHook);
        default: return reinterpret_cast<const void*>(&cinemaOffHook);
    }
}

// ---- installation (frame thread) ------------------------------------------------------------------------------------------------

// A 5-byte jmp reaches +/-2 GB, and this DLL may load farther than that from the game: the patch jumps to a relay page placed near the
// target, which jumps on, absolutely, to the hook (jmp [rip+0]; dq hook).
constexpr size_t kRelayBytes = 14;

struct Slot {
    CodeHook hook;
    uint8_t* relay = nullptr;
    uintptr_t target = 0;
    int point = 0;
    bool armed = false;
};
Slot g_slots[nvp::kPointCount];

uint8_t* allocateRelay(uintptr_t target) noexcept {
    SYSTEM_INFO info{};
    GetSystemInfo(&info);
    const uintptr_t granularity = info.dwAllocationGranularity;
    const uintptr_t floor = reinterpret_cast<uintptr_t>(info.lpMinimumApplicationAddress);
    const uintptr_t ceiling = reinterpret_cast<uintptr_t>(info.lpMaximumApplicationAddress);
    const uintptr_t distance = uintptr_t(INT32_MAX) - 0x10000u;
    uintptr_t at = target > distance ? target - distance : floor;
    if (at < floor) at = floor;
    const uintptr_t limit = target > ceiling - distance ? ceiling : target + distance;
    while (at < limit) {
        MEMORY_BASIC_INFORMATION region{};
        if (!VirtualQuery(reinterpret_cast<void*>(at), &region, sizeof(region))) break;
        const uintptr_t start = reinterpret_cast<uintptr_t>(region.BaseAddress);
        if (region.RegionSize > UINTPTR_MAX - start) break;
        const uintptr_t end = start + region.RegionSize;
        if (region.State == MEM_FREE) {
            uintptr_t candidate = at > start ? at : start;
            if (candidate > UINTPTR_MAX - (granularity - 1)) break;
            candidate = (candidate + granularity - 1) & ~(granularity - 1);
            if (candidate < limit && candidate < end && end - candidate >= 4096) {
                auto* p = static_cast<uint8_t*>(
                    VirtualAlloc(reinterpret_cast<void*>(candidate), 4096, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
                if (p) return p;
            }
        }
        if (end <= at) break;
        at = end;
    }
    return nullptr;
}

// Publish the original's forward and seal the relay BEFORE CodeHook makes the patch live: the first game call may arrive at once.
bool prepareSlot(void* trampoline, void* context) noexcept {
    auto& slot = *static_cast<Slot*>(context);
    g_original[slot.point].store(reinterpret_cast<uintptr_t>(trampoline), std::memory_order_release);
    DWORD oldProtect = 0;
    return VirtualProtect(slot.relay, 4096, PAGE_EXECUTE_READ, &oldProtect) &&
           FlushInstructionCache(GetCurrentProcess(), slot.relay, kRelayBytes);
}

__declspec(noinline) bool sehCheckBytes(uintptr_t address, const uint8_t* expected, size_t n) noexcept {
    __try {
        return std::memcmp(reinterpret_cast<const void*>(address), expected, n) == 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

__declspec(noinline) bool sehIdentity(uintptr_t base, uint32_t* timestamp, uint32_t* imageSize) noexcept {
    __try {
        uint32_t peOff = 0;
        std::memcpy(&peOff, reinterpret_cast<const void*>(base + 0x3C), 4);
        if (peOff > 0x1000) return false;
        std::memcpy(timestamp, reinterpret_cast<const void*>(base + peOff + 8), 4);
        std::memcpy(imageSize, reinterpret_cast<const void*>(base + peOff + 0x50), 4);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

void say(NativeVrProbeSink sink, void* ctx, const char* fmt, ...) {
    char line[700];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    sink(ctx, line);
}

void logSink(void*, const char* line) { Log::get().note("%s", line); }

}  // namespace

int nativeVrProbeInstall(uintptr_t base, const uintptr_t* targetsOverride, bool checkIdentity, NativeVrProbeSink sink, void* ctx) {
    if (checkIdentity) {
        uint32_t timestamp = 0, imageSize = 0;
        if (!sehIdentity(base, &timestamp, &imageSize) || timestamp != nvp::kExpectedTimestamp || imageSize != nvp::kExpectedImageSize) {
            say(sink, ctx,
                "native-vr-probe: NOT installed: the game is not build 332841 (PE stamp 0x%08X, image size 0x%X); nothing was patched and the "
                "game runs as without the probe",
                timestamp, imageSize);
            int armed = 0;
            for (const Slot& s : g_slots) armed += s.armed ? 1 : 0;
            return armed;
        }
        g_imageSize.store(imageSize, std::memory_order_relaxed);
    }
    g_base.store(base, std::memory_order_relaxed);
    int armed = 0;
    for (int point = 0; point < nvp::kPointCount; ++point) {
        Slot& slot = g_slots[point];
        if (slot.armed) {
            ++armed;
            continue;
        }
        const uintptr_t target = targetsOverride && targetsOverride[point] ? targetsOverride[point] : base + nvp::kPointRva[point];
        if (!sehCheckBytes(target, nvp::kPrologue, nvp::kPrologueBytes)) {
            say(sink, ctx, "native-vr-probe: %s at EliteDangerous64.exe+0x%llX NOT hooked: its first %zu bytes are not build 332841's; nothing "
                           "was patched there",
                nvp::kPointName[point], static_cast<unsigned long long>(nvp::kPointRva[point]), nvp::kPrologueBytes);
            continue;
        }
        if (!slot.relay) slot.relay = allocateRelay(target);
        if (!slot.relay) {
            say(sink, ctx, "native-vr-probe: %s NOT hooked: no memory could be placed within 2 GB of it; nothing was patched there",
                nvp::kPointName[point]);
            continue;
        }
        const uint8_t jump[6] = {0xFF, 0x25, 0, 0, 0, 0};
        const uintptr_t hook = reinterpret_cast<uintptr_t>(hookFor(point));
        std::memcpy(slot.relay, jump, sizeof(jump));
        std::memcpy(slot.relay + sizeof(jump), &hook, 8);
        slot.point = point;
        slot.target = target;
        if (!slot.hook.install(reinterpret_cast<void*>(target), slot.relay, nullptr, "native-vr-probe", &prepareSlot, &slot)) {
            say(sink, ctx, "native-vr-probe: %s NOT hooked: CodeHook refused it (its own line above says why); nothing was patched there",
                nvp::kPointName[point]);
            DWORD oldProtect = 0;
            VirtualProtect(slot.relay, 4096, PAGE_READWRITE, &oldProtect);  // so a retry can rewrite it
            continue;
        }
        slot.armed = true;
        ++armed;
        say(sink, ctx, "native-vr-probe: %s hooked at EliteDangerous64.exe+0x%llX (0x%llX), %zu prologue bytes verified, %zu moved; "
                       "it records and then calls the original unchanged",
            nvp::kPointName[point], static_cast<unsigned long long>(nvp::kPointRva[point]), static_cast<unsigned long long>(target),
            nvp::kPrologueBytes, slot.hook.stolenBytes());
        // Process lifetime: the relay and trampoline are never freed, so a call already inside them can finish.
    }
    return armed;
}

void nativeVrProbeSetRecording(bool on) { g_recording.store(on, std::memory_order_relaxed); }
uint64_t nativeVrProbeRecorded() { return g_recorded.load(std::memory_order_relaxed); }
uint64_t nativeVrProbeDropped() { return g_ring.dropped(); }

void nativeVrProbeDrain(uint64_t nowMs, NativeVrProbeSink sink, void* ctx) {
    static nvp::Limiter limiter;
    const uint64_t base = g_base.load(std::memory_order_relaxed);
    const uint64_t size = g_imageSize.load(std::memory_order_relaxed);
    nvp::Event e;
    char line[700];
    while (g_ring.pop(&e)) {
        if (!limiter.admit(e, nowMs)) continue;
        if (nvp::formatEvent(e, base, size, line, sizeof(line)) > 0) sink(ctx, line);
    }
    if (limiter.summary(nowMs, g_ring.dropped(), line, sizeof(line)) > 0) sink(ctx, line);
}

void nativeVrProbeFrameBoundary() {
    static uint32_t generation = ~0u;
    static bool on = false, said = false, tried = false;
    static int armed = 0;
    const Config& cfg = Config::get();
    const uint32_t now = cfg.generation();
    if (now != generation) {
        generation = now;
        const bool want = cfg.getBool("experimental.native_vr_probe", false);
        if (!said || want != on) {
            Log::get().note("native-vr-probe: %s (experimental.native_vr_probe = %d)",
                            want ? (tried ? "on again: the hooks record" : "on") : (tried ? "off: the hooks only forward now" : "off"),
                            want ? 1 : 0);
            said = true;
        }
        on = want;
    }
    if (on && !tried) {
        tried = true;
        armed = nativeVrProbeInstall(reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr)), nullptr, true, &logSink, nullptr);
        Log::get().note("native-vr-probe: installed on %d of %d points (build 332841)%s", armed, nvp::kPointCount,
                        armed == nvp::kPointCount ? "" : "; the lines above say why the rest were not");
    }
    nativeVrProbeSetRecording(on && armed > 0);
    if (armed > 0) nativeVrProbeDrain(GetTickCount64(), &logSink, nullptr);
}

}  // namespace edvr
