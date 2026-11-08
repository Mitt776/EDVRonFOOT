// Build gate for the EDVRonFOOT on-foot aim dot (src\d3d11\aim_dot_core.h, experimental.native_vr_on_foot_aim_dot, .43).
//
// Drives the pure half: the key's every value, rubbish and empty (both hide); the atlas by its 1.161 proportions; the centre in pixels
// (2076x1787 -> x 1032..1044, y 456..467 with .42's dot x 1035..1041, y 459..464 inside; 2768x2383 -> x 1377..1391, y 608..623),
// rounded outward, inside a tiny atlas and never empty; the weapon cue (vector 336 or 642 yes, each alone, the pistol's 168 and 546
// too; .42's other vector counts and text or icons of 336 no); the plain dot by its shape, any colour (.57), on real centres (.42's
// plain dots and .56's red plain dot yes; the weapon's dot and the tool's own red dot in the hand no) and made ones (blue, 40 dimmer
// yes; a rim 60 brighter, an empty or dim centre, a dot at the window's edge and another atlas size no); and the text of every line,
// the signature and the pixels line; .58's signature compare (a line on any change of it) and the centre now line; .60's search along
// the centre's rows (the dot at .59's x 1036, 1267, 1497 and at the strip's edges found; the weapon's and the tool's dots, an empty
// strip, a marker's bright bar elsewhere not plain; the nearer of two plain dots; the read's at in the signature and its line); .62's
// pattern for any atlas size (2076x1787 .42's own, the area scaling at 1:1 to the byte; 1868x1608 -> 10x11, sum ~3075, spot 7;
// 1245x1072 -> 7x8, ~1366, 5; 2768x2383 -> 14x15, ~6753, 10; none past 32 a side), .42's and .56's plain dots scaled onto each plain at
// the centre and moved along the row, the weapon's and the tool's dots scaled other by shape, and the lines: pattern for, trial, the
// 7x8 pixels line whole and the 14x15 one cut.
#include "../../src/d3d11/aim_dot_core.h"
#include "aim_dot_samples.h"
#include <cstdio>
#include <cstring>
#include <vector>

using namespace edvr;
using namespace edvr::adh;

namespace {
const Pattern& P2076() {
    static const Pattern p = patternOf(2076, 1787);
    return p;
}
int g_checks = 0, g_failures = 0;
void check(bool ok, const char* what) {
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::printf("FAIL: %s\n", what);
    }
}
bool has(const char* line, const char* part) { return std::strstr(line, part) != nullptr; }

void partKeys() {
    bool valid = false;
    check(parseMode("hide", &valid) == Mode::Hide && valid, "hide");
    check(parseMode("show", &valid) == Mode::Show && valid, "show");
    check(parseMode(" Show \r\n", &valid) == Mode::Show && valid, "case and spaces");
    check(parseMode("", &valid) == Mode::Hide && valid, "empty is hide");
    check(parseMode(nullptr, &valid) == Mode::Hide && valid, "missing is hide");
    check(parseMode("off", &valid) == Mode::Hide && !valid, "off is rubbish, hide, said");
    check(parseMode("shown", &valid) == Mode::Hide && !valid, "a longer word is rubbish");
    check(std::strcmp(modeName(Mode::Hide), "hide") == 0 && std::strcmp(modeName(Mode::Show), "show") == 0, "names");
}

void partAtlas() {
    check(isAtlasShape(2768, 2383), "2768x2383");
    check(isAtlasShape(2076, 1787), "2076x1787 (.42)");
    check(isAtlasShape(4089, 3521), "the cinema's 4089x3521");
    check(!isAtlasShape(2412, 1449), "the HUD is not");
    check(!isAtlasShape(2162, 1922), "the ship name's atlas is not");
    check(!isAtlasShape(1521, 1402), "the lift menu is not");
    check(!isAtlasShape(0, 0) && !isAtlasShape(100, 0), "empty is not");
    check(atlasFormatOk(27) && atlasFormatOk(28) && !atlasFormatOk(29) && !atlasFormatOk(87), "formats");
}

