#pragma once
// The on-foot head aim probe's latch between the provider and the probe (EDVRonFOOT; openspec change on-foot-head-aim-probe).
//
// Once a runtime frame the provider's beginFrame (src\d3d11\native_frame.cpp) asks the probe for the frame's turn: the probe latches that frame's
// shift, which its hooks hand the game until the next latch, and answers the angle the runtime turns the game's head pose by
// (EdvrNativeFrameOutput::headAimYawRad, native_frame.h version 7). The probe registers its latch here at install; until it does -- and in every
// process that never links the probe, such as the provider's own rig -- a latch reads no turn and no probe.
#include <atomic>
#include <cstdint>

namespace edvr {
namespace head_aim {

struct Turn {
    float yawRad = 0.0f;   // the turn of the game's head pose, 0 for none
    uint32_t probe = 0;    // 1 while the probe acts
};

using LatchFn = Turn (*)(uint64_t sequence, uint64_t nowMs);

inline std::atomic<LatchFn>& latchSlot() {
    static std::atomic<LatchFn> slot{nullptr};
    return slot;
}
inline void registerLatch(LatchFn fn) { latchSlot().store(fn, std::memory_order_release); }

// The provider, once a runtime frame. A turn that is not a number reads 0.
inline Turn latch(uint64_t sequence, uint64_t nowMs) {
    const LatchFn fn = latchSlot().load(std::memory_order_acquire);
    if (!fn) return Turn{};
    Turn t = fn(sequence, nowMs);
    if (!(t.yawRad == t.yawRad)) t.yawRad = 0.0f;
    t.probe = t.probe ? 1u : 0u;
    return t;
}

}  // namespace head_aim
}  // namespace edvr
