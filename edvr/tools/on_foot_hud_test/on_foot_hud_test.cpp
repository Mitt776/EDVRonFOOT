// Build gate for the EDVRonFOOT on-foot HUD (src\d3d11\on_foot_hud_core.h, experimental.native_vr_on_foot_hud).
//
// Drives the pure half on synthetic frames: the .9 census's panels (headset stereo: 2412x1449 takes most interface draws, then
// 2768x2383 and 2162x1922; the census counted 88-122 font draws a snapshot, the split here is the fixture's), a frame with the game's
// own composite (B10B032B) shown nothing, no panels, equal counts, the body anchor after a head turn and after a seated-space turn,
// the decision order, the change latch and its budget, the two keys' wrong values, and the text of every line.
//
// .24 (the HUD cylinder): the HUD panel is picked by its proportions 1.665 (stereo 2412x1449 beside 2162x1922 with more draws, the
// cinema's 3564x2140, the larger of two, none), the copy is held through frames the game did not redraw it (every other frame, 29 and
// 30 in a row), a composite draw counts only when it reads the HUD panel, the sheet is a cylinder round the head (a 90 degree sheet
// ends 45 degrees from the centre, the way menuPanelHit reads it), the keys read empty as body and 85, the summary's order has the HUD
// panel first, and a new panel size gets one line.
//
// .25 (the menus): the four layers by their shapes (the lift 1029x949, the answers 1410x245, the dialog 1410x749, the HUD; the atlas,
// the captions and the landing's one-off panels none), their places from the HUD's width (85 and 50 degrees), placeAnchor's turn and
// drop, each layer's own hold and its own composite bit, a menu's quiet latch, and the menu lines and the summary's menus field.
//
// .27 (the mission description and the trader): 1410x761 is the description and not the dialog's 1410x749 (and the other way), its
// sheet's bottom on the answers' top; the trader's layer is never found by its 16:9 shape, only by what the withheld draw read; the
// head-locked draw is recognised whole (the eye-panel shader, 360 indices, an eye, the panels' format, 16:9) and nothing less; the
// trader is the whole cinema frame from the HUD's width; its line says the game's copy is withheld, the summary counts the withheld.
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
    check(m.count == 0 && m.dropped == 0 && !m.composites, "clear empties the tally");
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
}

void partComposite() {
    int a, b, atlas, now, last, lift;
    const void* none[kLayers] = {};
    const void* hudNow[kLayers] = {&now};
    const void* hudLast[kLayers] = {&last};
    const void* readsHud[3] = {&atlas, &now, nullptr};
    check(readsPanels(readsHud, 3, hudNow, hudLast) == 1u, "a composite draw that reads the HUD panel: the game shows it");
    const void* readsOld[3] = {nullptr, nullptr, &last};
    check(readsPanels(readsOld, 3, hudNow, hudLast) == 1u, "it reads the last frame's HUD panel (this one's is not drawn yet): the same");
    check(readsPanels(readsOld, 3, none, hudLast) == 1u && readsPanels(readsOld, 3, hudNow, none) == 0u, "either id may be unknown");
    const void* readsAtlas[3] = {&atlas, &a, &b};
    check(readsPanels(readsAtlas, 3, hudNow, hudLast) == 0u, "a composite draw that reads the crosshair atlas: the sheet stays");
    const void* empty[3] = {};
    check(readsPanels(empty, 3, none, none) == 0u && readsPanels(empty, 3, hudNow, hudLast) == 0u,
          "no textures: nothing, and null is never a match");
    check(readsPanels(readsHud, 3, none, none) == 0u, "no panel known: no match");
    // .25: the bit is the layer's whose panel is read; the HUD's is not set by a read of the lift menu.
    const void* both[kLayers] = {&now, &lift};
    const void* readsLift[3] = {&atlas, &lift, nullptr};
    check(readsPanels(readsLift, 3, both, none) == (1u << int(Layer::Lift)), "a composite draw that reads the lift menu: the lift's bit only");
    const void* readsTwo[3] = {&lift, &now, nullptr};
    check(readsPanels(readsTwo, 3, both, none) == ((1u << int(Layer::Lift)) | 1u), "reading both: both bits");
}

