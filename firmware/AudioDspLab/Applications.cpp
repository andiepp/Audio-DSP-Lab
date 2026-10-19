#include "Applications.h"
#include "Config.h"
#include "Errors.h"
#include "Dsp.h"
#include "Eeprom24.h"
#include "Pots.h"
#include "Mclk.h"
#include "Radio.h"
#include "Analyzer.h"
#include "DacMux.h"
#include "Distortion.h"
#include "StudioBackup.h"
#include "Synth.h"
#include "Generator.h"
#include <Arduino.h>
#include <Preferences.h>
#include <Wire.h>
#include <math.h>

#include "DspImageEqualizer.h"
#include "DspImageRadio.h"
#include "DspImageAnalysis.h"

#define MAX_PARAMS                    56      // the synthesizer has 48
#define POT_PICKUP_THRESHOLD          80      // counts of 4095 (~2 %)
#define POT_BACKLASH                  28      // ADC noise: up to about 40 counts peak-to-peak after the scaling (Pots.cpp)
#define POT_RAW_MAX                   4095
// a self-boot reads at most 9248 bytes at 384 kHz (8 x fs): about 0.25 s
#define SIGMASTUDIO_BOOT_MS           600
// the startup option: whether a power-up starts the application
// selected last, and which one that was
#define STARTUP_STORE                 "aml-startup"
#define STARTUP_KEY_ON                "on"
#define STARTUP_KEY_APP               "app"

// ----------------------------------------------------------------------------
//  Shared parameter helpers
// ----------------------------------------------------------------------------

// level in 0.01 dB -> linear gain
static float levelToGain(int32_t centiDb) {
  return powf(10.0f, centiDb / 2000.0f);
}

// current value of a parameter of the active application (defined below)
static int32_t currentValue(uint8_t paramId);

// the rate the DSP core runs at: DSP_CORE_RATE under the firmware's own clock,
// twice the stream's rate while the radio drives it
static uint32_t coreRate = DSP_CORE_RATE;

// ----------------------------------------------------------------------------
//  Application 0x01 - signal generator
//  The signal is made on the ESP32 (Generator.cpp) and runs through the
//  radio's image, as the synthesizer's does: I2S in, the three bands set
//  flat, the volume (the amplitude here) as an RC-slewed gain, out on K3
//  (DAC0/DAC1) and K4 (DAC2/DAC3).
//  Pots as in the other applications: P1 the waveform, P2 the frequency
//  (one decade, chosen by p2-range), P3 the width, P4 the amplitude.
// ----------------------------------------------------------------------------

#define GEN_WAVEFORM                  1       // GeneratorWaveform: sine .. noise
#define GEN_FREQUENCY                 2       // 0.01 Hz, 20 Hz .. 20 kHz
#define GEN_AMPLITUDE                 3       // 0.01 dB, -60 .. 0 dBFS
#define GEN_OUTPUT                    4       // 0 off, 1 on
#define GEN_P2_RANGE                  5       // P2's decade: 0 20-200 Hz, 1 200 Hz-2 kHz, 2 2-20 kHz
#define GEN_WIDTH                     6       // %, 5 .. 95: the rectangle's high part, the triangle's rise

// the radio's volume and output gains (defined with the equalizer)
static bool eqGain(int32_t volume, int32_t output);

static bool genWaveform(int32_t value) {
  generatorSetWaveform((uint8_t)value);
  return true;
}

static bool genFrequency(int32_t value) {
  generatorSetFrequency(value);
  return true;
}

static bool genWidth(int32_t value) {
  generatorSetWidth((uint8_t)value);
  return true;
}

// amplitude and output share the slewed gain: off = gain 0
static bool genAmplitude(int32_t value) {
  return eqGain(value, currentValue(GEN_OUTPUT));
}

static bool genOutput(int32_t value) {
  return eqGain(currentValue(GEN_AMPLITUDE), value);
}

// P2: one decade, logarithmic: 20 Hz x 10^range .. 10 times that
static int32_t potToFrequencyIn(uint16_t raw, int32_t range) {
  return (int32_t)lroundf(2000.0f * powf(10.0f, (float)range) * powf(10.0f, raw / 4095.0f));
}

static int32_t potToFrequency(uint16_t raw) {
  return potToFrequencyIn(raw, currentValue(GEN_P2_RANGE));
}

// with pot control on, a new range moves the frequency to where P2 stands
// in it, and P2 drives it from there (defined below, with the pot state)
static void p2FollowsRange(int32_t range);

static bool genP2Range(int32_t value) {
  p2FollowsRange(value);
  return true;
}

// P4 (the analyzer's P2): a level, -60 .. 0 dB, linear in dB
static int32_t potToAmplitude(uint16_t raw) {
  return (int32_t)lroundf(-6000.0f + raw * (6000.0f / 4095.0f));
}

// P1: six equal zones, in the order of the waveforms
static int32_t potToWaveform(uint16_t raw) {
  return min((int)GENERATOR_WAVEFORM_LAST, (int)(raw * (GENERATOR_WAVEFORM_LAST + 1) / 4096));
}

// P3 (and the synthesizer's P1): a width, 5 .. 95 % over the travel
static int32_t potToWidth(uint16_t raw) {
  return 5 + (int32_t)lroundf(raw * (90.0f / 4095.0f));
}

static bool genTakeClock() {
  mclkStop();
  coreRate = DSP_CORE_RATE;
  return generatorBegin();
}

static void genReleaseClock() {
  generatorEnd();
  if (!mclkRunning()) mclkStart(DSP_SAMPLE_RATE);
}

static const ParamDef generatorParams[] = {
  { GEN_WAVEFORM,  0,     GENERATOR_WAVEFORM_LAST, GENERATOR_SINE, genWaveform, false },
  { GEN_FREQUENCY, 2000,  2000000, 100000, genFrequency, false },
  { GEN_AMPLITUDE, -6000, 0,       -2000,  genAmplitude, false },
  { GEN_OUTPUT,    0,     1,       0,      genOutput,    true },
  { GEN_P2_RANGE,  0,     2,       1,      genP2Range,   true },
  { GEN_WIDTH,     5,     95,      50,     genWidth,     false },
};
static const PotMap generatorPots[] = {
  { 0, GEN_WAVEFORM,  potToWaveform },
  { 1, GEN_FREQUENCY, potToFrequency },
  { 2, GEN_WIDTH,     potToWidth },
  { 3, GEN_AMPLITUDE, potToAmplitude },
};

