#pragma once
// Native-VR-on-foot probe, the pure half (EDVRonFOOT; docs\..\..\docs\research\native-vr-on-foot.md in the project, and
// native_vr_probe.h for the hooks). Everything here is plain C++ the rig drives directly: the build-332841 identity, the five
// watch points and their prologues, the event the hooks record, the ring that carries events from the game's threads to the frame
// thread, the limiter that keeps a per-frame call from flooding the log, the filter that writes a pipeline decision only when it
// changes, the cinema-to-stereo mode substitution, and the text of every line.
//
// OBSERVATION ONLY, with two exceptions: experimental.native_vr_block_cinema makes the rebuild hook substitute the stereo mode for the
// cinema mode for the duration of one call (cinemaModeSubstitute), and experimental.native_vr_on_foot makes the stereo mode source hook
// put the headset stereo mode in place of a cinema mode the game set there, and keep it there (holdStep). Nothing else changes a value
// the game passes or returns.
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
//   5  the stereo mode source (this, ...), a render task once a frame: X = *(this+0x7F8) holds the mode the game wants at X+0x20; it
//      copies it into the renderer's request S+0x3698 (S = *(this+0x50)) and computes the headset eyes' projections only for a mode
//      that is not cinema. The per-frame update then makes the request the current mode S+0x3694 -- the rebuild's *(P+0x28)+0x9D4,
//      with *(P+0x28) = S+0x2CC0 -- and rebuilds the pipeline when it changed.
//   6  the default camera's name (cam, out): makes a new game string at *out, "HMD First Person Camera" on foot in a headset stereo
//      mode. Called when the camera is chosen again, not every frame.
// Points 0..2 begin push rbx; sub rsp,20h; point 3 push rbp/rbx/rsi/rdi/r12; point 4 mov [rsp+8],rbx; mov [rsp+10h],rbp; point 5
// push rbp/rbx/rsi/rdi/r14; point 6 mov [rsp+8],rbx; mov [rsp+10h],rsi -- no relative operand, which CodeHook relocates as is. The
// project's docs\research\native-vr-on-foot.md has the reading ("Читатели и писатели режима").
enum : int {
    kPointMode = 0,
    kPointCinemaOn = 1,
    kPointCinemaOff = 2,
    kPointBuild = 3,
    kPointTeardown = 4,
    kPointSource = 5,
    kPointCamera = 6,
    kPointCount = 7
};
constexpr uintptr_t kPointRva[kPointCount] = {0x283AFB0u, 0x28A9690u, 0x28AB150u, 0x2854FA0u, 0x288BF20u, 0x281AC10u, 0x2DFC7A0u};
constexpr const char* kPointName[kPointCount] = {"SetIdentStereoRenderModeActivity execute", "CinemaPipelineFeature activate",
                                                 "CinemaPipelineFeature deactivate", "view pipeline rebuild", "view pipeline teardown",
                                                 "stereo mode source", "camera choice"};
constexpr size_t kPrologueBytes = 6;
constexpr uint8_t kPrologue[kPrologueBytes] = {0x40, 0x53, 0x48, 0x83, 0xEC, 0x20};
constexpr size_t kBuildPrologueBytes = 7;
constexpr uint8_t kBuildPrologue[kBuildPrologueBytes] = {0x40, 0x55, 0x53, 0x56, 0x57, 0x41, 0x54};
constexpr size_t kTeardownPrologueBytes = 10;
constexpr uint8_t kTeardownPrologue[kTeardownPrologueBytes] = {0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C, 0x24, 0x10};
constexpr size_t kSourcePrologueBytes = 7;
constexpr uint8_t kSourcePrologue[kSourcePrologueBytes] = {0x40, 0x55, 0x53, 0x56, 0x57, 0x41, 0x56};
constexpr size_t kCameraPrologueBytes = 10;
constexpr uint8_t kCameraPrologue[kCameraPrologueBytes] = {0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10};

