// Build gate for the EDVRonFOOT desktop eye view (src\d3d11\desktop_view_core.h, experimental.native_vr_desktop_view and
// experimental.native_vr_desktop_view_fit; openspec change desktop-eye-mirror).
//
// Drives the pure half: the keys' reading (right, left, off, crop, whole, empty, junk, case and padding), the picture's place in the
// window (the Quest 3's 2688x2944 eye in 1920x1080 cropped and whole, a wide eye, odd sizes, the window resized, zero sizes), the texture
// coordinates through flipped and double-wide bounds, the eye's size, the colour path for each pair of formats, the Present's verdict
// (drawn, no eye, stale, the frame number out of step) and the log line's text. Nothing here touches a device, a clock or a file, so
// --dry-run is the same run as --self-test.
#include "../../src/d3d11/desktop_view_core.h"
#include <cmath>
#include <cstdio>
#include <cstring>

using namespace edvr::dv;

namespace {
int g_checks = 0, g_failures = 0;
void check(bool ok, const char* what) {
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::printf("FAIL: %s\n", what);
    }
}
bool near(double a, double b, double eps = 1e-4) { return std::fabs(a - b) < eps; }

void partKeys() {
    bool valid = false;
    check(parseView("right", &valid) == View::Right && valid, "right");
    check(parseView("left", &valid) == View::Left && valid, "left");
    check(parseView("off", &valid) == View::Off && valid, "off");
    check(parseView("", &valid) == View::Off && valid, "empty view is off, and valid");
    check(parseView(nullptr, &valid) == View::Off && valid, "missing view is off");
    check(parseView("  RIGHT \t", &valid) == View::Right && valid, "case and padding");
    check(parseView("2", &valid) == View::Off && !valid, "junk view is off and named invalid");
    check(parseView("rightt", &valid) == View::Off && !valid, "a longer word is junk");
    check(parseFit("whole", &valid) == Fit::Whole && valid, "whole");
    check(parseFit("crop", &valid) == Fit::Crop && valid, "crop");
    check(parseFit("", &valid) == Fit::Crop && valid, "empty fit is crop");
    check(parseFit(" Whole ", &valid) == Fit::Whole && valid, "fit case and padding");
    check(parseFit("stretch", &valid) == Fit::Crop && !valid, "junk fit is crop and named invalid");
    check(viewEye(View::Right) == 1 && viewEye(View::Left) == 0 && viewEye(View::Off) == -1, "the OpenVR eye index");
    check(!std::strcmp(viewName(View::Right), "right") && !std::strcmp(fitName(Fit::Whole), "whole"), "names");
}

