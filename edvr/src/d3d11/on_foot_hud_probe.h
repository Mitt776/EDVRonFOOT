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
//   - Hardware write watches on the HUD node's flag (debug slot 2) and its graph's flag +0x14 (slot 3) found the one writer (H1);
//     .16 gave both slots to new targets, .19 slot 2 to the provider subscription and left slot 3 free (below).
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
//   - One hardware write watch, debug slot 2, on the low four bytes of the subscription (+0x28) of the first provider that answered with
//     0x8 (view 0's when it has one), on every thread. Each distinct writing instruction gives one line: the value, whether it carries a
//     bit of the HUD nodes' cameras, where the commander was, the callers. Every 30 s the watch gives its writes per site and place or
//     that it stood quiet. It is re-armed when the journal's on-foot changes (threads started since), comes off with a line when its
//     provider stays out of its view's list for 2 s, comes off with the key, and for good after 100 000 hits in 30 s. When no provider
//     ever answered with 0x8 the 30-second summary says so and no watch is armed. Slot 3 is not used.
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
// Hook one of the other points (ohp::kPointWalk, kPointAdd, kPointRemove, kPointPass) at base + its RVA, or at `targetOverride`. No identity check:
// the frame boundary calls it only once onFootHudProbeInstall passed it. One line either way; true once hooked. Frame thread.
bool onFootHudProbeInstallPoint(int point, uintptr_t base, uintptr_t targetOverride, OnFootHudProbeSink sink, void* ctx);
// The write watch (watch: ohp::kWatchSubscription on debug slot 2), exposed for the rig. Arm puts the slot on every thread of the
// process that does not hold it already and says one line, with `note` (or null) at its end; returns the threads armed. Drain writes the
// new writers' lines; Disarm takes the slot off them, drains and says why with the totals. Frame thread. SetPlace: where the commander
// is (ohp::Place) for the writes and the provider snapshots that come next; the camera drain sets it every frame. Any thread.
int onFootHudProbeWatchArm(int watch, uintptr_t address, uint64_t object, const char* note, OnFootHudProbeSink sink, void* ctx);
bool onFootHudProbeWatchOn(int watch);
uintptr_t onFootHudProbeWatchAddress(int watch);
void onFootHudProbeWatchDrain(int watch, OnFootHudProbeSink sink, void* ctx);
void onFootHudProbeWatchDisarm(int watch, const char* why, OnFootHudProbeSink sink, void* ctx);
void onFootHudProbeStopWatches(const char* why, OnFootHudProbeSink sink, void* ctx);
void onFootHudProbeSetPlace(int place);
// The OR of the HUD nodes' camera bits (cam+0x8E0) the watch's handler tests the written subscription against; the camera drain sets it
// every frame from the provider table. Any thread.
void onFootHudProbeSetCameraBits(uint64_t bits);
// While false the hook only forwards. Any thread.
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
// providers and watch (the place, the HUD nodes' lists from the ring, the 30-second summary, the watch's target on view 0's pass, the
// writers, the watch's 30-second line, the fuse). `stereo` is
// experimental.native_vr_on_foot (1, 0); `head12` the head pose in headPose's layout, or null. Frame thread.
void onFootHudProbeCameraDrain(uint64_t nowMs, int onFoot, int stereo, const float* head12, OnFootHudProbeSink sink, void* ctx);

}  // namespace edvr
