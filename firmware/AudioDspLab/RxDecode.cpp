// The decoders run in the receiver's task, beside the chain: -O2 as RxChain.cpp
#pragma GCC optimize("O2")

#include "RxDecode.h"
#include <Arduino.h>
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include "dsps_fft2r.h"

#define FSB                           12000.0f            // the down-converter's output
#define PI_F                          3.14159265f
#define TWO_PI_F                      6.28318531f

// RTTY
#define RTTY_DEC                      6
#define RTTY_RATE                     (FSB / RTTY_DEC)    // 2000 S/s
#define RTTY_TAPS                     95
#define RTTY_BIT_MAX                  64                  // samples a bit, 31 baud and up
#define RTTY_DEC_CAP                  512                 // decisions kept for framing
#define RTTY_TEXT_BLOCK               16                  // characters a block can bring
#define AFC_N                         512                 // 3.9 Hz bins at 2000 S/s
#define AFC_HOP                       256
#define AFC_RANGE                     300.0f

// fax
#define FAX_DEC                       2
#define FAX_RATE                      (FSB / FAX_DEC)     // 6000 S/s: 1.7 samples a pixel at 120 lpm
#define FAX_TAPS                      31
#define FAX_CUTOFF                    1300.0f
#define FAX_SMOOTH                    3                   // 0.5 ms, as the SDR-IQ's 4 at 8 kS/s
#define FAX_DEVIATION                 400.0f
#define FAX_AFC_ALPHA                 (1.0f / 12.0f)      // about 2 ms
#define FAX_HIST_BINS                 161                 // -800 .. +800 Hz in 10 Hz bins
#define FAX_HIST_SAMPLES              (4 * 6000)          // 4 s
#define FAX_TONE_SAMPLES              3000                // half a second
#define FAX_QUEUE                     4
#define FAX_PHASING_LINES             40

struct cpx { float re, im; };

// a mixer: a phasor turned by a fixed step each sample, no sine per sample;
// the step is set and the length restored once a block
struct Osc { float c, s, stepC, stepS; };

// ITA2: letters and figures (0x0E, 0x0F mark the shift codes 27 and 31)
static const char LETTERS[32] = {
  0, 'E', '\n', 'A', ' ', 'S', 'I', 'U', '\r', 'D', 'R', 'J', 'N', 'F', 'C', 'K',
  'T', 'Z', 'L', 'W', 'H', 'Y', 'P', 'Q', 'O', 'B', 'G', 0x0E, 'M', 'X', 'V', 0x0F };
static const char FIGURES[32] = {
  0, '3', '\n', '-', ' ', '\'', '8', '7', '\r', 0x05, '4', '\a', ',', '!', ':', '(',
  '5', '+', ')', '2', '#', '6', '0', '1', '9', '?', '&', 0x0E, '.', '/', '=', 0x0F };
#define ITA2_LTRS                     0x1F
#define ITA2_FIGS                     0x1B
#define ITA2_SPACE                    0x04
#define ITA2_LF                       0x02
#define ITA2_CR                       0x08

// ----------------------------------------------------------------------------
//  State
// ----------------------------------------------------------------------------

struct Framer {
  float d[RTTY_DEC_CAP];                  // decisions since the last start search
  int head, len;
  bool figures;
  uint32_t good, bad, seenGood, seenBad;
  float quality, score;
  char text[RTTY_TEXT_BLOCK];
  int textLength;
};

struct Rtty {
  float shift, baud;
  int bitSamples;
  bool afc;
  uint8_t polarity;
  // mixer and decimator
  Osc mix;
  float h[RTTY_TAPS];
  cpx hist[2 * RTTY_TAPS];
  int histPos, decPhase;
  // AFC
  cpx afcRing[AFC_N];
  int afcPos, afcFill, afcNew, afcSamples, afcFrames, afcFound;
  float afcPower[AFC_N];
  float afcWindow[AFC_N];
  float fft[2 * AFC_N];
  float afcOffset;
  Osc afcOsc;
  // tone sums over one bit
  Osc tone;
  cpx up[RTTY_BIT_MAX], lo[RTTY_BIT_MAX];
  float upRe, upIm, loRe, loIm;
  int win;
  Framer f[2];                            // 0: mark the upper tone, 1: the lower
  bool reverse;
  float decision;
};

