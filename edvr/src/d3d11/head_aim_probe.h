#pragma once
// The on-foot head aim probe (EDVRonFOOT; experimental.native_vr_on_foot_head_aim_probe and _compensate; openspec change on-foot-head-aim-probe;
// head_aim_probe_core.h has the pure half and says what it is for). TEMPORARY: it and both keys go with on-foot-aiming-cleanup.
//
// HOW. Three entries of the game's own dispatch tables (334925; checked against the bytes of what they point at, or nothing is touched) are exchanged
// at startup:
//   the humanoid movement component's angle getters, vf+0xB0 (+0x5206B58 -> +0x1A45930) and vf+0xC8 (+0x5206B70 -> +0x1A49040): call the original,
//     count the HMC, and add the latched shift when the HMC is the player's;
//   the character eye's matrix getter, vf+0x20 (+0x5200058 -> +0xF893D0, `lea rax,[rcx+0x268]`, shared by five other tables): when called from the
//     on-foot camera (the return address +0x10743CB), remember the eye. Its HMC ([eye+0xD0]) is the player's, once the getters saw it too.
// No direct call or jump to any of the three exists in the executable (checked 2026-10-31), so the tables are every way in. With `off` the getters
// only call the original.
//
// THREADS. The getters run on game threads: atomics only, no lock, no log. The provider's beginFrame latches the frame's shift
// (head_aim_signal.h) and gets the turn for the runtime. The frame thread (headAimProbeFrameBoundary) reads the keys and the journal, identifies the
// player once a second, publishes what the latch may do, and writes every line.
#include <cstdint>

namespace edvr {

// Exchange the table entries (once; later calls do nothing). `flatProfile` true: say it is inert and touch nothing.
void headAimProbeInstall(bool flatProfile);

// Once a frame from device_hook's boundary, after the journal's tick.
void headAimProbeFrameBoundary();

}  // namespace edvr