// ----------------------------------------------------------------------------
//  Application 0x02 - equalizer
//  SigmaStudio project sigmastudio/Equalizer, export of 2026-10-11 09:44.
//  Line input K2 (ADC0 left, ADC1 right) -> bass, middle, treble ->
//  volume -> K3 (DAC0/DAC1) and K4 (DAC2/DAC3).
//  Parameter RAM (Export/ADAU1701_Equalizer_IC_1_PARAM.h):
//    'General (2nd order)' cells, B0 B1 B2 A1 A2 at consecutive addresses,
//    the A coefficients stored negated (y = B.x + A1.y[n-1] + A2.y[n-2]):
//      BassL 0, BassR 5, MiddleL 10, MiddleR 15, TrebleL 20, TrebleR 25
//    'Gain (RC Slew)' targets VolumeL 30, VolumeR 32 (5.23 linear gain);
//    steps 31/33 left at their default
//  The firmware computes every band from its gain and frequency (RBJ audio EQ
//  cookbook: low shelf and high shelf with slope 1, peaking with Q 0.7) for
//  the rate the core really runs, DSP_CORE_RATE (96 kHz: twice the sample
//  rate, Config.h).
// ----------------------------------------------------------------------------

#define EQ_BASS_GAIN                  1       // 0.01 dB, -12 .. +12 dB
#define EQ_BASS_FREQUENCY             2       // 0.01 Hz, 50 .. 400 Hz
#define EQ_MIDDLE_GAIN                3
#define EQ_MIDDLE_FREQUENCY           4       // 300 Hz .. 5 kHz
#define EQ_TREBLE_GAIN                5
#define EQ_TREBLE_FREQUENCY           6       // 500 Hz .. 8 kHz (2 .. 16 kHz before)
#define EQ_VOLUME                     7       // 0.01 dB, -60 .. 0 dB
#define EQ_OUTPUT                     8       // 0 off, 1 on

#define EQ_ADDR_BASS_L                0
#define EQ_ADDR_BASS_R                5
#define EQ_ADDR_MIDDLE_L              10
#define EQ_ADDR_MIDDLE_R              15
#define EQ_ADDR_TREBLE_L              20
#define EQ_ADDR_TREBLE_R              25
#define EQ_ADDR_VOLUME_L              30
#define EQ_ADDR_VOLUME_R              32
#define EQ_MIDDLE_Q                   0.7

enum EqShape : uint8_t { EQ_LOW_SHELF, EQ_PEAKING, EQ_HIGH_SHELF };

// gain in 0.01 dB, frequency in 0.01 Hz -> B0 B1 B2 A1 A2 as the DSP stores
// them; computed in double because a 50 Hz pole lies within 0.002 of one
static void eqBiquad(EqShape shape, int32_t centiDb, int32_t centiHz, float out[5]) {
  const double gain = pow(10.0, centiDb / 4000.0);              // A = 10^(dB/40)
  const double w0 = 2.0 * M_PI * (centiHz / 100.0) / coreRate;
  const double cw = cos(w0);
  const double sw = sin(w0);
  double b0, b1, b2, a0, a1, a2;
  if (shape == EQ_PEAKING) {
    const double alpha = sw / (2.0 * EQ_MIDDLE_Q);
    b0 = 1.0 + alpha * gain;
    b1 = -2.0 * cw;
    b2 = 1.0 - alpha * gain;
    a0 = 1.0 + alpha / gain;
    a1 = -2.0 * cw;
    a2 = 1.0 - alpha / gain;
  } else {
    const double k = 2.0 * sqrt(gain) * (sw / 2.0 * sqrt(2.0));  // slope 1
    const double s = shape == EQ_LOW_SHELF ? 1.0 : -1.0;          // high shelf mirrors cw
    b0 =            gain * ((gain + 1) - s * (gain - 1) * cw + k);
    b1 = 2.0 * s *  gain * ((gain - 1) - s * (gain + 1) * cw);
    b2 =            gain * ((gain + 1) - s * (gain - 1) * cw - k);
    a0 =                    (gain + 1) + s * (gain - 1) * cw + k;
    a1 = -2.0 * s *        ((gain - 1) + s * (gain + 1) * cw);
    a2 =                    (gain + 1) + s * (gain - 1) * cw - k;
  }
  out[0] = (float)(b0 / a0);
  out[1] = (float)(b1 / a0);
  out[2] = (float)(b2 / a0);
  out[3] = (float)(-a1 / a0);
  out[4] = (float)(-a2 / a0);
}

static bool eqBand(EqShape shape, uint16_t left, uint16_t right, int32_t centiDb, int32_t centiHz) {
  float coeff[5];
  eqBiquad(shape, centiDb, centiHz, coeff);
  const uint16_t leftAddresses[] = { left, (uint16_t)(left + 1), (uint16_t)(left + 2),
                                     (uint16_t)(left + 3), (uint16_t)(left + 4) };
  const uint16_t rightAddresses[] = { right, (uint16_t)(right + 1), (uint16_t)(right + 2),
                                      (uint16_t)(right + 3), (uint16_t)(right + 4) };
  return dspSafeload(leftAddresses, coeff, 5) && dspSafeload(rightAddresses, coeff, 5);
}

// an apply receives the new value before it is stored, so the band's other
// value comes from the table
static bool eqBassGain(int32_t value) {
  return eqBand(EQ_LOW_SHELF, EQ_ADDR_BASS_L, EQ_ADDR_BASS_R, value, currentValue(EQ_BASS_FREQUENCY));
}

static bool eqBassFrequency(int32_t value) {
  return eqBand(EQ_LOW_SHELF, EQ_ADDR_BASS_L, EQ_ADDR_BASS_R, currentValue(EQ_BASS_GAIN), value);
}

static bool eqMiddleGain(int32_t value) {
  return eqBand(EQ_PEAKING, EQ_ADDR_MIDDLE_L, EQ_ADDR_MIDDLE_R, value, currentValue(EQ_MIDDLE_FREQUENCY));
}

static bool eqMiddleFrequency(int32_t value) {
  return eqBand(EQ_PEAKING, EQ_ADDR_MIDDLE_L, EQ_ADDR_MIDDLE_R, currentValue(EQ_MIDDLE_GAIN), value);
}

static bool eqTrebleGain(int32_t value) {
  return eqBand(EQ_HIGH_SHELF, EQ_ADDR_TREBLE_L, EQ_ADDR_TREBLE_R, value, currentValue(EQ_TREBLE_FREQUENCY));
}

static bool eqTrebleFrequency(int32_t value) {
  return eqBand(EQ_HIGH_SHELF, EQ_ADDR_TREBLE_L, EQ_ADDR_TREBLE_R, currentValue(EQ_TREBLE_GAIN), value);
}

// volume and output share the slewed gains: off = gain 0
static bool eqGain(int32_t volume, int32_t output) {
  const uint16_t addresses[] = { EQ_ADDR_VOLUME_L, EQ_ADDR_VOLUME_R };
  const float gain = output ? levelToGain(volume) : 0.0f;
  const float values[] = { gain, gain };
  return dspSafeload(addresses, values, 2);
}

static bool eqVolume(int32_t value) {
  return eqGain(value, currentValue(EQ_OUTPUT));
}

static bool eqOutput(int32_t value) {
  return eqGain(currentValue(EQ_VOLUME), value);
}

