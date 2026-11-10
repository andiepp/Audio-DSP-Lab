// The receiver chain at -O2: the firmware is built -Os, which costs this
// code about a third more time (design/receiver.md, section 3)
#pragma GCC optimize("O2")

#include "RxChain.h"
#include <Arduino.h>
#include <math.h>
#include <string.h>
#include "dsps_fft2r.h"
#include <esp_rom_crc.h>

#define FS                            48000.0f
#define RX_DEC                        4
#define FSB                           (FS / RX_DEC)          // 12 kHz baseband
#define BLOCK                         RX_CHAIN_BLOCK
#define RXB                           (BLOCK / RX_DEC)       // 64 baseband samples a block
#define PI_D                          3.14159265358979
#define PI_F                          3.14159265f

#define NCO_TABLE                     1024
#define DDC_TAPS                      43                  // 80 dB, pass 3 kHz, stop 9 kHz
#define DUC_TAPS                      43
#define DUC_HIST                      ((DUC_TAPS + RX_DEC - 1) / RX_DEC)
#define UP_N                          128                 // overlap-save FFT
#define UP_PMAX                       ((RX_TAPS_MAX + RXB) / RXB)   // 8 partitions
#define AGC_LOOK                      64                  // 5.3 ms
#define AGC_TARGET                    -12.0f
#define AGC_ATTACK_S                  0.001f
#define DB_FLOOR                      -150.0f
#define DB_RE_32768                   90.309f             // 20 log10(32768)
#define LN10_20                       0.11512925f
#define DC_R                          0.97416f            // DC blocker, -3 dB near 50 Hz
#define FM_TARGET                     8192.0f             // -12 dBFS at the nominal deviation
#define FAVG_A                        (1.0f / 1200.0f)    // FM tuning indicator, ~0.1 s
#define IN_SCALE                      (1.0f / 256.0f)     // 24-bit sample -> counts re 32768
#define TRANSITION_MIN                60.0f
#define TRANSITION_MAX                300.0f
#define DESIGN_TASK_STACK             4096
#define DESIGN_TASK_PRIORITY          1                   // below the audio task's 4
#define DESIGN_TASK_CORE              0
#define NR_N                          256                 // spectral NR: FFT length
#define NR_BINS                       (NR_N / 2 + 1)
#define NR_SUBS                       8                   // minimum statistics: sub-windows
#define NR_SUBL                       32                  // ... of this many frames (1.37 s in all)
#define NR_ALPHA                      0.85f               // power smoothing per frame
#define NR_BIAS                       2.0f                // minimum -> mean noise power
#define NR_DD                         0.98f               // decision-directed weight
#define NR_XIMIN                      0.003f              // -25 dB: the least a-priori SNR
#define LMS_L                         64                  // LMS taps
#define LMS_D                         16                  // LMS decorrelation delay

struct cfloat { float re, im; };

// ----------------------------------------------------------------------------
//  State, on the heap while the receiver runs
// ----------------------------------------------------------------------------

struct Ddc {
  uint32_t phase, inc;
  float h[DDC_TAPS];
  cfloat buf[DDC_TAPS - 1 + BLOCK];       // history + one mixed block
  float dcI, dcQ;                         // I/Q: DC trackers
  float kq, ki;                           // I/Q: Q' = kq Q + ki I
};

struct Duc {
  uint32_t phase, inc;                    // inc 0: audio, no mixing
  float h[DUC_TAPS];
  cfloat buf[DUC_HIST + RXB];
};

struct Agc {
  cfloat d[AGC_LOOK];
  int idx, on;
  float maxGain;
  int hangN, hang;
  float decay, peak, gain, alpha;
};

struct Demod {
  float x1, y1;
  cfloat zp;
  float fmScale, fAvg, deBeta, de;
};

struct TestGen {
  uint32_t phase, inc;
  float amp, noise;                       // counts re 32768: peak, RMS
  float qGain;                            // I/Q: Q's gain (a front end's error), linear
  uint32_t qPhase;                        // I/Q: Q's phase error, 2^32 a turn
  uint32_t keyPeriod, keyPos;             // samples; 0 = steady
  float keyEnv;
  uint32_t noiseSeed;
};

struct Chain {
  RxConfig cfg;                           // in force (the task's copy)
  Ddc ddc;
  Duc duc;
  Agc agc;
  Demod demod;
  TestGen test;
  float squelchGain;
  float inI[BLOCK], inQ[BLOCK];
  cfloat bb[RXB], bbf[RXB], bba[RXB], aud[RXB];
  float out48[BLOCK];
  cfloat xPrev[RXB];                      // overlap-save: the previous block
  float work[2 * UP_N];                   // overlap-save: the accumulated spectrum
  int xHead;                              // the newest input spectrum
  int act;                                // the active coefficient set
  int parts[2];                           // its partitions
  uint16_t taps[2];
  float transition[2];
  bool downloaded[2];
};

// the noise reduction (nr.c), on the heap of its own (about 12 KB)
struct Nr {
  uint8_t mode;
  float level;
  // spectral
  float in[NR_N], acc[NR_N];
  float smooth[NR_BINS], cur[NR_BINS], sub[NR_SUBS][NR_BINS], noise[NR_BINS];
  float gPrev[NR_BINS], gamPrev[NR_BINS];
  int frame, subIdx, filled;
  float window[NR_N];                     // square-root Hann
  float fft[2 * NR_N];                    // interleaved, for esp-dsp
  float gainDb;                           // the average gain, for the meter
  // LMS
  float x[LMS_L + LMS_D + RXB];
  float w[LMS_L];
};

// a download in progress (the loop), and the taps the design task
// transforms once it ended well
struct Load {
  bool active;                            // between begin and end
  bool complex;
  uint16_t expected, received;
  uint32_t crc;
  cfloat taps[RX_TAPS_MAX];               // natural order
};