void partCentre() {
    Rect r = centreOf(2076, 1787);
    check(r.x0 == 1032 && r.x1 == 1044 && r.y0 == 456 && r.y1 == 467, "2076x1787 -> x 1032..1044, y 456..467");
    check(r.x0 <= 1035 && r.x1 >= 1042 && r.y0 <= 459 && r.y1 >= 465, ".42's dot inside");
    check(r.w() == 12 && r.h() == 11, "12x11");
    r = centreOf(2768, 2383);
    check(r.x0 == 1377 && r.x1 == 1391 && r.y0 == 608 && r.y1 == 623, "2768x2383 -> x 1377..1391, y 608..623");
    r = centreOf(3, 2);
    check(r.w() >= 1 && r.h() >= 1 && r.x1 <= 3 && r.y1 <= 2, "a tiny atlas: inside, not empty");
    r = centreOf(0, 0);
    check(r.w() == 0 && r.h() == 0, "no atlas, no centre");
}

void partCue() {
    check(weaponCue(true, 336) == 0, "vector 336");
    check(weaponCue(true, 642) == 1, "vector 642");
    check(weaponCue(true, 168) == 2 && weaponCue(true, 546) == 3, "the pistol's vector 168 and 546 (.44)");
    const uint32_t others[] = {3, 21, 66, 120, 138, 141, 186, 231, 0, 6};
    bool none = true;
    for (uint32_t n : others) none = none && weaponCue(true, n) < 0;
    check(none, ".42's other vector draws are not the cue");
    check(weaponCue(false, 336) < 0 && weaponCue(false, 642) < 0, "text or icons of 336 or 642 are not");
}

// A centre as the glue reads it: RGB samples widened to the atlas's 4 bytes a pixel, padded rows (pitch 64), alpha 255.
void widen(const uint8_t* rgb, uint8_t* out, uint32_t pitch) {
    std::memset(out, 0xEE, size_t(pitch) * kPatternH);
    for (uint32_t y = 0; y < kPatternH; ++y)
        for (uint32_t x = 0; x < kPatternW; ++x) {
            for (uint32_t c = 0; c < 3; ++c) out[size_t(y) * pitch + x * 4 + c] = rgb[(y * kPatternW + x) * 3 + c];
            out[size_t(y) * pitch + x * 4 + 3] = 255;
        }
}

// A sample changed pixel by pixel: `f` gets R, G, B of one pixel and its x, y in the window.
template <class F>
void tweak(const uint8_t* rgb, uint8_t* out, F f) {
    std::memcpy(out, rgb, kPatternW * kPatternH * 3);
    for (uint32_t y = 0; y < kPatternH; ++y)
        for (uint32_t x = 0; x < kPatternW; ++x) f(out + (y * kPatternW + x) * 3, x, y);
}
int peakOf(const uint8_t* p) { return p[0] > p[1] ? (p[0] > p[2] ? p[0] : p[2]) : (p[1] > p[2] ? p[1] : p[2]); }
uint8_t clampByte(int v) { return uint8_t(v < 0 ? 0 : (v > 255 ? 255 : v)); }

