#include "Receiver.h"
#include "AudioOut.h"
#include "Capture.h"
#include <Arduino.h>
#include <math.h>

#define RECEIVER_TASK_STACK           4096
#define RECEIVER_TASK_CORE            0       // the firmware's loop never yields core 1
#define RECEIVER_TASK_PRIORITY        4       // above the generator's and the synthesizer's 3
#define RECEIVER_READ_TIMEOUT_MS      50      // ten blocks: no clock
#define RECEIVER_FULL_SCALE           8388608.0f  // 2^23: the 24-bit sample
// the output ring: two blocks (8 x 64 frames, 10.7 ms). A write returns
// with the ring full, and the next input block may come up to a block
// later: one block of ring would run dry at an unlucky phase
#define RECEIVER_OUT_DMA_BUFFERS      (2 * RECEIVER_BLOCK_FRAMES / 64)

static TaskHandle_t task = nullptr;
static volatile bool stopRequested = false;

// written by the task, read by the loop (a torn read shows one stale field)
static volatile ReceiverStats stats;

// the block's RMS without its DC (the ADCs sit near -0.016 of full scale,
// -36 dBFS, which would hide the signal), from integer sums: double is
// software on the ESP32 and took 0.4 ms a block
// N^2 x variance = N x sum of squares - sum^2, exact in 64 bits (each term
// below 2^62), so a floor of a few counts survives a DC of 134000
static float rmsDb(int64_t sum, int64_t squares) {
  const int64_t scaled = (int64_t)RECEIVER_BLOCK_FRAMES * squares - sum * sum;
  if (scaled <= 0) return -200.0f;
  const float variance = (float)scaled / ((float)RECEIVER_BLOCK_FRAMES * RECEIVER_BLOCK_FRAMES);
  return 10.0f * log10f(variance / (RECEIVER_FULL_SCALE * RECEIVER_FULL_SCALE));
}

static void receiverTask(void *) {
  static int32_t in[2 * RECEIVER_BLOCK_FRAMES];
  static int16_t out[2 * RECEIVER_BLOCK_FRAMES];

  while (!stopRequested) {
    // while the DSP is held in reset or boots its image, no clock comes:
    // the output's DMA plays silence meanwhile (auto_clear)
    if (!captureStreamRead(in, RECEIVER_READ_TIMEOUT_MS)) {
      stats.gaps = stats.gaps + 1;
      continue;
    }
    const uint32_t startedAt = micros();
    int64_t sumI = 0, sumQ = 0, squaresI = 0, squaresQ = 0;   // 256 x 2^46 fits
    for (int n = 0; n < RECEIVER_BLOCK_FRAMES; n++) {
      const int32_t i = in[2 * n] >> 8;           // 24 bits at the top of the slot
      const int32_t q = in[2 * n + 1] >> 8;
      sumI += i;
      sumQ += q;
      squaresI += (int64_t)i * i;
      squaresQ += (int64_t)q * q;
      const int16_t audio = (int16_t)(i >> 8);    // R1: I passed through, 16 bits
      out[2 * n] = audio;
      out[2 * n + 1] = audio;
    }
    const uint32_t took = micros() - startedAt;
    stats.inputDb[0] = rmsDb(sumI, squaresI);
    stats.inputDb[1] = rmsDb(sumQ, squaresQ);
    stats.processUs = took;
    if (took > stats.processMaxUs) stats.processMaxUs = took;
    stats.blocks = stats.blocks + 1;
    audioOutWrite(out, sizeof(out));            // paced by the locked clocks
  }

  task = nullptr;
  vTaskDelete(nullptr);
}

bool receiverBegin() {
  receiverEnd();
  stats.blocks = 0;
  stats.gaps = 0;
  stats.inputDb[0] = stats.inputDb[1] = -200.0f;
  stats.processUs = 0;
  stats.processMaxUs = 0;
  if (!audioOutStart(RECEIVER_OUT_DMA_BUFFERS)) return false;
  if (!captureStreamOpen()) {
    audioOutStop();
    return false;
  }
  stopRequested = false;
  if (xTaskCreatePinnedToCore(receiverTask, "receiver", RECEIVER_TASK_STACK, nullptr,
                              RECEIVER_TASK_PRIORITY, &task, RECEIVER_TASK_CORE) != pdPASS) {
    task = nullptr;
    receiverEnd();
    return false;
  }
  return true;
}

void receiverEnd() {
  if (task != nullptr) {
    stopRequested = true;
    for (int i = 0; i < 50 && task != nullptr; i++) delay(10);   // the task ends itself
  }
  captureStreamClose();
  audioOutStop();
}

bool receiverRunning() {
  return task != nullptr;
}

void receiverGetStats(ReceiverStats &out) {
  out.blocks = stats.blocks;
  out.gaps = stats.gaps;
  out.inputDb[0] = stats.inputDb[0];
  out.inputDb[1] = stats.inputDb[1];
  out.processUs = stats.processUs;
  out.processMaxUs = stats.processMaxUs;
}
