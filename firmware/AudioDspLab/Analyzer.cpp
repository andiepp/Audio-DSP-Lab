#include "Analyzer.h"
#include "Config.h"
#include "Errors.h"
#include "Dsp.h"
#include <Arduino.h>
#include <Preferences.h>
#include <math.h>

// ----------------------------------------------------------------------------
//  SigmaStudio project sigmastudio/Response, export of 2026-10-12 09:26.
//  Parameter RAM (Export/ADAU1701_Response_IC_1_PARAM.h):
//    Sine increment 1 (5.23, f / (DSP_CORE_RATE / 2), as the generator's)
//    'General (2nd order)' double precision cells, B0 B1 B2 A1 A2, the A
//    coefficients stored negated: BandL 5, BandR 10, SmoothL 15, SmoothR 20
//  Readback (data capture) settings, program count and register select:
//    LevelL 0x017A, LevelR 0x0186; captured in 0x081A and 0x081B
// ----------------------------------------------------------------------------

#define ADDR_SINE_INCREMENT           1
#define ADDR_BAND_L                   5
#define ADDR_BAND_R                   10
#define ADDR_SMOOTH_L                 15
#define ADDR_SMOOTH_R                 20
#define CAPTURE_REGISTER_L            0x081A
#define CAPTURE_REGISTER_R            0x081B
#define CAPTURE_SETTING_L             0x017A
#define CAPTURE_SETTING_R             0x0186

// the band-pass: RBJ, 0 dB at its centre, Q 5 (a fifth of an octave either
// side is -3 dB down), so noise and harmonics stay out of the reading
#define BAND_Q                        5.0
// the smoothing: one pole at f / SMOOTH_DIVISOR. The rectified sine
// ripples at 2f with two thirds of its mean; one pole that far below
// takes it to about 1 % (0.1 dB) before the readings are averaged
#define SMOOTH_DIVISOR                32.0
// a point waits this many periods of its frequency for band-pass and
// smoothing to settle (the smoothing's time constant is 5.1 periods)
#define SETTLE_PERIODS                40.0f
#define SETTLE_MIN_MS                 20
#define SWEEP_FIRST_SETTLE_MS         150     // the output's gain slews in
// readings per point, spread over one ripple period (half a period of f)
#define READINGS                      8
#define READING_MIN_SPACING_US        200

#define LEVEL_FLOOR_CENTI_DB          -14000  // -140 dBFS: nothing measurable
#define REFERENCE_MATCH               0.001   // a point within 0.1 % of a reference frequency takes it

#define NVS_NAMESPACE_REFERENCE       "aml-ref"
#define NVS_KEY_COUNT                 "n"
#define NVS_KEY_POINTS                "pts"

static AnalyzerPoint points[ANALYZER_MAX_POINTS];
static AnalyzerPoint reference[ANALYZER_MAX_POINTS];
static uint16_t pointCount = 0;
static uint16_t pointTotal = 0;
static uint16_t referenceCount = 0;
static bool referenceLoaded = false;
static SweepState sweepState = SWEEP_IDLE;

static int32_t toneCentiHz = 100000;
static int32_t sweepStart = 0;
static int32_t sweepStop = 0;

// the point being measured
enum Phase : uint8_t { PHASE_NONE, PHASE_SETTLING, PHASE_READING };
static Phase phase = PHASE_NONE;
static uint32_t settledAt = 0;          // millis
static uint32_t nextReadingAt = 0;      // micros
static uint32_t readingSpacingUs = 0;
static int64_t readingSum[2];
static uint8_t readingCount = 0;

// ----------------------------------------------------------------------------
//  DSP
// ----------------------------------------------------------------------------

static int32_t pointFrequency(uint16_t index) {
  if (pointTotal <= 1) return sweepStart;
  double ratio = (double)sweepStop / sweepStart;
  return (int32_t)llround(sweepStart * pow(ratio, (double)index / (pointTotal - 1)));
}

