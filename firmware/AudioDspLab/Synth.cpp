#include "Synth.h"
#include "Config.h"
#include <Arduino.h>
#include <driver/i2s_std.h>
#include <math.h>

// one source at 100 % plays at +-0.5 of full scale: -6 dBFS, as firmware
// 1.4's single oscillator; the volume is the DSP's
#define SYNTH_SOURCE_GAIN             0.5f
#define SYNTH_LIMIT_KNEE              0.5f                // the limiter is linear below this
// wave shapes: widths and the PWM's swing, in parts of the cycle
#define SYNTH_WIDTH_LOWEST            0.05f
#define SYNTH_WIDTH_HIGHEST           0.95f
#define SYNTH_PWM_SWING               0.45f
// decay and release come within 1 % of their target at the time set
#define SYNTH_TIME_CONSTANTS          4.6f                // ln(100)
#define SYNTH_SETTLED                 0.0005f             // -66 dB: release ends, decay settles
// filter: emphasis 100 % sits just past the ladder's self-oscillation (4)
#define SYNTH_EMPHASIS_MAXIMUM        4.2f
#define SYNTH_CONTOUR_OCTAVES         5.0f
#define SYNTH_CUTOFF_LOWEST           10.0f
#define SYNTH_CUTOFF_HIGHEST          (0.45f * SYNTH_SAMPLE_RATE)
#define SYNTH_MIDDLE_C                261.6256f
// performance
#define SYNTH_CUTOFF_INTERVAL         4                   // samples between two cutoff computations
#define SYNTH_BRIGHTNESS_OCTAVES      3.0f
#define SYNTH_FILTER_LFO_OCTAVES      2.0f
#define SYNTH_BLOCK_FRAMES            64
// 4 x 64 frames queued: about 5 ms from a key to the DSP (the radio's
// audio-tools default queue holds close to 100 ms, too long to play)
#define SYNTH_DMA_BUFFERS             4
#define SYNTH_DMA_FRAMES              64
#define SYNTH_TASK_STACK              4096
#define SYNTH_TASK_CORE               0       // the firmware's loop never yields core 1
#define SYNTH_TASK_PRIORITY           3
#define SYNTH_HELD_MAX                16
#define MIDI_BAUD_RATE                31250
#define MIDI_RX_BUFFER                1024

// --- voice: set by the firmware's loop, read by the audio task ---------------------

static volatile float notePitch = 8.7814f;                      // the note's log2(Hz), A4
static volatile bool gate = false;
static volatile bool silent = true;                             // set by the task

// per oscillator: waveform, the pitch ratio from octave and detune, the level
static volatile uint8_t waveforms[SYNTH_OSCILLATORS] = { SYNTH_SAWTOOTH, SYNTH_SAWTOOTH, SYNTH_SAWTOOTH };
static volatile float ratios[SYNTH_OSCILLATORS] = { 1.0f, 1.0f, 1.0f };
static volatile float levels[SYNTH_OSCILLATORS] = { 1.0f, 0.0f, 0.0f };
static volatile float noiseLevel = 0.0f;
static volatile float widths[SYNTH_OSCILLATORS] = { 0.5f, 0.5f, 0.5f };      // the rectangle's high part
static volatile float symmetries[SYNTH_OSCILLATORS] = { 0.5f, 0.5f, 0.5f };  // the triangle's rise
static volatile float pwmDepth = 0.0f;                                        // 0 .. SYNTH_PWM_SWING
static int8_t octaves[SYNTH_OSCILLATORS] = { 0, 0, 0 };          // the loop's copies, for the ratio
static int16_t detunes[SYNTH_OSCILLATORS] = { 0, 0, 0 };

// envelope: steps and coefficients per sample, computed by the loop
static volatile uint32_t triggers = 0;                          // envelope starts asked for
static volatile float attackStep = 1.0f / (5 * 48);
static volatile float decayCoefficient = 0.0f;
static volatile float sustainLevel = 1.0f;
static volatile float releaseCoefficient = 0.0f;
static volatile uint8_t stageNow = SYNTH_IDLE;                  // set by the task

