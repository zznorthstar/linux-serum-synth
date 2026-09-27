#pragma once
// Native FX parameter slot tables. The DSP addresses parameters by slot index;
// the Serum adapter uses `serumKey` to fill slots from a preset and the native
// preset format uses `name`. Units are the units stored by Serum presets.
#include "Patch.h"
#include <cmath>
#include <cstddef>
#include <span>

namespace zyg {
struct FxParamInfo {
    const char* name;      // native identifier
    const char* serumKey;  // observed serialized key (adapter only)
    double def, lo, hi;
};

namespace fx {
enum Bode { bShift, bRange, bFeedback, bDelayTime, bBlur, bDelayBalance, bOutputMix, bOutputWidth, bWet, bMonoInput, bSwapAB, bBeatSync, bRetrig };
enum Chorus { cDelay, cDelay2, cDepth, cFeedback, cFilt, cFiltMode, cRate, cWet, cBeatSync };
enum Comp { pAttack, pRelease, pThresh, pRatio, pRatioBelow, pMakeup, pWet, pMultiband, pXoverLow, pXoverHi,
            pGain0, pGain1, pGain2, pRatio0, pRatio1, pRatio2, pRatioBelow0, pRatioBelow1, pRatioBelow2,
            pThreshUD0, pThreshUD1, pThreshUD2 };
enum Conv { vAttack, vDamping, vDecay, vIpTrim, vPredelay, vSize, vTone, vWet, vMinPhase };
enum Delay { dBW, dFeedback, dFreq, dTimeL, dTimeR, dOffsetL, dOffsetR, dWet, dMode, dLink, dBeatSync };
enum Distortion { xBW, xDrive, xFreq, xLPHP, xNumStages, xPrePost, xWet };
enum Eq { eFreq1, eFreq2, eGain1, eGain2, eReso1, eReso2, eType1, eType2 };
enum Filter { fDrive, fFreq, fReso, fStereo, fVar, fWet, fX, fY, fPad };
enum Flanger { lDepth, lFeedback, lRate, lWet, lWidth, lBeatSync };
enum HyperD { hDetune, hDimESize, hDimEWet, hRate, hUnison, hWet, hRetrig };
enum Phaser { aDepth, aDepth2, aFeedback, aFreq, aNumPoles, aRate, aWet, aWidth, aBeatSync };
enum Reverb { rDelay, rFeedback, rFreq, rFreqB, rFreqC, rMode, rPreDelay, rSize, rVintageScale, rVintageScaleB, rWet, rWidth };
enum Utils { uBalance, uHPF, uLFMono, uLFXover, uLPF, uPolarityL, uPolarityR, uWet, uWidth };
enum Split { sFreq, sFreq2, sCount1, sCount2, sCount3 };
// ZYG extensions.
enum Pump { uDepth, uBeats, uShape, uHold, uAttack, uTrigger, uThresh, uRelease, uWetMix };
enum PumpTrigger { pumpTempo = 0, pumpNote = 1, pumpSidechain = 2, pumpFollow = 3 };
// Ducking amount (1 = fully ducked, 0 = open) at fraction `t` of a pump cycle; shared by the DSP and the editor display.
inline double pumpDuck(double t, double holdPercent, double shapePercent) noexcept {
    const double holdF = holdPercent < 0.0 ? 0.0 : holdPercent > 95.0 ? 0.95 : holdPercent / 100.0;
    if (t < holdF) return 1.0;
    double u = (t - holdF) / (1.0 - holdF); u = u < 0.0 ? 0.0 : u > 1.0 ? 1.0 : u;
    const double sh = shapePercent < -100.0 ? -100.0 : shapePercent > 100.0 ? 100.0 : shapePercent;
    double k = 1.0; { const double x = sh / 40.0 * 0.6931471805599453; k = std::exp(x); }
    return 1.0 - std::pow(u, k);
}
enum Stutter { tBeats, tActive, tMode, tChance, tPitch, tGate, tFalloff, tReverse, tSmooth, tWetMix };
}

std::span<const FxParamInfo> fxParamTable(FxType type) noexcept;
// Frequency and time parameters move logarithmically under modulation (see ParamRange::log).
bool fxParamIsLog(FxType type, std::size_t slot) noexcept;
FxType fxTypeFromSerum(const std::string& name) noexcept;
const char* fxTypeName(FxType type) noexcept;
// Native value for `slot`, falling back to the table default when unset.
inline double fxValue(const FxModule& m, std::size_t slot) noexcept {
    return m.p[slot];
}
void applyFxDefaults(FxModule& module);
}
