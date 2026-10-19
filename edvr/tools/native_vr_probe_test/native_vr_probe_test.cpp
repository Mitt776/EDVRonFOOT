// Build gate for the EDVRonFOOT native-VR-on-foot probe (src\d3d11\native_vr_probe.h, experimental.native_vr_probe).
//
// Part A drives native_vr_probe_core.h: the mode mapping, the ring (order, a full ring drops and counts, four producers lose
// nothing), the limiter (one line per event per second, a line budget, the summary) and the text of every line.
// Part B compiles the REAL glue (native_vr_probe.cpp) with the real CodeHook and hooks three synthetic functions that begin with the
// game's prologue: each hook forwards its arguments unchanged, records only while recording is on, reads the game's fields (and
// survives an unreadable one), and a point with the wrong prologue is refused with one line and left untouched, then hooked on a
// later call once its bytes are right.
// Part C hooks synthetic copies of the view pipeline's rebuild and teardown (their own prologues, then a jump into C++ that acts as the
// game does on a fake pipeline): the decision line is written only when it changes, and experimental.native_vr_block_cinema's
// substitution -- the rebuild sees 3 for 5 and 4 for 6, the field is the game's again after, anything else is left alone, a value the
// game writes during the call is kept, activate and deactivate still reach the game -- with its line, limited like the rest.
// Part D hooks synthetic copies of the stereo mode source and the camera choice: experimental.native_vr_on_foot's hold -- 5 becomes 3
// and 6 becomes 4 before the game's copy, and stays so between calls; a mode the game sets itself (3, 0) is left alone and ends the
// hold; nothing with the key off; a cinema mode is never written -- its start, repeat (limited, with the summary) and end lines, the
// end by the journal, and the camera line written only when the name or the mode changes.
#include "../../src/d3d11/native_vr_probe.h"
#include "../../src/d3d11/native_vr_probe_core.h"
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

namespace edvr {
void breadcrumb(const char*) {}  // production guard.cpp's crash-channel dependency (proxy.cpp), as the other rigs stub it
bool journalOnFootKnown() { return false; }  // journal_watch.cpp's, for the frame boundary the rig never calls
bool journalOnFoot() { return false; }
}

using namespace edvr;

namespace {
int g_checks = 0, g_failures = 0;
void check(bool ok, const char* what) {
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::printf("FAIL: %s\n", what);
    }
}

std::vector<std::string> g_lines;
void sink(void*, const char* line) { g_lines.emplace_back(line); }
size_t countLines(const char* needle) {
    size_t n = 0;
    for (const auto& l : g_lines) n += l.find(needle) != std::string::npos ? 1 : 0;
    return n;
}
bool anyLine(const char* needle) { return countLines(needle) > 0; }

nvp::Event modeEvent(uint32_t requested, uint64_t self) {
    nvp::Event e;
    e.point = nvp::kPointMode;
    e.readOk = 1;
    e.requested = requested;
    e.self = self;
    return e;
}