struct Fax {
  float lpm, slant;
  uint16_t ioc, width;
  bool afc;
  float samplesPerLine;
  Osc mix;
  float h[FAX_TAPS];
  cpx hist[2 * FAX_TAPS];
  int histPos, decPhase;
  float lastRe, lastIm;
  float smooth[FAX_SMOOTH];
  int smoothAt;
  // AFC
  uint16_t histogram[FAX_HIST_BINS];
  uint32_t histogramCount;
  float afcAverage, afcOffset;
  // lines
  float sum[RX_FAX_WIDTH_MAX];
  uint8_t count[RX_FAX_WIDTH_MAX];
  float position;
  uint8_t line[RX_FAX_WIDTH_MAX];
  uint8_t previous[RX_FAX_WIDTH_MAX];     // the line before: a phased line is its end and this one's start
  uint32_t lines;
  float lineLevel;
  // tones
  float startHz, stopHz;
  int toneN;
  float toneS, toneQ;
  float tc[2], ts[2], cr[2], ci[2], er[2], ei[2], stepC[2], stepS[2];
  int startWindows, stopWindows;
  float startLevel, stopLevel;
  // phasing
  bool phasing;
  int phasingLines;
  float profile[RX_FAX_WIDTH_MAX];
  int phase;                              // -1 none
  uint32_t starts, stops, phases;
  // finished lines for the loop
  uint8_t queue[FAX_QUEUE][RX_FAX_WIDTH_MAX];
  uint32_t queueNumber[FAX_QUEUE];
  int queueHead, queueLength;
};

static Rtty *rtty = nullptr;
static Fax *fax = nullptr;
static uint8_t kind = RX_DECODER_OFF;
static bool noMemory = false;
static int fftSign = 1;                   // +1: bin k holds +k bins of frequency

static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
static char history[RX_TEXT_HISTORY];
static volatile uint32_t textCount = 0;
static RxDecodeStatus shared;             // the loop's copy, under the lock

// ----------------------------------------------------------------------------
//  Helpers
// ----------------------------------------------------------------------------

// a windowed-sinc low-pass (Hamming), unity gain at DC
static void lowpass(float *h, int n, float cutoff, float fs) {
  const float fc = cutoff / fs;
  const int m = n - 1;
  float sum = 0.0f;
  for (int k = 0; k < n; k++) {
    const float x = k - m * 0.5f;
    const float sinc = fabsf(x) < 1e-6f ? 2.0f * fc : sinf(2.0f * PI_F * fc * x) / (PI_F * x);
    h[k] = sinc * (0.54f - 0.46f * cosf(2.0f * PI_F * k / m));
    sum += h[k];
  }
  for (int k = 0; k < n; k++) h[k] /= sum;
}

static inline void oscSet(Osc &o, float hz, float fs) {
  o.stepC = cosf(-TWO_PI_F * hz / fs);    // turning down by hz
  o.stepS = sinf(-TWO_PI_F * hz / fs);
  const float m = o.c * o.c + o.s * o.s;
  if (m < 0.5f) {
    o.c = 1.0f;
    o.s = 0.0f;
  } else {
    const float k = 1.0f / sqrtf(m);
    o.c *= k;
    o.s *= k;
  }
}

// z times the phasor (z mixed down by the step's frequency), then a step on
static inline cpx oscMix(Osc &o, cpx z) {
  const cpx y = { z.re * o.c - z.im * o.s, z.re * o.s + z.im * o.c };
  const float c = o.c * o.stepC - o.s * o.stepS;
  o.s = o.s * o.stepC + o.c * o.stepS;
  o.c = c;
  return y;
}

// a complex FIR with real taps: the history is kept twice, so the taps never wrap
static inline void firPush(cpx *hist, int &pos, int taps, cpx x) {
  hist[pos] = x;
  hist[pos + taps] = x;
  pos = pos + 1 == taps ? 0 : pos + 1;
}

static inline cpx firDot(const float *h, const cpx *hist, int pos, int taps) {
  const cpx *p = hist + pos;              // the oldest first
  float re = 0.0f, im = 0.0f;
  for (int k = 0; k < taps; k++) {
    re += h[taps - 1 - k] * p[k].re;
    im += h[taps - 1 - k] * p[k].im;
  }
  return { re, im };
}