// P1..P3: -12 .. +12 dB, linear in dB
static int32_t potToBandGain(uint16_t raw) {
  return (int32_t)lroundf(-1200.0f + raw * (2400.0f / 4095.0f));
}

static const ParamDef equalizerParams[] = {
  { EQ_BASS_GAIN,        -1200, 1200,    0,       eqBassGain,        false },
  { EQ_BASS_FREQUENCY,   5000,  40000,   20000,   eqBassFrequency,   false },
  { EQ_MIDDLE_GAIN,      -1200, 1200,    0,       eqMiddleGain,      false },
  { EQ_MIDDLE_FREQUENCY, 30000, 500000,  100000,  eqMiddleFrequency, false },
  { EQ_TREBLE_GAIN,      -1200, 1200,    0,       eqTrebleGain,      false },
  { EQ_TREBLE_FREQUENCY, 50000, 800000,  200000,  eqTrebleFrequency, false },
  { EQ_VOLUME,           -6000, 0,       -2000,   eqVolume,          false },
  { EQ_OUTPUT,           0,     1,       0,       eqOutput,          true },
};
static const PotMap equalizerPots[] = {
  { 0, EQ_BASS_GAIN,   potToBandGain },
  { 1, EQ_MIDDLE_GAIN, potToBandGain },
  { 2, EQ_TREBLE_GAIN, potToBandGain },
  { 3, EQ_VOLUME,      potToAmplitude },
};

// ----------------------------------------------------------------------------
//  Application 0x03 - internet radio
//  SigmaStudio project sigmastudio/Radio: the Equalizer project with its
//  input moved to I2S, so the cells and addresses are the equalizer's.
//  Parameters 1-8 are the equalizer's, 9 the station. While the radio
//  runs, the DSP's clock is the stream's (MCLK = 256 x its rate) and the
//  core rate twice that; the bands follow every change of it.
// ----------------------------------------------------------------------------

#define RADIO_STATION                 9       // index into the station list
#define RADIO_STATION_LAST            7       // the list has eight stations (Radio.cpp)

static bool radioStation(int32_t value) {
  radioSelect((uint8_t)value);
  return true;
}

static bool radioTakeClock() {
  mclkStop();
  coreRate = 2UL * RADIO_DEFAULT_SAMPLE_RATE;
  return radioBegin(0);
}

static void radioReleaseClock() {
  radioEnd();
  coreRate = DSP_CORE_RATE;
  if (!mclkRunning()) mclkStart(DSP_SAMPLE_RATE);
}

static const ParamDef radioParams[] = {
  { EQ_BASS_GAIN,        -1200, 1200,    0,       eqBassGain,        false },
  { EQ_BASS_FREQUENCY,   5000,  40000,   20000,   eqBassFrequency,   false },
  { EQ_MIDDLE_GAIN,      -1200, 1200,    0,       eqMiddleGain,      false },
  { EQ_MIDDLE_FREQUENCY, 30000, 500000,  100000,  eqMiddleFrequency, false },
  { EQ_TREBLE_GAIN,      -1200, 1200,    0,       eqTrebleGain,      false },
  { EQ_TREBLE_FREQUENCY, 50000, 800000,  200000,  eqTrebleFrequency, false },
  { EQ_VOLUME,           -6000, 0,       -2000,   eqVolume,          false },
  { EQ_OUTPUT,           0,     1,       0,       eqOutput,          true },
  { RADIO_STATION,       0,     RADIO_STATION_LAST, 0, radioStation, false },
};

// ----------------------------------------------------------------------------
//  Application 0x04 - analysis: Frequency Response and THD
//  SigmaStudio project sigmastudio/Analysis: the generator's sine and
//  slewed gain on K3 and K4; per input channel a band-pass, the absolute
//  value, a smoothing low-pass and a readback. Sweeps, filters, readings
//  and the reference are the analyzer's (Analyzer.cpp); the parameters
//  here are the output and the sweep's settings.
//  Parameter RAM: Amplitude target 3 (5.23 linear gain), step 4 (default).
// ----------------------------------------------------------------------------

#define ANA_ADDR_AMPLITUDE_TARGET    3

static bool anaGain(int32_t level, int32_t output) {
  return dspSafeloadOne(ANA_ADDR_AMPLITUDE_TARGET, output ? levelToGain(level) : 0.0f);
}

static bool anaLevel(int32_t value) {
  return anaGain(value, currentValue(ANA_OUTPUT));
}

static bool anaOutput(int32_t value) {
  return anaGain(currentValue(ANA_LEVEL), value);
}

// the sweep's settings take effect when a sweep starts
static bool anaSetting(int32_t) {
  return true;
}

// while the THD measurement runs, the tone sits on a bin's centre
static bool anaTone(int32_t value) {
  const bool snapped = distortionState() == DISTORTION_RUNNING && distortionMode() == DISTORTION_THD;
  return analyzerSetTone(snapped ? distortionSnap(value) : value);
}

static const ParamDef analysisParams[] = {
  { ANA_LEVEL,             -6000, 0,       -2000,   anaLevel,   false },
  { ANA_OUTPUT,            0,     1,       0,       anaOutput,  true },
  { ANA_START,             2000,  2000000, 2000,    anaSetting, false },
  { ANA_STOP,              2000,  2000000, 2000000, anaSetting, false },
  { ANA_POINTS_PER_DECADE, 1,     40,      10,      anaSetting, false },
  { ANA_TONE,              2000,  2000000, 100000,  anaTone,    false },
};
static const PotMap analysisPots[] = {
  { 1, ANA_LEVEL, potToAmplitude },
};

// ----------------------------------------------------------------------------
//  Application 0x06 - synthesizer
//  One voice made on the ESP32 (Synth.cpp), played from the MIDI input,
//  sent over I2S to the radio's DSP image: the bands flat, the volume
//  (cells and addresses the equalizer's) set here, P4 playing it. The
//  synthesizer's I2S channel clocks the DSP at 256 x 48 kHz, so the core
//  runs at DSP_CORE_RATE as under the firmware's own clock.
// ----------------------------------------------------------------------------

