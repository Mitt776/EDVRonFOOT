// On-foot HUD probe: the hooks, their installation, the write watches and the frame thread's drain (on_foot_hud_probe.h says what and
// why).
#include "on_foot_hud_probe.h"
#include "on_foot_hud_probe_core.h"
#include "debug_register_core.h"
#include "journal_watch.h"
#include "../common/code_hook.h"
#include "../common/config.h"
#include "../common/frame_flag.h"
#include "../common/log.h"
#include <windows.h>
#include <tlhelp32.h>
#include <atomic>
#include <cmath>
#include <cstring>

namespace edvr {
namespace {

using Fn1 = void (*)(void*);
using Fn2 = void (*)(void*, void*);
using FnWalk = void (*)(void*, uint64_t);
using FnSwitch = uint64_t (*)(void*, uint64_t);
using FnList = void (*)(void*, void*, uint64_t);

// One hooked function of the game: its CodeHook, the relay page near it and the forward to its original.
struct Point {
    CodeHook hook;
    uint8_t* relay = nullptr;
    std::atomic<uintptr_t> original{0};
    bool installed = false;
};
Point g_points[ohp::kPointCount];

std::atomic<uintptr_t> g_vtable{0};  // base + the node vtable's RVA
std::atomic<uintptr_t> g_base{0};
std::atomic<bool> g_recording{false};
std::atomic<uint64_t> g_draws{0};  // every node draw seen while recording
std::atomic<uint64_t> g_calls{0};  // of them, the PostGUI nodes'
std::atomic<uint64_t> g_faults{0};
std::atomic<uint64_t> g_faultNode{0};  // the first node a field of which could not be read
ohp::Ring<64> g_ring;
// The census (ohp::Census): on from the install until a PostGUI node comes or the census is written.
ohp::Census g_census;
std::atomic<bool> g_censusOn{false};
std::atomic<uintptr_t> g_imageSize{0x10000000};  // the rig's image has no headers: anything inside 256 MB counts as in it
bool g_identityOk = false;

// The tracked PostGUI nodes (ohp::kTracked cells, keyed by kind and view bit): the node draw hook fills them, the walk hook reads them
// and notes each one's graph.
struct Cell {
    std::atomic<uint64_t> key{0};
    std::atomic<uint64_t> node{0};
    std::atomic<uint64_t> graph{0};
};
Cell g_cells[ohp::kTracked];
std::atomic<uint64_t> g_cellsFull{0};

// The walk: what this frame's walks saw (ohp::kSeen* per cell, ohp::kWalked*), taken by the frame thread with an exchange.
std::atomic<uint32_t> g_seen{0};
std::atomic<uint64_t> g_walks{0}, g_idleWalks{0}, g_walkFaults{0};
// The graph add and remove hooks: PostGUI nodes' events to the frame thread; the others only counted.
ohp::Ring<32, ohp::GraphEvent> g_events;
std::atomic<uint64_t> g_graphPostGui{0}, g_graphOther{0};
// .15: the passes the PostGUI switch is called with (the switch hook notes them), and the frame mask of the HUD node drawn with each
// camera (the frame thread's, from the node snapshots).
ohp::PassTable g_passes;
ohp::MaskByCamera g_masks;
// .19: the HUD node's providers. The node draw hook reads the list after the original and keeps the answers by provider, view and place
// in the table; a list of a new shape (or in a new place) goes to the ring for the frame thread. g_place is the frame thread's, for the
// hook and the write watch's handler.
ohp::ProviderTable g_providers;
ohp::Ring<4, ohp::ProviderSnap> g_providerRing;
std::atomic<int> g_place{ohp::kPlaceShip};
// .20: the _HUD provider's gates. After a matched snapshot of the HUD node that lists the provider (vtable base + kProvBVtableRva) the
// hook walks its record, at most once per g_walkEveryMs per view (g_walkAt: the tick of the view's last walk); the walk goes to the ring
// when it matched or found no record, every walk is counted by place and result; g_hudMaterials remembers the materials that gave _HUD.
ohp::HudMaterials g_hudMaterials;
ohp::GateWalks g_gateWalks;
ohp::Ring<2, ohp::GateWalk> g_gateRing;
std::atomic<uint64_t> g_walkAt[ohp::kProvViews];
std::atomic<uint64_t> g_walkEveryMs{ohp::kWalkEveryMs};
// .21: the HUD part. The frame thread learns it from the cinema walk (g_hudPart, one writer) and the walk reads it in every place; the
// reads are counted by place and verdict. The HUD node's draw opens a window for its thread (g_windows) and the list prep hook sets its
// list; the draw path's calls (onFootHudProbeNoteDraw) keep the draws of the window, and those outside it with the HUD meshes' vertex
// shaders, by shape (g_shapes).
ohp::HudPartMemory g_hudPart;
ohp::PartReads g_partReads;
ohp::NodeWindows g_windows;
ohp::ShapeTable g_shapes;

// ---- the hook side: no lock, no allocation, no log -------------------------------------------------------------------------------

__declspec(noinline) bool sehRead(void*, uint64_t addr, void* out, size_t n) noexcept {
    __try {
        std::memcpy(out, reinterpret_cast<const void*>(addr), n);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// The node's kind, from three fields: ours or not costs nothing more.
__declspec(noinline) bool sehReadKind(const void* node, uint64_t* vtable, uint64_t* first, uint32_t* count) noexcept {
    __try {
        const auto* p = static_cast<const uint8_t*>(node);
        *vtable = *reinterpret_cast<const uint64_t*>(p);
        *first = *reinterpret_cast<const uint64_t*>(p + ohp::kNodeFirst);
        *count = *reinterpret_cast<const uint32_t*>(p + ohp::kNodeCount);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

uintptr_t gameBase() { return g_vtable.load(std::memory_order_relaxed) - ohp::kNodeVtableRva; }

// A PostGUI node drawn: keep it in its cell. A new node in a cell (a pipeline rebuild) forgets the old one's graph.
void remember(int kind, int32_t viewBit, uint64_t node) {
    const uint64_t key = ohp::trackKey(kind, viewBit);
    for (Cell& c : g_cells) {
        uint64_t k = c.key.load(std::memory_order_acquire);
        if (k == 0) {
            uint64_t expected = 0;
            k = c.key.compare_exchange_strong(expected, key, std::memory_order_acq_rel) ? key : expected;
        }
        if (k != key) continue;
        if (c.node.load(std::memory_order_relaxed) != node) {
            c.graph.store(0, std::memory_order_relaxed);
            c.node.store(node, std::memory_order_release);
        }
        return;
    }
    g_cellsFull.fetch_add(1, std::memory_order_relaxed);
}

// .20: the walk of the _HUD provider's record (a few KB more on the stack than the snapshot, so a function of its own: only a walk that
// goes pays for it). The record is found by the camera and the set of the snapshot, the provider's answer is its +0x30 in it. .21: the HUD
// part the cinema walk learned is read in this walk, in any place (a read is counted by verdict; with no part learned the walk is counted
// as one without a part).
__declspec(noinline) void walkOfRecord(const ohp::ProviderSnap& s, const ohp::Provider& b, int place) {
    ohp::GateWalk w;
    ohp::HudPartKey key;
    const bool learned = g_hudPart.get(&key);
    const int result = ohp::walkHudProvider(b.addr, s.node, s.cam, s.set, b.answer, s.viewBit, s.viewSlot, place, &sehRead, nullptr, &g_hudMaterials, &w,
                                            learned ? &key : nullptr);
    g_gateWalks.note(place, result, w.cutShort != 0);
    if (learned) g_partReads.note(place, w.part.verdict);
    else g_partReads.noteNotLearned(place);
    if (result == ohp::kWalkMatched || result == ohp::kWalkNoRecord) g_gateRing.push(w);
    else if (result == ohp::kWalkAnswerDiffers) g_gateWalks.noteBad(result, w.recordAnswer, w.providerAnswer, s.node);
    else if (result == ohp::kWalkBitNotInAnswer) g_gateWalks.noteBad(result, w.recomputed, w.recordAnswer, s.node);
    else if (result == ohp::kWalkUnbelieved) g_gateWalks.noteBad(result, w.slot, w.view, s.node);
}

// After a matched snapshot that lists the _HUD provider: at most once per g_walkEveryMs per view (a compare-and-swap on the view's tick),
// the first time also the collectors' table.
__declspec(noinline) void walkHudProviderOf(const ohp::ProviderSnap& s) {
    const uintptr_t base = gameBase();
    int at = -1;
    for (uint32_t i = 0; i < s.stored && at < 0; ++i)
        if (s.p[i].addr && s.p[i].vtable == base + ohp::kProvBVtableRva) at = static_cast<int>(i);
    if (at < 0 || s.viewSlot < 0 || s.viewSlot >= ohp::kProvViews) return;
    uint64_t now = GetTickCount64();
    if (!now) now = 1;
    std::atomic<uint64_t>& stamp = g_walkAt[s.viewSlot];
    uint64_t last = stamp.load(std::memory_order_relaxed);
    if (last != 0 && now - last < g_walkEveryMs.load(std::memory_order_relaxed)) return;
    if (!stamp.compare_exchange_strong(last, now, std::memory_order_relaxed)) return;
    if (!g_gateWalks.collectorsDone()) {
        uint64_t fn[ohp::kCollectors];
        int32_t offset[ohp::kCollectors];
        if (ohp::readCollectors(&sehRead, nullptr, base, fn, offset) && g_gateWalks.claimCollectors()) g_gateWalks.publishCollectors(fn, offset);
    }
    walkOfRecord(s, s.p[at], static_cast<int>(s.place));
}

// .19, the HUD node only, after its draw: its context is still prepared for it, so the providers' answers are this node's. The snapshot
// (a few KB on the stack, filled by the reader) goes in the table when it matches; only a list of a new shape or a new place is copied
// into the ring.
__declspec(noinline) void providersOfHudNode(void* node) {
    ohp::ProviderSnap s;
    const int result = ohp::readProviders(reinterpret_cast<uint64_t>(node), &sehRead, nullptr, &s);
    const bool fresh = g_providers.take(result, s, g_place.load(std::memory_order_relaxed));
    if (fresh) g_providerRing.push(s);
    if (result == ohp::kSnapOk) walkHudProviderOf(s);
}

// .21, BEFORE the original and for the HUD node (lists 36-39) only: the window of the node's draw, for the calling thread. A cell, the
// thread, the node's view bit and the place; no lock, no allocation. The cell, or -1 when the node is not the HUD node or none is free.
__declspec(noinline) int openWindowFor(void* node) {
    uint64_t vtable = 0, first = 0;
    uint32_t count = 0;
    if (!sehReadKind(node, &vtable, &first, &count) || !ohp::isOurs(vtable, first, count, gameBase()) || ohp::kindOf(first) != ohp::kKindHud) return -1;
    int32_t viewBit = -1;
    uint64_t view = 0;
    if (sehRead(nullptr, reinterpret_cast<uint64_t>(node) + ohp::kNodeView, &view, sizeof(view)))
        sehRead(nullptr, view + ohp::kViewBitIndex, &viewBit, sizeof(viewBit));
    return g_windows.open(GetCurrentThreadId(), viewBit, g_place.load(std::memory_order_relaxed));
}

// After the original: a list's models may be handed in during the draw itself (0x594EF0's first loop), so before it would read empty.
__declspec(noinline) void nodeDrawHook(void* node) {
    const int window = g_recording.load(std::memory_order_relaxed) && node ? openWindowFor(node) : -1;
    reinterpret_cast<Fn1>(g_points[ohp::kPointNodeDraw].original.load(std::memory_order_acquire))(node);
    if (window >= 0) g_windows.close(window);
    if (!g_recording.load(std::memory_order_relaxed) || !node) return;
    g_draws.fetch_add(1, std::memory_order_relaxed);
    uint64_t vtable = 0, first = 0;
    uint32_t count = 0;
    if (!sehReadKind(node, &vtable, &first, &count)) return;
    const uintptr_t base = gameBase();
    if (!ohp::isOurs(vtable, first, count, base)) {
        if (g_censusOn.load(std::memory_order_relaxed))
            g_census.note(ohp::Census::key(vtable, base, first, count, g_imageSize.load(std::memory_order_relaxed)));
        return;
    }
    g_calls.fetch_add(1, std::memory_order_relaxed);
    if (ohp::kindOf(first) == ohp::kKindHud) providersOfHudNode(node);
    ohp::Snapshot s;
    if (!ohp::capture(reinterpret_cast<uint64_t>(node), &sehRead, nullptr, &s)) {
        g_faults.fetch_add(1, std::memory_order_relaxed);
        uint64_t none = 0;
        g_faultNode.compare_exchange_strong(none, reinterpret_cast<uint64_t>(node), std::memory_order_relaxed);
        return;
    }
    remember(ohp::kindOf(first), s.viewBitIndex, reinterpret_cast<uint64_t>(node));
    g_ring.push(s);
}

// BEFORE the original: what the walk finds on its way in is what decides whether the node is called.
__declspec(noinline) void walkHook(void* system, uint64_t run) {
    if (g_recording.load(std::memory_order_relaxed) && system) {
        if (static_cast<uint8_t>(run) == 0) {
            g_idleWalks.fetch_add(1, std::memory_order_relaxed);
            g_seen.fetch_or(ohp::kWalkedIdle, std::memory_order_relaxed);
        } else {
            g_walks.fetch_add(1, std::memory_order_relaxed);
            uint64_t nodes[ohp::kTracked];
            bool any = false;
            for (int c = 0; c < ohp::kTracked; ++c) {
                nodes[c] = g_cells[c].node.load(std::memory_order_acquire);
                any = any || nodes[c] != 0;
            }
            uint32_t seen = 0;
            uint64_t graphs[ohp::kTracked] = {};
            if (!any) {
                g_seen.fetch_or(ohp::kWalkedRun, std::memory_order_relaxed);
            } else if (ohp::scanWalk(reinterpret_cast<uint64_t>(system), nodes, &sehRead, nullptr, &seen, graphs)) {
                g_seen.fetch_or(seen | ohp::kWalkedRun, std::memory_order_relaxed);
                for (int c = 0; c < ohp::kTracked; ++c)
                    if (graphs[c] && g_cells[c].node.load(std::memory_order_relaxed) == nodes[c])
                        g_cells[c].graph.store(graphs[c], std::memory_order_relaxed);
            } else {
                g_walkFaults.fetch_add(1, std::memory_order_relaxed);
            }
        }
    }
    reinterpret_cast<FnWalk>(g_points[ohp::kPointWalk].original.load(std::memory_order_acquire))(system, run);
}

// After the original: the event carries the graph's count after the call. Frame 0 of the capture is this hook, skipped; frame 1 is the
// game's caller of the add or remove.
template <int P>
__declspec(noinline) void graphHook(void* graph, void* node) {
    reinterpret_cast<Fn2>(g_points[P].original.load(std::memory_order_acquire))(graph, node);
    if (!g_recording.load(std::memory_order_relaxed) || !node) return;
    uint64_t vtable = 0, first = 0;
    uint32_t count = 0;
    if (!sehReadKind(node, &vtable, &first, &count) || !ohp::isOurs(vtable, first, count, gameBase())) {
        g_graphOther.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    g_graphPostGui.fetch_add(1, std::memory_order_relaxed);
    ohp::GraphEvent e;
    e.remove = P == ohp::kPointRemove ? 1 : 0;
    e.node = reinterpret_cast<uint64_t>(node);
    e.first = first;
    e.graph = reinterpret_cast<uint64_t>(graph);
    uint64_t after = 0;
    e.countAfter = sehRead(nullptr, e.graph + ohp::kGraphNodeCount, &after, sizeof(after)) ? static_cast<int64_t>(after) : -1;
    e.thread = GetCurrentThreadId();
    void* frames[ohp::kStackFrames] = {};
    const USHORT n = RtlCaptureStackBackTrace(1, ohp::kStackFrames, frames, nullptr);
    e.frames = static_cast<uint8_t>(n);
    for (USHORT i = 0; i < n; ++i) e.stack[i] = reinterpret_cast<uint64_t>(frames[i]);
    g_events.push(e);
}

// BEFORE the original, and nothing more: the pass goes in the table, the frame thread reads its cameras. The bool is forwarded as the
// caller left the whole register, and the original's result is returned.
__declspec(noinline) uint64_t passSwitchHook(void* pass, uint64_t on) {
    if (g_recording.load(std::memory_order_relaxed) && pass) g_passes.note(reinterpret_cast<uint64_t>(pass));
    return reinterpret_cast<FnSwitch>(g_points[ohp::kPointPass].original.load(std::memory_order_acquire))(pass, on);
}

// .21, BEFORE the original (which is called with the three registers as the caller left them): the list the game is about to prepare and
// draw is the window's list, if the calling thread has a window. The list's bit is *(list+0x30), 1 << i for lists 36-39 (i = 0-3); any
// other mask, or one that cannot be read, is "no list".
__declspec(noinline) void listPrepHook(void* ctx, void* list, uint64_t third) {
    if (g_recording.load(std::memory_order_relaxed) && list) {
        const int cell = g_windows.find(GetCurrentThreadId());
        if (cell >= 0) {
            uint64_t mask = 0;
            g_windows.setList(cell, sehRead(nullptr, reinterpret_cast<uint64_t>(list) + ohp::kListMask, &mask, sizeof(mask)) ? ohp::listOfMask(mask) : -1);
        }
    }
    reinterpret_cast<FnList>(g_points[ohp::kPointList].original.load(std::memory_order_acquire))(ctx, list, third);
}

const void* hookFor(int point) {
    switch (point) {
        case ohp::kPointList: return reinterpret_cast<const void*>(&listPrepHook);
        case ohp::kPointNodeDraw: return reinterpret_cast<const void*>(&nodeDrawHook);
        case ohp::kPointWalk: return reinterpret_cast<const void*>(&walkHook);
        case ohp::kPointAdd: return reinterpret_cast<const void*>(&graphHook<ohp::kPointAdd>);
        case ohp::kPointRemove: return reinterpret_cast<const void*>(&graphHook<ohp::kPointRemove>);
        default: return reinterpret_cast<const void*>(&passSwitchHook);
    }
}

// ---- the write watches: the handler side (any thread; no lock, no allocation, no log) ----------------------------------------------
// .21: slots 2 and 3 the HUD part's array of meshes and their number (ohp::kWatchSlot): the same technique as native_vr_probe.cpp's writer
// watch (slot 1). The handler reads the value (g_watchLength bytes) and whether it is not 0 (the place was filled, not cleared).

std::atomic<uintptr_t> g_watchAddress[ohp::kWatchCount];
std::atomic<uintptr_t> g_watchPrevious[ohp::kWatchCount];
std::atomic<bool> g_watchActive[ohp::kWatchCount];
std::atomic<unsigned> g_watchLength[ohp::kWatchCount];
ohp::SiteTable g_sites[ohp::kWatchCount];

DWORD64& slotRegister(CONTEXT* c, unsigned slot) { return slot == 2 ? c->Dr2 : c->Dr3; }

// The callers of the writing instruction, unwound from the CPU's own context of the write (the handler's stack is not the writer's).
// A frame with no unwind data is a leaf: its return address is at rsp.
__declspec(noinline) int sehUnwind(const CONTEXT* at, uint64_t* stack) noexcept {
    int n = 0;
    __try {
        CONTEXT c = *at;
        for (; n < ohp::kStackFrames; ++n) {
            DWORD64 imageBase = 0;
            PRUNTIME_FUNCTION fn = RtlLookupFunctionEntry(c.Rip, &imageBase, nullptr);
            if (fn) {
                PVOID handlerData = nullptr;
                DWORD64 establisher = 0;
                RtlVirtualUnwind(UNW_FLAG_NHANDLER, imageBase, c.Rip, fn, &c, &handlerData, &establisher, nullptr);
            } else {
                c.Rip = *reinterpret_cast<const DWORD64*>(c.Rsp);
                c.Rsp += 8;
            }
            if (!c.Rip) break;
            stack[n] = c.Rip;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    return n;
}

LONG CALLBACK watchHandler(EXCEPTION_POINTERS* ep) {
    if (ep->ExceptionRecord->ExceptionCode != EXCEPTION_SINGLE_STEP) return EXCEPTION_CONTINUE_SEARCH;
    CONTEXT* c = ep->ContextRecord;
    for (int f = 0; f < ohp::kWatchCount; ++f) {
        const unsigned slot = ohp::kWatchSlot[f];
        // Never cleared once set, and the one before kept: a trap that arrives after its thread was disarmed is still ours.
        const uintptr_t address = g_watchAddress[f].load(std::memory_order_acquire);
        const uintptr_t previous = g_watchPrevious[f].load(std::memory_order_acquire);
        DWORD64& dr = slotRegister(c, slot);
        const DWORD64 hitBit = DWORD64(1) << slot;
        // Slots 2 and 3 are this probe's alone in EDVR. A context that reports no slot is ours only at an address we watched, and never
        // when it is a trap-flag step (Dr6.BS): vtable_hook.cpp's write watch single-steps with it.
        const bool slotHit = (c->Dr6 & hitBit) != 0;
        const bool known = dr && (dr == address || dr == previous);
        if (!slotHit && !(known && !(c->Dr6 & 0x400F))) continue;
        c->Dr6 &= ~hitBit;
        if (!g_watchActive[f].load(std::memory_order_acquire) || dr != address) {
            // A straggler: off here, in the context this thread resumes with.
            c->Dr7 = (c->Dr7 & ~DWORD64(0xFFFFFFFFu)) | dr7::disarmSlotDr7(static_cast<uint32_t>(c->Dr7), slot);
            dr = 0;
            return EXCEPTION_CONTINUE_EXECUTION;
        }
        // The watched word as written (its length, 1, 4 or 8 bytes), and whether it is not 0.
        uint64_t value = 0;
        const unsigned length = g_watchLength[f].load(std::memory_order_relaxed);
        sehRead(nullptr, address, &value, length < 8 ? length : 8);
        const int place = g_place.load(std::memory_order_relaxed);
        const bool filled = value != 0;
        bool fresh = false;
        const int row = g_sites[f].record(static_cast<uint64_t>(c->Rip), place, value, filled, &fresh);
        if (fresh) {
            uint64_t stack[ohp::kStackFrames] = {};
            const int frames = sehUnwind(c, stack);
            g_sites[f].publish(row, GetCurrentThreadId(), place, value, filled, stack, frames);
        }
        return EXCEPTION_CONTINUE_EXECUTION;
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

// ---- the write watches: the frame thread side ----------------------------------------------------------------------------------

enum : int { kDrFailed = 0, kDrDone = 1, kDrSkipped = 2 };

// One slot on one thread, from OUTSIDE that thread (a running thread's own debug registers cannot be set by itself): suspend, read,
// change only that slot, write, resume. Arming leaves a thread whose slot someone else holds alone; disarming touches only a slot that
// still holds our address.
int setThreadWatch(DWORD tid, unsigned slot, uintptr_t address, unsigned length, bool arm) noexcept {
    HANDLE h = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_SET_CONTEXT, FALSE, tid);
    if (!h) return kDrFailed;
    int result = kDrFailed;
    if (SuspendThread(h) != static_cast<DWORD>(-1)) {
        CONTEXT ctx{};
        ctx.ContextFlags = CONTEXT_DEBUG_REGISTERS;
        if (GetThreadContext(h, &ctx)) {
            const uint32_t low = static_cast<uint32_t>(ctx.Dr7);
            const DWORD64 high = ctx.Dr7 & ~DWORD64(0xFFFFFFFFu);
            DWORD64& dr = slotRegister(&ctx, slot);
            const bool ours = dr == address;
            if (arm) {
                if (dr7::slotEnabledDr7(low, slot) && !ours) {
                    result = kDrSkipped;
                } else {
                    dr = address;
                    ctx.Dr7 = high | dr7::armSlotDr7(low, slot, dr7::kDr7RwWrite, ohp::drLengthCode(length));
                    result = SetThreadContext(h, &ctx) ? kDrDone : kDrFailed;
                }
            } else if (ours) {
                dr = 0;
                ctx.Dr7 = high | dr7::disarmSlotDr7(low, slot);
                result = SetThreadContext(h, &ctx) ? kDrDone : kDrFailed;
            } else {
                result = kDrSkipped;
            }
        }
        ResumeThread(h);
    }
    CloseHandle(h);
    return result;
}

// The calling thread cannot suspend itself: a helper thread sets its slot.
struct DrJob {
    DWORD tid;
    unsigned slot;
    uintptr_t address;
    unsigned length;
    bool arm;
    int result;
};
DWORD WINAPI drHelper(void* p) {
    auto* job = static_cast<DrJob*>(p);
    job->result = setThreadWatch(job->tid, job->slot, job->address, job->length, job->arm);
    return 0;
}
int setThreadWatchSafe(DWORD tid, unsigned slot, uintptr_t address, unsigned length, bool arm) noexcept {
    if (tid != GetCurrentThreadId()) return setThreadWatch(tid, slot, address, length, arm);
    DrJob job{tid, slot, address, length, arm, kDrFailed};
    HANDLE h = CreateThread(nullptr, 0, &drHelper, &job, 0, nullptr);
    if (!h) return kDrFailed;
    const bool done = WaitForSingleObject(h, 5000) == WAIT_OBJECT_0;
    CloseHandle(h);
    return done ? job.result : kDrFailed;
}

constexpr size_t kWatchThreads = 1024;
DWORD g_watchTids[ohp::kWatchCount][kWatchThreads];
size_t g_watchTidCount[ohp::kWatchCount] = {};
uint64_t g_watchObject[ohp::kWatchCount] = {};  // the structure the watched word is a field of, for the lines
PVOID g_watchHandler = nullptr;

void say(OnFootHudProbeSink sink, void* ctx, int n, const char* line) {
    if (n > 0) sink(ctx, line);
}

// ---- installation (frame thread) ------------------------------------------------------------------------------------------------

// A 5-byte jmp reaches +/-2 GB, and this DLL may load farther than that from the game: the patch jumps to a relay page placed near the
// target, which jumps on, absolutely, to the hook (jmp [rip+0]; dq hook). As native_vr_probe.cpp does.
constexpr size_t kRelayBytes = 14;

uint8_t* allocateRelay(uintptr_t target) noexcept {
    SYSTEM_INFO info{};
    GetSystemInfo(&info);
    const uintptr_t granularity = info.dwAllocationGranularity;
    const uintptr_t floor = reinterpret_cast<uintptr_t>(info.lpMinimumApplicationAddress);
    const uintptr_t ceiling = reinterpret_cast<uintptr_t>(info.lpMaximumApplicationAddress);
    const uintptr_t distance = uintptr_t(INT32_MAX) - 0x10000u;
    uintptr_t at = target > distance ? target - distance : floor;
    if (at < floor) at = floor;
    const uintptr_t limit = target > ceiling - distance ? ceiling : target + distance;
    while (at < limit) {
        MEMORY_BASIC_INFORMATION region{};
        if (!VirtualQuery(reinterpret_cast<void*>(at), &region, sizeof(region))) break;
        const uintptr_t start = reinterpret_cast<uintptr_t>(region.BaseAddress);
        if (region.RegionSize > UINTPTR_MAX - start) break;
        const uintptr_t end = start + region.RegionSize;
        if (region.State == MEM_FREE) {
            uintptr_t candidate = at > start ? at : start;
            if (candidate > UINTPTR_MAX - (granularity - 1)) break;
            candidate = (candidate + granularity - 1) & ~(granularity - 1);
            if (candidate < limit && candidate < end && end - candidate >= 4096) {
                auto* p = static_cast<uint8_t*>(
                    VirtualAlloc(reinterpret_cast<void*>(candidate), 4096, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
                if (p) return p;
            }
        }
        if (end <= at) break;
        at = end;
    }
    return nullptr;
}

// Publish the original's forward and seal the relay BEFORE CodeHook makes the patch live: the first game call may arrive at once.
bool prepare(void* trampoline, void* context) noexcept {
    auto& point = *static_cast<Point*>(context);
    point.original.store(reinterpret_cast<uintptr_t>(trampoline), std::memory_order_release);
    DWORD oldProtect = 0;
    return VirtualProtect(point.relay, 4096, PAGE_EXECUTE_READ, &oldProtect) &&
           FlushInstructionCache(GetCurrentProcess(), point.relay, kRelayBytes);
}

__declspec(noinline) bool sehIdentity(uintptr_t base, uint32_t* timestamp, uint32_t* imageSize) noexcept {
    __try {
        uint32_t peOff = 0;
        std::memcpy(&peOff, reinterpret_cast<const void*>(base + 0x3C), 4);
        if (peOff > 0x1000) return false;
        std::memcpy(timestamp, reinterpret_cast<const void*>(base + peOff + 8), 4);
        std::memcpy(imageSize, reinterpret_cast<const void*>(base + peOff + 0x50), 4);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

enum : int { kHooked = 0, kWrongBytes, kNoRelay, kRefused };

// One point: its 16 bytes, its relay, its CodeHook. Nothing is patched unless all of it holds.
int installPoint(int point, uintptr_t base, uintptr_t target) {
    Point& p = g_points[point];
    uint8_t bytes[ohp::kPrologueBytes] = {};
    if (!sehRead(nullptr, target, bytes, sizeof(bytes)) || std::memcmp(bytes, ohp::kPointPrologue[point], sizeof(bytes)) != 0) return kWrongBytes;
    if (!p.relay) p.relay = allocateRelay(target);
    if (!p.relay) return kNoRelay;
    g_vtable.store(base + ohp::kNodeVtableRva, std::memory_order_relaxed);
    g_base.store(base, std::memory_order_relaxed);
    const uint8_t jump[6] = {0xFF, 0x25, 0, 0, 0, 0};
    const uintptr_t hook = reinterpret_cast<uintptr_t>(hookFor(point));
    std::memcpy(p.relay, jump, sizeof(jump));
    std::memcpy(p.relay + sizeof(jump), &hook, 8);
    if (!p.hook.install(reinterpret_cast<void*>(target), p.relay, nullptr, "on-foot-hud-probe", &prepare, &p)) {
        DWORD oldProtect = 0;
        VirtualProtect(p.relay, 4096, PAGE_READWRITE, &oldProtect);  // so a retry can rewrite it
        return kRefused;
    }
    p.installed = true;
    // Process lifetime: the relay and trampoline are never freed, so a call already inside them can finish.
    return kHooked;
}

void logSink(void*, const char* line) { Log::get().note("%s", line); }

bool anyInstalled() {
    for (const Point& p : g_points)
        if (p.installed) return true;
    return false;
}

}  // namespace

// .21: the draw path's flag (on_foot_hud_probe.h): up while the key is on and the node draw is hooked.
namespace detail {
std::atomic<bool> g_onFootHudProbeWantsDraws{false};
}  // namespace detail

bool onFootHudProbeInstall(uintptr_t base, uintptr_t targetOverride, bool checkIdentity, OnFootHudProbeSink sink, void* ctx) {
    Point& p = g_points[ohp::kPointNodeDraw];
    if (p.installed) return true;
    char line[400];
    if (checkIdentity) {
        uint32_t timestamp = 0, imageSize = 0;
        if (!sehIdentity(base, &timestamp, &imageSize) || timestamp != ohp::kExpectedTimestamp || imageSize != ohp::kExpectedImageSize) {
            say(sink, ctx, ohp::formatWrongBuild(timestamp, imageSize, line, sizeof(line)), line);
            return false;
        }
        g_imageSize.store(imageSize, std::memory_order_relaxed);
    }
    g_identityOk = true;
    const uintptr_t target = targetOverride ? targetOverride : base + ohp::kNodeDrawRva;
    switch (installPoint(ohp::kPointNodeDraw, base, target)) {
        case kWrongBytes: say(sink, ctx, ohp::formatWrongBytes(line, sizeof(line)), line); return false;
        case kNoRelay:
            sink(ctx, "on-foot hud probe: NOT installed: no memory could be placed within 2 GB of the node draw; nothing was patched");
            return false;
        case kRefused:
            sink(ctx, "on-foot hud probe: NOT installed: CodeHook refused the node draw (its own line above says why); nothing was patched");
            return false;
        default: break;
    }
    g_censusOn.store(true, std::memory_order_relaxed);
    say(sink, ctx, ohp::formatHooked(target, p.hook.stolenBytes(), line, sizeof(line)), line);
    return true;
}

bool onFootHudProbeInstallPoint(int point, uintptr_t base, uintptr_t targetOverride, OnFootHudProbeSink sink, void* ctx) {
    if (point <= ohp::kPointNodeDraw || point >= ohp::kPointCount) return false;
    Point& p = g_points[point];
    if (p.installed) return true;
    char line[400];
    const uintptr_t target = targetOverride ? targetOverride : base + ohp::kPointRva[point];
    switch (installPoint(point, base, target)) {
        case kWrongBytes: say(sink, ctx, ohp::formatPointWrongBytes(point, line, sizeof(line)), line); return false;
        case kNoRelay: {
            const int n = std::snprintf(line, sizeof(line),
                                        "on-foot hud probe: NOT installed: no memory could be placed within 2 GB of the %s; that one was "
                                        "not patched, the others stand",
                                        ohp::pointName(point));
            say(sink, ctx, n, line);
            return false;
        }
        case kRefused: {
            const int n = std::snprintf(line, sizeof(line),
                                        "on-foot hud probe: NOT installed: CodeHook refused the %s (its own line above says why); that one "
                                        "was not patched, the others stand",
                                        ohp::pointName(point));
            say(sink, ctx, n, line);
            return false;
        }
        default: break;
    }
    say(sink, ctx, ohp::formatPointHooked(point, target, p.hook.stolenBytes(), line, sizeof(line)), line);
    return true;
}

void onFootHudProbeSetRecording(bool on) {
    g_recording.store(on, std::memory_order_relaxed);
    detail::g_onFootHudProbeWantsDraws.store(on && g_points[ohp::kPointNodeDraw].installed, std::memory_order_relaxed);
}

int onFootHudProbeDrawClass(uint64_t ownVsHash, ohp::DrawShape* shape) {
    const int cell = g_windows.find(GetCurrentThreadId());
    if (cell >= 0) {
        const ohp::NodeWindows::Head h = g_windows.next(cell);
        shape->list = static_cast<uint8_t>(h.list >= 0 ? h.list : ohp::kShapeListUnknown);
        shape->viewBit = h.viewBit;
        shape->place = static_cast<uint8_t>(h.place);
        shape->ordinal = h.ordinal;
        return 1;
    }
    if (!ohp::isHudMeshVs(ownVsHash)) return 0;
    shape->list = ohp::kShapeListOutside;
    shape->viewBit = -1;
    shape->place = static_cast<uint8_t>(g_place.load(std::memory_order_relaxed));
    shape->ordinal = 0;
    return 2;
}

void onFootHudProbeNoteShape(const ohp::DrawShape& shape) { g_shapes.note(shape); }

uint64_t onFootHudProbeCalls() { return g_calls.load(std::memory_order_relaxed); }
uint64_t onFootHudProbeFaults() { return g_faults.load(std::memory_order_relaxed); }

int onFootHudProbeWatchArm(int watch, const OnFootHudProbeWatch& target, OnFootHudProbeSink sink, void* ctx) {
    char line[400];
    const uintptr_t address = target.address;
    const uint64_t object = target.object;
    const char* const note = target.note;
    if (watch < 0 || watch >= ohp::kWatchCount || g_watchTidCount[watch] || !address) return 0;
    const char* const name = ohp::watchName(watch);
    if ((target.length != 1 && target.length != 4 && target.length != 8) || (address & (target.length - 1))) {
        say(sink, ctx, ohp::formatWatchNotOn(name, address, "the address is not aligned to its length (1, 4 or 8 bytes)", line, sizeof(line)), line);
        return 0;
    }
    if (!g_watchHandler) g_watchHandler = AddVectoredExceptionHandler(1, &watchHandler);
    if (!g_watchHandler) {
        say(sink, ctx, ohp::formatWatchNotOn(name, address, "the exception handler could not be added", line, sizeof(line)), line);
        return 0;
    }
    const unsigned slot = ohp::kWatchSlot[watch];
    // The handler must know the address and the length before the first thread can trap on it.
    g_watchLength[watch].store(target.length, std::memory_order_release);
    const uintptr_t was = g_watchAddress[watch].exchange(address, std::memory_order_acq_rel);
    if (was && was != address) g_watchPrevious[watch].store(was, std::memory_order_release);
    g_watchActive[watch].store(true, std::memory_order_release);
    g_watchObject[watch] = object;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snap == INVALID_HANDLE_VALUE) {
        g_watchActive[watch].store(false, std::memory_order_release);
        say(sink, ctx, ohp::formatWatchNotOn(name, address, "the process's threads could not be listed", line, sizeof(line)), line);
        return 0;
    }
    uint32_t armed = 0, skipped = 0, failed = 0;
    const DWORD pid = GetCurrentProcessId();
    THREADENTRY32 te{};
    te.dwSize = sizeof(te);
    for (BOOL ok = Thread32First(snap, &te); ok; ok = Thread32Next(snap, &te)) {
        if (te.th32OwnerProcessID != pid) continue;
        if (g_watchTidCount[watch] >= kWatchThreads) {
            ++failed;
            continue;
        }
        const int r = setThreadWatchSafe(te.th32ThreadID, slot, address, target.length, true);
        if (r == kDrDone) {
            g_watchTids[watch][g_watchTidCount[watch]++] = te.th32ThreadID;
            ++armed;
        } else if (r == kDrSkipped) {
            ++skipped;
        } else {
            ++failed;
        }
    }
    CloseHandle(snap);
    if (!armed) {
        g_watchActive[watch].store(false, std::memory_order_release);
        char why[160];
        std::snprintf(why, sizeof(why), "no thread took it (%u skipped, %u could not be set)", skipped, failed);
        say(sink, ctx, ohp::formatWatchNotOn(name, address, why, line, sizeof(line)), line);
        return 0;
    }
    say(sink, ctx, ohp::formatWatchOn(name, address, object, target.length, slot, armed, skipped, failed, note, line, sizeof(line)), line);
    return static_cast<int>(armed);
}

bool onFootHudProbeWatchOn(int watch) { return watch >= 0 && watch < ohp::kWatchCount && g_watchTidCount[watch] != 0; }

uintptr_t onFootHudProbeWatchAddress(int watch) {
    return onFootHudProbeWatchOn(watch) ? g_watchAddress[watch].load(std::memory_order_acquire) : 0;
}

void onFootHudProbeWatchDrain(int watch, OnFootHudProbeSink sink, void* ctx) {
    if (watch < 0 || watch >= ohp::kWatchCount) return;
    char line[700];
    int row = 0;
    while (g_sites[watch].nextUnsaid(&row))
        say(sink, ctx,
            ohp::formatWriter(ohp::watchName(watch), row, g_sites[watch].site(row), g_watchObject[watch],
                              g_base.load(std::memory_order_relaxed),
                              g_imageSize.load(std::memory_order_relaxed), line, sizeof(line)),
            line);
}

void onFootHudProbeWatchDisarm(int watch, const char* why, OnFootHudProbeSink sink, void* ctx) {
    if (!onFootHudProbeWatchOn(watch)) return;
    const unsigned slot = ohp::kWatchSlot[watch];
    const uintptr_t address = g_watchAddress[watch].load(std::memory_order_acquire);
    const unsigned length = g_watchLength[watch].load(std::memory_order_acquire);
    for (size_t i = 0; i < g_watchTidCount[watch]; ++i) setThreadWatchSafe(g_watchTids[watch][i], slot, address, length, false);
    g_watchTidCount[watch] = 0;
    g_watchActive[watch].store(false, std::memory_order_release);
    onFootHudProbeWatchDrain(watch, sink, ctx);
    char line[400];
    say(sink, ctx, ohp::formatWatchOff(ohp::watchName(watch), address, why, g_sites[watch], line, sizeof(line)),
        line);
}

void onFootHudProbeStopWatches(const char* why, OnFootHudProbeSink sink, void* ctx) {
    for (int f = 0; f < ohp::kWatchCount; ++f) onFootHudProbeWatchDisarm(f, why, sink, ctx);
}

namespace {

// The frame thread's side of the graph: the cells' new nodes and graphs, each cell's state, the events (the providers are .19's and the
// gates and the watch .20's, drainProviders and drainGates below).
void drainGraph(uint64_t nowMs, int onFoot, OnFootHudProbeSink sink, void* ctx) {
    static ohp::StateLog stateLog;
    static ohp::GraphSummary summary;
    static uint64_t saidNode[ohp::kTracked] = {}, saidGraph[ohp::kTracked] = {};
    static ohp::State current[ohp::kTracked] = {};
    char line[1600];
    const uintptr_t base = g_base.load(std::memory_order_relaxed);
    const uint64_t imageSize = g_imageSize.load(std::memory_order_relaxed);

    // The cells: a new node, a new graph.
    for (int c = 0; c < ohp::kTracked; ++c) {
        const uint64_t key = g_cells[c].key.load(std::memory_order_acquire);
        const uint64_t node = g_cells[c].node.load(std::memory_order_acquire);
        if (!key || !node) continue;
        const int kind = static_cast<int>((key >> 32) - 1);
        const int32_t bit = static_cast<int32_t>(static_cast<uint32_t>(key));
        if (node != saidNode[c]) {
            say(sink, ctx, ohp::formatTracked(kind, bit, node, saidNode[c], line, sizeof(line)), line);
            saidNode[c] = node;
            saidGraph[c] = 0;
            current[c] = ohp::kStateUnknown;
            stateLog.forget(c);
        }
        const uint64_t graph = g_cells[c].graph.load(std::memory_order_relaxed);
        if (graph && graph != saidGraph[c]) {
            say(sink, ctx, ohp::formatGraphOf(kind, bit, node, graph, saidGraph[c], line, sizeof(line)), line);
            saidGraph[c] = graph;
        }
    }

    // Each cell's state this frame.
    const uint32_t word = g_seen.exchange(0, std::memory_order_acq_rel);
    const bool run = (word & ohp::kWalkedRun) != 0, idle = (word & ohp::kWalkedIdle) != 0;
    ohp::TrackedView views[ohp::kTracked];
    int nViews = 0;
    for (int c = 0; c < ohp::kTracked; ++c) {
        const uint64_t key = g_cells[c].key.load(std::memory_order_acquire);
        const uint64_t node = saidNode[c];
        if (!key || !node) continue;
        const int kind = static_cast<int>((key >> 32) - 1);
        const int32_t bit = static_cast<int32_t>(static_cast<uint32_t>(key));
        const uint32_t seen = ohp::seenOf(word, c);
        const uint64_t graph = g_cells[c].graph.load(std::memory_order_relaxed);
        const int holds = !seen && (run || idle) ? ohp::graphHolds(graph, node, &sehRead, nullptr) : -1;
        const ohp::State s = ohp::classify(seen, run, idle, holds);
        if (s != ohp::kStateUnknown) current[c] = s;
        if (stateLog.admit(c, s, nowMs)) {
            uint8_t flag = 0, on = 0;
            const int f = sehRead(nullptr, node + ohp::kNodeOn, &flag, 1) ? flag : -1;
            const int g = graph && sehRead(nullptr, graph + ohp::kGraphOn, &on, 1) ? on : -1;
            say(sink, ctx, ohp::formatState(kind, bit, s, node, f, graph, g, onFoot, line, sizeof(line)), line);
        }
        views[nViews++] = ohp::TrackedView{kind, bit, current[c]};
    }

    // The events.
    ohp::GraphEvent e;
    while (g_events.pop(&e)) say(sink, ctx, ohp::formatGraphEvent(e, onFoot, base, imageSize, line, sizeof(line)), line);

    const ohp::GraphCounts total{g_walks.load(std::memory_order_relaxed), g_idleWalks.load(std::memory_order_relaxed),
                                 g_walkFaults.load(std::memory_order_relaxed), g_graphPostGui.load(std::memory_order_relaxed),
                                 g_graphOther.load(std::memory_order_relaxed), g_events.dropped(),
                                 g_sites[ohp::kWatchMeshes].hits() + g_sites[ohp::kWatchMeshCount].hits(), stateLog.held()};
    say(sink, ctx, summary.step(nowMs, total, views, nViews, line, sizeof(line)), line);
}

// .20, from drainProviders: the _HUD provider's gates; .21: the HUD part and its watches. Every frame: the walks the ring brought (the
// latest matched one of each view is kept; a matched walk in cinema may teach the HUD part), each view's gate census when its picture
// changed and each view's census of the part and the elements the same way (a view's at most every 5 s, the latest pending one kept), the
// first walk that did not match and the collectors' table, once. Every 30 s: the walks by place and the part's reads by place. The watches
// stand from the moment the part is learned, slot 2 on its array of meshes and slot 3 on their number. They move, with a line, when the
// cinema walk learns another part; come off, with a line, when the part has been out of the record of the view it was learned in for 2 s
// while that node keeps being drawn (and are set again when it is back); are set again on the threads started since when the journal's
// on-foot changes; come off with the key; and each comes off for good after 100 000 hits in 30 s.
void drainGates(uint64_t nowMs, int onFoot, OnFootHudProbeSink sink, void* ctx) {
    static ohp::GateSummary summary;
    static ohp::PartSummary partSummary;
    static ohp::Fuse fuse[ohp::kWatchCount];
    static ohp::WatchSummary watchSummary[ohp::kWatchCount];
    static ohp::GateWalk rec;
    static ohp::GateWalk pending[ohp::kProvViews], lastMatched[ohp::kProvViews], partPending[ohp::kProvViews];
    static bool hasPending[ohp::kProvViews] = {}, hasMatched[ohp::kProvViews] = {}, saidView[ohp::kProvViews] = {};
    static bool hasPartPending[ohp::kProvViews] = {}, saidPart[ohp::kProvViews] = {};
    static uint64_t saidPicture[ohp::kProvViews] = {}, saidMs[ohp::kProvViews] = {}, partSaidPicture[ohp::kProvViews] = {}, partSaidMs[ohp::kProvViews] = {};
    static uint64_t lastSnaps[ohp::kProvViews] = {}, lastGrowMs[ohp::kProvViews] = {};
    static int saidPlace[ohp::kProvViews] = {};
    // The watches: wanted (the part is learned and in the record), the generation they stand for, the address no thread took (per watch),
    // when the part was first missing from the record of the view it was learned in, each one's fuse.
    static bool wanted = false, fused[ohp::kWatchCount] = {};
    static uint32_t watchedGeneration = 0;
    static uint64_t refused[ohp::kWatchCount] = {}, absentSince = 0;
    static int lastOnFoot = -2;
    constexpr uint64_t kNotDrawnMs = 500;
    char line[1600];
    const uintptr_t base = g_base.load(std::memory_order_relaxed);
    const uint64_t imageSize = g_imageSize.load(std::memory_order_relaxed);
    ohp::HudPartKey key;
    bool haveKey = g_hudPart.get(&key);

    // The part is learned from a matched walk in cinema: the part with the most open _HUD paths, when none is learned yet or the learned
    // one is not in the record of its view (or has no open _HUD path there) in this walk. Said once per part.
    const auto learn = [&](const ohp::GateWalk& walk) {
        const ohp::PartPick& pick = walk.pick;
        if (pick.open == 0) return;
        bool changed = false;
        if (haveKey) {
            const ohp::PartRead& r = walk.part;
            const bool gone = r.looked && r.generation == key.generation && walk.viewBit == key.viewBit && (!r.found || r.openPaths == 0);
            if (!gone || pick.part == key.part) return;
            changed = true;
        }
        ohp::HudPartKey next{pick.group, pick.item, pick.part, pick.groupIndex, pick.itemIndex, pick.partIndex, walk.viewBit, 0};
        const uint64_t was = haveKey ? key.part : 0;
        next.generation = g_hudPart.publish(next);
        key = next;
        haveKey = true;
        if (changed) say(sink, ctx, ohp::formatPartChanged(was, next, pick.open, line, sizeof(line)), line);
        else say(sink, ctx, ohp::formatPartLearned(next, pick.open, line, sizeof(line)), line);
    };

    // The walks: each matched one is the view's latest, a cinema one may teach the part, the part's absence is timed; a census is pending
    // when the picture or the place is not the one written last.
    while (g_gateRing.pop(&rec)) {
        const int v = rec.viewSlot;
        if (v < 0 || v >= ohp::kProvViews) continue;
        if (rec.result == ohp::kWalkMatched) {
            lastMatched[v] = rec;
            hasMatched[v] = true;
            if (rec.place == static_cast<uint32_t>(ohp::kPlaceCinema)) learn(rec);
        }
        if (haveKey && rec.part.looked && rec.part.generation == key.generation && rec.viewBit == key.viewBit) {
            if (rec.part.verdict == ohp::kPartAbsent) {
                if (!absentSince) absentSince = nowMs;
            } else {
                absentSince = 0;
                if (rec.part.found) wanted = true;
            }
        }
        if (!saidView[v] || ohp::gatePicture(rec) != saidPicture[v] || static_cast<int>(rec.place) != saidPlace[v]) {
            pending[v] = rec;
            hasPending[v] = true;
        } else {
            hasPending[v] = false;
        }
        if (!saidPart[v] || ohp::partPicture(rec) != partSaidPicture[v]) {
            partPending[v] = rec;
            hasPartPending[v] = true;
        } else {
            hasPartPending[v] = false;
        }
    }
    for (int v = 0; v < ohp::kProvViews; ++v) {
        if (!hasPending[v] || (saidView[v] && nowMs - saidMs[v] < ohp::kGateLineMs)) continue;
        hasPending[v] = false;
        saidView[v] = true;
        saidMs[v] = nowMs;
        saidPicture[v] = ohp::gatePicture(pending[v]);
        saidPlace[v] = static_cast<int>(pending[v].place);
        if (pending[v].result == ohp::kWalkNoRecord) {
            say(sink, ctx, ohp::formatGateNoRecord(pending[v], line, sizeof(line)), line);
            continue;
        }
        say(sink, ctx, ohp::formatGateHead(pending[v], line, sizeof(line)), line);
        for (uint32_t i = 0; i < pending[v].stored && i < static_cast<uint32_t>(ohp::kCensusPaths); ++i)
            say(sink, ctx, ohp::formatGatePath(pending[v], i, line, sizeof(line)), line);
    }
    // The part and the elements: the part's read (and, for a part in the record, its meshes and pass entries), then the record's elements.
    for (int v = 0; v < ohp::kProvViews; ++v) {
        if (!hasPartPending[v] || (saidPart[v] && nowMs - partSaidMs[v] < ohp::kGateLineMs)) continue;
        hasPartPending[v] = false;
        saidPart[v] = true;
        partSaidMs[v] = nowMs;
        const ohp::GateWalk& pw = partPending[v];
        partSaidPicture[v] = ohp::partPicture(pw);
        if (pw.part.looked) {
            say(sink, ctx, ohp::formatPartHead(pw, line, sizeof(line)), line);
            for (uint32_t m = 0; pw.part.found && m < pw.part.stored; ++m) {
                say(sink, ctx, ohp::formatPartMesh(pw, m, line, sizeof(line)), line);
                for (uint32_t e = 0; pw.part.mesh[m].read && e < pw.part.mesh[m].entries; ++e) say(sink, ctx, ohp::formatPartEntry(pw, m, e, line, sizeof(line)), line);
            }
        }
        if (pw.result == ohp::kWalkNoRecord) continue;
        say(sink, ctx, ohp::formatElementsHead(pw, line, sizeof(line)), line);
        uint32_t written = 0, more = 0;
        for (uint32_t i = 0; i < pw.tallied; ++i) {
            if (!ohp::elementWorthALine(pw, i)) continue;
            if (written >= static_cast<uint32_t>(ohp::kCensusItems)) {
                ++more;
                continue;
            }
            ++written;
            say(sink, ctx, ohp::formatElementsItem(pw, i, line, sizeof(line)), line);
        }
        if (more) say(sink, ctx, ohp::formatElementsMore(more, line, sizeof(line)), line);
        for (uint32_t i = 0; i < pw.named; ++i) say(sink, ctx, ohp::formatElementsNamed(pw, i, line, sizeof(line)), line);
    }
    int result = 0;
    uint64_t badA = 0, badB = 0, badNode = 0;
    if (g_gateWalks.nextBad(&result, &badA, &badB, &badNode)) say(sink, ctx, ohp::formatGateBad(result, badA, badB, badNode, line, sizeof(line)), line);
    uint64_t fn[ohp::kCollectors];
    int32_t offset[ohp::kCollectors];
    if (g_gateWalks.nextCollectors(fn, offset)) say(sink, ctx, ohp::formatGateCollectors(fn, offset, base, imageSize, line, sizeof(line)), line);

    // The summaries: the window's walks by place and each view's last matched walk by gate; the part's reads by place, or that none is
    // learned.
    uint64_t seconds = 0;
    ohp::GateCounts window[ohp::kPlaces];
    const int due = summary.step(nowMs, g_gateWalks, &seconds, window);
    if (due) {
        uint64_t walks = 0;
        for (const ohp::GateCounts& c : window) walks += c.sum();
        say(sink, ctx, ohp::formatGateSummaryHead(seconds, walks, g_gateRing.dropped(), line, sizeof(line)), line);
        for (int p = 0; p < ohp::kPlaces; ++p)
            if (window[p].sum()) say(sink, ctx, ohp::formatGateSummaryPlace(p, window[p], line, sizeof(line)), line);
        for (int v = 0; v < ohp::kProvViews; ++v)
            if (hasMatched[v]) say(sink, ctx, ohp::formatGateSummaryView(lastMatched[v], line, sizeof(line)), line);
    }
    uint64_t partSeconds = 0;
    ohp::PartCounts partWindow[ohp::kPlaces];
    if (partSummary.step(nowMs, g_partReads, &partSeconds, partWindow)) {
        uint64_t reads = 0, notLearned = 0;
        for (const ohp::PartCounts& c : partWindow) {
            reads += c.sum();
            notLearned += c.notLearned;
        }
        say(sink, ctx, ohp::formatPartSummaryHead(partSeconds, haveKey ? &key : nullptr, reads, notLearned, line, sizeof(line)), line);
        for (int p = 0; p < ohp::kPlaces; ++p)
            if (partWindow[p].sum() || partWindow[p].notLearned) say(sink, ctx, ohp::formatPartSummaryPlace(p, partWindow[p], line, sizeof(line)), line);
    }

    // The watches. A view's HUD node is being drawn while its snapshots grow; the part's absence counts only then.
    bool drawn = false;
    for (int v = 0; v < ohp::kProvViews; ++v) {
        const uint64_t snaps = g_providers.view(v).snaps.load(std::memory_order_relaxed);
        if (snaps != lastSnaps[v]) {
            lastSnaps[v] = snaps;
            lastGrowMs[v] = nowMs;
        }
        drawn = drawn || nowMs - lastGrowMs[v] < kNotDrawnMs;
    }
    const bool rearm = lastOnFoot != -2 && onFoot != lastOnFoot;
    lastOnFoot = onFoot;
    const auto targetOf = [&](int w) -> uint64_t { return key.part + (w == ohp::kWatchMeshes ? ohp::kPartMeshes : ohp::kPartMeshCount); };
    if (haveKey && key.generation != watchedGeneration) {  // a new part: the watches that stand move to it
        watchedGeneration = key.generation;
        for (int w = 0; w < ohp::kWatchCount; ++w) {
            refused[w] = 0;
            if (!onFootHudProbeWatchOn(w)) continue;
            say(sink, ctx, ohp::formatWatchMoves(ohp::watchName(w), onFootHudProbeWatchAddress(w), targetOf(w), line, sizeof(line)), line);
            onFootHudProbeWatchDisarm(w, "the cinema walk learned another HUD part", sink, ctx);
        }
        wanted = true;
        absentSince = 0;
    }
    if (wanted && absentSince) {
        if (!drawn) {
            absentSince = nowMs;  // not drawn lately: absence is not measured
        } else if (nowMs - absentSince >= ohp::kGateStaleMs) {
            for (int w = 0; w < ohp::kWatchCount; ++w) onFootHudProbeWatchDisarm(w, "the HUD part has been out of the record for 2 s", sink, ctx);
            wanted = false;
            absentSince = 0;
        }
    }
    if (rearm)
        for (int w = 0; w < ohp::kWatchCount; ++w) onFootHudProbeWatchDisarm(w, "on foot changed: re-armed for threads started since", sink, ctx);
    if (wanted && haveKey) {
        for (int w = 0; w < ohp::kWatchCount; ++w) {
            if (fused[w] || onFootHudProbeWatchOn(w)) continue;
            const uint64_t at = targetOf(w);
            if (at == refused[w] && !rearm) continue;
            const OnFootHudProbeWatch target{static_cast<uintptr_t>(at), 8, key.part,
                                             w == ohp::kWatchMeshes ? "the HUD part's array of meshes" : "the HUD part's number of meshes"};
            if (!onFootHudProbeWatchArm(w, target, sink, ctx)) refused[w] = at;
        }
    }
    for (int w = 0; w < ohp::kWatchCount; ++w) {
        onFootHudProbeWatchDrain(w, sink, ctx);
        if (onFootHudProbeWatchOn(w))
            say(sink, ctx, watchSummary[w].step(nowMs, ohp::watchName(w), onFootHudProbeWatchAddress(w), g_sites[w], line, sizeof(line)), line);
        if (!fused[w] && fuse[w].step(nowMs, g_sites[w].hits())) {
            fused[w] = true;
            onFootHudProbeWatchDisarm(w, "over 100000 hits in 30 s: the place is written all the time; the watch stays off", sink, ctx);
        }
    }
}

// .21, from drainProviders: the draws of the HUD node. Every frame: the census of each (view bit, place) whose picture of shapes changed
// (up to 16 shapes by list, each view at most every 5 s), and, every 30 s, the windows and draws by place, list and context, and the
// draws outside the node.
void drainShapes(uint64_t nowMs, OnFootHudProbeSink sink, void* ctx) {
    static ohp::ShapeSummary summary;
    struct Group {
        int32_t viewBit = 0;
        int place = 0;
        bool used = false;
        uint64_t picture = 0, ms = 0;
    };
    static Group groups[ohp::kShapeGroups];
    char line[1600];
    struct Now {
        int32_t viewBit = 0;
        int place = 0;
        bool used = false;
        uint64_t picture = 0, draws = 0;
        uint32_t shapes = 0;
    } now[ohp::kShapeGroups];
    for (int i = 0; i < ohp::kShapeRows; ++i) {
        uint64_t key = 0, hits = 0;
        const ohp::DrawShape* s = nullptr;
        if (!g_shapes.row(i, &key, &hits, &s)) continue;
        int g = 0;
        while (g < ohp::kShapeGroups && now[g].used && !(now[g].viewBit == s->viewBit && now[g].place == s->place)) ++g;
        if (g == ohp::kShapeGroups) continue;
        now[g].used = true;
        now[g].viewBit = s->viewBit;
        now[g].place = s->place;
        now[g].picture += key;
        now[g].draws += hits;
        ++now[g].shapes;
    }
    for (const Now& n : now) {
        if (!n.used) continue;
        Group* at = nullptr;
        for (Group& g : groups)
            if (g.used && g.viewBit == n.viewBit && g.place == n.place) at = &g;
        if (!at)
            for (Group& g : groups)
                if (!g.used && !at) at = &g;
        if (!at) continue;
        const bool fresh = !at->used;
        if (!fresh && (at->picture == n.picture || nowMs - at->ms < ohp::kShapeLineMs)) continue;
        at->used = true;
        at->viewBit = n.viewBit;
        at->place = n.place;
        at->picture = n.picture;
        at->ms = nowMs;
        say(sink, ctx, ohp::formatShapeHead(n.viewBit, n.place, n.shapes, n.draws, line, sizeof(line)), line);
        uint32_t written = 0, more = 0;
        for (int l = 0; l < ohp::kShapeLists; ++l)
            for (int i = 0; i < ohp::kShapeRows; ++i) {
                uint64_t key = 0, hits = 0;
                const ohp::DrawShape* s = nullptr;
                if (!g_shapes.row(i, &key, &hits, &s) || s->list != l || s->viewBit != n.viewBit || s->place != n.place) continue;
                if (written >= static_cast<uint32_t>(ohp::kCensusShapes)) {
                    ++more;
                    continue;
                }
                ++written;
                say(sink, ctx, ohp::formatShape(*s, hits, line, sizeof(line)), line);
            }
        if (more) say(sink, ctx, ohp::formatShapesMore(more, line, sizeof(line)), line);
    }
    uint64_t seconds = 0;
    ohp::ShapeWindow win;
    if (summary.step(nowMs, g_windows.opened(), g_shapes, &seconds, &win)) {
        say(sink, ctx, ohp::formatShapeSummaryHead(seconds, win, static_cast<uint32_t>(g_shapes.kept()), line, sizeof(line)), line);
        for (int p = 0; p < ohp::kPlaces; ++p) {
            uint64_t inNode = 0;
            for (int l = 0; l < ohp::kShapeListOutside; ++l) inNode += win.n[p][l][0] + win.n[p][l][1];
            if (inNode) say(sink, ctx, ohp::formatShapeSummaryPlace(p, win, line, sizeof(line)), line);
        }
        say(sink, ctx, ohp::formatShapeSummaryOutside(win, line, sizeof(line)), line);
    }
}

// .19, from the camera drain: the HUD node's providers. Every frame: the place for the hook and the handler, the lists the ring brought (a
// view's at most every 5 s, the latest kept), the first snapshot that did not match; every 30 s the providers' summary; then .20's gates with
// .21's part and watches, and .21's draw shapes.
void drainProviders(uint64_t nowMs, int onFoot, int stereo, OnFootHudProbeSink sink, void* ctx) {
    static ohp::ProviderSummary summary;
    static ohp::ProviderSnap pending[ohp::kProvViews];
    static bool hasPending[ohp::kProvViews] = {}, saidView[ohp::kProvViews] = {};
    static uint64_t saidMs[ohp::kProvViews] = {};
    char line[1600];
    const uintptr_t base = g_base.load(std::memory_order_relaxed);
    const uint64_t imageSize = g_imageSize.load(std::memory_order_relaxed);
    g_place.store(ohp::placeOf(onFoot, stereo), std::memory_order_relaxed);

    // The lists: a view's latest waits for its 5 seconds.
    ohp::ProviderSnap rec;
    while (g_providerRing.pop(&rec))
        if (rec.viewSlot >= 0 && rec.viewSlot < ohp::kProvViews) {
            pending[rec.viewSlot] = rec;
            hasPending[rec.viewSlot] = true;
        }
    for (int v = 0; v < ohp::kProvViews; ++v) {
        if (!hasPending[v] || (saidView[v] && nowMs - saidMs[v] < ohp::kProvLineMs)) continue;
        hasPending[v] = false;
        saidView[v] = true;
        saidMs[v] = nowMs;
        say(sink, ctx, ohp::formatProviderHead(pending[v], line, sizeof(line)), line);
        for (uint32_t i = 0; i < pending[v].stored; ++i) say(sink, ctx, ohp::formatProviderLine(pending[v], i, base, imageSize, line, sizeof(line)), line);
    }
    int reason = 0;
    uint64_t badA = 0, badB = 0, badNode = 0;
    if (g_providers.nextBad(&reason, &badA, &badB, &badNode)) say(sink, ctx, ohp::formatBadPair(reason, badA, badB, badNode, line, sizeof(line)), line);

    // The summary: the window's snapshots (or none), the totals, the providers that were ever subscribed or answered, and, when none ever
    // answered with 0x8, that.
    uint64_t seconds = 0, snaps = 0;
    const int due = summary.step(nowMs, g_providers.attempts(), &seconds, &snaps);
    if (due) {
        say(sink, ctx, ohp::formatProviderSummary(seconds, snaps, g_providers, g_providerRing.dropped(), line, sizeof(line)), line);
        for (int i = 0; due == 1 && i < ohp::kProvRows; ++i) {
            const auto& r = g_providers.rowAt(i);
            if (ohp::providerWorthALine(r))
                say(sink, ctx, ohp::formatProviderRow(r, g_providers.viewBitOf(ohp::ProviderTable::rowView(r)), base, imageSize, line, sizeof(line)), line);
        }
        if (g_providers.matched() > 0 && !g_providers.hudSeen()) say(sink, ctx, ohp::formatNoHudProvider(line, sizeof(line)), line);
    }
    drainGates(nowMs, onFoot, sink, ctx);
    drainShapes(nowMs, sink, ctx);
}

}  // namespace

void onFootHudProbeDrain(uint64_t nowMs, int onFoot, OnFootHudProbeSink sink, void* ctx) {
    static ohp::ChangeFilter filter;
    static ohp::Summary summary;
    static bool saidFault = false;
    static uint64_t firstDrainMs = 0;
    char line[1600];
    // The census: off as soon as a PostGUI node came; written once when none came in the first 20 s of recording.
    if (!firstDrainMs) firstDrainMs = nowMs ? nowMs : 1;
    if (g_censusOn.load(std::memory_order_relaxed)) {
        if (g_calls.load(std::memory_order_relaxed) > 0) {
            g_censusOn.store(false, std::memory_order_relaxed);
        } else if (nowMs - firstDrainMs >= ohp::kCensusAfterMs) {
            g_censusOn.store(false, std::memory_order_relaxed);
            ohp::Census::Kind kinds[ohp::Census::kCells];
            const int n = g_census.kinds(kinds, ohp::Census::kCells);
            say(sink, ctx, ohp::formatCensusHead(n, g_census.overflow(), line, sizeof(line)), line);
            for (int i = 0; i < n; ++i) say(sink, ctx, ohp::formatCensusKind(kinds[i], line, sizeof(line)), line);
        }
    }
    ohp::Snapshot s, write;
    while (g_ring.pop(&s)) {
        s.onFoot = onFoot;
        if (s.first == ohp::kHudFirst) g_masks.note(s.cam, s.ctx, s.ctxMask, nowMs);  // every draw, before the filter: the pose lines' mask
        if (filter.admit(s, nowMs, &write)) say(sink, ctx, ohp::formatSnapshot(write, line, sizeof(line)), line);
    }
    while (filter.due(nowMs, &write)) say(sink, ctx, ohp::formatSnapshot(write, line, sizeof(line)), line);
    const uint64_t faultNode = g_faultNode.load(std::memory_order_relaxed);
    if (faultNode && !saidFault) {
        saidFault = true;
        say(sink, ctx, ohp::formatFault(faultNode, line, sizeof(line)), line);
    }
    const ohp::SummaryCounts total{g_draws.load(std::memory_order_relaxed), g_calls.load(std::memory_order_relaxed), filter.written(), filter.repeats(), filter.held(),
                                   g_ring.dropped(), g_faults.load(std::memory_order_relaxed)};
    say(sink, ctx, summary.step(nowMs, total, filter.nodes(nowMs, ohp::Summary::kWindowMs), line, sizeof(line)), line);
    drainGraph(nowMs, onFoot, sink, ctx);
}

void onFootHudProbeCameraDrain(uint64_t nowMs, int onFoot, int stereo, const float* head12, OnFootHudProbeSink sink, void* ctx) {
    // Per cell: the pass seen there, its calls and when it was last called, and the sample its next yaw step starts from.
    struct Track {
        uint64_t pass = 0, calls = 0, calledMs = 0, sampleMs = 0;
        bool sampled = false;
        int mode = -1;
        float mainYaw = 0, postYaw = 0, headYaw = 0;
    };
    static Track track[ohp::PassTable::kCells];
    static ohp::Follow follow[ohp::kViews][2];
    static ohp::SwitchWindow window;
    static bool started = false;
    static uint64_t lastMs = 0, nextSampleMs = 0, windowMs = 0, overflowBase = 0;
    static int lastOnFoot = -2;
    char line[1600];
    // The first drain, or the first after recording was off: every pass counts as called now, not as gone.
    const bool resumed = !started || nowMs - lastMs > ohp::kCalledMs;
    if (!started) {
        started = true;
        windowMs = nowMs;
        overflowBase = g_passes.overflow();
    }
    lastMs = nowMs;

    // The cells: a new pass, its calls, a pass the game no longer calls.
    for (int c = 0; c < ohp::PassTable::kCells; ++c) {
        Track& t = track[c];
        const uint64_t pass = g_passes.pass(c), calls = g_passes.calls(c);
        if (pass != t.pass) {
            t = Track{};
            t.pass = pass;
            t.calls = calls;
            t.calledMs = nowMs;
            if (pass) {
                ++window.fresh;
                ohp::PassPose p;
                ohp::readPass(pass, &sehRead, nullptr, &p);
                say(sink, ctx, ohp::formatNewPass(p, line, sizeof(line)), line);
            }
            continue;
        }
        if (!pass) continue;
        if (calls != t.calls) {
            window.calls += calls - t.calls;
            t.calls = calls;
            t.calledMs = nowMs;
        } else if (resumed) {
            t.calledMs = nowMs;
        } else if (nowMs - t.calledMs >= ohp::kRetireMs) {
            g_passes.retire(c, pass);
            ++window.retired;
            t = Track{};
        }
    }

    // The samples: every 0.5 s on foot, 5 s elsewhere, and at once when on foot changes. Each pass called within the last second gives
    // a pose line, and a yaw step from its sample before when that one is near enough and in the same mode.
    if (onFoot != lastOnFoot) {
        lastOnFoot = onFoot;
        nextSampleMs = nowMs;
    }
    if (nowMs >= nextSampleMs) {
        nextSampleMs = nowMs + (onFoot == 1 ? ohp::kSampleOnFootMs : ohp::kSampleElsewhereMs);
        const float headYaw = head12 ? ohp::headAngles(head12).yaw : NAN;
        for (int c = 0; c < ohp::PassTable::kCells; ++c) {
            Track& t = track[c];
            if (!t.pass || nowMs - t.calledMs > ohp::kCalledMs) continue;
            ohp::PassPose p;
            ohp::readPass(t.pass, &sehRead, nullptr, &p);
            uint64_t mask = 0;
            const int by = g_masks.find(p.post.cam, nowMs, &mask) ? 1 : g_masks.find(p.main.cam, nowMs, &mask) ? 2 : 0;
            say(sink, ctx, ohp::formatPose(p, onFoot, stereo, head12, by, mask, line, sizeof(line)), line);
            ++window.poses;
            if (!p.main.read || !p.post.read) {
                t.sampled = false;
                continue;
            }
            const float mainYaw = ohp::cameraAngles(p.main.m).yaw, postYaw = ohp::cameraAngles(p.post.m).yaw;
            if (t.sampled && t.mode == stereo && nowMs - t.sampleMs <= ohp::kStepMs && p.view >= 0 && p.view < ohp::kViews && (stereo == 0 || stereo == 1))
                follow[p.view][stereo].add(ohp::wrapDeg(mainYaw - t.mainYaw), ohp::wrapDeg(postYaw - t.postYaw), ohp::wrapDeg(headYaw - t.headYaw));
            t.sampled = true;
            t.sampleMs = nowMs;
            t.mode = stereo;
            t.mainYaw = mainYaw;
            t.postYaw = postYaw;
            t.headYaw = headYaw;
        }
    }

    // Every 30 s: each view and mode's follow line, then the switch's.
    if (nowMs - windowMs >= ohp::kFollowWindowMs) {
        const uint64_t seconds = (nowMs - windowMs) / 1000;
        windowMs = nowMs;
        for (int v = 0; v < ohp::kViews; ++v) {
            for (int m = 0; m < 2; ++m) {
                if (follow[v][m].any()) say(sink, ctx, ohp::formatFollow(v, m, seconds, follow[v][m], line, sizeof(line)), line);
                follow[v][m] = ohp::Follow{};
            }
        }
        uint64_t passes[ohp::PassTable::kCells] = {};
        int32_t views[ohp::PassTable::kCells] = {};
        int n = 0;
        for (const Track& t : track) {
            if (!t.pass) continue;
            passes[n] = t.pass;
            if (!sehRead(nullptr, t.pass + ohp::kPassView, &views[n], sizeof(views[n]))) views[n] = -1;
            ++n;
        }
        const uint64_t overflow = g_passes.overflow();
        window.overflow = overflow - overflowBase;
        overflowBase = overflow;
        say(sink, ctx, ohp::formatSwitchSummary(seconds, window, passes, views, n, line, sizeof(line)), line);
        window = ohp::SwitchWindow{};
    }

    // .19, .20 and .21: the providers, the _HUD provider's gates, the HUD part and its watches, the draw shapes.
    drainProviders(nowMs, onFoot, stereo, sink, ctx);
}

void onFootHudProbeSetPlace(int place) { g_place.store(place, std::memory_order_relaxed); }
void onFootHudProbeSetWalkInterval(uint64_t ms) { g_walkEveryMs.store(ms, std::memory_order_relaxed); }

void onFootHudProbeFrameBoundary() {
    static uint32_t generation = ~0u;
    static bool on = false, said = false, tried = false;
    static int stereo = 1;  // experimental.native_vr_on_foot, the pose lines' mode
    const Config& cfg = Config::get();
    const uint32_t now = cfg.generation();
    if (now != generation) {
        generation = now;
        stereo = cfg.getBool("experimental.native_vr_on_foot", true) ? 1 : 0;
        const bool want = cfg.getBool("experimental.on_foot_hud_probe", false);
        if (!said || want != on) {
            char line[200];
            if (ohp::formatKey(want, anyInstalled(), line, sizeof(line)) > 0) Log::get().note("%s", line);
            said = true;
        }
        on = want;
    }
    if (on && !tried) {
        tried = true;  // one attempt per launch: a refusal is said once, not every frame
        const uintptr_t base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
        onFootHudProbeInstall(base, 0, true, &logSink, nullptr);
        // Another build refuses every point; wrong bytes at one point refuse only that one.
        if (g_identityOk)
            for (int p = ohp::kPointWalk; p < ohp::kPointCount; ++p) onFootHudProbeInstallPoint(p, base, 0, &logSink, nullptr);
    }
    const bool recording = on && anyInstalled();
    onFootHudProbeSetRecording(recording);
    if (recording) {
        const int onFoot = journalOnFootKnown() ? (journalOnFoot() ? 1 : 0) : -1;
        const uint64_t nowMs = GetTickCount64();
        onFootHudProbeDrain(nowMs, onFoot, &logSink, nullptr);
        float head[12];
        onFootHudProbeCameraDrain(nowMs, onFoot, stereo, headPose(head) ? head : nullptr, &logSink, nullptr);
    } else {
        onFootHudProbeStopWatches("the key is off", &logSink, nullptr);
    }
}

}  // namespace edvr
