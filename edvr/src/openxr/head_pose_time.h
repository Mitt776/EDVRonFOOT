#pragma once

// Elite's "now" head pose, answered one display period after the drawn frame's display time (docs\terrain-culling.md, "What EDVR does now").
//
// THE DEFECT. Elite builds its planet-terrain culling camera from a head pose it asks IVRSystem::GetDeviceToAbsoluteTrackingPose for with a
// prediction of about 0 s, i.e. "now", while it DRAWS with the pose WaitGetPoses handed the render thread, located at the frame's
// predictedDisplayTime. Located at the wall clock, the culling pose sat 41-44 ms before the drawn one (flight 3, 2026-10-09) and was
// turned up to 4.1 degrees from it, in step with head speed (r = +0.98): the terrain tiles chosen for the leading edge of a head turn were
// the wrong ones, and the black squares were the gap.
//
// THE MODEL THAT FITS FLIGHTS 3 TO 5 (pilot-judged, not an observation of the culling camera's consumer). Elite's game thread calls this
// function directly BETWEEN frames, for work that feeds the NEXT frame, so the right pose is the one the next frame will be drawn with: the
// latest frame's display time plus that frame's display period. While a frame renders the game thread reads that frame's own latched
// WaitGetPoses pose, which is already right for what it computes then (the DSS scanner UI included). Located at the display time itself
// (flight 3's fix, `display`) the direct calls were one period short, and squares remained on rapid turns; forcing the latched readers
// onto the direct path as well (an engine patch, `display_direct` / `next_direct`) put them a period ahead, flashed black on a rapid turn
// and made the scanner UI follow the head. Answering the direct calls one period past the display time and touching nothing in the game
// (`next`) showed neither.
//
// THE FILTER. Which requests are Elite's own "now"? Not a build's return address (a game update would silently stop the fix): any call
// whose return address lies inside the game executable's mapped image and whose prediction is under 5 ms either way. A real prediction (a
// caller that wants the future), a caller outside the image (an overlay, a tool) and a call before any frame has been waited are answered
// as they always were, at the wall clock plus the prediction.
#include "exe_module.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>

