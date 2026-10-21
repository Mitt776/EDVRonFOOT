#pragma once
// Native-VR-on-foot probe, the pure half (EDVRonFOOT; docs\..\..\docs\research\native-vr-on-foot.md in the project, and
// native_vr_probe.h for the hooks). Everything here is plain C++ the rig drives directly: the build-332841 identity, the five
// watch points and their prologues, the event the hooks record, the ring that carries events from the game's threads to the frame
// thread, the limiter that keeps a per-frame call from flooding the log, the filter that writes a pipeline decision only when it
// changes, the cinema-to-stereo mode substitution, and the text of every line.
//
// OBSERVATION ONLY, with one exception: experimental.native_vr_on_foot (on by default) turns the game's own cinema-on-foot switch off
// while it is on (cinemaSwitchStep), and makes the stereo mode source hook put the headset stereo mode in place of a cinema mode the
// game still sets there, and keep it there (holdStep). Nothing else changes a value the game passes or returns.
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
//      mode. Camera Suite's reset to the default camera only (docs: "Выбор камеры на ногах"): not called in normal play.
//   7  the camera director's "switch to the camera by name" (vtable slot 6; this, const char* name, bool flag): when the commander's
//      entity has an object named "HMD" and a camera "HMD " + name is registered, it switches to that one -- the headset cameras that
//      follow the head -- otherwise to the plain name. The name is a C string, read BEFORE the original runs.
//   8  the director's first-person default (slot 7; this): asks "humanoid?" and calls slot 6 with "First Person Camera" or
//      "Cockpit Cam".
//   9  the director's other switch by name (slot 8; this, const char* name, bool ...): the same "HMD " choice on another path.
// Points 0..2 begin push rbx; sub rsp,20h; point 3 push rbp/rbx/rsi/rdi/r12; point 4 mov [rsp+8],rbx; mov [rsp+10h],rbp; point 5
// push rbp/rbx/rsi/rdi/r14; point 6 mov [rsp+8],rbx; mov [rsp+10h],rsi; point 7 mov [rsp+18h],rbx; push rbp; push rsi; point 8
// mov [rsp+10h],rbx; push rdi; point 9 mov [rsp+18h],r8b; push rbp; push rbx -- no relative operand, which CodeHook relocates as is.
// The project's docs\research\native-vr-on-foot.md has the reading ("Читатели и писатели режима", "Выбор камеры на ногах").
enum : int {
    kPointMode = 0,
    kPointCinemaOn = 1,
    kPointCinemaOff = 2,
    kPointBuild = 3,
    kPointTeardown = 4,
    kPointSource = 5,
    kPointCamera = 6,
    kPointCameraByName = 7,
    kPointCameraDefault = 8,
    kPointCameraByName2 = 9,
    kPointCount = 10
};
constexpr uintptr_t kPointRva[kPointCount] = {0x283AFB0u, 0x28A9690u, 0x28AB150u, 0x2854FA0u, 0x288BF20u,
                                              0x281AC10u, 0x2DFC7A0u, 0x1D79660u, 0x1D79980u, 0x1D203B0u};
constexpr const char* kPointName[kPointCount] = {"SetIdentStereoRenderModeActivity execute", "CinemaPipelineFeature activate",
                                                 "CinemaPipelineFeature deactivate", "view pipeline rebuild", "view pipeline teardown",
                                                 "stereo mode source", "camera choice", "camera by name", "first-person camera default",
                                                 "camera by name (second path)"};
