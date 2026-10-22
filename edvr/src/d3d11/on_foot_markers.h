#pragma once
// On-foot markers (EDVRonFOOT; experimental.native_vr_on_foot_markers; the project's docs\research\native-vr-on-foot.md, "Правка позы
// PostGUI .17"; on_foot_markers_core.h has the game's side).
//
// On foot in headset stereo the game's "over the world" layer -- the crosshair, the prompts over doors, terminals and people -- slides
// off its objects as the head turns: the game gives that layer's camera (the PostGUI pass's, one per eye) the head's pose every frame,
// as in the ship, while the layer is made for the cinema's identity camera. This lets the game write the pose and then, at once, gives
// both eyes' PostGUI cameras the identity rotation and commits it with the game's own call, as the game does in the cinema. The world's
// camera, the ship's and the cinema are not touched.
//
// HOW. Both writers of that pose (EliteDangerous64.exe+0x287D660 and +0x28457F0, build 334473 only, 16 bytes checked at each and at
// the commit +0x2874820) are hooked at their entry. A hook calls the original with every argument register and, after the OUTER call
// on its thread (a per-thread depth: 0x28457F0 called from inside 0x287D660 gives one step, not two), runs the step while the frame
// thread says so: the chain to both cameras, the checks (each pass's view id, the camera against the pass's own +0x50), the new matrix
// (rows 0-2 the identity, the position by the key: stereo keeps each eye's offset from the point between the eyes in the head's axes,
// flat puts both at 0), the block's flags |= 0xF, the commit 0x2874820 (cam, size, cam+0x20) under SEH. A frame that cannot be read is
// not corrected for either eye and is counted by its reason; three commit faults turn the correction off until the game restarts.
//
// THREADS. The hooks run on whichever game thread writes the pose: no lock, no allocation, no log. The frame thread
// (onFootMarkersFrameBoundary, at Present) reads the keys and the journal, publishes the mode, makes the one install attempt, writes
// the on and off lines, checks at Present whether both cameras still hold the correction's rotation, and writes a summary every 30 s
// while on. Another build, or bytes that differ at any point: nothing is patched, one line says why.
#include <cstddef>
#include <cstdint>
#include "on_foot_markers_core.h"

namespace edvr {

using OnFootMarkersSink = void (*)(void* ctx, const char* line);

// Once a frame at the Present boundary (device_hook.cpp's tkOnFootMarkers): the keys, the journal, the step below. Render thread only.
void onFootMarkersFrameBoundary();

// The pieces the frame boundary is made of, exposed for the rig.
//
// Hook both writers at base + their RVAs, or at targets[ofm::kPoint*] when targets is not null (the rig's synthetic functions; the
// commit is then called at targets[ofm::kPointCommit]). `checkIdentity` checks base's PE stamp first. All three points' bytes are
// checked before anything is patched. One line through `sink` either way; true once hooked (later calls return true and say nothing).
// Frame thread.
bool onFootMarkersInstall(uintptr_t base, const uintptr_t* targets, bool checkIdentity, OnFootMarkersSink sink, void* ctx);
bool onFootMarkersInstalled();
// The mode the hooks act on (ofm::kModeNone: they only forward and count). Any thread.
void onFootMarkersSetMode(int mode);
// The hooks' counters, cumulative.
ofm::Counts onFootMarkersCounts();
bool onFootMarkersDisabled();
// The frame step: the key line on a change of the key's text or of native_vr_on_foot, the decision, one install attempt per process
// the first time the decision is on, the mode published, the on and off lines, the disabled line once, the end-of-frame check and the
// 30-second summary. `keyText` is experimental.native_vr_on_foot_markers as read ("stereo" when the line is missing); `onFoot` the
// journal's answer (-1 unknown, 0, 1). Frame thread.
void onFootMarkersFrameStep(uint64_t nowMs, bool native, const char* keyText, int onFoot, uintptr_t base, const uintptr_t* targets,
                            bool checkIdentity, OnFootMarkersSink sink, void* ctx);

}  // namespace edvr