inline const uint8_t* prologueOf(int point, size_t* bytes) {
    if (point == kPointBuild) {
        *bytes = kBuildPrologueBytes;
        return kBuildPrologue;
    }
    if (point == kPointTeardown) {
        *bytes = kTeardownPrologueBytes;
        return kTeardownPrologue;
    }
    if (point == kPointSource) {
        *bytes = kSourcePrologueBytes;
        return kSourcePrologue;
    }
    if (point == kPointCamera) {
        *bytes = kCameraPrologueBytes;
        return kCameraPrologue;
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

// The stereo mode source's fields (build 332841).
constexpr uintptr_t kSourceObject = 0x7F8;        // this+0x7F8: X, the object that holds the mode the game wants
constexpr uintptr_t kSourceStereoMode = 0x20;     // X+0x20: int, the same values as the pipeline's mode
constexpr uintptr_t kSourceRenderer = 0x50;       // this+0x50: S, the renderer (request +0x3698, current +0x3694)
constexpr uintptr_t kRendererViewSettings = 0x2CC0;  // S+0x2CC0: what the rebuild's *(P+0x28) should be

// The camera name string the game makes (refcounted): length at +0, the characters from +0x14.
constexpr uintptr_t kNameChars = 0x14;
constexpr size_t kNameBytes = 48;  // with the terminator: longer names are cut

// The headset stereo mode the block puts in place of a cinema mode for one rebuild: 5 -> 3, 6 -> 4; 0 for any mode it leaves alone.
constexpr int32_t cinemaModeSubstitute(int32_t mode) { return mode == 5 || mode == 6 ? mode - 2 : 0; }

// experimental.native_vr_on_foot, what the source hook does with the mode it finds at X+0x20. A cinema mode with the key on is
// replaced by the headset stereo mode (the same substitute as the block) and the hold starts, or, when one is running, the game has
// set cinema again over ours: a repeat. A mode that is not what the hold put there ends the hold -- the game set a mode of its own.
// The game's own 3 on boarding cannot be told from ours by value; the frame thread ends that hold by the journal.
// The hold NEVER writes a cinema mode: it writes only a substitute, and only over a cinema mode.
enum : uint8_t { kHoldNone = 0, kHoldStart = 1, kHoldAgain = 2, kHoldEnd = 3 };
struct HoldState {
    bool holding = false;
    int32_t value = 0;  // what the hold put at X+0x20
};
struct HoldStep {
    int32_t write = 0;  // the value to write over the game's (0: none)
    uint8_t kind = kHoldNone;
};
inline HoldStep holdStep(HoldState& s, bool key, int32_t mode) {
    HoldStep r;
    const int32_t stereo = cinemaModeSubstitute(mode);
    if (key && stereo) {
        r.write = stereo;
        r.kind = s.holding ? kHoldAgain : kHoldStart;
        s.holding = true;
        s.value = stereo;
    } else if (s.holding && mode != s.value) {
        r.kind = kHoldEnd;
        s.holding = false;
    }
    return r;
}

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
    // Source point: kHold* (kHoldNone: the probe's line for a mode the game changed); `self` this, `other` X, `aux` S (a repeat: the
    // repeats since the hold started -- kept out of eventKey, so repeats stay one event for the limiter). Rebuild and teardown: `other`
    // the view settings *(P+0x28). Camera: `self` the camera, `name` its name, `mode` the source's mode at the time, `substituted` the
    // hold's value when one is running.
    uint8_t kind = kHoldNone;
    uint64_t aux = 0;
    char name[kNameBytes] = {};
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
    mix(e.kind);
    return h;
}

inline uint64_t nameHash(const Event& e) {
    uint64_t h = 1469598103934665603ull;
    for (size_t i = 0; i < kNameBytes && e.name[i]; ++i) {
        h ^= static_cast<uint8_t>(e.name[i]);
        h *= 1099511628211ull;
    }
    return h;
}

// Frame thread only. A pipeline event is written when its point and pipeline show a mode, a substitution or a result not seen there
// last time -- a rebuild that repeats itself says nothing new. The camera is one: its line when the name or the mode changed.
class ChangeFilter {
public:
    bool admit(const Event& e) {
        const uint64_t key = (static_cast<uint64_t>(e.point) << 56) ^ (e.point == kPointCamera ? 0 : e.self);
        const uint64_t sig = (static_cast<uint64_t>(static_cast<uint32_t>(e.mode)) << 32) ^
                             (static_cast<uint64_t>(static_cast<uint32_t>(e.substituted)) << 16) ^ (static_cast<uint64_t>(e.built) << 8) ^
                             e.cinema ^ (static_cast<uint64_t>(e.readOk) << 60) ^ nameHash(e);
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

// The line after the first install: how many of the points are hooked, and whether the camera's is one of them.
inline int formatInstalled(int armed, bool cameraArmed, char* out, size_t cap) {
    return std::snprintf(out, cap, "native-vr-probe: installed on %d of %d points (build 332841)%s%s", armed, kPointCount,
                         armed == kPointCount ? "" : "; the lines above say why the rest were not",
                         cameraArmed ? "" : "; camera: skipped (its point is not hooked, see above): no camera lines this session");
}

inline const char* onFootText(int onFoot) { return onFoot < 0 ? "unknown" : (onFoot ? "yes" : "no"); }

// experimental.native_vr_on_foot's line when it is first read and whenever it changes (`again`: it was said before).
inline int formatOnFootKey(bool on, bool again, char* out, size_t cap) {
    return std::snprintf(out, cap, "native-vr-on-foot: %s (experimental.native_vr_on_foot = %d)",
                         on ? "on: a cinema mode the game sets is replaced by headset stereo and held there"
                            : (again ? "off: no new hold starts; the next disembark after boarding is the game's cinema again" : "off"),
                         on ? 1 : 0);
}

// "EliteDangerous64.exe+0x283AFB0" for an address inside the image, the bare address otherwise.
inline int formatAddress(uint64_t address, uint64_t base, uint64_t imageSize, char* out, size_t cap) {
    if (base && address >= base && address - base < imageSize)
        return std::snprintf(out, cap, "EliteDangerous64.exe+0x%llX", static_cast<unsigned long long>(address - base));
    return std::snprintf(out, cap, "0x%llX", static_cast<unsigned long long>(address));
}

// The event's log line. `onFoot` (-1 unknown, 0, 1) is the journal's answer when the line is written; the block's and the hold's lines
// show it.
inline int formatEvent(const Event& e, uint64_t base, uint64_t imageSize, char* out, size_t cap, int onFoot = -1) {
    if (e.point == kPointSource) {
        const unsigned long long self = e.self, x = e.other;
        if (!e.readOk)
            return std::snprintf(out, cap, "native-vr-probe: stereo mode source 0x%llX could not be read; thread %u", self, e.thread);
        const unsigned long long settings = e.aux ? e.aux + kRendererViewSettings : 0;
        switch (e.kind) {
            case kHoldStart:
                return std::snprintf(out, cap,
                                     "native-vr-on-foot: hold start: the game set stereo mode %d (cinema), put back to %d (headset stereo) and "
                                     "held there; on foot (journal): %s; source object 0x%llX, renderer 0x%llX (its view settings 0x%llX "
                                     "should be the rebuild's); thread %u",
                                     e.mode, e.substituted, onFootText(onFoot), x, static_cast<unsigned long long>(e.aux), settings, e.thread);
            case kHoldAgain:
                return std::snprintf(out, cap,
                                     "native-vr-on-foot: hold again: the game set stereo mode %d over ours, put back to %d (%llu time(s) "
                                     "since the hold started); on foot (journal): %s",
                                     e.mode, e.substituted, static_cast<unsigned long long>(e.aux), onFootText(onFoot));
            case kHoldEnd:
                return std::snprintf(out, cap,
                                     "native-vr-on-foot: hold end: game set mode %d itself; the hold stops and the mode is the game's; "
                                     "on foot (journal): %s",
                                     e.mode, onFootText(onFoot));
            default:
                return std::snprintf(out, cap,
                                     "native-vr-probe: stereo mode source: the game wants stereo mode %d (%s); source object 0x%llX, "
                                     "renderer 0x%llX, its view settings 0x%llX; thread %u",
                                     e.mode,
                                     e.mode == 5 || e.mode == 6 ? "cinema" : (e.mode == 3 || e.mode == 4 ? "headset stereo" : "other"), x,
                                     static_cast<unsigned long long>(e.aux), settings, e.thread);
        }
    }
    if (e.point == kPointCamera) {
        char held[48] = "";
        if (e.substituted) std::snprintf(held, sizeof(held), ", held by native_vr_on_foot");
        if (!e.readOk)
            return std::snprintf(out, cap, "native-vr-probe: camera: the name could not be read, at stereo mode %d%s; thread %u", e.mode,
                                 held, e.thread);
        return std::snprintf(out, cap, "native-vr-probe: camera \"%s\" at stereo mode %d%s; thread %u", e.name, e.mode, held, e.thread);
    }
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
                                 "built %s, with cinema %s; thread %u; called from %s; view settings 0x%llX",
                                 static_cast<unsigned long long>(e.self), e.mode, built, cinema, e.thread, caller,
                                 static_cast<unsigned long long>(e.other));
        if (e.substituted)
            return std::snprintf(out, cap,
                                 "native-vr-block-cinema: cinema blocked on a rebuild: view pipeline 0x%llX, stereo mode %d -> %d for this "
                                 "rebuild (back to %d after); result: sub-pipelines built %s, with cinema %s; on foot (journal): %s; thread %u; "
                                 "called from %s",
                                 static_cast<unsigned long long>(e.self), e.mode, e.substituted, e.mode, built, cinema, onFootText(onFoot),
                                 e.thread, caller);
        return std::snprintf(out, cap,
                             "native-vr-probe: cinema decision (rebuild): view pipeline 0x%llX, stereo mode %d (%s); result: sub-pipelines "
                             "built %s, with cinema %s; thread %u; called from %s; view settings 0x%llX",
                             static_cast<unsigned long long>(e.self), e.mode,
                             e.mode == 5 || e.mode == 6 ? "cinema" : (e.mode == 3 || e.mode == 4 ? "headset stereo" : "other"), built, cinema,
                             e.thread, caller, static_cast<unsigned long long>(e.other));
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