void partA() {
    check(nvp::appliedMode(0) == 0 && nvp::appliedMode(1) == 1 && nvp::appliedMode(2) == 2, "modes 0..2 reach the view as they are");
    check(nvp::appliedMode(3) == 1 && nvp::appliedMode(7) == 1 && nvp::appliedMode(0xFFFFFFFFu) == 1, "modes >= 3 reach the view as 1");

    {
        nvp::Ring<8> ring;
        bool all = true;
        for (uint32_t i = 0; i < 8; ++i) all = all && ring.push(modeEvent(i, 0));
        check(all, "ring: eight events fit a ring of eight");
        check(!ring.push(modeEvent(99, 0)) && ring.dropped() == 1, "ring: the ninth is dropped and counted");
        nvp::Event e;
        bool ordered = true;
        for (uint32_t i = 0; i < 8; ++i) ordered = ordered && ring.pop(&e) && e.requested == i;
        check(ordered && !ring.pop(&e), "ring: popped in order, then empty");
        check(ring.push(modeEvent(5, 0)) && ring.pop(&e) && e.requested == 5, "ring: usable again after draining");
    }
    {
        static nvp::Ring<1024> ring;
        std::vector<std::thread> producers;
        for (int t = 0; t < 4; ++t)
            producers.emplace_back([t] {
                for (uint32_t i = 0; i < 200; ++i)
                    while (!ring.push(modeEvent(i, static_cast<uint64_t>(t)))) std::this_thread::yield();
            });
        for (auto& p : producers) p.join();
        nvp::Event e;
        uint64_t count = 0, sum = 0;
        while (ring.pop(&e)) {
            ++count;
            sum += e.requested;
        }
        check(count == 800 && sum == 4ull * (199ull * 200ull / 2ull), "ring: four producers, 800 events, none lost or torn");
    }
    {
        nvp::Limiter limiter;
        const nvp::Event e = modeEvent(2, 0x1000);
        check(limiter.admit(e, 0), "limiter: the first event is written");
        bool held = true;
        for (uint64_t t = 1; t < 1000; ++t) held = held && !limiter.admit(e, t);
        check(held, "limiter: 999 repeats within the second are not");
        char line[400];
        check(limiter.summary(500, 0, line, sizeof(line)) == 0, "limiter: no summary before the window ends");
        check(limiter.admit(e, 1000), "limiter: the same event is written again a second later");
        check(limiter.summary(1000, 0, line, sizeof(line)) > 0 && std::strstr(line, "999 repeat(s)"),
              "limiter: the summary counts the 999 repeats");
        check(limiter.summary(1001, 0, line, sizeof(line)) == 0, "limiter: a summary is said once");
        int written = 0;
        for (uint64_t i = 0; i < 30; ++i) written += limiter.admit(modeEvent(1, 0x2000 + i), 2000) ? 1 : 0;
        check(written == static_cast<int>(nvp::Limiter::kBudget), "limiter: 30 distinct events in a second: the budget's 20 are written");
        check(limiter.summary(3000, 4, line, sizeof(line)) > 0 && std::strstr(line, "10 over") && std::strstr(line, "4 event(s) lost"),
              "limiter: the summary names the 10 over budget and the ring's losses");
    }
    {
        const uint64_t base = 0x140000000ull, size = nvp::kExpectedImageSize;
        nvp::Event e = modeEvent(5, 0xABC);
        e.other = 0xDEF;
        e.frames = 2;
        e.stack[0] = base + 0x1234;
        e.stack[1] = 0x7FF000001000ull;
        char line[700];
        nvp::formatEvent(e, base, size, line, sizeof(line));
        check(std::strstr(line, "stereo mode: requested 5 -> applied 1") && std::strstr(line, "activity 0xABC, owner 0xDEF") &&
                  std::strstr(line, "called from EliteDangerous64.exe+0x1234") && std::strstr(line, "then 0x7FF000001000"),
              "text: the mode line names both modes, the objects, the caller inside the exe and a frame outside it");
        e.readOk = 0;
        nvp::formatEvent(e, base, size, line, sizeof(line));
        check(std::strstr(line, "requested ? (the activity could not be read)") != nullptr, "text: an unreadable activity says so");
        e.point = nvp::kPointCinemaOn;
        e.readOk = 1;
        e.flagBefore = 0;
        e.frames = 0;
        nvp::formatEvent(e, base, size, line, sizeof(line));
        check(std::strstr(line, "cinema feature ON") && std::strstr(line, "active flag before 0") && std::strstr(line, "called from (no stack)"),
              "text: the cinema line, and a capture with no frames");
        e.point = nvp::kPointCinemaOff;
        e.flagBefore = 1;
        nvp::formatEvent(e, base, size, line, sizeof(line));
        check(std::strstr(line, "cinema feature OFF") && std::strstr(line, "active flag before 1"), "text: the cinema off line");
    }
    check(nvp::cinemaModeSubstitute(5) == 3 && nvp::cinemaModeSubstitute(6) == 4, "block: cinema 5 and 6 become headset stereo 3 and 4");
    check(nvp::cinemaModeSubstitute(0) == 0 && nvp::cinemaModeSubstitute(3) == 0 && nvp::cinemaModeSubstitute(4) == 0 &&
              nvp::cinemaModeSubstitute(7) == 0 && nvp::cinemaModeSubstitute(-1) == 0,
          "block: every other mode is left alone");
    {
        char line[700];
        nvp::Event e;
        e.point = nvp::kPointBuild;
        e.readOk = 1;
        e.self = 0xAB0;
        e.mode = 5;
        e.built = 3;
        e.cinema = 3;
        nvp::formatEvent(e, 0, 0, line, sizeof(line));
        check(std::strstr(line, "cinema decision (rebuild): view pipeline 0xAB0, stereo mode 5 (cinema); result: sub-pipelines built 0,1, "
                                "with cinema 0,1") != nullptr,
              "text: the rebuild decision line");
        e.mode = 3;
        e.cinema = 0;
        nvp::formatEvent(e, 0, 0, line, sizeof(line));
        check(std::strstr(line, "stereo mode 3 (headset stereo)") && std::strstr(line, "with cinema none"), "text: a stereo rebuild");
        e.mode = 5;
        e.substituted = 3;
        nvp::formatEvent(e, 0, 0, line, sizeof(line), 1);
        check(std::strstr(line, "native-vr-block-cinema: cinema blocked on a rebuild: view pipeline 0xAB0, stereo mode 5 -> 3 for this "
                                "rebuild (back to 5 after)") &&
                  std::strstr(line, "on foot (journal): yes"),
              "text: the block line, with the journal's answer");
        nvp::formatEvent(e, 0, 0, line, sizeof(line), -1);
        check(std::strstr(line, "on foot (journal): unknown") != nullptr, "text: the block line when the journal does not know");
        e.point = nvp::kPointTeardown;
        e.substituted = 0;
        e.built = 3;
        e.cinema = 3;
        nvp::formatEvent(e, 0, 0, line, sizeof(line));
        check(std::strstr(line, "cinema decision (teardown): view pipeline 0xAB0, stereo mode 5; tearing down sub-pipelines built 0,1, "
                                "with cinema 0,1") != nullptr,
              "text: the teardown line");
        e.readOk = 0;
        nvp::formatEvent(e, 0, 0, line, sizeof(line));
        check(std::strstr(line, "view pipeline 0xAB0 could not be read") != nullptr, "text: an unreadable pipeline says so");
        nvp::formatInstalled(nvp::kPointCount, true, line, sizeof(line));
        check(std::strcmp(line, "native-vr-probe: installed on 7 of 7 points (build 332841)") == 0, "text: the install line, all points");
        nvp::formatInstalled(3, true, line, sizeof(line));
        check(std::strstr(line, "installed on 3 of 7 points") && std::strstr(line, "say why the rest") && !std::strstr(line, "camera"),
              "text: the install line, some refused");
        nvp::formatOnFootKey(false, false, line, sizeof(line));
        check(std::strcmp(line, "native-vr-on-foot: off (experimental.native_vr_on_foot = 0)") == 0, "text: the key's first line, off");
        nvp::formatOnFootKey(true, true, line, sizeof(line));
        check(std::strstr(line, "native-vr-on-foot: on: a cinema mode the game sets is replaced by headset stereo") &&
                  std::strstr(line, "native_vr_on_foot = 1)"),
              "text: the key's line, on");
        nvp::formatOnFootKey(false, true, line, sizeof(line));
        check(std::strstr(line, "native-vr-on-foot: off: no new hold starts") != nullptr, "text: the key's line, off again");
        nvp::formatInstalled(6, false, line, sizeof(line));
        check(std::strstr(line, "installed on 6 of 7 points") && std::strstr(line, "camera: skipped (its point is not hooked"),
              "text: the install line says when the camera is skipped");
    }
    {
        nvp::HoldState s;
        nvp::HoldStep r = nvp::holdStep(s, false, 5);
        check(!r.write && r.kind == nvp::kHoldNone && !s.holding, "hold: key off, cinema is left alone");
        r = nvp::holdStep(s, true, 5);
        check(r.write == 3 && r.kind == nvp::kHoldStart && s.holding && s.value == 3, "hold: key on, 5 becomes 3 and the hold starts");
        r = nvp::holdStep(s, true, 3);
        check(!r.write && r.kind == nvp::kHoldNone && s.holding, "hold: ours in place, nothing to do");
        r = nvp::holdStep(s, true, 5);
        check(r.write == 3 && r.kind == nvp::kHoldAgain, "hold: the game sets 5 again over ours: a repeat");
        r = nvp::holdStep(s, true, 6);
        check(r.write == 4 && r.kind == nvp::kHoldAgain && s.value == 4, "hold: 6 becomes 4");
        r = nvp::holdStep(s, true, 0);
        check(!r.write && r.kind == nvp::kHoldEnd && !s.holding, "hold: a mode of the game's own ends it, unwritten");
        r = nvp::holdStep(s, true, 3);
        check(!r.write && r.kind == nvp::kHoldNone, "hold: the game's own stereo is left alone");
        nvp::holdStep(s, true, 5);
        r = nvp::holdStep(s, false, 5);
        check(!r.write && r.kind == nvp::kHoldEnd && !s.holding, "hold: key off and the game sets 5: the hold ends, 5 stays the game's");
        bool neverCinema = true;
        for (int key = 0; key < 2; ++key)
            for (int32_t m = -2; m < 10; ++m)
                for (int h = 0; h < 2; ++h) {
                    nvp::HoldState t;
                    t.holding = h != 0;
                    t.value = 3;
                    const int32_t w = nvp::holdStep(t, key != 0, m).write;
                    neverCinema = neverCinema && w != 5 && w != 6 && (w == 0 || m == 5 || m == 6);
                }
        check(neverCinema, "hold: in no case is 5 or 6 written, and nothing but a cinema mode is written over");
    }
    {
        char line[700];
        nvp::Event e;
        e.point = nvp::kPointSource;
        e.readOk = 1;
        e.self = 0x100;
        e.other = 0x200;
        e.aux = 0x10000;
        e.mode = 5;
        e.substituted = 3;
        e.kind = nvp::kHoldStart;
        nvp::formatEvent(e, 0, 0, line, sizeof(line), 0);
        check(std::strstr(line, "native-vr-on-foot: hold start: the game set stereo mode 5 (cinema), put back to 3 (headset stereo)") &&
                  std::strstr(line, "on foot (journal): no") && std::strstr(line, "renderer 0x10000 (its view settings 0x12CC0"),
              "text: the hold start line, with the renderer's view settings to compare");
        e.kind = nvp::kHoldAgain;
        e.aux = 7;
        nvp::formatEvent(e, 0, 0, line, sizeof(line), 1);
        check(std::strstr(line, "hold again: the game set stereo mode 5 over ours, put back to 3 (7 time(s)") != nullptr,
              "text: the repeat line counts the repeats");
        e.kind = nvp::kHoldEnd;
        e.mode = 0;
        nvp::formatEvent(e, 0, 0, line, sizeof(line), 1);
        check(std::strstr(line, "native-vr-on-foot: hold end: game set mode 0 itself") != nullptr, "text: the end line");
        e.kind = nvp::kHoldNone;
        e.mode = 3;
        e.aux = 0x10000;
        nvp::formatEvent(e, 0, 0, line, sizeof(line));
        check(std::strstr(line, "native-vr-probe: stereo mode source: the game wants stereo mode 3 (headset stereo)") &&
                  std::strstr(line, "its view settings 0x12CC0"),
              "text: the probe's source line");
        e.point = nvp::kPointCamera;
        std::strcpy(e.name, "HMD First Person Camera");
        e.mode = 3;
        e.substituted = 3;
        nvp::formatEvent(e, 0, 0, line, sizeof(line));
        check(std::strstr(line, "native-vr-probe: camera \"HMD First Person Camera\" at stereo mode 3, held by native_vr_on_foot") != nullptr,
              "text: the camera line, held");
        e.substituted = 0;
        e.readOk = 0;
        nvp::formatEvent(e, 0, 0, line, sizeof(line));
        check(std::strstr(line, "camera: the name could not be read, at stereo mode 3;") != nullptr, "text: an unreadable camera name");
    }
    {
        nvp::ChangeFilter changes;
        nvp::Event e;
        e.point = nvp::kPointBuild;
        e.readOk = 1;
        e.self = 0x10;
        e.mode = 5;
        int written = 0;
        for (int i = 0; i < 1000; ++i) written += changes.admit(e) ? 1 : 0;
        check(written == 1, "changes: a thousand identical rebuilds give one line");
        e.mode = 3;
        check(changes.admit(e), "changes: a different mode gives another");
        e.self = 0x20;
        check(changes.admit(e), "changes: another pipeline is its own");
        e.self = 0x10;
        e.point = nvp::kPointTeardown;
        check(changes.admit(e), "changes: the teardown is apart from the rebuild");
        nvp::Event c;
        c.point = nvp::kPointCamera;
        c.readOk = 1;
        c.mode = 5;
        std::strcpy(c.name, "First Person Camera");
        written = 0;
        for (int i = 0; i < 1000; ++i) {
            c.self = 0x1000 + i;  // the camera object is not part of it
            written += changes.admit(c) ? 1 : 0;
        }
        check(written == 1, "changes: a thousand identical camera choices give one line");
        std::strcpy(c.name, "HMD First Person Camera");
        check(changes.admit(c), "changes: a changed camera name gives another");
        c.mode = 3;
        check(changes.admit(c), "changes: a changed mode with the same name gives another");
    }
}

