// Build gate for the EDVRonFOOT on-foot HUD probe (src\d3d11\on_foot_hud_probe.h, experimental.on_foot_hud_probe).
//
// Part A drives on_foot_hud_probe_core.h on a fake game in this rig's own memory -- a list-range node, its list set, draw context,
// camera, render view and lists 36-46: which nodes are ours, the camera's mask passing a list or not, models in the camera's own
// pocket, in another, nowhere, a printable and a garbage view name, an unreadable field, the change filter (a repeat is not written,
// a change inside a second waits for it), the 30-second summary, a full ring counting its losses and every line's text.
// Part B compiles the REAL glue (on_foot_hud_probe.cpp) with the real CodeHook and hooks a synthetic function that begins with the
// game's prologue: another build is refused with one line; wrong bytes are refused with one line and left untouched; then it is
// hooked; the node reaches the original unchanged, the snapshot is taken after the original (which hands models in during the call);
// with recording off the hook only forwards; a node of another vtable is not read; an unreadable field is survived with one line.
#include "../../src/d3d11/on_foot_hud_probe.h"
#include "../../src/d3d11/on_foot_hud_probe_core.h"
#include <windows.h>
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

bool plainRead(void*, uint64_t addr, void* out, size_t n) {
    std::memcpy(out, reinterpret_cast<const void*>(addr), n);
    return true;
}

template <typename T>
void put(uint8_t* at, uintptr_t offset, T value) {
    std::memcpy(at + offset, &value, sizeof(T));
}
uint64_t addr(const void* p) { return reinterpret_cast<uint64_t>(p); }

// The fake game: one node of lists 36-39 (or 40-46), as the PostGUI pass makes them.
struct FakeGame {
    uint64_t vtable[4] = {};
    alignas(16) uint8_t node[0x200] = {};
    alignas(16) uint8_t set[0x100] = {};
    uint64_t array[50] = {};
    alignas(16) uint8_t lists[11][0x240] = {};
    alignas(16) uint8_t ctx[0x300] = {};
    alignas(16) uint8_t cam[0x900] = {};
    alignas(16) uint8_t view[0x40] = {};
    alignas(16) uint8_t name[0x40] = {};

    uintptr_t base() const { return addr(vtable) - ohp::kNodeVtableRva; }
    uint8_t* list(uint32_t index) { return lists[index - 36]; }
    void setName(const char* text) {
        const uint64_t n = std::strlen(text);
        put(name, 0, n);
        std::memcpy(name + ohp::kNameChars, text, n);
    }
    void setFirst(uint32_t first, uint32_t count) {
        put<uint64_t>(node, ohp::kNodeFirst, first);
        put<uint32_t>(node, ohp::kNodeCount, count);
    }
    void build() {
        put(node, 0, addr(vtable));
        setFirst(36, 4);
        put(node, ohp::kNodeSet, addr(set));
        put(node, ohp::kNodeView, addr(view));
        put<uint32_t>(set, ohp::kSetLists, 50);
        put(set, ohp::kSetContext, addr(ctx));
        put(set, ohp::kSetArray, addr(array));
        for (uint32_t i = 36; i <= 46; ++i) {
            array[i] = addr(list(i));
            put<uint64_t>(list(i), ohp::kListMask, 0x4);  // a list type bit
        }
        put(ctx, ohp::kCtxCamera, addr(cam));
        put<uint64_t>(ctx, ohp::kCtxMask, 0x4);
        put<uint32_t>(cam, ohp::kCamFlags18, 0x10000);
        put<uint32_t>(cam, ohp::kCamFlags250, 0xF);
        put<uint32_t>(cam, ohp::kCamPocket, 3);
        put(view, ohp::kViewCamera, addr(cam));
        put(view, ohp::kViewName, addr(name));
        put<uint64_t>(view, ohp::kViewBit, 0x2);
        put<int32_t>(view, ohp::kViewBitIndex, 1);
        setName("PostGUIView_Primary");
    }
    void model(uint32_t list, int pocket, uint64_t head) { put(this->list(list), ohp::kListPockets + 8 * pocket, head); }
};

