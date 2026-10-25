// Build gate for the EDVRonFOOT on-foot HUD (src\d3d11\on_foot_hud_core.h, experimental.native_vr_on_foot_hud).
//
// Drives the pure half on synthetic frames: the .9 census's panels (headset stereo: 2412x1449 takes most interface draws, then
// 2768x2383 and 2162x1922; the census counted 88-122 font draws a snapshot, the split here is the fixture's), a frame with the game's
// own composite (B10B032B) shown nothing, no panels, equal counts, the scouting key stepping 1 -> 2 -> 3 -> 1 and a panel count that
// changes in the middle of it, the body anchor after a head turn and after a seated-space turn, the decision order, the change latch
// and its budget, the two keys' wrong values, and the text of every line.
//
// .24 (the HUD cylinder): the HUD panel is picked by its proportions 1.665 (stereo 2412x1449 beside 2162x1922 with more draws, the
// cinema's 3564x2140, the larger of two, none), the copy is held through frames the game did not redraw it (every other frame, 29 and
// 30 in a row), a composite draw counts only when it reads the HUD panel, the sheet is a cylinder round the head (a 90 degree sheet
// ends 45 degrees from the centre, the way menuPanelHit reads it), the keys read empty as body and 85, the scouting order has the HUD
// panel first, and a new panel size gets one line.
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

// A frame of the stereo .22 census: the HUD panel with few draws, the captions with more.
FrameTally stereo22() {
    FrameTally t;
    for (int i = 0; i < 20; ++i) t.note(kSecond, 2162, 1922, kFormatTypeless);
    for (int i = 0; i < 10; ++i) t.note(kMain, 2412, 1449, kFormatTypeless);
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
          ".9: the most-drawn panel is 2412x1449");
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

void partHud() {
    check(isHudShape(2412, 1449) && isHudShape(3564, 2140) && isHudShape(2008, 1206), "the HUD shape: stereo, cinema and the base panel");
    check(!isHudShape(2162, 1922) && !isHudShape(2768, 2383) && !isHudShape(1521, 1402) && !isHudShape(2083, 362) && !isHudShape(2083, 1107),
          "the captions, the atlas, the lift menu and the dialogs are not the HUD shape");
    check(!isHudShape(0, 5) && !isHudShape(5, 0), "an empty size is not the HUD shape");
    check(isHudShape(1665, 1000) && isHudShape(1675, 1000) && isHudShape(1655, 1000) && !isHudShape(1676, 1000) && !isHudShape(1654, 1000),
          "1.665 +- 0.01, edges in");

    // .22 stereo: 2162x1922 has the most draws; the HUD panel is 2412x1449 all the same.
    FrameTally s = stereo22();
    orderPanels(s.panels, s.count);
    check(s.panels[0].id == kMain && s.panels[0].w == 2412 && s.panels[1].w == 2162,
          "stereo: the HUD panel first although it has fewer draws");
    check(findHud(s.panels, s.count) == 0, "stereo: the HUD panel found at 0");

    // The cinema's frame: 3564x2140 and the others.
    FrameTally c;
    for (int i = 0; i < 6; ++i) c.note(kSecond, 2768, 2383, kFormatTypeless);
    for (int i = 0; i < 3; ++i) c.note(kMain, 3564, 2140, kFormatUnorm);
    check(findHud(c.panels, c.count) == 1, "cinema: 3564x2140 is the HUD panel");
    orderPanels(c.panels, c.count);
    check(c.panels[0].w == 3564 && c.panels[1].w == 2768, "cinema: the HUD panel moved to the front, the rest by draws");

    // Two panels of the shape: the larger.
    FrameTally two;
    for (int i = 0; i < 9; ++i) two.note(kSecond, 2412, 1449, kFormatTypeless);
    for (int i = 0; i < 2; ++i) two.note(kMain, 3564, 2140, kFormatTypeless);
    check(findHud(two.panels, two.count) == 1, "two HUD-shaped panels: the larger by area, not the one with more draws");

    // None.
    FrameTally none;
    for (int i = 0; i < 9; ++i) none.note(kMain, 2162, 1922, kFormatTypeless);
    check(findHud(none.panels, none.count) == -1, "no HUD-shaped panel: none");
    check(findHud(nullptr, 0) == -1, "no panels: none");
    orderPanels(none.panels, none.count);
    check(none.panels[0].w == 2162, "no HUD panel: the order is by draws");

    // Equal draws, the HUD last in the ranking: it still goes first and the others keep their order.
    FrameTally e;
    for (int i = 0; i < 5; ++i) e.note(&g_res[2], 100, 100, 27);
    for (int i = 0; i < 5; ++i) e.note(&g_res[1], 300, 200, 27);
    e.note(&g_res[0], 1665, 1000, 27);
    orderPanels(e.panels, e.count);
    check(e.panels[0].w == 1665 && e.panels[1].w == 300 && e.panels[2].w == 100, "the HUD panel first, the others ranked after it");

    // The default shows only the HUD panel; a frame without one shows none.
    Selector sel;
    check(shownIndex(s.panels, s.count, sel) == 0, "default: the HUD panel is shown");
    check(shownIndex(none.panels, none.count, sel) == -1, "default, no HUD panel: nothing to show (the hold decides)");
    check(shownIndex(nullptr, 0, sel) == -1, "default, no panels: nothing");
    sel.press(s.count);
    check(shownIndex(s.panels, s.count, sel) == 1 && s.panels[1].w == 2162, "after a press: the next panel, by the order");
    sel.press(s.count);
    check(shownIndex(s.panels, s.count, sel) == 0, "after the last: the HUD panel again");
    Selector scouting;
    scouting.press(none.count);
    check(shownIndex(none.panels, none.count, scouting) == 0, "scouting with no HUD panel: by draws, as before");
}

