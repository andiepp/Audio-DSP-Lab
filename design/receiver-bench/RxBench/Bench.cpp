// RxBench.ino -- ESP32 timing of the TMS320-DSP-Box receiver chain
// (H:\TMS320-DSP-Box\projects\dspbox_rx) for one 5.33 ms block:
// 256 I/Q samples in at 48 kHz -> 64 complex at 12 kHz -> 256 audio out.
// Stages are ported 1:1 from rx.c, chan.c, agc.c, demod.c, nr.c; variants
// of the channel filter use esp-dsp (dot products, FIR) and uniformly
// partitioned FFT convolution, checked against the direct form.
// Runs in a task pinned to core 1; prints mean / max microseconds per block.

#include <Arduino.h>
#include <math.h>
#include <string.h>
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "dsps_fft2r.h"
#include "dsps_fir.h"
#include "dsps_dotprod.h"

#define BLOCK 256
#define DEC   4
#define RXB   64
#define FS    48000.0f
#define FSB   12000.0f
#define PI_D  3.14159265358979
#define PI_F  3.14159265f
#define BLOCK_US 5333.33f

struct cfloat { float re, im; };

// ------------------------------------------------------------------ NCO ---
#define NCO_TABLE 1024
static float sineTable[NCO_TABLE + 1];
static void nco_initTable() {
  for (int i = 0; i <= NCO_TABLE; i++) sineTable[i] = (float)sin(2.0 * PI_D * i / NCO_TABLE);
}
static uint32_t nco_inc(float f, float fs) {
  double x = (double)f / (double)fs;
  x -= floor(x);
  return (uint32_t)(x * 4294967296.0);
}
static inline float nco_sin(uint32_t ph) {
  uint32_t k = ph >> 22;
  float fr = (float)(ph & 0x3FFFFF) * (1.0f / 4194304.0f);
  return sineTable[k] + fr * (sineTable[k + 1] - sineTable[k]);
}
static inline void nco_cossin(uint32_t ph, float *c, float *s) {
  *s = nco_sin(ph);
  *c = nco_sin(ph + 0x40000000u);
}

// ----------------------------------------------------------- Kaiser FIR ---
static double bessel_i0(double x) {
  double sum = 1.0, term = 1.0, q = x * x / 4.0;
  for (int k = 1; k < 40; k++) { term *= q / ((double)k * k); sum += term; if (term < sum * 1e-12) break; }
  return sum;
}
static double kaiser_beta(double A) {
  if (A > 50.0) return 0.1102 * (A - 8.7);
  if (A >= 21.0) return 0.5842 * pow(A - 21.0, 0.4) + 0.07886 * (A - 21.0);
  return 0.0;
}
static void kaiser_lowpass(float *h, int n, float fc, float fs, float A, float gain) {
  double beta = kaiser_beta(A), i0b = bessel_i0(beta);
  double m = (n - 1) / 2.0, wc = 2.0 * PI_D * fc / fs, sum = 0.0;
  for (int k = 0; k < n; k++) {
    double t = k - m, r = t / m;
    double sinc = (t == 0.0) ? wc / PI_D : sin(wc * t) / (PI_D * t);
    double w = bessel_i0(beta * sqrt(1.0 - r * r)) / i0b;
    h[k] = (float)(sinc * w); sum += h[k];
  }
  for (int k = 0; k < n; k++) h[k] = (float)(h[k] * gain / sum);
}

// ------------------------------------------------------ down-converter ---
#define DDC_TAPS 43
struct Ddc {
  uint32_t phase, inc;
  float h[DDC_TAPS];
  cfloat buf[DDC_TAPS - 1 + BLOCK];
  float dcI, dcQ, kq, ki;
};
static Ddc ddc;

static void ddc_init(Ddc *d, float fIF) {
  memset(d, 0, sizeof *d);
  d->inc = nco_inc(fIF, FS);
  kaiser_lowpass(d->h, DDC_TAPS, 6000.0f, FS, 80.0f, 1.0f);
  float g = powf(10.0f, 0.3f / 20.0f), p = 2.0f * (PI_F / 180.0f);   // some correction
  d->kq = 1.0f / (g * cosf(p)); d->ki = -tanf(p);
}

static void ddc_filter(Ddc *d, cfloat *out) {
  cfloat *z = d->buf + DDC_TAPS - 1;
  const float *h = d->h;
  for (int m = 0; m < RXB; m++) {
    const cfloat *p = z + DEC * m + (DEC - 1);
    float re = 0.0f, im = 0.0f;
    for (int k = 0; k < DDC_TAPS; k++) { re += h[k] * p[-k].re; im += h[k] * p[-k].im; }
    out[m].re = re; out[m].im = im;
  }
  for (int i = 0; i < DDC_TAPS - 1; i++) d->buf[i] = z[BLOCK - (DDC_TAPS - 1) + i];
}