// LFOs: cycles per sample, waveform, depth (vibrato in octaves, tremolo 0 .. 1)
static volatile float lfoIncrements[SYNTH_LFOS] = { 5.0f / SYNTH_SAMPLE_RATE, 4.0f / SYNTH_SAMPLE_RATE };
static volatile uint8_t lfoWaveforms[SYNTH_LFOS] = { SYNTH_LFO_TRIANGLE, SYNTH_LFO_SINE };
static volatile float lfoWidths[SYNTH_LFOS] = { 0.5f, 0.5f };
static volatile float lfoSymmetries[SYNTH_LFOS] = { 0.5f, 0.5f };
static volatile float vibratoOctaves = 50.0f / 1200.0f;
static volatile float tremoloDepth = 0.0f;
static volatile uint8_t modStrip = 0;                           // controller 1, 0 .. 127

// filter: the cutoff in octaves above 1 Hz, the feedback, the contour
static volatile float cutoffOctaves = 14.2877f;                 // log2(20000)
static volatile float emphasis = 0.0f;                          // ladder feedback k, 0 .. 4.2
static volatile float contourOctaves = 0.0f;
static volatile float contourAttackStep = 1.0f / (5 * 48);
static volatile float contourDecayCoefficient = 0.0f;
static volatile float contourSustain = 0.5f;
static volatile float keyTracking = 0.0f;                       // 0 .. 1

// performance: glide per block, bend in octaves, velocity, LFO 1 to the filter
static volatile float glideCoefficient = 0.0f;                  // 0: jump to the note
static volatile float bendPosition = 0.0f;                      // the pitch strip, -1 .. 1
static volatile float bendRange = 2.0f / 12.0f;                 // octaves at the strip's end
static volatile float velocity = 1.0f;                          // the last key's, 0 .. 1
static volatile float velocityLoudness = 0.0f;
static volatile float velocityBrightness = 0.0f;                // octaves at full velocity
static volatile float filterLfoOctaves = 0.0f;                  // at full mod strip

// load, written by the task: cycles of the blocks since the last read
static volatile uint32_t loadBlocks = 0;
static volatile uint64_t loadCycles = 0;
static volatile uint32_t loadPeakCycles = 0;

static uint8_t currentNote = 0xFF;
static uint8_t held[SYNTH_HELD_MAX];                            // oldest first
static uint8_t heldCount = 0;
static uint8_t channel = 15;                                    // 1 .. 16

// --- MIDI parser (the monitor's) ----------------------------------

static uint8_t runningStatus = 0;
static uint8_t data[2];
static uint8_t dataCount = 0;
static uint8_t dataNeeded = 0;
static bool midiOpen = false;

static uint32_t noteMessages = 0;
static uint32_t otherChannel = 0;
static uint32_t midiBytes = 0;
static uint32_t uartErrors = 0;                                 // written by the UART event task

// --- task and I2S -----------------------------------------------------------------

static i2s_chan_handle_t txChannel = NULL;
static TaskHandle_t task = nullptr;
static volatile bool stopRequested = false;

// ----------------------------------------------------------------------------
//  Oscillator
// ----------------------------------------------------------------------------

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

static inline float oscillator(float phase, float dt, uint8_t shape, float width, float rise) {
  switch (shape) {
    case SYNTH_SAWTOOTH:
      return 2.0f * phase - 1.0f - polyBlep(phase, dt);
    case SYNTH_RECTANGLE:
      return rectangle(phase, dt, width);
    case SYNTH_TRIANGLE:
      return triangle(phase, dt, rise);
    default:
      return sinf(2.0f * (float)M_PI * phase);
  }
}

// white noise from a 32-bit xorshift, -1 .. 1
static inline float noise(uint32_t &state) {
  state ^= state << 13;
  state ^= state >> 17;
  state ^= state << 5;
  return (int32_t)state * (1.0f / 2147483648.0f);
}

// linear up to the knee, then bending toward 2 x the knee: one source at
// 100 % passes untouched, several saturate gently instead of clipping
static inline float softLimit(float x) {
  float magnitude = fabsf(x);
  if (magnitude <= SYNTH_LIMIT_KNEE) return x;
  float bent = SYNTH_LIMIT_KNEE + SYNTH_LIMIT_KNEE * tanhf((magnitude - SYNTH_LIMIT_KNEE) / SYNTH_LIMIT_KNEE);
  return x < 0.0f ? -bent : bent;
}