static void appendText(const char *text, int length) {
  if (length <= 0) return;
  portENTER_CRITICAL(&lock);
  uint32_t n = textCount;
  for (int i = 0; i < length; i++) history[n++ % RX_TEXT_HISTORY] = text[i];
  textCount = n;
  portEXIT_CRITICAL(&lock);
}

int rxIta2Code(char c, bool figures) {
  const char *table = figures ? FIGURES : LETTERS;
  for (int i = 0; i < 32; i++) {
    if (table[i] == c && i != ITA2_LTRS && i != ITA2_FIGS && c != 0) return i;
  }
  return -1;
}

// ----------------------------------------------------------------------------
//  RTTY (RttyReceiver, RttyDecoder)
// ----------------------------------------------------------------------------

static inline float framerAt(const Framer &f, int i) { return f.d[(f.head + i) % RTTY_DEC_CAP]; }

static inline void framerPop(Framer &f, int n) {
  if (n > f.len) n = f.len;
  f.head = (f.head + n) % RTTY_DEC_CAP;
  f.len -= n;
}

static char ita2Decode(Framer &f, int code) {
  switch (code) {
    case ITA2_LTRS: f.figures = false; return 0;
    case ITA2_FIGS: f.figures = true; return 0;
    case ITA2_SPACE: f.figures = false; return ' ';     // unshift on space
    case ITA2_LF: return '\n';
    case ITA2_CR: return 0;                             // the LF that follows makes the new line
    case 0: return 0;
  }
  const char c = (f.figures ? FIGURES : LETTERS)[code];
  return c < ' ' ? 0 : c;
}

// one decision; looks for a frame in the history (RttyDecoder.Frame): the
// decision is a one-bit moving sum, so it crosses zero half a bit into the
// start bit and is best for bit i half a bit after that plus i bits
static void framerStep(const Rtty &r, Framer &f, float decision) {
  if (f.len == RTTY_DEC_CAP) framerPop(f, 1);
  f.d[(f.head + f.len) % RTTY_DEC_CAP] = decision;
  f.len++;

  const int b = r.bitSamples, half = b / 2, edge = 1;
  while (f.len >= 2 && !(framerAt(f, 0) > 0.0f && framerAt(f, 1) <= 0.0f)) framerPop(f, 1);
  if (f.len < edge + half + 6 * b + 1) return;

  const float start = framerAt(f, edge + half);
  if (start > -0.1f) {                    // a glitch, not a start bit
    framerPop(f, 1);
    return;
  }
  int code = 0;
  float quality = fabsf(start);
  for (int bit = 0; bit < 5; bit++) {
    const float v = framerAt(f, edge + half + (bit + 1) * b);
    quality += fabsf(v);
    if (v > 0.0f) code |= 1 << bit;
  }
  const float stop = framerAt(f, edge + half + 6 * b);
  quality = (quality + fabsf(stop)) / 7.0f;
  f.quality = 0.9f * f.quality + 0.1f * quality;
  if (stop <= 0.0f) {                     // not a frame: search again from the next sample
    f.bad++;
    framerPop(f, 1);
    return;
  }
  f.good++;
  framerPop(f, edge + 6 * b);
  const char c = ita2Decode(f, code);
  if (c != 0 && f.textLength < RTTY_TEXT_BLOCK) f.text[f.textLength++] = c;
}

static void rttyReset(Rtty &r, const RxDecodeConfig &c) {
  const bool sameShift = r.shift == c.rttyShift && r.baud == c.rttyBaud;
  r.afc = c.afc;
  r.polarity = c.polarity;
  if (sameShift && r.bitSamples != 0) return;
  memset(&r, 0, sizeof(r));
  r.shift = c.rttyShift;
  r.baud = c.rttyBaud;
  r.afc = c.afc;
  r.polarity = c.polarity;
  r.bitSamples = constrain((int)lroundf(RTTY_RATE / c.rttyBaud), 8, RTTY_BIT_MAX);
  // the SDR-IQ's slice: max(600, shift + 6 baud + 2 x 300 Hz) wide
  const float bandwidth = fmaxf(600.0f, c.rttyShift + 6.0f * c.rttyBaud + 2.0f * AFC_RANGE);
  lowpass(r.h, RTTY_TAPS, fminf(0.5f * bandwidth, 0.45f * RTTY_RATE), FSB);
  for (int i = 0; i < AFC_N; i++) r.afcWindow[i] = 0.5f - 0.5f * cosf(2.0f * PI_F * i / AFC_N);
}

