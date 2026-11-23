// Build gate for the EDVRonFOOT on-foot camera height (src\d3d11\on_foot_height_core.h, experimental.native_vr_on_foot_height; openspec
// change on-foot-height).
//
// Drives the pure half: the key's number as whole centimetres (in range, past either end, fractions, not a number; empty, missing and
// junk text read 0 in Config::getFloat before they get here, which tools\config_test holds), the target on foot, in the ship, with the
// journal not knowing, in the main menu and with native on foot off, the ramp (0 -> +10 cm in about 0.1 s at 16 ms frames without
// overshoot, back to 0 on leaving, a pause's huge dt moving one small step), and the key line's text. Nothing here touches a device, a
// clock or a file, so --dry-run is the same run as --self-test.
#include "../../src/d3d11/on_foot_height_core.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>

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
bool near(double a, double b, double eps = 1e-5) { return std::fabs(a - b) < eps; }

void partKey() {
    check(cmFromValue(0.0f) == 0, "0 (also what getFloat hands over for empty, missing or junk such as abc) is 0");
    check(cmFromValue(5.0f) == 5 && cmFromValue(-5.0f) == -5, "+5 and -5 as they are");
    check(cmFromValue(50.0f) == 20, "50 clamps to +20");
    check(cmFromValue(-15.0f) == -10, "-15 clamps to -10");
    check(cmFromValue(4.6f) == 5 && cmFromValue(4.4f) == 4 && cmFromValue(-4.6f) == -5, "a fraction rounds to the nearest cm");
    check(cmFromValue(20.0f) == 20 && cmFromValue(-10.0f) == -10, "the ends themselves");
    check(cmFromValue(std::numeric_limits<float>::quiet_NaN()) == 0 && cmFromValue(std::numeric_limits<float>::infinity()) == 0 &&
              cmFromValue(-std::numeric_limits<float>::infinity()) == 0,
          "not a number, +inf and -inf read 0");
    check(cmFromValue(1e30f) == 20 && cmFromValue(-1e30f) == -10, "a huge number clamps, never overflows");
}

void partTarget() {
    TargetIn in;
    in.cm = 10;
    in.journalKnown = in.onFoot = true;
    check(onFootFlag(in) && near(targetM(in), 0.10), "ON FOOT, key +10: target 0.10 m");
    in.onFoot = false;
    check(!onFootFlag(in) && targetM(in) == 0.0f, "IN THE SHIP (the journal says not on foot): 0");
    in.onFoot = true;
    in.journalKnown = false;
    check(targetM(in) == 0.0f, "the journal not knowing yet: 0");
    in.journalKnown = true;
    in.mainMenu = true;
    check(targetM(in) == 0.0f, "THE MAIN MENU after gameplay (Status.json still says on foot): 0");
    in.mainMenu = false;
    in.nativeOnFoot = false;
    check(targetM(in) == 0.0f, "native_vr_on_foot off: 0");
    in.nativeOnFoot = true;
    in.cm = -7;
    check(near(targetM(in), -0.07), "a negative key lowers: -0.07 m");
}

void partRamp() {
    const float dt = 0.016f;
    float h = 0.0f, t = 0.0f, most = 0.0f;
    while (h < 0.10f && t < 1.0f) {
        h = step(h, 0.10f, dt);
        t += dt;
        if (h > most) most = h;
    }
    check(h == 0.10f && t > 0.08f && t < 0.13f, "0 -> +10 CM at 16 ms frames: arrives in about 0.1 s");
    for (int i = 0; i < 50; ++i) {
        h = step(h, 0.10f, dt);
        if (h > most) most = h;
    }
    check(h == 0.10f && most <= 0.10f, "...and never overshoots, and stays");
    float down = h;
    int frames = 0;
    while (down > 0.0f && frames < 100) {
        down = step(down, 0.0f, dt);
        ++frames;
    }
    check(down == 0.0f && frames >= 6 && frames <= 8, "LEAVING FOR THE SHIP OR THE MENU (target 0): back to exactly 0 in about 0.1 s");
    check(near(step(0.0f, 0.20f, 5.0f), double(kSpeedMps * kMaxDtS)), "A PAUSE'S HUGE DT moves one small step (5 cm), never the lot");
    check(step(0.05f, 0.10f, 0.0f) == 0.05f && step(0.05f, 0.10f, -1.0f) == 0.05f &&
              step(0.05f, 0.10f, std::numeric_limits<float>::quiet_NaN()) == 0.05f,
          "a zero, negative or NaN dt moves nothing");
    check(step(std::numeric_limits<float>::quiet_NaN(), 0.0f, dt) == 0.0f, "a current that is not a number starts again from 0");
    float full = -0.10f;
    int fullFrames = 0;
    while (full < 0.20f && fullFrames < 100) {
        full = step(full, 0.20f, dt);
        ++fullFrames;
    }
    check(full == 0.20f && fullFrames >= 18 && fullFrames <= 20, "the whole 30 cm (-10 -> +20) takes about 0.3 s");
}

void partLine() {
    char line[64];
    formatLine(7, line, sizeof(line));
    check(!std::strcmp(line, "on-foot height +7 cm"), "+7");
    formatLine(-3, line, sizeof(line));
    check(!std::strcmp(line, "on-foot height -3 cm"), "-3");
    formatLine(0, line, sizeof(line));
    check(!std::strcmp(line, "on-foot height 0 cm"), "0");
}
}  // namespace

int main(int argc, char** argv) {
    if (argc < 2 || (std::strcmp(argv[1], "--self-test") != 0 && std::strcmp(argv[1], "--dry-run") != 0)) {
        std::printf("usage: on_foot_height_test --self-test | --dry-run (the pure half only; --dry-run writes nothing, as does --self-test)\n");
        return 2;
    }
    partKey();
    partTarget();
    partRamp();
    partLine();
    std::printf("%s: on_foot_height_test, %d checks, %d failed\n", g_failures ? "FAIL" : "PASS", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
