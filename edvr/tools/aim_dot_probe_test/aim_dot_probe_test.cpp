// Build gate for the EDVRonFOOT on-foot aim dot probe (src\d3d11\aim_dot_probe_core.h, experimental.native_vr_on_foot_dot; temporary).
//
// Drives the pure half: the key's every value, rubbish and empty; the atlas by its 1.161 proportions (2768x2383 and the cinema's
// 4089x3521 yes, at HMD quality 0.75 too; the HUD's 2412x1449, the ship name's 2162x1922 and the lift's 1521x1402 no); the dot's cell
// in pixels (2768x2383 -> x 1228..1540, y 462..770), rounded outward, inside a tiny atlas and never empty; the cell zoom; the
// snapshot files' names and their limit of 12; and the text of every line.
#include "../../src/d3d11/aim_dot_probe_core.h"
#include <cstdio>
#include <cstring>
#include <vector>

using namespace edvr;
using namespace edvr::adp;

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
    check(parseMode("off", &valid) == Mode::Off && valid, "off");
    check(parseMode("census", &valid) == Mode::Census && valid, "census");
    check(parseMode("erase", &valid) == Mode::Erase && valid, "erase");
    check(parseMode(" Erase \r\n", &valid) == Mode::Erase && valid, "case and spaces");
    check(parseMode("", &valid) == Mode::Off && valid, "empty is off");
    check(parseMode(nullptr, &valid) == Mode::Off && valid, "missing is off");
    check(parseMode("hide", &valid) == Mode::Off && !valid, "rubbish is off, said");
    check(parseMode("erase2", &valid) == Mode::Off && !valid, "a longer word is rubbish");
    check(std::strcmp(modeName(Mode::Census), "census") == 0 && std::strcmp(modeName(Mode::Erase), "erase") == 0 &&
              std::strcmp(modeName(Mode::Off), "off") == 0,
          "names");
}

void partAtlas() {
    check(isAtlasShape(2768, 2383), "stereo atlas");
    check(isAtlasShape(4089, 3521), "cinema atlas");
    check(isAtlasShape(2076, 1787), "HMD quality 0.75");
    check(!isAtlasShape(2412, 1449), "the HUD panel");
    check(!isAtlasShape(2162, 1922), "the ship name's 1.125");
    check(!isAtlasShape(1521, 1402), "the lift menu");
    check(!isAtlasShape(1029, 949), "the lift menu, stereo");
    check(!isAtlasShape(0, 10) && !isAtlasShape(10, 0), "empty");
    check(atlasFormatOk(27) && atlasFormatOk(28) && !atlasFormatOk(29) && !atlasFormatOk(87), "formats");
}

void partCell() {
    const Cell c = cellOf(2768, 2383);
    check(c.x0 == 1228 && c.x1 == 1540 && c.y0 == 462 && c.y1 == 770, "2768x2383 -> x 1228..1540, y 462..770");
    check(c.w() == 312 && c.h() == 308, "its size");
    const Cell q = cellOf(2076, 1787);  // HMD quality 0.75
    check(q.x0 == 921 && q.x1 == 1155 && q.y0 == 346 && q.y1 == 578, "0.75 scaled");
    check(q.x0 <= uint32_t(0.444 * 2076) && q.x1 >= uint32_t(0.556 * 2076), "0.75 outward");
    const Cell t = cellOf(1, 1);
    check(t.x0 == 0 && t.x1 == 1 && t.y0 == 0 && t.y1 == 1, "a 1x1 atlas: the whole pixel");
    const Cell s = cellOf(3, 2);
    check(s.x1 <= 3 && s.y1 <= 2 && s.w() >= 1 && s.h() >= 1, "a tiny atlas: inside, not empty");
    const Cell z = cellOf(0, 0);
    check(z.w() == 0 && z.h() == 0, "no atlas, no cell");
    for (uint32_t w = 1; w < 400; w += 7)
        for (uint32_t h = 1; h < 400; h += 11) {
            const Cell k = cellOf(w, h);
            if (k.x1 > w || k.y1 > h || !k.w() || !k.h()) {
                check(false, "every small size inside and not empty");
                return;
            }
        }
}

void partZoom() {
    // A 4x3 atlas, pixel (x, y) holds x, y, 7, 9; a cell x 1..3, y 1..2 zoomed 4x is 8x4.
    std::vector<uint8_t> a(4 * 3 * 4);
    for (uint32_t y = 0; y < 3; ++y)
        for (uint32_t x = 0; x < 4; ++x) {
            uint8_t* p = &a[(y * 4 + x) * 4];
            p[0] = uint8_t(x);
            p[1] = uint8_t(y);
            p[2] = 7;
            p[3] = 9;
        }
    Cell c;
    c.x0 = 1;
    c.x1 = 3;
    c.y0 = 1;
    c.y1 = 2;
    std::vector<uint8_t> out(c.w() * 4 * c.h() * 4 * 4);
    zoomCell(a.data(), 4, c, kCellZoom, out.data());
    check(out[0] == 1 && out[1] == 1 && out[2] == 7 && out[3] == 9, "zoom: first pixel");
    check(out[(3 * 8 + 7) * 4] == 2 && out[(3 * 8 + 7) * 4 + 1] == 1, "zoom: last pixel");
    check(out[(0 * 8 + 4) * 4] == 2, "zoom: the second source pixel at 4");
    // The BMP: header, size, bottom-up rows, B G R A.
    std::vector<uint8_t> bmp(bmpBytes(4, 3));
    makeBmp32(a.data(), 4, 3, bmp.data());
    uint32_t size = 0, w = 0, h = 0;
    std::memcpy(&size, &bmp[2], 4);
    std::memcpy(&w, &bmp[18], 4);
    std::memcpy(&h, &bmp[22], 4);
    check(bmp[0] == 'B' && bmp[1] == 'M' && size == bmp.size() && w == 4 && h == 3 && bmp[28] == 32, "bmp header");
    const uint8_t* first = &bmp[kBmpHeader];  // the bottom row's first pixel: source (0, 2)
    check(first[0] == 7 && first[1] == 2 && first[2] == 0 && first[3] == 9, "bmp bottom-up, B G R A");
}