void partLayers() {
    // The menus' stereo shapes, from .24's lines.
    check(isLayerShape(Layer::Lift, 1029, 949) && findLayer(Layer::Lift, nullptr, 0) == -1, "1029x949 is the lift and door menu");
    check(isLayerShape(Layer::Answers, 1410, 245), "1410x245 is the answers");
    check(isLayerShape(Layer::Dialog, 1410, 749), "1410x749 is the second dialog panel");
    check(isLayerShape(Layer::Hud, 2412, 1449) && isHudShape(2412, 1449), "2412x1449 is the HUD");
    // The cinema's sizes are the same shapes.
    check(isLayerShape(Layer::Lift, 1521, 1402) && isLayerShape(Layer::Answers, 2083, 362) && isLayerShape(Layer::Dialog, 2083, 1107),
          "the cinema's menus are the same shapes");
    // Each size is one layer at most, and the atlas, the captions and the landing's one-off panels none.
    const uint32_t sizes[][2] = {{2412, 1449}, {1029, 949}, {1410, 245}, {1410, 749}};  // in Layer's order
    for (int i = 0; i < 4; ++i) {
        int layers = 0;
        for (int k = 0; k < kLayers; ++k) layers += isLayerShape(Layer(k), sizes[i][0], sizes[i][1]) ? 1 : 0;
        check(layers == 1 && isLayerShape(Layer(i), sizes[i][0], sizes[i][1]), "each menu size is its own layer only");
    }
    const uint32_t strangers[][2] = {{2768, 2383}, {2162, 1922}, {961, 961}, {1321, 1081}, {1600, 1400}, {2000, 1636},
                                     {2329, 500},  {1000, 1497}, {1599, 1000}, {918, 1000}};
    for (const auto& s : strangers)
        for (int k = 0; k < kLayers; ++k) check(!isLayerShape(Layer(k), s[0], s[1]), "the atlas, the captions and the one-off panels: no layer");
    check(!isLayerShape(Layer::Lift, 0, 1) && !isLayerShape(Layer::Lift, 1, 0), "an empty size is no layer");

    // A frame of the dialog: the HUD, the answers and the captions; two answers-shaped panels, the larger taken.
    FrameTally t;
    for (int i = 0; i < 9; ++i) t.note(&g_res[0], 2412, 1449, kFormatTypeless);
    for (int i = 0; i < 20; ++i) t.note(&g_res[1], 2162, 1922, kFormatTypeless);
    for (int i = 0; i < 7; ++i) t.note(&g_res[2], 705, 122, kFormatTypeless);
    int other;
    for (int i = 0; i < 3; ++i) t.note(&other, 1410, 245, kFormatTypeless);
    int found[kLayers];
    findLayers(t.panels, t.count, found);
    check(found[int(Layer::Hud)] == 0 && found[int(Layer::Answers)] == 3 && found[int(Layer::Lift)] == -1 &&
              found[int(Layer::Dialog)] == -1,
          "a dialog frame: the HUD and the answers, no lift, no second panel");
    check(t.panels[found[int(Layer::Answers)]].w == 1410, "two answers-shaped panels: the larger, not the one with more draws");
}

