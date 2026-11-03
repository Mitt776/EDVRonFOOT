#pragma once
// The on-foot HUD, the pure half (EDVRonFOOT; experimental.native_vr_on_foot_hud; the project's docs\research\native-vr-on-foot.md,
// "Полёт .9", and on_foot_hud.h for the glue). Everything here is plain C++ the rig (tools\on_foot_hud_test) drives directly: which
// interface panels the frame drew and which of them is shown, whether the sheet is shown this frame and why not, the body anchor, the
// two keys' reading, and the text of every line.
//
// WHAT THE FLIGHT FOUND. With native VR on foot the game still draws the whole on-foot interface -- bars, compass, crosshair, the
// gadget wheel, the dialogues -- with its three interface shaders into hidden panels (R8G8B8A8_TYPELESS, viewed UNORM; 2412x1449 the
// one that takes most of them, 2768x2383 and 2162x1922 the others). In the cinema the game lays those panels into the picture with
// vs B10B032BDFD46700 and vs C4B4B334B26E81A9; in headset stereo it skips that step and nobody reads the HUD panel. The glue shows the
// HUD panel -- the one with the HUD's proportions, 1.665 -- as a sheet bent round the head, in the eye, at the door, the way the F8
// menu is drawn (.24: the panel by its shape, not by its draw count; held through the frames the game does not redraw it).
//
// .25 (the menus): the game draws its lift and ship-door menu, the NPC's answers and the dialog's second panel in stereo into panels
// of their own (1029x949, 1410x245; .24) and lays them into the picture only in the cinema. Each is a layer of its own -- found by its
// proportions, held, stopped by the game's own composite -- shown at the place the cinema's frame gives it (.22), sized from the
// HUD's width, over the HUD.
//
// .27 (the census walks of 26.10): the NPC's mission description is a panel of its own too (1410x761), shown right above the answers.
// The materials trader's menu (2307x1297, 16:9) the game DOES lay into the eyes itself, on a curved mesh fixed to the eyes; the draw
// hook withholds that one draw and the menu is shown as a layer of its own in front of the body. .28: 60/85 of the HUD's width, and
// the panel copied straight from the withheld draw's own texture -- the biologist's and the outfitter's menus come the same way but
// are not always drawn by the interface shaders, so a draw is never withheld without its sheet.
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace edvr {
namespace ofh {

// The three interface vertex shaders (ui_depth.cpp's kGuiVector/kGuiText/kGuiIcons, gui_draw_snapshot.h): a draw with one of them
// into a target that is not an eye is the interface being built.
constexpr uint64_t kGuiVector = 0x666EF0C4C616F67Eull;
constexpr uint64_t kGuiText = 0x1012E00B3CB44469ull;
constexpr uint64_t kGuiIcons = 0xA3E5D3FCBC1165F8ull;
// The game laying its panels into the picture (the cinema's composite): any draw with one of these in the frame means the game shows
// its interface itself, and the sheet is not drawn over it.
constexpr uint64_t kGameCompositeA = 0xB10B032BDFD46700ull;
constexpr uint64_t kGameCompositeB = 0xC4B4B334B26E81A9ull;

inline bool isGuiVs(uint64_t vs) { return vs == kGuiVector || vs == kGuiText || vs == kGuiIcons; }
inline bool isGameCompositeVs(uint64_t vs) { return vs == kGameCompositeA || vs == kGameCompositeB; }
// The game laying a panel straight into the eyes (the census of 26.10): one draw per eye with this vertex shader, the panel in pixel
// slot 1. The trader's menu comes on a curved mesh of 360 indices; the same shader lays small panels (288x171, a 1410x245) as quads of
// 6, with or without the trader, and those are left alone.
// .48 (the flight of .47, 04.11): the TAB wheel's Summary (1730x973 at HMD quality 0.75) comes a second on the 360 mesh, then only on
// the wide mesh of 5760 indices -- the game's own way of laying a full-screen menu into the eyes (loader_panel.h). The same mesh on the
// same 16:9 panel carries the loading dialog when the game starts with the commander on foot (31.10, 01.11), and then the game draws no
// HUD panel at all: the wide mesh is withheld only while a HUD panel was drawn in the last kHoldFrames frames.
constexpr uint64_t kEyePanelVs = 0xA888D51024D9798Eull;
constexpr uint32_t kHeadLockedMenuIndices = 360;
constexpr uint32_t kWideMenuIndices = 5760;
constexpr int kEyePanelSlot = 1;

// R8G8B8A8_TYPELESS (27) or its UNORM view (28), one sample: the shape of every panel the census found. Anything else is not taken.
constexpr uint32_t kFormatTypeless = 27;
constexpr uint32_t kFormatUnorm = 28;
inline bool panelShapeOk(uint32_t format, uint32_t samples, bool eye) {
    return !eye && samples == 1 && (format == kFormatTypeless || format == kFormatUnorm);
}

// One target the frame drew interface into. `id` is the resource's identity only.
struct Panel {
    const void* id = nullptr;
    uint32_t w = 0, h = 0, format = 0;
    uint32_t draws = 0;
};
constexpr int kMaxPanels = 8;

// The sheet's distance and the HUD's bend: curve 1.0 is a radius equal to the distance (below, "The sheet's place").
constexpr float kDistance = 1.4f;
constexpr float kCurve = 1.0f;
// The menus bend less: in the cinema's frame (.22) their bend is a quarter to a fifth of the HUD's, and they are narrower, so a
// cylinder round the eye would bend them twice the game's. 0.4 is a radius of 3.5 m, the axis behind the head; one number, the
// flight judges it.
constexpr float kMenuCurve = 0.4f;

// The layers: what each panel is, by its proportions (width over height +- 0.01, in double: in float 1.655 would fall a hair outside;
// or a height within one pixel of the width over the proportions -- .29: below HMD quality 1.0 the game draws every panel that much
// smaller, rounded to the pixel, and the answers at 0.75 come 1057x183, 5.776, a rounding the narrow panel's 0.01 does not cover),
// and its place in the cinema's 3408x1917 frame as fractions, x right, y down (.22's census; the menus lie the HUD's side up). The HUD
// is the measure: its width is kWidthDefault, the frame is as many degrees as the HUD's width over its 0.956. Of several panels of one
// shape, the largest. The order is the compositing order: the HUD first, the menus over it, the trader's menu -- the whole frame --
// over all, the shops after it (the two never come together). The mission description stands right above the answers: as wide
// (x 0.17..0.83), its bottom at the answers' top as the sheets' own proportions give it (the answers 0.66 / 5.755 / (1917/3408) =
// 0.204 of the frame high about 0.81, top 0.708; the description 0.66 / 1.853 / 0.5625 = 0.633 high, centre 0.392). The two shops
// (.30, 29.10) the game hangs in the world over the counter, turned, right of the vendor -- no place in the frame to take; Dima's
// choice: centred at the eyes, as wide as their pixels at the answers' density (1410 px = 0.66 of the frame), as high as their
// proportions give. The info panel (.47, the walks of 03.11) -- the taxi, the ship's loadout and the info items of the TAB wheel, one
// panel of the game's with whatever it holds -- the same way: centred, at the answers' density, last over all.
enum class Layer : int { Hud = 0, Lift = 1, Answers = 2, Dialog = 3, Mission = 4, Trader = 5, Biologist = 6, Arms = 7, Info = 8 };
constexpr int kLayers = 9;
struct LayerSpec {
    double aspect;
    float x0, x1, y0, y1;
    float curve;
    const char* name;   // in the lines
    const char* brief;  // in the summary
    // Fewer interface draws and the panel is not the layer's: 922x576 (1.601, one draw) lies inside the shops' 1.606, the shops are
    // drawn 16 to 44 times. Not for all: the answers come with one draw.
    uint32_t minDraws = 0;
    // The panel's width when the HUD panel is 2412 wide, 0 for any width. The game scales every panel by one HMD quality, so a shop
    // is always that share of the HUD panel's width (1410 / 2412 = 1057 / 1809); 922x576 came with 13 draws at the ship's ramp (.31,
    // 02:42:06) and is 922 / 1809, no shop.
    uint32_t widthAtHud = 0;
};
constexpr uint32_t kHudWidthRef = 2412;
constexpr LayerSpec kLayerSpecs[kLayers] = {
    {1.665, 0.022f, 0.978f, -0.017f, 1.017f, kCurve, "the HUD panel", "hud"},                    // stereo 2412x1449, cinema 3564x2140
    {1.084, 0.185f, 0.815f, -0.045f, 0.987f, kMenuCurve, "the lift and door menu", "lift"},       // stereo 1029x949, cinema 1521x1402
    {5.755, 0.17f, 0.83f, 0.69f, 0.93f, kMenuCurve, "the answers", "answers"},                   // stereo 1410x245, cinema 2083x362
    {1.882, 0.32f, 0.99f, 0.03f, 0.70f, kMenuCurve, "the second dialog panel", "dialog"},        // stereo 1410x749, cinema 2083x1107
    {1.853, 0.17f, 0.83f, 0.075f, 0.708f, kMenuCurve, "the mission description", "mission"},    // stereo 1410x761
    // stereo 2307x1297, cinema 3408x1917 (the whole frame); shown 60/85 of the HUD's width (Dima after .27): 0.956 * 60 / 85 of the frame
    {16.0 / 9.0, 0.1626f, 0.8374f, 0.0f, 1.0f, kCurve, "the trader menu", "trader"},
    // Vista Genomics: stereo 1399x961, 1049x720 at HMD quality 0.75; 0.66 * 1399 / 1410 = 0.6549 wide, 0.7997 high
    {1399.0 / 961.0, 0.1726f, 0.8274f, 0.1002f, 0.8998f, kMenuCurve, "the biologist menu", "bio", 5, 1399},
    // Pioneer Supplies: stereo 1410x878, 1057x658 at 0.75, cinema 1761x1096; 0.66 wide, 0.7306 high
    {1410.0 / 878.0, 0.17f, 0.83f, 0.1347f, 0.8653f, kMenuCurve, "the weapons menu", "arms", 5, 1410},
    // the info panel: stereo 1584x938, 1188x703 at 0.75 (1.689, not the HUD's 1.665); 0.66 * 1584 / 1410 = 0.7414 wide, 0.7805 high;
    // one draw is enough (an info item of the wheel came with 4), the HUD's share guards the shape
    {1584.0 / 938.0, 0.1293f, 0.8707f, 0.1097f, 0.8903f, kMenuCurve, "the info panel", "info", 1, 1584},
};
inline const LayerSpec& specOf(Layer l) { return kLayerSpecs[int(l)]; }
inline bool isLayerShape(Layer l, uint32_t w, uint32_t h) {
    if (!w || !h) return false;
    const double aspect = specOf(l).aspect;
    return std::fabs(double(w) / double(h) - aspect) <= 0.01 + 1e-9 || std::fabs(double(w) / aspect - double(h)) <= 1.0 + 1e-9;
}
// Is the panel as wide as its layer's share of the HUD panel's width (within 2 pixels: the two are rounded apart)? A layer without a
// share fits any width; one with a share fits none while no HUD panel has been seen (hudW 0).
inline bool fitsHud(Layer l, uint32_t w, uint32_t hudW) {
    const uint32_t share = specOf(l).widthAtHud;
    if (!share) return true;
    if (!hudW) return false;
    return std::fabs(double(w) - double(hudW) * share / kHudWidthRef) <= 2.0 + 1e-9;
}
// The trader's layer is never found by its shape: a 16:9 panel also comes once in a while on its own (.26, 01:25:30, one draw). Its
// panel is the one the withheld draw read (findPanelById), so it shows only while the game is trying to lay the menu at the head.
inline int findLayer(Layer l, const Panel* p, int n, uint32_t hudW = 0) {
    if (l == Layer::Trader) return -1;
    int best = -1;
    for (int i = 0; i < n; ++i)
        if (isLayerShape(l, p[i].w, p[i].h) && p[i].draws >= specOf(l).minDraws && fitsHud(l, p[i].w, hudW) &&
            (best < 0 || uint64_t(p[i].w) * p[i].h > uint64_t(p[best].w) * p[best].h))
            best = i;
    return best;
}
// Every layer's panel in the frame, -1 for none. The HUD first: its width this frame, else the last one seen (lastHudW, 0 for none),
// is the measure of the shops' (fitsHud) -- the game stops drawing the HUD panel while a shop is open.
inline void findLayers(const Panel* p, int n, int out[kLayers], uint32_t lastHudW) {
    out[int(Layer::Hud)] = findLayer(Layer::Hud, p, n);
    const uint32_t hudW = out[int(Layer::Hud)] >= 0 ? p[out[int(Layer::Hud)]].w : lastHudW;
    for (int k = 1; k < kLayers; ++k) out[k] = findLayer(Layer(k), p, n, hudW);
}
inline int findPanelById(const Panel* p, int n, const void* id) {
    if (!id) return -1;
    for (int i = 0; i < n; ++i)
        if (p[i].id == id) return i;
    return -1;
}

// The draw that lays the trader's menu at the head: the eye-panel shader into an eye, its panel 16:9 in the panels' format. Withhold:
// all of it, the curved mesh of 360 indices included -- the one draw the hook skips. OtherMesh: everything but the index count, the
// sign of a game build whose mesh changed (one line a session says so, with the count); the draw is left alone and the menu stays at
// the head, as before .27. .48, the wide mesh of 5760: WithholdWide while a HUD panel was drawn in the last 30 frames (`hudRecent`),
// withheld the trader's way; LoaderWide without one -- the loading dialog -- left alone, counted, no line.
enum class EyePanelDraw : int { No = 0, Withhold = 1, OtherMesh = 2, WithholdWide = 3, LoaderWide = 4 };
inline EyePanelDraw classifyEyePanelDraw(uint64_t vs, uint32_t indices, bool eye, uint32_t format, uint32_t w, uint32_t h,
                                         bool hudRecent) {
    if (vs != kEyePanelVs || !eye || !(format == kFormatTypeless || format == kFormatUnorm)) return EyePanelDraw::No;
    if (!isLayerShape(Layer::Trader, w, h)) return EyePanelDraw::No;
    if (indices == kHeadLockedMenuIndices) return EyePanelDraw::Withhold;
    if (indices == kWideMenuIndices) return hudRecent ? EyePanelDraw::WithholdWide : EyePanelDraw::LoaderWide;
    return EyePanelDraw::OtherMesh;
}
inline bool isWithheldKind(EyePanelDraw k) { return k == EyePanelDraw::Withhold || k == EyePanelDraw::WithholdWide; }
inline bool isHeadLockedMenuDraw(uint64_t vs, uint32_t indices, bool eye, uint32_t format, uint32_t w, uint32_t h, bool hudRecent) {
    return isWithheldKind(classifyEyePanelDraw(vs, indices, eye, format, w, h, hudRecent));
}

// The HUD panel is the one with the HUD's proportions, 1.665 (cinema 3564x2140, stereo 2412x1449, 1809x1086 at HMD quality 0.75: one
// shape for all, found by the layers' rule; the draw count does jump on a station).
inline bool isHudShape(uint32_t w, uint32_t h) { return isLayerShape(Layer::Hud, w, h); }
inline int findHud(const Panel* p, int n) { return findLayer(Layer::Hud, p, n); }

// Most interface draws first; equal counts, the larger area first; equal both, the order they were met (stable).
inline void rankPanels(Panel* p, int n) {
    for (int i = 1; i < n; ++i) {
        const Panel x = p[i];
        const uint64_t ax = uint64_t(x.w) * x.h;
        int j = i - 1;
        while (j >= 0 && (p[j].draws < x.draws || (p[j].draws == x.draws && uint64_t(p[j].w) * p[j].h < ax))) {
            p[j + 1] = p[j];
            --j;
        }
        p[j + 1] = x;
    }
}

// Which layers' panels does a draw of the game's composite shaders read? `read` is the resources behind its texture slots (null for an
// empty slot), `now` and `last` each layer's panel this frame and the last (null when not known yet). Bit k set: the game lays layer
// k's panel into the picture itself; a read of anything else (the crosshair's and markers' atlas, the captions) sets nothing.
inline uint32_t readsPanels(const void* const* read, int n, const void* const now[kLayers], const void* const last[kLayers]) {
    uint32_t mask = 0;
    for (int i = 0; i < n; ++i)
        for (int k = 0; k < kLayers; ++k)
            if (read[i] && (read[i] == now[k] || read[i] == last[k])) mask |= 1u << k;
    return mask;
}

// The summary's order: the HUD panel first (when the frame has one), then the others by draw count.
inline void orderPanels(Panel* p, int n) {
    rankPanels(p, n);
    const int h = findHud(p, n);
    if (h > 0) {
        const Panel x = p[h];
        for (int i = h; i > 0; --i) p[i] = p[i - 1];
        p[0] = x;
    }
}

// A frame's panels as they arrive: a draw adds to its target's count, a new target takes the next slot, a ninth is dropped and
// counted. Returns the slot, -1 when dropped.
struct FrameTally {
    Panel panels[kMaxPanels];
    int count = 0;
    uint32_t dropped = 0;
    uint32_t composites = 0;  // readsPanels' bits: the layers the game laid into the picture itself this frame
    const void* withheld = nullptr;  // the panel the withheld head-locked draw read (identity only): the trader layer's
    uint32_t withheldDraws = 0;
    uint32_t withheldWideDraws = 0;  // of them, on the wide mesh (.48)
    uint32_t withheldMesh = 0;       // the index count of the frame's first withheld draw, for the trader's showing line
    uint32_t wideLeftDraws = 0;      // wide-mesh draws left to the game: no HUD panel in the last 30 frames (the loading dialog)
    int note(const void* id, uint32_t w, uint32_t h, uint32_t format) {
        for (int i = 0; i < count; ++i)
            if (panels[i].id == id) {
                ++panels[i].draws;
                return i;
            }
        if (count == kMaxPanels) {
            ++dropped;
            return -1;
        }
        Panel& p = panels[count];
        p.id = id;
        p.w = w;
        p.h = h;
        p.format = format;
        p.draws = 1;
        return count++;
    }
    void clear() { *this = FrameTally{}; }
};

// Each layer's panel in a frame, -1 for none: by shape, the trader's by what the frame's withheld draw read -- a frame that withheld it
// but did not redraw the panel (a station draws every other frame) has none, and the layer's hold keeps the last copy.
inline void findLayerPanels(const FrameTally& t, int out[kLayers], uint32_t lastHudW) {
    findLayers(t.panels, t.count, out, lastHudW);
    out[int(Layer::Trader)] = findPanelById(t.panels, t.count, t.withheld);
}

// Copy only from a frame that drew into the panel; otherwise keep showing the last copy, and give up after kHoldFrames in a row
// without one (the game clears the panel at the start of a frame and on a station redraws it every other frame: a copy of the
// cleared frame was the flicker of .10). step() once per frame the door decides; one per layer.
constexpr uint32_t kHoldFrames = 30;
enum class HoldStep : int { Fresh = 0, Held = 1, Expired = 2 };
struct Hold {
    bool have = false;
    uint32_t missed = 0;
    HoldStep step(bool drew) {
        if (drew) {
            have = true;
            missed = 0;
            return HoldStep::Fresh;
        }
        if (!have) return HoldStep::Expired;
        if (++missed >= kHoldFrames) {
            reset();
            return HoldStep::Expired;
        }
        return HoldStep::Held;
    }
    void reset() {
        have = false;
        missed = 0;
    }
};

// The sizes of panels met while the sheet is needed, for the one line each gets (.24: where do the lift, dialog and door menus go).
// 32 places; a 33rd size is counted, not kept.
constexpr int kMaxSeenSizes = 32;
struct SeenSizes {
    uint32_t w[kMaxSeenSizes] = {}, h[kMaxSeenSizes] = {};
    int count = 0, windowFrom = 0;
    uint32_t unrecorded = 0;
    // True when the size is new and kept: the caller writes its line.
    bool note(uint32_t pw, uint32_t ph) {
        for (int i = 0; i < count; ++i)
            if (w[i] == pw && h[i] == ph) return false;
        if (count == kMaxSeenSizes) {
            ++unrecorded;
            return false;
        }
        w[count] = pw;
        h[count] = ph;
        ++count;
        return true;
    }
    void newWindow() {
        windowFrom = count;
        unrecorded = 0;
    }
};

// experimental.native_vr_on_foot_hud: visor (to the head), body (in front of the body), off. Missing or empty is body; anything else
// is body too, with *valid false so the glue says so once.
enum class Mode : int { Visor = 0, Body = 1, Off = 2 };
inline const char* modeName(Mode m) { return m == Mode::Body ? "body" : m == Mode::Off ? "off" : "visor"; }
inline Mode parseMode(const char* text, bool* valid) {
    if (valid) *valid = true;
    if (!text) return Mode::Body;
    while (*text == ' ' || *text == '\t') ++text;
    size_t n = std::strlen(text);
    while (n && (text[n - 1] == ' ' || text[n - 1] == '\t' || text[n - 1] == '\r' || text[n - 1] == '\n')) --n;
    if (!n) return Mode::Body;
    auto is = [&](const char* word) {
        if (std::strlen(word) != n) return false;
        for (size_t i = 0; i < n; ++i) {
            char c = text[i];
            if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
            if (c != word[i]) return false;
        }
        return true;
    };
    if (is("visor")) return Mode::Visor;
    if (is("body")) return Mode::Body;
    if (is("off")) return Mode::Off;
    if (valid) *valid = false;
    return Mode::Body;
}

// The HUD sheet's width in degrees: fixed (the menus are shares of it). It was a key until .45 (30..110, default 85).
constexpr float kWidthDefault = 85.0f;

// The sheet's place: a cylinder round the head, 1.4 m out. curve 1.0 is a radius equal to the distance, so the cylinder's axis is the
// anchor's own point -- the head -- and every point of the sheet is 1.4 m from the eyes. The half-width is the ARC, as menuPanelHit
// reads it (u = (theta * R - shift + halfW) / 2halfW, R = dist / curve): dist * half the angle in radians, so that the edge stands at
// half the angle from the centre. The height follows the panel's proportions. (kDistance and kCurve are at the layers above.) A menu's
// flatter cylinder takes the same half-width: an approximation, a fraction of a degree off at its edge.
inline float halfWidth(float widthDeg) { return kDistance * widthDeg * 0.5f * 0.0174532925f; }
inline float aspectOf(uint32_t w, uint32_t h) { return w ? float(h) / float(w) : 0.0f; }
inline float ratioOf(uint32_t w, uint32_t h) { return h ? float(w) / float(h) : 0.0f; }  // the lines' spect: width over height

// The body anchor: the seated (LOCAL) space's forward -- yaw only, no tilt, the identity rotation -- at the head's position. The stick
// turns the world on foot, not LOCAL, so a sheet still in LOCAL stays in front of the body and turns with the stick, while the head
// turns past it; at the head's position so that a step in the room never puts the face into it. Poses are 3x4 rows, [R | t].
inline void bodyAnchor(const float head[12], float out[12]) {
    for (int i = 0; i < 12; ++i) out[i] = 0.0f;
    out[0] = out[5] = out[10] = 1.0f;
    out[3] = head[3];
    out[7] = head[7];
    out[11] = head[11];
}

// A layer's place from the HUD's width: the cinema's frame is hudWidthDeg / 0.956 degrees of arc across, and 1917/3408 of that high at
// the sheet's distance; a layer stands at its centre's fraction of it, its width the frame's times its own. The HUD is the measure:
// exactly kWidthDefault wide, straight ahead, not moved. A positive yaw is to the right, a positive down below the anchor.
constexpr float kRadPerDeg = 0.0174532925f;
constexpr float kCinemaHeightOverWidth = 1917.0f / 3408.0f;
struct Place {
    float widthDeg = 0.0f, yawDeg = 0.0f, downM = 0.0f, curve = kCurve;
};
inline Place placeOf(Layer l, float hudWidthDeg) {
    Place p;
    p.curve = specOf(l).curve;
    if (l == Layer::Hud) {
        p.widthDeg = hudWidthDeg;
        return p;
    }
    const LayerSpec& h = specOf(Layer::Hud);
    const LayerSpec& s = specOf(l);
    const float frameDeg = hudWidthDeg / (h.x1 - h.x0);
    const float frameHeightM = kDistance * frameDeg * kRadPerDeg * kCinemaHeightOverWidth;
    p.widthDeg = frameDeg * (s.x1 - s.x0);
    p.yawDeg = frameDeg * ((s.x0 + s.x1) * 0.5f - 0.5f);
    p.downM = ((s.y0 + s.y1) * 0.5f - 0.5f) * frameHeightM;
    return p;
}

// The anchor turned about its own vertical axis by yawDeg (right positive, headLockAnchor's sense) and lowered along its own up by
// downM. The cylinder still stands round the vertical through the head and the sheet stays upright: no tilt, the rotation stays
// rigid. Poses are 3x4 rows, [R | t].
inline void placeAnchor(const float anchor[12], float yawDeg, float downM, float out[12]) {
    const float y = -yawDeg * kRadPerDeg, c = std::cos(y), s = std::sin(y);
    const float r[9] = {c, 0.0f, s, 0.0f, 1.0f, 0.0f, -s, 0.0f, c};
    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 3; ++col)
            out[row * 4 + col] = anchor[row * 4] * r[col] + anchor[row * 4 + 1] * r[3 + col] + anchor[row * 4 + 2] * r[6 + col];
        out[row * 4 + 3] = anchor[row * 4 + 3] - downM * anchor[row * 4 + 1];
    }
}