static void ddc_process_iq(Ddc *d, const float *iIn, const float *qIn, cfloat *out) {
  cfloat *z = d->buf + DDC_TAPS - 1;
  uint32_t ph = d->phase, inc = d->inc;
  float dcI = d->dcI, dcQ = d->dcQ, kq = d->kq, ki = d->ki;
  const float k = 1.0f / 4096.0f;
  for (int i = 0; i < BLOCK; i++) {
    float c, s, x, y;
    dcI += (iIn[i] - dcI) * k; dcQ += (qIn[i] - dcQ) * k;
    x = iIn[i] - dcI;
    y = kq * (qIn[i] - dcQ) + ki * x;
    nco_cossin(ph, &c, &s);
    z[i].re = x * c + y * s;
    z[i].im = y * c - x * s;
    ph += inc;
  }
  d->phase = ph; d->dcI = dcI; d->dcQ = dcQ;
  ddc_filter(d, out);
}

// variant: mixing as above, low-pass with two esp-dsp decimating FIRs
static float zRe[BLOCK], zIm[BLOCK], dRe[RXB], dIm[RXB];
static float firdCoef[DDC_TAPS], firdDelRe[DDC_TAPS + 4], firdDelIm[DDC_TAPS + 4];
static fir_f32_t firdRe, firdIm;
static void ddc_process_iq_dsps(Ddc *d, const float *iIn, const float *qIn, cfloat *out) {
  uint32_t ph = d->phase, inc = d->inc;
  float dcI = d->dcI, dcQ = d->dcQ, kq = d->kq, ki = d->ki;
  const float k = 1.0f / 4096.0f;
  for (int i = 0; i < BLOCK; i++) {
    float c, s, x, y;
    dcI += (iIn[i] - dcI) * k; dcQ += (qIn[i] - dcQ) * k;
    x = iIn[i] - dcI;
    y = kq * (qIn[i] - dcQ) + ki * x;
    nco_cossin(ph, &c, &s);
    zRe[i] = x * c + y * s;
    zIm[i] = y * c - x * s;
    ph += inc;
  }
  d->phase = ph; d->dcI = dcI; d->dcQ = dcQ;
  dsps_fird_f32_ae32(&firdRe, zRe, dRe, RXB);
  dsps_fird_f32_ae32(&firdIm, zIm, dIm, RXB);
  for (int m = 0; m < RXB; m++) { out[m].re = dRe[m]; out[m].im = dIm[m]; }
}

// -------------------------------------------------------- up-converter ---
#define DUC_TAPS 43
#define DUC_HIST ((DUC_TAPS + DEC - 1) / DEC)
struct Duc { float h[DUC_TAPS]; cfloat buf[DUC_HIST + RXB]; };
static Duc duc;
static void duc_init(Duc *u) {
  memset(u, 0, sizeof *u);
  kaiser_lowpass(u->h, DUC_TAPS, 6000.0f, FS, 80.0f, (float)DEC);
}
static void duc_process(Duc *u, const cfloat *in, float *out) {   // fIF = 0: audio
  cfloat *x = u->buf + DUC_HIST;
  const float *h = u->h;
  for (int m = 0; m < RXB; m++) x[m] = in[m];
  for (int m = 0; m < RXB; m++)
    for (int p = 0; p < DEC; p++) {
      float re = 0.0f;
      for (int k = p, i = 0; k < DUC_TAPS; k += DEC, i++) re += h[k] * x[m - i].re;
      out[DEC * m + p] = re;
    }
  for (int i = 0; i < DUC_HIST; i++) u->buf[i] = x[RXB - DUC_HIST + i];
}

// ------------------------------------------------------ channel filter ---
#define CHAN_MAX 511
static cfloat chH[CHAN_MAX];                    // time-reversed, as chan.c
static cfloat chNat[CHAN_MAX];                  // natural order h[k]
static int chN;
static cfloat chBuf[CHAN_MAX - 1 + RXB];

static float i0f(float x) {
  float sum = 1.0f, term = 1.0f, q = x * x * 0.25f;
  for (int k = 1; k < 50; k++) { term *= q / ((float)k * k); sum += term; if (term < sum * 1e-8f) break; }
  return sum;
}

// chan_request + chan_step for a given tap count (bench: n forced)
static float protoTap(int k, int n, float wc, float beta, float i0b) {
  float m = 0.5f * (n - 1), t = k - m, r = t / m;
  float sinc = (t == 0.0f) ? wc / PI_F : sinf(wc * t) / (PI_F * t);
  float w = i0f(beta * sqrtf(1.0f - r * r)) / i0b;
  return sinc * w;
}
static void chan_design(int n, float lo, float hi, float A) {
  float bw = hi - lo, fc = 0.5f * (lo + hi);
  float beta = (A > 50.0f) ? 0.1102f * (A - 8.7f) : 0.0f;
  float i0b = i0f(beta), wc = 2.0f * PI_F * (0.5f * bw) / FSB, m = 0.5f * (n - 1);
  static float proto[CHAN_MAX];
  double sum = 0.0;
  for (int k = 0; k < n; k++) { proto[k] = protoTap(k, n, wc, beta, i0b); sum += proto[k]; }
  uint32_t inc = nco_inc(fc, FSB), ph = inc * (uint32_t)(-(int)m);
  float g = (float)(1.0 / sum);
  for (int k = 0; k < n; k++) {
    float cs, sn, p = proto[k] * g;
    nco_cossin(ph, &cs, &sn);
    chNat[k].re = p * cs; chNat[k].im = p * sn;
    chH[n - 1 - k] = chNat[k];
    ph += inc;
  }
  chN = n;
  memset(chBuf, 0, sizeof chBuf);
}

