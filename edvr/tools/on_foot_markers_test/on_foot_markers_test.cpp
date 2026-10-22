// Build gate for the EDVRonFOOT on-foot markers (src\d3d11\on_foot_markers.h, experimental.native_vr_on_foot_markers).
//
// Part A drives on_foot_markers_core.h: the key (no line, stereo, flat, off, any case, garbage), the decision over native_vr_on_foot,
// the key and the journal, the corrected pose (stereo keeps the eyes' distance and does not move with a +/-60 degree turn or a 20
// degree tilt of the head, flat puts both at 0, both a fixed point), the identity test, the chain to both cameras on a fake game in
// this rig's memory with each of its refusals, the toggle, the 30-second window and every line's text, "0 writer calls" among them.
// Part B compiles the REAL glue (on_foot_markers.cpp) with the real CodeHook and hooks synthetic writers that begin with the game's
// 16 bytes, on fake passes and cameras with a fake commit: another build and a wrong byte refuse everything with one line; then both
// are hooked; with the mode off the writers are only forwarded; on, both eyes' rows are the identity, the flags |= 0xF, the commit
// called with (cam, size, cam+0x20); a nested writer gives one step; a mismatched camera and a wrong view id skip the frame; the frame
// step's key, on and off lines, the end-of-frame check, the summary, and three commit faults turning it off.
#include "../../src/d3d11/on_foot_markers.h"
#include "../../src/d3d11/on_foot_markers_core.h"
#include <windows.h>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace edvr {
void breadcrumb(const char*) {}  // production guard.cpp's crash-channel dependency, as the other rigs stub it
bool journalOnFootKnown() { return false; }  // journal_watch.cpp's, for the frame boundary the rig never calls
bool journalOnFoot() { return false; }
}  // namespace edvr

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
void dumpLines() {
    for (const auto& l : g_lines) std::printf("  | %s\n", l.c_str());
}

uint64_t addr(const void* p) { return reinterpret_cast<uint64_t>(p); }
template <class T>
void put(void* base, size_t off, T v) {
    std::memcpy(static_cast<uint8_t*>(base) + off, &v, sizeof(v));
}
template <class T>
T get(const void* base, size_t off) {
    T v;
    std::memcpy(&v, static_cast<const uint8_t*>(base) + off, sizeof(v));
    return v;
}

bool plainRead(void*, uint64_t a, void* out, size_t n) {
    std::memcpy(out, reinterpret_cast<const void*>(a), n);
    return true;
}
bool noRead(void*, uint64_t, void*, size_t) { return false; }

bool within(float a, float b, float eps) { return std::fabs(a - b) <= eps; }

// A head pose as the game writes it into an eye's PostGUI camera: rows right, up, forward from yaw and pitch, row 3 the eye's position
// (the head's plus its offset e in the head's axes).
constexpr float kRad = 0.0174532925f;
void headPose(float yawDeg, float pitchDeg, const float head[3], const float e[3], float m[16]) {
    const float y = yawDeg * kRad, p = pitchDeg * kRad;
    const float right[3] = {std::cos(y), 0.f, -std::sin(y)};
    const float up[3] = {-std::sin(y) * std::sin(p), std::cos(p), -std::cos(y) * std::sin(p)};
    const float fwd[3] = {std::sin(y) * std::cos(p), std::sin(p), std::cos(y) * std::cos(p)};
    const float* rows[3] = {right, up, fwd};
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 3; ++c) m[4 * r + c] = rows[r][c];
        m[4 * r + 3] = 0.f;
    }
    for (int c = 0; c < 3; ++c) m[12 + c] = head[c] + e[0] * right[c] + e[1] * up[c] + e[2] * fwd[c];
    m[15] = 1.f;
}

// ---- the fake game: mgr -> pipeline -> frame -> passes -> pass 0/1 -> holder -> camera -> cam --------------------------------------

struct FakeGame {
    alignas(16) uint8_t mgr[0x800] = {};
    alignas(16) uint8_t pipeline[0x1200] = {};
    alignas(16) uint8_t frame[0x2D00] = {};
    alignas(16) uint8_t passes[0x100] = {};
    alignas(16) uint8_t pass[2][0x320] = {};
    alignas(16) uint8_t holder[2][0x40] = {};
    alignas(16) uint8_t camera[2][0x110] = {};
    alignas(16) uint8_t cam[2][0x400] = {};
    alignas(16) uint8_t other[0x400] = {};