// band-pass B0 B1 B2 A1 A2 as the DSP stores them (A negated); computed in
// double, since at 20 Hz the poles lie within 0.0007 of one
static void bandCoefficients(double hertz, float out[5]) {
  const double w0 = 2.0 * M_PI * hertz / DSP_CORE_RATE;
  const double alpha = sin(w0) / (2.0 * BAND_Q);
  const double a0 = 1.0 + alpha;
  out[0] = (float)(alpha / a0);
  out[1] = 0.0f;
  out[2] = (float)(-alpha / a0);
  out[3] = (float)(2.0 * cos(w0) / a0);
  out[4] = (float)(-(1.0 - alpha) / a0);
}

// one pole in the second-order cell: y = k.x + (1 - k).y[n-1]. Both words
// are whole counts of 2^-23 that add up to exactly one, so its gain at DC
// is exactly one, however small k gets
static void smoothWords(double hertz, int32_t out[5]) {
  const double corner = hertz / SMOOTH_DIVISOR;
  const double k = 1.0 - exp(-2.0 * M_PI * corner / DSP_CORE_RATE);
  int32_t kWord = (int32_t)llround(k * 8388608.0);
  if (kWord < 1) kWord = 1;
  out[0] = kWord;
  out[1] = 0;
  out[2] = 0;
  out[3] = 8388608 - kWord;
  out[4] = 0;
}

static bool setFrequency(int32_t centiHz) {
  const double hertz = centiHz / 100.0;
  float band[5];
  int32_t smooth[5];
  bandCoefficients(hertz, band);
  smoothWords(hertz, smooth);
  const uint16_t bandL[] = { ADDR_BAND_L, ADDR_BAND_L + 1, ADDR_BAND_L + 2, ADDR_BAND_L + 3, ADDR_BAND_L + 4 };
  const uint16_t bandR[] = { ADDR_BAND_R, ADDR_BAND_R + 1, ADDR_BAND_R + 2, ADDR_BAND_R + 3, ADDR_BAND_R + 4 };
  const uint16_t smoothL[] = { ADDR_SMOOTH_L, ADDR_SMOOTH_L + 1, ADDR_SMOOTH_L + 2, ADDR_SMOOTH_L + 3, ADDR_SMOOTH_L + 4 };
  const uint16_t smoothR[] = { ADDR_SMOOTH_R, ADDR_SMOOTH_R + 1, ADDR_SMOOTH_R + 2, ADDR_SMOOTH_R + 3, ADDR_SMOOTH_R + 4 };
  return dspSafeloadOne(ADDR_SINE_INCREMENT, (float)(hertz / (DSP_CORE_RATE / 2.0)))
      && dspSafeload(bandL, band, 5) && dspSafeload(bandR, band, 5)
      && dspSafeloadRaw(smoothL, smooth, 5) && dspSafeloadRaw(smoothR, smooth, 5);
}

// the mean of the rectified sine -> its peak in 0.01 dBFS
static int32_t toLevel(double meanWord) {
  const double mean = meanWord / 524288.0;            // 5.19
  const double peak = mean * M_PI / 2.0;
  if (peak <= 1e-7) return LEVEL_FLOOR_CENTI_DB;
  return max((int32_t)LEVEL_FLOOR_CENTI_DB, (int32_t)llround(2000.0 * log10(peak)));
}

static uint32_t settleMs(int32_t centiHz) {
  return max((uint32_t)SETTLE_MIN_MS, (uint32_t)(SETTLE_PERIODS * 1000.0f * 100.0f / centiHz));
}

static uint32_t spacingUs(int32_t centiHz) {
  // READINGS spread over half a period of f: one period of the ripple
  return max((uint32_t)READING_MIN_SPACING_US, (uint32_t)(1e8 / (2.0 * READINGS * centiHz)));
}

// ----------------------------------------------------------------------------
//  Sweep
// ----------------------------------------------------------------------------

