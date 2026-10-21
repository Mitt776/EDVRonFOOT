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
//   0x27F9D00: the list-range node, vtable 0x52ED2C0; the PostGUI pass (0x28A71D0) makes two per view from the table 0x5E86018
//     ({name, 1, 0, 3}, {name, 1, 4, 10}): lists 0-3 and 4-10 OF THE POSTGUI SET -- 36-39 and 40-46 of the game's list names. The
//     .11 probe waited for 36 and 40 there and saw no node at all (flight .11); a node is ours by its vtable and its pair.
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

constexpr uint32_t kHudFirst = 0, kHudCount = 4;          // PostGui_FirstPerson_WeaponAR .. _HUD
constexpr uint32_t kPostGuiFirst = 4, kPostGuiCount = 7;   // PostGui_Background .. PostGui_Mouse
constexpr uint32_t kListNameBase = 36;  // the PostGUI set's list 0 is the game's list name 36: the lines use the names' numbers
constexpr uint32_t kMaxLists = 7;
constexpr size_t kNameBytes = 32;        // with the terminator

// Reads n bytes of the game at addr; false if they cannot be read. The hook's reader is SEH-guarded, the rig's reads its own memory.
using ReadFn = bool (*)(void* ctx, uint64_t addr, void* out, size_t n);

template <typename T>
bool readAs(ReadFn read, void* rctx, uint64_t addr, T* out) {
    return addr >= 0x10000 && read(rctx, addr, out, sizeof(T));
}