void partPattern() {
    const uint32_t pitch = 64;
    uint8_t c[64 * 11];
    uint8_t t[kPatternW * kPatternH * 3];
    auto read = [&](const uint8_t* rgb) {
        widen(rgb, c, pitch);
        return readCentre(c, pitch, P2076());
    };
    check(centreOf(2076, 1787).w() == kPatternW && centreOf(2076, 1787).h() == kPatternH, "the window is the centre's size");

    // Real centres.
    CentreRead r = read(kPlainDot);
    check(r.plain && r.why == Why::None, ".42's 01: the plain dot");
    check(r.lit == 20 && r.bright == 17, ".42's 01: 20 lit, 17 bright");
    check(r.box && r.bx0 == 3 && r.bx1 == 8 && r.by0 == 3 && r.by1 == 7, ".42's 01: the box x 3..8, y 3..7");
    check(r.maxR == 173 && r.maxG == 255 && r.maxB == 250, ".42's 01: the brightest is 173,255,250");
    check(read(kPlain42b).plain, ".42's 04: the plain dot at a door");
    r = read(kRed56);
    check(r.plain && r.maxR == 255 && r.maxG == 0 && r.maxB == 0, ".56's red plain dot (a weapon put away): plain, whatever its colour");
    check(read(kShift59a).plain, ".59's 02: the dot moved by the radial menu, a fraction of a pixel off: plain (its sum)");
    check(read(kShift59b).plain, ".59's 03: moved again by the exobiologist's tool: plain");
    r = read(kWeapon42);
    check(!r.plain && r.why == Why::Shape, ".42's 02: the weapon's dot is other, by its shape");
    r = read(kTool43);
    check(!r.plain && r.why == Why::Shape, ".43's 01: the tool's own red dot in the hand is other, by its shape");

    // Made ones.
    tweak(kPlainDot, t, [](uint8_t* p, uint32_t, uint32_t) {
        const int m = peakOf(p);
        p[0] = uint8_t(m * 2 / 255);
        p[1] = uint8_t(m * 163 / 255);
        p[2] = uint8_t(m);
    });
    check(read(t).plain, "blue 2,163,255 of the plain shape (the pistol's colour at a ship): plain");
    tweak(kPlainDot, t, [](uint8_t* p, uint32_t, uint32_t) {
        if (peakOf(p))
            for (int k = 0; k < 3; ++k) p[k] = clampByte(p[k] - 15);
    });
    check(read(t).plain, "dimmer by 15 a channel: within the sum's 15%, plain");
    tweak(kPlainDot, t, [](uint8_t* p, uint32_t, uint32_t) {
        if (peakOf(p))
            for (int k = 0; k < 3; ++k) p[k] = clampByte(p[k] - 60);
    });
    r = read(t);
    check(!r.plain && r.why == Why::Shape, "dimmer by 60 a channel: past the sum's 15%, shape");
    tweak(kPlainDot, t, [](uint8_t* p, uint32_t, uint32_t) {
        if (peakOf(p) && peakOf(p) < 255)
            for (int k = 0; k < 3; ++k) p[k] = clampByte(p[k] + 60);
    });
    r = read(t);
    check(!r.plain && r.why == Why::Shape, "the rim brighter by 60: past the tolerance, shape");
    std::memset(t, 0, sizeof(t));
    r = read(t);
    check(!r.plain && r.why == Why::Empty && !r.box && r.lit == 0, "an empty centre: other, empty, no box");
    tweak(kPlainDot, t, [](uint8_t* p, uint32_t, uint32_t) {
        for (int k = 0; k < 3; ++k) p[k] = uint8_t(p[k] / 3);
    });
    check(read(t).why == Why::Empty, "a dot dimmer than 128 at its core: empty");
    std::memset(t, 0, sizeof(t));
    for (uint32_t y = 0; y < kPatternH; ++y)
        for (uint32_t x = 3; x < kPatternW; ++x)
            for (int k = 0; k < 3; ++k) t[(y * kPatternW + x - 3) * 3 + k] = kPlainDot[(y * kPatternW + x) * 3 + k];
    r = read(t);
    check(!r.plain && r.why == Why::Shape && r.bx0 == 0, "the dot moved to the window's edge: other, shape");

    // No pattern, no pixels.
    widen(kPlainDot, c, pitch);
    r = readCentre(c, pitch, patternOf(0, 0));
    check(!r.plain && r.why == Why::Atlas, "no pattern: other, atlas");
    r = readCentre(nullptr, pitch, P2076());
    check(!r.plain && r.why == Why::Atlas, "no pixels: other");
}

// .60: the centre's rows the atlas's whole width, a sample put at x (the window's left edge), 4 bytes a pixel, alpha 255.
constexpr uint32_t kStripW = 2076, kStripPitch = kStripW * 4 + 16;
uint8_t g_strip[kStripPitch * kPatternH];
void stripClear() { std::memset(g_strip, 0, sizeof(g_strip)); }
void stripPut(const uint8_t* rgb, uint32_t x0) {
    for (uint32_t y = 0; y < kPatternH; ++y)
        for (uint32_t x = 0; x < kPatternW; ++x) {
            uint8_t* p = g_strip + size_t(y) * kStripPitch + size_t(x0 + x) * 4;
            for (uint32_t c = 0; c < 3; ++c) p[c] = rgb[(y * kPatternW + x) * 3 + c];
            p[3] = 255;
        }
}
CentreRead stripRead() { return readRow(g_strip, kStripPitch, kStripW, 1032, P2076()); }

