#pragma once
// The on-foot aim dot, the pure half (EDVRonFOOT; experimental.native_vr_on_foot_aim_dot, .43; the project's
// docs\research\native-vr-on-foot.md, "Точка без оружия (.43)", and aim_dot.h for the glue). Plain C++ the rig
// (tools\aim_dot_test) drives directly: the key's reading, the atlas by its proportions, the centre's rectangle in pixels, the
// weapon cue, the plain dot's pattern for any atlas size (.62), the reading of the centre's rows, and the text of every line.
//
// ANY SIZE (.62, on-foot-aim-dot-any-size). The atlas scales with the runtime and HMD quality (2076x1787 SteamVR 0.75, 1868x1608 Oculus
// 0.75, 1245x1072 Oculus 0.5), always 1.161 wide to high. .42's pattern is scaled by area onto each size's centre window (.62 flew it
// on trial: read, not erased); since .63 the plain dot is erased at any size, and on any size but 2076x1787 the module learns the
// plain dot from the centre in the first reads on foot (Learner below), the scaled pattern its guard.
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
// whose teal dot is erased goes here: the probe (aim_dot_probe.cpp, removed in .45 and again in .61; the tag
// v0.19.1-edvronfoot.59 has it) names the draw that lays it.
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
// 48 a channel of it; since .57 the same 48 holds the brightness of each pixel, whatever its colour. Since .62 (on-foot-aim-dot-any-size)
// it is the source of every atlas size's pattern: see Pattern below.
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

// The plain dot by its shape (.57, on-foot-aim-dot-daylight). After a weapon or a tool is put away, and after any item of the radial
// menu, the game leaves the plain dot in the atlas in red: the same picture, its R equal to the plain dot's G to the byte (.56's
// flight, 08.11; the headset shows it teal, so the atlas's colour is not the eye's). .44 (48 a channel) and .56 (its tint) both read
// it as a tool's dot and kept it. Now the colour is not looked at: a pixel's brightness is its brightest channel, and the centre is
// the plain dot when
//  - not empty: some pixel's brightness is kCorePeak or more;
//  - the shape (.60): the window's brightness adds up to kPlainSum within kSumPercent, the lit spot's box is kSpotMax or less each
//    way and does not touch the window's edge. .57 held every pixel within kPatternTolerance of kPlainDot; but the dot moved by the
//    radial menu or a tool (.59) lies a fraction of a pixel off and its edges blur 70-100 off the pattern, while its sum stays (3778,
//    3786 against 3798). The tool's own red dot in the hand (.43) adds up to 5351 (+41%), the weapon's dot (.42) to 5956 (+57%).
// kPatternTolerance stays for readRow: the window whose pixels lie closest to the pattern is the one read.
constexpr int kLit = 24, kBright = 64, kCorePeak = 128, kPatternTolerance = 48;
constexpr int kPlainSum = 3798, kSumPercent = 15;
constexpr uint32_t kSpotMax = 7;
inline int brightnessOf(const uint8_t* p) { return p[0] > p[1] ? (p[0] > p[2] ? p[0] : p[2]) : (p[1] > p[2] ? p[1] : p[2]); }