#define SYN_WAVEFORM                  1       // oscillator 1: 0 sine, 1 sawtooth, 2 rectangle, 3 triangle
#define SYN_VOLUME                    2       // 0.01 dB, -60 .. 0 dB
#define SYN_OUTPUT                    3       // 0 off, 1 on
#define SYN_CHANNEL                   4       // MIDI channel 1 .. 16
// oscillators 1 to 3 and the mixer
#define SYN_OSC1_OCTAVE               5       // -2 .. 2
#define SYN_OSC1_LEVEL                6       // 0 .. 100 %
#define SYN_OSC2_WAVEFORM             7       // as SYN_WAVEFORM
#define SYN_OSC2_OCTAVE               8
#define SYN_OSC2_DETUNE               9       // -700 .. 700 cents
#define SYN_OSC2_LEVEL                10
#define SYN_OSC3_WAVEFORM             11
#define SYN_OSC3_OCTAVE               12
#define SYN_OSC3_DETUNE               13
#define SYN_OSC3_LEVEL                14
#define SYN_NOISE_LEVEL               15      // 0 .. 100 %
// the LFOs and the envelope
#define SYN_LFO1_RATE                 16      // 0.01 Hz, 0.1 .. 20 Hz
#define SYN_LFO1_WAVEFORM             17      // 0 sine, 1 triangle, 2 square
#define SYN_LFO1_DEPTH                18      // vibrato, 0 .. 1200 cents at full mod strip
#define SYN_LFO2_RATE                 19
#define SYN_LFO2_WAVEFORM             20
#define SYN_LFO2_DEPTH                21      // tremolo, 0 .. 100 %
#define SYN_ATTACK                    22      // 1 .. 10000 ms
#define SYN_DECAY                     23
#define SYN_SUSTAIN                   24      // 0 .. 100 %
#define SYN_RELEASE                   25      // 1 .. 10000 ms
// the filter
#define SYN_CUTOFF                    26      // 20 .. 20000 Hz
#define SYN_EMPHASIS                  27      // 0 .. 100 %, 100 rings
#define SYN_CONTOUR_AMOUNT            28      // 0 .. 100 %, 100 = +5 octaves
#define SYN_CONTOUR_ATTACK            29      // 1 .. 10000 ms
#define SYN_CONTOUR_DECAY             30      // 1 .. 10000 ms, also its release
#define SYN_CONTOUR_SUSTAIN           31      // 0 .. 100 %
#define SYN_KEY_TRACKING              32      // 0 .. 100 %
// performance
#define SYN_GLIDE                     33      // 0 .. 5000 ms, 0 off
#define SYN_BEND_RANGE                34      // 0 .. 12 semitones
#define SYN_VELOCITY_LOUDNESS         35      // 0 .. 100 %
#define SYN_VELOCITY_BRIGHTNESS       36      // 0 .. 100 %, 100 = +3 octaves
#define SYN_FILTER_LFO                37      // 0 .. 100 %, 100 = +-2 octaves
// wave shapes
#define SYN_OSC1_WIDTH                38      // 5 .. 95 %, the rectangle; P1
#define SYN_OSC2_WIDTH                39
#define SYN_OSC3_WIDTH                40
#define SYN_OSC1_SYMMETRY             41      // 10 .. 90 %, the triangle's rise
#define SYN_OSC2_SYMMETRY             42
#define SYN_OSC3_SYMMETRY             43
#define SYN_LFO1_WIDTH                44      // 5 .. 95 %, the square
#define SYN_LFO2_WIDTH                45
#define SYN_LFO1_SYMMETRY             46      // 0 .. 100 %, the triangle's rise; 0, 100 ramps
#define SYN_LFO2_SYMMETRY             47
#define SYN_PWM_DEPTH                 48      // 0 .. 100 %, LFO 2 on every width, 100 = +-45 points

static bool synWaveform(int32_t value) { synthSetWaveform(0, (uint8_t)value); return true; }
static bool synOsc1Octave(int32_t value) { synthSetOctave(0, (int8_t)value); return true; }
static bool synOsc1Level(int32_t value) { synthSetLevel(0, (uint8_t)value); return true; }
static bool synOsc2Waveform(int32_t value) { synthSetWaveform(1, (uint8_t)value); return true; }
static bool synOsc2Octave(int32_t value) { synthSetOctave(1, (int8_t)value); return true; }
static bool synOsc2Detune(int32_t value) { synthSetDetune(1, (int16_t)value); return true; }
static bool synOsc2Level(int32_t value) { synthSetLevel(1, (uint8_t)value); return true; }
static bool synOsc3Waveform(int32_t value) { synthSetWaveform(2, (uint8_t)value); return true; }
static bool synOsc3Octave(int32_t value) { synthSetOctave(2, (int8_t)value); return true; }
static bool synOsc3Detune(int32_t value) { synthSetDetune(2, (int16_t)value); return true; }
static bool synOsc3Level(int32_t value) { synthSetLevel(2, (uint8_t)value); return true; }
static bool synNoiseLevel(int32_t value) { synthSetNoise((uint8_t)value); return true; }
static bool synLfo1Rate(int32_t value) { synthSetLfoRate(0, (uint16_t)value); return true; }
static bool synLfo1Waveform(int32_t value) { synthSetLfoWaveform(0, (uint8_t)value); return true; }
static bool synLfo1Depth(int32_t value) { synthSetLfoDepth(0, (uint16_t)value); return true; }
static bool synLfo2Rate(int32_t value) { synthSetLfoRate(1, (uint16_t)value); return true; }
static bool synLfo2Waveform(int32_t value) { synthSetLfoWaveform(1, (uint8_t)value); return true; }
static bool synLfo2Depth(int32_t value) { synthSetLfoDepth(1, (uint16_t)value); return true; }
static bool synAttack(int32_t value) { synthSetAttack((uint16_t)value); return true; }
static bool synDecay(int32_t value) { synthSetDecay((uint16_t)value); return true; }
static bool synSustain(int32_t value) { synthSetSustain((uint8_t)value); return true; }
static bool synRelease(int32_t value) { synthSetRelease((uint16_t)value); return true; }

static bool synCutoff(int32_t value) { synthSetCutoff((uint16_t)value); return true; }
static bool synEmphasis(int32_t value) { synthSetEmphasis((uint8_t)value); return true; }
static bool synContourAmount(int32_t value) { synthSetContourAmount((uint8_t)value); return true; }
static bool synContourAttack(int32_t value) { synthSetContourAttack((uint16_t)value); return true; }
static bool synContourDecay(int32_t value) { synthSetContourDecay((uint16_t)value); return true; }
static bool synContourSustain(int32_t value) { synthSetContourSustain((uint8_t)value); return true; }
static bool synKeyTracking(int32_t value) { synthSetKeyTracking((uint8_t)value); return true; }
static bool synGlide(int32_t value) { synthSetGlide((uint16_t)value); return true; }
static bool synBendRange(int32_t value) { synthSetBendRange((uint8_t)value); return true; }
static bool synVelocityLoudness(int32_t value) { synthSetVelocityLoudness((uint8_t)value); return true; }
static bool synVelocityBrightness(int32_t value) { synthSetVelocityBrightness((uint8_t)value); return true; }
static bool synFilterLfo(int32_t value) { synthSetFilterLfo((uint8_t)value); return true; }
static bool synOsc1Width(int32_t value) { synthSetWidth(0, (uint8_t)value); return true; }
static bool synOsc2Width(int32_t value) { synthSetWidth(1, (uint8_t)value); return true; }
static bool synOsc3Width(int32_t value) { synthSetWidth(2, (uint8_t)value); return true; }
static bool synOsc1Symmetry(int32_t value) { synthSetSymmetry(0, (uint8_t)value); return true; }
static bool synOsc2Symmetry(int32_t value) { synthSetSymmetry(1, (uint8_t)value); return true; }
static bool synOsc3Symmetry(int32_t value) { synthSetSymmetry(2, (uint8_t)value); return true; }
static bool synLfo1Width(int32_t value) { synthSetLfoWidth(0, (uint8_t)value); return true; }
static bool synLfo2Width(int32_t value) { synthSetLfoWidth(1, (uint8_t)value); return true; }
static bool synLfo1Symmetry(int32_t value) { synthSetLfoSymmetry(0, (uint8_t)value); return true; }
static bool synLfo2Symmetry(int32_t value) { synthSetLfoSymmetry(1, (uint8_t)value); return true; }
static bool synPwmDepth(int32_t value) { synthSetPwmDepth((uint8_t)value); return true; }

