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
// trader is 60/85 of the HUD's width (.28; the whole frame on .27); its line says the game's copy is withheld, the summary counts
// the withheld.
//
// .31 (the two shops): Vista Genomics' 1399x961 and Pioneer Supplies' 1410x878 (1049x720 and 1057x658 at HMD quality 0.75) are
// their own layers only, and only with 5 interface draws or more -- 922x576 (1.601, one draw) is Pioneer's shape and no layer;
// both centred at the eyes, as wide as the answers' pixels give; the summary names them bio and arms. A shop is as wide as its share of
// the HUD panel's width (this frame's, else the last seen): 922x576 with 13 draws at the ship's ramp (.31) is none.
#include "../../src/d3d11/on_foot_hud_core.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

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
    // 1599x1000 is inside Pioneer's 1.606 +- 0.01 since .31; one draw keeps it off (the shops' draws, below).
    for (const auto& s : strangers)
        for (int k = 0; k < kLayers; ++k)
            if (!(s[0] == 1599 && Layer(k) == Layer::Arms))
                check(!isLayerShape(Layer(k), s[0], s[1]), "the atlas, the captions and the one-off panels: no layer");
    check(!isLayerShape(Layer::Lift, 0, 1) && !isLayerShape(Layer::Lift, 1, 0), "an empty size is no layer");

    // HMD quality 0.75 (.29's logs): every panel that much smaller, rounded to the pixel; the answers' 1057x183 is 5.776, outside
    // 5.755 +- 0.01, and is the answers by its height. Each is its own layer only.
    const uint32_t q75[][2] = {{1809, 1086}, {772, 711}, {1057, 183}};  // HUD, lift, answers
    const uint32_t q75Mission[2] = {1057, 571};
    for (int i = 0; i < 3; ++i) {
        int layers = 0;
        for (int k = 0; k < kLayers; ++k) layers += isLayerShape(Layer(k), q75[i][0], q75[i][1]) ? 1 : 0;
        check(layers == 1 && isLayerShape(Layer(i), q75[i][0], q75[i][1]), "HMD quality 0.75: each size is its own layer only");
    }
    {
        int layers = 0;
        for (int k = 0; k < kLayers; ++k) layers += isLayerShape(Layer(k), q75Mission[0], q75Mission[1]) ? 1 : 0;
        check(layers == 1 && isLayerShape(Layer::Mission, q75Mission[0], q75Mission[1]), "HMD quality 0.75: 1057x571 is the mission description only");
    }
    check(classifyEyePanelDraw(kEyePanelVs, kHeadLockedMenuIndices, true, kFormatUnorm, 1730, 973, true) == EyePanelDraw::Withhold,
          "HMD quality 0.75: the trader's 1730x973 is withheld");
    // Every layer's stereo and cinema size at HMD quality 0.50..1.00, rounded down and to the nearest: its own layer, and no other.
    const struct {
        Layer l;
        uint32_t w, h;
    } bases[] = {{Layer::Hud, 2412, 1449},     {Layer::Hud, 3564, 2140},     {Layer::Lift, 1029, 949},     {Layer::Lift, 1521, 1402},
                 {Layer::Answers, 1410, 245},  {Layer::Answers, 2083, 362},  {Layer::Dialog, 1410, 749},   {Layer::Dialog, 2083, 1107},
                 {Layer::Mission, 1410, 761},  {Layer::Trader, 2307, 1297},  {Layer::Trader, 3408, 1917},
                 {Layer::Biologist, 1399, 961}, {Layer::Arms, 1410, 878},    {Layer::Arms, 1761, 1096},  {Layer::Info, 1584, 938}};
    int scaledWrong = 0;
    for (const auto& b : bases)
        for (int q = 50; q <= 100; ++q)
            for (int nearest = 0; nearest < 2; ++nearest) {
                const double f = q / 100.0, add = nearest ? 0.5 : 0.0;
                const uint32_t w = uint32_t(std::floor(b.w * f + add)), h = uint32_t(std::floor(b.h * f + add));
                for (int k = 0; k < kLayers; ++k)
                    if (isLayerShape(Layer(k), w, h) != (Layer(k) == b.l)) ++scaledWrong;
            }
    check(scaledWrong == 0, "every layer at HMD quality 0.50..1.00: its own layer, and no other");
    // The panels at 0.75 that are no layer (.29's and .30's new-panel lines, with their draws there; 1230x769 the landing's, .24 on,
    // always one draw). 922x576 and 1230x769 are Pioneer's shape (1.601 and 1.599 inside 1.606 +- 0.01) and no layer by their one
    // draw; the rest are no layer's shape at all.
    const uint32_t strangers75[][3] = {{1384, 297, 6}, {922, 576, 1}, {2076, 1787, 7}, {2076, 1816, 82}, {1621, 1441, 32},
                                       {991, 810, 33}, {720, 720, 19}, {461, 461, 1},   {461, 691, 6},    {277, 302, 1},
                                       {1230, 769, 1}, {1599, 1000, 1}};
    for (const auto& s : strangers75) {
        const Panel one{&g_res[0], s[0], s[1], kFormatTypeless, s[2]};
        for (int k = 0; k < kLayers; ++k) {
            check(findLayer(Layer(k), &one, 1, 1809) == -1, "the panels at HMD quality 0.75 that are no layer: none");
            if (!((s[0] == 922 || s[0] == 1230 || s[0] == 1599) && Layer(k) == Layer::Arms))
                check(!isLayerShape(Layer(k), s[0], s[1]), "the panels at HMD quality 0.75: no layer's shape, the three Pioneer's only");
        }
    }
    check(isLayerShape(Layer::Arms, 922, 576) && isLayerShape(Layer::Arms, 1230, 769) && isLayerShape(Layer::Arms, 1599, 1000),
          "922x576, 1230x769 and 1599x1000 are Pioneer's shape: only their draws keep them off");

    // The two shops (.30, 29.10): each its own layer only, at 1.0 and 0.75; found with the draws the game gives them, not with 4.
    const struct {
        Layer l;
        uint32_t w, h, draws, hud;
    } shops[] = {{Layer::Biologist, 1399, 961, 16, 2412}, {Layer::Biologist, 1049, 720, 16, 1809}, {Layer::Arms, 1410, 878, 25, 2412},
                 {Layer::Arms, 1057, 658, 44, 1809}};
    for (const auto& s : shops) {
        int layers = 0;
        for (int k = 0; k < kLayers; ++k) layers += isLayerShape(Layer(k), s.w, s.h) ? 1 : 0;
        check(layers == 1 && isLayerShape(s.l, s.w, s.h), "a shop's size is its own layer only");
        const Panel drawn{&g_res[0], s.w, s.h, kFormatTypeless, s.draws}, thin{&g_res[0], s.w, s.h, kFormatTypeless, 4},
            five{&g_res[0], s.w, s.h, kFormatTypeless, 5};
        check(findLayer(s.l, &drawn, 1, s.hud) == 0 && findLayer(s.l, &five, 1, s.hud) == 0, "a shop's panel with 5 draws or more: found");
        check(findLayer(s.l, &thin, 1, s.hud) == -1, "a shop's panel with 4 draws: not found");
        check(findLayer(s.l, &drawn, 1, 0) == -1, "a shop's panel while no HUD panel has been seen: not found");
        check(findLayer(s.l, &drawn, 1, s.hud == 2412 ? 1809 : 2412) == -1, "a shop's panel beside the other quality's HUD: not found");
    }
    // Every quality 0.50..1.00, both roundings: the shop beside the HUD panel of the same quality is found.
    int shopsMissed = 0;
    for (int q = 50; q <= 100; ++q)
        for (int nearest = 0; nearest < 2; ++nearest) {
            const double f = q / 100.0, add = nearest ? 0.5 : 0.0;
            const uint32_t hud = uint32_t(std::floor(2412 * f + add));
            const Panel bio{&g_res[0], uint32_t(std::floor(1399 * f + add)), uint32_t(std::floor(961 * f + add)), kFormatTypeless, 16};
            const Panel arms{&g_res[0], uint32_t(std::floor(1410 * f + add)), uint32_t(std::floor(878 * f + add)), kFormatTypeless, 25};
            if (findLayer(Layer::Biologist, &bio, 1, hud) != 0) ++shopsMissed;
            if (findLayer(Layer::Arms, &arms, 1, hud) != 0) ++shopsMissed;
        }
    check(shopsMissed == 0, "the shops at HMD quality 0.50..1.00 beside the HUD of that quality: found");
    // .31, 02:42:06, the ship's ramp: 922x576 with 13 draws, no HUD panel in the frame, the last one 1809 wide -- no shop. The shop
    // the same way (the game stops the HUD while it is open): found by the remembered width; the HUD in the frame is used first.
    {
        FrameTally ramp;
        for (int i = 0; i < 13; ++i) ramp.note(&g_res[0], 922, 576, kFormatTypeless);
        for (int i = 0; i < 82; ++i) ramp.note(&g_res[1], 2076, 1816, kFormatTypeless);
        int out[kLayers];
        findLayers(ramp.panels, ramp.count, out, 1809);
        int any = 0;
        for (int k = 0; k < kLayers; ++k) any += out[k] >= 0 ? 1 : 0;
        check(any == 0, ".31 ramp: 922x576 with 13 draws beside a 1809 HUD is no layer");
        FrameTally shop;
        for (int i = 0; i < 44; ++i) shop.note(&g_res[0], 1057, 658, kFormatTypeless);
        findLayers(shop.panels, shop.count, out, 1809);
        check(out[int(Layer::Arms)] == 0 && out[int(Layer::Hud)] == -1, "Pioneer without the HUD in the frame: by the last HUD's width");
        findLayers(shop.panels, shop.count, out, 0);
        check(out[int(Layer::Arms)] == -1, "Pioneer before any HUD panel was seen: not shown");
        for (int i = 0; i < 12; ++i) shop.note(&g_res[2], 1809, 1086, kFormatTypeless);
        findLayers(shop.panels, shop.count, out, 2412);
        check(out[int(Layer::Arms)] == 0 && out[int(Layer::Hud)] == 1, "the HUD in the frame is the measure before the remembered one");
    }
    // The info panel (.47, the walks of 03.11: the taxi 25 draws, an info item 4, the loadout 69): 1584x938, 1188x703 at 0.75 --
    // its own layer only, found with as few as one draw beside the HUD of its quality, never with none.
    {
        const uint32_t infos[][2] = {{1584, 938}, {1188, 703}};
        for (const auto& s : infos) {
            int layers = 0;
            for (int k = 0; k < kLayers; ++k) layers += isLayerShape(Layer(k), s[0], s[1]) ? 1 : 0;
            check(layers == 1 && isLayerShape(Layer::Info, s[0], s[1]), "the info panel's size is its own layer only");
            const uint32_t hud = s[0] == 1584 ? 2412 : 1809;
            for (uint32_t draws : {25u, 4u, 1u}) {
                FrameTally f;
                for (uint32_t i = 0; i < draws; ++i) f.note(&g_res[0], s[0], s[1], kFormatTypeless);
                int out[kLayers];
                findLayers(f.panels, f.count, out, hud);
                int any = 0;
                for (int k = 0; k < kLayers; ++k) any += out[k] >= 0 ? 1 : 0;
                check(out[int(Layer::Info)] == 0 && any == 1, "the info panel with 25, 4 or 1 draws: the info layer, and no other");
            }
            const Panel none{&g_res[0], s[0], s[1], kFormatTypeless, 0};
            int anyNone = 0;
            for (int k = 0; k < kLayers; ++k) anyNone += findLayer(Layer(k), &none, 1, hud) >= 0 ? 1 : 0;
            check(anyNone == 0, "the info panel's size with no draws: no layer");
        }
        const Panel taxi{&g_res[0], 1188, 703, kFormatTypeless, 25};
        check(findLayer(Layer::Info, &taxi, 1, 1809) == 0, "1188x703 beside the HUD 1809x1086: found");
        check(findLayer(Layer::Info, &taxi, 1, 2412) == -1, "1188x703 beside the HUD 2412x1449: not found");
        check(findLayer(Layer::Info, &taxi, 1, 0) == -1, "1188x703 while no HUD panel has been seen: not found");
        FrameTally f;
        for (int i = 0; i < 25; ++i) f.note(&g_res[0], 1188, 703, kFormatTypeless);
        int out[kLayers];
        findLayers(f.panels, f.count, out, 1809);
        check(out[int(Layer::Info)] == 0 && out[int(Layer::Hud)] == -1, "the info panel without the HUD in the frame: by the last HUD's width");
        // The HUD's neighbour: 1.689 and 1.665 never mixed.
        const uint32_t huds[][2] = {{1809, 1086}, {2412, 1449}};
        for (const auto& h : huds)
            check(isLayerShape(Layer::Hud, h[0], h[1]) && !isLayerShape(Layer::Info, h[0], h[1]), "the HUD panel is the HUD only, not the info panel");
        for (const auto& s : infos)
            check(!isLayerShape(Layer::Hud, s[0], s[1]), "the info panel is not the HUD");
        FrameTally both;
        for (int i = 0; i < 9; ++i) both.note(&g_res[0], 1809, 1086, kFormatTypeless);
        for (int i = 0; i < 4; ++i) both.note(&g_res[1], 1188, 703, kFormatTypeless);
        findLayers(both.panels, both.count, out, 0);
        check(out[int(Layer::Hud)] == 0 && out[int(Layer::Info)] == 1, "the HUD and the info panel in one frame: each its own");
        // Every quality 0.50..1.00, both roundings, beside the HUD of that quality: found.
        int infoMissed = 0;
        for (int q = 50; q <= 100; ++q)
            for (int nearest = 0; nearest < 2; ++nearest) {
                const double fq = q / 100.0, add = nearest ? 0.5 : 0.0;
                const uint32_t hud = uint32_t(std::floor(2412 * fq + add));
                const Panel info{&g_res[0], uint32_t(std::floor(1584 * fq + add)), uint32_t(std::floor(938 * fq + add)), kFormatTypeless, 1};
                if (findLayer(Layer::Info, &info, 1, hud) != 0) ++infoMissed;
            }
        check(infoMissed == 0, "the info panel at HMD quality 0.50..1.00 beside the HUD of that quality: found");
    }
    check(specOf(Layer::Answers).minDraws == 0 && specOf(Layer::Hud).minDraws == 0, "the other layers need no draws: the answers come with one");
    const Panel answersOne{&g_res[0], 1057, 183, kFormatTypeless, 1};
    check(findLayer(Layer::Answers, &answersOne, 1) == 0, "the answers with one draw: found");

    // A frame of the dialog: the HUD, the answers and the captions; two answers-shaped panels, the larger taken.
    FrameTally t;
    for (int i = 0; i < 9; ++i) t.note(&g_res[0], 2412, 1449, kFormatTypeless);
    for (int i = 0; i < 20; ++i) t.note(&g_res[1], 2162, 1922, kFormatTypeless);
    for (int i = 0; i < 7; ++i) t.note(&g_res[2], 705, 122, kFormatTypeless);
    int other;
    for (int i = 0; i < 3; ++i) t.note(&other, 1410, 245, kFormatTypeless);
    int found[kLayers];
    findLayers(t.panels, t.count, found, 0);
    check(found[int(Layer::Hud)] == 0 && found[int(Layer::Answers)] == 3 && found[int(Layer::Lift)] == -1 &&
              found[int(Layer::Dialog)] == -1,
          "a dialog frame: the HUD and the answers, no lift, no second panel");
    check(t.panels[found[int(Layer::Answers)]].w == 1410, "two answers-shaped panels: the larger, not the one with more draws");
}