// ---- Part A: the pure half --------------------------------------------------------------------------------------------------------

void partA() {
    static FakeGame g;
    g.build();
    const uintptr_t base = g.base();
    check(ohp::isOurs(addr(g.vtable), 36, base) && ohp::isOurs(addr(g.vtable), 40, base), "core: lists 36 and 40 of the vtable are ours");
    check(!ohp::isOurs(addr(g.vtable), 0, base) && !ohp::isOurs(addr(g.vtable) + 8, 36, base), "core: another list or vtable is not");

    g.model(36, 3, 0x1234);  // the camera's own pocket
    g.model(37, 5, 0x1234);  // another camera's
    g.model(37, 9, 0x1234);
    put<uint64_t>(g.list(38), ohp::kListMask, 0x8);  // a mask the camera does not pass
    ohp::Snapshot s;
    check(ohp::capture(addr(g.node), &plainRead, nullptr, &s), "core: the node is read");
    check(s.first == 36 && s.count == 4 && s.pocket == 3 && s.cam == addr(g.cam) && s.viewCam == addr(g.cam) && s.viewBitIndex == 1 &&
              std::strcmp(s.name, "PostGUIView_Primary") == 0,
          "core: the node's lists, the camera, its pocket and the view");
    check(s.list[0].pockets == (1ull << 3) && s.list[1].pockets == ((1ull << 5) | (1ull << 9)) && s.list[2].pockets == 0 &&
              s.list[2].mask == 0x8,
          "core: the pockets with models, per list");
    char line[1600];
    check(ohp::formatSnapshot(s, line, sizeof(line)) > 0, "core: the line is made");
    const std::string text = line;
    check(text.find("view \"PostGUIView_Primary\" bit 1 (0x2) lists 36-39 on-foot=?") != std::string::npos &&
              text.find("(is the view's") != std::string::npos && text.find("pocket 3") != std::string::npos,
          "core text: the view, the lists, the camera");
    check(text.find("| 36 pass=yes here=yes elsewhere={} mask=0x4") != std::string::npos &&
              text.find("| 37 pass=yes here=no elsewhere={5,9} mask=0x4") != std::string::npos &&
              text.find("| 38 pass=no here=no elsewhere={} mask=0x8") != std::string::npos &&
              text.find("| 39 pass=yes here=no elsewhere={}") != std::string::npos,
          "core text: each list -- passed, here, elsewhere");
    for (int p = 10; p < 20; ++p) g.model(39, p, 1);
    ohp::capture(addr(g.node), &plainRead, nullptr, &s);
    ohp::formatSnapshot(s, line, sizeof(line));
    check(std::strstr(line, "| 39 pass=yes here=no elsewhere={10,11,12,13,14,15,16,17,+2}") != nullptr,
          "core text: more than eight other pockets are counted");

    g.setName("Post\x01View");
    ohp::capture(addr(g.node), &plainRead, nullptr, &s);
    check(std::strcmp(s.name, "?") == 0, "core: a name with an unprintable character is '?'");
    g.setName("PostGUIView_Primary");
    put(g.view, ohp::kViewName, uint64_t(0));
    ohp::capture(addr(g.node), &plainRead, nullptr, &s);
    check(std::strcmp(s.name, "?") == 0, "core: no name is '?'");
    put(g.view, ohp::kViewName, addr(g.name));

    put(g.ctx, ohp::kCtxCamera, uint64_t(0));  // no camera: its fields read as zeros, the pocket unknown
    check(ohp::capture(addr(g.node), &plainRead, nullptr, &s) && s.pocket == UINT32_MAX, "core: no camera, pocket unknown");
    ohp::formatSnapshot(s, line, sizeof(line));
    check(std::strstr(line, "pocket -1") && std::strstr(line, "| 36 pass=yes here=? elsewhere={3}"), "core text: no camera's pocket");
    put(g.ctx, ohp::kCtxCamera, addr(g.cam));
    put(g.node, ohp::kNodeSet, uint64_t(0x10));  // a pointer no game would have
    check(!ohp::capture(addr(g.node), &plainRead, nullptr, &s), "core: an unreadable list set drops the snapshot");
    put(g.node, ohp::kNodeSet, addr(g.set));
    put<uint32_t>(g.set, ohp::kSetLists, 38);  // fewer lists than the node names
    check(ohp::capture(addr(g.node), &plainRead, nullptr, &s) && s.count == 2, "core: only the lists the set has");
    put<uint32_t>(g.set, ohp::kSetLists, 50);

    // The change filter.
    ohp::ChangeFilter filter;
    ohp::Snapshot w;
    ohp::capture(addr(g.node), &plainRead, nullptr, &s);
    check(filter.admit(s, 1000, &w) && w.node == s.node, "filter: a node's first snapshot is written");
    check(!filter.admit(s, 1010, &w) && filter.repeats() == 1, "filter: the same again is a repeat");
    ohp::Snapshot moved = s;
    moved.onFoot = 1;
    check(!filter.admit(moved, 1020, &w) && filter.held() == 1, "filter: a change inside the second waits");
    check(!filter.due(1500, &w), "filter: not before the second is over");
    check(filter.due(2000, &w) && w.onFoot == 1 && !filter.due(2000, &w), "filter: then it is written, once");
    ohp::Snapshot back = moved;
    back.onFoot = 0;
    check(!filter.admit(back, 2100, &w), "filter: another change waits");
    check(!filter.admit(moved, 2200, &w) && !filter.due(3500, &w), "filter: a change undone before its turn is not written");
    ohp::Snapshot other = s;
    other.node = 0x777;
    check(filter.admit(other, 2300, &w), "filter: another node is written at once");
    for (uint64_t n = 0; n < 20; ++n) {
        other.node = 0x1000 + n;
        filter.admit(other, 3000 + n, &w);
    }
    check(filter.nodes(3100, 1000) == ohp::ChangeFilter::kNodes, "filter: sixteen nodes kept, the oldest made room");

    // The summary.
    ohp::Summary summary;
    ohp::SummaryCounts total;
    check(summary.step(0, total, 2, line, sizeof(line)) == 0, "summary: the first call only starts the window");
    total.draws = 90000;
    total.calls = 7200;
    total.written = 3;
    total.repeats = 7197;
    check(summary.step(29999, total, 2, line, sizeof(line)) == 0, "summary: not before 30 s");
    check(summary.step(30000, total, 4, line, sizeof(line)) > 0 &&
              std::strstr(line, "on-foot hud probe: 30 s: 90000 node draw(s) in all; PostGUI: 4 node(s), 7200 call(s), 3 line(s), "
                                "7197 repeat(s), 0 change(s) kept back, 0 lost, 0 unreadable") != nullptr,
          "summary: the window's counts");
    check(summary.step(60000, total, 4, line, sizeof(line)) == 0, "summary: no draws in the window, no line");
    total.draws += 500;
    check(summary.step(90000, total, 0, line, sizeof(line)) > 0 && std::strstr(line, "500 node draw(s) in all; PostGUI: 0 node(s), 0 call(s)"),
          "summary: draws but no PostGUI node, said");

    // The ring.
    static ohp::Ring<64> ring;
    for (int i = 0; i < 70; ++i) {
        s.node = static_cast<uint64_t>(i);
        ring.push(s);
    }
    ohp::Snapshot out;
    int popped = 0;
    bool ordered = true;
    while (ring.pop(&out)) ordered = ordered && out.node == static_cast<uint64_t>(popped++);
    check(popped == 64 && ordered && ring.dropped() == 6, "ring: in order, a full ring drops and counts");

    // The other lines.
    check(ohp::formatKey(false, false, line, sizeof(line)) > 0 &&
              std::strcmp(line, "on-foot hud probe: off: nothing in the game is touched (experimental.on_foot_hud_probe = 0)") == 0,
          "text: key off at launch");
    check(ohp::formatKey(false, true, line, sizeof(line)) > 0 && std::strstr(line, "off: the hook only forwards now"), "text: key off live");
    check(ohp::formatKey(true, false, line, sizeof(line)) > 0 && std::strstr(line, "probe: on (experimental.on_foot_hud_probe = 1)"),
          "text: key on");
}

