#pragma once
// The on-foot aim dot (EDVRonFOOT; experimental.native_vr_on_foot_aim_dot, .43; the project's docs\research\native-vr-on-foot.md,
// "Точка без оружия (.43)"; aim_dot_core.h for the pure half).
//
// With hide (the default) the plain dot in the middle of the view is made transparent in the interface's atlas: only in a frame
// without a weapon cue, and only when the centre read back is the plain dot's picture (.44) -- the weapons' and the pistol's crosshairs,
// the tools' dots, the markers, the hints and the jetpack's arc stay. It rides the on-foot HUD's census (on_foot_hud.cpp)
// as .42's probe did: called only while that census is (on foot, native VR on foot, the HUD key not off). With show it only follows the
// weapon cue for its lines -- not one call more to the graphics card.
//
//   aimDotHideNoteGuiDraw         an interface draw into a hidden panel: the atlas is the panel of 1.161 proportions; its first draw of
//                                 a frame counts the frame, a vector draw of a cue count marks the frame as one with a weapon, a draw
//                                 after this frame's erase asks for another before the next read.
//   aimDotHideBeforeEyeComposite  the game's composite into an eye, with what it reads in pixel slots 0-2: before the first that reads
//                                 the atlas the ready read-backs decide and the centre's rows are copied, the atlas's whole width
//                                 (.60: the radial menu and the tools move the dot along them); then (and after a rewrite) the plain
//                                 dot is erased where the newest read found it, in a frame without the cue (hide).
//   aimDotHideFrameBoundary       the frame thread: the key, the frame's number, the weapon on/off lines, the 30-second summary.
#include <atomic>
#include <cstdint>

struct ID3D11DeviceContext;

namespace edvr {

namespace detail {
extern std::atomic<bool> g_aimDotHideWants;
extern std::atomic<bool> g_aimDotHideErases;
}

// The HUD's census runs and this has not stood down (with show too: the cue's lines).
inline bool aimDotHideWants() { return detail::g_aimDotHideWants.load(std::memory_order_relaxed); }
// As aimDotHideWants, and the key is hide: the composites into the eyes are wanted too.
inline bool aimDotHideErases() { return detail::g_aimDotHideErases.load(std::memory_order_relaxed); }

// `id`, `w`, `h`: the bound target's resource (identity only) and size, as the HUD's census resolved them.
void aimDotHideNoteGuiDraw(ID3D11DeviceContext* ctx, uint64_t vs, uint32_t count, const void* id, uint32_t w, uint32_t h);
// `read`: the resources behind pixel slots 0-2 (identity only, null for an empty slot).
void aimDotHideBeforeEyeComposite(ID3D11DeviceContext* ctx, const void* const read[3]);
// `hudCensus`: the HUD key not off and native_vr_on_foot on; `onFoot`: the HUD's census runs this frame (on foot as well).
void aimDotHideFrameBoundary(bool hudCensus, bool onFoot);

}  // namespace edvr
