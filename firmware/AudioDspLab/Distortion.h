// ****************************************************************************
// ***              D I S T O R T I O N   M E A S U R E M E N T             ***
// ****************************************************************************
//  Part of the response analyzer (application 0x04): its image sends both
//  line inputs, untouched, to the DSP's serial output, which the ESP32's
//  second I2S port receives (Capture.cpp). A task of its own takes 8192
//  frames of both channels (0.17 s at 48 kHz), windows them (Blackman-
//  Harris, 4 terms), transforms them (esp-dsp: a complex FFT of 4096
//  points and the split into the real spectrum) and averages the power
//  of DISTORTION_AVERAGES captures. From that spectrum, per channel: the
//  fundamental's level, the harmonics H2 to H10 below 20 kHz, THD and
//  THD+N (20 Hz to 20 kHz without DC and the fundamental), and a spectrum
//  of 256 points on a logarithmic axis for the plots.
//
//  The DSP and the capture share one clock, so a tone on a bin's centre
//  (distortionSnap) sits on its bin exactly; the window is the margin for
//  tones that do not (an outside source).
//
//  The task touches the serial output and its own memory only; the
//  firmware's loop alone talks to the DSP. About 120 KB of heap while it
//  runs, all of it given back by distortionStop().
// ----------------------------------------------------------------------------

#ifndef DISTORTION_H
#define DISTORTION_H

#include <stdint.h>

#define DISTORTION_FRAMES             8192    // per capture: 0.17 s at 48 kHz
#define DISTORTION_AVERAGES           4       // captures a result averages: about 0.75 s
#define DISTORTION_HARMONIC_LAST      10      // H2 .. H10
#define DISTORTION_SPECTRUM_POINTS    256     // 20 Hz .. 24 kHz, logarithmic
#define DISTORTION_LOW_HZ             20.0f   // THD+N's band
#define DISTORTION_HIGH_HZ            20000.0f

enum DistortionState : uint8_t {
  DISTORTION_IDLE    = 0,
  DISTORTION_RUNNING = 1,
  DISTORTION_FAILED  = 2,   // the serial output lost its clocks
};

struct DistortionChannel {
  float levelDb;            // the fundamental's peak, dBFS
  float thdDb;              // the harmonics against the fundamental, dB; NAN without any below 20 kHz
  float thdnDb;             // all but the fundamental from 20 Hz to 20 kHz, dB
  float harmonicDb[DISTORTION_HARMONIC_LAST + 1];   // [2] .. [10], dBc; NAN above 20 kHz
};

struct DistortionResult {
  uint32_t count;           // results since the start
  float hertz;              // the fundamental
  DistortionChannel channel[2];
  float spectrumDb[2][DISTORTION_SPECTRUM_POINTS];  // per point the strongest bin, as a sine's peak in dBFS
};

uint8_t distortionStart();                  // an error code (Errors.h); restarts a running one
void distortionStop();                      // waits for the task, gives the memory back
DistortionState distortionState();
bool distortionBusy();                      // the task holds the serial output capture
uint32_t distortionCount();
bool distortionResult(DistortionResult &result);   // the latest; false before the first

float distortionBinHertz();
int32_t distortionSnap(int32_t centiHz);    // the nearest bin centre, 0.01 Hz
float distortionSpectrumHertz(uint16_t point);

// the analysis on a synthetic signal: left 1 kHz-ish at -20 dBFS with H3 at
// -60 dBc, right the same with H2 at -80 dBc. An error code (Errors.h)
uint8_t distortionSelfTest(DistortionResult &result);

#endif

// ============================================================================
// ==================================  E O F  =================================
// ============================================================================