// The plain dot's pattern for one atlas size (.62, on-foot-aim-dot-any-size). Through the Oculus runtime the atlas is 1868x1608 (HMD
// quality 0.75) or 1245x1072 (0.5), in the same 1.161 proportions as .42's 2076x1787; until .61 the module gave up on any size but
// 2076x1787 and the dot showed. The pattern is the window, each pixel's brightness, the sum a plain dot adds up to and the lit spot's
// largest side:
//  - 2076x1787: .42's dot as flown (12x11, kPlainSum, kSpotMax), the decisions of .60 to the byte;
//  - any other size: the window is the centre's rectangle of that atlas (centreOf, so the search and the erase are one size), each
//    pixel the area-weighted mean of the .42 pixels its footprint covers (the game's dot is an antialiased vector shape: the mean keeps
//    its brightness, the nearest pixel would drop or double its edges), the sum kPlainSum * (w/2076) * (h/1787), the spot
//    ceil(kSpotMax * w/2076). It depends on the size alone, never on what the game drew.
// A window larger than kWindowMax a side (an atlas past ~6000 wide) has no pattern: `valid` false, read as `atlas`.
constexpr uint32_t kWindowMax = 32;
struct Pattern {
    bool valid = false, native = false;  // native: 2076x1787, .42's own
    uint32_t atlasW = 0, atlasH = 0;
    uint32_t w = 0, h = 0;  // the window
    double scale = 0;       // the atlas's width over 2076
    int sum = 0;
    uint32_t spotMax = 0;
    uint8_t px[kWindowMax * kWindowMax] = {};  // brightness, rows top down, w a row
    int at(uint32_t x, uint32_t y) const { return px[y * w + x]; }
};
// One byte a pixel of .42's 12x11 window (`src`, `srcStride` bytes from one pixel to the next, rows kPatternW pixels) scaled by area
// onto the centre window of a w x h atlas (`out`, `outStride` bytes a pixel, the window's width a row). Outside .42's window is dark.
// The test rig scales real samples, channel by channel, with the same arithmetic.
inline bool scaleWindow(const uint8_t* src, uint32_t srcStride, uint32_t w, uint32_t h, uint8_t* out, uint32_t outStride) {
    const Rect c = centreOf(w, h), c0 = centreOf(kPatternAtlasW, kPatternAtlasH);
    if (!c.w() || !c.h() || c.w() > kWindowMax || c.h() > kWindowMax) return false;
    const double sx = double(w) / kPatternAtlasW, sy = double(h) / kPatternAtlasH;
    const auto overlap = [](double a0, double a1, double b0, double b1) {
        const double lo = a0 > b0 ? a0 : b0, hi = a1 < b1 ? a1 : b1;
        return hi > lo ? hi - lo : 0.0;
    };
    for (uint32_t v = 0; v < c.h(); ++v)
        for (uint32_t u = 0; u < c.w(); ++u) {
            // The pixel's footprint in .42's atlas pixels.
            const double ax0 = (c.x0 + u) / sx, ax1 = (c.x0 + u + 1) / sx, ay0 = (c.y0 + v) / sy, ay1 = (c.y0 + v + 1) / sy;
            double acc = 0;
            for (uint32_t j = 0; j < kPatternH; ++j) {
                const double oy = overlap(ay0, ay1, double(c0.y0 + j), double(c0.y0 + j + 1));
                if (oy <= 0) continue;
                for (uint32_t i = 0; i < kPatternW; ++i) {
                    const double ox = overlap(ax0, ax1, double(c0.x0 + i), double(c0.x0 + i + 1));
                    if (ox <= 0) continue;
                    acc += ox * oy * src[size_t(j * kPatternW + i) * srcStride];
                }
            }
            const double mean = acc / ((ax1 - ax0) * (ay1 - ay0));
            const long q = std::lround(mean);
            out[size_t(v * c.w() + u) * outStride] = uint8_t(q < 0 ? 0 : (q > 255 ? 255 : q));
        }
    return true;
}
inline Pattern patternOf(uint32_t w, uint32_t h) {
    Pattern p;
    p.atlasW = w;
    p.atlasH = h;
    if (!w || !h) return p;
    uint8_t bright[kPatternW * kPatternH];
    for (uint32_t i = 0; i < kPatternW * kPatternH; ++i) bright[i] = uint8_t(brightnessOf(kPlainDot + i * 3));
    if (w == kPatternAtlasW && h == kPatternAtlasH) {
        p.valid = p.native = true;
        p.w = kPatternW;
        p.h = kPatternH;
        p.scale = 1.0;
        p.sum = kPlainSum;
        p.spotMax = kSpotMax;
        std::memcpy(p.px, bright, sizeof(bright));
        return p;
    }
    if (!scaleWindow(bright, 1, w, h, p.px, 1)) return p;
    const Rect c = centreOf(w, h);
    const double sx = double(w) / kPatternAtlasW, sy = double(h) / kPatternAtlasH;
    p.valid = true;
    p.w = c.w();
    p.h = c.h();
    p.scale = sx;
    p.sum = int(std::lround(kPlainSum * sx * sy));
    p.spotMax = uint32_t(std::ceil(kSpotMax * sx - 1e-9));
    if (p.spotMax < 1) p.spotMax = 1;
    return p;
}

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
    uint32_t at = 0;  // the window's left edge in the atlas (.60: the row's search; readCentre alone leaves it 0)
    int sum = 0;      // the window's brightness added up (.62: for the rig and the learning; not in the signature or the lines)
    bool spotOk = false;  // .63: something kCorePeak or brighter, the spot within the pattern's limit and off the window's edge (the
                          // shape without the sum: what the learning takes)
};
// `rgba`: the centre's pixels, 4 bytes each (R, G, B, A as the atlas stores them), `pitch` bytes a row; `pat`: the atlas's pattern
// (its window, sum and spot; no pattern reads as atlas).
inline CentreRead readCentre(const uint8_t* rgba, uint32_t pitch, const Pattern& pat) {
    CentreRead r;
    if (!rgba || !pat.valid) return r;
    int peak = -1, sum = 0;
    for (uint32_t y = 0; y < pat.h; ++y)
        for (uint32_t x = 0; x < pat.w; ++x) {
            const uint8_t* p = rgba + size_t(y) * pitch + size_t(x) * 4;
            const int m = brightnessOf(p);
            sum += m;
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
    r.sum = sum;
    const int off = sum > pat.sum ? sum - pat.sum : pat.sum - sum;
    r.spotOk = peak >= kCorePeak && r.box && r.bx0 > 0 && r.by0 > 0 && r.bx1 + 1 < pat.w && r.by1 + 1 < pat.h &&
               r.bx1 - r.bx0 + 1 <= pat.spotMax && r.by1 - r.by0 + 1 <= pat.spotMax;
    const bool shapeOk = off * 100 <= pat.sum * kSumPercent && r.spotOk;
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
// The plain dot along its row (.60, on-foot-aim-dot-daylight). After an item of the radial menu or a tool (the exobiologist's), the
// game lays the dot in the atlas further right on the same rows, ~230 px a time, and it stays there till the next time on foot (.59's
// snapshots, 08.11: x 1036, 1267, 1497 of 2076); the eye still shows it in the middle. So the centre's rows are read the atlas's
// whole width (a strip the pattern's window high) and the plain dot is looked for along them: every window start within the window's
// width of a column whose brightest pixel is kCorePeak or more is tried, and the window whose pixels lie closest to the pattern's
// brightness (the largest difference the smallest; a tie goes to the one nearer the centre) is read as readCentre reads the centre.
// Nothing bright in the strip: the centre's window, empty. `rgba`, `pitch`: the strip, `stripW` pixels wide (the atlas's width), from
// the atlas's x 0; `centreX0`: the centre window's left edge; `pat`: the atlas's pattern. The read's `at` is the window's left edge,
// where the erase goes.
inline int windowDistance(const uint8_t* rgba, uint32_t pitch, uint32_t x0, const Pattern& pat, int stopAt) {
    int worst = 0;
    for (uint32_t y = 0; y < pat.h; ++y)
        for (uint32_t x = 0; x < pat.w; ++x) {
            const int m = brightnessOf(rgba + size_t(y) * pitch + size_t(x0 + x) * 4);
            const int want = pat.at(x, y);
            const int d = m > want ? m - want : want - m;
            if (d > worst) {
                worst = d;
                if (worst > stopAt) return worst;
            }
        }
    return worst;
}
inline CentreRead readRow(const uint8_t* rgba, uint32_t pitch, uint32_t stripW, uint32_t centreX0, const Pattern& pat) {
    if (!rgba || !pat.valid || stripW < pat.w || centreX0 > stripW - pat.w) return readCentre(nullptr, pitch, pat);
    const uint32_t winW = pat.w, winH = pat.h;
    const uint32_t lastStart = stripW - winW;
    bool any = false;
    uint32_t best = centreX0, tried = 0;  // `tried`: window starts below it are done
    int bestD = 1 << 30;
    const auto nearer = [&](uint32_t a, uint32_t b) {
        const uint32_t da = a > centreX0 ? a - centreX0 : centreX0 - a, db = b > centreX0 ? b - centreX0 : centreX0 - b;
        return da < db;
    };
    for (uint32_t x = 0; x < stripW; ++x) {
        int peak = 0;
        for (uint32_t y = 0; y < winH && peak < kCorePeak; ++y) {
            const int m = brightnessOf(rgba + size_t(y) * pitch + size_t(x) * 4);
            if (m > peak) peak = m;
        }
        if (peak < kCorePeak) continue;
        any = true;
        uint32_t s0 = x >= winW - 1 ? x - (winW - 1) : 0;
        if (s0 < tried) s0 = tried;
        const uint32_t s1 = x < lastStart ? x : lastStart;
        for (uint32_t s = s0; s <= s1; ++s) {
            const int d = windowDistance(rgba, pitch, s, pat, bestD);
            if (d < bestD || (d == bestD && nearer(s, best))) {
                bestD = d;
                best = s;
            }
        }
        if (s1 + 1 > tried) tried = s1 + 1;
    }
    if (!any) best = centreX0;
    CentreRead r = readCentre(rgba + size_t(best) * 4, pitch, pat);
    r.at = best;
    return r;
}
// The learned pattern (.63, on-foot-aim-dot-any-size, design 3 and 5). Only on a size that is not 2076x1787: right after stepping out
// the game lays the plain dot in the centre (.59: the shift is reset). kLearnReads reads in a row with no weapon cue, the read's window
// at the centre's left edge (+-1) and dot-like (spotOk: bright, the spot within the limit, off the window's edge -- the sum is not
// asked) make the last window the pattern: its brightness, its sum; the window and the spot limit stay the scaled pattern's. The
// scaled pattern guards it: a sum more than kLearnGuardPercent off the scaled sum is refused (.62's flight: the real dot at 1245x1072
// is +13%, the tool's dot in the hand +44..+50%), the count starts again, and the scaled pattern goes on. Learned once a time on foot;
// the glue forgets it off foot and on a new size. Any other read breaks the row.
constexpr int kLearnReads = 5, kLearnGuardPercent = 25;
enum class Learn : int { None = 0, Learned, Refused };
struct Learner {
    int run = 0, refusals = 0;
    bool learned = false;
    Pattern pat;  // valid once learned
    void reset() { *this = Learner{}; }
};
// The pattern the reads go by: the learned one once there is one, else the scaled (or .42's own).
inline const Pattern& activePattern(const Learner& l, const Pattern& scaled) { return l.learned ? l.pat : scaled; }
// One read by `scaled` (as the glue read it before anything was learned): `window` the read's window (`at`), `pitch` its rows,
// `weapon` the cue of the frame copied. Learned: l.pat holds the pattern.
inline Learn learnFrom(Learner& l, const Pattern& scaled, const CentreRead& read, const uint8_t* window, uint32_t pitch,
                       uint32_t centreX0, bool weapon) {
    if (!scaled.valid || scaled.native || l.learned) return Learn::None;
    const uint32_t d = read.at > centreX0 ? read.at - centreX0 : centreX0 - read.at;
    if (weapon || !window || !read.spotOk || d > 1) {
        l.run = 0;
        return Learn::None;
    }
    if (++l.run < kLearnReads) return Learn::None;
    l.run = 0;
    const int off = read.sum > scaled.sum ? read.sum - scaled.sum : scaled.sum - read.sum;
    if (off * 100 > scaled.sum * kLearnGuardPercent) {
        ++l.refusals;
        return Learn::Refused;
    }
    Pattern p = scaled;
    for (uint32_t y = 0; y < p.h; ++y)
        for (uint32_t x = 0; x < p.w; ++x) p.px[y * p.w + x] = uint8_t(brightnessOf(window + size_t(y) * pitch + size_t(x) * 4));
    p.sum = read.sum;
    l.pat = p;
    l.learned = true;
    return Learn::Learned;
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
// the check that failed (empty, shape; atlas: no pattern or no pixels). (.62's trial mark went in .63: every size erases.)
inline void formatCentre(const CentreRead& r, char* out, size_t cap) {
    char box[48];
    if (r.box)
        std::snprintf(box, sizeof(box), "x%u..%u,y%u..%u", r.bx0, r.bx1, r.by0, r.by1);
    else
        std::snprintf(box, sizeof(box), "none");
    if (r.plain)
        std::snprintf(out, cap,
                      "aim dot: centre plain (the plain dot without a weapon: erased; lit=%u, bright=%u, box=%s, max=%u,%u,%u, at=x%u)",
                      r.lit, r.bright, box, r.maxR, r.maxG, r.maxB, r.at);
    else
        std::snprintf(out, cap,
                      "aim dot: centre other (a weapon's or a tool's dot, or nothing: kept; why=%s, lit=%u, bright=%u, box=%s, max=%u,%u,%u, "
                      "at=x%u)",
                      whyName(r.why), r.lit, r.bright, box, r.maxR, r.maxG, r.maxB, r.at);
}
// The same read for the lines (.58): a line is written on any change of these, not only of the decision -- .57's flight left the
// centre other for minutes and never said what it held after an empty read.
inline bool sameSignature(const CentreRead& a, const CentreRead& b) {
    return a.plain == b.plain && a.why == b.why && a.lit == b.lit && a.bright == b.bright && a.box == b.box && a.bx0 == b.bx0 &&
           a.bx1 == b.bx1 && a.by0 == b.by0 && a.by1 == b.by1 && a.maxR == b.maxR && a.maxG == b.maxG && a.maxB == b.maxB && a.at == b.at;
}
// The centre as it is, every kCentreNowMs on foot, outside the decision lines' quota (.58): `aim dot: centre now plain|other (…)`,
// the same signature, so a PAUSE mark finds what the centre held while a dot showed.
constexpr uint64_t kCentreNowMs = 10000;
inline void formatCentreNow(const CentreRead& r, char* out, size_t cap) {
    char line[300];
    formatCentre(r, line, sizeof(line));
    const char* head = "aim dot: centre ";
    const size_t n = std::strlen(head);
    if (std::strncmp(line, head, n) == 0)
        std::snprintf(out, cap, "aim dot: centre now %s", line + n);
    else
        std::snprintf(out, cap, "%s", line);
}
// The window's every pixel after an other line: `winH` rows top down split by '/', `winW`
// RRGGBB each split by spaces. 12x11 is ~950 characters and `cap` 1100 holds it (the log's line is 1200); a larger window (2768's
// 14x15) is cut where the next pixel would not fit, and ends in " ..." .
constexpr size_t kPixelsLineCap = 1100;
inline void formatCentrePixels(const uint8_t* rgba, uint32_t pitch, uint32_t winW, uint32_t winH, char* out, size_t cap) {
    if (!cap) return;
    int len = std::snprintf(out, cap, "aim dot: centre pixels");
    if (len < 0) return;
    constexpr size_t kCell = 9, kMore = 5;  // " / RRGGBB" at most; " ..." and the end
    for (uint32_t y = 0; y < winH; ++y)
        for (uint32_t x = 0; x < winW; ++x) {
            if (size_t(len) + kCell + kMore > cap) {
                std::snprintf(out + len, cap - size_t(len), " ...");
                return;
            }
            const uint8_t* p = rgba + size_t(y) * pitch + size_t(x) * 4;
            const int k = std::snprintf(out + len, cap - size_t(len), "%s%02X%02X%02X", x ? " " : (y ? " / " : " "), p[0], p[1], p[2]);
            if (k < 0) return;
            len += k;
        }
}
// One line a new atlas size (.62): the pattern the plain dot is looked for by there.
inline void formatPattern(const Pattern& p, char* out, size_t cap) {
    if (!p.valid)
        std::snprintf(out, cap, "aim dot: pattern for %ux%u: none (the centre is larger than %ux%u a side: nothing is erased, the dot shows)",
                      p.atlasW, p.atlasH, kWindowMax, kWindowMax);
    else
        std::snprintf(out, cap, "aim dot: pattern for %ux%u: scale %.3f (window %ux%u, sum %d, spot <= %u; %s)", p.atlasW, p.atlasH, p.scale,
                      p.w, p.h, p.sum, p.spotMax,
                      p.native ? ".42's dot as flown" : ".42's dot scaled by area; learned from the centre on foot");
}
// .63: the learning's lines, `scaled` the guard.
inline void formatLearned(const Pattern& scaled, const Learner& l, uint32_t at, char* out, size_t cap) {
    std::snprintf(out, cap, "aim dot: learned the plain dot for %ux%u (sum %d, scaled %d, at=x%u)", scaled.atlasW, scaled.atlasH, l.pat.sum,
                  scaled.sum, at);
}
inline void formatLearnRefused(const Pattern& scaled, int sum, uint32_t at, char* out, size_t cap) {
    std::snprintf(out, cap,
                  "aim dot: learn refused for %ux%u (sum %d, scaled %d, at=x%u: more than %d%% off; the scaled pattern goes on)",
                  scaled.atlasW, scaled.atlasH, sum, scaled.sum, at, kLearnGuardPercent);
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