// the design's request and its result (the loop and the audio task write
// the request, the design task the result)
struct Design {
  float low, high, stopband;
  bool fromLoad;                          // the request: the loaded taps, not a design
  volatile uint32_t generation;           // a newer request abandons a design
  uint32_t doneGeneration;
  float proto[RX_TAPS_MAX];               // the real prototype
};

static Chain *chain = nullptr;
static float *sineTable = nullptr;        // NCO_TABLE + 1
static float *spectra[2][UP_PMAX];        // partition spectra of both sets, 2 x UP_N interleaved
static float *inputSpectra[UP_PMAX];      // the frequency-domain delay line
static Design *design = nullptr;
static Nr *nr = nullptr;
static Load *load = nullptr;
static volatile bool loadInUse = false;   // the design task has the loaded taps to transform
static TaskHandle_t designTask = nullptr;
static volatile bool designStop = false;
static volatile bool designBusy = false;
static volatile bool swapReady = false;   // the inactive set holds a finished design
static volatile uint32_t designs = 0;

static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
static RxConfig pending;                  // the loop's configuration
static uint32_t pendingDirty = 0;

static volatile RxMeter meter;

// ----------------------------------------------------------------------------
//  NCO and Kaiser design (rx.c)
// ----------------------------------------------------------------------------

static uint32_t ncoInc(float f, float fs) {
  double x = (double)f / (double)fs;
  x -= floor(x);                          // wrap into [0, 1)
  return (uint32_t)(x * 4294967296.0);
}

static inline float ncoSin(uint32_t phase) {
  const uint32_t k = phase >> 22;
  const float fraction = (float)(phase & 0x3FFFFF) * (1.0f / 4194304.0f);
  return sineTable[k] + fraction * (sineTable[k + 1] - sineTable[k]);
}

static inline void ncoCosSin(uint32_t phase, float &c, float &s) {
  s = ncoSin(phase);
  c = ncoSin(phase + 0x40000000u);
}

static double besselI0(double x) {
  double sum = 1.0, term = 1.0, q = x * x / 4.0;
  for (int k = 1; k < 40; k++) {
    term *= q / ((double)k * k);
    sum += term;
    if (term < sum * 1e-12) break;
  }
  return sum;
}

static float besselI0f(float x) {
  float sum = 1.0f, term = 1.0f, q = x * x * 0.25f;
  for (int k = 1; k < 50; k++) {
    term *= q / ((float)k * k);
    sum += term;
    if (term < sum * 1e-8f) break;
  }
  return sum;
}

static float kaiserBeta(float a) {
  if (a > 50.0f) return 0.1102f * (a - 8.7f);
  if (a >= 21.0f) return 0.5842f * powf(a - 21.0f, 0.4f) + 0.07886f * (a - 21.0f);
  return 0.0f;
}

static int kaiserTaps(float a, float df, float fs) {
  const double dw = 2.0 * PI_D * df / fs;
  const int n = (int)ceil((a - 7.95) / (2.285 * dw)) + 1;
  return n | 1;                           // odd: an integer group delay
}

// the converters' low-pass (once, at the start: double is software here)
static void kaiserLowpass(float *h, int n, float fc, float fs, float a, float gain) {
  const double beta = kaiserBeta(a), i0b = besselI0(beta);
  const double m = (n - 1) / 2.0, wc = 2.0 * PI_D * fc / fs;
  double sum = 0.0;
  for (int k = 0; k < n; k++) {
    const double t = k - m, r = t / m;
    const double sinc = t == 0.0 ? wc / PI_D : sin(wc * t) / (PI_D * t);
    h[k] = (float)(sinc * besselI0(beta * sqrt(1.0 - r * r)) / i0b);
    sum += h[k];
  }
  for (int k = 0; k < n; k++) h[k] = (float)(h[k] * gain / sum);   // exact DC gain
}

// ----------------------------------------------------------------------------
//  Down- and up-converter (rx.c)
// ----------------------------------------------------------------------------

static void ddcInit(Ddc &d) {
  memset(&d, 0, sizeof d);
  kaiserLowpass(d.h, DDC_TAPS, 6000.0f, FS, 80.0f, 1.0f);
  d.kq = 1.0f;
}

static void ddcIqConfig(Ddc &d, float gainDb, float phaseDeg) {
  const float g = powf(10.0f, gainDb / 20.0f), p = phaseDeg * (PI_F / 180.0f);
  d.kq = 1.0f / (g * cosf(p));
  d.ki = -tanf(p);
}

// low-pass the mixed block, keep every 4th output, keep the history
static void ddcFilter(Ddc &d, cfloat *out) {
  cfloat *z = d.buf + DDC_TAPS - 1;
  for (int m = 0; m < RXB; m++) {
    const cfloat *p = z + RX_DEC * m + (RX_DEC - 1);
    float re = 0.0f, im = 0.0f;
    for (int k = 0; k < DDC_TAPS; k++) {
      re += d.h[k] * p[-k].re;
      im += d.h[k] * p[-k].im;
    }
    out[m].re = re;
    out[m].im = im;
  }
  memmove(d.buf, z + BLOCK - (DDC_TAPS - 1), (DDC_TAPS - 1) * sizeof(cfloat));
}

// I/Q: DC removed (about 2 Hz), Q corrected, z = (I + j Q') e^(-j w n): a
// tone of amplitude A at offset + f comes out as A e^(j 2 pi f t)
static void ddcProcessIq(Ddc &d, const float *iIn, const float *qIn, cfloat *out) {
  cfloat *z = d.buf + DDC_TAPS - 1;
  uint32_t ph = d.phase;
  const uint32_t inc = d.inc;
  float dcI = d.dcI, dcQ = d.dcQ;
  const float kq = d.kq, ki = d.ki, k = 1.0f / 4096.0f;
  for (int i = 0; i < BLOCK; i++) {
    float c, s;
    dcI += (iIn[i] - dcI) * k;
    dcQ += (qIn[i] - dcQ) * k;
    const float x = iIn[i] - dcI;
    const float y = kq * (qIn[i] - dcQ) + ki * x;
    ncoCosSin(ph, c, s);
    z[i].re = x * c + y * s;
    z[i].im = y * c - x * s;
    ph += inc;
  }
  d.phase = ph;
  d.dcI = dcI;
  d.dcQ = dcQ;
  ddcFilter(d, out);
}

