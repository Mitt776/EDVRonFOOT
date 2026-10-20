// Build gate for the EDVRonFOOT on-foot HUD (src\d3d11\on_foot_hud_core.h, experimental.native_vr_on_foot_hud).
//
// Drives the pure half on synthetic frames: the .9 census's panels (headset stereo: 2412x1449 takes most interface draws, then
// 2768x2383 and 2162x1922; the census counted 88-122 font draws a snapshot, the split here is the fixture's), a frame with the game's
// own composite (B10B032B) shown nothing, no panels, equal counts, the scouting key stepping 1 -> 2 -> 3 -> 1 and a panel count that
// changes in the middle of it, the body anchor after a head turn and after a seated-space turn, the decision order, the change latch
// and its budget, the two keys' wrong values, and the text of every line.
#include "../../src/d3d11/on_foot_hud_core.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

using namespace edvr;
using namespace edvr::ofh;

namespace {
int g_checks = 0, g_failures = 0;
void check(bool ok, const char* what) {
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::printf("FAIL: %s\n", what);
    }
}
bool has(const char* text, const char* needle) { return std::strstr(text, needle) != nullptr; }
bool near(float a, float b) { return std::fabs(a - b) < 1e-5f; }

// Three distinct identities, as the glue's resource pointers would be.
int g_res[3];
const void* kMain = &g_res[0];
const void* kSecond = &g_res[1];
const void* kThird = &g_res[2];

// A stereo frame from the .9 census: the three panels, in the order the game first draws into them (not the rank order).
FrameTally census9() {
    FrameTally t;
    for (int i = 0; i < 12; ++i) t.note(kThird, 2162, 1922, kFormatTypeless);
    for (int i = 0; i < 30; ++i) t.note(kSecond, 2768, 2383, kFormatTypeless);
    for (int i = 0; i < 70; ++i) t.note(kMain, 2412, 1449, kFormatTypeless);
    return t;
}

void partShapes() {
    check(isGuiVs(kGuiVector) && isGuiVs(kGuiText) && isGuiVs(kGuiIcons), "the three interface shaders are recognised");
    check(!isGuiVs(kGameCompositeA) && !isGuiVs(0), "nothing else is an interface shader");
    check(isGameCompositeVs(0xB10B032BDFD46700ull) && isGameCompositeVs(0xC4B4B334B26E81A9ull), "the game's two composites");
    check(!isGameCompositeVs(kGuiText), "an interface draw is not the game's composite");
    check(panelShapeOk(27, 1, false) && panelShapeOk(28, 1, false), "format 27 and its UNORM view 28, one sample");
    check(!panelShapeOk(27, 4, false), "a multisampled target is not a panel");
    check(!panelShapeOk(10, 1, false) && !panelShapeOk(87, 1, false), "another format is not a panel");
    check(!panelShapeOk(27, 1, true), "an eye is never a panel");
}

void partRank() {
    FrameTally t = census9();
    check(t.count == 3 && t.dropped == 0, ".9: three panels tallied");
    rankPanels(t.panels, t.count);
    check(t.panels[0].id == kMain && t.panels[0].w == 2412 && t.panels[0].h == 1449 && t.panels[0].draws == 70,
          ".9: the main panel is 2412x1449");
    check(t.panels[1].id == kSecond && t.panels[1].w == 2768, ".9: second 2768x2383");
    check(t.panels[2].id == kThird && t.panels[2].w == 2162, ".9: third 2162x1922");

    // Equal counts: the larger area first; equal both, the order met.
    FrameTally e;
    for (int i = 0; i < 5; ++i) e.note(kThird, 100, 100, 27);
    for (int i = 0; i < 5; ++i) e.note(kSecond, 300, 200, 27);
    for (int i = 0; i < 5; ++i) e.note(kMain, 200, 300, 27);
    rankPanels(e.panels, e.count);
    check(e.panels[0].id == kSecond && e.panels[1].id == kMain && e.panels[2].id == kThird,
          "equal counts: larger area first, equal area in the order met");

    // No panels.
    FrameTally z;
    rankPanels(z.panels, z.count);
    check(z.count == 0, "no panels: nothing ranked");

    // A ninth target is dropped and counted.
    FrameTally m;
    int slots[kMaxPanels + 1];
    for (int i = 0; i <= kMaxPanels; ++i) slots[i] = m.note(&slots[i], 10, 10, 27);
    check(m.count == kMaxPanels && m.dropped == 1 && slots[kMaxPanels] == -1, "a ninth target is dropped and counted");
    m.clear();
    check(m.count == 0 && m.dropped == 0 && !m.gameComposites, "clear empties the tally");
}

void partSelector() {
    Selector s;
    check(s.effective(3) == 0, "no press: the main panel");
    check(s.press(3) == 1 && s.effective(3) == 1, "press 1: the second panel");
    check(s.press(3) == 2 && s.effective(3) == 2, "press 2: the third panel");
    check(s.press(3) == 0 && s.effective(3) == 0, "press 3: back to the main panel");
    // The count changes in the middle: the third panel is chosen, then the frame has two.
    s.press(3);
    s.press(3);
    check(s.index == 2 && s.effective(2) == 0, "the third panel gone: the main panel is shown");
    check(s.press(2) == 1, "the next press steps on from the main panel");
    check(s.press(0) == 0 && s.effective(0) == 0, "no panels: the choice returns to the main panel");
    Selector one;
    check(one.press(1) == 0, "one panel: every press stays on it");
}