static void chan_process(const cfloat *in, cfloat *out) {          // direct, chan.c
  cfloat *x = chBuf + CHAN_MAX - 1;
  const cfloat *h = chH;
  int n = chN;
  for (int m = 0; m < RXB; m++) x[m] = in[m];
  for (int m = 0; m < RXB; m++) {
    const cfloat *xp = x + m - (n - 1);
    float re = 0.0f, im = 0.0f;
    for (int j = 0; j < n; j++) {
      float hr = h[j].re, hi = h[j].im, xr = xp[j].re, xi = xp[j].im;
      re += hr * xr - hi * xi;
      im += hr * xi + hi * xr;
    }
    out[m].re = re; out[m].im = im;
  }
  for (int m = 0; m < CHAN_MAX - 1; m++) chBuf[m] = x[RXB - (CHAN_MAX - 1) + m];
}

// variant: split re / im arrays, four esp-dsp ae32 dot products per output
static float chHr[CHAN_MAX], chHi[CHAN_MAX];
static float chXr[CHAN_MAX - 1 + RXB], chXi[CHAN_MAX - 1 + RXB];
static void chan_split() {
  for (int j = 0; j < chN; j++) { chHr[j] = chH[j].re; chHi[j] = chH[j].im; }
  memset(chXr, 0, sizeof chXr); memset(chXi, 0, sizeof chXi);
}
static void chan_process_dot(const cfloat *in, cfloat *out) {
  float *xr = chXr + CHAN_MAX - 1, *xi = chXi + CHAN_MAX - 1;
  int n = chN;
  for (int m = 0; m < RXB; m++) { xr[m] = in[m].re; xi[m] = in[m].im; }
  for (int m = 0; m < RXB; m++) {
    const float *pr = xr + m - (n - 1), *pi = xi + m - (n - 1);
    float a, b, c, d;
    dsps_dotprod_f32_ae32(chHr, pr, &a, n);
    dsps_dotprod_f32_ae32(chHi, pi, &b, n);
    dsps_dotprod_f32_ae32(chHr, pi, &c, n);
    dsps_dotprod_f32_ae32(chHi, pr, &d, n);
    out[m].re = a - b; out[m].im = c + d;
  }
  memmove(chXr, chXr + RXB, (CHAN_MAX - 1) * sizeof(float));
  memmove(chXi, chXi + RXB, (CHAN_MAX - 1) * sizeof(float));
}

// variant: four esp-dsp ae32 real FIRs (hr*xr - hi*xi, hr*xi + hi*xr)
static fir_f32_t fRR, fII, fRI, fIR;
static float fCr[CHAN_MAX], fCi[CHAN_MAX];
static float fD1[CHAN_MAX + 4], fD2[CHAN_MAX + 4], fD3[CHAN_MAX + 4], fD4[CHAN_MAX + 4];
static float tXr[RXB], tXi[RXB], fy1[RXB], fy2[RXB], fy3[RXB], fy4[RXB];
static int fCoefReversed = 0;
static void chan_fir_init() {
  for (int k = 0; k < chN; k++) {
    int j = fCoefReversed ? chN - 1 - k : k;
    fCr[k] = chNat[j].re; fCi[k] = chNat[j].im;
  }
  memset(fD1, 0, sizeof fD1); memset(fD2, 0, sizeof fD2); memset(fD3, 0, sizeof fD3); memset(fD4, 0, sizeof fD4);
  dsps_fir_init_f32(&fRR, fCr, fD1, chN);
  dsps_fir_init_f32(&fII, fCi, fD2, chN);
  dsps_fir_init_f32(&fRI, fCr, fD3, chN);
  dsps_fir_init_f32(&fIR, fCi, fD4, chN);
}
static void chan_process_fir(const cfloat *in, cfloat *out) {
  for (int m = 0; m < RXB; m++) { tXr[m] = in[m].re; tXi[m] = in[m].im; }
  dsps_fir_f32_ae32(&fRR, tXr, fy1, RXB);
  dsps_fir_f32_ae32(&fII, tXi, fy2, RXB);
  dsps_fir_f32_ae32(&fRI, tXi, fy3, RXB);
  dsps_fir_f32_ae32(&fIR, tXr, fy4, RXB);
  for (int m = 0; m < RXB; m++) { out[m].re = fy1[m] - fy2[m]; out[m].im = fy3[m] + fy4[m]; }
}