// a real IF: z = 2 x e^(-j w n), the factor 2 restores the image's half
static void ddcProcessReal(Ddc &d, const float *in, cfloat *out) {
  cfloat *z = d.buf + DDC_TAPS - 1;
  uint32_t ph = d.phase;
  const uint32_t inc = d.inc;
  for (int i = 0; i < BLOCK; i++) {
    float c, s;
    const float x2 = 2.0f * in[i];
    ncoCosSin(ph, c, s);
    z[i].re = x2 * c;
    z[i].im = -x2 * s;
    ph += inc;
  }
  d.phase = ph;
  ddcFilter(d, out);
}

static void ducInit(Duc &u) {
  memset(&u, 0, sizeof u);
  // gain 4: zero-stuffing leaves a quarter of the energy in the passband
  kaiserLowpass(u.h, DUC_TAPS, 6000.0f, FS, 80.0f, (float)RX_DEC);
}

static void ducProcess(Duc &u, const cfloat *in, float *out) {
  cfloat *x = u.buf + DUC_HIST;
  uint32_t ph = u.phase;
  const uint32_t inc = u.inc;
  for (int m = 0; m < RXB; m++) x[m] = in[m];
  for (int m = 0; m < RXB; m++) {
    for (int p = 0; p < RX_DEC; p++) {
      float re = 0.0f, im = 0.0f;
      for (int k = p, i = 0; k < DUC_TAPS; k += RX_DEC, i++) {
        re += u.h[k] * x[m - i].re;
        im += u.h[k] * x[m - i].im;
      }
      if (inc == 0) {
        out[RX_DEC * m + p] = re;
      } else {
        float c, s;
        ncoCosSin(ph, c, s);
        out[RX_DEC * m + p] = re * c - im * s;
        ph += inc;
      }
    }
  }
  u.phase = ph;
  memmove(u.buf, x + RXB - DUC_HIST, DUC_HIST * sizeof(cfloat));
}

// ----------------------------------------------------------------------------
//  Channel filter: design (chan.c) and partitioned overlap-save
// ----------------------------------------------------------------------------

static void fft128(float *data) {
  dsps_fft2r_fc32(data, UP_N);
  dsps_bit_rev_fc32(data, UP_N);
}

// the request as chan_request takes it: taps from the Kaiser formula for
// a transition of a quarter of the bandwidth (60 .. 300 Hz), widened if
// that needs more than RX_TAPS_MAX
static void designSize(float low, float high, float stopband, int &n, float &transition) {
  transition = constrain(0.25f * (high - low), TRANSITION_MIN, TRANSITION_MAX);
  n = kaiserTaps(stopband, transition, FSB);
  if (n > RX_TAPS_MAX) {
    n = RX_TAPS_MAX;
    transition = (stopband - 7.95f) / (2.285f * (n - 1)) * FSB / (2.0f * PI_F);
  }
}

// into the inactive set: the prototype, normalised to unity gain at the
// passband centre, shifted there (h[k] = p[k] e^(j 2 pi fc (k - M) / fs)),
// partitioned and transformed. False if a newer request came meanwhile
static bool designInto(int set, float low, float high, float stopband, uint32_t generation) {
  int n;
  float transition;
  designSize(low, high, stopband, n, transition);
  const float beta = kaiserBeta(stopband), i0b = besselI0f(beta);
  const float wc = 2.0f * PI_F * (0.5f * (high - low)) / FSB, m = 0.5f * (n - 1);
  float sum = 0.0f;
  for (int k = 0; k < n; k++) {
    if ((k & 63) == 0 && design->generation != generation) return false;
    const float t = k - m, r = t / m;
    const float sinc = t == 0.0f ? wc / PI_F : sinf(wc * t) / (PI_F * t);
    design->proto[k] = sinc * besselI0f(beta * sqrtf(1.0f - r * r)) / i0b;
    sum += design->proto[k];
  }
  const float g = 1.0f / sum;
  const uint32_t inc = ncoInc(0.5f * (low + high), FSB);
  uint32_t ph = inc * (uint32_t)(-(int)m);          // the phase at k = 0, i.e. -M
  const int parts = (n + RXB - 1) / RXB;
  for (int p = 0; p < parts; p++) {
    float *h = spectra[set][p];
    memset(h, 0, 2 * UP_N * sizeof(float));
    for (int i = 0; i < RXB; i++) {
      const int k = p * RXB + i;
      if (k >= n) break;
      float c, s;
      ncoCosSin(ph, c, s);
      const float v = design->proto[k] * g;
      h[2 * i] = v * c;
      h[2 * i + 1] = v * s;
      ph += inc;
    }
    fft128(h);
  }
  if (design->generation != generation) return false;
  chain->parts[set] = parts;
  chain->taps[set] = (uint16_t)n;
  chain->transition[set] = transition;
  chain->downloaded[set] = false;
  return true;
}

// the loaded taps into the inactive set, partitioned and transformed
static bool partitionInto(int set, uint32_t generation) {
  const int n = load->expected;
  const int parts = (n + RXB - 1) / RXB;
  for (int p = 0; p < parts; p++) {
    if (design->generation != generation) return false;
    float *h = spectra[set][p];
    memset(h, 0, 2 * UP_N * sizeof(float));
    for (int i = 0; i < RXB; i++) {
      const int k = p * RXB + i;
      if (k >= n) break;
      h[2 * i] = load->taps[k].re;
      h[2 * i + 1] = load->taps[k].im;
    }
    fft128(h);
  }
  if (design->generation != generation) return false;
  chain->parts[set] = parts;
  chain->taps[set] = (uint16_t)n;
  chain->transition[set] = 0.0f;
  chain->downloaded[set] = true;
  return true;
}

