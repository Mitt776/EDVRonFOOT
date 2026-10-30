// Build gate for the EDVRonFOOT on-foot head aim probe (src\d3d11\head_aim_probe_core.h, experimental.native_vr_on_foot_head_aim_probe and
// _compensate; temporary, openspec change on-foot-head-aim-probe).
//
// Drives the pure half: both keys' reading (every value, empty, junk, case and padding), the gate (the ship, native_vr_on_foot off, no hooks, no
// player), the ramp (0.5 s up and down at 16 ms frames, an angle eased out before the other eases in, a pause's huge dt one small step), the sway
// (period, bounds, no jump between frames), the turn by the compensation's sign, who the player is (the camera's HMC seen by the getters, the single
// HMC, several, none), the per-frame account, and the text of every line. Nothing here touches the clock, the game, the journal or a file, so
// --dry-run is the same run as --self-test.
#include "../../src/d3d11/head_aim_probe_core.h"
#include <cmath>
#include <cstdio>
#include <cstring>

using namespace edvr::hap;

namespace {
int g_checks = 0, g_failures = 0;
void check(bool ok, const char* what) {
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::printf("FAIL: %s\n", what);
    }
}
bool near(double a, double b, double eps = 1e-5) { return std::fabs(a - b) < eps; }
constexpr float kFrame = 0.016f;

Gate everything() {
    Gate g;
    g.nativeOnFoot = g.journalKnown = g.onFoot = g.installed = g.player = true;
    return g;
}

void partKeys() {
    bool v = false;
    check(parseMode(nullptr, &v) == Mode::Off && v, "no mode key is off");
    check(parseMode("", &v) == Mode::Off && v, "an empty mode is off");
    check(parseMode("off", &v) == Mode::Off && v, "off");
    check(parseMode("watch", &v) == Mode::Watch && v, "watch");
    check(parseMode("angle1", &v) == Mode::Angle1 && v, "angle1");
    check(parseMode("angle2", &v) == Mode::Angle2 && v, "angle2");
    check(parseMode("angle1-sway", &v) == Mode::Angle1Sway && v, "angle1-sway");
    check(parseMode("angle2-sway", &v) == Mode::Angle2Sway && v, "angle2-sway");
    check(parseMode("  Angle1-SWAY \r\n", &v) == Mode::Angle1Sway && v, "case and padding");
    check(parseMode("angle3", &v) == Mode::Off && !v, "angle3 reads off and is reported");
    check(parseMode("1", &v) == Mode::Off && !v, "junk reads off and is reported");
    check(parseComp(nullptr, &v) == Comp::Off && v, "no compensation key is off");
    check(parseComp("minus", &v) == Comp::Minus && v, "minus");
    check(parseComp(" PLUS ", &v) == Comp::Plus && v, "plus");
    check(parseComp("both", &v) == Comp::Off && !v, "junk compensation reads off and is reported");
    check(angleOf(Mode::Watch) == 0 && angleOf(Mode::Angle1) == 1 && angleOf(Mode::Angle2Sway) == 2, "the angle of each mode");
    check(!swayOf(Mode::Angle1) && swayOf(Mode::Angle1Sway), "the sway of each mode");
}

void partGate() {
    Gate g = everything();
    check(shiftAllowed(g), "everything holds: the shift is allowed");
    g.onFoot = false;
    check(!shiftAllowed(g) && !onFootFlag(g), "in the ship: no shift, not on foot");
    g = everything(); g.nativeOnFoot = false;
    check(!shiftAllowed(g), "native_vr_on_foot off: no shift");
    g = everything(); g.journalKnown = false;
    check(!shiftAllowed(g), "the journal not knowing: no shift");
    g = everything(); g.installed = false;
    check(!shiftAllowed(g) && onFootFlag(g), "no hooks: no shift, still on foot");
    g = everything(); g.player = false;
    check(!shiftAllowed(g), "no player: no shift");
}

// Frames until the ramp's level reaches `goal`, at most `cap`; -1 if never.
int framesTo(Ramp& r, Mode m, bool allowed, float goal, int cap = 200) {
    for (int i = 1; i <= cap; ++i) {
        const Shift s = step(r, m, allowed, kFrame, 0.0);
        if (goal >= 1.0f ? s.level >= 1.0f : s.level <= 0.0f) return i;
    }
    return -1;
}