// the LFO's triangle falls over 1 - rise and rises over rise: 0 is a
// falling ramp, 1 a rising one, 0.5 a symmetric triangle; the square
// is high for width of the cycle
static inline float lfoValue(float phase, uint8_t shape, float width, float rise) {
  switch (shape) {
    case SYNTH_LFO_TRIANGLE: {
      if (rise <= 0.0f) return 1.0f - 2.0f * phase;
      if (rise >= 1.0f) return -1.0f + 2.0f * phase;
      const float fall = 1.0f - rise;
      return phase < fall ? 1.0f - 2.0f * phase / fall : -1.0f + 2.0f * (phase - fall) / rise;
    }
    case SYNTH_LFO_SQUARE:   return phase < width ? 1.0f : -1.0f;
    default:                 return sinf(2.0f * (float)M_PI * phase);
  }
}

static inline float advance(float phase, float step) {
  phase += step;
  return phase >= 1.0f ? phase - floorf(phase) : phase;
}

// the tremolo's gain at an LFO value: 1 at the top, 1 - depth at the bottom
static inline float tremoloGain(float value, float depth) {
  return 1.0f - depth * 0.5f * (1.0f - value);
}

// one envelope: attack linear, decay and release exponential
struct Envelope {
  float level;
  uint8_t stage;
};

static inline void stepEnvelope(Envelope &e, float attack, float decay, float sustain, float release) {
  switch (e.stage) {
    case SYNTH_ATTACK:
      e.level += attack;
      if (e.level >= 1.0f) {
        e.level = 1.0f;
        e.stage = SYNTH_DECAY;
      }
      break;
    case SYNTH_DECAY:
    case SYNTH_SUSTAIN:
      // a sustain changed while a key is held is followed at the decay's pace
      e.level = sustain + (e.level - sustain) * decay;
      if (e.stage == SYNTH_DECAY && fabsf(e.level - sustain) < SYNTH_SETTLED) e.stage = SYNTH_SUSTAIN;
      break;
    case SYNTH_RELEASE:
      e.level *= release;
      if (e.level < SYNTH_SETTLED) {
        e.level = 0.0f;
        e.stage = SYNTH_IDLE;
      }
      break;
    default:
      e.level = 0.0f;
      break;
  }
}