// Y = sum_p H_p X_(t-p); the output is the second half of the inverse
// transform (the first half carries the circular wrap)
static void filterProcess(const cfloat *in, cfloat *out) {
  Chain &c = *chain;
  c.xHead = (c.xHead + 1) % UP_PMAX;
  float *x = inputSpectra[c.xHead];
  for (int k = 0; k < RXB; k++) {
    x[2 * k] = c.xPrev[k].re;
    x[2 * k + 1] = c.xPrev[k].im;
    x[2 * (RXB + k)] = in[k].re;
    x[2 * (RXB + k) + 1] = in[k].im;
    c.xPrev[k] = in[k];
  }
  fft128(x);
  float *y = c.work;
  memset(y, 0, sizeof c.work);
  const int parts = c.parts[c.act];
  for (int p = 0; p < parts; p++) {
    const float *h = spectra[c.act][p];
    const float *xp = inputSpectra[(c.xHead - p + UP_PMAX) % UP_PMAX];
    for (int b = 0; b < UP_N; b++) {
      const float hr = h[2 * b], hi = h[2 * b + 1], xr = xp[2 * b], xi = xp[2 * b + 1];
      y[2 * b] += hr * xr - hi * xi;
      y[2 * b + 1] += hr * xi + hi * xr;
    }
  }
  // the inverse transform as a forward one of the conjugate
  for (int b = 0; b < UP_N; b++) y[2 * b + 1] = -y[2 * b + 1];
  fft128(y);
  const float s = 1.0f / UP_N;
  for (int k = 0; k < RXB; k++) {
    out[k].re = y[2 * (RXB + k)] * s;
    out[k].im = -y[2 * (RXB + k) + 1] * s;
  }
}

static void designTaskRun(void *) {
  while (!designStop) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    while (!designStop) {
      portENTER_CRITICAL(&lock);
      const uint32_t generation = design->generation;
      const float low = design->low, high = design->high, stopband = design->stopband;
      const bool fromLoad = design->fromLoad;
      portEXIT_CRITICAL(&lock);
      if (generation == design->doneGeneration) break;
      designBusy = true;
      // the inactive set is free once the audio task took the last result
      while (swapReady && !designStop) vTaskDelay(1);
      if (designStop) break;
      const bool done = fromLoad ? partitionInto(1 - chain->act, generation)
                                 : designInto(1 - chain->act, low, high, stopband, generation);
      if (fromLoad) loadInUse = false;
      if (done) {
        design->doneGeneration = generation;
        __sync_synchronize();
        swapReady = true;
      }
    }
    designBusy = false;
  }
  designTask = nullptr;
  vTaskDelete(nullptr);
}

static void requestDesign(float low, float high, float stopband) {
  if (load != nullptr) load->active = false;      // a design replaces a download
  portENTER_CRITICAL(&lock);
  design->low = low;
  design->high = high;
  design->stopband = stopband;
  design->fromLoad = false;
  design->generation = design->generation + 1;   // volatile: no ++
  portEXIT_CRITICAL(&lock);
  if (designTask != nullptr) xTaskNotifyGive(designTask);
}

// ----------------------------------------------------------------------------
//  AGC and S-meter (agc.c)
// ----------------------------------------------------------------------------

static void agcInit(Agc &a) {
  memset(&a, 0, sizeof a);
  a.peak = DB_FLOOR;
  a.alpha = 1.0f - expf(-1.0f / (AGC_ATTACK_S * FSB));
}

static void agcConfig(Agc &a, bool on, float maxGainDb, float hangS, float decayDbS) {
  a.on = on;
  a.maxGain = maxGainDb;
  a.hangN = (int)(hangS * FSB);
  a.decay = decayDbS / FSB;
  if (!on) a.gain = maxGainDb;           // a manual gain applies at once
}

static void agcProcess(Agc &a, const cfloat *in, cfloat *out) {
  float peak = a.peak, gain = a.gain;
  const float alpha = a.alpha, decay = a.decay, maxGain = a.maxGain;
  int hang = a.hang, idx = a.idx;
  const int hangN = a.hangN, on = a.on;
  for (int i = 0; i < RXB; i++) {
    const float e2 = in[i].re * in[i].re + in[i].im * in[i].im;
    const float eDb = 10.0f * log10f(e2 + 1e-12f) - DB_RE_32768;
    // the detector: instant rise, hang, then a constant decay in dB
    if (eDb > peak) {
      peak = eDb;
      hang = hangN;
    } else if (hang > 0) {
      hang--;
    } else {
      peak -= decay;
    }
    if (peak < DB_FLOOR) peak = DB_FLOOR;
    float target = on ? AGC_TARGET - peak : maxGain;
    if (target > maxGain) target = maxGain;
    gain += (target - gain) * alpha;
    // the sample that entered AGC_LOOK samples ago
    const float g = expf(gain * LN10_20);
    out[i].re = a.d[idx].re * g;
    out[i].im = a.d[idx].im * g;
    a.d[idx] = in[i];
    if (++idx >= AGC_LOOK) idx = 0;
  }
  a.peak = peak;
  a.gain = gain;
  a.hang = hang;
  a.idx = idx;
}

// ----------------------------------------------------------------------------
//  Demodulators (demod.c)
// ----------------------------------------------------------------------------

static void demodInit(Demod &d) {
  memset(&d, 0, sizeof d);
  d.fmScale = FM_TARGET / 2500.0f;
  d.deBeta = 1.0f;
}

static void demodConfig(Demod &d, float deviationHz, float deemphasisUs) {
  d.fmScale = FM_TARGET / deviationHz;
  d.deBeta = deemphasisUs > 0.0f ? 1.0f - expf(-1.0f / (deemphasisUs * 1e-6f * FSB)) : 1.0f;
}