void partPlaces() {
    // The HUD is the measure: the key's width, straight ahead, not moved, curve 1.
    const Place hud = placeOf(Layer::Hud, 85);
    check(near(hud.widthDeg, 85) && hud.yawDeg == 0.0f && hud.downM == 0.0f && near(hud.curve, kCurve), "the HUD: 85 deg, 0, 0, curve 1");
    // At 85 degrees the cinema's frame is 85 / 0.956 = 88.9 degrees across, 1.22 m high at 1.4 m.
    const Place lift = placeOf(Layer::Lift, 85);
    check(std::fabs(lift.widthDeg - 56.0f) < 0.5f && std::fabs(lift.yawDeg) < 0.01f, "the lift: about 56 deg wide, straight ahead");
    check(lift.downM < 0.0f && lift.downM > -0.06f, "the lift: a few centimetres up (its centre is at 0.471 of the frame)");
    check(near(lift.curve, kMenuCurve) && near(kMenuCurve, 0.4f), "the menus: curve 0.4");
    const Place answers = placeOf(Layer::Answers, 85);
    check(std::fabs(answers.downM - 0.38f) < 0.01f && std::fabs(answers.yawDeg) < 0.01f, "the answers: about 0.38 m down, centred");
    check(std::fabs(std::atan2(answers.downM, kDistance) / kRadPerDeg - 15.0f) < 0.5f, "the answers: about 15 deg below the eyes");
    check(std::fabs(answers.widthDeg - 58.7f) < 0.5f, "the answers: about two thirds of the HUD wide");
    const Place dialog = placeOf(Layer::Dialog, 85);
    check(std::fabs(dialog.yawDeg - 13.8f) < 0.3f && dialog.downM < -0.1f, "the dialog's second panel: about 14 deg right, above");
    // At 50 degrees everything is smaller in proportion.
    const Place lift50 = placeOf(Layer::Lift, 50), answers50 = placeOf(Layer::Answers, 50), dialog50 = placeOf(Layer::Dialog, 50);
    const float r = 50.0f / 85.0f;
    check(std::fabs(lift50.widthDeg - lift.widthDeg * r) < 1e-3f && std::fabs(answers50.downM - answers.downM * r) < 1e-4f &&
              std::fabs(dialog50.yawDeg - dialog.yawDeg * r) < 1e-3f && near(placeOf(Layer::Hud, 50).widthDeg, 50),
          "50 deg: the width, the turn and the drop all in proportion");
}

bool rigid(const float* p) {
    for (int a = 0; a < 3; ++a)
        for (int b = a; b < 3; ++b) {
            float dot = 0;
            for (int c = 0; c < 3; ++c) dot += p[a * 4 + c] * p[b * 4 + c];
            if (std::fabs(dot - (a == b ? 1.f : 0.f)) > 1e-4f) return false;
        }
    return true;
}

void partPlaceAnchor() {
    const float unit[12] = {1, 0, 0, 0.2f, 0, 1, 0, 1.7f, 0, 0, 1, -0.4f};
    float a[12];
    placeAnchor(unit, 0, 0, a);
    bool same = true;
    for (int i = 0; i < 12; ++i) same = same && a[i] == unit[i];
    check(same, "no turn and no drop: the anchor exactly (the HUD's sheet is .24's)");
    // Forward is -Z (menuPanelHit's sheet stands at z = -dist): a positive turn takes it to +x.
    placeAnchor(unit, 30, 0, a);
    check(-a[2] > 0.49f && -a[2] < 0.51f && std::fabs(a[6]) < 1e-6f && rigid(a), "30 deg: forward goes right (+x), stays level, rigid");
    check(near(a[3], 0.2f) && near(a[7], 1.7f) && near(a[11], -0.4f), "a turn does not move the anchor");
    placeAnchor(unit, 0, 0.38f, a);
    check(near(a[7], 1.7f - 0.38f) && near(a[3], 0.2f) && near(a[11], -0.4f) && rigid(a), "a drop lowers y, nothing else");
    // On a head turned 90 degrees left the turn and the drop are the head's own.
    const float head[12] = {0, 0, 1, 0.1f, 0, 1, 0, 1.7f, -1, 0, 0, -0.3f};
    placeAnchor(head, 14, 0.2f, a);
    check(rigid(a) && near(a[7], 1.5f) && std::fabs(a[5] - 1.0f) < 1e-6f, "a turned anchor: still upright and rigid, lowered along its up");
}

