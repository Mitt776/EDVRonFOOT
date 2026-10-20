// Native-VR-on-foot probe: the hooks, their installation and the frame thread's drain (native_vr_probe.h says what and why).
#include "native_vr_probe.h"
#include "native_vr_probe_core.h"
#include "debug_register_core.h"
#include "journal_watch.h"
#include "../common/code_hook.h"
#include "../common/config.h"
#include "../common/log.h"
#include <windows.h>
#include <tlhelp32.h>
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
using FnCall4 = uint64_t (*)(void*, void*, void*, void*);  // the camera director's methods: every argument register forwarded as is

std::atomic<uintptr_t> g_original[nvp::kPointCount];
std::atomic<bool> g_recording{false};
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
// The writer watch (nvp::WriterTable): the address a hold start asks the frame thread to watch, the address being watched (the
// handler's match), and the range of this module, whose writes are the hold's own.
std::atomic<uintptr_t> g_watchWanted{0};
std::atomic<uintptr_t> g_watchAddress{0}, g_watchPrevious{0};
std::atomic<bool> g_watchActive{false};  // between arming and the end of disarming: a hit is recorded, otherwise only taken off
std::atomic<uintptr_t> g_ownLo{0}, g_ownHi{0};
nvp::WriterTable g_writers;

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

