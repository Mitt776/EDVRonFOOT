#pragma once
// The aiming eye, runtime side (openspec change on-foot-ads-dominant-eye): while the commander holds LT on foot the d3d11 half sends an eye and a blend
// (EdvrNativeFrameOutput::aimEye/aimEyeBlend, native_frame.h version 6, aim_eye_signal.h), and the head pose handed to the GAME is moved by minus that
// much of the eye's offset from the head, in the head's own axes. The game's eye (head * GetEyeToHeadTransform) then stands where the middle of the head
// was: on the line of the iron sights. The poses the layer is composed with (frameViews, d3d11_stereo.cpp) are never shifted, so the headset takes the
// image as seen from the real eyes and does not turn it back; rotation and the distance between the game's eyes are untouched.
// The position snap (openspec change on-foot-ads-position-snap): as the blend leaves 0, AimSnap keeps how far the head has wandered from the seated
// origin (the last recenter put the origin in the head, so that is the pose's own translation), and while the blend holds, the game's head pose is moved
// back by blend times that, in tracking axes and without any rotation; the eye shift is added on top. At blend 1 the game's aiming eye stands at the
// origin, as right after a recenter. Kept once per press, dropped when the blend is back to 0 or the origin changes; more than kAimSnapMaxM away is a
// forgotten recenter or a tracking fault, and that press gets no snap.
// The head aim probe (openspec change on-foot-head-aim-probe): the d3d11 half shifts one of the game's angles of the commander and sends the angle the
// game's head pose is turned by about the seated space's vertical (EdvrNativeFrameOutput::headAimYawRad, version 7), so that the game's camera (head
// pose x the character's eye) can be held still while the character turns. The whole pose turns, rotation and translation, about the axis through the
// seated origin, last of all, after the eye shift and the snap. Never the layer.
// The head aim feature (openspec change on-foot-head-aim): the same, then a second turn about the seated X axis by headAimPitchRad (version 8).
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

// Moves a head-to-tracking pose by a tracking-axes offset: translation += offset. The rotation is untouched.
inline void applyTrackingOffset(vr::HmdMatrix34_t& pose, const double offset[3]) {
  for (int r = 0; r < 3; ++r) pose.m[r][3] = float(double(pose.m[r][3]) + offset[r]);
}

// Turns a head-to-tracking pose about the tracking space's vertical (+Y) through its origin by `yawRad`: pose = R(yawRad) * pose, rotation and
// translation alike. A positive angle turns +Z toward +X; which sign holds the world still is the probe's flight to find. 0 and anything
// that is not a finite angle leave the pose alone.
inline void turnAboutVertical(vr::HmdMatrix34_t& pose, double yawRad) {
  if (yawRad == 0.0 || !std::isfinite(yawRad)) return;
  const double c = std::cos(yawRad), s = std::sin(yawRad);
  for (int col = 0; col < 4; ++col) {
    const double x = pose.m[0][col], z = pose.m[2][col];
    pose.m[0][col] = float(c * x + s * z);
    pose.m[2][col] = float(-s * x + c * z);
  }
}

// Turns a head-to-tracking pose about the tracking space's X axis (the horizontal) through the origin by `pitchRad`: pose = R_x(pitchRad) * pose,
// rotation and translation alike, right-handed like turnAboutVertical (y' = c*y - s*z, z' = s*y + c*z). A head 20 degrees up is R_x(+20), so the
// angle -20 (the game's tilt, + = down) brings it back to the identity. 0 and anything that is not a finite angle leave the pose alone.
inline void turnAboutHorizontal(vr::HmdMatrix34_t& pose, double pitchRad) {
  if (pitchRad == 0.0 || !std::isfinite(pitchRad)) return;
  const double c = std::cos(pitchRad), s = std::sin(pitchRad);
  for (int col = 0; col < 4; ++col) {
    const double y = pose.m[1][col], z = pose.m[2][col];
    pose.m[1][col] = float(c * y - s * z);
    pose.m[2][col] = float(s * y + c * z);
  }
}