void partPlace() {
    // The Quest 3's eye in a 1080p window, cropped: the window full, the eye's full width, its height cut around the centre.
    Placement p = place(Fit::Crop, 1920, 1080, 2688, 2944);
    check(p.ok && !p.clear, "crop: drawn, no clear");
    check(p.dst[0] == 0 && p.dst[1] == 0 && p.dst[2] == 1920 && p.dst[3] == 1080, "crop fills the window");
    check(near(p.src[0], 0) && near(p.src[2], 1), "crop keeps the eye's whole width");
    const double keep = (2688.0 / 2944.0) / (1920.0 / 1080.0);
    check(near(p.src[3] - p.src[1], keep) && near((p.src[1] + p.src[3]) / 2, 0.5), "crop: the kept height, centred");
    // Shown size in eye pixels: 2688 x (2944 * keep) must have the window's shape (no stretch).
    check(near(2688.0 / (2944.0 * (p.src[3] - p.src[1])), 1920.0 / 1080.0, 1e-3), "crop: no stretch");
    // Whole: all the eye, bars at the sides, centred, whole pixels.
    p = place(Fit::Whole, 1920, 1080, 2688, 2944);
    check(p.ok && p.clear, "whole: drawn over a cleared window");
    check(p.src[0] == 0 && p.src[1] == 0 && p.src[2] == 1 && p.src[3] == 1, "whole shows all the eye");
    check(p.dst[3] == 1080 && p.dst[1] == 0, "whole: the full height");
    check(p.dst[2] == std::floor(1080.0 * 2688.0 / 2944.0 + 0.5), "whole: the width keeps the eye's shape");
    check(p.dst[0] == std::floor((1920 - p.dst[2]) / 2), "whole: centred");
    check(near(p.dst[2] / p.dst[3], 2688.0 / 2944.0, 2e-3), "whole: no stretch");
    // A wide eye in a tall window: crop cuts the sides, whole puts bars above and below.
    p = place(Fit::Crop, 1000, 1000, 2000, 1000);
    check(p.ok && near(p.src[0], 0.25) && near(p.src[2], 0.75) && p.src[1] == 0 && p.src[3] == 1, "crop: a wide eye loses its sides");
    p = place(Fit::Whole, 1000, 1000, 2000, 1000);
    check(p.ok && p.clear && p.dst[2] == 1000 && p.dst[3] == 500 && p.dst[1] == 250, "whole: a wide eye has bars above and below");
    // Odd sizes: whole pixels, inside the window.
    p = place(Fit::Whole, 1921, 1081, 2687, 2945);
    check(p.ok && p.dst[0] >= 0 && p.dst[1] >= 0 && p.dst[0] + p.dst[2] <= 1921 && p.dst[1] + p.dst[3] <= 1081, "odd: inside");
    check(p.dst[0] == std::floor(p.dst[0]) && p.dst[2] == std::floor(p.dst[2]), "odd: whole pixels");
    p = place(Fit::Crop, 1921, 1081, 2687, 2945);
    check(p.ok && p.src[1] > 0 && p.src[3] < 1 && near((p.src[1] + p.src[3]) / 2, 0.5), "odd crop: centred");
    // The same shape: nothing cut, no bars.
    p = place(Fit::Whole, 1600, 900, 3200, 1800);
    check(p.ok && !p.clear && p.dst[2] == 1600 && p.dst[3] == 900, "same shape: whole has no bars");
    // The window resized: the placement follows the new size.
    p = place(Fit::Crop, 2560, 1440, 2688, 2944);
    check(p.ok && p.dst[2] == 2560 && p.dst[3] == 1440, "resized window: crop fills the new size");
    p = place(Fit::Whole, 1280, 720, 2688, 2944);
    check(p.ok && p.dst[3] == 720 && p.dst[2] == std::floor(720.0 * 2688.0 / 2944.0 + 0.5), "resized window: whole follows");
    // Nothing to draw.
    check(!place(Fit::Crop, 0, 1080, 2688, 2944).ok, "zero window");
    check(!place(Fit::Whole, 1920, 1080, 0, 2944).ok, "zero eye");
    check(!place(Fit::Crop, 1920, 1080, NAN, 2944).ok, "not finite");
}

void partUv() {
    const float full[4] = {0, 0, 1, 1};
    const float src[4] = {0, 0.25f, 1, 0.75f};
    float uv[4];
    sourceUv(src, full, uv);
    check(near(uv[0], 0) && near(uv[1], 0.25) && near(uv[2], 1) && near(uv[3], 0.75), "full bounds: uv is src");
    const float flipped[4] = {0, 1, 1, 0};   // the menu's output retains a submitted flip
    sourceUv(src, flipped, uv);
    check(near(uv[1], 0.75) && near(uv[3], 0.25), "a flipped v stays flipped");
    const float rightHalf[4] = {0.5f, 0, 1, 1};   // a double-wide texture's right eye
    const float all[4] = {0, 0, 1, 1};
    sourceUv(all, rightHalf, uv);
    check(near(uv[0], 0.5) && near(uv[2], 1), "double-wide: only the eye's half is read");
    uint32_t w = 0, h = 0;
    eyeSize(5376, 2944, rightHalf, w, h);
    check(w == 2688 && h == 2944, "double-wide: the eye's own size");
    eyeSize(2688, 2944, flipped, w, h);
    check(w == 2688 && h == 2944, "flipped: the size is positive");
    check(validBounds(full) && validBounds(flipped), "valid bounds");
    const float empty[4] = {0.5f, 0, 0.5f, 1}, outside[4] = {0, 0, 1.5f, 1}, nanb[4] = {0, NAN, 1, 1};
    check(!validBounds(empty) && !validBounds(outside) && !validBounds(nanb), "invalid bounds");
}