void partComposite() {
    int a, b, atlas, now, last;
    const void* hudNow = &now;
    const void* hudLast = &last;
    const void* readsHud[3] = {&atlas, &now, nullptr};
    check(readsHudPanel(readsHud, 3, hudNow, hudLast), "a composite draw that reads the HUD panel: the game shows it");
    const void* readsOld[3] = {nullptr, nullptr, &last};
    check(readsHudPanel(readsOld, 3, hudNow, hudLast), "it reads the last frame's HUD panel (this one's is not drawn yet): the same");
    check(readsHudPanel(readsOld, 3, nullptr, hudLast) && !readsHudPanel(readsOld, 3, hudNow, nullptr), "either id may be unknown");
    const void* readsAtlas[3] = {&atlas, &a, &b};
    check(!readsHudPanel(readsAtlas, 3, hudNow, hudLast), "a composite draw that reads the crosshair atlas: the sheet stays");
    const void* empty[3] = {};
    check(!readsHudPanel(empty, 3, nullptr, nullptr) && !readsHudPanel(empty, 3, hudNow, hudLast),
          "no textures: nothing, and null is never a match");
    check(!readsHudPanel(readsHud, 3, nullptr, nullptr), "no HUD panel known: no match");
}

void partHold() {
    Hold h;
    check(h.step(false) == HoldStep::Expired, "no copy yet and no HUD panel: nothing to hold");
    check(h.step(true) == HoldStep::Fresh, "a frame with a HUD draw: a fresh copy");
    // A station: the game draws every other frame; the copy is held through the gaps and never expires.
    bool expired = false;
    for (int i = 0; i < 200; ++i)
        if (h.step(i % 2 == 0) == HoldStep::Expired) expired = true;
    check(!expired, "every other frame: no gap ever expires the copy");
    // 29 frames without a panel hold, the 30th lets go.
    h.step(true);
    bool held29 = true;
    for (uint32_t i = 0; i < 29; ++i) held29 = held29 && h.step(false) == HoldStep::Held;
    check(held29, "29 frames without the HUD panel: the copy is held");
    check(h.step(false) == HoldStep::Expired, "the 30th frame without it: stopped");
    check(h.step(false) == HoldStep::Expired, "and it stays stopped");
    check(h.step(true) == HoldStep::Fresh, "the HUD panel again: fresh");
    // 29 and a fresh one: the count starts over.
    for (uint32_t i = 0; i < 29; ++i) h.step(false);
    h.step(true);
    check(h.step(false) == HoldStep::Held && h.missed == 1, "a fresh frame restarts the count");
    h.reset();
    check(h.step(false) == HoldStep::Expired, "reset (the ship, the cinema, the key off) drops the copy");
    check(kHoldFrames == 30, "30 frames");
}

void partArc() {
    // menuPanelHit's reading of the sheet: R = dist / curve; u = (theta * R - shift + halfW) / 2halfW. The same here.
    const float R = kDistance / kCurve;
    check(near(kCurve, 1.0f) && near(R, kDistance), "the sheet is a cylinder round the head: radius = distance");
    const float k = 3.14159265f / 180.0f;
    const float widths[] = {30, 70, 85, 90, 110};
    for (float w : widths) {
        const float halfW = halfWidth(w);
        auto uAt = [&](float thetaDeg) { return (thetaDeg * k * R + halfW) / (2.0f * halfW); };
        check(std::fabs(uAt(w * 0.5f) - 1.0f) < 1e-4f && std::fabs(uAt(-w * 0.5f)) < 1e-4f && std::fabs(uAt(0) - 0.5f) < 1e-4f,
              "the sheet's edges stand at half the width from the centre, the middle at 0.5");
    }
    check(std::fabs(halfWidth(90) / R / k - 45.0f) < 1e-3f, "90 degrees: the edge is at 45 degrees");
    check(std::fabs(halfWidth(90) - kDistance * 0.7853982f) < 1e-4f, "90 degrees: the half-width is the arc 1.4 * pi/4");
    check(std::fabs(halfWidth(85) - 1.0386f) < 1e-3f, "85 degrees: +-1.04 m along the arc");
    // The height follows the panel: 0.6 of the width at 2412x1449, +-0.62 m at 85 degrees.
    check(std::fabs(halfWidth(85) * aspectOf(2412, 1449) - 0.624f) < 2e-3f, "85 degrees, 2412x1449: +-0.62 m high");
    // Every edge point is one distance from the head (the axis).
    const float th = 45.0f * k;
    check(std::fabs(std::hypot(R * std::sin(th), R * std::cos(th)) - kDistance) < 1e-5f, "the edge is as far from the eye as the centre");
}

