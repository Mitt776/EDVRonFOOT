#pragma once
// Native-VR-on-foot probe (EDVRonFOOT; experimental.native_vr_probe; the project's docs\research\native-vr-on-foot.md).
//
// On foot the game shows a flat "cinema" screen in the headset. Static analysis of build 332841 points at one narrow place where that
// is decided: the data-driven SetIdentStereoRenderModeActivity, which sets a view's stereo mode, and the CinemaPipelineFeature it
// leads to. This probe hooks those three functions and WRITES DOWN what passes through them -- the mode asked for and applied, the
// feature switched on and off, and who called -- so a flight can say which value means cinema. It changes nothing the game sees.
//
// THREADS. The hooks run on whichever game thread calls them. They take no lock, allocate nothing and write no log line: they read
// the game's fields under SEH, capture a short stack and push one event into a lock-free ring, then call the original with the same
// arguments. The frame thread drains the ring into the log (nativeVrProbeFrameBoundary), with a limiter.
//
// THE HOOKS (build 332841 only; a PE or prologue mismatch leaves that point unhooked with one line saying why):
//   +0x283AFB0  SetIdentStereoRenderModeActivity's execute
//   +0x28A9690  CinemaPipelineFeature's activate
//   +0x28AB150  CinemaPipelineFeature's deactivate
#include <cstddef>
#include <cstdint>

namespace edvr {

using NativeVrProbeSink = void (*)(void* ctx, const char* line);

// Once a frame at the Present boundary (device_hook.cpp's tkNativeVrProbe). Reads experimental.native_vr_probe; the first time it is
// on, installs the hooks; while it is on, the hooks record; drains the ring into the log. Render thread only.
void nativeVrProbeFrameBoundary();

// The pieces the frame boundary is made of, exposed for the rig.
//
// Install every point not yet armed. `base` is the game image; each target is base + its RVA unless `targetsOverride` names it (the
// rig's synthetic functions). `checkIdentity` checks base's PE stamp first. Each armed point and each refusal is one line through
// `sink`. Returns the number of points armed in all (earlier calls included). Frame thread.
int nativeVrProbeInstall(uintptr_t base, const uintptr_t* targetsOverride, bool checkIdentity, NativeVrProbeSink sink, void* ctx);
// While false the hooks only forward. Any thread.
void nativeVrProbeSetRecording(bool on);
// Write the ring's events through `sink`, limited, plus the window's summary. Frame thread.
void nativeVrProbeDrain(uint64_t nowMs, NativeVrProbeSink sink, void* ctx);
// Events recorded since launch (proof the hooks are alive), and lost to a full ring.
uint64_t nativeVrProbeRecorded();
uint64_t nativeVrProbeDropped();

}  // namespace edvr