// Shown this frame, or why not. Order: the keys, then where the commander is, then the game's own composite, then the panels.
enum class Reason : int { Showing = 0, KeyOff = 1, InTheShip = 2, GameComposites = 3, NoPanels = 4 };
inline const char* reasonText(Reason r) {
    switch (r) {
        case Reason::Showing: return "showing";
        case Reason::KeyOff: return "key off";
        case Reason::InTheShip: return "in the ship";
        case Reason::GameComposites: return "the game composites its panels";
        case Reason::NoPanels: return "no panels: no HUD panel for 30 frames";
    }
    return "?";
}
struct DecisionIn {
    int onFoot = -1;           // the journal: 1 on foot, 0 not, -1 not known yet (read as not on foot)
    bool nativeOnFoot = true;  // experimental.native_vr_on_foot
    Mode mode = Mode::Body;
    bool gameComposites = false;
    int panels = 0;
};
inline Reason decide(const DecisionIn& in) {
    if (in.mode == Mode::Off || !in.nativeOnFoot) return Reason::KeyOff;
    if (in.onFoot != 1) return Reason::InTheShip;
    if (in.gameComposites) return Reason::GameComposites;
    if (in.panels <= 0) return Reason::NoPanels;
    return Reason::Showing;
}

// One layer's frame: the decision (in.gameComposites is this layer's own bit), then the hold -- a layer whose panel the frame did not
// draw keeps its copy up to kHoldFrames, then stops for no panels. Each layer steps its own hold: the HUD's frames never keep a menu
// alive, nor a menu's the HUD.
inline Reason stepLayer(DecisionIn in, bool drew, Hold& hold, HoldStep* step) {
    in.panels = 1;  // the panels are the hold's to judge
    Reason r = decide(in);
    HoldStep st = HoldStep::Expired;
    if (r == Reason::Showing) {
        st = hold.step(drew);
        if (st == HoldStep::Expired) r = Reason::NoPanels;
    } else {
        hold.reset();
    }
    if (step) *step = st;
    return r;
}

