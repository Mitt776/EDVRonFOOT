#pragma once
// The on-foot aim dot probe (EDVRonFOOT; experimental.native_vr_on_foot_dot, temporary, .42; the project's
// docs\research\native-vr-on-foot.md, "Зонд точки прицела (.42)"; aim_dot_probe_core.h for the pure half).
//
// The probe rides the on-foot HUD's census (on_foot_hud.cpp): it is called only while that census is (on foot, native VR on foot, the
// HUD key not off) and only while its own key is not off -- with off, not one call more to the graphics card.
//
//   aimDotNoteGuiDraw         an interface draw into a hidden panel: the atlas is the panel of 1.161 proportions; its first draw of a
//                             frame counts the frame, a draw after this frame's erase asks for another before the next read; in a
//                             census frame the draw's viewport and scissor go into the snapshot's list.
//   aimDotBeforeEyeComposite  the game's composite into an eye, with what it reads in pixel slots 0-2: one that reads the atlas is an
//                             eye quad; the first of a frame (and the first after a rewrite) erases the dot's cell (erase), or takes
//                             the snapshot (census) -- before the draw itself, which goes on as ever.
//   aimDotFrameBoundary       the frame thread: the key, the frame's number, the 2-second wait, the 30-second summary.
//   aimDotMark                the log mark (PAUSE, hotkey.dump_camera): census takes a snapshot of the next frame with the atlas.
#include <atomic>
#include <cstdint>

struct ID3D11DeviceContext;

namespace edvr {

namespace detail {
extern std::atomic<bool> g_aimDotWants;
}

// The key is not off and the probe has not stood down.
inline bool aimDotWants() { return detail::g_aimDotWants.load(std::memory_order_relaxed); }

// `id`, `w`, `h`, `format`: the bound target's resource (identity only), size and view format, as the HUD's census resolved them.
void aimDotNoteGuiDraw(ID3D11DeviceContext* ctx, uint64_t vs, uint32_t count, const void* id, uint32_t w, uint32_t h, uint32_t format);
// `read`: the resources behind pixel slots 0-2 (identity only, null for an empty slot).
void aimDotBeforeEyeComposite(ID3D11DeviceContext* ctx, const void* const read[3]);
// `hudCensus`: the HUD key not off and native_vr_on_foot on; `onFoot`: the HUD's census runs this frame (on foot as well).
void aimDotFrameBoundary(bool hudCensus, bool onFoot);
void aimDotMark();

}  // namespace edvr
