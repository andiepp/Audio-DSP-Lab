// ****************************************************************************
// ***                           G E N E R A T O R                          ***
// ****************************************************************************
//  Application 0x01's signal, made on the ESP32 and sent to the DSP over
//  I2S (AudioOut) as the synthesizer's is: one oscillator, sine,
//  rectangle, triangle, sawtooth, inverse sawtooth or white noise, at
//  20 Hz to 20 kHz. The rectangle's width is its high part (5 to 95 %),
//  the triangle's its rise; rectangle, triangle and the sawtooths are
//  band-limited (Oscillators.h). Every shape peaks at full scale; the
//  DSP sets the amplitude. A 32-bit phase keeps the frequency exact to
//  0.01 Hz, and a waveform change fades out and in (5 ms each way).
// ----------------------------------------------------------------------------

#ifndef GENERATOR_H
#define GENERATOR_H

#include <stdint.h>

// indices as the protocol lists them (ParamInfo.cpp)
enum GeneratorWaveform : uint8_t {
  GENERATOR_SINE        = 0,
  GENERATOR_RECTANGLE   = 1,
  GENERATOR_TRIANGLE    = 2,
  GENERATOR_SAW         = 3,
  GENERATOR_INVERSE_SAW = 4,
  GENERATOR_NOISE       = 5,
};
#define GENERATOR_WAVEFORM_LAST       GENERATOR_NOISE

bool generatorBegin();                          // starts the I2S output and the task
void generatorEnd();                            // idempotent
void generatorSetWaveform(uint8_t waveform);
void generatorSetFrequency(int32_t centiHertz); // 2000 .. 2000000
void generatorSetWidth(uint8_t percent);        // 5 .. 95

#endif
