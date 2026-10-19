// Native-VR-on-foot probe: the hooks, their installation and the frame thread's drain (native_vr_probe.h says what and why).
#include "native_vr_probe.h"
#include "native_vr_probe_core.h"
#include "journal_watch.h"
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
using FnSource = uint64_t (*)(void*, void*, void*, void*);
using FnCamera = void* (*)(void*, void*);

std::atomic<uintptr_t> g_original[nvp::kPointCount];
std::atomic<bool> g_recording{false};
std::atomic<bool> g_block{false};
std::atomic<bool> g_onFoot{false};
// The hold (nvp::holdStep). The source hook steps it on the game's render thread; the frame thread only ends it by the journal.
std::atomic<bool> g_holding{false};
std::atomic<int32_t> g_holdValue{0};
std::atomic<uint64_t> g_holdRepeats{0};
// What X+0x20 holds after the source hook (the camera line's mode), and the game's mode the probe last wrote a line for.
constexpr int32_t kNotSeen = INT32_MIN;
constexpr int32_t kUnreadable = INT32_MIN + 1;
std::atomic<int32_t> g_sourceMode{kNotSeen};
std::atomic<int32_t> g_sourceSaid{kNotSeen};
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

// The view pipeline P: where its stereo mode lives and what it reads there.
__declspec(noinline) uint8_t sehReadPipelineMode(const uint8_t* pipeline, int32_t** field, int32_t* mode) noexcept {
    __try {
        const uint8_t* config = *reinterpret_cast<const uint8_t* const*>(pipeline + nvp::kPipelineConfig);
        *field = reinterpret_cast<int32_t*>(const_cast<uint8_t*>(config) + nvp::kConfigStereoMode);
        *mode = **field;
        return 1;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

// Bit i: sub-pipeline i built (+0x329), with cinema (+0x330). An unreadable sub-pipeline counts as neither.
__declspec(noinline) void sehReadSubs(const uint8_t* pipeline, uint8_t* built, uint8_t* cinema) noexcept {
    *built = *cinema = 0;
    for (int i = 0; i < nvp::kSubPipelines; ++i) {
        __try {
            const uint8_t* sub = *reinterpret_cast<const uint8_t* const*>(pipeline + nvp::kPipelineSubs + 8 * i);
            if (sub && sub[nvp::kSubBuilt]) *built |= static_cast<uint8_t>(1u << i);
            if (sub && sub[nvp::kSubCinema]) *cinema |= static_cast<uint8_t>(1u << i);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
    }
}

// Write `to` only while the field still holds `from`: what the block put there, it takes back, and nothing else.
__declspec(noinline) bool sehSwapMode(int32_t* field, int32_t from, int32_t to) noexcept {
    __try {
        if (*field != from) return false;
        *field = to;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// The source's mode field X+0x20 (X = *(this+0x7F8)) and the renderer S = *(this+0x50), whose view settings the line names.
__declspec(noinline) uint8_t sehReadSource(const uint8_t* self, int32_t** field, int32_t* mode, uint64_t* x, uint64_t* renderer) noexcept {
    __try {
        const uint8_t* object = *reinterpret_cast<const uint8_t* const*>(self + nvp::kSourceObject);
        *x = reinterpret_cast<uint64_t>(object);
        *renderer = *reinterpret_cast<const uint64_t*>(self + nvp::kSourceRenderer);
        *field = reinterpret_cast<int32_t*>(const_cast<uint8_t*>(object) + nvp::kSourceStereoMode);
        *mode = **field;
        return 1;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

// The name the camera choice just made at *out: up to kNameBytes - 1 characters, anything unprintable as '?'. No string: "(none)".
__declspec(noinline) uint8_t sehReadName(void* const* out, char* name) noexcept {
    __try {
        const uint8_t* str = static_cast<const uint8_t*>(*out);
        if (!str) {
            std::memcpy(name, "(none)", 7);
            return 1;
        }
        const uint64_t length = *reinterpret_cast<const uint64_t*>(str);
        const size_t n = length < nvp::kNameBytes - 1 ? static_cast<size_t>(length) : nvp::kNameBytes - 1;
        for (size_t i = 0; i < n; ++i) {
            const char c = static_cast<char>(str[nvp::kNameChars + i]);
            name[i] = c >= 32 && c < 127 ? c : '?';
        }
        name[n] = 0;
        return 1;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        name[0] = 0;
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

// The rebuild decides cinema or stereo from the view's stereo mode. With the block on and a cinema mode (5, 6) there, the hook puts
// the headset stereo mode (3, 4) in its place for this one call and takes it back after: the pipeline is built for stereo -- both
// eyes draw the scene, no cinema node -- while the field keeps the value the game holds. Same thread as the game's own reads.
__declspec(noinline) void buildHook(void* self) {
    const bool recording = g_recording.load(std::memory_order_relaxed);
    const bool block = g_block.load(std::memory_order_relaxed);
    nvp::Event e;
    int32_t* field = nullptr;
    if (recording || block) e.readOk = sehReadPipelineMode(static_cast<const uint8_t*>(self), &field, &e.mode);
    if (e.readOk) e.other = reinterpret_cast<uint64_t>(field) - nvp::kConfigStereoMode;
    if (block && e.readOk) {
        const int32_t stereo = nvp::cinemaModeSubstitute(e.mode);
        if (stereo && sehSwapMode(field, e.mode, stereo)) e.substituted = stereo;
    }
    reinterpret_cast<Fn1>(g_original[nvp::kPointBuild].load(std::memory_order_acquire))(self);
    if (e.substituted) sehSwapMode(field, e.substituted, e.mode);
    if (recording || e.substituted) {
        e.point = nvp::kPointBuild;
        e.self = reinterpret_cast<uint64_t>(self);
        if (e.readOk) sehReadSubs(static_cast<const uint8_t*>(self), &e.built, &e.cinema);
        e.thread = GetCurrentThreadId();
        captureStack(e);
        g_ring.push(e);
        g_recorded.fetch_add(1, std::memory_order_relaxed);
    }
}

__declspec(noinline) void teardownHook(void* self) {
    if (g_recording.load(std::memory_order_relaxed)) {
        nvp::Event e;
        e.point = nvp::kPointTeardown;
        e.self = reinterpret_cast<uint64_t>(self);
        int32_t* field = nullptr;
        e.readOk = sehReadPipelineMode(static_cast<const uint8_t*>(self), &field, &e.mode);
        if (e.readOk) {
            e.other = reinterpret_cast<uint64_t>(field) - nvp::kConfigStereoMode;
            sehReadSubs(static_cast<const uint8_t*>(self), &e.built, &e.cinema);
        }
        e.thread = GetCurrentThreadId();
        captureStack(e);
        g_ring.push(e);
        g_recorded.fetch_add(1, std::memory_order_relaxed);
    }
    reinterpret_cast<Fn1>(g_original[nvp::kPointTeardown].load(std::memory_order_acquire))(self);
}

// The stereo mode source, once a frame on the game's render thread, BEFORE the original copies X+0x20 into the renderer's request and
// decides whether to compute the headset eyes' projections. With experimental.native_vr_on_foot on, a cinema mode there becomes the
// headset stereo mode (nvp::holdStep), so the request, the projections, the pipeline and the camera all see stereo; the game never
// rebuilds for cinema. The write is a compare-and-swap of the value just read, never a cinema mode. With the probe on, a mode the game
// changed is one event (not one a frame).
__declspec(noinline) uint64_t sourceHook(void* self, void* a, void* b, void* c) {
    const bool recording = g_recording.load(std::memory_order_relaxed);
    const bool key = g_onFoot.load(std::memory_order_relaxed);
    if (recording || key || g_holding.load(std::memory_order_relaxed)) {
        nvp::Event e;
        e.point = nvp::kPointSource;
        e.self = reinterpret_cast<uint64_t>(self);
        int32_t* field = nullptr;
        e.readOk = sehReadSource(static_cast<const uint8_t*>(self), &field, &e.mode, &e.other, &e.aux);
        bool push = false;
        if (e.readOk) {
            nvp::HoldState s;
            s.holding = g_holding.load(std::memory_order_acquire);
            s.value = g_holdValue.load(std::memory_order_relaxed);
            const nvp::HoldStep step = nvp::holdStep(s, key, e.mode);
            if (!step.write || sehSwapMode(field, e.mode, step.write)) {
                g_holdValue.store(s.value, std::memory_order_relaxed);
                g_holding.store(s.holding, std::memory_order_release);
                g_sourceMode.store(step.write ? step.write : e.mode, std::memory_order_relaxed);
                if (step.kind == nvp::kHoldStart) g_holdRepeats.store(0, std::memory_order_relaxed);
                if (step.kind == nvp::kHoldAgain) e.aux = g_holdRepeats.fetch_add(1, std::memory_order_relaxed) + 1;
                e.kind = step.kind;
                e.substituted = step.write;
                push = step.kind != nvp::kHoldNone;
            }
            if (recording && g_sourceSaid.exchange(e.mode, std::memory_order_relaxed) != e.mode) push = true;
        } else if (recording && g_sourceSaid.exchange(kUnreadable, std::memory_order_relaxed) != kUnreadable) {
            push = true;
        }
        if (push) {
            e.thread = GetCurrentThreadId();
            g_ring.push(e);
            g_recorded.fetch_add(1, std::memory_order_relaxed);
        }
    }
    return reinterpret_cast<FnSource>(g_original[nvp::kPointSource].load(std::memory_order_acquire))(self, a, b, c);
}

// The camera choice makes its name only on the way out: the original first, then the name is copied out of the new string.
__declspec(noinline) void* cameraHook(void* self, void* out) {
    void* result = reinterpret_cast<FnCamera>(g_original[nvp::kPointCamera].load(std::memory_order_acquire))(self, out);
    if (g_recording.load(std::memory_order_relaxed)) {
        nvp::Event e;
        e.point = nvp::kPointCamera;
        e.self = reinterpret_cast<uint64_t>(self);
        e.readOk = sehReadName(static_cast<void* const*>(out), e.name);
        e.mode = g_sourceMode.load(std::memory_order_relaxed);
        if (g_holding.load(std::memory_order_acquire)) e.substituted = g_holdValue.load(std::memory_order_relaxed);
        e.thread = GetCurrentThreadId();
        g_ring.push(e);
        g_recorded.fetch_add(1, std::memory_order_relaxed);
    }
    return result;
}

const void* hookFor(int point) {
    switch (point) {
        case nvp::kPointMode: return reinterpret_cast<const void*>(&modeHook);
        case nvp::kPointCinemaOn: return reinterpret_cast<const void*>(&cinemaOnHook);
        case nvp::kPointCinemaOff: return reinterpret_cast<const void*>(&cinemaOffHook);
        case nvp::kPointBuild: return reinterpret_cast<const void*>(&buildHook);
        case nvp::kPointSource: return reinterpret_cast<const void*>(&sourceHook);
        case nvp::kPointCamera: return reinterpret_cast<const void*>(&cameraHook);
        default: return reinterpret_cast<const void*>(&teardownHook);
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
        size_t prologueBytes = 0;
        const uint8_t* prologue = nvp::prologueOf(point, &prologueBytes);
        if (!sehCheckBytes(target, prologue, prologueBytes)) {
            say(sink, ctx, "native-vr-probe: %s at EliteDangerous64.exe+0x%llX NOT hooked: its first %zu bytes are not build 332841's; nothing "
                           "was patched there",
                nvp::kPointName[point], static_cast<unsigned long long>(nvp::kPointRva[point]), prologueBytes);
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
                       "it records and then calls the original unchanged%s",
            nvp::kPointName[point], static_cast<unsigned long long>(nvp::kPointRva[point]), static_cast<unsigned long long>(target),
            prologueBytes, slot.hook.stolenBytes(),
            point == nvp::kPointBuild    ? " (only experimental.native_vr_block_cinema changes what it sees)"
            : point == nvp::kPointSource ? " (only experimental.native_vr_on_foot changes what it sees)"
                                         : "");
        // Process lifetime: the relay and trampoline are never freed, so a call already inside them can finish.
    }
    return armed;
}

void nativeVrProbeSetRecording(bool on) { g_recording.store(on, std::memory_order_relaxed); }
void nativeVrProbeSetBlockCinema(bool on) { g_block.store(on, std::memory_order_relaxed); }
void nativeVrProbeSetOnFoot(bool on) { g_onFoot.store(on, std::memory_order_relaxed); }
bool nativeVrProbeHolding() { return g_holding.load(std::memory_order_acquire); }

void nativeVrOnFootJournal(int onFoot, NativeVrProbeSink sink, void* ctx) {
    static int last = -1;
    if (last == 1 && onFoot == 0 && g_holding.exchange(false, std::memory_order_acq_rel))
        say(sink, ctx, "native-vr-on-foot: hold end: back in the ship (journal: on foot yes -> no) after %llu repeat(s); the mode is the "
                       "game's again",
            static_cast<unsigned long long>(g_holdRepeats.load(std::memory_order_relaxed)));
    last = onFoot;
}
bool nativeVrProbeArmed(int point) { return point >= 0 && point < nvp::kPointCount && g_slots[point].armed; }
uint64_t nativeVrProbeRecorded() { return g_recorded.load(std::memory_order_relaxed); }
uint64_t nativeVrProbeDropped() { return g_ring.dropped(); }

void nativeVrProbeDrain(uint64_t nowMs, int onFoot, NativeVrProbeSink sink, void* ctx) {
    static nvp::Limiter limiter;
    static nvp::ChangeFilter changes;
    const uint64_t base = g_base.load(std::memory_order_relaxed);
    const uint64_t size = g_imageSize.load(std::memory_order_relaxed);
    nvp::Event e;
    char line[700];
    while (g_ring.pop(&e)) {
        const bool pipeline = e.point == nvp::kPointBuild || e.point == nvp::kPointTeardown;
        // A decision or camera line only when it changed; a block or hold line every time, through the limiter, so repeats are counted.
        const bool changeOnly = (pipeline && !e.substituted) || e.point == nvp::kPointCamera;
        if (changeOnly ? !changes.admit(e) : !limiter.admit(e, nowMs)) continue;
        if (pipeline && e.substituted) changes.admit(e);
        if (nvp::formatEvent(e, base, size, line, sizeof(line), onFoot) > 0) sink(ctx, line);
    }
    if (limiter.summary(nowMs, g_ring.dropped(), line, sizeof(line)) > 0) sink(ctx, line);
}

void nativeVrProbeFrameBoundary() {
    static uint32_t generation = ~0u;
    static bool on = false, block = false, said = false, saidBlock = false, tried = false, saidBlockUnarmed = false;
    static bool onFootKey = false, saidOnFoot = false, saidOnFootUnarmed = false;
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
        const bool wantBlock = cfg.getBool("experimental.native_vr_block_cinema", false);
        if (!saidBlock || wantBlock != block) {
            Log::get().note("native-vr-block-cinema: %s (experimental.native_vr_block_cinema = %d)",
                            wantBlock ? "on: the next view pipeline rebuild in a cinema mode is built for headset stereo instead"
                                      : (saidBlock ? "off: the next rebuild is the game's own again" : "off"),
                            wantBlock ? 1 : 0);
            saidBlock = true;
        }
        block = wantBlock;
        const bool wantOnFoot = cfg.getBool("experimental.native_vr_on_foot", false);
        if (!saidOnFoot || wantOnFoot != onFootKey) {
            char line[200];
            nvp::formatOnFootKey(wantOnFoot, saidOnFoot, line, sizeof(line));
            Log::get().note("%s", line);
            saidOnFoot = true;
        }
        onFootKey = wantOnFoot;
    }
    if ((on || block || onFootKey) && !tried) {
        tried = true;
        armed = nativeVrProbeInstall(reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr)), nullptr, true, &logSink, nullptr);
        char line[260];
        nvp::formatInstalled(armed, nativeVrProbeArmed(nvp::kPointCamera), line, sizeof(line));
        Log::get().note("%s", line);
    }
    const bool sourceArmed = nativeVrProbeArmed(nvp::kPointSource);
    if (onFootKey && tried && !sourceArmed && !saidOnFootUnarmed) {
        Log::get().note("native-vr-on-foot: cannot work: the stereo mode source is not hooked (see the lines above); the game keeps its "
                        "cinema");
        saidOnFootUnarmed = true;
    }
    nativeVrProbeSetOnFoot(onFootKey && sourceArmed);
    const bool blockArmed = nativeVrProbeArmed(nvp::kPointBuild);
    if (block && tried && !blockArmed && !saidBlockUnarmed) {
        Log::get().note("native-vr-block-cinema: cannot work: the view pipeline rebuild is not hooked (see the lines above); the game "
                        "keeps its cinema");
        saidBlockUnarmed = true;
    }
    nativeVrProbeSetRecording(on && armed > 0);
    nativeVrProbeSetBlockCinema(block && blockArmed);
    if (armed > 0) {
        const int onFoot = journalOnFootKnown() ? (journalOnFoot() ? 1 : 0) : -1;
        nativeVrProbeDrain(GetTickCount64(), onFoot, &logSink, nullptr);
        nativeVrOnFootJournal(onFoot, &logSink, nullptr);
    }
}

}  // namespace edvr
