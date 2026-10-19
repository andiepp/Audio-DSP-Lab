#include "Distortion.h"
#include "Capture.h"
#include "Config.h"
#include "Errors.h"
#include <Arduino.h>
#include <math.h>
#include <string.h>
#include "dsps_fft2r.h"

#define FRAMES                        DISTORTION_FRAMES
#define HALF                          (DISTORTION_FRAMES / 2)   // complex points of the FFT
#define LOBE                          4       // the window's main lobe: 4 bins either side
#define SEARCH                        2       // a harmonic's peak may lie this many bins off
#define STOP_WAIT_MS                  2000
#define TASK_STACK                    4096
#define FLOOR_DB                      -200.0f

// one channel at a time: after the radio the heap holds two blocks of 32 KB
// at most and a single one of 16 KB, so the measurement asks for one of
// 32 KB (the samples) and three of 16 KB (esp-dsp's table, the two sums)
static float *samples = nullptr;            // FRAMES samples, then the FFT's HALF complex points
static float *powerLeft = nullptr;          // HALF + 1 bins, summed over the averages
static float *powerRight = nullptr;
static DistortionResult *working = nullptr;
static double windowSum = 0;                // the window's sum and the sum of its squares
static double windowSquares = 0;
static bool libraryForward = true;          // esp-dsp's FFT takes e^-j (else e^+j)

static TaskHandle_t task = NULL;
static volatile bool stopRequested = false;
static volatile bool taskDone = true;
static volatile DistortionState state = DISTORTION_IDLE;
static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
static DistortionResult published;
static uint32_t resultCount = 0;

// ----------------------------------------------------------------------------
//  Memory and the FFT
// ----------------------------------------------------------------------------

// the window, Blackman-Harris with 4 terms, periodic (sidelobes below
// -92 dB), sample by sample without a table: cos(2x) and cos(3x) from
// cos(x). Single precision throughout: the ESP32's FPU has no double
template <typename Each>
static void forEachWindowValue(Each each) {
  const float step = 2.0f * (float)M_PI / FRAMES;
  for (int n = 0; n < FRAMES; n++) {
    const float c = cosf(step * n);
    const float c2 = 2 * c * c - 1, c3 = (4 * c * c - 3) * c;
    each(n, 0.35875f - 0.48829f * c + 0.14128f * c2 - 0.01168f * c3);
  }
}

static void release() {
  free(samples);
  free(powerLeft);
  free(powerRight);
  free(working);
  samples = powerLeft = powerRight = nullptr;
  working = nullptr;
  dsps_fft2r_deinit_fc32();
}

static bool allocate() {
  // the largest first, while the heap still has it
  samples = (float *)malloc(FRAMES * sizeof(float));
  if (samples == nullptr || dsps_fft2r_init_fc32(NULL, HALF) != ESP_OK) {
    release();
    return false;
  }
  powerLeft = (float *)malloc((HALF + 1) * sizeof(float));
  powerRight = (float *)malloc((HALF + 1) * sizeof(float));
  working = (DistortionResult *)malloc(sizeof(DistortionResult));
  if (powerLeft == nullptr || powerRight == nullptr || working == nullptr) {
    release();
    return false;
  }

  windowSum = windowSquares = 0;
  forEachWindowValue([](int, float w) {
    windowSum += w;
    windowSquares += (double)w * w;
  });

  // which way the library turns: e^(+j 2 pi 3 m / HALF) lands on bin 3 of
  // a forward (e^-j) transform, on bin HALF - 3 of the other
  for (int m = 0; m < HALF; m++) {
    samples[2 * m] = cosf(2.0f * (float)M_PI * 3 * m / HALF);
    samples[2 * m + 1] = sinf(2.0f * (float)M_PI * 3 * m / HALF);
  }
  dsps_fft2r_fc32(samples, HALF);
  dsps_bit_rev_fc32(samples, HALF);
  libraryForward = hypotf(samples[6], samples[7]) > hypotf(samples[2 * (HALF - 3)], samples[2 * (HALF - 3) + 1]);
  return true;
}