void partRow() {
    const uint32_t cx = centreOf(2076, 1787).x0;
    check(cx == 1032, "the centre window starts at x 1032");
    // .59's snapshots: the dot's bright columns at 1036, 1267, 1497 -> windows at 1032, 1263, 1493.
    const uint32_t at[] = {1032, 1263, 1493};
    for (uint32_t a : at) {
        stripClear();
        stripPut(kPlainDot, a);
        const CentreRead r = stripRead();
        char what[120];
        std::snprintf(what, sizeof(what), "the plain dot at x %u: found there, plain", a);
        check(r.plain && r.at == a && r.lit == 20 && r.bright == 17, what);
    }
    stripClear();
    stripPut(kRed56, 1493);
    CentreRead r = stripRead();
    check(r.plain && r.at == 1493 && r.maxR == 255, ".56's red plain dot moved right: found, plain");
    stripClear();
    stripPut(kPlainDot, 3);
    r = stripRead();
    check(r.plain && r.at == 3, "a plain dot near the strip's left edge: found");
    stripClear();
    stripPut(kPlainDot, kStripW - kPatternW);
    r = stripRead();
    check(r.plain && r.at == kStripW - kPatternW, "a plain dot at the strip's right edge: found");
    stripClear();
    stripPut(kTool43, 1263);
    r = stripRead();
    check(!r.plain && r.why == Why::Shape, "the tool's own red dot moved right: other, shape");
    stripClear();
    stripPut(kShift59a, 1263);
    r = stripRead();
    check(r.plain && r.at == 1263, ".59's 02 in its place: found at x 1263, plain");
    stripClear();
    stripPut(kShift59b, 1493);
    r = stripRead();
    check(r.plain && r.at == 1493, ".59's 03 in its place: found at x 1493, plain");
    stripClear();
    stripPut(kWeapon42, cx);
    r = stripRead();
    check(!r.plain && r.why == Why::Shape, "the weapon's dot in the centre: other, shape");
    stripClear();
    r = stripRead();
    check(!r.plain && r.why == Why::Empty && r.at == cx, "an empty strip: other, empty, the centre's window");
    stripClear();
    for (uint32_t y = 2; y < 9; ++y)
        for (uint32_t x = 400; x < 460; ++x) {
            uint8_t* p = g_strip + size_t(y) * kStripPitch + size_t(x) * 4;
            p[0] = 200;
            p[1] = 200;
            p[2] = 200;
            p[3] = 255;
        }
    r = stripRead();
    check(!r.plain && r.why == Why::Shape, "a marker's bright bar crossing the rows, no dot: other, shape");
    stripPut(kPlainDot, 1263);
    r = stripRead();
    check(r.plain && r.at == 1263, "the bar and the plain dot: the dot found");
    stripClear();
    stripPut(kPlainDot, 1263);
    stripPut(kPlainDot, 1493);
    r = stripRead();
    check(r.plain && r.at == 1263, "two plain dots: the nearer the centre");
    stripClear();
    stripPut(kPlainDot, 1263);
    check(!readRow(g_strip, kStripPitch, kStripW, 1032, patternOf(0, 0)).plain, "no pattern: other");
    check(!readRow(nullptr, kStripPitch, kStripW, 1032, P2076()).plain, "no pixels: other");
    check(!readRow(g_strip, kStripPitch, 8, 0, P2076()).plain, "a strip narrower than the window: other");
    const CentreRead a = stripRead();
    stripClear();
    stripPut(kPlainDot, 1493);
    const CentreRead b = stripRead();
    check(a.plain && b.plain && !sameSignature(a, b), "the same dot moved: the signature differs (at)");
    char line[400];
    formatCentre(b, line, sizeof(line));
    check(has(line, "aim dot: centre plain (") && has(line, ", at=x1493)"), "the line says where: at=x1493");
}