void partRamp() {
    Ramp r;
    const Shift first = step(r, Mode::Angle1, true, kFrame, 0.0);
    check(first.angle == 1 && near(first.level, kFrame / kRampSeconds) && near(first.rad, kShiftRad * kFrame / kRampSeconds, 1e-6),
          "the first frame moves one step of the ramp, angle 1");
    const int up = framesTo(r, Mode::Angle1, true, 1.0f);
    check(up >= 29 && up <= 32, "the rest of the rise takes about 0.5 s");
    const Shift full = step(r, Mode::Angle1, true, kFrame, 0.0);
    check(full.angle == 1 && near(full.rad, kShiftRad), "full: exactly 10 degrees");
    check(near(double(kShiftRad) * kDegPerRad, 10.0, 1e-4), "kShiftRad is 10 degrees");
    const int down = framesTo(r, Mode::Off, true, 0.0f);
    check(down >= 30 && down <= 33, "off eases it out in about 0.5 s");
    check(step(r, Mode::Off, true, kFrame, 0.0).angle == 0, "then nothing is shifted");

    // Not allowed (the ship, no player): eases out, never in.
    Ramp n;
    check(step(n, Mode::Angle2, false, kFrame, 0.0).angle == 0, "not allowed: nothing from zero");
    Ramp m;
    framesTo(m, Mode::Angle2, true, 1.0f);
    const Shift leaving = step(m, Mode::Angle2, false, kFrame, 0.0);
    check(leaving.angle == 2 && leaving.level < 1.0f && leaving.level > 0.9f, "allowed lost mid-shift: it eases out, it does not drop");

    // angle1 -> angle2: angle 1 eases out first, then angle 2 eases in; never both, never a jump.
    Ramp s;
    framesTo(s, Mode::Angle1, true, 1.0f);
    float last = 1.0f;
    bool sawTwo = false, jumped = false, oneAfterTwo = false;
    for (int i = 0; i < 100; ++i) {
        const Shift x = step(s, Mode::Angle2, true, kFrame, 0.0);
        if (std::fabs(x.level - last) > kFrame / kRampSeconds + 1e-5f) jumped = true;
        last = x.level;
        if (x.angle == 2) sawTwo = true;
        if (sawTwo && x.angle == 1) oneAfterTwo = true;
    }
    check(sawTwo && !jumped && !oneAfterTwo && s.angle == 2 && s.level >= 1.0f, "switching angles: one eases out, the other eases in, no jump");

    // A pause: a 5 s dt moves at most kMaxDtSeconds.
    Ramp p;
    const Shift paused = step(p, Mode::Angle1, true, 5.0f, 0.0);
    check(near(paused.level, kMaxDtSeconds / kRampSeconds), "a pause's huge dt moves one small step");
    Ramp q;
    const Shift nan = step(q, Mode::Angle1, true, std::nanf(""), 0.0);
    check(nan.level == 0.0f && nan.angle == 0, "a dt that is not a number moves nothing");
}

void partSway() {
    Ramp r;
    framesTo(r, Mode::Angle1Sway, true, 1.0f);
    double lo = 1, hi = -1;
    float prev = step(r, Mode::Angle1Sway, true, kFrame, 0.0).rad;
    bool jump = false;
    for (int i = 1; i <= 500; ++i) {
        const Shift x = step(r, Mode::Angle1Sway, true, kFrame, i * 0.016);
        if (x.rad < lo) lo = x.rad;
        if (x.rad > hi) hi = x.rad;
        if (std::fabs(x.rad - prev) > 0.01f) jump = true;   // 2*pi/4 * 0.1745 * 0.016 = 0.0044 a frame at most
        prev = x.rad;
    }
    check(near(hi, kShiftRad, 1e-3) && near(lo, -kShiftRad, 1e-3), "the sway reaches +10 and -10 degrees");
    check(!jump, "the sway moves without a jump between frames");
    check(near(step(r, Mode::Angle1Sway, true, kFrame, 1.0).rad, kShiftRad, 1e-5), "a quarter period: +10");
    check(near(step(r, Mode::Angle1Sway, true, kFrame, 3.0).rad, -kShiftRad, 1e-5), "three quarters: -10");
    check(near(step(r, Mode::Angle1Sway, true, kFrame, 4.0).rad, 0.0, 1e-5), "a full period: back to 0");
    check(step(r, Mode::Angle1Sway, true, kFrame, 1.0).angle == 1, "the sway shifts its own angle");
}

