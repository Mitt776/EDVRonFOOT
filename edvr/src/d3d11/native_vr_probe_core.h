#pragma once
// Native-VR-on-foot probe, the pure half (EDVRonFOOT; docs\..\..\docs\research\native-vr-on-foot.md in the project, and
// native_vr_probe.h for the hooks). Everything here is plain C++ the rig drives directly: the build-332841 identity, the five
// watch points and their prologues, the event the hooks record, the ring that carries events from the game's threads to the frame
// thread, the limiter that keeps a per-frame call from flooding the log, the filter that writes a pipeline decision only when it
// changes, the cinema-to-stereo mode substitution, and the text of every line.
//
// OBSERVATION ONLY, with one exception: experimental.native_vr_block_cinema makes the rebuild hook substitute the stereo mode for the
// cinema mode for the duration of one call (cinemaModeSubstitute). Nothing else changes a value the game passes or returns.
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <unordered_map>

namespace edvr {
namespace nvp {

// The PE TimeDateStamp and SizeOfImage every build-332841 hook in this tree checks (explorer_cam_core.h has the same pair).
constexpr uint32_t kExpectedTimestamp = 1788384820u;
constexpr uint32_t kExpectedImageSize = 104894464u;

// The three watch points (Ghidra, build 332841):
//   0  SetIdentStereoRenderModeActivity's execute: reads the mode its data asked for at *(*(this+0x10)+8), maps >= 3 to 1, and hands
//      it to the view's virtual +0x148.
//   1  CinemaPipelineFeature's activate (this, arg): sets this+0x20 to 1 and joins the cinema node to the render graph.
//   2  CinemaPipelineFeature's deactivate (this): clears this+0x20 and takes the node out.
//   3  the view pipeline's rebuild (P): reads the view's stereo mode m = *(int*)(*(P+0x28)+0x9D4) and builds the four sub-pipelines
//      F[i] = *(P+0x90+8i); m 5 or 6 (cinema) builds F[0] and F[1] with the cinema node and F[1] without the scene. It sets each
//      built F's +0x329 (built) and +0x330 (with cinema).
//   4  the view pipeline's teardown (P): deactivates every node of each built F and clears +0x329.
// Points 0..2 begin push rbx; sub rsp,20h; point 3 push rbp/rbx/rsi/rdi/r12; point 4 mov [rsp+8],rbx; mov [rsp+10h],rbp -- no
// relative operand, which CodeHook relocates as is. The project's docs\research\native-vr-on-foot.md has the reading.
enum : int { kPointMode = 0, kPointCinemaOn = 1, kPointCinemaOff = 2, kPointBuild = 3, kPointTeardown = 4, kPointCount = 5 };
constexpr uintptr_t kPointRva[kPointCount] = {0x283AFB0u, 0x28A9690u, 0x28AB150u, 0x2854FA0u, 0x288BF20u};
constexpr const char* kPointName[kPointCount] = {"SetIdentStereoRenderModeActivity execute", "CinemaPipelineFeature activate",
                                                 "CinemaPipelineFeature deactivate", "view pipeline rebuild", "view pipeline teardown"};
constexpr size_t kPrologueBytes = 6;
constexpr uint8_t kPrologue[kPrologueBytes] = {0x40, 0x53, 0x48, 0x83, 0xEC, 0x20};
constexpr size_t kBuildPrologueBytes = 7;
constexpr uint8_t kBuildPrologue[kBuildPrologueBytes] = {0x40, 0x55, 0x53, 0x56, 0x57, 0x41, 0x54};
constexpr size_t kTeardownPrologueBytes = 10;
constexpr uint8_t kTeardownPrologue[kTeardownPrologueBytes] = {0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C, 0x24, 0x10};

inline const uint8_t* prologueOf(int point, size_t* bytes) {
    if (point == kPointBuild) {
        *bytes = kBuildPrologueBytes;
        return kBuildPrologue;
    }
    if (point == kPointTeardown) {
        *bytes = kTeardownPrologueBytes;
        return kTeardownPrologue;
    }
    *bytes = kPrologueBytes;
    return kPrologue;
}

// The pipeline's fields (build 332841).
constexpr uintptr_t kPipelineConfig = 0x28;     // P+0x28: the view's settings
constexpr uintptr_t kConfigStereoMode = 0x9D4;  // settings+0x9D4: int, 0 mono, 3/4 headset stereo, 5/6 headset cinema
constexpr uintptr_t kPipelineSubs = 0x90;       // P+0x90..0xA8: four sub-pipeline pointers
constexpr int kSubPipelines = 4;
constexpr uintptr_t kSubBuilt = 0x329;
constexpr uintptr_t kSubCinema = 0x330;

// The headset stereo mode the block puts in place of a cinema mode for one rebuild: 5 -> 3, 6 -> 4; 0 for any mode it leaves alone.
constexpr int32_t cinemaModeSubstitute(int32_t mode) { return mode == 5 || mode == 6 ? mode - 2 : 0; }

// What the game asks for and what reaches the view: the execute's own mapping, restated so the log can show both.
constexpr uint32_t appliedMode(uint32_t requested) { return requested < 3u ? requested : 1u; }

constexpr int kStackFrames = 6;

struct Event {
    uint8_t point = 0;          // kPoint*
    uint8_t readOk = 0;         // 1: the game's fields below were read; 0: a read faulted (the original still ran)
    uint8_t flagBefore = 0;     // cinema points: this+0x20 before the original
    uint8_t frames = 0;         // valid entries in stack
    uint32_t requested = 0;     // mode point: the data's mode
    uint32_t thread = 0;
    uint64_t self = 0;          // the activity (mode) or the feature (cinema)
    uint64_t other = 0;         // mode: *(this+8), the activity's owner; activate: *arg; deactivate: 0
    uint64_t stack[kStackFrames] = {};  // return addresses, the game's caller first
    // Pipeline points (rebuild, teardown): the view's stereo mode as the game holds it, the mode the block put in its place for this
    // rebuild (0: none), and per sub-pipeline bit i: built (+0x329) and with cinema (+0x330) -- after a rebuild, before a teardown.
    int32_t mode = 0;
    int32_t substituted = 0;
    uint8_t built = 0;
    uint8_t cinema = 0;
};

// Bounded multi-producer, single-consumer ring (Vyukov's). The hooks push from whichever game thread calls them -- no lock, no
// allocation, no system call; a full ring drops the event and the drop is counted. Only the frame thread pops.
template <size_t N>
class Ring {
    static_assert(N >= 2 && (N & (N - 1)) == 0, "N is a power of two");

public:
    Ring() {
        for (size_t i = 0; i < N; ++i) m_cells[i].seq.store(i, std::memory_order_relaxed);
    }
    bool push(const Event& e) noexcept {
        size_t pos = m_enqueue.load(std::memory_order_relaxed);
        for (;;) {
            Cell& cell = m_cells[pos & (N - 1)];
            const size_t seq = cell.seq.load(std::memory_order_acquire);
            const intptr_t dif = static_cast<intptr_t>(seq) - static_cast<intptr_t>(pos);
            if (dif == 0) {
                if (m_enqueue.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed)) {
                    cell.event = e;
                    cell.seq.store(pos + 1, std::memory_order_release);
                    return true;
                }
            } else if (dif < 0) {
                m_dropped.fetch_add(1, std::memory_order_relaxed);
                return false;
            } else {
                pos = m_enqueue.load(std::memory_order_relaxed);
            }
        }
    }
    bool pop(Event* out) noexcept {
        Cell& cell = m_cells[m_dequeue & (N - 1)];
        const size_t seq = cell.seq.load(std::memory_order_acquire);
        if (static_cast<intptr_t>(seq) - static_cast<intptr_t>(m_dequeue + 1) != 0) return false;
        *out = cell.event;
        cell.seq.store(m_dequeue + N, std::memory_order_release);
        ++m_dequeue;
        return true;
    }
    uint64_t dropped() const noexcept { return m_dropped.load(std::memory_order_relaxed); }

private:
    struct Cell {
        std::atomic<size_t> seq{0};
        Event event;
    };
    Cell m_cells[N];
    std::atomic<size_t> m_enqueue{0};
    size_t m_dequeue = 0;
    std::atomic<uint64_t> m_dropped{0};
};

// The same event, for the limiter: the point, who, what and the immediate caller. Thread and deeper frames are not part of it -- a
// per-frame call from a pool of job threads is still the same event.
inline uint64_t eventKey(const Event& e) {
    uint64_t h = 1469598103934665603ull;
    auto mix = [&h](uint64_t v) {
        for (int i = 0; i < 8; ++i) {
            h ^= (v >> (8 * i)) & 0xFF;
            h *= 1099511628211ull;
        }
    };
    mix(e.point);
    mix(e.readOk);
    mix(e.flagBefore);
    mix(e.requested);
    mix(e.self);
    mix(e.other);
    mix(e.frames ? e.stack[0] : 0);
    mix(static_cast<uint32_t>(e.mode));
    mix(static_cast<uint32_t>(e.substituted));
    mix(e.built);
    mix(e.cinema);
    return h;
}

// Frame thread only. A pipeline event is written when its point and pipeline show a mode, a substitution or a result not seen there
// last time -- a rebuild that repeats itself says nothing new.
class ChangeFilter {
public:
    bool admit(const Event& e) {
        const uint64_t key = (static_cast<uint64_t>(e.point) << 56) ^ e.self;
        const uint64_t sig = (static_cast<uint64_t>(static_cast<uint32_t>(e.mode)) << 32) ^
                             (static_cast<uint64_t>(static_cast<uint32_t>(e.substituted)) << 16) ^ (static_cast<uint64_t>(e.built) << 8) ^
                             e.cinema ^ (static_cast<uint64_t>(e.readOk) << 60);
        auto it = m_last.find(key);
        if (it != m_last.end() && it->second == sig) return false;
        if (m_last.size() > 1024) m_last.clear();  // a bound, not a policy
        m_last[key] = sig;
        return true;
    }

private:
    std::unordered_map<uint64_t, uint64_t> m_last;
};

// "0,1" for the set bits of a sub-pipeline mask, "none" for none.
inline void formatMask(uint8_t mask, char* out, size_t cap) {
    size_t at = 0;
    out[0] = 0;
    for (int i = 0; i < kSubPipelines; ++i) {
        if (!(mask & (1u << i))) continue;
        const int n = std::snprintf(out + at, cap - at, "%s%d", at ? "," : "", i);
        if (n < 0 || static_cast<size_t>(n) >= cap - at) break;
        at += static_cast<size_t>(n);
    }
    if (!at) std::snprintf(out, cap, "none");
}

// Frame thread only. One line per distinct event per window, at most kBudget lines per window in all; everything else is counted and
// said in one summary line per window that had any.
class Limiter {
public:
    static constexpr uint64_t kWindowMs = 1000;
    static constexpr uint32_t kBudget = 20;