static void demodProcess(Demod &d, uint8_t mode, const cfloat *in, cfloat *audio) {
  switch (mode) {
    case RX_AM: {
      float x1 = d.x1, y1 = d.y1;
      for (int i = 0; i < RXB; i++) {
        const float e = sqrtf(in[i].re * in[i].re + in[i].im * in[i].im);
        const float y = e - x1 + DC_R * y1;
        x1 = e;
        y1 = y;
        audio[i].re = y;
        audio[i].im = 0.0f;
      }
      d.x1 = x1;
      d.y1 = y1;
      break;
    }
    case RX_USB:
    case RX_LSB:
    case RX_CW:
      // the filter removed the other sideband, the mixer sits on the carrier
      for (int i = 0; i < RXB; i++) {
        audio[i].re = in[i].re;
        audio[i].im = 0.0f;
      }
      break;
    case RX_FM: {
      float x1 = d.x1, y1 = d.y1, fAvg = d.fAvg, de = d.de;
      const float k = FSB / (2.0f * PI_F), beta = d.deBeta, scale = d.fmScale;
      cfloat zp = d.zp;
      for (int i = 0; i < RXB; i++) {
        // z[n] z*[n-1]: its angle is this sample's phase step
        const float pr = in[i].re * zp.re + in[i].im * zp.im;
        const float pi = in[i].im * zp.re - in[i].re * zp.im;
        const float f = atan2f(pi, pr) * k;
        const float x = f * scale;
        zp = in[i];
        fAvg += (f - fAvg) * FAVG_A;
        const float y = x - x1 + DC_R * y1;
        x1 = x;
        y1 = y;
        de += (y - de) * beta;
        audio[i].re = de;
        audio[i].im = 0.0f;
      }
      d.x1 = x1;
      d.y1 = y1;
      d.fAvg = fAvg;
      d.de = de;
      d.zp = zp;
      break;
    }
    default:                              // off: the filtered baseband
      for (int i = 0; i < RXB; i++) audio[i] = in[i];
      break;
  }
}

// ----------------------------------------------------------------------------
//  Noise reduction (nr.c)
// ----------------------------------------------------------------------------

static void nrReset(Nr &n) {
  memset(n.in, 0, sizeof n.in);
  memset(n.acc, 0, sizeof n.acc);
  memset(n.smooth, 0, sizeof n.smooth);
  memset(n.cur, 0, sizeof n.cur);
  memset(n.sub, 0, sizeof n.sub);
  memset(n.noise, 0, sizeof n.noise);
  for (int i = 0; i < NR_BINS; i++) {
    n.gPrev[i] = 1.0f;
    n.gamPrev[i] = 1.0f;
  }
  n.frame = n.subIdx = n.filled = 0;
  n.gainDb = 0.0f;
  memset(n.x, 0, sizeof n.x);
  memset(n.w, 0, sizeof n.w);
}

static void nrInit(Nr &n) {
  for (int i = 0; i < NR_N; i++) n.window[i] = sqrtf(0.5f - 0.5f * cosf(2.0f * PI_F * i / NR_N));
  n.mode = RX_NR_OFF;
  n.level = 0.0f;
  nrReset(n);
}

// a new method starts afresh: no stale estimates
static void nrConfig(Nr &n, uint8_t mode, float levelDb) {
  if (mode != n.mode) nrReset(n);
  n.mode = mode;
  n.level = levelDb;
}

static void fft256(float *data) {
  dsps_fft2r_fc32(data, NR_N);
  dsps_bit_rev_fc32(data, NR_N);
}

// short-time spectral gain: FFT 256 every 64 samples (75 % overlap), square-
// root Hann for analysis and synthesis (the product adds up to 2 at this
// hop: the output is halved); per bin the noise by minimum statistics, the
// Wiener gain from the decision-directed a-priori SNR, never below the
// floor 10^(-level/20). Delay 192 samples (16 ms)
static void nrSpectral(Nr &n, cfloat *audio) {
  const float gMin = powf(10.0f, -n.level / 20.0f);
  float gSum = 0.0f;
  float *f = n.fft;
  memmove(n.in, n.in + RXB, (NR_N - RXB) * sizeof(float));
  for (int i = 0; i < RXB; i++) n.in[NR_N - RXB + i] = audio[i].re;
  for (int i = 0; i < NR_N; i++) {
    f[2 * i] = n.in[i] * n.window[i];
    f[2 * i + 1] = 0.0f;
  }
  fft256(f);

  // minimum statistics: a new sub-window every NR_SUBL frames
  if (++n.frame >= NR_SUBL) {
    n.frame = 0;
    memcpy(n.sub[n.subIdx], n.cur, sizeof n.cur);
    n.subIdx = (n.subIdx + 1) % NR_SUBS;
    if (n.filled < NR_SUBS) n.filled++;
    memcpy(n.cur, n.smooth, sizeof n.cur);
  }

  for (int i = 0; i < NR_BINS; i++) {
    const float p = f[2 * i] * f[2 * i] + f[2 * i + 1] * f[2 * i + 1];
    n.smooth[i] = n.filled == 0 && n.frame == 1 ? p : NR_ALPHA * n.smooth[i] + (1.0f - NR_ALPHA) * p;
    if (n.smooth[i] < n.cur[i] || n.cur[i] == 0.0f) n.cur[i] = n.smooth[i];
    float mn = n.cur[i];
    for (int s = 0; s < n.filled; s++) {
      if (n.sub[s][i] < mn) mn = n.sub[s][i];
    }
    n.noise[i] = NR_BIAS * mn + 1e-6f;
    const float gam = p / n.noise[i];
    float xi = NR_DD * n.gPrev[i] * n.gPrev[i] * n.gamPrev[i] + (1.0f - NR_DD) * (gam > 1.0f ? gam - 1.0f : 0.0f);
    if (xi < NR_XIMIN) xi = NR_XIMIN;
    float g = xi / (1.0f + xi);
    if (g < gMin) g = gMin;
    n.gPrev[i] = g;
    n.gamPrev[i] = gam;
    gSum += g;
    f[2 * i] *= g;
    f[2 * i + 1] *= g;
    if (i > 0 && i < NR_N / 2) {          // the mirror half, conjugate
      f[2 * (NR_N - i)] = f[2 * i];
      f[2 * (NR_N - i) + 1] = -f[2 * i + 1];
    }
  }
  n.gainDb = 20.0f * log10f(gSum / NR_BINS + 1e-9f);

  // the inverse by conjugation, the synthesis window, overlap-add
  for (int i = 0; i < NR_N; i++) f[2 * i + 1] = -f[2 * i + 1];
  fft256(f);
  for (int i = 0; i < NR_N; i++) n.acc[i] += f[2 * i] * (1.0f / NR_N) * n.window[i];
  for (int i = 0; i < RXB; i++) {
    audio[i].re = 0.5f * n.acc[i];
    audio[i].im = 0.0f;
  }
  memmove(n.acc, n.acc + RXB, (NR_N - RXB) * sizeof(float));
  memset(n.acc + NR_N - RXB, 0, RXB * sizeof(float));
}