void partColor() {
    ColorPlan c = colorPlan(DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, DXGI_FORMAT_R8G8B8A8_UNORM);
    check(c.path == ColorPath::Copy && c.srv == DXGI_FORMAT_R8G8B8A8_UNORM && c.rtv == DXGI_FORMAT_R8G8B8A8_UNORM,
          "sRGB eye into an 8-bit window: the bytes as they are");
    c = colorPlan(DXGI_FORMAT_R8G8B8A8_TYPELESS, DXGI_FORMAT_B8G8R8A8_UNORM);
    check(c.path == ColorPath::Copy && c.srv == DXGI_FORMAT_R8G8B8A8_UNORM && c.rtv == DXGI_FORMAT_B8G8R8A8_UNORM, "typeless eye, BGRA window");
    c = colorPlan(DXGI_FORMAT_R16G16B16A16_FLOAT, DXGI_FORMAT_R8G8B8A8_UNORM);
    check(c.path == ColorPath::Encode && c.srv == DXGI_FORMAT_R16G16B16A16_FLOAT, "a float eye into an 8-bit window is encoded");
    c = colorPlan(DXGI_FORMAT_R11G11B10_FLOAT, DXGI_FORMAT_R10G10B10A2_UNORM);
    check(c.path == ColorPath::Encode, "R11G11B10 into a 10-bit window is encoded");
    c = colorPlan(DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_R16G16B16A16_FLOAT);
    check(c.path == ColorPath::Decode && c.rtv == DXGI_FORMAT_R16G16B16A16_FLOAT, "an 8-bit eye into a float window is decoded");
    c = colorPlan(DXGI_FORMAT_R16G16B16A16_FLOAT, DXGI_FORMAT_R16G16B16A16_FLOAT);
    check(c.path == ColorPath::Copy, "float into float");
    c = colorPlan(DXGI_FORMAT_R10G10B10A2_TYPELESS, DXGI_FORMAT_R8G8B8A8_UNORM_SRGB);
    check(c.path == ColorPath::Copy && c.rtv == DXGI_FORMAT_R8G8B8A8_UNORM, "an sRGB window is written without conversion");
    c = colorPlan(DXGI_FORMAT_BC1_UNORM, DXGI_FORMAT_R8G8B8A8_UNORM);
    check(c.path == ColorPath::Unsupported, "an unknown eye format is skipped");
    c = colorPlan(DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_UNKNOWN);
    check(c.path == ColorPath::Unsupported, "an unknown window format is skipped");
}

void partVerdict() {
    check(decide(true, 7, 7, 100, 99) == Verdict::Draw, "this frame's eye is drawn");
    check(decide(false, 0, 7, 0, 99) == Verdict::NoEye, "nothing held: no eye (loading)");
    check(decide(true, 6, 7, 100, 99) == Verdict::NoEye, "noted before the last Present: no eye, never the old picture");
    check(decide(true, 7, 7, 99, 99) == Verdict::Stale, "the frame already drawn: stale");
    check(decide(true, 7, 7, 98, 99) == Verdict::Stale, "an older frame: stale");
    check(decide(true, 7, 7, 0, 0) == Verdict::Stale, "sequence zero: stale");
    check(decide(true, 1, 1, 1, 0) == Verdict::Draw, "the first frame");
    // A run: frames 10..14, Present after each, frame 12's eye never noted (an FSS-held eye), frame 13 noted twice (a replay).
    uint64_t last = 9, drawn = 0, noEye = 0, stale = 0;
    for (uint64_t present = 1, seq = 10; seq <= 14; ++seq, ++present) {
        const bool held = seq != 12;
        const uint64_t heldSeq = seq == 14 ? 13 : seq;
        const Verdict v = decide(held, present, present, heldSeq, last);
        if (v == Verdict::Draw) { ++drawn; last = heldSeq; }
        else if (v == Verdict::NoEye) ++noEye;
        else ++stale;
    }
    check(drawn == 3 && noEye == 1 && stale == 1, "a run with a missing eye and a repeated frame");
}

