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
// .15: Part A15 drives the cameras' half of the core (the angles of a camera and of the head, the yaw step across +/-180, the follow
// slopes 1, 0 and -1, too little turn, a jump left out, the pass table, every line's text with "none" for what could not be read);
// Part B15 hooks a synthetic function that begins with the PostGUI pass switch's bytes and drives the camera drain over two
// 30-second windows on passes and cameras in the rig's memory.
// .19: Part A16 drives the subscription watch's half of the core (the place, the sites by place with the camera bit, the ninth site
// counted, every line's text, the 30-second line and a quiet one, the fuse, the draw context by camera); Part A19 drives the provider
// list on fake providers (a list of three, one giving _HUD, the unsubscribed one answering 0, the context of another node and an OR
// that is not the mask dropped with their own reasons and the first pair kept once, the 65th provider only a number, more providers
// than rows, a third view, an unreadable address skipping the snapshot, a changed answer not changing the shape and a changed
// subscription or place changing it, the ring's overflow, every line's text, the summary's window); Part C arms the watch for real on
// this rig's threads (the taken slot skipped, one line per writer with its value, camera bit and place, the repeats only counted, off
// with the totals, an unaligned address refused); Part B15 sees the camera drain write the HUD node's lists and the 30-second summary
// (no provider gives 0x8 in stereo: no watch, said in one line), arm the watch on the first provider that gives it (view 0's) in
// cinema, write its 30-second line, move it to another provider when its own leaves the list for 2 s, take it off when none is left,
// arm it again, take it off with the key and trip the fuse with 100 001 real writes.
#include "../../src/d3d11/on_foot_hud_probe.h"
#include "../../src/d3d11/on_foot_hud_probe_core.h"
#include "../../src/d3d11/debug_register_core.h"
#include <windows.h>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <string>
#include <vector>

namespace edvr {
void breadcrumb(const char*) {}  // production guard.cpp's crash-channel dependency, as the other rigs stub it
bool journalOnFootKnown() { return false; }  // journal_watch.cpp's, for the frame boundary the rig never calls
bool journalOnFoot() { return false; }
bool headPose(float*) { return false; }  // frame_flag.cpp's, for the same
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
    uint8_t node[0x200] = {};
    uint8_t set[0x100] = {};
    uint64_t array[11] = {};  // the PostGUI set: its list 0 is the game's list name 36
    uint8_t lists[11][0x240] = {};
    uint8_t ctx[0x300] = {};
    uint8_t cam[0x900] = {};
    uint8_t view[0x40] = {};
    uint8_t name[0x40] = {};
    // .19: the provider list L (ctx+0x108: *L the array, u32 at L+8 the number), 80 providers of which the list holds some, three
    // vtables whose slot 0x68 holds the answering method. u64 cells: the subscription (+0x28) is watched in B15, 4-aligned.
    uint8_t provList[0x10] = {};
    uint64_t provArray[80] = {};
    uint64_t providers[80][8] = {};
    uint64_t vts[3][0x10] = {};

    uintptr_t base() const { return addr(vtable) - ohp::kNodeVtableRva; }
    uint64_t method(int k) const { return base() + 0x4E2B10 + 0x40 * k; }
    // The list holds these providers, in this order (an index of `providers`, or -1 for an empty slot).
    void setList(std::initializer_list<int> which) {
        uint32_t n = 0;
        for (int i : which) provArray[n++] = i < 0 ? 0 : addr(providers[i]);
        put(provList, 0, addr(provArray));
        put<uint32_t>(provList, ohp::kListNumber, n);
    }
    void setProvider(int i, uint64_t sub, uint64_t answer) {
        auto* p = reinterpret_cast<uint8_t*>(providers[i]);
        put(p, 0, addr(vts[i % 3]));
        vts[i % 3][ohp::kVtAnswer / 8] = method(i % 3);
        put(p, ohp::kProvSub, sub);
        put(p, ohp::kProvAnswer, answer);
    }
    uint64_t* subOf(int i) { return &providers[i][ohp::kProvSub / 8]; }
    // The frame mask the game's prepare would have made of the list now: the OR of the answers with the camera's constant lists.
    void sync() {
        uint64_t mask = 0, cam8F0 = 0;
        std::memcpy(&cam8F0, cam + ohp::kCam8F0, 8);
        mask = cam8F0;
        uint32_t n = 0;
        std::memcpy(&n, provList + ohp::kListNumber, 4);
        for (uint32_t i = 0; i < n; ++i)
            if (provArray[i]) {
                uint64_t answer = 0;
                std::memcpy(&answer, reinterpret_cast<const uint8_t*>(provArray[i]) + ohp::kProvAnswer, 8);
                mask |= answer;
            }
        put(ctx, ohp::kCtxMask, mask);
    }
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
        setFirst(0, 4);
        put(node, ohp::kNodeSet, addr(set));
        put(node, ohp::kNodeView, addr(view));
        put<uint32_t>(set, ohp::kSetLists, 11);
        put(set, ohp::kSetContext, addr(ctx));
        put(set, ohp::kSetArray, addr(array));
        for (uint32_t i = 36; i <= 46; ++i) {
            array[i - 36] = addr(list(i));
            put<uint64_t>(list(i), ohp::kListMask, 0x4);  // a list type bit
        }
        put(ctx, ohp::kCtxCamera, addr(cam));
        put<uint64_t>(ctx, ohp::kCtxMask, 0x4);
        // .19: prepared for this node, an empty provider list, the camera's constant lists 0x4: the mask 0x4 is the OR of nothing with them.
        put(ctx, ohp::kCtxNode, addr(node));
        put(ctx, ohp::kCtxProviders, addr(provList));
        put<uint64_t>(cam, ohp::kCam8F0, 0x4);
        setList({});
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
    check(ohp::isOurs(addr(g.vtable), 0, 4, base) && ohp::isOurs(addr(g.vtable), 4, 7, base), "core: lists 0+4 and 4+7 of the vtable are ours");
    check(!ohp::isOurs(addr(g.vtable), 36, 4, base) && !ohp::isOurs(addr(g.vtable), 40, 7, base) && !ohp::isOurs(addr(g.vtable), 0, 3, base) &&
              !ohp::isOurs(addr(g.vtable), 4, 4, base) && !ohp::isOurs(addr(g.vtable) + 8, 0, 4, base),
          "core: the names' numbers (the .11 filter), another count or vtable is not");

    g.model(36, 3, 0x1234);  // the camera's own pocket
    g.model(37, 5, 0x1234);  // another camera's
    g.model(37, 9, 0x1234);
    put<uint64_t>(g.list(38), ohp::kListMask, 0x8);  // a mask the camera does not pass
    ohp::Snapshot s;
    check(ohp::capture(addr(g.node), &plainRead, nullptr, &s), "core: the node is read");
    check(s.first == 0 && s.count == 4 && s.pocket == 3 && s.cam == addr(g.cam) && s.viewCam == addr(g.cam) && s.viewBitIndex == 1 &&
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
    put<uint32_t>(g.set, ohp::kSetLists, 2);  // fewer lists than the node names
    check(ohp::capture(addr(g.node), &plainRead, nullptr, &s) && s.count == 2, "core: only the lists the set has");
    put<uint32_t>(g.set, ohp::kSetLists, 11);
    g.setFirst(4, 7);
    check(ohp::capture(addr(g.node), &plainRead, nullptr, &s) && s.first == 4 && s.count == 7 && s.list[0].mask == 0x4,
          "core: the second node reads lists 4-10 of the set");
    ohp::formatSnapshot(s, line, sizeof(line));
    check(std::strstr(line, "lists 40-46") && std::strstr(line, "| 40 pass=") && std::strstr(line, "| 46 pass="),
          "core text: the second node's lists by the names' numbers");
    g.setFirst(0, 4);

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

    // The census.
    static ohp::Census census;
    const uint64_t a = ohp::Census::key(base + 0x100, base, 0, 12, 0x640C000);
    const uint64_t b = ohp::Census::key(base + ohp::kNodeVtableRva, base, 36, 4, 0x640C000);
    const uint64_t outside = ohp::Census::key(0x1234, base, 2, 1, 0x640C000);
    for (int i = 0; i < 3; ++i) census.note(a);
    for (int i = 0; i < 5; ++i) census.note(b);
    census.note(outside);
    ohp::Census::Kind kinds[ohp::Census::kCells];
    int n = census.kinds(kinds, ohp::Census::kCells);
    check(n == 3 && kinds[0].hits == 5 && kinds[0].vtableRva == ohp::kNodeVtableRva && kinds[0].first == 36 && kinds[0].count == 4 &&
              kinds[1].hits == 3 && kinds[1].vtableRva == 0x100 && kinds[2].vtableRva == 0xFFFFFFFFu,
          "census: one cell a kind, most drawn first, a vtable outside the image");
    for (uint32_t i = 0; i < 40; ++i) census.note(ohp::Census::key(base + 0x200, base, i, 1, 0x640C000));
    n = census.kinds(kinds, ohp::Census::kCells);
    check(n == ohp::Census::kCells && census.overflow() == 40 - (ohp::Census::kCells - 3), "census: a full table counts what did not fit");
    check(ohp::formatCensusKind(kinds[0], line, sizeof(line)) > 0 &&
              std::strcmp(line, "on-foot hud probe: census: vtable +0x52ED2C0 (the list-range node's), first list 36, 4 list(s): 5 draw(s)") == 0,
          "census text: a kind");
    check(ohp::formatCensusHead(32, 11, line, sizeof(line)) > 0 && std::strstr(line, "no PostGUI node (vtable +0x52ED2C0, lists 0+4 or 4+7) in 20 s") &&
              std::strstr(line, "32 kind(s), 11 draw(s) of kinds that did not fit"),
          "census text: the head");

    // The other lines.
    check(ohp::formatKey(false, false, line, sizeof(line)) > 0 &&
              std::strcmp(line, "on-foot hud probe: off: nothing in the game is touched (experimental.on_foot_hud_probe = 0)") == 0,
          "text: key off at launch");
    check(ohp::formatKey(false, true, line, sizeof(line)) > 0 && std::strstr(line, "off: the hook only forwards now"), "text: key off live");
    check(ohp::formatKey(true, false, line, sizeof(line)) > 0 && std::strstr(line, "probe: on (experimental.on_foot_hud_probe = 1)"),
          "text: key on");
}

// ---- Part A, .13: the graph, the states, the events, the watch's sites ------------------------------------------------------------

// A fake graph system: S with up to three graphs, each with its own node list.
struct FakeGraphs {
    uint8_t system[0x50] = {};
    uint64_t graphs[3] = {};
    uint8_t graph[3][0x40] = {};
    uint64_t nodes[3][8] = {};
    uint8_t node[4][0x40] = {};  // the tracked nodes (their +0x30 flag)

