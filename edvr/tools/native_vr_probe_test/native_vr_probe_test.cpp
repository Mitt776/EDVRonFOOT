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
        nvp::formatInstalled(nvp::kPointCount, line, sizeof(line));
        check(std::strcmp(line, "native-vr-probe: installed on 5 of 5 points (build 332841)") == 0, "text: the install line, all points");
        nvp::formatInstalled(3, line, sizeof(line));
        check(std::strstr(line, "installed on 3 of 5 points") && std::strstr(line, "say why the rest"), "text: the install line, some refused");
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

// The game's prologue, then (rebuild) pop what it pushed, and mov rax, imm64; jmp rax into the C++ stand-in with rcx and the return
// address as the caller left them.
size_t writeJumpFunction(uint8_t* at, int point, void (*target)(uint8_t*)) {
    size_t n = 0, bytes = 0;
    const uint8_t* prologue = nvp::prologueOf(point, &bytes);
    std::memcpy(at, prologue, bytes);
    n += bytes;
    if (point == nvp::kPointBuild) {
        const uint8_t pops[] = {0x41, 0x5C, 0x5F, 0x5E, 0x5B, 0x5D};  // pop r12; pop rdi; pop rsi; pop rbx; pop rbp
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
    writeFunction(fnMode, &g_modeSelf, nullptr);
    writeFunction(fnOn, &g_onSelf, &g_onArg);
    writeFunction(fnOff, &g_offSelf, nullptr);
    writeJumpFunction(fnBuild, nvp::kPointBuild, &fakeBuild);
    writeJumpFunction(fnTeardown, nvp::kPointTeardown, &fakeTeardown);
    fnOff[0] = 0x41;  // not the game's prologue: the point must be refused
    FlushInstructionCache(GetCurrentProcess(), page, 4096);
    const uintptr_t targets[nvp::kPointCount] = {reinterpret_cast<uintptr_t>(fnMode), reinterpret_cast<uintptr_t>(fnOn),
                                                 reinterpret_cast<uintptr_t>(fnOff), reinterpret_cast<uintptr_t>(fnBuild),
                                                 reinterpret_cast<uintptr_t>(fnTeardown)};
    using Fn1 = void (*)(void*);
    using Fn2 = void (*)(void*, void*);
    auto callMode = reinterpret_cast<Fn1>(fnMode);
    auto callOn = reinterpret_cast<Fn2>(fnOn);
    auto callOff = reinterpret_cast<Fn1>(fnOff);

    g_lines.clear();
    int armed = nativeVrProbeInstall(0x140000000ull, targets, false, &sink, nullptr);
    check(armed == 4, "glue: four good prologues are hooked, the bad one is not");
    check(countLines("CinemaPipelineFeature deactivate at EliteDangerous64.exe+0x28AB150 NOT hooked") == 1,
          "glue: the refused point is named once, by its RVA");
    check(fnOff[0] == 0x41 && std::memcmp(fnOff + 1, nvp::kPrologue + 1, nvp::kPrologueBytes - 1) == 0,
          "glue: the refused point's bytes are untouched");
    check(fnMode[0] == 0xE9 && fnOn[0] == 0xE9 && fnBuild[0] == 0xE9 && fnTeardown[0] == 0xE9,
          "glue: the four hooked points now begin with the jump");
    check(anyLine("view pipeline rebuild hooked") && anyLine("7 prologue bytes verified, 5 moved") &&
              anyLine("only experimental.native_vr_block_cinema changes what it sees") && anyLine("view pipeline teardown hooked") &&
              anyLine("10 prologue bytes verified, 5 moved"),
          "glue: the rebuild and the teardown, each with its own prologue");

    fnOff[0] = 0x40;
    FlushInstructionCache(GetCurrentProcess(), fnOff, 1);
    g_lines.clear();
    armed = nativeVrProbeInstall(0x140000000ull, targets, false, &sink, nullptr);
    check(armed == 5 && countLines("hooked at") == 1 && anyLine("CinemaPipelineFeature deactivate hooked"),
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
