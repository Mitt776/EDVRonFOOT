#pragma once
// The aiming eye, runtime side (aim_eye_shift.h; openspec change on-foot-ads-dominant-eye): the offset the game's head pose is moved by, that the game's
// aiming eye then stands where the middle of the head was at any head rotation, the version ladder against a d3d11.dll of either age, and the log lines.
// The position snap (on-foot-ads-position-snap): the wander kept once per press, the game's aiming eye back at the origin, the 0.5 m limit, the log fields.
#include "../../src/openxr/aim_eye_shift.h"
#include "../../src/openxr/native_frame_client.h"
#include "../../src/d3d11/head_aim_core.h"
#include <cmath>
#include <limits>
#include <string>
#include <vector>

namespace edvr::openxr::test {
namespace aim_fixture {
// A head-to-tracking pose: rotation about y (yaw), then about z (roll), degrees, at a position.
inline vr::HmdMatrix34_t headPose(double yawDeg, double rollDeg, double x, double y, double z) {
  const double k = 3.14159265358979323846 / 180.0;
  const double cy = std::cos(yawDeg * k), sy = std::sin(yawDeg * k), cr = std::cos(rollDeg * k), sr = std::sin(rollDeg * k);
  const double ry[3][3] = {{cy, 0, sy}, {0, 1, 0}, {-sy, 0, cy}};
  const double rz[3][3] = {{cr, -sr, 0}, {sr, cr, 0}, {0, 0, 1}};
  vr::HmdMatrix34_t m{};
  for (int i = 0; i < 3; ++i)
    for (int j = 0; j < 3; ++j) {
      double v = 0;
      for (int n = 0; n < 3; ++n) v += ry[i][n] * rz[n][j];
      m.m[i][j] = float(v);
    }
  m.m[0][3] = float(x); m.m[1][3] = float(y); m.m[2][3] = float(z);
  return m;
}
inline vr::HmdMatrix34_t eye(double x, double y, double z) {
  vr::HmdMatrix34_t m{};
  m.m[0][0] = m.m[1][1] = m.m[2][2] = 1.0f;
  m.m[0][3] = float(x); m.m[1][3] = float(y); m.m[2][3] = float(z);
  return m;
}
// Where the game puts an eye: head * eyeToHead, its translation.
inline void eyeWorld(const vr::HmdMatrix34_t& head, const vr::HmdMatrix34_t& e, double out[3]) {
  for (int r = 0; r < 3; ++r) out[r] = head.m[r][3] + head.m[r][0] * e.m[0][3] + head.m[r][1] * e.m[1][3] + head.m[r][2] * e.m[2][3];
}
// A provider of a given age writing a given aim signal; it refuses any shape above maxVersion.
struct Provider {
  uint32_t maxVersion = 6, eye = 2, onFoot = 1; float blend = 1.0f; bool scribble = false;
  HRESULT answer(EdvrNativeFrameOutput& o) {
    if (o.version > maxVersion || o.version < EDVR_NATIVE_FRAME_VERSION_1) return E_INVALIDARG;
    const uint32_t want = o.version == 8 ? uint32_t(sizeof(o)) : o.version == 7 ? EDVR_NATIVE_FRAME_OUTPUT_SIZE_7 : o.version == 6 ? EDVR_NATIVE_FRAME_OUTPUT_SIZE_6 : o.version == 5 ? EDVR_NATIVE_FRAME_OUTPUT_SIZE_5 : o.version == 4 ? EDVR_NATIVE_FRAME_OUTPUT_SIZE_4
      : o.version == 3 ? EDVR_NATIVE_FRAME_OUTPUT_SIZE_3 : o.version == 2 ? EDVR_NATIVE_FRAME_OUTPUT_SIZE_2 : EDVR_NATIVE_FRAME_OUTPUT_SIZE_1;
    if (o.size != want) return E_INVALIDARG;
    if (o.version >= 6 || scribble) { o.aimEye = eye; o.aimEyeBlend = blend; o.aimOnFoot = onFoot; }
    return S_OK;
  }
};
}  // namespace aim_fixture

template<class Check> void runAimEyeCases(Check&& check) {
  using namespace aim_fixture;
  const vr::HmdMatrix34_t eyes[2] = {eye(-0.032, 0, 0), eye(0.032, 0, 0)};
  // ---- the offset -----------------------------------------------------------------------------------------------------------------------------------------
  {
    double o[3];
    check(aimEyeHeadOffset(eyes, 2, 1.0f, o) && std::fabs(o[0] + 0.032) < 1e-6 && o[1] == 0.0 && o[2] == 0.0,
          "RIGHT EYE, blend 1: the head moves 32 mm to its left (minus the right eye's offset), nothing else");
    check(aimEyeHeadOffset(eyes, 1, 1.0f, o) && std::fabs(o[0] - 0.032) < 1e-6, "LEFT EYE: the same distance to the right");
    check(aimEyeHeadOffset(eyes, 2, 0.5f, o) && std::fabs(o[0] + 0.016) < 1e-6, "half a blend is half the offset");
    check(aimEyeHeadOffset(eyes, 2, 7.0f, o) && std::fabs(o[0] + 0.032) < 1e-6, "a blend above 1 is clamped to 1");
    check(!aimEyeHeadOffset(eyes, 2, 0.0f, o) && o[0] == 0.0, "blend 0: no offset");
    check(!aimEyeHeadOffset(eyes, 2, std::numeric_limits<float>::quiet_NaN(), o) && o[0] == 0.0, "a NaN blend: no offset");
    check(!aimEyeHeadOffset(eyes, 0, 1.0f, o) && !aimEyeHeadOffset(eyes, 3, 1.0f, o) && o[0] == 0.0, "no eye (0) or an unknown one (3): no offset");
    const vr::HmdMatrix34_t wild[2] = {eye(-0.5, 0, 0), eye(std::numeric_limits<double>::infinity(), 0, 0)};
    check(!aimEyeHeadOffset(wild, 1, 1.0f, o) && !aimEyeHeadOffset(wild, 2, 1.0f, o), "an implausible (50 cm) or infinite eye offset: no offset");
    check(std::fabs(aimEyeFullMm(eyes, 2) - 32.0) < 1e-3 && std::fabs(aimEyeFullMm(eyes, 1) - 32.0) < 1e-3 && aimEyeFullMm(eyes, 0) == 0.0,
          "the full shift in mm is the eye's distance from the head: 32 for either eye, 0 for none");
  }
  // ---- the game's aiming eye lands on the middle of the head, at any head rotation (3.5: 0, 90 yaw, 30 roll) ------------------------------------------------
  const double turns[][2] = {{0, 0}, {90, 0}, {0, 30}, {90, 30}};
  for (const auto& t : turns) {
    for (uint32_t which : {1u, 2u}) {
      const auto real = headPose(t[0], t[1], 0.4, 1.6, -0.2);
      auto shifted = real;
      double o[3];
      aimEyeHeadOffset(eyes, which, 1.0f, o);
      applyAimEyeOffset(shifted, o);
      double w[3];
      eyeWorld(shifted, eyes[which - 1], w);
      const double miss = std::sqrt((w[0] - 0.4) * (w[0] - 0.4) + (w[1] - 1.6) * (w[1] - 1.6) + (w[2] + 0.2) * (w[2] + 0.2));
      bool rotationSame = true;
      for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j) rotationSame = rotationSame && shifted.m[i][j] == real.m[i][j];
      double other[3], otherReal[3];
      eyeWorld(shifted, eyes[2 - which], other);
      eyeWorld(real, eyes[2 - which], otherReal);
      const double ipdShifted = std::sqrt((other[0] - w[0]) * (other[0] - w[0]) + (other[1] - w[1]) * (other[1] - w[1]) + (other[2] - w[2]) * (other[2] - w[2]));
      check(miss < 1e-5 && rotationSame && std::fabs(ipdShifted - 0.064) < 1e-5,
            "AT YAW 0/90 AND ROLL 0/30, either eye: the game's aiming eye stands exactly where the middle of the head was, the rotation is untouched and the "
            "distance between the game's eyes is still 64 mm");
    }
  }
  // ---- the ladder: runtime 6 against a d3d11.dll of either age (5/6) --------------------------------------------------------------------------------------
  {
    using Client = edvr::openxr::NativeFrameClient;
    auto ladder = [&](Provider& p, uint32_t& version, EdvrNativeFrameOutput& out) {
      return Client::beginLadder([&](EdvrNativeFrameOutput& o) { return p.answer(o); }, version, out);
    };
    Provider p; uint32_t v = 6; EdvrNativeFrameOutput out{};
    check(ladder(p, v, out) == S_OK && out.version == 6 && out.aimEye == 2 && out.aimEyeBlend == 1.0f && out.aimOnFoot == 1,
          "RUNTIME 6, D3D11 6: the eye, the blend and the on-foot flag arrive");
    Provider old; old.maxVersion = 5; v = 6;
    check(ladder(old, v, out) == S_OK && out.version == 5 && v == 5 && out.aimEye == 0 && out.aimEyeBlend == 0.0f && out.aimOnFoot == 0,
          "RUNTIME 6, D3D11 5: the client steps down to 5 and reads no shift (and no on-foot flag)");
    Provider liar; liar.maxVersion = 5; liar.scribble = true; v = 6;
    check(ladder(liar, v, out) == S_OK && out.version == 5 && out.aimEyeBlend == 0.0f && out.aimEye == 0,
          "a version 5 provider that writes the aim slots anyway is not believed");
    Provider nan; nan.blend = std::numeric_limits<float>::quiet_NaN(); v = 6;
    check(ladder(nan, v, out) == S_OK && out.aimEyeBlend == 0.0f && out.aimEye == 0, "a NaN blend reads no shift");
    Provider odd; odd.eye = 3; v = 6;
    check(ladder(odd, v, out) == S_OK && out.aimEyeBlend == 0.0f && out.aimEye == 0, "an eye that is neither 1 nor 2 reads no shift");
    check(Client::ask(6).aimEyeBlend == 0.0f && Client::ask(6).aimEye == 0, "a fresh ask carries no shift until a provider says otherwise");
    // RUNTIME 5 (before this change) against a d3d11.dll 6: native_frame_test proves the provider answers a version 5 struct in its own shape and never
    // writes the aim slots past it; such a runtime has no code that shifts anything.
  }
  // ---- the log lines --------------------------------------------------------------------------------------------------------------------------------------
  {
    std::vector<std::string> lines;
    auto sink = [&](const char* l) { lines.emplace_back(l); };
    auto count = [&](const char* prefix) { unsigned n = 0; for (const auto& l : lines) n += l.rfind(prefix, 0) == 0; return n; };
    const AimSnap none;
    AimEyeLog log;
    uint64_t ms = 1000, seq = 1;
    for (int i = 0; i < 10; ++i) log.frame(6, 2, 0.0f, 1, 32.0, none, seq++, ms += 11, sink);
    check(lines.empty(), "on foot, LT not held: no line yet");
    log.frame(6, 2, 0.13f, 1, 32.0, none, seq++, ms += 11, sink);
    check(lines.size() == 1 && lines[0] == "aim_eye_on,eye=right,shift_mm=32.0,snap=none,follow=0,sequence=11\n",
          "THE BLEND LEAVES 0: one aim_eye_on line, the eye and the full shift");
    for (int i = 0; i < 20; ++i) log.frame(6, 2, 1.0f, 1, 32.0, none, seq++, ms += 11, sink);
    check(lines.size() == 1, "...and nothing while it holds");
    log.frame(6, 2, 0.0f, 1, 32.0, none, seq++, ms += 11, sink);
    check(lines.size() == 2 && lines[1] == "aim_eye_off,sequence=32\n", "BACK TO 0: one aim_eye_off line");
    log.frame(6, 1, 0.5f, 1, 31.0, none, seq++, ms += 11, sink);
    check(count("aim_eye_on,eye=left,shift_mm=31.0") == 1, "a left-eye press names the left eye");
    log.frame(6, 1, 0.0f, 1, 31.0, none, seq++, ms += 11, sink);
    ms += AimEyeLog::kWindowMs;
    log.frame(6, 2, 0.0f, 1, 32.0, none, seq++, ms, sink);
    check(count("aim_eye_summary,frames=35,shifted=22,max_mm=32.0,max_snap_mm=0.0,channel=6") == 1,
          "30 S ON FOOT: a summary of all frames, the shifted ones and the largest shift");
    lines.clear();
    ms += AimEyeLog::kWindowMs;
    log.frame(6, 2, 0.0f, 0, 32.0, none, seq++, ms, sink);
    check(lines.empty(), "30 s in a ship (channel 6, not on foot): no summary");
    AimEyeLog oldPair;
    lines.clear(); ms = 5000;
    oldPair.frame(5, 0, 0.0f, 0, 0.0, none, 1, ms, sink);
    oldPair.frame(5, 0, 0.0f, 0, 0.0, none, 2, ms + AimEyeLog::kWindowMs, sink);
    check(lines.size() == 1 && lines[0] == "aim_eye_summary,frames=2,shifted=0,max_mm=0.0,max_snap_mm=0.0,channel=5\n",
          "AN OLDER D3D11.DLL (channel 5): the summary is written anyway, channel=5 and shifted=0 -- 'the fix never reached the runtime', not 'LT was never held'");
    AimEyeLog noGeometry;
    lines.clear();
    noGeometry.frame(6, 2, 1.0f, 1, 0.0, none, 1, 100, sink);
    check(lines.empty() && !noGeometry.on(), "a blend with no usable eye offset (0 mm) is not a shift and writes no aim_eye_on");
  }
  // ---- the position snap (openspec change on-foot-ads-position-snap) --------------------------------------------------------------------------------------
  auto offsetIs = [](const double o[3], double x, double y, double z) {
    return std::fabs(o[0] - x) < 1e-6 && std::fabs(o[1] - y) < 1e-6 && std::fabs(o[2] - z) < 1e-6;
  };
  const double snapTurns[][2] = {{0, 0}, {90, 0}, {0, 30}};
  for (const auto& t : snapTurns) {
    const auto real = headPose(t[0], t[1], 0.0, -0.05, 0.02);
    AimSnap snap;
    snap.frame(2, 1.0f, real, true);
    auto game = real;
    double s[3], o[3];
    const bool snapped = snap.offset(1.0f, s);
    aimEyeHeadOffset(eyes, 2, 1.0f, o);
    applyTrackingOffset(game, s);
    applyAimEyeOffset(game, o);
    double w[3];
    eyeWorld(game, eyes[1], w);
    bool rotationSame = true;
    for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j) rotationSame = rotationSame && game.m[i][j] == real.m[i][j];
    check(snapped && snap.state() == AimSnapState::Held && std::sqrt(w[0] * w[0] + w[1] * w[1] + w[2] * w[2]) < 1e-5 && rotationSame,
          "SLID DOWN 5 CM AND 2 CM BACK, AT YAW 0/90 AND ROLL 30, blend 1: the game's aiming eye stands exactly at the origin, the rotation is untouched");
  }
  {
    AimSnap snap;
    double s[3];
    snap.frame(2, 0.13f, headPose(0, 0, 0.0, -0.05, 0.02), true);
    check(snap.offset(0.5f, s) && offsetIs(s, 0.0, 0.025, -0.01), "half a blend is exactly half the snap");
    check(std::fabs(snap.mm() - 53.85) < 0.01 && std::fabs(snap.yMm() + 50.0) < 1e-3, "the kept wander in mm, and its vertical part with its sign");
    snap.frame(2, 1.0f, headPose(40, 10, 0.1, 0.2, 0.3), true);
    snap.frame(2, 1.0f, headPose(-20, 0, -0.1, 0.0, 0.1), true);
    check(snap.offset(1.0f, s) && offsetIs(s, 0.0, 0.05, -0.02), "THE HEAD MOVES WITH LT HELD: the kept wander does not change");
    snap.frame(2, 0.3f, headPose(0, 0, 0.2, 0.2, 0.2), true);
    snap.frame(2, 0.6f, headPose(0, 0, 0.2, 0.2, 0.2), true);
    check(snap.offset(1.0f, s) && offsetIs(s, 0.0, 0.05, -0.02), "PRESSED AGAIN FROM A BLEND OF 0.3: the old wander, no jump");
    snap.frame(2, 0.0f, headPose(0, 0, 0.2, 0.2, 0.2), true);
    check(snap.state() == AimSnapState::None && !snap.offset(1.0f, s) && offsetIs(s, 0, 0, 0), "BLEND BACK TO 0: dropped, no offset");
    snap.frame(1, 0.1f, headPose(0, 0, 0.01, -0.02, 0.0), true);
    check(snap.offset(1.0f, s) && offsetIs(s, -0.01, 0.02, 0.0), "a new press keeps a new wander");
    snap.reset();
    snap.frame(1, 1.0f, headPose(0, 0, 0.0, 0.001, 0.0), true);
    check(snap.state() == AimSnapState::Held && snap.offset(1.0f, s) && offsetIs(s, 0.0, -0.001, 0.0),
          "A RECENTER IN THE MIDDLE OF A PRESS (reset): the next frame keeps the new wander, about 0");
    snap.frame(0, 1.0f, headPose(0, 0, 0.0, 0.0, 0.0), true);
    check(snap.state() == AimSnapState::None, "no eye: dropped");
  }
  {
    AimSnap snap;
    double s[3], o[3];
    snap.frame(2, 0.1f, headPose(0, 0, 0.0, -0.6, 0.0), true);
    check(snap.state() == AimSnapState::Far && !snap.offset(1.0f, s) && aimEyeHeadOffset(eyes, 2, 1.0f, o),
          "0.6 M FROM THE ORIGIN: far, no snap, the eye shift still works");
    snap.frame(2, 1.0f, headPose(0, 0, 0.0, -0.01, 0.0), true);
    check(snap.state() == AimSnapState::Far && !snap.offset(1.0f, s), "...far until the end of that press, even back near the origin");
    snap.frame(2, 0.0f, headPose(0, 0, 0.0, -0.01, 0.0), true);
    snap.frame(2, 0.1f, headPose(0, 0, 0.0, -0.01, 0.0), true);
    check(snap.state() == AimSnapState::Held, "...and the next press is held again");
    AimSnap nan;
    auto bad = headPose(0, 0, 0.0, 0.0, 0.0);
    bad.m[1][3] = std::numeric_limits<float>::quiet_NaN();
    nan.frame(2, 1.0f, bad, true);
    check(nan.state() == AimSnapState::Far && !nan.offset(1.0f, s), "A NaN IN THE TRANSLATION: far, no snap");
    AimSnap late;
    late.frame(2, 0.1f, headPose(0, 0, 0.0, -0.03, 0.0), false);
    check(late.state() == AimSnapState::None && !late.offset(1.0f, s), "AN INVALID POSE IN THE FIRST FRAME: nothing kept yet");
    late.frame(2, 0.2f, headPose(0, 0, 0.0, -0.03, 0.0), true);
    check(late.state() == AimSnapState::Held && late.offset(1.0f, s) && offsetIs(s, 0.0, 0.03, 0.0), "...kept on the next frame with a valid pose");
  }
  {
    std::vector<std::string> lines;
    auto sink = [&](const char* l) { lines.emplace_back(l); };
    AimSnap snap;
    AimEyeLog log;
    snap.frame(2, 0.13f, headPose(0, 0, 0.0, -0.045, 0.0), true);
    log.frame(6, 2, 0.13f, 1, 32.0, snap, 1, 100, sink);
    check(lines.size() == 1 && lines[0] == "aim_eye_on,eye=right,shift_mm=32.0,snap=held,snap_mm=45.0,snap_y_mm=-45.0,follow=0,sequence=1\n",
          "SLID DOWN: the aim_eye_on line carries snap=held, the wander and its vertical part with a minus");
    log.frame(6, 2, 1.0f, 1, 32.0, snap, 2, 111, sink);
    snap.frame(2, 0.0f, headPose(0, 0, 0.0, -0.045, 0.0), true);
    log.frame(6, 2, 0.0f, 1, 32.0, snap, 3, 100 + AimEyeLog::kWindowMs, sink);
    check(lines.size() == 3 && lines[2] == "aim_eye_summary,frames=3,shifted=2,max_mm=32.0,max_snap_mm=45.0,channel=6\n",
          "the summary carries the largest snap of the window");
    lines.clear();
    snap.frame(2, 0.13f, headPose(0, 0, 0.0, -0.6, 0.0), true);
    log.frame(6, 2, 0.13f, 1, 32.0, snap, 4, 30200, sink);
    check(lines.size() == 1 && lines[0] == "aim_eye_on,eye=right,shift_mm=32.0,snap=far,snap_mm=600.0,follow=0,sequence=4\n",
          "TOO FAR: the aim_eye_on line says snap=far and how far");
    AimSnap late;
    AimEyeLog lateLog;
    lines.clear();
    late.frame(2, 0.13f, headPose(0, 0, 0.0, -0.03, 0.0), false);
    lateLog.frame(6, 2, 0.13f, 1, 32.0, late, 1, 100, sink);
    check(lines.size() == 1 && lines[0] == "aim_eye_on,eye=right,shift_mm=32.0,snap=none,follow=0,sequence=1\n", "NO VALID POSE YET: snap=none");
  }
  // ---- the follow (design 14): with the head aim feature the wander is taken from every frame's pose --------------------------------------------------------
  {
    // The head turns about a neck point 10 cm behind and 10 cm below the origin (the head starts on the origin): its position is neck + R_y * (0, 0.1, -0.1).
    auto turned = [](double yawDeg) {
      const double k = 3.14159265358979323846 / 180.0, sy = std::sin(yawDeg * k), cy = std::cos(yawDeg * k);
      return headPose(yawDeg, 0, -0.1 * sy, 0.0, 0.1 - 0.1 * cy);
    };
    auto missOf = [&](const AimSnap& snap, const vr::HmdMatrix34_t& real) {
      auto game = real;
      double s[3], o[3];
      snap.offset(1.0f, s);
      aimEyeHeadOffset(eyes, 2, 1.0f, o);
      applyTrackingOffset(game, s);
      applyAimEyeOffset(game, o);
      double w[3];
      eyeWorld(game, eyes[1], w);   // the right eye of the game's pose: the origin when the head sits on it
      return std::sqrt(w[0] * w[0] + w[1] * w[1] + w[2] * w[2]);
    };
    AimSnap follow, still;
    double worstFollow = 0.0, worstStill = 0.0;
    bool held = true;
    for (int deg : {0, 10, 30, 15, 0, -10, -30, -20, 0}) {
      const auto real = turned(deg);
      follow.frame(2, 1.0f, real, true, true);
      still.frame(2, 1.0f, real, true, false);
      held = held && follow.state() == AimSnapState::Held && follow.follow() && !still.follow();
      worstFollow = std::fmax(worstFollow, missOf(follow, real));
      worstStill = std::fmax(worstStill, missOf(still, real));
    }
    check(held && worstFollow < 1e-5,
          "FOLLOW, THE HEAD TURNS +-30 DEGREES ABOUT A NECK 10 CM BEHIND AND BELOW THE ORIGIN: every frame's snap puts the game's eye back on the origin");
    check(worstStill > 0.04, "WITHOUT FOLLOW the same turn carries the eye off the origin by centimetres (the .34 behaviour, unchanged)");
    // The far limit holds: a press that went far stays far with follow, and a head back near the origin does not bring the snap back; a new press does.
    AimSnap farSnap;
    double s[3];
    farSnap.frame(2, 0.1f, headPose(0, 0, 0.0, -0.6, 0.0), true, true);
    farSnap.frame(2, 1.0f, headPose(0, 0, 0.0, -0.01, 0.0), true, true);
    check(farSnap.state() == AimSnapState::Far && !farSnap.offset(1.0f, s), "FOLLOW, FAR: no snap, and far until the end of that press");
    farSnap.frame(2, 0.0f, headPose(0, 0, 0.0, -0.01, 0.0), true, true);
    farSnap.frame(2, 0.1f, headPose(0, 0, 0.0, -0.01, 0.0), true, true);
    check(farSnap.state() == AimSnapState::Held && farSnap.offset(1.0f, s) && offsetIs(s, 0.0, 0.01, 0.0), "...the next press is held again, from the pose of its frame");
    // A head that wanders out of range while following goes far, as a press does without it.
    AimSnap wander;
    wander.frame(2, 1.0f, headPose(0, 0, 0.0, -0.01, 0.0), true, true);
    wander.frame(2, 1.0f, headPose(0, 0, 0.0, -0.7, 0.0), true, true);
    check(wander.state() == AimSnapState::Far && !wander.offset(1.0f, s), "FOLLOW: a pose past the limit mid-press is far, no snap");
    // A frame with no valid pose keeps the last wander; the follow flag drops with the press.
    AimSnap gap;
    gap.frame(2, 1.0f, headPose(0, 0, 0.0, -0.03, 0.0), true, true);
    gap.frame(2, 1.0f, headPose(0, 0, 0.0, -0.9, 0.0), false, true);
    check(gap.state() == AimSnapState::Held && gap.offset(1.0f, s) && offsetIs(s, 0.0, 0.03, 0.0), "FOLLOW, AN INVALID POSE: the last wander is kept");
    gap.frame(2, 0.0f, headPose(0, 0, 0.0, 0.0, 0.0), true, true);
    check(gap.state() == AimSnapState::None && !gap.follow(), "...and blend 0 drops the snap and the flag");
    // Follow leaves a held press alone when it is switched off: the flag follows the caller, the kept wander stays (as in .34).
    AimSnap off;
    off.frame(2, 1.0f, headPose(0, 0, 0.0, -0.03, 0.0), true, true);
    off.frame(2, 1.0f, headPose(0, 0, 0.0, -0.05, 0.0), true, false);
    check(!off.follow() && off.offset(1.0f, s) && offsetIs(s, 0.0, 0.03, 0.0), "FOLLOW SWITCHED OFF MID-PRESS: the last wander stays, no new one is taken");
    // The line.
    std::vector<std::string> lines;
    auto sink = [&](const char* l) { lines.emplace_back(l); };
    AimSnap logged;
    AimEyeLog log;
    logged.frame(2, 0.13f, headPose(0, 0, 0.0, -0.045, 0.0), true, true);
    log.frame(6, 2, 0.13f, 1, 32.0, logged, 1, 100, sink);
    check(lines.size() == 1 && lines[0] == "aim_eye_on,eye=right,shift_mm=32.0,snap=held,snap_mm=45.0,snap_y_mm=-45.0,follow=1,sequence=1\n",
          "THE LINE: aim_eye_on says follow=1 when the head aim feature acts");
  }
}