void partDecision() {
    DecisionIn in;
    in.onFoot = 1;
    in.panels = 3;
    check(decide(in) == Reason::Showing, "on foot, keys on, panels, no game composite: shown");
    DecisionIn g = in;
    g.gameComposites = true;
    check(decide(g) == Reason::GameComposites, ".9 cinema: B10B032B in the frame: not shown");
    DecisionIn n = in;
    n.panels = 0;
    check(decide(n) == Reason::NoPanels, "no panels: not shown");
    DecisionIn s = in;
    s.onFoot = 0;
    check(decide(s) == Reason::InTheShip, "in the ship: not shown");
    s.onFoot = -1;
    check(decide(s) == Reason::InTheShip, "the journal not known yet: read as in the ship");
    DecisionIn k = in;
    k.mode = Mode::Off;
    check(decide(k) == Reason::KeyOff, "experimental.native_vr_on_foot_hud = off: not shown");
    k = in;
    k.nativeOnFoot = false;
    check(decide(k) == Reason::KeyOff, "experimental.native_vr_on_foot = 0 (the cinema): not shown");
    k.onFoot = 0;
    k.gameComposites = true;
    check(decide(k) == Reason::KeyOff, "the keys are asked first");
    DecisionIn b = in;
    b.mode = Mode::Body;
    check(decide(b) == Reason::Showing, "body is shown as visor is");
}

void partAnchor() {
    // The head turned 90 degrees left (yaw), 1.7 m up and a step forward. Rows [R | t]; a left turn about +Y maps -Z to -X.
    const float head[12] = {0, 0, 1, 0.1f, 0, 1, 0, 1.7f, -1, 0, 0, -0.3f};
    float a[12];
    bodyAnchor(head, a);
    check(near(a[0], 1) && near(a[5], 1) && near(a[10], 1) && near(a[1], 0) && near(a[2], 0) && near(a[4], 0) && near(a[6], 0) &&
              near(a[8], 0) && near(a[9], 0),
          "body: the head's turn does not turn the sheet (the seated space's forward)");
    check(near(a[3], 0.1f) && near(a[7], 1.7f) && near(a[11], -0.3f), "body: at the head's position");
    // A seated-space turn (the runtime recentres LOCAL 90 degrees): the head now reports straight ahead in the new space, and the
    // anchor is that space's forward -- the sheet stays in front of the body.
    const float recentred[12] = {1, 0, 0, 0, 0, 1, 0, 1.7f, 0, 0, 1, 0};
    bodyAnchor(recentred, a);
    check(near(a[0], 1) && near(a[10], 1) && near(a[2], 0) && near(a[3], 0) && near(a[11], 0), "body: after a LOCAL turn, its forward");
    // A tilted head (looking down 30 degrees): no tilt reaches the anchor.
    const float c = std::cos(0.5236f), s = std::sin(0.5236f);
    const float tilted[12] = {1, 0, 0, 0, 0, c, s, 1.6f, 0, -s, c, 0};
    bodyAnchor(tilted, a);
    check(near(a[5], 1) && near(a[6], 0) && near(a[9], 0) && near(a[10], 1), "body: the head's tilt does not tilt the sheet");
}

void partKeys() {
    bool ok = false;
    check(parseMode("visor", &ok) == Mode::Visor && ok, "visor");
    check(parseMode(" Body ", &ok) == Mode::Body && ok, "body, any case, spaces trimmed");
    check(parseMode("OFF", &ok) == Mode::Off && ok, "off");
    check(parseMode("", &ok) == Mode::Visor && ok, "empty: visor, not an error");
    check(parseMode(nullptr, &ok) == Mode::Visor && ok, "missing: visor");
    check(parseMode("head", &ok) == Mode::Visor && !ok, "a wrong word: visor, said");
    check(parseMode("1", &ok) == Mode::Visor && !ok, "a number: visor, said");
    check(parseMode("visors", &ok) == Mode::Visor && !ok, "a longer word is not visor");
    check(near(parseWidth(70, &ok), 70) && ok, "width 70");
    check(near(parseWidth(30, &ok), 30) && ok && near(parseWidth(110, &ok), 110) && ok, "width 30 and 110 are in range");
    check(near(parseWidth(29.9f, &ok), 70) && !ok, "width below 30: 70, said");
    check(near(parseWidth(111, &ok), 70) && !ok, "width above 110: 70, said");
    check(near(parseWidth(std::nanf(""), &ok), 70) && !ok, "width NaN: 70, said");
    check(near(halfWidth(90), kDistance), "90 degrees: the half-width is the distance");
    check(near(aspectOf(2412, 1449), 1449.0f / 2412.0f) && aspectOf(0, 5) == 0.0f, "the panel's proportions");
}