static void beginPoint(uint32_t extraSettleMs) {
  int32_t centiHz = pointFrequency(pointCount);
  if (!setFrequency(centiHz)) {
    sweepState = SWEEP_FAILED;
    phase = PHASE_NONE;
    return;
  }
  settledAt = millis() + settleMs(centiHz) + extraSettleMs;
  readingSpacingUs = spacingUs(centiHz);
  readingSum[0] = readingSum[1] = 0;
  readingCount = 0;
  phase = PHASE_SETTLING;
}

static void endSweep(SweepState result) {
  sweepState = result;
  phase = PHASE_NONE;
  setFrequency(toneCentiHz);                           // the tone again
}

bool analyzerBegin(int32_t centiHz) {
  pointCount = 0;
  pointTotal = 0;
  sweepState = SWEEP_IDLE;
  phase = PHASE_NONE;
  toneCentiHz = centiHz;
  return dspCaptureSelect(CAPTURE_REGISTER_L, CAPTURE_SETTING_L)
      && dspCaptureSelect(CAPTURE_REGISTER_R, CAPTURE_SETTING_R)
      && setFrequency(centiHz);
}

void analyzerEnd() {
  if (sweepState == SWEEP_RUNNING) sweepState = SWEEP_STOPPED;
  phase = PHASE_NONE;
}

bool analyzerSetTone(int32_t centiHz) {
  toneCentiHz = centiHz;
  if (sweepState == SWEEP_RUNNING) return true;        // taken back when the sweep ends
  return setFrequency(centiHz);
}

uint8_t analyzerStartSweep(int32_t startCentiHz, int32_t stopCentiHz, int32_t pointsPerDecade) {
  if (startCentiHz <= 0 || stopCentiHz <= startCentiHz || pointsPerDecade < 1) return ERR_OUT_OF_RANGE;
  double decades = log10((double)stopCentiHz / startCentiHz);
  long total = lround(decades * pointsPerDecade) + 1;
  if (total < 2) total = 2;
  if (total > ANALYZER_MAX_POINTS) return ERR_OUT_OF_RANGE;
  sweepStart = startCentiHz;
  sweepStop = stopCentiHz;
  pointTotal = (uint16_t)total;
  pointCount = 0;
  sweepState = SWEEP_RUNNING;
  beginPoint(SWEEP_FIRST_SETTLE_MS);
  return sweepState == SWEEP_RUNNING ? ERR_NONE : ERR_DSP_WRITE_FAILED;
}

void analyzerStopSweep() {
  if (sweepState == SWEEP_RUNNING) endSweep(SWEEP_STOPPED);
}

void analyzerPoll() {
  if (sweepState != SWEEP_RUNNING) return;
  if (phase == PHASE_SETTLING) {
    if ((int32_t)(millis() - settledAt) < 0) return;
    phase = PHASE_READING;
    nextReadingAt = micros();
  }
  if (phase != PHASE_READING || (int32_t)(micros() - nextReadingAt) < 0) return;

  int32_t left, right;
  if (!dspCaptureRead(CAPTURE_REGISTER_L, left) || !dspCaptureRead(CAPTURE_REGISTER_R, right)) {
    endSweep(SWEEP_FAILED);
    return;
  }
  readingSum[0] += left;
  readingSum[1] += right;
  nextReadingAt += readingSpacingUs;
  if (++readingCount < READINGS) return;

  AnalyzerPoint &point = points[pointCount];
  point.centiHz = pointFrequency(pointCount);
  point.levelLeft = toLevel((double)readingSum[0] / READINGS);
  point.levelRight = toLevel((double)readingSum[1] / READINGS);
  pointCount++;
  if (pointCount >= pointTotal) {
    endSweep(SWEEP_DONE);
  } else {
    beginPoint(0);
  }
}

SweepState analyzerState() { return sweepState; }
uint16_t analyzerPointCount() { return pointCount; }
uint16_t analyzerPointTotal() { return pointTotal; }

bool analyzerPoint(uint16_t index, AnalyzerPoint &point) {
  if (index >= pointCount) return false;
  point = points[index];
  return true;
}

// ----------------------------------------------------------------------------
//  Reference
// ----------------------------------------------------------------------------

