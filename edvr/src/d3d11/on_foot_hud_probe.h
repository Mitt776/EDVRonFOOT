#pragma once
// On-foot HUD probe (EDVRonFOOT; experimental.on_foot_hud_probe; the project's docs\research\native-vr-on-foot.md, "Разведка: где игра
// рисует пеший HUD"). OBSERVATION ONLY.
//
// In headset stereo on foot the game builds its PostGUI pass -- render lists 36-39 (PostGui_FirstPerson_*, the on-foot HUD) and 40-46
// -- yet the HUD never reaches the headset. This probe answers where it is lost: it hooks the game's list-range node draw
// (EliteDangerous64.exe+0x58F090, build 334925 only), calls the original unchanged and then, for the PostGUI pass's nodes only, writes
// down the view, its camera, whether the camera's mask passes each list and in which camera "pockets" the list holds models
// (on_foot_hud_probe_core.h has the game's fields).
//
// THREADS. The hook runs on whichever game thread draws the node. It takes no lock, allocates nothing and writes no log line: it
// reads under SEH and pushes one fixed-size snapshot into a lock-free ring. The frame thread writes the ring into the log
// (onFootHudProbeFrameBoundary): a node's line only when it changed, at most one a second per node, and a summary every 30 s.
//
// If no PostGUI node comes in the first 20 s of recording, the hook has meanwhile counted every other node by its kind (vtable,
// first list, number of lists), and the frame thread writes that census once: a walk on which ours could not be told still shows
// what there was (flight .11 lost a walk to a wrong filter).
//
// The hook goes in the first time the key is on and stays until the game exits; with the key off it only forwards. On another build,
// or when the function does not begin with build 334925's bytes, nothing is patched and one line says why.
//
// .13 (flight .12: on foot in stereo the lists 36-39 node is never drawn): three more hooks and two write watches find out why.
//   - The node draw hook also keeps the PostGUI nodes it sees, per kind and view (four cells).
//   - The graph walk hook (EliteDangerous64.exe+0x6BF980), BEFORE the original, looks for them in the walked graphs: in an enabled
//     graph and on, their flag +0x30 off (H1), their graph off (H3). The frame thread turns a frame's findings into a state per node:
//     on, H1, H3, out of every graph (H2), or its graph not walked -- a line on each change.
//   - The graph add and remove hooks (+0x6BFC60, +0x6BFE60), after the original, put a PostGUI node's event with the game's callers in
//     a ring; the frame thread writes a line per event.
//   - Hardware write watches on the HUD node's flag (debug slot 2) and its graph's flag +0x14 (slot 3) found the one writer (H1);
//     .16 gave both slots to new targets, .19 slot 2 to the provider subscription, .20 slot 2 to a gate, .21 slots 2 and 3 to the HUD part (below).
// Each point checks its own 16 bytes: wrong bytes refuse that one hook with one line and leave the others.
//
// .15 (flight .14: the lists 36-39 node is drawn on foot in stereo, but only the crosshair and the prompts, sliding off their objects
// as the head turns): a fifth hook on the PostGUI pass switch (EliteDangerous64.exe+0x28AD4A0), BEFORE the original, only puts the pass
// in a four-cell table. The frame thread says each new pass, gives back the cell of one the game no longer calls, and every 0.5 s on
// foot (5 s elsewhere) writes a pose line per pass: the view, the main and the PostGUI camera's yaw, pitch, roll and position, the
// PostGUI camera's raw rows, the head's yaw and pitch, the frame mask of the HUD node drawn with that camera, the mode
// (experimental.native_vr_on_foot). Every 30 s, per view and mode: how the PostGUI camera's yaw followed the main camera's, and the
// main camera's the head's; and the switch's own line (its calls: none means the hook never ran).
//
// .16 (flight .15: on foot in stereo the PostGUI camera holds the head's pose, and the frame never asks for list 39 _HUD) put hardware
// write watches on view 0's PostGUI camera and on the frame mask; flight .16 answered both (the camera's writer is 0x59F300 from
// 0x287D660, the mask's is the node's prepare 0x58F940) and the two watches are gone.
//
// .19 (flight .16: the prepare asks the scene's providers, and the mask is the OR of their answers with the camera's constant lists --
// which provider gives _HUD in cinema, and what does it do in stereo?): two things on the HUD node (lists 36-39) only.
//   - The node draw hook reads, after the original, the provider list the node's prepare walked (ctx+0x108: per provider the vtable,
//     the answering method vt+0x68, the subscription +0x28, the answer +0x30). A snapshot counts only when the draw context is the
//     node's (ctx+0x150) and the answers' OR with cam+0x8F0 gives the frame mask ctx+0x270; the others are counted by reason and the
//     first pair that did not match is written once. A matching one updates a lock-free table by provider and view: per place (ship,
//     on foot in stereo, in cinema) its frames, the frames it was subscribed to the camera, the frames it answered with _HUD (0x8). A
//     list whose shape (providers, vtables, who is subscribed) or place is new goes to a four-slot ring.
//   - The frame thread writes each view's list from the ring (a head line, a line per provider with the vtable and the answering
//     method as EliteDangerous64.exe+0x... offsets), at most every 5 s per view, and every 30 s a summary: the snapshots that matched
//     and did not, or that there were none (the node not drawn -- not the providers silent), and per provider that was ever subscribed
//     or answered its three counts by place and its last subscription and answer.
//   - (.19's hardware write watch on the first _HUD provider's subscription answered its question -- flight .19, outcome P2: the
//     subscription is on every camera -- and is gone; its slot became .20's gate watch, then .21's part watch, below.)
//
// .20 (flight .19: the _HUD provider B answers 0x8 in cinema and not in stereo on foot, subscribed either way; its answer is its record's
// for the node's camera and set, made of the gates of its meshes' passes): two things on the _HUD provider only.
//   - After a matched snapshot of the HUD node that lists B (vtable +0x5A72D58) the hook walks B's record for the node's camera and set,
//     at most once per second per view: groups, items, parts, meshes, every section of every mesh and every pass entry of it, and reads
//     all seven gates of each path (the flags of the binding's slot at the group, item and part, the mesh's camera mask against the
//     binding's, the instances of the section, the material's pass for the set's view, its on byte). A path whose material ever gave a
//     pass with 0x8 is a _HUD path: the walk names its first closed gate, or open, and the pass of the view the material was seen with
//     0x8 at. The OR of the open paths is checked against the record's answer and B's own +0x30 of this frame (what the record has more
//     is the seven collectors'; a bit the paths give and it does not is a misread walk). Nothing is written by the hook: a walk is a few
//     hundred reads under SEH on the thread that draws the node, a result in a ring of two, and counters.
//   - The frame thread writes a view's census (head, first closed gates counted, up to 16 _HUD paths) when its picture changed or the
//     place did, at most every 5 s per view; every 30 s the walks by place and each view's last matched census; the first walk that did
//     not match and the seven collectors' addresses, once. It remembers each _HUD path's last cinema state.
//   - (.20's hardware write watch on the word of a gate never stood: flight .20 answered Q8, the gates are not the cause. It is gone; its
//     slot is .21's below.)
//
// .21 (flight .20, Q8: in cinema the main HUD is two meshes of one part of the _HUD provider's record, in stereo no mesh gives _HUD -- is
// the part empty there, or are its materials without a _HUD pass for the view? and what are the textures of list 39?): three things.
//   - The cinema walk learns the HUD part (the part with the most open _HUD paths: its group, item and part, their places in the arrays).
//     Every walk after it, in any place, reads that part whole -- whether it is in the record (if not, what stands at its places), its
//     flags, its meshes, their pass entries and, for each entry's material, the pass of every view 0-15 -- and says the part is empty,
//     has no _HUD pass, has it only for another view, or has it for the set's view. The same walk checks every distinct material of the
//     record (at most 1024) for every view and counts, per item (the record's "element"), those with the _HUD list for the set's view
//     and those with it only for another view. The frame thread writes the part's read and the elements when their picture changes (no
//     address of a mesh or a material is in it), at most every 5 s per view, and every 30 s the reads by verdict and place -- or that no
//     part was learned yet.
//   - A sixth hook, on the game's list prep (EliteDangerous64.exe+0x592090, BEFORE the original, which it calls unchanged with
//     its three registers): the node draw hook opens a window for its thread while the HUD node (lists 36-39) is drawn, the prep hook
//     sets the window's list from the list's bit. EDVR's draw path (vscreen.cpp, above the foreign-context branch) calls
//     onFootHudProbeNoteDraw for every draw while onFootHudProbeWantsDraws() (one relaxed load): a draw in the window is kept by its
//     shape -- list, view, place, own or foreign context, kind and count, vertex and pixel shader hashes, target 0, the textures of the
//     pixel shader slots 0-7 with size, format, mips and whether they can be drawn into -- and a draw outside the window with one of the
//     HUD meshes' vertex shaders (B10B032B, C4B4B334; own context only) is kept as "outside the node". Up to 256 shapes; the census by
//     view and place is written when it changes (at most every 5 s), the 30-second line counts the draws by place, list and context, or
//     says the node was not drawn / drew nothing on its thread.
//   - Two hardware write watches, set as soon as the part is learned: debug slot 2 on the part's array of meshes (part+0x10), slot 3 on
//     their number (part+0x8), 8 bytes each, every thread. One line per distinct writing instruction (the value, whether it was not 0, the
//     place, the callers), the writes by place in the 30-second line (0 when quiet), moved with a line when the cinema walk learns another
//     part, off with a line when the part is out of the record for 2 s while the node is drawn, re-armed when the journal's on-foot
//     changes, off with the key and for good (each) after 100 000 hits in 30 s. They replace .20's gate watch, which never stood: the
//     gates are not the cause. The gate walk, its census and summaries stay.
//
// .22 (flight .21: where on the screen does each mesh of the HUD node lie, which part of which panel does it show, how is it curved?): the
// draws of the window above are replayed once, without any output to the screen, with stream output (on_foot_hud_layout.cpp, the replay
// in the draw path; the GPU's answer is read at the window's next draws without waiting). Per form of draw in a place: the first at once,
// then at most every 5 s; the layout (vertices as fractions of the screen, the visible part of the panel, a 3x3 grid, the bend, the
// panel's aspect) is a line when new or moved more than 0.01 of the screen, with a CSV of the vertices and a BMP of the panel (8-bit
// RGBA) beside it in <logDir>\hud_layout\<launch time>\, at most 48 of each. The vertex shaders' code is kept at their creation
// (device_hook.cpp) only when the key was on at the launch, 64 MB at most. The files and lines are the frame thread's
// (onFootHudProbeCameraDrain); the 30-second summary counts snapshots and refusals by place and reason or says "no snapshot". Observation
// only: the context's geometry shader, stream-output targets and topology are put back, the draw goes through the game's own draw function.
#include <atomic>
#include <cstddef>
#include <cstdint>

