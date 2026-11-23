#pragma once
// The on-foot camera height, runtime side (openspec change on-foot-height). The d3d11 half sends a height in metres, already ramped and already 0
// off foot, in the ship and in the main menu (EdvrNativeFrameOutput::onFootHeightM, native_frame.h version 9, on_foot_height_signal.h). The head pose
// handed to the GAME is lifted by it along the seated space's vertical (+Y), in tracking axes, LAST of all -- after the aiming eye, the position snap
// and the head aim's turn and tilt, so the lift is always straight up whatever the head does (the head aim turns about the seated origin and would
// swing a lift applied before it). While the aiming eye's shift acts (LT held on foot) the lift gives way by the same blend: h * (1 - blend), so at
// full aim the game's aiming eye is on the sights' line exactly as at height 0. The poses the layer is composed with are never lifted: the headset
// shows the image from the real eyes, the distance between the eyes and the world's scale are untouched.
// Pure: no OpenXR calls, no globals, so a rig can drive every case.
#include "aim_eye_shift.h"
#include <cmath>
#include <cstdint>
#include <cstdio>

namespace edvr::openxr {

// The lift this frame, in metres: the height, times (1 - blend) only while the aiming eye's shift acts this frame (aimShiftActs, the runtime's
// aimEyeHeadKnown); with the aiming eye off, or LT not held, the whole height. The blend is clamped to 0..1; a height that is not finite reads 0.
inline double onFootLiftM(float heightM, bool aimShiftActs, float blend) {
  if (!std::isfinite(heightM) || heightM == 0.0f) return 0.0;
  double share = 1.0;
  if (aimShiftActs && blend == blend) share = blend >= 1.0f ? 0.0 : blend <= 0.0f ? 1.0 : 1.0 - double(blend);
  return double(heightM) * share;
}

// Lifts a head-to-tracking pose by `liftM` along the tracking space's vertical: translation.y += liftM. The rotation is untouched.
inline void applyOnFootLift(vr::HmdMatrix34_t& pose, double liftM) {
  if (liftM == 0.0 || !std::isfinite(liftM)) return;
  const double offset[3] = {0.0, liftM, 0.0};
  applyTrackingOffset(pose, offset);
}

// The runtime's line: on_foot_height,mm=N,channel=V when the height RECEIVED (rounded to mm, before the LT share) has held still kSettleMs and
// differs from the last one written. The first written value is measured against 0, so a commander who never sets the key gets no line; one who
// does gets one line on foot and one mm=0 back in the ship, and never a line per frame of the ramp.
class OnFootHeightLog {
 public:
  static constexpr uint64_t kSettleMs = 500;
  template<class Sink>
  void frame(uint32_t channel, float heightM, uint64_t nowMs, Sink&& sink) {
    const long mm = std::isfinite(heightM) ? std::lround(double(heightM) * 1000.0) : 0;
    if (!seen_ || mm != pending_) { seen_ = true; pending_ = mm; since_ = nowMs; return; }
    if (mm == written_ || nowMs - since_ < kSettleMs) return;
    written_ = mm;
    char line[96];
    std::snprintf(line, sizeof(line), "on_foot_height,mm=%ld,channel=%u\n", mm, channel);
    sink(line);
  }
  long written() const { return written_; }
 private:
  bool seen_ = false;
  long pending_ = 0, written_ = 0;
  uint64_t since_ = 0;
};

}  // namespace edvr::openxr