// the strongest tone pair, one shift apart, within +/- 300 Hz (RttyReceiver.EstimateOffset)
static void rttyEstimate(Rtty &r) {
  const float binHz = RTTY_RATE / AFC_N;
  float total = 0.0f;
  for (int i = 0; i < AFC_N; i++) total += r.afcPower[i];
  float best = 0.0f, bestScore = 0.0f;
  for (float f = -AFC_RANGE; f <= AFC_RANGE; f += 0.5f * binHz) {
    float score = 0.0f;
    for (int side = -1; side <= 1; side += 2) {
      int bin = fftSign * (int)lroundf((f + side * 0.5f * r.shift) / binHz);
      bin = ((bin % AFC_N) + AFC_N) % AFC_N;
      score += r.afcPower[bin];
    }
    if (score > bestScore) {
      bestScore = score;
      best = f;
    }
  }
  // only a clear tone pair: both tones together well above the average bin
  if (bestScore > 20.0f * total / AFC_N) {
    r.afcFound++;
    r.afcOffset = r.afcFound == 1 ? best : 0.7f * r.afcOffset + 0.3f * best;
  }
}

static void rttyAfcFrame(Rtty &r) {
  for (int i = 0; i < AFC_N; i++) {
    const cpx &z = r.afcRing[(r.afcPos + i) % AFC_N];   // the oldest first
    r.fft[2 * i] = z.re * r.afcWindow[i];
    r.fft[2 * i + 1] = z.im * r.afcWindow[i];
  }
  dsps_fft2r_fc32(r.fft, AFC_N);
  dsps_bit_rev_fc32(r.fft, AFC_N);
  for (int i = 0; i < AFC_N; i++) r.afcPower[i] += r.fft[2 * i] * r.fft[2 * i] + r.fft[2 * i + 1] * r.fft[2 * i + 1];
  r.afcFrames++;
}

// one sample at 2000 S/s
static void rttySample(Rtty &r, cpx z) {
  // AFC: collect (before the shift), a spectrum each 256 samples, an estimate each second
  r.afcRing[r.afcPos] = z;
  r.afcPos = (r.afcPos + 1) % AFC_N;
  if (r.afcFill < AFC_N) r.afcFill++;
  if (++r.afcNew >= AFC_HOP && r.afcFill == AFC_N) {
    r.afcNew = 0;
    rttyAfcFrame(r);
  }
  if (++r.afcSamples >= (int)RTTY_RATE) {
    if (r.afcFrames > 0) rttyEstimate(r);
    memset(r.afcPower, 0, sizeof(r.afcPower));
    r.afcSamples = 0;
    r.afcFrames = 0;
  }
  if (r.afc) z = oscMix(r.afcOsc, z);

  // each tone mixed to 0 Hz and summed over one bit: the upper (+shift/2),
  // and the lower with the same phasor's conjugate
  const float c = r.tone.c, s = r.tone.s;
  const cpx u = oscMix(r.tone, z);
  const cpx l = { z.re * c + z.im * s, z.im * c - z.re * s };
  r.upRe += u.re - r.up[r.win].re;
  r.upIm += u.im - r.up[r.win].im;
  r.loRe += l.re - r.lo[r.win].re;
  r.loIm += l.im - r.lo[r.win].im;
  r.up[r.win] = u;
  r.lo[r.win] = l;
  if (++r.win >= r.bitSamples) {
    // the sums afresh once a bit, so float rounding never builds up
    r.win = 0;
    r.upRe = r.upIm = r.loRe = r.loIm = 0.0f;
    for (int k = 0; k < r.bitSamples; k++) {
      r.upRe += r.up[k].re;
      r.upIm += r.up[k].im;
      r.loRe += r.lo[k].re;
      r.loIm += r.lo[k].im;
    }
  }
  const float m = sqrtf(r.upRe * r.upRe + r.upIm * r.upIm);
  const float n = sqrtf(r.loRe * r.loRe + r.loIm * r.loIm);
  const float d = (m - n) / (m + n + 1e-12f);    // +1: the upper tone
  framerStep(r, r.f[0], d);
  framerStep(r, r.f[1], -d);
  r.decision = r.reverse ? -d : d;
}