struct ID3D11DeviceContext;

namespace edvr {

using OnFootHudProbeSink = void (*)(void* ctx, const char* line);

// Once a frame at the Present boundary (device_hook.cpp's tkOnFootHudProbe): the key, the install, the drain. Render thread only.
void onFootHudProbeFrameBoundary();

// The pieces the frame boundary is made of, exposed for the rig.
//
// Hook the node draw at base + its RVA, or at `targetOverride` when not 0 (the rig's synthetic function). The node vtable the hook
// accepts is base + its RVA. `checkIdentity` checks base's PE stamp first. One line through `sink` either way. True once hooked
// (later calls return true and say nothing). Frame thread.
bool onFootHudProbeInstall(uintptr_t base, uintptr_t targetOverride, bool checkIdentity, OnFootHudProbeSink sink, void* ctx);
// Hook one of the other points (ohp::kPointWalk, kPointAdd, kPointRemove, kPointPass) at base + its RVA, or at `targetOverride`. No identity check:
// the frame boundary calls it only once onFootHudProbeInstall passed it. One line either way; true once hooked. Frame thread.
bool onFootHudProbeInstallPoint(int point, uintptr_t base, uintptr_t targetOverride, OnFootHudProbeSink sink, void* ctx);
// The two write watches (ohp::kWatchMeshes on debug slot 2, ohp::kWatchMeshCount on slot 3), exposed for the rig. What one watches: the
// word (`address`, 8 bytes, aligned to its length: 1, 4 or 8 are accepted) of the structure `object` (the HUD part), and `note` (or
// null) at the end of the line.
struct OnFootHudProbeWatch {
    uintptr_t address;
    unsigned length;
    uint64_t object;
    const char* note;
};
// Arm puts the slot on every thread of the process that does not hold it already and says one line; returns the threads armed. Drain
// writes the new writers' lines; Disarm takes the slot off them, drains and says why with the totals. Frame thread. SetPlace: where the
// commander is (ohp::Place) for the writes and the provider snapshots that come next; the camera drain sets it every frame. Any thread.
int onFootHudProbeWatchArm(int watch, const OnFootHudProbeWatch& target, OnFootHudProbeSink sink, void* ctx);
bool onFootHudProbeWatchOn(int watch);
uintptr_t onFootHudProbeWatchAddress(int watch);
void onFootHudProbeWatchDrain(int watch, OnFootHudProbeSink sink, void* ctx);
void onFootHudProbeWatchDisarm(int watch, const char* why, OnFootHudProbeSink sink, void* ctx);
void onFootHudProbeStopWatches(const char* why, OnFootHudProbeSink sink, void* ctx);
void onFootHudProbeSetPlace(int place);
// How often a view's _HUD provider record is walked, in ms (kWalkEveryMs, 1000); the rig sets 0 to walk at every draw. Any thread.
void onFootHudProbeSetWalkInterval(uint64_t ms);
// While false the hook only forwards. Any thread. The draw path's flag (onFootHudProbeWantsDraws) is up only while this is true and the
// node draw is hooked.
void onFootHudProbeSetRecording(bool on);
// Calls of PostGUI nodes the hook saw while recording, and snapshots dropped because a field could not be read.
uint64_t onFootHudProbeCalls();
uint64_t onFootHudProbeFaults();
// Write the ring through `sink`: changed nodes, kept-back changes that are due, the fault line once, the 30-second summary; then the
// graph's side: tracked nodes and their graphs, each node's state on a change, the graph events and the graph's 30-second line.
// `onFoot` is the journal's answer (-1 unknown, 0, 1). Frame thread.
void onFootHudProbeDrain(uint64_t nowMs, int onFoot, OnFootHudProbeSink sink, void* ctx);
// .15, after onFootHudProbeDrain (which notes each HUD node's frame mask and draw context by its camera): the passes the switch hook
// noted -- a new one's line, a retired one, the pose lines when due, the follow lines and the switch's line every 30 s; then .19's
// providers (the place, the HUD nodes' lists from the ring, the 30-second summary), .20's gates (the _HUD provider's walks from the ring:
// each view's census, the 30-second walks) and .21's HUD part (learning it, its read and the elements when their picture changes, the
// 30-second reads, the watches: their targets, writers, 30-second lines and the fuse) and draw shapes (the census by view and place, the
// 30-second draws). `stereo` is
// experimental.native_vr_on_foot (1, 0); `head12` the head pose in headPose's layout, or null. Frame thread.
void onFootHudProbeCameraDrain(uint64_t nowMs, int onFoot, int stereo, const float* head12, OnFootHudProbeSink sink, void* ctx);

// ---- .21: the draws of the HUD node ------------------------------------------------------------------------------------------------
// The draw path's one load: up while the key is on and the node draw is hooked (onFootHudProbeSetRecording).
namespace detail {
extern std::atomic<bool> g_onFootHudProbeWantsDraws;
}
inline bool onFootHudProbeWantsDraws() { return detail::g_onFootHudProbeWantsDraws.load(std::memory_order_relaxed); }

namespace ohp {
struct DrawShape;
}
// What a draw is to the probe: 0 not its business, 1 inside the HUD node's draw window of the calling thread, 2 outside it with one of the
// HUD meshes' vertex shaders. `ownVsHash`: the vertex shader's content hash when the draw is on EDVR's own context (the shadow's), else 0.
// For 1 and 2 the list, view bit, place and the draw's order in the list are filled in `shape` (the rest is the caller's).
// Any thread; the draw path's.
int onFootHudProbeDrawClass(uint64_t ownVsHash, ohp::DrawShape* shape);
// A shape the caller filled: counted by place, list and context, and kept in the table (or counted when the table is full). Any thread.
void onFootHudProbeNoteShape(const ohp::DrawShape& shape);

struct DrawArgs;
// The game's own draw by kind ('D', 'I', 'N', 'X') and arguments, NOT through EDVR's hooks (vscreen.cpp's pureDrawReissue: the saved
// originals of the context's draw functions). .22's replay draws with it.
using OnFootHudRealDraw = void (*)(ID3D11DeviceContext* ctx, char kind, unsigned count, unsigned instances, const DrawArgs& args);
// vscreen.cpp's beginPanelOverride, above its foreign-context branch, while onFootHudProbeWantsDraws(): `own` is EDVR's own context.
// Reads the context's state with its getters (vscreen.cpp) and calls onFootHudProbeNoteShape; a draw in the HUD node's window also goes
// to .22's layout snapshot (on_foot_hud_layout.cpp), which replays it with `real`.
void onFootHudProbeNoteDraw(ID3D11DeviceContext* ctx, char kind, unsigned count, unsigned instances, const DrawArgs& args, bool own, OnFootHudRealDraw real);

// ---- .22: the layout snapshot (the D3D11 half is on_foot_hud_layout.cpp; the decisions, the text and the files are here) ---------------
namespace ohp {
struct Layout;
struct RefusalNote;
struct SnapshotResult;
}  // namespace ohp
// The frame number the frame thread counts (once a camera drain): the GPU's answer to a replay is given up on after 30 of them.
uint64_t onFootHudProbeFrame();
// A form's claim for a snapshot (any thread): true with its table row when it is due (the first in a place at once, then every 5 s);
// `prior` is what the row held, for onFootHudLayoutUnclaim when the replay could not go on. A full table is counted.
bool onFootHudLayoutClaim(uint64_t formKey, uint64_t nowMs, int* row, uint64_t* prior);
void onFootHudLayoutUnclaim(int row, uint64_t prior);
// The layout a row's snapshot gave: true when it is a line (the first, or the frame or the 3x3 grid moved more than 0.01 of the screen).
bool onFootHudLayoutDecide(int row, const ohp::Layout& layout);
// A snapshot read (by place, for the summary), a refusal (by reason, the first of its kind also gets a line), a result for the frame thread
// (it takes the ownership; a full queue deletes it and counts "queue full").
void onFootHudLayoutSnapped(int place);
void onFootHudLayoutRefuse(int reason, const ohp::RefusalNote& note);
void onFootHudLayoutSubmit(ohp::SnapshotResult* result);
// Vertex shader code for the snapshots: the budget (64 MB; the first refusal says one line). 0 kept, 1 or 2 refused.
int onFootHudProbeCodeAdd(uint64_t bytes);
// Whether the key was on when the game began creating shaders (read once, the first time asked): hookedCreateVS's one check.
namespace detail {
extern std::atomic<int> g_onFootHudProbeKeepsCode;
}
bool onFootHudProbeKeepsCodeSlow();
inline bool onFootHudProbeKeepsCode() {
    const int v = detail::g_onFootHudProbeKeepsCode.load(std::memory_order_relaxed);
    return v > 0 || (v < 0 && onFootHudProbeKeepsCodeSlow());
}
// The rig's: the folder the files go to (instead of <logDir>\hud_layout\<launch time>); null puts it back.
void onFootHudProbeSetLayoutDirectory(const wchar_t* directory);

}  // namespace edvr
