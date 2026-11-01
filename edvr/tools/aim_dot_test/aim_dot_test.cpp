// Build gate for the EDVRonFOOT on-foot aim dot (src\d3d11\aim_dot_core.h, experimental.native_vr_on_foot_aim_dot, .43).
//
// Drives the pure half: the key's every value, rubbish and empty (both hide); the atlas by its 1.161 proportions; the centre in pixels
// (2076x1787 -> x 1032..1044, y 456..467 with .42's dot x 1035..1041, y 459..464 inside; 2768x2383 -> x 1377..1391, y 608..623),
// rounded outward, inside a tiny atlas and never empty; the weapon cue (vector 336 or 642 yes, each alone; .42's other vector counts
// and text or icons of 336 no); and the text of every line.
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

void partPattern() {
    const uint32_t pitch = 64;
    uint8_t c[64 * 11];
    check(centreOf(2076, 1787).w() == kPatternW && centreOf(2076, 1787).h() == kPatternH, "the pattern is the centre's size");
    widen(kPlainDot, c, pitch);
    check(matchesPlainDot(c, pitch, 2076, 1787), "the pattern itself");
    widen(kPlain42b, c, pitch);
    check(matchesPlainDot(c, pitch, 2076, 1787), ".42's 04: the plain dot at a door");
    widen(kWeapon42, c, pitch);
    check(!matchesPlainDot(c, pitch, 2076, 1787), ".42's 02: the weapon's dot is not");
    widen(kTool43, c, pitch);
    check(!matchesPlainDot(c, pitch, 2076, 1787), ".43's 01: the tool's red dot is not");
    std::memset(c, 0, sizeof(c));
    check(!matchesPlainDot(c, pitch, 2076, 1787), "an empty centre is not");
    widen(kPlainDot, c, pitch);
    check(!matchesPlainDot(c, pitch, 2768, 2383) && !patternFor(2768, 2383), "another atlas size has no pattern");
    check(!matchesPlainDot(nullptr, pitch, 2076, 1787), "no pixels");
    c[5 * pitch + 5 * 4 + 1] = uint8_t(c[5 * pitch + 5 * 4 + 1] - 40);
    check(matchesPlainDot(c, pitch, 2076, 1787), "within the tolerance");
    c[5 * pitch + 5 * 4 + 1] = uint8_t(c[5 * pitch + 5 * 4 + 1] - 20);
    check(!matchesPlainDot(c, pitch, 2076, 1787), "past the tolerance");
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
    formatWeaponOff(line, sizeof(line));
    check(std::strcmp(line, "aim dot: weapon off") == 0, "weapon off");
    formatCentre(true, line, sizeof(line));
    check(has(line, "aim dot: centre plain"), "centre plain");
    formatCentre(false, line, sizeof(line));
    check(has(line, "aim dot: centre other"), "centre other");
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