void partNewPanels() {
    SeenSizes s;
    check(s.note(2412, 1449) && !s.note(2412, 1449), "a size is new once");
    check(s.note(2083, 362) && s.note(2083, 1107) && s.count == 3, "three sizes kept");
    SeenSizes f;
    int kept = 0;
    for (uint32_t i = 0; i < 40; ++i) kept += f.note(100 + i, 50) ? 1 : 0;
    check(kept == kMaxSeenSizes && f.count == kMaxSeenSizes && f.unrecorded == 8, "32 sizes kept, the rest counted");
    check(!f.note(100, 50) && f.unrecorded == 8, "a kept size is not counted again");
    f.newWindow();
    check(f.windowFrom == kMaxSeenSizes && f.unrecorded == 0, "a new window: the list starts over, the memory stays");
}

void partSelector() {
    Selector s;
    check(s.effective(3) == 0, "no press: the first panel");
    check(s.press(3) == 1 && s.effective(3) == 1, "press 1: the second panel");
    check(s.press(3) == 2 && s.effective(3) == 2, "press 2: the third panel");
    check(s.press(3) == 0 && s.effective(3) == 0, "press 3: back to the first panel");
    // The count changes in the middle: the third panel is chosen, then the frame has two.
    s.press(3);
    s.press(3);
    check(s.index == 2 && s.effective(2) == 0, "the third panel gone: the first panel is shown");
    check(s.press(2) == 1, "the next press steps on from the first panel");
    check(s.press(0) == 0 && s.effective(0) == 0, "no panels: the choice returns to the first panel");
    Selector one;
    check(!one.pressed && one.press(1) == 0 && one.pressed, "one panel: every press stays on it, and the key counts as used");
}

