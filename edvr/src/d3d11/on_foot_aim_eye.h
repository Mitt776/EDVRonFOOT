#pragma once
// The on-foot aiming eye (EDVRonFOOT; experimental.native_vr_on_foot_aim_eye; openspec change on-foot-ads-dominant-eye; the pure half and
// its reasons are in on_foot_aim_eye_core.h).
//
// While the commander is on foot in stereo and holds the gamepad's LT, the OpenXR runtime half shifts the head pose the game gets by the
// dominant eye's offset, so that eye sits on the iron sights. This is the half that decides: once a frame it reads the key, the journal
// and the left trigger, ramps the blend toward 0 or 1 over 0.12 s and publishes eye, blend and the on-foot flag
// (src\common\aim_eye_signal.h) for the native-frame provider to hand to the runtime.
namespace edvr {

// Once a frame at the Present boundary (device_hook.cpp's tkOnFootAimEye), after the journal's tick. Frame thread only. Publishes every
// frame: the signal goes stale after 200 ms and a stale one reads as no shift.
void onFootAimEyeFrameBoundary();

}  // namespace edvr
