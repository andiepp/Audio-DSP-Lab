#include "Generator.h"
#include "AudioOut.h"
#include "Oscillators.h"
#include <Arduino.h>
#include <math.h>

#define GENERATOR_BLOCK_FRAMES        64
#define GENERATOR_FADE_SAMPLES        240     // 5 ms at 48 kHz
#define GENERATOR_TASK_STACK          4096
#define GENERATOR_TASK_CORE           0       // the firmware's loop never yields core 1
#define GENERATOR_TASK_PRIORITY       3
#define GENERATOR_PHASE_ONE           4294967296.0  // 2^32: one cycle of the phase

// set by the firmware's loop, read by the task
static volatile uint8_t wanted = GENERATOR_SINE;
static volatile uint32_t increment = 0;         // phase step per sample, 2^32 a cycle
static volatile float width = 0.5f;

static TaskHandle_t task = nullptr;
static volatile bool stopRequested = false;

// one sample of a shape at a phase (0..1) and phase step dt
static float shapeAt(uint8_t shape, float phase, float dt, float share, uint32_t &noiseState) {
  switch (shape) {
    case GENERATOR_RECTANGLE: {
      // rectangle() removes the mean, which lifts a narrow pulse up to
      // 2 - 2 x width: scaled back so it peaks at full scale
      const float offset = fabsf(1.0f - 2.0f * share);
      return rectangle(phase, dt, share) / (1.0f + offset);
    }
    case GENERATOR_TRIANGLE: {
      // each slope at least two samples long, so the corners stay rounded
      float rise = share;
      if (4.0f * dt >= 1.0f) rise = 0.5f;
      else rise = constrain(rise, 2.0f * dt, 1.0f - 2.0f * dt);
      return triangle(phase, dt, rise);
    }
    case GENERATOR_SAW:
      return 2.0f * phase - 1.0f - polyBlep(phase, dt);
    case GENERATOR_INVERSE_SAW:
      return 1.0f - 2.0f * phase + polyBlep(phase, dt);
    case GENERATOR_NOISE:
      return noise(noiseState);
    default:
      return sinf(2.0f * (float)M_PI * phase);
  }
}

static void generatorTask(void *) {
  static int16_t frames[GENERATOR_BLOCK_FRAMES * 2];
  uint32_t phase = 0;
  uint32_t noiseState = 0x12345678;
  uint8_t playing = wanted;
  int fade = GENERATOR_FADE_SAMPLES;            // 0 silent .. GENERATOR_FADE_SAMPLES full

  while (!stopRequested) {
    const uint32_t step = increment;
    const float dt = (float)(step / GENERATOR_PHASE_ONE);
    const float share = width;
    const uint8_t asked = wanted;
    for (int i = 0; i < GENERATOR_BLOCK_FRAMES; i++) {
      // a new waveform: fade out, switch at silence, fade in
      if (asked != playing) {
        if (fade > 0) fade--;
        else playing = asked;
      } else if (fade < GENERATOR_FADE_SAMPLES) {
        fade++;
      }
      const float value = shapeAt(playing, (float)(phase / GENERATOR_PHASE_ONE), dt, share, noiseState);
      const float out = value * fade * (1.0f / GENERATOR_FADE_SAMPLES);
      const int16_t sample = (int16_t)constrain(lrintf(out * 32767.0f), -32767L, 32767L);
      frames[2 * i] = sample;
      frames[2 * i + 1] = sample;
      phase += step;                            // wraps at 2^32: exactly one cycle
    }
    audioOutWrite(frames, sizeof(frames));
  }

  task = nullptr;
  vTaskDelete(nullptr);
}

bool generatorBegin() {
  generatorEnd();
  if (!audioOutStart()) return false;
  stopRequested = false;
  if (xTaskCreatePinnedToCore(generatorTask, "generator", GENERATOR_TASK_STACK, nullptr,
                              GENERATOR_TASK_PRIORITY, &task, GENERATOR_TASK_CORE) != pdPASS) {
    task = nullptr;
    generatorEnd();
    return false;
  }
  return true;
}

void generatorEnd() {
  if (task != nullptr) {
    stopRequested = true;
    for (int i = 0; i < 50 && task != nullptr; i++) delay(10);   // the task ends itself
  }
  audioOutStop();
}

void generatorSetWaveform(uint8_t value) {
  if (value <= GENERATOR_WAVEFORM_LAST) wanted = value;
}

void generatorSetFrequency(int32_t centiHertz) {
  increment = (uint32_t)llround(centiHertz / 100.0 / AUDIO_OUT_RATE * GENERATOR_PHASE_ONE);
}

void generatorSetWidth(uint8_t percent) {
  if (percent >= 5 && percent <= 95) width = percent / 100.0f;
}