// P2: the cutoff, 20 Hz .. 20 kHz, logarithmic, three decades over the travel
static int32_t potToCutoff(uint16_t raw) {
  return (int32_t)lroundf(20.0f * powf(10.0f, raw * (3.0f / 4095.0f)));
}

// P3: the emphasis, 0 .. 100 %
static int32_t potToEmphasis(uint16_t raw) {
  return (int32_t)lroundf(raw * (100.0f / 4095.0f));
}

static bool synVolume(int32_t value) {
  return eqGain(value, currentValue(SYN_OUTPUT));
}

static bool synOutput(int32_t value) {
  return eqGain(currentValue(SYN_VOLUME), value);
}

static bool synChannel(int32_t value) {
  synthSetChannel((uint8_t)value);
  return true;
}

// the radio's image boots with whatever its bands were compiled with:
// 0 dB in every band makes each biquad pass the signal unchanged
static bool synFlatBands() {
  return eqBand(EQ_LOW_SHELF, EQ_ADDR_BASS_L, EQ_ADDR_BASS_R, 0, 20000) &&
         eqBand(EQ_PEAKING, EQ_ADDR_MIDDLE_L, EQ_ADDR_MIDDLE_R, 0, 100000) &&
         eqBand(EQ_HIGH_SHELF, EQ_ADDR_TREBLE_L, EQ_ADDR_TREBLE_R, 0, 200000);
}

static bool synTakeClock() {
  mclkStop();
  coreRate = DSP_CORE_RATE;
  return synthBegin();
}

static void synReleaseClock() {
  synthEnd();
  if (!mclkRunning()) mclkStart(DSP_SAMPLE_RATE);
}

static const ParamDef synthParams[] = {
  { SYN_WAVEFORM,      0,     SYNTH_WAVEFORM_LAST, SYNTH_SAWTOOTH, synWaveform,     false },
  { SYN_VOLUME,        -6000, 0,    -2000, synVolume,       false },
  { SYN_OUTPUT,        0,     1,    0,     synOutput,       true },
  { SYN_CHANNEL,       1,     16,   15,    synChannel,      false },
  { SYN_OSC1_OCTAVE,   -2,    2,    0,     synOsc1Octave,   false },
  { SYN_OSC1_LEVEL,    0,     100,  100,   synOsc1Level,    false },
  { SYN_OSC2_WAVEFORM, 0,     SYNTH_WAVEFORM_LAST, SYNTH_SAWTOOTH, synOsc2Waveform, false },
  { SYN_OSC2_OCTAVE,   -2,    2,    0,     synOsc2Octave,   false },
  { SYN_OSC2_DETUNE,   -700,  700,  0,     synOsc2Detune,   false },
  { SYN_OSC2_LEVEL,    0,     100,  0,     synOsc2Level,    false },
  { SYN_OSC3_WAVEFORM, 0,     SYNTH_WAVEFORM_LAST, SYNTH_SAWTOOTH, synOsc3Waveform, false },
  { SYN_OSC3_OCTAVE,   -2,    2,    0,     synOsc3Octave,   false },
  { SYN_OSC3_DETUNE,   -700,  700,  0,     synOsc3Detune,   false },
  { SYN_OSC3_LEVEL,    0,     100,  0,     synOsc3Level,    false },
  { SYN_NOISE_LEVEL,   0,     100,  0,     synNoiseLevel,   false },
  { SYN_LFO1_RATE,     10,    2000, 500,   synLfo1Rate,     false },
  { SYN_LFO1_WAVEFORM, 0,     2,    1,     synLfo1Waveform, false },
  { SYN_LFO1_DEPTH,    0,     1200, 50,    synLfo1Depth,    false },
  { SYN_LFO2_RATE,     10,    2000, 400,   synLfo2Rate,     false },
  { SYN_LFO2_WAVEFORM, 0,     2,    0,     synLfo2Waveform, false },
  { SYN_LFO2_DEPTH,    0,     100,  0,     synLfo2Depth,    false },
  { SYN_ATTACK,        1,     10000, 5,    synAttack,       false },
  { SYN_DECAY,         1,     10000, 200,  synDecay,        false },
  { SYN_SUSTAIN,       0,     100,  100,   synSustain,      false },
  { SYN_RELEASE,       1,     10000, 100,  synRelease,      false },
  { SYN_CUTOFF,        20,    20000, 20000, synCutoff,       false },
  { SYN_EMPHASIS,      0,     100,  0,     synEmphasis,     false },
  { SYN_CONTOUR_AMOUNT, 0,    100,  0,     synContourAmount, false },
  { SYN_CONTOUR_ATTACK, 1,    10000, 5,    synContourAttack, false },
  { SYN_CONTOUR_DECAY, 1,     10000, 300,  synContourDecay, false },
  { SYN_CONTOUR_SUSTAIN, 0,   100,  50,    synContourSustain, false },
  { SYN_KEY_TRACKING,  0,     100,  0,     synKeyTracking,  false },
  { SYN_GLIDE,         0,     5000, 0,     synGlide,        false },
  { SYN_BEND_RANGE,    0,     12,   2,     synBendRange,    false },
  { SYN_VELOCITY_LOUDNESS, 0, 100,  0,     synVelocityLoudness, false },
  { SYN_VELOCITY_BRIGHTNESS, 0, 100, 0,    synVelocityBrightness, false },
  { SYN_FILTER_LFO,    0,     100,  0,     synFilterLfo,    false },
  { SYN_OSC1_WIDTH,    5,     95,   50,    synOsc1Width,    false },
  { SYN_OSC2_WIDTH,    5,     95,   50,    synOsc2Width,    false },
  { SYN_OSC3_WIDTH,    5,     95,   50,    synOsc3Width,    false },
  { SYN_OSC1_SYMMETRY, 10,    90,   50,    synOsc1Symmetry, false },
  { SYN_OSC2_SYMMETRY, 10,    90,   50,    synOsc2Symmetry, false },
  { SYN_OSC3_SYMMETRY, 10,    90,   50,    synOsc3Symmetry, false },
  { SYN_LFO1_WIDTH,    5,     95,   50,    synLfo1Width,    false },
  { SYN_LFO2_WIDTH,    5,     95,   50,    synLfo2Width,    false },
  { SYN_LFO1_SYMMETRY, 0,     100,  50,    synLfo1Symmetry, false },
  { SYN_LFO2_SYMMETRY, 0,     100,  50,    synLfo2Symmetry, false },
  { SYN_PWM_DEPTH,     0,     100,  0,     synPwmDepth,     false },
};
static const PotMap synthPots[] = {
  { 0, SYN_OSC1_WIDTH, potToWidth },
  { 1, SYN_CUTOFF,   potToCutoff },
  { 2, SYN_EMPHASIS, potToEmphasis },
  { 3, SYN_VOLUME,   potToAmplitude },
};