static void rttyProcess(Rtty &r, const float *bb, int count, float centreHz) {
  r.f[0].textLength = 0;
  r.f[1].textLength = 0;
  oscSet(r.mix, centreHz, FSB);
  oscSet(r.afcOsc, r.afcOffset, RTTY_RATE);
  oscSet(r.tone, 0.5f * r.shift, RTTY_RATE);
  for (int k = 0; k < count; k++) {
    firPush(r.hist, r.histPos, RTTY_TAPS, oscMix(r.mix, { bb[2 * k], bb[2 * k + 1] }));
    // the low-pass's output at every sixth sample only
    if (++r.decPhase >= RTTY_DEC) {
      r.decPhase = 0;
      rttySample(r, firDot(r.h, r.hist, r.histPos, RTTY_TAPS));
    }
  }
  // the scores: per new frame +1 good, -1 rejected, decaying so a new station is followed
  for (int p = 0; p < 2; p++) {
    Framer &f = r.f[p];
    f.score = 0.995f * f.score + (float)(f.good - f.seenGood) - (float)(f.bad - f.seenBad);
    f.seenGood = f.good;
    f.seenBad = f.bad;
  }
  r.reverse = r.polarity == RX_POLARITY_NORMAL ? false
            : r.polarity == RX_POLARITY_REVERSE ? true
            : r.f[1].score > r.f[0].score;
  // quiet until the speaking decoder has a few more good frames than bad ones
  const Framer &active = r.f[r.reverse ? 1 : 0];
  if (active.score > 3.0f) appendText(active.text, active.textLength);
}

// ----------------------------------------------------------------------------
//  Fax (FaxDecoder)
// ----------------------------------------------------------------------------

static void faxTiming(Fax &x) {
  // slant: the line length in ppm; the line timing follows the board's crystal
  x.samplesPerLine = FAX_RATE * 60.0f / x.lpm * (1.0f + x.slant * 1e-6f);
}

static void faxToneSetup(Fax &x) {
  const float hz[2] = { x.startHz, x.stopHz };
  for (int t = 0; t < 2; t++) {
    x.stepC[t] = cosf(2.0f * PI_F * hz[t] / FAX_RATE);
    x.stepS[t] = sinf(2.0f * PI_F * hz[t] / FAX_RATE);
    x.tc[t] = 1.0f;
    x.ts[t] = 0.0f;
    x.cr[t] = x.ci[t] = x.er[t] = x.ei[t] = 0.0f;
  }
  x.toneN = 0;
  x.toneS = x.toneQ = 0.0f;
}

static void faxReset(Fax &x, const RxDecodeConfig &c) {
  const bool same = x.width != 0 && x.ioc == c.faxIoc;
  x.lpm = c.faxLpm;
  x.slant = c.faxSlant;
  x.afc = c.afc;
  faxTiming(x);
  if (same) return;
  memset(&x, 0, sizeof(x));
  x.lpm = c.faxLpm;
  x.slant = c.faxSlant;
  x.afc = c.afc;
  x.ioc = c.faxIoc;
  x.width = (uint16_t)min((int)lroundf(c.faxIoc * PI_F), RX_FAX_WIDTH_MAX);
  faxTiming(x);
  lowpass(x.h, FAX_TAPS, FAX_CUTOFF, FSB);
  x.lastRe = 1.0f;
  x.phase = -1;
  // the start tone: 300 Hz at IOC 576, 675 Hz at 288; the stop tone 450 Hz
  x.startHz = c.faxIoc == 288 ? 675.0f : 300.0f;
  x.stopHz = 450.0f;
  faxToneSetup(x);
}

