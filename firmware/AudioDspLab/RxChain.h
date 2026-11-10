// ****************************************************************************
// ***                      R E C E I V E R   C H A I N                     ***
// ****************************************************************************
//  The TMS320-DSP-Box receiver chain (H:\TMS320-DSP-Box\projects\dspbox_rx,
//  design\rx-chain.md) on the ESP32, design/receiver.md section 5:
//
//    48 kHz I/Q (or a real IF) --DDC--> 12 kHz complex --channel filter-->
//    --AGC / S-meter--> demodulator --> squelch --DUC--> 48 kHz audio
//
//  DDC     DC trackers, I/Q correction, NCO mix, 43-tap low-pass, /4 (rx.c)
//  filter  complex FIR of up to 511 taps, Kaiser-designed from two edges
//          relative to the carrier (chan.c), run as a uniformly partitioned
//          overlap-save convolution (block 64, FFT 128, up to 8
//          partitions): the direct form's output, a tenth of its time.
//          A design task (low priority) computes a new filter into the
//          inactive set; the audio task switches sets at a block
//          boundary. The input history is shared, so a switch is as
//          click-free as the DSP-Box's.
//  AGC     hang AGC with 5.3 ms look-ahead, S-meter = its detector (agc.c)
//  demod   AM, USB, LSB, CW, FM (demod.c); "off" passes the filtered
//          baseband, mixed back up to the offset
//  NR      noise reduction on the audio (nr.c): an LMS line enhancer (64
//          taps, 16 samples back) or a spectral gain (FFT 256, minimum
//          statistics, decision-directed Wiener gain); level 0 passes
//  squelch mutes below an S-meter level, a one-block ramp (main.c)
//  DUC     x4 interpolation, 43 taps (rx.c)
//
//  Levels are counts re 32768 as on the DSP-Box's AIC23, so every dB value
//  (S-meter, AGC target -12 dBFS, test level) means what it meant there.
//  The internal test signal (selftest.c, tone with noise and keying)
//  replaces the input when on.
//
//  The loop configures (rxChainConfigure), the audio task processes
//  (rxChainProcess); changes take effect at the next block.
// ----------------------------------------------------------------------------

#ifndef RX_CHAIN_H
#define RX_CHAIN_H

#include <stdint.h>

#define RX_CHAIN_BLOCK                256     // frames at 48 kHz per call
#define RX_EDGE_MAX                   5500    // |filter edge|, Hz: the DDC rolls off above
#define RX_TAPS_MAX                   511

enum RxMode : uint8_t { RX_OFF = 0, RX_AM = 1, RX_USB = 2, RX_LSB = 3, RX_CW = 4, RX_FM = 5 };
enum RxInput : uint8_t { RX_INPUT_IQ = 0, RX_INPUT_QI = 1, RX_INPUT_IF = 2 };
enum RxNr : uint8_t { RX_NR_OFF = 0, RX_NR_LMS = 1, RX_NR_SPECTRAL = 2 };

struct RxConfig {
  uint8_t mode;             // RxMode
  float offset;             // Hz: the DSP's tuning, -21000 .. 21000 (I/Q) or the IF
  float low, high;          // Hz: filter edges rel. carrier (-6 dB points)
  float stopband;           // dB
  float pitch;              // Hz: CW
  uint8_t agc;              // 0 off (manual gain), 1 fast, 2 medium, 3 slow
  float agcGain;            // dB: maximum gain, the manual gain when off
  float hang;               // s
  float decay;              // dB/s
  float fmDeviation;        // Hz: the nominal deviation gives -12 dBFS audio
  float deemphasis;         // us, 0 off
  float squelch;            // dBFS: mute below this S-meter level, -150 open
  uint8_t input;            // RxInput
  float iqGain;             // dB: Q's gain error of the front end
  float iqPhase;            // degrees: Q's phase error
  uint8_t nr;               // RxNr
  float nrLevel;            // dB: the spectral gain floor; LMS step 0.25 x 10^(-level/20); 0 passes
  bool test;                // the internal test signal replaces the input
  float testOffset;         // Hz from the offset
  float testLevel;          // dBFS
  float testNoise;          // dBFS RMS, -150 none
  float testKey;            // ms: keying period (half on), 0 steady
  float testIqGain;         // dB: the test tone's Q gain error (a front end's, simulated)
  float testIqPhase;        // degrees: its Q phase error
};

// what changed (rxChainConfigure)
#define RX_DIRTY_TUNE                 0x01
#define RX_DIRTY_FILTER               0x02
#define RX_DIRTY_MODE                 0x04
#define RX_DIRTY_AGC                  0x08
#define RX_DIRTY_FM                   0x10
#define RX_DIRTY_TEST                 0x20
#define RX_DIRTY_INPUT                0x40
#define RX_DIRTY_NR                   0x80
#define RX_DIRTY_ALL                  0xFF

struct RxMeter {
  float sMeter;             // dBFS: the AGC detector at the channel filter's output
  float agcGain;            // dB
  float fmOffset;           // Hz: FM tuning indicator
  bool squelchOpen;
  float audioDb;            // dBFS RMS of the demodulated audio (smoothed, ~0.1 s)
  uint16_t taps;            // the active filter
  float transition;         // Hz: its transition width
  uint32_t designs;         // filters switched in since the start
  bool designing;           // a design is in progress
  float nrGainDb;           // spectral NR: its average gain over the bins, dB
  bool downloaded;          // the active filter came from a download, not a design
};

// a filter designed on the PC (design/receiver.md, section 7): taps in
// natural order (tap k multiplies x[n - k]), real or complex, sent as
// float32. The end checks the count and the CRC-32 (IEEE, as zlib's) of the
// values' little-endian bytes in the order sent; the design task then
// transforms them into the inactive set, and the audio task switches at a
// block boundary as after a design. A design request (an edge, the stop
// band, the mode) replaces a downloaded filter.
enum RxLoadResult : uint8_t { RX_LOAD_OK = 0, RX_LOAD_NOT_LOADING, RX_LOAD_COUNT, RX_LOAD_CRC, RX_LOAD_BUSY };
RxLoadResult rxChainLoadBegin(uint16_t taps, bool complex);
// values: re (real) or re, im pairs (complex); received: taps so far
RxLoadResult rxChainLoadData(const float *values, uint8_t count, uint16_t &received);
RxLoadResult rxChainLoadEnd(uint32_t crc, uint16_t &taps);

// allocates (about 53 KB of heap, no piece over 13 KB), starts the design
// task and designs the first filter (a few ms); false without memory
bool rxChainBegin(const RxConfig &config);
void rxChainEnd();
// the loop: the whole configuration, and which parts of it changed
void rxChainConfigure(const RxConfig &config, uint32_t dirty);
// the audio task: one block, interleaved left/right 32-bit slots in (24
// bits at the top), interleaved 16-bit stereo out
void rxChainProcess(const int32_t *in, int16_t *out);
void rxChainGetMeter(RxMeter &meter);

#endif

// ============================================================================
// ==================================  E O F  =================================
// ============================================================================