    void build() {
        put(mgr, ofm::kMgrPipeline, addr(pipeline));
        put(pipeline, ofm::kPipelineFrame, addr(frame));
        put(frame, ofm::kFramePasses, addr(passes));
        for (int i = 0; i < 2; ++i) {
            put(passes, ofm::kPassesView0 + 8u * i, addr(pass[i]));
            put<int32_t>(pass[i], ofm::kPassView, i);
            put(pass[i], ofm::kPassPostGuiCamera, addr(cam[i]));
            put(pass[i], ofm::kPassCameraHolder, addr(holder[i]));
            put(holder[i], ofm::kHolderCamera, addr(camera[i]));
            put(camera[i], ofm::kHolderCameraObject, addr(cam[i]));
            put<uint64_t>(pass[i], ofm::kPassSize, 0x0000044000000780ull + i);
            put<uint32_t>(cam[i], ofm::kCamFlags, 0x100u);
        }
    }
    // The game's write: the head's pose into both eyes, the eyes 0.064 apart around a point off the head's centre.
    void writeHead(float yawDeg, float pitchDeg) {
        const float head[3] = {1.5f, 1.7f, -3.f};
        const float e0[3] = {-0.031f, 0.002f, 0.01f}, e1[3] = {0.033f, 0.002f, 0.01f};
        float m[16];
        headPose(yawDeg, pitchDeg, head, e0, m);
        std::memcpy(cam[0] + ofm::kCamPose, m, sizeof(m));
        headPose(yawDeg, pitchDeg, head, e1, m);
        std::memcpy(cam[1] + ofm::kCamPose, m, sizeof(m));
    }
    const float* pose(int i) const { return reinterpret_cast<const float*>(cam[i] + ofm::kCamPose); }
};

// ---- Part A: the pure core ------------------------------------------------------------------------------------------------------

