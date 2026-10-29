// Build gate for the EDVRonFOOT on-foot aiming eye (src\d3d11\on_foot_aim_eye_core.h, experimental.native_vr_on_foot_aim_eye).
//
// Drives the pure half: the key's reading (right, left, off, empty, junk such as "2", case and padding), the target (1 only on foot with
// LT held, native_vr_on_foot on, the journal knowing and the key not off), the blend's ramp (0.12 s up and 0.12 s down at 16 ms frames, a
// re-press from the middle continuing without a jump, a pause's huge dt moving one small step), the eye reported (the key's; kept while
// the blend ramps out after the key went off), and the key line's text. Nothing here touches the clock, the pad, the journal or a file,
// so --dry-run is the same run as --self-test.
#include "../../src/d3d11/on_foot_aim_eye_core.h"
#include <cmath>
#include <cstdio>
#include <cstring>

using namespace edvr::oae;

namespace {
int g_checks = 0, g_failures = 0;
void check(bool ok, const char* what) {
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::printf("FAIL: %s\n", what);
    }
}
bool near(float a, float b, float eps = 1e-5f) { return std::fabs(a - b) < eps; }

constexpr float kFrame = 0.016f;  // a 16 ms frame

TargetIn aiming(Key key = Key::Right) {
    TargetIn in;
    in.key = key;
    in.nativeOnFoot = true;
    in.journalKnown = true;
    in.onFoot = true;
    in.lt = true;
    return in;
}

// Frames of dt until the blend reaches `goal` (target `t`), at most `cap`; the count, or -1 if it never did.
int framesTo(Ramp& r, Key key, float t, float dt, float goal, int cap = 100) {
    for (int i = 1; i <= cap; ++i) {
        const Out o = step(r, key, t, dt);
        if (near(o.blend, goal, 1e-6f)) return i;
    }
    return -1;
}

void partKey() {
    bool valid = false;
    check(parseKey("right", &valid) == Key::Right && valid, "right is right");
    check(parseKey("left", &valid) == Key::Left && valid, "left is left");
    check(parseKey("off", &valid) == Key::Off && valid, "off is off");
    check(parseKey("", &valid) == Key::Right && valid, "empty is right, and not an error");
    check(parseKey(nullptr, &valid) == Key::Right && valid, "a missing key is right");
    check(parseKey("   ", &valid) == Key::Right && valid, "blanks are empty: right");
    check(parseKey("2", &valid) == Key::Right && !valid, "the junk value 2 reads as right and is not valid");
    check(parseKey("both", &valid) == Key::Right && !valid, "an unknown word reads as right");
    check(parseKey("lefty", &valid) == Key::Right && !valid, "a longer word starting with left is not left");
    check(parseKey("lef", &valid) == Key::Right && !valid, "a shorter word is not left");
    check(parseKey("LEFT", &valid) == Key::Left && valid, "case does not matter: LEFT");
    check(parseKey("Off", &valid) == Key::Off && valid, "case does not matter: Off");
    check(parseKey("  left \t\r\n", &valid) == Key::Left && valid, "padding and a line end are trimmed");
    check(parseKey("le ft", &valid) == Key::Right && !valid, "a space inside is junk");
    check(parseKey("left") == Key::Left, "the valid pointer is optional");
    check(std::strcmp(keyName(Key::Right), "right") == 0 && std::strcmp(keyName(Key::Left), "left") == 0 &&
              std::strcmp(keyName(Key::Off), "off") == 0,
          "the key names");
    check(eyeOf(Key::Left) == kEyeLeft && eyeOf(Key::Right) == kEyeRight && eyeOf(Key::Off) == kEyeNone,
          "the eyes the keys name (1 left, 2 right, 0 none: aim_eye_signal.h)");
}

void partTarget() {
    TargetIn in = aiming();
    check(target(in) == 1.0f && onFootFlag(in), "on foot, LT held, key right: aiming");
    in = aiming(Key::Left);
    check(target(in) == 1.0f, "key left: aiming");
    in = aiming(Key::Off);
    check(target(in) == 0.0f && onFootFlag(in), "key off: no aiming, though on foot");
    in = aiming();
    in.onFoot = false;
    check(target(in) == 0.0f && !onFootFlag(in), "in the ship (the journal says not on foot): no aiming");
    in = aiming();
    in.nativeOnFoot = false;
    check(target(in) == 0.0f && !onFootFlag(in), "the cinema (native_vr_on_foot off): no aiming, not on foot for the shift");
    in = aiming();
    in.journalKnown = false;
    check(target(in) == 0.0f && !onFootFlag(in), "the journal does not know: no aiming");
    in = aiming();
    in.journalKnown = false;
    in.onFoot = true;  // a stale answer behind an unknown journal must not count
    check(target(in) == 0.0f, "an unknown journal reads as not on foot even when onFoot is left true");
    in = aiming();
    in.lt = false;
    check(target(in) == 0.0f && onFootFlag(in), "no LT: no aiming, but still on foot");
    check(target(TargetIn{}) == 0.0f, "the default input aims at nothing");
}

