#pragma once
// On-foot HUD probe, the pure half (EDVRonFOOT; experimental.on_foot_hud_probe; the project's docs\research\native-vr-on-foot.md,
// "Разведка: где игра рисует пеший HUD"). on_foot_hud_probe.h has the hook. Everything here is plain C++ the rig drives directly: the
// build-334473 identity and prologue, the fields of the game's objects, which node is ours, the snapshot taken through a reader the
// caller supplies, the ring from the game's threads to the frame thread, the filter that writes a node only when it changed, the
// 30-second summary and the text of every line.
//
// What it answers: in headset stereo on foot the game builds its PostGUI pass (render lists 36-39 PostGui_FirstPerson_*, the HUD, and
// 40-46) but the HUD never reaches the headset. Either the lists hold nothing, or they hold models the view's camera does not draw.
//
// The game, build 334473 (Ghidra):
//   0x58F0F0 (node): draws a list-range node. S = *(node+0x108), ctx = *(S+0x60), the lists *(S+0x70)[i] (S+0x44 of them); for each
//     i in [*(node+0xD8), +*(node+0xE0)) it calls 0x592090(ctx, list) and 0x594EF0(ctx, list). It begins
//     rex push rbx; push rdi; push r14; sub rsp,80h; mov rax,[rcx+108h] -- no relative operand.
//   0x594EF0 (ctx, list): draws a list only when *(list+0x30) & *(ctx+0x270) is not 0. The list's models are a chain per camera
//     "pocket": head *(list+0x40 + 8k), k = *(cam+0x8E8), cam = *(ctx+0x250); 64 pockets.
//   0x27F9D00: the list-range node, vtable 0x52ED2C0; the PostGUI pass (0x28A71D0) makes two per view, lists 36-39 and 40-46.
//   0x57D1B0: the node's base puts its render view V at node+0x110 (built by 0x6BE020): V+0x10 the PostGUIView camera, V+0x20 its
//     name ("PostGUIView_%s", a game string: length at +0, characters from +0x14), V+0x28 the view's bit, V+0x30 the bit's number.
//   The camera: +0x18 and +0x250 flags, +0x8D0 and +0x8E0 masks, +0x8E8 its pocket.
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace edvr {
namespace ohp {

// Build 334473's PE TimeDateStamp and SizeOfImage (common/elite_oculus_profile.h checks the same stamp).
constexpr uint32_t kExpectedTimestamp = 0x6ACD513Du;
constexpr uint32_t kExpectedImageSize = 0x640C000u;

constexpr uintptr_t kNodeDrawRva = 0x58F0F0u;
constexpr uintptr_t kNodeVtableRva = 0x52ED2C0u;
constexpr size_t kPrologueBytes = 16;
constexpr uint8_t kPrologue[kPrologueBytes] = {0x40, 0x53, 0x57, 0x41, 0x56, 0x48, 0x81, 0xEC,
                                               0x80, 0x00, 0x00, 0x00, 0x48, 0x8B, 0x81, 0x08};

// The node.
constexpr uintptr_t kNodeFirst = 0xD8;   // u64: the first list
constexpr uintptr_t kNodeCount = 0xE0;   // u32: how many
constexpr uintptr_t kNodeSet = 0x108;    // the view's list set S
constexpr uintptr_t kNodeView = 0x110;   // the render view V
// The list set.
constexpr uintptr_t kSetLists = 0x44;    // u32: how many lists
constexpr uintptr_t kSetContext = 0x60;  // the draw context
constexpr uintptr_t kSetArray = 0x70;    // the lists
// The draw context.
constexpr uintptr_t kCtxCamera = 0x250;
constexpr uintptr_t kCtxMask = 0x270;    // u64
// The render view.
constexpr uintptr_t kViewCamera = 0x10;
constexpr uintptr_t kViewName = 0x20;
constexpr uintptr_t kViewBit = 0x28;     // u64
constexpr uintptr_t kViewBitIndex = 0x30;  // i32
constexpr uintptr_t kNameChars = 0x14;
// The camera.
constexpr uintptr_t kCamFlags18 = 0x18;    // u32
constexpr uintptr_t kCamFlags250 = 0x250;  // u32
constexpr uintptr_t kCamMask8D0 = 0x8D0;   // u32
constexpr uintptr_t kCamMask8E0 = 0x8E0;   // u64
constexpr uintptr_t kCamPocket = 0x8E8;    // u32
// The list.
constexpr uintptr_t kListMask = 0x30;      // u64
constexpr uintptr_t kListPockets = 0x40;   // 64 chain heads
constexpr int kPockets = 64;

constexpr uint32_t kHudFirst = 36;       // PostGui_FirstPerson_WeaponAR .. _HUD
constexpr uint32_t kPostGuiFirst = 40;   // PostGui_Background .. PostGui_Mouse
constexpr uint32_t kMaxLists = 7;
constexpr size_t kNameBytes = 32;        // with the terminator

// Reads n bytes of the game at addr; false if they cannot be read. The hook's reader is SEH-guarded, the rig's reads its own memory.
using ReadFn = bool (*)(void* ctx, uint64_t addr, void* out, size_t n);

template <typename T>
bool readAs(ReadFn read, void* rctx, uint64_t addr, T* out) {
    return addr >= 0x10000 && read(rctx, addr, out, sizeof(T));
}

// A node of the PostGUI pass: the list-range vtable and lists 36.. or 40...
inline bool isOurs(uint64_t vtable, uint64_t first, uintptr_t base) {
    return vtable == base + kNodeVtableRva && (first == kHudFirst || first == kPostGuiFirst);
}

struct ListState {
    uint64_t mask = 0;     // *(list+0x30)
    uint64_t pockets = 0;  // bit p: pocket p's chain head is not null
};

// Numbers only: no pointer into the game is kept past the hook.
struct Snapshot {
    uint64_t node = 0, ctx = 0, cam = 0, viewCam = 0;
    uint64_t ctxMask = 0, viewBit = 0, cam8E0 = 0;
    uint32_t first = 0, count = 0;  // count: the lists read (at most kMaxLists)
    int32_t viewBitIndex = -1;
    uint32_t camFlags18 = 0, camFlags250 = 0, cam8D0 = 0, pocket = 0;
    int32_t onFoot = -1;  // the journal's, set by the frame thread (-1 unknown)
    char name[kNameBytes] = {};
    ListState list[kMaxLists];
};

inline bool sameSnapshot(const Snapshot& a, const Snapshot& b) {
    if (a.node != b.node || a.ctx != b.ctx || a.cam != b.cam || a.viewCam != b.viewCam || a.ctxMask != b.ctxMask ||
        a.viewBit != b.viewBit || a.cam8E0 != b.cam8E0 || a.first != b.first || a.count != b.count ||
        a.viewBitIndex != b.viewBitIndex || a.camFlags18 != b.camFlags18 || a.camFlags250 != b.camFlags250 || a.cam8D0 != b.cam8D0 ||
        a.pocket != b.pocket || a.onFoot != b.onFoot || std::strcmp(a.name, b.name) != 0)
        return false;
    for (uint32_t i = 0; i < a.count; ++i)
        if (a.list[i].mask != b.list[i].mask || a.list[i].pockets != b.list[i].pockets) return false;
    return true;
}

// The view's name: up to kNameBytes - 1 printable characters; "?" when it cannot be read or holds anything else.
inline void readName(ReadFn read, void* rctx, uint64_t str, char* name) {
    std::strcpy(name, "?");
    uint64_t length = 0;
    if (!readAs(read, rctx, str, &length) || length == 0) return;
    char text[kNameBytes] = {};
    const size_t n = length < kNameBytes - 1 ? static_cast<size_t>(length) : kNameBytes - 1;
    if (!read(rctx, str + kNameChars, text, n)) return;
    for (size_t i = 0; i < n; ++i)
        if (text[i] < 32 || text[i] > 126) return;
    std::memcpy(name, text, n);
    name[n] = 0;
}

// The node after the game drew it. False if a field on the way could not be read: the snapshot is then incomplete and dropped.
// The camera's fields and the name are best effort (a missing camera reads as zeros, a missing name as "?").
inline bool capture(uint64_t node, ReadFn read, void* rctx, Snapshot* s) {
    *s = Snapshot{};
    s->node = node;
    uint64_t first = 0, set = 0, view = 0, array = 0;
    uint32_t count = 0, lists = 0;
    if (!readAs(read, rctx, node + kNodeFirst, &first) || !readAs(read, rctx, node + kNodeCount, &count) ||
        !readAs(read, rctx, node + kNodeSet, &set) || !readAs(read, rctx, set + kSetLists, &lists) ||
        !readAs(read, rctx, set + kSetContext, &s->ctx) || !readAs(read, rctx, set + kSetArray, &array) ||
        !readAs(read, rctx, s->ctx + kCtxCamera, &s->cam) || !readAs(read, rctx, s->ctx + kCtxMask, &s->ctxMask))
        return false;
    s->first = static_cast<uint32_t>(first);
    if (readAs(read, rctx, node + kNodeView, &view)) {
        uint64_t name = 0;
        readAs(read, rctx, view + kViewCamera, &s->viewCam);
        readAs(read, rctx, view + kViewBit, &s->viewBit);
        readAs(read, rctx, view + kViewBitIndex, &s->viewBitIndex);
        if (readAs(read, rctx, view + kViewName, &name)) readName(read, rctx, name, s->name);
        else std::strcpy(s->name, "?");
    } else {
        std::strcpy(s->name, "?");
    }
    readAs(read, rctx, s->cam + kCamFlags18, &s->camFlags18);
    readAs(read, rctx, s->cam + kCamFlags250, &s->camFlags250);
    readAs(read, rctx, s->cam + kCamMask8D0, &s->cam8D0);
    readAs(read, rctx, s->cam + kCamMask8E0, &s->cam8E0);
    if (!readAs(read, rctx, s->cam + kCamPocket, &s->pocket)) s->pocket = UINT32_MAX;
    // Only the lists the set has; never more than kMaxLists.
    uint32_t n = count < kMaxLists ? count : kMaxLists;
    if (first >= lists) n = 0;
    else if (first + n > lists) n = static_cast<uint32_t>(lists - first);
    s->count = n;
    for (uint32_t i = 0; i < n; ++i) {
        uint64_t list = 0;
        if (!readAs(read, rctx, array + 8 * (first + i), &list) || !readAs(read, rctx, list + kListMask, &s->list[i].mask)) return false;
        uint64_t heads[kPockets] = {};
        if (!readAs(read, rctx, list + kListPockets, &heads)) return false;
        for (int p = 0; p < kPockets; ++p)
            if (heads[p]) s->list[i].pockets |= 1ull << p;
    }
    return true;
}

// From the game's threads to the frame thread: many producers, one consumer; a full ring drops and counts.
template <size_t N>
class Ring {
    static_assert(N >= 2 && (N & (N - 1)) == 0, "N is a power of two");

public:
    Ring() {
        for (size_t i = 0; i < N; ++i) m_cells[i].seq.store(i, std::memory_order_relaxed);
    }
    bool push(const Snapshot& s) noexcept {
        size_t pos = m_enqueue.load(std::memory_order_relaxed);
        for (;;) {
            Cell& cell = m_cells[pos & (N - 1)];
            const size_t seq = cell.seq.load(std::memory_order_acquire);
            const intptr_t dif = static_cast<intptr_t>(seq) - static_cast<intptr_t>(pos);
            if (dif == 0) {
                if (m_enqueue.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed)) {
                    cell.snap = s;
                    cell.seq.store(pos + 1, std::memory_order_release);
                    return true;
                }
            } else if (dif < 0) {
                m_dropped.fetch_add(1, std::memory_order_relaxed);
                return false;
            } else {
                pos = m_enqueue.load(std::memory_order_relaxed);
            }
        }
    }
    bool pop(Snapshot* out) noexcept {
        Cell& cell = m_cells[m_dequeue & (N - 1)];
        const size_t seq = cell.seq.load(std::memory_order_acquire);
        if (static_cast<intptr_t>(seq) - static_cast<intptr_t>(m_dequeue + 1) != 0) return false;
        *out = cell.snap;
        cell.seq.store(m_dequeue + N, std::memory_order_release);
        ++m_dequeue;
        return true;
    }
    uint64_t dropped() const noexcept { return m_dropped.load(std::memory_order_relaxed); }

private:
    struct Cell {
        std::atomic<size_t> seq{0};
        Snapshot snap;
    };
    Cell m_cells[N];
    std::atomic<size_t> m_enqueue{0};
    size_t m_dequeue = 0;
    std::atomic<uint64_t> m_dropped{0};
};

// The node's line: the view, the camera, then each list -- does the camera's mask pass it, are there models in the camera's own
// pocket ("here"), in which other pockets.
inline int formatSnapshot(const Snapshot& s, char* out, size_t cap) {
    int n = std::snprintf(out, cap,
                          "on-foot hud probe: node 0x%llX view \"%s\" bit %d (0x%llX) lists %u-%u on-foot=%s cam 0x%llX (%s the view's 0x%llX) "
                          "flags 18=0x%X 250=0x%X masks 8d0=0x%X 8e0=0x%llX ctx-mask=0x%llX pocket %d",
                          static_cast<unsigned long long>(s.node), s.name, s.viewBitIndex, static_cast<unsigned long long>(s.viewBit),
                          s.first, s.first + (s.count ? s.count - 1 : 0), s.onFoot < 0 ? "?" : s.onFoot ? "yes" : "no",
                          static_cast<unsigned long long>(s.cam), s.cam == s.viewCam ? "is" : "not", static_cast<unsigned long long>(s.viewCam),
                          s.camFlags18, s.camFlags250, s.cam8D0, static_cast<unsigned long long>(s.cam8E0),
                          static_cast<unsigned long long>(s.ctxMask), s.pocket < static_cast<uint32_t>(kPockets) ? static_cast<int>(s.pocket) : -1);
    if (s.count == 0 && n > 0 && static_cast<size_t>(n) < cap) n += std::snprintf(out + n, cap - n, " | no lists");
    for (uint32_t i = 0; i < s.count && n > 0 && static_cast<size_t>(n) < cap; ++i) {
        const ListState& l = s.list[i];
        const bool own = s.pocket < static_cast<uint32_t>(kPockets);
        const uint64_t elsewhere = own ? l.pockets & ~(1ull << s.pocket) : l.pockets;
        n += std::snprintf(out + n, cap - n, " | %u pass=%s here=%s elsewhere={", s.first + i, (l.mask & s.ctxMask) ? "yes" : "no",
                           !own ? "?" : (l.pockets >> s.pocket) & 1 ? "yes" : "no");
        int listed = 0, more = 0;
        for (int p = 0; p < kPockets && n > 0 && static_cast<size_t>(n) < cap; ++p) {
            if (!((elsewhere >> p) & 1)) continue;
            if (listed == 8) {
                ++more;
                continue;
            }
            n += std::snprintf(out + n, cap - n, listed ? ",%d" : "%d", p);
            ++listed;
        }
        if (more && n > 0 && static_cast<size_t>(n) < cap) n += std::snprintf(out + n, cap - n, ",+%d", more);
        if (n > 0 && static_cast<size_t>(n) < cap)
            n += std::snprintf(out + n, cap - n, "} mask=0x%llX", static_cast<unsigned long long>(l.mask));
    }
    return n;
}

// Which snapshots reach the log: a node's first, then only a changed one, and per node at most one line a second -- a change inside
// the second waits and is written when the second is over, if it still differs from the last line. Nodes come and go with every
// pipeline rebuild (a boarding, a disembark): 16 are tracked, the longest unseen makes room.
class ChangeFilter {
public:
    static constexpr int kNodes = 16;
    static constexpr uint64_t kGapMs = 1000;

    // Takes one snapshot; true with *write when a line is due now. Counts what it keeps back.
    bool admit(const Snapshot& s, uint64_t nowMs, Snapshot* write) {
        Entry& e = entry(s.node, nowMs);
        e.seen = nowMs;
        if (e.written && sameSnapshot(e.last, s)) {
            e.pending = false;
            ++m_repeats;
            return false;
        }
        if (e.written && nowMs - e.at < kGapMs) {
            e.next = s;
            e.pending = true;
            ++m_held;
            return false;
        }
        return take(e, s, nowMs, write);
    }
    // A change kept back whose second is over: one per call, until false.
    bool due(uint64_t nowMs, Snapshot* write) {
        for (Entry& e : m_entries)
            if (e.used && e.pending && nowMs - e.at >= kGapMs) return take(e, e.next, nowMs, write);
        return false;
    }
    int nodes(uint64_t nowMs, uint64_t withinMs) const {
        int n = 0;
        for (const Entry& e : m_entries) n += e.used && nowMs - e.seen <= withinMs ? 1 : 0;
        return n;
    }
    uint64_t repeats() const { return m_repeats; }
    uint64_t held() const { return m_held; }
    uint64_t written() const { return m_written; }

private:
    struct Entry {
        bool used = false, written = false, pending = false;
        uint64_t node = 0, seen = 0, at = 0;
        Snapshot last, next;
    };
    bool take(Entry& e, const Snapshot& s, uint64_t nowMs, Snapshot* write) {
        e.last = s;
        e.written = true;
        e.pending = false;
        e.at = nowMs;
        ++m_written;
        *write = s;
        return true;
    }
    Entry& entry(uint64_t node, uint64_t nowMs) {
        for (Entry& e : m_entries)
            if (e.used && e.node == node) return e;
        Entry* oldest = nullptr;
        for (Entry& e : m_entries) {
            if (!e.used) {
                oldest = &e;
                break;
            }
            if (!oldest || e.seen < oldest->seen) oldest = &e;
        }
        *oldest = Entry{};
        oldest->used = true;
        oldest->node = node;
        oldest->seen = nowMs;
        return *oldest;
    }
    Entry m_entries[kNodes];
    uint64_t m_repeats = 0, m_held = 0, m_written = 0;
};

// The summary, every 30 s while the hook sees draws: every node draw it saw (proof it is alive even when no PostGUI node comes), the
// PostGUI nodes seen in the window and their calls, lines, repeats and kept-back changes, and what was lost to a full ring and to
// unreadable fields. Counters are totals since launch; the line gives the window's share.
struct SummaryCounts {
    uint64_t draws = 0, calls = 0, written = 0, repeats = 0, held = 0, lost = 0, faults = 0;
};

class Summary {
public:
    static constexpr uint64_t kWindowMs = 30000;
    int step(uint64_t nowMs, const SummaryCounts& total, int nodes, char* out, size_t cap) {
        if (!m_started) {
            m_started = true;
            m_since = nowMs;
            m_base = total;
            return 0;
        }
        if (nowMs - m_since < kWindowMs) return 0;
        const SummaryCounts w{total.draws - m_base.draws, total.calls - m_base.calls, total.written - m_base.written, total.repeats - m_base.repeats,
                              total.held - m_base.held, total.lost - m_base.lost, total.faults - m_base.faults};
        const uint64_t seconds = (nowMs - m_since) / 1000;
        m_since = nowMs;
        m_base = total;
        if (w.draws == 0) return 0;
        return std::snprintf(out, cap,
                             "on-foot hud probe: %llu s: %llu node draw(s) in all; PostGUI: %d node(s), %llu call(s), %llu line(s), "
                             "%llu repeat(s), %llu change(s) kept back, %llu lost, %llu unreadable",
                             static_cast<unsigned long long>(seconds), static_cast<unsigned long long>(w.draws), nodes,
                             static_cast<unsigned long long>(w.calls),
                             static_cast<unsigned long long>(w.written), static_cast<unsigned long long>(w.repeats),
                             static_cast<unsigned long long>(w.held), static_cast<unsigned long long>(w.lost),
                             static_cast<unsigned long long>(w.faults));
    }

private:
    bool m_started = false;
    uint64_t m_since = 0;
    SummaryCounts m_base;
};

// ---- the other lines --------------------------------------------------------------------------------------------------------------

// The key: at launch and on every change. `installed`: the hook is in (off then means it only forwards).
inline int formatKey(bool on, bool installed, char* out, size_t cap) {
    return std::snprintf(out, cap, "on-foot hud probe: %s (experimental.on_foot_hud_probe = %d)",
                         on ? (installed ? "on again: the hook records" : "on")
                            : (installed ? "off: the hook only forwards now" : "off: nothing in the game is touched"),
                         on ? 1 : 0);
}

inline int formatWrongBuild(uint32_t timestamp, uint32_t imageSize, char* out, size_t cap) {
    return std::snprintf(out, cap,
                         "on-foot hud probe: NOT installed: the game is not build 334473 (PE stamp 0x%08X, image size 0x%X); nothing was "
                         "patched and the game runs as without the probe",
                         timestamp, imageSize);
}

inline int formatWrongBytes(char* out, size_t cap) {
    return std::snprintf(out, cap,
                         "on-foot hud probe: NOT installed: the node draw at EliteDangerous64.exe+0x%llX does not begin with build 334473's "
                         "%zu bytes; nothing was patched",
                         static_cast<unsigned long long>(kNodeDrawRva), kPrologueBytes);
}

inline int formatHooked(uint64_t target, size_t moved, char* out, size_t cap) {
    return std::snprintf(out, cap,
                         "on-foot hud probe: hooked node draw at EliteDangerous64.exe+0x%llX (0x%llX), %zu bytes verified, %zu moved; it calls "
                         "the original unchanged and then reads the PostGUI nodes (lists 36-39, 40-46)",
                         static_cast<unsigned long long>(kNodeDrawRva), static_cast<unsigned long long>(target), kPrologueBytes, moved);
}

inline int formatFault(uint64_t node, char* out, size_t cap) {
    return std::snprintf(out, cap,
                         "on-foot hud probe: a field of node 0x%llX could not be read; that snapshot was skipped (the summary counts the "
                         "rest; this line is said once)",
                         static_cast<unsigned long long>(node));
}

}  // namespace ohp
}  // namespace edvr
