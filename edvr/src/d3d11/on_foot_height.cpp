#include "on_foot_height.h"
#include "on_foot_height_core.h"
#include "journal_watch.h"
#include "../common/on_foot_height_signal.h"
#include "../common/config.h"
#include "../common/log.h"
#include <windows.h>

namespace edvr {

void onFootHeightFrameBoundary() {
    static uint32_t generation = ~0u;
    static bool saidKey = false;
    static int cm = 0;
    static bool nativeOnFoot = true;
    static float height = 0.0f;
    static int64_t lastTicks = 0;
    static int64_t ticksPerSecond = 0;

    // The keys, live: read again only when the settings changed (F8, the installer, the file).
    const Config& cfg = Config::get();
    if (cfg.generation() != generation) {
        generation = cfg.generation();
        const int now = ofh::cmFromValue(cfg.getFloat("experimental.native_vr_on_foot_height", 0.0f));
        if (!saidKey || now != cm) {
            char line[64];
            ofh::formatLine(now, line, sizeof(line));
            Log::get().note("%s", line);
            saidKey = true;
        }
        cm = now;
        nativeOnFoot = cfg.getBool("experimental.native_vr_on_foot", true);
    }

    // The real time between frames: the first frame (and a clock that does not run) moves nothing.
    if (!ticksPerSecond) {
        LARGE_INTEGER f;
        QueryPerformanceFrequency(&f);
        ticksPerSecond = f.QuadPart;
    }
    LARGE_INTEGER c;
    QueryPerformanceCounter(&c);
    float dt = 0.0f;
    if (lastTicks && ticksPerSecond > 0 && c.QuadPart > lastTicks)
        dt = float(double(c.QuadPart - lastTicks) / double(ticksPerSecond));
    lastTicks = c.QuadPart;

    ofh::TargetIn in;
    in.cm = cm;
    in.nativeOnFoot = nativeOnFoot;
    in.journalKnown = journalOnFootKnown();
    in.onFoot = in.journalKnown && journalOnFoot();
    in.mainMenu = journalMainMenu();
    height = ofh::step(height, ofh::targetM(in), dt);
    on_foot_height::publish(height);
}

}  // namespace edvr