// the phasing pulse: the 5 %-wide column band that differs most from the median
static void faxFindPhase(Fax &x) {
  const int w = x.width;
  uint16_t levels[256] = {};
  for (int i = 0; i < w; i++) levels[constrain((int)lroundf(x.profile[i] / x.phasingLines), 0, 255)]++;
  int median = 0;
  for (int seen = 0; median < 256; median++) {
    seen += levels[median];
    if (seen > w / 2) break;
  }
  const float m = (float)median * x.phasingLines;
  const int pulse = max(4, w / 20);
  float window = 0.0f, best = 0.0f;
  int bestX = 0;
  for (int i = 0; i < w + pulse; i++) {
    window += x.profile[i % w] - m;
    if (i >= pulse) {
      window -= x.profile[(i - pulse) % w] - m;
      if (fabsf(window) > best) {
        best = fabsf(window);
        bestX = (i - pulse + 1) % w;      // the pulse's first column
      }
    }
  }
  x.phase = (bestX + pulse / 2) % w;      // the line starts at the pulse's centre
  x.phases++;
}

static void faxEmitLine(Fax &x) {
  const int w = x.width;
  float total = 0.0f, last = 0.5f;
  for (int i = 0; i < w; i++) {
    const float v = x.count[i] > 0 ? x.sum[i] / x.count[i] : last;
    last = v;
    total += v;
    x.line[i] = (uint8_t)lroundf(v * 255.0f);
  }
  x.lineLevel = total / w;
  memset(x.sum, 0, sizeof(float) * w);
  memset(x.count, 0, w);

  if (x.phasing) {
    for (int i = 0; i < w; i++) x.profile[i] += x.line[i];
    if (++x.phasingLines >= FAX_PHASING_LINES) {
      faxFindPhase(x);
      x.phasing = false;
    }
  }

  const int phase = x.phase < 0 ? 0 : x.phase;
  portENTER_CRITICAL(&lock);
  if (x.queueLength == FAX_QUEUE) {       // the loop fell behind: the oldest goes
    x.queueHead = (x.queueHead + 1) % FAX_QUEUE;
    x.queueLength--;
  }
  const int slot = (x.queueHead + x.queueLength) % FAX_QUEUE;
  uint8_t *out = x.queue[slot];
  // the transmitted line from its start: the received line before from the
  // phase on, then this one up to the phase (rotating one received line
  // would put a one-line step into the chart at column w - phase)
  for (int i = 0; i < w; i++) out[i] = i + phase < w ? x.previous[i + phase] : x.line[i + phase - w];
  x.queueNumber[slot] = x.lines;
  x.queueLength++;
  portEXIT_CRITICAL(&lock);
  memcpy(x.previous, x.line, w);
  x.lines++;
}

// the start and stop tones' share of the grey signal's AC power, each half second
static void faxTones(Fax &x, float grey) {
  const float v = grey - 0.5f;
  x.toneS += v;
  x.toneQ += v * v;
  for (int t = 0; t < 2; t++) {
    x.cr[t] += v * x.tc[t];
    x.ci[t] += v * x.ts[t];
    x.er[t] += x.tc[t];
    x.ei[t] += x.ts[t];
    const float c = x.tc[t] * x.stepC[t] - x.ts[t] * x.stepS[t];
    x.ts[t] = x.ts[t] * x.stepC[t] + x.tc[t] * x.stepS[t];
    x.tc[t] = c;
  }
  if (++x.toneN < FAX_TONE_SAMPLES) return;

  const float n = (float)x.toneN, mean = x.toneS / n;
  const float ac = x.toneQ - n * mean * mean;
  float share[2];
  for (int t = 0; t < 2; t++) {
    const float cr = x.cr[t] - mean * x.er[t], ci = x.ci[t] - mean * x.ei[t];
    // a quiet grey signal (white paper) has no tone, whatever its rounding says
    share[t] = ac / n > 0.01f ? 2.0f * (cr * cr + ci * ci) / n / ac : 0.0f;
  }
  x.startLevel = share[0];
  x.stopLevel = share[1];
  faxToneSetup(x);

  x.startWindows = x.startLevel > 0.4f ? x.startWindows + 1 : 0;
  x.stopWindows = x.stopLevel > 0.4f ? x.stopWindows + 1 : 0;
  if (x.startWindows == 4) {              // 2 s of start tone: a new chart
    x.phase = -1;
    x.phasing = true;
    x.starts++;
  }
  if (x.stopWindows == 4) {
    x.phasing = false;
    x.stops++;
  }
  if (x.startWindows > 0) {               // phasing starts when the start tone ends
    memset(x.profile, 0, sizeof(float) * x.width);
    x.phasingLines = 0;
  }
}