void partFiles() {
    char name[64];
    atlasFileName(1, 2768, 2383, name, sizeof(name));
    check(std::strcmp(name, "01_atlas_2768x2383.bmp") == 0, "atlas file name");
    cellFileName(12, name, sizeof(name));
    check(std::strcmp(name, "12_cell.bmp") == 0, "cell file name");
    check(snapshotFilesAllowed(0) && snapshotFilesAllowed(11) && !snapshotFilesAllowed(12) && !snapshotFilesAllowed(13), "12 at most");
    check(kMaxSnapshots * 2 == 24, "24 files");
}

void partLines() {
    char line[1200];
    formatKey(Mode::Census, true, "census", line, sizeof(line));
    check(has(line, "aim dot: key census"), "key line");
    formatKey(Mode::Off, false, "hide", line, sizeof(line));
    check(has(line, "aim dot: key off") && has(line, "'hide' is not understood"), "rubbish key line");
    formatWaitingForHud(line, sizeof(line));
    check(has(line, "aim dot: waiting for the on-foot HUD"), "waiting line");
    const Cell c = cellOf(2768, 2383);
    formatAtlasFound(2768, 2383, 27, c, line, sizeof(line));
    check(has(line, "aim dot: atlas found 2768x2383") && has(line, "x 1228..1540 y 462..770"), "atlas found");
    formatAtlasSize(2768, 2383, 2076, 1787, cellOf(2076, 1787), line, sizeof(line));
    check(has(line, "aim dot: atlas size 2768x2383 -> 2076x1787"), "atlas size");
    formatInert("the atlas's format is 87, not R8G8B8A8", line, sizeof(line));
    check(has(line, "aim dot: inert: the atlas's format is 87"), "inert");
    formatSnapshot(3, 2768, 2383, 5, 5, 2, 40, "03_atlas_2768x2383.bmp, 03_cell.bmp", line, sizeof(line));
    check(has(line, "aim dot: snapshot 3: atlas 2768x2383, eye quads 5/5") && has(line, "gui draws 40,") && has(line, "files 03_"),
          "snapshot line");
    formatSnapshot(13, 2768, 2383, 1, 1, 2, 300, "none (the limit)", line, sizeof(line));
    check(has(line, "(the first 256 listed)") && has(line, "files none"), "snapshot over the limits");
    GuiDraw d[kDrawsPerLine];
    for (int i = 0; i < kDrawsPerLine; ++i) {
        d[i].kind = Gui::Icons;
        d[i].count = 6;
        d[i].vpX = 0;
        d[i].vpY = 0;
        d[i].vpW = 2768;
        d[i].vpH = 2383;
        d[i].hasScissor = i % 2 == 0;
        d[i].scL = 1228;
        d[i].scT = 462;
        d[i].scR = 1540;
        d[i].scB = 770;
    }
    formatDraws(3, d, 0, kDrawsPerLine, line, sizeof(line));
    check(has(line, "aim dot: snapshot 3 draws 0..19: [icons n=6 vp=0,0,2768,2383 sc=1228,462,1540,770, icons n=6"), "draws line");
    check(has(line, "sc=none") && line[std::strlen(line) - 1] == ']', "draws line closed");
    check(std::strlen(line) < 1100, "a draws line fits the log's line");
    formatSkipped(line, sizeof(line));
    check(has(line, "aim dot: snapshot skipped: no atlas within 2 s"), "skipped");
    formatLimit(line, sizeof(line));
    check(has(line, "aim dot: snapshot limit"), "limit");
    Summary s;
    s.frames = 2700;
    s.atlasFrames = 2690;
    s.erased = 2690;
    formatSummary(Mode::Erase, 2768, 2383, s, line, sizeof(line));
    check(has(line, "aim dot: summary (erase") && has(line, "frames=2700, atlas_frames=2690") && has(line, "erased=2690") &&
              has(line, "rewrites_after_erase=0") && has(line, "failures=0"),
          "summary");
    s.clear();
    formatSummary(Mode::Census, 0, 0, s, line, sizeof(line));
    check(has(line, "atlas 0x0, frames=0"), "a summary of zeros");
}
}  // namespace

int main(int argc, char** argv) {
    if (argc < 2 || std::strcmp(argv[1], "--self-test") != 0) {
        std::printf("usage: aim_dot_probe_test --self-test\n");
        return 2;
    }
    partKeys();
    partAtlas();
    partCell();
    partZoom();
    partFiles();
    partLines();
    std::printf("%s: aim_dot_probe_test, %d checks, %d failed\n", g_failures ? "FAIL" : "PASS", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