// variant: uniformly partitioned overlap-save, block 64, FFT 128 (complex)
#define UP_N 128
#define UP_PMAX 8
static float upH[UP_PMAX][2 * UP_N];      // partition spectra, interleaved
static float upX[UP_PMAX][2 * UP_N];      // frequency-domain delay line
static float upT[2 * UP_N];               // time buffer / work
static cfloat upPrev[RXB];
static int upP, upHead;
static void fft128(float *d) { dsps_fft2r_fc32(d, UP_N); dsps_bit_rev_fc32(d, UP_N); }
static void upols_init() {
  upP = (chN + RXB - 1) / RXB;
  for (int p = 0; p < upP; p++) {
    memset(upH[p], 0, sizeof upH[p]);
    for (int k = 0; k < RXB; k++) {
      int j = p * RXB + k;
      if (j < chN) { upH[p][2 * k] = chNat[j].re; upH[p][2 * k + 1] = chNat[j].im; }
    }
    fft128(upH[p]);
  }
  memset(upX, 0, sizeof upX);
  memset(upPrev, 0, sizeof upPrev);
  upHead = 0;
}
static void upols_process(const cfloat *in, cfloat *out) {
  float *X = upX[upHead];
  for (int k = 0; k < RXB; k++) {
    X[2 * k] = upPrev[k].re; X[2 * k + 1] = upPrev[k].im;
    X[2 * (RXB + k)] = in[k].re; X[2 * (RXB + k) + 1] = in[k].im;
    upPrev[k] = in[k];
  }
  fft128(X);
  // Y = sum_p H_p X_(t-p), accumulated conjugated for the inverse FFT
  for (int b = 0; b < UP_N; b++) { upT[2 * b] = 0.0f; upT[2 * b + 1] = 0.0f; }
  for (int p = 0; p < upP; p++) {
    const float *H = upH[p], *Xp = upX[(upHead - p + UP_PMAX) % UP_PMAX];
    for (int b = 0; b < UP_N; b++) {
      float hr = H[2 * b], hi = H[2 * b + 1], xr = Xp[2 * b], xi = Xp[2 * b + 1];
      upT[2 * b]     += hr * xr - hi * xi;
      upT[2 * b + 1] += hr * xi + hi * xr;
    }
  }
  for (int b = 0; b < UP_N; b++) upT[2 * b + 1] = -upT[2 * b + 1];
  fft128(upT);
  const float s = 1.0f / UP_N;
  for (int k = 0; k < RXB; k++) { out[k].re = upT[2 * (RXB + k)] * s; out[k].im = -upT[2 * (RXB + k) + 1] * s; }
  upHead = (upHead + 1) % UP_PMAX;
}

// ------------------------------------------------------------------ AGC ---
#define AGC_LOOK 64
#define DB_FLOOR -150.0f
#define LN10_20 0.11512925f
struct Agc { cfloat d[AGC_LOOK]; int idx, on; float maxGain; int hangN, hang; float decay, peak, gain, alpha, outPeak; };
static Agc agc;
static void agc_init(Agc *a) {
  memset(a, 0, sizeof *a);
  a->peak = DB_FLOOR; a->alpha = 1.0f - expf(-1.0f / (0.001f * FSB));
  a->on = 1; a->maxGain = 60.0f; a->hangN = (int)(0.5f * FSB); a->decay = 20.0f / FSB;
}
static void agc_process(Agc *a, const cfloat *in, cfloat *out) {
  float peak = a->peak, gain = a->gain, alpha = a->alpha, decay = a->decay, maxGain = a->maxGain, outPk = 0.0f;
  int hang = a->hang, hangN = a->hangN, idx = a->idx, on = a->on;
  for (int i = 0; i < RXB; i++) {
    float e2 = in[i].re * in[i].re + in[i].im * in[i].im;
    float eDb = 10.0f * log10f(e2 + 1e-12f) - 90.309f;
    float target, g, o2;
    if (eDb > peak) { peak = eDb; hang = hangN; }
    else if (hang > 0) hang--;
    else peak -= decay;
    if (peak < DB_FLOOR) peak = DB_FLOOR;
    target = on ? -12.0f - peak : maxGain;
    if (target > maxGain) target = maxGain;
    gain += (target - gain) * alpha;
    g = expf(gain * LN10_20);
    out[i].re = a->d[idx].re * g; out[i].im = a->d[idx].im * g;
    a->d[idx] = in[i];
    if (++idx >= AGC_LOOK) idx = 0;
    o2 = out[i].re * out[i].re + out[i].im * out[i].im;
    if (o2 > outPk) outPk = o2;
  }
  a->peak = peak; a->gain = gain; a->hang = hang; a->idx = idx; a->outPeak = sqrtf(outPk);
}

// ---------------------------------------------------------- demodulators ---
#define DC_R 0.97416f
struct Demod { float x1, y1; cfloat zp; float fmScale, fAvg, deBeta, de; };
static Demod dem;
static void demod_init(Demod *d) {
  memset(d, 0, sizeof *d);
  d->fmScale = 8192.0f / 2500.0f;
  d->deBeta = 1.0f - expf(-1.0f / (750e-6f * FSB));
}
static void demod_fm(Demod *d, const cfloat *in, cfloat *audio) {
  float x1 = d->x1, y1 = d->y1, fAvg = d->fAvg, de = d->de;
  float k = FSB / (2.0f * PI_F), beta = d->deBeta, sc = d->fmScale;
  cfloat zp = d->zp;
  for (int i = 0; i < RXB; i++) {
    float pr = in[i].re * zp.re + in[i].im * zp.im;
    float pi = in[i].im * zp.re - in[i].re * zp.im;
    float f = atan2f(pi, pr) * k, x = f * sc, y;
    zp = in[i];
    fAvg += (f - fAvg) * (1.0f / 1200.0f);
    y = x - x1 + DC_R * y1; x1 = x; y1 = y;
    de += (y - de) * beta;
    audio[i].re = de; audio[i].im = 0.0f;
  }
  d->x1 = x1; d->y1 = y1; d->fAvg = fAvg; d->de = de; d->zp = zp;
}
static void demod_am(Demod *d, const cfloat *in, cfloat *audio) {
  float x1 = d->x1, y1 = d->y1;
  for (int i = 0; i < RXB; i++) {
    float e = sqrtf(in[i].re * in[i].re + in[i].im * in[i].im);
    float y = e - x1 + DC_R * y1; x1 = e; y1 = y;
    audio[i].re = y; audio[i].im = 0.0f;
  }
  d->x1 = x1; d->y1 = y1;
}