// white is the commonest level of a chart: the histogram's peak between +200
// and +700 Hz is put at +400 Hz, when it stands four times above the average
static void faxHistogram(Fax &x, float hz) {
  const int bin = (int)lroundf((hz + 800.0f) / 10.0f);
  if (bin >= 0 && bin < FAX_HIST_BINS) x.histogram[bin]++;
  if (++x.histogramCount < FAX_HIST_SAMPLES) return;
  int best = -1;
  uint32_t bestCount = 0;
  for (int b = (200 + 800) / 10; b <= (700 + 800) / 10; b++) {
    const uint32_t c = x.histogram[b - 1] + x.histogram[b] + x.histogram[b + 1];
    if (c > bestCount) {
      bestCount = c;
      best = b;
    }
  }
  const float average = 3.0f * x.histogramCount / FAX_HIST_BINS;
  if (best >= 0 && bestCount > 4.0f * average) {
    const float error = (best * 10.0f - 800.0f) - FAX_DEVIATION;
    x.afcOffset = constrain(x.afcOffset + 0.5f * error, -AFC_RANGE, AFC_RANGE);
  }
  memset(x.histogram, 0, sizeof(x.histogram));
  x.histogramCount = 0;
}

static void faxProcess(Fax &x, const float *bb, int count, float centreHz) {
  oscSet(x.mix, centreHz + (x.afc ? x.afcOffset : 0.0f), FSB);
  const float scale = FAX_RATE / TWO_PI_F;
  const int w = x.width;
  for (int k = 0; k < count; k++) {
    firPush(x.hist, x.histPos, FAX_TAPS, oscMix(x.mix, { bb[2 * k], bb[2 * k + 1] }));
    if (++x.decPhase < FAX_DEC) continue;     // the low-pass's output at every other sample
    x.decPhase = 0;
    const cpx z = firDot(x.h, x.hist, x.histPos, FAX_TAPS);

    // FM: the phase step between samples
    const float pr = z.re * x.lastRe + z.im * x.lastIm;
    const float pi = z.im * x.lastRe - z.re * x.lastIm;
    x.lastRe = z.re;
    x.lastIm = z.im;
    x.smooth[x.smoothAt] = atan2f(pi, pr) * scale;
    x.smoothAt = (x.smoothAt + 1) % FAX_SMOOTH;
    float hz = 0.0f;
    for (int i = 0; i < FAX_SMOOTH; i++) hz += x.smooth[i];
    hz *= 1.0f / FAX_SMOOTH;

    x.afcAverage += (hz - x.afcAverage) * FAX_AFC_ALPHA;
    faxHistogram(x, x.afcAverage);
    // 0 at -400 Hz, 1 at +400 Hz; 10 % and below black, 90 % and above white
    const float grey = constrain(((hz + FAX_DEVIATION) / (2.0f * FAX_DEVIATION) - 0.1f) / 0.8f, 0.0f, 1.0f);
    faxTones(x, grey);

    const int pixel = (int)(x.position / x.samplesPerLine * w);
    if (pixel >= 0 && pixel < w && x.count[pixel] < 255) {
      x.sum[pixel] += grey;
      x.count[pixel]++;
    }
    x.position += 1.0f;
    if (x.position >= x.samplesPerLine) {
      x.position -= x.samplesPerLine;
      faxEmitLine(x);
    }
  }
}

// ----------------------------------------------------------------------------
//  Public
// ----------------------------------------------------------------------------

// esp-dsp's sign: a tone at +10 bins of frequency lands in bin 10 or in N - 10
static void detectFftSign(float *data) {
  for (int i = 0; i < AFC_N; i++) {
    data[2 * i] = cosf(2.0f * PI_F * 10 * i / AFC_N);
    data[2 * i + 1] = sinf(2.0f * PI_F * 10 * i / AFC_N);
  }
  dsps_fft2r_fc32(data, AFC_N);
  dsps_bit_rev_fc32(data, AFC_N);
  const float plus = data[20] * data[20] + data[21] * data[21];
  const float minus = data[2 * (AFC_N - 10)] * data[2 * (AFC_N - 10)] + data[2 * (AFC_N - 10) + 1] * data[2 * (AFC_N - 10) + 1];
  fftSign = plus >= minus ? 1 : -1;
}

