// Build gate for the EDVRonFOOT on-foot aim dot (src\d3d11\aim_dot_core.h, experimental.native_vr_on_foot_aim_dot, .43).
//
// Drives the pure half: the key's every value, rubbish and empty (both hide); the atlas by its 1.161 proportions; the centre in pixels
// (2076x1787 -> x 1032..1044, y 456..467 with .42's dot x 1035..1041, y 459..464 inside; 2768x2383 -> x 1377..1391, y 608..623),
// rounded outward, inside a tiny atlas and never empty; the weapon cue (vector 336 or 642 yes, each alone, the pistol's 168 and 546
// too; .42's other vector counts and text or icons of 336 no); the plain dot by tint and size (.56) on real centres (.42's plain dots
// yes, the weapon's dot by its size and a tool's red dot by its hue no) and made ones (brighter, near white, blue, haloed yes; a paler
// red, an empty or dim centre, a dot at the window's edge, a block, one pixel and another atlas size no); and the text of every line,
// the signature and the pixels line.
#include "../../src/d3d11/aim_dot_core.h"
#include "aim_dot_samples.h"
#include <cstdio>
#include <cstring>

using namespace edvr;
using namespace edvr::adh;

namespace {
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
        return readCentre(c, pitch, 2076, 1787);
    };
    check(centreOf(2076, 1787).w() == kPatternW && centreOf(2076, 1787).h() == kPatternH, "the window is the centre's size");

    // Real centres.
    CentreRead r = read(kPlainDot);
    check(r.plain && r.why == Why::None, ".42's 01: the plain dot");
    check(r.lit == 20 && r.bright == 17, ".42's 01: 20 lit, 17 bright");
    check(r.box && r.bx0 == 3 && r.bx1 == 8 && r.by0 == 3 && r.by1 == 7, ".42's 01: the box x 3..8, y 3..7");
    check(r.maxR == 173 && r.maxG == 255 && r.maxB == 250, ".42's 01: the brightest is 173,255,250");
    check(read(kPlain42b).plain, ".42's 04: the plain dot at a door");
    r = read(kWeapon42);
    check(!r.plain && r.why == Why::Size && r.bright == 27, ".42's 02: the weapon's dot is other, by its size (27 bright)");
    r = read(kTool43);
    check(!r.plain && r.why == Why::Tint, ".43's 01: the tool's red dot is other, by its hue");

    // The day side: the same dot brighter (Dima, 05.11: the same colour, brighter).
    tweak(kPlainDot, t, [](uint8_t* p, uint32_t, uint32_t) {
        if (peakOf(p))
            for (int k = 0; k < 3; ++k) p[k] = clampByte(p[k] + 60);
    });
    r = read(t);
    check(r.plain, "brighter by 60 a channel: plain");
    check(r.bright == 22, "brighter: the faint rim turns bright, 22 (inside 24)");
    tweak(kPlainDot, t, [](uint8_t* p, uint32_t, uint32_t) {
        if (p[0] == 173 && p[1] == 255 && p[2] == 250) {
            p[0] = 240;
            p[2] = 255;
        }
    });
    check(read(t).plain, "near white 240,255,255 in the core: plain");
    tweak(kPlainDot, t, [](uint8_t* p, uint32_t, uint32_t) {
        const int m = peakOf(p);
        p[0] = uint8_t(m * 2 / 255);
        p[1] = uint8_t(m * 163 / 255);
        p[2] = uint8_t(m);
    });
    check(read(t).plain, "blue 2,163,255 (the pistol's colour at a ship): plain");
    tweak(kPlainDot, t, [](uint8_t* p, uint32_t x, uint32_t y) {
        if (!peakOf(p) && x >= 2 && x <= 9 && y >= 2 && y <= 8) p[0] = 0, p[1] = 40, p[2] = 40;
    });
    r = read(t);
    check(r.plain && r.lit > 20 && r.bright == 17, "a halo of 40 around it: plain, more lit, the same bright");

    // Others.
    tweak(kTool43, t, [](uint8_t* p, uint32_t, uint32_t) {
        if (peakOf(p))
            for (int k = 1; k < 3; ++k) p[k] = clampByte(p[k] + 30);
    });
    r = read(t);
    check(!r.plain && r.why == Why::Tint, "the tool's red dot, brighter and paler: other, hue");
    tweak(kPlainDot, t, [](uint8_t* p, uint32_t, uint32_t) {
        if (peakOf(p)) p[0] = clampByte(p[1] + 20);
    });
    check(read(t).why == Why::Tint, "red past G by 20: hue");
    tweak(kPlainDot, t, [](uint8_t* p, uint32_t, uint32_t) {
        if (peakOf(p)) p[0] = clampByte(p[1] + 8 < p[2] + 8 ? p[1] + 8 : p[2] + 8);
    });
    check(read(t).plain, "red equal to G and B plus the slack 8: still plain");
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
    check(!r.plain && r.why == Why::Edge && r.bx0 == 0, "the dot moved to the window's edge: other, edge");
    tweak(kPlainDot, t, [](uint8_t* p, uint32_t x, uint32_t y) {
        if (x >= 1 && x <= 10 && y >= 1 && y <= 9) p[0] = 100, p[1] = 200, p[2] = 200;
    });
    check(read(t).why == Why::Size, "a teal block of 90 bright pixels: size");
    tweak(kPlainDot, t, [](uint8_t* p, uint32_t x, uint32_t y) {
        if (!(x == 5 && y == 5)) p[0] = p[1] = p[2] = 0;
    });
    check(read(t).why == Why::Size, "one pixel: size");

    // The atlas.
    widen(kPlainDot, c, pitch);
    r = readCentre(c, pitch, 2768, 2383);
    check(!r.plain && r.why == Why::Atlas && !patternFor(2768, 2383), "another atlas size: other, atlas");
    r = readCentre(nullptr, pitch, 2076, 1787);
    check(!r.plain && r.why == Why::Atlas, "no pixels: other");
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
        formatCentre(readCentre(c, pitch, 2076, 1787), line, sizeof(line));
        check(std::strcmp(line, "aim dot: centre plain (the plain dot without a weapon: erased; lit=20, bright=17, box=x3..8,y3..7, "
                                "max=173,255,250)") == 0,
              "centre plain, with the signature");
        widen(kTool43, c, pitch);
        formatCentre(readCentre(c, pitch, 2076, 1787), line, sizeof(line));
        check(has(line, "aim dot: centre other (a weapon's or a tool's dot, or nothing: kept; why=hue, lit=24, bright=22, box=x3..8,y2..7, "
                        "max=255,0,0)"),
              "centre other, the tool: why=hue");
        std::memset(c, 0, sizeof(c));
        formatCentre(readCentre(c, pitch, 2076, 1787), line, sizeof(line));
        check(has(line, "why=empty, lit=0, bright=0, box=none, max=0,0,0)"), "centre other, empty: box none");
        widen(kPlainDot, c, pitch);
        formatCentre(readCentre(c, pitch, 2768, 2383), line, sizeof(line));
        check(has(line, "why=atlas"), "centre other, another atlas");
        char pixels[kPixelsLineCap];
        widen(kTool43, c, pitch);
        formatCentrePixels(c, pitch, pixels, sizeof(pixels));
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
    formatNoPattern(2768, 2383, line, sizeof(line));
    check(has(line, "pattern is for a 2076x1787 atlas, this one is 2768x2383"), "no pattern");
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
    partLines();
    std::printf("%s: aim_dot_test, %d checks, %d failed\n", g_failures ? "FAIL" : "PASS", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