void partLayerSteps() {
    DecisionIn in;
    in.onFoot = 1;
    Hold hud, lift;
    HoldStep st;
    // The HUD is drawn every frame, the lift once and then not: the lift's hold runs out on its own count, the HUD's never.
    check(stepLayer(in, true, hud, &st) == Reason::Showing && st == HoldStep::Fresh, "the HUD fresh");
    check(stepLayer(in, true, lift, &st) == Reason::Showing && st == HoldStep::Fresh, "the lift fresh");
    Reason liftReason = Reason::Showing;
    for (uint32_t i = 0; i < kHoldFrames; ++i) {
        check(stepLayer(in, true, hud, &st) == Reason::Showing && st == HoldStep::Fresh, "the HUD's frames go on");
        liftReason = stepLayer(in, false, lift, &st);
    }
    check(liftReason == Reason::NoPanels && st == HoldStep::Expired, "the lift closed: stopped after 30 frames, the HUD's frames notwithstanding");
    // And the other way: the HUD not drawn while a menu is open (.24) -- the menu goes on, the HUD's copy runs out.
    Hold hud2, lift2;
    stepLayer(in, true, hud2, &st);
    for (uint32_t i = 0; i < kHoldFrames; ++i) {
        stepLayer(in, false, hud2, &st);
        check(stepLayer(in, true, lift2, &st) == Reason::Showing, "the lift shown while the HUD is not drawn");
    }
    check(st == HoldStep::Fresh && !hud2.have, "the HUD's copy ran out, the lift's frames did not keep it");
    // The game composites the lift menu (the cinema): the lift stops, the HUD does not.
    DecisionIn liftIn = in;
    liftIn.gameComposites = true;
    check(stepLayer(liftIn, true, lift, &st) == Reason::GameComposites && !lift.have, "the game lays the lift itself: the lift stops, its copy dropped");
    check(stepLayer(in, true, hud, &st) == Reason::Showing, "the HUD shown all the same");
    DecisionIn ship = in;
    ship.onFoot = 0;
    check(stepLayer(ship, true, hud, &st) == Reason::InTheShip && !hud.have, "in the ship: stopped, the copy dropped");
}

// The sheet's height in metres at the distance: the arc over the panel's proportions, as menu_panel builds it.
float sheetHeightM(const Place& p, double aspect) { return float(kDistance * p.widthDeg * kRadPerDeg / aspect); }