void partTurn() {
    Shift s;
    s.angle = 1;
    s.rad = 0.1f;
    check(turnOf(Comp::Off, s) == 0.0f, "compensation off: no turn");
    check(turnOf(Comp::Minus, s) == -0.1f, "minus: minus the shift");
    check(turnOf(Comp::Plus, s) == 0.1f, "plus: the shift");
    s.angle = 0;
    check(turnOf(Comp::Minus, s) == 0.0f, "nothing shifted: no turn");
}

void partIdentity() {
    Census c;
    check(identify(0, false, c).source == Source::None && std::strcmp(identify(0, false, c).why, "no getter calls") == 0, "no calls: no player");
    note(c, 0x1000);
    note(c, 0x1000);
    check(countOf(c) == 1 && !c.many, "the same HMC twice counts once");
    Identity id = identify(0, false, c);
    check(id.source == Source::Single && id.player == 0x1000, "one HMC: it is the player");
    id = identify(0x1000, false, c);
    check(id.source == Source::Camera && id.player == 0x1000, "the camera's HMC seen by the getters: the player, by the camera");
    note(c, 0x2000);
    check(identify(0, false, c).source == Source::None, "two HMCs and no camera: no player");
    check(identify(0x2000, false, c).source == Source::Camera && identify(0x2000, false, c).player == 0x2000, "two HMCs, the camera names one: that one");
    check(identify(0x3000, false, c).source == Source::None, "the camera's HMC not among the getters': no player");
    Census full;
    for (uintptr_t i = 1; i <= uintptr_t(kSlots) + 1; ++i) note(full, i * 0x100);
    check(countOf(full) == kSlots && full.many, "more than the slots: many");
    const uintptr_t beyond = (uintptr_t(kSlots) + 1) * 0x100;
    check(identify(beyond, false, full).source == Source::None, "the camera's HMC beyond the slots, not flagged by the getters: not confirmed");
    check(identify(beyond, true, full).source == Source::Camera && identify(beyond, true, full).player == beyond,
          "...but flagged by the getters themselves it is the player, however big the crowd (flight of .35: 8+ HMCs)");
    check(identify(0x3000, true, c).source == Source::Camera, "the getters' own flag is enough without the census");
    Census zero;
    note(zero, 0);
    check(countOf(zero) == 0, "a null HMC is not counted");
}

void partPerFrame() {
    Window w;
    notePerFrame(w, 2);
    notePerFrame(w, 1);
    notePerFrame(w, 0);
    notePerFrame(w, 3);
    check(w.perFrameMin == 0 && w.perFrameMax == 3 && w.zeroFrames == 1, "per frame: min, max and the frames with no call");
}