// Four corner triangles x + y < c (and their mirrors): the shape of a lens mask's corners.
void cornerMask(float c, float out[24]) {
    const float tri[4][6] = {{0, 0, c, 0, 0, c}, {1, 0, 1 - c, 0, 1, c}, {0, 1, c, 1, 0, 1 - c}, {1, 1, 1 - c, 1, 1, 1 - c}};
    std::memcpy(out, tri, sizeof(tri));
}

void partZoom() {
    check(parseFit("zoom") == Fit::Zoom && !std::strcmp(fitName(Fit::Zoom), "zoom"), "zoom reads and names");
    // Dima's eye 2448x2672 in 1920x1080: crop keeps y 0.5 +- 0.2577; the corner x + y < 0.3 reaches its corners.
    float mask[24];
    cornerMask(0.3f, mask);
    double share = 0;
    const Placement crop = place(Fit::Crop, 1920, 1080, 2448, 2672);
    Placement z = zoomPlacement(1920, 1080, 2448, 2672, mask, 12, &share);
    const double hh = (crop.src[3] - crop.src[1]) / 2;
    const double exact = 0.7 / (0.5 + hh);   // the corner (0.5 - 0.5 s, 0.5 - hh s) on the line x + y = 0.3
    check(z.ok && share < 1 && share <= exact + 1e-9 && share > exact - 1.0 / 512, "zoom: just enough to clear the corners");
    check(z.dst[0] == 0 && z.dst[2] == 1920 && z.dst[3] == 1080 && !z.clear, "zoom fills the window");
    check(near((z.src[0] + z.src[2]) / 2, 0.5) && near((z.src[1] + z.src[3]) / 2, 0.5), "zoom: centred");
    check(near((z.src[2] - z.src[0]) * 2448.0 / ((z.src[3] - z.src[1]) * 2672.0), 1920.0 / 1080.0, 1e-3), "zoom: no stretch");
    const double kept[4] = {z.src[0], z.src[1], z.src[2], z.src[3]};
    check(!rectHitsMask(kept, mask, 12), "zoom: no mask triangle in the window");
    const double grown = 1.01;
    const double cx = 0.5, cy = 0.5, hw2 = (z.src[2] - z.src[0]) / 2 * grown, hh2 = (z.src[3] - z.src[1]) / 2 * grown;
    const double bigger[4] = {cx - hw2, cy - hh2, cx + hw2, cy + hh2};
    check(rectHitsMask(bigger, mask, 12), "zoom: one per cent more would show a corner");
    // No mask, or a mask that misses crop's rectangle: zoom is crop.
    z = zoomPlacement(1920, 1080, 2448, 2672, nullptr, 0, &share);
    check(share == 1 && z.src[1] == crop.src[1] && z.src[3] == crop.src[3], "no mask: zoom is crop");
    cornerMask(0.1f, mask);
    z = zoomPlacement(1920, 1080, 2448, 2672, mask, 12, &share);
    check(share == 1, "small corners outside crop: no zoom");
    // A mask over the centre: no more than twice.
    const float centre[6] = {0.4f, 0.4f, 0.6f, 0.4f, 0.5f, 0.6f};
    z = zoomPlacement(1920, 1080, 2448, 2672, centre, 3, &share);
    check(share == kZoomMinShare, "a mask over the centre: zoom stops at x2");
    // A triangle touching the rectangle at an edge only does not count.
    const double r[4] = {0.2, 0.2, 0.8, 0.8};
    const float touching[6] = {0.8f, 0.3f, 0.9f, 0.3f, 0.9f, 0.5f};
    const float inside[6] = {0.7f, 0.3f, 0.9f, 0.3f, 0.9f, 0.5f};
    const float degenerate[6] = {0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f};
    check(!rectHitsTriangle(r, touching) && rectHitsTriangle(r, inside) && !rectHitsTriangle(r, degenerate), "edge, overlap, degenerate");
    // The diagonal case the bounding boxes alone would call a hit.
    const float diagonal[6] = {0.0f, 0.0f, 0.35f, 0.0f, 0.0f, 0.35f};
    check(!rectHitsTriangle(r, diagonal), "a corner triangle beside the rectangle's corner misses it");
    char line[512];
    Window w;
    w.presents = 10; w.drawn = 10; w.windowW = 1920; w.windowH = 1080; w.eyeW = 2448; w.eyeH = 2672; w.path = ColorPath::Copy;
    w.zoomShare = 0.8; w.maskTriangles = 64;
    formatLine(line, sizeof(line), View::Right, Fit::Zoom, true, w);
    check(!std::strcmp(line, "desktop view: right zoom x1.250 (mask 64 triangles), presents 10, drawn 10, skipped no-eye 0 / stale 0 / "
                             "failed 0 (last none), window 1920x1080, eye 2448x2672, colour copy, gpu n/a."), "the zoom line");
}