void partLatch() {
    DecisionLatch l;
    check(l.admit(Reason::InTheShip, -1, 0, 0, Mode::Visor), "the first decision is written");
    check(!l.admit(Reason::InTheShip, -1, 0, 0, Mode::Visor), "the same decision is not");
    check(l.admit(Reason::Showing, 0, 2412, 1449, Mode::Visor), "showing is written");
    check(!l.admit(Reason::Showing, 0, 2412, 1449, Mode::Visor), "showing again is not");
    check(l.admit(Reason::Showing, 1, 2768, 2383, Mode::Visor), "another panel is written");
    check(l.admit(Reason::Showing, 1, 2768, 2383, Mode::Body), "another anchor is written");
    check(l.admit(Reason::InTheShip, 1, 2768, 2383, Mode::Body), "the stop is written");
    check(!l.admit(Reason::InTheShip, 0, 1, 1, Mode::Visor), "a stopped decision ignores the panel");
    // The budget: a flicker past it is counted, not written.
    DecisionLatch f;
    int written = 0;
    for (uint32_t i = 0; i < kChangeBudget + 10; ++i)
        written += f.admit(i % 2 ? Reason::NoPanels : Reason::Showing, 0, 10, 10, Mode::Visor) ? 1 : 0;
    check(written == int(kChangeBudget) && f.unwritten == 10, "a flicker: the budget's lines written, the rest counted");
    f.newWindow();
    check(f.admit(Reason::InTheShip, 0, 0, 0, Mode::Visor), "a new window writes again");
}

void partLines() {
    char line[700];
    formatKey(Mode::Visor, true, "visor", 70, true, 70, line, sizeof(line));
    check(has(line, "on-foot hud: visor:") && has(line, "experimental.native_vr_on_foot_hud = visor, size 70 deg") &&
              has(line, "crosshair follows the head"),
          "the key line, visor");
    formatKey(Mode::Visor, false, "head", 70, false, 200, line, sizeof(line));
    check(has(line, "\"head\" is not visor, body or off") && has(line, "size 200.0 is outside 30..110"), "the key line says what was wrong");
    formatKey(Mode::Off, true, "off", 70, true, 70, line, sizeof(line));
    check(has(line, "on-foot hud: off: on foot nothing is shown"), "the key line, off");
    formatKey(Mode::Body, true, "body", 50, true, 50, line, sizeof(line));
    check(has(line, "body:") && has(line, "crosshair is true") && has(line, "size 50 deg"), "the key line, body");

    FrameTally t = census9();
    rankPanels(t.panels, t.count);
    formatShowing(0, t.count, t.panels[0], Mode::Visor, 70, line, sizeof(line));
    check(std::strcmp(line, "on-foot hud: showing panel 1 of 3 (2412x1449, 70 interface draws), anchor visor, 70 deg") == 0,
          "the showing line");
    formatStopped(Reason::InTheShip, line, sizeof(line));
    check(std::strcmp(line, "on-foot hud: stopped (in the ship)") == 0, "the stopped line");
    formatStopped(Reason::GameComposites, line, sizeof(line));
    check(has(line, "stopped (the game composites its panels)"), "stopped: the game composites");
    formatStopped(Reason::KeyOff, line, sizeof(line));
    check(has(line, "stopped (key off)"), "stopped: key off");
    formatStopped(Reason::NoPanels, line, sizeof(line));
    check(has(line, "stopped (no panels)"), "stopped: no panels");
    formatSelected(1, t.count, &t.panels[1], line, sizeof(line));
    check(std::strcmp(line, "on-foot hud: panel 2 of 3 selected (2768x2383, 30 draws)") == 0, "the panel key line");
    formatSelected(0, 0, nullptr, line, sizeof(line));
    check(has(line, "no interface panel"), "the panel key line with no panels");

    Summary s;
    s.frames = 2700;
    s.shown = 2690;
    formatSummary(s, t.panels, t.count, 0, 0, line, sizeof(line));
    check(std::strcmp(line, "on-foot hud 30s: frames=2700 shown=2690 panels=[2412x1449 n=70, 2768x2383 n=30, 2162x1922 n=12] "
                            "game-composites=0 no-panels=0 copy-fail=0") == 0,
          "the summary line");
    Summary empty;
    empty.frames = 10;
    empty.noPanels = 10;
    formatSummary(empty, nullptr, 0, 2, 5, line, sizeof(line));
    check(has(line, "panels=[] (no panels found)") && has(line, "targets-dropped=2") && has(line, "changes-unwritten=5"),
          "the summary with no panels, drops and unwritten changes");
    // Every line carries the one prefix the log reader looks for.
    check(has(line, "on-foot hud"), "the prefix");
}
}  // namespace

int main(int argc, char** argv) {
    if (argc < 2 || std::strcmp(argv[1], "--self-test") != 0) {
        std::printf("usage: on_foot_hud_test --self-test\n");
        return 2;
    }
    partShapes();
    partRank();
    partSelector();
    partDecision();
    partAnchor();
    partKeys();
    partLatch();
    partLines();
    std::printf("%s: on_foot_hud_test, %d checks, %d failed\n", g_failures ? "FAIL" : "PASS", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