void partA() {
    // The key.
    check(ofm::parseKey("stereo").mode == ofm::kModeStereo && ofm::parseKey("stereo").understood, "key: stereo");
    check(ofm::parseKey("flat").mode == ofm::kModeFlat && ofm::parseKey(" Flat ").mode == ofm::kModeFlat, "key: flat, any case and spaces");
    check(ofm::parseKey("off").mode == ofm::kModeOff && ofm::parseKey("OFF").understood, "key: off");
    {
        const ofm::Key k = ofm::parseKey("maybe");
        check(k.mode == ofm::kModeStereo && !k.understood, "key: maybe reads as stereo, not understood");
        const ofm::Key e = ofm::parseKey("");
        check(e.mode == ofm::kModeStereo && !e.understood, "key: empty reads as stereo, not understood");
        const ofm::Key n = ofm::parseKey(nullptr);
        check(n.mode == ofm::kModeStereo && !n.understood, "key: null reads as stereo");
        check(!ofm::parseKey("stereostereostereo").understood, "key: a long value is not understood");
    }

    // The decision.
    check(ofm::decide(false, ofm::kModeStereo, 1).mode == ofm::kModeNone && ofm::decide(false, ofm::kModeStereo, 1).why == ofm::kWhyNative,
          "decide: native_vr_on_foot off");
    check(ofm::decide(true, ofm::kModeOff, 1).mode == ofm::kModeNone && ofm::decide(true, ofm::kModeOff, 1).why == ofm::kWhyKey, "decide: key off");
    check(ofm::decide(true, ofm::kModeStereo, 0).mode == ofm::kModeNone && ofm::decide(true, ofm::kModeStereo, 0).why == ofm::kWhyShip,
          "decide: the ship");
    check(ofm::decide(true, ofm::kModeStereo, -1).mode == ofm::kModeNone && ofm::decide(true, ofm::kModeStereo, -1).why == ofm::kWhyJournal,
          "decide: journal unknown");
    check(ofm::decide(true, ofm::kModeStereo, 1).mode == ofm::kModeStereo && ofm::decide(true, ofm::kModeFlat, 1).mode == ofm::kModeFlat,
          "decide: on foot, by the key");

    // The corrected pose.
    FakeGame g;
    float in[2][16], out[2][16], again[2][16];
    const int yaws[3] = {0, 60, -60};
    float first[2][3] = {};
    bool keepsGap = true, still = true, fixed = true, identity = true, wKept = true;
    for (int t = 0; t < 3; ++t) {
        g.writeHead(static_cast<float>(yaws[t]), t ? 20.f : 0.f);
        std::memcpy(in[0], g.pose(0), sizeof(in[0]));
        std::memcpy(in[1], g.pose(1), sizeof(in[1]));
        ofm::correct(in, ofm::kModeStereo, out);
        keepsGap = keepsGap && within(ofm::eyeGap(out[0], out[1]), ofm::eyeGap(in[0], in[1]), 1e-4f);
        identity = identity && ofm::identityRows(out[0]) && ofm::identityRows(out[1]);
        wKept = wKept && out[0][15] == in[0][15] && out[1][15] == in[1][15];
        for (int i = 0; i < 2; ++i)
            for (int c = 0; c < 3; ++c) {
                if (!t) first[i][c] = out[i][12 + c];
                still = still && within(out[i][12 + c], first[i][c], 1e-3f);
            }
        ofm::correct(out, ofm::kModeStereo, again);
        for (int i = 0; i < 2; ++i)
            for (int j = 0; j < 16; ++j) fixed = fixed && within(again[i][j], out[i][j], 1e-6f);
    }
    check(identity && wKept, "stereo: rows the identity, the fourth number kept");
    check(keepsGap, "stereo: the eyes' distance kept");
    check(still, "stereo: a +/-60 degree turn and a 20 degree tilt do not move the positions (1 mm)");
    check(within(first[0][0], -0.032f, 1e-5f) && within(first[1][0], 0.032f, 1e-5f) && within(first[0][1], 0.f, 1e-5f) && within(first[0][2], 0.f, 1e-5f),
          "stereo: each eye's offset from the point between them, in the head's axes");
    check(fixed, "stereo: a second step changes nothing");
    ofm::correct(in, ofm::kModeFlat, out);
    check(ofm::identityRows(out[0]) && ofm::identityRows(out[1]) && out[0][12] == 0 && out[0][13] == 0 && out[0][14] == 0 && out[1][12] == 0 &&
              out[1][13] == 0 && out[1][14] == 0 && out[0][15] == 1.f,
          "flat: both at 0, rows the identity");
    ofm::correct(out, ofm::kModeFlat, again);
    check(std::memcmp(out, again, sizeof(out)) == 0, "flat: a second step changes nothing");
    check(!ofm::identityRows(in[0]), "identity: the game's turned head is not");

    // The chain.
    g.build();
    ofm::Pair p;
    check(ofm::locate(addr(g.mgr), &plainRead, nullptr, &p) == ofm::kSkipNone && p.cam[0] == addr(g.cam[0]) && p.cam[1] == addr(g.cam[1]) &&
              p.size[1] == 0x0000044000000781ull && p.m[0][0] == g.pose(0)[0],
          "locate: both cameras, their sizes and poses");
    check(ofm::locate(addr(g.mgr), &noRead, nullptr, &p) == ofm::kSkipUnreadable, "locate: unreadable");
    put<uint64_t>(g.frame, ofm::kFramePasses, 0);
    check(ofm::locate(addr(g.mgr), &plainRead, nullptr, &p) == ofm::kSkipNoPasses, "locate: no passes");
    put(g.frame, ofm::kFramePasses, addr(g.passes));
    put<uint64_t>(g.passes, ofm::kPassesView0 + 8, 0);
    check(ofm::locate(addr(g.mgr), &plainRead, nullptr, &p) == ofm::kSkipNoPasses, "locate: no pass of view 1");
    put(g.passes, ofm::kPassesView0 + 8, addr(g.pass[1]));
    put<int32_t>(g.pass[1], ofm::kPassView, 0);
    check(ofm::locate(addr(g.mgr), &plainRead, nullptr, &p) == ofm::kSkipViewId, "locate: a wrong view id");
    put<int32_t>(g.pass[1], ofm::kPassView, 1);
    put(g.pass[0], ofm::kPassPostGuiCamera, addr(g.other));
    check(ofm::locate(addr(g.mgr), &plainRead, nullptr, &p) == ofm::kSkipMismatch, "locate: the camera is not the pass's +0x50");
    put(g.pass[0], ofm::kPassPostGuiCamera, addr(g.cam[0]));
    put<uint64_t>(g.holder[1], ofm::kHolderCamera, 0);
    check(ofm::locate(addr(g.mgr), &plainRead, nullptr, &p) == ofm::kSkipMismatch, "locate: no camera behind the accessor");
    put(g.holder[1], ofm::kHolderCamera, addr(g.camera[1]));

    // The toggle.
    ofm::Toggle t;
    check(t.step(ofm::decide(true, ofm::kModeStereo, -1)) && !t.step(ofm::decide(true, ofm::kModeStereo, -1)), "toggle: the first, not a repeat");
    check(t.step(ofm::decide(true, ofm::kModeStereo, 1)) && t.step(ofm::decide(true, ofm::kModeFlat, 1)) && t.step(ofm::decide(true, ofm::kModeStereo, 0)),
          "toggle: on, the mode, off");

    // The window.
    ofm::Window w;
    ofm::Counts c0, c1, d;
    ofm::Check k;
    uint64_t s = 0;
    check(!w.due(100000, c0, &d, &k, &s), "window: off, never due");
    w.start(1000, c0);
    w.note(true, true);
    w.note(true, false);
    w.note(false, false);
    c1.poseCalls = 2160;
    c1.steps = 2160;
    c1.corrected = 2159;
    c1.skips[ofm::kSkipMismatch] = 1;
    check(!w.due(30999, c1, &d, &k, &s), "window: not before 30 s");
    check(w.due(31000, c1, &d, &k, &s) && s == 30 && d.poseCalls == 2160 && d.corrected == 2159 && k.ours == 1 && k.games == 1 && k.unreadable == 1,
          "window: due at 30 s with its counts and checks");
    check(!w.due(31000, c1, &d, &k, &s) && w.due(61000, c1, &d, &k, &s) && d.poseCalls == 0 && k.ours == 0, "window: the next starts at its end");
    w.stop();
    check(!w.due(200000, c1, &d, &k, &s), "window: stopped, no part window");

    // The lines.
    char line[900];
    ofm::formatKey("stereo", ofm::parseKey("stereo"), true, line, sizeof(line));
    check(std::string(line) == "on-foot markers: experimental.native_vr_on_foot_markers = stereo", "line: the key");
    ofm::formatKey("maybe", ofm::parseKey("maybe"), true, line, sizeof(line));
    check(std::strstr(line, "'maybe' is not stereo, flat or off: read as stereo") != nullptr, "line: a key not understood");
    ofm::formatKey("stereo", ofm::parseKey("stereo"), false, line, sizeof(line));
    check(std::strstr(line, "works only with experimental.native_vr_on_foot, which is off") != nullptr, "line: needs native_vr_on_foot");
    ofm::formatKey("off", ofm::parseKey("off"), false, line, sizeof(line));
    check(std::strstr(line, "works only") == nullptr, "line: off says nothing of native_vr_on_foot");
    ofm::formatToggle(ofm::decide(true, ofm::kModeFlat, 1), line, sizeof(line));
    check(std::string(line) == "on-foot markers: on (on foot, flat)", "line: on");
    ofm::formatToggle(ofm::decide(true, ofm::kModeStereo, 0), line, sizeof(line));
    check(std::string(line) == "on-foot markers: off (the ship)", "line: off in the ship");
    ofm::formatToggle(ofm::decide(true, ofm::kModeStereo, -1), line, sizeof(line));
    check(std::string(line) == "on-foot markers: off (journal unknown)", "line: off, journal unknown");
    ofm::formatWrongBytes(ofm::kPointEyes, line, sizeof(line));
    check(std::strstr(line, "NOT installed: the eye pose writer 0x28457F0 at EliteDangerous64.exe+0x28457F0 does not begin with build 334473's 16 bytes") !=
              nullptr,
          "line: wrong bytes name the point");
    ofm::formatWrongBuild(0x12345678u, 0x1000u, line, sizeof(line));
    check(std::strstr(line, "not build 334473 (PE stamp 0x12345678, image size 0x1000)") != nullptr, "line: another build");
    ofm::formatHooked(0x140000000ull + 0x287D660, 7, 0x140000000ull + 0x28457F0, 12, line, sizeof(line));
    check(std::strstr(line, "hooked 0x287D660 and 0x28457F0, 16 bytes verified at each and at the commit 0x2874820") != nullptr &&
              std::strstr(line, "7 moved") && std::strstr(line, "12 moved"),
          "line: hooked");
    ofm::formatDisabled(3, line, sizeof(line));
    check(std::strstr(line, "faulted 3 times: the correction is off until the game restarts") != nullptr, "line: disabled");

    ofm::Counts none;
    ofm::formatSummary(30, ofm::kModeStereo, none, ofm::Check{}, nullptr, line, sizeof(line));
    check(std::strstr(line, "on-foot markers: 30 s stereo: 0 writer calls (0x287D660 0, 0x28457F0 0)") != nullptr &&
              std::strstr(line, "0 step(s), 0 corrected, none skipped") && std::strstr(line, "frame check ours 0 / game's 0") &&
              std::strstr(line, "no sample"),
          "line: the summary with 0 writer calls");
    ofm::Counts some;
    some.poseCalls = 2160;
    some.eyesOne = 0;
    some.eyesOther = 4;
    some.steps = 2164;
    some.corrected = 2160;
    some.skips[ofm::kSkipMismatch] = 3;
    some.commitFaults = 1;
    ofm::Check ck;
    ck.ours = 2150;
    ck.games = 10;
    ck.unreadable = 2;
    ofm::Sample sample;
    g.writeHead(30.f, 10.f);
    std::memcpy(sample.game[0], g.pose(0), sizeof(sample.game[0]));
    std::memcpy(sample.game[1], g.pose(1), sizeof(sample.game[1]));
    ofm::correct(sample.game, ofm::kModeStereo, sample.ours);
    ofm::formatSummary(30, ofm::kModeStereo, some, ck, &sample, line, sizeof(line));
    check(std::strstr(line, "writer calls 0x287D660 2160, 0x28457F0 n=1 0, other n 4; 2164 step(s), 2160 corrected, skipped: 3 camera mismatch, 1 commit fault") !=
              nullptr,
          "line: the summary's calls, steps and skips");
    check(std::strstr(line, "frame check ours 2150 / game's 10 (2 unreadable)") != nullptr, "line: the summary's check");
    check(std::strstr(line, "sample: game view 0 yaw 30.0 pitch 10.0 -> ours rows identity, left (-0.0320,0.0000,0.0000) right (0.0320,0.0000,0.0000)") !=
                  nullptr &&
              std::strstr(line, "eye gap game 0.0640 ours 0.0640"),
          "line: the summary's sample");
}