// ---- Part B: the real glue on synthetic functions -------------------------------------------------------------------------------

volatile uint64_t g_modeSelf = 0, g_onSelf = 0, g_onArg = 0, g_offSelf = 0, g_teardownSelf = 0;
volatile int32_t g_buildSaw = -1;
volatile int32_t g_buildWrites = 0;  // when not 0, the fake rebuild writes it into the mode, as the game might mid-call

// A fake view pipeline: P+0x28 the settings (mode at +0x9D4), P+0x90.. four sub-pipelines with +0x329 built and +0x330 cinema.
uint8_t g_config[0xA00];
uint8_t g_subs[nvp::kSubPipelines][0x340];
uint8_t g_pipeline[0xB0];
int32_t& pipelineMode() { return *reinterpret_cast<int32_t*>(g_config + nvp::kConfigStereoMode); }

// What the game's rebuild does with the mode it reads (docs: "Решение о кинотеатре").
void fakeBuild(uint8_t* pipeline) {
    const uint8_t* config = *reinterpret_cast<uint8_t**>(pipeline + nvp::kPipelineConfig);
    const int32_t mode = *reinterpret_cast<const int32_t*>(config + nvp::kConfigStereoMode);
    g_buildSaw = mode;
    const bool cinema = mode == 5 || mode == 6;
    for (int i = 0; i < 2; ++i) {
        if (i == 1 && mode == 0) break;
        uint8_t* sub = *reinterpret_cast<uint8_t**>(pipeline + nvp::kPipelineSubs + 8 * i);
        sub[nvp::kSubBuilt] = 1;
        sub[nvp::kSubCinema] = cinema ? 1 : 0;
    }
    if (g_buildWrites) pipelineMode() = g_buildWrites;
}
void fakeTeardown(uint8_t* pipeline) {
    g_teardownSelf = reinterpret_cast<uint64_t>(pipeline);
    for (int i = 0; i < nvp::kSubPipelines; ++i) {
        uint8_t* sub = *reinterpret_cast<uint8_t**>(pipeline + nvp::kPipelineSubs + 8 * i);
        if (!sub) continue;
        sub[nvp::kSubBuilt] = 0;
        sub[nvp::kSubCinema] = 0;
    }
}