// one channel's samples to its power spectrum, added to `power`
static void addSpectrum(float *x, float *power) {
  double sum = 0;
  for (int n = 0; n < FRAMES; n++) sum += x[n];
  const float mean = (float)(sum / FRAMES);
  forEachWindowValue([x, mean](int n, float w) { x[n] = (x[n] - mean) * w; });

  // the real samples as HALF complex points: z[m] = x[2m] + j x[2m+1]
  dsps_fft2r_fc32(x, HALF);
  dsps_bit_rev_fc32(x, HALF);

  // the split: X[k] = E[k] + W^k O[k], W = e^(-j 2 pi / FRAMES), with
  // E = (Z[k] + Z*[HALF-k]) / 2 and O = (Z[k] - Z*[HALF-k]) / 2j
  const float step = 2.0f * (float)M_PI / FRAMES;
  for (int k = 0; k <= HALF; k++) {
    const int a = libraryForward ? k % HALF : (HALF - k) % HALF;
    const int b = libraryForward ? (HALF - k) % HALF : k % HALF;
    const float wr = cosf(step * k), wi = -sinf(step * k);
    const float zr = x[2 * a], zi = x[2 * a + 1];
    const float yr = x[2 * b], yi = -x[2 * b + 1];            // Z*[HALF-k]
    const float er = 0.5f * (zr + yr), ei = 0.5f * (zi + yi);
    const float dr = 0.5f * (zr - yr), di = 0.5f * (zi - yi);
    const float orr = di, oi = -dr;                           // (Z - Z*) / 2j
    const float xr = er + wr * orr - wi * oi;
    const float xi = ei + wr * oi + wi * orr;
    power[k] += xr * xr + xi * xi;
  }
}

// ----------------------------------------------------------------------------
//  The analysis
// ----------------------------------------------------------------------------

static float decibels(double ratio) {
  return ratio > 0 ? (float)(10.0 * log10(ratio)) : FLOOR_DB;
}

static double lobe(const float *power, int centre) {
  double sum = 0;
  for (int k = max(centre - LOBE, 1); k <= min(centre + LOBE, HALF); k++) sum += power[k];
  return sum;
}

static int peakNear(const float *a, const float *b, int centre) {
  int best = centre;
  for (int k = max(centre - SEARCH, 1); k <= min(centre + SEARCH, HALF); k++) {
    if (a[k] + b[k] > a[best] + b[best]) best = k;
  }
  return best;
}

float distortionBinHertz() {
  return (float)DSP_SERIAL_OUT_RATE / FRAMES;
}

float distortionSpectrumHertz(uint16_t point) {
  return DISTORTION_LOW_HZ * powf(24000.0f / DISTORTION_LOW_HZ, point / (float)(DISTORTION_SPECTRUM_POINTS - 1));
}

// the summed spectra of `averages` captures to a result
static void analyse(int averages, DistortionResult &result) {
  const double bin = distortionBinHertz();
  const int first = max((int)ceil(DISTORTION_LOW_HZ / bin), LOBE + 1);
  const int last = min((int)floor(DISTORTION_HIGH_HZ / bin), HALF - LOBE);
  // a bin's power sum to the mean square of the signal it holds
  const double meanSquare = 2.0 / ((double)FRAMES * windowSquares * averages);
  // a bin's power to a sine's peak in it
  const double peakSquare = 4.0 / (windowSum * windowSum * averages);

  // the fundamental: the strongest bin of both channels in the band
  int f = first;
  for (int k = first; k <= last; k++) {
    if (powerLeft[k] + powerRight[k] > powerLeft[f] + powerRight[f]) f = k;
  }
  double weighted = 0, total = 0;
  for (int k = max(f - LOBE, 1); k <= f + LOBE; k++) {
    weighted += k * (double)(powerLeft[k] + powerRight[k]);
    total += powerLeft[k] + powerRight[k];
  }
  result.hertz = (float)(total > 0 ? weighted / total * bin : f * bin);

  const float *power[2] = { powerLeft, powerRight };
  for (int c = 0; c < 2; c++) {
    DistortionChannel &out = result.channel[c];
    const double fundamental = lobe(power[c], f) * meanSquare;
    out.levelDb = decibels(2.0 * fundamental);

    double harmonics = 0;
    bool any = false;
    for (int h = 0; h <= DISTORTION_HARMONIC_LAST; h++) out.harmonicDb[h] = NAN;
    for (int h = 2; h <= DISTORTION_HARMONIC_LAST; h++) {
      if (h * f * bin > DISTORTION_HIGH_HZ || h * f + LOBE + SEARCH > HALF) break;
      const double p = lobe(power[c], peakNear(powerLeft, powerRight, h * f)) * meanSquare;
      out.harmonicDb[h] = decibels(p / fundamental);
      harmonics += p;
      any = true;
    }
    out.thdDb = any ? decibels(harmonics / fundamental) : NAN;

    double band = 0;
    for (int k = first; k <= last; k++) band += power[c][k];
    const double rest = (band - lobe(power[c], f)) * meanSquare;
    out.thdnDb = decibels(rest / fundamental);

    for (uint16_t i = 0; i < DISTORTION_SPECTRUM_POINTS; i++) {
      const float hz = distortionSpectrumHertz(i);
      const float edge = powf(24000.0f / DISTORTION_LOW_HZ, 0.5f / (DISTORTION_SPECTRUM_POINTS - 1));
      int lo = (int)lroundf(hz / edge / bin), hi = (int)lroundf(hz * edge / bin);
      lo = constrain(lo, 1, HALF);
      hi = constrain(hi, lo, HALF);
      float strongest = 0;
      for (int k = lo; k <= hi; k++) strongest = max(strongest, power[c][k]);
      result.spectrumDb[c][i] = decibels(strongest * peakSquare);
    }
  }
}