// A node of the PostGUI pass: the list-range vtable and lists 0-3 or 4-10 of the set.
inline bool isOurs(uint64_t vtable, uint64_t first, uint32_t count, uintptr_t base) {
    return vtable == base + kNodeVtableRva &&
           ((first == kHudFirst && count == kHudCount) || (first == kPostGuiFirst && count == kPostGuiCount));
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
template <size_t N, typename T = Snapshot>
class Ring {
    static_assert(N >= 2 && (N & (N - 1)) == 0, "N is a power of two");

public:
    Ring() {
        for (size_t i = 0; i < N; ++i) m_cells[i].seq.store(i, std::memory_order_relaxed);
    }
    bool push(const T& s) noexcept {
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
    bool pop(T* out) noexcept {
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
        T snap;
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
                          kListNameBase + s.first, kListNameBase + s.first + (s.count ? s.count - 1 : 0), s.onFoot < 0 ? "?" : s.onFoot ? "yes" : "no",
                          static_cast<unsigned long long>(s.cam), s.cam == s.viewCam ? "is" : "not", static_cast<unsigned long long>(s.viewCam),
                          s.camFlags18, s.camFlags250, s.cam8D0, static_cast<unsigned long long>(s.cam8E0),
                          static_cast<unsigned long long>(s.ctxMask), s.pocket < static_cast<uint32_t>(kPockets) ? static_cast<int>(s.pocket) : -1);
    if (s.count == 0 && n > 0 && static_cast<size_t>(n) < cap) n += std::snprintf(out + n, cap - n, " | no lists");
    for (uint32_t i = 0; i < s.count && n > 0 && static_cast<size_t>(n) < cap; ++i) {
        const ListState& l = s.list[i];
        const bool own = s.pocket < static_cast<uint32_t>(kPockets);
        const uint64_t elsewhere = own ? l.pockets & ~(1ull << s.pocket) : l.pockets;
        n += std::snprintf(out + n, cap - n, " | %u pass=%s here=%s elsewhere={", kListNameBase + s.first + i, (l.mask & s.ctxMask) ? "yes" : "no",
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

// The node census (after flight .11): while no PostGUI node has come, the hook counts every other node by its kind -- the vtable's
// RVA (0xFFFFFFFF outside the image), its first list and its number of lists -- so a walk on which ours could not be told still
// shows what there was. Lock-free: a key claims an empty cell by compare-and-swap; a full table counts what did not fit.
class Census {
public:
    static constexpr int kCells = 32;
    static uint64_t key(uint64_t vtable, uintptr_t base, uint64_t first, uint32_t count, uint64_t imageSize) {
        const uint64_t rva = vtable >= base && vtable - base < imageSize ? vtable - base : 0xFFFFFFFFull;
        return (rva << 32) | ((first & 0xFFFF) << 16) | (count & 0xFFFF);
    }
    void note(uint64_t k) noexcept {
        const uint64_t stored = k + 1;  // 0 marks an empty cell
        for (Cell& c : m_cells) {
            uint64_t now = c.key.load(std::memory_order_acquire);
            if (now == 0 && c.key.compare_exchange_strong(now, stored, std::memory_order_acq_rel)) now = stored;
            if (now == stored) {
                c.hits.fetch_add(1, std::memory_order_relaxed);
                return;
            }
        }
        m_overflow.fetch_add(1, std::memory_order_relaxed);
    }
    struct Kind {
        uint32_t vtableRva = 0, first = 0, count = 0;
        uint64_t hits = 0;
    };
    // The kinds seen, most drawn first; returns how many.
    int kinds(Kind* out, int cap) const {
        int n = 0;
        for (const Cell& c : m_cells) {
            const uint64_t stored = c.key.load(std::memory_order_acquire);
            if (!stored || n == cap) continue;
            const uint64_t k = stored - 1;
            Kind kind{static_cast<uint32_t>(k >> 32), static_cast<uint32_t>((k >> 16) & 0xFFFF), static_cast<uint32_t>(k & 0xFFFF),
                      c.hits.load(std::memory_order_relaxed)};
            int at = n++;
            while (at > 0 && out[at - 1].hits < kind.hits) {
                out[at] = out[at - 1];
                --at;
            }
            out[at] = kind;
        }
        return n;
    }
    uint64_t overflow() const { return m_overflow.load(std::memory_order_relaxed); }

private:
    struct Cell {
        std::atomic<uint64_t> key{0};
        std::atomic<uint64_t> hits{0};
    };
    Cell m_cells[kCells];
    std::atomic<uint64_t> m_overflow{0};
};

constexpr uint64_t kCensusAfterMs = 20000;

inline int formatCensusHead(int kinds, uint64_t overflow, char* out, size_t cap) {
    return std::snprintf(out, cap,
                         "on-foot hud probe: no PostGUI node (vtable +0x%llX, lists 0+4 or 4+7) in %llu s of recording; the node kinds "
                         "seen instead, most drawn first: %d kind(s), %llu draw(s) of kinds that did not fit",
                         static_cast<unsigned long long>(kNodeVtableRva), static_cast<unsigned long long>(kCensusAfterMs / 1000), kinds,
                         static_cast<unsigned long long>(overflow));
}

inline int formatCensusKind(const Census::Kind& k, char* out, size_t cap) {
    if (k.vtableRva == 0xFFFFFFFFu)
        return std::snprintf(out, cap, "on-foot hud probe: census: vtable outside the image, first list %u, %u list(s): %llu draw(s)",
                             k.first, k.count, static_cast<unsigned long long>(k.hits));
    return std::snprintf(out, cap, "on-foot hud probe: census: vtable +0x%X%s, first list %u, %u list(s): %llu draw(s)", k.vtableRva,
                         k.vtableRva == kNodeVtableRva ? " (the list-range node's)" : "", k.first, k.count,
                         static_cast<unsigned long long>(k.hits));
}

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

// ---- .13: the PostGUI nodes in the game's node graph (docs: "Статика после .12") ------------------------------------------------
// Flight .12: on foot in stereo the frame's mask asks for the HUD lists, yet the lists 36-39 node is never drawn. The game draws a
// node only from its graph walk:
//   0x6BF980 (S, char run): one graph system. S+0x40 its graphs (S+0x38 of them). A graph G is on when byte G+0x14 is not 0; its nodes
//     are G+0x30 (G+0x28 of them; G+0x18 the room). A node N is on when byte N+0x30 is not 0. With run != 0 it calls, for each on node
//     of each on graph, the node's +0x10, then in a second round +0x18 (0x58B090: N+0x30 again, then +0x28 ... 0x58F0F0(N)).
//   0x6BFC60 (G, N) appends N to G's nodes; 0x6BFE60 (G, N) takes it out.
// So on foot the node is off (H1), out of every graph (H2) or in a graph that is off (H3). The walk hook reads which, before the
// original; the add and remove hooks say who moved a PostGUI node; two hardware write watches say who wrote N+0x30 and G+0x14.
enum Point : int { kPointNodeDraw = 0, kPointWalk, kPointAdd, kPointRemove, kPointCount };
constexpr uintptr_t kPointRva[kPointCount] = {kNodeDrawRva, 0x6BF980u, 0x6BFC60u, 0x6BFE60u};
// Each point's first 16 bytes in build 334473 (capstone, 2026-10-22). None has a relative operand in the bytes CodeHook moves.
constexpr uint8_t kPointPrologue[kPointCount][kPrologueBytes] = {
    {0x40, 0x53, 0x57, 0x41, 0x56, 0x48, 0x81, 0xEC, 0x80, 0x00, 0x00, 0x00, 0x48, 0x8B, 0x81, 0x08},
    // sub rsp,48h; mov rax,[rcx+20h]; mov [rsp+60h],rbx; mov rbx,[rcx+28h]
    {0x48, 0x83, 0xEC, 0x48, 0x48, 0x8B, 0x41, 0x20, 0x48, 0x89, 0x5C, 0x24, 0x60, 0x48, 0x8B, 0x59},
    // mov [rsp+10h],rdx; sub rsp,28h; mov r8,[rcx+28h]; add rcx,18h
    {0x48, 0x89, 0x54, 0x24, 0x10, 0x48, 0x83, 0xEC, 0x28, 0x4C, 0x8B, 0x41, 0x28, 0x48, 0x83, 0xC1},
    // mov [rsp+10h],rbx; push rdi; sub rsp,20h; mov rax,[rcx+28h]; lea rdi,[rcx+18h]
    {0x48, 0x89, 0x5C, 0x24, 0x10, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0x41, 0x28, 0x48, 0x8D},
};
inline const char* pointName(int p) {
    switch (p) {
        case kPointNodeDraw: return "node draw";
        case kPointWalk: return "graph walk";
        case kPointAdd: return "graph add";
        default: return "graph remove";
    }
}

// The graph system, the graph, the node.
constexpr uintptr_t kSysGraphCount = 0x38;  // u64
constexpr uintptr_t kSysGraphs = 0x40;
constexpr uintptr_t kGraphOn = 0x14;        // u8
constexpr uintptr_t kGraphNodeCount = 0x28; // u64
constexpr uintptr_t kGraphNodes = 0x30;
constexpr uintptr_t kNodeOn = 0x30;         // u8
// More than any system or graph the game builds: a larger count is a misread, and the walk's snapshot is dropped.
constexpr uint64_t kMaxGraphs = 64, kMaxGraphNodes = 256;

// The tracked nodes: the PostGUI pass's two per view, two views (flight .12: Primary bit 8, Secondary bit 49). A cell is keyed by the
// node's kind and its view's bit number, so a pipeline rebuild that makes new nodes refills the same cells.
constexpr int kTracked = 4;
enum Kind : int { kKindHud = 0, kKindUi = 1 };
inline int kindOf(uint64_t first) { return first == kHudFirst ? kKindHud : kKindUi; }
inline const char* kindName(int kind) { return kind == kKindHud ? "hud 36-39" : "ui 40-46"; }
inline uint64_t trackKey(int kind, int32_t viewBitIndex) {
    return ((static_cast<uint64_t>(kind) + 1) << 32) | static_cast<uint32_t>(viewBitIndex);
}

// What one walk saw of each tracked node: three bits a cell, OR-ed over a frame's walks, plus whether walks ran at all.
enum : uint32_t { kSeenOn = 1, kSeenNodeOff = 2, kSeenGraphOff = 4 };
constexpr uint32_t kSeenShift = 3;
constexpr uint32_t kWalkedRun = 1u << 16;   // a walk with run != 0 (the one that calls nodes) happened
constexpr uint32_t kWalkedIdle = 1u << 17;  // a walk with run == 0 happened
inline uint32_t seenOf(uint32_t word, int cell) { return (word >> (kSeenShift * cell)) & 7u; }

// One walk of system S: where each tracked node (0: none) is. *seen gets the bits, graphs[cell] the first graph holding the node.
// False when a field could not be read or a count is beyond any the game has; nothing is claimed then.
inline bool scanWalk(uint64_t system, const uint64_t nodes[kTracked], ReadFn read, void* rctx, uint32_t* seen, uint64_t graphs[kTracked]) {
    *seen = 0;
    for (int c = 0; c < kTracked; ++c) graphs[c] = 0;
    uint64_t count = 0, array = 0;
    if (!readAs(read, rctx, system + kSysGraphCount, &count) || !readAs(read, rctx, system + kSysGraphs, &array) || count > kMaxGraphs)
        return false;
    uint64_t graph[kMaxGraphs];
    if (count && (array < 0x10000 || !read(rctx, array, graph, static_cast<size_t>(count) * 8))) return false;
    for (uint64_t g = 0; g < count; ++g) {
        uint8_t on = 0;
        uint64_t n = 0, list = 0;
        if (!readAs(read, rctx, graph[g] + kGraphOn, &on) || !readAs(read, rctx, graph[g] + kGraphNodeCount, &n) ||
            !readAs(read, rctx, graph[g] + kGraphNodes, &list) || n > kMaxGraphNodes)
            return false;
        uint64_t node[kMaxGraphNodes];
        if (n && (list < 0x10000 || !read(rctx, list, node, static_cast<size_t>(n) * 8))) return false;
        for (uint64_t i = 0; i < n; ++i) {
            for (int c = 0; c < kTracked; ++c) {
                if (!nodes[c] || node[i] != nodes[c]) continue;
                uint8_t flag = 0;
                if (!readAs(read, rctx, nodes[c] + kNodeOn, &flag)) return false;
                const uint32_t bit = !on ? kSeenGraphOff : flag ? kSeenOn : kSeenNodeOff;
                *seen |= bit << (kSeenShift * c);
                if (!graphs[c]) graphs[c] = graph[g];
            }
        }
    }
    return true;
}

// Is the node still in that graph's list: 1 yes, 0 no, -1 unknown (no graph, or unreadable).
inline int graphHolds(uint64_t graph, uint64_t node, ReadFn read, void* rctx) {
    if (!graph || !node) return -1;
    uint64_t n = 0, list = 0;
    if (!readAs(read, rctx, graph + kGraphNodeCount, &n) || !readAs(read, rctx, graph + kGraphNodes, &list) || n > kMaxGraphNodes) return -1;
    uint64_t nodes[kMaxGraphNodes];
    if (n && (list < 0x10000 || !read(rctx, list, nodes, static_cast<size_t>(n) * 8))) return -1;
    for (uint64_t i = 0; i < n; ++i)
        if (nodes[i] == node) return 1;
    return 0;
}

enum State : uint8_t { kStateUnknown = 0, kStateOn, kStateNodeOff, kStateNotInGraph, kStateGraphOff, kStateNotWalked, kStateNotFound };

// A frame's state of one node: drawn if any walk found it on; else its flag off (H1); else its graph off (H3). Found in no walk: still
// in the graph it was last seen in -- the graph is not walked; gone from it -- out of every graph (H2); that graph unknown -- not
// found. No walk at all this frame: unknown, and the state stays as it was.
inline State classify(uint32_t seen, bool walkedRun, bool walkedIdle, int inLastGraph) {
    if (seen & kSeenOn) return kStateOn;
    if (seen & kSeenNodeOff) return kStateNodeOff;
    if (seen & kSeenGraphOff) return kStateGraphOff;
    if (!walkedRun && !walkedIdle) return kStateUnknown;
    if (!walkedRun) return inLastGraph == 0 ? kStateNotInGraph : kStateNotWalked;
    return inLastGraph == 1 ? kStateNotWalked : inLastGraph == 0 ? kStateNotInGraph : kStateNotFound;
}
inline const char* stateText(State s) {
    switch (s) {
        case kStateOn: return "on: in an enabled graph, the node enabled (drawn)";
        case kStateNodeOff: return "node-off (H1): in an enabled graph, the node's flag +0x30 is 0";
        case kStateNotInGraph: return "not-in-graph (H2): in no walked graph and gone from its graph";
        case kStateGraphOff: return "graph-off (H3): its graph's flag +0x14 is 0";
        case kStateNotWalked: return "graph-not-walked: still in its graph, which no node-calling walk visits";
        case kStateNotFound: return "not-found: in no walked graph, its graph not known";
        default: return "unknown";
    }
}
inline const char* stateShort(State s) {
    switch (s) {
        case kStateOn: return "on";
        case kStateNodeOff: return "node-off (H1)";
        case kStateNotInGraph: return "not-in-graph (H2)";
        case kStateGraphOff: return "graph-off (H3)";
        case kStateNotWalked: return "graph-not-walked";
        case kStateNotFound: return "not-found";
        default: return "unknown";
    }
}

// Which state lines reach the log: a cell's first known state and every change, at most 20 in 30 s for all cells together; a change
// over the allowance is counted, and written in a later window if it still stands.
class StateLog {
public:
    static constexpr int kPerWindow = 20;
    static constexpr uint64_t kWindowMs = 30000;
    bool admit(int cell, State s, uint64_t nowMs) {
        if (s == kStateUnknown || cell < 0 || cell >= kTracked) return false;
        if (!m_started || nowMs - m_since >= kWindowMs) {
            m_started = true;
            m_since = nowMs;
            m_inWindow = 0;
        }
        if (m_said[cell] && m_last[cell] == s) return false;
        if (m_inWindow >= kPerWindow) {
            ++m_held;
            return false;
        }
        ++m_inWindow;
        m_said[cell] = true;
        m_last[cell] = s;
        return true;
    }
    void forget(int cell) {  // a new node in the cell: its first state is written again
        if (cell >= 0 && cell < kTracked) m_said[cell] = false;
    }
    State last(int cell) const { return m_said[cell] ? m_last[cell] : kStateUnknown; }
    uint64_t held() const { return m_held; }

private:
    bool m_started = false, m_said[kTracked] = {};
    State m_last[kTracked] = {};
    uint64_t m_since = 0, m_held = 0;
    int m_inWindow = 0;
};

// The callers: "EliteDangerous64.exe+0x..." for the first frame in the game's image, "+0x..." for the next ones, raw outside it.
constexpr int kStackFrames = 6;
inline int formatCallers(const uint64_t* stack, int frames, uint64_t base, uint64_t imageSize, char* out, size_t cap) {
    if (!cap) return 0;
    out[0] = 0;
    int n = 0;
    bool named = false;
    if (frames <= 0) return std::snprintf(out, cap, "?");
    for (int i = 0; i < frames && n >= 0 && static_cast<size_t>(n) < cap; ++i) {
        const uint64_t a = stack[i];
        const char* sep = i ? " < " : "";
        if (a >= base && a - base < imageSize) {
            n += std::snprintf(out + n, cap - n, "%s%s+0x%llX", sep, named ? "" : "EliteDangerous64.exe", static_cast<unsigned long long>(a - base));
            named = true;
        } else {
            n += std::snprintf(out + n, cap - n, "%s0x%llX", sep, static_cast<unsigned long long>(a));
        }
    }
    return n;
}

// A PostGUI node added to a graph or taken out, with who did it. Numbers only.
struct GraphEvent {
    uint8_t remove = 0, frames = 0;
    uint32_t thread = 0;
    uint64_t node = 0, first = 0, graph = 0;
    int64_t countAfter = -1;  // the graph's nodes after the call (-1 unreadable)
    uint64_t stack[kStackFrames] = {};
};

inline int formatGraphEvent(const GraphEvent& e, int onFoot, uint64_t base, uint64_t imageSize, char* out, size_t cap) {
    char callers[400];
    formatCallers(e.stack, e.frames, base, imageSize, callers, sizeof(callers));
    return std::snprintf(out, cap, "on-foot hud probe: graph %s: node 0x%llX (%s) graph 0x%llX, %lld node(s) after, on-foot=%s, thread %u, from %s",
                         e.remove ? "remove" : "add", static_cast<unsigned long long>(e.node), kindName(kindOf(e.first)),
                         static_cast<unsigned long long>(e.graph), static_cast<long long>(e.countAfter),
                         onFoot < 0 ? "?" : onFoot ? "yes" : "no", e.thread, callers);
}

// ---- the write watches -------------------------------------------------------------------------------------------------------------
// Debug register slot 2 watches the first tracked HUD node's flag N+0x30, slot 3 its graph's G+0x14 -- write, 4 bytes, every thread
// (slot 0 is the flat camera producer probe's, slot 1 native_vr_probe's). The CPU reports a data breakpoint AFTER the write: the
// handler records each distinct instruction once (the address after it, the byte's value then, the callers) and counts the rest.
// A write to the next three bytes traps too; the value tells it apart (unchanged).
enum Flag : int { kFlagNode = 0, kFlagGraph = 1, kFlagCount = 2 };
constexpr unsigned kFlagSlot[kFlagCount] = {2, 3};
inline const char* flagName(int f) { return f == kFlagNode ? "node flag" : "graph flag"; }
inline uintptr_t flagOffset(int f) { return f == kFlagNode ? kNodeOn : kGraphOn; }
constexpr int kSites = 8;

struct Site {
    std::atomic<uint64_t> rip{0};
    std::atomic<uint64_t> hits{0};
    std::atomic<uint8_t> ready{0};
    uint8_t value = 0, frames = 0;
    uint32_t thread = 0;
    uint64_t stack[kStackFrames] = {};
};
// Lock-free, as nvp::WriterTable: a new site claims a free row by compare-and-swap, the handler fills it and only then sets `ready`.
class SiteTable {
public:
    int record(uint64_t rip, bool* fresh) noexcept {
        *fresh = false;
        m_hits.fetch_add(1, std::memory_order_relaxed);
        for (int i = 0; i < kSites; ++i) {
            uint64_t seen = m_sites[i].rip.load(std::memory_order_acquire);
            if (seen == 0) {
                uint64_t expected = 0;
                if (m_sites[i].rip.compare_exchange_strong(expected, rip, std::memory_order_acq_rel)) {
                    m_sites[i].hits.fetch_add(1, std::memory_order_relaxed);
                    *fresh = true;
                    return i;
                }
                seen = expected;
            }
            if (seen == rip) {
                m_sites[i].hits.fetch_add(1, std::memory_order_relaxed);
                return i;
            }
        }
        m_overflow.fetch_add(1, std::memory_order_relaxed);
        return -1;
    }
    void publish(int row, uint32_t thread, uint8_t value, const uint64_t* stack, int frames) noexcept {
        Site& s = m_sites[row];
        s.thread = thread;
        s.value = value;
        s.frames = static_cast<uint8_t>(frames < 0 ? 0 : (frames > kStackFrames ? kStackFrames : frames));
        for (int i = 0; i < s.frames; ++i) s.stack[i] = stack[i];
        s.ready.store(1, std::memory_order_release);
    }
    // Frame thread: rows published since the last call, each once.
    bool nextUnsaid(int* row) noexcept {
        for (int i = 0; i < kSites; ++i) {
            if (m_said[i] || !m_sites[i].ready.load(std::memory_order_acquire)) continue;
            m_said[i] = true;
            *row = i;
            return true;
        }
        return false;
    }
    const Site& site(int row) const { return m_sites[row]; }
    int sites() const {
        int n = 0;
        for (const Site& s : m_sites) n += s.rip.load(std::memory_order_acquire) ? 1 : 0;
        return n;
    }
    uint64_t hits() const { return m_hits.load(std::memory_order_relaxed); }
    uint64_t overflow() const { return m_overflow.load(std::memory_order_relaxed); }

private:
    Site m_sites[kSites];
    bool m_said[kSites] = {};
    std::atomic<uint64_t> m_hits{0}, m_overflow{0};
};

// More than 100 000 traps in 30 s (a flag the game writes every frame): the watches come off, the rest of the probe goes on.
class Fuse {
public:
    static constexpr uint64_t kWindowMs = 30000, kMaxHits = 100000;
    bool step(uint64_t nowMs, uint64_t totalHits) {
        if (!m_started || nowMs - m_since >= kWindowMs) {
            m_started = true;
            m_since = nowMs;
            m_base = totalHits;
            return false;
        }
        return totalHits - m_base > kMaxHits;
    }

private:
    bool m_started = false;
    uint64_t m_since = 0, m_base = 0;
};

inline int formatWatchOn(int flag, uint64_t address, uint64_t object, uint32_t armed, uint32_t skipped, uint32_t failed, char* out, size_t cap) {
    return std::snprintf(out, cap,
                         "on-foot hud probe: watch on: %s 0x%llX (0x%llX+0x%llX) on debug slot %u, write, 4 bytes: %u thread(s), %u skipped "
                         "(slot taken), %u could not be set",
                         flagName(flag), static_cast<unsigned long long>(address), static_cast<unsigned long long>(object),
                         static_cast<unsigned long long>(flagOffset(flag)), kFlagSlot[flag], armed, skipped, failed);
}
inline int formatWatchNotOn(int flag, uint64_t address, const char* why, char* out, size_t cap) {
    return std::snprintf(out, cap, "on-foot hud probe: watch NOT on: %s 0x%llX: %s; the state and graph lines go on", flagName(flag),
                         static_cast<unsigned long long>(address), why);
}
inline int formatWatchOff(int flag, uint64_t address, const char* why, const SiteTable& t, char* out, size_t cap) {
    return std::snprintf(out, cap, "on-foot hud probe: watch off: %s 0x%llX: %s; %d site(s), %llu hit(s), %llu from sites that did not fit",
                         flagName(flag), static_cast<unsigned long long>(address), why, t.sites(), static_cast<unsigned long long>(t.hits()),
                         static_cast<unsigned long long>(t.overflow()));
}
// A site is the instruction AND whether it wrote 0: the handler keys a row by siteKey, so one place that writes both 1 and 0 gives two
// lines, and the 0 is never hidden behind a first 1. A user-mode address never has bit 63.
constexpr uint64_t kSiteZeroBit = 1ull << 63;
inline uint64_t siteKey(uint64_t rip, uint8_t value) { return value ? rip : rip | kSiteZeroBit; }

// One line per site: the instruction after the write, the byte then, the callers (unwound from the writer's own context).
inline int formatWriter(int flag, int row, const Site& s, uint64_t object, uint64_t base, uint64_t imageSize, char* out, size_t cap) {
    char at[100], callers[400];
    const uint64_t rip = s.rip.load(std::memory_order_acquire) & ~kSiteZeroBit;
    formatCallers(&rip, 1, base, imageSize, at, sizeof(at));
    formatCallers(s.stack, s.frames, base, imageSize, callers, sizeof(callers));
    return std::snprintf(out, cap, "on-foot hud probe: writer #%d of the %s of 0x%llX: at %s (after the write), value %u, thread %u, from %s",
                         row + 1, flagName(flag), static_cast<unsigned long long>(object), at, s.value, s.thread, callers);
}

// ---- the .13 lines ------------------------------------------------------------------------------------------------------------------

inline int formatPointHooked(int point, uint64_t target, size_t moved, char* out, size_t cap) {
    return std::snprintf(out, cap, "on-foot hud probe: hooked %s at EliteDangerous64.exe+0x%llX (0x%llX), %zu bytes verified, %zu moved; %s",
                         pointName(point), static_cast<unsigned long long>(kPointRva[point]), static_cast<unsigned long long>(target),
                         kPrologueBytes, moved,
                         point == kPointWalk ? "it reads where the PostGUI nodes are before the original"
                                             : "it calls the original unchanged and then says which PostGUI node moved and who moved it");
}
inline int formatPointWrongBytes(int point, char* out, size_t cap) {
    return std::snprintf(out, cap,
                         "on-foot hud probe: NOT installed: the %s at EliteDangerous64.exe+0x%llX does not begin with build 334473's %zu "
                         "bytes; that one was not patched, the others stand",
                         pointName(point), static_cast<unsigned long long>(kPointRva[point]), kPrologueBytes);
}
inline int formatTracked(int kind, int32_t viewBit, uint64_t node, uint64_t was, char* out, size_t cap) {
    if (!was)
        return std::snprintf(out, cap, "on-foot hud probe: tracking %s node 0x%llX of view bit %d", kindName(kind),
                             static_cast<unsigned long long>(node), viewBit);
    return std::snprintf(out, cap, "on-foot hud probe: tracking %s node 0x%llX of view bit %d (was 0x%llX: the pipeline was rebuilt)",
                         kindName(kind), static_cast<unsigned long long>(node), viewBit, static_cast<unsigned long long>(was));
}
inline int formatGraphOf(int kind, int32_t viewBit, uint64_t node, uint64_t graph, uint64_t was, char* out, size_t cap) {
    return std::snprintf(out, cap, "on-foot hud probe: %s node 0x%llX of view bit %d is in graph 0x%llX%s", kindName(kind),
                         static_cast<unsigned long long>(node), viewBit, static_cast<unsigned long long>(graph), was ? " (a new graph)" : "");
}
// flag, graphOn: the bytes as the frame thread reads them for the line (-1 unreadable).
inline int formatState(int kind, int32_t viewBit, State s, uint64_t node, int flag, uint64_t graph, int graphOn, int onFoot, char* out, size_t cap) {
    char f[8], g[8];
    if (flag < 0) std::snprintf(f, sizeof(f), "?");
    else std::snprintf(f, sizeof(f), "%d", flag);
    if (graphOn < 0) std::snprintf(g, sizeof(g), "?");
    else std::snprintf(g, sizeof(g), "%d", graphOn);
    return std::snprintf(out, cap, "on-foot hud probe: state %s view bit %d: %s; node 0x%llX flag=%s graph 0x%llX on=%s on-foot=%s",
                         kindName(kind), viewBit, stateText(s), static_cast<unsigned long long>(node), f, static_cast<unsigned long long>(graph), g,
                         onFoot < 0 ? "?" : onFoot ? "yes" : "no");
}

// The graph's 30-second line, beside the summary: the walks, each tracked node's state, the PostGUI nodes moved, the watches.
struct GraphCounts {
    uint64_t walks = 0, idle = 0, unreadable = 0, postgui = 0, other = 0, lost = 0, hits = 0, held = 0;
};
struct TrackedView {
    int kind = 0;
    int32_t viewBit = -1;
    State state = kStateUnknown;
};
class GraphSummary {
public:
    static constexpr uint64_t kWindowMs = 30000;
    int step(uint64_t nowMs, const GraphCounts& total, const TrackedView* tracked, int n, char* out, size_t cap) {
        if (!m_started) {
            m_started = true;
            m_since = nowMs;
            m_base = total;
            return 0;
        }
        if (nowMs - m_since < kWindowMs) return 0;
        const GraphCounts w{total.walks - m_base.walks, total.idle - m_base.idle, total.unreadable - m_base.unreadable,
                            total.postgui - m_base.postgui, total.other - m_base.other, total.lost - m_base.lost,
                            total.hits - m_base.hits, total.held - m_base.held};
        const uint64_t seconds = (nowMs - m_since) / 1000;
        m_since = nowMs;
        m_base = total;
        int len = std::snprintf(out, cap, "on-foot hud probe: %llu s graph: %llu node-calling walk(s), %llu without nodes, %llu unreadable; ",
                                static_cast<unsigned long long>(seconds), static_cast<unsigned long long>(w.walks),
                                static_cast<unsigned long long>(w.idle), static_cast<unsigned long long>(w.unreadable));
        bool hud = false;
        for (int i = 0; i < n; ++i) hud = hud || tracked[i].kind == kKindHud;
        if (!hud && len > 0 && static_cast<size_t>(len) < cap)
            len += std::snprintf(out + len, cap - len, "the hud 36-39 node is not recognised (none drawn since recording began)");
        for (int i = 0; i < n && len > 0 && static_cast<size_t>(len) < cap; ++i)
            len += std::snprintf(out + len, cap - len, "%s%s bit %d %s", i ? ", " : "", kindName(tracked[i].kind), tracked[i].viewBit,
                                 stateShort(tracked[i].state));
        if (len > 0 && static_cast<size_t>(len) < cap)
            len += std::snprintf(out + len, cap - len,
                                 "; graph events: %llu PostGUI, %llu other, %llu lost; watch: %llu hit(s); %llu state line(s) kept back",
                                 static_cast<unsigned long long>(w.postgui), static_cast<unsigned long long>(w.other),
                                 static_cast<unsigned long long>(w.lost), static_cast<unsigned long long>(w.hits),
                                 static_cast<unsigned long long>(w.held));
        return len;
    }

private:
    bool m_started = false;
    uint64_t m_since = 0;
    GraphCounts m_base;
};

}  // namespace ohp
}  // namespace edvr
