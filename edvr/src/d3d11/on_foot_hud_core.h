#pragma once
// The on-foot HUD, the pure half (EDVRonFOOT; experimental.native_vr_on_foot_hud; the project's docs\research\native-vr-on-foot.md,
// "Полёт .9", and on_foot_hud.h for the glue). Everything here is plain C++ the rig (tools\on_foot_hud_test) drives directly: which
// interface panels the frame drew and which of them is shown, whether the sheet is shown this frame and why not, the body anchor, the
// two keys' reading, and the text of every line.
//
// WHAT THE FLIGHT FOUND. With native VR on foot the game still draws the whole on-foot interface -- bars, compass, crosshair, the
// gadget wheel, the dialogues -- with its three interface shaders into hidden panels (R8G8B8A8_TYPELESS, viewed UNORM; 2412x1449 the
// one that takes most of them, 2768x2383 and 2162x1922 the others). In the cinema the game lays those panels into the picture with
// vs B10B032BDFD46700 and vs C4B4B334B26E81A9; in headset stereo it skips that step and nobody reads them. The glue shows the panel
// with most interface draws as a curved sheet in the eye, at the door, the way the F8 menu is drawn.
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

// A frame's panels as they arrive: a draw adds to its target's count, a new target takes the next slot, a ninth is dropped and
// counted. Returns the slot, -1 when dropped.
struct FrameTally {
    Panel panels[kMaxPanels];
    int count = 0;
    uint32_t dropped = 0;
    bool gameComposites = false;
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

// The scouting key's choice: 0 the main panel, 1 the second, 2 the third... A press steps to the next panel the frame has and from the
// last back to the main one. A choice the frame no longer has (fewer panels than before) reads as the main panel, and the next press
// steps on from there.
struct Selector {
    int index = 0;
    int effective(int count) const { return index < count ? index : 0; }
    int press(int count) {
        index = count > 0 ? (effective(count) + 1) % count : 0;
        return index;
    }
};

// experimental.native_vr_on_foot_hud: visor (to the head), body (in front of the body), off. Missing or empty is visor; anything else
// is visor too, with *valid false so the glue says so once.
enum class Mode : int { Visor = 0, Body = 1, Off = 2 };
inline const char* modeName(Mode m) { return m == Mode::Body ? "body" : m == Mode::Off ? "off" : "visor"; }
inline Mode parseMode(const char* text, bool* valid) {
    if (valid) *valid = true;
    if (!text) return Mode::Visor;
    while (*text == ' ' || *text == '\t') ++text;
    size_t n = std::strlen(text);
    while (n && (text[n - 1] == ' ' || text[n - 1] == '\t' || text[n - 1] == '\r' || text[n - 1] == '\n')) --n;
    if (!n) return Mode::Visor;
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
    return Mode::Visor;
}

// experimental.native_vr_on_foot_hud_size: the sheet's width in degrees, 30..110; anything else (NaN included) is 70, *valid false.
constexpr float kWidthDefault = 70.0f, kWidthMin = 30.0f, kWidthMax = 110.0f;
inline float parseWidth(float raw, bool* valid) {
    const bool ok = raw >= kWidthMin && raw <= kWidthMax;
    if (valid) *valid = ok;
    return ok ? raw : kWidthDefault;
}

// The sheet's place, the F8 menu's: 1.4 m out, the menu's default wrap. Its half-width along the surface from the angle; its height
// from the panel's proportions.
constexpr float kDistance = 1.4f;
constexpr float kCurve = 0.2f;
inline float halfWidth(float widthDeg) { return kDistance * std::tan(widthDeg * 0.5f * 0.0174532925f); }
inline float aspectOf(uint32_t w, uint32_t h) { return w ? float(h) / float(w) : 0.0f; }

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

// Shown this frame, or why not. Order: the keys, then where the commander is, then the game's own composite, then the panels.
enum class Reason : int { Showing = 0, KeyOff = 1, InTheShip = 2, GameComposites = 3, NoPanels = 4 };
inline const char* reasonText(Reason r) {
    switch (r) {
        case Reason::Showing: return "showing";
        case Reason::KeyOff: return "key off";
        case Reason::InTheShip: return "in the ship";
        case Reason::GameComposites: return "the game composites its panels";
        case Reason::NoPanels: return "no panels";
    }
    return "?";
}
struct DecisionIn {
    int onFoot = -1;           // the journal: 1 on foot, 0 not, -1 not known yet (read as not on foot)
    bool nativeOnFoot = true;  // experimental.native_vr_on_foot
    Mode mode = Mode::Visor;
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

// A decision line only when the decision changes: the reason, and while showing also the panel's number and size and the anchor. A
// flicker cannot flood the log: past kChangeBudget change lines in one summary window the rest are counted, not written, and the
// summary says how many.
constexpr uint32_t kChangeBudget = 20;
struct DecisionLatch {
    bool said = false;
    Reason reason = Reason::KeyOff;
    int index = -1;
    uint32_t w = 0, h = 0;
    Mode mode = Mode::Visor;
    uint32_t windowLines = 0, unwritten = 0;
    bool changed(Reason r, int idx, uint32_t pw, uint32_t ph, Mode m) const {
        if (!said || r != reason) return true;
        return r == Reason::Showing && (idx != index || pw != w || ph != h || m != mode);
    }
    // True when the change is to be written.
    bool admit(Reason r, int idx, uint32_t pw, uint32_t ph, Mode m) {
        if (!changed(r, idx, pw, ph, m)) return false;
        said = true;
        reason = r;
        index = idx;
        w = pw;
        h = ph;
        mode = m;
        if (windowLines >= kChangeBudget) {
            ++unwritten;
            return false;
        }
        ++windowLines;
        return true;
    }
    void newWindow() { windowLines = 0; }
};

// The 30-second summary's counters.
constexpr uint64_t kSummaryMs = 30000;
struct Summary {
    uint64_t frames = 0, shown = 0, gameComposites = 0, copyFail = 0, noPanels = 0;
    void clear() { *this = Summary{}; }
};

// ---- the lines ----

inline int formatKey(Mode mode, bool modeValid, const char* modeText, float width, bool widthValid, float widthRaw, char* out,
                     size_t cap) {
    char bad[160] = "";
    int n = 0;
    if (!modeValid)
        n += std::snprintf(bad + n, sizeof(bad) - size_t(n), "; \"%s\" is not visor, body or off, read as visor", modeText ? modeText : "");
    if (!widthValid && n >= 0 && size_t(n) < sizeof(bad))
        std::snprintf(bad + n, sizeof(bad) - size_t(n), "; size %.1f is outside 30..110, read as 70", double(widthRaw));
    const char* what = mode == Mode::Off ? "off: on foot nothing is shown"
                       : mode == Mode::Body ? "body: on foot the game's interface stands in front of the body (the crosshair is true)"
                                            : "visor: on foot the game's interface stays in front of the eyes (the crosshair follows "
                                              "the head, the weapon the body)";
    return std::snprintf(out, cap, "on-foot hud: %s (experimental.native_vr_on_foot_hud = %s, size %.0f deg)%s", what, modeName(mode),
                         double(width), bad);
}

inline int formatShowing(int index, int count, const Panel& p, Mode mode, float width, char* out, size_t cap) {
    return std::snprintf(out, cap, "on-foot hud: showing panel %d of %d (%ux%u, %u interface draws), anchor %s, %.0f deg", index + 1,
                         count, p.w, p.h, p.draws, modeName(mode), double(width));
}

inline int formatStopped(Reason r, char* out, size_t cap) {
    return std::snprintf(out, cap, "on-foot hud: stopped (%s)", reasonText(r));
}

inline int formatSelected(int index, int count, const Panel* p, char* out, size_t cap) {
    if (!p || count <= 0)
        return std::snprintf(out, cap, "on-foot hud: panel key pressed, but this frame drew no interface panel; the main panel is shown "
                                       "when there is one");
    return std::snprintf(out, cap, "on-foot hud: panel %d of %d selected (%ux%u, %u draws)", index + 1, count, p->w, p->h, p->draws);
}

inline int formatSummary(const Summary& s, const Panel* p, int count, uint32_t dropped, uint32_t unwritten, char* out, size_t cap) {
    char list[400] = "";
    int n = 0;
    for (int i = 0; i < count && n >= 0 && size_t(n) < sizeof(list); ++i)
        n += std::snprintf(list + n, sizeof(list) - size_t(n), "%s%ux%u n=%u", i ? ", " : "", p[i].w, p[i].h, p[i].draws);
    char extra[120] = "";
    int e = 0;
    if (dropped) e += std::snprintf(extra + e, sizeof(extra) - size_t(e), " targets-dropped=%u", dropped);
    if (unwritten && e >= 0 && size_t(e) < sizeof(extra))
        std::snprintf(extra + e, sizeof(extra) - size_t(e), " changes-unwritten=%u", unwritten);
    return std::snprintf(out, cap,
                         "on-foot hud 30s: frames=%llu shown=%llu panels=[%s]%s game-composites=%llu no-panels=%llu copy-fail=%llu%s",
                         static_cast<unsigned long long>(s.frames), static_cast<unsigned long long>(s.shown), list,
                         count ? "" : " (no panels found)", static_cast<unsigned long long>(s.gameComposites),
                         static_cast<unsigned long long>(s.noPanels), static_cast<unsigned long long>(s.copyFail), extra);
}

}  // namespace ofh
}  // namespace edvr