// The head aim, runtime side (aim_eye_shift.h; openspec changes on-foot-head-aim-probe and on-foot-head-aim): the turn of the game's head pose about the
// seated vertical and the tilt about the horizontal (their order, and end to end with head_aim_core.h), the version ladder against a d3d11.dll of any
// age (6/7/8), and the runtime's line.
template<class Check> void runHeadAimCases(Check&& check) {
  using namespace aim_fixture;
  const double k = 3.14159265358979323846 / 180.0;
  auto same = [](const vr::HmdMatrix34_t& a, const vr::HmdMatrix34_t& b, double eps) {
    for (int i = 0; i < 3; ++i)
      for (int j = 0; j < 4; ++j)
        if (std::fabs(double(a.m[i][j]) - double(b.m[i][j])) > eps) return false;
    return true;
  };
  {
    const vr::HmdMatrix34_t start = headPose(30, 20, 0.1, 1.2, -0.3);
    vr::HmdMatrix34_t p = start;
    turnAboutVertical(p, 10 * k);
    turnAboutVertical(p, -10 * k);
    check(same(p, start, 1e-5), "TURN: +10 then -10 degrees gives the pose back");
    vr::HmdMatrix34_t yawOnly = headPose(30, 0, 0.0, 1.2, 0.0);
    turnAboutVertical(yawOnly, 10 * k);
    check(same(yawOnly, headPose(40, 0, 0.0, 1.2, 0.0), 1e-5), "a head on the axis turned by 10 degrees: its yaw grows by 10, it does not move");
    vr::HmdMatrix34_t off = headPose(0, 0, 0.0, 1.2, -1.0);
    turnAboutVertical(off, 90 * k);
    check(std::fabs(off.m[0][3] - (-1.0)) < 1e-5 && std::fabs(off.m[1][3] - 1.2) < 1e-5 && std::fabs(off.m[2][3]) < 1e-5,
          "a head 1 m off the axis is carried round it: +90 degrees turns -Z onto -X, the height kept");
    vr::HmdMatrix34_t q = start;
    turnAboutVertical(q, 37 * k);
    bool ortho = true;
    for (int a = 0; a < 3; ++a)
      for (int b = 0; b < 3; ++b) {
        double d = 0;
        for (int r = 0; r < 3; ++r) d += double(q.m[r][a]) * double(q.m[r][b]);
        if (std::fabs(d - (a == b ? 1.0 : 0.0)) > 1e-5) ortho = false;
      }
    check(ortho, "the turned rotation stays orthonormal");
    vr::HmdMatrix34_t z = start;
    turnAboutVertical(z, 0.0);
    turnAboutVertical(z, std::numeric_limits<double>::quiet_NaN());
    check(same(z, start, 0.0), "0 and an angle that is not a number leave the pose alone");
  }
  // ---- the tilt: turnAboutHorizontal (openspec change on-foot-head-aim) -------------------------------------------------------------------------------------
  {
    auto tiltPose = [&](double upDeg, double x, double y, double z) {
      // R_x(+up): a head looking `up` degrees above the horizon.
      const double c = std::cos(upDeg * k), s = std::sin(upDeg * k);
      vr::HmdMatrix34_t m{};
      m.m[0][0] = 1.0f; m.m[1][1] = float(c); m.m[1][2] = float(-s); m.m[2][1] = float(s); m.m[2][2] = float(c);
      m.m[0][3] = float(x); m.m[1][3] = float(y); m.m[2][3] = float(z);
      return m;
    };
    auto identityRotation = [](const vr::HmdMatrix34_t& a, double eps) {
      for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
          if (std::fabs(double(a.m[i][j]) - (i == j ? 1.0 : 0.0)) > eps) return false;
      return true;
    };
    vr::HmdMatrix34_t up = tiltPose(20, 0.0, 0.0, 0.0);
    turnAboutHorizontal(up, -20 * k);
    check(identityRotation(up, 1e-5), "TILT: a head 20 degrees up (R_x(+20)) turned by the game's angle 1 of -20 gives the identity rotation");
    // 45 right and 30 up: pose = R_y(-45) * R_x(+30). The yaw turn by +45 (angle 2), then the tilt by -30 (angle 1) gives the identity.
    const vr::HmdMatrix34_t rx30 = tiltPose(30, 0.0, 0.0, 0.0);
    vr::HmdMatrix34_t both{};
    {
      const double c = std::cos(-45 * k), s = std::sin(-45 * k);
      const double ry[3][3] = {{c, 0, s}, {0, 1, 0}, {-s, 0, c}};
      for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) {
          double v = 0;
          for (int n = 0; n < 3; ++n) v += ry[i][n] * rx30.m[n][j];
          both.m[i][j] = float(v);
        }
    }
    vr::HmdMatrix34_t b = both;
    turnAboutVertical(b, 45 * k);
    turnAboutHorizontal(b, -30 * k);
    check(identityRotation(b, 1e-5), "45 RIGHT AND 30 UP: the turn by angle 2 (+45), then the tilt by angle 1 (-30), gives the identity rotation");
    vr::HmdMatrix34_t wrongOrder = both;
    turnAboutHorizontal(wrongOrder, -30 * k);
    turnAboutVertical(wrongOrder, 45 * k);
    check(!identityRotation(wrongOrder, 1e-3), "the other order does not (the order matters: turn first, then tilt)");
    // End to end: the angles extracted from that pose by head_aim_core.h, through its own outputs, bring the rotation back.
    {
      float p[12];
      for (int r = 0; r < 3; ++r) { for (int c = 0; c < 4; ++c) p[r * 4 + c] = both.m[r][c]; }
      edvr::ha::Angles a;
      check(edvr::ha::anglesFromPose(p, &a) && std::fabs(a.yaw - 45 * k) < 1e-5 && std::fabs(a.pitchUp - 30 * k) < 1e-5,
            "the core reads 45 right and 30 up out of that pose");
      edvr::ha::State st;
      edvr::ha::Inputs in;
      in.key = edvr::ha::Key::Raw; in.nativeOnFoot = in.journalKnown = in.journalOnFoot = in.player = in.channel8 = true; in.focusKnown = true; in.head = p;
      edvr::ha::Output o;
      for (int i = 0; i < 40; ++i) { in.nowMs = 1000 + 28 * i; o = edvr::ha::step(st, in); }
      const float orig = 0.0f;   // the stick holds the tilt at 0
      const float added = edvr::ha::pitchAdded(orig, o.pitchTarget, o.level);
      check(std::fabs(double(o.yawAdded) - 45 * k) < 1e-5 && std::fabs(double(added) + 30 * k) < 1e-5, "the core's yaw added is +45 and its pitch added -30");
      vr::HmdMatrix34_t e = both;
      turnAboutVertical(e, double(o.yawAdded));
      turnAboutHorizontal(e, double(edvr::ha::runtimePitch(edvr::ha::PitchComp::Plus, added)));
      check(identityRotation(e, 1e-4), "END TO END, sign plus: the core's numbers through both turns give the identity rotation");
      vr::HmdMatrix34_t m = both;
      turnAboutVertical(m, double(o.yawAdded));
      turnAboutHorizontal(m, double(edvr::ha::runtimePitch(edvr::ha::PitchComp::Minus, added)));
      check(!identityRotation(m, 1e-2), "sign minus does not (the flight picks the sign)");
      vr::HmdMatrix34_t n = both;
      turnAboutVertical(n, double(o.yawAdded));
      turnAboutHorizontal(n, double(edvr::ha::runtimePitch(edvr::ha::PitchComp::Off, added)));
      check(!identityRotation(n, 1e-2), "sign off leaves the tilt in the pose");
    }
    vr::HmdMatrix34_t t0 = tiltPose(20, 0.3, 1.2, -0.5);
    const vr::HmdMatrix34_t keep = t0;
    turnAboutHorizontal(t0, 17 * k);
    turnAboutHorizontal(t0, -17 * k);
    check(same(t0, keep, 1e-5), "TILT: +17 then -17 degrees gives the pose back");
    vr::HmdMatrix34_t carried = tiltPose(0, 0.0, 0.0, -1.0);
    turnAboutHorizontal(carried, 90 * k);
    check(std::fabs(carried.m[0][3]) < 1e-5 && std::fabs(carried.m[1][3] - 1.0) < 1e-5 && std::fabs(carried.m[2][3]) < 1e-5,
          "a head 1 m ahead (-Z) is carried round the X axis: +90 degrees puts it 1 m up, the x kept");
    vr::HmdMatrix34_t q = headPose(30, 20, 0.1, 1.2, -0.3);
    turnAboutHorizontal(q, 37 * k);
    bool ortho = true;
    for (int a = 0; a < 3; ++a)
      for (int c = 0; c < 3; ++c) {
        double d = 0;
        for (int r = 0; r < 3; ++r) d += double(q.m[r][a]) * double(q.m[r][c]);
        if (std::fabs(d - (a == c ? 1.0 : 0.0)) > 1e-5) ortho = false;
      }
    check(ortho, "the tilted rotation stays orthonormal");
    vr::HmdMatrix34_t z = tiltPose(20, 0.3, 1.2, -0.5);
    const vr::HmdMatrix34_t zKeep = z;
    turnAboutHorizontal(z, 0.0);
    turnAboutHorizontal(z, std::numeric_limits<double>::quiet_NaN());
    turnAboutHorizontal(z, std::numeric_limits<double>::infinity());
    check(same(z, zKeep, 0.0), "TILT: 0, an angle that is not a number and infinity leave the pose alone");
  }
  // ---- the ladder: runtime 8 against a d3d11.dll of either age (6/7/8) -------------------------------------------------------------------------------------
  {
    using Client = edvr::openxr::NativeFrameClient;
    struct P8 {
      uint32_t maxVersion = 8; float yaw = 0.2f, pitch = -0.1f; uint32_t probe = 1, feature = 1;
      HRESULT answer(EdvrNativeFrameOutput& o) {
        if (o.version > maxVersion) return E_INVALIDARG;
        const uint32_t want = o.version == 8 ? uint32_t(sizeof(o)) : o.version == 7 ? EDVR_NATIVE_FRAME_OUTPUT_SIZE_7 : o.version == 6 ? EDVR_NATIVE_FRAME_OUTPUT_SIZE_6 : 0u;
        if (o.size != want) return E_INVALIDARG;
        o.aimEye = 2; o.aimEyeBlend = 1.0f; o.aimOnFoot = 1;
        if (o.version >= 7) { o.headAimYawRad = yaw; o.headAimProbe = probe; }
        if (o.version >= 8) { o.headAimPitchRad = pitch; o.headAimFeature = feature; }
        return S_OK;
      }
    };
    auto ladder = [&](P8& p, uint32_t& version, EdvrNativeFrameOutput& out) {
      return Client::beginLadder([&](EdvrNativeFrameOutput& o) { return p.answer(o); }, version, out);
    };
    P8 p; uint32_t v = 8; EdvrNativeFrameOutput out{};
    check(ladder(p, v, out) == S_OK && out.version == 8 && out.headAimYawRad == 0.2f && out.headAimProbe == 1 && out.headAimPitchRad == -0.1f &&
              out.headAimFeature == 1 && out.aimEye == 2,
          "RUNTIME 8, D3D11 8: the turn, the tilt, both flags and the aim arrive");
    P8 d7; d7.maxVersion = 7; v = 8;
    check(ladder(d7, v, out) == S_OK && out.version == 7 && v == 7 && out.headAimYawRad == 0.2f && out.headAimProbe == 1 && out.headAimPitchRad == 0.0f &&
              out.headAimFeature == 0,
          "RUNTIME 8, OLD D3D11 (7): one step down, the probe's turn still arrives, the tilt reads 0 and the feature flag 0");
    P8 d6; d6.maxVersion = 6; v = 8;
    check(ladder(d6, v, out) == S_OK && out.version == 6 && v == 6 && out.headAimYawRad == 0.0f && out.headAimProbe == 0 && out.headAimPitchRad == 0.0f &&
              out.headAimFeature == 0 && out.aimEye == 2,
          "RUNTIME 8, D3D11 6: two steps down, the aim still arrives, no turn, no tilt");
    P8 bad; bad.pitch = std::numeric_limits<float>::infinity(); bad.feature = 9; v = 8;
    check(ladder(bad, v, out) == S_OK && out.headAimPitchRad == 0.0f && out.headAimFeature == 0 && out.headAimYawRad == 0.2f,
          "a tilt that is not finite, or a flag above 1, reads no tilt (and leaves the turn)");
    P8 nan; nan.pitch = std::numeric_limits<float>::quiet_NaN(); v = 8;
    check(ladder(nan, v, out) == S_OK && out.headAimPitchRad == 0.0f, "a NaN tilt reads 0");
    P8 badYaw; badYaw.yaw = std::numeric_limits<float>::infinity(); badYaw.probe = 9; v = 8;
    check(ladder(badYaw, v, out) == S_OK && out.headAimYawRad == 0.0f && out.headAimProbe == 0, "a turn that is not finite, or a flag above 1, reads no turn");
  }
  // ---- the runtime's line ---------------------------------------------------------------------------------------------------------------------------------
  {
    std::vector<std::string> lines;
    auto sink = [&](const char* line) { lines.emplace_back(line); };
    HeadAimLog log;
    log.frame(8, 0, 0, 0.0, 0.0, 1000, sink);
    log.frame(8, 0, 0, 0.0, 0.0, 31000, sink);
    check(lines.empty(), "probe and feature off, channel 8: no line");
    log.frame(8, 1, 0, 10 * k, 0.0, 32000, sink);
    log.frame(8, 1, 0, -5 * k, 0.0, 33000, sink);
    log.frame(8, 1, 0, 0.0, 0.0, 61000, sink);
    check(lines.size() == 1 && lines[0] == "head_aim_runtime,frames=3,probe=3,turned=2,max_deg=10.0,channel=8,pitched=0,max_pitch_deg=0.0\n",
          "THE PROBE ACTING: frames, probe frames, turned frames and the largest turn of the window, no tilt");
    HeadAimLog feature;
    lines.clear();
    feature.frame(8, 0, 1, 10 * k, -30 * k, 1000, sink);
    feature.frame(8, 0, 1, -20 * k, 12 * k, 2000, sink);
    feature.frame(8, 0, 1, 0.0, 0.0, 31000, sink);
    check(lines.size() == 1 && lines[0] == "head_aim_runtime,frames=3,probe=0,turned=2,max_deg=20.0,channel=8,pitched=2,max_pitch_deg=30.0\n",
          "THE FEATURE ACTING: turned and pitched frames and the largest of each, written without the probe");
    HeadAimLog next;
    lines.clear();
    next.frame(8, 0, 1, 1 * k, 1 * k, 0, sink);
    next.frame(8, 0, 0, 0.0, 0.0, 30000, sink);
    next.frame(8, 0, 0, 0.0, 0.0, 60000, sink);
    check(lines.size() == 1 && lines.back().find("pitched=1,max_pitch_deg=1.0") != std::string::npos, "the window resets after a line (the second window is quiet and writes nothing)");
    HeadAimLog older;
    lines.clear();
    older.frame(7, 0, 0, 0.0, 0.0, 0, sink);
    older.frame(7, 0, 0, 0.0, 0.0, 30000, sink);
    check(lines.size() == 1 && lines[0] == "head_aim_runtime,frames=2,probe=0,turned=0,max_deg=0.0,channel=7,pitched=0,max_pitch_deg=0.0\n",
          "AN OLDER D3D11.DLL (7): the line comes anyway, saying the tilt never reached the runtime");
    HeadAimLog oldest;
    lines.clear();
    oldest.frame(6, 0, 0, 0.0, 0.0, 0, sink);
    oldest.frame(6, 0, 0, 0.0, 0.0, 30000, sink);
    check(lines.size() == 1 && lines[0] == "head_aim_runtime,frames=2,probe=0,turned=0,max_deg=0.0,channel=6,pitched=0,max_pitch_deg=0.0\n",
          "AN OLDER D3D11.DLL (6): the line comes anyway");
  }
}
}  // namespace edvr::openxr::test
