#pragma once
// The on-foot camera height (EDVRonFOOT; experimental.native_vr_on_foot_height; openspec change on-foot-height; the pure half and its
// reasons are in on_foot_height_core.h).
//
// On foot the OpenXR runtime half lifts the head pose the game gets by a few centimetres along the seated vertical, so the game's camera
// stands at the commander's chosen height. This is the half that decides: once a frame it reads the key, the journal and the main menu,
// ramps the height toward the key on foot (0 anywhere else) and publishes it in metres (src\common\on_foot_height_signal.h) for the
// native-frame provider to hand to the runtime.
namespace edvr {

// Once a frame at the Present boundary (device_hook.cpp's tkOnFootHeight), after the journal's tick. Frame thread only.
void onFootHeightFrameBoundary();

}  // namespace edvr