// ------------------------------------------------------- noise reduction ---
#define NR_N 256
#define NR_BINS (NR_N / 2 + 1)
#define NR_SUBS 8
#define NR_SUBL 32
#define LMS_L 64
#define LMS_D 16
struct Nr {
  float level;
  float in[NR_N], acc[NR_N];
  float smooth[NR_BINS], cur[NR_BINS], sub[NR_SUBS][NR_BINS], noise[NR_BINS];
  float gPrev[NR_BINS], gamPrev[NR_BINS];
  int frame, subIdx, filled;
  float x[LMS_L + LMS_D + RXB], w[LMS_L], pow;
};
static Nr nr;
static float nrWin[NR_N], twRe[NR_N / 2], twIm[NR_N / 2];
static short rev[NR_N];
static float fre[NR_N], fim[NR_N];
static float nrGainAvg;
static void nr_tables() {
  for (int i = 0; i < NR_N; i++) nrWin[i] = (float)sqrt(0.5 - 0.5 * cos(2.0 * PI_D * i / NR_N));
  for (int i = 0; i < NR_N / 2; i++) { twRe[i] = (float)cos(2.0 * PI_D * i / NR_N); twIm[i] = (float)-sin(2.0 * PI_D * i / NR_N); }
  for (int i = 0; i < NR_N; i++) { int j = 0; for (int b = 0; b < 8; b++) if (i & (1 << b)) j |= 1 << (7 - b); rev[i] = (short)j; }
}
static void nr_init(Nr *n) {
  memset(n, 0, sizeof *n);
  for (int i = 0; i < NR_BINS; i++) { n->gPrev[i] = 1.0f; n->gamPrev[i] = 1.0f; }
  n->level = 12.0f;
}
static void fft256() {
  for (int i = 0; i < NR_N; i++) {
    int j = rev[i];
    if (j > i) { float t = fre[i]; fre[i] = fre[j]; fre[j] = t; t = fim[i]; fim[i] = fim[j]; fim[j] = t; }
  }
  for (int len = 2; len <= NR_N; len <<= 1) {
    int half = len >> 1, step = NR_N / len;
    for (int i = 0; i < NR_N; i += len)
      for (int k = 0; k < half; k++) {
        float wr = twRe[k * step], wi = twIm[k * step];
        int a = i + k, c = a + half;
        float xr = fre[c] * wr - fim[c] * wi, xi = fre[c] * wi + fim[c] * wr;
        fre[c] = fre[a] - xr; fim[c] = fim[a] - xi;
        fre[a] += xr; fim[a] += xi;
      }
  }
}
static void nr_spectral(Nr *n, cfloat *audio) {
  float gMin = powf(10.0f, -n->level / 20.0f), gSum = 0.0f;
  memmove(n->in, n->in + RXB, (NR_N - RXB) * sizeof(float));
  for (int i = 0; i < RXB; i++) n->in[NR_N - RXB + i] = audio[i].re;
  for (int i = 0; i < NR_N; i++) { fre[i] = n->in[i] * nrWin[i]; fim[i] = 0.0f; }
  fft256();
  if (++n->frame >= NR_SUBL) {
    n->frame = 0;
    for (int i = 0; i < NR_BINS; i++) n->sub[n->subIdx][i] = n->cur[i];
    n->subIdx = (n->subIdx + 1) % NR_SUBS;
    if (n->filled < NR_SUBS) n->filled++;
    for (int i = 0; i < NR_BINS; i++) n->cur[i] = n->smooth[i];
  }
  for (int i = 0; i < NR_BINS; i++) {
    float p = fre[i] * fre[i] + fim[i] * fim[i], mn, gam, xi, g;
    n->smooth[i] = (n->filled == 0 && n->frame == 1) ? p : 0.85f * n->smooth[i] + 0.15f * p;
    if (n->smooth[i] < n->cur[i] || n->cur[i] == 0.0f) n->cur[i] = n->smooth[i];
    mn = n->cur[i];
    for (int s = 0; s < n->filled; s++) if (n->sub[s][i] < mn) mn = n->sub[s][i];
    n->noise[i] = 2.0f * mn + 1e-6f;
    gam = p / n->noise[i];
    xi = 0.98f * n->gPrev[i] * n->gPrev[i] * n->gamPrev[i] + 0.02f * (gam > 1.0f ? gam - 1.0f : 0.0f);
    if (xi < 0.003f) xi = 0.003f;
    g = xi / (1.0f + xi);
    if (g < gMin) g = gMin;
    n->gPrev[i] = g; n->gamPrev[i] = gam; gSum += g;
    fre[i] *= g; fim[i] *= g;
    if (i > 0 && i < NR_N / 2) { fre[NR_N - i] = fre[i]; fim[NR_N - i] = -fim[i]; }
  }
  nrGainAvg = 20.0f * log10f(gSum / NR_BINS + 1e-9f);
  for (int i = 0; i < NR_N; i++) fim[i] = -fim[i];
  fft256();
  for (int i = 0; i < NR_N; i++) n->acc[i] += fre[i] * (1.0f / NR_N) * nrWin[i];
  for (int i = 0; i < RXB; i++) { audio[i].re = 0.5f * n->acc[i]; audio[i].im = 0.0f; }
  memmove(n->acc, n->acc + RXB, (NR_N - RXB) * sizeof(float));
  for (int i = NR_N - RXB; i < NR_N; i++) n->acc[i] = 0.0f;
}
static void nr_lms(Nr *n, cfloat *audio) {
  const int H = LMS_L + LMS_D;
  float mu = 0.25f * powf(10.0f, -n->level / 20.0f);
  float *x = n->x, *w = n->w, p;
  for (int i = 0; i < RXB; i++) x[H + i] = audio[i].re;
  { const float *r = x + H - LMS_D - 1; p = 0.0f; for (int k = 0; k < LMS_L; k++) p += r[-k] * r[-k]; }
  for (int i = 0; i < RXB; i++) {
    const float *r = x + H + i - LMS_D;
    float y = 0.0f, e, g, newest = r[0], oldest = r[-LMS_L];
    p += newest * newest - oldest * oldest;
    if (p < 0.0f) p = 0.0f;
    for (int k = 0; k < LMS_L; k++) y += w[k] * r[-k];
    e = x[H + i] - y;
    g = mu * e / (p + 1.0f);
    for (int k = 0; k < LMS_L; k++) w[k] = 0.99995f * w[k] + g * r[-k];
    audio[i].re = y; audio[i].im = 0.0f;
  }
  if (!(audio[RXB - 1].re == audio[RXB - 1].re) || fabsf(audio[RXB - 1].re) > 1e9f) {
    memset(w, 0, LMS_L * sizeof(float));
    for (int i = 0; i < RXB; i++) audio[i].re = 0.0f;
  }
  memmove(x, x + RXB, H * sizeof(float));
  n->pow = p;
}