void partMissionTrader() {
    // Shapes: the description and the dialog's second panel are both 1410 wide and neither is the other.
    check(isLayerShape(Layer::Mission, 1410, 761) && !isLayerShape(Layer::Dialog, 1410, 761), "1410x761 is the mission description only");
    check(isLayerShape(Layer::Dialog, 1410, 749) && !isLayerShape(Layer::Mission, 1410, 749), "1410x749 is the dialog's panel, not the description");
    check(isLayerShape(Layer::Answers, 1410, 245) && !isLayerShape(Layer::Mission, 1410, 245), "1410x245 stays the answers");
    for (int k = 0; k < kLayers; ++k)
        if (k != int(Layer::Trader)) check(!isLayerShape(Layer(k), 2307, 1297), "2307x1297 is no layer but the trader's");
    check(isLayerShape(Layer::Trader, 2307, 1297) && isLayerShape(Layer::Trader, 3408, 1917), "the trader's menu: 16:9, stereo and cinema");

    // The trader's layer is never found by shape -- only by what the withheld draw read.
    int other;
    FrameTally t;
    for (int i = 0; i < 9; ++i) t.note(&g_res[0], 2412, 1449, kFormatTypeless);
    for (int i = 0; i < 38; ++i) t.note(&other, 2307, 1297, kFormatTypeless);
    for (int i = 0; i < 17; ++i) t.note(&g_res[1], 1410, 761, kFormatTypeless);
    check(findLayer(Layer::Trader, t.panels, t.count) == -1, "a 16:9 panel drawn: not the trader's layer by its shape");
    int found[kLayers];
    findLayerPanels(t, found);
    check(found[int(Layer::Trader)] == -1 && found[int(Layer::Mission)] == 2 && found[int(Layer::Hud)] == 0,
          "no withheld draw: no trader; the description found by its shape");
    t.withheld = &other;
    findLayerPanels(t, found);
    check(found[int(Layer::Trader)] == 1, "the withheld draw read the 16:9 panel: that is the trader's");
    t.withheld = &g_res[2];
    findLayerPanels(t, found);
    check(found[int(Layer::Trader)] == -1, "it read a panel the frame did not draw: none this frame (the hold keeps the copy)");
    check(findPanelById(t.panels, t.count, nullptr) == -1, "no id: none");

    // The head-locked draw, recognised whole and nothing less.
    check(isHeadLockedMenuDraw(kEyePanelVs, 360, true, kFormatTypeless, 2307, 1297) &&
              isHeadLockedMenuDraw(kEyePanelVs, 360, true, kFormatUnorm, 2307, 1297),
          "the trader's draw: the eye-panel shader, 360 indices, into an eye, 2307x1297");
    check(!isHeadLockedMenuDraw(kEyePanelVs, 6, true, kFormatTypeless, 2307, 1297), "a quad of 6: not withheld");
    check(!isHeadLockedMenuDraw(kEyePanelVs, 360, false, kFormatTypeless, 2307, 1297), "not into an eye: not withheld");
    check(!isHeadLockedMenuDraw(kGuiText, 360, true, kFormatTypeless, 2307, 1297) &&
              !isHeadLockedMenuDraw(kGameCompositeA, 360, true, kFormatTypeless, 2307, 1297),
          "another shader: not withheld");
    check(!isHeadLockedMenuDraw(kEyePanelVs, 360, true, kFormatTypeless, 1410, 245) &&
              !isHeadLockedMenuDraw(kEyePanelVs, 6, true, kFormatTypeless, 288, 171),
          "the small panels the same shader lays: not withheld");
    check(!isHeadLockedMenuDraw(kEyePanelVs, 360, true, 44, 2307, 1297), "another format: not withheld");
    check(classifyEyePanelDraw(kEyePanelVs, 420, true, kFormatTypeless, 2307, 1297) == EyePanelDraw::OtherMesh &&
              classifyEyePanelDraw(kEyePanelVs, 6, true, kFormatTypeless, 2307, 1297) == EyePanelDraw::OtherMesh &&
              classifyEyePanelDraw(kEyePanelVs, 6, true, kFormatTypeless, 1410, 245) == EyePanelDraw::No,
          "a 16:9 panel on another mesh: the other-mesh line; a small panel: nothing");

    // Places at 85 degrees: the description as wide as the answers, centred, its bottom on their top; the trader the whole frame.
    const Place answers = placeOf(Layer::Answers, 85), mission = placeOf(Layer::Mission, 85);
    check(std::fabs(mission.widthDeg - 58.7f) < 0.5f && std::fabs(mission.yawDeg) < 0.01f && near(mission.curve, kMenuCurve),
          "the description: about 59 deg wide, straight ahead, a menu's bend");
    const float answersTop = answers.downM - sheetHeightM(answers, 1410.0 / 245.0) * 0.5f;
    const float missionBottom = mission.downM + sheetHeightM(mission, 1410.0 / 761.0) * 0.5f;
    check(std::fabs(answersTop - missionBottom) < 0.005f, "the description's bottom is on the answers' top");
    check(mission.downM < 0.0f, "the description stands above the eyes' line");
    const Place trader = placeOf(Layer::Trader, 85);
    check(std::fabs(trader.widthDeg - 88.9f) < 0.1f && std::fabs(trader.yawDeg) < 0.01f && std::fabs(trader.downM) < 1e-4f &&
              near(trader.curve, kCurve),
          "the trader: the whole frame, about 88.9 deg, straight ahead, not moved, the HUD's bend");
    check(std::fabs(placeOf(Layer::Trader, 60).widthDeg - 62.8f) < 0.1f, "60 deg: the trader about 62.8");

    // The trader's hold: withheld every frame but redrawn every other one -- shown throughout; its frames never keep the HUD alive.
    DecisionIn in;
    in.onFoot = 1;
    Hold traderHold, hudHold;
    HoldStep st;
    stepLayer(in, true, hudHold, &st);
    bool allShown = true;
    for (int i = 0; i < 60; ++i) allShown = allShown && stepLayer(in, i % 2 == 0, traderHold, &st) == Reason::Showing;
    check(allShown, "a trader redrawn every other frame: shown throughout");
    Reason hudReason = Reason::Showing;
    for (uint32_t i = 0; i < kHoldFrames; ++i) {
        stepLayer(in, true, traderHold, &st);
        hudReason = stepLayer(in, false, hudHold, &st);
    }
    check(hudReason == Reason::NoPanels, "the trader's frames do not keep the HUD's copy");
    Reason traderReason = Reason::Showing;
    for (uint32_t i = 0; i < kHoldFrames; ++i) traderReason = stepLayer(in, false, traderHold, &st);
    check(traderReason == Reason::NoPanels, "the menu closed (no withheld draw): the trader stops after 30 frames");

    // Lines.
    char line[1200];
    Panel menu;
    menu.w = 2307;
    menu.h = 1297;
    menu.draws = 38;
    formatMenuShowing(Layer::Trader, menu, trader, Mode::Body, line, sizeof(line));
    check(has(line, "on-foot hud: showing the trader menu (2307x1297, aspect 1.779, 38 interface draws) at its place: 89 deg wide") &&
              has(line, "curve 1.0, anchor body; the game's own head-locked copy withheld"),
          "the trader's line: its place, and the game's copy withheld");
    Panel description;
    description.w = 1410;
    description.h = 761;
    formatMenuShowing(Layer::Mission, description, mission, Mode::Body, line, sizeof(line));
    check(has(line, "showing the mission description (1410x761, aspect 1.853") && has(line, "m up") && !has(line, "withheld"),
          "the description's line: above, nothing withheld");
    formatMenuStopped(Layer::Trader, Reason::NoPanels, line, sizeof(line));
    check(std::strcmp(line, "on-foot hud: the trader menu stopped (closed: not drawn for 30 frames)") == 0, "the trader closed");
    formatOtherMesh(2307, 1297, 420, line, sizeof(line));
    check(has(line, "2307x1297 panel") && has(line, "mesh of 420 indices") && has(line, "trader menu's 360") && has(line, "Said once."),
          "the other-mesh line names the size and the count");
    Summary s;
    s.frames = 900;
    s.menus[int(Layer::Trader)].shown = 880;
    s.menus[int(Layer::Trader)].held = 400;
    s.menus[int(Layer::Mission)].shown = 300;
    s.withheld = 1760;
    SeenSizes seen;
    formatSummary(s, t.panels, t.count, 0, 0, seen, line, sizeof(line));
    check(has(line, "menus=[mission shown=300 held=0, trader shown=880 held=400] withheld=1760 new-panels=[]"),
          "the summary: the description, the trader and the withheld draws");
    s.withheld = 0;
    formatSummary(s, t.panels, t.count, 0, 0, seen, line, sizeof(line));
    check(!has(line, "withheld"), "nothing withheld: no withheld field");
}

