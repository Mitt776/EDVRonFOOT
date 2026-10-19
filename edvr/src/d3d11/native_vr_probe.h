#pragma once
// Native-VR-on-foot probe (EDVRonFOOT; experimental.native_vr_probe; the project's docs\research\native-vr-on-foot.md).
//
// On foot the game shows a flat "cinema" screen in the headset. The first probe flight and Ghidra found where that is decided: the
// view pipeline's rebuild reads the view's stereo mode (3/4 headset stereo, 5/6 headset cinema) and builds the cinema node, and the
// second eye without the scene, for 5/6. This probe hooks the rebuild, the teardown, the CinemaPipelineFeature's activate and
// deactivate and SetIdentStereoRenderModeActivity, and WRITES DOWN what passes through them. It changes nothing the game sees --
//
// except under experimental.native_vr_on_foot (ON by default; native VR on foot): the rebuild's mode is only the last copy of the
// mode the game wants, X+0x20, which a render task copies into the renderer's request once a frame and by which it decides whether to
// compute the headset eyes' projections. The source hook puts headset stereo in place of a cinema mode found there and keeps it there
// until the game sets a mode of its own or the journal says the commander boarded. It never writes a cinema mode. The same key also
// turns off the game's own cinema-on-foot switch (+0x5E82B3E, 1 -> 0; nativeVrCinemaSwitchStep), so the game itself keeps headset
// stereo and its headset camera on foot and the hold has nothing to do; the hold stays as the fallback.
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
//   +0x2854FA0  the view pipeline's rebuild (the cinema decision)
//   +0x288BF20  the view pipeline's teardown
//   +0x281AC10  the stereo mode source (the hold's one write)
//   +0x2DFC7A0  the default camera's name (written after the original made it)
//   +0x1D79660  the camera director's switch by name (the name asked for, written before the original)
//   +0x1D79980  the director's first-person default
//   +0x1D203B0  the director's other switch by name
//
// With the probe on, a hold start also puts a hardware write watch on the stereo mode source for a while (nativeVrWriterWatch*): who
// in the game writes cinema there.
#include <cstddef>
#include <cstdint>

namespace edvr {

using NativeVrProbeSink = void (*)(void* ctx, const char* line);

// Once a frame at the Present boundary (device_hook.cpp's tkNativeVrProbe). Reads experimental.native_vr_probe and
// experimental.native_vr_on_foot (on when missing); the first time either is on, installs the hooks; while the probe is on, the hooks
// record; while native_vr_on_foot is on, the cinema-on-foot switch is off and the source holds; drains the ring into the log and ends
// a hold by the journal. Render thread only.
void nativeVrProbeFrameBoundary();

// The pieces the frame boundary is made of, exposed for the rig.
//
// Install every point not yet armed. `base` is the game image; each target is base + its RVA unless `targetsOverride` names it (the
// rig's synthetic functions). `checkIdentity` checks base's PE stamp first. Each armed point and each refusal is one line through
// `sink`. Returns the number of points armed in all (earlier calls included). Frame thread.
int nativeVrProbeInstall(uintptr_t base, const uintptr_t* targetsOverride, bool checkIdentity, NativeVrProbeSink sink, void* ctx);
// While false the hooks only forward. Any thread.
void nativeVrProbeSetRecording(bool on);
// While true the stereo mode source hook replaces a cinema mode with headset stereo and holds it (experimental.native_vr_on_foot).
// Any thread.
void nativeVrProbeSetOnFoot(bool on);
// Whether a hold is running.
bool nativeVrProbeHolding();
// The journal's answer this frame (-1 unknown, 0, 1): on foot yes -> no ends a running hold, with one line -- the game's own 3 on
// boarding looks like ours. Frame thread.
void nativeVrOnFootJournal(int onFoot, NativeVrProbeSink sink, void* ctx);
// Whether a point (nvp::kPoint*) is hooked.
bool nativeVrProbeArmed(int point);
// Write the ring's events through `sink` -- pipeline decisions when they change, the rest limited -- plus the window's summary.
// `onFoot` is the journal's answer (-1 unknown, 0, 1) for the hold's lines. Frame thread.
void nativeVrProbeDrain(uint64_t nowMs, int onFoot, NativeVrProbeSink sink, void* ctx);
// Events recorded since launch (proof the hooks are alive), and lost to a full ring.
uint64_t nativeVrProbeRecorded();
uint64_t nativeVrProbeDropped();

// The writer watch (nvp::WriterTable; experimental.native_vr_probe). A hold start with the probe on asks for a hardware write watch on
// the stereo mode source; the frame thread sets it on every thread of the process (debug register slot 1), writes each distinct game
// instruction that writes there once, and takes it off when the hold ends, the probe goes off or after 30 s. Frame thread.
//   Arm: one line saying on how many threads, or why not; returns the threads armed (0: not on, or already on).
int nativeVrWriterWatchArm(uintptr_t address, uint64_t nowMs, NativeVrProbeSink sink, void* ctx);
bool nativeVrWriterWatchOn();
//   The writing sites recorded since the last call, each once.
void nativeVrWriterWatchDrain(NativeVrProbeSink sink, void* ctx);
//   Off on every thread it was set on, then the summary line (verdict: nvp::kWatch*).
void nativeVrWriterWatchDisarm(uint8_t verdict, uint64_t nowMs, NativeVrProbeSink sink, void* ctx);
//   Once a frame: arm on a hold start's request, drain, and take it off by nvp::watchVerdict.
void nativeVrWriterWatchStep(uint64_t nowMs, bool recording, NativeVrProbeSink sink, void* ctx);

// experimental.native_vr_on_foot's head camera (nvp::cinemaSwitchStep): once a frame, the game's cinema-on-foot switch at `address`
// (null: not build 332841) is turned 1 -> 0 while the key is on, and our 0 back to 1 when it goes off, with a line on each change and
// when the journal's on-foot answer changes while the switch is ours. Frame thread.
void nativeVrCinemaSwitchStep(uint8_t* address, bool key, int onFoot, NativeVrProbeSink sink, void* ctx);

}  // namespace edvr
