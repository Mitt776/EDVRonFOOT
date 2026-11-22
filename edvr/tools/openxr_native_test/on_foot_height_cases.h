#pragma once
// The on-foot camera height, runtime side (on_foot_lift.h; openspec change on-foot-height): the version 9 ladder against a d3d11.dll of either age, the
// lift straight up at any head rotation and after the whole pose chain, the share given way to LT, and the runtime's line.
#include "../../src/openxr/on_foot_lift.h"
#include "../../src/openxr/native_frame_client.h"
#include "aim_eye_cases.h"
#include <cmath>
#include <limits>
#include <string>
#include <vector>

namespace edvr::openxr::test {

template<class Check> void runOnFootHeightCases(Check&& check) {
  using namespace aim_fixture;
  // ---- the ladder: runtime 9 against a d3d11.dll of either age (8/9) --------------------------------------------------------------------------------------
  {
    using Client = edvr::openxr::NativeFrameClient;
    struct P9 {
      uint32_t maxVersion = 9; float height = 0.1f; bool scribble = false;
      HRESULT answer(EdvrNativeFrameOutput& o) {
        if (o.version > maxVersion || o.version < EDVR_NATIVE_FRAME_VERSION_1) return E_INVALIDARG;
        const uint32_t want = o.version == 9 ? uint32_t(sizeof(o)) : o.version == 8 ? EDVR_NATIVE_FRAME_OUTPUT_SIZE_8 : o.version == 7 ? EDVR_NATIVE_FRAME_OUTPUT_SIZE_7 : 0u;
        if (o.size != want) return E_INVALIDARG;
        o.aimEye = 2; o.aimEyeBlend = 0.5f; o.aimOnFoot = 1;
        o.headAimYawRad = 0.2f; o.headAimProbe = 1;
        if (o.version >= 8) { o.headAimPitchRad = -0.1f; o.headAimFeature = 1; }
        if (o.version >= 9 || scribble) o.onFootHeightM = height;
        return S_OK;
      }
    };
    auto ladder = [&](P9& p, uint32_t& version, EdvrNativeFrameOutput& out) {
      return Client::beginLadder([&](EdvrNativeFrameOutput& o) { return p.answer(o); }, version, out);
    };
    P9 p; uint32_t v = 9; EdvrNativeFrameOutput out{};
    check(ladder(p, v, out) == S_OK && out.version == 9 && v == 9 && out.onFootHeightM == 0.1f && out.headAimPitchRad == -0.1f && out.aimEye == 2,
          "RUNTIME 9, D3D11 9: the height arrives with the tilt and the aim");
    P9 d8; d8.maxVersion = 8; v = 9;
    check(ladder(d8, v, out) == S_OK && out.version == 8 && v == 8 && out.onFootHeightM == 0.0f && out.headAimPitchRad == -0.1f && out.aimEye == 2,
          "RUNTIME 9, D3D11 8 (0.1.7): one step down, the height reads 0, the tilt and the aim still arrive");
    P9 liar; liar.maxVersion = 8; liar.scribble = true; v = 9;
    check(ladder(liar, v, out) == S_OK && out.version == 8 && out.onFootHeightM == 0.0f, "a version 8 provider that writes the height slot anyway is not believed");
    P9 nan; nan.height = std::numeric_limits<float>::quiet_NaN(); v = 9;
    check(ladder(nan, v, out) == S_OK && out.onFootHeightM == 0.0f, "a NaN height reads 0");
    P9 wide; wide.height = 1.0f; v = 9;
    check(ladder(wide, v, out) == S_OK && out.onFootHeightM == 0.0f, "a height of 1 m (past 0.25 m) reads 0");
    P9 low; low.height = -0.1f; v = 9;
    check(ladder(low, v, out) == S_OK && out.onFootHeightM == -0.1f, "-10 cm passes");
    check(Client::ask(9).onFootHeightM == 0.0f, "a fresh ask carries height 0 until a provider says otherwise");
  }
  // ---- the share given way to LT --------------------------------------------------------------------------------------------------------------------------
  {
    check(std::fabs(onFootLiftM(0.1f, false, 0.0f) - 0.1) < 1e-7, "H = 0.1, NO LT: the whole 0.1 m");
    check(onFootLiftM(0.1f, true, 1.0f) == 0.0, "FULL AIM (blend 1, the eye's shift acting): 0, the sights on the eye's line as at height 0");
    check(std::fabs(onFootLiftM(0.1f, true, 0.5f) - 0.05) < 1e-7, "half a blend: half the height, 0.05 m");
    check(std::fabs(onFootLiftM(0.1f, false, 1.0f) - 0.1) < 1e-7, "AIMING EYE OFF (no shift acts) with LT held: the whole 0.1 m");
    check(onFootLiftM(0.1f, true, 7.0f) == 0.0 && std::fabs(onFootLiftM(0.1f, true, -1.0f) - 0.1) < 1e-7, "a blend past 1 or below 0 is clamped");
    check(std::fabs(onFootLiftM(0.1f, true, std::numeric_limits<float>::quiet_NaN()) - 0.1) < 1e-7, "a NaN blend gives nothing way");
    check(onFootLiftM(std::numeric_limits<float>::infinity(), false, 0.0f) == 0.0 && onFootLiftM(0.0f, false, 0.0f) == 0.0, "an infinite or zero height: no lift");
    check(std::fabs(onFootLiftM(-0.05f, false, 0.0f) + 0.05) < 1e-7, "a negative height lowers");
  }
  // ---- straight up at any head rotation, after the whole chain (3.1: the three poses agree) ---------------------------------------------------------------
  {
    const double k = 3.14159265358979323846 / 180.0;
    const vr::HmdMatrix34_t eyes[2] = {eye(-0.032, 0, 0), eye(0.032, 0, 0)};
    const double turns[][2] = {{0, 0}, {90, 0}, {0, 30}, {-45, 20}};
    for (const auto& t : turns) {
      // The chain a frame's render pose, its game pose and a "now" answer all go through: snap, eye shift, turn, tilt, then the lift.
      auto chain = [&](double liftM) {
        auto pose = headPose(t[0], t[1], 0.1, 0.05, -0.03);
        double eyeOffset[3];
        aimEyeHeadOffset(eyes, 2, 0.3f, eyeOffset);
        const double snap[3] = {-0.003, -0.015, 0.009};
        applyTrackingOffset(pose, snap);
        applyAimEyeOffset(pose, eyeOffset);
        turnAboutVertical(pose, 10 * k);
        turnAboutHorizontal(pose, -5 * k);
        applyOnFootLift(pose, liftM);
        return pose;
      };
      const auto base = chain(0.0), lifted = chain(0.1);
      bool rotationSame = true;
      for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j) rotationSame = rotationSame && lifted.m[i][j] == base.m[i][j];
      check(rotationSame && std::fabs(lifted.m[0][3] - base.m[0][3]) < 1e-6 && std::fabs(lifted.m[1][3] - base.m[1][3] - 0.1) < 1e-6 &&
                std::fabs(lifted.m[2][3] - base.m[2][3]) < 1e-6,
            "AT YAW 0/90/-45 AND ROLL 0/30/20, after the snap, the eye shift, the turn and the tilt: the lift is exactly 0.1 m straight up, the rotation untouched");
      double eyeBase[3], eyeLifted[3];
      eyeWorld(base, eyes[0], eyeBase);
      eyeWorld(lifted, eyes[0], eyeLifted);
      check(std::fabs(eyeLifted[1] - eyeBase[1] - 0.1) < 1e-6 && std::fabs(eyeLifted[0] - eyeBase[0]) < 1e-6 && std::fabs(eyeLifted[2] - eyeBase[2]) < 1e-6,
            "...and so is the game's eye: 0.1 m up and not a millimetre forward or sideways");
    }
    auto z = headPose(30, 10, 0.1, 0.2, 0.3);
    const auto zKeep = z;
    applyOnFootLift(z, 0.0);
    applyOnFootLift(z, std::numeric_limits<double>::quiet_NaN());
    bool same = true;
    for (int i = 0; i < 3; ++i) for (int j = 0; j < 4; ++j) same = same && z.m[i][j] == zKeep.m[i][j];
    check(same, "a lift of 0 or NaN leaves the pose alone");
  }
  // ---- the runtime's line ---------------------------------------------------------------------------------------------------------------------------------
  {
    std::vector<std::string> lines;
    auto sink = [&](const char* l) { lines.emplace_back(l); };
    OnFootHeightLog log;
    uint64_t ms = 1000;
    for (int i = 0; i < 100; ++i) log.frame(9, 0.0f, ms += 11, sink);
    check(lines.empty(), "height 0 all along (the key never set): no line");
    for (int i = 0; i <= 10; ++i) log.frame(9, 0.01f * float(i), ms += 11, sink);
    for (int i = 0; i < 100; ++i) log.frame(9, 0.1f, ms += 11, sink);
    check(lines.size() == 1 && lines[0] == "on_foot_height,mm=100,channel=9\n", "A RAMP 0 -> 100 MM THAT THEN HOLDS: one line, mm=100, none per ramp frame");
    for (int i = 10; i >= 0; --i) log.frame(9, 0.01f * float(i), ms += 11, sink);
    for (int i = 0; i < 30; ++i) log.frame(9, 0.0f, ms += 11, sink);
    check(lines.size() == 1, "...back to 0 but not yet still for 0.5 s: nothing yet");
    for (int i = 0; i < 30; ++i) log.frame(9, 0.0f, ms += 11, sink);
    check(lines.size() == 2 && lines[1] == "on_foot_height,mm=0,channel=9\n", "BACK IN THE SHIP: one mm=0 line");
    for (int i = 0; i < 100; ++i) log.frame(9, 0.0f, ms += 11, sink);
    check(lines.size() == 2, "...and nothing more while it stays 0");
    for (int i = 0; i < 60; ++i) log.frame(9, 0.07f, ms += 11, sink);
    check(lines.size() == 3 && lines[2] == "on_foot_height,mm=70,channel=9\n", "+7 CM: mm=70 and channel 9");
  }
}

}  // namespace edvr::openxr::test
