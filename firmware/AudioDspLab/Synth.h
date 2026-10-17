// ****************************************************************************
// ***                        S Y N T H E S I Z E R                         ***
// ****************************************************************************
//  Application 0x06: one voice made on the ESP32,
//  played from the MIDI input K5, sent to the DSP over I2S.
//
//  The voice: three oscillators, each sine, triangle,
//  sawtooth, square or pulse (25 %), with an octave and, for 2 and 3, a
//  detune; sawtooth, square and pulse band-limited with PolyBLEP. A mixer
//  sets each oscillator's level and white noise's; one source at 100 %
//  plays at -6 dBFS, and the sum above that passes a soft limiter rather
//  than clip.
//
//  Shaping: an amplitude envelope, attack linear, decay and
//  release exponential (within 1 % of their target at the time set),
//  single trigger as the MiniMoog's: a key pressed while another is held
//  changes the pitch without starting the envelope again. Last-note
//  priority: a new key takes over, releasing it returns to a key still
//  held. LFO 1 bends the pitch (vibrato), scaled by the mod strip
//  (controller 1) as the MiniMoog's modulation wheel; LFO 2 the amplitude
//  (tremolo).
//
//  Filter: a four-pole ladder low-pass between the mixer and
//  the amplitude envelope, in zero-delay-feedback form with its input
//  saturated (tanh), so emphasis rings up to self-oscillation as the
//  MiniMoog's. Its cutoff is computed every sample from the cutoff set,
//  the contour (a second envelope started with the first, its release the
//  decay time, up to +5 octaves) and keyboard tracking around C4.
//
//  Performance: glide between notes, pitch bend from the
//  KeyStep's pitch strip, velocity to loudness and to brightness, LFO 1 to
//  the filter scaled by the mod strip. The filter's cutoff is computed
//  every fourth sample.
//
//  Wave shapes: square and pulse are one
//  rectangle with a width per oscillator (5 to 95 %), P1 playing oscillator
//  1's; the triangle has a symmetry (the rise's share, 10 to 90 %), its
//  corners band-limited with PolyBLAMP. Each LFO has a width for its square
//  and a symmetry for its triangle (0 and 100 % are ramps). LFO 2 moves
//  every rectangle's width (pulse-width modulation) by the depth set.
//
//  An audio task on core 0 computes 48 kHz, 16-bit samples and writes the
//  same signal to both I2S channels; its I2S channel clocks the DSP (MCLK
//  256 x 48 kHz), as the radio's does. The firmware's loop on core 1 reads
//  MIDI (synthPoll) and sets the voice; the task only reads what the loop
//  set. Neither touches I2C: the volume is the DSP's, set by the loop.
//
//  MIDI: Serial1 on GPIO38 at 31250 8N1, running status honoured, realtime
//  bytes anywhere. Notes on the selected channel; note on with velocity 0
//  is note off; controllers 120 and 123 silence the voice.
// ----------------------------------------------------------------------------

#ifndef SYNTH_H
#define SYNTH_H

#include <stdint.h>

#define SYNTH_SAMPLE_RATE             48000
#define SYNTH_OSCILLATORS             3

// indices as the firmware stores them: 0 to 2, 3 since 1.5;
// the rectangle was the square, and 1.5 to 1.9's pulse (4) is the
// rectangle at 25 % since 1.10
enum SynthWaveform : uint8_t {
  SYNTH_SINE      = 0,
  SYNTH_SAWTOOTH  = 1,
  SYNTH_RECTANGLE = 2,
  SYNTH_TRIANGLE  = 3,
};
#define SYNTH_WAVEFORM_LAST           SYNTH_TRIANGLE
#define SYNTH_LFOS                    2       // 0 vibrato, 1 tremolo

enum SynthLfoWaveform : uint8_t {
  SYNTH_LFO_SINE     = 0,
  SYNTH_LFO_TRIANGLE = 1,
  SYNTH_LFO_SQUARE   = 2,
};