// adaptive line enhancer: an NLMS predictor of 64 taps looks 16 samples
// (1.3 ms) back and predicts the present; tones, CW and voiced speech come
// out, the noise does not. Step 0.25 x 10^(-level/20). No delay
static void nrLms(Nr &n, cfloat *audio) {
  const int h = LMS_L + LMS_D;            // history kept before the block
  const float mu = 0.25f * powf(10.0f, -n.level / 20.0f);
  float *x = n.x, *w = n.w;
  for (int i = 0; i < RXB; i++) x[h + i] = audio[i].re;
  // the taps' power, exact at the block start (a running sum drifts in
  // float when a strong signal stops), then sliding within the block
  float p = 0.0f;
  {
    const float *r = x + h - LMS_D - 1;
    for (int k = 0; k < LMS_L; k++) p += r[-k] * r[-k];
  }
  for (int i = 0; i < RXB; i++) {
    const float *r = x + h + i - LMS_D;   // r[-k], k = 0 .. L-1: the reference taps
    const float newest = r[0], oldest = r[-LMS_L];
    p += newest * newest - oldest * oldest;
    if (p < 0.0f) p = 0.0f;
    float y = 0.0f;
    for (int k = 0; k < LMS_L; k++) y += w[k] * r[-k];
    const float e = x[h + i] - y;
    const float g = mu * e / (p + 1.0f);
    for (int k = 0; k < LMS_L; k++) w[k] = 0.99995f * w[k] + g * r[-k];
    audio[i].re = y;
    audio[i].im = 0.0f;
  }
  // a non-finite output (should not happen) starts the predictor afresh
  const float last = audio[RXB - 1].re;
  if (!(last == last) || fabsf(last) > 1e9f) {
    memset(w, 0, LMS_L * sizeof(float));
    for (int i = 0; i < RXB; i++) audio[i].re = 0.0f;
  }
  memmove(x, x + RXB, h * sizeof(float));
}

// level 0: the spectral path still runs (gain 1, its estimate stays
// current, its delay does not jump); the LMS passes the audio
static void nrProcess(Nr &n, cfloat *audio) {
  if (n.mode == RX_NR_SPECTRAL) nrSpectral(n, audio);
  else if (n.mode == RX_NR_LMS && n.level > 0.0f) nrLms(n, audio);
}

// ----------------------------------------------------------------------------
//  The internal test signal (selftest.c): a tone, I/Q or real, with
//  noise and keying
// ----------------------------------------------------------------------------

static void testConfigure(TestGen &t, const RxConfig &c) {
  t.inc = ncoInc(c.offset + c.testOffset, FS);
  t.amp = 32768.0f * powf(10.0f, c.testLevel / 20.0f);
  t.noise = c.testNoise <= -150.0f ? 0.0f : 32768.0f * powf(10.0f, c.testNoise / 20.0f);
  t.keyPeriod = c.testKey > 0.0f ? (uint32_t)(c.testKey * (FS / 1000.0f)) : 0;
  t.qGain = powf(10.0f, c.testIqGain / 20.0f);
  t.qPhase = (uint32_t)(int32_t)lroundf(c.testIqPhase / 360.0f * 4294967296.0f);
}

static inline float testNoise(TestGen &t) {
  // a sum of four uniform values in [-0.5, 0.5): variance 1/3
  float u = 0.0f;
  for (int j = 0; j < 4; j++) {
    t.noiseSeed = t.noiseSeed * 1664525u + 1013904223u;
    u += (float)t.noiseSeed - 2147483648.0f;
  }
  return u * (1.7320508f / 4294967296.0f);
}

static void testGenerate(TestGen &t, bool iq, float *i, float *q) {
  for (int n = 0; n < BLOCK; n++) {
    float key = 1.0f;
    if (t.keyPeriod > 0) {
      const bool on = t.keyPos < t.keyPeriod / 2;
      if (on && t.keyEnv < 1.0f) t.keyEnv += 1.0f / 240.0f;      // 5 ms ramps
      if (!on && t.keyEnv > 0.0f) t.keyEnv -= 1.0f / 240.0f;
      t.keyEnv = constrain(t.keyEnv, 0.0f, 1.0f);
      const float sn = ncoSin((uint32_t)(t.keyEnv * 1073741824.0f));
      key = sn * sn;                      // raised cosine
      if (++t.keyPos >= t.keyPeriod) t.keyPos = 0;
    }
    const float a = t.amp * key;
    if (iq) {
      i[n] = a * ncoSin(t.phase + 0x40000000u);                  // cos
      q[n] = a * t.qGain * ncoSin(t.phase + t.qPhase);           // G sin(+p)
    } else {
      i[n] = a * ncoSin(t.phase);
      q[n] = 0.0f;
    }
    if (t.noise > 0.0f) {
      i[n] += t.noise * testNoise(t);
      if (iq) q[n] += t.noise * testNoise(t);
    }
    t.phase += t.inc;
  }
}