void partRamp() {
    // Up: 0.12 s at 16 ms frames is 7.5 frames, so 8.
    Ramp r;
    int up = framesTo(r, Key::Right, 1.0f, kFrame, 1.0f);
    check(up >= 7 && up <= 8, "0 to 1 takes 7 or 8 frames of 16 ms (0.12 s)");
    check(up == 8, "...and 8 exactly at 16 ms");
    // One frame short of that is not yet 1.
    Ramp r2;
    for (int i = 0; i < 7; ++i) step(r2, Key::Right, 1.0f, kFrame);
    check(r2.blend > 0.9f && r2.blend < 1.0f, "after 7 frames (0.112 s) the blend is close but not 1");
    // Down.
    int down = framesTo(r, Key::Right, 0.0f, kFrame, 0.0f);
    check(down >= 7 && down <= 8, "1 to 0 takes 7 or 8 frames of 16 ms (0.12 s)");
    // Linear: equal steps.
    Ramp r3;
    const float a = step(r3, Key::Right, 1.0f, kFrame).blend;
    const float b = step(r3, Key::Right, 1.0f, kFrame).blend;
    const float c = step(r3, Key::Right, 1.0f, kFrame).blend;
    check(near(a, kFrame / kRampSeconds) && near(b - a, a) && near(c - b, a), "the ramp is linear: the same step every frame");
    // The step follows the real time between frames: 60 frames of 2 ms is also 0.12 s.
    Ramp r4;
    int fine = framesTo(r4, Key::Right, 1.0f, 0.002f, 1.0f, 200);
    check(fine >= 60 && fine <= 61, "0.002 s frames take 60 of them: the ramp is in time, not in frames");
    // A frame of dt 0 (or a backwards clock, or NaN) moves nothing.
    Ramp r5;
    r5.blend = 0.5f;
    check(near(step(r5, Key::Right, 1.0f, 0.0f).blend, 0.5f), "dt 0 moves nothing");
    check(near(step(r5, Key::Right, 1.0f, -1.0f).blend, 0.5f), "a negative dt moves nothing");
    check(near(step(r5, Key::Right, 1.0f, std::nanf("")).blend, 0.5f), "a NaN dt moves nothing");
    // Holding at the target stays there.
    Ramp r6;
    r6.blend = 1.0f;
    for (int i = 0; i < 20; ++i) step(r6, Key::Right, 1.0f, kFrame);
    check(r6.blend == 1.0f, "held at 1 stays at 1 (clamped)");
    Ramp r7;
    for (int i = 0; i < 20; ++i) step(r7, Key::Right, 0.0f, kFrame);
    check(r7.blend == 0.0f, "idle at 0 stays at 0");
}

void partRepress() {
    // Press for 4 frames (to the middle), release for 2, press again: it goes on from where it stands.
    Ramp r;
    float last = 0.0f;
    for (int i = 0; i < 4; ++i) last = step(r, Key::Right, 1.0f, kFrame).blend;
    check(last > 0.4f && last < 0.6f, "four frames up: about the middle");
    const float peak = last;
    for (int i = 0; i < 2; ++i) last = step(r, Key::Right, 0.0f, kFrame).blend;
    check(last < peak && last > 0.0f, "released in the middle: on its way down");
    const float low = last;
    const float again = step(r, Key::Right, 1.0f, kFrame).blend;
    check(near(again - low, kFrame / kRampSeconds), "pressed again from the middle: one ordinary step up from where it was, no jump");
    // And a reversal at the very frame of the press down from 1: one step down.
    Ramp full;
    full.blend = 1.0f;
    check(near(step(full, Key::Right, 0.0f, kFrame).blend, 1.0f - kFrame / kRampSeconds), "release from 1: one ordinary step down");
    // Rapid presses never leave [0, 1] and never move more than one step a frame.
    Ramp fast;
    float prev = 0.0f;
    bool ok = true;
    for (int i = 0; i < 400; ++i) {
        const float t = (i / 3) % 2 ? 0.0f : 1.0f;  // a press every 3 frames
        const float b = step(fast, Key::Right, t, kFrame).blend;
        if (b < 0.0f || b > 1.0f || std::fabs(b - prev) > kFrame / kRampSeconds + 1e-5f) ok = false;
        prev = b;
    }
    check(ok, "rapid presses: always in [0, 1], never more than one step a frame");
}

void partPause() {
    // A pause or a load: dt of seconds. It advances at most one small step.
    Ramp r;
    const float one = step(r, Key::Right, 1.0f, 5.0f).blend;
    check(one > 0.0f && near(one, kMaxDtSeconds / kRampSeconds), "a 5 s dt moves one clamped step (1/30 s of ramp), not to 1");
    check(one < 0.5f, "...which is a small step");
    Ramp r2;
    r2.blend = 1.0f;
    const float down = step(r2, Key::Right, 0.0f, 100.0f).blend;
    check(near(down, 1.0f - kMaxDtSeconds / kRampSeconds) && down > 0.5f, "a 100 s dt down: one clamped step, not to 0");
    Ramp r3;
    r3.blend = 0.9f;
    check(step(r3, Key::Right, 1.0f, 1e9f).blend <= 1.0f, "an absurd dt never overshoots 1");
    check(near(kMaxDtSeconds, 1.0f / 30.0f), "the clamp is 1/30 s");
    check(near(kRampSeconds, 0.12f), "the ramp is 0.12 s");
}