enum SynthEnvelopeStage : uint8_t {
  SYNTH_IDLE    = 0,
  SYNTH_ATTACK  = 1,
  SYNTH_DECAY   = 2,
  SYNTH_SUSTAIN = 3,
  SYNTH_RELEASE = 4,
};

struct SynthStatus {
  uint8_t note;                       // the note sounding or ramping down, 0xFF: none
  uint8_t held;                       // keys held on the channel
  bool gate;                          // a key is held
  uint8_t channel;                    // 1 .. 16
  uint32_t noteMessages;              // note on/off on the channel
  uint32_t otherChannel;              // note on/off on other channels
  uint32_t bytes;                     // MIDI bytes received
  uint32_t uartErrors;
};

// takes the DSP clock (I2S at 48 kHz), opens MIDI and starts the audio task
bool synthBegin();
// stops the task, MIDI and the I2S channel; idempotent
void synthEnd();
// the firmware's loop: reads MIDI and sets the voice
void synthPoll();

// oscillator 0 .. 2
void synthSetWaveform(uint8_t oscillator, uint8_t waveform);
void synthSetOctave(uint8_t oscillator, int8_t octave);      // -2 .. 2
void synthSetDetune(uint8_t oscillator, int16_t cents);      // -700 .. 700
void synthSetLevel(uint8_t oscillator, uint8_t percent);     // 0 .. 100
void synthSetNoise(uint8_t percent);                         // 0 .. 100
void synthSetAttack(uint16_t milliseconds);                  // 1 .. 10000
void synthSetDecay(uint16_t milliseconds);
void synthSetSustain(uint8_t percent);                       // 0 .. 100
void synthSetRelease(uint16_t milliseconds);
// lfo 0 vibrato (depth in cents, 0 .. 1200), 1 tremolo (depth in %, 0 .. 100)
void synthSetLfoRate(uint8_t lfo, uint16_t centiHertz);      // 10 .. 2000
void synthSetLfoWaveform(uint8_t lfo, uint8_t waveform);
void synthSetLfoDepth(uint8_t lfo, uint16_t depth);
// wave shapes
void synthSetWidth(uint8_t oscillator, uint8_t percent);     // 5 .. 95, the rectangle
void synthSetSymmetry(uint8_t oscillator, uint8_t percent);  // 10 .. 90, the triangle's rise
void synthSetLfoWidth(uint8_t lfo, uint8_t percent);         // 5 .. 95
void synthSetLfoSymmetry(uint8_t lfo, uint8_t percent);      // 0 .. 100
void synthSetPwmDepth(uint8_t percent);                      // 0 .. 100, 100 = +-45 points
// the mod strip's last position (0 .. 127) and the envelope's stage
void synthModulation(uint8_t &modStrip, uint8_t &stage);
// the filter
void synthSetCutoff(uint16_t hertz);                         // 20 .. 20000
void synthSetEmphasis(uint8_t percent);                      // 0 .. 100, 100 rings
void synthSetContourAmount(uint8_t percent);                 // 0 .. 100, 100 = +5 octaves
void synthSetContourAttack(uint16_t milliseconds);           // 1 .. 10000
void synthSetContourDecay(uint16_t milliseconds);            // also its release
void synthSetContourSustain(uint8_t percent);                // 0 .. 100
void synthSetKeyTracking(uint8_t percent);                   // 0 .. 100
// performance
void synthSetGlide(uint16_t milliseconds);                   // 0 .. 5000, 0 off
void synthSetBendRange(uint8_t semitones);                   // 0 .. 12
void synthSetVelocityLoudness(uint8_t percent);              // 0 .. 100
void synthSetVelocityBrightness(uint8_t percent);            // 0 .. 100, 100 = +3 octaves
void synthSetFilterLfo(uint8_t percent);                     // 0 .. 100, 100 = +-2 octaves
// the audio task's load since the last call, in 0.1 % of a block's time:
// the average and the peak block; zero when it does not run
void synthLoad(uint16_t &average, uint16_t &peak);
void synthSetChannel(uint8_t channel);   // 1 .. 16; silences the voice
void synthStatus(SynthStatus &status);

#endif