// The stereo mode source's stand-in: this+0x7F8 the object X (mode at +0x20), this+0x50 the renderer S (request at +0x3698).
uint8_t g_sourceObject[0x40];
uint8_t g_renderer[0x3700];
uint8_t g_sourceThis[0x800];
volatile int32_t g_sourceSaw = -1;
int32_t& sourceMode() { return *reinterpret_cast<int32_t*>(g_sourceObject + nvp::kSourceStereoMode); }
int32_t& rendererRequest() { return *reinterpret_cast<int32_t*>(g_renderer + 0x3698); }

// What the game's source task does with the mode (docs: "Читатели и писатели режима"): copies it into the request.
uint64_t fakeSource(uint8_t* self, void*, void*, void*) {
    const uint8_t* x = *reinterpret_cast<uint8_t**>(self + nvp::kSourceObject);
    uint8_t* s = *reinterpret_cast<uint8_t**>(self + nvp::kSourceRenderer);
    if (reinterpret_cast<uintptr_t>(x) < 0x10000 || !s) return 0xC0FFEE;  // the rig's broken source: only the hook is under test
    const int32_t mode = *reinterpret_cast<const int32_t*>(x + nvp::kSourceStereoMode);
    g_sourceSaw = mode;
    *reinterpret_cast<int32_t*>(s + 0x3698) = mode;
    return 0xC0FFEE;
}

// The camera choice's stand-in: a new string at *out, as the game makes it (length at +0, characters from +0x14).
uint8_t g_cameraString[0x80];
volatile uint64_t g_cameraSelf = 0;
bool g_cameraNone = false;
void setCameraName(const char* name) {
    const uint64_t n = std::strlen(name);
    std::memcpy(g_cameraString, &n, 8);
    std::memcpy(g_cameraString + 8, &n, 8);
    std::memcpy(g_cameraString + nvp::kNameChars, name, n + 1);
}
void** fakeCamera(void* cam, void** out) {
    g_cameraSelf = reinterpret_cast<uint64_t>(cam);
    *out = g_cameraNone ? nullptr : g_cameraString;
    return out;
}

// The game's prologue, then pop what it pushed (rebuild, source), and mov rax, imm64; jmp rax into the C++ stand-in with the arguments
// and the return address as the caller left them.
size_t writeJumpFunction(uint8_t* at, int point, const void* target) {
    size_t n = 0, bytes = 0;
    const uint8_t* prologue = nvp::prologueOf(point, &bytes);
    std::memcpy(at, prologue, bytes);
    n += bytes;
    if (point == nvp::kPointBuild) {
        const uint8_t pops[] = {0x41, 0x5C, 0x5F, 0x5E, 0x5B, 0x5D};  // pop r12; pop rdi; pop rsi; pop rbx; pop rbp
        std::memcpy(at + n, pops, sizeof(pops));
        n += sizeof(pops);
    }
    if (point == nvp::kPointSource) {
        const uint8_t pops[] = {0x41, 0x5E, 0x5F, 0x5E, 0x5B, 0x5D};  // pop r14; pop rdi; pop rsi; pop rbx; pop rbp
        std::memcpy(at + n, pops, sizeof(pops));
        n += sizeof(pops);
    }
    at[n++] = 0x48;
    at[n++] = 0xB8;
    const uint64_t address = reinterpret_cast<uint64_t>(target);
    std::memcpy(at + n, &address, 8);
    n += 8;
    at[n++] = 0xFF;
    at[n++] = 0xE0;  // jmp rax
    return n;
}

