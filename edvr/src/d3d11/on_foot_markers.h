#pragma once
// On-foot markers (EDVRonFOOT; the project's docs\research\native-vr-on-foot.md, "Правка позы PostGUI .17" and "Stereo как поведение
// .18"; on_foot_markers_core.h has the game's side).
//
// On foot in headset stereo the game's "over the world" layer -- the crosshair, the prompts over doors, terminals and people -- slides
// off its objects as the head turns: the game gives that layer's camera (the PostGUI pass's, one per eye) the head's pose every frame,
// as in the ship, while the layer is made for the cinema's identity camera. This lets the game write the pose and then, at once, gives
// both eyes' PostGUI cameras the identity rotation and commits it with the game's own call, as the game does in the cinema. The world's
// camera, the ship's and the cinema are not touched. No key of its own: on whenever experimental.native_vr_on_foot is and the journal
// says on foot.
//
// HOW. The three points -- the two writers of that pose (EliteDangerous64.exe+0x287D660 and +0x28457F0 in build 334473) and the
// commit (+0x2874820) -- are found by their code once, the first frame native VR on foot is on: each entry's pattern exactly once in
// the game's executable sections, the pose writer reading the cameras' chain and calling the other two. The writers are hooked at their
// entry the first time the correction comes on. A hook calls the original with every argument register and, after the OUTER call on
// its thread (a per-thread depth: the eye writer called from inside the pose writer gives one step, not two), runs the step while the
// frame thread says so: the chain to both cameras, the checks (each pass's view id, the camera against the pass's own +0x50), the new
// matrix (rows 0-2 the identity, each eye at its offset from the point between the eyes in the head's axes), the block's flags |= 0xF,
// the commit (cam, size, cam+0x20) under SEH. A frame that cannot be read is not corrected for either eye and is counted by its
// reason; three commit faults turn the correction off until the game restarts.
//
// THREADS. The hooks run on whichever game thread writes the pose: no lock, no allocation, no log. The frame thread
// (onFootMarkersFrameBoundary, at Present) reads native_vr_on_foot and the journal, makes the search and the one install attempt,
// publishes whether to correct, writes the on and off lines, checks at Present whether both cameras still hold the correction's
// rotation, and writes a summary every 30 s while on. A point not found, or found more than once, or a check of the pose writer that
// fails: nothing is patched, one line says why.
#include <cstddef>
#include <cstdint>
#include "on_foot_markers_core.h"

namespace edvr {

using OnFootMarkersSink = void (*)(void* ctx, const char* line);

// Once a frame at the Present boundary (device_hook.cpp's tkOnFootMarkers): native_vr_on_foot, the journal, the step below. Render
// thread only.
void onFootMarkersFrameBoundary();

// The pieces the frame boundary is made of, exposed for the rig.
//
// Search the image at `base` for the three points: in `sections` (the rig's synthetic image), or, when null, in the sections base's own
// PE headers name. One line through `sink` either way, with the time it took; true when found. Once hooked it says nothing and returns
// true. Frame thread.
bool onFootMarkersFind(uintptr_t base, const ofm::Section* sections, int count, OnFootMarkersSink sink, void* ctx);
// Hook both writers where the last search found them (nothing, and no line, when it did not: its line said why). One line on a hook
// or a refusal; true once hooked (later calls return true and say nothing). Frame thread.
bool onFootMarkersInstall(OnFootMarkersSink sink, void* ctx);
bool onFootMarkersInstalled();
// Whether the hooks correct (false: they only forward and count). Any thread.
void onFootMarkersSetOn(bool on);
// The hooks' counters, cumulative.
ofm::Counts onFootMarkersCounts();
bool onFootMarkersDisabled();
// The frame step: the search the first time `native` is on, the decision, one install attempt per process the first time the decision
// is on, whether to correct published, the on and off lines, the disabled line once, the end-of-frame check and the 30-second summary.
// `onFoot` is the journal's answer (-1 unknown, 0, 1); `base`, `sections` and `count` as onFootMarkersFind takes them. Frame thread.
void onFootMarkersFrameStep(uint64_t nowMs, bool native, int onFoot, uintptr_t base, const ofm::Section* sections, int count,
                            OnFootMarkersSink sink, void* ctx);

}  // namespace edvr