// .62: the pattern for any atlas size, and .42's real samples scaled onto it by the same arithmetic, channel by channel.
struct Scaled {
    uint32_t w = 0, h = 0;
    uint8_t rgb[kWindowMax * kWindowMax * 3] = {};
};
Scaled scaled(const uint8_t* rgb, uint32_t aw, uint32_t ah) {
    Scaled s;
    const Rect c = centreOf(aw, ah);
    s.w = c.w();
    s.h = c.h();
    for (int k = 0; k < 3; ++k) scaleWindow(rgb + k, 3, aw, ah, s.rgb + k, 3);
    return s;
}
// A strip of the atlas's width, `rows` high, 4 bytes a pixel; a window put at x0 with alpha 255.
struct Strip {
    uint32_t w = 0, h = 0, pitch = 0;
    std::vector<uint8_t> px;
    Strip(uint32_t aw, uint32_t rows) : w(aw), h(rows), pitch(aw * 4 + 16), px(size_t(aw * 4 + 16) * rows, 0) {}
    void put(const Scaled& s, uint32_t x0) {
        for (uint32_t y = 0; y < s.h && y < h; ++y)
            for (uint32_t x = 0; x < s.w; ++x) {
                uint8_t* p = px.data() + size_t(y) * pitch + size_t(x0 + x) * 4;
                for (uint32_t k = 0; k < 3; ++k) p[k] = s.rgb[(y * s.w + x) * 3 + k];
                p[3] = 255;
            }
    }
};
bool near(int a, int b, int by) { return (a > b ? a - b : b - a) <= by; }

