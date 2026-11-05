// Build gate for the EDVRonFOOT on-foot markers (src\d3d11\on_foot_markers.h).
//
// Part A drives on_foot_markers_core.h: the decision over native_vr_on_foot and the journal, the corrected pose (it keeps the eyes'
// distance and does not move with a +/-60 degree turn or a 20 degree tilt of the head, a fixed point), the identity test, the search
// for the three points on a synthetic image (found; an entry missing; an entry twice; no chain; no call of either writer; a call past
// the window; other bytes where the patterns allow them; a copy outside the executable sections ignored; the sections from PE headers),
// the chain to both cameras on a fake game in this rig's memory with each of its refusals, the toggle, the sample box's reset, the
// 30-second window and every line's text, "0 writer calls" among them.
// Part B compiles the REAL glue (on_foot_markers.cpp) with the real CodeHook and hooks synthetic writers in an executable synthetic
// image whose entries run the game's own prologue shapes, on fake passes and cameras with a fake commit: an entry not found and a call
// missing refuse everything with one line; then both are found and hooked; off, the writers are only forwarded; on, both eyes' rows are
// the identity, the flags |= 0xF, the commit called with (cam, size, cam+0x20); a nested writer gives one step; a mismatched camera and
// a wrong view id skip the frame; the frame step's on and off lines, the end-of-frame check, the summary, a fresh sample on every time
// on foot, and three commit faults turning it off.
// `--exe <path>` (by hand, not the gate): the search over a real EliteDangerous64.exe, its line printed.
#include "../../src/d3d11/on_foot_markers.h"
#include "../../src/d3d11/on_foot_markers_core.h"
#include <windows.h>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace edvr {
void breadcrumb(const char*) {}  // production guard.cpp's crash-channel dependency, as the other rigs stub it
bool journalOnFootKnown() { return false; }  // journal_watch.cpp's, for the frame boundary the rig never calls
bool journalOnFoot() { return false; }
bool journalMainMenu() { return false; }
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
    alignas(16) uint8_t node[0x2A00] = {};  // *(mgr+0x58): the pose writer's prologue tests a byte of it
    alignas(16) uint8_t pipeline[0x1200] = {};
    alignas(16) uint8_t frame[0x2D00] = {};
    alignas(16) uint8_t passes[0x100] = {};
    alignas(16) uint8_t pass[2][0x320] = {};
    alignas(16) uint8_t holder[2][0x40] = {};
    alignas(16) uint8_t camera[2][0x110] = {};
    alignas(16) uint8_t cam[2][0x400] = {};
    alignas(16) uint8_t other[0x400] = {};

    void build() {
        put(mgr, 0x58, addr(node));
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

// ---- a synthetic image for the search ------------------------------------------------------------------------------------------

// A pattern's bytes at `at`, `fill` where it allows any.
void putSig(uint8_t* image, uint32_t at, const ofm::Sig& s, uint8_t fill = 0x11) {
    for (size_t i = 0; i < s.n; ++i) image[at + i] = s.bytes[i] == ofm::kAny ? fill : static_cast<uint8_t>(s.bytes[i]);
}
// call rel32 at `at` to `target`.
void putCall(uint8_t* image, uint32_t at, uint32_t target) {
    image[at] = 0xE8;
    put<int32_t>(image, at + 1, static_cast<int32_t>(static_cast<int64_t>(target) - (at + 5)));
}

// The layout both parts use: the executable section [0x1000, 0x2000), the pose writer at 0x1000 with the chain at +0x47 and its calls
// of the eye writer (0x1500) at +0x100 and of the commit (0x1800) at +0x120; a data section at 0x2000 with a copy of the pose writer's
// pattern the search must not see.
constexpr uint32_t kPoseRva = 0x1000, kEyesRva = 0x1500, kCommitRva = 0x1800, kChainAt = 0x47, kCallEyesAt = 0x100, kCallCommitAt = 0x120;
constexpr uint32_t kImageBytes = 0x3000;
const ofm::Section kSections[2] = {{0x1000, 0x1000, true}, {0x2000, 0x1000, false}};

void buildImage(std::vector<uint8_t>& image) {
    image.assign(kImageBytes, 0);
    putSig(image.data(), kPoseRva, ofm::kPointSig[ofm::kPointPose]);
    putSig(image.data(), kPoseRva + kChainAt, ofm::kChainSig);
    putCall(image.data(), kPoseRva + kCallEyesAt, kEyesRva);
    putCall(image.data(), kPoseRva + kCallCommitAt, kCommitRva);
    putSig(image.data(), kEyesRva, ofm::kPointSig[ofm::kPointEyes]);
    putSig(image.data(), kCommitRva, ofm::kPointSig[ofm::kPointCommit]);
    putSig(image.data(), 0x2100, ofm::kPointSig[ofm::kPointPose]);
}

// ---- Part A: the pure core ------------------------------------------------------------------------------------------------------

void partASearch() {
    std::vector<uint8_t> img;
    buildImage(img);
    ofm::Found f = ofm::findPoints(img.data(), kSections, 2);
    check(f.result == ofm::kFound && f.rva[ofm::kPointPose] == kPoseRva && f.rva[ofm::kPointEyes] == kEyesRva &&
              f.rva[ofm::kPointCommit] == kCommitRva && f.chainRva == kPoseRva + kChainAt && f.matches[ofm::kPointPose] == 1,
          "search: all three found, the copy in the data section not counted");

    // Other bytes where the patterns allow any (another stack frame, another cookie): still found.
    buildImage(img);
    putSig(img.data(), kPoseRva, ofm::kPointSig[ofm::kPointPose], 0x98);
    putSig(img.data(), kEyesRva, ofm::kPointSig[ofm::kPointEyes], 0x7C);
    putSig(img.data(), kCommitRva, ofm::kPointSig[ofm::kPointCommit], 0x30);
    check(ofm::findPoints(img.data(), kSections, 2).result == ofm::kFound, "search: masked bytes may differ");

    buildImage(img);
    img[kEyesRva + 1] = 0x57;
    f = ofm::findPoints(img.data(), kSections, 2);
    check(f.result == ofm::kFindNone && f.point == ofm::kPointEyes, "search: the eye writer's entry missing");

    buildImage(img);
    putSig(img.data(), 0x1C00, ofm::kPointSig[ofm::kPointCommit]);
    f = ofm::findPoints(img.data(), kSections, 2);
    check(f.result == ofm::kFindMany && f.point == ofm::kPointCommit && f.matches[ofm::kPointCommit] == 2, "search: the commit's entry twice");

    buildImage(img);
    img[kPoseRva + kChainAt + 3] = 0xF8;  // +0x7F8, not the pipeline's +0x7F0
    check(ofm::findPoints(img.data(), kSections, 2).result == ofm::kFindNoChain, "search: no chain");

    buildImage(img);
    putSig(img.data(), kPoseRva + kChainAt, ofm::kChainSig);
    std::memset(img.data() + kPoseRva + kChainAt, 0, ofm::kChainSig.n);
    putSig(img.data(), kPoseRva + ofm::kChainWindow, ofm::kChainSig);
    check(ofm::findPoints(img.data(), kSections, 2).result == ofm::kFindNoChain, "search: the chain past its window");

    buildImage(img);
    putCall(img.data(), kPoseRva + kCallEyesAt, kEyesRva + 4);
    check(ofm::findPoints(img.data(), kSections, 2).result == ofm::kFindNoCallEyes, "search: no call of the eye writer");

    buildImage(img);
    putCall(img.data(), kPoseRva + kCallCommitAt, kCommitRva + 4);
    check(ofm::findPoints(img.data(), kSections, 2).result == ofm::kFindNoCallCommit, "search: no call of the commit");

    buildImage(img);
    std::memset(img.data() + kPoseRva + kCallCommitAt, 0, 5);
    putCall(img.data(), kPoseRva + ofm::kCallWindow - 4, kCommitRva);  // its last byte past the window
    check(ofm::findPoints(img.data(), kSections, 2).result == ofm::kFindNoCallCommit, "search: a call past the window");
    putCall(img.data(), kPoseRva + ofm::kCallWindow - 5, kCommitRva);
    check(ofm::findPoints(img.data(), kSections, 2).result == ofm::kFound, "search: a call at the window's end");

    // The sections from PE headers: two, the second clamped to SizeOfImage.
    buildImage(img);
    const uint32_t pe = 0x80, opt = 0xF0, table = pe + 24 + opt;
    put<uint32_t>(img.data(), 0x3C, pe);
    std::memcpy(img.data() + pe, "PE\0\0", 4);
    put<uint16_t>(img.data(), pe + 6, 2);
    put<uint16_t>(img.data(), pe + 20, static_cast<uint16_t>(opt));
    put<uint32_t>(img.data(), pe + 24 + 56, kImageBytes);
    put<uint32_t>(img.data(), table + 8, 0x1000);
    put<uint32_t>(img.data(), table + 12, 0x1000);
    put<uint32_t>(img.data(), table + 36, 0x60000020u);
    put<uint32_t>(img.data(), table + 40 + 8, 0x5000);
    put<uint32_t>(img.data(), table + 40 + 12, 0x2000);
    put<uint32_t>(img.data(), table + 40 + 36, 0x40000040u);
    ofm::Section s[ofm::kMaxSections];
    const int n = ofm::readSections(img.data(), s, ofm::kMaxSections);
    check(n == 2 && s[0].rva == 0x1000 && s[0].size == 0x1000 && s[0].execute && s[1].rva == 0x2000 && s[1].size == 0x1000 && !s[1].execute,
          "sections: from the headers, clamped to the image");
    check(ofm::findPoints(img.data(), s, n).result == ofm::kFound, "sections: the search over them");
    check(ofm::readSections(img.data(), s, 1) == -1, "sections: more than fit refused");
    img[pe] = 'X';
    check(ofm::readSections(img.data(), s, ofm::kMaxSections) == -1, "sections: no PE signature refused");

    // The lines.
    char line[900];
    buildImage(img);
    ofm::formatFound(ofm::findPoints(img.data(), kSections, 2), 42, line, sizeof(line));
    check(std::strstr(line, "on-foot markers: found the pose writer at EliteDangerous64.exe+0x1000, the eye pose writer at +0x1500 and the commit "
                            "at +0x1800 by their code (each pattern once; the pose writer reads the cameras' chain at +0x1047 and calls both; 42 ms)") !=
              nullptr,
          "line: found");
    ofm::Found bad;
    bad.result = ofm::kFindNone;
    bad.point = ofm::kPointEyes;
    ofm::formatFound(bad, 5, line, sizeof(line));
    check(std::strstr(line, "NOT installed: the eye pose writer's pattern is not in the game's code; nothing will be patched") != nullptr &&
              std::strstr(line, "(5 ms)"),
          "line: not found");
    bad.result = ofm::kFindMany;
    bad.point = ofm::kPointCommit;
    bad.matches[ofm::kPointCommit] = 3;
    ofm::formatFound(bad, 5, line, sizeof(line));
    check(std::strstr(line, "the commit's pattern is in the game's code 3 times, not once") != nullptr, "line: many");
    bad.result = ofm::kFindNoChain;
    bad.rva[ofm::kPointPose] = 0x287D660;
    ofm::formatFound(bad, 5, line, sizeof(line));
    check(std::strstr(line, "the pose writer at EliteDangerous64.exe+0x287D660 does not read the cameras' chain") != nullptr &&
              std::strstr(line, "first 0x100 bytes"),
          "line: no chain");
    bad.result = ofm::kFindNoCallCommit;
    bad.rva[ofm::kPointCommit] = 0x2874820;
    ofm::formatFound(bad, 5, line, sizeof(line));
    check(std::strstr(line, "does not call the commit at +0x2874820 in its first 0x400 bytes") != nullptr, "line: no call");
    ofm::formatFound(ofm::Found{}, 0, line, sizeof(line));
    check(std::strstr(line, "the game's PE headers could not be read") != nullptr, "line: no headers");
}

void partA() {
    // The decision.
    check(!ofm::decide(false, 1).on && ofm::decide(false, 1).why == ofm::kWhyNative, "decide: native_vr_on_foot off");
    check(!ofm::decide(true, 0).on && ofm::decide(true, 0).why == ofm::kWhyShip, "decide: the ship");
    check(!ofm::decide(true, -1).on && ofm::decide(true, -1).why == ofm::kWhyJournal, "decide: journal unknown");
    check(ofm::decide(true, 1).on && ofm::decide(true, 1).why == ofm::kWhyOn, "decide: on foot");
    check(!ofm::decide(true, 1, true).on && ofm::decide(true, 1, true).why == ofm::kWhyMainMenu,
          "decide: the main menu, Status.json still on foot (.54): off");
    check(ofm::decide(false, 1, true).why == ofm::kWhyNative, "decide: native_vr_on_foot off outranks the main menu");

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
        ofm::correct(in, out);
        keepsGap = keepsGap && within(ofm::eyeGap(out[0], out[1]), ofm::eyeGap(in[0], in[1]), 1e-4f);
        identity = identity && ofm::identityRows(out[0]) && ofm::identityRows(out[1]);
        wKept = wKept && out[0][15] == in[0][15] && out[1][15] == in[1][15];
        for (int i = 0; i < 2; ++i)
            for (int c = 0; c < 3; ++c) {
                if (!t) first[i][c] = out[i][12 + c];
                still = still && within(out[i][12 + c], first[i][c], 1e-3f);
            }
        ofm::correct(out, again);
        for (int i = 0; i < 2; ++i)
            for (int j = 0; j < 16; ++j) fixed = fixed && within(again[i][j], out[i][j], 1e-6f);
    }
    check(identity && wKept, "correct: rows the identity, the fourth number kept");
    check(keepsGap, "correct: the eyes' distance kept");
    check(still, "correct: a +/-60 degree turn and a 20 degree tilt do not move the positions (1 mm)");
    check(within(first[0][0], -0.032f, 1e-5f) && within(first[1][0], 0.032f, 1e-5f) && within(first[0][1], 0.f, 1e-5f) && within(first[0][2], 0.f, 1e-5f),
          "correct: each eye's offset from the point between them, in the head's axes");
    check(fixed, "correct: a second step changes nothing");
    check(!ofm::identityRows(in[0]), "identity: the game's turned head is not");

    // The search.
    partASearch();

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
    check(t.step(ofm::decide(true, -1)) && !t.step(ofm::decide(true, -1)), "toggle: the first, not a repeat");
    check(t.step(ofm::decide(true, 1)) && !t.step(ofm::decide(true, 1)) && t.step(ofm::decide(true, 0)) && t.step(ofm::decide(false, 0)),
          "toggle: on, off, another reason");

    // The sample box: a sample left from an earlier time on is dropped by reset; the next is the new one.
    {
        ofm::SampleBox box;
        ofm::Sample a, b, got;
        a.game[0][0] = 1.f;
        b.game[0][0] = 2.f;
        box.want();
        box.put(a.game, a.ours);
        box.want();  // no-op: one is ready
        box.put(b.game, b.ours);
        check(box.take(&got) && got.game[0][0] == 1.f && !box.take(&got), "sample: the first put, taken once");
        box.want();
        box.put(a.game, a.ours);
        box.reset();
        check(!box.take(&got), "sample: reset drops a ready one");
        box.want();
        check(box.wanted(), "sample: wanted again after reset");
        box.put(b.game, b.ours);
        check(box.take(&got) && got.game[0][0] == 2.f, "sample: the new one after reset");
        box.want();
        box.reset();
        check(box.wanted(), "sample: reset leaves a wanted one wanted");
    }

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
    ofm::formatToggle(ofm::decide(true, 1), line, sizeof(line));
    check(std::string(line) == "on-foot markers: on (on foot)", "line: on");
    ofm::formatToggle(ofm::decide(true, 0), line, sizeof(line));
    check(std::string(line) == "on-foot markers: off (the ship)", "line: off in the ship");
    ofm::formatToggle(ofm::decide(true, -1), line, sizeof(line));
    check(std::string(line) == "on-foot markers: off (journal unknown)", "line: off, journal unknown");
    ofm::formatToggle(ofm::decide(false, 1), line, sizeof(line));
    check(std::string(line) == "on-foot markers: off (native_vr_on_foot is off)", "line: off, native_vr_on_foot off");
    ofm::formatToggle(ofm::decide(true, 1, true), line, sizeof(line));
    check(std::string(line) == "on-foot markers: off (main menu (journal Music: MainMenu): the game places its menu itself)",
          "line: off, the main menu");
    ofm::formatHooked(0x287D660, 7, 0x28457F0, 12, line, sizeof(line));
    check(std::strstr(line, "hooked the pose writer (+0x287D660, 7 bytes moved) and the eye pose writer (+0x28457F0, 12 moved)") != nullptr,
          "line: hooked");
    ofm::formatNotHooked(ofm::kPointEyes, "CodeHook refused it (its own line above says why)", line, sizeof(line));
    check(std::strstr(line, "NOT installed: the eye pose writer: CodeHook refused it") != nullptr, "line: not hooked");
    ofm::formatDisabled(3, line, sizeof(line));
    check(std::strstr(line, "the commit faulted 3 times: the correction is off until the game restarts") != nullptr, "line: disabled");

    ofm::Counts none;
    ofm::formatSummary(30, none, ofm::Check{}, nullptr, line, sizeof(line));
    check(std::strstr(line, "on-foot markers: 30 s: 0 writer calls (pose 0, eyes 0): the game reached no hooked writer") != nullptr &&
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
    ofm::correct(sample.game, sample.ours);
    ofm::formatSummary(30, some, ck, &sample, line, sizeof(line));
    check(std::strstr(line, "on-foot markers: 30 s: writer calls pose 2160, eyes n=1 0, eyes other n 4; 2164 step(s), 2160 corrected, skipped: 3 "
                            "camera mismatch, 1 commit fault") != nullptr,
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

// The function at `at`: `code` (the game's prologue shape, then its own undoing), then mov rax, imm64; jmp rax into the stand-in with
// the arguments and the return address as the caller left them.
void writeFunction(uint8_t* at, const std::vector<uint8_t>& code, const void* standIn) {
    std::memcpy(at, code.data(), code.size());
    size_t n = code.size();
    at[n++] = 0x48;
    at[n++] = 0xB8;
    const uint64_t target = addr(standIn);
    std::memcpy(at + n, &target, 8);
    n += 8;
    at[n++] = 0xFF;
    at[n++] = 0xE0;
}
void putDisp(std::vector<uint8_t>& code, size_t at, int32_t v) { std::memcpy(code.data() + at, &v, 4); }

void partB() {
    auto* image = static_cast<uint8_t*>(VirtualAlloc(nullptr, kImageBytes, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE));
    if (!image) {
        check(false, "glue: an executable synthetic image");
        return;
    }
    std::vector<uint8_t> layout;
    buildImage(layout);
    std::memcpy(image, layout.data(), layout.size());
    uint8_t* poseAt = image + kPoseRva;
    uint8_t* eyesAt = image + kEyesRva;
    uint8_t* commitAt = image + kCommitRva;
    const uint32_t kRetRva = 0x1C00, kCookieRva = 0x1C10;
    image[kRetRva] = 0xC3;
    put<uint64_t>(image, kCookieRva, 0x2B992DDFA232ull);

    // The pose writer, 334473's shape: mov rax,rsp; mov [rax+10h],rdx; push rbx; push rsi; sub rsp,68h; mov rbx,[rcx+58h]; mov rsi,rcx;
    // test byte [rbx+29A0h],1; je +0 -- undone: add rsp,68h; pop rsi; pop rbx. The chain and the calls stay where buildImage put them.
    const std::vector<uint8_t> pose = {0x48, 0x8B, 0xC4, 0x48, 0x89, 0x50, 0x10, 0x53, 0x56, 0x48, 0x83, 0xEC, 0x68, 0x48, 0x8B, 0x59, 0x58,
                                       0x48, 0x8B, 0xF1, 0xF6, 0x83, 0xA0, 0x29, 0x00, 0x00, 0x01, 0x0F, 0x84, 0x00, 0x00, 0x00, 0x00,
                                       0x48, 0x83, 0xC4, 0x68, 0x5E, 0x5B};
    // The eye writer: push rbp; push rbx; push rsi; lea rbp,[rsp-80h]; mov eax,100h; call (a ret); sub rsp,rax; mov rax,[cookie];
    // xor rax,rsp; mov [rbp+40h],rax; mov ebx,edx; mov rsi,rcx; test r8,r8 -- undone: add rsp,100h; pop rsi; pop rbx; pop rbp.
    std::vector<uint8_t> eyes = {0x40, 0x55, 0x53, 0x56, 0x48, 0x8D, 0xAC, 0x24, 0x80, 0xFF, 0xFF, 0xFF, 0xB8, 0x00, 0x01, 0x00, 0x00,
                                 0xE8, 0, 0, 0, 0, 0x48, 0x2B, 0xE0, 0x48, 0x8B, 0x05, 0, 0, 0, 0, 0x48, 0x33, 0xC4, 0x48, 0x89, 0x85,
                                 0x40, 0x00, 0x00, 0x00, 0x8B, 0xDA, 0x48, 0x8B, 0xF1, 0x4D, 0x85, 0xC0, 0x48, 0x81, 0xC4, 0x00, 0x01,
                                 0x00, 0x00, 0x5E, 0x5B, 0x5D};
    putDisp(eyes, 18, static_cast<int32_t>(kRetRva - (kEyesRva + 22)));
    putDisp(eyes, 28, static_cast<int32_t>(kCookieRva - (kEyesRva + 32)));
    // The commit: mov [rsp+10h],rbx; mov [rsp+20h],rsi; push rdi; sub rsp,270h; mov rax,[cookie]; xor rax,rsp; mov [rsp+260h],rax;
    // test byte [r8+230h],1 -- undone: add rsp,270h; pop rdi.
    std::vector<uint8_t> commit = {0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x20, 0x57, 0x48, 0x81, 0xEC, 0x70, 0x02,
                                   0x00, 0x00, 0x48, 0x8B, 0x05, 0, 0, 0, 0, 0x48, 0x33, 0xC4, 0x48, 0x89, 0x84, 0x24, 0x60, 0x02,
                                   0x00, 0x00, 0x41, 0xF6, 0x80, 0x30, 0x02, 0x00, 0x00, 0x01, 0x48, 0x81, 0xC4, 0x70, 0x02, 0x00,
                                   0x00, 0x5F};
    putDisp(commit, 21, static_cast<int32_t>(kCookieRva - (kCommitRva + 25)));
    writeFunction(poseAt, pose, reinterpret_cast<const void*>(&fakePose));
    writeFunction(eyesAt, eyes, reinterpret_cast<const void*>(&fakeEyes));
    writeFunction(commitAt, commit, reinterpret_cast<const void*>(&fakeCommit));
    FlushInstructionCache(GetCurrentProcess(), image, kImageBytes);
    g_game.build();
    const uintptr_t base = addr(image);
    g_poseFn = reinterpret_cast<FnWriter>(poseAt);
    g_eyesFn = reinterpret_cast<FnWriter>(eyesAt);
    const uint64_t mgr = addr(g_game.mgr);

    // The synthetic functions run as they are, before any hook, and their entries are what the search looks for.
    check(g_poseFn(mgr, 7, 0, 0) == 0x1234 && g_poseCalls == 1 && g_poseA2 == 7, "synthetic pose writer runs");
    check(g_eyesFn(mgr, 2, 0xAB, 0) == 0x5678 && g_eyesCalls == 1 && g_eyesA3 == 0xAB, "synthetic eye writer runs");
    check(ofm::findPoints(image, kSections, 2).result == ofm::kFound, "synthetic image: the search finds its three points");

    // The eye writer's entry not found: one line, nothing patched, and the install says nothing.
    eyesAt[1] = 0x57;
    g_lines.clear();
    check(!onFootMarkersFind(base, kSections, 2, &sink, nullptr) && g_lines.size() == 1 &&
              anyLine("NOT installed: the eye pose writer's pattern is not in the game's code") && !onFootMarkersInstall(&sink, nullptr) &&
              g_lines.size() == 1 && poseAt[0] == 0x48 && eyesAt[0] == 0x40,
          "glue: an entry not found, one line, nothing patched");
    eyesAt[1] = 0x55;
    // The pose writer not calling the commit: refused too.
    image[kPoseRva + kCallCommitAt] = 0x90;
    g_lines.clear();
    check(!onFootMarkersFind(base, kSections, 2, &sink, nullptr) && anyLine("does not call the commit at +0x1800") &&
              !onFootMarkersInstall(&sink, nullptr) && poseAt[0] == 0x48,
          "glue: a call missing, one line, nothing patched");
    image[kPoseRva + kCallCommitAt] = 0xE8;
    FlushInstructionCache(GetCurrentProcess(), image, kImageBytes);

    // Found, hooked.
    g_lines.clear();
    check(onFootMarkersFind(base, kSections, 2, &sink, nullptr) && g_lines.size() == 1 &&
              anyLine("found the pose writer at EliteDangerous64.exe+0x1000, the eye pose writer at +0x1500 and the commit at +0x1800"),
          "glue: found");
    g_lines.clear();
    check(onFootMarkersInstall(&sink, nullptr) && g_lines.size() == 1 &&
              anyLine("hooked the pose writer (+0x1000, 7 bytes moved) and the eye pose writer (+0x1500, 12 moved)") && poseAt[0] == 0xE9 &&
              eyesAt[0] == 0xE9 && commitAt[0] == 0x48,
          "glue: both writers hooked, 7 and 12 bytes moved, the commit untouched");
    if (g_failures) dumpLines();
    g_lines.clear();
    check(onFootMarkersInstall(&sink, nullptr) && onFootMarkersFind(base, kSections, 2, &sink, nullptr) && g_lines.empty(),
          "glue: a second install or search says nothing");

    // Off: forwarded and counted, nothing touched.
    onFootMarkersSetOn(false);
    g_poseCalls = 0;
    check(g_poseFn(mgr, 9, 0, 0) == 0x1234 && g_poseCalls == 1 && g_poseA2 == 9, "glue: off, the pose writer forwarded with its arguments");
    check(!ofm::identityRows(g_game.pose(0)) && g_commitCalls == 0 && onFootMarkersCounts().poseCalls == 1 && onFootMarkersCounts().steps == 0,
          "glue: off, the game's pose stays, no step, no commit");

    // On: both eyes the identity, flags |= 0xF, the commit with (cam, size, cam+0x20).
    onFootMarkersSetOn(true);
    check(g_poseFn(mgr, 7, 0, 0) == 0x1234, "glue: on, the original's result returned");
    check(ofm::identityRows(g_game.pose(0)) && ofm::identityRows(g_game.pose(1)), "glue: on, both eyes' rows the identity");
    check(within(g_game.pose(0)[12], -0.032f, 1e-4f) && within(g_game.pose(1)[12], 0.032f, 1e-4f) && within(g_game.pose(0)[13], 0.f, 1e-4f) &&
              g_game.pose(0)[15] == 1.f,
          "glue: on, each eye at its offset");
    check(get<uint32_t>(g_game.cam[0], ofm::kCamFlags) == 0x10Fu && get<uint32_t>(g_game.cam[1], ofm::kCamFlags) == 0x10Fu, "glue: on, flags |= 0xF");
    check(g_commitCalls == 2 && g_commitArgsOk && std::memcmp(g_game.cam[0] + 0x310, g_game.cam[0] + ofm::kCamPose, 64) == 0,
          "glue: on, the commit called twice with (cam, size, cam+0x20)");
    {
        const ofm::Counts c = onFootMarkersCounts();
        check(c.poseCalls == 2 && c.steps == 1 && c.corrected == 1, "glue: on, one step corrected");
    }

    // The eye writer from the pipeline frame (n = 2): counted apart, its third argument forwarded, corrected.
    check(g_eyesFn(mgr, 2, 0xCD, 0) == 0x5678 && g_eyesN == 2 && g_eyesA3 == 0xCD && g_commitCalls == 4 && onFootMarkersCounts().eyesOther == 1 &&
              onFootMarkersCounts().eyesOne == 0 && ofm::identityRows(g_game.pose(0)),
          "glue: the eye writer with n = 2, forwarded and corrected");

    // Nested: the pose writer calling the eye writer (n = 1) gives one step.
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

    // The frame step. 1000 s on: the journal does not know yet (its search finds the hooks in and says nothing).
    g_lines.clear();
    onFootMarkersFrameStep(1000000, true, -1, base, kSections, 2, &sink, nullptr);
    check(anyLine("on-foot markers: off (journal unknown)") && g_lines.size() == 1, "step: off, journal unknown, one line");
    g_poseFn(mgr, 7, 0, 0);
    check(!ofm::identityRows(g_game.pose(0)), "step: journal unknown, hands off");
    g_lines.clear();
    onFootMarkersFrameStep(1000100, true, -1, base, kSections, 2, &sink, nullptr);
    check(g_lines.empty(), "step: nothing to say while nothing changes");

    // On foot: on; a corrected frame checks ours, a third writer after it the game's. The game's are three: the frame that turns it on
    // still sees the view-id test's uncorrected pose, and the summary's own frame sees the third writer's.
    onFootMarkersFrameStep(1001000, true, 1, base, kSections, 2, &sink, nullptr);
    check(anyLine("on-foot markers: on (on foot)") && g_lines.size() == 1, "step: on foot, on");
    g_poseFn(mgr, 7, 0, 0);
    onFootMarkersFrameStep(1001010, true, 1, base, kSections, 2, &sink, nullptr);
    g_game.writeHead(10.f, 0.f);  // someone else, after the correction
    onFootMarkersFrameStep(1001020, true, 1, base, kSections, 2, &sink, nullptr);
    g_lines.clear();
    onFootMarkersFrameStep(1031000, true, 1, base, kSections, 2, &sink, nullptr);
    check(anyLine("on-foot markers: 30 s: writer calls pose 1, eyes n=1 0, eyes other n 0; 1 step(s), 1 corrected, none skipped") &&
              anyLine("frame check ours 1 / game's 3") && anyLine("yaw 40.0 pitch 5.0 -> ours rows identity, left (-0.0320,0.0000,0.0000) right (0.0320,0.0000,0.0000)") &&
              anyLine("eye gap game 0.0640 ours 0.0640"),
          "step: the summary, its check and its sample");
    if (g_failures) dumpLines();
    g_lines.clear();
    onFootMarkersFrameStep(1061000, true, 1, base, kSections, 2, &sink, nullptr);
    check(anyLine("on-foot markers: 30 s: 0 writer calls") && anyLine("no sample"), "step: a window with no writer call says 0");

    // Without native VR on foot; then the ship; no summary while off.
    g_lines.clear();
    onFootMarkersFrameStep(1065000, false, 1, base, kSections, 2, &sink, nullptr);
    check(anyLine("on-foot markers: off (native_vr_on_foot is off)") && g_lines.size() == 1, "step: native_vr_on_foot off");
    g_poseFn(mgr, 7, 0, 0);
    check(!ofm::identityRows(g_game.pose(0)), "step: native_vr_on_foot off, the game's pose stays");
    // The main menu after gameplay with Status.json still on foot: off, the game's pose stays; LoadGame on foot: on again.
    g_lines.clear();
    onFootMarkersFrameStep(1065100, true, 1, base, kSections, 2, &sink, nullptr);
    check(anyLine("on-foot markers: on (on foot)"), "step: on foot again");
    g_lines.clear();
    onFootMarkersFrameStep(1065200, true, 1, base, kSections, 2, &sink, nullptr, true);
    check(anyLine("on-foot markers: off (main menu (journal Music: MainMenu)") && g_lines.size() == 1, "step: the main menu");
    g_poseFn(mgr, 7, 0, 0);
    check(!ofm::identityRows(g_game.pose(0)), "step: the main menu, the game's pose stays");
    g_lines.clear();
    onFootMarkersFrameStep(1065300, true, 1, base, kSections, 2, &sink, nullptr);
    check(anyLine("on-foot markers: on (on foot)"), "step: LoadGame on foot, on again");
    g_lines.clear();
    onFootMarkersFrameStep(1066000, true, 0, base, kSections, 2, &sink, nullptr);
    check(anyLine("on-foot markers: off (the ship)"), "step: the ship");
    g_lines.clear();
    onFootMarkersFrameStep(1200000, true, 0, base, kSections, 2, &sink, nullptr);
    check(g_lines.empty(), "step: off, no summary");

    // A fresh sample every time on foot: one taken on foot at yaw 40 and left when the ship came before the summary does not reach the
    // next time's summary; that one shows the yaw of its own time, -25.
    onFootMarkersFrameStep(1300000, true, 1, base, kSections, 2, &sink, nullptr);
    g_yaw = 40.f;
    g_poseFn(mgr, 7, 0, 0);
    onFootMarkersFrameStep(1301000, true, 0, base, kSections, 2, &sink, nullptr);
    onFootMarkersFrameStep(1302000, true, 1, base, kSections, 2, &sink, nullptr);
    g_yaw = -25.f;
    g_poseFn(mgr, 7, 0, 0);
    g_lines.clear();
    onFootMarkersFrameStep(1332000, true, 1, base, kSections, 2, &sink, nullptr);
    check(anyLine("sample: game view 0 yaw -25.0 pitch 5.0") && !anyLine("yaw 40.0"), "step: the summary's sample is this time on foot's");
    if (g_failures) dumpLines();
    g_yaw = 40.f;

    // Three commit faults: the correction off for good, one line, the writers only forwarded.
    g_commitFault = true;
    for (int i = 0; i < 3; ++i) g_poseFn(mgr, 7, 0, 0);
    g_commitFault = false;
    check(onFootMarkersDisabled() && onFootMarkersCounts().commitFaults == 3, "glue: three commit faults, disabled");
    g_lines.clear();
    onFootMarkersFrameStep(1333000, true, 1, base, kSections, 2, &sink, nullptr);
    check(anyLine("the commit faulted 3 times: the correction is off until the game restarts") && anyLine("off (the commit faulted 3 times)"),
          "step: disabled, said");
    const int commits = g_commitCalls;
    check(g_poseFn(mgr, 7, 0, 0) == 0x1234 && g_commitCalls == commits && !ofm::identityRows(g_game.pose(0)), "glue: disabled, only forwarded");
}

// The search over a real EliteDangerous64.exe: its sections mapped as the loader would, then the line the game would log.
int searchExe(const char* path) {
    FILE* f = std::fopen(path, "rb");
    if (!f) {
        std::printf("cannot open %s\n", path);
        return 2;
    }
    std::vector<uint8_t> raw;
    uint8_t chunk[1 << 16];
    for (size_t n; (n = std::fread(chunk, 1, sizeof(chunk), f)) > 0;) raw.insert(raw.end(), chunk, chunk + n);
    std::fclose(f);
    if (raw.size() < 0x400) {
        std::printf("%s: too short for a PE\n", path);
        return 2;
    }
    const uint32_t pe = get<uint32_t>(raw.data(), 0x3C);
    if (pe > 0x1000 || pe + 24 + 60 > raw.size()) {
        std::printf("%s: no PE header\n", path);
        return 2;
    }
    const uint32_t imageSize = get<uint32_t>(raw.data(), pe + 24 + 56), headers = get<uint32_t>(raw.data(), pe + 24 + 60);
    const uint16_t count = get<uint16_t>(raw.data(), pe + 6), optional = get<uint16_t>(raw.data(), pe + 20);
    std::vector<uint8_t> image(imageSize, 0);
    std::memcpy(image.data(), raw.data(), headers < raw.size() && headers < imageSize ? headers : 0x400);
    for (uint16_t i = 0; i < count; ++i) {
        const size_t h = pe + 24 + optional + 40u * i;
        const uint32_t vsize = get<uint32_t>(raw.data(), h + 8), va = get<uint32_t>(raw.data(), h + 12), rsize = get<uint32_t>(raw.data(), h + 16),
                       rptr = get<uint32_t>(raw.data(), h + 20);
        uint32_t n = vsize < rsize ? vsize : rsize;
        if (va >= imageSize || rptr >= raw.size()) continue;
        if (n > imageSize - va) n = imageSize - va;
        if (n > raw.size() - rptr) n = static_cast<uint32_t>(raw.size() - rptr);
        std::memcpy(image.data() + va, raw.data() + rptr, n);
    }
    ofm::Section sections[ofm::kMaxSections];
    const int n = ofm::readSections(image.data(), sections, ofm::kMaxSections);
    if (n < 0) {
        std::printf("%s: its section headers make no sense\n", path);
        return 2;
    }
    const auto t0 = std::chrono::steady_clock::now();
    const ofm::Found found = ofm::findPoints(image.data(), sections, n);
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
    char line[900];
    ofm::formatFound(found, static_cast<uint64_t>(ms), line, sizeof(line));
    std::printf("PE stamp 0x%08X\n%s\n", get<uint32_t>(raw.data(), pe + 8), line);
    return found.result == ofm::kFound ? 0 : 1;
}
}  // namespace

int main(int argc, char** argv) {
    if (argc == 3 && std::strcmp(argv[1], "--exe") == 0) return searchExe(argv[2]);
    if (argc < 2 || std::strcmp(argv[1], "--self-test") != 0) {
        std::printf("usage: on_foot_markers_test --self-test | --exe <EliteDangerous64.exe>\n");
        return 2;
    }
    partA();
    partB();
    std::printf("%s: on_foot_markers_test, %d checks, %d failed\n", g_failures ? "FAIL" : "PASS", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