// The cinema-on-foot switch (nvp::cinemaSwitchStep): its value, -1 if unreadable; and a compare-and-swap, so a value the game put
// there between the read and the write is never overwritten.
__declspec(noinline) int sehReadByte(const uint8_t* p) noexcept {
    __try {
        return *reinterpret_cast<const volatile uint8_t*>(p);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return -1;
    }
}
__declspec(noinline) bool sehSwapByte(uint8_t* p, uint8_t from, uint8_t to) noexcept {
    __try {
        return static_cast<uint8_t>(_InterlockedCompareExchange8(reinterpret_cast<volatile char*>(p), static_cast<char>(to),
                                                                 static_cast<char>(from))) == from;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// The image's identity and sections from its own headers, then the switch's pattern in its executable sections (nvp::findCinemaSwitch).
// False if the headers could not be read or name more sections than fit; a fault while scanning is a not-found.
constexpr int kMaxSections = 32;
__declspec(noinline) bool sehFindSwitch(uintptr_t base, nvp::SwitchFind* found, uint32_t* timestamp, uint32_t* imageSize) noexcept {
    __try {
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
        *timestamp = nt->FileHeader.TimeDateStamp;
        *imageSize = nt->OptionalHeader.SizeOfImage;
        const int count = nt->FileHeader.NumberOfSections;
        if (count > kMaxSections) return false;
        nvp::ImageSection sections[kMaxSections];
        const IMAGE_SECTION_HEADER* h = IMAGE_FIRST_SECTION(nt);
        for (int i = 0; i < count; ++i) {
            sections[i].rva = h[i].VirtualAddress;
            sections[i].size = h[i].Misc.VirtualSize;
            sections[i].execute = (h[i].Characteristics & IMAGE_SCN_MEM_EXECUTE) != 0;
            sections[i].write = (h[i].Characteristics & IMAGE_SCN_MEM_WRITE) != 0;
            if (sections[i].rva + static_cast<uint64_t>(sections[i].size) > *imageSize) sections[i].size = 0;  // never past the image
        }
        *found = nvp::findCinemaSwitch(reinterpret_cast<const uint8_t*>(base), sections, count);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        *found = nvp::SwitchFind{};
        return true;
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

// The name a camera director's method was asked for, a C string: up to kNameBytes - 1 characters, anything unprintable as '?'. No
// pointer: "(none)".
__declspec(noinline) uint8_t sehReadCString(const char* text, char* name) noexcept {
    __try {
        if (!text) {
            std::memcpy(name, "(none)", 7);
            return 1;
        }
        size_t n = 0;
        for (; n < nvp::kNameBytes - 1 && text[n]; ++n) name[n] = text[n] >= 32 && text[n] < 127 ? text[n] : '?';
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

// The rebuild decides cinema or stereo from the view's stereo mode: the mode before the call, the sub-pipelines it built after.
__declspec(noinline) void buildHook(void* self) {
    const bool recording = g_recording.load(std::memory_order_relaxed);
    nvp::Event e;
    int32_t* field = nullptr;
    if (recording) e.readOk = sehReadPipelineMode(static_cast<const uint8_t*>(self), &field, &e.mode);
    if (e.readOk) e.other = reinterpret_cast<uint64_t>(field) - nvp::kConfigStereoMode;
    reinterpret_cast<Fn1>(g_original[nvp::kPointBuild].load(std::memory_order_acquire))(self);
    if (recording) {
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
                if (step.kind == nvp::kHoldStart) {
                    g_holdRepeats.store(0, std::memory_order_relaxed);
                    // The probe watches who writes cinema here next (the frame thread arms it).
                    if (recording) g_watchWanted.store(reinterpret_cast<uintptr_t>(field), std::memory_order_release);
                }
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

// The camera director's switch by name (points 7 and 9): the name it was asked for, read before the original -- which picks "HMD " +
// name or the plain name inside -- then the original with every argument register as it came.
template <int Point>
__declspec(noinline) uint64_t cameraByNameHook(void* self, void* name, void* flag, void* fourth) {
    if (g_recording.load(std::memory_order_relaxed)) {
        nvp::Event e;
        e.point = static_cast<uint8_t>(Point);
        e.self = reinterpret_cast<uint64_t>(self);
        e.readOk = sehReadCString(static_cast<const char*>(name), e.name);
        e.flagBefore = static_cast<uint8_t>(reinterpret_cast<uintptr_t>(flag));
        e.mode = g_sourceMode.load(std::memory_order_relaxed);
        if (g_holding.load(std::memory_order_acquire)) e.substituted = g_holdValue.load(std::memory_order_relaxed);
        e.thread = GetCurrentThreadId();
        captureStack(e);
        g_ring.push(e);
        g_recorded.fetch_add(1, std::memory_order_relaxed);
    }
    return reinterpret_cast<FnCall4>(g_original[Point].load(std::memory_order_acquire))(self, name, flag, fourth);
}

// The director's first-person default (point 8): that it was asked, and at which mode; its call to point 7 says the name.
__declspec(noinline) uint64_t cameraDefaultHook(void* self, void* a, void* b, void* c) {
    if (g_recording.load(std::memory_order_relaxed)) {
        nvp::Event e;
        e.point = nvp::kPointCameraDefault;
        e.self = reinterpret_cast<uint64_t>(self);
        e.readOk = 1;
        e.mode = g_sourceMode.load(std::memory_order_relaxed);
        if (g_holding.load(std::memory_order_acquire)) e.substituted = g_holdValue.load(std::memory_order_relaxed);
        e.thread = GetCurrentThreadId();
        captureStack(e);
        g_ring.push(e);
        g_recorded.fetch_add(1, std::memory_order_relaxed);
    }
    return reinterpret_cast<FnCall4>(g_original[nvp::kPointCameraDefault].load(std::memory_order_acquire))(self, a, b, c);
}

const void* hookFor(int point) {
    switch (point) {
        case nvp::kPointMode: return reinterpret_cast<const void*>(&modeHook);
        case nvp::kPointCinemaOn: return reinterpret_cast<const void*>(&cinemaOnHook);
        case nvp::kPointCinemaOff: return reinterpret_cast<const void*>(&cinemaOffHook);
        case nvp::kPointBuild: return reinterpret_cast<const void*>(&buildHook);
        case nvp::kPointSource: return reinterpret_cast<const void*>(&sourceHook);
        case nvp::kPointCamera: return reinterpret_cast<const void*>(&cameraHook);
        case nvp::kPointCameraByName: return reinterpret_cast<const void*>(&cameraByNameHook<nvp::kPointCameraByName>);
        case nvp::kPointCameraDefault: return reinterpret_cast<const void*>(&cameraDefaultHook);
        case nvp::kPointCameraByName2: return reinterpret_cast<const void*>(&cameraByNameHook<nvp::kPointCameraByName2>);
        default: return reinterpret_cast<const void*>(&teardownHook);
    }
}

// ---- the writer watch: the handler side (any thread; no lock, no allocation, no log) ----------------------------------------------

// The callers of the writing instruction, unwound from the CPU's own context of the write (the handler's stack is not the writer's).
// A frame with no unwind data is a leaf: its return address is at rsp.
__declspec(noinline) int sehUnwind(const CONTEXT* at, uint64_t* stack) noexcept {
    int n = 0;
    __try {
        CONTEXT c = *at;
        for (; n < nvp::kStackFrames; ++n) {
            DWORD64 imageBase = 0;
            PRUNTIME_FUNCTION fn = RtlLookupFunctionEntry(c.Rip, &imageBase, nullptr);
            if (fn) {
                PVOID handlerData = nullptr;
                DWORD64 establisher = 0;
                RtlVirtualUnwind(UNW_FLAG_NHANDLER, imageBase, c.Rip, fn, &c, &handlerData, &establisher, nullptr);
            } else {
                c.Rip = *reinterpret_cast<const DWORD64*>(c.Rsp);
                c.Rsp += 8;
            }
            if (!c.Rip) break;
            stack[n] = c.Rip;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    return n;
}

LONG CALLBACK writerWatchHandler(EXCEPTION_POINTERS* ep) {
    if (ep->ExceptionRecord->ExceptionCode != EXCEPTION_SINGLE_STEP) return EXCEPTION_CONTINUE_SEARCH;
    // Never cleared once set, and the one before kept: a write that trapped just before its thread was disarmed may reach this handler
    // after the frame thread moved on, and an unclaimed single-step would end the game.
    const uintptr_t address = g_watchAddress.load(std::memory_order_acquire);
    const uintptr_t previous = g_watchPrevious.load(std::memory_order_acquire);
    CONTEXT* c = ep->ContextRecord;
    const DWORD64 hitBit = DWORD64(1) << nvp::kWatchSlot;
    // Slot 1 is this watch's alone in EDVR (the flat producer probe has slot 0, the vtable write watch uses page protection), so its
    // hit is ours whatever Dr1 holds; a context that reports no slot at all is ours only at an address we watched. An unclaimed
    // single-step ends the game (common\vtable_hook.cpp measured it), so the test leans toward claiming.
    const bool slotHit = (c->Dr6 & hitBit) != 0;
    const bool known = c->Dr1 && (c->Dr1 == address || c->Dr1 == previous);
    if (!slotHit && !(known && !(c->Dr6 & 0xF))) return EXCEPTION_CONTINUE_SEARCH;
    c->Dr6 &= ~hitBit;
    if (!g_watchActive.load(std::memory_order_acquire) || c->Dr1 != address) {
        // A straggler: a thread the frame thread could not take the watch off, or a trap that arrived after it did. Off here, in the
        // context this thread resumes with.
        c->Dr7 = (c->Dr7 & ~DWORD64(0xFFFFFFFFu)) | dr7::disarmSlotDr7(static_cast<uint32_t>(c->Dr7), nvp::kWatchSlot);
        c->Dr1 = 0;
        return EXCEPTION_CONTINUE_EXECUTION;
    }
    const uintptr_t rip = static_cast<uintptr_t>(c->Rip);
    if (rip >= g_ownLo.load(std::memory_order_relaxed) && rip < g_ownHi.load(std::memory_order_relaxed)) {
        g_writers.countOwn();
    } else {
        bool fresh = false;
        const int row = g_writers.record(rip, &fresh);
        if (fresh) {
            uint64_t stack[nvp::kStackFrames] = {};
            const int frames = sehUnwind(c, stack);
            g_writers.publish(row, GetCurrentThreadId(), stack, frames);
        }
    }
    return EXCEPTION_CONTINUE_EXECUTION;
}

// ---- the writer watch: the frame thread side ------------------------------------------------------------------------------------

enum : int { kDrFailed = 0, kDrDone = 1, kDrSkipped = 2 };

// Slot kWatchSlot on one thread, by handle, from OUTSIDE that thread (a running thread's own debug registers cannot be set by
// itself): suspend, read, change only that slot, write, resume. Arming leaves a thread whose slot someone else holds alone; disarming
// touches only a slot that still holds our address.
int setThreadWatch(DWORD tid, uintptr_t address, bool arm) noexcept {
    HANDLE h = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_SET_CONTEXT, FALSE, tid);
    if (!h) return kDrFailed;
    int result = kDrFailed;
    if (SuspendThread(h) != static_cast<DWORD>(-1)) {
        CONTEXT ctx{};
        ctx.ContextFlags = CONTEXT_DEBUG_REGISTERS;
        if (GetThreadContext(h, &ctx)) {
            const uint32_t low = static_cast<uint32_t>(ctx.Dr7);
            const DWORD64 high = ctx.Dr7 & ~DWORD64(0xFFFFFFFFu);
            const bool ours = ctx.Dr1 == address;
            if (arm) {
                if (dr7::slotEnabledDr7(low, nvp::kWatchSlot) && !ours) {
                    result = kDrSkipped;
                } else {
                    ctx.Dr1 = address;
                    ctx.Dr7 = high | dr7::armSlotDr7(low, nvp::kWatchSlot, dr7::kDr7RwWrite, dr7::kDr7Len4Bytes);
                    result = SetThreadContext(h, &ctx) ? kDrDone : kDrFailed;
                }
            } else if (ours) {
                ctx.Dr1 = 0;
                ctx.Dr7 = high | dr7::disarmSlotDr7(low, nvp::kWatchSlot);
                result = SetThreadContext(h, &ctx) ? kDrDone : kDrFailed;
            } else {
                result = kDrSkipped;
            }
        }
        ResumeThread(h);
    }
    CloseHandle(h);
    return result;
}

// The calling thread cannot suspend itself: a helper thread sets its slot.
struct DrJob {
    DWORD tid;
    uintptr_t address;
    bool arm;
    int result;
};
DWORD WINAPI drHelper(void* p) {
    auto* job = static_cast<DrJob*>(p);
    job->result = setThreadWatch(job->tid, job->address, job->arm);
    return 0;
}
int setThreadWatchSafe(DWORD tid, uintptr_t address, bool arm) noexcept {
    if (tid != GetCurrentThreadId()) return setThreadWatch(tid, address, arm);
    DrJob job{tid, address, arm, kDrFailed};
    HANDLE h = CreateThread(nullptr, 0, &drHelper, &job, 0, nullptr);
    if (!h) return kDrFailed;
    const bool done = WaitForSingleObject(h, 5000) == WAIT_OBJECT_0;
    CloseHandle(h);
    return done ? job.result : kDrFailed;
}

constexpr size_t kWatchThreads = 1024;
DWORD g_watchTids[kWatchThreads];
size_t g_watchTidCount = 0;
PVOID g_watchHandler = nullptr;
uint64_t g_watchArmedAtMs = 0;

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

// This module's range: the hold's own write to the source (sehSwapMode) is inside it.
void ownModuleRange(uintptr_t* lo, uintptr_t* hi) {
    *lo = *hi = 0;
    HMODULE self = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCWSTR>(&sourceHook), &self) ||
        !self)
        return;
    const auto base = reinterpret_cast<uintptr_t>(self);
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    *lo = base;
    *hi = base + nt->OptionalHeader.SizeOfImage;
}

}  // namespace

int nativeVrWriterWatchArm(uintptr_t address, uint64_t nowMs, NativeVrProbeSink sink, void* ctx) {
    char line[400];
    if (g_watchTidCount) return 0;
    if (!address) return 0;
    if (!g_watchHandler) g_watchHandler = AddVectoredExceptionHandler(1, &writerWatchHandler);
    if (!g_watchHandler) {
        nvp::formatWatchNotOn("the exception handler could not be added", line, sizeof(line));
        sink(ctx, line);
        return 0;
    }
    uintptr_t lo = 0, hi = 0;
    ownModuleRange(&lo, &hi);
    g_ownLo.store(lo, std::memory_order_relaxed);
    g_ownHi.store(hi, std::memory_order_relaxed);
    // The handler must know the address before the first thread can trap on it.
    const uintptr_t was = g_watchAddress.exchange(address, std::memory_order_acq_rel);
    if (was && was != address) g_watchPrevious.store(was, std::memory_order_release);
    g_watchActive.store(true, std::memory_order_release);
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snap == INVALID_HANDLE_VALUE) {
        nvp::formatWatchNotOn("the process's threads could not be listed", line, sizeof(line));
        sink(ctx, line);
        return 0;
    }
    uint32_t armed = 0, skipped = 0, failed = 0;
    const DWORD pid = GetCurrentProcessId();
    THREADENTRY32 te{};
    te.dwSize = sizeof(te);
    for (BOOL ok = Thread32First(snap, &te); ok; ok = Thread32Next(snap, &te)) {
        if (te.th32OwnerProcessID != pid) continue;
        if (g_watchTidCount >= kWatchThreads) {
            ++failed;
            continue;
        }
        const int r = setThreadWatchSafe(te.th32ThreadID, address, true);
        if (r == kDrDone) {
            g_watchTids[g_watchTidCount++] = te.th32ThreadID;
            ++armed;
        } else if (r == kDrSkipped) {
            ++skipped;
        } else {
            ++failed;
        }
    }
    CloseHandle(snap);
    if (!armed) {
        g_watchActive.store(false, std::memory_order_release);
        char why[160];
        std::snprintf(why, sizeof(why), "no thread took it (%u skipped, %u could not be set)", skipped, failed);
        nvp::formatWatchNotOn(why, line, sizeof(line));
        sink(ctx, line);
        return 0;
    }
    g_watchArmedAtMs = nowMs;
    nvp::formatWatchOn(address, armed, skipped, failed, line, sizeof(line));
    sink(ctx, line);
    return static_cast<int>(armed);
}

bool nativeVrWriterWatchOn() { return g_watchTidCount != 0; }

void nativeVrWriterWatchDrain(NativeVrProbeSink sink, void* ctx) {
    char line[700];
    int row = 0;
    while (g_writers.nextUnsaid(&row)) {
        if (nvp::formatWriterSite(row, g_writers.site(row), g_base.load(std::memory_order_relaxed),
                                  g_imageSize.load(std::memory_order_relaxed), line, sizeof(line)) > 0)
            sink(ctx, line);
    }
}

void nativeVrWriterWatchDisarm(uint8_t verdict, uint64_t nowMs, NativeVrProbeSink sink, void* ctx) {
    if (!g_watchTidCount) return;
    const uintptr_t address = g_watchAddress.load(std::memory_order_acquire);
    for (size_t i = 0; i < g_watchTidCount; ++i) setThreadWatchSafe(g_watchTids[i], address, false);
    g_watchTidCount = 0;
    g_watchActive.store(false, std::memory_order_release);
    nativeVrWriterWatchDrain(sink, ctx);
    char line[400];
    nvp::formatWatchOff(verdict, nowMs - g_watchArmedAtMs, g_writers, line, sizeof(line));
    sink(ctx, line);
}

void nativeVrWriterWatchStep(uint64_t nowMs, bool recording, NativeVrProbeSink sink, void* ctx) {
    const uintptr_t wanted = g_watchWanted.exchange(0, std::memory_order_acq_rel);
    if (wanted && recording && !nativeVrWriterWatchOn()) nativeVrWriterWatchArm(wanted, nowMs, sink, ctx);
    if (!nativeVrWriterWatchOn()) return;
    nativeVrWriterWatchDrain(sink, ctx);
    const uint8_t verdict = nvp::watchVerdict(nowMs, g_watchArmedAtMs, g_holding.load(std::memory_order_acquire), recording);
    if (verdict != nvp::kWatchKeep) nativeVrWriterWatchDisarm(verdict, nowMs, sink, ctx);
}

uint32_t nativeVrCinemaSwitchFind(uintptr_t base, NativeVrProbeSink sink, void* ctx) {
    nvp::SwitchFind found;
    uint32_t timestamp = 0, imageSize = 0;
    const uint64_t startMs = GetTickCount64();
    char line[400];
    if (!sehFindSwitch(base, &found, &timestamp, &imageSize)) {
        say(sink, ctx, "native-vr-on-foot: cinema-on-foot switch not searched: the game's image headers could not be read; the game "
                       "keeps its cinema on foot");
        return 0;
    }
    if (nvp::formatFind(found, timestamp, imageSize, GetTickCount64() - startMs, line, sizeof(line)) > 0) sink(ctx, line);
    return found.result == nvp::kFindFound ? found.byteRva : 0;
}

void nativeVrCinemaSwitchStep(uint8_t* address, uint32_t rva, bool key, int onFoot, NativeVrProbeSink sink, void* ctx) {
    static nvp::SwitchState state;
    static bool saidNoAddress = false;
    static int lastOnFoot = -1;
    char line[400];
    if (!address) {
        if (key && !saidNoAddress)
            sink(ctx, "native-vr-on-foot: head camera cannot work (the cinema-on-foot switch was not found, see its line); nothing "
                      "written; the game keeps its cinema");
        saidNoAddress = key;
        return;
    }
    const int value = sehReadByte(address);
    nvp::SwitchStep step = nvp::cinemaSwitchStep(state, key, value);
    if (step.write >= 0 && !sehSwapByte(address, static_cast<uint8_t>(value), static_cast<uint8_t>(step.write))) {
        // The game changed it between the read and the write: next frame decides again on what it holds then.
        if (step.kind == nvp::kSwitchSet) state.ours = false;
        if (step.kind == nvp::kSwitchAgain) --state.again;
        return;
    }
    if (step.kind != nvp::kSwitchAgain || nvp::switchAgainSaid(state.again)) {
        if (nvp::formatSwitch(step, state, value, onFoot, rva, line, sizeof(line)) > 0) sink(ctx, line);
    }
    if (state.ours && onFoot >= 0 && onFoot != lastOnFoot && lastOnFoot >= 0) {
        nvp::formatHeadCameraJournal(onFoot, line, sizeof(line));
        sink(ctx, line);
    }
    lastOnFoot = onFoot;
}

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
            point == nvp::kPointSource ? " (only experimental.native_vr_on_foot changes what it sees)" : "");
        // Process lifetime: the relay and trampoline are never freed, so a call already inside them can finish.
    }
    return armed;
}

void nativeVrProbeSetRecording(bool on) { g_recording.store(on, std::memory_order_relaxed); }
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
        // A decision or camera line only when it changed; a hold line every time, through the limiter, so repeats are counted.
        const bool changeOnly = pipeline || nvp::isCameraPoint(e.point);
        if (changeOnly ? !changes.admit(e) : !limiter.admit(e, nowMs)) continue;
        if (nvp::formatEvent(e, base, size, line, sizeof(line), onFoot) > 0) sink(ctx, line);
    }
    if (limiter.summary(nowMs, g_ring.dropped(), line, sizeof(line)) > 0) sink(ctx, line);
}

