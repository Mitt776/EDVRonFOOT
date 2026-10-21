#pragma once
// On-foot HUD probe (EDVRonFOOT; experimental.on_foot_hud_probe; the project's docs\research\native-vr-on-foot.md, "Разведка: где игра
// рисует пеший HUD"). OBSERVATION ONLY.
//
// In headset stereo on foot the game builds its PostGUI pass -- render lists 36-39 (PostGui_FirstPerson_*, the on-foot HUD) and 40-46
// -- yet the HUD never reaches the headset. This probe answers where it is lost: it hooks the game's list-range node draw
// (EliteDangerous64.exe+0x58F0F0, build 334473 only), calls the original unchanged and then, for the PostGUI pass's nodes only, writes
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
// or when the function does not begin with build 334473's bytes, nothing is patched and one line says why.
//
// .13 (flight .12: on foot in stereo the lists 36-39 node is never drawn): three more hooks and two write watches find out why.
//   - The node draw hook also keeps the PostGUI nodes it sees, per kind and view (four cells).
//   - The graph walk hook (EliteDangerous64.exe+0x6BF980), BEFORE the original, looks for them in the walked graphs: in an enabled
//     graph and on, their flag +0x30 off (H1), their graph off (H3). The frame thread turns a frame's findings into a state per node:
//     on, H1, H3, out of every graph (H2), or its graph not walked -- a line on each change.
//   - The graph add and remove hooks (+0x6BFC60, +0x6BFE60), after the original, put a PostGUI node's event with the game's callers in
//     a ring; the frame thread writes a line per event.
//   - The frame thread puts hardware write watches on the first tracked HUD node's flag (debug slot 2) and its graph's flag +0x14
//     (slot 3) on every thread; each distinct writer -- the instruction and whether it wrote 0 -- gives one line with its callers. They
//     are re-armed when the journal's on-foot changes (threads started since), come off with the key, and come off for good after
//     100 000 hits in 30 s.
// Each point checks its own 16 bytes: wrong bytes refuse that one hook with one line and leave the others.
#include <cstddef>
#include <cstdint>

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
// Hook one of the graph points (ohp::kPointWalk, kPointAdd, kPointRemove) at base + its RVA, or at `targetOverride`. No identity check:
// the frame boundary calls it only once onFootHudProbeInstall passed it. One line either way; true once hooked. Frame thread.
bool onFootHudProbeInstallPoint(int point, uintptr_t base, uintptr_t targetOverride, OnFootHudProbeSink sink, void* ctx);
// The write watches (flag: ohp::kFlagNode on debug slot 2, ohp::kFlagGraph on slot 3), exposed for the rig. Arm puts the slot on every
// thread of the process that does not hold it already and says one line; returns the threads armed. Drain writes the new writers'
// lines; Disarm takes the slot off them, drains and says why with the totals. Frame thread.
int onFootHudProbeWatchArm(int flag, uintptr_t address, uint64_t object, OnFootHudProbeSink sink, void* ctx);
bool onFootHudProbeWatchOn(int flag);
uintptr_t onFootHudProbeWatchAddress(int flag);
void onFootHudProbeWatchDrain(int flag, OnFootHudProbeSink sink, void* ctx);
void onFootHudProbeWatchDisarm(int flag, const char* why, OnFootHudProbeSink sink, void* ctx);
void onFootHudProbeStopWatches(const char* why, OnFootHudProbeSink sink, void* ctx);
// While false the hook only forwards. Any thread.
void onFootHudProbeSetRecording(bool on);
// Calls of PostGUI nodes the hook saw while recording, and snapshots dropped because a field could not be read.
uint64_t onFootHudProbeCalls();
uint64_t onFootHudProbeFaults();
// Write the ring through `sink`: changed nodes, kept-back changes that are due, the fault line once, the 30-second summary; then the
// graph's side: tracked nodes and their graphs, each node's state on a change, the graph events, the watches (armed, re-armed, their
// writers, the fuse) and the graph's 30-second line. `onFoot` is the journal's answer (-1 unknown, 0, 1). Frame thread.
void onFootHudProbeDrain(uint64_t nowMs, int onFoot, OnFootHudProbeSink sink, void* ctx);

}  // namespace edvr
