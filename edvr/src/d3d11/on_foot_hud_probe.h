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
// The hook goes in the first time the key is on and stays until the game exits; with the key off it only forwards. On another build,
// or when the function does not begin with build 334473's bytes, nothing is patched and one line says why.
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
// While false the hook only forwards. Any thread.
void onFootHudProbeSetRecording(bool on);
// Calls of PostGUI nodes the hook saw while recording, and snapshots dropped because a field could not be read.
uint64_t onFootHudProbeCalls();
uint64_t onFootHudProbeFaults();
// Write the ring through `sink`: changed nodes, kept-back changes that are due, the fault line once, the 30-second summary. `onFoot`
// is the journal's answer (-1 unknown, 0, 1), stamped on each snapshot. Frame thread.
void onFootHudProbeDrain(uint64_t nowMs, int onFoot, OnFootHudProbeSink sink, void* ctx);

}  // namespace edvr