namespace edvr::openxr {

// A prediction this close to zero is a request for "now". The bound is exclusive: exactly 5 ms is a prediction.
constexpr float kNowPredictionLimitSeconds = 0.005f;

inline bool isNowRequest(float predictionSeconds) {
  return std::fabs(predictionSeconds) < kNowPredictionLimitSeconds;   // (a NaN is false: the caller refuses it before this)
}

// Is this call one the display time answers? `returnRva` is the caller's return address as an RVA in the game executable (frameRva):
// a value below kFrameUnknown is inside the image.
inline bool answeredAtDisplayTime(uint32_t returnRva, float predictionSeconds) {
  return returnRva < kFrameUnknown && isNowRequest(predictionSeconds);
}

// The latest frame's predictedDisplayTime, or false when there is none yet (no frame waited, a display time that is not positive) and the call
// falls back to now + prediction. (The instant a call is located at is this plus the frame's period: answerTime below.)
inline bool displayTimeTarget(int64_t latestDisplayTime, int64_t* out) {
  if (latestDisplayTime <= 0) return false;
  *out = latestDisplayTime;
  return true;
}

// HOW LONG A DISPLAY TIME STAYS GOOD. The latest frame's display time is the instant of the frame being drawn, and a real one is always
// ahead of now (flight 3 measured now about 42 ms before it). It stops being usable when it is behind now by more than one display period:
// by then the display has moved on to a later frame, so the instant belongs to a frame that is no longer the one being drawn. That
// is what a session that has stopped, a recenter, a loading screen where WaitGetPoses does not run, or a frame that was never replaced
// look like to this cache, and a runtime may answer such a time with stale tracking or reject it. The tolerance is the frame's own period
// (its predictedDisplayPeriod), capped at 50 ms so a period that is not a display period cannot stretch it; with no period it is 0, and the
// display time must not be behind now at all.
constexpr int64_t kPoseFrameMaxToleranceNs = 50000000;

inline int64_t poseFrameTolerance(int64_t periodNs) {
  if (periodNs <= 0) return 0;
  return periodNs > kPoseFrameMaxToleranceNs ? kPoseFrameMaxToleranceNs : periodNs;
}

// Is `displayTime` (positive: displayTimeTarget has accepted it) still good at `now`? Both are runtime times in ns.
inline bool displayTimeFresh(int64_t displayTime, int64_t periodNs, int64_t now) {
  if (displayTime >= now) return true;                               // ahead of now, or exactly now: the normal case
  if (displayTime <= 0 || now <= 0) return false;                    // not a time this comparison can judge; the caller falls back
  return now - displayTime <= poseFrameTolerance(periodNs);          // both positive, so the difference cannot overflow
}

// THE INSTANT. One display period after the cached frame's display time (`addPeriod(display, period, out)` is space_pose.h nextPredictionTime,
// the arithmetic the game-pose array of WaitGetPoses already uses). The cached frame must still be fresh (displayTimeFresh), judged on the
// CACHED frame's display time, never on the target a period ahead of it. Returns false, and the call is located at now + prediction (counted
// as a fallback), when there is no usable display time, when the cached frame is no longer fresh, when the period cannot be added (no
// period, or the sum would overflow), or when a reference-space change falls between the display time and the target
// (`crosses(from, to)`, reference_changes.h): a pose across it would be in another origin.
template <class AddPeriod, class Crosses>
inline bool answerTime(int64_t latestDisplayTime, int64_t periodNs, int64_t now, AddPeriod&& addPeriod, Crosses&& crosses, int64_t* out) {
  int64_t display = 0;
  if (!displayTimeTarget(latestDisplayTime, &display)) return false;
  if (!displayTimeFresh(display, periodNs, now)) return false;
  int64_t target = 0;
  if (!addPeriod(display, periodNs, target)) return false;
  if (crosses(display, target)) return false;
  *out = target;
  return true;
}

// ---- THE ON-FOOT MARKER POSE PROBE (TEMPORARY; openspec change on-foot-marker-pose) --------------------------------------------------------------
// On foot the game draws at 45 fps on a 90 Hz display, so the next frame is shown TWO periods after the drawn one, and the answer above (one
// period) trails the next drawn pose by a period. With experimental.native_vr_on_foot_marker_pose = frame (the d3d11 half's markerPoseFrame 1:
// the key says frame and the journal says on foot) the "now" answer and the gameplay pose of WaitGetPoses use the MEASURED step between the
// display times the game was handed instead of the period: answerTime(display, step, ...) -- the target display + step, and the freshness
// tolerance the step (capped at 50 ms as before). Everywhere else, and with the key at next, the period: exactly the 0.1.5 answer.

// How many display-time differences the step is the median of.
constexpr unsigned kFrameStepHistory = 16;
constexpr int64_t kFrameStepMaxNs = 50000000;

// The step, ns: the median of `n` differences (the upper middle one for an even count), held to at least one period and at most
// min(4 periods, 50 ms) (never below one period). No period (<= 0) is 0, which no instant can be formed from; no history is one period.
inline int64_t frameStepNs(const int64_t* diffs, unsigned n, int64_t periodNs) {
  if (periodNs <= 0) return 0;
  if (!diffs || n == 0) return periodNs;
  if (n > kFrameStepHistory) n = kFrameStepHistory;
  int64_t sorted[kFrameStepHistory];
  for (unsigned i = 0; i < n; ++i) sorted[i] = diffs[i];
  for (unsigned i = 1; i < n; ++i)                                   // insertion sort: sixteen values at most
    for (unsigned j = i; j > 0 && sorted[j - 1] > sorted[j]; --j) { const int64_t t = sorted[j]; sorted[j] = sorted[j - 1]; sorted[j - 1] = t; }
  int64_t step = sorted[n / 2];
  int64_t hi = periodNs > kFrameStepMaxNs / 4 ? kFrameStepMaxNs : periodNs * 4;
  if (hi > kFrameStepMaxNs) hi = kFrameStepMaxNs;
  if (hi < periodNs) hi = periodNs;
  if (step < periodNs) step = periodNs;
  if (step > hi) step = hi;
  return step;
}

// The last kFrameStepHistory differences between the display times the game was handed (frame.predictedDisplayTime after admitFrame). A time that
// is not positive is ignored; one that does not move forward is kept as the new base and adds no difference. reset() on a new session, origin or
// geometry (the host's dropPoseFrame).
class FrameStepHistory {
 public:
  void note(int64_t displayTime) {
    if (displayTime <= 0) return;
    if (last_ > 0 && displayTime > last_) {
      diffs_[next_] = displayTime - last_;
      next_ = (next_ + 1) % kFrameStepHistory;
      if (count_ < kFrameStepHistory) ++count_;
    }
    last_ = displayTime;
  }
  void reset() { count_ = 0; next_ = 0; last_ = 0; }
  unsigned count() const { return count_; }
  int64_t step(int64_t periodNs) const { return frameStepNs(diffs_, count_, periodNs); }