// ------------------------------------------------------------ test data ---
#define NBLK 8                                  // distinct input blocks
static int32_t (*rawI)[BLOCK], (*rawQ)[BLOCK];  // heap: 24-bit left-justified, as I2S delivers
static float inI[BLOCK], inQ[BLOCK];
static cfloat bb[RXB], bbf[RXB], bbf2[RXB], bba[RXB], aud[RXB];
static float out48[BLOCK];
static int32_t outRaw[2 * BLOCK];
static uint32_t rng = 12345;
static float frand() { rng = rng * 1664525u + 1013904223u; return (float)(int32_t)rng * (1.0f / 2147483648.0f); }

static void makeInput() {
  // I/Q: tone 1.5 kHz above the tuning (fIF 6 kHz -> 7.5 kHz), -20 dBFS, plus noise, FM 2.5 kHz dev at 400 Hz
  double ph = 0.0;
  for (int b = 0; b < NBLK; b++)
    for (int i = 0; i < BLOCK; i++) {
      double t = (double)(b * BLOCK + i) / FS;
      double f = 7500.0 + 2500.0 * sin(2.0 * PI_D * 400.0 * t);
      ph += 2.0 * PI_D * f / FS;
      float a = 0.1f;
      float iv = a * (float)cos(ph) + 0.01f * frand();
      float qv = a * (float)sin(ph) + 0.01f * frand();
      rawI[b][i] = (int32_t)(iv * 8388607.0f) << 8;
      rawQ[b][i] = (int32_t)(qv * 8388607.0f) << 8;
    }
}

// ------------------------------------------------------------ measuring ---
enum { S_CONV_IN, S_DDC, S_DDC_DSPS, S_CH175, S_CH511, S_CH511_DOT, S_CH511_FIR, S_CH511_UP, S_CH175_UP,
       S_AGC, S_FM, S_AM, S_LMS, S_SPEC, S_DUC, S_CONV_OUT, S_CHAIN, S_CHAIN_FFT_LMS, S_CHAIN_FFT_SPEC, S_CHAIN_FFT_AM, S_COUNT };
static const char *sName[S_COUNT] = {
  "I2S int32 -> float (2 x 256)", "DDC I/Q: DC, IQ corr, NCO mix, 43-tap /4 (C)", "DDC I/Q, low-pass by esp-dsp fird ae32",
  "Channel FIR 175 taps, direct (C)", "Channel FIR 511 taps, direct (C)", "Channel FIR 511, 4x dsps_dotprod_f32_ae32",
  "Channel FIR 511, 4x dsps_fir_f32_ae32", "Channel FIR 511, FFT-128 partitioned (8)", "Channel FIR 175, FFT-128 partitioned (3)",
  "AGC (log10f + expf per sample)", "FM demod (atan2f) + de-emphasis", "AM demod (sqrtf) + DC block",
  "NR LMS 64 taps", "NR spectral (FFT 256 x2, min. stats)", "x4 interpolation 43 taps", "float -> I2S int32 (2 x 256)",
  "CHAIN worst case (C 511, FM, LMS)", "CHAIN FFT 511, FM, LMS", "CHAIN FFT 511, FM, spectral NR", "CHAIN FFT 175, AM, spectral NR" };