void partMenuLatch() {
    DecisionLatch l;
    l.quietUntilShown = true;
    check(!l.admit(Reason::InTheShip, 0, 0, Mode::Body), "a menu never shown: its stop is not written");
    check(!l.admit(Reason::NoPanels, 0, 0, Mode::Body), "nor its no-panels");
    check(l.admit(Reason::Showing, 1029, 949, Mode::Body), "its opening is");
    check(l.admit(Reason::NoPanels, 1029, 949, Mode::Body), "its closing is");
    check(!l.admit(Reason::InTheShip, 0, 0, Mode::Body), "after the closing, nothing more until it opens again");
    check(l.admit(Reason::Showing, 1029, 949, Mode::Body) && l.windowLines == 3, "it opens again: written, and every line counted");
    DecisionLatch h;
    check(h.admit(Reason::InTheShip, 0, 0, Mode::Body), "the HUD's latch writes its stops as on .24");
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
    check(l.admit(Reason::InTheShip, 0, 0, Mode::Visor), "the first decision is written");
    check(!l.admit(Reason::InTheShip, 0, 0, Mode::Visor), "the same decision is not");
    check(l.admit(Reason::Showing, 2412, 1449, Mode::Visor), "showing is written");
    check(!l.admit(Reason::Showing, 2412, 1449, Mode::Visor), "showing again is not");
    check(l.admit(Reason::Showing, 3564, 2140, Mode::Visor), "another size is written");
    check(l.admit(Reason::Showing, 3564, 2140, Mode::Body), "another anchor is written");
    check(l.admit(Reason::InTheShip, 3564, 2140, Mode::Body), "the stop is written");
    check(!l.admit(Reason::InTheShip, 1, 1, Mode::Visor), "a stopped decision ignores the panel");
    // The budget: a flicker past it is counted, not written.
    DecisionLatch f;
    int written = 0;
    for (uint32_t i = 0; i < kChangeBudget + 10; ++i)
        written += f.admit(i % 2 ? Reason::NoPanels : Reason::Showing, 10, 10, Mode::Visor) ? 1 : 0;
    check(written == int(kChangeBudget) && f.unwritten == 10, "a flicker: the budget's lines written, the rest counted");
    f.newWindow();
    check(f.admit(Reason::InTheShip, 0, 0, Mode::Visor), "a new window writes again");
}

