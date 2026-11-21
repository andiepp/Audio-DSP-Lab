// ****************************************************************************
// ***                 R E C E I V E R   D E C O D E R S                    ***
// ****************************************************************************
//  RTTY and HF weather fax for the receiver (design/rtty-fax.md): the
//  SDR-IQ Receiver's decoders (H:\SDR-IQ, SdrIq.Core RttyReceiver,
//  RttyDecoder, FaxDecoder), same algorithms and numbers, ported to C++.
//  They take the down-converter's 12 kHz complex output, before the channel
//  filter: the station's listed frequency (the centre between the RTTY
//  tones, the fax centre) on the IF dial, the AFC takes up +/- 300 Hz.
//
//  RTTY  mixed to 0 Hz, low-pass and /6 to 2000 S/s; each tone (+/- shift/2)
//        summed over one bit (a matched filter), decision (|U| - |L|) /
//        (|U| + |L|); start-stop framing, ITA2 with LTRS/FIGS shifts. Both
//        polarities decode side by side, AUTO speaks with the better one.
//        AFC: the strongest tone pair in the averaged 512-point spectra of
//        each second, +/- 300 Hz.
//  fax   mixed to 0 Hz, a +/- 1.3 kHz low-pass at 12 kS/s; FM detector
//        (phase difference); grey (f + 400) / 800, 10 % black and 90 %
//        white; lines of IOC x pi pixels at the lines per minute; start
//        (300 Hz at IOC 576, 675 Hz at 288) and stop tone (450 Hz) by their
//        share of the grey signal's AC power; phasing: the column band that
//        differs most over 40 lines is the line start. AFC: white is the
//        commonest level: the histogram's peak is put at +400 Hz.
//
//  The audio task configures and feeds the decoders; the loop takes the
//  text, the fax lines and the status.
// ----------------------------------------------------------------------------

#ifndef RX_DECODE_H
#define RX_DECODE_H

#include <stdint.h>
#include <stddef.h>

enum RxDecoderKind : uint8_t { RX_DECODER_OFF = 0, RX_DECODER_RTTY = 1, RX_DECODER_FAX = 2 };
enum RxPolarity : uint8_t { RX_POLARITY_AUTO = 0, RX_POLARITY_NORMAL = 1, RX_POLARITY_REVERSE = 2 };

#define RX_FAX_WIDTH_MAX              1810    // IOC 576 x pi
#define RX_TEXT_HISTORY               2048    // decoded characters kept for "rtty"

struct RxDecodeConfig {
  uint8_t decoder;          // RxDecoderKind
  float rttyShift;          // Hz
  float rttyBaud;
  uint8_t polarity;         // RxPolarity
  bool afc;
  float faxLpm;
  uint16_t faxIoc;
  float faxSlant;           // ppm of the line length
};

struct RxDecodeStatus {
  uint8_t decoder;          // what runs (RX_DECODER_OFF also when there was no memory)
  bool noMemory;
  float afcHz;              // the AFC's correction
  // RTTY
  float quality;            // 0 noise .. 1 clean
  bool reverse;             // the polarity decoding
  uint32_t good, bad;       // frames
  float decision;           // the last mark/space decision, -1 .. +1
  // fax
  uint32_t lines;           // lines so far
  uint16_t width;
  float startLevel, stopLevel;   // the tones' share of the grey signal's AC power
  bool phasing;             // after a start tone, before the phase is found
  int16_t phase;            // the line start found (pixels), -1 none yet
  uint32_t starts, stops, phases;   // events so far
  float lineLevel;          // the last line's mean grey, 0 black .. 1 white
};

// the audio task: (re)configures, allocating what the decoder needs (false:
// no memory, the decoder stays off); frees with RX_DECODER_OFF
bool rxDecodeConfigure(const RxDecodeConfig &config);
// one block of the down-converter's output, interleaved re, im at 12 kHz;
// centreHz: where the station's centre lies in it (CW: the pitch)
void rxDecodeProcess(const float *baseband, int count, float centreHz);
void rxDecodeFree();

// the loop: text decoded after *cursor (a count of characters since the
// start), up to max - 1 characters and a zero; the cursor moves on. A cursor
// older than the history starts at its oldest character
size_t rxDecodeText(uint32_t &cursor, char *out, size_t max);
uint32_t rxDecodeTextCount();
void rxDecodeGetStatus(RxDecodeStatus &status);
// the oldest fax line not taken: its pixels (0 black .. 255 white), width and number
bool rxDecodeTakeLine(uint8_t *pixels, uint16_t &width, uint32_t &number);

// ITA2 for the test signal: the code of a character in the letters or figures
// case (-1 none) -- the decoder's own tables
int rxIta2Code(char c, bool figures);

#endif

// ============================================================================
// ==================================  E O F  =================================
// ============================================================================
