// ****************************************************************************
// ***                        O S C I L L A T O R S                         ***
// ****************************************************************************
//  The band-limited wave shapes the synthesizer and the generator share:
//  phase 0..1, dt the phase increment per sample. Steps are rounded with
//  PolyBLEP, corners (steps in the slope) with PolyBLAMP.
// ----------------------------------------------------------------------------

#ifndef OSCILLATORS_H
#define OSCILLATORS_H

#include <stdint.h>

// the residual that removes a step's aliasing: t is the phase (0..1), dt
// the increment; non-zero only within one sample of the step
static inline float polyBlep(float t, float dt) {
  if (t < dt) {
    t /= dt;
    return t + t - t * t - 1.0f;
  }
  if (t > 1.0f - dt) {
    t = (t - 1.0f) / dt;
    return t * t + t + t + 1.0f;
  }
  return 0.0f;
}

// a rectangle high for the first `width` of the cycle, with both steps
// band-limited and its mean removed (a 25 % pulse would sit at -0.5)
static inline float rectangle(float phase, float dt, float width) {
  float value = phase < width ? 1.0f : -1.0f;
  float fall = phase - width;
  if (fall < 0.0f) fall += 1.0f;
  return value + polyBlep(phase, dt) - polyBlep(fall, dt) + (1.0f - 2.0f * width);
}

// the residual that rounds a corner (a step in the slope): t the phase
// since the corner, dt the increment; the integral of polyBlep's
static inline float polyBlamp(float t, float dt) {
  if (t < dt) {
    float x = t / dt - 1.0f;
    return -x * x * x * (1.0f / 3.0f);
  }
  if (t > 1.0f - dt) {
    float x = (t - 1.0f) / dt + 1.0f;
    return x * x * x * (1.0f / 3.0f);
  }
  return 0.0f;
}

// a triangle falling from the top over 1 - rise of the cycle and rising
// over rise (0.5: a symmetric triangle), both corners band-limited
static inline float triangle(float phase, float dt, float rise) {
  const float fall = 1.0f - rise;
  float value = phase < fall ? 1.0f - 2.0f * phase / fall : -1.0f + 2.0f * (phase - fall) / rise;
  const float corner = (2.0f / rise + 2.0f / fall) * dt;
  float sinceTrough = phase - fall;
  if (sinceTrough < 0.0f) sinceTrough += 1.0f;
  return value - corner * polyBlamp(phase, dt) + corner * polyBlamp(sinceTrough, dt);
}

// white noise from a 32-bit xorshift, -1 .. 1
static inline float noise(uint32_t &state) {
  state ^= state << 13;
  state ^= state >> 17;
  state ^= state << 5;
  return (int32_t)state * (1.0f / 2147483648.0f);
}

#endif
