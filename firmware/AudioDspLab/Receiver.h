// ****************************************************************************
// ***                           R E C E I V E R                            ***
// ****************************************************************************
//  Application 0x07's audio path (design/receiver.md). The DSP's
//  "Receiver" image sends ADC0 (I) and ADC1 (Q) unprocessed on its serial
//  output (SerialOutL / SerialOutR); the ESP32 takes them in on I2S1
//  (Capture.h, the stream), processes them in blocks of 256 frames
//  (5.33 ms at 48 kHz) and sends the audio back over I2S0 (AudioOut.h),
//  through the image's equalizer and volume to K3, K4 and the headphones.
//  I2S0 also makes the DSP's MCLK, from which the DSP clocks its serial
//  output: input and output are sample-locked, nothing drifts.
//
//  Step R1: the processing is a pass-through, I on both outputs.
// ----------------------------------------------------------------------------

#ifndef RECEIVER_H
#define RECEIVER_H

#include <stdint.h>

#define RECEIVER_BLOCK_FRAMES         256     // = CAPTURE_CHUNK_FRAMES

struct ReceiverStats {
  uint32_t blocks;          // blocks processed since the start
  uint32_t gaps;            // reads that timed out (no clock: the DSP in reset or booting)
  float inputDb[2];         // the last block's RMS of I and Q, dBFS (full scale 2^23)
  uint32_t processUs;       // the last block's processing time
  uint32_t processMaxUs;    // the longest since the start
};

bool receiverBegin();                       // the I2S output (and MCLK), the input stream, the task
void receiverEnd();                         // idempotent
bool receiverRunning();
void receiverGetStats(ReceiverStats &stats);

#endif

// ============================================================================
// ==================================  E O F  =================================
// ============================================================================