void partDecision() {
    DecisionIn in;
    in.onFoot = 1;
    in.panels = 3;
    check(decide(in) == Reason::Showing, "on foot, keys on, panels, no game composite: shown");
    DecisionIn g = in;
    g.gameComposites = true;
    check(decide(g) == Reason::GameComposites, ".9 cinema: B10B032B reading the HUD in the frame: not shown");
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
    b.mode = Mode::Visor;
    check(decide(b) == Reason::Showing, "visor is shown as body is");
    check(DecisionIn{}.mode == Mode::Body, "the default mode is body");
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
    check(parseMode("", &ok) == Mode::Body && ok, "empty: body, not an error");
    check(parseMode("  ", &ok) == Mode::Body && ok, "blanks: body, not an error");
    check(parseMode(nullptr, &ok) == Mode::Body && ok, "missing: body");
    check(parseMode("head", &ok) == Mode::Body && !ok, "a wrong word: body, said");
    check(parseMode("1", &ok) == Mode::Body && !ok, "a number: body, said");
    check(parseMode("visors", &ok) == Mode::Body && !ok, "a longer word is not visor");
    check(near(kWidthDefault, 85) && near(kWidthMin, 30) && near(kWidthMax, 110), "the width default 85, range 30..110");
    check(near(parseWidth(85, &ok), 85) && ok, "width 85");
    check(near(parseWidth(70, &ok), 70) && ok, "width 70 is still in range");
    check(near(parseWidth(30, &ok), 30) && ok && near(parseWidth(110, &ok), 110) && ok, "width 30 and 110 are in range");
    check(near(parseWidth(29.9f, &ok), 85) && !ok, "width below 30: 85, said");
    check(near(parseWidth(111, &ok), 85) && !ok, "width above 110: 85, said");
    check(near(parseWidth(std::nanf(""), &ok), 85) && !ok, "width NaN: 85, said");
    check(near(aspectOf(2412, 1449), 1449.0f / 2412.0f) && aspectOf(0, 5) == 0.0f, "the panel's proportions");
    check(std::fabs(ratioOf(2083, 362) - 5.754f) < 1e-3f && ratioOf(5, 0) == 0.0f, "the lines' aspect is width over height");
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
    formatKey(Mode::Body, false, "head", 85, false, 200, line, sizeof(line));
    check(has(line, "\"head\" is not visor, body or off, read as body") && has(line, "size 200.0 is outside 30..110, read as 85") &&
              has(line, "on-foot hud: body:") && has(line, "size 85 deg"),
          "the key line says what was wrong, and what it was read as");
    formatKey(Mode::Body, true, "", 85, true, 85, line, sizeof(line));
    check(has(line, "= body, size 85 deg") && !has(line, "read as"), "a missing key: the line names body and 85, nothing wrong");
    formatKey(Mode::Off, true, "off", 85, true, 85, line, sizeof(line));
    check(has(line, "on-foot hud: off: on foot nothing is shown"), "the key line, off");
    formatKey(Mode::Body, true, "body", 50, true, 50, line, sizeof(line));
    check(has(line, "body:") && has(line, "crosshair is true") && has(line, "size 50 deg"), "the key line, body");

    FrameTally t = census9();
    rankPanels(t.panels, t.count);
    formatShowing(0, t.count, t.panels[0], Mode::Body, 85, line, sizeof(line));
    check(std::strcmp(line, "on-foot hud: showing the HUD panel (2412x1449, aspect 1.665, 70 interface draws), anchor body, 85 deg, "
                            "cylinder around the head") == 0,
          "the showing line");
    formatShowing(1, t.count, t.panels[1], Mode::Visor, 70, line, sizeof(line));
    check(std::strcmp(line, "on-foot hud: showing panel 2 of 3 (2768x2383, aspect 1.162, 30 interface draws), anchor visor, 70 deg, "
                            "cylinder around the head") == 0,
          "the showing line of a scouted panel");
    formatStopped(Reason::InTheShip, line, sizeof(line));
    check(std::strcmp(line, "on-foot hud: stopped (in the ship)") == 0, "the stopped line");
    formatStopped(Reason::GameComposites, line, sizeof(line));
    check(has(line, "stopped (the game composites its panels)"), "stopped: the game composites");
    formatStopped(Reason::KeyOff, line, sizeof(line));
    check(has(line, "stopped (key off)"), "stopped: key off");
    formatStopped(Reason::NoPanels, line, sizeof(line));
    check(std::strcmp(line, "on-foot hud: stopped (no panels: no HUD panel for 30 frames)") == 0, "stopped: no panels");
    formatNewPanel(2083, 362, 14, line, sizeof(line));
    check(std::strcmp(line, "on-foot hud: new panel 2083x362 (aspect 5.754) with 14 interface draws") == 0, "the new panel line");
    formatSelected(1, t.count, &t.panels[1], line, sizeof(line));
    check(std::strcmp(line, "on-foot hud: panel 2 of 3 selected (2768x2383, aspect 1.162, 30 draws)") == 0, "the panel key line");
    formatSelected(0, 0, nullptr, line, sizeof(line));
    check(has(line, "no interface panel"), "the panel key line with no panels");

    Summary s;
    s.frames = 2700;
    s.shown = 2690;
    s.held = 40;
    SeenSizes seen;
    formatSummary(s, t.panels, t.count, 0, 0, seen, line, sizeof(line));
    check(std::strcmp(line, "on-foot hud 30s: frames=2700 shown=2690 held=40 panels=[2412x1449 n=70, 2768x2383 n=30, 2162x1922 n=12] "
                            "game-composites=0 no-panels=0 copy-fail=0 new-panels=[]") == 0,
          "the summary line");
    seen.note(2412, 1449);
    seen.newWindow();
    seen.note(2083, 362);
    seen.note(1521, 1402);
    formatSummary(s, t.panels, t.count, 0, 0, seen, line, sizeof(line));
    check(has(line, "held=40") && has(line, "new-panels=[2083x362, 1521x1402]"), "the summary lists the sizes new in this window only");
    Summary empty;
    empty.frames = 10;
    empty.noPanels = 10;
    seen.unrecorded = 3;
    formatSummary(empty, nullptr, 0, 2, 5, seen, line, sizeof(line));
    check(has(line, "panels=[] (no panels found)") && has(line, "targets-dropped=2") && has(line, "changes-unwritten=5") &&
              has(line, "new-panels-unrecorded=3") && !has(line, "1.665"),
          "the summary with no panels, drops and unwritten changes: no HUD panel in it");
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
    partHud();
    partComposite();
    partHold();
    partArc();
    partNewPanels();
    partSelector();
    partDecision();
    partAnchor();
    partKeys();
    partLatch();
    partLines();
    std::printf("%s: on_foot_hud_test, %d checks, %d failed\n", g_failures ? "FAIL" : "PASS", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