// ---- Part B: the real glue on a synthetic function -------------------------------------------------------------------------------

FakeGame g_game;
volatile uint64_t g_originalSaw = 0;
volatile int g_originalCalls = 0;
bool g_handIn = false;

// What the game's node draw does for the rig: it sees the node, and may hand models in during the call.
void fakeNodeDraw(uint8_t* node) {
    g_originalSaw = addr(node);
    ++g_originalCalls;
    if (g_handIn) g_game.model(39, 3, 0xABCD);
}

// The game's prologue in full (rex push rbx; push rdi; push r14; sub rsp,80h; mov rax,[rcx+108h]), then undone (add rsp,80h; pop
// r14; pop rdi; pop rbx), then mov rax, imm64; jmp rax into the stand-in with the node and the return address as the caller left them.
void writeFunction(uint8_t* at) {
    const uint8_t prologue[] = {0x40, 0x53, 0x57, 0x41, 0x56, 0x48, 0x81, 0xEC, 0x80, 0x00, 0x00, 0x00, 0x48, 0x8B, 0x81, 0x08, 0x01, 0x00, 0x00};
    const uint8_t undo[] = {0x48, 0x81, 0xC4, 0x80, 0x00, 0x00, 0x00, 0x41, 0x5E, 0x5F, 0x5B};
    size_t n = 0;
    std::memcpy(at + n, prologue, sizeof(prologue));
    n += sizeof(prologue);
    std::memcpy(at + n, undo, sizeof(undo));
    n += sizeof(undo);
    at[n++] = 0x48;
    at[n++] = 0xB8;
    const uint64_t target = addr(reinterpret_cast<const void*>(&fakeNodeDraw));
    std::memcpy(at + n, &target, 8);
    n += 8;
    at[n++] = 0xFF;
    at[n++] = 0xE0;
}