// prologue; mov rax, imm64; mov [rax], rcx; [mov rax, imm64; mov [rax], rdx;] add rsp,20h; pop rbx; ret
size_t writeFunction(uint8_t* at, volatile uint64_t* first, volatile uint64_t* second) {
    size_t n = 0;
    std::memcpy(at + n, nvp::kPrologue, nvp::kPrologueBytes);
    n += nvp::kPrologueBytes;
    auto movRax = [&](volatile uint64_t* where) {
        at[n++] = 0x48;
        at[n++] = 0xB8;
        const uint64_t address = reinterpret_cast<uint64_t>(where);
        std::memcpy(at + n, &address, 8);
        n += 8;
    };
    movRax(first);
    at[n++] = 0x48; at[n++] = 0x89; at[n++] = 0x08;  // mov [rax], rcx
    if (second) {
        movRax(second);
        at[n++] = 0x48; at[n++] = 0x89; at[n++] = 0x10;  // mov [rax], rdx
    }
    at[n++] = 0x48; at[n++] = 0x83; at[n++] = 0xC4; at[n++] = 0x20;  // add rsp, 20h
    at[n++] = 0x5B;                                                  // pop rbx
    at[n++] = 0xC3;                                                  // ret
    return n;
}

struct FakeActivity {
    uint64_t vtable = 0;
    uint64_t owner = 0;
    uint64_t data = 0;
};

void partC(void (*callBuild)(void*), void (*callTeardown)(void*), void (*callOn)(void*, void*), void (*callOff)(void*), uint64_t now) {
    *reinterpret_cast<uint8_t**>(g_pipeline + nvp::kPipelineConfig) = g_config;
    for (int i = 0; i < nvp::kSubPipelines; ++i) *reinterpret_cast<uint8_t**>(g_pipeline + nvp::kPipelineSubs + 8 * i) = g_subs[i];

    // Probe and block off: the rebuild is the game's, nothing is recorded.
    nativeVrProbeSetRecording(false);
    nativeVrProbeSetBlockCinema(false);
    pipelineMode() = 5;
    g_lines.clear();
    callBuild(g_pipeline);
    nativeVrProbeDrain(now, -1, &sink, nullptr);
    check(g_buildSaw == 5 && g_subs[0][nvp::kSubCinema] == 1 && g_lines.empty(), "pipeline: both off, the rebuild sees 5 and builds cinema; no line");

    // Probe on: the decision lines, once per change.
    nativeVrProbeSetRecording(true);
    now += 2000;
    g_lines.clear();
    callTeardown(g_pipeline);
    check(g_teardownSelf == reinterpret_cast<uint64_t>(g_pipeline), "pipeline: the teardown forwards its argument");
    callBuild(g_pipeline);
    nativeVrProbeDrain(now, -1, &sink, nullptr);
    check(countLines("cinema decision (teardown)") == 1 && anyLine("tearing down sub-pipelines built 0,1, with cinema 0,1"),
          "pipeline: the teardown line shows what is torn down");
    check(countLines("cinema decision (rebuild)") == 1 && anyLine("stereo mode 5 (cinema); result: sub-pipelines built 0,1, with cinema 0,1"),
          "pipeline: the rebuild line shows the mode and the cinema result");
    check(!anyLine("called from (no stack)"), "pipeline: the caller was captured");

    now += 2000;
    g_lines.clear();
    for (int i = 0; i < 1000; ++i) callBuild(g_pipeline);
    nativeVrProbeDrain(now, -1, &sink, nullptr);
    check(countLines("cinema decision (rebuild)") == 0, "pipeline: a thousand more identical rebuilds add nothing");
    pipelineMode() = 3;
    callBuild(g_pipeline);
    nativeVrProbeDrain(now, -1, &sink, nullptr);
    check(countLines("cinema decision (rebuild)") == 1 && anyLine("stereo mode 3 (headset stereo)") && anyLine("with cinema none"),
          "pipeline: a changed mode is written at once");
    check(!anyLine("native-vr-block-cinema"), "pipeline: no block line while the block is off");

    // Block on, probe off: the substitution and its line, nothing else.
    nativeVrProbeSetRecording(false);
    nativeVrProbeSetBlockCinema(true);
    now += 2000;
    g_lines.clear();
    pipelineMode() = 5;
    callBuild(g_pipeline);
    nativeVrProbeDrain(now, 1, &sink, nullptr);
    check(g_buildSaw == 3 && pipelineMode() == 5, "block: the rebuild sees 3 for 5, and the field is 5 again after");
    check(g_subs[0][nvp::kSubCinema] == 0 && g_subs[1][nvp::kSubCinema] == 0 && g_subs[1][nvp::kSubBuilt] == 1,
          "block: so both eyes are built, without cinema");
    check(countLines("cinema blocked on a rebuild") == 1 && anyLine("stereo mode 5 -> 3 for this rebuild (back to 5 after)") &&
              anyLine("with cinema none") && anyLine("on foot (journal): yes"),
          "block: its line, with the result and the journal's answer");
    check(!anyLine("cinema decision"), "block: with the probe off no decision line is written");
    pipelineMode() = 6;
    callBuild(g_pipeline);
    check(g_buildSaw == 4 && pipelineMode() == 6, "block: 6 is seen as 4, and is 6 again after");
    pipelineMode() = 3;
    callBuild(g_pipeline);
    check(g_buildSaw == 3 && pipelineMode() == 3, "block: a stereo mode is left alone");
    pipelineMode() = 0;
    callBuild(g_pipeline);
    check(g_buildSaw == 0 && pipelineMode() == 0, "block: mono is left alone");
    pipelineMode() = 5;
    g_buildWrites = 7;
    callBuild(g_pipeline);
    g_buildWrites = 0;
    check(g_buildSaw == 3 && pipelineMode() == 7, "block: a value the game writes during the rebuild is kept, not overwritten with 5");

    now += 2000;
    nativeVrProbeDrain(now, 1, &sink, nullptr);
    now += 2000;
    g_lines.clear();
    pipelineMode() = 5;
    for (int i = 0; i < 100; ++i) callBuild(g_pipeline);
    nativeVrProbeDrain(now, 1, &sink, nullptr);
    check(countLines("cinema blocked on a rebuild") == 1, "block: a hundred blocked rebuilds in a frame write one line");
    nativeVrProbeDrain(now + 1500, 1, &sink, nullptr);
    check(anyLine("99 repeat(s) were not written"), "block: and the summary counts the other 99");

    // Activate and deactivate are the game's even with the block on.
    uint8_t feature[0x40] = {};
    uint64_t view = 0x77;
    g_onSelf = g_onArg = g_offSelf = 0;
    callOn(feature, &view);
    callOff(feature);
    check(g_onSelf == reinterpret_cast<uint64_t>(feature) && g_onArg == reinterpret_cast<uint64_t>(&view) &&
              g_offSelf == reinterpret_cast<uint64_t>(feature),
          "block: activate and deactivate still reach the game, unchanged");

    // Off again: the next rebuild is the game's own.
    nativeVrProbeSetBlockCinema(false);
    pipelineMode() = 5;
    callBuild(g_pipeline);
    check(g_buildSaw == 5 && g_subs[0][nvp::kSubCinema] == 1, "block: off again, the rebuild sees 5 and builds cinema");

    // An unreadable pipeline is forwarded and, with the probe on, said so.
    nativeVrProbeSetRecording(true);
    nativeVrProbeSetBlockCinema(true);
    uint8_t broken[0xB0] = {};
    *reinterpret_cast<uint64_t*>(broken + nvp::kPipelineConfig) = 0x10;
    now += 4000;
    g_lines.clear();
    callTeardown(broken);
    nativeVrProbeDrain(now, -1, &sink, nullptr);
    check(g_teardownSelf == reinterpret_cast<uint64_t>(broken) && countLines("could not be read") == 1,
          "pipeline: an unreadable pipeline is said so and still forwarded");
    nativeVrProbeSetBlockCinema(false);
    nativeVrProbeSetRecording(false);
}