// The head aim's runtime line:
//   head_aim_runtime,frames=F,probe=P,turned=T,max_deg=M,channel=C,pitched=N,max_pitch_deg=X   every 30 s while the d3d11 half says the probe or the
//     feature acts (probe or feature > 0), or ALWAYS when the channel is below 8 (an older d3d11.dll cannot tilt anything): channel=7 with pitched=0
//     then reads "the tilt never reached the runtime", channel=6 "the turn never did either".
class HeadAimLog {
 public:
  static constexpr uint64_t kWindowMs = 30000;
  template<class Sink>
  void frame(uint32_t channel, uint32_t probe, uint32_t feature, double yawRad, double pitchRad, uint64_t nowMs, Sink&& sink) {
    if (!windowStarted_) { windowStarted_ = true; windowBegan_ = nowMs; }
    ++frames_;
    if (probe) ++probe_;
    if (feature) ++feature_;
    if (yawRad != 0.0 && std::isfinite(yawRad)) {
      ++turned_;
      const double deg = std::fabs(yawRad) * 57.29577951308232;
      if (deg > maxDeg_) maxDeg_ = deg;
    }
    if (pitchRad != 0.0 && std::isfinite(pitchRad)) {
      ++pitched_;
      const double deg = std::fabs(pitchRad) * 57.29577951308232;
      if (deg > maxPitchDeg_) maxPitchDeg_ = deg;
    }
    if (nowMs - windowBegan_ >= kWindowMs) {
      if (probe_ || feature_ || channel < 8) {
        char line[200];
        std::snprintf(line, sizeof(line), "head_aim_runtime,frames=%llu,probe=%llu,turned=%llu,max_deg=%.1f,channel=%u,pitched=%llu,max_pitch_deg=%.1f\n",
                      (unsigned long long)frames_, (unsigned long long)probe_, (unsigned long long)turned_, maxDeg_, channel,
                      (unsigned long long)pitched_, maxPitchDeg_);
        sink(line);
      }
      windowBegan_ = nowMs; frames_ = probe_ = feature_ = turned_ = pitched_ = 0; maxDeg_ = maxPitchDeg_ = 0.0;
    }
  }
 private:
  bool windowStarted_ = false;
  uint64_t windowBegan_ = 0, frames_ = 0, probe_ = 0, feature_ = 0, turned_ = 0, pitched_ = 0;
  double maxDeg_ = 0.0, maxPitchDeg_ = 0.0;
};

// A head further than this from the origin as LT is pressed is not "slid down in the chair": no snap for that press.
constexpr double kAimSnapMaxM = 0.5;

// none: nothing kept (no press, or no valid pose yet this press); held: the wander is kept and snapped back; far: kept but beyond kAimSnapMaxM or not
// finite, so no snap until the blend is back to 0.
enum class AimSnapState : uint32_t { None = 0, Held, Far };
inline const char* aimSnapName(AimSnapState s) { return s == AimSnapState::Held ? "held" : s == AimSnapState::Far ? "far" : "none"; }

class AimSnap {
 public:
  // Once a runtime frame, with the channel's eye and blend and the frame's real (unshifted) head-to-tracking pose. Keeps the pose's translation when the
  // blend is above 0 for eye 1/2 and nothing is kept yet; a blend back to 0 (or no eye) drops it.
  void frame(uint32_t aimEye, float blend, const vr::HmdMatrix34_t& pose, bool poseValid) {
    if ((aimEye != 1 && aimEye != 2) || !(blend > 0.0f)) { reset(); return; }
    if (state_ != AimSnapState::None || !poseValid) return;
    for (int i = 0; i < 3; ++i) p0_[i] = pose.m[i][3];
    const double len = std::sqrt(p0_[0] * p0_[0] + p0_[1] * p0_[1] + p0_[2] * p0_[2]);
    state_ = std::isfinite(len) && len <= kAimSnapMaxM ? AimSnapState::Held : AimSnapState::Far;
  }
  void reset() { state_ = AimSnapState::None; p0_[0] = p0_[1] = p0_[2] = 0.0; }
  // This frame's offset in tracking axes and metres: -blend * the kept wander (blend clamped to 1). False, and zero, unless held and the blend is above 0.
  bool offset(float blend, double out[3]) const {
    out[0] = out[1] = out[2] = 0.0;
    if (state_ != AimSnapState::Held || !(blend > 0.0f)) return false;
    const double b = blend > 1.0f ? 1.0 : double(blend);
    for (int i = 0; i < 3; ++i) out[i] = -b * p0_[i];
    return true;
  }
  AimSnapState state() const { return state_; }
  // The kept wander's length and its vertical part (minus: the head is below the origin), millimetres.
  double mm() const { return 1000.0 * std::sqrt(p0_[0] * p0_[0] + p0_[1] * p0_[1] + p0_[2] * p0_[2]); }
  double yMm() const { return 1000.0 * p0_[1]; }
 private:
  AimSnapState state_ = AimSnapState::None;
  double p0_[3]{};
};

