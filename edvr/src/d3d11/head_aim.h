#pragma once
// The on-foot head aim (EDVRonFOOT; experimental.native_vr_on_foot_head_aim; openspec changes on-foot-head-aim-probe, on-foot-head-aim and
// on-foot-aiming-cleanup; head_aim_core.h has the pure half and says what it does). The glue: the table entries, the getters, the latch and the frame
// thread's step.
//
// HOW. Three entries of the game's own dispatch tables (334925; checked against the bytes of what they point at, or nothing is touched) are exchanged
// at startup:
//   the humanoid movement component's angle getters, vf+0xB0 (+0x5206B58 -> +0x1A45930) and vf+0xC8 (+0x5206B70 -> +0x1A49040): call the original,
//     count the HMC, and, when the HMC is the player's and the feature leads the frame, hand the head's tilt (angle 1) and add the head's turn (angle 2);
//   the character eye's matrix getter, vf+0x20 (+0x5200058 -> +0xF893D0, `lea rax,[rcx+0x268]`, shared by five other tables): when called from the
//     on-foot camera (the return address +0x10743CB), remember the eye. Its HMC ([eye+0xD0]) is the player's, once the getters saw it too.
// No direct call or jump to any of the three exists in the executable (checked 2026-10-31), so the tables are every way in. With the key `off` the
// getters only call the original.
//
// THE LATCH. Once a runtime frame the provider's beginFrame (head_aim_signal.h) asks for the frame's turn. When the key is not off and the runtime
// asked channel version 8 the latch runs the feature's step on the frame's head pose and answers the turn and the tilt the runtime turns the game's head
// pose back by. A version 7 runtime never engages the feature (reason channel): the latch answers nothing.
//
// THREADS. The getters run on game threads: atomics only, no lock, no log. The provider's beginFrame latches the frame. The frame thread
// (headAimFrameBoundary) reads the key and the journal, identifies the player once a second, publishes what the latch may do, and writes every line.
#include <cstdint>

namespace edvr {

// Exchange the table entries (once; later calls do nothing). `flatProfile` true: say it is inert and touch nothing.
void headAimInstall(bool flatProfile);

// Once a frame from device_hook's boundary, after the journal's tick.
void headAimFrameBoundary();

}  // namespace edvr