using FnSourceCall = uint64_t (*)(void*, void*, void*, void*);
using FnCameraCall = void** (*)(void*, void**);

void partD(FnSourceCall callSource, FnCameraCall callCamera, uint64_t now) {
    *reinterpret_cast<uint8_t**>(g_sourceThis + nvp::kSourceObject) = g_sourceObject;
    *reinterpret_cast<uint8_t**>(g_sourceThis + nvp::kSourceRenderer) = g_renderer;
    auto frame = [&](int32_t gameSets) {  // the game sets the mode (or not: -1), then the source task runs
        if (gameSets >= 0) sourceMode() = gameSets;
        return callSource(g_sourceThis, nullptr, nullptr, nullptr);
    };

    // Key off, probe off: the game's own.
    nativeVrProbeSetRecording(false);
    nativeVrProbeSetOnFoot(false);
    g_lines.clear();
    const uint64_t result = frame(5);
    nativeVrProbeDrain(now, -1, &sink, nullptr);
    check(result == 0xC0FFEE, "source: the original's result is returned");
    check(g_sourceSaw == 5 && sourceMode() == 5 && rendererRequest() == 5 && g_lines.empty() && !nativeVrProbeHolding(),
          "source: key off, the game copies its 5; no line, no hold");

    // Key on, probe off: 5 -> 3 before the game's copy, and held between calls.
    nativeVrProbeSetOnFoot(true);
    now += 2000;
    g_lines.clear();
    frame(-1);
    nativeVrProbeDrain(now, 0, &sink, nullptr);
    check(g_sourceSaw == 3 && sourceMode() == 3 && rendererRequest() == 3 && nativeVrProbeHolding(),
          "source: key on, the game copies 3 for its 5 and the hold runs");
    check(countLines("native-vr-on-foot: hold start: the game set stereo mode 5 (cinema), put back to 3") == 1 &&
              anyLine("on foot (journal): no") && anyLine("its view settings 0x"),
          "source: one hold start line, with the journal and the view settings");
    check(!anyLine("native-vr-probe:"), "source: with the probe off no probe line");
    for (int i = 0; i < 10; ++i) frame(-1);
    nativeVrProbeDrain(now, 0, &sink, nullptr);
    check(g_sourceSaw == 3 && sourceMode() == 3 && countLines("native-vr-on-foot") == 1, "source: ten more frames: still 3, no more lines");

    // The game sets 5 again every frame: a hundred repeats, one line, then the summary.
    now += 2000;
    nativeVrProbeDrain(now, 0, &sink, nullptr);
    now += 2000;
    g_lines.clear();
    bool held = true;
    for (int i = 0; i < 100; ++i) {
        frame(5);
        held = held && g_sourceSaw == 3 && sourceMode() == 3;
    }
    nativeVrProbeDrain(now, 1, &sink, nullptr);
    check(held, "source: every repeat is put back to 3 before the game's copy");
    check(countLines("hold again") == 1 && anyLine("(1 time(s) since the hold started)") && countLines("hold start:") == 0,
          "source: a hundred repeats in a frame write one line");
    nativeVrProbeDrain(now + 1500, 1, &sink, nullptr);
    check(anyLine("99 repeat(s) were not written"), "source: and the summary counts the other 99");

    // 6 is held as 4; a mode of the game's own ends the hold and is not touched.
    now += 4000;
    g_lines.clear();
    frame(6);
    check(g_sourceSaw == 4 && sourceMode() == 4, "source: 6 is put back to 4");
    frame(0);
    nativeVrProbeDrain(now, 1, &sink, nullptr);
    check(g_sourceSaw == 0 && sourceMode() == 0 && !nativeVrProbeHolding() && countLines("hold end: game set mode 0 itself") == 1,
          "source: the game's 0 ends the hold, untouched, with one line");
    frame(3);
    check(g_sourceSaw == 3 && sourceMode() == 3 && !nativeVrProbeHolding(), "source: the game's own 3 is left alone, no hold");

    // Boarding: the game's 3 looks like ours; the journal's yes -> no ends the hold.
    now += 2000;
    g_lines.clear();
    frame(5);
    check(nativeVrProbeHolding(), "journal: a hold runs");
    nativeVrOnFootJournal(1, &sink, nullptr);
    frame(3);
    nativeVrOnFootJournal(0, &sink, nullptr);
    check(!nativeVrProbeHolding() && countLines("hold end: back in the ship (journal: on foot yes -> no)") == 1,
          "journal: on foot yes -> no ends the hold with one line");
    nativeVrOnFootJournal(0, &sink, nullptr);
    check(countLines("back in the ship") == 1, "journal: said once");

    // Key off while held: nothing new holds, the game's 5 stays 5 -- the hold never writes cinema itself.
    now += 2000;
    frame(5);
    check(nativeVrProbeHolding() && sourceMode() == 3, "key off: a hold runs before");
    nativeVrProbeSetOnFoot(false);
    g_lines.clear();
    frame(-1);
    check(sourceMode() == 3 && g_sourceSaw == 3, "key off: ours stays until the game writes, nothing is put back");
    frame(5);
    nativeVrProbeDrain(now, 1, &sink, nullptr);
    check(g_sourceSaw == 5 && sourceMode() == 5 && !nativeVrProbeHolding() && countLines("hold end: game set mode 5 itself") == 1,
          "key off: the game's next 5 is its own and ends the hold");

    // Probe on: the source's mode is a line when it changes, not every frame; an unreadable source is said once and forwarded.
    nativeVrProbeSetRecording(true);
    now += 2000;
    g_lines.clear();
    for (int i = 0; i < 50; ++i) frame(3);
    frame(5);
    nativeVrProbeDrain(now, 1, &sink, nullptr);
    check(countLines("stereo mode source: the game wants stereo mode 3 (headset stereo)") == 1 &&
              countLines("the game wants stereo mode 5 (cinema)") == 1,
          "probe: one source line per change of the game's mode");
    uint8_t broken[0x800] = {};
    *reinterpret_cast<uint64_t*>(broken + nvp::kSourceObject) = 0x10;
    now += 2000;
    g_lines.clear();
    nativeVrProbeSetOnFoot(true);
    callSource(broken, nullptr, nullptr, nullptr);
    callSource(broken, nullptr, nullptr, nullptr);
    nativeVrProbeDrain(now, 1, &sink, nullptr);
    check(countLines("stereo mode source 0x") == 1 && anyLine("could not be read"), "probe: an unreadable source is said once, forwarded");

    // The camera: its name after the original made it, a line only when the name or the mode changes.
    now += 2000;
    g_lines.clear();
    frame(5);  // held: 3
    setCameraName("HMD First Person Camera");
    uint8_t cam[16] = {};
    void* out = nullptr;
    void** r = callCamera(cam, &out);
    check(r == &out && out == g_cameraString && g_cameraSelf == reinterpret_cast<uint64_t>(cam),
          "camera: the original gets the camera and makes its string; its result is returned");
    for (int i = 0; i < 1000; ++i) callCamera(cam, &out);
    nativeVrProbeDrain(now, 1, &sink, nullptr);
    check(countLines("native-vr-probe: camera \"HMD First Person Camera\" at stereo mode 3, held by native_vr_on_foot") == 1,
          "camera: a thousand identical choices give one line, with the held mode");
    setCameraName("First Person Camera");
    callCamera(cam, &out);
    g_cameraNone = true;
    callCamera(cam, &out);
    g_cameraNone = false;
    nativeVrProbeDrain(now, 1, &sink, nullptr);
    check(countLines("camera \"First Person Camera\"") == 1 && countLines("camera \"(none)\"") == 1, "camera: each changed name is a line");
    nativeVrProbeSetRecording(false);
    g_lines.clear();
    setCameraName("Cockpit Cam");
    callCamera(cam, &out);
    nativeVrProbeDrain(now + 5000, 1, &sink, nullptr);
    check(out == g_cameraString && !anyLine("camera"), "camera: probe off, forwarded and not written");
    nativeVrProbeSetOnFoot(false);
    frame(0);
}