void nativeVrProbeFrameBoundary() {
    static uint32_t generation = ~0u;
    static bool on = false, said = false, tried = false;
    static bool onFootKey = false, saidOnFoot = false, saidOnFootUnarmed = false;
    static int armed = 0;
    static uint32_t switchRva = 0;  // the cinema-on-foot switch, found once at the install (0: not found)
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
        // Native VR on foot is the default: a missing key means on.
        const bool wantOnFoot = cfg.getBool("experimental.native_vr_on_foot", true);
        if (!saidOnFoot || wantOnFoot != onFootKey) {
            char line[200];
            nvp::formatOnFootKey(wantOnFoot, saidOnFoot, line, sizeof(line));
            Log::get().note("%s", line);
            saidOnFoot = true;
        }
        onFootKey = wantOnFoot;
    }
    const int onFoot = journalOnFootKnown() ? (journalOnFoot() ? 1 : 0) : -1;
    if ((on || onFootKey) && !tried) {
        tried = true;
        armed = nativeVrProbeInstall(reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr)), nullptr, true, &logSink, nullptr);
        int cameraArmed = 0;
        for (int point = 0; point < nvp::kPointCount; ++point) cameraArmed += nvp::isCameraPoint(point) && nativeVrProbeArmed(point) ? 1 : 0;
        char line[300];
        nvp::formatInstalled(armed, cameraArmed, line, sizeof(line));
        Log::get().note("%s", line);
        // The switch is data, not a hook, and found by its reader's code: any build where that code is the one match (the hooks
        // above are build 332841's only).
        switchRva = nativeVrCinemaSwitchFind(reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr)), &logSink, nullptr);
    }
    const bool sourceArmed = nativeVrProbeArmed(nvp::kPointSource);
    if (onFootKey && tried && !sourceArmed && !saidOnFootUnarmed) {
        if (switchRva)
            Log::get().note("native-vr-on-foot: the hold (build 332841 only) is not installed (see the lines above); the head camera "
                            "switch works without it");
        else
            Log::get().note("native-vr-on-foot: cannot work: the stereo mode source is not hooked and the cinema-on-foot switch was "
                            "not found (see the lines above); the game keeps its cinema");
        saidOnFootUnarmed = true;
    }
    nativeVrProbeSetOnFoot(onFootKey && sourceArmed);
    if (tried)
        nativeVrCinemaSwitchStep(switchRva ? reinterpret_cast<uint8_t*>(GetModuleHandleW(nullptr)) + switchRva : nullptr, switchRva,
                                 onFootKey, onFoot, &logSink, nullptr);
    nativeVrProbeSetRecording(on && armed > 0);
    if (armed > 0) {
        const uint64_t nowMs = GetTickCount64();
        nativeVrProbeDrain(nowMs, onFoot, &logSink, nullptr);
        nativeVrOnFootJournal(onFoot, &logSink, nullptr);
        nativeVrWriterWatchStep(nowMs, on && armed > 0, &logSink, nullptr);
    }
}

}  // namespace edvr