void partEye() {
    Ramp r;
    Out o = step(r, Key::Right, 0.0f, kFrame);
    check(o.eye == kEyeRight && o.blend == 0.0f, "key right: the right eye is reported, blend 0");
    o = step(r, Key::Left, 0.0f, kFrame);
    check(o.eye == kEyeLeft, "key left: the left eye");
    // The key changes while the blend is above 0: the eye is the current key's.
    Ramp mid;
    for (int i = 0; i < 4; ++i) step(mid, Key::Right, 1.0f, kFrame);
    o = step(mid, Key::Left, 1.0f, kFrame);
    check(o.eye == kEyeLeft && o.blend > 0.5f, "the key changes from right to left mid-ramp: the eye follows the key at once");
    // Off from the start: no eye.
    Ramp none;
    o = step(none, Key::Off, 0.0f, kFrame);
    check(o.eye == kEyeNone && o.blend == 0.0f, "key off, never aimed: no eye");
    // The key goes off while aiming: the last eye is kept while the blend ramps out, then none.
    Ramp out;
    for (int i = 0; i < 20; ++i) step(out, Key::Left, 1.0f, kFrame);
    check(out.blend == 1.0f, "aimed to full on the left");
    bool kept = true;
    int frames = 0;
    for (; frames < 100; ++frames) {
        o = step(out, Key::Off, 0.0f, kFrame);  // the key off makes the target 0
        if (o.blend > 0.0f) {
            if (o.eye != kEyeLeft) kept = false;
        } else {
            break;
        }
    }
    check(kept, "key off mid-aim: the last eye (left) is reported while the blend ramps out");
    check(frames >= 7 && frames <= 8 && o.blend == 0.0f && o.eye == kEyeNone,
          "...and when the blend is back at 0 the eye is none (in 7 or 8 frames)");
    // And it stays none afterwards.
    o = step(out, Key::Off, 0.0f, kFrame);
    check(o.eye == kEyeNone, "stays none with the key off");
    // Key back on: the eye returns.
    o = step(out, Key::Right, 0.0f, kFrame);
    check(o.eye == kEyeRight, "key back on: the right eye is reported again");
}

void partWhole() {
    // The whole chain on a short session: ship, step off, aim twice, back to the ship.
    Ramp r;
    TargetIn in = aiming();
    in.onFoot = false;
    for (int i = 0; i < 10; ++i) check(step(r, in.key, target(in), kFrame).blend == 0.0f, "in the ship LT shifts nothing");
    in.onFoot = true;
    in.lt = false;
    for (int i = 0; i < 10; ++i) step(r, in.key, target(in), kFrame);
    check(r.blend == 0.0f, "on foot without LT: nothing");
    in.lt = true;
    for (int i = 0; i < 10; ++i) step(r, in.key, target(in), kFrame);
    check(r.blend == 1.0f, "LT held on foot: full after 10 frames");
    in.onFoot = false;  // boarding while LT is held: ramps out smoothly, not a jump
    const float b = step(r, in.key, target(in), kFrame).blend;
    check(near(b, 1.0f - kFrame / kRampSeconds), "the commander stops being on foot: the shift ramps out, one step at a time");
    in.journalKnown = false;
    in.onFoot = true;
    const float b2 = step(r, in.key, target(in), kFrame).blend;
    check(b2 < b, "the journal forgetting: also ramps out");
}

void partLines() {
    char line[64];
    formatKey(Key::Right, line, sizeof(line));
    check(std::strcmp(line, "aim_eye_key,value=right") == 0, "the key line, right");
    formatKey(Key::Left, line, sizeof(line));
    check(std::strcmp(line, "aim_eye_key,value=left") == 0, "the key line, left");
    formatKey(Key::Off, line, sizeof(line));
    check(std::strcmp(line, "aim_eye_key,value=off") == 0, "the key line, off");
    bool valid = true;
    formatKey(parseKey("2", &valid), line, sizeof(line));
    check(std::strcmp(line, "aim_eye_key,value=right") == 0, "the junk key's line says right");
}
}  // namespace

int main(int argc, char** argv) {
    if (argc < 2 || (std::strcmp(argv[1], "--self-test") != 0 && std::strcmp(argv[1], "--dry-run") != 0)) {
        std::printf("usage: on_foot_aim_eye_test --self-test | --dry-run (the pure half only; --dry-run writes nothing, as does --self-test)\n");
        return 2;
    }
    partKey();
    partTarget();
    partRamp();
    partRepress();
    partPause();
    partEye();
    partWhole();
    partLines();
    std::printf("%s: on_foot_aim_eye_test, %d checks, %d failed\n", g_failures ? "FAIL" : "PASS", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