static const AppDef applications[] = {
  { 0x01, "Signal generator", DspImageRadio, DspImageRadioLength,
    generatorParams, 6, generatorPots, 4, genTakeClock, genReleaseClock },
  { 0x02, "Equalizer", DspImageEqualizer, DspImageEqualizerLength,
    equalizerParams, 8, equalizerPots, 4, nullptr, nullptr },
  { 0x03, "Internet radio", DspImageRadio, DspImageRadioLength,
    radioParams, 9, equalizerPots, 4, radioTakeClock, radioReleaseClock },
  { APP_ANALYSIS, "Analysis", DspImageAnalysis, DspImageAnalysisLength,
    analysisParams, 6, analysisPots, 1, nullptr, nullptr },
  // no image, no parameters: the DSP runs what SigmaStudio wrote (see start)
  { APP_SIGMASTUDIO, "SigmaStudio mode", nullptr, 0,
    nullptr, 0, nullptr, 0, nullptr, nullptr },
  // the test applications 0xF0 and 0xF1 (Audio-Lab Demo1/Demo2) went at 1.3
  { APP_SYNTH, "Synthesizer", DspImageRadio, DspImageRadioLength,
    synthParams, sizeof(synthParams) / sizeof(synthParams[0]),
    synthPots, sizeof(synthPots) / sizeof(synthPots[0]), synTakeClock, synReleaseClock },
};
static const uint8_t applicationCount = sizeof(applications) / sizeof(applications[0]);

// ----------------------------------------------------------------------------
//  State
// ----------------------------------------------------------------------------

static const AppDef *active = nullptr;
static AppState state = APP_STATE_IDLE;
static uint8_t lastError = ERR_NONE;
static int32_t values[MAX_PARAMS];
static uint16_t potAnchor[POT_COUNT];      // pot position at the last write from the PC
static bool potEngaged[POT_COUNT];
static bool potControl = false;            // off at every power-up
static int potApplied[POT_COUNT];          // pot position the current value was taken from
static bool startupRestores = false;       // off: the SigmaStudio mode at every power-up
static uint8_t startupApp = APP_SIGMASTUDIO;
static ParamListener paramListener = nullptr;

static void notifyChanged(uint8_t paramId, int32_t value) {
  if (paramListener != nullptr) paramListener(paramId, value);
}

static const AppDef *findApp(uint8_t id) {
  for (uint8_t i = 0; i < applicationCount; i++) {
    if (applications[i].id == id) return &applications[i];
  }
  return nullptr;
}

static int findParam(uint8_t paramId) {
  if (active == nullptr) return -1;
  for (uint8_t i = 0; i < active->paramCount; i++) {
    if (active->params[i].id == paramId) return i;
  }
  return -1;
}

static int32_t currentValue(uint8_t paramId) {
  int index = findParam(paramId);
  return index < 0 ? 0 : values[index];
}

// the pot's position, its ends snapped so they stay reachable
static int potPosition(uint8_t pot) {
  int raw = potRaw(pot);
  if (raw <= POT_BACKLASH) return 0;
  if (raw >= POT_RAW_MAX - POT_BACKLASH) return POT_RAW_MAX;
  return raw;
}

static void p2FollowsRange(int32_t range) {
  if (!potControl || active == nullptr || state != APP_STATE_RUNNING) return;
  int index = findParam(GEN_FREQUENCY);
  if (index < 0) return;
  const ParamDef &param = active->params[index];
  int position = potPosition(1);
  int32_t frequency = constrain(potToFrequencyIn((uint16_t)position, range), param.minimum, param.maximum);
  if (frequency != values[index] && param.apply(frequency)) {
    values[index] = frequency;
    notifyChanged(GEN_FREQUENCY, frequency);
  }
  potEngaged[1] = true;                     // P2 drives the frequency in its new decade
  potApplied[1] = position;
}

// a pot must move again before it overrides the value it drives
static void disengagePotsFor(uint8_t paramId) {
  if (active == nullptr) return;
  for (uint8_t i = 0; i < active->potCount; i++) {
    const PotMap &map = active->pots[i];
    if (map.paramId == paramId) {
      potAnchor[map.pot] = potRaw(map.pot);
      potEngaged[map.pot] = false;
    }
  }
}

// whether an image, read up to its end message, is one of the firmware's own
// (StudioBackup tells your SigmaStudio program from them by this). The
// firmware's arrays carry SigmaStudio's padding after the end message, so the
// image read is compared with their beginning
static bool isNodeImage(const uint8_t *image, size_t length) {
  for (uint8_t i = 0; i < applicationCount; i++) {
    const AppDef &app = applications[i];
    if (app.image != nullptr && length <= app.imageLength && memcmp(app.image, image, length) == 0) {
      return true;
    }
  }
  return false;
}

// another master (the USBi) may have used the bus while the firmware kept off
// it, which can leave the ESP32's I2C driver in an error state; the firmware
// starts it afresh before it touches the bus again
static void takeBusBack() {
  Wire.end();
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL, I2C_CLOCK_HZ);
}

static uint8_t fail(uint8_t error) {
  state = APP_STATE_FAILED;
  lastError = error;
  return error;
}

