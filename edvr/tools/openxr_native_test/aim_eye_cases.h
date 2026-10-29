#pragma once
// The aiming eye, runtime side (aim_eye_shift.h; openspec change on-foot-ads-dominant-eye): the offset the game's head pose is moved by, that the game's
// aiming eye then stands where the middle of the head was at any head rotation, the version ladder against a d3d11.dll of either age, and the log lines.
#include "../../src/openxr/aim_eye_shift.h"
#include "../../src/openxr/native_frame_client.h"
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
    const uint32_t want = o.version == 6 ? uint32_t(sizeof(o)) : o.version == 5 ? EDVR_NATIVE_FRAME_OUTPUT_SIZE_5 : o.version == 4 ? EDVR_NATIVE_FRAME_OUTPUT_SIZE_4
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
    AimEyeLog log;
    uint64_t ms = 1000, seq = 1;
    for (int i = 0; i < 10; ++i) log.frame(6, 2, 0.0f, 1, 32.0, seq++, ms += 11, sink);
    check(lines.empty(), "on foot, LT not held: no line yet");
    log.frame(6, 2, 0.13f, 1, 32.0, seq++, ms += 11, sink);
    check(lines.size() == 1 && lines[0] == "aim_eye_on,eye=right,shift_mm=32.0,sequence=11\n", "THE BLEND LEAVES 0: one aim_eye_on line, the eye and the full shift");
    for (int i = 0; i < 20; ++i) log.frame(6, 2, 1.0f, 1, 32.0, seq++, ms += 11, sink);
    check(lines.size() == 1, "...and nothing while it holds");
    log.frame(6, 2, 0.0f, 1, 32.0, seq++, ms += 11, sink);
    check(lines.size() == 2 && lines[1] == "aim_eye_off,sequence=32\n", "BACK TO 0: one aim_eye_off line");
    log.frame(6, 1, 0.5f, 1, 31.0, seq++, ms += 11, sink);
    check(count("aim_eye_on,eye=left,shift_mm=31.0") == 1, "a left-eye press names the left eye");
    log.frame(6, 1, 0.0f, 1, 31.0, seq++, ms += 11, sink);
    ms += AimEyeLog::kWindowMs;
    log.frame(6, 2, 0.0f, 1, 32.0, seq++, ms, sink);
    check(count("aim_eye_summary,frames=35,shifted=22,max_mm=32.0,channel=6") == 1, "30 S ON FOOT: a summary of all frames, the shifted ones and the largest shift");
    lines.clear();
    ms += AimEyeLog::kWindowMs;
    log.frame(6, 2, 0.0f, 0, 32.0, seq++, ms, sink);
    check(lines.empty(), "30 s in a ship (channel 6, not on foot): no summary");
    AimEyeLog oldPair;
    lines.clear(); ms = 5000;
    oldPair.frame(5, 0, 0.0f, 0, 0.0, 1, ms, sink);
    oldPair.frame(5, 0, 0.0f, 0, 0.0, 2, ms + AimEyeLog::kWindowMs, sink);
    check(lines.size() == 1 && lines[0] == "aim_eye_summary,frames=2,shifted=0,max_mm=0.0,channel=5\n",
          "AN OLDER D3D11.DLL (channel 5): the summary is written anyway, channel=5 and shifted=0 -- 'the fix never reached the runtime', not 'LT was never held'");
    AimEyeLog noGeometry;
    lines.clear();
    noGeometry.frame(6, 2, 1.0f, 1, 0.0, 1, 100, sink);
    check(lines.empty() && !noGeometry.on(), "a blend with no usable eye offset (0 mm) is not a shift and writes no aim_eye_on");
  }
}
}  // namespace edvr::openxr::test
