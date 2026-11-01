#pragma once
// The on-foot head aim, the pure half (EDVRonFOOT; experimental.native_vr_on_foot_head_aim; openspec changes on-foot-head-aim and
// on-foot-aiming-cleanup; head_aim.cpp has the glue). Everything here is plain C++ the rig (tools\head_aim_test) drives directly: how the key is read,
// the head's turn and tilt out of the frame's pose, the one-euro filter, the 0.5 s ramp and the gates that decide its target, what the game's two
// angle getters hand on, the tilt the runtime turns the game's head pose by, who the player is among the humanoids the getters see, the on-foot
// gate, and the text of every line.
//
// WHAT IT DOES. The commander's aim follows the headset (path A, ROADMAP.md): the game's angle 2 (the character's look turn relative to the body, +
// = right) gets the head's turn added, the game's angle 1 (the tilt, + = DOWN) becomes the head's tilt, and the runtime turns the head pose the game
// gets back by the same two numbers, so the world stands still while the barrel and the game's dot go where the head looks.
//
// THE ANGLES (334925; docs\research\native-vr-on-foot.md, "Прицел головой: разведка"). The character's eye matrix turns the body's by the angle HMC
// (the humanoid movement component) vf+0xC8 returns, then by the one vf+0xB0 returns, both through the same sine and cosine: radians.
//   angle 1, vf+0xB0 (+0x1A45930): [hmc+0x6F0] - [hmc+0x710]
//   angle 2, vf+0xC8 (+0x1A49040): [hmc+0x70C] + [hmc+0x6FC]
//
// THE HEAD'S ANGLES. `physicalHead` (EdvrNativeFrameInput) is the real head-to-seated-space pose, row-major 3x4 (m[r][c] = p[r*4+c]). The head looks
// along f = -(third rotation column) = (-p[2], -p[6], -p[10]); yaw = atan2(f.x, -f.z) (+ = right), pitchUp = atan2(f.y, sqrt(f.x^2 + f.z^2)). Roll is
// not read: it stays in the pose the game gets.
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace edvr {
namespace ha {

constexpr double kPi = 3.14159265358979323846;
constexpr double kDegPerRad = 57.29577951308232;

// experimental.native_vr_on_foot_head_aim: on (the default; also when missing or empty and, with *valid false, for anything unknown, the old `raw`
// included), off.
enum class Key : int { Off = 0, On };

inline const char* keyName(Key k) { return k == Key::On ? "on" : "off"; }

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
// An angle brought into (-pi, pi].
inline double wrap(double a) {
    if (!std::isfinite(a)) return 0.0;
    while (a > kPi) a -= 2.0 * kPi;
    while (a <= -kPi) a += 2.0 * kPi;
    return a;
}
}  // namespace detail

inline Key parseKey(const char* raw, bool* valid = nullptr) {
    if (valid) *valid = true;
    size_t n = 0;
    const char* t = detail::trim(raw, &n);
    if (!n) return Key::On;
    if (detail::is(t, n, "off")) return Key::Off;
    if (detail::is(t, n, "on")) return Key::On;
    if (valid) *valid = false;
    return Key::On;
}

// The barrel sits this many degrees lower than the head's tilt (+ = lower): the fit the flights of .37-.40 settled on, no key.
constexpr double kPitchOffsetDeg = 10.0;
// The tilt the head asks for with the offset on it (the game's sign, + = down). The offset goes in before the blend and before the pose correction, so the
// runtime's correction carries it too and only the barrel moves.
inline float offsetTarget(float target) { return float(double(target) + kPitchOffsetDeg / kDegPerRad); }

// ---- the head's angles -----------------------------------------------------------------------------------------------------------------------------

struct Angles {
    double yaw = 0.0;       // + = right, radians
    double pitchUp = 0.0;   // + = up, radians
};
// False (and zero angles) for a pose that is not finite or whose forward vector is nothing.
inline bool anglesFromPose(const float* p, Angles* out) {
    *out = Angles{};
    if (!p) return false;
    for (int i = 0; i < 12; ++i) if (!std::isfinite(p[i])) return false;
    const double fx = -double(p[2]), fy = -double(p[6]), fz = -double(p[10]);
    const double flat = std::sqrt(fx * fx + fz * fz);
    if (!(flat + std::fabs(fy) > 1e-6)) return false;
    out->yaw = std::atan2(fx, -fz);
    out->pitchUp = std::atan2(fy, flat);
    return true;
}

// ---- the filter ------------------------------------------------------------------------------------------------------------------------------------

// One-euro, per axis (units: radians and seconds). On rest the cutoff sits at kMinCutoff and the shake dies; the faster the head moves the higher the
// cutoff climbs (kBeta) and the less the filter lags. No dead zone: the output reaches the input.
constexpr double kMinCutoffHz = 1.0, kBeta = 0.5, kDCutoffHz = 1.0;
// A dt longer than this (a pause, a load) moves the filter and the ramp only this far.
constexpr double kMaxDtSeconds = 0.1;

inline double alphaFor(double cutoffHz, double dt) {
    const double tau = 1.0 / (2.0 * kPi * cutoffHz);
    return 1.0 / (1.0 + tau / dt);
}
struct Axis {
    bool init = false;
    double x = 0.0;      // the filtered value
    double prev = 0.0;   // the last raw input
    double dx = 0.0;     // the filtered speed
};
// One sample. `wrapAngle`: differences are taken the short way through +-pi and the output stays in (-pi, pi]. dt <= 0 holds the output.
inline double filterStep(Axis& a, double v, double dt, bool wrapAngle) {
    if (!std::isfinite(v)) return a.x;
    if (!a.init) {
        a.init = true;
        a.x = a.prev = v;
        a.dx = 0.0;
        return a.x;
    }
    if (!(dt > 0.0)) return a.x;
    if (dt > kMaxDtSeconds) dt = kMaxDtSeconds;
    const double dv = wrapAngle ? detail::wrap(v - a.prev) : v - a.prev;
    a.dx += alphaFor(kDCutoffHz, dt) * (dv / dt - a.dx);
    const double cutoff = kMinCutoffHz + kBeta * std::fabs(a.dx);
    const double err = wrapAngle ? detail::wrap(v - a.x) : v - a.x;
    a.x += alphaFor(cutoff, dt) * err;
    if (wrapAngle) a.x = detail::wrap(a.x);
    a.prev = v;
    return a.x;
}

// ---- the gates and the ramp ------------------------------------------------------------------------------------------------------------------------

// The first gate that is shut, for the "head aim: off (reason)" line.
enum class Reason : int { None = 0, Key, Ship, Player, Channel, Menu, Focus };
inline const char* reasonName(Reason r) {
    switch (r) {
        case Reason::Key: return "key";
        case Reason::Ship: return "ship";
        case Reason::Player: return "player";
        case Reason::Channel: return "channel";
        case Reason::Menu: return "menu";
        case Reason::Focus: return "focus";
        default: return "none";
    }
}

constexpr double kRampSeconds = 0.5;
constexpr double kYawLimitRad = kPi / 2.0;
// No head pose for this long: the target is 0.
constexpr uint64_t kPoseStaleMs = 250;

struct Inputs {
    Key key = Key::Off;
    bool nativeOnFoot = false;    // experimental.native_vr_on_foot
    bool journalKnown = false;
    bool journalOnFoot = false;   // the journal says on foot
    bool player = false;          // the player's HMC is known
    bool channel8 = false;        // the runtime asked version 8
    bool menu = false;            // an on-foot menu layer is held (on_foot_hud, anything but the Hud layer)
    bool focusKnown = false;      // the journal's GuiFocus
    uint32_t focus = 0;
    const float* head = nullptr;  // the frame's physicalHead, nullptr when the frame has no pose
    uint64_t nowMs = 0;           // the latch's clock
};

struct State {
    Axis yaw, pitch;
    bool hasAngles = false;
    uint64_t lastMs = 0, lastPoseMs = 0;
    double headYaw = 0.0, headPitchUp = 0.0;   // the last pose's angles
    double filtYaw = 0.0, filtPitchUp = 0.0;   // after the filter
    double level = 0.0;
    // The hold (menu and focus): the output's angles as the last frame gave them (before the level), the values frozen when a hold began, and how far the
    // output has come back from them to the live angles (1 = live).
    double baseYaw = 0.0, basePitch = 0.0;
    double frozenYaw = 0.0, frozenPitch = 0.0;
    double live = 1.0;
    bool holding = false;
};

struct Output {
    float yawAdded = 0.0f;       // added to the player's angle 2, radians
    bool capped = false;         // yawAdded is held at the limit
    float pitchTarget = 0.0f;    // the angle 1 the head asks for, the game's sign (+ = down)
    float level = 0.0f;
    Reason reason = Reason::Key;   // the first shut gate, None when all are open
    double lagDeg = 0.0;         // the largest |head - filtered| of the two axes, this frame
    bool held = false;           // a menu or the GUI focus holds the angles at their last values (the level stays)
};

inline Reason firstShutGate(const Inputs& in, bool poseFresh) {
    if (in.key == Key::Off) return Reason::Key;
    if (!in.nativeOnFoot || !in.journalKnown || !in.journalOnFoot) return Reason::Ship;
    if (!in.player) return Reason::Player;
    if (!in.channel8 || !poseFresh) return Reason::Channel;
    if (in.menu) return Reason::Menu;
    if (in.focusKnown && in.focus != 0) return Reason::Focus;
    return Reason::None;
}

// One latch. With the key off the target is 0 too, so the level runs out smoothly; once it has, the state is dropped (the next `on` starts the filter
// from the head).
inline Output step(State& s, const Inputs& in) {
    double dt = s.lastMs && in.nowMs > s.lastMs ? double(in.nowMs - s.lastMs) / 1000.0 : 0.0;
    if (dt > kMaxDtSeconds) dt = kMaxDtSeconds;
    s.lastMs = in.nowMs;

    Output out;
    Angles a;
    if (anglesFromPose(in.head, &a)) {
        s.hasAngles = true;
        s.lastPoseMs = in.nowMs;
        s.headYaw = a.yaw;
        s.headPitchUp = a.pitchUp;
        s.filtYaw = filterStep(s.yaw, a.yaw, dt, true);
        s.filtPitchUp = filterStep(s.pitch, a.pitchUp, dt, false);
        const double lagYaw = std::fabs(detail::wrap(s.headYaw - s.filtYaw)), lagPitch = std::fabs(s.headPitchUp - s.filtPitchUp);
        out.lagDeg = (lagYaw > lagPitch ? lagYaw : lagPitch) * kDegPerRad;
    }
    const bool poseFresh = s.hasAngles && in.nowMs - s.lastPoseMs <= kPoseStaleMs;
    out.reason = firstShutGate(in, poseFresh);

    // A menu or the GUI focus, with every gate before them open and something to hold, holds the angles where they were (the level stays, the filter goes on).
    // Any other shut gate lets the level run out, from the held angles when a hold or the way back from one is still on.
    const bool hold = (out.reason == Reason::Menu || out.reason == Reason::Focus) && s.level > 0.0;
    const double move = dt / kRampSeconds;
    if (out.reason == Reason::None || hold) s.level = s.level + move >= 1.0 ? 1.0 : s.level + move;
    else s.level = s.level - move <= 0.0 ? 0.0 : s.level - move;
    if (hold) {
        if (!s.holding) { s.frozenYaw = s.baseYaw; s.frozenPitch = s.basePitch; s.live = 0.0; }
        s.holding = true;
        out.held = true;
    } else {
        s.holding = false;
        if (out.reason == Reason::None) s.live = s.live + move >= 1.0 ? 1.0 : s.live + move;
        else if (s.level <= 0.0) s.live = 1.0;
        else if (s.live < 1.0) { s.frozenYaw = s.baseYaw; s.frozenPitch = s.basePitch; s.live = 0.0; }   // leaving from the held angles
    }
    double baseYaw = s.filtYaw, basePitch = -s.filtPitchUp;
    if (s.live < 1.0) {
        baseYaw = detail::wrap(s.frozenYaw + detail::wrap(s.filtYaw - s.frozenYaw) * s.live);
        basePitch = s.frozenPitch + (-s.filtPitchUp - s.frozenPitch) * s.live;
    }
    s.baseYaw = baseYaw;
    s.basePitch = basePitch;

    double yaw = s.level * baseYaw;
    if (yaw > kYawLimitRad) { yaw = kYawLimitRad; out.capped = true; }
    if (yaw < -kYawLimitRad) { yaw = -kYawLimitRad; out.capped = true; }
    out.yawAdded = float(yaw);
    out.pitchTarget = float(basePitch);
    out.level = float(s.level);
    if (s.level <= 0.0 && in.key == Key::Off) s = State{};
    return out;
}

// ---- what the game and the runtime get -------------------------------------------------------------------------------------------------------------

// The player's angle 1 on the way out: the original at level 0, exactly the head's tilt at level 1 (the stick's tilt stops counting).
inline float blendPitch(float original, float target, float level) {
    return level >= 1.0f ? target : level <= 0.0f ? original : original + (target - original) * level;
}
// The tilt the runtime turns the game's head pose by (and gets as `added`): the head's whole tilt times the level, whatever the stick's tilt (the original
// angle 1) was, so the stick's tilt comes off the picture too. 0 at level 0.
inline float pitchCorrection(float target, float level) { return level <= 0.0f ? 0.0f : target * (level >= 1.0f ? 1.0f : level); }

// ---- where the hooks act, and who the player is --------------------------------------------------------------------------------------------------------

// The on-foot flag: the frame thread's verdict on the three things that must hold.
struct Gate {
    bool nativeOnFoot = false;   // experimental.native_vr_on_foot
    bool journalKnown = false;
    bool onFoot = false;         // the journal says on foot
};
inline bool onFootFlag(const Gate& g) { return g.nativeOnFoot && g.journalKnown && g.onFoot; }

// Distinct HMCs seen by the getters in a window, up to kSlots (more reads "many"). The glue fills an atomic copy of this; the rig a plain one.
// 32: on foot near a settlement the getters see more humanoids than 8 (flight of .35: 8+), and the census must still say how many.
constexpr int kSlots = 32;
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
// were called on it in the same window (`cameraSeen`, flagged by the getters themselves, so a crowd past the census's slots cannot hide it). The single
// way: the only HMC the getters saw. Neither: no player, and why.
enum class Source : int { None = 0, Camera, Single };
inline const char* sourceName(Source s) { return s == Source::Camera ? "camera" : s == Source::Single ? "single" : "none"; }
struct Identity {
    uintptr_t player = 0;
    Source source = Source::None;
    const char* why = "no getter calls";
};
inline Identity identify(uintptr_t cameraHmc, bool cameraSeen, const Census& c) {
    Identity id;
    if (cameraHmc && (cameraSeen || contains(c, cameraHmc))) {
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

// The camera's chain as the frame thread read it this second: did the on-foot camera hand its matrix getter an eye, is that eye's table the eye's
// (+0x5200038), is there an HMC at [eye+0xD0], is ITS table the HMC's (+0x5206AA8), and did the getters see it.
struct CameraCheck {
    bool eye = false, eyeTable = false, hmc = false, hmcTable = false, seen = false;
};
// ---- the frame's beat (the sync counter and the camera answer) -------------------------------------------------------------------------------------

// What the game's camera builds did in one game frame: none, more than one, one before the latch, one after it.
enum class CamClass : int { None = 0, Multi, Before, After };
// `before`/`after`: the camera moments counted before and after this frame's latch. A frame with no latch has no "after" (its moments all count before).
inline CamClass classifyCamera(uint32_t before, uint32_t after, bool latched) {
    if (!latched) { before += after; after = 0; }
    const uint32_t total = before + after;
    if (total == 0) return CamClass::None;
    if (total > 1) return CamClass::Multi;
    return after ? CamClass::After : CamClass::Before;
}
struct SyncWindow {
    uint64_t frames = 0, before = 0, after = 0, none = 0, multi = 0, noLatch = 0;
};
inline void noteSyncFrame(SyncWindow& w, uint32_t before, uint32_t after, bool latched) {
    ++w.frames;
    if (!latched) ++w.noLatch;
    switch (classifyCamera(before, after, latched)) {
        case CamClass::None: ++w.none; break;
        case CamClass::Multi: ++w.multi; break;
        case CamClass::Before: ++w.before; break;
        default: ++w.after; break;
    }
}

// The angles in force at the game's last camera build, and the latch number they came from.
struct Snapshot {
    bool valid = false;
    uint64_t latchNo = 0;
    float yaw = 0.0f, pitch = 0.0f;   // yaw added, and the tilt for the runtime
};
struct Answer {
    float yaw = 0.0f, pitch = 0.0f;
};
// What the latch tells the runtime: the angles in force at the game's last camera build (the snapshot), when it was taken since the previous latch (its
// number is the previous latch's), else the new angles. The getters get the new angles either way.
inline Answer chooseAnswer(const Snapshot& snap, uint64_t prevLatchNo, float newYaw, float newPitch) {
    Answer a;
    if (snap.valid && snap.latchNo == prevLatchNo) { a.yaw = snap.yaw; a.pitch = snap.pitch; }
    else { a.yaw = newYaw; a.pitch = newPitch; }
    return a;
}

// ---- the lines -------------------------------------------------------------------------------------------------------------------------------------

inline int formatKeys(Key k, char* out, size_t cap) { return std::snprintf(out, cap, "head aim: key=%s", keyName(k)); }
// head_aim_sync,frames=..,cam_before=..,cam_after=..,cam_none=..,cam_multi=..,no_latch=..
inline int formatSync(const SyncWindow& w, char* out, size_t cap) {
    return std::snprintf(out, cap, "head_aim_sync,frames=%llu,cam_before=%llu,cam_after=%llu,cam_none=%llu,cam_multi=%llu,no_latch=%llu",
                         (unsigned long long)w.frames, (unsigned long long)w.before, (unsigned long long)w.after, (unsigned long long)w.none,
                         (unsigned long long)w.multi, (unsigned long long)w.noLatch);
}
inline int formatHooked(bool camera, char* out, size_t cap) {
    return std::snprintf(out, cap, "head aim: hooked angle1 +0x1A45930 angle2 +0x1A49040%s",
                         camera ? ", eye matrix +0xF893D0 from the camera at +0x10743CB" : ", camera not hooked: the player is the single HMC only");
}
inline int formatInert(const char* why, char* out, size_t cap) { return std::snprintf(out, cap, "head aim: inert: %s", why ? why : "?"); }
inline int formatIdentity(const Identity& id, int hmcs, bool many, char* out, size_t cap) {
    if (id.source != Source::None)
        return std::snprintf(out, cap, "head aim: player identified by %s, hmcs=%d%s", sourceName(id.source), hmcs, many ? "+" : "");
    return std::snprintf(out, cap, "head aim: player not identified (%s), hmcs=%d%s", id.why, hmcs, many ? "+" : "");
}
inline int formatUnknownKey(const char* which, const char* raw, const char* reads, char* out, size_t cap) {
    return std::snprintf(out, cap, "head aim: %s value '%.40s' not understood, reads %s", which, raw ? raw : "", reads);
}
// On the level's edges: 0 -> above 0 is "on", back to 0 is "off (reason)".
inline int formatEdge(bool on, Reason why, char* out, size_t cap) {
    return on ? std::snprintf(out, cap, "head aim: on") : std::snprintf(out, cap, "head aim: off (%s)", reasonName(why));
}
// When a hold begins (the way out of it is the "on" line).
inline int formatHold(Reason why, char* out, size_t cap) { return std::snprintf(out, cap, "head aim: hold (%s)", reasonName(why)); }

// The 30 s window's counts.
struct Window {
    uint64_t frames = 0, active = 0, capped = 0, held = 0;
    double maxYawDeg = 0, maxPitchDeg = 0, maxLagDeg = 0;
    uint32_t channel = 0;   // the last request version: 8 reaches the feature, 7 or less does not, 0 no runtime asked
    double stickDeg = 0;    // the player's last original angle 1, degrees, the game's sign (+ = down); set by the reader of the window
};
// One latch's account. `pitchAddedRad` is what the game's angle 1 got on top of the original.
inline void noteFrame(Window& w, const Output& o, float pitchAddedRad, uint32_t requestVersion) {
    ++w.frames;
    w.channel = requestVersion;
    if (o.level > 0.0f) {
        ++w.active;
        const double yawDeg = std::fabs(double(o.yawAdded)) * kDegPerRad, pitchDeg = std::fabs(double(pitchAddedRad)) * kDegPerRad;
        if (yawDeg > w.maxYawDeg) w.maxYawDeg = yawDeg;
        if (pitchDeg > w.maxPitchDeg) w.maxPitchDeg = pitchDeg;
        if (o.capped) ++w.capped;
    }
    if (o.held) ++w.held;
    if (o.lagDeg > w.maxLagDeg) w.maxLagDeg = o.lagDeg;
}
// head_aim_feature,frames=..,active=..,max_yaw_deg=..,max_pitch_deg=..,capped=..,max_lag_deg=..,channel=..,stick_deg=..,held=..
inline int formatSummary(const Window& w, char* out, size_t cap) {
    return std::snprintf(out, cap,
                         "head_aim_feature,frames=%llu,active=%llu,max_yaw_deg=%.1f,max_pitch_deg=%.1f,capped=%llu,max_lag_deg=%.1f,channel=%u,stick_deg=%.1f,held=%llu",
                         (unsigned long long)w.frames, (unsigned long long)w.active, w.maxYawDeg, w.maxPitchDeg, (unsigned long long)w.capped,
                         w.maxLagDeg, w.channel, w.stickDeg, (unsigned long long)w.held);
}

}  // namespace ha
}  // namespace edvr