// load image (only if the EEPROM differs), boot, apply the values: the
// defaults, or the values the application had when the start resumes it
// (leaving service mode); with pot control on, a pot-driven parameter
// starts where its pot stands either way
static uint8_t start(const AppDef *app, bool resume) {
  state = APP_STATE_LOADING;
  dspHoldReset();
  delay(5);
  takeBusBack();
  // a sweep stops with its application; the SigmaStudio mode gives the
  // pots and the EEPROM back
  if (active != nullptr && active->id == APP_ANALYSIS) analyzerEnd();
  bool leavingStudio = active != nullptr && active->id == APP_SIGMASTUDIO;
  if (leavingStudio) {
    dacMuxEnd();
    digitalWrite(PIN_EEPROM_WP, HIGH);
  }
  // your SigmaStudio program is copied before another image replaces it,
  // leaving the mode or at power-up (nothing active yet);
  // without the copy the switch is refused, so it is never lost
  if ((leavingStudio || active == nullptr) && app->id != APP_SIGMASTUDIO &&
      studioBackupSave(isNodeImage) == BACKUP_FAILED) {
    if (!leavingStudio) return fail(ERR_EEPROM_FAILED);   // appsInit falls back
    start(active, true);                                // back to the mode as it was
    lastError = ERR_EEPROM_FAILED;
    return ERR_EEPROM_FAILED;
  }
  // the DSP's clock: the application leaving releases what it took, the new
  // one takes its own, or the firmware's 48 kHz runs, before the DSP boots
  if (active != nullptr && active->end != nullptr) active->end();
  active = app;
  if (app->begin != nullptr) {
    if (!app->begin()) return fail(ERR_DSP_NOT_RESPONDING);
  } else if (!mclkRunning()) {
    mclkStart(DSP_SAMPLE_RATE);
  }
  if (app->id == APP_SIGMASTUDIO) {
    // the DSP boots what the EEPROM holds: it self-boots on the rising
    // edge of reset only with WP high (and JP2 on Selfboot), as a master on
    // the bus. Once it has loaded, WP low gives the EEPROM to SigmaStudio
    // and the USBi, as service mode does. The firmware writes nothing and
    // stays off the bus.
    // your SigmaStudio program back, if one of the firmware's images replaced it
    // (with the DSP still in reset); a failure (a USBi on the bus) leaves
    // the EEPROM as it is and is reported, the mode runs anyway
    BackupResult restored = studioBackupRestore(isNodeImage);
    digitalWrite(PIN_EEPROM_WP, HIGH);
    dspReleaseReset();
    delay(SIGMASTUDIO_BOOT_MS);
    digitalWrite(PIN_EEPROM_WP, LOW);
    if (!dacMuxBegin()) return fail(ERR_DSP_WRITE_FAILED);
    state = APP_STATE_RUNNING;
    lastError = restored == BACKUP_FAILED ? ERR_EEPROM_FAILED : ERR_NONE;
    return ERR_NONE;
  }
  if (!eepromMatches(app->image, app->imageLength) &&
      !eepromWrite(app->image, app->imageLength)) {
    return fail(ERR_EEPROM_FAILED);
  }
  if (!dspWaitForBoot(DSP_BOOT_TIMEOUT_MS)) {
    return fail(ERR_DSP_NOT_RESPONDING);
  }
  // all values first: an apply may depend on another parameter's value
  for (uint8_t i = 0; i < app->paramCount && i < MAX_PARAMS; i++) {
    if (!resume || app->params[i].resetOnStart) values[i] = app->params[i].initial;
  }
  for (uint8_t i = 0; i < app->potCount && potControl; i++) {
    const PotMap &map = app->pots[i];
    int index = findParam(map.paramId);
    if (index < 0) continue;
    const ParamDef &param = app->params[index];
    potApplied[map.pot] = potPosition(map.pot);
    potEngaged[map.pot] = true;               // the pot drives until the PC writes
    values[index] = constrain(map.map((uint16_t)potApplied[map.pot]), param.minimum, param.maximum);
  }
  for (uint8_t i = 0; i < app->paramCount && i < MAX_PARAMS; i++) {
    if (!app->params[i].apply(values[i])) return fail(ERR_DSP_WRITE_FAILED);
  }
  if (app->id == APP_ANALYSIS && !analyzerBegin(currentValue(ANA_TONE))) {
    return fail(ERR_DSP_WRITE_FAILED);
  }
  if ((app->id == APP_SYNTH || app->begin == genTakeClock) && !synFlatBands()) {
    return fail(ERR_DSP_WRITE_FAILED);
  }
  state = APP_STATE_RUNNING;
  lastError = ERR_NONE;
  return ERR_NONE;
}

// ----------------------------------------------------------------------------
//  Public
// ----------------------------------------------------------------------------

static void loadStartup() {
  Preferences store;
  if (!store.begin(STARTUP_STORE, true)) return;        // nothing ever stored: off
  startupRestores = store.getBool(STARTUP_KEY_ON, false);
  startupApp = store.getUChar(STARTUP_KEY_APP, APP_SIGMASTUDIO);
  store.end();
}

// writes what changed only
static bool storeStartup(bool restores, uint8_t id) {
  if (restores == startupRestores && id == startupApp) return true;
  Preferences store;
  if (!store.begin(STARTUP_STORE, false)) return false;
  bool ok = true;
  if (restores != startupRestores) ok = store.putBool(STARTUP_KEY_ON, restores) == 1;
  if (ok && id != startupApp) ok = store.putUChar(STARTUP_KEY_APP, id) == 1;
  store.end();
  if (!ok) return false;
  startupRestores = restores;
  startupApp = id;
  return true;
}

// a power-up into the radio or the synthesizer is meant to be heard: their
// output comes on ; the generator and the analyzer
// may drive a device under test and stay off
static void outputOnAtPowerUp() {
  uint8_t paramId = active->begin == radioTakeClock ? EQ_OUTPUT
                  : active->id == APP_SYNTH ? SYN_OUTPUT : 0;
  int index = paramId == 0 ? -1 : findParam(paramId);
  if (index >= 0 && active->params[index].apply(1)) values[index] = 1;
}

// the SigmaStudio mode at power-up, or with the startup option
// on the application selected last. That one starts as a
// selection does, with its defaults, but the radio and the synthesizer
// with their output on; if it cannot start (the copy of your
// program, its image or its boot fails, as with a USBi on the header) the
// SigmaStudio mode starts instead, the failure kept as the last error
void appsInit() {
  loadStartup();
  const AppDef *studio = findApp(APP_SIGMASTUDIO);
  const AppDef *app = startupRestores ? findApp(startupApp) : nullptr;
  if (app == nullptr || app == studio) {
    start(studio, false);
    return;
  }
  uint8_t error = start(app, false);
  if (error == ERR_NONE) {
    outputOnAtPowerUp();
    return;
  }
  start(studio, false);
  lastError = error;
}

// the radio's stream changed its rate: the core rate follows, and the bands
// are computed again for it (the firmware's loop alone talks to the DSP)
static void followRadioRate() {
  uint32_t rate;
  if (!radioTakeRateChange(rate) || rate == 0) return;
  coreRate = 2UL * rate;
  for (uint8_t i = 0; i < active->paramCount; i++) {
    uint8_t id = active->params[i].id;
    if (id >= EQ_BASS_GAIN && id <= EQ_TREBLE_FREQUENCY) active->params[i].apply(values[i]);
  }
}