// A decision line only when the decision changes: the reason, and while showing also the panel's size and the anchor. A flicker
// cannot flood the log: past kChangeBudget change lines in one summary window the rest are counted, not written, and the summary says
// how many. A menu's latch is quiet until shown: a menu that was never open writes no stop line, so the log carries only its opening
// and its closing.
constexpr uint32_t kChangeBudget = 20;
struct DecisionLatch {
    bool said = false;
    Reason reason = Reason::KeyOff;
    uint32_t w = 0, h = 0;
    Mode mode = Mode::Visor;
    bool quietUntilShown = false;
    uint32_t windowLines = 0, unwritten = 0;
    bool changed(Reason r, uint32_t pw, uint32_t ph, Mode m) const {
        if (!said || r != reason) return true;
        return r == Reason::Showing && (pw != w || ph != h || m != mode);
    }
    // True when the change is to be written.
    bool admit(Reason r, uint32_t pw, uint32_t ph, Mode m) {
        if (!changed(r, pw, ph, m)) return false;
        const bool wasShowing = said && reason == Reason::Showing;
        said = true;
        reason = r;
        w = pw;
        h = ph;
        mode = m;
        if (quietUntilShown && r != Reason::Showing && !wasShowing) return false;
        if (windowLines >= kChangeBudget) {
            ++unwritten;
            return false;
        }
        ++windowLines;
        return true;
    }
    void newWindow() { windowLines = 0; }
};