void partLines() {
    char line[1200];
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
    formatShowing(t.panels[0], Mode::Body, 85, line, sizeof(line));
    check(std::strcmp(line, "on-foot hud: showing the HUD panel (2412x1449, aspect 1.665, 70 interface draws), anchor body, 85 deg, "
                            "cylinder around the head") == 0,
          "the showing line");
    Panel lift;
    lift.w = 1029;
    lift.h = 949;
    lift.draws = 40;
    formatMenuShowing(Layer::Lift, lift, placeOf(Layer::Lift, 85), Mode::Body, line, sizeof(line));
    check(std::strcmp(line, "on-foot hud: showing the lift and door menu (1029x949, aspect 1.084, 40 interface draws) at its place: "
                            "56 deg wide, 0 deg right, 0.04 m up, curve 0.4, anchor body") == 0,
          "the lift's showing line, with its place");
    Panel answers;
    answers.w = 1410;
    answers.h = 245;
    answers.draws = 12;
    formatMenuShowing(Layer::Answers, answers, placeOf(Layer::Answers, 85), Mode::Visor, line, sizeof(line));
    check(has(line, "showing the answers (1410x245, aspect 5.755") && has(line, "0.38 m down") && has(line, "anchor visor"),
          "the answers' showing line: below");
    Panel dialog;
    dialog.w = 1410;
    dialog.h = 749;
    formatMenuShowing(Layer::Dialog, dialog, placeOf(Layer::Dialog, 85), Mode::Body, line, sizeof(line));
    check(has(line, "the second dialog panel") && has(line, "14 deg right") && has(line, "m up"), "the dialog's line: right, above");
    formatMenuStopped(Layer::Lift, Reason::NoPanels, line, sizeof(line));
    check(std::strcmp(line, "on-foot hud: the lift and door menu stopped (closed: not drawn for 30 frames)") == 0, "the lift closed");
    formatMenuStopped(Layer::Answers, Reason::GameComposites, line, sizeof(line));
    check(std::strcmp(line, "on-foot hud: the answers stopped (the game composites its panels)") == 0, "the answers: the game lays them");
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
    // The menus that were shown in the window; one with zeros is not written.
    Summary withMenus = s;
    withMenus.menus[int(Layer::Lift)].shown = 900;
    withMenus.menus[int(Layer::Lift)].held = 12;
    withMenus.menus[int(Layer::Dialog)].shown = 30;
    SeenSizes quiet;
    formatSummary(withMenus, t.panels, t.count, 0, 0, quiet, line, sizeof(line));
    check(has(line, "copy-fail=0 menus=[lift shown=900 held=12, dialog shown=30 held=0] new-panels=[]") && !has(line, "answers"),
          "the summary's menus: the lift and the dialog, not the answers that were never shown");
    check(has(line, "frames=2700 shown=2690 held=40"), "the HUD's own counters as on .24");
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
    partLayers();
    partPlaces();
    partPlaceAnchor();
    partLayerSteps();
    partMissionTrader();
    partMenuLatch();
    partHold();
    partArc();
    partNewPanels();
    partDecision();
    partAnchor();
    partKeys();
    partLatch();
    partLines();
    std::printf("%s: on_foot_hud_test, %d checks, %d failed\n", g_failures ? "FAIL" : "PASS", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
