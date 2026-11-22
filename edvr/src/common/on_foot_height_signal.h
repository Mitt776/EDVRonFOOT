#pragma once
// The on-foot camera height between EDVR's two halves (openspec change on-foot-height).
//
// The d3d11 half decides, once a frame, how high the game's camera should sit on foot (the key while on foot, 0 anywhere else) and
// ramps the value toward it (src\d3d11\on_foot_height_core.h). It publishes the ramped value here, in metres; the provider's beginFrame
// reads it into EdvrNativeFrameOutput::onFootHeightM (native_frame.h, version 9), and the OpenXR runtime half lifts the head pose it
// gives the game by that much, times (1 - the aiming eye's blend) while LT is held (native_runtime_host.h).
//
// NEVER LIFTED BY ACCIDENT. Nothing published reads 0; a value that is not a number, or is more than kMaxM either way, reads 0.
// No staleness rule, unlike the aiming eye: both writer and reader live in d3d11.dll, the writer's own ramp already takes the value
// to 0 off foot, and a value dropped to 0 on a stall would jump back afterwards -- the one thing this setting must never do.
#include <atomic>
#include <cmath>

namespace edvr {
namespace on_foot_height {

constexpr float kMaxM = 0.25f;   // the widest the key allows is 0.20 m; past this a value is not ours

// A height a consumer may use: NaN, infinity and anything past kMaxM read 0.
inline float sanitize(float m) { return (std::isfinite(m) && std::fabs(m) <= kMaxM) ? m : 0.0f; }

inline std::atomic<float>& signalWord() {
    static std::atomic<float> word{0.0f};
    return word;
}
// The frame thread, once a frame.
inline void publish(float metres) { signalWord().store(sanitize(metres), std::memory_order_release); }
// The provider's beginFrame, once a runtime frame.
inline float read() { return sanitize(signalWord().load(std::memory_order_acquire)); }
// Forgotten: reads 0 until the next publish.
inline void clear() { signalWord().store(0.0f, std::memory_order_release); }

}  // namespace on_foot_height
}  // namespace edvr