void partB() {
    auto* page = static_cast<uint8_t*>(VirtualAlloc(nullptr, 4096, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE));
    if (!page) {
        check(false, "glue: a code page for the synthetic functions");
        return;
    }
    uint8_t* fnMode = page;
    uint8_t* fnOn = page + 0x100;
    uint8_t* fnOff = page + 0x200;
    uint8_t* fnBuild = page + 0x300;
    uint8_t* fnTeardown = page + 0x400;
    uint8_t* fnSource = page + 0x500;
    uint8_t* fnCamera = page + 0x600;
    writeFunction(fnMode, &g_modeSelf, nullptr);
    writeFunction(fnOn, &g_onSelf, &g_onArg);
    writeFunction(fnOff, &g_offSelf, nullptr);
    writeJumpFunction(fnBuild, nvp::kPointBuild, reinterpret_cast<const void*>(&fakeBuild));
    writeJumpFunction(fnTeardown, nvp::kPointTeardown, reinterpret_cast<const void*>(&fakeTeardown));
    writeJumpFunction(fnSource, nvp::kPointSource, reinterpret_cast<const void*>(&fakeSource));
    writeJumpFunction(fnCamera, nvp::kPointCamera, reinterpret_cast<const void*>(&fakeCamera));
    fnOff[0] = 0x41;  // not the game's prologue: the point must be refused
    FlushInstructionCache(GetCurrentProcess(), page, 4096);
    const uintptr_t targets[nvp::kPointCount] = {reinterpret_cast<uintptr_t>(fnMode),     reinterpret_cast<uintptr_t>(fnOn),
                                                 reinterpret_cast<uintptr_t>(fnOff),      reinterpret_cast<uintptr_t>(fnBuild),
                                                 reinterpret_cast<uintptr_t>(fnTeardown), reinterpret_cast<uintptr_t>(fnSource),
                                                 reinterpret_cast<uintptr_t>(fnCamera)};
    using Fn1 = void (*)(void*);
    using Fn2 = void (*)(void*, void*);
    auto callMode = reinterpret_cast<Fn1>(fnMode);
    auto callOn = reinterpret_cast<Fn2>(fnOn);
    auto callOff = reinterpret_cast<Fn1>(fnOff);

    g_lines.clear();
    int armed = nativeVrProbeInstall(0x140000000ull, targets, false, &sink, nullptr);
    check(armed == 6, "glue: six good prologues are hooked, the bad one is not");
    check(countLines("CinemaPipelineFeature deactivate at EliteDangerous64.exe+0x28AB150 NOT hooked") == 1,
          "glue: the refused point is named once, by its RVA");
    check(fnOff[0] == 0x41 && std::memcmp(fnOff + 1, nvp::kPrologue + 1, nvp::kPrologueBytes - 1) == 0,
          "glue: the refused point's bytes are untouched");
    check(fnMode[0] == 0xE9 && fnOn[0] == 0xE9 && fnBuild[0] == 0xE9 && fnTeardown[0] == 0xE9 && fnSource[0] == 0xE9 && fnCamera[0] == 0xE9,
          "glue: the six hooked points now begin with the jump");
    check(anyLine("view pipeline rebuild hooked") && anyLine("7 prologue bytes verified, 5 moved") &&
              anyLine("only experimental.native_vr_block_cinema changes what it sees") && anyLine("view pipeline teardown hooked") &&
              anyLine("10 prologue bytes verified, 5 moved"),
          "glue: the rebuild and the teardown, each with its own prologue");
    check(anyLine("stereo mode source hooked at EliteDangerous64.exe+0x281AC10") &&
              anyLine("only experimental.native_vr_on_foot changes what it sees") &&
              anyLine("camera choice hooked at EliteDangerous64.exe+0x2DFC7A0"),
          "glue: the source and the camera, by their RVAs");

    fnOff[0] = 0x40;
    FlushInstructionCache(GetCurrentProcess(), fnOff, 1);
    g_lines.clear();
    armed = nativeVrProbeInstall(0x140000000ull, targets, false, &sink, nullptr);
    check(armed == 7 && countLines("hooked at") == 1 && anyLine("CinemaPipelineFeature deactivate hooked"),
          "glue: a later call hooks the corrected point and leaves the armed ones alone");

    uint64_t now = 100000;
    uint32_t modeData[4] = {0, 0, 2, 0};  // +8: the mode the data asked for
    FakeActivity activity;
    activity.owner = 0x5555;
    activity.data = reinterpret_cast<uint64_t>(modeData);

    // Not recording: forward only.
    const uint64_t before = nativeVrProbeRecorded();
    g_modeSelf = 0;
    callMode(&activity);
    check(g_modeSelf == reinterpret_cast<uint64_t>(&activity) && nativeVrProbeRecorded() == before,
          "glue: with recording off the original runs with the same argument and nothing is recorded");

    nativeVrProbeSetRecording(true);
    g_lines.clear();
    g_modeSelf = 0;
    callMode(&activity);
    nativeVrProbeDrain(now, -1, &sink, nullptr);
    check(g_modeSelf == reinterpret_cast<uint64_t>(&activity), "glue: recording, the original still gets the activity");
    check(countLines("stereo mode: requested 2 -> applied 2") == 1 && anyLine("owner 0x5555"), "glue: the mode line, with the owner");
    check(!anyLine("called from (no stack)"), "glue: the caller was captured");

    now += 2000;
    modeData[2] = 5;
    g_lines.clear();
    callMode(&activity);
    nativeVrProbeDrain(now, -1, &sink, nullptr);
    check(countLines("requested 5 -> applied 1") == 1, "glue: a mode >= 3 is shown as the 1 the view gets");

    now += 2000;
    FakeActivity broken;
    broken.data = 0x10;  // the game's pointer is garbage: the read must fault, be caught, and the call go on
    g_lines.clear();
    g_modeSelf = 0;
    callMode(&broken);
    nativeVrProbeDrain(now, -1, &sink, nullptr);
    check(g_modeSelf == reinterpret_cast<uint64_t>(&broken) && countLines("could not be read") == 1,
          "glue: an unreadable activity is said so and still forwarded");

    now += 2000;
    uint8_t feature[0x40] = {};
    uint64_t view = 0x1234;
    g_lines.clear();
    g_onSelf = g_onArg = 0;
    callOn(feature, &view);
    feature[0x20] = 1;  // what the real activate would have done
    g_offSelf = 0;
    callOff(feature);
    nativeVrProbeDrain(now, -1, &sink, nullptr);
    check(g_onSelf == reinterpret_cast<uint64_t>(feature) && g_onArg == reinterpret_cast<uint64_t>(&view),
          "glue: activate forwards both arguments unchanged");
    check(g_offSelf == reinterpret_cast<uint64_t>(feature), "glue: deactivate forwards its argument unchanged");
    check(countLines("cinema feature ON") == 1 && anyLine("active flag before 0, arg 0x1234"), "glue: the ON line with the arg");
    check(countLines("cinema feature OFF") == 1 && anyLine("active flag before 1"), "glue: the OFF line");

    now += 2000;
    modeData[2] = 0;
    g_lines.clear();
    for (int i = 0; i < 100; ++i) callMode(&activity);
    nativeVrProbeDrain(now, -1, &sink, nullptr);
    check(countLines("requested 0 -> applied 0") == 1, "glue: a hundred identical calls in a frame write one line");
    nativeVrProbeDrain(now + 1500, -1, &sink, nullptr);
    check(anyLine("99 repeat(s) were not written"), "glue: and the next window's summary counts the other 99");

    nativeVrProbeSetRecording(false);
    partC(reinterpret_cast<void (*)(void*)>(fnBuild), reinterpret_cast<void (*)(void*)>(fnTeardown), callOn, callOff, now + 5000);
    partD(reinterpret_cast<FnSourceCall>(fnSource), reinterpret_cast<FnCameraCall>(fnCamera), now + 100000);
}
}  // namespace

int main(int argc, char** argv) {
    if (argc < 2 || std::strcmp(argv[1], "--self-test") != 0) {
        std::printf("usage: native_vr_probe_test --self-test\n");
        return 2;
    }
    partA();
    partB();
    std::printf("%s: native_vr_probe_test, %d checks, %d failed\n", g_failures ? "FAIL" : "PASS", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
