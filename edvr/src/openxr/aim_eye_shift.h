#pragma once
// The aiming eye, runtime side (openspec change on-foot-ads-dominant-eye): while the commander holds LT on foot the d3d11 half sends an eye and a blend
// (EdvrNativeFrameOutput::aimEye/aimEyeBlend, native_frame.h version 6, aim_eye_signal.h), and the head pose handed to the GAME is moved by minus that
// much of the eye's offset from the head, in the head's own axes. The game's eye (head * GetEyeToHeadTransform) then stands where the middle of the head
// was: on the line of the iron sights. The poses the layer is composed with (frameViews, d3d11_stereo.cpp) are never shifted, so the headset takes the
// image as seen from the real eyes and does not turn it back; rotation and the distance between the game's eyes are untouched.
// Pure: no OpenXR calls, no globals, so a rig can drive every case.
#include "geometry_snapshot.h"
#include <cmath>
#include <cstdint>
#include <cstdio>

namespace edvr::openxr {

// An eye offset larger than this is not a headset's eye: no shift.
constexpr double kAimEyeMaxOffsetM = 0.1;

inline const char* aimEyeName(uint32_t eye) { return eye == 1 ? "left" : eye == 2 ? "right" : "none"; }

// The offset, in head axes and metres, the game's head pose is moved by: -blend * (the translation of that eye's eye-to-head transform). aimEye 1 is the
// left eye (eyeToHead[0]), 2 the right (eyeToHead[1]). False, and a zero offset, for no eye, a blend that is not above 0, or an offset that is not finite or
// not a plausible eye's. The blend is clamped to 1.
inline bool aimEyeHeadOffset(const vr::HmdMatrix34_t eyeToHead[2], uint32_t aimEye, float blend, double out[3]) {
  out[0] = out[1] = out[2] = 0.0;
  if ((aimEye != 1 && aimEye != 2) || !(blend > 0.0f)) return false;
  const auto& m = eyeToHead[aimEye - 1].m;
  const double t[3] = {m[0][3], m[1][3], m[2][3]};
  if (!std::isfinite(t[0]) || !std::isfinite(t[1]) || !std::isfinite(t[2])) return false;
  if (std::sqrt(t[0] * t[0] + t[1] * t[1] + t[2] * t[2]) > kAimEyeMaxOffsetM) return false;
  const double b = blend > 1.0f ? 1.0 : double(blend);
  for (int i = 0; i < 3; ++i) out[i] = -b * t[i];
  return true;
}

// The full offset of that eye from the head in millimetres (what blend 1 moves the head by), 0 when there is none to use.
inline double aimEyeFullMm(const vr::HmdMatrix34_t eyeToHead[2], uint32_t aimEye) {
  double o[3];
  if (!aimEyeHeadOffset(eyeToHead, aimEye, 1.0f, o)) return 0.0;
  return 1000.0 * std::sqrt(o[0] * o[0] + o[1] * o[1] + o[2] * o[2]);
}

// Moves a head-to-tracking pose by a head-axes offset: translation += R * offset. The rotation is untouched.
inline void applyAimEyeOffset(vr::HmdMatrix34_t& pose, const double offset[3]) {
  for (int r = 0; r < 3; ++r)
    pose.m[r][3] = float(double(pose.m[r][3]) + pose.m[r][0] * offset[0] + pose.m[r][1] * offset[1] + pose.m[r][2] * offset[2]);
}

// The aiming eye's log lines, once a runtime frame:
//   aim_eye_on,eye=right,shift_mm=31.8,sequence=N   the blend left 0 (shift_mm is the full eye offset blend 1 moves by)
//   aim_eye_off,sequence=N                          ...and came back to 0
//   aim_eye_summary,frames=F,shifted=S,max_mm=M,channel=C   every 30 s while the d3d11 half says on foot, or ALWAYS when the channel is below 6 (an
//     older d3d11.dll cannot say on foot): channel=5 with shifted=0 then reads "the fix never reached the runtime", not "LT was never held".
class AimEyeLog {
 public:
  static constexpr uint64_t kWindowMs = 30000;
  template<class Sink>
  void frame(uint32_t channel, uint32_t eye, float blend, uint32_t onFoot, double fullMm, uint64_t sequence, uint64_t nowMs, Sink&& sink) {
    char line[160];
    const bool shifted = blend > 0.0f && fullMm > 0.0;
    if (shifted && !on_) {
      std::snprintf(line, sizeof(line), "aim_eye_on,eye=%s,shift_mm=%.1f,sequence=%llu\n", aimEyeName(eye), fullMm, (unsigned long long)sequence);
      sink(line);
    } else if (!shifted && on_) {
      std::snprintf(line, sizeof(line), "aim_eye_off,sequence=%llu\n", (unsigned long long)sequence);
      sink(line);
    }
    on_ = shifted;
    if (!windowStarted_) { windowStarted_ = true; windowBegan_ = nowMs; }
    ++frames_;
    if (shifted) { ++shifted_; const double mm = fullMm * (blend > 1.0f ? 1.0 : double(blend)); if (mm > maxMm_) maxMm_ = mm; }
    if (onFoot) onFootSeen_ = true;
    if (nowMs - windowBegan_ >= kWindowMs) {
      if (onFootSeen_ || channel < 6) {
        std::snprintf(line, sizeof(line), "aim_eye_summary,frames=%llu,shifted=%llu,max_mm=%.1f,channel=%u\n", (unsigned long long)frames_,
                      (unsigned long long)shifted_, maxMm_, channel);
        sink(line);
      }
      windowBegan_ = nowMs; frames_ = shifted_ = 0; maxMm_ = 0.0; onFootSeen_ = false;
    }
  }
  bool on() const { return on_; }
 private:
  bool on_ = false, windowStarted_ = false, onFootSeen_ = false;
  uint64_t windowBegan_ = 0, frames_ = 0, shifted_ = 0;
  double maxMm_ = 0.0;
};

}  // namespace edvr::openxr
