// On-foot markers: the hooks, the correction step, their installation and the frame thread's step (on_foot_markers.h says what and why).
#include "on_foot_markers.h"
#include "journal_watch.h"
#include "../common/code_hook.h"
#include "../common/config.h"
#include "../common/log.h"
#include <windows.h>
#include <atomic>
#include <cstring>
#include <string>

namespace edvr {
namespace {

using FnWriter = uint64_t (*)(uint64_t, uint64_t, uint64_t, uint64_t);
using FnCommit = void (*)(uint64_t cam, uint64_t size, uint64_t block);

// One hooked writer of the game: its CodeHook, the relay page near it and the forward to its original.
struct Point {
    CodeHook hook;
    uint8_t* relay = nullptr;
    std::atomic<uintptr_t> original{0};
    bool installed = false;
};
Point g_points[ofm::kPointCommit];  // the two writers; the commit is only called

std::atomic<uintptr_t> g_commit{0};
std::atomic<int> g_mode{ofm::kModeNone};
std::atomic<bool> g_disabled{false};
bool g_installed = false;

// The hooks' counters (ofm::Counts, cumulative).
std::atomic<uint64_t> g_poseCalls{0}, g_eyesOne{0}, g_eyesOther{0}, g_steps{0}, g_corrected{0}, g_commitFaults{0};
std::atomic<uint64_t> g_skips[ofm::kSkipCount];
// The cameras the last corrected step wrote, for the frame thread's end-of-frame check.
std::atomic<uint64_t> g_cam[ofm::kViews];
ofm::SampleBox g_sample;

// Per thread: how deep in the hooked writers this thread is. The step runs after the outer call only.
thread_local int t_depth = 0;

// ---- the hook side: no lock, no allocation, no log -------------------------------------------------------------------------------

__declspec(noinline) bool sehRead(void*, uint64_t addr, void* out, size_t n) noexcept {
    __try {
        std::memcpy(out, reinterpret_cast<const void*>(addr), n);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// The new matrix into cam+0x20 and the block's flags |= 0xF, as the game's pose setter does after a change.
__declspec(noinline) bool sehWritePose(uint64_t cam, const float* m) noexcept {
    __try {
        std::memcpy(reinterpret_cast<void*>(cam + ofm::kCamPose), m, sizeof(float) * ofm::kPoseFloats);
        *reinterpret_cast<volatile uint32_t*>(cam + ofm::kCamFlags) |= ofm::kStaleFlags;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

__declspec(noinline) bool sehCommit(uintptr_t commit, uint64_t cam, uint64_t size) noexcept {
    __try {
        reinterpret_cast<FnCommit>(commit)(cam, size, cam + ofm::kCamPose);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// After an outer writer call: the correction, while the frame thread says so.
void afterWriter(uint64_t mgr) {
    const int mode = g_mode.load(std::memory_order_relaxed);
    if (mode == ofm::kModeNone || g_disabled.load(std::memory_order_relaxed)) return;
    g_steps.fetch_add(1, std::memory_order_relaxed);
    ofm::Pair p;
    const int skip = ofm::locate(mgr, &sehRead, nullptr, &p);
    if (skip != ofm::kSkipNone) {
        g_skips[skip].fetch_add(1, std::memory_order_relaxed);
        return;
    }
    float out[ofm::kViews][ofm::kPoseFloats];
    ofm::correct(p.m, mode, out);
    if (g_sample.wanted()) g_sample.put(p.m, out);
    for (int i = 0; i < ofm::kViews; ++i) {
        if (!sehWritePose(p.cam[i], out[i])) {
            g_skips[ofm::kSkipUnreadable].fetch_add(1, std::memory_order_relaxed);
            return;
        }
    }
    const uintptr_t commit = g_commit.load(std::memory_order_acquire);
    for (int i = 0; i < ofm::kViews; ++i) {
        if (!sehCommit(commit, p.cam[i], p.size[i])) {
            if (g_commitFaults.fetch_add(1, std::memory_order_relaxed) + 1 >= ofm::kFaultsToDisable)
                g_disabled.store(true, std::memory_order_relaxed);
            return;
        }
    }
    g_corrected.fetch_add(1, std::memory_order_relaxed);
    for (int i = 0; i < ofm::kViews; ++i) g_cam[i].store(p.cam[i], std::memory_order_relaxed);
}

uint64_t forward(int point, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4) {
    ++t_depth;
    const uint64_t r = reinterpret_cast<FnWriter>(g_points[point].original.load(std::memory_order_acquire))(a1, a2, a3, a4);
    --t_depth;
    return r;
}

// 0x287D660 (mgr, a2): every argument register forwarded, the original's result returned.
__declspec(noinline) uint64_t poseHook(uint64_t mgr, uint64_t a2, uint64_t a3, uint64_t a4) {
    g_poseCalls.fetch_add(1, std::memory_order_relaxed);
    const uint64_t r = forward(ofm::kPointPose, mgr, a2, a3, a4);
    if (t_depth == 0) afterWriter(mgr);
    return r;
}

// 0x28457F0 (mgr, n, a3): n = 1 from inside 0x287D660, 2 from the pipeline frame 0x286B250.
__declspec(noinline) uint64_t eyesHook(uint64_t mgr, uint64_t n, uint64_t a3, uint64_t a4) {
    (static_cast<int32_t>(n) == 1 ? g_eyesOne : g_eyesOther).fetch_add(1, std::memory_order_relaxed);
    const uint64_t r = forward(ofm::kPointEyes, mgr, n, a3, a4);
    if (t_depth == 0) afterWriter(mgr);
    return r;
}

// ---- installation (frame thread) ------------------------------------------------------------------------------------------------

// A 5-byte jmp reaches +/-2 GB, and this DLL may load farther than that from the game: the patch jumps to a relay page placed near the
// target, which jumps on, absolutely, to the hook (jmp [rip+0]; dq hook). As on_foot_hud_probe.cpp does.
constexpr size_t kRelayBytes = 14;

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
bool prepare(void* trampoline, void* context) noexcept {
    auto& point = *static_cast<Point*>(context);
    point.original.store(reinterpret_cast<uintptr_t>(trampoline), std::memory_order_release);
    DWORD oldProtect = 0;
    return VirtualProtect(point.relay, 4096, PAGE_EXECUTE_READ, &oldProtect) &&
           FlushInstructionCache(GetCurrentProcess(), point.relay, kRelayBytes);
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

const void* hookFor(int point) {
    return point == ofm::kPointPose ? reinterpret_cast<const void*>(&poseHook) : reinterpret_cast<const void*>(&eyesHook);
}

// One writer's CodeHook through its relay (allocated beforehand). The relay is put back to writable on a refusal, so a retry can
// rewrite it.
bool hookPoint(int point, uintptr_t target) {
    Point& p = g_points[point];
    if (p.installed) return true;
    const uint8_t jump[6] = {0xFF, 0x25, 0, 0, 0, 0};
    const uintptr_t hook = reinterpret_cast<uintptr_t>(hookFor(point));
    std::memcpy(p.relay, jump, sizeof(jump));
    std::memcpy(p.relay + sizeof(jump), &hook, 8);
    if (!p.hook.install(reinterpret_cast<void*>(target), p.relay, nullptr, "on-foot-markers", &prepare, &p)) {
        DWORD oldProtect = 0;
        VirtualProtect(p.relay, 4096, PAGE_READWRITE, &oldProtect);
        return false;
    }
    // Process lifetime: the relay and trampoline are never freed, so a call already inside them can finish.
    p.installed = true;
    return true;
}

void say(OnFootMarkersSink sink, void* ctx, int n, const char* line) {
    if (n > 0) sink(ctx, line);
}

void logSink(void*, const char* line) { Log::get().note("%s", line); }

}  // namespace

bool onFootMarkersInstall(uintptr_t base, const uintptr_t* targets, bool checkIdentity, OnFootMarkersSink sink, void* ctx) {
    if (g_installed) return true;
    char line[600];
    if (checkIdentity) {
        uint32_t timestamp = 0, imageSize = 0;
        if (!sehIdentity(base, &timestamp, &imageSize) || timestamp != ofm::kExpectedTimestamp || imageSize != ofm::kExpectedImageSize) {
            say(sink, ctx, ofm::formatWrongBuild(timestamp, imageSize, line, sizeof(line)), line);
            return false;
        }
    }
    // Every point's bytes before anything is patched: half a correction (one writer) would be worse than none.
    uintptr_t at[ofm::kPointCount];
    for (int p = 0; p < ofm::kPointCount; ++p) {
        at[p] = targets ? targets[p] : base + ofm::kPointRva[p];
        uint8_t bytes[ofm::kPrologueBytes] = {};
        if (!sehRead(nullptr, at[p], bytes, sizeof(bytes)) || std::memcmp(bytes, ofm::kPointPrologue[p], sizeof(bytes)) != 0) {
            say(sink, ctx, ofm::formatWrongBytes(p, line, sizeof(line)), line);
            return false;
        }
    }
    for (int p = 0; p < ofm::kPointCommit; ++p) {
        if (!g_points[p].relay) g_points[p].relay = allocateRelay(at[p]);
        if (!g_points[p].relay) {
            say(sink, ctx, ofm::formatNotHooked(p, "no memory could be placed within 2 GB of it", line, sizeof(line)), line);
            return false;
        }
    }
    g_commit.store(at[ofm::kPointCommit], std::memory_order_release);
    for (int p = 0; p < ofm::kPointCommit; ++p) {
        if (hookPoint(p, at[p])) continue;
        // A hook already in is left in (taking it out could pull a trampoline from under a game call): it only forwards and counts,
        // the frame step never turns the correction on.
        say(sink, ctx, ofm::formatNotHooked(p, "CodeHook refused it (its own line above says why)", line, sizeof(line)), line);
        return false;
    }
    g_installed = true;
    say(sink, ctx,
        ofm::formatHooked(at[ofm::kPointPose], g_points[ofm::kPointPose].hook.stolenBytes(), at[ofm::kPointEyes],
                          g_points[ofm::kPointEyes].hook.stolenBytes(), line, sizeof(line)),
        line);
    return true;
}

bool onFootMarkersInstalled() { return g_installed; }
void onFootMarkersSetMode(int mode) { g_mode.store(mode, std::memory_order_relaxed); }
bool onFootMarkersDisabled() { return g_disabled.load(std::memory_order_relaxed); }

ofm::Counts onFootMarkersCounts() {
    ofm::Counts c;
    c.poseCalls = g_poseCalls.load(std::memory_order_relaxed);
    c.eyesOne = g_eyesOne.load(std::memory_order_relaxed);
    c.eyesOther = g_eyesOther.load(std::memory_order_relaxed);
    c.steps = g_steps.load(std::memory_order_relaxed);
    c.corrected = g_corrected.load(std::memory_order_relaxed);
    c.commitFaults = g_commitFaults.load(std::memory_order_relaxed);
    for (int s = 0; s < ofm::kSkipCount; ++s) c.skips[s] = g_skips[s].load(std::memory_order_relaxed);
    return c;
}

void onFootMarkersFrameStep(uint64_t nowMs, bool native, const char* keyText, int onFoot, uintptr_t base, const uintptr_t* targets,
                            bool checkIdentity, OnFootMarkersSink sink, void* ctx) {
    static bool said = false, lastNative = true, tried = false, saidDisabled = false;
    static char lastText[64] = {};
    static ofm::Key key;
    static ofm::Toggle toggle;
    static ofm::Window window;
    char line[900];
    const char* text = keyText ? keyText : "";

    // The key: its line at the first frame and on every change of its text or of native_vr_on_foot.
    if (!said || native != lastNative || std::strncmp(text, lastText, sizeof(lastText) - 1) != 0) {
        said = true;
        lastNative = native;
        std::strncpy(lastText, text, sizeof(lastText) - 1);
        key = ofm::parseKey(text);
        say(sink, ctx, ofm::formatKey(text, key, native, line, sizeof(line)), line);
    }

    // The decision; the one install attempt the first time it is on (a refusal is said once, not every frame).
    ofm::Decision d = ofm::decide(native, key.mode, onFoot);
    if (d.mode != ofm::kModeNone && !tried) {
        tried = true;
        onFootMarkersInstall(base, targets, checkIdentity, sink, ctx);
    }
    if (d.mode != ofm::kModeNone && !g_installed) d = ofm::Decision{ofm::kModeNone, ofm::kWhyNotInstalled};
    if (g_disabled.load(std::memory_order_relaxed)) {
        if (!saidDisabled) {
            saidDisabled = true;
            say(sink, ctx, ofm::formatDisabled(g_commitFaults.load(std::memory_order_relaxed), line, sizeof(line)), line);
        }
        if (d.mode != ofm::kModeNone) d = ofm::Decision{ofm::kModeNone, ofm::kWhyDisabled};
    }
    g_mode.store(d.mode, std::memory_order_relaxed);
    if (toggle.step(d)) {
        say(sink, ctx, ofm::formatToggle(d, line, sizeof(line)), line);
        if (d.mode != ofm::kModeNone) {
            window.start(nowMs, onFootMarkersCounts());
            g_sample.want();
        } else {
            window.stop();
        }
    }
    if (d.mode == ofm::kModeNone) return;

    // The end-of-frame check: both cameras the last step corrected, still the correction's rotation or someone else's.
    const uint64_t cam0 = g_cam[0].load(std::memory_order_relaxed), cam1 = g_cam[1].load(std::memory_order_relaxed);
    if (cam0 && cam1) {
        float m0[ofm::kPoseFloats], m1[ofm::kPoseFloats];
        const bool readable = sehRead(nullptr, cam0 + ofm::kCamPose, m0, sizeof(m0)) && sehRead(nullptr, cam1 + ofm::kCamPose, m1, sizeof(m1));
        window.note(readable, readable && ofm::identityRows(m0) && ofm::identityRows(m1));
    }

    ofm::Counts delta;
    ofm::Check check;
    uint64_t seconds = 0;
    if (window.due(nowMs, onFootMarkersCounts(), &delta, &check, &seconds)) {
        ofm::Sample s;
        const bool sampled = g_sample.take(&s);
        say(sink, ctx, ofm::formatSummary(seconds, d.mode, delta, check, sampled ? &s : nullptr, line, sizeof(line)), line);
        g_sample.want();
    }
}

void onFootMarkersFrameBoundary() {
    static uint32_t generation = ~0u;
    static bool native = true;
    static std::string text = "stereo";
    const Config& cfg = Config::get();
    const uint32_t now = cfg.generation();
    if (now != generation) {
        generation = now;
        native = cfg.getBool("experimental.native_vr_on_foot", true);
        text = cfg.getString("experimental.native_vr_on_foot_markers", "stereo");
    }
    const int onFoot = journalOnFootKnown() ? (journalOnFoot() ? 1 : 0) : -1;
    const uintptr_t base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    onFootMarkersFrameStep(GetTickCount64(), native, text.c_str(), onFoot, base, nullptr, true, &logSink, nullptr);
}

}  // namespace edvr