// The 30-second summary's counters: the HUD's shown and held as on .24, each menu's in menus[] by its Layer ([0] unused), the
// game's composites and the failed copies of all layers.
constexpr uint64_t kSummaryMs = 30000;
struct LayerCount {
    uint64_t shown = 0, held = 0;
};
struct Summary {
    uint64_t frames = 0, shown = 0, held = 0, gameComposites = 0, copyFail = 0, noPanels = 0;
    uint64_t withheld = 0;  // the game's head-locked draws of the trader's menu the hook skipped
    uint64_t withheldWide = 0;  // of them, on the wide mesh of 5760 (.48)
    uint64_t wideLeft = 0;      // wide-mesh draws left to the game without a HUD panel (the loading dialog)
    LayerCount menus[kLayers];
    void clear() { *this = Summary{}; }
};

// ---- the lines ----

inline int formatKey(Mode mode, bool modeValid, const char* modeText, char* out, size_t cap) {
    char bad[160] = "";
    if (!modeValid)
        std::snprintf(bad, sizeof(bad), "; \"%s\" is not visor, body or off, read as body", modeText ? modeText : "");
    const char* what = mode == Mode::Off ? "off: on foot nothing is shown"
                       : mode == Mode::Body ? "body: on foot the game's interface stands in front of the body (the crosshair is true)"
                                            : "visor: on foot the game's interface stays in front of the eyes (the crosshair follows "
                                              "the head, the weapon the body)";
    return std::snprintf(out, cap, "on-foot hud: %s (experimental.native_vr_on_foot_hud = %s)%s", what, modeName(mode), bad);
}