    // True: write this event's line now.
    bool admit(const Event& e, uint64_t nowMs) {
        roll(nowMs);
        const uint64_t key = eventKey(e);
        auto it = m_seen.find(key);
        const bool fresh = it == m_seen.end() || nowMs - it->second >= kWindowMs;
        if (!fresh || m_lines >= kBudget) {
            ++m_suppressed;
            if (fresh) ++m_overBudget;
            return false;
        }
        if (m_seen.size() > 4096) m_seen.clear();  // a bound, not a policy: past this every event is simply new again
        m_seen[key] = nowMs;
        ++m_lines;
        return true;
    }
    // The summary for a window that has ended (or 0 if there is nothing to say). `dropped` is the ring's running total.
    int summary(uint64_t nowMs, uint64_t dropped, char* out, size_t cap) {
        roll(nowMs);
        if (!m_doneSuppressed && dropped == m_dropReported) return 0;
        const int n = std::snprintf(out, cap,
                                    "native-vr-probe: in the last %llu ms %llu repeat(s) were not written (%llu over the %u-line budget); "
                                    "%llu event(s) lost to a full ring since launch",
                                    static_cast<unsigned long long>(m_doneSpan), static_cast<unsigned long long>(m_doneSuppressed),
                                    static_cast<unsigned long long>(m_doneOverBudget), kBudget, static_cast<unsigned long long>(dropped));
        m_doneSuppressed = m_doneOverBudget = 0;
        m_dropReported = dropped;
        return n;
    }

private:
    void roll(uint64_t nowMs) {
        if (!m_started) {
            m_started = true;
            m_windowStart = nowMs;
        }
        if (nowMs - m_windowStart < kWindowMs) return;
        m_doneSuppressed += m_suppressed;
        m_doneOverBudget += m_overBudget;
        m_doneSpan = nowMs - m_windowStart;
        m_suppressed = m_overBudget = 0;
        m_lines = 0;
        m_windowStart = nowMs;
    }
    std::unordered_map<uint64_t, uint64_t> m_seen;
    bool m_started = false;
    uint64_t m_windowStart = 0, m_doneSpan = 0;
    uint32_t m_lines = 0;
    uint64_t m_suppressed = 0, m_overBudget = 0, m_doneSuppressed = 0, m_doneOverBudget = 0, m_dropReported = 0;
};

// The line after the first install: how many of the points are hooked.
inline int formatInstalled(int armed, char* out, size_t cap) {
    return std::snprintf(out, cap, "native-vr-probe: installed on %d of %d points (build 332841)%s", armed, kPointCount,
                         armed == kPointCount ? "" : "; the lines above say why the rest were not");
}

// "EliteDangerous64.exe+0x283AFB0" for an address inside the image, the bare address otherwise.
inline int formatAddress(uint64_t address, uint64_t base, uint64_t imageSize, char* out, size_t cap) {
    if (base && address >= base && address - base < imageSize)
        return std::snprintf(out, cap, "EliteDangerous64.exe+0x%llX", static_cast<unsigned long long>(address - base));
    return std::snprintf(out, cap, "0x%llX", static_cast<unsigned long long>(address));
}

// The event's log line. `onFoot` (-1 unknown, 0, 1) is the journal's answer when the line is written; only the block line shows it.
inline int formatEvent(const Event& e, uint64_t base, uint64_t imageSize, char* out, size_t cap, int onFoot = -1) {
    char caller[64] = "(no stack)";
    char stack[6 * 40] = "";
    if (e.frames) formatAddress(e.stack[0], base, imageSize, caller, sizeof(caller));
    size_t at = 0;
    for (int i = 1; i < e.frames && i < kStackFrames; ++i) {
        char one[40];
        formatAddress(e.stack[i], base, imageSize, one, sizeof(one));
        const int n = std::snprintf(stack + at, sizeof(stack) - at, "%s%s", at ? " < " : "", one);
        if (n < 0 || static_cast<size_t>(n) >= sizeof(stack) - at) break;
        at += static_cast<size_t>(n);
    }
    if (e.point == kPointBuild || e.point == kPointTeardown) {
        if (!e.readOk)
            return std::snprintf(out, cap, "native-vr-probe: cinema decision (%s): view pipeline 0x%llX could not be read; thread %u; called from %s",
                                 e.point == kPointBuild ? "rebuild" : "teardown", static_cast<unsigned long long>(e.self), e.thread, caller);
        char built[16], cinema[16];
        formatMask(e.built, built, sizeof(built));
        formatMask(e.cinema, cinema, sizeof(cinema));
        if (e.point == kPointTeardown)
            return std::snprintf(out, cap,
                                 "native-vr-probe: cinema decision (teardown): view pipeline 0x%llX, stereo mode %d; tearing down sub-pipelines "
                                 "built %s, with cinema %s; thread %u; called from %s",
                                 static_cast<unsigned long long>(e.self), e.mode, built, cinema, e.thread, caller);
        if (e.substituted)
            return std::snprintf(out, cap,
                                 "native-vr-block-cinema: cinema blocked on a rebuild: view pipeline 0x%llX, stereo mode %d -> %d for this "
                                 "rebuild (back to %d after); result: sub-pipelines built %s, with cinema %s; on foot (journal): %s; thread %u; "
                                 "called from %s",
                                 static_cast<unsigned long long>(e.self), e.mode, e.substituted, e.mode, built, cinema,
                                 onFoot < 0 ? "unknown" : (onFoot ? "yes" : "no"), e.thread, caller);
        return std::snprintf(out, cap,
                             "native-vr-probe: cinema decision (rebuild): view pipeline 0x%llX, stereo mode %d (%s); result: sub-pipelines "
                             "built %s, with cinema %s; thread %u; called from %s",
                             static_cast<unsigned long long>(e.self), e.mode,
                             e.mode == 5 || e.mode == 6 ? "cinema" : (e.mode == 3 || e.mode == 4 ? "headset stereo" : "other"), built, cinema,
                             e.thread, caller);
    }
    if (e.point == kPointMode) {
        char mode[64];
        if (e.readOk)
            std::snprintf(mode, sizeof(mode), "requested %u -> applied %u", e.requested, appliedMode(e.requested));
        else
            std::snprintf(mode, sizeof(mode), "requested ? (the activity could not be read)");
        return std::snprintf(out, cap, "native-vr-probe: stereo mode: %s; activity 0x%llX, owner 0x%llX; thread %u; called from %s; then %s",
                             mode, static_cast<unsigned long long>(e.self), static_cast<unsigned long long>(e.other), e.thread, caller,
                             at ? stack : "(nothing deeper)");
    }
    const char* what = e.point == kPointCinemaOn ? "ON" : "OFF";
    char flag[48];
    if (e.readOk)
        std::snprintf(flag, sizeof(flag), "active flag before %u", e.flagBefore);
    else
        std::snprintf(flag, sizeof(flag), "active flag unreadable");
    return std::snprintf(out, cap, "native-vr-probe: cinema feature %s: feature 0x%llX, %s, arg 0x%llX; thread %u; called from %s; then %s", what,
                         static_cast<unsigned long long>(e.self), flag, static_cast<unsigned long long>(e.other), e.thread, caller,
                         at ? stack : "(nothing deeper)");
}

}  // namespace nvp
}  // namespace edvr
