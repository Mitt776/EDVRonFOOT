#include "on_foot_aim_eye.h"
#include "on_foot_aim_eye_core.h"
#include "journal_watch.h"
#include "xinput_watch.h"
#include "../common/aim_eye_signal.h"
#include "../common/config.h"
#include "../common/log.h"
#include <windows.h>
#include <string>

namespace edvr {

void onFootAimEyeFrameBoundary() {
    static uint32_t generation = ~0u;
    static bool saidKey = false;
    static oae::Key key = oae::Key::Right;
    static bool nativeOnFoot = true;
    static oae::Ramp ramp;
    static int64_t lastTicks = 0;
    static int64_t ticksPerSecond = 0;

    // The keys, live: read again only when the settings changed.
    const Config& cfg = Config::get();
    if (cfg.generation() != generation) {
        generation = cfg.generation();
        const oae::Key now = oae::parseKey(cfg.getString("experimental.native_vr_on_foot_aim_eye", "right").c_str());
        if (!saidKey || now != key) {
            char line[64];
            oae::formatKey(now, line, sizeof(line));
            Log::get().note("%s", line);
            saidKey = true;
        }
        key = now;
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

    oae::TargetIn in;
    in.key = key;
    in.nativeOnFoot = nativeOnFoot;
    in.journalKnown = journalOnFootKnown();
    in.onFoot = in.journalKnown && journalOnFoot();
    // The pad is polled for this alone only while aiming is possible: the rest of the time (the ship, the cinema, the key off) nothing
    // here costs a poll. One poll per frame however many ask (xinput_watch.cpp).
    if (in.key != oae::Key::Off && oae::onFootFlag(in)) {
        xinputWatchTick();
        XinputBinding lt;
        lt.trigger = 1;  // the left trigger: HumanoidZoomButton as it stands on this pad
        lt.valid = true;
        in.lt = xinputHeld(lt);
    }

    const oae::Out out = oae::step(ramp, key, oae::target(in), dt);
    aim_eye::Signal s;
    s.eye = out.eye;
    s.blend = out.blend;
    s.onFoot = oae::onFootFlag(in) ? 1u : 0u;
    aim_eye::publish(s, GetTickCount64());
}

}  // namespace edvr