// The HUD panel's start, as on .24.
inline int formatShowing(const Panel& p, Mode mode, float width, char* out, size_t cap) {
    return std::snprintf(out, cap,
                         "on-foot hud: showing the HUD panel (%ux%u, aspect %.3f, %u interface draws), anchor %s, %.0f deg, cylinder "
                         "around the head",
                         p.w, p.h, double(ratioOf(p.w, p.h)), p.draws, modeName(mode), double(width));
}

// A menu's start, with the place it was given; the trader's says its head-locked copy is withheld, and on which mesh (.48: `mesh`, the
// index count of the show's first withheld draw, 0 for not known -- then no mesh is said).
inline int formatMenuShowing(Layer l, const Panel& p, const Place& at, Mode mode, char* out, size_t cap, uint32_t mesh = 0) {
    char tail[96] = "";
    if (l == Layer::Trader) {
        if (mesh)
            std::snprintf(tail, sizeof(tail), "; the game's own head-locked copy withheld (mesh %u)", mesh);
        else
            std::snprintf(tail, sizeof(tail), "; the game's own head-locked copy withheld");
    }
    return std::snprintf(out, cap,
                         "on-foot hud: showing %s (%ux%u, aspect %.3f, %u interface draws) at its place: %.0f deg wide, %.0f deg %s, "
                         "%.2f m %s, curve %.1f, anchor %s%s",
                         specOf(l).name, p.w, p.h, double(ratioOf(p.w, p.h)), p.draws, double(at.widthDeg),
                         double(std::fabs(at.yawDeg)), at.yawDeg < -0.05f ? "left" : "right", double(std::fabs(at.downM)),
                         at.downM > 0.005f ? "down" : "up", double(at.curve), modeName(mode), tail);
}

