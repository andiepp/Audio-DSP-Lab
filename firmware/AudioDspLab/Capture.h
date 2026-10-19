// ****************************************************************************
// ***                  S E R I A L   O U T P U T   C A P T U R E           ***
// ****************************************************************************
//  The DSP's serial output (MP6 SDATA_OUT0, MP10 OUTPUT_LRCLK, MP11
//  OUTPUT_BCLK) reaches ESP32 GPIO32, 13 and 19. The ESP32's second I2S
//  port (the first gives the DSP its MCLK) takes it in as a slave, I2S
//  with 32-bit slots, so the DSP drives both clocks. The images' register
//  settings make the DSP's port a master at 96 kHz with 64 BCLK a frame
//  (diag clocks: about 95,980 Hz and 6.14 MHz, timed by the ESP32's own
//  clock). The ground for the distortion measurement: the line input's
//  samples, as the DSP sends them.
//
//  It touches no I2C, so it works in SigmaStudio's service mode too, with
//  whatever program SigmaStudio downloaded.
// ----------------------------------------------------------------------------

#ifndef CAPTURE_H
#define CAPTURE_H

#include <stdint.h>

#define CAPTURE_CHUNK_FRAMES          256     // frames per read (one DMA buffer)
#define CAPTURE_MIN_FRAMES            CAPTURE_CHUNK_FRAMES
#define CAPTURE_MAX_FRAMES            96000   // 1 s: the loop waits meanwhile
#define CAPTURE_DEFAULT_FRAMES        19200   // 75 chunks, 0.2 s

struct CaptureChannel {
  float rmsDb;              // dBFS (full scale = 2^23; a full-scale sine is -3.01)
  float peakDb;             // dBFS
  float hertz;              // from the zero crossings, at the measured rate
};

struct CaptureResult {
  float rate;               // frames per second, timed against the ESP32's clock
  uint32_t frames;          // frames measured
  CaptureChannel left;
  CaptureChannel right;
  uint32_t firstWord;       // the first left slot as received: shows the alignment
};

// receives about `frames` frames (rounded up to whole chunks); false if no
// clock came within CAPTURE_CLOCK_TIMEOUT_MS
bool captureMeasure(uint32_t frames, CaptureResult &result);

// the serial output's clocks, counted on their pins (-1 if the counter
// could not be set up)
void captureClocks(float &lrclkHertz, float &bclkHertz);

#endif

// ============================================================================
// ==================================  E O F  =================================
// ============================================================================
