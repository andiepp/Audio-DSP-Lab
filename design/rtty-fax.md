# DWD RTTY and weather fax in the receiver backend

Status: APPROVED 2026-11-21; implemented the same day (board firmware 1.8, panel 1.8); see section 8 for what was measured.

The SDR-IQ Receiver (H:\SDR-IQ, 1.6) decodes the Deutscher Wetterdienst's RTTY (50 baud ITA2, 85 or 450 Hz shift, mark on the lower tone) and its HF weather fax (120 lpm, IOC 576, ±400 Hz) on the PC, from the SDR's I/Q. This note brings the same two decoders to the Audio-DSP-Lab receiver backend, its PC application and the CYD front panel.

## 1 Where the decoding runs

In the SDR-IQ project the PC has the I/Q. Here only the board's ESP32 has it: the PC application and the panel see protocol text. So **both decoders run on the board**, in the receiver's task, and send their results as protocol events:

| Part | Does |
|---|---|
| Board firmware | RttyDecoder and FaxDecoder ported to C++ from SdrIq.Core (same algorithms, same numbers), fed from the receiver chain; text and fax lines as events |
| PC application | A Decoder view on the Receiver page: RTTY text, quality, AFC, polarity; the daily log and the ZCZC..NNNN messages; the fax chart window, PNGs |
| Panel | Decoder keys on the receiver page; the last RTTY lines with a quality bar; a scrolling fax preview at reduced width |
| Simulator | Plays a decoded DWD text and a test chart, for the demo, the tests and the screenshots |

## 2 The tap and the tuning

The decoders take the **down-converter's output**: 12 kHz complex, centred on the IF dial, before the channel filter (whose edges belong to the audio). The front end puts the station's listed frequency (the centre between the RTTY tones, the fax centre) on the IF, as the SDR-IQ Receiver's IF mode already does; the decoders' AFC (±300 Hz) takes up the rest, as in the SDR-IQ project.

The audio goes on in the selected mode. In CW the down-converter sits at the IF minus the pitch; the decoders compensate, so CW with a wide filter (1 kHz) is the way to listen to both tones. Only one decoder runs at a time.

| Decoder | Rate | From the 12 kHz stream |
|---|---|---|
| RTTY | 2000 S/s | low-pass and decimation by 6 |
| Fax | 12000 S/s | a complex low-pass of about ±1.2 kHz, no decimation (a line of 6000 samples at 120 lpm, 3.3 samples per pixel) |

The line timing follows the board's crystal (ESP32, 40 MHz); the fax's slant correction (ppm) straightens a slanted chart, as on the SDR-IQ.

## 3 Parameters (receiver application)

| Parameter | Kind | Range | Default |
|---|---|---|---|
| `decoder` | choice | off, rtty, fax | off |
| `rtty-shift` | choice | 85, 170, 450, 850 Hz | 450 |
| `rtty-polarity` | choice | auto, normal, reverse | auto |
| `fax-lpm` | choice | 60, 90, 120, 240 | 120 |
| `fax-ioc` | choice | 288, 576 | 576 |
| `fax-slant` | number | ±200 ppm | 0 |
| `decoder-afc` | switch | | on |

They join the receiver's table, so `params`, the simulator's table test and the stored settings cover them like every other parameter. No pot changes.

## 4 Protocol

- `! rtty "<text>"`: the characters decoded since the last event (LF as `\n`), at most every 200 ms.
- `! decoder <kind> <quality> <afc-hz> <polarity> <good> <bad>` for RTTY, `! decoder fax <state> <line> <afc-hz> <start> <stop>` for fax, twice a second, with the meter.
- `! fax <line> <part> <parts> <data>`: one chart line, 4-bit grey, run-length coded text (a hex digit per pixel, a letter for a run of the previous level); a white line of 1810 pixels is about 90 characters, a busy one about 1000, sent in parts that fit the board's 384-byte line limit. A port chooses the width with `fax events off|full|<width>`: the PC takes full width (1810 at IOC 576), the panel 302 (every 6th pixel averaged), so its 115,200-baud cable is not swamped. `! fax start`, `! fax stop` and `! fax phase <pixel>` mark the chart.
- `rtty` answers the last 1,000 characters (a panel or PC that connects late gets the screen filled).

## 5 Budget

| Item | Estimate |
|---|---|
| CPU | RTTY about 2 % of a block (two decoders, AFC FFT of 512 points once per 128 ms); fax about 3 % (atan2f at 12 kS/s, line assembly) |
| Heap, while on | RTTY about 25 KB (AFC buffer, FFT, two decoders); fax about 25 KB (line and phasing sums at 1810 pixels, filter) |
| USB | a fax line at full width: 0.1 to 1 KB, two per second |
| Panel cable | at 302 pixels: at most about 0.6 KB/s |

## 6 Steps

1. Firmware: decoders ported, fed from the chain; parameters; events and `rtty`, `fax events`; checked with the internal test signal (an RTTY or fax test mode of the test tone) and a recording played into K2.
2. Simulator and PC application: Decoder view, log and messages, fax window and PNGs; tests (ITA2 framing, RLE, the table test).
3. Panel: decoder keys, RTTY lines, fax preview.
4. On the air: the SDR-IQ Receiver as front end in IF mode (DDH9 11039 kHz, DDK3 7880 kHz) into K2.
5. Docs: manual, protocol, panel, Implementation Details; release.

## 7 Open points

- Panel fax preview: a 302 × 150 pixel 4-bit canvas (about 23 KB of the CYD's heap) showing the newest 75 s of the chart, or RTTY only on the panel.
- The test signal: an FSK and a fax test mode in the board's test generator (so the decoders can be checked without a front end), or recordings only.

## 8 As built (2026-11-21)

- **Rates.** RTTY at 2000 S/s as planned. Fax at **6000 S/s** (low-pass at 12 kS/s, every other sample), not 12000: half the cost, still 1.7 samples a pixel at 120 lpm.
- **Mixers** are rotating phasors (one complex multiplication a sample, the step set and the length restored once a block); a sine per sample made the first RTTY build cost 17 % of a block.
- **CPU** (share of the 5.33 ms block, measured with the test signal): RTTY +3.6 %, fax +4.9 %. The block in which a decoder is switched on takes up to 6.1 ms (allocation, filter design, FFT sign check); the output ring of two blocks covers it.
- **Phasing.** A phased line is taken from the end of one received line and the start of the next. Rotating one received line (as the SDR-IQ Receiver does) put a one-line step into the chart at column width - phase; seen on the first test chart and corrected.
- **Fax line text.** A full run of 20 is followed directly by the next run letter; the first encoder repeated the pixel's digit after each run, which doubled a white line to 174 characters (now 92).
- **Checked on the board** (COM10, test signal, no front end): RTTY the whole test text, AUTO on reverse, quality 0.98, AFC -3 Hz; fax start tone, phasing (line start found), the test chart clean at 120 lpm, also 120 Hz off with noise (AFC 110 Hz). The panel (two-USB CYD) shows both, at 300 pixels.
- **Not yet checked on the air**: the SDR-IQ front end into K2 (the PC's line-out was unplugged that day).