void partB() {
    auto* page = static_cast<uint8_t*>(VirtualAlloc(nullptr, 4096, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE));
    if (!page) {
        check(false, "glue: a code page for the synthetic function");
        return;
    }
    writeFunction(page);
    FlushInstructionCache(GetCurrentProcess(), page, 4096);
    g_game.build();
    const uintptr_t base = g_game.base();
    using Fn1 = void (*)(void*);
    auto draw = reinterpret_cast<Fn1>(page);

    // Another build: a fake image whose PE stamp is not 334473's.
    std::vector<uint8_t> pe(0x200, 0);
    put<uint32_t>(pe.data(), 0x3C, 0x80);
    put<uint32_t>(pe.data(), 0x80 + 8, 0x12345678u);
    put<uint32_t>(pe.data(), 0x80 + 0x50, 0x1000u);
    g_lines.clear();
    check(!onFootHudProbeInstall(addr(pe.data()), addr(page), true, &sink, nullptr) && g_lines.size() == 1 &&
              anyLine("NOT installed: the game is not build 334473 (PE stamp 0x12345678, image size 0x1000)") && page[0] == 0x40,
          "glue: another build, one line, nothing patched");

    page[1] = 0x54;  // not the game's prologue
    g_lines.clear();
    check(!onFootHudProbeInstall(base, addr(page), false, &sink, nullptr) && g_lines.size() == 1 &&
              anyLine("does not begin with build 334473's 16 bytes") && page[0] == 0x40 && page[1] == 0x54,
          "glue: wrong bytes, one line, untouched");
    page[1] = 0x53;
    FlushInstructionCache(GetCurrentProcess(), page, 2);
    g_lines.clear();
    check(onFootHudProbeInstall(base, addr(page), false, &sink, nullptr) && g_lines.size() == 1 &&
              anyLine("hooked node draw at EliteDangerous64.exe+0x58F0F0") && anyLine("16 bytes verified, 5 moved") && page[0] == 0xE9,
          "glue: the right bytes are hooked, five moved");
    g_lines.clear();
    check(onFootHudProbeInstall(base, addr(page), false, &sink, nullptr) && g_lines.empty(), "glue: a second install says nothing");

    // Off: forward only.
    onFootHudProbeSetRecording(false);
    draw(g_game.node);
    check(g_originalCalls == 1 && g_originalSaw == addr(g_game.node) && onFootHudProbeCalls() == 0, "glue: off, only forwarded");

    // On: the snapshot after the original, which hands a model into list 39's own pocket during the call.
    onFootHudProbeSetRecording(true);
    g_handIn = true;
    draw(g_game.node);
    g_handIn = false;
    check(g_originalCalls == 2 && g_originalSaw == addr(g_game.node) && onFootHudProbeCalls() == 1, "glue: on, forwarded and counted");
    g_lines.clear();
    onFootHudProbeDrain(100000, 1, &sink, nullptr);
    check(countLines("on-foot hud probe: node 0x") == 1 && anyLine("on-foot=yes") && anyLine("| 39 pass=yes here=yes"),
          "glue: the line, after the original's hand-in, stamped on foot");
    draw(g_game.node);
    g_lines.clear();
    onFootHudProbeDrain(100100, 1, &sink, nullptr);
    check(g_lines.empty(), "glue: the same snapshot again is not written");

    // A node of another kind: forwarded, not read, not counted.
    put(g_game.node, 0, addr(g_game.vtable) + 8);
    draw(g_game.node);
    check(g_originalCalls == 4 && onFootHudProbeCalls() == 2, "glue: another vtable, forwarded and not counted");
    put(g_game.node, 0, addr(g_game.vtable));

    // An unreadable list set: forwarded, survived, one line once.
    void* noAccess = VirtualAlloc(nullptr, 4096, MEM_RESERVE | MEM_COMMIT, PAGE_NOACCESS);
    put(g_game.node, ohp::kNodeSet, addr(noAccess));
    draw(g_game.node);
    draw(g_game.node);
    g_lines.clear();
    onFootHudProbeDrain(100200, 1, &sink, nullptr);
    check(g_originalCalls == 6 && onFootHudProbeFaults() == 2 && countLines("could not be read") == 1, "glue: unreadable, one line");
    g_lines.clear();
    onFootHudProbeDrain(100300, 1, &sink, nullptr);
    check(countLines("could not be read") == 0, "glue: the fault line is said once");
    put(g_game.node, ohp::kNodeSet, addr(g_game.set));
    if (noAccess) VirtualFree(noAccess, 0, MEM_RELEASE);

    // The summary, 30 s on.
    draw(g_game.node);
    g_lines.clear();
    onFootHudProbeDrain(130300, 0, &sink, nullptr);
    check(anyLine("on-foot hud probe: 30 s: 5 node draw(s) in all; PostGUI: 1 node(s), 4 call(s)") && anyLine("2 unreadable"),
          "glue: the summary");
}
}  // namespace

int main(int argc, char** argv) {
    if (argc < 2 || std::strcmp(argv[1], "--self-test") != 0) {
        std::printf("usage: on_foot_hud_probe_test --self-test\n");
        return 2;
    }
    partA();
    partB();
    std::printf("%s: on_foot_hud_probe_test, %d checks, %d failed\n", g_failures ? "FAIL" : "PASS", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