 private:
  int64_t diffs_[kFrameStepHistory]{};
  unsigned count_ = 0, next_ = 0;
  int64_t last_ = 0;
};

// The probe's log lines (runtime log), written from WaitGetPoses:
//   marker pose: frame (on foot, measured frame step)      -- on each change of the mode in force, with the reason
//   marker pose: next (key next) | (not on foot) | (d3d11.dll without the field) | (no d3d11 half)
//   marker pose: frame, step p50 22.22 ms max 22.22 ms, answered 1234   -- every 30 s while frame is in force, and once more as it ends
// `answered` counts the "now" answers that were located by the step; 0 while frame is in force means the probe never acted.
class MarkerPoseLog {
 public:
  static constexpr uint64_t kWindowMs = 30000;
  static constexpr unsigned kSteps = 4096;   // 30 s at 90 Hz is 2700
  // One WaitGetPoses frame. `flag` is the d3d11 half's markerPoseFrame (0 next, 1 frame on foot, 2 frame not on foot), `stepNs` the step this
  // frame would use. Returns true when the mode in force changed (the pose-gap window closes then).
  template <class Sink>
  bool frame(bool acquired, uint32_t providerVersion, uint32_t flag, int64_t stepNs, uint64_t nowMs, Sink&& sink) {
    const bool frameMode = acquired && providerVersion >= 9 && flag == 1;
    bool changed = false;
    if (frameMode != active_) {
      if (active_) flush(sink);                                      // the partial window of the mode that ends
      active_ = frameMode;
      changed = true;
      const char* reason = frameMode ? "on foot, measured frame step"
                         : !acquired ? "no d3d11 half"
                         : providerVersion < 9 ? "d3d11.dll without the field"
                         : flag == 2 ? "not on foot" : "key next";
      char line[128];
      std::snprintf(line, sizeof(line), "marker pose: %s (%s)", frameMode ? "frame" : "next", reason);
      sink(static_cast<const char*>(line));
      if (frameMode) { startMs_ = nowMs; steps_ = 0; answered_ = 0; }
    }
    if (active_) {
      if (stepNs > 0 && steps_ < kSteps) step_[steps_++] = stepNs;
      if (nowMs - startMs_ >= kWindowMs) { flush(sink); startMs_ = nowMs; }
    }
    return changed;
  }
  void noteAnswered() { if (active_) ++answered_; }
  bool active() const { return active_; }

 private:
  template <class Sink>
  void flush(Sink&& sink) {
    char line[160];
    if (steps_ == 0) {
      std::snprintf(line, sizeof(line), "marker pose: frame, step n/a, answered %llu", (unsigned long long)answered_);
    } else {
      static int64_t sorted[kSteps];                                  // owner thread only
      for (unsigned i = 0; i < steps_; ++i) sorted[i] = step_[i];
      std::sort(sorted, sorted + steps_);
      std::snprintf(line, sizeof(line), "marker pose: frame, step p50 %.2f ms max %.2f ms, answered %llu",
                    double(sorted[steps_ / 2]) / 1e6, double(sorted[steps_ - 1]) / 1e6, (unsigned long long)answered_);
    }
    sink(static_cast<const char*>(line));
    steps_ = 0; answered_ = 0;
  }
  bool active_ = false;
  uint64_t startMs_ = 0, answered_ = 0;
  unsigned steps_ = 0;
  int64_t step_[kSteps]{};
};

// The log lines that say the fix is acting, and for whom. The first caller to qualify writes one line; each further DISTINCT return RVA that
// qualifies writes one more, up to kFurtherCap, so a caller that turns up after a game update is named. One more line says the cap was reached.
//   head pose: Elite's "now" requests are answered one display period after the drawn frame's display time (first from exe+0x...)
//   head pose: another Elite caller of "now" is answered one display period after the drawn frame's display time (exe+0x...)
//   head pose: more than 8 further callers of "now"; the rest are not logged
class HeadPoseSightings {
 public:
  static constexpr unsigned kFurtherCap = 8;
  template <class Sink>
  void note(uint32_t returnRva, Sink&& sink) {
    for (unsigned i = 0; i < count_; ++i)
      if (seen_[i] == returnRva) return;
    char line[192];
    if (count_ == 0) {
      std::snprintf(line, sizeof(line), "head pose: Elite's \"now\" requests are answered one display period after the drawn frame's display time (first from exe+0x%X)", returnRva);
    } else if (count_ <= kFurtherCap) {
      std::snprintf(line, sizeof(line), "head pose: another Elite caller of \"now\" is answered one display period after the drawn frame's display time (exe+0x%X)", returnRva);
    } else {
      if (!capSaid_) {
        capSaid_ = true;
        std::snprintf(line, sizeof(line), "head pose: more than %u further callers of \"now\"; the rest are not logged", kFurtherCap);
        sink(static_cast<const char*>(line));
      }
      return;
    }
    seen_[count_++] = returnRva;
    sink(static_cast<const char*>(line));
  }
  unsigned distinct() const { return count_; }

 private:
  uint32_t seen_[kFurtherCap + 1]{};
  unsigned count_ = 0;
  bool capSaid_ = false;
};

}  // namespace edvr::openxr