constexpr bool isCameraPoint(int point) {
    return point == kPointCamera || point == kPointCameraByName || point == kPointCameraDefault || point == kPointCameraByName2;
}
constexpr int kCameraPoints = 4;
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
constexpr size_t kByNamePrologueBytes = 7;
constexpr uint8_t kByNamePrologue[kByNamePrologueBytes] = {0x48, 0x89, 0x5C, 0x24, 0x18, 0x55, 0x56};
constexpr size_t kDefaultPrologueBytes = 6;
constexpr uint8_t kDefaultPrologue[kDefaultPrologueBytes] = {0x48, 0x89, 0x5C, 0x24, 0x10, 0x57};
constexpr size_t kByName2PrologueBytes = 7;
constexpr uint8_t kByName2Prologue[kByName2PrologueBytes] = {0x44, 0x88, 0x44, 0x24, 0x18, 0x55, 0x53};

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
    if (point == kPointCameraByName) {
        *bytes = kByNamePrologueBytes;
        return kByNamePrologue;
    }
    if (point == kPointCameraDefault) {
        *bytes = kDefaultPrologueBytes;
        return kDefaultPrologue;
    }
    if (point == kPointCameraByName2) {
        *bytes = kByName2PrologueBytes;
        return kByName2Prologue;
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

// The headset stereo mode the hold puts in place of a cinema mode: 5 -> 3, 6 -> 4; 0 for any mode it leaves alone.
constexpr int32_t cinemaModeSubstitute(int32_t mode) { return mode == 5 || mode == 6 ? mode - 2 : 0; }

// experimental.native_vr_on_foot, what the source hook does with the mode it finds at X+0x20. A cinema mode with the key on is
// replaced by the headset stereo mode (cinemaModeSubstitute) and the hold starts, or, when one is running, the game has
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
    // Pipeline points (rebuild, teardown): the view's stereo mode as the game holds it, and per sub-pipeline bit i: built (+0x329) and with cinema (+0x330) -- after a rebuild, before a teardown.
    int32_t mode = 0;
    int32_t substituted = 0;
    uint8_t built = 0;
    uint8_t cinema = 0;
    // Source point: kHold* (kHoldNone: the probe's line for a mode the game changed); `self` this, `other` X, `aux` S (a repeat: the
    // repeats since the hold started -- kept out of eventKey, so repeats stay one event for the limiter). Rebuild and teardown: `other`
    // the view settings *(P+0x28). Camera: `self` the camera, `name` its name, `mode` the source's mode at the time, `substituted` the
    // hold's value when one is running. Camera by name (7, 9): `name` the name asked for (read before the original), `flagBefore` the
    // flag argument's low byte, stack[0] the caller. First-person default (8): `mode`/`substituted` as the camera's, stack[0] the caller.
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

// Frame thread only. A pipeline event is written when its point and pipeline show a mode or a result not seen there
// last time -- a rebuild that repeats itself says nothing new. The camera is one: its line when the name or the mode changed.
class ChangeFilter {
public:
    bool admit(const Event& e) {
        const uint64_t key = (static_cast<uint64_t>(e.point) << 56) ^ (isCameraPoint(e.point) ? 0 : e.self);
        const uint64_t sig = (static_cast<uint64_t>(static_cast<uint32_t>(e.mode)) << 32) ^
                             (static_cast<uint64_t>(static_cast<uint32_t>(e.substituted)) << 16) ^ (static_cast<uint64_t>(e.built) << 8) ^
                             e.cinema ^ (static_cast<uint64_t>(e.readOk) << 60) ^ nameHash(e) ^ (static_cast<uint64_t>(e.flagBefore) << 40);
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

// The line after the first install: how many of the points are hooked, and how many of the camera's (kCameraPoints).
inline int formatInstalled(int armed, int cameraArmed, char* out, size_t cap) {
    char camera[160] = "";
    if (cameraArmed <= 0)
        std::snprintf(camera, sizeof(camera), "; camera: skipped (no camera point is hooked, see above): no camera lines this session");
    else if (cameraArmed < kCameraPoints)
        std::snprintf(camera, sizeof(camera), "; camera: %d of %d camera points hooked, the lines above say which are skipped", cameraArmed,
                      kCameraPoints);
    return std::snprintf(out, cap, "native-vr-probe: installed on %d of %d points (build 332841)%s%s", armed, kPointCount,
                         armed == kPointCount ? "" : "; the lines above say why the rest were not", camera);
}

// ---- the writer watch (native_vr_probe.cpp, experimental.native_vr_probe) ---------------------------------------------------------
// On foot the game writes cinema into the stereo mode source X+0x20 about 40 times a second, and statics could not find who. At a
// hold's start the frame thread puts a hardware write watch on that address -- debug register slot 1 (slot 0 is the flat camera
// producer probe's), write, 4 bytes -- on every thread of the process. Its exception handler records each distinct instruction that
// wrote there once (the address just after the write, as the CPU reports a data breakpoint, and the callers), and counts the rest.
// The watch reads nothing it does not own and changes no value: a data breakpoint fires AFTER the write is done.
constexpr unsigned kWatchSlot = 1;
constexpr uint64_t kWatchMs = 30000;  // the longest a watch stays on
constexpr int kWriterSites = 8;

// Lock-free: the handler runs on whichever thread wrote. A new site claims a free row with a compare-and-swap, captures the callers
// and only then sets `ready`, which the frame thread waits for before it writes the line. A full table counts the overflow.
struct WriterSite {
    std::atomic<uint64_t> rip{0};
    std::atomic<uint64_t> hits{0};
    std::atomic<uint8_t> ready{0};
    uint8_t frames = 0;
    uint32_t thread = 0;
    uint64_t stack[kStackFrames] = {};
};
class WriterTable {
public:
    // The row of `rip` (a new one: *fresh = true, and the caller must publish it), or -1 when the table is full.
    int record(uint64_t rip, bool* fresh) noexcept {
        *fresh = false;
        for (int i = 0; i < kWriterSites; ++i) {
            uint64_t seen = m_sites[i].rip.load(std::memory_order_acquire);
            if (seen == 0) {
                uint64_t expected = 0;
                if (m_sites[i].rip.compare_exchange_strong(expected, rip, std::memory_order_acq_rel)) {
                    m_sites[i].hits.fetch_add(1, std::memory_order_relaxed);
                    *fresh = true;
                    return i;
                }
                seen = expected;
            }
            if (seen == rip) {
                m_sites[i].hits.fetch_add(1, std::memory_order_relaxed);
                return i;
            }
        }
        m_overflow.fetch_add(1, std::memory_order_relaxed);
        return -1;
    }
    void publish(int row, uint32_t thread, const uint64_t* stack, int frames) noexcept {
        WriterSite& s = m_sites[row];
        s.thread = thread;
        s.frames = static_cast<uint8_t>(frames < 0 ? 0 : (frames > kStackFrames ? kStackFrames : frames));
        for (int i = 0; i < s.frames; ++i) s.stack[i] = stack[i];
        s.ready.store(1, std::memory_order_release);
    }
    void countOwn() noexcept { m_own.fetch_add(1, std::memory_order_relaxed); }
    // Frame thread: rows published since the last call, each once (`said` is the frame thread's own record).
    bool nextUnsaid(int* row) noexcept {
        for (int i = 0; i < kWriterSites; ++i) {
            if (m_said[i] || !m_sites[i].ready.load(std::memory_order_acquire)) continue;
            m_said[i] = true;
            *row = i;
            return true;
        }
        return false;
    }
    const WriterSite& site(int row) const noexcept { return m_sites[row]; }
    int sites() const noexcept {
        int n = 0;
        for (const WriterSite& s : m_sites) n += s.rip.load(std::memory_order_relaxed) ? 1 : 0;
        return n;
    }
    uint64_t gameHits() const noexcept {
        uint64_t n = 0;
        for (const WriterSite& s : m_sites) n += s.hits.load(std::memory_order_relaxed);
        return n + m_overflow.load(std::memory_order_relaxed);
    }
    uint64_t ownHits() const noexcept { return m_own.load(std::memory_order_relaxed); }
    uint64_t overflow() const noexcept { return m_overflow.load(std::memory_order_relaxed); }

private:
    WriterSite m_sites[kWriterSites];
    std::atomic<uint64_t> m_own{0}, m_overflow{0};
    bool m_said[kWriterSites] = {};
};

// When the frame thread takes a watch off: the hold ended, the probe was turned off, or the watch has been on kWatchMs.
enum : uint8_t { kWatchKeep = 0, kWatchHoldEnd = 1, kWatchProbeOff = 2, kWatchTimeout = 3 };
inline uint8_t watchVerdict(uint64_t nowMs, uint64_t armedAtMs, bool holding, bool recording) {
    if (!recording) return kWatchProbeOff;
    if (!holding) return kWatchHoldEnd;
    if (nowMs - armedAtMs >= kWatchMs) return kWatchTimeout;
    return kWatchKeep;
}
inline const char* watchVerdictText(uint8_t v) {
    switch (v) {
        case kWatchHoldEnd: return "the hold ended";
        case kWatchProbeOff: return "the probe was turned off";
        case kWatchTimeout: return "30 s passed";
        default: return "still on";
    }
}

inline int formatWatchOn(uint64_t address, uint32_t armed, uint32_t skipped, uint32_t failed, char* out, size_t cap) {
    return std::snprintf(out, cap,
                         "native-vr-probe: writer watch on: stereo mode source 0x%llX (debug register %u, write, 4 bytes) on %u thread(s); "
                         "%u skipped (that register already in use there), %u could not be set; it comes off when the hold ends, the "
                         "probe goes off, or after %llu s",
                         static_cast<unsigned long long>(address), kWatchSlot, armed, skipped, failed,
                         static_cast<unsigned long long>(kWatchMs / 1000));
}
inline int formatWatchNotOn(const char* why, char* out, size_t cap) {
    return std::snprintf(out, cap, "native-vr-probe: writer watch NOT on: %s; the probe and the game go on as before", why);
}
inline int formatWatchOff(uint8_t verdict, uint64_t ms, const WriterTable& t, char* out, size_t cap) {
    return std::snprintf(out, cap,
                         "native-vr-probe: writer watch off (%s) after %llu ms: %d writing site(s) of the game, %llu write(s) by the game "
                         "(%llu past the %d-site table), %llu by EDVR's own hold",
                         watchVerdictText(verdict), static_cast<unsigned long long>(ms), t.sites(),
                         static_cast<unsigned long long>(t.gameHits()), static_cast<unsigned long long>(t.overflow()), kWriterSites,
                         static_cast<unsigned long long>(t.ownHits()));
}

// "EliteDangerous64.exe+0x283AFB0" for an address inside the image, the bare address otherwise.
inline int formatAddress(uint64_t address, uint64_t base, uint64_t imageSize, char* out, size_t cap);

// One writing site, once: the address the CPU reported (just AFTER the writing instruction) and the callers.
inline int formatWriterSite(int row, const WriterSite& s, uint64_t base, uint64_t imageSize, char* out, size_t cap) {
    char rip[64], stack[kStackFrames * 40] = "";
    formatAddress(s.rip.load(std::memory_order_relaxed), base, imageSize, rip, sizeof(rip));
    size_t at = 0;
    for (int i = 0; i < s.frames; ++i) {
        char one[40];
        formatAddress(s.stack[i], base, imageSize, one, sizeof(one));
        const int n = std::snprintf(stack + at, sizeof(stack) - at, "%s%s", at ? " < " : "", one);
        if (n < 0 || static_cast<size_t>(n) >= sizeof(stack) - at) break;
        at += static_cast<size_t>(n);
    }
    return std::snprintf(out, cap,
                         "native-vr-probe: cinema writer #%d: the game wrote the stereo mode source at %s (the instruction just before "
                         "this address); thread %u; called from %s",
                         row + 1, rip, s.thread, at ? stack : "(no stack)");
}

inline const char* onFootText(int onFoot) { return onFoot < 0 ? "unknown" : (onFoot ? "yes" : "no"); }

// experimental.native_vr_on_foot's line when it is first read and whenever it changes (`again`: it was said before).
inline int formatOnFootKey(bool on, bool again, char* out, size_t cap) {
    return std::snprintf(out, cap, "native-vr-on-foot: %s (experimental.native_vr_on_foot = %d)",
                         on ? "on: on foot the game keeps headset stereo (its cinema-on-foot switch off); a cinema mode it still sets is "
                              "replaced by headset stereo and held there"
                            : (again ? "off: no new hold starts; the next disembark after boarding is the game's cinema again" : "off"),
                         on ? 1 : 0);
}

// experimental.native_vr_on_foot's head camera: the game's own cinema-on-foot switch, a byte in its render settings block. The
// per-frame update (0x28873C0 in build 332841, 0x2889A00 in 334473) turns headset stereo (3/4) into cinema (5/6) only while this
// byte is not 0 AND the view says first person on foot; that test is the byte's one reader (0x2887AF9 / 0x288A139), and in the ship
// the second condition is never met, so the byte changes nothing there. With the byte 0 the game itself keeps headset stereo on foot
// -- the source, +0xC8 and the camera all at once, as in the ship -- where the hold put back only the source. The exe's value is 1;
// the switch writes only 1 -> 0 (key on) and 0 -> 1 (key off, and only a 0 it wrote), and nothing over any other value.
constexpr uint8_t kCinemaSwitchGame = 1;

// The byte is found by its reader's code, not by a table of builds (the address moved 0x5E82B3E -> 0x5E85B8A between 332841 and
// 334473; the code around the reader did not). The pattern is the view's question and then the test of the byte:
//   FF 9r B0 00 00 00       call [reg+0xB0]                 (the view: first person on foot?)
//   4x 38 mr <disp32>       cmp byte ptr [rip+disp], r8b..r15b (mr: mod 00, rm 101)
//   74 ??  84 C0  75        je / test al, al / jne
// The registers are free (a rebuild picks others easily); the shape is not. The byte's RVA is the end of the cmp plus disp32. It is
// taken only when the pattern is in the game's executable code exactly once and the byte lies inside a writable section; the value
// test is cinemaSwitchStep's own (1 is written to 0, 0 is the game's own, anything else is refused).
constexpr size_t kSwitchPatternBytes = 18;
constexpr uint32_t kSwitchCmpOffset = 6;  // the cmp's first byte, from the pattern's first
constexpr uint32_t kSwitchCmpBytes = 7;
inline bool switchPatternAt(const uint8_t* p) {
    return p[0] == 0xFF && (p[1] & 0xF8) == 0x90 && p[1] != 0x94 && p[2] == 0xB0 && p[3] == 0 && p[4] == 0 && p[5] == 0 &&
           (p[6] == 0x44 || p[6] == 0x45) && p[7] == 0x38 && (p[8] & 0xC7) == 0x05 && p[13] == 0x74 && p[15] == 0x84 &&
           p[16] == 0xC0 && p[17] == 0x75;
}
struct ImageSection {
    uint32_t rva = 0, size = 0;  // the section in the loaded image
    bool execute = false, write = false;
};
enum : uint8_t {
    kFindFound = 0,
    kFindNone = 1,         // no match in the executable sections
    kFindMany = 2,         // more than one
    kFindOutside = 3,      // the one match points outside every section
    kFindNotWritable = 4,  // ... or into a section that cannot be written
};
struct SwitchFind {
    uint8_t result = kFindNone;
    uint32_t matches = 0;
    uint32_t readerRva = 0;  // the cmp's RVA (the first match's)
    uint32_t byteRva = 0;
};
// A reader's shape: its length, its first byte (the scan's anchor), the test, and where in it the RIP-relative read is.
struct PatternShape {
    size_t bytes;
    uint8_t first;
    bool (*at)(const uint8_t*);
    uint32_t readerOffset;  // the reading instruction's first byte, from the pattern's first
    uint32_t dispOffset;    // its disp32, from the pattern's first
    uint32_t readerBytes;   // its length: the target is its end plus disp32
};
constexpr PatternShape kCinemaShape = {kSwitchPatternBytes, 0xFF, &switchPatternAt, kSwitchCmpOffset, kSwitchCmpOffset + 3, kSwitchCmpBytes};

// `image` is the loaded image (or the rig's copy of one); only the executable sections are read.
inline SwitchFind findPatternTarget(const uint8_t* image, const ImageSection* sections, int count, const PatternShape& shape) {
    SwitchFind f;
    for (int i = 0; i < count; ++i) {
        const ImageSection& s = sections[i];
        if (!s.execute || s.size < shape.bytes) continue;
        const uint8_t* p = image + s.rva;
        const uint8_t* end = p + s.size - shape.bytes + 1;
        while ((p = static_cast<const uint8_t*>(std::memchr(p, shape.first, static_cast<size_t>(end - p)))) != nullptr) {
            if (shape.at(p)) {
                if (++f.matches == 1) {
                    const uint32_t at = s.rva + static_cast<uint32_t>(p - (image + s.rva));
                    int32_t disp;
                    std::memcpy(&disp, p + shape.dispOffset, sizeof(disp));
                    f.readerRva = at + shape.readerOffset;
                    f.byteRva = static_cast<uint32_t>(static_cast<int64_t>(f.readerRva) + shape.readerBytes + disp);
                }
            }
            if (++p >= end) break;
        }
    }
    if (f.matches == 0) return f;
    if (f.matches > 1) {
        f.result = kFindMany;
        return f;
    }
    f.result = kFindOutside;
    for (int i = 0; i < count; ++i) {
        const ImageSection& s = sections[i];
        if (f.byteRva >= s.rva && f.byteRva - s.rva < s.size) {
            f.result = s.write ? kFindFound : kFindNotWritable;
            break;
        }
    }
    return f;
}
inline SwitchFind findCinemaSwitch(const uint8_t* image, const ImageSection* sections, int count) {
    return findPatternTarget(image, sections, count, kCinemaShape);
}

// experimental.native_vr_on_foot_game_hud: the game's own on-foot HUD in headset stereo. The per-frame update (0x2889A00 in 334473)
// turns the FirstPersonPostGUI pass -- the node that lays the HUD's sheets 36-39 into the picture -- on or off every frame by an int
// of its render settings (0x5E85B94, next to the cinema byte): -1 ("auto") means on while a float of the view (+0x16C) is > 0, which
// on foot in headset stereo it is not, so the node goes off at the disembark (flight .13); anything else means on (!= 0). With 1 the
// game keeps its HUD on itself. The switch writes only -1 -> 1 (key on) and 1 -> -1 (key off, and only a 1 it wrote).
constexpr int32_t kHudSwitchGame = -1;
constexpr int32_t kHudSwitchOurs = 1;
// The int's one reader (0x288A055 in 334473), found by its code like the cinema byte:
//   8B 05 <disp32>                  mov e?x, [rip+disp]          (the int)
//   0F 57 xx                        xorps xmm, xmm
//   4x 8B mr 80 11 00 00            mov r64, [r64+0x1180]        (the view)
//   4x 3B mr                        cmp e?x, r32                 (-1 in a register)
//   75 ??                           jne
//   48 8B 4r 18                     mov r64, [r64+0x18]
//   F3 0F 10 mr 6C 01 00 00         movss xmm, [r64+0x16C]       (the float)
// Registers free, shape fixed. The int's RVA is the end of the first mov plus disp32.
constexpr size_t kHudPatternBytes = 33;
inline bool hudPatternAt(const uint8_t* p) {
    const auto disp32Mem = [](uint8_t mr) { return (mr & 0xC0) == 0x80 && (mr & 7) != 4; };  // [r64+disp32], no SIB
    return p[0] == 0x8B && (p[1] & 0xC7) == 0x05 &&                                            // mov r32, [rip+disp32]
           p[6] == 0x0F && p[7] == 0x57 && (p[8] & 0xC0) == 0xC0 &&                             // xorps xmm, xmm
           (p[9] & 0xF8) == 0x48 && p[10] == 0x8B && disp32Mem(p[11]) && p[12] == 0x80 && p[13] == 0x11 && p[14] == 0 &&
           p[15] == 0 &&                                                                         // mov r64, [r64+0x1180]
           (p[16] & 0xF0) == 0x40 && p[17] == 0x3B && (p[18] & 0xC0) == 0xC0 &&                  // cmp r32, r32
           p[19] == 0x75 &&                                                                      // jne
           (p[21] & 0xF8) == 0x48 && p[22] == 0x8B && (p[23] & 0xC0) == 0x40 && (p[23] & 7) != 4 && p[24] == 0x18 &&  // [r64+0x18]
           p[25] == 0xF3 && p[26] == 0x0F && p[27] == 0x10 && disp32Mem(p[28]) && p[29] == 0x6C && p[30] == 0x01 && p[31] == 0 &&
           p[32] == 0;                                                                           // movss xmm, [r64+0x16C]
}
constexpr PatternShape kHudShape = {kHudPatternBytes, 0x8B, &hudPatternAt, 0, 2, 6};
inline SwitchFind findHudSwitch(const uint8_t* image, const ImageSection* sections, int count) {
    return findPatternTarget(image, sections, count, kHudShape);
}
inline int formatHudFind(const SwitchFind& f, uint64_t ms, char* out, size_t cap) {
    const char* tail = "nothing will be written there; on foot the game turns its HUD off as before";
    switch (f.result) {
        case kFindFound:
            return std::snprintf(out, cap,
                                 "native-vr-on-foot: game HUD switch found at EliteDangerous64.exe+0x%X (read at +0x%X, the one read of "
                                 "its kind before the view's +0x1180 -> +0x18 -> +0x16C in the game's code; %llu ms)",
                                 f.byteRva, f.readerRva, static_cast<unsigned long long>(ms));
        case kFindNone:
            return std::snprintf(out, cap, "native-vr-on-foot: game HUD switch not found: no read of its kind in the game's code; %s", tail);
        case kFindMany:
            return std::snprintf(out, cap, "native-vr-on-foot: game HUD switch not found: %u reads of its kind in the game's code, not one; %s",
                                 f.matches, tail);
        case kFindOutside:
            return std::snprintf(out, cap,
                                 "native-vr-on-foot: game HUD switch not found: the one match (read at +0x%X) points outside the game's "
                                 "image (+0x%X); %s",
                                 f.readerRva, f.byteRva, tail);
        default:
            return std::snprintf(out, cap,
                                 "native-vr-on-foot: game HUD switch not found: the one match (read at +0x%X) points into a section "
                                 "that cannot be written (+0x%X); %s",
                                 f.readerRva, f.byteRva, tail);
    }
}
inline int formatFind(const SwitchFind& f, uint32_t timestamp, uint32_t imageSize, uint64_t ms, char* out, size_t cap) {
    const char* tail = "nothing will be written there; the game keeps its cinema on foot";
    switch (f.result) {
        case kFindFound:
            return std::snprintf(out, cap,
                                 "native-vr-on-foot: cinema-on-foot switch found at EliteDangerous64.exe+0x%X (read at +0x%X, the one "
                                 "call-then-compare of its kind in the game's code; PE stamp 0x%08X, image size 0x%X; %llu ms)",
                                 f.byteRva, f.readerRva, timestamp, imageSize, static_cast<unsigned long long>(ms));
        case kFindNone:
            return std::snprintf(out, cap,
                                 "native-vr-on-foot: cinema-on-foot switch not found: no call-then-compare of its kind in the game's "
                                 "code (PE stamp 0x%08X, image size 0x%X; %llu ms); %s",
                                 timestamp, imageSize, static_cast<unsigned long long>(ms), tail);
        case kFindMany:
            return std::snprintf(out, cap,
                                 "native-vr-on-foot: cinema-on-foot switch not found: %u call-then-compare matches in the game's code, "
                                 "not one (PE stamp 0x%08X, image size 0x%X); %s",
                                 f.matches, timestamp, imageSize, tail);
        case kFindOutside:
            return std::snprintf(out, cap,
                                 "native-vr-on-foot: cinema-on-foot switch not found: the one match (read at +0x%X) points outside the "
                                 "game's image (+0x%X; PE stamp 0x%08X); %s",
                                 f.readerRva, f.byteRva, timestamp, tail);
        default:
            return std::snprintf(out, cap,
                                 "native-vr-on-foot: cinema-on-foot switch not found: the one match (read at +0x%X) points into a "
                                 "section that cannot be written (+0x%X; PE stamp 0x%08X); %s",
                                 f.readerRva, f.byteRva, timestamp, tail);
    }
}
enum : uint8_t {
    kSwitchNone = 0,
    kSwitchSet = 1,       // 1 -> 0: ours from now on
    kSwitchAgain = 2,     // the game put 1 back over ours: 0 again
    kSwitchRestored = 3,  // key off: our 0 -> the game's 1
    kSwitchLeft = 4,      // key off, but the byte is no longer our 0: left as it is
    kSwitchStrange = 5,   // neither 1 nor 0 (or unreadable): never written, said once until the key goes off
    kSwitchGameOff = 6,   // already 0 before us: the game's own, not ours, never restored
};
struct SwitchState {
    bool ours = false;         // the byte holds a 0 the switch wrote
    bool refused = false;      // kSwitchStrange said: nothing is written until the key goes off
    bool saidGameOff = false;  // kSwitchGameOff said
    uint32_t again = 0;        // kSwitchAgain count since kSwitchSet
};
struct SwitchStep {
    int write = -1;  // the value to write (-1: none)
    uint8_t kind = kSwitchNone;
};
// Any switch of the game's data that holds `game` and that we hold at `ours`: the write as a flag, since -1 is a value here.
struct ValueStep {
    bool write = false;
    int32_t value = 0;  // the value to write, when `write`
    uint8_t kind = kSwitchNone;
};
// `value` is the switch as read this frame, meaningless unless `readOk`. kSwitchGameOff here means "already `ours`, the game's own".
inline ValueStep switchStep(SwitchState& s, bool key, bool readOk, int32_t value, int32_t game, int32_t ours) {
    ValueStep r;
    if (key) {
        if (s.refused) return r;
        if (readOk && value == game) {
            r.write = true;
            r.value = ours;
            r.kind = s.ours ? kSwitchAgain : kSwitchSet;
            s.again = s.ours ? s.again + 1 : 0;
            s.ours = true;
        } else if (readOk && value == ours) {
            if (!s.ours && !s.saidGameOff) {
                r.kind = kSwitchGameOff;
                s.saidGameOff = true;
            }
        } else {
            r.kind = kSwitchStrange;
            s.ours = false;
            s.refused = true;
        }
    } else {
        if (s.ours) {
            if (readOk && value == ours) {
                r.write = true;
                r.value = game;
                r.kind = kSwitchRestored;
            } else {
                r.kind = kSwitchLeft;
            }
        }
        s.ours = false;
        s.refused = false;
        s.saidGameOff = false;
        s.again = 0;
    }
    return r;
}
// `value` is the byte as read this frame, -1 if it could not be read.
inline SwitchStep cinemaSwitchStep(SwitchState& s, bool key, int value) {
    const ValueStep v = switchStep(s, key, value >= 0, value, kCinemaSwitchGame, 0);
    SwitchStep r;
    r.kind = v.kind;
    r.write = v.write ? v.value : -1;
    return r;
}
inline ValueStep hudSwitchStep(SwitchState& s, bool key, bool readOk, int32_t value) {
    return switchStep(s, key, readOk, value, kHudSwitchGame, kHudSwitchOurs);
}
// A repeat is said at 1, 10, 100, ... so a game that puts 1 back every frame is visible without flooding the log.
inline bool switchAgainSaid(uint32_t again) {
    while (again >= 10 && again % 10 == 0) again /= 10;
    return again == 1;
}
// `rva` is where the switch was found (findCinemaSwitch).
inline int formatSwitch(const SwitchStep& step, const SwitchState& s, int value, int onFoot, uint32_t rva, char* out, size_t cap) {
    switch (step.kind) {
        case kSwitchSet:
            return std::snprintf(out, cap,
                                 "native-vr-on-foot: head camera on: the game's cinema-on-foot switch (EliteDangerous64.exe+0x%X) 1 -> 0; "
                                 "on foot the game keeps headset stereo and its headset camera itself, as in the ship (on foot (journal): %s)",
                                 rva, onFootText(onFoot));
        case kSwitchAgain:
            return std::snprintf(out, cap, "native-vr-on-foot: head camera: the game put its cinema-on-foot switch back to 1; 0 again (%u time(s))",
                                 s.again);
        case kSwitchRestored:
            return std::snprintf(out, cap,
                                 "native-vr-on-foot: head camera off: the cinema-on-foot switch is the game's 1 again; the next disembark is "
                                 "the game's cinema");
        case kSwitchLeft:
            return std::snprintf(out, cap, "native-vr-on-foot: head camera off: the cinema-on-foot switch holds %d, not our 0; left as it is",
                                 value);
        case kSwitchStrange:
            if (value < 0)
                return std::snprintf(out, cap,
                                     "native-vr-on-foot: head camera cannot work (the cinema-on-foot switch could not be read); nothing "
                                     "written there; the hold works as before");
            return std::snprintf(out, cap,
                                 "native-vr-on-foot: head camera cannot work (the cinema-on-foot switch holds %d, not the game's 1); "
                                 "nothing written there; the hold works as before",
                                 value);
        case kSwitchGameOff:
            return std::snprintf(out, cap,
                                 "native-vr-on-foot: head camera: the cinema-on-foot switch is already 0, the game's own; nothing written");
        default:
            return 0;
    }
}
// The journal's on-foot answer changed while the switch is ours: the line a flight log is read by.
inline int formatHeadCameraJournal(int onFoot, char* out, size_t cap) {
    return std::snprintf(out, cap, "native-vr-on-foot: head camera: %s",
                         onFoot == 1 ? "on foot (journal) with the cinema-on-foot switch 0: the game's own headset stereo"
                                     : "back in the ship (journal); the switch stays 0 while the key is on (it acts only on foot)");
}

// The game HUD switch's lines (hudSwitchStep); `rva` is where it was found, `value` what it held when read.
inline int formatHudSwitch(const ValueStep& step, const SwitchState& s, bool readOk, int32_t value, int onFoot, uint32_t rva, char* out,
                           size_t cap) {
    switch (step.kind) {
        case kSwitchSet:
            return std::snprintf(out, cap,
                                 "native-vr-on-foot: game HUD on: the game's HUD switch (EliteDangerous64.exe+0x%X) -1 (auto) -> 1 (on); on "
                                 "foot the game keeps its FirstPersonPostGUI pass, the HUD, on in headset stereo (on foot (journal): %s)",
                                 rva, onFootText(onFoot));
        case kSwitchAgain:
            return std::snprintf(out, cap, "native-vr-on-foot: game HUD: the game put its HUD switch back to -1; 1 again (%u time(s))", s.again);
        case kSwitchRestored:
            return std::snprintf(out, cap, "native-vr-on-foot: game HUD off: the HUD switch is the game's -1 (auto) again");
        case kSwitchLeft:
            if (!readOk)
                return std::snprintf(out, cap, "native-vr-on-foot: game HUD off: the HUD switch could not be read; left as it is");
            return std::snprintf(out, cap, "native-vr-on-foot: game HUD off: the HUD switch holds %d, not our 1; left as it is", value);
        case kSwitchStrange:
            if (!readOk)
                return std::snprintf(out, cap,
                                     "native-vr-on-foot: game HUD cannot work (the HUD switch could not be read); nothing written there");
            return std::snprintf(out, cap,
                                 "native-vr-on-foot: game HUD cannot work (the HUD switch holds %d, not the game's -1); nothing written "
                                 "there",
                                 value);
        case kSwitchGameOff:
            return std::snprintf(out, cap, "native-vr-on-foot: game HUD: the HUD switch is already 1, the game's own; nothing written");
        default:
            return 0;
    }
}
// experimental.native_vr_on_foot_game_hud as it acts: the key itself and native_vr_on_foot under it.
inline int formatGameHudKey(bool key, bool onFootKey, char* out, size_t cap) {
    if (key && onFootKey)
        return std::snprintf(out, cap,
                             "native-vr-on-foot: game HUD on (experimental.native_vr_on_foot_game_hud = 1): the game keeps its own on-foot "
                             "HUD on in headset stereo");
    if (key)
        return std::snprintf(out, cap,
                             "native-vr-on-foot: game HUD idle (experimental.native_vr_on_foot_game_hud = 1, but native_vr_on_foot = 0): "
                             "the HUD switch is the game's");
    return std::snprintf(out, cap, "native-vr-on-foot: game HUD off (experimental.native_vr_on_foot_game_hud = 0): the HUD switch is the game's");
}

// "EliteDangerous64.exe+0x283AFB0" for an address inside the image, the bare address otherwise.
inline int formatAddress(uint64_t address, uint64_t base, uint64_t imageSize, char* out, size_t cap) {
    if (base && address >= base && address - base < imageSize)
        return std::snprintf(out, cap, "EliteDangerous64.exe+0x%llX", static_cast<unsigned long long>(address - base));
    return std::snprintf(out, cap, "0x%llX", static_cast<unsigned long long>(address));
}

// The event's log line. `onFoot` (-1 unknown, 0, 1) is the journal's answer when the line is written; the hold's lines show it.
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
    if (isCameraPoint(e.point)) {
        char held[48] = "";
        if (e.substituted) std::snprintf(held, sizeof(held), ", held by native_vr_on_foot");
        char site[120], caller[64] = "";
        std::snprintf(site, sizeof(site), "site %s (+0x%llX)", kPointName[e.point], static_cast<unsigned long long>(kPointRva[e.point]));
        if (e.point != kPointCamera && e.frames) {
            char at[48];
            formatAddress(e.stack[0], base, imageSize, at, sizeof(at));
            std::snprintf(caller, sizeof(caller), "; called from %s", at);
        }
        if (e.point == kPointCameraDefault)
            return std::snprintf(out, cap, "native-vr-probe: camera: first-person default asked at stereo mode %d%s; %s; thread %u%s", e.mode,
                                 held, site, e.thread, caller);
        char flag[24] = "";
        if (e.point != kPointCamera) std::snprintf(flag, sizeof(flag), ", flag %u", e.flagBefore);
        if (!e.readOk)
            return std::snprintf(out, cap, "native-vr-probe: camera: the name could not be read, at stereo mode %d%s; %s%s; thread %u%s",
                                 e.mode, held, site, flag, e.thread, caller);
        return std::snprintf(out, cap, "native-vr-probe: camera \"%s\" at stereo mode %d%s; %s%s; thread %u%s", e.name, e.mode, held, site,
                             flag, e.thread, caller);
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