// ---- Part B: the real glue on synthetic writers ----------------------------------------------------------------------------------

FakeGame g_game;
using FnWriter = uint64_t (*)(uint64_t, uint64_t, uint64_t, uint64_t);
FnWriter g_poseFn = nullptr, g_eyesFn = nullptr;
volatile int g_poseCalls = 0, g_eyesCalls = 0, g_commitCalls = 0;
volatile uint64_t g_poseA2 = 0, g_eyesN = 0, g_eyesA3 = 0;
bool g_nest = false, g_commitFault = false, g_commitArgsOk = true;
float g_yaw = 40.f;

uint64_t fakePose(uint64_t mgr, uint64_t a2) {
    ++g_poseCalls;
    g_poseA2 = a2;
    g_game.writeHead(g_yaw, 5.f);
    if (g_nest) g_eyesFn(mgr, 1, 0, 0);
    return 0x1234;
}
uint64_t fakeEyes(uint64_t, uint64_t n, uint64_t a3) {
    ++g_eyesCalls;
    g_eyesN = n;
    g_eyesA3 = a3;
    g_game.writeHead(g_yaw, 5.f);
    return 0x5678;
}
void fakeCommit(uint64_t cam, uint64_t size, uint64_t block) {
    if (g_commitFault) *reinterpret_cast<volatile int*>(uintptr_t(0x10)) = 1;
    ++g_commitCalls;
    const int i = cam == addr(g_game.cam[1]) ? 1 : 0;
    g_commitArgsOk = g_commitArgsOk && (cam == addr(g_game.cam[0]) || cam == addr(g_game.cam[1])) && block == cam + ofm::kCamPose &&
                     size == get<uint64_t>(g_game.pass[i], ofm::kPassSize);
    std::memcpy(reinterpret_cast<void*>(cam + 0x310), reinterpret_cast<const void*>(block), 64);
}

