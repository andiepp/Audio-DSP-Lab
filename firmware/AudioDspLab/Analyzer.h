// ****************************************************************************
// ***                  R E S P O N S E   A N A L Y Z E R                   ***
// ****************************************************************************
//  Application 0x04: a sine on K3 and K4; on each channel of the
//  line input K2 a band-pass tracking the sine, the absolute value and a
//  smoothing low-pass, whose output the firmware reads back through the DSP's
//  data capture registers. A level is the sine's peak in dBFS.
//
//  A sweep steps through points spaced evenly on a logarithmic axis; at
//  each it sets the sine and both filters, waits until they settled and
//  averages several readings. It runs from the firmware's loop and never
//  blocks it, so the serial port keeps being answered.
//
//  The last complete sweep can be kept as the reference (NVS); every point
//  is also given relative to it, interpolated on the logarithmic axis.
// ----------------------------------------------------------------------------

#ifndef ANALYZER_H
#define ANALYZER_H

#include <stdint.h>

#define ANALYZER_MAX_POINTS           128     // 3 decades at 40 per decade fit
#define ANALYZER_NO_VALUE             INT32_MIN

enum SweepState : uint8_t {
  SWEEP_IDLE    = 0,    // nothing measured since the application started
  SWEEP_RUNNING = 1,
  SWEEP_DONE    = 2,    // the last sweep completed
  SWEEP_STOPPED = 3,    // the last sweep was stopped, its points so far kept
  SWEEP_FAILED  = 4,    // a DSP access failed
};

struct AnalyzerPoint {
  int32_t centiHz;      // 0.01 Hz
  int32_t levelLeft;    // 0.01 dBFS
  int32_t levelRight;
};

// the application starts: capture registers set, sweep cleared, tone on
bool analyzerBegin(int32_t toneCentiHz);
// the application ends or the DSP is handed over: a running sweep stops
void analyzerEnd();
void analyzerPoll();

// the tone between sweeps; a running sweep takes it back when it ends
bool analyzerSetTone(int32_t centiHz);

// returns an error code (Errors.h); start and stop in 0.01 Hz, start below stop
uint8_t analyzerStartSweep(int32_t startCentiHz, int32_t stopCentiHz, int32_t pointsPerDecade);
void analyzerStopSweep();

SweepState analyzerState();
uint16_t analyzerPointCount();          // measured so far
uint16_t analyzerPointTotal();          // of the sweep running or last run
bool analyzerPoint(uint16_t index, AnalyzerPoint &point);
// the point's levels relative to the reference; ANALYZER_NO_VALUE where
// there is no reference at its frequency
void analyzerRelative(const AnalyzerPoint &point, int32_t &left, int32_t &right);

// keeps the last complete sweep as the reference; returns an error code (Errors.h)
uint8_t analyzerKeepReference();
uint16_t analyzerReferenceCount();
bool analyzerReferencePoint(uint16_t index, AnalyzerPoint &point);

// one reading at the tone playing now (averaged); false on a DSP failure
bool analyzerMeasure(int32_t &left, int32_t &right);
// diagnostic: the raw capture words, 5.19
bool analyzerReadRaw(int32_t &left, int32_t &right);

#endif