static uint64_t tSum[S_COUNT]; static uint32_t tMax[S_COUNT], tN[S_COUNT];
static inline void rec(int s, int64_t t0) {
  uint32_t d = (uint32_t)(esp_timer_get_time() - t0);
  tSum[s] += d; tN[s]++; if (d > tMax[s]) tMax[s] = d;
}

static float maxErr(const cfloat *a, const cfloat *b, float *ref) {
  float e = 0.0f, r = 0.0f;
  for (int i = 0; i < RXB; i++) {
    float dr = a[i].re - b[i].re, di = a[i].im - b[i].im;
    e = fmaxf(e, sqrtf(dr * dr + di * di));
    r = fmaxf(r, sqrtf(a[i].re * a[i].re + a[i].im * a[i].im));
  }
  *ref = r; return e;
}

static void convIn(int b) {
  for (int i = 0; i < BLOCK; i++) {
    inI[i] = (float)(rawI[b][i] >> 8) * (1.0f / 256.0f);   // counts re 32768
    inQ[i] = (float)(rawQ[b][i] >> 8) * (1.0f / 256.0f);
  }
}
static void convOut() {
  for (int i = 0; i < BLOCK; i++) {
    float v = out48[i] * 256.0f;
    if (v > 8388607.0f) v = 8388607.0f; else if (v < -8388608.0f) v = -8388608.0f;
    int32_t s = (int32_t)v << 8;
    outRaw[2 * i] = s; outRaw[2 * i + 1] = s;
  }
}

static void verify() {
  // equal inputs -> compare the variants of the channel filter at 511 and 175 taps
  static cfloat ref[RXB], alt[RXB];
  for (int pass = 0; pass < 2; pass++) {
    int n = pass ? 175 : 511;
    for (int rev2 = 0; rev2 < 2; rev2++) {
      chan_design(n, -250.0f, 250.0f, 80.0f);       // CW-like, centre offset
      if (n == 175) chan_design(n, 300.0f, 2700.0f, 80.0f);
      chan_split(); fCoefReversed = rev2; chan_fir_init(); upols_init();
      float eDot = 0, eFir = 0, eUp = 0, r = 0, rr;
      for (int b = 0; b < 40; b++) {
        cfloat x[RXB];
        for (int i = 0; i < RXB; i++) { x[i].re = 3000.0f * frand(); x[i].im = 3000.0f * frand(); }
        chan_process(x, ref);
        chan_process_dot(x, alt); eDot = fmaxf(eDot, maxErr(ref, alt, &rr)); r = fmaxf(r, rr);
        chan_process_fir(x, alt); eFir = fmaxf(eFir, maxErr(ref, alt, &rr));
        upols_process(x, alt); eUp = fmaxf(eUp, maxErr(ref, alt, &rr));
      }
      Serial.printf("verify %3d taps (fir coef %s): max|y| %.1f, err dot %.3g, fir %.3g, fft-part %.3g\n",
                    n, rev2 ? "reversed" : "natural", r, eDot, eFir, eUp);
    }
  }
}

