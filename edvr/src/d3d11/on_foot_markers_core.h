#pragma once
// On-foot markers, the pure half (EDVRonFOOT; experimental.native_vr_on_foot_markers; the project's docs\research\native-vr-on-foot.md,
// "Правка позы PostGUI .17"). on_foot_markers.h has the hooks. Everything here is plain C++ the rig drives directly: build 334473's
// identity and the three points' bytes, the chain to the PostGUI cameras read through a reader the caller supplies, the key, the
// decision whether to correct, the corrected pose, the counters, the 30-second window and the text of every line. Nothing here depends
// on the on-foot HUD probe (on_foot_hud_probe_core.h): the probe goes when the HUD arc closes.
//
// What it fixes: on foot in headset stereo the game draws its "over the world" layer (the crosshair, the prompts over objects and
// people) with a camera -- the PostGUI pass's -- that it gives the head's pose every frame, as in the ship. In the cinema the same
// camera is the identity, and the layer is made for that: the markers already carry the gaze, so the head's turn in the camera moves
// them a second time (flights .15, .16). After the game writes that pose, both eyes' PostGUI cameras get the identity rotation.
//
// The game, build 334473 (Ghidra; design.md of openspec change native-vr-on-foot-postgui-pose):
//   0x287D660 (mgr, a2), called from 0x287ABA0, and 0x28457F0 (mgr, n, a3), called from 0x287D660 (n = 1) and from the pipeline frame
//     0x286B250 (n = 2), each write the PostGUI cameras' pose every frame: a copy into cam+0x20 (0x59F300), then the commit
//     0x2874820 (cam, size, cam+0x20), which rebuilds the block's derived data when its flags hold bit 0x1, copies it to cam+0x310 and
//     lays out the culling. The game's pose setters (0x4F2C20) set the block's flags |= 0xF after changing the matrix.
//   The cameras: passes = *(*(*(mgr+0x7F0)+0x1180)+0x2CB8); pass_i = *(passes+0x90+8i) (view 0 the left eye, 1 the right);
//     cam_i = *(*(*(pass_i+0x2E0)+0x38)+0x100) (the accessor 0x28ABB00), the same object as *(pass_i+0x50); size_i = *(u64*)(pass_i+0x310).
//   The pose: cam+0x20, a row-major 4x4 -- rows 0-2 the axes (right, up, forward), row 3 (cam+0x50) the position -- and its flags at
//     cam+0x250.
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace edvr {
namespace ofm {

// Build 334473's PE TimeDateStamp and SizeOfImage (common/elite_oculus_profile.h checks the same stamp).
constexpr uint32_t kExpectedTimestamp = 0x6ACD513Du;
constexpr uint32_t kExpectedImageSize = 0x640C000u;

// The points: the two writers are hooked; the commit is only called, and its bytes are checked so the call is the game's own.
enum Point : int { kPointPose = 0, kPointEyes, kPointCommit, kPointCount };
constexpr uintptr_t kPointRva[kPointCount] = {0x287D660u, 0x28457F0u, 0x2874820u};
constexpr size_t kPrologueBytes = 16;
// Each point's first 16 bytes in build 334473 (capstone, 2026-10-22). None has a relative operand in them; CodeHook moves 7 bytes of
// 0x287D660 (mov rax,rsp; mov [rax+10h],rdx) and 12 of 0x28457F0 (push rbp; push rbx; push rsi; lea rbp,[rsp-17B0h]) -- its
// __chkstk call is at +0x11, past them.
constexpr uint8_t kPointPrologue[kPointCount][kPrologueBytes] = {
    // mov rax,rsp; mov [rax+10h],rdx; push rbx; push rsi; sub rsp,68h; mov rbx,[rcx+58h]
    {0x48, 0x8B, 0xC4, 0x48, 0x89, 0x50, 0x10, 0x53, 0x56, 0x48, 0x83, 0xEC, 0x68, 0x48, 0x8B, 0x59},
    // push rbp; push rbx; push rsi; lea rbp,[rsp-17B0h]; mov eax,18B0h
    {0x40, 0x55, 0x53, 0x56, 0x48, 0x8D, 0xAC, 0x24, 0x50, 0xE8, 0xFF, 0xFF, 0xB8, 0xB0, 0x18, 0x00},
    // mov [rsp+10h],rbx; mov [rsp+20h],rsi; push rdi; sub rsp,270h
    {0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x20, 0x57, 0x48, 0x81, 0xEC, 0x70, 0x02},
};
inline const char* pointName(int p) {
    switch (p) {
        case kPointPose: return "pose writer 0x287D660";
        case kPointEyes: return "eye pose writer 0x28457F0";
        default: return "commit 0x2874820";
    }
}

// The chain to the cameras.
constexpr uintptr_t kMgrPipeline = 0x7F0;
constexpr uintptr_t kPipelineFrame = 0x1180;
constexpr uintptr_t kFramePasses = 0x2CB8;
constexpr uintptr_t kPassesView0 = 0x90;  // view 1 at +0x98
constexpr uintptr_t kPassView = 0x8;      // i32
constexpr uintptr_t kPassPostGuiCamera = 0x50;
constexpr uintptr_t kPassCameraHolder = 0x2E0;
constexpr uintptr_t kHolderCamera = 0x38;
constexpr uintptr_t kHolderCameraObject = 0x100;
constexpr uintptr_t kPassSize = 0x310;  // u64
constexpr uintptr_t kCamPose = 0x20;
constexpr uintptr_t kCamFlags = 0x250;  // u32: the pose block's flags (block+0x230)
constexpr uint32_t kStaleFlags = 0xF;   // what the game's setter 0x4F2C20 sets after a matrix change
constexpr int kPoseFloats = 16;
constexpr int kViews = 2;

// ---- the key, the decision ------------------------------------------------------------------------------------------------------

enum Mode : int { kModeNone = 0, kModeStereo = 1, kModeFlat = 2, kModeOff = 3 };
inline const char* modeName(int m) { return m == kModeStereo ? "stereo" : m == kModeFlat ? "flat" : m == kModeOff ? "off" : "none"; }

struct Key {
    int mode = kModeStereo;
    bool understood = true;
};
// experimental.native_vr_on_foot_markers: stereo, flat or off, any case, spaces around ignored. No line reads as stereo (the caller's
// default); anything else, an empty value too, reads as stereo and is not understood.
inline Key parseKey(const char* text) {
    Key k;
    char v[16] = {};
    size_t n = 0;
    const char* p = text ? text : "";
    while (*p == ' ' || *p == '\t') ++p;
    for (; *p && n + 1 < sizeof(v); ++p) v[n++] = static_cast<char>(*p >= 'A' && *p <= 'Z' ? *p - 'A' + 'a' : *p);
    while (n && (v[n - 1] == ' ' || v[n - 1] == '\t')) v[--n] = 0;
    if (*p) n = 0;  // longer than any value: not understood
    if (n && !std::strcmp(v, "stereo")) k.mode = kModeStereo;
    else if (n && !std::strcmp(v, "flat")) k.mode = kModeFlat;
    else if (n && !std::strcmp(v, "off")) k.mode = kModeOff;
    else k.understood = false;
    return k;
}

// Why the correction is off (kWhyOn: it is on).
enum Why : int { kWhyOn = 0, kWhyNative, kWhyKey, kWhyShip, kWhyJournal, kWhyNotInstalled, kWhyDisabled };
inline const char* whyText(int w) {
    switch (w) {
        case kWhyNative: return "native_vr_on_foot is off";
        case kWhyKey: return "the key";
        case kWhyShip: return "the ship";
        case kWhyJournal: return "journal unknown";
        case kWhyNotInstalled: return "not installed, the line above says why";
        case kWhyDisabled: return "the commit faulted 3 times";
        default: return "on";
    }
}
struct Decision {
    int mode = kModeNone;  // kModeStereo or kModeFlat while on
    int why = kWhyJournal;
};
// native: experimental.native_vr_on_foot; keyMode: the parsed key; onFoot: the journal (1, 0, -1 unknown). Unknown means hands off.
inline Decision decide(bool native, int keyMode, int onFoot) {
    if (!native) return {kModeNone, kWhyNative};
    if (keyMode == kModeOff) return {kModeNone, kWhyKey};
    if (onFoot < 0) return {kModeNone, kWhyJournal};
    if (onFoot == 0) return {kModeNone, kWhyShip};
    return {keyMode, kWhyOn};
}

// The on and off lines: one when the decision changes, the first one included.
class Toggle {
public:
    bool step(const Decision& d) {
        if (d.mode == m_mode && d.why == m_why) return false;
        m_mode = d.mode;
        m_why = d.why;
        return true;
    }

private:
    int m_mode = -1, m_why = -1;
};

// ---- the cameras ---------------------------------------------------------------------------------------------------------------

// Reads n bytes of the game at addr; false if they cannot be read. The hook's reader is SEH-guarded, the rig's reads its own memory.
using ReadFn = bool (*)(void* ctx, uint64_t addr, void* out, size_t n);
template <class T>
inline bool readAs(ReadFn read, void* rctx, uint64_t addr, T* out) {
    return addr && read(rctx, addr, out, sizeof(T));
}

// Why a frame was not corrected.
enum Skip : int { kSkipNone = 0, kSkipNoPasses, kSkipViewId, kSkipMismatch, kSkipUnreadable, kSkipCount };
inline const char* skipName(int s) {
    switch (s) {
        case kSkipNoPasses: return "no passes";
        case kSkipViewId: return "view id";
        case kSkipMismatch: return "camera mismatch";
        case kSkipUnreadable: return "unreadable";
        default: return "none";
    }
}

struct Pair {
    uint64_t pass[kViews] = {}, cam[kViews] = {}, size[kViews] = {};
    float m[kViews][kPoseFloats] = {};
};
// Both eyes' PostGUI cameras from the writer's mgr, with the checks: each pass's view id is its slot, the camera the accessor reaches
// is the pass's own +0x50, neither is 0. Any failure: kSkip*, and the frame is corrected for neither eye.
inline int locate(uint64_t mgr, ReadFn read, void* rctx, Pair* p) {
    *p = Pair{};
    uint64_t pipeline = 0, frame = 0, passes = 0;
    if (!readAs(read, rctx, mgr + kMgrPipeline, &pipeline) || !readAs(read, rctx, pipeline + kPipelineFrame, &frame) ||
        !readAs(read, rctx, frame + kFramePasses, &passes))
        return kSkipUnreadable;
    if (!passes) return kSkipNoPasses;
    for (int i = 0; i < kViews; ++i) {
        if (!readAs(read, rctx, passes + kPassesView0 + 8u * i, &p->pass[i])) return kSkipUnreadable;
        if (!p->pass[i]) return kSkipNoPasses;
    }
    for (int i = 0; i < kViews; ++i) {
        const uint64_t pass = p->pass[i];
        int32_t view = -1;
        if (!readAs(read, rctx, pass + kPassView, &view)) return kSkipUnreadable;
        if (view != i) return kSkipViewId;
        uint64_t holder = 0, camera = 0, cam = 0, direct = 0;
        if (!readAs(read, rctx, pass + kPassCameraHolder, &holder) || !readAs(read, rctx, pass + kPassPostGuiCamera, &direct)) return kSkipUnreadable;
        if (!holder) return kSkipMismatch;
        if (!readAs(read, rctx, holder + kHolderCamera, &camera)) return kSkipUnreadable;
        if (!camera) return kSkipMismatch;
        if (!readAs(read, rctx, camera + kHolderCameraObject, &cam)) return kSkipUnreadable;
        if (!cam || cam != direct) return kSkipMismatch;
        p->cam[i] = cam;
        if (!readAs(read, rctx, pass + kPassSize, &p->size[i]) || !read(rctx, cam + kCamPose, p->m[i], sizeof(p->m[i]))) return kSkipUnreadable;
    }
    return kSkipNone;
}

// The corrected pose of both eyes from the game's: rows 0-2 the identity (their fourth numbers 0), row 3's x, y, z by the mode, its
// fourth number as the game left it.
//   stereo: each eye's offset from the point between the eyes, in that eye's own axes -- dot(p_i - m, r0_i), dot(.., r1_i),
//           dot(.., r2_i), m = (p_0 + p_1) / 2. The head's turn and tilt do not move it; the eyes' distance is kept.
//   flat:   both at 0, as the game sets them in the cinema.
// Both are fixed points: a second step over a corrected pair changes nothing (beyond a float's rounding).
inline void correct(const float in[kViews][kPoseFloats], int mode, float out[kViews][kPoseFloats]) {
    float mid[3];
    for (int k = 0; k < 3; ++k) mid[k] = 0.5f * (in[0][12 + k] + in[1][12 + k]);
    for (int i = 0; i < kViews; ++i) {
        float pos[3] = {0, 0, 0};
        if (mode == kModeStereo) {
            const float d[3] = {in[i][12] - mid[0], in[i][13] - mid[1], in[i][14] - mid[2]};
            for (int r = 0; r < 3; ++r) pos[r] = d[0] * in[i][4 * r] + d[1] * in[i][4 * r + 1] + d[2] * in[i][4 * r + 2];
        }
        for (int r = 0; r < 3; ++r)
            for (int c = 0; c < 4; ++c) out[i][4 * r + c] = r == c ? 1.f : 0.f;
        out[i][12] = pos[0];
        out[i][13] = pos[1];
        out[i][14] = pos[2];
        out[i][15] = in[i][15];
    }
}

// Rows 0-2 the identity within 1e-4: the correction's pose, not the game's.
constexpr float kIdentityEps = 1e-4f;
inline bool identityRows(const float* m) {
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 3; ++c)
            if (!(std::fabs(m[4 * r + c] - (r == c ? 1.f : 0.f)) <= kIdentityEps)) return false;
    return true;
}

// ---- the counters, the sample, the window ---------------------------------------------------------------------------------------

// The hook side's, cumulative. poseCalls: 0x287D660; eyesOne / eyesOther: 0x28457F0 with n = 1 / another n. steps: correction steps
// tried (an outer writer call while on); corrected: both eyes committed; skips by kSkip*; commitFaults: a fault in the commit call.
struct Counts {
    uint64_t poseCalls = 0, eyesOne = 0, eyesOther = 0, steps = 0, corrected = 0, commitFaults = 0;
    uint64_t skips[kSkipCount] = {};
};
inline Counts minus(const Counts& a, const Counts& b) {
    Counts d;
    d.poseCalls = a.poseCalls - b.poseCalls;
    d.eyesOne = a.eyesOne - b.eyesOne;
    d.eyesOther = a.eyesOther - b.eyesOther;
    d.steps = a.steps - b.steps;
    d.corrected = a.corrected - b.corrected;
    d.commitFaults = a.commitFaults - b.commitFaults;
    for (int s = 0; s < kSkipCount; ++s) d.skips[s] = a.skips[s] - b.skips[s];
    return d;
}
constexpr uint64_t kFaultsToDisable = 3;

// The frame thread's end-of-frame check: both cameras' rows 0-2 read at Present -- the correction's (identity) or someone else's.
struct Check {
    uint64_t ours = 0, games = 0, unreadable = 0;
};

// One step's poses for the summary: the game's and ours, both eyes. The frame thread asks once a window, the hook fills the next step's,
// the frame thread takes it -- the fields only change hands through the state's release and acquire.
struct Sample {
    float game[kViews][kPoseFloats] = {};
    float ours[kViews][kPoseFloats] = {};
};
class SampleBox {
public:
    void want() {
        int idle = kIdle;
        m_state.compare_exchange_strong(idle, kWanted, std::memory_order_acq_rel);
    }
    bool wanted() const { return m_state.load(std::memory_order_relaxed) == kWanted; }
    // Hook side: only the thread that wins the state writes.
    void put(const float game[kViews][kPoseFloats], const float ours[kViews][kPoseFloats]) {
        int w = kWanted;
        if (!m_state.compare_exchange_strong(w, kWriting, std::memory_order_acquire)) return;
        std::memcpy(m_sample.game, game, sizeof(m_sample.game));
        std::memcpy(m_sample.ours, ours, sizeof(m_sample.ours));
        m_state.store(kReady, std::memory_order_release);
    }
    bool take(Sample* out) {
        if (m_state.load(std::memory_order_acquire) != kReady) return false;
        *out = m_sample;
        m_state.store(kIdle, std::memory_order_release);
        return true;
    }

private:
    enum : int { kIdle = 0, kWanted, kWriting, kReady };
    std::atomic<int> m_state{kIdle};
    Sample m_sample;
};

// The 30-second window, open while the correction is on: started when it comes on (the counters then are its base), closed when it
// goes off (nothing is written for a part window), due every 30 s.
class Window {
public:
    static constexpr uint64_t kMs = 30000;
    void start(uint64_t nowMs, const Counts& now) {
        m_on = true;
        m_startMs = nowMs;
        m_base = now;
        m_check = Check{};
    }
    void stop() { m_on = false; }
    bool on() const { return m_on; }
    void note(bool readable, bool ours) {
        if (!m_on) return;
        if (!readable) ++m_check.unreadable;
        else if (ours) ++m_check.ours;
        else ++m_check.games;
    }
    // Due: the window's counts and check, its seconds; the next window starts now.
    bool due(uint64_t nowMs, const Counts& now, Counts* delta, Check* check, uint64_t* seconds) {
        if (!m_on || nowMs - m_startMs < kMs) return false;
        *delta = minus(now, m_base);
        *check = m_check;
        *seconds = (nowMs - m_startMs) / 1000;
        start(nowMs, now);
        return true;
    }

private:
    bool m_on = false;
    uint64_t m_startMs = 0;
    Counts m_base;
    Check m_check;
};

// ---- the lines ------------------------------------------------------------------------------------------------------------------

inline int formatHooked(uint64_t pose, size_t movedPose, uint64_t eyes, size_t movedEyes, char* out, size_t cap) {
    return std::snprintf(out, cap,
                         "on-foot markers: hooked 0x287D660 and 0x28457F0, 16 bytes verified at each and at the commit 0x2874820 (0x%llX, %zu "
                         "moved; 0x%llX, %zu moved); after the game writes the PostGUI cameras' pose, on foot in stereo, both eyes get the "
                         "identity rotation",
                         static_cast<unsigned long long>(pose), movedPose, static_cast<unsigned long long>(eyes), movedEyes);
}

inline int formatWrongBuild(uint32_t timestamp, uint32_t imageSize, char* out, size_t cap) {
    return std::snprintf(out, cap,
                         "on-foot markers: NOT installed: the game is not build 334473 (PE stamp 0x%08X, image size 0x%X); nothing was patched "
                         "and the markers stay as the game sets them",
                         timestamp, imageSize);
}

inline int formatWrongBytes(int point, char* out, size_t cap) {
    return std::snprintf(out, cap,
                         "on-foot markers: NOT installed: the %s at EliteDangerous64.exe+0x%llX does not begin with build 334473's %zu bytes; "
                         "nothing was patched",
                         pointName(point), static_cast<unsigned long long>(kPointRva[point]), kPrologueBytes);
}

// why: "no memory could be placed within 2 GB of it" or "CodeHook refused it (its own line above says why)".
inline int formatNotHooked(int point, const char* why, char* out, size_t cap) {
    return std::snprintf(out, cap, "on-foot markers: NOT installed: the %s: %s; nothing stays patched", pointName(point), why);
}

// The key: at launch and on every change of it or of native_vr_on_foot.
inline int formatKey(const char* text, const Key& k, bool native, char* out, size_t cap) {
    int n = 0;
    if (k.understood)
        n = std::snprintf(out, cap, "on-foot markers: experimental.native_vr_on_foot_markers = %s", modeName(k.mode));
    else
        n = std::snprintf(out, cap, "on-foot markers: experimental.native_vr_on_foot_markers = '%s' is not stereo, flat or off: read as stereo",
                          text ? text : "");
    if (n > 0 && static_cast<size_t>(n) < cap && !native && k.mode != kModeOff)
        n += std::snprintf(out + n, cap - n, "; it works only with experimental.native_vr_on_foot, which is off: nothing is touched");
    return n;
}

inline int formatToggle(const Decision& d, char* out, size_t cap) {
    if (d.mode != kModeNone) return std::snprintf(out, cap, "on-foot markers: on (on foot, %s)", modeName(d.mode));
    return std::snprintf(out, cap, "on-foot markers: off (%s)", whyText(d.why));
}

inline int formatDisabled(uint64_t faults, char* out, size_t cap) {
    return std::snprintf(out, cap,
                         "on-foot markers: the commit 0x2874820 faulted %llu times: the correction is off until the game restarts (the writers "
                         "are only forwarded)",
                         static_cast<unsigned long long>(faults));
}

inline float eyeGap(const float a[kPoseFloats], const float b[kPoseFloats]) {
    const float dx = a[12] - b[12], dy = a[13] - b[13], dz = a[14] - b[14];
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

// The 30-second line. sample: null when no step filled one in the window.
inline int formatSummary(uint64_t seconds, int mode, const Counts& c, const Check& k, const Sample* sample, char* out, size_t cap) {
    const uint64_t calls = c.poseCalls + c.eyesOne + c.eyesOther;
    int n = std::snprintf(out, cap, "on-foot markers: %llu s %s: ", static_cast<unsigned long long>(seconds), modeName(mode));
    auto add = [&](const char* fmt, auto... args) {
        if (n > 0 && static_cast<size_t>(n) < cap) n += std::snprintf(out + n, cap - n, fmt, args...);
    };
    if (!calls)
        add("0 writer calls (0x287D660 0, 0x28457F0 0): the game reached no hooked writer");
    else
        add("writer calls 0x287D660 %llu, 0x28457F0 n=1 %llu, other n %llu", static_cast<unsigned long long>(c.poseCalls),
            static_cast<unsigned long long>(c.eyesOne), static_cast<unsigned long long>(c.eyesOther));
    add("; %llu step(s), %llu corrected", static_cast<unsigned long long>(c.steps), static_cast<unsigned long long>(c.corrected));
    uint64_t skipped = 0;
    for (int s = kSkipNoPasses; s < kSkipCount; ++s) skipped += c.skips[s];
    if (!skipped && !c.commitFaults) {
        add(", none skipped");
    } else {
        add(", skipped:");
        bool first = true;
        for (int s = kSkipNoPasses; s < kSkipCount; ++s) {
            if (!c.skips[s]) continue;
            add("%s %llu %s", first ? "" : ",", static_cast<unsigned long long>(c.skips[s]), skipName(s));
            first = false;
        }
        if (c.commitFaults) add("%s %llu commit fault", first ? "" : ",", static_cast<unsigned long long>(c.commitFaults));
    }
    add("; frame check ours %llu / game's %llu", static_cast<unsigned long long>(k.ours), static_cast<unsigned long long>(k.games));
    if (k.unreadable) add(" (%llu unreadable)", static_cast<unsigned long long>(k.unreadable));
    if (!sample) {
        add("; no sample");
        return n;
    }
    const float* g = sample->game[0];
    const float yaw = std::atan2(g[8], g[10]) * 57.2957795f;
    const float pitch = std::asin(g[9] > 1.f ? 1.f : g[9] < -1.f ? -1.f : g[9]) * 57.2957795f;
    const float* l = sample->ours[0];
    const float* r = sample->ours[1];
    auto mm = [](float v) { return static_cast<double>(std::fabs(v) < 5e-5f ? 0.f : v); };  // no "-0.0000" from a float's dust
    add("; sample: game view 0 yaw %.1f pitch %.1f -> ours rows %s, left (%.4f,%.4f,%.4f) right (%.4f,%.4f,%.4f); eye gap game %.4f ours %.4f",
        yaw, pitch, identityRows(l) && identityRows(r) ? "identity" : "NOT identity", mm(l[12]), mm(l[13]), mm(l[14]), mm(r[12]), mm(r[13]),
        mm(r[14]), eyeGap(sample->game[0], sample->game[1]), eyeGap(l, r));
    return n;
}

}  // namespace ofm
}  // namespace edvr
