// Build gate for the EDVRonFOOT native-VR-on-foot probe (src\d3d11\native_vr_probe.h, experimental.native_vr_probe).
//
// Part A drives native_vr_probe_core.h: the mode mapping, the ring (order, a full ring drops and counts, four producers lose
// nothing), the limiter (one line per event per second, a line budget, the summary) and the text of every line.
// Part B compiles the REAL glue (native_vr_probe.cpp) with the real CodeHook and hooks three synthetic functions that begin with the
// game's prologue: each hook forwards its arguments unchanged, records only while recording is on, reads the game's fields (and
// survives an unreadable one), and a point with the wrong prologue is refused with one line and left untouched, then hooked on a
// later call once its bytes are right.
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
}

// ---- Part B: the real glue on synthetic functions -------------------------------------------------------------------------------

volatile uint64_t g_modeSelf = 0, g_onSelf = 0, g_onArg = 0, g_offSelf = 0;

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

void partB() {
    auto* page = static_cast<uint8_t*>(VirtualAlloc(nullptr, 4096, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE));
    if (!page) {
        check(false, "glue: a code page for the synthetic functions");
        return;
    }
    uint8_t* fnMode = page;
    uint8_t* fnOn = page + 0x100;
    uint8_t* fnOff = page + 0x200;
    writeFunction(fnMode, &g_modeSelf, nullptr);
    writeFunction(fnOn, &g_onSelf, &g_onArg);
    writeFunction(fnOff, &g_offSelf, nullptr);
    fnOff[0] = 0x41;  // not the game's prologue: the point must be refused
    FlushInstructionCache(GetCurrentProcess(), page, 4096);
    const uintptr_t targets[nvp::kPointCount] = {reinterpret_cast<uintptr_t>(fnMode), reinterpret_cast<uintptr_t>(fnOn),
                                                 reinterpret_cast<uintptr_t>(fnOff)};
    using Fn1 = void (*)(void*);
    using Fn2 = void (*)(void*, void*);
    auto callMode = reinterpret_cast<Fn1>(fnMode);
    auto callOn = reinterpret_cast<Fn2>(fnOn);
    auto callOff = reinterpret_cast<Fn1>(fnOff);

    g_lines.clear();
    int armed = nativeVrProbeInstall(0x140000000ull, targets, false, &sink, nullptr);
    check(armed == 2, "glue: two good prologues are hooked, the bad one is not");
    check(countLines("CinemaPipelineFeature deactivate at EliteDangerous64.exe+0x28AB150 NOT hooked") == 1,
          "glue: the refused point is named once, by its RVA");
    check(fnOff[0] == 0x41 && std::memcmp(fnOff + 1, nvp::kPrologue + 1, nvp::kPrologueBytes - 1) == 0,
          "glue: the refused point's bytes are untouched");
    check(fnMode[0] == 0xE9 && fnOn[0] == 0xE9, "glue: the two hooked points now begin with the jump");

    fnOff[0] = 0x40;
    FlushInstructionCache(GetCurrentProcess(), fnOff, 1);
    g_lines.clear();
    armed = nativeVrProbeInstall(0x140000000ull, targets, false, &sink, nullptr);
    check(armed == 3 && countLines("hooked at") == 1 && anyLine("CinemaPipelineFeature deactivate hooked"),
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
    nativeVrProbeDrain(now, &sink, nullptr);
    check(g_modeSelf == reinterpret_cast<uint64_t>(&activity), "glue: recording, the original still gets the activity");
    check(countLines("stereo mode: requested 2 -> applied 2") == 1 && anyLine("owner 0x5555"), "glue: the mode line, with the owner");
    check(!anyLine("called from (no stack)"), "glue: the caller was captured");

    now += 2000;
    modeData[2] = 5;
    g_lines.clear();
    callMode(&activity);
    nativeVrProbeDrain(now, &sink, nullptr);
    check(countLines("requested 5 -> applied 1") == 1, "glue: a mode >= 3 is shown as the 1 the view gets");

    now += 2000;
    FakeActivity broken;
    broken.data = 0x10;  // the game's pointer is garbage: the read must fault, be caught, and the call go on
    g_lines.clear();
    g_modeSelf = 0;
    callMode(&broken);
    nativeVrProbeDrain(now, &sink, nullptr);
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
    nativeVrProbeDrain(now, &sink, nullptr);
    check(g_onSelf == reinterpret_cast<uint64_t>(feature) && g_onArg == reinterpret_cast<uint64_t>(&view),
          "glue: activate forwards both arguments unchanged");
    check(g_offSelf == reinterpret_cast<uint64_t>(feature), "glue: deactivate forwards its argument unchanged");
    check(countLines("cinema feature ON") == 1 && anyLine("active flag before 0, arg 0x1234"), "glue: the ON line with the arg");
    check(countLines("cinema feature OFF") == 1 && anyLine("active flag before 1"), "glue: the OFF line");

    now += 2000;
    modeData[2] = 0;
    g_lines.clear();
    for (int i = 0; i < 100; ++i) callMode(&activity);
    nativeVrProbeDrain(now, &sink, nullptr);
    check(countLines("requested 0 -> applied 0") == 1, "glue: a hundred identical calls in a frame write one line");
    nativeVrProbeDrain(now + 1500, &sink, nullptr);
    check(anyLine("99 repeat(s) were not written"), "glue: and the next window's summary counts the other 99");

    nativeVrProbeSetRecording(false);
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
