#pragma once
// Native-VR-on-foot probe (EDVRonFOOT; experimental.native_vr_probe; the project's docs\research\native-vr-on-foot.md).
//
// On foot the game shows a flat "cinema" screen in the headset. The first probe flight and Ghidra found where that is decided: the
// view pipeline's rebuild reads the view's stereo mode (3/4 headset stereo, 5/6 headset cinema) and builds the cinema node, and the
// second eye without the scene, for 5/6. This probe hooks the rebuild, the teardown, the CinemaPipelineFeature's activate and
// deactivate and SetIdentStereoRenderModeActivity, and WRITES DOWN what passes through them. It changes nothing the game sees --
//
// except under experimental.native_vr_block_cinema (off by default; the EXPERIMENT the probe was built for): the rebuild hook puts the
// headset stereo mode (5 -> 3, 6 -> 4) in place of a cinema mode for the one call, then takes it back. Activate and deactivate stay
// the game's: a cinema node never activated is never in the list the teardown deactivates.
//
// THREADS. The hooks run on whichever game thread calls them. They take no lock, allocate nothing and write no log line: they read
// the game's fields under SEH, capture a short stack and push one event into a lock-free ring, then call the original with the same
// arguments. The frame thread drains the ring into the log (nativeVrProbeFrameBoundary), with a limiter.
//
// THE HOOKS (build 332841 only; a PE or prologue mismatch leaves that point unhooked with one line saying why). They are installed
// the first time either key is on and stay until the game exits; while the probe is off they record nothing.
//   +0x283AFB0  SetIdentStereoRenderModeActivity's execute
//   +0x28A9690  CinemaPipelineFeature's activate
//   +0x28AB150  CinemaPipelineFeature's deactivate
//   +0x2854FA0  the view pipeline's rebuild (the cinema decision; the block's one write)
//   +0x288BF20  the view pipeline's teardown
#include <cstddef>
#include <cstdint>

namespace edvr {

using NativeVrProbeSink = void (*)(void* ctx, const char* line);

// Once a frame at the Present boundary (device_hook.cpp's tkNativeVrProbe). Reads experimental.native_vr_probe and
// experimental.native_vr_block_cinema; the first time either is on, installs the hooks; while the probe is on, the hooks record; while
// the block is on, the rebuild substitutes; drains the ring into the log. Render thread only.
void nativeVrProbeFrameBoundary();

// The pieces the frame boundary is made of, exposed for the rig.
//
// Install every point not yet armed. `base` is the game image; each target is base + its RVA unless `targetsOverride` names it (the
// rig's synthetic functions). `checkIdentity` checks base's PE stamp first. Each armed point and each refusal is one line through
// `sink`. Returns the number of points armed in all (earlier calls included). Frame thread.
int nativeVrProbeInstall(uintptr_t base, const uintptr_t* targetsOverride, bool checkIdentity, NativeVrProbeSink sink, void* ctx);
// While false the hooks only forward. Any thread.
void nativeVrProbeSetRecording(bool on);
// While true the rebuild hook substitutes the headset stereo mode for a cinema mode. Any thread.
void nativeVrProbeSetBlockCinema(bool on);
// Whether a point (nvp::kPoint*) is hooked.
bool nativeVrProbeArmed(int point);
// Write the ring's events through `sink` -- pipeline decisions when they change, the rest limited -- plus the window's summary.
// `onFoot` is the journal's answer (-1 unknown, 0, 1) for the block's line. Frame thread.
void nativeVrProbeDrain(uint64_t nowMs, int onFoot, NativeVrProbeSink sink, void* ctx);
// Events recorded since launch (proof the hooks are alive), and lost to a full ring.
uint64_t nativeVrProbeRecorded();
uint64_t nativeVrProbeDropped();

}  // namespace edvr