void partScaled() {
    // 2076x1787: .42's own, and the scaling at 1:1 gives it back to the byte.
    const Pattern& n = P2076();
    check(n.valid && n.native && n.w == 12 && n.h == 11 && n.sum == 3798 && n.spotMax == 7 && n.scale == 1.0, "2076x1787: .42 as flown");
    bool same = true;
    uint8_t bright[kPatternW * kPatternH], back[kWindowMax * kWindowMax];
    for (uint32_t i = 0; i < kPatternW * kPatternH; ++i) bright[i] = uint8_t(brightnessOf(kPlainDot + i * 3));
    check(scaleWindow(bright, 1, 2076, 1787, back, 1), "scaling onto 2076x1787 works");
    for (uint32_t i = 0; i < kPatternW * kPatternH; ++i) same = same && back[i] == bright[i] && n.px[i] == bright[i];
    check(same, "the area scaling at 1:1 is .42's brightness to the byte");

    struct Size {
        uint32_t w, h, ww, wh, spot;
        int sum;
        const char* name;
    } sizes[] = {{1868, 1608, 10, 11, 7, 3075, "1868x1608 (Oculus, HMD quality 0.75)"},
                 {1245, 1072, 7, 8, 5, 1366, "1245x1072 (Oculus, HMD quality 0.5)"},
                 {2768, 2383, 14, 15, 10, 6753, "2768x2383"}};
    for (const Size& z : sizes) {
        char what[240];
        const Pattern p = patternOf(z.w, z.h);
        const Rect c = centreOf(z.w, z.h);
        std::snprintf(what, sizeof(what), "%s: window %ux%u, sum ~%d, spot %u, not native (got %ux%u, %d, %u)", z.name, z.ww, z.wh, z.sum,
                      z.spot, p.w, p.h, p.sum, p.spotMax);
        check(p.valid && !p.native && p.w == z.ww && p.h == z.wh && c.w() == p.w && c.h() == p.h && near(p.sum, z.sum, 5) &&
                  p.spotMax == z.spot,
              what);
        int total = 0;
        for (uint32_t i = 0; i < p.w * p.h; ++i) total += p.px[i];
        std::snprintf(what, sizeof(what), "%s: the scaled pixels add up to the sum within 5%% (%d vs %d)", z.name, total, p.sum);
        check(near(total, p.sum, p.sum / 20), what);

        // Real samples scaled: plain ones plain, the weapon's and the tool's other by shape; in the centre window and along the row.
        struct S {
            const uint8_t* rgb;
            bool plain;
            const char* name;
        } samples[] = {{kPlainDot, true, ".42's 01"}, {kPlain42b, true, ".42's 04"}, {kRed56, true, ".56's red plain dot"},
                       {kWeapon42, false, ".42's weapon dot"}, {kTool43, false, ".43's tool dot"}};
        for (const S& sm : samples) {
            const Scaled sc = scaled(sm.rgb, z.w, z.h);
            Strip st(z.w, p.h);
            st.put(sc, c.x0);
            const CentreRead one = readCentre(st.px.data() + size_t(c.x0) * 4, st.pitch, p);
            const CentreRead row = readRow(st.px.data(), st.pitch, st.w, c.x0, p);
            const bool ok = sm.plain ? (one.plain && row.plain && row.at == c.x0)
                                     : (!one.plain && one.why == Why::Shape && !row.plain && row.why == Why::Shape);
            std::snprintf(what, sizeof(what), "%s: %s scaled is %s (sum %d of %d, box x%u..%u y%u..%u, why %s, at=x%u)", z.name, sm.name,
                          sm.plain ? "plain, at the centre" : "other, shape", one.sum, p.sum, one.bx0, one.bx1, one.by0, one.by1,
                          whyName(one.why), row.at);
            check(ok, what);
        }
        // The plain dot moved right along the row (.59's ~231 px of 2076, scaled): found there.
        const Scaled sc = scaled(kPlainDot, z.w, z.h);
        Strip st(z.w, p.h);
        const uint32_t moved = c.x0 + uint32_t(231 * p.scale);
        st.put(sc, moved);
        const CentreRead row = readRow(st.px.data(), st.pitch, st.w, c.x0, p);
        std::snprintf(what, sizeof(what), "%s: the scaled plain dot moved right: found at x%u", z.name, moved);
        check(row.plain && row.at == moved, what);
    }
    const Pattern big = patternOf(8000, 6891);
    check(!big.valid, "a centre larger than 32 a side: no pattern");

    // The lines.
    char line[400];
    formatPattern(patternOf(1868, 1608), line, sizeof(line));
    check(has(line, "aim dot: pattern for 1868x1608: scale 0.900 (window 10x11, sum 30") &&
              has(line, ", spot <= 7; .42's dot scaled by area, trial"),
          "pattern for 1868x1608");
    formatPattern(P2076(), line, sizeof(line));
    check(std::strcmp(line, "aim dot: pattern for 2076x1787: scale 1.000 (window 12x11, sum 3798, spot <= 7; .42's dot as flown, erased)") ==
              0,
          "pattern for 2076x1787");
    formatPattern(big, line, sizeof(line));
    check(has(line, "aim dot: pattern for 8000x6891: none"), "pattern for a size with none");
    const Pattern p1245 = patternOf(1245, 1072);
    const Rect c1245 = centreOf(1245, 1072);
    Strip st(1245, p1245.h);
    st.put(scaled(kPlainDot, 1245, 1072), c1245.x0);
    const CentreRead rp = readRow(st.px.data(), st.pitch, st.w, c1245.x0, p1245);
    formatCentre(rp, line, sizeof(line), true);
    check(has(line, "aim dot: centre plain (trial, not erased: the plain dot without a weapon; lit="), "centre plain, trial");
    formatCentreNow(rp, line, sizeof(line), true);
    check(has(line, "aim dot: centre now plain (trial, not erased:"), "centre now plain, trial");
    formatCentre(rp, line, sizeof(line));
    check(has(line, "aim dot: centre plain (the plain dot without a weapon: erased;"), "centre plain, not trial: as in .60");
    Strip st2(1245, p1245.h);
    st2.put(scaled(kTool43, 1245, 1072), c1245.x0);
    const CentreRead rt = readRow(st2.px.data(), st2.pitch, st2.w, c1245.x0, p1245);
    formatCentre(rt, line, sizeof(line), true);
    check(has(line, "aim dot: centre other (trial; a weapon's or a tool's dot, or nothing: kept; why=shape"), "centre other, trial");
    char pixels[kPixelsLineCap];
    formatCentrePixels(st.px.data() + size_t(rp.at) * 4, st.pitch, p1245.w, p1245.h, pixels, sizeof(pixels));
    int rows = 1, cells = 0;
    for (const char* q = pixels; *q; ++q) rows += (*q == '/');
    for (const char* q = pixels + std::strlen("aim dot: centre pixels"); *q;) {
        while (*q == ' ' || *q == '/') ++q;
        int k = 0;
        while (q[k] && q[k] != ' ') ++k;
        cells += k == 6;
        q += k;
    }
    check(rows == 8 && cells == 56 && !has(pixels, "..."), "pixels of the 7x8 window: 8 rows, 56 cells");
    const Pattern p2768 = patternOf(2768, 2383);
    const uint32_t x2768 = centreOf(2768, 2383).x0;
    Strip st3(2768, p2768.h);
    st3.put(scaled(kPlainDot, 2768, 2383), x2768);
    formatCentrePixels(st3.px.data() + size_t(x2768) * 4, st3.pitch, p2768.w, p2768.h, pixels, sizeof(pixels));
    const size_t len = std::strlen(pixels);
    check(len < sizeof(pixels) - 1 && len > 4 && std::strcmp(pixels + len - 4, " ...") == 0, "pixels of the 14x15 window: cut, ends in ...");
}