static void loadReference() {
  if (referenceLoaded) return;
  referenceLoaded = true;
  Preferences store;
  if (!store.begin(NVS_NAMESPACE_REFERENCE, true)) return;
  uint16_t count = store.getUShort(NVS_KEY_COUNT, 0);
  if (count > 0 && count <= ANALYZER_MAX_POINTS &&
      store.getBytes(NVS_KEY_POINTS, reference, count * sizeof(AnalyzerPoint)) == count * sizeof(AnalyzerPoint)) {
    referenceCount = count;
  }
  store.end();
}

uint8_t analyzerKeepReference() {
  if (sweepState != SWEEP_DONE || pointCount < 2) return ERR_NO_SWEEP;
  memcpy(reference, points, pointCount * sizeof(AnalyzerPoint));
  referenceCount = pointCount;
  referenceLoaded = true;
  Preferences store;
  if (!store.begin(NVS_NAMESPACE_REFERENCE, false)) return ERR_EEPROM_FAILED;
  bool stored = store.putBytes(NVS_KEY_POINTS, reference, referenceCount * sizeof(AnalyzerPoint)) ==
                    referenceCount * sizeof(AnalyzerPoint)
                && store.putUShort(NVS_KEY_COUNT, referenceCount) == sizeof(uint16_t);
  store.end();
  return stored ? ERR_NONE : ERR_EEPROM_FAILED;
}

uint16_t analyzerReferenceCount() {
  loadReference();
  return referenceCount;
}

bool analyzerReferencePoint(uint16_t index, AnalyzerPoint &point) {
  loadReference();
  if (index >= referenceCount) return false;
  point = reference[index];
  return true;
}

// the reference's level at a frequency, interpolated linearly on the
// logarithmic axis between its two neighbours
static bool referenceAt(int32_t centiHz, int32_t &left, int32_t &right) {
  loadReference();
  if (referenceCount == 0) return false;
  for (uint16_t i = 0; i < referenceCount; i++) {
    if (fabs((double)centiHz / reference[i].centiHz - 1.0) <= REFERENCE_MATCH) {
      left = reference[i].levelLeft;
      right = reference[i].levelRight;
      return true;
    }
  }
  for (uint16_t i = 0; i + 1 < referenceCount; i++) {
    const AnalyzerPoint &low = reference[i];
    const AnalyzerPoint &high = reference[i + 1];
    if (centiHz < low.centiHz || centiHz > high.centiHz) continue;
    double t = log((double)centiHz / low.centiHz) / log((double)high.centiHz / low.centiHz);
    left = (int32_t)llround(low.levelLeft + t * (high.levelLeft - low.levelLeft));
    right = (int32_t)llround(low.levelRight + t * (high.levelRight - low.levelRight));
    return true;
  }
  return false;
}

void analyzerRelative(const AnalyzerPoint &point, int32_t &left, int32_t &right) {
  int32_t refLeft, refRight;
  if (!referenceAt(point.centiHz, refLeft, refRight)) {
    left = right = ANALYZER_NO_VALUE;
    return;
  }
  left = point.levelLeft - refLeft;
  right = point.levelRight - refRight;
}

// ----------------------------------------------------------------------------
//  Single readings
// ----------------------------------------------------------------------------

bool analyzerReadRaw(int32_t &left, int32_t &right) {
  return dspCaptureRead(CAPTURE_REGISTER_L, left) && dspCaptureRead(CAPTURE_REGISTER_R, right);
}

bool analyzerMeasure(int32_t &left, int32_t &right) {
  const uint32_t spacing = spacingUs(toneCentiHz);
  int64_t sum[2] = { 0, 0 };
  for (int i = 0; i < READINGS; i++) {
    int32_t l, r;
    if (!analyzerReadRaw(l, r)) return false;
    sum[0] += l;
    sum[1] += r;
    delayMicroseconds(spacing);
  }
  left = toLevel((double)sum[0] / READINGS);
  right = toLevel((double)sum[1] / READINGS);
  return true;
}