// ----------------------------------------------------------------------------
//  The task
// ----------------------------------------------------------------------------

static void run(void *) {
  while (!stopRequested) {
    memset(powerLeft, 0, (HALF + 1) * sizeof(float));
    memset(powerRight, 0, (HALF + 1) * sizeof(float));
    bool clocked = true;
    for (int a = 0; a < 2 * DISTORTION_AVERAGES && clocked && !stopRequested; a++) {
      const bool isLeft = a % 2 == 0;
      clocked = captureSamples(FRAMES, isLeft ? samples : nullptr, isLeft ? nullptr : samples);
      if (clocked) addSpectrum(samples, isLeft ? powerLeft : powerRight);
    }
    if (stopRequested) break;
    if (!clocked) {
      state = DISTORTION_FAILED;
      break;
    }
    analyse(DISTORTION_AVERAGES, *working);
    working->count = resultCount + 1;
    portENTER_CRITICAL(&lock);
    memcpy(&published, working, sizeof(published));
    resultCount = working->count;
    portEXIT_CRITICAL(&lock);
  }
  taskDone = true;
  vTaskDelete(NULL);
}

// ----------------------------------------------------------------------------
//  Public
// ----------------------------------------------------------------------------

uint8_t distortionStart() {
  distortionStop();
  if (!allocate()) return ERR_NO_MEMORY;
  resultCount = 0;
  published.count = 0;
  stopRequested = false;
  taskDone = false;
  state = DISTORTION_RUNNING;
  // the WiFi core: the radio is not running while the analyzer does
  if (xTaskCreatePinnedToCore(run, "distortion", TASK_STACK, NULL, 1, &task, 0) != pdPASS) {
    task = NULL;
    taskDone = true;
    state = DISTORTION_IDLE;
    release();
    return ERR_NO_MEMORY;
  }
  return ERR_NONE;
}

void distortionStop() {
  if (task != NULL) {
    stopRequested = true;
    const uint32_t startedAt = millis();
    while (!taskDone && millis() - startedAt < STOP_WAIT_MS) delay(5);
    task = NULL;
  }
  if (samples != nullptr) release();
  state = DISTORTION_IDLE;
}

DistortionState distortionState() {
  return state;
}

bool distortionBusy() {
  return !taskDone;
}

uint32_t distortionCount() {
  return resultCount;
}

bool distortionResult(DistortionResult &result) {
  if (resultCount == 0) return false;
  portENTER_CRITICAL(&lock);
  memcpy(&result, &published, sizeof(result));
  portEXIT_CRITICAL(&lock);
  return true;
}

int32_t distortionSnap(int32_t centiHz) {
  const double bin = distortionBinHertz();
  const long k = lround(centiHz / 100.0 / bin);
  return (int32_t)lround(max(k, 1L) * bin * 100.0);
}

uint8_t distortionSelfTest(DistortionResult &result) {
  if (distortionBusy()) return ERR_BUSY;
  if (!allocate()) return ERR_NO_MEMORY;
  memset(powerLeft, 0, (HALF + 1) * sizeof(float));
  memset(powerRight, 0, (HALF + 1) * sizeof(float));
  const int k = 171;                        // 1001.95 Hz
  for (int n = 0; n < FRAMES; n++) {
    const double phase = 2.0 * M_PI * k * n / FRAMES;
    samples[n] = (float)(0.1 * sin(phase) + 0.0001 * sin(3 * phase + 0.3) - 0.016);
  }
  addSpectrum(samples, powerLeft);
  for (int n = 0; n < FRAMES; n++) {
    const double phase = 2.0 * M_PI * k * n / FRAMES;
    samples[n] = (float)(0.1 * sin(phase) + 0.00001 * sin(2 * phase + 0.7) - 0.015);
  }
  addSpectrum(samples, powerRight);
  analyse(1, result);
  result.count = 0;
  release();
  return ERR_NONE;
}