void partLines() {
    char line[600];
    formatKey(Mode::Hide, true, "hide", line, sizeof(line));
    check(has(line, "aim dot: key hide (experimental.native_vr_on_foot_aim_dot"), "key hide");
    formatKey(Mode::Show, true, "show", line, sizeof(line));
    check(has(line, "aim dot: key show"), "key show");
    formatKey(Mode::Hide, false, "off", line, sizeof(line));
    check(has(line, "aim dot: key hide") && has(line, "'off' is not understood, reads hide"), "key rubbish");
    Rect r = centreOf(2076, 1787);
    formatAtlasFound(2076, 1787, 27, r, line, sizeof(line));
    check(has(line, "aim dot: atlas found 2076x1787 (format 27), the centre x 1032..1044 y 456..467 (12x11)"), "atlas found");
    formatAtlasSize(2076, 1787, 2768, 2383, centreOf(2768, 2383), line, sizeof(line));
    check(has(line, "aim dot: atlas size 2076x1787 -> 2768x2383, the centre x 1377..1391 y 608..623"), "atlas size");
    formatWeaponOn(3u, line, sizeof(line));
    check(std::strcmp(line, "aim dot: weapon on (vector n=336,642)") == 0, "weapon on, both");
    formatWeaponOn(2u, line, sizeof(line));
    check(std::strcmp(line, "aim dot: weapon on (vector n=642)") == 0, "weapon on, one");
    formatWeaponOn(12u, line, sizeof(line));
    check(std::strcmp(line, "aim dot: weapon on (vector n=168,546)") == 0, "weapon on, the pistol");
    formatWeaponOff(line, sizeof(line));
    check(std::strcmp(line, "aim dot: weapon off") == 0, "weapon off");
    {
        const uint32_t pitch = 64;
        uint8_t c[64 * 11];
        widen(kPlainDot, c, pitch);
        formatCentre(readCentre(c, pitch, P2076()), line, sizeof(line));
        check(std::strcmp(line, "aim dot: centre plain (the plain dot without a weapon: erased; lit=20, bright=17, box=x3..8,y3..7, "
                                "max=173,255,250, at=x0)") == 0,
              "centre plain, with the signature");
        widen(kTool43, c, pitch);
        formatCentre(readCentre(c, pitch, P2076()), line, sizeof(line));
        check(has(line, "aim dot: centre other (a weapon's or a tool's dot, or nothing: kept; why=shape, lit=24, bright=22, box=x3..8,y2..7, "
                        "max=255,0,0, at=x0)"),
              "centre other, the tool in the hand: why=shape");
        widen(kRed56, c, pitch);
        formatCentre(readCentre(c, pitch, P2076()), line, sizeof(line));
        check(std::strcmp(line, "aim dot: centre plain (the plain dot without a weapon: erased; lit=20, bright=17, box=x3..8,y3..7, "
                                "max=255,0,0, at=x0)") == 0,
              "centre plain, the red plain dot: max=255,0,0");
        std::memset(c, 0, sizeof(c));
        formatCentre(readCentre(c, pitch, P2076()), line, sizeof(line));
        check(has(line, "why=empty, lit=0, bright=0, box=none, max=0,0,0, at=x0)"), "centre other, empty: box none");
        widen(kPlainDot, c, pitch);
        formatCentre(readCentre(c, pitch, patternOf(0, 0)), line, sizeof(line));
        check(has(line, "why=atlas"), "centre other, no pattern");
        // .58: a line on any change of the signature, and the centre now.
        widen(kRed56, c, pitch);
        const CentreRead red = readCentre(c, pitch, P2076()), red2 = readCentre(c, pitch, P2076());
        check(sameSignature(red, red2), "the same read twice: the same signature");
        widen(kPlainDot, c, pitch);
        const CentreRead teal = readCentre(c, pitch, P2076());
        check(teal.plain == red.plain && !sameSignature(teal, red), "teal and red, both plain: the colour differs, so the signature");
        widen(kTool43, c, pitch);
        const CentreRead tool = readCentre(c, pitch, P2076());
        std::memset(c, 0, sizeof(c));
        const CentreRead empty = readCentre(c, pitch, P2076());
        check(!tool.plain && !empty.plain && !sameSignature(tool, empty), "empty, then the tool's dot: both other, the signature differs");
        CentreRead moved = teal;
        moved.bx0 += 1;
        check(!sameSignature(teal, moved), "the box moved: differs");
        CentreRead more = tool;
        more.lit += 1;
        check(!sameSignature(tool, more), "one more lit pixel: differs");
        formatCentreNow(teal, line, sizeof(line));
        check(std::strcmp(line, "aim dot: centre now plain (the plain dot without a weapon: erased; lit=20, bright=17, box=x3..8,y3..7, "
                                "max=173,255,250, at=x0)") == 0,
              "centre now, plain");
        formatCentreNow(empty, line, sizeof(line));
        check(has(line, "aim dot: centre now other (a weapon's or a tool's dot, or nothing: kept; why=empty, lit=0"), "centre now, other");
        check(kCentreNowMs == 10000, "centre now every 10 s");
        char pixels[kPixelsLineCap];
        widen(kTool43, c, pitch);
        formatCentrePixels(c, pitch, kPatternW, kPatternH, pixels, sizeof(pixels));
        const char* head = "aim dot: centre pixels 000000 000000 000000 000000 000000 000000 000000 000000 000000 000000 000000 "
                           "000000 / 000000";
        check(std::strncmp(pixels, head, std::strlen(head)) == 0,
              "pixels: the prefix, the first row, then a row split");
        check(has(pixels, " / 000000 000000 000000 000000 FF0000 FF0000 FF0000 FF0000 000000"), "pixels: the tool's fourth row");
        int rows = 1, cells = 0;
        for (const char* q = pixels; *q; ++q) rows += (*q == '/');
        for (const char* q = pixels + std::strlen("aim dot: centre pixels"); *q;) {  // tokens of six hex digits
            while (*q == ' ' || *q == '/') ++q;
            int n = 0;
            while (q[n] && q[n] != ' ') ++n;
            bool hex = n == 6;
            for (int i = 0; hex && i < n; ++i) hex = std::strchr("0123456789ABCDEF", q[i]) != nullptr;
            cells += hex;
            q += n;
        }
        check(rows == int(kPatternH) && cells == int(kPatternW * kPatternH), "pixels: 11 rows, 132 cells");
        check(std::strlen(pixels) < sizeof(pixels) - 1, "pixels: the line fits, not cut");
    }
    formatInert("why", line, sizeof(line));
    check(has(line, "aim dot: inert: why; the picture is as with show"), "inert");
    Summary s;
    s.atlasFrames = 2700;
    s.weaponFrames = 10;
    s.hidden = 2690;
    formatSummary(Mode::Hide, 2076, 1787, s, line, sizeof(line));
    check(has(line, "aim dot: summary (hide, 30 s on foot): atlas 2076x1787, atlas_frames=2700, weapon_frames=10, hidden=2690") &&
              has(line, "rewrites=0, plain=0, other=0, unread=0, quiet_lines=0, failures=0"),
          "summary");
    s.clear();
    formatSummary(Mode::Show, 0, 0, s, line, sizeof(line));
    check(has(line, "(show, 30 s on foot): atlas 0x0, atlas_frames=0"), "a summary of zeros");
}
}  // namespace

int main(int argc, char** argv) {
    if (argc < 2 || std::strcmp(argv[1], "--self-test") != 0) {
        std::printf("usage: aim_dot_test --self-test\n");
        return 2;
    }
    partKeys();
    partAtlas();
    partCentre();
    partCue();
    partPattern();
    partRow();
    partScaled();
    partLines();
    std::printf("%s: aim_dot_test, %d checks, %d failed\n", g_failures ? "FAIL" : "PASS", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
