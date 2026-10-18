#include "Curves.h"
#include <complex>
#include <math.h>

#define EQ_CORE_RATE                  192000.0
#define EQ_MIDDLE_Q                   0.7
#define SYNTH_RATE                    48000.0
#define SYNTH_EMPHASIS_MAXIMUM        4.2
#define SYNTH_CONTOUR_OCTAVES         5.0
#define SYNTH_CUTOFF_LOWEST           10.0
#define SYNTH_CUTOFF_HIGHEST          (0.45 * SYNTH_RATE)
#define ENVELOPE_TIME_CONSTANTS       4.6     // ln(100): within 1 % at the time set

typedef std::complex<double> Complex;

// one band's gain at a frequency, from its biquad (as eqBiquad designs it)
static double bandMagnitude(EqBand band, double gainDb, double centreHz, double hertz) {
  const double a = pow(10.0, gainDb / 40.0);
  const double w0 = 2.0 * M_PI * centreHz / EQ_CORE_RATE;
  const double cw = cos(w0);
  const double sw = sin(w0);
  double b0, b1, b2, a0, a1, a2;
  if (band == EQ_MIDDLE) {
    const double alpha = sw / (2.0 * EQ_MIDDLE_Q);
    b0 = 1.0 + alpha * a;
    b1 = -2.0 * cw;
    b2 = 1.0 - alpha * a;
    a0 = 1.0 + alpha / a;
    a1 = -2.0 * cw;
    a2 = 1.0 - alpha / a;
  } else {
    const double k = 2.0 * sqrt(a) * (sw / 2.0 * sqrt(2.0));     // slope 1
    const double s = band == EQ_BASS ? 1.0 : -1.0;               // the high shelf mirrors cw
    b0 =           a * ((a + 1) - s * (a - 1) * cw + k);
    b1 = 2.0 * s * a * ((a - 1) - s * (a + 1) * cw);
    b2 =           a * ((a + 1) - s * (a - 1) * cw - k);
    a0 =                (a + 1) + s * (a - 1) * cw + k;
    a1 = -2.0 * s *    ((a - 1) + s * (a + 1) * cw);
    a2 =                (a + 1) + s * (a - 1) * cw - k;
  }
  const double w = 2.0 * M_PI * hertz / EQ_CORE_RATE;
  const Complex z1 = std::polar(1.0, -w);
  const Complex z2 = z1 * z1;
  return std::abs((b0 + b1 * z1 + b2 * z2) / (a0 + a1 * z1 + a2 * z2));
}

float equalizerGainDb(float hertz, const float gainsDb[3], const float centresHz[3]) {
  double magnitude = 1.0;
  for (int band = 0; band < 3; band++) magnitude *= bandMagnitude((EqBand)band, gainsDb[band], centresHz[band], hertz);
  return (float)(20.0 * log10(magnitude));
}

float ladderGainDb(float hertz, float cutoffHz, float emphasisPercent) {
  const double k = SYNTH_EMPHASIS_MAXIMUM * fmin(fmax(emphasisPercent, 0.0), 100.0) / 100.0;
  const double cutoff = fmin(fmax(cutoffHz, SYNTH_CUTOFF_LOWEST), SYNTH_CUTOFF_HIGHEST);
  const double g = tan(M_PI * cutoff / SYNTH_RATE);
  const double w = tan(M_PI * fmin(hertz, 0.499 * SYNTH_RATE) / SYNTH_RATE);
  const Complex stage = g / Complex(g, w);
  const Complex four = stage * stage * stage * stage;
  const double magnitude = std::abs(four / (1.0 + k * four)) * (1.0 + 0.25 * k);
  return magnitude > 0 ? (float)(20.0 * log10(magnitude)) : -200.0f;
}

float contourPeakHz(float cutoffHz, float amountPercent) {
  const double peak = cutoffHz * pow(2.0, SYNTH_CONTOUR_OCTAVES * fmin(fmax(amountPercent, 0.0), 100.0) / 100.0);
  return (float)fmin(fmax(peak, SYNTH_CUTOFF_LOWEST), SYNTH_CUTOFF_HIGHEST);
}

float heldLevel(float ms, float attackMs, float decayMs, float sustain) {
  if (ms < attackMs) return ms / attackMs;
  return sustain + (1.0f - sustain) * expf(-ENVELOPE_TIME_CONSTANTS * (ms - attackMs) / decayMs);
}

float releasedLevel(float ms, float fromLevel, float releaseMs) {
  return fromLevel * expf(-ENVELOPE_TIME_CONSTANTS * ms / releaseMs);
}
