#pragma once
// The on-foot head aim probe, the pure half (EDVRonFOOT; experimental.native_vr_on_foot_head_aim_probe and _compensate; openspec change
// on-foot-head-aim-probe; head_aim_probe.h for the glue). Everything here is plain C++ the rig (tools\head_aim_probe_test) drives directly: how both
// keys are read, the frame's shift (its 0.5 s ramp and the 4 s sway), the turn the runtime gives the game's head pose, who the player is, and the text
// of every line.
//
// WHAT IT IS FOR. Head aim (path A, ROADMAP.md) turns the commander's body and tilt after the headset and turns the game's head pose back by the same
// amount. Before writing that, one flight must say which of the two angles the game reads from the humanoid movement component (HMC) is the turn and
// which the tilt, whether the barrel and the game's dot follow a shifted angle from the hip, which sign of the turn holds the world still, and whether
// the turn keeps up frame by frame. The probe shifts one angle by a fixed 10 degrees or sways it +/-10 degrees, switched live in F8.
//
// THE ANGLES (334925; docs\research\native-vr-on-foot.md, "Прицел головой: разведка" and "Зонд прицела головой"). The character's eye matrix turns
// the body's by the angle HMC vf+0xC8 returns, then by the one vf+0xB0 returns, both through the same sine and cosine: radians.
//   angle 1, vf+0xB0 (+0x1A45930): [hmc+0x6F0] - [hmc+0x710]
//   angle 2, vf+0xC8 (+0x1A49040): [hmc+0x70C] + [hmc+0x6FC]
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace edvr {
namespace hap {

// experimental.native_vr_on_foot_head_aim_probe: off (the default; also when empty and, with *valid false, for anything unknown), watch, angle1,
// angle2, angle1-sway, angle2-sway.
enum class Mode : int { Off = 0, Watch, Angle1, Angle2, Angle1Sway, Angle2Sway };
// experimental.native_vr_on_foot_head_aim_probe_compensate: off (the default; also for anything unknown), minus, plus.
enum class Comp : int { Off = 0, Minus, Plus };

inline const char* modeName(Mode m) {
    switch (m) {
        case Mode::Watch: return "watch";
        case Mode::Angle1: return "angle1";
        case Mode::Angle2: return "angle2";
        case Mode::Angle1Sway: return "angle1-sway";
        case Mode::Angle2Sway: return "angle2-sway";
        default: return "off";
    }
}
inline const char* compName(Comp c) { return c == Comp::Minus ? "minus" : c == Comp::Plus ? "plus" : "off"; }
// The angle a mode shifts: 1, 2, or 0 for none.
inline int angleOf(Mode m) {
    return m == Mode::Angle1 || m == Mode::Angle1Sway ? 1 : m == Mode::Angle2 || m == Mode::Angle2Sway ? 2 : 0;
}
inline bool swayOf(Mode m) { return m == Mode::Angle1Sway || m == Mode::Angle2Sway; }

namespace detail {
// Trimmed, case-insensitive: true if `text` (n characters) is `word`.
inline bool is(const char* text, size_t n, const char* word) {
    if (std::strlen(word) != n) return false;
    for (size_t i = 0; i < n; ++i) {
        char c = text[i];
        if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
        if (c != word[i]) return false;
    }
    return true;
}
inline const char* trim(const char* text, size_t* n) {
    if (!text) { *n = 0; return ""; }
    while (*text == ' ' || *text == '\t') ++text;
    size_t k = std::strlen(text);
    while (k && (text[k - 1] == ' ' || text[k - 1] == '\t' || text[k - 1] == '\r' || text[k - 1] == '\n')) --k;
    *n = k;
    return text;
}
}  // namespace detail

inline Mode parseMode(const char* raw, bool* valid = nullptr) {
    if (valid) *valid = true;
    size_t n = 0;
    const char* t = detail::trim(raw, &n);
    if (!n || detail::is(t, n, "off")) return Mode::Off;
    if (detail::is(t, n, "watch")) return Mode::Watch;
    if (detail::is(t, n, "angle1")) return Mode::Angle1;
    if (detail::is(t, n, "angle2")) return Mode::Angle2;
    if (detail::is(t, n, "angle1-sway")) return Mode::Angle1Sway;
    if (detail::is(t, n, "angle2-sway")) return Mode::Angle2Sway;
    if (valid) *valid = false;
    return Mode::Off;
}
inline Comp parseComp(const char* raw, bool* valid = nullptr) {
    if (valid) *valid = true;
    size_t n = 0;
    const char* t = detail::trim(raw, &n);
    if (!n || detail::is(t, n, "off")) return Comp::Off;
    if (detail::is(t, n, "minus")) return Comp::Minus;
    if (detail::is(t, n, "plus")) return Comp::Plus;
    if (valid) *valid = false;
    return Comp::Off;
}

constexpr double kPi = 3.14159265358979323846;
constexpr float kShiftDeg = 10.0f;
constexpr float kShiftRad = float(kShiftDeg * kPi / 180.0);
constexpr float kRampSeconds = 0.5f;
constexpr float kSwayPeriodSeconds = 4.0f;
// A dt longer than this (a pause, a load) moves the ramp only this far: never a jump.
constexpr float kMaxDtSeconds = 0.1f;

// Where the probe may act this frame. The shift acts only when every one holds; watch needs only onFoot and nativeOnFoot.
struct Gate {
    bool nativeOnFoot = false;   // experimental.native_vr_on_foot
    bool journalKnown = false;
    bool onFoot = false;         // the journal says on foot
    bool installed = false;      // the hooks are in
    bool player = false;         // the player's HMC is known
};
inline bool onFootFlag(const Gate& g) { return g.nativeOnFoot && g.journalKnown && g.onFoot; }
inline bool shiftAllowed(const Gate& g) { return onFootFlag(g) && g.installed && g.player; }

// The ramp toward the shift: level 0..1, a full rise or fall in kRampSeconds. The angle is kept while the level runs out after the key left it, so a
// switch from angle1 to off (or to angle2) eases angle 1 out before angle 2 eases in.
struct Ramp {
    float level = 0.0f;
    int angle = 0;      // the angle the level belongs to: 0, 1, 2
    bool sway = false;
};
struct Shift {
    int angle = 0;          // 0 none, 1, 2
    float rad = 0.0f;       // added to that angle
    float level = 0.0f;     // the ramp
};
// One step: `t` is the probe's clock in seconds (the sway's phase), `dt` the time since the last step.
inline Shift step(Ramp& r, Mode mode, bool allowed, float dt, double t) {
    if (!(dt >= 0.0f)) dt = 0.0f;
    if (dt > kMaxDtSeconds) dt = kMaxDtSeconds;
    const int want = allowed ? angleOf(mode) : 0;
    const float move = dt / kRampSeconds;
    if (r.level <= 0.0f) {
        r.level = 0.0f;
        r.angle = want;
        r.sway = swayOf(mode);
    }
    if (want != 0 && want == r.angle) {
        r.sway = swayOf(mode);
        r.level = r.level + move >= 1.0f ? 1.0f : r.level + move;
    } else {
        r.level = r.level - move <= 0.0f ? 0.0f : r.level - move;
    }
    Shift s;
    if (r.level <= 0.0f || r.angle == 0) return s;
    s.angle = r.angle;
    s.level = r.level;
    const double wave = r.sway ? std::sin(2.0 * kPi * t / double(kSwayPeriodSeconds)) : 1.0;
    s.rad = float(double(kShiftRad) * double(r.level) * wave);
    return s;
}

// The turn the runtime gives the game's head pose for a shift: minus or plus the shift, 0 when off or nothing is shifted.
inline float turnOf(Comp c, const Shift& s) {
    if (s.angle == 0 || c == Comp::Off) return 0.0f;
    return c == Comp::Minus ? -s.rad : s.rad;
}

// Distinct HMCs seen by the getters in a window, up to kSlots (more reads "many"). The glue fills an atomic copy of this; the rig a plain one.
constexpr int kSlots = 8;
struct Census {
    uintptr_t slot[kSlots] = {};
    bool many = false;
};
inline void note(Census& c, uintptr_t p) {
    if (!p) return;
    for (int i = 0; i < kSlots; ++i) {
        if (c.slot[i] == p) return;
        if (c.slot[i] == 0) { c.slot[i] = p; return; }
    }
    c.many = true;
}
inline int countOf(const Census& c) {
    int n = 0;
    for (int i = 0; i < kSlots; ++i) n += c.slot[i] ? 1 : 0;
    return n;
}
inline bool contains(const Census& c, uintptr_t p) {
    if (!p) return false;
    for (int i = 0; i < kSlots; ++i) if (c.slot[i] == p) return true;
    return false;
}

// Who the player is, from one window. The camera's way: the HMC behind the eye the on-foot camera reads its matrix from, confirmed only if the getters
// were called on it in the same window. The single way: the only HMC the getters saw. Neither: no player, and why.
enum class Source : int { None = 0, Camera, Single };
inline const char* sourceName(Source s) { return s == Source::Camera ? "camera" : s == Source::Single ? "single" : "none"; }
struct Identity {
    uintptr_t player = 0;
    Source source = Source::None;
    const char* why = "no getter calls";
};
inline Identity identify(uintptr_t cameraHmc, const Census& c) {
    Identity id;
    if (cameraHmc && contains(c, cameraHmc)) {
        id.player = cameraHmc;
        id.source = Source::Camera;
        id.why = "";
        return id;
    }
    const int n = countOf(c);
    if (n == 1 && !c.many) {
        id.player = c.slot[0];
        id.source = Source::Single;
        id.why = "";
        return id;
    }
    if (n == 0) id.why = cameraHmc ? "the camera's HMC was not seen by the getters" : "no getter calls";
    else id.why = cameraHmc ? "the camera's HMC was not seen by the getters, and several HMCs were" : "several HMCs and no camera";
    return id;
}

// ---- the lines ----------------------------------------------------------------------------------------------------------------------------------------

inline int formatKeys(Mode m, Comp c, char* out, size_t cap) {
    return std::snprintf(out, cap, "head aim probe: key=%s compensate=%s", modeName(m), compName(c));
}
inline int formatUnknownKey(const char* which, const char* raw, char* out, size_t cap) {
    return std::snprintf(out, cap, "head aim probe: %s value '%.40s' not understood, reads off", which, raw ? raw : "");
}
inline int formatHooked(bool camera, char* out, size_t cap) {
    return std::snprintf(out, cap, "head aim probe: hooked angle1 +0x1A45930 angle2 +0x1A49040%s",
                         camera ? ", eye matrix +0xF893D0 from the camera at +0x10743CB" : ", camera not hooked: the player is the single HMC only");
}
inline int formatInert(const char* why, char* out, size_t cap) {
    return std::snprintf(out, cap, "head aim probe: inert: %s", why ? why : "?");
}
inline int formatIdentity(const Identity& id, int hmcs, bool many, char* out, size_t cap) {
    if (id.source != Source::None)
        return std::snprintf(out, cap, "head aim probe: player identified by %s, hmcs=%d%s", sourceName(id.source), hmcs, many ? "+" : "");
    return std::snprintf(out, cap, "head aim probe: player not identified (%s), hmcs=%d%s", id.why, hmcs, many ? "+" : "");
}
inline int formatShiftEdge(bool on, Mode m, Comp c, char* out, size_t cap) {
    return on ? std::snprintf(out, cap, "head aim probe: shift on, %s, compensate=%s", modeName(m), compName(c))
              : std::snprintf(out, cap, "head aim probe: shift off");
}

// The fields of the player's HMC, raw, and whether they could be read.
struct Fields {
    bool read = false;
    float f6f0 = 0, f6fc = 0, f70c = 0, f710 = 0, f748 = 0, f754 = 0, f764 = 0, f768 = 0;
};
constexpr double kDegPerRad = 57.29577951308232;
// head_aim_watch,a1=..,a1_out=..,a2=..,a2_out=..,f6f0=..,...,hmcs=N,player=yes|no  -- the angles in degrees as the game built them and as the probe
// handed them on; the fields as they are. "nan" for anything not read.
inline int formatWatch(const Fields& f, float shift1Rad, float shift2Rad, int hmcs, bool many, bool player, char* out, size_t cap) {
    if (!f.read)
        return std::snprintf(out, cap, "head_aim_watch,a1=nan,a1_out=nan,a2=nan,a2_out=nan,fields=unread,hmcs=%d%s,player=%s", hmcs, many ? "+" : "",
                             player ? "yes" : "no");
    const double a1 = double(f.f6f0) - double(f.f710), a2 = double(f.f70c) + double(f.f6fc);
    return std::snprintf(out, cap,
                         "head_aim_watch,a1=%.2f,a1_out=%.2f,a2=%.2f,a2_out=%.2f,f6f0=%.4f,f6fc=%.4f,f70c=%.4f,f710=%.4f,f748=%.4f,f754=%.4f,"
                         "f764=%.4f,f768=%.4f,hmcs=%d%s,player=%s",
                         a1 * kDegPerRad, (a1 + shift1Rad) * kDegPerRad, a2 * kDegPerRad, (a2 + shift2Rad) * kDegPerRad, f.f6f0, f.f6fc, f.f70c,
                         f.f710, f.f748, f.f754, f.f764, f.f768, hmcs, many ? "+" : "", player ? "yes" : "no");
}

// The 30 s window's counts.
struct Window {
    uint64_t calls1 = 0, calls2 = 0;         // getter calls, every HMC
    uint64_t latches = 0, shifted = 0;       // runtime frames latched; of them with a shift
    uint64_t perFrameMin = 0, perFrameMax = 0, zeroFrames = 0;   // the player's angle-1 calls between two latches, while shifting
    bool perFrameSeen = false;
    double maxShiftDeg = 0, maxTurnDeg = 0;
};
// head_aim_summary,calls1=..,calls2=..,latches=..,shifted=..,max_shift_deg=..,max_turn_deg=..,per_frame=MIN..MAX,zero_frames=Z
inline int formatSummary(const Window& w, char* out, size_t cap) {
    return std::snprintf(out, cap,
                         "head_aim_summary,calls1=%llu,calls2=%llu,latches=%llu,shifted=%llu,max_shift_deg=%.1f,max_turn_deg=%.1f,per_frame=%llu..%llu,"
                         "zero_frames=%llu",
                         (unsigned long long)w.calls1, (unsigned long long)w.calls2, (unsigned long long)w.latches, (unsigned long long)w.shifted,
                         w.maxShiftDeg, w.maxTurnDeg, (unsigned long long)w.perFrameMin, (unsigned long long)w.perFrameMax,
                         (unsigned long long)w.zeroFrames);
}
// One latch's account of the player's angle-1 calls since the previous latch, while shifting.
inline void notePerFrame(Window& w, uint64_t calls) {
    if (calls == 0) ++w.zeroFrames;
    if (!w.perFrameSeen || calls < w.perFrameMin) w.perFrameMin = calls;
    w.perFrameSeen = true;
    if (calls > w.perFrameMax) w.perFrameMax = calls;
}

}  // namespace hap
}  // namespace edvr
