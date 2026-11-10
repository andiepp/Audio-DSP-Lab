// ****************************************************************************
// ***                           R E C E I V E R                            ***
// ****************************************************************************
//  Application 0x07's audio path (design/receiver.md). The DSP's
//  "Receiver" image sends ADC0 (I) and ADC1 (Q) unprocessed on its serial
//  output (SerialOutL / SerialOutR); the ESP32 takes them in on I2S1
//  (Capture.h, the stream), runs the receiver chain on them (RxChain.h) in
//  blocks of 256 frames (5.33 ms at 48 kHz) and sends the audio back over
//  I2S0 (AudioOut.h), through the image's equalizer and volume to K3, K4
//  and the headphones. I2S0 also makes the DSP's MCLK, from which the DSP
//  clocks its serial output: input and output are sample-locked, nothing
//  drifts.
// ----------------------------------------------------------------------------

#ifndef RECEIVER_H
#define RECEIVER_H

#include <stdint.h>
#include "RxChain.h"

#define RECEIVER_BLOCK_FRAMES         RX_CHAIN_BLOCK      // = CAPTURE_CHUNK_FRAMES

struct ReceiverStats {
  uint32_t blocks;          // blocks processed since the start
  uint32_t gaps;            // reads that timed out (no clock: the DSP in reset or booting)
  float inputDb[2];         // the last block's RMS of I and Q without DC, dBFS (full scale 2^23)
  uint32_t processUs;       // the last block's processing time
  uint32_t processMaxUs;    // the longest since the start (or the last reset)
  float processAvgUs;       // running average (1/64)
};

// the I2S output (and MCLK), the chain with this configuration, the input
// stream, the task
bool receiverBegin(const RxConfig &config);
void receiverEnd();                         // idempotent
bool receiverRunning();
void receiverGetStats(ReceiverStats &stats);
void receiverResetMax();                    // the longest processing time starts again
float receiverCpuPercent();                 // the average processing time, % of a block

#endif

// ============================================================================
// ==================================  E O F  =================================
// ============================================================================