// Once a session: the game laid a 16:9 panel into an eye with the eye-panel shader on a mesh that is neither the trader's 360 indices
// nor the wide menus' 5760 (those are never this line, .48).
inline int formatOtherMesh(uint32_t w, uint32_t h, uint32_t indices, char* out, size_t cap) {
    return std::snprintf(out, cap,
                         "on-foot hud: the game lays a %ux%u panel into the eye with vs A888D51024D9798E on a mesh of %u indices, not the "
                         "trader menu's %u or the wide menus' %u; left alone (the menu stays where the game puts it). Said once.",
                         w, h, indices, kHeadLockedMenuIndices, kWideMenuIndices);
}

// A menu's end: closed when the game stopped drawing it, else the reason.
inline int formatMenuStopped(Layer l, Reason r, char* out, size_t cap) {
    return std::snprintf(out, cap, "on-foot hud: %s stopped (%s)", specOf(l).name,
                         r == Reason::NoPanels ? "closed: not drawn for 30 frames" : reasonText(r));
}

// A size met for the first time this session while the sheet is needed.
inline int formatNewPanel(uint32_t w, uint32_t h, uint32_t draws, char* out, size_t cap) {
    return std::snprintf(out, cap, "on-foot hud: new panel %ux%u (aspect %.3f) with %u interface draws", w, h, double(ratioOf(w, h)),
                         draws);
}

