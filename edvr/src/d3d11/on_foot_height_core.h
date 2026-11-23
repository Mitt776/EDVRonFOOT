#pragma once
// The on-foot camera height, the pure half (EDVRonFOOT; experimental.native_vr_on_foot_height; openspec change on-foot-height, and
// on_foot_height.h for the glue). Everything here is plain C++ the rig (tools\on_foot_height_test) drives directly: how the key's number
// becomes whole centimetres, the target (the key on foot, 0 anywhere else), the ramp toward it, and the text of the key's line.
//
// WHAT IT IS FOR. On foot the game puts its camera at the character's eye point and adds the head's wander from the seated origin; a
// recenter puts that origin in the head. The eye point sits a few centimetres below the NPCs' eyes, so the commander feels shorter than
// everyone else, and no recenter changes it. The runtime half lifts the head pose the GAME gets by the height this half sends
// (src\openxr\on_foot_lift.h, through src\common\on_foot_height_signal.h); the headset's image still comes from the real eyes.
#include <cmath>
#include <cstddef>
#include <cstdio>

namespace edvr {
namespace ofh {

constexpr int kMinCm = -10, kMaxCm = 20;   // the key's range, as its ui: line states it
constexpr float kSpeedMps = 1.0f;          // the ramp: the whole 30 cm in 0.3 s, the usual 5-10 cm in 0.05-0.1 s
constexpr float kMaxDtS = 0.05f;           // a frame's step is at most this long: a pause or a loading hitch moves 5 cm, never the lot

// The key's value (Config::getFloat: empty, missing and junk already read as 0) as whole centimetres: not finite reads 0, a fraction
// rounds to the nearest (4.6 -> 5, -4.5 -> -5), and past the range clamps to its near end.
inline int cmFromValue(float value) {
    if (!std::isfinite(value)) return 0;
    if (value >= float(kMaxCm)) return kMaxCm;
    if (value <= float(kMinCm)) return kMinCm;
    const long cm = std::lround(double(value));
    return cm > kMaxCm ? kMaxCm : cm < kMinCm ? kMinCm : int(cm);
}

struct TargetIn {
    int cm = 0;                 // the key, whole cm
    bool nativeOnFoot = true;   // experimental.native_vr_on_foot
    bool journalKnown = false;  // the journal has said on foot or not
    bool onFoot = false;        // ...and it says on foot
    bool mainMenu = false;      // the journal's main menu after gameplay (Status.json still says on foot there for ~35 s)
};
// The height to head for, metres: the key while on foot (native on foot on, the journal says on foot, no main menu), 0 anywhere else --
// the ship, the SRV, a taxi, the cinema, the main menu.
inline bool onFootFlag(const TargetIn& in) { return in.nativeOnFoot && in.journalKnown && in.onFoot && !in.mainMenu; }
inline float targetM(const TargetIn& in) { return onFootFlag(in) ? float(in.cm) / 100.0f : 0.0f; }

// One frame of the ramp: `current` moves toward `target` at kSpeedMps for dtSeconds (held to 0..kMaxDtS), never past it.
inline float step(float current, float target, float dtSeconds) {
    if (!std::isfinite(current)) current = 0.0f;
    if (!(dtSeconds > 0.0f)) return current;
    if (dtSeconds > kMaxDtS) dtSeconds = kMaxDtS;
    const float move = kSpeedMps * dtSeconds;
    if (target > current) return current + move >= target ? target : current + move;
    if (target < current) return current - move <= target ? target : current - move;
    return current;
}

// The key's line, at start and at each change of it: "on-foot height +7 cm", "on-foot height -3 cm", "on-foot height 0 cm".
inline int formatLine(int cm, char* out, size_t cap) {
    return cm > 0 ? std::snprintf(out, cap, "on-foot height +%d cm", cm) : std::snprintf(out, cap, "on-foot height %d cm", cm);
}

}  // namespace ofh
}  // namespace edvr
