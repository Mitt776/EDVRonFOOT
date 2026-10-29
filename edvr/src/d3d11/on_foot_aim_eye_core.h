#pragma once
// The on-foot aiming eye, the pure half (EDVRonFOOT; experimental.native_vr_on_foot_aim_eye; openspec change on-foot-ads-dominant-eye,
// and on_foot_aim_eye.h for the glue). Everything here is plain C++ the rig (tools\on_foot_aim_eye_test) drives directly: how the key is
// read, whether the commander is aiming this frame (the target, 0 or 1), the blend's ramp toward it, which eye is reported, and the text
// of the key's line.
//
// WHAT IT IS FOR. On foot in stereo the game puts the weapon's iron sights on the line from the middle of the head. A headset wearer
// aims with one eye, 30 mm to the side of it. While LT is held the runtime half shifts the head pose the game gets by the dominant eye's
// offset (src\openxr\aim_eye_shift.h), so that eye sits on the sights. The runtime knows the geometry; it does not know whether LT is
// held or whether the commander is on foot, and it must not jump -- so this half decides and smooths, once a frame, and hands the
// runtime an eye and a blend 0..1 (src\common\aim_eye_signal.h).
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace edvr {
namespace oae {

// experimental.native_vr_on_foot_aim_eye: right (the default; also when empty and for anything unknown), left, off.
enum class Key : int { Right = 0, Left = 1, Off = 2 };
inline const char* keyName(Key k) { return k == Key::Left ? "left" : k == Key::Off ? "off" : "right"; }
// The eye the signal names (aim_eye_signal.h's kEyeNone/kEyeLeft/kEyeRight: 0, 1, 2).
constexpr uint32_t kEyeNone = 0, kEyeLeft = 1, kEyeRight = 2;
inline uint32_t eyeOf(Key k) { return k == Key::Left ? kEyeLeft : k == Key::Right ? kEyeRight : kEyeNone; }

// Case-insensitive, trimmed. *valid is false for a value that is none of the three (the empty one is the default, so it is valid).
inline Key parseKey(const char* text, bool* valid = nullptr) {
    if (valid) *valid = true;
    if (!text) return Key::Right;
    while (*text == ' ' || *text == '\t') ++text;
    size_t n = std::strlen(text);
    while (n && (text[n - 1] == ' ' || text[n - 1] == '\t' || text[n - 1] == '\r' || text[n - 1] == '\n')) --n;
    if (!n) return Key::Right;
    auto is = [&](const char* word) {
        if (std::strlen(word) != n) return false;
        for (size_t i = 0; i < n; ++i) {
            char c = text[i];
            if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
            if (c != word[i]) return false;
        }
        return true;
    };
    if (is("right")) return Key::Right;
    if (is("left")) return Key::Left;
    if (is("off")) return Key::Off;
    if (valid) *valid = false;
    return Key::Right;
}

// The target: 1 only while the commander is aiming on foot, else 0. Aiming is LT held (the game's HumanoidZoomButton on this pad); the
// journal must know the commander is on foot (an unknown answer reads as not on foot), and native VR on foot must be on -- in the cinema
// there is no head camera to shift.
struct TargetIn {
    Key key = Key::Right;
    bool nativeOnFoot = true;   // experimental.native_vr_on_foot
    bool journalKnown = false;  // journalOnFootKnown()
    bool onFoot = false;        // journalOnFoot()
    bool lt = false;            // the left trigger is held
};
// On foot as the shift sees it: the runtime half reads this as the signal's onFoot flag (it counts the frames the commander was on foot).
inline bool onFootFlag(const TargetIn& in) { return in.nativeOnFoot && in.journalKnown && in.onFoot; }
inline float target(const TargetIn& in) { return in.key != Key::Off && onFootFlag(in) && in.lt ? 1.0f : 0.0f; }

// The ramp: a full shift takes kRampSeconds, in either direction, from wherever the blend stands. The frame's dt is the real time
// between frames, clamped to kMaxDtSeconds: a pause, a load or a hitch advances the blend by one small step and never jumps it.
constexpr float kRampSeconds = 0.12f;
constexpr float kMaxDtSeconds = 1.0f / 30.0f;

struct Ramp {
    float blend = 0.0f;         // 0..1
    uint32_t lastEye = kEyeNone;  // the last eye the key named; kept while the blend ramps out after the key went off
};
struct Out {
    uint32_t eye = kEyeNone;
    float blend = 0.0f;
};

// Once a frame. `dtSeconds` is the real time since the last step (0 or negative: no movement; NaN is read as 0).
inline Out step(Ramp& r, Key key, float target01, float dtSeconds) {
    float dt = dtSeconds;
    if (!(dt > 0.0f)) dt = 0.0f;
    if (dt > kMaxDtSeconds) dt = kMaxDtSeconds;
    const float move = dt / kRampSeconds;
    if (target01 > r.blend) {
        r.blend += move;
        if (r.blend > target01) r.blend = target01;
    } else if (target01 < r.blend) {
        r.blend -= move;
        if (r.blend < target01) r.blend = target01;
    }
    if (!(r.blend > 0.0f)) r.blend = 0.0f;
    if (r.blend > 1.0f) r.blend = 1.0f;
    // The eye is the key's. With the key off the last eye is kept until the blend is back at 0, so the ramp-out is smooth.
    if (key != Key::Off) r.lastEye = eyeOf(key);
    else if (r.blend <= 0.0f) r.lastEye = kEyeNone;
    Out o;
    o.eye = r.lastEye;
    o.blend = r.blend;
    return o;
}

// ---- the line ----

// Once at the start and on every change of the key.
inline int formatKey(Key key, char* out, size_t cap) { return std::snprintf(out, cap, "aim_eye_key,value=%s", keyName(key)); }

}  // namespace oae
}  // namespace edvr
