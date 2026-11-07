#pragma once
// The on-foot aim dot, the pure half (EDVRonFOOT; experimental.native_vr_on_foot_aim_dot, .43; the project's
// docs\research\native-vr-on-foot.md, "Точка без оружия (.43)", and aim_dot.h for the glue). Plain C++ the rig
// (tools\aim_dot_test) drives directly: the key's reading, the atlas by its proportions, the centre's rectangle in pixels, the
// weapon cue, and the text of every line.
//
// WHAT .42 FOUND. The atlas of the interface's small things is a canvas in screen coordinates: the aim dot, the weapon's crosshair and
// the jetpack's fuel arc all lie in its centre cell, so the cell cannot be erased whole. Without a weapon the dot is ~6x5 px in the
// centre (x 1035..1041, y 459..464 of 2076x1787); with a weapon the game draws two more vector draws into the atlas, of 336 and 642
// indices, before any eye reads it. Hidden: the centre, only in a frame with neither.
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace edvr {
namespace adh {

// experimental.native_vr_on_foot_aim_dot: hide (the default), show. Missing or empty is hide; anything else is hide too, with *valid
// false so the glue says so in its key line.
enum class Mode : int { Hide = 0, Show = 1 };
inline const char* modeName(Mode m) { return m == Mode::Show ? "show" : "hide"; }
inline Mode parseMode(const char* text, bool* valid) {
    if (valid) *valid = true;
    if (!text) return Mode::Hide;
    while (*text == ' ' || *text == '\t') ++text;
    size_t n = std::strlen(text);
    while (n && (text[n - 1] == ' ' || text[n - 1] == '\t' || text[n - 1] == '\r' || text[n - 1] == '\n')) --n;
    if (!n) return Mode::Hide;
    auto is = [&](const char* word) {
        if (std::strlen(word) != n) return false;
        for (size_t i = 0; i < n; ++i) {
            char c = text[i];
            if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
            if (c != word[i]) return false;
        }
        return true;
    };
    if (is("hide")) return Mode::Hide;
    if (is("show")) return Mode::Show;
    if (valid) *valid = false;
    return Mode::Hide;
}

// The atlas: width over height 1.161 +- 0.01, any size (it grows and shrinks with HMD quality), R8G8B8A8 TYPELESS (27) or UNORM (28).
constexpr double kAtlasRatio = 1.161;
constexpr double kAtlasTolerance = 0.01;
inline bool isAtlasShape(uint32_t w, uint32_t h) {
    if (!w || !h) return false;
    return std::fabs(double(w) / double(h) - kAtlasRatio) <= kAtlasTolerance + 1e-9;
}
inline bool atlasFormatOk(uint32_t format) { return format == 27 || format == 28; }

// The centre in fractions of the atlas, in pixels of the atlas found: rounded outward, held inside the atlas, never empty. x0, y0
// inclusive; x1, y1 exclusive. At 2076x1787 it is 12x11 around .42's 6x5 dot, at 2768x2383 14x15.
constexpr double kCentreU0 = 0.4975, kCentreU1 = 0.5025, kCentreV0 = 0.2553, kCentreV1 = 0.2612;
struct Rect {
    uint32_t x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    uint32_t w() const { return x1 - x0; }
    uint32_t h() const { return y1 - y0; }
};
inline void span(double f0, double f1, uint32_t size, uint32_t* a, uint32_t* b) {
    double lo = std::floor(f0 * size + 1e-9), hi = std::ceil(f1 * size - 1e-9);
    if (lo < 0) lo = 0;
    if (hi > size) hi = size;
    uint32_t l = uint32_t(lo), h = uint32_t(hi);
    if (h <= l) {  // a tiny atlas: one pixel at least, inside
        if (l >= size) l = size ? size - 1 : 0;
        h = l + 1;
    }
    *a = l;
    *b = h;
}
inline Rect centreOf(uint32_t w, uint32_t h) {
    Rect r;
    if (!w || !h) return r;
    span(kCentreU0, kCentreU1, w, &r.x0, &r.x1);
    span(kCentreV0, kCentreV1, h, &r.y0, &r.y1);
    return r;
}

// The weapon cue: a vector draw into the atlas with one of these index counts. One is enough. 336 and 642: both rifles flown (.42);
// 168 and 546: the pistol (.44, the probe's census: 546 lays its dot, which in teal is the plain dot's picture to the byte). A weapon
// whose teal dot is erased goes here: the probe (aim_dot_probe.cpp, removed in .45, in git before it) names the draw that lays it.
constexpr uint32_t kWeaponCueCounts[] = {336, 642, 168, 546};
constexpr int kWeaponCueN = int(sizeof(kWeaponCueCounts) / sizeof(kWeaponCueCounts[0]));
// The cue's index in kWeaponCueCounts, or -1.
inline int weaponCue(bool vectorShader, uint32_t count) {
    if (!vectorShader) return -1;
    for (int i = 0; i < kWeaponCueN; ++i)
        if (kWeaponCueCounts[i] == count) return i;
    return -1;
}

// The plain dot's picture at night (.44; since .57 its brightness is the shape readCentre below holds a centre to). .42's snapshot 01
// (2076x1787), R, G, B of the centre's 12x11 window, rows top down; .42's 04 is the same to the byte. .44 erased only a centre within
// 48 a channel of it; since .57 the same 48 holds the brightness of each pixel, whatever its colour. Any other atlas size has no
// pattern, so nothing is erased there (the dot shows).
constexpr uint32_t kPatternW = 12, kPatternH = 11, kPatternAtlasW = 2076, kPatternAtlasH = 1787;
constexpr uint8_t kPlainDot[kPatternW * kPatternH * 3] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 72, 106, 104, 139, 205, 201, 145, 214, 210, 78, 115, 113, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 3, 5, 5, 172, 254, 249, 173, 255, 250, 173, 255, 250, 167, 246, 241, 14, 21, 21, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 33, 49, 48, 173, 255, 250, 173, 255, 250, 173, 255, 250, 173, 255, 250, 35, 52, 51, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 117, 173, 170, 173, 255, 250, 173, 255, 250, 129, 190, 186, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 41, 60, 59, 46, 68, 67, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
};
inline bool patternFor(uint32_t w, uint32_t h) { return w == kPatternAtlasW && h == kPatternAtlasH; }

// The plain dot by its shape (.57, on-foot-aim-dot-daylight). After a weapon or a tool is put away, and after any item of the radial
// menu, the game leaves the plain dot in the atlas in red: the same picture, its R equal to the plain dot's G to the byte (.56's
// flight, 08.11; the headset shows it teal, so the atlas's colour is not the eye's). .44 (48 a channel) and .56 (its tint) both read
// it as a tool's dot and kept it. Now the colour is not looked at: a pixel's brightness is its brightest channel, and the centre is
// the plain dot when
//  - not empty: some pixel's brightness is kCorePeak or more;
//  - the shape: every one of the window's pixels is within kPatternTolerance of kPlainDot's brightness there.
// The samples: the plain dot and its red copy 0 away; the weapon's dot (.42) 15 pixels past 48, up to 202; the tool's own red dot in
// the hand (.43) 13 pixels, up to 174. The lit and bright counts are the signature only.
constexpr int kLit = 24, kBright = 64, kCorePeak = 128, kPatternTolerance = 48;
enum class Why : int { None = 0, Atlas, Empty, Shape };
inline const char* whyName(Why w) {
    switch (w) {
        case Why::None: return "none";
        case Why::Atlas: return "atlas";
        case Why::Empty: return "empty";
        case Why::Shape: return "shape";
    }
    return "?";
}
// The decision and the signature its lines carry: lit and bright pixels, the lit spot's box in the window (inclusive; none when
// nothing is lit) and the brightest pixel's colour.
struct CentreRead {
    bool plain = false;
    Why why = Why::Atlas;
    uint32_t lit = 0, bright = 0;
    bool box = false;
    uint32_t bx0 = 0, bx1 = 0, by0 = 0, by1 = 0;
    uint8_t maxR = 0, maxG = 0, maxB = 0;
};
inline int brightnessOf(const uint8_t* p) { return p[0] > p[1] ? (p[0] > p[2] ? p[0] : p[2]) : (p[1] > p[2] ? p[1] : p[2]); }
// `rgba`: the centre's pixels, 4 bytes each (R, G, B, A as the atlas stores them), `pitch` bytes a row; `w`, `h`: the atlas's size.
inline CentreRead readCentre(const uint8_t* rgba, uint32_t pitch, uint32_t w, uint32_t h) {
    CentreRead r;
    if (!rgba || !patternFor(w, h)) return r;
    bool shapeOk = true;
    int peak = -1;
    for (uint32_t y = 0; y < kPatternH; ++y)
        for (uint32_t x = 0; x < kPatternW; ++x) {
            const uint8_t* p = rgba + size_t(y) * pitch + size_t(x) * 4;
            const int m = brightnessOf(p), want = brightnessOf(kPlainDot + (y * kPatternW + x) * 3);
            if (m - want > kPatternTolerance || want - m > kPatternTolerance) shapeOk = false;
            if (m > peak) {
                peak = m;
                r.maxR = p[0];
                r.maxG = p[1];
                r.maxB = p[2];
            }
            if (m < kLit) continue;
            ++r.lit;
            if (m >= kBright) ++r.bright;
            if (!r.box) {
                r.box = true;
                r.bx0 = r.bx1 = x;
                r.by0 = r.by1 = y;
            } else {
                if (x < r.bx0) r.bx0 = x;
                if (x > r.bx1) r.bx1 = x;
                if (y < r.by0) r.by0 = y;
                if (y > r.by1) r.by1 = y;
            }
        }
    if (peak < kCorePeak)
        r.why = Why::Empty;
    else if (!shapeOk)
        r.why = Why::Shape;
    else {
        r.why = Why::None;
        r.plain = true;
    }
    return r;
}
// The decision's lines, on each change: at most kCentreLinesPerWindow a summary window, the rest counted.
constexpr int kCentreLinesPerWindow = 20;

constexpr uint64_t kSummaryMs = 30000;

// -- the lines --
inline void formatKey(Mode m, bool valid, const char* text, char* out, size_t cap) {
    if (valid)
        std::snprintf(out, cap,
                      "aim dot: key %s (experimental.native_vr_on_foot_aim_dot; hide: no aim dot on foot without a weapon, show: as "
                      "the game draws it; on foot, with native_vr_on_foot and the on-foot HUD on)",
                      modeName(m));
    else
        std::snprintf(out, cap,
                      "aim dot: key hide (experimental.native_vr_on_foot_aim_dot = '%s' is not understood, reads hide: hide or show)",
                      text ? text : "");
}
inline void formatAtlasFound(uint32_t w, uint32_t h, uint32_t format, const Rect& r, char* out, size_t cap) {
    std::snprintf(out, cap, "aim dot: atlas found %ux%u (format %u), the centre x %u..%u y %u..%u (%ux%u)", w, h, format, r.x0, r.x1, r.y0,
                  r.y1, r.w(), r.h());
}
inline void formatAtlasSize(uint32_t w0, uint32_t h0, uint32_t w, uint32_t h, const Rect& r, char* out, size_t cap) {
    std::snprintf(out, cap, "aim dot: atlas size %ux%u -> %ux%u, the centre x %u..%u y %u..%u (%ux%u)", w0, h0, w, h, r.x0, r.x1, r.y0,
                  r.y1, r.w(), r.h());
}
// `seen`: one bit per kWeaponCueCounts entry met in the frame.
inline void formatWeaponOn(uint32_t seen, char* out, size_t cap) {
    int len = std::snprintf(out, cap, "aim dot: weapon on (vector n=");
    bool first = true;
    for (int i = 0; i < kWeaponCueN && len > 0 && size_t(len) < cap; ++i) {
        if (!(seen & (1u << i))) continue;
        const int k = std::snprintf(out + len, cap - size_t(len), "%s%u", first ? "" : ",", kWeaponCueCounts[i]);
        if (k < 0) break;
        len += k;
        first = false;
    }
    if (len > 0 && size_t(len) + 2 <= cap) std::snprintf(out + len, cap - size_t(len), ")");
}
inline void formatWeaponOff(char* out, size_t cap) { std::snprintf(out, cap, "aim dot: weapon off"); }
// The decision with the read's signature (.56): lit and bright pixels, the spot's box in the window, the brightest colour; for other
// the check that failed (empty, shape; atlas for another atlas size).
inline void formatCentre(const CentreRead& r, char* out, size_t cap) {
    char box[48];
    if (r.box)
        std::snprintf(box, sizeof(box), "x%u..%u,y%u..%u", r.bx0, r.bx1, r.by0, r.by1);
    else
        std::snprintf(box, sizeof(box), "none");
    if (r.plain)
        std::snprintf(out, cap, "aim dot: centre plain (the plain dot without a weapon: erased; lit=%u, bright=%u, box=%s, max=%u,%u,%u)",
                      r.lit, r.bright, box, r.maxR, r.maxG, r.maxB);
    else
        std::snprintf(out, cap,
                      "aim dot: centre other (a weapon's or a tool's dot, or nothing: kept; why=%s, lit=%u, bright=%u, box=%s, max=%u,%u,%u)",
                      whyName(r.why), r.lit, r.bright, box, r.maxR, r.maxG, r.maxB);
}
// The window's every pixel after an other line: kPatternH rows top down split by '/', kPatternW RRGGBB each split by spaces. ~950
// characters; `cap` 1100 holds it (the log's line is 1200).
constexpr size_t kPixelsLineCap = 1100;
inline void formatCentrePixels(const uint8_t* rgba, uint32_t pitch, char* out, size_t cap) {
    if (!cap) return;
    int len = std::snprintf(out, cap, "aim dot: centre pixels");
    for (uint32_t y = 0; y < kPatternH && len > 0 && size_t(len) < cap; ++y)
        for (uint32_t x = 0; x < kPatternW && len > 0 && size_t(len) < cap; ++x) {
            const uint8_t* p = rgba + size_t(y) * pitch + size_t(x) * 4;
            const int k = std::snprintf(out + len, cap - size_t(len), "%s%02X%02X%02X", x ? " " : (y ? " / " : " "), p[0], p[1], p[2]);
            if (k < 0) break;
            len += k;
        }
}
inline void formatNoPattern(uint32_t w, uint32_t h, char* out, size_t cap) {
    std::snprintf(out, cap, "aim dot: the plain dot's pattern is for a %ux%u atlas, this one is %ux%u: nothing is erased (the dot shows)",
                  kPatternAtlasW, kPatternAtlasH, w, h);
}
inline void formatInert(const char* why, char* out, size_t cap) {
    std::snprintf(out, cap, "aim dot: inert: %s; the picture is as with show until the key changes", why);
}

struct Summary {
    uint64_t atlasFrames = 0, weaponFrames = 0, hidden = 0, rewrites = 0, plain = 0, other = 0, unread = 0, quietLines = 0, failures = 0;
    void clear() { *this = Summary{}; }
};
inline void formatSummary(Mode m, uint32_t w, uint32_t h, const Summary& s, char* out, size_t cap) {
    std::snprintf(out, cap,
                  "aim dot: summary (%s, 30 s on foot): atlas %ux%u, atlas_frames=%llu, weapon_frames=%llu, hidden=%llu, rewrites=%llu, "
                  "plain=%llu, other=%llu, unread=%llu, quiet_lines=%llu, failures=%llu",
                  modeName(m), w, h, (unsigned long long)s.atlasFrames, (unsigned long long)s.weaponFrames, (unsigned long long)s.hidden,
                  (unsigned long long)s.rewrites, (unsigned long long)s.plain, (unsigned long long)s.other, (unsigned long long)s.unread,
                  (unsigned long long)s.quietLines, (unsigned long long)s.failures);
}

}  // namespace adh
}  // namespace edvr
