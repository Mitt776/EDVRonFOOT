// On-foot HUD probe: the hook, its installation and the frame thread's drain (on_foot_hud_probe.h says what and why).
#include "on_foot_hud_probe.h"
#include "on_foot_hud_probe_core.h"
#include "journal_watch.h"
#include "../common/code_hook.h"
#include "../common/config.h"
#include "../common/log.h"
#include <windows.h>
#include <atomic>
#include <cstring>

namespace edvr {
namespace {

using Fn1 = void (*)(void*);

std::atomic<uintptr_t> g_original{0};
std::atomic<uintptr_t> g_vtable{0};  // base + the node vtable's RVA
std::atomic<bool> g_recording{false};
std::atomic<uint64_t> g_draws{0};  // every node draw seen while recording
std::atomic<uint64_t> g_calls{0};  // of them, the PostGUI nodes'
std::atomic<uint64_t> g_faults{0};
std::atomic<uint64_t> g_faultNode{0};  // the first node a field of which could not be read
ohp::Ring<64> g_ring;

// ---- the hook side: no lock, no allocation, no log -------------------------------------------------------------------------------

__declspec(noinline) bool sehRead(void*, uint64_t addr, void* out, size_t n) noexcept {
    __try {
        std::memcpy(out, reinterpret_cast<const void*>(addr), n);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// Ours or not, from two fields: anything else costs nothing more.
__declspec(noinline) bool sehIsOurs(const void* node, uintptr_t vtable) noexcept {
    __try {
        const auto* p = static_cast<const uint8_t*>(node);
        return ohp::isOurs(*reinterpret_cast<const uint64_t*>(p), *reinterpret_cast<const uint64_t*>(p + ohp::kNodeFirst),
                           vtable - ohp::kNodeVtableRva);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// After the original: a list's models may be handed in during the draw itself (0x594EF0's first loop), so before it would read empty.
__declspec(noinline) void nodeDrawHook(void* node) {
    reinterpret_cast<Fn1>(g_original.load(std::memory_order_acquire))(node);
    if (!g_recording.load(std::memory_order_relaxed) || !node) return;
    g_draws.fetch_add(1, std::memory_order_relaxed);
    if (!sehIsOurs(node, g_vtable.load(std::memory_order_relaxed))) return;
    g_calls.fetch_add(1, std::memory_order_relaxed);
    ohp::Snapshot s;
    if (!ohp::capture(reinterpret_cast<uint64_t>(node), &sehRead, nullptr, &s)) {
        g_faults.fetch_add(1, std::memory_order_relaxed);
        uint64_t none = 0;
        g_faultNode.compare_exchange_strong(none, reinterpret_cast<uint64_t>(node), std::memory_order_relaxed);
        return;
    }
    g_ring.push(s);
}

// ---- installation (frame thread) ------------------------------------------------------------------------------------------------

// A 5-byte jmp reaches +/-2 GB, and this DLL may load farther than that from the game: the patch jumps to a relay page placed near the
// target, which jumps on, absolutely, to the hook (jmp [rip+0]; dq hook). As native_vr_probe.cpp does.
constexpr size_t kRelayBytes = 14;
CodeHook g_hook;
uint8_t* g_relay = nullptr;
bool g_installed = false;

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
bool prepare(void* trampoline, void*) noexcept {
    g_original.store(reinterpret_cast<uintptr_t>(trampoline), std::memory_order_release);
    DWORD oldProtect = 0;
    return VirtualProtect(g_relay, 4096, PAGE_EXECUTE_READ, &oldProtect) &&
           FlushInstructionCache(GetCurrentProcess(), g_relay, kRelayBytes);
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

void say(OnFootHudProbeSink sink, void* ctx, int n, const char* line) {
    if (n > 0) sink(ctx, line);
}

void logSink(void*, const char* line) { Log::get().note("%s", line); }

}  // namespace

bool onFootHudProbeInstall(uintptr_t base, uintptr_t targetOverride, bool checkIdentity, OnFootHudProbeSink sink, void* ctx) {
    if (g_installed) return true;
    char line[400];
    if (checkIdentity) {
        uint32_t timestamp = 0, imageSize = 0;
        if (!sehIdentity(base, &timestamp, &imageSize) || timestamp != ohp::kExpectedTimestamp || imageSize != ohp::kExpectedImageSize) {
            say(sink, ctx, ohp::formatWrongBuild(timestamp, imageSize, line, sizeof(line)), line);
            return false;
        }
    }
    const uintptr_t target = targetOverride ? targetOverride : base + ohp::kNodeDrawRva;
    uint8_t bytes[ohp::kPrologueBytes] = {};
    if (!sehRead(nullptr, target, bytes, sizeof(bytes)) || std::memcmp(bytes, ohp::kPrologue, sizeof(bytes)) != 0) {
        say(sink, ctx, ohp::formatWrongBytes(line, sizeof(line)), line);
        return false;
    }
    if (!g_relay) g_relay = allocateRelay(target);
    if (!g_relay) {
        sink(ctx, "on-foot hud probe: NOT installed: no memory could be placed within 2 GB of the node draw; nothing was patched");
        return false;
    }
    g_vtable.store(base + ohp::kNodeVtableRva, std::memory_order_relaxed);
    const uint8_t jump[6] = {0xFF, 0x25, 0, 0, 0, 0};
    const uintptr_t hook = reinterpret_cast<uintptr_t>(&nodeDrawHook);
    std::memcpy(g_relay, jump, sizeof(jump));
    std::memcpy(g_relay + sizeof(jump), &hook, 8);
    if (!g_hook.install(reinterpret_cast<void*>(target), g_relay, nullptr, "on-foot-hud-probe", &prepare, nullptr)) {
        sink(ctx, "on-foot hud probe: NOT installed: CodeHook refused the node draw (its own line above says why); nothing was patched");
        DWORD oldProtect = 0;
        VirtualProtect(g_relay, 4096, PAGE_READWRITE, &oldProtect);  // so a retry can rewrite it
        return false;
    }
    g_installed = true;
    say(sink, ctx, ohp::formatHooked(target, g_hook.stolenBytes(), line, sizeof(line)), line);
    // Process lifetime: the relay and trampoline are never freed, so a call already inside them can finish.
    return true;
}

void onFootHudProbeSetRecording(bool on) { g_recording.store(on, std::memory_order_relaxed); }
uint64_t onFootHudProbeCalls() { return g_calls.load(std::memory_order_relaxed); }
uint64_t onFootHudProbeFaults() { return g_faults.load(std::memory_order_relaxed); }

void onFootHudProbeDrain(uint64_t nowMs, int onFoot, OnFootHudProbeSink sink, void* ctx) {
    static ohp::ChangeFilter filter;
    static ohp::Summary summary;
    static bool saidFault = false;
    char line[1600];
    ohp::Snapshot s, write;
    while (g_ring.pop(&s)) {
        s.onFoot = onFoot;
        if (filter.admit(s, nowMs, &write)) say(sink, ctx, ohp::formatSnapshot(write, line, sizeof(line)), line);
    }
    while (filter.due(nowMs, &write)) say(sink, ctx, ohp::formatSnapshot(write, line, sizeof(line)), line);
    const uint64_t faultNode = g_faultNode.load(std::memory_order_relaxed);
    if (faultNode && !saidFault) {
        saidFault = true;
        say(sink, ctx, ohp::formatFault(faultNode, line, sizeof(line)), line);
    }
    const ohp::SummaryCounts total{g_draws.load(std::memory_order_relaxed), g_calls.load(std::memory_order_relaxed), filter.written(), filter.repeats(), filter.held(),
                                   g_ring.dropped(), g_faults.load(std::memory_order_relaxed)};
    say(sink, ctx, summary.step(nowMs, total, filter.nodes(nowMs, ohp::Summary::kWindowMs), line, sizeof(line)), line);
}

void onFootHudProbeFrameBoundary() {
    static uint32_t generation = ~0u;
    static bool on = false, said = false, tried = false;
    const Config& cfg = Config::get();
    const uint32_t now = cfg.generation();
    if (now != generation) {
        generation = now;
        const bool want = cfg.getBool("experimental.on_foot_hud_probe", false);
        if (!said || want != on) {
            char line[200];
            if (ohp::formatKey(want, g_installed, line, sizeof(line)) > 0) Log::get().note("%s", line);
            said = true;
        }
        on = want;
    }
    if (on && !tried) {
        tried = true;  // one attempt per launch: a refusal is said once, not every frame
        onFootHudProbeInstall(reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr)), 0, true, &logSink, nullptr);
    }
    onFootHudProbeSetRecording(on && g_installed);
    if (on && g_installed) {
        const int onFoot = journalOnFootKnown() ? (journalOnFoot() ? 1 : 0) : -1;
        onFootHudProbeDrain(GetTickCount64(), onFoot, &logSink, nullptr);
    }
}

}  // namespace edvr