inline int formatStopped(Reason r, char* out, size_t cap) {
    return std::snprintf(out, cap, "on-foot hud: stopped (%s)", reasonText(r));
}

// The 30-second summary: the frame's panels, the HUD's counters, then the menus that were shown in the window (none: no menus field,
// the line is .24's).
inline int formatSummary(const Summary& s, const Panel* p, int count, uint32_t dropped, uint32_t unwritten, const SeenSizes& seen, char* out,
                         size_t cap) {
    char list[400] = "";
    int n = 0;
    for (int i = 0; i < count && n >= 0 && size_t(n) < sizeof(list); ++i)
        n += std::snprintf(list + n, sizeof(list) - size_t(n), "%s%ux%u n=%u", i ? ", " : "", p[i].w, p[i].h, p[i].draws);
    char fresh[200] = "";  // the sizes first met in this window
    int f = 0;
    for (int i = seen.windowFrom; i < seen.count && f >= 0 && size_t(f) < sizeof(fresh); ++i)
        f += std::snprintf(fresh + f, sizeof(fresh) - size_t(f), "%s%ux%u", i > seen.windowFrom ? ", " : "", seen.w[i], seen.h[i]);
    char extra[160] = "";
    int e = 0;
    if (dropped) e += std::snprintf(extra + e, sizeof(extra) - size_t(e), " targets-dropped=%u", dropped);
    if (unwritten && e >= 0 && size_t(e) < sizeof(extra))
        e += std::snprintf(extra + e, sizeof(extra) - size_t(e), " changes-unwritten=%u", unwritten);
    if (seen.unrecorded && e >= 0 && size_t(e) < sizeof(extra))
        std::snprintf(extra + e, sizeof(extra) - size_t(e), " new-panels-unrecorded=%u", seen.unrecorded);
    char menus[320] = "";
    int m = 0;
    for (int k = 1; k < kLayers && m >= 0 && size_t(m) < sizeof(menus); ++k) {
        const LayerCount& c = s.menus[k];
        if (!c.shown && !c.held) continue;
        m += std::snprintf(menus + m, sizeof(menus) - size_t(m), "%s%s shown=%llu held=%llu", m ? ", " : " menus=[",
                           kLayerSpecs[k].brief, static_cast<unsigned long long>(c.shown), static_cast<unsigned long long>(c.held));
    }
    if (m > 0 && size_t(m) + 1 < sizeof(menus)) {
        menus[m] = ']';
        menus[m + 1] = '\0';
    }
    // .48: the wide mesh's share of the withheld, and the wide draws left to the game, each only when not zero (else .47's line).
    char withheld[120] = "";
    int w = 0;
    if (s.withheld) w += std::snprintf(withheld, sizeof(withheld), " withheld=%llu", static_cast<unsigned long long>(s.withheld));
    if (s.withheld && s.withheldWide && w >= 0 && size_t(w) < sizeof(withheld))
        w += std::snprintf(withheld + w, sizeof(withheld) - size_t(w), " (wide %llu)", static_cast<unsigned long long>(s.withheldWide));
    if (s.wideLeft && w >= 0 && size_t(w) < sizeof(withheld))
        std::snprintf(withheld + w, sizeof(withheld) - size_t(w), " wide-left=%llu", static_cast<unsigned long long>(s.wideLeft));
    return std::snprintf(out, cap,
                         "on-foot hud 30s: frames=%llu shown=%llu held=%llu panels=[%s]%s game-composites=%llu no-panels=%llu "
                         "copy-fail=%llu%s%s new-panels=[%s]%s",
                         static_cast<unsigned long long>(s.frames), static_cast<unsigned long long>(s.shown),
                         static_cast<unsigned long long>(s.held), list, count ? "" : " (no panels found)",
                         static_cast<unsigned long long>(s.gameComposites), static_cast<unsigned long long>(s.noPanels),
                         static_cast<unsigned long long>(s.copyFail), menus, withheld, fresh, extra);
}

}  // namespace ofh
}  // namespace edvr
