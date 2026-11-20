#pragma once
// Elite's "now" head pose, answered one display period after the drawn frame's display time (src/openxr/head_pose_time.h;
// docs\terrain-culling.md): which calls it answers (a return address inside the game's image and a prediction under 5 ms either way, at its
// boundary and on both signs), which it leaves alone, the instant it forms (display time + period, with the freshness test on the cached frame),
// and the log lines that say whom it is answering, and when the cap is hit.
#include "../../src/openxr/head_pose_time.h"
#include "../../src/openxr/space_pose.h"

#include <cmath>
#include <limits>
#include <string>
#include <vector>

namespace head_pose_time_cases {
using namespace edvr::openxr;

// The on-foot marker pose probe (on-foot-marker-pose): the measured step, its history, the answer with the step, and the probe's log lines.
template<class Check>
void runMarkerPoseCases(Check&& check) {
  constexpr int64_t P=11111111;   // 90 Hz
  {
    int64_t ones[16],twos[16],mixed[16];
    for(int i=0;i<16;++i){ones[i]=P;twos[i]=2*P;mixed[i]=(i%2)?2*P:P;}
    check(frameStepNs(ones,16,P)==P,"STEP: an even one period a frame is one period");
    check(frameStepNs(twos,16,P)==2*P,"...an even two periods (45 fps at 90 Hz) is two");
    check(frameStepNs(mixed,16,P)==2*P&&frameStepNs(mixed,15,P)==P,"...a 1/2 mix is the median: the upper middle of an even count");
    int64_t spike[16];for(int i=0;i<16;++i)spike[i]=2*P;spike[7]=40*P;spike[3]=1;
    check(frameStepNs(spike,16,P)==2*P,"...a single outlier either way does not move it");
    check(frameStepNs(nullptr,0,P)==P&&frameStepNs(ones,0,P)==P,"...no history is one period (the 0.1.5 answer)");
    check(frameStepNs(ones,16,0)==0&&frameStepNs(ones,16,-5)==0,"...no period is 0: no instant is formed, the call falls back as before");
    int64_t slow[16];for(int i=0;i<16;++i)slow[i]=7*P;
    check(frameStepNs(slow,16,P)==4*P,"CEILING: at most four periods (44.4 ms at 90 Hz)");
    constexpr int64_t P60=16666667;int64_t slow60[16];for(int i=0;i<16;++i)slow60[i]=4*P60;
    check(frameStepNs(slow60,16,P60)==50000000,"...and at most 50 ms (four 60 Hz periods would be 66.7)");
    int64_t tiny[16];for(int i=0;i<16;++i)tiny[i]=P/3;
    check(frameStepNs(tiny,16,P)==P,"FLOOR: at least one period");
    const int64_t huge=std::numeric_limits<int64_t>::max()/2;int64_t big[16];for(int i=0;i<16;++i)big[i]=huge;
    check(frameStepNs(big,16,huge)==huge,"...a period past the ceiling is its own floor, without overflow");
  }
  {
    FrameStepHistory h;
    check(h.step(P)==P&&h.count()==0,"HISTORY: empty is one period");
    h.note(1000);
    check(h.count()==0,"...the first time is only a base");
    int64_t t=1000;for(int i=0;i<20;++i){t+=2*P;h.note(t);}
    check(h.count()==16&&h.step(P)==2*P,"...twenty frames two periods apart keep sixteen differences and read two periods");
    h.note(t);h.note(0);h.note(-5);
    check(h.count()==16&&h.step(P)==2*P,"...a time that does not move forward, or is not positive, adds nothing");
    for(int i=0;i<16;++i){t+=P;h.note(t);}
    check(h.step(P)==P,"...the window slides: sixteen one-period frames later it reads one period");
    h.reset();
    check(h.count()==0&&h.step(P)==P,"...reset() empties it");
  }
  {
    const auto add=[](int64_t shown,int64_t period,int64_t& out){return nextPredictionTime(shown,period,out);};
    const auto never=[](int64_t,int64_t){return false;};
    const int64_t D=1000000000;int64_t out=0;
    check(answerTime(D,P,D-5000000,add,never,&out)&&out==D+P,"ANSWER, next: display + one period (the 0.1.5 answer)");
    check(answerTime(D,2*P,D-5000000,add,never,&out)&&out==D+2*P,"...frame at two periods: display + 2P");
    check(answerTime(D,2*P,D+2*P,add,never,&out)&&!answerTime(D,P,D+2*P,add,never,&out),
          "...and the freshness tolerance is the step: a frame 2P old is still good at step 2P, not at step P");
  }
  {
    std::vector<std::string> lines;
    const auto sink=[&](const char* s){lines.emplace_back(s);};
    MarkerPoseLog log;
    check(!log.frame(true,9,0,P,1000,sink)&&lines.empty()&&!log.active(),"LOG: next from the start writes nothing");
    check(log.frame(true,9,1,2*P,2000,sink)&&log.active()&&lines.size()==1&&lines[0]=="marker pose: frame (on foot, measured frame step)",
          "...a change to frame writes `marker pose: frame (on foot, measured frame step)` and says it changed");
    log.noteAnswered();log.noteAnswered();log.noteAnswered();
    check(!log.frame(true,9,1,2*P,31999,sink)&&lines.size()==1,"...nothing more before 30 s");
    log.frame(true,9,1,3*P,32000,sink);
    check(lines.size()==2&&lines[1]=="marker pose: frame, step p50 22.22 ms max 33.33 ms, answered 3","...at 30 s: the step median and max in ms and the answers");
    check(log.frame(true,9,2,P,33000,sink)&&!log.active()&&lines.size()==4&&lines[2]=="marker pose: frame, step n/a, answered 0"&&lines[3]=="marker pose: next (not on foot)",
          "...leaving foot writes the partial window (here empty: answered 0, which is how a probe that never acted reads) and `marker pose: next (not on foot)`");
    log.noteAnswered();
    log.frame(true,9,1,2*P,34000,sink);log.frame(true,9,0,2*P,35000,sink);
    check(lines.size()==7&&lines[5]=="marker pose: frame, step p50 22.22 ms max 22.22 ms, answered 0"&&lines[6]=="marker pose: next (key next)",
          "...an answer noted while next is not counted, and the key back to next says `key next`");
    log.frame(true,9,1,2*P,36000,sink);log.frame(true,8,1,2*P,37000,sink);log.frame(true,9,1,2*P,38000,sink);log.frame(false,9,1,2*P,39000,sink);
    check(lines.size()==13&&lines[9]=="marker pose: next (d3d11.dll without the field)"&&lines[12]=="marker pose: next (no d3d11 half)",
          "...an older d3d11.dll or none reads next, and says why");
  }
}

template<class Check>
void runHeadPoseTimeCases(Check&& check) {
  runMarkerPoseCases(check);
  // ---- what "now" is ---------------------------------------------------------------------------------------------------------------------
  {
    check(kNowPredictionLimitSeconds==0.005f,"the bound is 5 ms");
    check(isNowRequest(0.0f)&&isNowRequest(-0.0f)&&isNowRequest(0.001f)&&isNowRequest(0.0049f)&&isNowRequest(0.00499f)&&isNowRequest(0.004999f),
          "a prediction under 5 ms is a request for \"now\"");
    check(!isNowRequest(0.005f)&&!isNowRequest(0.0051f)&&!isNowRequest(0.011f)&&!isNowRequest(0.0111111f)&&!isNowRequest(0.1f)&&!isNowRequest(1.0f),
          "...exactly 5 ms is already a prediction, and so is anything over it (a frame period is 11.1 ms)");
    check(isNowRequest(-0.001f)&&isNowRequest(-0.0049f)&&isNowRequest(-0.004999f)&&!isNowRequest(-0.005f)&&!isNowRequest(-0.0051f)&&!isNowRequest(-0.011f)&&!isNowRequest(-0.25f),
          "...on both signs: -4.9 ms is \"now\", -5 ms and the quarter second Elite's older callers pass are not");
    check(!isNowRequest(std::numeric_limits<float>::quiet_NaN())&&!isNowRequest(std::numeric_limits<float>::infinity())&&!isNowRequest(-std::numeric_limits<float>::infinity()),
          "a NaN and an infinity are not \"now\"");
  }
  // ---- whose call it is --------------------------------------------------------------------------------------------------------------------
  {
    check(answeredAtDisplayTime(0x4E3881,0.0f)&&answeredAtDisplayTime(0,0.0f)&&answeredAtDisplayTime(0x6000000,0.003f)&&answeredAtDisplayTime(kFrameUnknown-1,-0.004f),
          "a return address inside the image with a prediction of about 0 is answered one period past the display time: the build's own RVA is not asked");
    check(!answeredAtDisplayTime(0x4E3881,0.005f)&&!answeredAtDisplayTime(0x4E3881,-0.005f)&&!answeredAtDisplayTime(0x4E3881,0.25f)&&!answeredAtDisplayTime(0x4E3881,-0.25f)&&
              !answeredAtDisplayTime(0x4E3881,0.011f),
          "...a real prediction from inside the image is not (a caller that wants the future)");
    check(!answeredAtDisplayTime(kFrameOutside,0.0f)&&!answeredAtDisplayTime(kFrameUnknown,0.0f)&&!answeredAtDisplayTime(kFrameOutside,0.25f),
          "...a caller outside the image, or one whose address was not captured, never is");
    // The image: where an address is inside it.
    const ExeModule module{0x140000000ull,0x1000,0x1234,0x1000};
    check(frameRva(module,0x140000000ull)==0&&frameRva(module,0x140000FFFull)==0xFFF&&frameRva(module,0x140001000ull)==kFrameOutside&&frameRva(module,0x13FFFFFFFull)==kFrameOutside&&
              frameRva(module,0)==kFrameUnknown&&frameRva(ExeModule{},0x140000000ull)==kFrameOutside,
          "an address is inside the image from its base to the last byte of its size; one past either end, and any address when the image is unknown, is outside; null is not captured");
    check(answeredAtDisplayTime(frameRva(module,0x140000800ull),0.0f)&&!answeredAtDisplayTime(frameRva(module,0x140001000ull),0.0f)&&!answeredAtDisplayTime(frameRva(module,0x13FFFFFFFull),0.0f),
          "...so the filter holds at the image's edges");
  }
  // ---- the instant ---------------------------------------------------------------------------------------------------------------------------
  {
    int64_t t=-7;
    check(displayTimeTarget(5000000000,&t)&&t==5000000000&&displayTimeTarget(1,&t)&&t==1&&displayTimeTarget(INT64_MAX,&t)&&t==INT64_MAX,"the instant is the latest frame's display time");
    t=-7;
    check(!displayTimeTarget(0,&t)&&!displayTimeTarget(-1,&t)&&!displayTimeTarget(INT64_MIN,&t)&&t==-7,"with no frame yet (a display time that is not positive) there is none, and the output is left alone");
  }
  // ---- the instant: display time + one period ------------------------------------------------------------------------------------------------
  {
    constexpr int64_t period = 11111111, display = 5000000000, lead = 42000000;
    const auto add = [](int64_t shown, int64_t p, int64_t& out) { return nextPredictionTime(shown, p, out); };
    const auto never = [](int64_t, int64_t) { return false; };
    int64_t at = -7;
    check(answerTime(display, period, display - lead, add, never, &at) && at == display + period,
          "the instant is the latest frame's display time plus that frame's period (flight 3's now was 42 ms before the display time)");
    at = -7;
    check(answerTime(display, 13888889, display - lead, add, never, &at) && at == display + 13888889 && answerTime(display, 1, display, add, never, &at) && at == display + 1,
          "...whatever the period (72 Hz, 1 ns)");
    // The freshness test is on the CACHED frame: one period behind now is still good (the target is then now), a nanosecond more is not.
    at = -7;
    check(answerTime(display, period, display + period, add, never, &at) && at == display + period,
          "a cached frame exactly one period behind now still answers (judged on the frame's own display time, not on the target a period ahead of it)");
    at = -7;
    check(!answerTime(display, period, display + period + 1, add, never, &at) && !answerTime(display, period, display + 3 * period, add, never, &at) && at == -7,
          "...one nanosecond more, or three periods, it does not: no target, and the output is left alone");
    check(answerTime(display, period, display + period, add, never, &at) && !answerTime(display, period, display + period + 1, add, never, &at),
          "(the boundary is exactly one period, inclusive)");
    check(answerTime(display, 10000000, display + 10000000, add, never, &at) && at == display + 10000000 && !answerTime(display, 10000000, display + 10000001, add, never, &at),
          "(and it moves with the frame's own period)");
    // No frame, no period, overflow.
    at = -7;
    check(!answerTime(0, period, 1, add, never, &at) && !answerTime(-5, period, 1, add, never, &at) && !answerTime(INT64_MIN, period, 1, add, never, &at) && at == -7,
          "no display time (not positive): no target");
    check(!answerTime(0, period, 0, add, never, &at) && !answerTime(-5, period, -10, add, never, &at) && !answerTime(-1, period, INT64_MIN, add, never, &at) && at == -7,
          "...even when now is not ahead of it (a display time that is not positive is no display time, whatever the clock says)");
    check(!answerTime(display, 0, display, add, never, &at) && !answerTime(display, -1, display, add, never, &at) && !answerTime(display, INT64_MIN, display, add, never, &at) && at == -7,
          "a frame that reports no period (zero or negative) has no target: the call is located at now + prediction, a counted fallback");
    check(!answerTime(INT64_MAX - 5, period, INT64_MAX - 5, add, never, &at) && at == -7 && answerTime(INT64_MAX - period, period, INT64_MAX - period, add, never, &at) && at == INT64_MAX,
          "a display time so late that one more period overflows has no target and does not wrap; one that just fits has");
    // A reference-space change between the display time and the target: a pose there would be in another origin.
    const auto crossing = [&](int64_t from, int64_t to) { return from < display + 5000000 && display + 5000000 <= to; };
    at = -7;
    check(!answerTime(display, period, display - lead, add, crossing, &at) && at == -7, "a reference change between the display time and the target: no target");
    int64_t seenFrom = 0, seenTo = 0;
    check(answerTime(display, period, display - lead, add, [&](int64_t from, int64_t to) { seenFrom = from; seenTo = to; return false; }, &at) && seenFrom == display && seenTo == display + period,
          "...asked about exactly the span from the display time to the target");
    int64_t addCalls = 0;
    answerTime(display, period, display + 2 * period, [&](int64_t, int64_t, int64_t&) { ++addCalls; return true; }, never, &at);
    answerTime(0, period, 1, [&](int64_t, int64_t, int64_t&) { ++addCalls; return true; }, never, &at);
    check(addCalls == 0, "(a stale or absent frame never gets as far as adding a period)");
  }
  // ---- how long a display time stays good ----------------------------------------------------------------------------------------------------
  {
    constexpr int64_t period = 11111111, display = 5000000000;
    check(kPoseFrameMaxToleranceNs == 50000000, "the cap on the tolerance is 50 ms");
    check(poseFrameTolerance(period) == period && poseFrameTolerance(1) == 1 && poseFrameTolerance(kPoseFrameMaxToleranceNs) == kPoseFrameMaxToleranceNs,
          "the tolerance is one display period, as the frame reports it");
    check(poseFrameTolerance(kPoseFrameMaxToleranceNs + 1) == kPoseFrameMaxToleranceNs && poseFrameTolerance(INT64_MAX) == kPoseFrameMaxToleranceNs,
          "...capped at 50 ms, so a period that is not a display period cannot stretch it");
    check(poseFrameTolerance(0) == 0 && poseFrameTolerance(-1) == 0 && poseFrameTolerance(INT64_MIN) == 0, "...and no period (zero, negative) is no tolerance");
    check(displayTimeFresh(display, period, display - 42000000) && displayTimeFresh(display, period, display - 1) && displayTimeFresh(display, period, display),
          "a display time ahead of now, or exactly now, is good (the real case: now is about 42 ms before it)");
    check(displayTimeFresh(display, period, display + 1) && displayTimeFresh(display, period, display + period),
          "...and stays good until it is one display period behind now, inclusive");
    check(!displayTimeFresh(display, period, display + period + 1) && !displayTimeFresh(display, period, display + 2 * period) &&
              !displayTimeFresh(display, period, display + 5000000000),
          "...one nanosecond past that, or a second later, it is not");
    check(displayTimeFresh(display, 0, display) && !displayTimeFresh(display, 0, display + 1) && !displayTimeFresh(display, -5, display + 1),
          "with no period the display time must not be behind now at all");
    check(displayTimeFresh(display, INT64_MAX, display + kPoseFrameMaxToleranceNs) && !displayTimeFresh(display, INT64_MAX, display + kPoseFrameMaxToleranceNs + 1),
          "a huge period is held to the 50 ms cap");
    check(!displayTimeFresh(0, period, 1) && !displayTimeFresh(-7, period, 1) && displayTimeFresh(1, period, 1 + period) && !displayTimeFresh(1, period, 2 + period),
          "a display time that is not positive is never judged good once now is ahead of it; the smallest positive one follows the same rule as any other");
    check(displayTimeFresh(INT64_MAX, period, INT64_MAX) && displayTimeFresh(INT64_MAX, period, INT64_MAX - 1) && !displayTimeFresh(INT64_MAX - 3 * period, period, INT64_MAX),
          "the comparison does not overflow at the ends of the range");
  }
  // ---- the lines -----------------------------------------------------------------------------------------------------------------------------
  {
    HeadPoseSightings s;
    std::vector<std::string> lines;
    const auto sink=[&](const char* l){lines.emplace_back(l);};
    s.note(0x4E3881,sink);
    check(lines.size()==1&&lines[0]=="head pose: Elite's \"now\" requests are answered one display period after the drawn frame's display time (first from exe+0x4E3881)"&&s.distinct()==1,
          "the first caller answered writes the line (the exact text), with the return RVA");
    s.note(0x4E3881,sink);s.note(0x4E3881,sink);
    check(lines.size()==1,"...the same caller again writes nothing");
    s.note(0x2A03F51,sink);
    check(lines.size()==2&&lines[1]=="head pose: another Elite caller of \"now\" is answered one display period after the drawn frame's display time (exe+0x2A03F51)"&&s.distinct()==2,
          "a further distinct return address writes one more line, named (a caller that appears after a game update)");
    for(uint32_t i=0;i<6;++i)s.note(0x100+i,sink);
    check(lines.size()==8&&s.distinct()==8,"(six more distinct callers: eight lines in all so far)");
    s.note(0x200,sink);
    check(lines.size()==9&&lines[8]=="head pose: another Elite caller of \"now\" is answered one display period after the drawn frame's display time (exe+0x200)"&&s.distinct()==9,
          "the eighth FURTHER caller (the ninth in all) is still named");
    s.note(0x300,sink);
    check(lines.size()==10&&lines[9]=="head pose: more than 8 further callers of \"now\"; the rest are not logged"&&s.distinct()==9,
          "the next distinct caller finds the cap: one line says the rest are not logged, and it is not remembered");
    s.note(0x301,sink);s.note(0x300,sink);s.note(0x4E3881,sink);s.note(0x200,sink);
    check(lines.size()==10,"...which is said once; later callers, new or old, write nothing");
  }
}
}  // namespace head_pose_time_cases