void appsPoll() {
  if (active != nullptr && state == APP_STATE_RUNNING && active->begin == radioTakeClock) followRadioRate();
  if (active != nullptr && state == APP_STATE_RUNNING && active->id == APP_ANALYSIS) analyzerPoll();
  if (active != nullptr && state == APP_STATE_RUNNING && active->id == APP_SYNTH) synthPoll();
  bool sampled = potsPoll();
  // the SigmaStudio mode forwards the pots always, whatever pot control says
  if (active != nullptr && state == APP_STATE_RUNNING && active->id == APP_SIGMASTUDIO) {
    dacMuxPoll();
    return;
  }
  if (!sampled || !potControl || active == nullptr || state != APP_STATE_RUNNING) return;
  for (uint8_t i = 0; i < active->potCount; i++) {
    const PotMap &map = active->pots[i];
    int raw = potRaw(map.pot);
    int &applied = potApplied[map.pot];
    if (!potEngaged[map.pot]) {
      if (abs(raw - (int)potAnchor[map.pot]) <= POT_PICKUP_THRESHOLD) continue;
      potEngaged[map.pot] = true;
      applied = raw;
    } else if (raw > applied + POT_BACKLASH) {
      applied = raw - POT_BACKLASH;           // trails the pot like gear backlash:
    } else if (raw < applied - POT_BACKLASH) {
      applied = raw + POT_BACKLASH;           // reversing needs 2 x POT_BACKLASH
    } else {
      continue;                               // noise, not a movement
    }
    int position = potPosition(map.pot);
    if (position != raw) applied = position;  // the ends stay reachable
    int index = findParam(map.paramId);
    if (index < 0) continue;
    const ParamDef &param = active->params[index];
    int32_t value = constrain(map.map((uint16_t)applied), param.minimum, param.maximum);
    if (value != values[index]) {
      values[index] = value;
      param.apply(value);
      notifyChanged(map.paramId, value);
    }
  }
}

uint8_t appActive() {
  return active ? active->id : APP_NONE;
}

AppState appState() {
  return state;
}

uint8_t appLastError() {
  return lastError;
}

uint8_t appSelect(uint8_t id) {
  if (state == APP_STATE_SERVICE) return ERR_SERVICE_MODE;
  const AppDef *app = findApp(id);
  if (app == nullptr) return ERR_UNKNOWN_APPLICATION;
  uint8_t error = start(app, false);
  // with the startup option on, the selection is what the next power-up
  // starts; a failed NVS write leaves the one before (0x17 shows which)
  if (error == ERR_NONE && startupRestores) storeStartup(true, id);
  return error;
}

uint8_t appParamGet(uint8_t paramId, int32_t &value) {
  int index = findParam(paramId);
  if (index < 0) return ERR_UNKNOWN_PARAMETER;
  value = values[index];
  return ERR_NONE;
}

uint8_t appParamSet(uint8_t paramId, int32_t value) {
  if (state == APP_STATE_SERVICE) return ERR_SERVICE_MODE;
  int index = findParam(paramId);
  if (index < 0) return ERR_UNKNOWN_PARAMETER;
  const ParamDef &param = active->params[index];
  if (value < param.minimum || value > param.maximum) return ERR_OUT_OF_RANGE;
  if (state != APP_STATE_RUNNING) return ERR_DSP_NOT_RESPONDING;
  if (!param.apply(value)) return ERR_DSP_WRITE_FAILED;
  values[index] = value;
  disengagePotsFor(paramId);
  return ERR_NONE;
}

// Service mode = the Demo sketch's PROG mode: EEPROM writable, DSP running,
// the firmware off the I2C bus so SigmaStudio/USBi owns it.
uint8_t appServiceMode(bool enable) {
  if (enable) {
    if (state == APP_STATE_SERVICE) return ERR_NONE;
    if (active != nullptr && active->id == APP_ANALYSIS) analyzerEnd();
    if (active != nullptr && active->id == APP_SIGMASTUDIO) dacMuxEnd();
    // SigmaStudio gets the firmware's 48 kHz clock: an application that took
    // the clock (the radio) lets it go; leaving service mode restarts it
    if (active != nullptr && active->end != nullptr) active->end();
    if (!mclkRunning()) mclkStart(DSP_SAMPLE_RATE);
    state = APP_STATE_SERVICE;
    digitalWrite(PIN_EEPROM_WP, LOW);
    digitalWrite(PIN_DSP_RESET, HIGH);
    return ERR_NONE;
  }
  if (state != APP_STATE_SERVICE) return ERR_NONE;
  digitalWrite(PIN_EEPROM_WP, HIGH);
  // the EEPROM is brought back to the active application (SigmaStudio
  // writes made in service mode are replaced)
  if (active != nullptr) return start(active, true);
  state = APP_STATE_IDLE;
  dspHoldReset();
  delay(5);
  dspWaitForBoot(DSP_BOOT_TIMEOUT_MS);
  return ERR_NONE;
}

bool appInServiceMode() {
  return state == APP_STATE_SERVICE;
}

// turning it on takes the pots' positions at once: every pot-driven value
// goes to where its pot stands, and the pot drives it from there (as an
// application start with pot control on does)
void appPotControl(bool enable) {
  bool takeOver = enable && !potControl;
  potControl = enable;
  if (!takeOver || active == nullptr || state != APP_STATE_RUNNING) return;
  for (uint8_t i = 0; i < active->potCount; i++) {
    const PotMap &map = active->pots[i];
    int index = findParam(map.paramId);
    if (index < 0) continue;
    const ParamDef &param = active->params[index];
    potApplied[map.pot] = potPosition(map.pot);
    potEngaged[map.pot] = true;
    int32_t value = constrain(map.map((uint16_t)potApplied[map.pot]), param.minimum, param.maximum);
    if (value != values[index] && param.apply(value)) {
      values[index] = value;
      notifyChanged(map.paramId, value);
    }
  }
}

bool appPotControlEnabled() {
  return potControl;
}

uint8_t appStudioScan(uint16_t &length, bool &nodeImage, uint32_t &crc, uint16_t &stoppedAt) {
  return studioBackupScan(isNodeImage, length, nodeImage, crc, stoppedAt);
}

// switching the option on stores the application selected (running, or
// in service mode); after a failed start it stores the SigmaStudio mode
uint8_t appStartupSet(bool restores) {
  uint8_t id = startupApp;
  if (restores) {
    id = active != nullptr && (state == APP_STATE_RUNNING || state == APP_STATE_SERVICE)
      ? active->id : APP_SIGMASTUDIO;
  }
  return storeStartup(restores, id) ? ERR_NONE : ERR_STORE_FAILED;
}

void appStartupGet(bool &restores, uint8_t &id) {
  restores = startupRestores;
  id = startupApp;
}

uint8_t appCount() {
  return applicationCount;
}

const AppDef *appAt(uint8_t index) {
  return index < applicationCount ? &applications[index] : nullptr;
}

int8_t appParamPot(uint8_t paramId) {
  if (active == nullptr) return -1;
  for (uint8_t i = 0; i < active->potCount; i++) {
    if (active->pots[i].paramId == paramId) return (int8_t)active->pots[i].pot;
  }
  return -1;
}

uint8_t appParamCount() {
  return active != nullptr ? active->paramCount : 0;
}

const ParamDef *appParamAt(uint8_t index) {
  return active != nullptr && index < active->paramCount ? &active->params[index] : nullptr;
}

void appOnParamChanged(ParamListener listener) {
  paramListener = listener;
}

bool appRunning(uint8_t id) {
  return active != nullptr && active->id == id && state == APP_STATE_RUNNING;
}