void partLine() {
    char line[512];
    Window w;
    w.presents = 1350;
    formatLine(line, sizeof(line), View::Off, Fit::Crop, true, w);
    check(!std::strcmp(line, "desktop view: off (experimental.native_vr_desktop_view), presents 1350, no GPU work."), "the off line");
    w.drawn = 1340; w.noEye = 8; w.stale = 1; w.failed = 1; w.lastFailure = Failure::Format;
    w.windowW = 1920; w.windowH = 1080; w.eyeW = 2688; w.eyeH = 2944; w.path = ColorPath::Copy;
    w.noteGpu(0.1); w.noteGpu(0.3); w.noteGpu(NAN);
    formatLine(line, sizeof(line), View::Right, Fit::Crop, true, w);
    check(!std::strcmp(line, "desktop view: right crop, presents 1350, drawn 1340, skipped no-eye 8 / stale 1 / failed 1 (last format), "
                             "window 1920x1080, eye 2688x2944, colour copy, gpu mean 0.200 ms max 0.300 ms."), "the right crop line");
    Window none;
    none.presents = 900;
    formatLine(line, sizeof(line), View::Left, Fit::Whole, false, none);
    check(!std::strcmp(line, "desktop view: left whole, no runtime, presents 900, drawn 0, skipped no-eye 0 / stale 0 / failed 0 (last none), "
                             "window 0x0, eye 0x0, colour unsupported, gpu n/a."), "the no-runtime line says drawn 0");
    check(std::strstr(line, "desktop view") == line, "the grep word starts the line");
}
}  // namespace

int main(int argc, char** argv) {
    if (argc < 2 || (std::strcmp(argv[1], "--self-test") != 0 && std::strcmp(argv[1], "--dry-run") != 0)) {
        std::printf("usage: desktop_view_test --self-test | --dry-run (the pure half only; --dry-run writes nothing, as does --self-test)\n");
        return 2;
    }
    partKeys();
    partPlace();
    partUv();
    partColor();
    partVerdict();
    partZoom();
    partLine();
    std::printf("%s: desktop_view_test, %d checks, %d failed\n", g_failures ? "FAIL" : "PASS", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