// ----------------------------------------------------------------------------
//  Configuration (main.c apply_params)
// ----------------------------------------------------------------------------

static void apply(Chain &c, uint32_t dirty) {
  const RxConfig &cfg = c.cfg;
  if (dirty & (RX_DIRTY_TUNE | RX_DIRTY_MODE)) {
    // CW: mix to offset - pitch, so a carrier on the offset sounds at the
    // pitch; "off" mixes the filtered baseband back up to the offset
    c.ddc.inc = ncoInc(cfg.offset - (cfg.mode == RX_CW ? cfg.pitch : 0.0f), FS);
    c.duc.inc = cfg.mode == RX_OFF ? ncoInc(cfg.offset, FS) : 0;
  }
  if (dirty & RX_DIRTY_MODE) demodInit(c.demod);
  if (dirty & (RX_DIRTY_FM | RX_DIRTY_MODE)) demodConfig(c.demod, cfg.fmDeviation, cfg.deemphasis);
  if (dirty & RX_DIRTY_INPUT) ddcIqConfig(c.ddc, cfg.iqGain, cfg.iqPhase);
  if (dirty & RX_DIRTY_AGC) agcConfig(c.agc, cfg.agc != 0, cfg.agcGain, cfg.hang, cfg.decay);
  if (dirty & (RX_DIRTY_TEST | RX_DIRTY_TUNE)) testConfigure(c.test, cfg);
  if (dirty & RX_DIRTY_NR) nrConfig(*nr, cfg.nr, cfg.nrLevel);
  if (dirty & RX_DIRTY_FILTER) requestDesign(cfg.low, cfg.high, cfg.stopband);
}

// ----------------------------------------------------------------------------
//  Public
// ----------------------------------------------------------------------------

static void freeAll() {
  free(chain);
  chain = nullptr;
  free(sineTable);
  sineTable = nullptr;
  free(design);
  design = nullptr;
  free(nr);
  nr = nullptr;
  free(load);
  load = nullptr;
  loadInUse = false;
  for (int s = 0; s < 2; s++) {
    for (int p = 0; p < UP_PMAX; p++) {
      free(spectra[s][p]);
      spectra[s][p] = nullptr;
    }
  }
  for (int p = 0; p < UP_PMAX; p++) {
    free(inputSpectra[p]);
    inputSpectra[p] = nullptr;
  }
  dsps_fft2r_deinit_fc32();
}

bool rxChainBegin(const RxConfig &config) {
  rxChainEnd();
  const size_t spectrum = 2 * UP_N * sizeof(float);    // 1 KB
  chain = (Chain *)calloc(1, sizeof(Chain));
  sineTable = (float *)malloc((NCO_TABLE + 1) * sizeof(float));
  design = (Design *)calloc(1, sizeof(Design));
  nr = (Nr *)calloc(1, sizeof(Nr));
  load = (Load *)calloc(1, sizeof(Load));
  bool ok = chain != nullptr && sineTable != nullptr && design != nullptr && nr != nullptr && load != nullptr;
  for (int s = 0; s < 2 && ok; s++) {
    for (int p = 0; p < UP_PMAX && ok; p++) ok = (spectra[s][p] = (float *)calloc(1, spectrum)) != nullptr;
  }
  for (int p = 0; p < UP_PMAX && ok; p++) ok = (inputSpectra[p] = (float *)calloc(1, spectrum)) != nullptr;
  if (ok) {
    // one table serves every length up to its own
    const esp_err_t fft = dsps_fft2r_init_fc32(NULL, NR_N);
    ok = fft == ESP_OK || fft == ESP_ERR_DSP_REINITIALIZED;
  }
  if (!ok) {
    freeAll();
    return false;
  }

  for (int i = 0; i <= NCO_TABLE; i++) sineTable[i] = (float)sin(2.0 * PI_D * i / NCO_TABLE);
  Chain &c = *chain;
  c.cfg = config;
  ddcInit(c.ddc);
  ducInit(c.duc);
  agcInit(c.agc);
  demodInit(c.demod);
  nrInit(*nr);
  c.test.noiseSeed = 12345u;
  c.test.keyEnv = 1.0f;
  c.squelchGain = 1.0f;
  apply(c, RX_DIRTY_ALL & ~RX_DIRTY_FILTER);
  // the first filter at once, so the first block is filtered
  c.act = 0;
  design->generation = 1;
  design->doneGeneration = 1;
  designInto(0, config.low, config.high, config.stopband, 1);
  swapReady = false;
  designs = 1;
  portENTER_CRITICAL(&lock);
  pending = config;
  pendingDirty = 0;
  portEXIT_CRITICAL(&lock);
  memset((void *)&meter, 0, sizeof meter);
  meter.sMeter = DB_FLOOR;
  meter.audioDb = DB_FLOOR;
  designStop = false;
  if (xTaskCreatePinnedToCore(designTaskRun, "rx-design", DESIGN_TASK_STACK, nullptr, DESIGN_TASK_PRIORITY,
                              &designTask, DESIGN_TASK_CORE) != pdPASS) {
    designTask = nullptr;
    freeAll();
    return false;
  }
  return true;
}

// the audio task has stopped calling rxChainProcess before this
void rxChainEnd() {
  if (designTask != nullptr) {
    designStop = true;
    xTaskNotifyGive(designTask);
    for (int i = 0; i < 50 && designTask != nullptr; i++) delay(10);
  }
  if (chain != nullptr || sineTable != nullptr || design != nullptr || nr != nullptr) freeAll();
}

// RX_DIRTY_TAKE: the task takes the configuration even when nothing it
// must recompute changed (the squelch is read every block)
#define RX_DIRTY_TAKE                 0x80000000u

void rxChainConfigure(const RxConfig &config, uint32_t dirty) {
  portENTER_CRITICAL(&lock);
  pending = config;
  pendingDirty |= dirty | RX_DIRTY_TAKE;
  portEXIT_CRITICAL(&lock);
}

