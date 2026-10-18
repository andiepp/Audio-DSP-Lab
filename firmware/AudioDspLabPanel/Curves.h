// ****************************************************************************
// ***                              C U R V E S                             ***
// ****************************************************************************
//  What the panel draws, computed as the board computes it: the
//  equalizer's bands (RBJ cookbook, shelves with slope 1, a peak with
//  Q 0.7, at the DSP's 192 kHz; firmware Applications.cpp), the
//  synthesizer's ladder filter (four bilinear stages, feedback k, make-up
//  1 + k/4, at 48 kHz) and its envelopes (firmware Synth.cpp).
// ----------------------------------------------------------------------------

#ifndef PANEL_CURVES_H
#define PANEL_CURVES_H

enum EqBand { EQ_BASS, EQ_MIDDLE, EQ_TREBLE };

// the three bands together at a frequency, in dB
float equalizerGainDb(float hertz, const float gainsDb[3], const float centresHz[3]);

// the ladder at a frequency, in dB; emphasis 0..100 %
float ladderGainDb(float hertz, float cutoffHz, float emphasisPercent);
// where the contour takes the cutoff at its peak; amount 0..100 %
float contourPeakHz(float cutoffHz, float amountPercent);

// an envelope's level 0..1, ms after the key went down, the key held
float heldLevel(float ms, float attackMs, float decayMs, float sustain);
// the level a release reached ms after the key went up
float releasedLevel(float ms, float fromLevel, float releaseMs);

#endif