static void benchTask(void *) {
  vTaskDelay(pdMS_TO_TICKS(500));
  for (;;) {
    memset(tSum, 0, sizeof tSum); memset(tMax, 0, sizeof tMax); memset(tN, 0, sizeof tN);
    verify();
    const int RUNS = 400;
    // stage timings, each on real data flowing through the chain
    ddc_init(&ddc, 6000.0f); agc_init(&agc); demod_init(&dem); nr_init(&nr); duc_init(&duc);
    dsps_fird_init_f32(&firdRe, firdCoef, firdDelRe, DDC_TAPS, DEC);
    dsps_fird_init_f32(&firdIm, firdCoef, firdDelIm, DDC_TAPS, DEC);
    for (int ch = 0; ch < 2; ch++) {
      int n = ch ? 175 : 511;
      if (n == 511) chan_design(511, -250.0f, 250.0f, 80.0f); else chan_design(175, 300.0f, 2700.0f, 80.0f);
      chan_split(); fCoefReversed = 0; chan_fir_init(); upols_init();
      for (int r = 0; r < RUNS; r++) {
        int b = r % NBLK; int64_t t;
        t = esp_timer_get_time(); convIn(b); rec(S_CONV_IN, t);
        t = esp_timer_get_time(); ddc_process_iq(&ddc, inI, inQ, bb); rec(S_DDC, t);
        if (ch == 0) {
          Ddc save = ddc;
          t = esp_timer_get_time(); ddc_process_iq_dsps(&ddc, inI, inQ, bbf2); rec(S_DDC_DSPS, t);
          ddc = save;
          t = esp_timer_get_time(); chan_process(bb, bbf); rec(S_CH511, t);
          t = esp_timer_get_time(); chan_process_dot(bb, bbf2); rec(S_CH511_DOT, t);
          t = esp_timer_get_time(); chan_process_fir(bb, bbf2); rec(S_CH511_FIR, t);
          t = esp_timer_get_time(); upols_process(bb, bbf2); rec(S_CH511_UP, t);
        } else {
          t = esp_timer_get_time(); chan_process(bb, bbf); rec(S_CH175, t);
          t = esp_timer_get_time(); upols_process(bb, bbf2); rec(S_CH175_UP, t);
        }
        t = esp_timer_get_time(); agc_process(&agc, bbf, bba); rec(S_AGC, t);
        { Demod save = dem; t = esp_timer_get_time(); demod_am(&dem, bba, aud); rec(S_AM, t); dem = save; }
        t = esp_timer_get_time(); demod_fm(&dem, bba, aud); rec(S_FM, t);
        { static cfloat a2[RXB]; memcpy(a2, aud, sizeof a2);
          t = esp_timer_get_time(); nr_spectral(&nr, a2); rec(S_SPEC, t); }
        t = esp_timer_get_time(); nr_lms(&nr, aud); rec(S_LMS, t);
        t = esp_timer_get_time(); duc_process(&duc, aud, out48); rec(S_DUC, t);
        t = esp_timer_get_time(); convOut(); rec(S_CONV_OUT, t);
      }
    }
    // the worst-case chain in one piece
    ddc_init(&ddc, 6000.0f); agc_init(&agc); demod_init(&dem); nr_init(&nr); duc_init(&duc);
    chan_design(511, -250.0f, 250.0f, 80.0f);
    for (int r = 0; r < RUNS; r++) {
      int64_t t = esp_timer_get_time();
      convIn(r % NBLK);
      ddc_process_iq(&ddc, inI, inQ, bb);
      chan_process(bb, bbf);
      agc_process(&agc, bbf, bba);
      demod_fm(&dem, bba, aud);
      nr_lms(&nr, aud);
      duc_process(&duc, aud, out48);
      convOut();
      rec(S_CHAIN, t);
    }
    // the recommended chain: partitioned FFT channel filter
    for (int v = 0; v < 3; v++) {
      ddc_init(&ddc, 6000.0f); agc_init(&agc); demod_init(&dem); nr_init(&nr); duc_init(&duc);
      if (v < 2) chan_design(511, -250.0f, 250.0f, 80.0f); else chan_design(175, -3000.0f, 3000.0f, 80.0f);
      upols_init();
      for (int r = 0; r < RUNS; r++) {
        int64_t t = esp_timer_get_time();
        convIn(r % NBLK);
        ddc_process_iq(&ddc, inI, inQ, bb);
        upols_process(bb, bbf);
        agc_process(&agc, bbf, bba);
        if (v < 2) demod_fm(&dem, bba, aud); else demod_am(&dem, bba, aud);
        if (v == 0) nr_lms(&nr, aud); else nr_spectral(&nr, aud);
        duc_process(&duc, aud, out48);
        convOut();
        rec(S_CHAIN_FFT_LMS + v, t);
      }
    }
    // the channel filter's design: microseconds per tap (incremental budget)
    float wc = 2.0f * PI_F * 250.0f / FSB, beta = 0.1102f * (80.0f - 8.7f), i0b = i0f(beta), acc = 0.0f;
    int64_t t = esp_timer_get_time();
    for (int k = 0; k < 511; k++) acc += protoTap(k, 511, wc, beta, i0b);
    uint32_t designUs = (uint32_t)(esp_timer_get_time() - t);

    Serial.printf("\n=== RxBench  %s, CPU %u MHz, core %d, block 256 @ 48 kHz = %.0f us ===\n",
#ifdef __OPTIMIZE_SIZE__
                  "-Os",
#else
                  "-O2",
#endif
                  (unsigned)getCpuFrequencyMhz(), xPortGetCoreID(), BLOCK_US);
    Serial.printf("%-46s %8s %8s %7s\n", "stage", "mean us", "max us", "% blk");
    for (int s = 0; s < S_COUNT; s++) {
      if (!tN[s]) continue;
      float mean = (float)tSum[s] / tN[s];
      Serial.printf("%-46s %8.1f %8u %6.1f%%\n", sName[s], mean, (unsigned)tMax[s], 100.0f * mean / BLOCK_US);
    }
    Serial.printf("Channel filter design: 511 prototype taps in %u us (%.1f us/tap)  [%g]\n", (unsigned)designUs, designUs / 511.0f, acc);
    Serial.printf("heap free %u, largest %u, internal free %u; out sample %ld, nrGain %.1f dB\n",
                  (unsigned)ESP.getFreeHeap(), (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT),
                  (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL), (long)outRaw[100], nrGainAvg);
    Serial.println("=== end ===");
    vTaskDelay(pdMS_TO_TICKS(4000));
  }
}

void setup() {
  Serial.begin(115200);
  delay(300);
  rawI = (int32_t (*)[BLOCK])malloc(sizeof(int32_t) * NBLK * BLOCK);
  rawQ = (int32_t (*)[BLOCK])malloc(sizeof(int32_t) * NBLK * BLOCK);
  nco_initTable(); nr_tables(); makeInput();
  kaiser_lowpass(firdCoef, DDC_TAPS, 6000.0f, FS, 80.0f, 1.0f);
  dsps_fft2r_init_fc32(NULL, 256);
  xTaskCreatePinnedToCore(benchTask, "bench", 16384, nullptr, 5, nullptr, 1);
}

void loop() { vTaskDelay(pdMS_TO_TICKS(1000)); }