// The game's bytes, their own undoing, then mov rax, imm64; jmp rax into the stand-in with the arguments and the return address as the
// caller left them.
size_t writeFunction(uint8_t* at, const uint8_t* head, size_t headLen, const uint8_t* undo, size_t undoLen, const void* standIn) {
    size_t n = 0;
    std::memcpy(at + n, head, headLen);
    n += headLen;
    std::memcpy(at + n, undo, undoLen);
    n += undoLen;
    at[n++] = 0x48;
    at[n++] = 0xB8;
    const uint64_t target = addr(standIn);
    std::memcpy(at + n, &target, 8);
    n += 8;
    at[n++] = 0xFF;
    at[n++] = 0xE0;
    return n;
}

void partB() {
    auto* page = static_cast<uint8_t*>(VirtualAlloc(nullptr, 4096, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE));
    if (!page) {
        check(false, "glue: a code page for the synthetic writers");
        return;
    }
    uint8_t* poseAt = page;
    uint8_t* eyesAt = page + 0x100;
    uint8_t* commitAt = page + 0x200;
    // 0x287D660: mov rax,rsp; mov [rax+10h],rdx; push rbx; push rsi; sub rsp,68h; mov rbx,[rcx+58h] -- undone: add rsp,68h; pop rsi; pop rbx.
    const uint8_t poseHead[] = {0x48, 0x8B, 0xC4, 0x48, 0x89, 0x50, 0x10, 0x53, 0x56, 0x48, 0x83, 0xEC, 0x68, 0x48, 0x8B, 0x59, 0x58};
    const uint8_t poseUndo[] = {0x48, 0x83, 0xC4, 0x68, 0x5E, 0x5B};
    // 0x28457F0: push rbp; push rbx; push rsi; lea rbp,[rsp-17B0h]; mov eax,18B0h -- undone: pop rsi; pop rbx; pop rbp.
    const uint8_t eyesHead[] = {0x40, 0x55, 0x53, 0x56, 0x48, 0x8D, 0xAC, 0x24, 0x50, 0xE8, 0xFF, 0xFF, 0xB8, 0xB0, 0x18, 0x00, 0x00};
    const uint8_t eyesUndo[] = {0x5E, 0x5B, 0x5D};
    // 0x2874820: mov [rsp+10h],rbx; mov [rsp+20h],rsi; push rdi; sub rsp,270h -- undone: add rsp,270h; pop rdi.
    const uint8_t commitHead[] = {0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x20, 0x57, 0x48, 0x81, 0xEC, 0x70, 0x02, 0x00, 0x00};
    const uint8_t commitUndo[] = {0x48, 0x81, 0xC4, 0x70, 0x02, 0x00, 0x00, 0x5F};
    writeFunction(poseAt, poseHead, sizeof(poseHead), poseUndo, sizeof(poseUndo), reinterpret_cast<const void*>(&fakePose));
    writeFunction(eyesAt, eyesHead, sizeof(eyesHead), eyesUndo, sizeof(eyesUndo), reinterpret_cast<const void*>(&fakeEyes));
    writeFunction(commitAt, commitHead, sizeof(commitHead), commitUndo, sizeof(commitUndo), reinterpret_cast<const void*>(&fakeCommit));
    FlushInstructionCache(GetCurrentProcess(), page, 4096);
    g_game.build();
    const uintptr_t targets[ofm::kPointCount] = {addr(poseAt), addr(eyesAt), addr(commitAt)};
    g_poseFn = reinterpret_cast<FnWriter>(poseAt);
    g_eyesFn = reinterpret_cast<FnWriter>(eyesAt);
    const uint64_t mgr = addr(g_game.mgr);

    // The synthetic functions run as they are, before any hook.
    check(g_poseFn(mgr, 7, 0, 0) == 0x1234 && g_poseCalls == 1 && g_poseA2 == 7, "synthetic pose writer runs");
    check(g_eyesFn(mgr, 2, 0xAB, 0) == 0x5678 && g_eyesCalls == 1 && g_eyesA3 == 0xAB, "synthetic eye writer runs");

    // Another build.
    std::vector<uint8_t> pe(0x200, 0);
    put<uint32_t>(pe.data(), 0x3C, 0x80);
    put<uint32_t>(pe.data(), 0x80 + 8, 0x12345678u);
    put<uint32_t>(pe.data(), 0x80 + 0x50, 0x1000u);
    g_lines.clear();
    check(!onFootMarkersInstall(addr(pe.data()), targets, true, &sink, nullptr) && g_lines.size() == 1 &&
              anyLine("NOT installed: the game is not build 334473 (PE stamp 0x12345678, image size 0x1000)") && poseAt[0] == 0x48 && eyesAt[0] == 0x40,
          "glue: another build, one line, nothing patched");

    // A wrong byte at 0x28457F0: nothing patched, not even 0x287D660.
    eyesAt[3] = 0x57;
    g_lines.clear();
    check(!onFootMarkersInstall(0, targets, false, &sink, nullptr) && g_lines.size() == 1 &&
              anyLine("the eye pose writer 0x28457F0 at EliteDangerous64.exe+0x28457F0 does not begin with build 334473's 16 bytes") &&
              poseAt[0] == 0x48 && eyesAt[0] == 0x40 && eyesAt[3] == 0x57,
          "glue: a wrong byte, one line naming the point, nothing patched");
    eyesAt[3] = 0x56;
    commitAt[15] = 0x03;
    g_lines.clear();
    check(!onFootMarkersInstall(0, targets, false, &sink, nullptr) && anyLine("the commit 0x2874820 at") && poseAt[0] == 0x48,
          "glue: a wrong byte at the commit refuses too");
    commitAt[15] = 0x02;
    FlushInstructionCache(GetCurrentProcess(), page, 4096);

    // Hooked.
    g_lines.clear();
    check(onFootMarkersInstall(0, targets, false, &sink, nullptr) && g_lines.size() == 1 &&
              anyLine("hooked 0x287D660 and 0x28457F0, 16 bytes verified at each and at the commit 0x2874820") && anyLine(", 7 moved;") &&
              anyLine(", 12 moved)") && poseAt[0] == 0xE9 && eyesAt[0] == 0xE9 && commitAt[0] == 0x48,
          "glue: both writers hooked, 7 and 12 bytes moved, the commit untouched");
    if (g_failures) dumpLines();
    g_lines.clear();
    check(onFootMarkersInstall(0, targets, false, &sink, nullptr) && g_lines.empty(), "glue: a second install says nothing");

    // Mode none: forwarded and counted, nothing touched.
    onFootMarkersSetMode(ofm::kModeNone);
    g_poseCalls = 0;
    check(g_poseFn(mgr, 9, 0, 0) == 0x1234 && g_poseCalls == 1 && g_poseA2 == 9, "glue: off, the pose writer forwarded with its arguments");
    check(!ofm::identityRows(g_game.pose(0)) && g_commitCalls == 0 && onFootMarkersCounts().poseCalls == 1 && onFootMarkersCounts().steps == 0,
          "glue: off, the game's pose stays, no step, no commit");

    // Stereo: both eyes the identity, flags |= 0xF, the commit with (cam, size, cam+0x20).
    onFootMarkersSetMode(ofm::kModeStereo);
    check(g_poseFn(mgr, 7, 0, 0) == 0x1234, "glue: on, the original's result returned");
    check(ofm::identityRows(g_game.pose(0)) && ofm::identityRows(g_game.pose(1)), "glue: on, both eyes' rows the identity");
    check(within(g_game.pose(0)[12], -0.032f, 1e-4f) && within(g_game.pose(1)[12], 0.032f, 1e-4f) && within(g_game.pose(0)[13], 0.f, 1e-4f) &&
              g_game.pose(0)[15] == 1.f,
          "glue: on, stereo positions");
    check(get<uint32_t>(g_game.cam[0], ofm::kCamFlags) == 0x10Fu && get<uint32_t>(g_game.cam[1], ofm::kCamFlags) == 0x10Fu, "glue: on, flags |= 0xF");
    check(g_commitCalls == 2 && g_commitArgsOk && std::memcmp(g_game.cam[0] + 0x310, g_game.cam[0] + ofm::kCamPose, 64) == 0,
          "glue: on, the commit called twice with (cam, size, cam+0x20)");
    {
        const ofm::Counts c = onFootMarkersCounts();
        check(c.poseCalls == 2 && c.steps == 1 && c.corrected == 1, "glue: on, one step corrected");
    }

    // 0x28457F0 from the pipeline frame (n = 2): counted apart, its third argument forwarded, corrected.
    check(g_eyesFn(mgr, 2, 0xCD, 0) == 0x5678 && g_eyesN == 2 && g_eyesA3 == 0xCD && g_commitCalls == 4 && onFootMarkersCounts().eyesOther == 1 &&
              onFootMarkersCounts().eyesOne == 0 && ofm::identityRows(g_game.pose(0)),
          "glue: the eye writer with n = 2, forwarded and corrected");

    // Nested: 0x287D660 calling 0x28457F0 (n = 1) gives one step.
    g_nest = true;
    g_poseFn(mgr, 7, 0, 0);
    g_nest = false;
    {
        const ofm::Counts c = onFootMarkersCounts();
        check(g_commitCalls == 6 && c.eyesOne == 1 && c.steps == 3 && c.corrected == 3, "glue: a nested writer, one step");
    }

    // A camera that is not the pass's own: the frame skipped for both eyes, the reason counted.
    put(g_game.pass[0], ofm::kPassPostGuiCamera, addr(g_game.other));
    g_poseFn(mgr, 7, 0, 0);
    check(!ofm::identityRows(g_game.pose(0)) && !ofm::identityRows(g_game.pose(1)) && g_commitCalls == 6 &&
              onFootMarkersCounts().skips[ofm::kSkipMismatch] == 1,
          "glue: camera mismatch, the frame skipped, counted");
    put(g_game.pass[0], ofm::kPassPostGuiCamera, addr(g_game.cam[0]));
    put<int32_t>(g_game.pass[1], ofm::kPassView, 0);
    g_poseFn(mgr, 7, 0, 0);
    check(!ofm::identityRows(g_game.pose(1)) && onFootMarkersCounts().skips[ofm::kSkipViewId] == 1, "glue: a wrong view id, skipped, counted");
    put<int32_t>(g_game.pass[1], ofm::kPassView, 1);

    // The frame step. 1000 s on: the journal does not know yet.
    g_lines.clear();
    onFootMarkersFrameStep(1000000, true, "stereo", -1, 0, targets, false, &sink, nullptr);
    check(anyLine("experimental.native_vr_on_foot_markers = stereo") && anyLine("on-foot markers: off (journal unknown)") && g_lines.size() == 2,
          "step: the key and off, journal unknown");
    g_poseFn(mgr, 7, 0, 0);
    check(!ofm::identityRows(g_game.pose(0)), "step: journal unknown, hands off");
    g_lines.clear();
    onFootMarkersFrameStep(1000100, true, "stereo", -1, 0, targets, false, &sink, nullptr);
    check(g_lines.empty(), "step: nothing to say while nothing changes");

    // On foot: on; a corrected frame checks ours, a third writer after it the game's. The game's are three: the frame that turns it on
    // still sees the view-id test's uncorrected pose, and the summary's own frame sees the third writer's.
    onFootMarkersFrameStep(1001000, true, "stereo", 1, 0, targets, false, &sink, nullptr);
    check(anyLine("on-foot markers: on (on foot, stereo)") && g_lines.size() == 1, "step: on foot, on");
    g_poseFn(mgr, 7, 0, 0);
    onFootMarkersFrameStep(1001010, true, "stereo", 1, 0, targets, false, &sink, nullptr);
    g_game.writeHead(10.f, 0.f);  // someone else, after the correction
    onFootMarkersFrameStep(1001020, true, "stereo", 1, 0, targets, false, &sink, nullptr);
    g_lines.clear();
    onFootMarkersFrameStep(1031000, true, "stereo", 1, 0, targets, false, &sink, nullptr);
    check(anyLine("on-foot markers: 30 s stereo: writer calls 0x287D660 1, 0x28457F0 n=1 0, other n 0; 1 step(s), 1 corrected, none skipped") &&
              anyLine("frame check ours 1 / game's 3") && anyLine("ours rows identity, left (-0.0320,0.0000,0.0000) right (0.0320,0.0000,0.0000)") &&
              anyLine("eye gap game 0.0640 ours 0.0640"),
          "step: the summary, its check and its sample");
    if (g_failures) dumpLines();
    g_lines.clear();
    onFootMarkersFrameStep(1061000, true, "stereo", 1, 0, targets, false, &sink, nullptr);
    check(anyLine("on-foot markers: 30 s stereo: 0 writer calls") && anyLine("no sample"), "step: a window with no writer call says 0");

    // The key: maybe (stereo, no toggle line), flat, off.
    g_lines.clear();
    onFootMarkersFrameStep(1062000, true, "maybe", 1, 0, targets, false, &sink, nullptr);
    check(anyLine("'maybe' is not stereo, flat or off: read as stereo") && g_lines.size() == 1, "step: maybe, read as stereo, still on");
    g_lines.clear();
    onFootMarkersFrameStep(1063000, true, "flat", 1, 0, targets, false, &sink, nullptr);
    check(anyLine("experimental.native_vr_on_foot_markers = flat") && anyLine("on-foot markers: on (on foot, flat)"), "step: flat");
    g_poseFn(mgr, 7, 0, 0);
    check(ofm::identityRows(g_game.pose(0)) && g_game.pose(0)[12] == 0.f && g_game.pose(1)[12] == 0.f && g_game.pose(1)[14] == 0.f,
          "step: flat, both at 0");
    g_lines.clear();
    onFootMarkersFrameStep(1064000, true, "off", 1, 0, targets, false, &sink, nullptr);
    check(anyLine("= off") && anyLine("on-foot markers: off (the key)"), "step: off by the key");
    g_poseFn(mgr, 7, 0, 0);
    check(!ofm::identityRows(g_game.pose(0)), "step: off, the game's pose stays");

    // Without native VR on foot; then the ship; no summary while off.
    g_lines.clear();
    onFootMarkersFrameStep(1065000, false, "stereo", 1, 0, targets, false, &sink, nullptr);
    check(anyLine("works only with experimental.native_vr_on_foot, which is off") && anyLine("off (native_vr_on_foot is off)"),
          "step: native_vr_on_foot off");
    g_lines.clear();
    onFootMarkersFrameStep(1066000, true, "stereo", 0, 0, targets, false, &sink, nullptr);
    check(anyLine("on-foot markers: off (the ship)"), "step: the ship");
    g_lines.clear();
    onFootMarkersFrameStep(1200000, true, "stereo", 0, 0, targets, false, &sink, nullptr);
    check(g_lines.empty(), "step: off, no summary");

    // Three commit faults: the correction off for good, one line, the writers only forwarded.
    onFootMarkersFrameStep(1201000, true, "stereo", 1, 0, targets, false, &sink, nullptr);
    g_commitFault = true;
    for (int i = 0; i < 3; ++i) g_poseFn(mgr, 7, 0, 0);
    g_commitFault = false;
    check(onFootMarkersDisabled() && onFootMarkersCounts().commitFaults == 3, "glue: three commit faults, disabled");
    g_lines.clear();
    onFootMarkersFrameStep(1201100, true, "stereo", 1, 0, targets, false, &sink, nullptr);
    check(anyLine("faulted 3 times: the correction is off until the game restarts") && anyLine("off (the commit faulted 3 times)"),
          "step: disabled, said");
    const int commits = g_commitCalls;
    check(g_poseFn(mgr, 7, 0, 0) == 0x1234 && g_commitCalls == commits && !ofm::identityRows(g_game.pose(0)), "glue: disabled, only forwarded");
}
}  // namespace

int main(int argc, char** argv) {
    if (argc < 2 || std::strcmp(argv[1], "--self-test") != 0) {
        std::printf("usage: on_foot_markers_test --self-test\n");
        return 2;
    }
    partA();
    partB();
    std::printf("%s: on_foot_markers_test, %d checks, %d failed\n", g_failures ? "FAIL" : "PASS", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
