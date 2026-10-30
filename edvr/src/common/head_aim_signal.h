#pragma once
// The on-foot head aim's latch between the provider and the d3d11 half (EDVRonFOOT; openspec changes on-foot-head-aim-probe and on-foot-head-aim).
//
// Once a runtime frame the provider's beginFrame (src\d3d11\native_frame.cpp) asks the head aim code for the frame's turn: it latches that frame's angles,
// which its hooks hand the game until the next latch, and answers the angles the runtime turns the game's head pose by
// (EdvrNativeFrameOutput::headAimYawRad and headAimPitchRad, native_frame.h versions 7 and 8). The code registers its latch here at install; until it
// does -- and in every process that never links it, such as the provider's own rig -- a latch reads no turn.
//
// The latch gets the frame's real head pose (the channel's physicalHead, 12 floats row-major 3x4, nullptr when the frame has none) and the version the
// runtime asked: the feature (the pose-driven aim) needs 8, the probe (a fixed shift) needs 7.
#include <atomic>
#include <cmath>
#include <cstdint>

namespace edvr {
namespace head_aim {

struct Turn {
    float yawRad = 0.0f;     // the turn of the game's head pose about the seated vertical, 0 for none
    float pitchRad = 0.0f;   // the turn about the seated horizontal (X), after the yaw one, with the sign key applied; 0 for none (version 8)
    uint32_t probe = 0;      // 1 while the probe acts
    uint32_t feature = 0;    // 1 while the feature leads the frame (version 8)
};

using LatchFn = Turn (*)(uint64_t sequence, uint64_t nowMs, const float* head, uint32_t requestVersion);

inline std::atomic<LatchFn>& latchSlot() {
    static std::atomic<LatchFn> slot{nullptr};
    return slot;
}
inline void registerLatch(LatchFn fn) { latchSlot().store(fn, std::memory_order_release); }

// The provider, once a runtime frame. A turn that is not a number reads 0.
inline Turn latch(uint64_t sequence, uint64_t nowMs, const float* head, uint32_t requestVersion) {
    const LatchFn fn = latchSlot().load(std::memory_order_acquire);
    if (!fn) return Turn{};
    Turn t = fn(sequence, nowMs, head, requestVersion);
    if (!std::isfinite(t.yawRad)) t.yawRad = 0.0f;
    if (!std::isfinite(t.pitchRad)) t.pitchRad = 0.0f;
    t.probe = t.probe ? 1u : 0u;
    t.feature = t.feature ? 1u : 0u;
    return t;
}

}  // namespace head_aim
}  // namespace edvr
