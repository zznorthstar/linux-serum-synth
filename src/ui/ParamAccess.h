#pragma once
// Typed read/write access to patch parameters by (OscParam / FilterParam / ...)
// so panels can bind widgets without one lambda per field.
#include "../Patch.h"
#include <algorithm>

namespace zyg::ui {

inline double getOsc(const Oscillator& o, OscParam p) {
    switch (p) {
        case OscParam::volume: return o.volume; case OscParam::pan: return o.pan;
        case OscParam::coarse: return o.semitone; case OscParam::fine: return o.fine;
        case OscParam::octave: return o.octave; case OscParam::detune: return o.detune;
        case OscParam::blend: return o.blend; case OscParam::unisonStereo: return o.unisonStereo;
        case OscParam::unisonWarp: return o.unisonWarp; case OscParam::unisonWarp2: return o.unisonWarp2;
        case OscParam::unisonWTPos: return o.unisonWTPos; case OscParam::tablePos: return o.tablePosition;
        case OscParam::warp1: return o.warpOneAmount; case OscParam::warp2: return o.warpTwoAmount;
        case OscParam::warpVar1: return o.warpDefinitions[0].var; case OscParam::warpVar2: return o.warpDefinitions[1].var;
        case OscParam::initialPhase: return o.initialPhase; case OscParam::randomPhase: return o.randomPhase;
        case OscParam::start: return o.start; case OscParam::end: return o.end;
        case OscParam::position: return o.position; case OscParam::scanRate: return o.scanRate;
        case OscParam::loopStart: return o.loopStart; case OscParam::loopEnd: return o.loopEnd;
        case OscParam::pitch: return o.pitch; case OscParam::pitchRatio: return o.pitchRatio;
        case OscParam::hzOffset: return o.hzOffset; case OscParam::grainLength: return o.grainLength;
        case OscParam::density: return o.density; case OscParam::randomOffset: return o.randomOffset;
        case OscParam::randomPitch: return o.randomPitch; case OscParam::randomPan: return o.randomPan;
        case OscParam::randomGain: return o.randomGain; case OscParam::randomDir: return o.randomDir;
        case OscParam::randomGrainLength: return o.randomGrainLength; case OscParam::windowParam: return o.windowParam;
        case OscParam::windowSkew: return o.windowSkew; case OscParam::timbreShift: return o.timbreShift;
        case OscParam::envAttack: return o.sampleEnv.attack; case OscParam::envDecay: return o.sampleEnv.decay;
        case OscParam::envSustain: return o.sampleEnv.sustain; case OscParam::envRelease: return o.sampleEnv.release;
        case OscParam::color: return o.noiseColor; case OscParam::freqLo: return o.freqLo;
        case OscParam::freqHi: return o.freqHi; case OscParam::specShift: return o.specFilterShift;
        case OscParam::specWet: return o.specFilterWet; case OscParam::randomWarp: return o.randomWarp;
        case OscParam::randomWarp2: return o.randomWarp2;
        default: return 0.0;
    }
}

inline void setOsc(Oscillator& o, OscParam p, double v) {
    switch (p) {
        case OscParam::volume: o.volume = v; break; case OscParam::pan: o.pan = v; break;
        case OscParam::coarse: o.semitone = v; break; case OscParam::fine: o.fine = v; break;
        case OscParam::octave: o.octave = int(v); break; case OscParam::detune: o.detune = v; break;
        case OscParam::blend: o.blend = v; break; case OscParam::unisonStereo: o.unisonStereo = v; break;
        case OscParam::unisonWarp: o.unisonWarp = v; break; case OscParam::unisonWarp2: o.unisonWarp2 = v; break;
        case OscParam::unisonWTPos: o.unisonWTPos = v; break; case OscParam::tablePos: o.tablePosition = v; break;
        case OscParam::warp1: o.warpOneAmount = v; break; case OscParam::warp2: o.warpTwoAmount = v; break;
        case OscParam::warpVar1: o.warpDefinitions[0].var = v; break;
        case OscParam::warpVar2: o.warpDefinitions[1].var = v; break;
        case OscParam::initialPhase: o.initialPhase = v; break; case OscParam::randomPhase: o.randomPhase = v; break;
        case OscParam::start: o.start = v; break; case OscParam::end: o.end = v; break;
        case OscParam::position: o.position = v; break; case OscParam::scanRate: o.scanRate = v; break;
        case OscParam::loopStart: o.loopStart = v; break; case OscParam::loopEnd: o.loopEnd = v; break;
        case OscParam::pitch: o.pitch = v; break; case OscParam::pitchRatio: o.pitchRatio = v; break;
        case OscParam::hzOffset: o.hzOffset = v; break; case OscParam::grainLength: o.grainLength = v; break;
        case OscParam::density: o.density = v; break; case OscParam::randomOffset: o.randomOffset = v; break;
        case OscParam::randomPitch: o.randomPitch = v; break; case OscParam::randomPan: o.randomPan = v; break;
        case OscParam::randomGain: o.randomGain = v; break; case OscParam::randomDir: o.randomDir = v; break;
        case OscParam::randomGrainLength: o.randomGrainLength = v; break; case OscParam::windowParam: o.windowParam = v; break;
        case OscParam::windowSkew: o.windowSkew = v; break; case OscParam::timbreShift: o.timbreShift = v; break;
        case OscParam::envAttack: o.sampleEnv.attack = v; break; case OscParam::envDecay: o.sampleEnv.decay = v; break;
        case OscParam::envSustain: o.sampleEnv.sustain = v; break; case OscParam::envRelease: o.sampleEnv.release = v; break;
        case OscParam::color: o.noiseColor = v; break; case OscParam::freqLo: o.freqLo = v; break;
        case OscParam::freqHi: o.freqHi = v; break; case OscParam::specShift: o.specFilterShift = v; break;
        case OscParam::specWet: o.specFilterWet = v; break; case OscParam::randomWarp: o.randomWarp = v; break;
        case OscParam::randomWarp2: o.randomWarp2 = v; break;
        default: break;
    }
}

inline double getFilter(const Filter& f, FilterParam p) {
    switch (p) {
        case FilterParam::freq: return f.cutoff; case FilterParam::reso: return f.resonance;
        case FilterParam::drive: return f.drive; case FilterParam::var: return f.var; case FilterParam::wet: return f.wet;
        case FilterParam::level: return f.level; case FilterParam::stereo: return f.stereo;
        case FilterParam::x: return f.x; case FilterParam::y: return f.y; default: return 0.0;
    }
}
inline void setFilter(Filter& f, FilterParam p, double v) {
    switch (p) {
        case FilterParam::freq: f.cutoff = v; break; case FilterParam::reso: f.resonance = v; break;
        case FilterParam::drive: f.drive = v; break; case FilterParam::var: f.var = v; break; case FilterParam::wet: f.wet = v; break;
        case FilterParam::level: f.level = v; break; case FilterParam::stereo: f.stereo = v; break;
        case FilterParam::x: f.x = v; break; case FilterParam::y: f.y = v; break; default: break;
    }
}

inline double getEnv(const Envelope& e, EnvParam p) {
    switch (p) {
        case EnvParam::attack: return e.attack; case EnvParam::hold: return e.hold; case EnvParam::decay: return e.decay;
        case EnvParam::sustain: return e.sustain; case EnvParam::release: return e.release;
        case EnvParam::curve1: return e.curve[0]; case EnvParam::curve2: return e.curve[1]; case EnvParam::curve3: return e.curve[2];
        default: return 0.0;
    }
}
inline void setEnv(Envelope& e, EnvParam p, double v) {
    switch (p) {
        case EnvParam::attack: e.attack = v; break; case EnvParam::hold: e.hold = v; break; case EnvParam::decay: e.decay = v; break;
        case EnvParam::sustain: e.sustain = v; break; case EnvParam::release: e.release = v; break;
        case EnvParam::curve1: e.curve[0] = v; break; case EnvParam::curve2: e.curve[1] = v; break;
        case EnvParam::curve3: e.curve[2] = v; break; default: break;
    }
}

inline double getLfo(const LfoDefinition& l, LfoParam p) {
    switch (p) {
        case LfoParam::rate: return l.rateHz; case LfoParam::phase: return l.phaseDegrees; case LfoParam::rise: return l.rise;
        case LfoParam::delay: return l.delay; case LfoParam::smooth: return l.smooth; default: return 0.0;
    }
}
inline void setLfo(LfoDefinition& l, LfoParam p, double v) {
    switch (p) {
        case LfoParam::rate: l.rateHz = v; break; case LfoParam::phase: l.phaseDegrees = v; break;
        case LfoParam::rise: l.rise = v; break; case LfoParam::delay: l.delay = v; break;
        case LfoParam::smooth: l.smooth = v; break; default: break;
    }
}

// Names shown in the matrix destination menu and knob labels.
inline const char* oscParamName(OscParam p) {
    switch (p) {
        case OscParam::volume: return "LEVEL"; case OscParam::pan: return "PAN"; case OscParam::coarse: return "SEMI";
        case OscParam::fine: return "FINE"; case OscParam::octave: return "OCT"; case OscParam::detune: return "DETUNE";
        case OscParam::blend: return "BLEND"; case OscParam::unisonStereo: return "STEREO";
        case OscParam::unisonWarp: return "UNI WARP 1"; case OscParam::unisonWarp2: return "UNI WARP 2";
        case OscParam::unisonWTPos: return "UNI WT POS"; case OscParam::tablePos: return "WT POS";
        case OscParam::warp1: return "WARP 1"; case OscParam::warp2: return "WARP 2";
        case OscParam::warpVar1: return "WARP 1 VAR"; case OscParam::warpVar2: return "WARP 2 VAR";
        case OscParam::initialPhase: return "PHASE"; case OscParam::randomPhase: return "RAND PHASE";
        case OscParam::start: return "START"; case OscParam::end: return "END"; case OscParam::position: return "POSITION";
        case OscParam::scanRate: return "SCAN"; case OscParam::loopStart: return "LOOP START"; case OscParam::loopEnd: return "LOOP END";
        case OscParam::pitch: return "PITCH"; case OscParam::pitchRatio: return "RATIO"; case OscParam::hzOffset: return "OFFSET HZ";
        case OscParam::grainLength: return "LENGTH"; case OscParam::density: return "DENSITY";
        case OscParam::randomOffset: return "RAND OFFSET"; case OscParam::randomPitch: return "RAND PITCH";
        case OscParam::randomPan: return "RAND PAN"; case OscParam::randomGain: return "RAND GAIN";
        case OscParam::randomDir: return "RAND DIR"; case OscParam::randomGrainLength: return "RAND LENGTH";
        case OscParam::windowParam: return "WINDOW"; case OscParam::windowSkew: return "SKEW"; case OscParam::timbreShift: return "TIMBRE";
        case OscParam::envAttack: return "ENV ATK"; case OscParam::envDecay: return "ENV DEC";
        case OscParam::envSustain: return "ENV SUS"; case OscParam::envRelease: return "ENV REL";
        case OscParam::color: return "COLOR"; case OscParam::freqLo: return "FREQ LO"; case OscParam::freqHi: return "FREQ HI";
        case OscParam::specShift: return "SPEC SHIFT"; case OscParam::specWet: return "SPEC WET";
        case OscParam::randomWarp: return "RAND WARP 1"; case OscParam::randomWarp2: return "RAND WARP 2";
        default: return "?";
    }
}
inline const char* filterParamName(FilterParam p) {
    switch (p) {
        case FilterParam::freq: return "CUTOFF"; case FilterParam::reso: return "RES"; case FilterParam::drive: return "DRIVE";
        case FilterParam::var: return "VAR"; case FilterParam::wet: return "MIX"; case FilterParam::level: return "LEVEL";
        case FilterParam::stereo: return "STEREO"; case FilterParam::x: return "X"; case FilterParam::y: return "Y";
        case FilterParam::pan: return "PAN"; default: return "?";
    }
}

}