void rxChainProcess(const int32_t *in, int16_t *out) {
  Chain &c = *chain;

  // the loop's changes, at the block boundary
  portENTER_CRITICAL(&lock);
  const uint32_t dirty = pendingDirty;
  pendingDirty = 0;
  if (dirty != 0) c.cfg = pending;
  portEXIT_CRITICAL(&lock);
  if (dirty != 0) apply(c, dirty);
  // a finished design goes live
  if (swapReady) {
    c.act = 1 - c.act;
    __sync_synchronize();
    swapReady = false;
    designs = designs + 1;
  }

  // the input: the test signal, or the DSP's serial output
  const RxConfig &cfg = c.cfg;
  const bool iq = cfg.input != RX_INPUT_IF;
  if (cfg.test) {
    testGenerate(c.test, iq, c.inI, c.inQ);
  } else {
    const bool swapped = cfg.input == RX_INPUT_QI;
    for (int n = 0; n < BLOCK; n++) {
      const float left = (float)(in[2 * n] >> 8) * IN_SCALE;
      const float right = (float)(in[2 * n + 1] >> 8) * IN_SCALE;
      c.inI[n] = swapped ? right : left;
      c.inQ[n] = swapped ? left : right;
    }
  }
  if (iq) ddcProcessIq(c.ddc, c.inI, c.inQ, c.bb);
  else ddcProcessReal(c.ddc, c.inI, c.bb);

  filterProcess(c.bb, c.bbf);
  agcProcess(c.agc, c.bbf, c.bba);
  demodProcess(c.demod, cfg.mode, c.bba, c.aud);
  if (cfg.mode != RX_OFF) nrProcess(*nr, c.aud);   // off carries the complex baseband

  float energy = 0.0f;
  for (int i = 0; i < RXB; i++) energy += c.aud[i].re * c.aud[i].re;
  const float audioDb = 10.0f * log10f(energy / RXB + 1e-6f) - DB_RE_32768;

  // the squelch: open at or above the threshold, a one-block ramp
  const bool open = c.agc.peak >= cfg.squelch;
  const float target = open ? 1.0f : 0.0f, step = (target - c.squelchGain) / RXB;
  for (int i = 0; i < RXB; i++) {
    c.squelchGain += step;
    c.aud[i].re *= c.squelchGain;
    c.aud[i].im *= c.squelchGain;
  }
  c.squelchGain = target;

  ducProcess(c.duc, c.aud, c.out48);
  for (int n = 0; n < BLOCK; n++) {
    const float y = constrain(c.out48[n], -32768.0f, 32767.0f);
    const int16_t s = (int16_t)lrintf(y);
    out[2 * n] = s;
    out[2 * n + 1] = s;
  }

  meter.sMeter = c.agc.peak;
  meter.agcGain = c.agc.gain;
  meter.fmOffset = c.demod.fAvg;
  meter.squelchOpen = open;
  meter.audioDb = meter.audioDb + (audioDb - meter.audioDb) * 0.05f;
  meter.taps = c.taps[c.act];
  meter.transition = c.transition[c.act];
  meter.designs = designs;
  meter.designing = designBusy || swapReady;
  meter.nrGainDb = nr->mode == RX_NR_SPECTRAL ? nr->gainDb : 0.0f;
  meter.downloaded = c.downloaded[c.act];
}

void rxChainGetMeter(RxMeter &out) {
  out.sMeter = meter.sMeter;
  out.agcGain = meter.agcGain;
  out.fmOffset = meter.fmOffset;
  out.squelchOpen = meter.squelchOpen;
  out.audioDb = meter.audioDb;
  out.taps = meter.taps;
  out.transition = meter.transition;
  out.designs = meter.designs;
  // also a request the design task has not taken up yet: a download just
  // ended, a design just asked for -- the answer must not call the old filter ready
  out.designing = meter.designing || loadInUse || swapReady
                  || (design != nullptr && design->generation != design->doneGeneration);
  out.nrGainDb = meter.nrGainDb;
  out.downloaded = meter.downloaded;
}

// ----------------------------------------------------------------------------
//  Filter download (the loop)
// ----------------------------------------------------------------------------

RxLoadResult rxChainLoadBegin(uint16_t taps, bool complex) {
  if (load == nullptr) return RX_LOAD_NOT_LOADING;
  if (loadInUse) return RX_LOAD_BUSY;                // the last one is still being transformed
  load->active = true;
  load->complex = complex;
  load->expected = taps;
  load->received = 0;
  load->crc = 0;
  return RX_LOAD_OK;
}

RxLoadResult rxChainLoadData(const float *values, uint8_t count, uint16_t &received) {
  if (load == nullptr || !load->active) return RX_LOAD_NOT_LOADING;
  const uint8_t per = load->complex ? 2 : 1;
  if (count % per != 0 || load->received + count / per > load->expected) {
    received = load->received;
    return RX_LOAD_COUNT;
  }
  load->crc = esp_rom_crc32_le(load->crc, (const uint8_t *)values, count * sizeof(float));
  for (uint8_t i = 0; i < count; i += per) {
    cfloat &t = load->taps[load->received++];
    t.re = values[i];
    t.im = per == 2 ? values[i + 1] : 0.0f;
  }
  received = load->received;
  return RX_LOAD_OK;
}

RxLoadResult rxChainLoadEnd(uint32_t crc, uint16_t &taps) {
  if (load == nullptr || !load->active) return RX_LOAD_NOT_LOADING;
  load->active = false;
  taps = load->received;
  if (load->received != load->expected) return RX_LOAD_COUNT;
  if (load->crc != crc) return RX_LOAD_CRC;
  loadInUse = true;
  portENTER_CRITICAL(&lock);
  design->fromLoad = true;
  design->generation = design->generation + 1;   // volatile: no ++
  portEXIT_CRITICAL(&lock);
  if (designTask != nullptr) xTaskNotifyGive(designTask);
  return RX_LOAD_OK;
}
