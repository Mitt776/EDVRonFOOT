#pragma once
// The aiming eye's signal between EDVR's two halves (openspec change on-foot-ads-dominant-eye).
//
// The d3d11 half decides, once a frame, whether the commander is aiming on foot (LT held) and ramps a blend 0..1 toward it
// (src\d3d11\on_foot_aim_eye_core.h). It publishes the eye, the blend and the on-foot flag here; the provider's beginFrame reads them into
// EdvrNativeFrameOutput::aimEye/aimEyeBlend/aimOnFoot (native_frame.h, version 6), and the OpenXR runtime half shifts the head pose it gives the game
// by that much of the eye's offset (src\openxr\aim_eye_shift.h).
//
// NEVER SHIFTED BY ACCIDENT. A reader that cannot trust the signal reads no shift (blend 0):
//   * nothing published yet, or a mismatched DLL pair (the struct then has no aim fields, and native_frame_client.h zeroes them);
//   * an eye other than 1 or 2, or a blend that is not a number, reads 0; a blend above 1 reads 1;
//   * a STALE signal: the d3d11 half stopped publishing. Past kStaleHoldMs of silence the blend decays to 0 over kStaleDecayMs, so the world is never
//     left shifted, and never jumps back.
// One 64-bit atomic carries all three and the publish time, so a reader never sees one with another's age.
#include <atomic>
#include <cstdint>

namespace edvr {
namespace aim_eye {

constexpr uint32_t kStaleHoldMs = 200;    // a signal older than this starts to decay
constexpr uint32_t kStaleDecayMs = 100;   // ...and is gone this much later: 0.3 s after the last publish

enum : uint32_t { kEyeNone = 0, kEyeLeft = 1, kEyeRight = 2 };

struct Signal {
    uint32_t eye = kEyeNone;   // 0 none, 1 left, 2 right
    float blend = 0.0f;        // 0..1
    uint32_t onFoot = 0;       // 0/1
};

// A blend a consumer may use: NaN and anything not above 0 read 0, anything above 1 reads 1.
inline float sanitize(float v) { return (v == v && v > 0.0f) ? (v > 1.0f ? 1.0f : v) : 0.0f; }

// bit 63: published; bits 62..32: the publish time in ms (31 bits, wrapping); bit 18: on foot; bits 17..16: the eye; bits 15..0: the blend, 0..65535.
inline uint64_t pack(const Signal& s, uint64_t nowMs) {
    const uint32_t q = static_cast<uint32_t>(sanitize(s.blend) * 65535.0f + 0.5f);
    return (1ull << 63) | ((nowMs & 0x7FFFFFFFull) << 32) | (uint64_t(s.onFoot ? 1u : 0u) << 18) |
           (uint64_t(s.eye & 3u) << 16) | uint64_t(q > 65535u ? 65535u : q);
}
// The signal a reader should use at `nowMs`: the published one while it is fresh, its blend decaying once stale, all zero when nothing was published.
inline Signal effective(uint64_t packed, uint64_t nowMs) {
    Signal s{};
    if ((packed >> 63) == 0) return s;
    s.onFoot = static_cast<uint32_t>((packed >> 18) & 1u);
    s.eye = static_cast<uint32_t>((packed >> 16) & 3u);
    if (s.eye != kEyeLeft && s.eye != kEyeRight) s.eye = kEyeNone;
    float blend = static_cast<float>(packed & 0xFFFFu) / 65535.0f;
    const uint32_t at = static_cast<uint32_t>((packed >> 32) & 0x7FFFFFFFull);
    uint32_t age = (static_cast<uint32_t>(nowMs & 0x7FFFFFFFull) - at) & 0x7FFFFFFFu;
    if (age > 0x40000000u) age = 0;   // published a moment after `nowMs` was read: fresh
    if (age >= kStaleHoldMs + kStaleDecayMs) { s.onFoot = 0; blend = 0.0f; }
    else if (age > kStaleHoldMs) blend *= 1.0f - static_cast<float>(age - kStaleHoldMs) / static_cast<float>(kStaleDecayMs);
    s.blend = s.eye == kEyeNone ? 0.0f : blend;
    return s;
}

inline std::atomic<uint64_t>& signalWord() {
    static std::atomic<uint64_t> word{0};
    return word;
}
// The frame thread, once a frame.
inline void publish(const Signal& s, uint64_t nowMs) { signalWord().store(pack(s, nowMs), std::memory_order_release); }
// The provider's beginFrame, once a runtime frame.
inline Signal read(uint64_t nowMs) { return effective(signalWord().load(std::memory_order_acquire), nowMs); }
// Forgotten: reads no shift until the next publish.
inline void clear() { signalWord().store(0, std::memory_order_release); }

}  // namespace aim_eye
}  // namespace edvr