void partPlaces() {
    // The HUD is the measure: kWidthDefault wide, straight ahead, not moved, curve 1.
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
    // The shops: centred at the eyes, as wide as the answers' pixels give (Pioneer's 1410 as the answers', Vista's 1399).
    const Place bio = placeOf(Layer::Biologist, 85), arms = placeOf(Layer::Arms, 85);
    check(std::fabs(arms.widthDeg - answers.widthDeg) < 0.05f && std::fabs(bio.widthDeg - answers.widthDeg * 1399.0f / 1410.0f) < 0.05f,
          "the shops: as wide as the answers, by their pixels");
    check(std::fabs(bio.yawDeg) < 0.01f && std::fabs(arms.yawDeg) < 0.01f && std::fabs(bio.downM) < 0.002f && std::fabs(arms.downM) < 0.002f,
          "the shops: straight ahead, at the eyes");
    check(near(bio.curve, kMenuCurve) && near(arms.curve, kMenuCurve), "the shops: the menus' curve");
    const float ab = specOf(Layer::Arms).y1 - specOf(Layer::Arms).y0, bb = specOf(Layer::Biologist).y1 - specOf(Layer::Biologist).y0;
    check(std::fabs((specOf(Layer::Arms).x1 - specOf(Layer::Arms).x0) * 3408.0f / ab / 1917.0f - 1410.0f / 878.0f) < 0.005f &&
              std::fabs((specOf(Layer::Biologist).x1 - specOf(Layer::Biologist).x0) * 3408.0f / bb / 1917.0f - 1399.0f / 961.0f) < 0.005f,
          "the shops: as high as their proportions give");
    check(std::fabs(placeOf(Layer::Arms, 50).widthDeg - arms.widthDeg * r) < 1e-3f, "the shops at 50 deg: narrower in proportion");
    // The info panel: centred at the eyes, about 66 deg at the answers' density, as high as its proportions give.
    const Place info = placeOf(Layer::Info, 85);
    check(std::fabs(info.widthDeg - 66.0f) < 1.0f && std::fabs(info.widthDeg - answers.widthDeg * 1584.0f / 1410.0f) < 0.1f,
          "the info panel: about 66 deg wide, by its pixels at the answers' density");
    check(std::fabs(info.yawDeg) < 0.01f && std::fabs(info.downM) < 0.002f && near(info.curve, kMenuCurve),
          "the info panel: straight ahead, at the eyes, the menus' curve");
    const float ib = specOf(Layer::Info).y1 - specOf(Layer::Info).y0;
    check(std::fabs((specOf(Layer::Info).x1 - specOf(Layer::Info).x0) * 3408.0f / ib / 1917.0f - 1584.0f / 938.0f) < 0.005f,
          "the info panel: as high as its proportions give");
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
    findLayerPanels(t, found, 0);
    check(found[int(Layer::Trader)] == -1 && found[int(Layer::Mission)] == 2 && found[int(Layer::Hud)] == 0,
          "no withheld draw: no trader; the description found by its shape");
    t.withheld = &other;
    findLayerPanels(t, found, 0);
    check(found[int(Layer::Trader)] == 1, "the withheld draw read the 16:9 panel: that is the trader's");
    t.withheld = &g_res[2];
    findLayerPanels(t, found, 0);
    check(found[int(Layer::Trader)] == -1, "it read a panel the frame did not draw: none this frame (the hold keeps the copy)");
    check(findPanelById(t.panels, t.count, nullptr) == -1, "no id: none");

    // The head-locked draw, recognised whole and nothing less.
    check(isHeadLockedMenuDraw(kEyePanelVs, 360, true, kFormatTypeless, 2307, 1297, true) &&
              isHeadLockedMenuDraw(kEyePanelVs, 360, true, kFormatUnorm, 2307, 1297, true),
          "the trader's draw: the eye-panel shader, 360 indices, into an eye, 2307x1297");
    check(!isHeadLockedMenuDraw(kEyePanelVs, 6, true, kFormatTypeless, 2307, 1297, true), "a quad of 6: not withheld");
    check(!isHeadLockedMenuDraw(kEyePanelVs, 360, false, kFormatTypeless, 2307, 1297, true), "not into an eye: not withheld");
    check(!isHeadLockedMenuDraw(kGuiText, 360, true, kFormatTypeless, 2307, 1297, true) &&
              !isHeadLockedMenuDraw(kGameCompositeA, 360, true, kFormatTypeless, 2307, 1297, true),
          "another shader: not withheld");
    check(!isHeadLockedMenuDraw(kEyePanelVs, 360, true, kFormatTypeless, 1410, 245, true) &&
              !isHeadLockedMenuDraw(kEyePanelVs, 6, true, kFormatTypeless, 288, 171, true),
          "the small panels the same shader lays: not withheld");
    check(!isHeadLockedMenuDraw(kEyePanelVs, 360, true, 44, 2307, 1297, true), "another format: not withheld");
    check(classifyEyePanelDraw(kEyePanelVs, 420, true, kFormatTypeless, 2307, 1297, true) == EyePanelDraw::OtherMesh &&
              classifyEyePanelDraw(kEyePanelVs, 6, true, kFormatTypeless, 2307, 1297, true) == EyePanelDraw::OtherMesh &&
              classifyEyePanelDraw(kEyePanelVs, 6, true, kFormatTypeless, 1410, 245, true) == EyePanelDraw::No,
          "a 16:9 panel on another mesh: the other-mesh line; a small panel: nothing");

    // .48: the wide mesh of 5760 -- the Summary while a HUD panel was drawn, the loading dialog without one.
    const uint32_t sizes48[2][2] = {{1730, 973}, {2307, 1297}};
    for (const auto& z : sizes48) {
        check(classifyEyePanelDraw(kEyePanelVs, 360, true, kFormatTypeless, z[0], z[1], true) == EyePanelDraw::Withhold &&
                  classifyEyePanelDraw(kEyePanelVs, 360, true, kFormatUnorm, z[0], z[1], false) == EyePanelDraw::Withhold,
              ".48: the 360 mesh is withheld with the HUD panel and without, at both qualities");
        check(classifyEyePanelDraw(kEyePanelVs, kWideMenuIndices, true, kFormatTypeless, z[0], z[1], true) == EyePanelDraw::WithholdWide &&
                  classifyEyePanelDraw(kEyePanelVs, kWideMenuIndices, true, kFormatUnorm, z[0], z[1], true) == EyePanelDraw::WithholdWide,
              ".48: the wide mesh with a HUD panel in the last 30 frames: withheld as wide, at both qualities");
        check(classifyEyePanelDraw(kEyePanelVs, kWideMenuIndices, true, kFormatTypeless, z[0], z[1], false) == EyePanelDraw::LoaderWide,
              ".48: the wide mesh without a HUD panel (the loading dialog): left alone, no other-mesh line");
        check(classifyEyePanelDraw(kEyePanelVs, 108, true, kFormatTypeless, z[0], z[1], true) == EyePanelDraw::OtherMesh &&
                  classifyEyePanelDraw(kEyePanelVs, 6, true, kFormatTypeless, z[0], z[1], false) == EyePanelDraw::OtherMesh,
              ".48: 108 and 6 indices stay the other mesh");
    }
    check(kWideMenuIndices == 5760, ".48: the wide mesh is 5760 indices");
    check(classifyEyePanelDraw(kEyePanelVs, 5760, true, kFormatTypeless, 1410, 245, true) == EyePanelDraw::No &&
              classifyEyePanelDraw(kEyePanelVs, 5760, false, kFormatTypeless, 1730, 973, true) == EyePanelDraw::No &&
              classifyEyePanelDraw(kGameCompositeA, 5760, true, kFormatTypeless, 1730, 973, true) == EyePanelDraw::No &&
              classifyEyePanelDraw(kEyePanelVs, 5760, true, 44, 1730, 973, true) == EyePanelDraw::No,
          ".48: the wide mesh not 16:9, not into an eye, another shader or format: nothing");
    check(isHeadLockedMenuDraw(kEyePanelVs, 5760, true, kFormatTypeless, 1730, 973, true) &&
              !isHeadLockedMenuDraw(kEyePanelVs, 5760, true, kFormatTypeless, 1730, 973, false) &&
              isHeadLockedMenuDraw(kEyePanelVs, 360, true, kFormatTypeless, 1730, 973, false),
          ".48: withheld means the 360 mesh, or the wide one with a HUD panel");

    // Places at 85 degrees: the description as wide as the answers, centred, its bottom on their top; the trader the whole frame.
    const Place answers = placeOf(Layer::Answers, 85), mission = placeOf(Layer::Mission, 85);
    check(std::fabs(mission.widthDeg - 58.7f) < 0.5f && std::fabs(mission.yawDeg) < 0.01f && near(mission.curve, kMenuCurve),
          "the description: about 59 deg wide, straight ahead, a menu's bend");
    const float answersTop = answers.downM - sheetHeightM(answers, 1410.0 / 245.0) * 0.5f;
    const float missionBottom = mission.downM + sheetHeightM(mission, 1410.0 / 761.0) * 0.5f;
    check(std::fabs(answersTop - missionBottom) < 0.005f, "the description's bottom is on the answers' top");
    check(mission.downM < 0.0f, "the description stands above the eyes' line");
    const Place trader = placeOf(Layer::Trader, 85);
    check(std::fabs(trader.widthDeg - 60.0f) < 0.05f && std::fabs(trader.yawDeg) < 0.01f && std::fabs(trader.downM) < 1e-4f &&
              near(trader.curve, kCurve),
          "the trader: 60/85 of the HUD (60 deg at 85), straight ahead, not moved, the HUD's bend");
    check(std::fabs(placeOf(Layer::Trader, 60).widthDeg - 42.35f) < 0.05f, "60 deg: the trader about 42.4");

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
    check(has(line, "on-foot hud: showing the trader menu (2307x1297, aspect 1.779, 38 interface draws) at its place: 60 deg wide") &&
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
    check(has(line, "or the wide menus' 5760"), ".48: the other-mesh line names the wide mesh too");
    formatMenuShowing(Layer::Trader, menu, trader, Mode::Body, line, sizeof(line), 5760);
    check(has(line, "anchor body; the game's own head-locked copy withheld (mesh 5760)"), ".48: the trader's line on the wide mesh");
    formatMenuShowing(Layer::Trader, menu, trader, Mode::Body, line, sizeof(line), 360);
    check(has(line, "anchor body; the game's own head-locked copy withheld (mesh 360)"), ".48: the trader's line on the 360 mesh");
    formatMenuShowing(Layer::Info, menu, trader, Mode::Body, line, sizeof(line), 5760);
    check(!has(line, "mesh") && !has(line, "withheld"), ".48: no mesh on another layer's line");
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
    check(!has(line, "wide"), ".48: no wide draws: the summary as on .47");
    s.withheldWide = 40;
    formatSummary(s, t.panels, t.count, 0, 0, seen, line, sizeof(line));
    check(has(line, "trader shown=880 held=400] withheld=1760 (wide 40) new-panels=[]"), ".48: the summary with the wide share");
    s.wideLeft = 12;
    formatSummary(s, t.panels, t.count, 0, 0, seen, line, sizeof(line));
    check(has(line, "withheld=1760 (wide 40) wide-left=12 new-panels=[]"), ".48: the summary with the wide draws left to the game");
    s.withheldWide = 0;
    formatSummary(s, t.panels, t.count, 0, 0, seen, line, sizeof(line));
    check(has(line, "withheld=1760 wide-left=12 new-panels=[]"), ".48: wide-left alone");
    s.withheld = 0;
    formatSummary(s, t.panels, t.count, 0, 0, seen, line, sizeof(line));
    check(has(line, "held=400] wide-left=12 new-panels=[]") && !has(line, "withheld"), ".48: wide-left without any withheld");
    s.wideLeft = 0;
    s.withheld = 1760;
    s.withheld = 0;
    formatSummary(s, t.panels, t.count, 0, 0, seen, line, sizeof(line));
    check(!has(line, "withheld"), "nothing withheld: no withheld field");
    s.menus[int(Layer::Biologist)].shown = 120;
    s.menus[int(Layer::Arms)].held = 7;
    formatSummary(s, t.panels, t.count, 0, 0, seen, line, sizeof(line));
    check(has(line, "trader shown=880 held=400, bio shown=120 held=0, arms shown=0 held=7]"), "the summary: the shops as bio and arms");
    const Panel vista{&g_res[0], 1399, 961, kFormatTypeless, 16}, pioneer{&g_res[0], 1410, 878, kFormatTypeless, 25};
    formatMenuShowing(Layer::Biologist, vista, placeOf(Layer::Biologist, 85), Mode::Body, line, sizeof(line));
    check(has(line, "showing the biologist menu (1399x961"), "the biologist's line");
    formatMenuShowing(Layer::Arms, pioneer, placeOf(Layer::Arms, 85), Mode::Body, line, sizeof(line));
    check(has(line, "showing the weapons menu (1410x878"), "the weapons menu's line");
    s.menus[int(Layer::Info)].shown = 60;
    formatSummary(s, t.panels, t.count, 0, 0, seen, line, sizeof(line));
    check(has(line, "arms shown=0 held=7, info shown=60 held=0]"), "the summary: the info panel as info");
    const Panel taxi{&g_res[0], 1188, 703, kFormatTypeless, 25};
    formatMenuShowing(Layer::Info, taxi, placeOf(Layer::Info, 85), Mode::Body, line, sizeof(line));
    check(has(line, "showing the info panel (1188x703"), "the info panel's line");
    formatMenuStopped(Layer::Info, Reason::NoPanels, line, sizeof(line));
    check(std::strcmp(line, "on-foot hud: the info panel stopped (closed: not drawn for 30 frames)") == 0, "the info panel closed");
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
    check(near(kWidthDefault, 85), "the HUD width is 85, no key");
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
    formatKey(Mode::Visor, true, "visor", line, sizeof(line));
    check(has(line, "on-foot hud: visor:") && has(line, "experimental.native_vr_on_foot_hud = visor)") && !has(line, "size") &&
              has(line, "crosshair follows the head"),
          "the key line, visor");
    formatKey(Mode::Body, false, "head", line, sizeof(line));
    check(has(line, "\"head\" is not visor, body or off, read as body") && has(line, "on-foot hud: body:"),
          "the key line says what was wrong, and what it was read as");
    formatKey(Mode::Body, true, "", line, sizeof(line));
    check(has(line, "= body)") && !has(line, "read as"), "a missing key: the line names body, nothing wrong");
    formatKey(Mode::Off, true, "off", line, sizeof(line));
    check(has(line, "on-foot hud: off: on foot nothing is shown"), "the key line, off");
    formatKey(Mode::Body, true, "body", line, sizeof(line));
    check(has(line, "body:") && has(line, "crosshair is true"), "the key line, body");

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

// .51: the info panel's keep -- a static menu (the Summary) held while its panel holds a picture, gone when the game clears it.
void partInfoKeep() {
    // 8x8 RGBA8, pitch 40: all clear but for one pixel.
    std::vector<uint8_t> px(40 * 8, 0);
    check(!panelHasPicture(px.data(), 40, 8, 8, 1), ".51 keep: a cleared panel holds no picture");
    px[size_t(4) * 40 + 4 * 4 + 3] = 9;  // alpha 9 at (4,4)
    check(panelHasPicture(px.data(), 40, 8, 8, 1) && panelHasPicture(px.data(), 40, 8, 8, 4),
          ".51 keep: one pixel with alpha above 8 is a picture (also at every 4th pixel: (4,4) is sampled)");
    px[size_t(4) * 40 + 4 * 4 + 3] = 8;
    px[size_t(4) * 40 + 4 * 4 + 1] = 8;
    check(!panelHasPicture(px.data(), 40, 8, 8, 1), ".51 keep: 8 and below is no picture");
    px[size_t(5) * 40 + 5 * 4 + 0] = 200;  // colour at (5,5), not on the 4-grid
    check(panelHasPicture(px.data(), 40, 8, 8, 1) && !panelHasPicture(px.data(), 40, 8, 8, 4),
          ".51 keep: colour without alpha is a picture; off the sampled grid it is not seen");
    check(!panelHasPicture(nullptr, 40, 8, 8) && !panelHasPicture(px.data(), 16, 8, 8) && !panelHasPicture(px.data(), 40, 0, 8),
          ".51 keep: no data, a short pitch or no width: no picture");

    PanelKeep k;
    check(!k.armed && k.verdict() == Kept::None && !k.due(0), ".51 keep: idle: no verdict, nothing due");
    k.drawn();
    check(k.armed && k.due(1000) && k.verdict() == Kept::Pictured, ".51 keep: after a draw: pictured, a read due at once");
    k.lastRead = 1000;
    check(!k.due(1099) && k.due(1100), ".51 keep: then every 100 ms");
    k.read(true);
    k.read(false);
    check(k.verdict() == Kept::Pictured, ".51 keep: one empty read: still pictured");
    k.read(true);
    k.read(false);
    check(k.verdict() == Kept::Pictured, ".51 keep: a picture breaks the empty run");
    k.read(false);
    check(!k.armed && k.verdict() == Kept::Cleared && k.verdict() == Kept::None, ".51 keep: two empty in a row: cleared, once");
    k.drawn();
    check(k.verdict() == Kept::Pictured && k.emptyRun == 0, ".51 keep: a fresh draw arms it again");
    k.reset();
    check(!k.armed && k.verdict() == Kept::None, ".51 keep: reset");
    check(kKeepPeriodMs == 100 && kKeepEmptyToStop == 2, ".51 keep: 100 ms, 2 empty reads");

    // The hold: kept frames are held without a missed frame -- far past 30; cleared expires at once; no keep is .47's hold.
    Hold h;
    check(h.step(true) == HoldStep::Fresh, ".51 hold: a drawn frame is fresh");
    bool heldAll = true;
    for (uint32_t i = 0; i < kHoldFrames * 20; ++i) heldAll = heldAll && h.step(false, Kept::Pictured) == HoldStep::Held;
    check(heldAll && h.missed == 0, ".51 hold: a pictured panel is held 600 frames, nothing missed");
    check(h.step(false, Kept::Cleared) == HoldStep::Expired && !h.have, ".51 hold: cleared: expired at once");
    check(h.step(false, Kept::Pictured) == HoldStep::Expired, ".51 hold: nothing to hold after it");
    Hold plain;
    plain.step(true);
    HoldStep last = HoldStep::Held;
    for (uint32_t i = 0; i < kHoldFrames; ++i) last = plain.step(false);
    check(last == HoldStep::Expired, ".51 hold: without the keep: 30 frames, as on .47");
    Hold mixed;
    mixed.step(true);
    for (uint32_t i = 0; i < kHoldFrames - 1; ++i) mixed.step(false);
    check(mixed.step(false, Kept::Pictured) == HoldStep::Held && mixed.missed == 0, ".51 hold: the keep resets the missed frames");

    // stepLayer: the info layer of the Summary -- 5 drawn frames, then 600 kept: shown throughout; cleared: no panels.
    DecisionIn in;
    in.onFoot = 1;
    Hold info;
    HoldStep st;
    bool shown = true;
    for (int i = 0; i < 5; ++i) shown = shown && stepLayer(in, true, info, &st) == Reason::Showing;
    for (int i = 0; i < 600; ++i) shown = shown && stepLayer(in, false, info, &st, Kept::Pictured) == Reason::Showing;
    check(shown, ".51 the Summary: 5 drawn frames and 600 kept, shown throughout");
    check(stepLayer(in, false, info, &st, Kept::Cleared) == Reason::NoPanels && st == HoldStep::Expired,
          ".51 the Summary closed: the game cleared the panel, the sheet goes that frame");

    char line[400];
    formatMenuKept(Layer::Info, line, sizeof(line));
    check(has(line, "on-foot hud: the info panel is kept: the game stopped drawing it and its picture stays") &&
              has(line, "until the game clears the panel"),
          ".51 the kept line");
    formatMenuStopped(Layer::Info, Reason::NoPanels, line, sizeof(line), true);
    check(std::strcmp(line, "on-foot hud: the info panel stopped (closed: the game cleared the panel)") == 0, ".51 closed by clearing");
    formatMenuStopped(Layer::Info, Reason::NoPanels, line, sizeof(line));
    check(std::strcmp(line, "on-foot hud: the info panel stopped (closed: not drawn for 30 frames)") == 0, ".51 closed as before");
    formatMenuStopped(Layer::Info, Reason::InTheShip, line, sizeof(line), true);
    check(std::strcmp(line, "on-foot hud: the info panel stopped (in the ship)") == 0, ".51 another reason wins over cleared");
    Summary sum;
    sum.frames = 900;
    sum.menus[int(Layer::Info)].shown = 800;
    sum.menus[int(Layer::Info)].held = 790;
    sum.menus[int(Layer::Info)].kept = 780;
    SeenSizes seen;
    formatSummary(sum, nullptr, 0, 0, 0, seen, line, sizeof(line));
    check(has(line, "menus=[info shown=800 held=790 kept=780]"), ".51 the summary: kept frames");
    sum.menus[int(Layer::Info)].kept = 0;
    formatSummary(sum, nullptr, 0, 0, 0, seen, line, sizeof(line));
    check(has(line, "menus=[info shown=800 held=790]") && !has(line, "kept"), ".51 the summary: no kept field without them");
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
    partInfoKeep();  // .51
    std::printf("%s: on_foot_hud_test, %d checks, %d failed\n", g_failures ? "FAIL" : "PASS", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