// The aiming eye's log lines, once a runtime frame:
//   aim_eye_on,eye=right,shift_mm=31.8,snap=held,snap_mm=48.2,snap_y_mm=-45.0,sequence=N   the blend left 0 (shift_mm is the full eye offset blend 1
//     moves by; snap_mm the kept wander from the origin, snap_y_mm its vertical part). snap=far,snap_mm=612.0: beyond kAimSnapMaxM, no snap this press;
//     snap=none: no valid pose this frame, the wander is kept next frame
//   aim_eye_off,sequence=N                          ...and came back to 0
//   aim_eye_summary,frames=F,shifted=S,max_mm=M,max_snap_mm=P,channel=C   every 30 s while the d3d11 half says on foot, or ALWAYS when the channel is
//     below 6 (an older d3d11.dll cannot say on foot): channel=5 with shifted=0 then reads "the fix never reached the runtime", not "LT was never held".
//     max_snap_mm is the largest blend * snap_mm of the window.
class AimEyeLog {
 public:
  static constexpr uint64_t kWindowMs = 30000;
  template<class Sink>
  void frame(uint32_t channel, uint32_t eye, float blend, uint32_t onFoot, double fullMm, const AimSnap& snap, uint64_t sequence, uint64_t nowMs,
             Sink&& sink) {
    char line[200];
    const bool shifted = blend > 0.0f && fullMm > 0.0;
    if (shifted && !on_) {
      char snapPart[80];
      if (snap.state() == AimSnapState::Held)
        std::snprintf(snapPart, sizeof(snapPart), "snap=held,snap_mm=%.1f,snap_y_mm=%.1f", snap.mm(), snap.yMm());
      else if (snap.state() == AimSnapState::Far)
        std::snprintf(snapPart, sizeof(snapPart), "snap=far,snap_mm=%.1f", snap.mm());
      else
        std::snprintf(snapPart, sizeof(snapPart), "snap=none");
      std::snprintf(line, sizeof(line), "aim_eye_on,eye=%s,shift_mm=%.1f,%s,sequence=%llu\n", aimEyeName(eye), fullMm, snapPart,
                    (unsigned long long)sequence);
      sink(line);
    } else if (!shifted && on_) {
      std::snprintf(line, sizeof(line), "aim_eye_off,sequence=%llu\n", (unsigned long long)sequence);
      sink(line);
    }
    on_ = shifted;
    if (!windowStarted_) { windowStarted_ = true; windowBegan_ = nowMs; }
    ++frames_;
    if (shifted) {
      ++shifted_;
      const double b = blend > 1.0f ? 1.0 : double(blend);
      if (fullMm * b > maxMm_) maxMm_ = fullMm * b;
      if (snap.state() == AimSnapState::Held && snap.mm() * b > maxSnapMm_) maxSnapMm_ = snap.mm() * b;
    }
    if (onFoot) onFootSeen_ = true;
    if (nowMs - windowBegan_ >= kWindowMs) {
      if (onFootSeen_ || channel < 6) {
        std::snprintf(line, sizeof(line), "aim_eye_summary,frames=%llu,shifted=%llu,max_mm=%.1f,max_snap_mm=%.1f,channel=%u\n",
                      (unsigned long long)frames_, (unsigned long long)shifted_, maxMm_, maxSnapMm_, channel);
        sink(line);
      }
      windowBegan_ = nowMs; frames_ = shifted_ = 0; maxMm_ = maxSnapMm_ = 0.0; onFootSeen_ = false;
    }
  }
  bool on() const { return on_; }
 private:
  bool on_ = false, windowStarted_ = false, onFootSeen_ = false;
  uint64_t windowBegan_ = 0, frames_ = 0, shifted_ = 0;
  double maxMm_ = 0.0, maxSnapMm_ = 0.0;
};

}  // namespace edvr::openxr