// a rational tanh, within 2 % up to 3 and exactly 1 beyond: cheaper than tanhf
static inline float fastTanh(float x) {
  if (x > 3.0f) return 1.0f;
  if (x < -3.0f) return -1.0f;
  float x2 = x * x;
  return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

// the ladder: four topology-preserving one-poles in series, the feedback
// from the last resolved without a delay (Zavalishin), the input to the
// stages saturated. g = tan(pi fc / fs); k the feedback, 4 the onset of
// self-oscillation.
struct Ladder {
  float s[4];
};

static inline float ladderStep(Ladder &f, float x, float g, float k) {
  const float b = 1.0f / (1.0f + g);
  const float G = g * b;
  // what the output would be from the stages' states alone
  const float sigma = ((G * f.s[0] + f.s[1]) * G + f.s[2]) * G * b + f.s[3] * b;
  const float G4 = (G * G) * (G * G);
  // two times tanh of half: near linear at the mixer's -6 dBFS, saturating above
  float u = 2.0f * fastTanh(0.5f * (x - k * sigma) / (1.0f + k * G4));
  for (int i = 0; i < 4; i++) {
    float v = (u - f.s[i]) * G;
    float y = v + f.s[i];
    f.s[i] = y + v;
    u = y;
  }
  return u;
}

static void audioTask(void *) {
  static int16_t frames[SYNTH_BLOCK_FRAMES * 2];
  float phases[SYNTH_OSCILLATORS] = { 0.0f, 0.0f, 0.0f };
  float lfoPhases[SYNTH_LFOS] = { 0.0f, 0.0f };
  Envelope amp = { 0.0f, SYNTH_IDLE };
  Envelope contour = { 0.0f, SYNTH_IDLE };
  Ladder filter = { { 0.0f, 0.0f, 0.0f, 0.0f } };
  uint32_t seenTriggers = triggers;
  float tremoloBefore = 1.0f;
  float loudnessBefore = 1.0f;
  float pitch = notePitch;                  // log2(Hz), gliding toward notePitch
  uint32_t noiseState = 0x12345678;

  while (!stopRequested) {
    // what the loop set, taken once per block (1.3 ms)
    const uint32_t blockStart = ESP.getCycleCount();
    uint32_t asked = triggers;
    if (asked != seenTriggers) {
      seenTriggers = asked;
      amp.stage = SYNTH_ATTACK;             // from where the level is: no click
      contour.stage = SYNTH_ATTACK;
    } else if (!gate) {
      if (amp.stage != SYNTH_IDLE && amp.stage != SYNTH_RELEASE) amp.stage = SYNTH_RELEASE;
      if (contour.stage != SYNTH_IDLE && contour.stage != SYNTH_RELEASE) contour.stage = SYNTH_RELEASE;
    }
    const float attack = attackStep;
    const float decay = decayCoefficient;
    const float sustain = sustainLevel;
    const float release = releaseCoefficient;
    const float contourAttack = contourAttackStep;
    const float contourDecay = contourDecayCoefficient;
    const float contourLevel = contourSustain;
    const float k = emphasis;
    // the loss of passband that feedback causes, partly made up
    const float makeUp = 1.0f + 0.25f * k;

    // LFOs once per block: the vibrato holds its pitch for the block, the
    // tremolo's gain moves in a straight line across it
    const float blockCycles = (float)SYNTH_BLOCK_FRAMES;
    lfoPhases[0] = advance(lfoPhases[0], lfoIncrements[0] * blockCycles);
    lfoPhases[1] = advance(lfoPhases[1], lfoIncrements[1] * blockCycles);
    const float wheel = modStrip / 127.0f;
    const float lfo1 = lfoValue(lfoPhases[0], lfoWaveforms[0], lfoWidths[0], lfoSymmetries[0]);
    const float lfo2 = lfoValue(lfoPhases[1], lfoWaveforms[1], lfoWidths[1], lfoSymmetries[1]);
    const float tremoloAfter = tremoloGain(lfo2, tremoloDepth);
    // pulse-width modulation: LFO 2 moves every rectangle's width, once per block
    const float pwm = pwmDepth * lfo2;
    const float tremoloStep = (tremoloAfter - tremoloBefore) / SYNTH_BLOCK_FRAMES;
    float tremolo = tremoloBefore;
    tremoloBefore = tremoloAfter;
    // soft keys softer: the last key's velocity, in a straight line across the block
    const float key = velocity;
    const float loudnessAfter = 1.0f - velocityLoudness * (1.0f - key);
    const float loudnessStep = (loudnessAfter - loudnessBefore) / SYNTH_BLOCK_FRAMES;
    float loudness = loudnessBefore;
    loudnessBefore = loudnessAfter;

    // glide: the pitch comes toward the note's as the MiniMoog's does
    const float target = notePitch;
    const float glide = glideCoefficient;
    pitch = glide > 0.0f ? target + (pitch - target) * glide : target;
    // the pitch strip and the vibrato bend all three oscillators
    const float base = exp2f(pitch + bendPosition * bendRange + vibratoOctaves * wheel * lfo1) / SYNTH_SAMPLE_RATE;
    // the cutoff before the contour: the set cutoff, moved with the key (as
    // it glides, not as it bends), by the key's velocity and by LFO 1
    const float tracked = cutoffOctaves
                          + keyTracking * (pitch - log2f(SYNTH_MIDDLE_C))
                          + velocityBrightness * key
                          + filterLfoOctaves * wheel * lfo1;
    const float contourRange = contourOctaves;
    float dts[SYNTH_OSCILLATORS];
    uint8_t shapes[SYNTH_OSCILLATORS];
    float gains[SYNTH_OSCILLATORS];
    float blockWidths[SYNTH_OSCILLATORS];
    float rises[SYNTH_OSCILLATORS];
    for (int o = 0; o < SYNTH_OSCILLATORS; o++) {
      dts[o] = base * ratios[o];
      shapes[o] = waveforms[o];
      blockWidths[o] = fminf(fmaxf(widths[o] + pwm, SYNTH_WIDTH_LOWEST), SYNTH_WIDTH_HIGHEST);
      rises[o] = symmetries[o];
      // an oscillator at or above half the sample rate has nothing to give
      gains[o] = dts[o] < 0.5f ? levels[o] : 0.0f;
    }
    const float noiseGain = noiseLevel;
    float g = 0.0f;

    for (int i = 0; i < SYNTH_BLOCK_FRAMES; i++) {
      stepEnvelope(amp, attack, decay, sustain, release);
      // the contour releases with its decay time, as the MiniMoog's with the decay switch on
      stepEnvelope(contour, contourAttack, contourDecay, contourLevel, contourDecay);
      tremolo += tremoloStep;
      loudness += loudnessStep;
      int16_t sample = 0;
      if (amp.level > 0.0f) {
        float mix = noiseGain > 0.0f ? noiseGain * noise(noiseState) : 0.0f;
        for (int o = 0; o < SYNTH_OSCILLATORS; o++) {
          if (gains[o] > 0.0f) mix += gains[o] * oscillator(phases[o], dts[o], shapes[o], blockWidths[o], rises[o]);
        }
        // the cutoff every fourth sample (12 kHz): exp2f and tanf were most of
        // the filter's cost
        if ((i % SYNTH_CUTOFF_INTERVAL) == 0 || g == 0.0f) {
          float cutoff = exp2f(tracked + contourRange * contour.level);
          cutoff = fminf(fmaxf(cutoff, SYNTH_CUTOFF_LOWEST), SYNTH_CUTOFF_HIGHEST);
          g = tanf((float)M_PI * cutoff / SYNTH_SAMPLE_RATE);
        }
        float out = ladderStep(filter, softLimit(mix * SYNTH_SOURCE_GAIN), g, k) * makeUp;
        out *= amp.level * tremolo * loudness;
        out = fminf(fmaxf(out, -1.0f), 1.0f);
        sample = (int16_t)lrintf(out * 32767.0f);
      } else {
        // silent: the next note starts the filter from rest, with nothing left ringing
        filter.s[0] = filter.s[1] = filter.s[2] = filter.s[3] = 0.0f;
      }
      // the oscillators run free, as the MiniMoog's
      for (int o = 0; o < SYNTH_OSCILLATORS; o++) {
        phases[o] += dts[o];
        if (phases[o] >= 1.0f) phases[o] -= floorf(phases[o]);
      }
      frames[2 * i] = sample;
      frames[2 * i + 1] = sample;
    }
    silent = amp.stage == SYNTH_IDLE;
    stageNow = amp.stage;
    // the block's computation, without the wait for the DMA below
    const uint32_t cycles = ESP.getCycleCount() - blockStart;
    loadCycles = loadCycles + cycles;
    loadBlocks = loadBlocks + 1;
    if (cycles > loadPeakCycles) loadPeakCycles = cycles;
    size_t written;
    i2s_channel_write(txChannel, frames, sizeof(frames), &written, pdMS_TO_TICKS(100));
  }

  task = nullptr;
  vTaskDelete(nullptr);
}

static bool startI2s() {
  i2s_chan_config_t channelConfig = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
  channelConfig.auto_clear = true;          // silence, not the last block, if the task lags
  channelConfig.dma_desc_num = SYNTH_DMA_BUFFERS;
  channelConfig.dma_frame_num = SYNTH_DMA_FRAMES;
  if (i2s_new_channel(&channelConfig, &txChannel, NULL) != ESP_OK) {
    txChannel = NULL;
    return false;
  }

  i2s_std_config_t config = {
    .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(SYNTH_SAMPLE_RATE),
    .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
    .gpio_cfg = {
      .mclk = (gpio_num_t)PIN_MCLK,
      .bclk = (gpio_num_t)PIN_I2S_BCLK,
      .ws   = (gpio_num_t)PIN_I2S_LRCLK,
      .dout = (gpio_num_t)PIN_I2S_DATA,
      .din  = I2S_GPIO_UNUSED,
      .invert_flags = { .mclk_inv = false, .bclk_inv = false, .ws_inv = false },
    },
  };
  config.clk_cfg.clk_src = I2S_CLK_SRC_APLL;                 // exact 12.288 MHz
  config.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_256;      // ADAU1701 PLL 256 x fs

  if (i2s_channel_init_std_mode(txChannel, &config) != ESP_OK ||
      i2s_channel_enable(txChannel) != ESP_OK) {
    i2s_del_channel(txChannel);
    txChannel = NULL;
    return false;
  }
  return true;
}

static void stopI2s() {
  if (txChannel == NULL) return;
  i2s_channel_disable(txChannel);
  i2s_del_channel(txChannel);
  txChannel = NULL;
}

// ----------------------------------------------------------------------------
//  Voice
// ----------------------------------------------------------------------------

static void play(uint8_t note) {
  currentNote = note;
  notePitch = log2f(440.0f) + (note - 69) / 12.0f;
  gate = true;
}

static void removeHeld(uint8_t note) {
  for (uint8_t i = 0; i < heldCount; i++) {
    if (held[i] != note) continue;
    for (uint8_t j = i; j + 1 < heldCount; j++) held[j] = held[j + 1];
    heldCount--;
    return;
  }
}

// single trigger: only the first key of a phrase starts the envelope
static void noteOn(uint8_t note, uint8_t keyVelocity) {
  bool first = heldCount == 0;
  velocity = keyVelocity / 127.0f;           // the last key's, also when legato
  removeHeld(note);
  if (heldCount == SYNTH_HELD_MAX) removeHeld(held[0]);
  held[heldCount++] = note;
  play(note);
  if (first) triggers = triggers + 1;
}

// releasing the newest key returns to the newest one still held
static void noteOff(uint8_t note) {
  removeHeld(note);
  if (heldCount > 0) {
    play(held[heldCount - 1]);
  } else {
    gate = false;                           // the note releases at its pitch
  }
}

static void silence() {
  heldCount = 0;
  gate = false;
}

// ----------------------------------------------------------------------------
//  MIDI
// ----------------------------------------------------------------------------

static uint8_t dataBytesFor(uint8_t status) {
  switch (status & 0xF0) {
    case 0x80: case 0x90: case 0xA0: case 0xB0: case 0xE0: return 2;
    case 0xC0: case 0xD0: return 1;
  }
  return 0;
}

static void onChannelMessage(uint8_t status, const uint8_t *d) {
  const uint8_t type = status & 0xF0;
  const bool ours = (status & 0x0F) == channel - 1;
  if (type == 0x80 || type == 0x90) {
    if (!ours) {
      otherChannel++;
      return;
    }
    noteMessages++;
    if (type == 0x90 && d[1] > 0) {
      noteOn(d[0], d[1]);
    } else {
      noteOff(d[0]);
    }
  } else if (type == 0xE0 && ours) {
    // the pitch strip: 14 bits, 8192 in the middle
    int value = ((int)d[1] << 7) | d[0];
    bendPosition = fminf(1.0f, (value - 8192) / 8192.0f);
  } else if (type == 0xB0 && ours && d[0] == 1) {
    modStrip = d[1];                        // the modulation wheel: the KeyStep's mod strip
  } else if (type == 0xB0 && ours && (d[0] == 120 || d[0] == 123)) {
    silence();                              // all sound off, all notes off
  }
}

static void onByte(uint8_t b) {
  if (b >= 0xF8) return;                    // realtime: anywhere, changes nothing
  if (b & 0x80) {
    // system common and SysEx end running status; their data is dropped
    runningStatus = b < 0xF0 ? b : 0;
    dataNeeded = dataBytesFor(b);
    dataCount = 0;
    return;
  }
  if (runningStatus == 0) return;
  data[dataCount++] = b;
  if (dataCount < dataNeeded) return;
  dataCount = 0;
  onChannelMessage(runningStatus, data);
}

static void onUartError(hardwareSerial_error_t) {
  __atomic_fetch_add(&uartErrors, 1, __ATOMIC_RELAXED);
}

// ----------------------------------------------------------------------------
//  Public
// ----------------------------------------------------------------------------

bool synthBegin() {
  synthEnd();
  silence();
  currentNote = 0xFF;
  silent = true;
  runningStatus = 0;
  dataCount = 0;
  noteMessages = otherChannel = midiBytes = 0;
  modStrip = 0;                             // no vibrato until the strip is touched
  bendPosition = 0.0f;                      // the pitch strip springs back to the middle
  __atomic_store_n(&uartErrors, 0, __ATOMIC_RELAXED);

  if (!startI2s()) return false;

  // both pins named: UART1's default pins on the ESP32 are GPIO9 and 10,
  // the firmware's I2C bus. GPIO4 (MIDI out) leads nowhere without R36.
  Serial1.setRxBufferSize(MIDI_RX_BUFFER);
  Serial1.begin(MIDI_BAUD_RATE, SERIAL_8N1, PIN_MIDI_IN, PIN_MIDI_OUT);
  Serial1.onReceiveError(onUartError);
  midiOpen = true;

  stopRequested = false;
  if (xTaskCreatePinnedToCore(audioTask, "synth", SYNTH_TASK_STACK, nullptr,
                              SYNTH_TASK_PRIORITY, &task, SYNTH_TASK_CORE) != pdPASS) {
    task = nullptr;
    synthEnd();
    return false;
  }
  return true;
}

void synthEnd() {
  if (task != nullptr) {
    stopRequested = true;
    for (int i = 0; i < 50 && task != nullptr; i++) delay(10);   // the task ends itself
  }
  if (midiOpen) {
    Serial1.end();
    midiOpen = false;
  }
  stopI2s();
  silence();
}

void synthPoll() {
  if (!midiOpen) return;
  while (Serial1.available()) {
    midiBytes++;
    onByte((uint8_t)Serial1.read());
  }
}

static void updateRatio(uint8_t oscillator) {
  ratios[oscillator] = powf(2.0f, octaves[oscillator] + detunes[oscillator] / 1200.0f);
}

void synthSetWaveform(uint8_t oscillator, uint8_t value) {
  if (oscillator < SYNTH_OSCILLATORS && value <= SYNTH_WAVEFORM_LAST) waveforms[oscillator] = value;
}

void synthSetOctave(uint8_t oscillator, int8_t octave) {
  if (oscillator >= SYNTH_OSCILLATORS || octave < -2 || octave > 2) return;
  octaves[oscillator] = octave;
  updateRatio(oscillator);
}

void synthSetDetune(uint8_t oscillator, int16_t cents) {
  if (oscillator >= SYNTH_OSCILLATORS || cents < -700 || cents > 700) return;
  detunes[oscillator] = cents;
  updateRatio(oscillator);
}

void synthSetLevel(uint8_t oscillator, uint8_t percent) {
  if (oscillator < SYNTH_OSCILLATORS && percent <= 100) levels[oscillator] = percent / 100.0f;
}

void synthSetNoise(uint8_t percent) {
  if (percent <= 100) noiseLevel = percent / 100.0f;
}

static float samplesOf(uint16_t milliseconds) {
  return milliseconds * (SYNTH_SAMPLE_RATE / 1000.0f);
}

// the per-sample factor that brings a level within 1 % of its target in the time set
static float coefficientFor(uint16_t milliseconds) {
  return expf(-SYNTH_TIME_CONSTANTS / samplesOf(milliseconds));
}

void synthSetAttack(uint16_t milliseconds) {
  if (milliseconds >= 1 && milliseconds <= 10000) attackStep = 1.0f / samplesOf(milliseconds);
}

void synthSetDecay(uint16_t milliseconds) {
  if (milliseconds >= 1 && milliseconds <= 10000) decayCoefficient = coefficientFor(milliseconds);
}

void synthSetSustain(uint8_t percent) {
  if (percent <= 100) sustainLevel = percent / 100.0f;
}

void synthSetRelease(uint16_t milliseconds) {
  if (milliseconds >= 1 && milliseconds <= 10000) releaseCoefficient = coefficientFor(milliseconds);
}

void synthSetLfoRate(uint8_t lfo, uint16_t centiHertz) {
  if (lfo < SYNTH_LFOS && centiHertz >= 10 && centiHertz <= 2000) {
    lfoIncrements[lfo] = centiHertz / 100.0f / SYNTH_SAMPLE_RATE;
  }
}

void synthSetLfoWaveform(uint8_t lfo, uint8_t value) {
  if (lfo < SYNTH_LFOS && value <= SYNTH_LFO_SQUARE) lfoWaveforms[lfo] = value;
}

void synthSetLfoDepth(uint8_t lfo, uint16_t depth) {
  if (lfo == 0 && depth <= 1200) vibratoOctaves = depth / 1200.0f;
  if (lfo == 1 && depth <= 100) tremoloDepth = depth / 100.0f;
}

void synthSetWidth(uint8_t oscillator, uint8_t percent) {
  if (oscillator < SYNTH_OSCILLATORS && percent >= 5 && percent <= 95) widths[oscillator] = percent / 100.0f;
}

void synthSetSymmetry(uint8_t oscillator, uint8_t percent) {
  if (oscillator < SYNTH_OSCILLATORS && percent >= 10 && percent <= 90) symmetries[oscillator] = percent / 100.0f;
}

void synthSetLfoWidth(uint8_t lfo, uint8_t percent) {
  if (lfo < SYNTH_LFOS && percent >= 5 && percent <= 95) lfoWidths[lfo] = percent / 100.0f;
}

void synthSetLfoSymmetry(uint8_t lfo, uint8_t percent) {
  if (lfo < SYNTH_LFOS && percent <= 100) lfoSymmetries[lfo] = percent / 100.0f;
}

void synthSetPwmDepth(uint8_t percent) {
  if (percent <= 100) pwmDepth = percent / 100.0f * SYNTH_PWM_SWING;
}

void synthModulation(uint8_t &strip, uint8_t &stage) {
  strip = modStrip;
  stage = stageNow;
}

void synthSetCutoff(uint16_t hertz) {
  if (hertz >= 20 && hertz <= 20000) cutoffOctaves = log2f((float)hertz);
}

void synthSetEmphasis(uint8_t percent) {
  if (percent <= 100) emphasis = SYNTH_EMPHASIS_MAXIMUM * percent / 100.0f;
}

void synthSetContourAmount(uint8_t percent) {
  if (percent <= 100) contourOctaves = SYNTH_CONTOUR_OCTAVES * percent / 100.0f;
}

void synthSetContourAttack(uint16_t milliseconds) {
  if (milliseconds >= 1 && milliseconds <= 10000) contourAttackStep = 1.0f / samplesOf(milliseconds);
}

void synthSetContourDecay(uint16_t milliseconds) {
  if (milliseconds >= 1 && milliseconds <= 10000) contourDecayCoefficient = coefficientFor(milliseconds);
}

void synthSetContourSustain(uint8_t percent) {
  if (percent <= 100) contourSustain = percent / 100.0f;
}

void synthSetKeyTracking(uint8_t percent) {
  if (percent <= 100) keyTracking = percent / 100.0f;
}

// glide: the per-block factor that brings the pitch within 1 % of the
// interval in the time set (a block is 64 samples)
void synthSetGlide(uint16_t milliseconds) {
  if (milliseconds > 5000) return;
  glideCoefficient = milliseconds == 0
                         ? 0.0f
                         : expf(-SYNTH_TIME_CONSTANTS * SYNTH_BLOCK_FRAMES / samplesOf(milliseconds));
}

void synthSetBendRange(uint8_t semitones) {
  if (semitones <= 12) bendRange = semitones / 12.0f;
}

void synthSetVelocityLoudness(uint8_t percent) {
  if (percent <= 100) velocityLoudness = percent / 100.0f;
}

void synthSetVelocityBrightness(uint8_t percent) {
  if (percent <= 100) velocityBrightness = SYNTH_BRIGHTNESS_OCTAVES * percent / 100.0f;
}

void synthSetFilterLfo(uint8_t percent) {
  if (percent <= 100) filterLfoOctaves = SYNTH_FILTER_LFO_OCTAVES * percent / 100.0f;
}

// a block of 64 samples lasts 1.33 ms: CPU cycles at the clock the core runs
void synthLoad(uint16_t &average, uint16_t &peak) {
  const float budget = getCpuFrequencyMhz() * 1.0e6f * SYNTH_BLOCK_FRAMES / SYNTH_SAMPLE_RATE;
  const uint32_t blocks = loadBlocks;
  average = blocks == 0 ? 0 : (uint16_t)lroundf((float)loadCycles / blocks / budget * 1000.0f);
  peak = (uint16_t)lroundf(loadPeakCycles / budget * 1000.0f);
  loadCycles = 0;
  loadBlocks = 0;
  loadPeakCycles = 0;
}

void synthSetChannel(uint8_t value) {
  if (value < 1 || value > 16) return;
  if (value != channel) silence();
  channel = value;
}

void synthStatus(SynthStatus &status) {
  status.note = gate || !silent ? currentNote : 0xFF;
  status.held = heldCount;
  status.gate = gate;
  status.channel = channel;
  status.noteMessages = noteMessages;
  status.otherChannel = otherChannel;
  status.bytes = midiBytes;
  status.uartErrors = __atomic_load_n(&uartErrors, __ATOMIC_RELAXED);
}