    void build(int graphCount) {
        put<uint64_t>(system, ohp::kSysGraphCount, static_cast<uint64_t>(graphCount));
        put(system, ohp::kSysGraphs, addr(graphs));
        for (int g = 0; g < 3; ++g) {
            graphs[g] = addr(graph[g]);
            put<uint8_t>(graph[g], ohp::kGraphOn, 1);
            put<uint64_t>(graph[g], ohp::kGraphNodeCount, 0);
            put(graph[g], ohp::kGraphNodes, addr(nodes[g]));
        }
        for (auto& n : node) put<uint8_t>(n, ohp::kNodeOn, 1);
    }
    void add(int g, uint64_t n) {
        uint64_t count = 0;
        std::memcpy(&count, graph[g] + ohp::kGraphNodeCount, 8);
        nodes[g][count] = n;
        put<uint64_t>(graph[g], ohp::kGraphNodeCount, count + 1);
    }
    void clear(int g) { put<uint64_t>(graph[g], ohp::kGraphNodeCount, 0); }
};

void partA13() {
    static FakeGraphs f;
    f.build(2);
    f.add(0, 0x1111);          // a node of nobody's
    f.add(0, addr(f.node[0])); // hud, view 0: in graph 0
    f.add(1, addr(f.node[1])); // ui, view 0: in graph 1
    const uint64_t tracked[ohp::kTracked] = {addr(f.node[0]), addr(f.node[1]), addr(f.node[2]), 0};
    uint32_t seen = 0;
    uint64_t graphs[ohp::kTracked] = {};
    check(ohp::scanWalk(addr(f.system), tracked, &plainRead, nullptr, &seen, graphs), "graph: the walk is read");
    check(ohp::seenOf(seen, 0) == ohp::kSeenOn && ohp::seenOf(seen, 1) == ohp::kSeenOn && ohp::seenOf(seen, 2) == 0 && ohp::seenOf(seen, 3) == 0 &&
              graphs[0] == addr(f.graph[0]) && graphs[1] == addr(f.graph[1]) && graphs[2] == 0,
          "graph: each tracked node found in its graph, the missing one not");
    put<uint8_t>(f.node[0], ohp::kNodeOn, 0);
    put<uint8_t>(f.graph[1], ohp::kGraphOn, 0);
    ohp::scanWalk(addr(f.system), tracked, &plainRead, nullptr, &seen, graphs);
    check(ohp::seenOf(seen, 0) == ohp::kSeenNodeOff && ohp::seenOf(seen, 1) == ohp::kSeenGraphOff, "graph: node off, graph off");
    put<uint8_t>(f.node[0], ohp::kNodeOn, 1);
    put<uint8_t>(f.graph[1], ohp::kGraphOn, 1);
    put<uint64_t>(f.system, ohp::kSysGraphCount, ohp::kMaxGraphs + 1);
    check(!ohp::scanWalk(addr(f.system), tracked, &plainRead, nullptr, &seen, graphs) && seen == 0, "graph: a count beyond any is refused");
    put<uint64_t>(f.system, ohp::kSysGraphCount, 2);
    put<uint64_t>(f.graph[1], ohp::kGraphNodeCount, ohp::kMaxGraphNodes + 1);
    check(!ohp::scanWalk(addr(f.system), tracked, &plainRead, nullptr, &seen, graphs), "graph: a node count beyond any is refused");
    put<uint64_t>(f.graph[1], ohp::kGraphNodeCount, 1);
    check(ohp::graphHolds(addr(f.graph[0]), addr(f.node[0]), &plainRead, nullptr) == 1 &&
              ohp::graphHolds(addr(f.graph[1]), addr(f.node[0]), &plainRead, nullptr) == 0 &&
              ohp::graphHolds(0, addr(f.node[0]), &plainRead, nullptr) == -1,
          "graph: holds, does not, unknown");

    // The states.
    using ohp::classify;
    check(classify(ohp::kSeenOn | ohp::kSeenNodeOff, true, false, 1) == ohp::kStateOn, "state: on wins");
    check(classify(ohp::kSeenNodeOff | ohp::kSeenGraphOff, true, false, 1) == ohp::kStateNodeOff, "state: H1");
    check(classify(ohp::kSeenGraphOff, true, false, 1) == ohp::kStateGraphOff, "state: H3");
    check(classify(0, true, false, 0) == ohp::kStateNotInGraph, "state: H2");
    check(classify(0, true, false, 1) == ohp::kStateNotWalked, "state: still in its graph, not walked");
    check(classify(0, true, false, -1) == ohp::kStateNotFound, "state: its graph unknown");
    check(classify(0, false, true, 1) == ohp::kStateNotWalked && classify(0, false, true, 0) == ohp::kStateNotInGraph,
          "state: walks without nodes only");
    check(classify(0, false, false, 0) == ohp::kStateUnknown, "state: no walk, unknown");

    ohp::StateLog log;
    check(!log.admit(0, ohp::kStateUnknown, 0), "state log: unknown is never written");
    check(log.admit(0, ohp::kStateOn, 0) && !log.admit(0, ohp::kStateOn, 10), "state log: the first, not a repeat");
    check(log.admit(0, ohp::kStateNodeOff, 20) && log.last(0) == ohp::kStateNodeOff, "state log: a change");
    int written = 2;
    for (int i = 0; i < 30; ++i) written += log.admit(1, i & 1 ? ohp::kStateOn : ohp::kStateGraphOff, 100 + i) ? 1 : 0;
    // 18 more are written (the last, i = 17, "on"); of the 12 over the allowance only the six "graph-off" differ from it.
    check(written == ohp::StateLog::kPerWindow && log.held() == 6, "state log: 20 in 30 s, the changes over it counted");
    check(log.admit(1, log.last(1) == ohp::kStateOn ? ohp::kStateGraphOff : ohp::kStateOn, 30000), "state log: the next window writes again");
    log.forget(0);
    check(log.admit(0, ohp::kStateNodeOff, 30010), "state log: a new node's first state is written again");

    // The callers and an event.
    const uint64_t base = 0x140000000ull, size = 0x640C000ull;
    const uint64_t stack[3] = {base + 0x28ADA4D, base + 0x28575C0, 0x7FFE12345678ull};
    char line[1600];
    ohp::formatCallers(stack, 3, base, size, line, sizeof(line));
    check(std::strcmp(line, "EliteDangerous64.exe+0x28ADA4D < +0x28575C0 < 0x7FFE12345678") == 0, "text: the callers");
    ohp::formatCallers(stack, 0, base, size, line, sizeof(line));
    check(std::strcmp(line, "?") == 0, "text: no callers");
    ohp::GraphEvent e;
    e.remove = 1;
    e.node = 0xAB0;
    e.first = 0;
    e.graph = 0xC00;
    e.countAfter = 1;
    e.thread = 77;
    e.frames = 2;
    e.stack[0] = stack[0];
    e.stack[1] = stack[1];
    ohp::formatGraphEvent(e, 1, base, size, line, sizeof(line));
    check(std::strcmp(line, "on-foot hud probe: graph remove: node 0xAB0 (hud 36-39) graph 0xC00, 1 node(s) after, on-foot=yes, thread 77, from "
                            "EliteDangerous64.exe+0x28ADA4D < +0x28575C0") == 0,
          "text: a remove event");
    static ohp::Ring<32, ohp::GraphEvent> events;
    for (int i = 0; i < 35; ++i) {
        e.node = static_cast<uint64_t>(i);
        events.push(e);
    }
    int popped = 0;
    ohp::GraphEvent out;
    while (events.pop(&out)) ++popped;
    check(popped == 32 && events.dropped() == 3, "events: a full ring drops and counts");

    // The lines.
    check(ohp::formatPointHooked(ohp::kPointWalk, 0x1406BF980ull, 8, line, sizeof(line)) > 0 &&
              std::strstr(line, "hooked graph walk at EliteDangerous64.exe+0x6BF980 (0x1406BF980), 16 bytes verified, 8 moved; it reads"),
          "text: a point hooked");
    check(ohp::formatPointWrongBytes(ohp::kPointRemove, line, sizeof(line)) > 0 &&
              std::strstr(line, "NOT installed: the graph remove at EliteDangerous64.exe+0x6BFE60 does not begin") &&
              std::strstr(line, "the others stand"),
          "text: a point refused");
    check(ohp::formatState(ohp::kKindHud, 8, ohp::kStateNodeOff, 0xAB0, 0, 0xC00, 1, 1, line, sizeof(line)) > 0 &&
              std::strcmp(line, "on-foot hud probe: state hud 36-39 view bit 8: node-off (H1): in an enabled graph, the node's flag +0x30 is 0; "
                                "node 0xAB0 flag=0 graph 0xC00 on=1 on-foot=yes") == 0,
          "text: a state");
    check(ohp::formatState(ohp::kKindUi, 49, ohp::kStateNotFound, 0xAB0, -1, 0, -1, -1, line, sizeof(line)) > 0 &&
              std::strstr(line, "state ui 40-46 view bit 49: not-found") && std::strstr(line, "flag=? graph 0x0 on=? on-foot=?"),
          "text: a state, unread");
    check(ohp::formatTracked(ohp::kKindHud, 8, 0xAB0, 0, line, sizeof(line)) > 0 &&
              std::strcmp(line, "on-foot hud probe: tracking hud 36-39 node 0xAB0 of view bit 8") == 0,
          "text: tracking");

    ohp::GraphSummary gs;
    ohp::GraphCounts total;
    ohp::TrackedView none[1];
    check(gs.step(0, total, none, 0, line, sizeof(line)) == 0, "graph summary: the first call starts the window");
    total.walks = 3600;
    check(gs.step(30000, total, none, 0, line, sizeof(line)) > 0 && std::strstr(line, "30 s graph: 3600 node-calling walk(s)") &&
              std::strstr(line, "the hud 36-39 node is not recognised"),
          "graph summary: no hud node, said");
    ohp::TrackedView views[2];
    views[0] = {ohp::kKindHud, 8, ohp::kStateNodeOff};
    views[1] = {ohp::kKindUi, 8, ohp::kStateOn};
    total.postgui = 2;
    total.hits = 5;
    check(gs.step(60000, total, views, 2, line, sizeof(line)) > 0 &&
              std::strstr(line, "hud 36-39 bit 8 node-off (H1), ui 40-46 bit 8 on; graph events: 2 PostGUI, 0 other, 0 lost; watch: 5 hit(s)") &&
              !std::strstr(line, "not recognised"),
          "graph summary: the states and the window's counts");
}

// ---- Part A, .15: the cameras ---------------------------------------------------------------------------------------------------

bool nearDeg(float a, float b) { return std::fabs(a - b) <= 0.05f; }

// A camera's pose block for a yaw, pitch and roll in degrees, as cameraAngles reads it: rows right, up, forward, then the position.
void cameraBlock(float* m, float yawDeg, float pitchDeg, float rollDeg, float x = 0.f, float y = 0.f, float z = 0.f) {
    const float a = yawDeg / ohp::kDegrees, p = pitchDeg / ohp::kDegrees, r = rollDeg / ohp::kDegrees;
    const float right[3] = {std::cos(a), 0.f, -std::sin(a)};
    const float up[3] = {-std::sin(a) * std::sin(p), std::cos(p), -std::cos(a) * std::sin(p)};
    for (int i = 0; i < 3; ++i) {
        m[i] = right[i] * std::cos(r) + up[i] * std::sin(r);
        m[4 + i] = -right[i] * std::sin(r) + up[i] * std::cos(r);
    }
    m[8] = std::sin(a) * std::cos(p);
    m[9] = std::sin(p);
    m[10] = std::cos(a) * std::cos(p);
    m[3] = m[7] = m[11] = 0.f;
    m[12] = x;
    m[13] = y;
    m[14] = z;
    m[15] = 1.f;
}
// The head as headPose has it: OpenVR, row-major 3x4, the axes in the columns, forward -Z (the third column is the negated forward).
void headBlock(float* m12, float yawDeg, float pitchDeg) {
    float m[16];
    cameraBlock(m, yawDeg, pitchDeg, 0.f);
    for (int i = 0; i < 3; ++i) {
        m12[4 * i + 0] = m[i];
        m12[4 * i + 1] = m[4 + i];
        m12[4 * i + 2] = -m[8 + i];
        m12[4 * i + 3] = 0.f;
    }
}

void partA15() {
    float m[16];
    cameraBlock(m, 30.f, 0.f, 0.f);
    ohp::Angles a = ohp::cameraAngles(m);
    check(nearDeg(a.yaw, 30.f) && nearDeg(a.pitch, 0.f) && nearDeg(a.roll, 0.f), "angles: a camera turned 30 deg");
    cameraBlock(m, -30.f, 0.f, 0.f);
    check(nearDeg(ohp::cameraAngles(m).yaw, -30.f), "angles: a camera turned -30 deg");
    cameraBlock(m, 30.f, 20.f, 10.f);
    a = ohp::cameraAngles(m);
    check(nearDeg(a.yaw, 30.f) && nearDeg(a.pitch, 20.f) && nearDeg(a.roll, 10.f), "angles: a camera at yaw 30, pitch 20, roll 10");
    float h[12];
    headBlock(h, 30.f, 20.f);
    a = ohp::headAngles(h);
    check(nearDeg(a.yaw, 30.f) && nearDeg(a.pitch, 20.f), "angles: the head turned 30 and up 20");
    headBlock(h, -30.f, 0.f);
    check(nearDeg(ohp::headAngles(h).yaw, -30.f), "angles: the head turned -30");

    // The yaw step.
    check(nearDeg(ohp::wrapDeg(-170.f - 170.f), 20.f) && nearDeg(ohp::wrapDeg(170.f - -170.f), -20.f) && nearDeg(ohp::wrapDeg(180.f), 180.f) &&
              nearDeg(ohp::wrapDeg(-180.f), 180.f) && nearDeg(ohp::wrapDeg(10.f), 10.f) && std::isnan(ohp::wrapDeg(NAN)),
          "step: across +/-180 the short way, in (-180, 180]");

    // The follow slopes.
    ohp::Follow same, still, back;
    for (int i = 0; i < 6; ++i) {
        const float d = i % 2 ? -10.f : 10.f;
        same.add(d, d, d);
        still.add(d, 0.f, NAN);
        back.add(d, -d, -d);
    }
    char line[1600], want[800];
    check(ohp::formatFollow(0, 1, 30, same, line, sizeof(line)) > 0 &&
              std::strcmp(line, "on-foot hud probe: 30 s follow view 0 stereo: postgui/main k=1.00 (main turned 60 deg); main/head k=1.00 (head "
                                "turned 60 deg); 6 step(s), 0 left out (a yaw step over 90 deg)") == 0,
          "follow: turning the same way, 1");
    ohp::formatFollow(1, 0, 30, still, line, sizeof(line));
    check(std::strstr(line, "follow view 1 cinema: postgui/main k=0.00 (main turned 60 deg); main/head no head pose") != nullptr,
          "follow: not turning, 0; no head pose");
    ohp::formatFollow(0, 1, 30, back, line, sizeof(line));
    check(std::strstr(line, "postgui/main k=-1.00") && std::strstr(line, "main/head k=-1.00"), "follow: turning the other way, -1");
    ohp::Follow little;
    little.add(10.f, 10.f, 3.f);
    little.add(-5.f, -5.f, 3.f);
    ohp::formatFollow(0, 1, 30, little, line, sizeof(line));
    check(std::strstr(line, "postgui/main too little turn (main 15 deg); main/head too little turn (head 6 deg)") != nullptr,
          "follow: under 20 deg in all, too little");
    ohp::Follow jumpy;
    for (int i = 0; i < 3; ++i) jumpy.add(10.f, 10.f, NAN);
    jumpy.add(100.f, 0.f, NAN);
    jumpy.add(10.f, 10.f, 95.f);
    jumpy.add(10.f, INFINITY, NAN);
    check(jumpy.steps == 3 && jumpy.jumps == 3 && nearDeg(static_cast<float>(jumpy.mainTurn), 30.f) && jumpy.headSteps == 0,
          "follow: a step over 90 deg, or not finite, is left out and counted");
    ohp::formatFollow(0, 1, 30, jumpy, line, sizeof(line));
    check(std::strstr(line, "postgui/main k=1.00 (main turned 30 deg)") && std::strstr(line, "3 step(s), 3 left out"), "follow text: the jumps");
    check(!ohp::Follow{}.any() && jumpy.any(), "follow: an empty one has nothing to say");

    // The pass table.
    static ohp::PassTable table;
    table.note(0xA0);
    table.note(0xA0);
    table.note(0xB0);
    table.note(0xC0);
    table.note(0xD0);
    table.note(0xE0);
    check(table.pass(0) == 0xA0 && table.calls(0) == 2 && table.pass(3) == 0xD0 && table.overflow() == 1,
          "passes: four cells, a repeat counted in its own cell, the fifth did not fit");
    table.retire(0, 0xA0);
    table.note(0xB0);
    check(table.pass(0) == 0 && table.calls(1) == 2, "passes: a freed cell does not take a pass already in another");
    table.note(0xE0);
    check(table.pass(0) == 0xE0 && table.overflow() == 1, "passes: a new pass takes the freed cell");
    table.retire(1, 0x999);
    check(table.pass(1) == 0xB0, "passes: retiring another pass than the cell's does nothing");

    // A pass and its cameras, and the lines.
    static struct {
        uint8_t pass[0x60];
        uint8_t main[0x260];
        uint8_t post[0x260];
    } f = {};
    put<int32_t>(f.pass, ohp::kPassView, 0);
    put(f.pass, ohp::kPassMainCamera, addr(f.main));
    put(f.pass, ohp::kPassPostGuiCamera, addr(f.post));
    cameraBlock(m, 30.f, 0.f, 0.f, 1.f, 2.f, 3.f);
    std::memcpy(f.main + ohp::kCamPose, m, sizeof(m));
    const float identity[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    std::memcpy(f.post + ohp::kCamPose, identity, sizeof(identity));
    put<uint32_t>(f.post, ohp::kCamFlags250, 0xF);
    ohp::PassPose p;
    ohp::readPass(addr(f.pass), &plainRead, nullptr, &p);
    check(p.view == 0 && p.main.read && p.post.read && p.post.flagsRead && p.post.flags250 == 0xF && p.main.cam == addr(f.main) &&
              p.post.cam == addr(f.post),
          "pose: the pass and both cameras read");
    ohp::formatPose(p, 1, 1, h, 1, 0x204, line, sizeof(line));
    std::snprintf(want, sizeof(want),
                  "on-foot hud probe: pose view 0 stereo on-foot=yes pass 0x%llX: main 0x%llX yaw 30.0 pitch 0.0 roll 0.0 at (1.00,2.00,3.00); "
                  "postgui 0x%llX yaw 0.0 pitch 0.0 roll 0.0 at (0.00,0.00,0.00) rows (1.000,0.000,0.000; 0.000,1.000,0.000; 0.000,0.000,1.000) "
                  "flags250=0xF; postgui-main yaw -30.0; head yaw -30.0 pitch 0.0; hud mask 0x204",
                  static_cast<unsigned long long>(addr(f.pass)), static_cast<unsigned long long>(addr(f.main)),
                  static_cast<unsigned long long>(addr(f.post)));
    check(std::strcmp(line, want) == 0, "pose text: both cameras, the PostGUI rows, the difference, the head, the mask");
    put(f.pass, ohp::kPassPostGuiCamera, uint64_t(0x10));  // a pointer no game would have
    ohp::readPass(addr(f.pass), &plainRead, nullptr, &p);
    ohp::formatPose(p, 0, 0, nullptr, 2, 0x20C, line, sizeof(line));
    check(std::strstr(line, "pose view 0 cinema on-foot=no") &&
              std::strstr(line, "; postgui 0x10 none; postgui-main none; head none; hud mask 0x20C (by the main camera)") && !std::strstr(line, "rows"),
          "pose text: an unreadable camera and no head pose are none");
    ohp::formatPose(p, -1, -1, nullptr, 0, 0, line, sizeof(line));
    check(std::strstr(line, "pose view 0 ? on-foot=?") && std::strstr(line, "; hud mask ?"), "pose text: nothing known, ?");
    put(f.pass, ohp::kPassPostGuiCamera, addr(f.main));
    ohp::readPass(addr(f.pass), &plainRead, nullptr, &p);
    ohp::formatNewPass(p, line, sizeof(line));
    std::snprintf(want, sizeof(want), "on-foot hud probe: pass switch: new pass 0x%llX, view 0: main camera 0x%llX, PostGUI camera 0x%llX (the same camera)",
                  static_cast<unsigned long long>(addr(f.pass)), static_cast<unsigned long long>(addr(f.main)),
                  static_cast<unsigned long long>(addr(f.main)));
    check(std::strcmp(line, want) == 0, "text: a new pass whose PostGUI camera is the main one");
    ohp::readPass(0x10, &plainRead, nullptr, &p);
    ohp::formatNewPass(p, line, sizeof(line));
    check(std::strstr(line, "new pass 0x10, view ?: main camera 0x0, PostGUI camera 0x0") && !std::strstr(line, "same"), "text: an unreadable pass");

    // The masks by camera.
    ohp::MaskByCamera masks;
    uint64_t mask = 0;
    masks.note(0xCA0, 0xC00, 0x204, 1000);
    masks.note(0xCA0, 0xC00, 0x20C, 1500);
    check(masks.find(0xCA0, 2000, &mask) && mask == 0x20C, "masks: the last per camera");
    check(!masks.find(0xCA0, 2501, &mask) && !masks.find(0xCB0, 2000, &mask) && !masks.find(0, 2000, &mask),
          "masks: none older than a second, of another camera, of no camera");
    for (uint64_t c = 1; c <= 8; ++c) masks.note(0x1000 + c, 0xC00, c, 3000 + c);
    check(!masks.find(0xCA0, 1600, &mask) && masks.find(0x1001, 3100, &mask) && mask == 1, "masks: eight cameras, the oldest made room");

    // The switch's lines.
    ohp::SwitchWindow w;
    w.calls = 3600;
    w.fresh = 2;
    w.retired = 1;
    w.poses = 120;
    const uint64_t passes[2] = {0xAA0, 0xBB0};
    const int32_t views[2] = {0, -1};
    ohp::formatSwitchSummary(30, w, passes, views, 2, line, sizeof(line));
    check(std::strcmp(line, "on-foot hud probe: 30 s pass switch: 3600 call(s); 2 pass(es) now: view 0 0xAA0, view ? 0xBB0; 2 new, 1 retired, 0 did "
                            "not fit; 120 pose line(s)") == 0,
          "text: the switch's 30-second line");
    ohp::formatSwitchSummary(30, ohp::SwitchWindow{}, passes, views, 0, line, sizeof(line));
    check(std::strstr(line, "0 call(s); 0 pass(es) now; 0 new") != nullptr, "text: a switch never called");
    check(ohp::formatPointHooked(ohp::kPointPass, 0x1428AD4A0ull, 6, line, sizeof(line)) > 0 &&
              std::strstr(line, "hooked pass switch at EliteDangerous64.exe+0x28AD4A0 (0x1428AD4A0), 16 bytes verified, 6 moved; it only notes the pass"),
          "text: the switch hooked");
}

// ---- Part A, .19: the subscription watch's sites, places and lines ------------------------------------------------------------------

void partA16() {
    const uint64_t base = 0x140000000ull, size = 0x640C000ull;
    const uint64_t stack[2] = {base + 0x28ADA4D, base + 0x28575C0};
    char line[1600];

    // The place: in the ship unless the journal says on foot; then the mode.
    check(ohp::placeOf(1, 1) == ohp::kPlaceStereo && ohp::placeOf(1, 0) == ohp::kPlaceCinema && ohp::placeOf(0, 1) == ohp::kPlaceShip &&
              ohp::placeOf(-1, 0) == ohp::kPlaceShip,
          "place: stereo, cinema, the ship, unknown counts as the ship");
    check(ohp::kHudBit == 0x8 && ohp::kWatchCount == 1 && ohp::kWatchSlot[ohp::kWatchSubscription] == 2 &&
              ohp::watchOffset(ohp::kWatchSubscription) == 0x28 && std::strcmp(ohp::watchName(ohp::kWatchSubscription), "hud provider subscription") == 0,
          "watch: list 39's bit, one watch on slot 2 at the subscription, slot 3 not used");

    // The subscription's sites: by place, with the camera bit as the handler tells it, the last value kept, the first one published.
    static ohp::SiteTable subs;
    bool fresh = false;
    const int row = subs.record(base + 0x59F313, ohp::kPlaceStereo, 0x4, true, &fresh);
    check(row == 0 && fresh, "sites: a new site");
    subs.publish(row, 9, ohp::kPlaceStereo, 0x4, true, stack, 2);
    check(subs.record(base + 0x59F313, ohp::kPlaceCinema, 0x0, false, &fresh) == 0 && !fresh && subs.site(0).hits.load() == 2 &&
              subs.site(0).at[ohp::kPlaceStereo].load() == 1 && subs.site(0).at[ohp::kPlaceCinema].load() == 1 &&
              subs.site(0).at[ohp::kPlaceShip].load() == 0 && subs.site(0).camBit[ohp::kPlaceStereo].load() == 1 &&
              subs.site(0).camBit[ohp::kPlaceCinema].load() == 0 && subs.site(0).last.load() == 0,
          "sites: a repeat only counted, by place and camera bit, its value the last");
    int r = -1;
    check(subs.nextUnsaid(&r) && r == 0 && !subs.nextUnsaid(&r), "sites: said once");
    check(ohp::formatWriter(ohp::kWatchSubscription, 0, subs.site(0), 0xCA0, base, size, line, sizeof(line)) > 0 &&
              std::strcmp(line, "on-foot hud probe: writer #1 of the hud provider subscription of 0xCA0: at EliteDangerous64.exe+0x59F313 (after "
                                "the write), value 0x4 (with the HUD camera bit), on foot in stereo, thread 9, from EliteDangerous64.exe+0x28ADA4D "
                                "< +0x28575C0") == 0,
          "text: a writer, its value and place, the camera bit on");

    // A second site whose first value has no camera bit, and no callers.
    static ohp::SiteTable masks;
    masks.record(base + 0x588A75, ohp::kPlaceCinema, 0x800000, true, &fresh);
    masks.publish(0, 9, ohp::kPlaceCinema, 0x800000, true, stack, 2);
    masks.record(base + 0x588A75, ohp::kPlaceStereo, 0x0, false, &fresh);
    check(masks.site(0).camBit[ohp::kPlaceCinema].load() == 1 && masks.site(0).camBit[ohp::kPlaceStereo].load() == 0 &&
              masks.site(0).at[ohp::kPlaceStereo].load() == 1,
          "sites: the camera bit by place");
    masks.publish(0, 9, ohp::kPlaceShip, 0x0, false, stack, 0);
    check(ohp::formatWriter(ohp::kWatchSubscription, 0, masks.site(0), 0xC00, base, size, line, sizeof(line)) > 0 &&
              std::strstr(line, "value 0x0 (without the HUD camera bit), in the ship, thread 9, from ?"),
          "text: a writer, the camera bit off, no callers");

    // The 30-second line: the window's writes per site and place; a quiet watch says so.
    ohp::WatchSummary ws;
    check(ws.step(0, ohp::kWatchSubscription, 0xE70, masks, line, sizeof(line)) == 0, "watch summary: the first call starts the window");
    for (int i = 0; i < 3; ++i) masks.record(base + 0x588A75, ohp::kPlaceCinema, 0x4, true, &fresh);
    masks.record(base + 0x588A75, ohp::kPlaceStereo, 0x0, false, &fresh);
    masks.record(base + 0x588B00, ohp::kPlaceStereo, 0x0, false, &fresh);
    check(ws.step(29999, ohp::kWatchSubscription, 0xE70, masks, line, sizeof(line)) == 0, "watch summary: not before 30 s");
    check(ws.step(30000, ohp::kWatchSubscription, 0xE70, masks, line, sizeof(line)) > 0 &&
              std::strcmp(line, "on-foot hud probe: 30 s watch hud provider subscription 0xE70: 5 write(s); #1 ship 0, stereo 1, cinema 3 (with the "
                                "camera bit 0, 0, 3), last 0x0; #2 ship 0, stereo 1, cinema 0 (with the camera bit 0, 0, 0), last 0x0; 0 from sites "
                                "that did not fit") == 0,
          "watch summary: the window by site and place, with the camera bit");
    check(ws.step(60000, ohp::kWatchSubscription, 0xE70, masks, line, sizeof(line)) > 0 &&
              std::strcmp(line, "on-foot hud probe: 30 s watch hud provider subscription 0xE70: 0 write(s): it is on and stood quiet; 0 from sites "
                                "that did not fit") == 0,
          "watch summary: a quiet watch says so");

    // The ninth site is only counted, in the table and in the line.
    static ohp::SiteTable many;
    many.record(base + 0x59F313, ohp::kPlaceStereo, 0x4, true, &fresh);
    for (uint64_t i = 1; i < 10; ++i) many.record(base + 0x2000 + i, ohp::kPlaceShip, 0, false, &fresh);
    check(many.sites() == ohp::kSites && many.overflow() == 2 && many.hits() == 10, "sites: the ninth site is overflow");
    ohp::WatchSummary wc;
    wc.step(0, ohp::kWatchSubscription, 0xCC0, many, line, sizeof(line));
    many.record(base + 0x59F313, ohp::kPlaceStereo, 0x4, true, &fresh);
    many.record(base + 0x59F313, ohp::kPlaceStereo, 0x4, true, &fresh);
    many.record(base + 0x3000, ohp::kPlaceStereo, 0, false, &fresh);
    check(wc.step(30000, ohp::kWatchSubscription, 0xCC0, many, line, sizeof(line)) > 0 &&
              std::strcmp(line, "on-foot hud probe: 30 s watch hud provider subscription 0xCC0: 3 write(s); #1 ship 0, stereo 2, cinema 0 (with the "
                                "camera bit 0, 2, 0), last 0x4; 1 from sites that did not fit") == 0,
          "watch summary: the window, the site that did not fit counted");

    // The watch lines.
    check(ohp::formatWatchOff(ohp::kWatchSubscription, 0xCC0, "the key is off", many, line, sizeof(line)) > 0 &&
              std::strstr(line, "watch off: hud provider subscription 0xCC0: the key is off; 8 site(s), 13 hit(s), 3 from sites that did not fit"),
          "text: watch off");
    check(ohp::formatWatchOn(ohp::kWatchSubscription, 0xE70, 0xE48, 40, 1, 2, nullptr, line, sizeof(line)) > 0 &&
              std::strcmp(line, "on-foot hud probe: watch on: hud provider subscription 0xE70 (0xE48+0x28) on debug slot 2, write, 4 bytes: 40 "
                                "thread(s), 1 skipped (slot taken), 2 could not be set") == 0,
          "text: watch on");
    char note[200];
    ohp::formatWatchTarget(1, 1, note, sizeof(note));
    check(ohp::formatWatchOn(ohp::kWatchSubscription, 0xE70, 0xE48, 40, 1, 2, note, line, sizeof(line)) > 0 &&
              std::strstr(line, "2 could not be set; the first provider with 0x8, of view bit 1, the pass's view 0"),
          "text: watch on, the target's note: view 0");
    ohp::formatWatchTarget(2, 0, note, sizeof(note));
    check(std::strcmp(note, "the first provider with 0x8, of view bit 2 (view 0 has none with 0x8)") == 0, "text: the note, another view");
    ohp::formatWatchTarget(2, -1, note, sizeof(note));
    check(std::strcmp(note, "the first provider with 0x8, of view bit 2 (the pass's view 0 is not known yet)") == 0, "text: the note, no pass yet");
    check(ohp::formatWatchNotOn(ohp::kWatchSubscription, 0xCC0, "no thread took it", line, sizeof(line)) > 0 &&
              std::strstr(line, "watch NOT on: hud provider subscription 0xCC0: no thread took it; the provider, pose, state and graph lines go on"),
          "text: watch not on");
    check(ohp::formatWatchMoves(ohp::kWatchSubscription, 0xE70, 0xF70, line, sizeof(line)) > 0 &&
              std::strcmp(line, "on-foot hud probe: watch moves: hud provider subscription 0xE70 -> 0xF70 (the watched provider left its view's list "
                                "for 2 s; this one answered with 0x8)") == 0,
          "text: a watch moves");

    // The fuse.
    ohp::Fuse fuse;
    check(!fuse.step(0, 0) && !fuse.step(1000, 100000), "fuse: 100 000 in the window is allowed");
    check(fuse.step(2000, 100001), "fuse: one more trips it");
    check(!fuse.step(31000, 150000) && !fuse.step(32000, 160000), "fuse: a new window starts over");

    // The draw context by camera, for the pose lines' mask.
    ohp::MaskByCamera byCam;
    byCam.note(0xCA0, 0xC00, 0x20C, 1000);
    uint64_t mask = 0, ctx = 0;
    check(byCam.find(0xCA0, 1500, &mask, &ctx) && mask == 0x20C && ctx == 0xC00 && !byCam.find(0xCA0, 2001, &mask, &ctx),
          "mask by camera: the draw context with the mask, not when stale");
}

// ---- Part A, .19: the HUD node's providers ---------------------------------------------------------------------------------------

const ohp::ProviderTable::Row* findRow(const ohp::ProviderTable& t, uint64_t provider, int view = 0) {
    for (int i = 0; i < ohp::kProvRows; ++i) {
        const auto& r = t.rowAt(i);
        if (r.key.load() && ohp::ProviderTable::rowAddress(r) == provider && ohp::ProviderTable::rowView(r) == view) return &r;
    }
    return nullptr;
}

// A reader that cannot read one range, as the unreadable address in the list.
struct Blocked {
    uint64_t lo, hi;
};
bool blockedRead(void* ctx, uint64_t a, void* out, size_t n) {
    const auto* b = static_cast<const Blocked*>(ctx);
    if (a < b->hi && a + n > b->lo) return false;
    std::memcpy(out, reinterpret_cast<const void*>(a), n);
    return true;
}

void partA19() {
    static FakeGame g;
    static ohp::ProviderTable t;
    g.build();
    const uint64_t base = g.base(), size = 0x10000000ull, node = addr(g.node);
    char line[1600], want[1600];
    // The camera is on bit 0x4 and asks for list 38 (0x4) always. Three providers: one subscribed and giving _HUD, one subscribed
    // and silent, one not subscribed.
    put<uint64_t>(g.cam, ohp::kCamMask8E0, 0x4);
    g.setProvider(0, 0x4, 0x8);
    g.setProvider(1, 0x4, 0x0);
    g.setProvider(2, 0x0, 0x0);
    g.setList({0, 1, 2});
    g.sync();

    // The list as the game's prepare read it, and the check that it is the node's and gives the mask.
    ohp::ProviderSnap s;
    check(ohp::readProviders(node, &plainRead, nullptr, &s) == ohp::kSnapOk && s.number == 3 && s.stored == 3 && s.extras == 0 && s.mask == 0xC &&
              s.cam8E0 == 0x4 && s.cam8F0 == 0x4 && s.viewBit == 1 && s.cam == addr(g.cam) && s.ctx == addr(g.ctx) && s.shape != 0,
          "providers: the list read, the node's, the answers' OR with the constant lists is the mask");
    check(s.p[0].addr == addr(g.providers[0]) && s.p[0].vtable == addr(g.vts[0]) && s.p[0].answerFn == g.method(0) && s.p[0].sub == 0x4 &&
              s.p[0].answer == 0x8 && ohp::subscribed(s.p[0], s.cam8E0) && !ohp::subscribed(s.p[2], s.cam8E0) && s.p[2].answerFn == g.method(2),
          "providers: address, vtable, answering method, subscription, answer; subscribed or not");

    // Into the table: one gives _HUD, one is subscribed and silent, the unsubscribed one has an answer 0; counts by place.
    check(t.take(ohp::kSnapOk, s, ohp::kPlaceCinema) && s.viewSlot == 0 && t.matched() == 1, "table: the first matched snapshot is new");
    ohp::ProviderSnap again;
    ohp::readProviders(node, &plainRead, nullptr, &again);
    check(!t.take(ohp::kSnapOk, again, ohp::kPlaceCinema) && t.matched() == 2, "table: the same list in the same place is not new");
    const auto *p0 = findRow(t, addr(g.providers[0])), *p1 = findRow(t, addr(g.providers[1])), *p2 = findRow(t, addr(g.providers[2]));
    check(p0 && p1 && p2 && p0->frames[ohp::kPlaceCinema].load() == 2 && p0->subscribed[ohp::kPlaceCinema].load() == 2 &&
              p0->hud[ohp::kPlaceCinema].load() == 2 && p0->hud[ohp::kPlaceStereo].load() == 0 && p0->hudSeq.load() == 1 && p0->answer.load() == 8 &&
              p1->frames[ohp::kPlaceCinema].load() == 2 && p1->subscribed[ohp::kPlaceCinema].load() == 2 && p1->hud[ohp::kPlaceCinema].load() == 0 &&
              p1->hudSeq.load() == 0 && p2->frames[ohp::kPlaceCinema].load() == 2 && p2->subscribed[ohp::kPlaceCinema].load() == 0 &&
              p2->answer.load() == 0 && t.hudSeen() && t.camBits() == 0x4 && t.viewBitOf(0) == 1 && t.viewBitOf(1) == -1,
          "table: frames, frames subscribed, frames with 0x8 by place; the unsubscribed one answers 0");
    check(ohp::providerWorthALine(*p0) && ohp::providerWorthALine(*p1) && !ohp::providerWorthALine(*p2),
          "table: a provider never subscribed and never answering has no summary line");

    // The lines of the list.
    std::snprintf(want, sizeof(want),
                  "on-foot hud probe: providers of the HUD node: view bit 1, on foot in cinema, node 0x%llX camera 0x%llX: camera bit (cam+0x8E0) "
                  "0x4, constant lists (cam+0x8F0) 0x4, frame mask (ctx+0x270) 0xC; 3 provider(s), 3 listed, 0 extra not listed",
                  static_cast<unsigned long long>(node), static_cast<unsigned long long>(addr(g.cam)));
    check(ohp::formatProviderHead(s, line, sizeof(line)) > 0 && std::strcmp(line, want) == 0, "text: the list's head");
    std::snprintf(want, sizeof(want),
                  "on-foot hud probe:   provider #0 0x%llX vtable EliteDangerous64.exe+0x%llX answer method EliteDangerous64.exe+0x%llX +0x28=0x4 "
                  "subscribed=yes +0x30=0x8 _HUD=yes",
                  static_cast<unsigned long long>(addr(g.providers[0])), static_cast<unsigned long long>(addr(g.vts[0]) - base),
                  static_cast<unsigned long long>(g.method(0) - base));
    check(ohp::formatProviderLine(s, 0, base, size, line, sizeof(line)) > 0 && std::strcmp(line, want) == 0, "text: the provider that gives _HUD");
    check(ohp::formatProviderLine(s, 2, base, size, line, sizeof(line)) > 0 && std::strstr(line, " +0x28=0x0 subscribed=no +0x30=0x0 _HUD=no"),
          "text: the unsubscribed provider says not subscribed, answer 0");
    char out[120];
    ohp::formatImageAddress(0x1234, base, size, out, sizeof(out));
    check(std::strcmp(out, "0x1234 (outside the image)") == 0, "text: an address outside the image is absolute, marked");
    ohp::formatImageAddress(0, base, size, out, sizeof(out));
    check(std::strcmp(out, "none") == 0, "text: no address");
    g.setList({0, -1, 2});
    ohp::ProviderSnap holes;
    check(ohp::readProviders(node, &plainRead, nullptr, &holes) == ohp::kSnapOk && holes.stored == 3 && holes.p[1].addr == 0 &&
              ohp::formatProviderLine(holes, 1, base, size, line, sizeof(line)) > 0 && std::strstr(line, "provider #1: empty slot"),
          "providers: an empty slot of the list is listed as empty");
    g.setList({0, 1, 2});

    // The context is another node's: dropped, counted by its reason, the pair kept for one line, no row touched.
    put(g.ctx, ohp::kCtxNode, node + 0x1000);
    ohp::ProviderSnap other;
    check(ohp::readProviders(node, &plainRead, nullptr, &other) == ohp::kSnapOtherNode && other.badA == node + 0x1000 && other.badB == node,
          "providers: the context prepared for another node is told");
    check(!t.take(ohp::kSnapOtherNode, other, ohp::kPlaceCinema) && t.otherNode() == 1 && t.matched() == 2 &&
              p0->frames[ohp::kPlaceCinema].load() == 2,
          "table: another node's context is only counted, no row");
    put(g.ctx, ohp::kCtxNode, node);
    int reason = 0;
    uint64_t a = 0, b = 0, badNode = 0;
    check(t.nextBad(&reason, &a, &b, &badNode) && reason == ohp::kSnapOtherNode && a == node + 0x1000 && b == node && !t.nextBad(&reason, &a, &b, &badNode),
          "table: the first pair that did not match, once");
    std::snprintf(want, sizeof(want),
                  "on-foot hud probe: first provider snapshot that did not match: the draw context was prepared for another node (ctx+0x150 = 0x%llX, the "
                  "node drawn 0x%llX); later ones are only counted",
                  static_cast<unsigned long long>(node + 0x1000), static_cast<unsigned long long>(node));
    check(ohp::formatBadPair(reason, a, b, node, line, sizeof(line)) > 0 && std::strcmp(line, want) == 0, "text: the other-node pair");

    // The OR of the answers is not the frame mask: another reason, counted, no row touched; the pair is not said again.
    put<uint64_t>(g.ctx, ohp::kCtxMask, 0x4);
    ohp::ProviderSnap wrongMask;
    check(ohp::readProviders(node, &plainRead, nullptr, &wrongMask) == ohp::kSnapOrMismatch && wrongMask.badA == 0xC && wrongMask.badB == 0x4,
          "providers: the OR that is not the mask is told");
    check(!t.take(ohp::kSnapOrMismatch, wrongMask, ohp::kPlaceCinema) && t.orMismatch() == 1 && t.otherNode() == 1 && t.matched() == 2 &&
              p0->frames[ohp::kPlaceCinema].load() == 2 && !t.nextBad(&reason, &a, &b, &badNode),
          "table: the other reason is counted apart, nothing else changes");
    static ohp::ProviderTable t2;
    t2.take(ohp::kSnapOrMismatch, wrongMask, ohp::kPlaceShip);
    check(t2.nextBad(&reason, &a, &b, &badNode) && reason == ohp::kSnapOrMismatch, "table: the OR pair is kept as well");
    std::snprintf(want, sizeof(want),
                  "on-foot hud probe: first provider snapshot that did not match: the answers' OR with cam+0x8F0 is 0xC but the frame mask ctx+0x270 of "
                  "node 0x%llX is 0x4; later ones are only counted",
                  static_cast<unsigned long long>(node));
    check(ohp::formatBadPair(reason, a, b, node, line, sizeof(line)) > 0 && std::strcmp(line, want) == 0, "text: the OR pair");
    g.sync();

    // An unreadable provider, and an unreadable list: the snapshot is skipped, the fault counted, nothing else changes.
    Blocked block{addr(g.providers[1]), addr(g.providers[1]) + 0x40};
    ohp::ProviderSnap fault;
    check(ohp::readProviders(node, &blockedRead, &block, &fault) == ohp::kSnapFault && !t.take(ohp::kSnapFault, fault, ohp::kPlaceCinema) &&
              t.faults() == 1 && p1->frames[ohp::kPlaceCinema].load() == 2,
          "providers: an unreadable provider skips the snapshot, the fault is counted");
    block = Blocked{addr(g.provList), addr(g.provList) + 0x10};
    check(ohp::readProviders(node, &blockedRead, &block, &fault) == ohp::kSnapFault, "providers: an unreadable list skips the snapshot");
    block = Blocked{addr(g.vts[1]), addr(g.vts[1]) + sizeof(g.vts[1])};
    check(ohp::readProviders(node, &blockedRead, &block, &fault) == ohp::kSnapOk && fault.p[1].vtable == addr(g.vts[1]) && fault.p[1].answerFn == 0,
          "providers: an unreadable answering method is 0 and does not skip the snapshot");
    g.setList({0, 1, 2});
    g.setProvider(0, 0x4, 0x8);

    // The shape: the answer changes, the make-up does not -- not new; the subscription changes -- new; the place changes -- new.
    g.setProvider(1, 0x4, 0x2);
    g.sync();
    ohp::ProviderSnap answered;
    ohp::readProviders(node, &plainRead, nullptr, &answered);
    check(answered.shape == s.shape && !t.take(ohp::kSnapOk, answered, ohp::kPlaceCinema) && p1->nonzero.load() == 1 && p1->answer.load() == 2,
          "shape: a changed answer is the same shape, only counted");
    g.setProvider(2, 0x4, 0x0);
    g.sync();
    ohp::ProviderSnap subscribedNow;
    ohp::readProviders(node, &plainRead, nullptr, &subscribedNow);
    check(subscribedNow.shape != s.shape && t.take(ohp::kSnapOk, subscribedNow, ohp::kPlaceCinema) && p2->subscribed[ohp::kPlaceCinema].load() == 1 &&
              ohp::providerWorthALine(*p2),
          "shape: a changed subscription is a new shape; the provider now has a line");
    check(t.take(ohp::kSnapOk, subscribedNow, ohp::kPlaceStereo) && p0->frames[ohp::kPlaceStereo].load() == 1 && p0->hud[ohp::kPlaceStereo].load() == 1,
          "shape: the same list in another place is new");
    std::snprintf(want, sizeof(want),
                  "on-foot hud probe:   provider 0x%llX view bit 1 vtable EliteDangerous64.exe+0x%llX answer method EliteDangerous64.exe+0x%llX: "
                  "frames/subscribed/_HUD ship 0/0/0, stereo 1/1/1, cinema 4/4/4; last +0x28 0x4, last +0x30 0x8",
                  static_cast<unsigned long long>(addr(g.providers[0])), static_cast<unsigned long long>(addr(g.vts[0]) - base),
                  static_cast<unsigned long long>(g.method(0) - base));
    check(ohp::formatProviderRow(*p0, 1, base, size, line, sizeof(line)) > 0 && std::strcmp(line, want) == 0, "text: a provider's three counts and its last values");

    // The totals line: matched, not matched by reason, unreadable, the ring, the extras.
    check(ohp::formatProviderSummary(30, 5, t, 2, line, sizeof(line)) > 0 &&
              std::strcmp(line,
                          "on-foot hud probe: 30 s providers of the HUD node: 5 snapshot(s) of the HUD node in these seconds; since recording began 5 "
                          "matched, 2 not matched (1 another node's context, 1 OR of the answers != frame mask), 1 unreadable; ring full 2 time(s); 0 "
                          "extra provider(s) not listed, 0 did not fit the table, 0 view(s) did not fit") == 0,
          "text: the 30-second totals, matched against not matched by reason");
    check(ohp::formatProviderSummary(30, 0, t, 0, line, sizeof(line)) > 0 &&
              std::strstr(line, "30 s providers of the HUD node: no snapshots of the HUD node in these seconds (the node was not drawn, or none could be "
                                "read): the providers are not silent, they were not asked; since recording began 5 matched"),
          "text: no snapshots is said, and is not 'providers silent'");
    check(ohp::formatNoHudProvider(line, sizeof(line)) > 0 &&
              std::strcmp(line, "on-foot hud probe: no provider of the HUD node has answered with the _HUD bit 0x8 in a matched snapshot yet: the "
                                "subscription watch is not armed") == 0,
          "text: no provider ever gave 0x8");

    // The summary's window: due 30 s after its first call; with snapshots or without.
    ohp::ProviderSummary ps;
    uint64_t seconds = 0, snaps = 0;
    check(ps.step(1000, 10, &seconds, &snaps) == 0 && ps.step(30999, 20, &seconds, &snaps) == 0, "summary: the first call starts the window, not due before 30 s");
    check(ps.step(31000, 25, &seconds, &snaps) == 1 && seconds == 30 && snaps == 15, "summary: due, with the window's snapshots");
    check(ps.step(61000, 25, &seconds, &snaps) == 2 && seconds == 30 && snaps == 0, "summary: due, the node was not read at all");

    // A 65th provider is only a number: 64 listed, one extra; the table and the count agree.
    static ohp::ProviderTable t65;
    for (int i = 0; i < 80; ++i) g.setProvider(i, 0x4, 0x0);
    std::initializer_list<int> none = {};
    g.setList(none);
    {
        // The 65-entry list, built by hand: setList takes an initializer list, the array is filled directly.
        for (int i = 0; i < 65; ++i) g.provArray[i] = addr(g.providers[i]);
        put(g.provList, 0, addr(g.provArray));
        put<uint32_t>(g.provList, ohp::kListNumber, 65);
    }
    g.sync();
    ohp::ProviderSnap big;
    check(ohp::readProviders(node, &plainRead, nullptr, &big) == ohp::kSnapOk && big.number == 65 && big.stored == 64 && big.extras == 1,
          "providers: the 65th is not listed, it is counted");
    check(t65.take(ohp::kSnapOk, big, ohp::kPlaceStereo) && t65.extras() == 1 && findRow(t65, addr(g.providers[63])) && !findRow(t65, addr(g.providers[64])),
          "table: the 65th provider has no row");
    std::snprintf(want, sizeof(want), "; 65 provider(s), 64 listed, 1 extra not listed");
    check(ohp::formatProviderHead(big, line, sizeof(line)) > 0 && std::strstr(line, want), "text: the head says how many were not listed");
    // Mask for the 65th: its answer counts in the OR (read, not listed): a 65th that answers 0x8 makes the mask 0xC, and the snapshot matches.
    g.setProvider(64, 0x4, 0x8);
    g.sync();
    ohp::ProviderSnap bigHud;
    check(ohp::readProviders(node, &plainRead, nullptr, &bigHud) == ohp::kSnapOk && bigHud.mask == 0xC && bigHud.extras == 1,
          "providers: the answers of the extra providers count in the OR");

    // More providers than the table has rows, and a third view: counted, not stored.
    static ohp::ProviderTable full;
    for (int i = 0; i < 80; ++i) g.setProvider(i, 0x4, 0x0);
    g.setList(none);
    for (int i = 0; i < 64; ++i) g.provArray[i] = addr(g.providers[i]);
    put<uint32_t>(g.provList, ohp::kListNumber, 64);
    g.sync();
    ohp::ProviderSnap one;
    ohp::readProviders(node, &plainRead, nullptr, &one);
    put<int32_t>(g.view, ohp::kViewBitIndex, 1);
    ohp::readProviders(node, &plainRead, nullptr, &one);
    full.take(ohp::kSnapOk, one, ohp::kPlaceStereo);
    put<int32_t>(g.view, ohp::kViewBitIndex, 2);
    ohp::readProviders(node, &plainRead, nullptr, &one);
    full.take(ohp::kSnapOk, one, ohp::kPlaceStereo);
    check(full.rowsFull() == 0 && full.viewBitOf(1) == 2 && findRow(full, addr(g.providers[5]), 0) && findRow(full, addr(g.providers[5]), 1),
          "table: 64 providers in each of two views fill the rows, the same provider has a row per view");
    for (int i = 0; i < 64; ++i) g.provArray[i] = addr(g.providers[16 + i]);  // 16 new providers (64-79)
    g.sync();
    ohp::readProviders(node, &plainRead, nullptr, &one);
    full.take(ohp::kSnapOk, one, ohp::kPlaceStereo);
    check(full.rowsFull() == 16, "table: providers past the rows are counted, not stored");
    put<int32_t>(g.view, ohp::kViewBitIndex, 3);
    ohp::readProviders(node, &plainRead, nullptr, &one);
    check(!full.take(ohp::kSnapOk, one, ohp::kPlaceStereo) && full.viewsFull() == 1, "table: a third view is counted, not stored");
    put<int32_t>(g.view, ohp::kViewBitIndex, 1);

    // The ring to the frame thread: four records, the fifth is lost and counted.
    static ohp::Ring<4, ohp::ProviderSnap> ring;
    ohp::ProviderSnap rec;
    for (int i = 0; i < 5; ++i) {
        rec = s;
        rec.number = static_cast<uint32_t>(i);
        ring.push(rec);
    }
    ohp::ProviderSnap got;
    int popped = 0;
    bool inOrder = true;
    while (ring.pop(&got)) inOrder = inOrder && got.number == static_cast<uint32_t>(popped++) && got.p[0].addr == s.p[0].addr;
    check(popped == 4 && inOrder && ring.dropped() == 1, "ring: four records in order, the overflow counted");
}

// ---- Part B: the real glue on a synthetic function -------------------------------------------------------------------------------

FakeGame g_game;
void (*g_draw)(void*) = nullptr;  // the hooked synthetic node draw, for Part B15
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
    g_draw = draw;

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

    // Recording, but only nodes of other kinds so far: the census collects them and is written once 20 s after the first drain.
    onFootHudProbeSetRecording(true);
    put(g_game.node, 0, addr(g_game.vtable) + 8);
    draw(g_game.node);
    draw(g_game.node);
    put(g_game.node, 0, addr(g_game.vtable));
    g_game.setFirst(0, 3);
    draw(g_game.node);
    g_game.setFirst(0, 4);
    g_lines.clear();
    onFootHudProbeDrain(50000, 0, &sink, nullptr);
    check(g_lines.empty() && g_originalCalls == 3 && onFootHudProbeCalls() == 0, "glue census: other kinds forwarded, nothing said yet");
    onFootHudProbeDrain(69999, 0, &sink, nullptr);
    check(g_lines.empty(), "glue census: not before 20 s");
    onFootHudProbeDrain(70000, 0, &sink, nullptr);
    check(countLines("no PostGUI node") == 1 && anyLine("2 kind(s)") && anyLine("census: vtable +0x52ED2C8, first list 0, 4 list(s): 2 draw(s)") &&
              anyLine("census: vtable +0x52ED2C0 (the list-range node's), first list 0, 3 list(s): 1 draw(s)"),
          "glue census: written once, the kinds seen");
    g_lines.clear();
    onFootHudProbeDrain(95000, 0, &sink, nullptr);
    check(countLines("census") == 0, "glue census: only once");
    g_originalCalls = 0;

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
    check(anyLine("tracking hud 36-39 node 0x") && anyLine("of view bit 1") && !anyLine("watch on"),
          "glue: the hud node drawn is tracked, no watch from the node drain (.16: the camera drain's)");
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
    onFootHudProbeDrain(125000, 0, &sink, nullptr);  // 30 s after the summary the 95000 drain wrote
    check(anyLine("on-foot hud probe: 30 s: 6 node draw(s) in all; PostGUI: 1 node(s), 5 call(s)") && anyLine("2 unreadable"),
          "glue: the summary");
}

// ---- Part B, .13: the graph hooks on synthetic functions --------------------------------------------------------------------------

FakeGraphs g_graphs;
volatile uint64_t g_walkSystem = 0, g_walkRun = 0, g_addGraph = 0, g_addNode = 0;
volatile int g_walkCalls = 0, g_addCalls = 0, g_removeCalls = 0;
bool g_walkFlip = false;

// The game's walk for the rig: it may switch the node off while it runs (the hook must have read it before).
void fakeWalk(uint8_t* system, uint64_t run) {
    g_walkSystem = addr(system);
    g_walkRun = run;
    ++g_walkCalls;
    if (g_walkFlip) put<uint8_t>(g_game.node, ohp::kNodeOn, 0);
}
// The game's add and remove: append the node to the graph's list, take it out.
void fakeAdd(uint8_t* graph, uint64_t node) {
    g_addGraph = addr(graph);
    g_addNode = node;
    ++g_addCalls;
    uint64_t count = 0, list = 0;
    std::memcpy(&count, graph + ohp::kGraphNodeCount, 8);
    std::memcpy(&list, graph + ohp::kGraphNodes, 8);
    reinterpret_cast<uint64_t*>(list)[count] = node;
    put<uint64_t>(graph, ohp::kGraphNodeCount, count + 1);
}
void fakeRemove(uint8_t* graph, uint64_t node) {
    ++g_removeCalls;
    uint64_t count = 0, list = 0;
    std::memcpy(&count, graph + ohp::kGraphNodeCount, 8);
    std::memcpy(&list, graph + ohp::kGraphNodes, 8);
    auto* nodes = reinterpret_cast<uint64_t*>(list);
    uint64_t kept = 0;
    for (uint64_t i = 0; i < count; ++i)
        if (nodes[i] != node) nodes[kept++] = nodes[i];
    put<uint64_t>(graph, ohp::kGraphNodeCount, kept);
}

// Each point's prologue in full, its undoing, then mov rax, imm64; jmp rax into the stand-in with the arguments and the return address
// as the caller left them.
size_t writeTail(uint8_t* at, size_t n, const void* target) {
    at[n++] = 0x48;
    at[n++] = 0xB8;
    const uint64_t t = addr(target);
    std::memcpy(at + n, &t, 8);
    n += 8;
    at[n++] = 0xFF;
    at[n++] = 0xE0;
    return n;
}
void writeWalk(uint8_t* at) {
    // sub rsp,48h; mov rax,[rcx+20h]; mov [rsp+60h],rbx; mov rbx,[rcx+28h]; then mov rbx,[rsp+60h]; add rsp,48h
    const uint8_t code[] = {0x48, 0x83, 0xEC, 0x48, 0x48, 0x8B, 0x41, 0x20, 0x48, 0x89, 0x5C, 0x24, 0x60, 0x48, 0x8B, 0x59, 0x28,
                            0x48, 0x8B, 0x5C, 0x24, 0x60, 0x48, 0x83, 0xC4, 0x48};
    std::memcpy(at, code, sizeof(code));
    writeTail(at, sizeof(code), reinterpret_cast<const void*>(&fakeWalk));
}
void writeAdd(uint8_t* at) {
    // mov [rsp+10h],rdx; sub rsp,28h; mov r8,[rcx+28h]; add rcx,18h; then sub rcx,18h; add rsp,28h
    const uint8_t code[] = {0x48, 0x89, 0x54, 0x24, 0x10, 0x48, 0x83, 0xEC, 0x28, 0x4C, 0x8B, 0x41, 0x28, 0x48, 0x83, 0xC1, 0x18,
                            0x48, 0x83, 0xE9, 0x18, 0x48, 0x83, 0xC4, 0x28};
    std::memcpy(at, code, sizeof(code));
    writeTail(at, sizeof(code), reinterpret_cast<const void*>(&fakeAdd));
}
void writeRemove(uint8_t* at) {
    // mov [rsp+10h],rbx; push rdi; sub rsp,20h; mov rax,[rcx+28h]; lea rdi,[rcx+18h]; then add rsp,20h; pop rdi
    const uint8_t code[] = {0x48, 0x89, 0x5C, 0x24, 0x10, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0x41, 0x28, 0x48, 0x8D, 0x79, 0x18,
                            0x48, 0x83, 0xC4, 0x20, 0x5F};
    std::memcpy(at, code, sizeof(code));
    writeTail(at, sizeof(code), reinterpret_cast<const void*>(&fakeRemove));
}

void partB13() {
    auto* page = static_cast<uint8_t*>(VirtualAlloc(nullptr, 4096, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE));
    if (!page) {
        check(false, "graph glue: a code page for the synthetic functions");
        return;
    }
    uint8_t* walkAt = page;
    uint8_t* addAt = page + 0x100;
    uint8_t* removeAt = page + 0x200;
    writeWalk(walkAt);
    writeAdd(addAt);
    writeRemove(removeAt);
    addAt[0] = 0x90;  // the add point's bytes are wrong at first
    FlushInstructionCache(GetCurrentProcess(), page, 4096);
    const uintptr_t base = g_game.base();

    g_lines.clear();
    const bool walkHooked = onFootHudProbeInstallPoint(ohp::kPointWalk, base, addr(walkAt), &sink, nullptr);
    const bool addHooked = onFootHudProbeInstallPoint(ohp::kPointAdd, base, addr(addAt), &sink, nullptr);
    const bool removeHooked = onFootHudProbeInstallPoint(ohp::kPointRemove, base, addr(removeAt), &sink, nullptr);
    check(walkHooked && !addHooked && removeHooked && g_lines.size() == 3 &&
              anyLine("hooked graph walk at EliteDangerous64.exe+0x6BF980") && anyLine("8 moved") &&
              anyLine("NOT installed: the graph add at EliteDangerous64.exe+0x6BFC60 does not begin") &&
              anyLine("hooked graph remove at EliteDangerous64.exe+0x6BFE60") && addAt[0] == 0x90 && walkAt[0] == 0xE9 && removeAt[0] == 0xE9,
          "graph glue: one point's wrong bytes refuse only it, the others are hooked");
    addAt[0] = 0x48;
    FlushInstructionCache(GetCurrentProcess(), addAt, 1);
    g_lines.clear();
    check(onFootHudProbeInstallPoint(ohp::kPointAdd, base, addr(addAt), &sink, nullptr) && anyLine("hooked graph add") && anyLine("5 moved"),
          "graph glue: the add point, right bytes, hooked");

    using FnWalk = void (*)(void*, uint64_t);
    using Fn2 = void (*)(void*, void*);
    auto walk = reinterpret_cast<FnWalk>(walkAt);
    auto add = reinterpret_cast<Fn2>(addAt);
    auto remove = reinterpret_cast<Fn2>(removeAt);

    // The tracked hud node (view bit 1, from Part B's draws) in graph 0 of a two-graph system, on.
    g_graphs.build(2);
    g_graphs.add(0, addr(g_game.node));
    put<uint8_t>(g_game.node, ohp::kNodeOn, 1);
    onFootHudProbeSetRecording(true);
    g_lines.clear();
    onFootHudProbeDrain(130000, 0, &sink, nullptr);

    // The state is read BEFORE the original: the stand-in switches the node off while it runs.
    g_walkFlip = true;
    walk(g_graphs.system, 1);
    g_walkFlip = false;
    check(g_walkCalls == 1 && g_walkSystem == addr(g_graphs.system) && g_walkRun == 1, "graph glue: the walk forwarded unchanged");
    g_lines.clear();
    onFootHudProbeDrain(130100, 0, &sink, nullptr);
    char want[200];
    std::snprintf(want, sizeof(want), "is in graph 0x%llX", static_cast<unsigned long long>(addr(g_graphs.graph[0])));
    check(anyLine(want) && countLines("state hud 36-39 view bit 1: on: in an enabled graph") == 1 && anyLine("on-foot=no"),
          "graph glue: in its graph and on, as the walk found it on its way in");
    check(!anyLine("watch") && !anyLine("writer #"), "graph glue: the graph's side puts no watch (.16 gave the slots to the camera drain)");

    walk(g_graphs.system, 1);
    g_lines.clear();
    onFootHudProbeDrain(130200, 1, &sink, nullptr);
    check(countLines("state hud 36-39 view bit 1: node-off (H1)") == 1 && anyLine("flag=0") && anyLine("on-foot=yes"), "graph glue: H1");

    put<uint8_t>(g_game.node, ohp::kNodeOn, 1);
    put<uint8_t>(g_graphs.graph[0], ohp::kGraphOn, 0);
    walk(g_graphs.system, 1);
    g_lines.clear();
    onFootHudProbeDrain(130300, 1, &sink, nullptr);
    check(countLines("state hud 36-39 view bit 1: graph-off (H3)") == 1 && anyLine("on=0"), "graph glue: H3");

    put<uint8_t>(g_graphs.graph[0], ohp::kGraphOn, 1);
    g_graphs.clear(0);
    walk(g_graphs.system, 1);
    g_lines.clear();
    onFootHudProbeDrain(130400, 1, &sink, nullptr);
    check(countLines("state hud 36-39 view bit 1: not-in-graph (H2)") == 1, "graph glue: H2");

    g_graphs.add(0, addr(g_game.node));
    put<uint64_t>(g_graphs.system, ohp::kSysGraphCount, 0);  // the walk visits no graph
    walk(g_graphs.system, 1);
    g_lines.clear();
    onFootHudProbeDrain(130500, 1, &sink, nullptr);
    check(countLines("state hud 36-39 view bit 1: graph-not-walked") == 1, "graph glue: still in its graph, not walked");
    put<uint64_t>(g_graphs.system, ohp::kSysGraphCount, 2);
    walk(g_graphs.system, 0);  // a walk that calls no node
    g_lines.clear();
    onFootHudProbeDrain(130600, 1, &sink, nullptr);
    onFootHudProbeDrain(130700, 1, &sink, nullptr);  // and a frame with no walk
    check(countLines("state ") == 0 && g_walkCalls == 6 && g_walkRun == 0, "graph glue: no change, no walk: no state line");

    // Add and remove: after the original, with the game's callers; a node of another kind only counted; off, only forwarded.
    g_addCalls = 0;
    add(g_graphs.graph[1], g_game.node);
    remove(g_graphs.graph[1], g_game.node);
    add(g_graphs.graph[1], g_graphs.node[3]);  // no vtable: not a PostGUI node
    g_lines.clear();
    onFootHudProbeDrain(130800, 1, &sink, nullptr);
    std::snprintf(want, sizeof(want), "graph add: node 0x%llX (hud 36-39) graph 0x%llX, 1 node(s) after, on-foot=yes, thread ",
                  static_cast<unsigned long long>(addr(g_game.node)), static_cast<unsigned long long>(addr(g_graphs.graph[1])));
    check(anyLine(want) && anyLine("graph remove: node 0x") && anyLine(", 0 node(s) after") && countLines("graph add") == 1 && anyLine(", from "),
          "graph glue: the add and the remove, with the count after the original");
    check(g_addCalls == 2 && g_removeCalls == 1 && g_addGraph == addr(g_graphs.graph[1]) && g_addNode == addr(g_graphs.node[3]),
          "graph glue: add and remove forwarded unchanged");
    onFootHudProbeSetRecording(false);
    add(g_graphs.graph[2], g_game.node);
    g_lines.clear();
    onFootHudProbeDrain(130900, 1, &sink, nullptr);
    check(g_addCalls == 3 && countLines("graph add") == 0, "graph glue: off, only forwarded");

    // The graph's 30-second line (its window began at Part B's first drain, 50000; the last one was written at 125000).
    onFootHudProbeSetRecording(true);
    g_lines.clear();
    onFootHudProbeDrain(155000, 1, &sink, nullptr);
    check(anyLine("30 s graph: 5 node-calling walk(s), 1 without nodes") && anyLine("hud 36-39 bit 1 graph-not-walked") &&
              anyLine("graph events: 2 PostGUI, 1 other"),
          "graph glue: the graph's summary");
}

// ---- Part B, .15: the pass switch on a synthetic function, the camera drain ---------------------------------------------------------

volatile uint64_t g_switchPass = 0, g_switchOn = 0;
volatile int g_switchCalls = 0;

uint64_t fakeSwitch(uint8_t* pass, uint64_t on) {
    g_switchPass = addr(pass);
    g_switchOn = on;
    ++g_switchCalls;
    return 0x5A5A;
}
// The switch's 16 bytes (push rbx; sub rsp,20h; mov rbx,[rcx+2E0h]; cmp [rbx+70h],dl), undone (add rsp,20h; pop rbx), then the jump
// into the stand-in.
void writeSwitch(uint8_t* at) {
    const uint8_t code[] = {0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0x99, 0xE0, 0x02, 0x00, 0x00, 0x38, 0x53, 0x70,
                            0x48, 0x83, 0xC4, 0x20, 0x5B};
    std::memcpy(at, code, sizeof(code));
    writeTail(at, sizeof(code), reinterpret_cast<const void*>(&fakeSwitch));
}

// A pass of the game: its view, its main camera here, its PostGUI camera wherever the rig says, and its PostGUI feature (the switch's
// first instructions read pass+0x2E0 and that one's +0x70).
struct FakePass {
    uint8_t pass[0x340] = {};
    uint8_t feature[0x80] = {};
    uint8_t main[0x300] = {};
    void build(int32_t view, uint64_t postCamera) {
        put<int32_t>(pass, ohp::kPassView, view);
        put(pass, ohp::kPassMainCamera, addr(main));
        put(pass, ohp::kPassPostGuiCamera, postCamera);
        put(pass, 0x2E0, addr(feature));
    }
};
void setCamera(uint8_t* cam, float yawDeg, float x = 0.f, float y = 0.f, float z = 0.f) {
    float m[16];
    cameraBlock(m, yawDeg, 0.f, 0.f, x, y, z);
    std::memcpy(cam + ohp::kCamPose, m, sizeof(m));
}
// .19: the stores that change a subscription, each from its own place in the rig (a site is an instruction): Part C's two and Part B15's.
// Each leaves its own mark after the store: the linker folds identical functions into one, and a site is an instruction.
volatile int g_mark = 0;
__declspec(noinline) void writeSub(volatile uint64_t* p, uint64_t v) {
    *p = v;
    g_mark = 1;
}
__declspec(noinline) void writeSubAlt(volatile uint64_t* p, uint64_t v) {
    *p = v;
    g_mark = 2;
}
__declspec(noinline) void writeSubGame(volatile uint64_t* p, uint64_t v) {
    *p = v;
    g_mark = 3;
}
// Yaw steps of 10 deg: three up, three down, over and over.
float triangle(int i) {
    const int k = i % 6;
    return 10.f * static_cast<float>(k < 3 ? k : 6 - k);
}

void partB15() {
    auto* page = static_cast<uint8_t*>(VirtualAlloc(nullptr, 4096, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE));
    void* noAccess = VirtualAlloc(nullptr, 4096, MEM_RESERVE | MEM_COMMIT, PAGE_NOACCESS);
    if (!page || !noAccess || !g_draw) {
        check(false, "pass glue: a code page, a page that cannot be read, the hooked node draw");
        return;
    }
    writeSwitch(page);
    page[2] = 0x90;  // the switch's bytes are wrong at first
    FlushInstructionCache(GetCurrentProcess(), page, 4096);
    const uintptr_t base = g_game.base();
    g_lines.clear();
    check(!onFootHudProbeInstallPoint(ohp::kPointPass, base, addr(page), &sink, nullptr) && g_lines.size() == 1 &&
              anyLine("NOT installed: the pass switch at EliteDangerous64.exe+0x28AD4A0 does not begin") && anyLine("the others stand") &&
              page[0] == 0x40 && page[2] == 0x90,
          "pass glue: wrong bytes, one line, untouched");
    page[2] = 0x48;
    FlushInstructionCache(GetCurrentProcess(), page, 4096);
    g_lines.clear();
    check(onFootHudProbeInstallPoint(ohp::kPointPass, base, addr(page), &sink, nullptr) && g_lines.size() == 1 &&
              anyLine("hooked pass switch at EliteDangerous64.exe+0x28AD4A0") && anyLine("6 moved") && page[0] == 0xE9,
          "pass glue: the right bytes are hooked, six moved");

    using FnSwitch = uint64_t (*)(void*, uint64_t);
    auto sw = reinterpret_cast<FnSwitch>(page);
    static FakePass a, b;
    a.build(0, addr(g_game.cam));  // view 0's PostGUI camera is the one the HUD node is drawn with
    b.build(1, addr(noAccess));    // view 1's cannot be read
    setCamera(a.main, 30.f, 1.f, 2.f, 3.f);
    setCamera(b.main, 0.f);
    setCamera(g_game.cam, 0.f);
    float head[12];
    headBlock(head, 10.f, 0.f);

    // Off: only forwarded -- the whole register as the caller left it, the original's result back -- and nothing noted.
    onFootHudProbeSetRecording(false);
    const uint64_t result = sw(a.pass, 0x100000001ull);
    check(result == 0x5A5A && g_switchCalls == 1 && g_switchPass == addr(a.pass) && g_switchOn == 0x100000001ull,
          "pass glue: off, forwarded unchanged with the original's result");
    onFootHudProbeSetRecording(true);
    const uint64_t t0 = 200000;
    g_lines.clear();
    onFootHudProbeCameraDrain(t0, 1, 1, head, &sink, nullptr);
    check(countLines("new pass") == 0 && countLines("pose view") == 0, "pass glue: a call while off is not noted");
    // .19: Part B's draws of the HUD node read an empty provider list, and a matched one: its first list is written now.
    check(g_lines.size() == 1 && anyLine("providers of the HUD node: view bit 1, in the ship, node 0x") &&
              anyLine("camera bit (cam+0x8E0) 0x0, constant lists (cam+0x8F0) 0x4, frame mask (ctx+0x270) 0x4; 0 provider(s), 0 listed, 0 extra not listed"),
          "pass glue .19: the first list of the HUD node, empty, the mask the camera's constant lists");

    // On: each pass noted and said once; the pose lines wait for the half second.
    sw(a.pass, 1);
    sw(b.pass, 1);
    g_lines.clear();
    onFootHudProbeCameraDrain(t0 + 100, 1, 1, head, &sink, nullptr);
    char want[600];
    std::snprintf(want, sizeof(want), "pass switch: new pass 0x%llX, view 0: main camera 0x%llX, PostGUI camera 0x%llX",
                  static_cast<unsigned long long>(addr(a.pass)), static_cast<unsigned long long>(addr(a.main)),
                  static_cast<unsigned long long>(addr(g_game.cam)));
    check(anyLine(want) && countLines("new pass") == 2 && countLines("pose view") == 0 && g_switchCalls == 3 && g_switchOn == 1,
          "pass glue: on, both passes noted, said and forwarded");
    check(!anyLine("watch on"), "pass glue .19: no provider has answered with 0x8, so no watch yet");

    // The half second: a pose line a pass. View 0's HUD node, drawn with its PostGUI camera, gives the mask. .19: its providers, as the
    // hypothesis has them in stereo: the one that gives _HUD in cinema (0) is not subscribed to the camera (bit 0x4), the others are
    // subscribed and silent.
    put<uint64_t>(g_game.cam, ohp::kCamMask8E0, 0x4);
    g_game.setProvider(0, 0x0, 0x0);
    g_game.setProvider(1, 0x4, 0x0);
    g_game.setProvider(2, 0x4, 0x0);
    g_game.setList({0, 1, 2});
    g_game.sync();
    g_draw(g_game.node);
    onFootHudProbeDrain(t0 + 500, 1, &sink, nullptr);
    sw(a.pass, 1);
    sw(b.pass, 1);
    g_lines.clear();
    onFootHudProbeCameraDrain(t0 + 500, 1, 1, head, &sink, nullptr);
    std::snprintf(want, sizeof(want),
                  "pose view 0 stereo on-foot=yes pass 0x%llX: main 0x%llX yaw 30.0 pitch 0.0 roll 0.0 at (1.00,2.00,3.00); postgui 0x%llX yaw 0.0",
                  static_cast<unsigned long long>(addr(a.pass)), static_cast<unsigned long long>(addr(a.main)),
                  static_cast<unsigned long long>(addr(g_game.cam)));
    check(anyLine(want) && anyLine("flags250=0xF; postgui-main yaw -30.0; head yaw 10.0 pitch 0.0; hud mask 0x4"),
          "pass glue: view 0's pose line, the head, the mask of the node drawn with the PostGUI camera");
    std::snprintf(want, sizeof(want), "; postgui 0x%llX none; postgui-main none; head yaw 10.0 pitch 0.0; hud mask ?",
                  static_cast<unsigned long long>(addr(noAccess)));
    check(countLines("pose view") == 2 && anyLine("pose view 1 stereo on-foot=yes") && anyLine(want),
          "pass glue: view 1's PostGUI camera cannot be read: none, and no mask");

    // Thirty seconds on foot in stereo, the head turning and both cameras with it: both slopes 1; view 1 has no steps. .19: the HUD node
    // is drawn with view 0's PostGUI camera every step, and its providers give no _HUD (the one that does in cinema is not subscribed):
    // the list is written once (at its 5 seconds), the 30-second summary says so, and says that no provider ever gave 0x8; no watch.
    uint64_t t = t0 + 500;
    int i = 0;
    auto provider = [&](int k) { return addr(g_game.providers[k]); };
    while (t < t0 + 30000) {
        ++i;
        t += 500;
        const float yaw = 30.f + triangle(i);
        setCamera(a.main, yaw, 1.f, 2.f, 3.f);
        setCamera(g_game.cam, yaw - 30.f);
        headBlock(head, yaw - 20.f, 0.f);
        sw(a.pass, 1);
        sw(b.pass, 1);
        g_draw(g_game.node);
        onFootHudProbeDrain(t, 1, &sink, nullptr);
        if (t == t0 + 30000) {
            std::snprintf(want, sizeof(want),
                          "providers of the HUD node: view bit 1, on foot in stereo, node 0x%llX camera 0x%llX: camera bit (cam+0x8E0) 0x4, constant "
                          "lists (cam+0x8F0) 0x4, frame mask (ctx+0x270) 0x4; 3 provider(s), 3 listed, 0 extra not listed",
                          static_cast<unsigned long long>(addr(g_game.node)), static_cast<unsigned long long>(addr(g_game.cam)));
            check(countLines(want) == 1, "pass glue .19: the list in stereo, written once");
            std::snprintf(want, sizeof(want),
                          "provider #0 0x%llX vtable EliteDangerous64.exe+0x%llX answer method EliteDangerous64.exe+0x%llX +0x28=0x0 subscribed=no "
                          "+0x30=0x0 _HUD=no",
                          static_cast<unsigned long long>(provider(0)), static_cast<unsigned long long>(addr(g_game.vts[0]) - base),
                          static_cast<unsigned long long>(g_game.method(0) - base));
            check(anyLine(want) && anyLine("provider #1 0x") && anyLine("+0x28=0x4 subscribed=yes +0x30=0x0 _HUD=no"),
                  "pass glue .19: the provider that gives _HUD in cinema is not subscribed in stereo");
            g_lines.clear();
        }
        onFootHudProbeCameraDrain(t, 1, 1, head, &sink, nullptr);
    }
    check(anyLine("30 s follow view 0 stereo: postgui/main k=1.00 (main turned 590 deg); main/head k=1.00 (head turned 590 deg); 59 step(s), 0 left "
                  "out") &&
              countLines("follow view 1") == 0,
          "pass glue: 30 s, the PostGUI camera turning with the main one, the main one with the head: 1 and 1");
    check(anyLine("30 s pass switch: 120 call(s); 2 pass(es) now: view 0 0x") && anyLine(", view 1 0x") &&
              anyLine("2 new, 0 retired, 0 did not fit; 120 pose line(s)"),
          "pass glue: the switch's 30-second line");
    check(anyLine("30 s providers of the HUD node: 60 snapshot(s) of the HUD node in these seconds; since recording began ") &&
              anyLine(" matched, 0 not matched (0 another node's context, 0 OR of the answers != frame mask), 2 unreadable; ring full 0 time(s); 0 "
                      "extra provider(s) not listed, 0 did not fit the table, 0 view(s) did not fit"),
          "pass glue .19: the 30-second summary, matched against not, the two unreadable draws of Part B");
    std::snprintf(want, sizeof(want),
                  "provider 0x%llX view bit 1 vtable EliteDangerous64.exe+0x%llX answer method EliteDangerous64.exe+0x%llX: frames/subscribed/_HUD ship "
                  "0/0/0, stereo 60/60/0, cinema 0/0/0; last +0x28 0x4, last +0x30 0x0",
                  static_cast<unsigned long long>(provider(1)), static_cast<unsigned long long>(addr(g_game.vts[1]) - base),
                  static_cast<unsigned long long>(g_game.method(1) - base));
    check(anyLine(want) && countLines("provider 0x") == 2 && anyLine("no provider of the HUD node has answered with the _HUD bit 0x8") &&
              !anyLine("watch on"),
          "pass glue .19: the summary's rows are the subscribed providers, and says no provider ever gave 0x8; no watch armed");

    // Thirty seconds in cinema, the PostGUI camera still, no head pose, view 1's pass no longer called: 0, and that pass retired. .19:
    // the provider that gives _HUD is subscribed now: the frame mask has 0x8, the first matched snapshot with it arms the watch on its
    // subscription (view 0's, the pass's), and the game rewrites the subscription at every step, with the camera's bit.
    onFootHudProbeSetPlace(ohp::kPlaceCinema);
    g_game.setProvider(0, 0x4, 0x8);
    g_game.sync();
    const uint64_t sub0 = provider(0) + ohp::kProvSub;
    for (int n = 0; n < 60; ++n) {
        ++i;
        t += 500;
        setCamera(a.main, 30.f + triangle(i), 1.f, 2.f, 3.f);
        sw(a.pass, 1);
        g_draw(g_game.node);
        onFootHudProbeDrain(t, 1, &sink, nullptr);
        if (t == t0 + 60000) {
            std::snprintf(want, sizeof(want),
                          "providers of the HUD node: view bit 1, on foot in cinema, node 0x%llX camera 0x%llX: camera bit (cam+0x8E0) 0x4, constant "
                          "lists (cam+0x8F0) 0x4, frame mask (ctx+0x270) 0xC; 3 provider(s), 3 listed, 0 extra not listed",
                          static_cast<unsigned long long>(addr(g_game.node)), static_cast<unsigned long long>(addr(g_game.cam)));
            check(countLines(want) == 1 && anyLine("+0x28=0x4 subscribed=yes +0x30=0x8 _HUD=yes"), "pass glue .19: the list in cinema, the provider subscribed");
            std::snprintf(want, sizeof(want), "watch on: hud provider subscription 0x%llX (0x%llX+0x28) on debug slot 2, write, 4 bytes: ",
                          static_cast<unsigned long long>(sub0), static_cast<unsigned long long>(provider(0)));
            check(anyLine(want) && anyLine("; the first provider with 0x8, of view bit 1, the pass's view 0") && countLines("watch on") == 1,
                  "pass glue .19: the first provider with 0x8 watched on its subscription, view 0's");
            std::snprintf(want, sizeof(want), "writer #3 of the hud provider subscription of 0x%llX: at ", static_cast<unsigned long long>(provider(0)));
            check(countLines(want) == 1 && anyLine("(after the write), value 0x4 (with the HUD camera bit), on foot in cinema, thread "),
                  "pass glue .19: one writer line, its value, the camera bit and the place");
            g_lines.clear();
        }
        onFootHudProbeCameraDrain(t, 1, 0, nullptr, &sink, nullptr);
        writeSubGame(g_game.subOf(0), 0x4);
    }
    check(anyLine("30 s follow view 0 cinema: postgui/main k=0.00 (main turned 590 deg); main/head no head pose; 59 step(s)") &&
              countLines("follow view 0 stereo") == 0 && anyLine("pose view 0 cinema") && anyLine("head none"),
          "pass glue: 30 s in cinema, the PostGUI camera still: 0; the mode change makes no step");
    check(anyLine("30 s pass switch: 60 call(s); 1 pass(es) now: view 0 0x") && anyLine("0 new, 1 retired, 0 did not fit; 62 pose line(s)"),
          "pass glue: a pass not called for 2 s is retired");
    std::snprintf(want, sizeof(want),
                  "provider 0x%llX view bit 1 vtable EliteDangerous64.exe+0x%llX answer method EliteDangerous64.exe+0x%llX: frames/subscribed/_HUD ship "
                  "0/0/0, stereo 60/0/0, cinema 60/60/60; last +0x28 0x4, last +0x30 0x8",
                  static_cast<unsigned long long>(provider(0)), static_cast<unsigned long long>(addr(g_game.vts[0]) - base),
                  static_cast<unsigned long long>(g_game.method(0) - base));
    check(anyLine("30 s providers of the HUD node: 60 snapshot(s) of the HUD node in these seconds") && anyLine(want) &&
              !anyLine("no provider of the HUD node has answered"),
          "pass glue .19: the summary after cinema: the provider is subscribed and gives _HUD there and is not subscribed in stereo");
    sw(b.pass, 1);
    g_lines.clear();
    onFootHudProbeCameraDrain(t + 100, 1, 0, nullptr, &sink, nullptr);
    std::snprintf(want, sizeof(want), "new pass 0x%llX, view 1", static_cast<unsigned long long>(addr(b.pass)));
    check(anyLine(want), "pass glue: a retired pass called again is new again");

    // The watch's own 30 seconds (armed at the first cinema step, t0 + 30500): 60 writes of one instruction, all in cinema, all with the
    // camera's bit; the sites of Part C came before it, so this one is the third.
    g_lines.clear();
    onFootHudProbeCameraDrain(t + 500, 1, 0, nullptr, &sink, nullptr);
    std::snprintf(want, sizeof(want),
                  "30 s watch hud provider subscription 0x%llX: 60 write(s); #3 ship 0, stereo 0, cinema 60 (with the camera bit 0, 0, 60), last 0x4; 0 "
                  "from sites that did not fit",
                  static_cast<unsigned long long>(sub0));
    check(anyLine(want), "pass glue .19: the watch's 30-second line, the writes by place with the camera bit");

    // The watched provider leaves its view's list (a rebuilt scene) and another that gives _HUD is in it: after 2 s the watch moves.
    t += 500;
    g_game.setProvider(3, 0x4, 0x8);
    g_game.setList({1, 2, 3});
    g_game.sync();
    g_draw(g_game.node);
    onFootHudProbeCameraDrain(t + 500, 1, 0, nullptr, &sink, nullptr);
    check(onFootHudProbeWatchOn(ohp::kWatchSubscription) && onFootHudProbeWatchAddress(ohp::kWatchSubscription) == sub0,
          "pass glue .19: the provider is out of the list for 0.5 s: the watch stays");
    const uint64_t sub3 = provider(3) + ohp::kProvSub;
    g_lines.clear();
    g_draw(g_game.node);  // the node keeps being read without it: that is what measures its absence
    onFootHudProbeCameraDrain(t + 2200, 1, 0, nullptr, &sink, nullptr);
    std::snprintf(want, sizeof(want), "watch moves: hud provider subscription 0x%llX -> 0x%llX (the watched provider left its view's list for 2 s; ",
                  static_cast<unsigned long long>(sub0), static_cast<unsigned long long>(sub3));
    check(anyLine(want) && anyLine("its provider left its view's list") && anyLine("watch on: hud provider subscription") &&
              onFootHudProbeWatchAddress(ohp::kWatchSubscription) == sub3,
          "pass glue .19: a provider that left its list moves the watch to the next one with 0x8");

    // No provider with 0x8 is left in the list: after 2 s the watch comes off with a line.
    t += 3000;
    g_game.setList({1, 2});
    g_game.sync();
    g_draw(g_game.node);
    onFootHudProbeCameraDrain(t, 1, 0, nullptr, &sink, nullptr);
    check(onFootHudProbeWatchOn(ohp::kWatchSubscription), "pass glue .19: the provider is out of the list for 0.8 s: the watch stays");
    // A loading screen: the node is not drawn for 5 s, so nothing is measured and the watch stays.
    onFootHudProbeCameraDrain(t + 5000, 1, 0, nullptr, &sink, nullptr);
    check(onFootHudProbeWatchOn(ohp::kWatchSubscription) && onFootHudProbeWatchAddress(ohp::kWatchSubscription) == sub3,
          "pass glue .19: the HUD node not drawn for 5 s (a loading screen): the watch stays");
    g_draw(g_game.node);
    onFootHudProbeCameraDrain(t + 5500, 1, 0, nullptr, &sink, nullptr);
    g_lines.clear();
    g_draw(g_game.node);
    onFootHudProbeCameraDrain(t + 7600, 1, 0, nullptr, &sink, nullptr);
    std::snprintf(want, sizeof(want), "watch off: hud provider subscription 0x%llX: the provider has been out of its view's list for 2 s", static_cast<unsigned long long>(sub3));
    check(anyLine(want) && !onFootHudProbeWatchOn(ohp::kWatchSubscription), "pass glue .19: the provider gone for 2 s of draws takes the watch off with a line");

    // It is back: the watch is armed again on the first provider with 0x8, and the key off takes it off with the totals.
    t += 8000;
    g_game.setList({0, 1, 2});
    g_game.sync();
    g_draw(g_game.node);
    g_lines.clear();
    onFootHudProbeCameraDrain(t, 1, 0, nullptr, &sink, nullptr);
    check(onFootHudProbeWatchAddress(ohp::kWatchSubscription) == sub0 && countLines("watch on: hud provider subscription") == 1,
          "pass glue .19: the first provider with 0x8 is back and watched again");
    onFootHudProbeSetRecording(false);
    g_lines.clear();
    onFootHudProbeStopWatches("the key is off", &sink, nullptr);
    check(anyLine("watch off: hud provider subscription") && countLines("the key is off") == 1 && !onFootHudProbeWatchOn(ohp::kWatchSubscription),
          "pass glue .19: the key off takes the watch off");
    writeSubGame(g_game.subOf(0), 0x0);
    g_lines.clear();
    onFootHudProbeWatchDrain(ohp::kWatchSubscription, &sink, nullptr);
    check(g_lines.empty(), "pass glue .19: no writer after the watch is off");

    // The fuse: recording again, armed again, then more than 100 000 writes in the window: the watch is off for good, with a line.
    onFootHudProbeSetRecording(true);
    t += 500;
    g_draw(g_game.node);
    g_lines.clear();
    onFootHudProbeCameraDrain(t, 1, 0, nullptr, &sink, nullptr);
    check(onFootHudProbeWatchOn(ohp::kWatchSubscription), "pass glue .19: recording again, the watch is armed again");
    for (int k = 0; k < 100001; ++k) writeSubGame(g_game.subOf(0), 0x4);
    g_lines.clear();
    onFootHudProbeCameraDrain(t + 500, 1, 0, nullptr, &sink, nullptr);
    check(anyLine("over 100000 hits in 30 s: the place is written all the time; the watch stays off") && !onFootHudProbeWatchOn(ohp::kWatchSubscription),
          "pass glue .19: the fuse takes the watch off after 100 000 writes");
    t += 1000;
    g_draw(g_game.node);
    g_lines.clear();
    onFootHudProbeCameraDrain(t + 500, 1, 0, nullptr, &sink, nullptr);
    check(!anyLine("watch on") && !onFootHudProbeWatchOn(ohp::kWatchSubscription), "pass glue .19: a tripped fuse is not re-armed");
    onFootHudProbeSetRecording(false);
}

// ---- Part C: the write watch, real, on this rig's threads ---------------------------------------------------------------------------

HANDLE g_busyGo = nullptr;
DWORD WINAPI busyThread(void*) {
    WaitForSingleObject(g_busyGo, 30000);
    return 0;
}

void partC() {
    alignas(16) static uint64_t sub[2] = {0x4};  // a provider's subscription: the watched place is its low four bytes
    alignas(16) static uint8_t other[16] = {};
    const uint64_t subAt = addr(sub);
    // A thread that already holds slot 2 for someone else.
    g_busyGo = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    HANDLE busy = CreateThread(nullptr, 0, &busyThread, nullptr, 0, nullptr);
    bool held = false;
    if (busy && SuspendThread(busy) != static_cast<DWORD>(-1)) {
        CONTEXT c{};
        c.ContextFlags = CONTEXT_DEBUG_REGISTERS;
        if (GetThreadContext(busy, &c)) {
            c.Dr2 = addr(other);
            c.Dr7 = (c.Dr7 & ~DWORD64(0xFFFFFFFFu)) | dr7::armSlotDr7(static_cast<uint32_t>(c.Dr7), 2);
            held = SetThreadContext(busy, &c) != 0;
        }
        ResumeThread(busy);
    }
    check(held, "watch: a thread with slot 2 taken");

    g_lines.clear();
    const int armed = onFootHudProbeWatchArm(ohp::kWatchSubscription, subAt, subAt - ohp::kProvSub, "the rig's provider", &sink, nullptr);
    check(armed > 0 && onFootHudProbeWatchOn(ohp::kWatchSubscription) && anyLine("watch on: hud provider subscription") && anyLine("on debug slot 2") &&
              anyLine("; the rig's provider") && !anyLine(" 0 skipped"),
          "watch: the subscription's armed on the rig's threads, the taken one skipped");
    if (busy) {
        CONTEXT c{};
        c.ContextFlags = CONTEXT_DEBUG_REGISTERS;
        SuspendThread(busy);
        check(GetThreadContext(busy, &c) && c.Dr2 == addr(other), "watch: the taken slot is left as it was");
        ResumeThread(busy);
    }
    g_lines.clear();
    check(onFootHudProbeWatchArm(ohp::kWatchSubscription, subAt, 0, nullptr, &sink, nullptr) == 0 && onFootHudProbeWatchArm(1, subAt, 0, nullptr, &sink, nullptr) == 0 &&
              g_lines.empty(),
          "watch: armed again or another watch: nothing");
    // The HUD nodes' camera bit is 0x4. On foot in stereo the subscription is written twice by one instruction with the bit; in cinema once
    // more by it; in the ship without the bit; then another instruction writes it in stereo without the bit.
    onFootHudProbeSetCameraBits(0x4);
    onFootHudProbeSetPlace(ohp::kPlaceStereo);
    writeSub(sub, 0x4);
    writeSub(sub, 0x4);
    onFootHudProbeSetPlace(ohp::kPlaceCinema);
    writeSub(sub, 0x4);
    onFootHudProbeSetPlace(ohp::kPlaceShip);
    writeSub(sub, 0x0);
    onFootHudProbeSetPlace(ohp::kPlaceStereo);
    writeSubAlt(sub, 0x0);
    g_lines.clear();
    onFootHudProbeWatchDrain(ohp::kWatchSubscription, &sink, nullptr);
    check(g_lines.size() == 2 && anyLine("writer #1 of the hud provider subscription of") && anyLine("value 0x4 (with the HUD camera bit), on foot in stereo") &&
              anyLine("writer #2 of the hud provider subscription of") && anyLine("value 0x0 (without the HUD camera bit), on foot in stereo"),
          "watch: one line per writer, with the value it wrote, the camera bit and where; the repeats only counted");
    g_lines.clear();
    onFootHudProbeWatchDisarm(ohp::kWatchSubscription, "the rig is done", &sink, nullptr);
    check(anyLine("watch off: hud provider subscription") && countLines("the rig is done; 2 site(s), 5 hit(s)") == 1 &&
              !onFootHudProbeWatchOn(ohp::kWatchSubscription),
          "watch: off, with its totals");
    writeSub(sub, 0x4);
    g_lines.clear();
    onFootHudProbeWatchDrain(ohp::kWatchSubscription, &sink, nullptr);
    check(g_lines.empty(), "watch: off, no trap");
    g_lines.clear();
    check(onFootHudProbeWatchArm(ohp::kWatchSubscription, subAt + 1, 0, nullptr, &sink, nullptr) == 0 && anyLine("not 4-aligned"),
          "watch: an unaligned address is refused with a line");
    SetEvent(g_busyGo);
    if (busy) {
        WaitForSingleObject(busy, 5000);
        CloseHandle(busy);
    }
    onFootHudProbeSetPlace(ohp::kPlaceShip);  // as the process began: Part B's draws are read in the ship
    onFootHudProbeSetCameraBits(0);
}
}  // namespace

int main(int argc, char** argv) {
    if (argc < 2 || std::strcmp(argv[1], "--self-test") != 0) {
        std::printf("usage: on_foot_hud_probe_test --self-test\n");
        return 2;
    }
    partA();
    partA13();
    partA15();
    partA16();
    partA19();
    partC();  // before Part B: its writer tables start empty
    partB();
    partB13();
    partB15();
    std::printf("%s: on_foot_hud_probe_test, %d checks, %d failed\n", g_failures ? "FAIL" : "PASS", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
