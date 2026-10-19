#pragma once
// Native-VR-on-foot probe, the pure half (EDVRonFOOT; docs\..\..\docs\research\native-vr-on-foot.md in the project, and
// native_vr_probe.h for the hooks). Everything here is plain C++ the rig drives directly: the build-332841 identity, the three
// watch points and their prologue, the event the hooks record, the ring that carries events from the game's threads to the frame
// thread, the limiter that keeps a per-frame call from flooding the log, and the text of every line.
//
// OBSERVATION ONLY. Nothing here, and nothing in the hooks, changes a value the game passes or returns.
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
// All three begin push rbx; sub rsp,20h -- no relative operand, which CodeHook relocates as is.
enum : int { kPointMode = 0, kPointCinemaOn = 1, kPointCinemaOff = 2, kPointCount = 3 };
constexpr uintptr_t kPointRva[kPointCount] = {0x283AFB0u, 0x28A9690u, 0x28AB150u};
constexpr const char* kPointName[kPointCount] = {"SetIdentStereoRenderModeActivity execute", "CinemaPipelineFeature activate",
                                                 "CinemaPipelineFeature deactivate"};
constexpr size_t kPrologueBytes = 6;
constexpr uint8_t kPrologue[kPrologueBytes] = {0x40, 0x53, 0x48, 0x83, 0xEC, 0x20};

// What the game asks for and what reaches the view: the execute's own mapping, restated so the log can show both.
constexpr uint32_t appliedMode(uint32_t requested) { return requested < 3u ? requested : 1u; }

constexpr int kStackFrames = 6;

struct Event {
    uint8_t point = 0;          // kPointMode, kPointCinemaOn, kPointCinemaOff
    uint8_t readOk = 0;         // 1: the game's fields below were read; 0: a read faulted (the original still ran)
    uint8_t flagBefore = 0;     // cinema points: this+0x20 before the original
    uint8_t frames = 0;         // valid entries in stack
    uint32_t requested = 0;     // mode point: the data's mode
    uint32_t thread = 0;
    uint64_t self = 0;          // the activity (mode) or the feature (cinema)
    uint64_t other = 0;         // mode: *(this+8), the activity's owner; activate: *arg; deactivate: 0
    uint64_t stack[kStackFrames] = {};  // return addresses, the game's caller first
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
    return h;
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

// "EliteDangerous64.exe+0x283AFB0" for an address inside the image, the bare address otherwise.
inline int formatAddress(uint64_t address, uint64_t base, uint64_t imageSize, char* out, size_t cap) {
    if (base && address >= base && address - base < imageSize)
        return std::snprintf(out, cap, "EliteDangerous64.exe+0x%llX", static_cast<unsigned long long>(address - base));
    return std::snprintf(out, cap, "0x%llX", static_cast<unsigned long long>(address));
}

// The event's log line.
inline int formatEvent(const Event& e, uint64_t base, uint64_t imageSize, char* out, size_t cap) {
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