void partLines() {
    char line[512];
    formatKeys(Mode::Angle1Sway, Comp::Minus, line, sizeof(line));
    check(std::strcmp(line, "head aim probe: key=angle1-sway compensate=minus") == 0, "the keys line");
    formatUnknownKey("native_vr_on_foot_head_aim_probe", "angle3", line, sizeof(line));
    check(std::strcmp(line, "head aim probe: native_vr_on_foot_head_aim_probe value 'angle3' not understood, reads off") == 0, "the unknown key line");
    formatInert("the angle getters or their table entries are not 334925's", line, sizeof(line));
    check(std::strcmp(line, "head aim probe: inert: the angle getters or their table entries are not 334925's") == 0, "the inert line");
    formatHooked(true, line, sizeof(line));
    check(std::strstr(line, "head aim probe: hooked angle1 +0x1A45930 angle2 +0x1A49040, eye matrix") == line, "the hooked line, with the camera");
    formatHooked(false, line, sizeof(line));
    check(std::strstr(line, "camera not hooked") != nullptr, "the hooked line, without the camera");
    Identity id;
    id.source = Source::Camera;
    id.player = 1;
    formatIdentity(id, 3, false, line, sizeof(line));
    check(std::strcmp(line, "head aim probe: player identified by camera, hmcs=3") == 0, "the identified line");
    Census two;
    note(two, 1);
    note(two, 2);
    formatIdentity(identify(0, false, two), 2, false, line, sizeof(line));
    check(std::strcmp(line, "head aim probe: player not identified (several HMCs and no camera), hmcs=2") == 0, "the not identified line");
    formatShiftEdge(true, Mode::Angle1, Comp::Plus, line, sizeof(line));
    check(std::strcmp(line, "head aim probe: shift on, angle1, compensate=plus") == 0, "the shift on line");
    formatShiftEdge(false, Mode::Angle1, Comp::Plus, line, sizeof(line));
    check(std::strcmp(line, "head aim probe: shift off") == 0, "the shift off line");

    Fields f;
    f.read = true;
    f.f6f0 = 1.0f;
    f.f710 = 0.5f;
    f.f70c = 0.25f;
    f.f6fc = 0.25f;
    formatWatch(f, kShiftRad, 0.0f, 1, false, true, line, sizeof(line));
    char want[512];
    std::snprintf(want, sizeof(want), "head_aim_watch,a1=%.2f,a1_out=%.2f,a2=%.2f,a2_out=%.2f,f6f0=1.0000,f6fc=0.2500,f70c=0.2500,f710=0.5000,"
                  "f748=0.0000,f754=0.0000,f764=0.0000,f768=0.0000,hmcs=1,player=yes,src=player",
                  0.5 * kDegPerRad, 0.5 * kDegPerRad + 10.0, 0.5 * kDegPerRad, 0.5 * kDegPerRad);
    check(std::strcmp(line, want) == 0, "the watch line: both angles in degrees as built and as handed on, the fields raw");
    Fields unread;
    formatWatch(unread, 0.0f, 0.0f, 9, true, false, line, sizeof(line));
    check(std::strcmp(line, "head_aim_watch,a1=nan,a1_out=nan,a2=nan,a2_out=nan,fields=unread,hmcs=9+,player=no,src=none") == 0, "the watch line, unread");
    CameraCheck cam;
    char tail[160];
    formatCamera(cam, tail, sizeof(tail));
    check(std::strcmp(tail, ",cam_eye=no,eye_table=none,cam_hmc=no,hmc_table=none,cam_seen=no") == 0, "the camera part, nothing");
    cam.eye = cam.eyeTable = cam.hmc = true;
    formatCamera(cam, tail, sizeof(tail));
    check(std::strcmp(tail, ",cam_eye=yes,eye_table=ok,cam_hmc=yes,hmc_table=bad,cam_seen=no") == 0, "the camera part, an HMC whose table is not the HMC's");
    formatWatch(f, 0.0f, 0.0f, 32, true, false, line, sizeof(line), "camera", tail);
    check(std::strstr(line, ",hmcs=32+,player=no,src=camera,cam_eye=yes,eye_table=ok,") != nullptr, "the watch line names its source and carries the camera part");

    Window w;
    formatSummary(w, line, sizeof(line));
    check(std::strcmp(line, "head_aim_summary,calls1=0,calls2=0,latches=0,shifted=0,max_shift_deg=0.0,max_turn_deg=0.0,per_frame=0..0,"
                            "zero_frames=0") == 0,
          "the summary with zeros: a probe that never ran says so");
}
}  // namespace

int main(int argc, char** argv) {
    if (argc < 2 || (std::strcmp(argv[1], "--self-test") != 0 && std::strcmp(argv[1], "--dry-run") != 0)) {
        std::printf("usage: head_aim_probe_test --self-test | --dry-run (the pure half only; --dry-run writes nothing, as does --self-test)\n");
        return 2;
    }
    partKeys();
    partGate();
    partRamp();
    partSway();
    partTurn();
    partIdentity();
    partPerFrame();
    partLines();
    std::printf("%s: head_aim_probe_test, %d checks, %d failed\n", g_failures ? "FAIL" : "PASS", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