void rxDecodeFree() {
  portENTER_CRITICAL(&lock);
  Rtty *r = rtty;
  Fax *x = fax;
  rtty = nullptr;
  fax = nullptr;
  kind = RX_DECODER_OFF;
  portEXIT_CRITICAL(&lock);
  free(r);
  free(x);
}

bool rxDecodeConfigure(const RxDecodeConfig &config) {
  if (config.decoder != kind) {
    rxDecodeFree();
    noMemory = false;
    if (config.decoder == RX_DECODER_RTTY) {
      Rtty *r = (Rtty *)calloc(1, sizeof(Rtty));
      if (r == nullptr) {
        noMemory = true;
        return false;
      }
      detectFftSign(r->fft);
      portENTER_CRITICAL(&lock);
      rtty = r;
      kind = RX_DECODER_RTTY;
      portEXIT_CRITICAL(&lock);
    } else if (config.decoder == RX_DECODER_FAX) {
      Fax *x = (Fax *)calloc(1, sizeof(Fax));
      if (x == nullptr) {
        noMemory = true;
        return false;
      }
      portENTER_CRITICAL(&lock);
      fax = x;
      kind = RX_DECODER_FAX;
      portEXIT_CRITICAL(&lock);
    }
  }
  if (rtty != nullptr) rttyReset(*rtty, config);
  if (fax != nullptr) faxReset(*fax, config);
  return true;
}

void rxDecodeProcess(const float *baseband, int count, float centreHz) {
  RxDecodeStatus s = {};
  s.decoder = kind;
  s.noMemory = noMemory;
  s.phase = -1;
  if (rtty != nullptr) {
    Rtty &r = *rtty;
    rttyProcess(r, baseband, count, centreHz);
    const Framer &f = r.f[r.reverse ? 1 : 0];
    s.afcHz = r.afcOffset;
    s.quality = f.quality;
    s.reverse = r.reverse;
    s.good = f.good;
    s.bad = f.bad;
    s.decision = r.decision;
  }
  if (fax != nullptr) {
    Fax &x = *fax;
    faxProcess(x, baseband, count, centreHz);
    s.afcHz = x.afcOffset;
    s.lines = x.lines;
    s.width = x.width;
    s.startLevel = x.startLevel;
    s.stopLevel = x.stopLevel;
    s.phasing = x.phasing;
    s.phase = (int16_t)x.phase;
    s.starts = x.starts;
    s.stops = x.stops;
    s.phases = x.phases;
    s.lineLevel = x.lineLevel;
  }
  portENTER_CRITICAL(&lock);
  shared = s;
  portEXIT_CRITICAL(&lock);
}

size_t rxDecodeText(uint32_t &cursor, char *out, size_t max) {
  size_t n = 0;
  portENTER_CRITICAL(&lock);
  const uint32_t end = textCount;
  if (end - cursor > RX_TEXT_HISTORY) cursor = end - RX_TEXT_HISTORY;
  while (cursor != end && n + 1 < max) out[n++] = history[cursor++ % RX_TEXT_HISTORY];
  portEXIT_CRITICAL(&lock);
  if (max > 0) out[n] = '\0';
  return n;
}

uint32_t rxDecodeTextCount() {
  return textCount;
}

void rxDecodeGetStatus(RxDecodeStatus &status) {
  portENTER_CRITICAL(&lock);
  status = shared;
  if (kind == RX_DECODER_OFF) status.decoder = RX_DECODER_OFF;
  portEXIT_CRITICAL(&lock);
  status.noMemory = noMemory;
}

bool rxDecodeTakeLine(uint8_t *pixels, uint16_t &width, uint32_t &number) {
  bool taken = false;
  portENTER_CRITICAL(&lock);
  if (fax != nullptr && fax->queueLength > 0) {
    Fax &x = *fax;
    const int slot = x.queueHead;
    width = x.width;
    number = x.queueNumber[slot];
    memcpy(pixels, x.queue[slot], width);
    x.queueHead = (x.queueHead + 1) % FAX_QUEUE;
    x.queueLength--;
    taken = true;
  }
  portEXIT_CRITICAL(&lock);
  return taken;
}

// ============================================================================
// ==================================  E O F  =================================
// ============================================================================
