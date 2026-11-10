# Receiver -- the DSP-Box receiver chain as an Audio-DSP-Lab application

Design note, 2026-11-09. Status: **approved 2026-11-09** (decisions in
section 13); step R1 waits for the SigmaStudio project (section 11). The
feasibility benchmark is done on the board at COM10 (section 3); nothing
in the firmware, the application or the SigmaStudio projects has changed.

## 1 Goal

The configurable receiver of the TMS320-DSP-Box (`H:\TMS320-DSP-Box`,
`projects\dspbox_rx`, design `design\rx-chain.md`, `design\sdr-input.md`,
`design\app.md`) as one more Audio-DSP-Lab application, **`receiver`**:

- the ADAU1701's two ADCs take I and Q from the SDR front end (or a real
  IF on ADC0) and pass them unprocessed to the ESP32 on the DSP's serial
  output;
- the ESP32 runs the chain: down-converter, channel filter, AGC and
  S-meter, AM / USB / LSB / CW / FM, noise reduction (LMS, spectral),
  squelch, interpolation back to 48 kHz;
- the audio goes back to the DSP over the radio's I2S path, through the
  equalizer and volume to K3 / K4 and the headphones;
- operated from the Windows application, the CYD front panel and the pots.

Accepted: the receiver is one application among the others (one
application and one DSP image at a time), not simultaneous with them.

## 2 What is there

**DSP-Box chain** (C6713, about 10 % CPU, 13 % with I/Q): 48 kHz real IF
or I/Q -> NCO mix + 43-tap low-pass, /4 to 12 kHz complex (`rx.c`,
`ddc_process_iq` with DC trackers and gain/phase correction) -> complex
channel FIR up to 511 taps, Kaiser-designed on the DSP, incremental,
click-free set switch, download from the PC (`chan.c`) -> hang AGC with
look-ahead and S-meter (`agc.c`) -> demodulators (`demod.c`) -> noise
reduction (`nr.c`) -> squelch (`main.c`) -> x4 interpolation (`rx.c`,
`duc_process`). Block: 256 samples at 48 kHz = 64 at 12 kHz = 5.33 ms.

**DSP-Box-Lab** (`H:\TMS320-DSP-Box\app`): Core `Filters\` (Kaiser port of
`chan.c`, `FilterSpec`, `MaskDesign` for PC-designed filters,
`FilterSweep` measured response, `FilterDownload`), `FrontEnd\`
(`SdrTuner` 6 .. 18 kHz window, `ImageNull`, `FrontEndClient` for the
Uno with the Si5351A), pages Receiver / Filter / Designer / Test,
controls `DigitDial`, S-meter.

**Audio-DSP-Lab board facts** used here (proven):

- DSP serial output MP6 -> GPIO32, clocks MP10 / MP11 -> GPIO13 / 19 into
  I2S1 as slave, 48 kHz, 64 BCLK a frame, bit-exact (`Capture.cpp`,
  `diag record`; the Analysis image carries `SerialOutL/R` = DIG0 / DIG1).
- ESP32 I2S0 -> DSP MP0 (data), MP4 / MP5 (clocks) is the audio path of
  radio, synthesizer and generator (`AudioOut.cpp`); the same port puts
  12.288 MHz MCLK on GPIO0 (JP1 on external MCLK). The DSP's serial
  output runs from that MCLK, so **input and output are sample-locked**:
  no drift, no resampling.
- DSP fs 48 kHz, core 96 kHz (program runs twice a sample); the ADCs
  deliver 48 kHz.
- ESP32 FPU single precision; esp-dsp in the core (3.3.10); static DRAM is
  tight (large buffers on the heap, as `Distortion.cpp` does).
- ESP32 I2C (GPIO9 SDA / GPIO10 SCL, `Wire`, 400 kHz) carries the DSP
  (0x34) and the EEPROM (0x50) and leads to the I2C header K1 (USBi).

## 3 Benchmark (deciding number)

Sketch `design\receiver-bench\RxBench` (`Bench.cpp`): every stage ported
1:1 from `dspbox_rx`, plus esp-dsp variants and a uniformly partitioned
FFT convolution (overlap-save, block 64, complex FFT 128, 8 partitions
for 511 taps) of the channel filter. Task pinned to core 1, 240 MHz,
`esp_timer`, 400 blocks per stage, I/Q test signal (FM tone + noise),
block = 5333 us. Board COM10 (ESP32-PICO-V3), 2026-11-09; afterwards
restored with the 1.4 release flash tool (reports `Audio-DSP-Lab 1.4`,
`sigmastudio running`, as found). Raw output:
`design\receiver-bench\results-2026-11-09-COM10.txt`.

| Stage (one block) | -Os us | -O2 us | -O2 % of block |
|---|---:|---:|---:|
| I2S int32 -> float (2 x 256) | 24 | 19 | 0.4 |
| DDC I/Q: DC, I/Q correction, NCO mix, 43-tap /4 (C) | 232 | 139 | 2.6 |
| DDC, low-pass by esp-dsp `dsps_fird_f32_ae32` | 276 | 189 | 3.5 |
| Channel FIR 175 taps, direct form (C, `chan.c`) | 1465 | 903 | 16.9 |
| **Channel FIR 511 taps, direct form (C)** | **3803** | **2163** | **40.5** |
| Channel FIR 511, 4 x `dsps_dotprod_f32_ae32` | 2463 | 2461 | 46.1 |
| Channel FIR 511, 4 x `dsps_fir_f32_ae32` | 2229 | 2228 | 41.8 |
| **Channel FIR 511, FFT-128 partitioned** | **254** | **220** | **4.1** |
| Channel FIR 175, FFT-128 partitioned (3 partitions) | 179 | 166 | 3.1 |
| AGC, `log10f` + `expf` per sample | 190 | 191 | 3.6 |
| FM demodulator (`atan2f`) + de-emphasis | 103 | 103 | 1.9 |
| AM demodulator (`sqrtf`) + DC blocker | 29 | 29 | 0.5 |
| NR LMS, 64 taps | 396 | 347 | 6.5 |
| NR spectral (2 x FFT 256, minimum statistics) | 864 | 637 | 11.9 |
| x4 interpolation, 43 taps | 163 | 89 | 1.7 |
| float -> I2S int32 (2 x 256) | 28 | 23 | 0.4 |
| **Chain, direct 511, FM, LMS** (as on the C6713) | 4924 | 3059 | 57.4 (-Os 92.3) |
| **Chain, FFT 511, FM, LMS** | 1375 | **1116** | **20.9** (-Os 25.8) |
| **Chain, FFT 511, FM, spectral NR** | 1828 | **1402** | **26.3** (-Os 34.3) |
| Chain, FFT 175, AM, spectral NR | 1688 | 1285 | 24.1 |
| Channel filter design, per prototype tap (`i0f`, `sinf`) | 6.7 | 6.5 | 511 taps = 3.3 ms |

Correctness: on equal random input the dot-product, FIR and FFT variants
match the direct form to 1e-6 of full scale, at 511 taps (CW, real
symmetric) and 175 taps (USB, complex). `dsps_fir_f32` wants the taps
time-reversed (as `chan.c` stores them). The slowest single block of a
stage exceeds its mean by at most 0.15 ms (AGC, spectral NR).

**Conclusions.**

1. The C6713 port as it is does **not** fit: the direct-form 511-tap
   complex FIR alone takes 71 % (-Os) / 41 % (-O2) of the block; the whole
   chain 92 % / 57 %, no margin for I2S, the protocol and jitter.
2. esp-dsp's ae32 dot product and FIR are **not faster** than plain C at
   -O2 for this size (four real passes over the data instead of one
   complex one); not worth the dependency here.
3. **FFT fast convolution is needed for long filters, and it is cheap**:
   uniformly partitioned overlap-save with the block's own 64 samples
   (FFT 128) does 511 taps in 0.22 ms -- 10 x faster, **no added
   latency** (the output of a block is that of the direct form), exact.
   One code path serves every tap count (1 .. 8 partitions).
4. With it the worst case (511 taps, FM, spectral NR) is **26 % of one
   core at -O2** (34 % at -Os), 21 % with LMS. Compile the receiver's
   source with `#pragma GCC optimize("O2")`; the rest of the firmware
   stays -Os.
5. The on-chip design (Kaiser, 6.5 us a tap) runs in a background task,
   not in the audio block.
6. The AGC's per-sample `log10f` / `expf` (3.6 %) and the spectral NR's
   own radix-2 FFT could be made cheaper (fast log2/exp2, esp-dsp FFT),
   but there is no need.

**Verdict: feasible**, about 1/4 of one core with ample margin.

## 4 Architecture

```
SDR front end (I, Q)  -> [preamp, +30 dB] -> K2 line input
ADAU1701  "Receiver" image:
    ADC0 (I) ---------------------------> SerialOutL (DIG0) --MP6---> GPIO32  ESP32 I2S1 RX (slave)
    ADC1 (Q) ---------------------------> SerialOutR (DIG1)           (MP10/MP11 clocks -> GPIO13/19)
    I2S in (MP0, from ESP32 I2S0) -> Bass/Middle/Treble -> Volume -> DAC0/1 -> K3/K4, headphones
ESP32  receiver task (core 0, high priority):
    I2S1 RX 256 frames -> DDC -> channel filter (FFT partitioned) -> AGC / S-meter
      -> demodulator -> NR -> squelch -> x4 interpolation -> I2S0 TX 256 frames (L = R = audio)
    design task (low priority): Kaiser design -> partition spectra -> switch at a block boundary
    loop (core 1, unchanged): protocol, parameters, pots, Si5351A (option A)
```

- **Image "Receiver"** (new SigmaStudio project, section 10): the Radio
  project (I2S in -> equalizer -> volume, cells and addresses unchanged,
  so the firmware's equalizer code serves it) plus the two serial-output
  blocks of the Analysis project, fed straight from ADC0 / ADC1.
- **Receiver task** in the place of the radio's and synthesizer's audio
  tasks (core 0, WiFi idle in this application). Blocks of 256 frames:
  I2S1 RX DMA 4 x 256 frames, I2S0 TX as `AudioOut` (16-bit; the AGC
  target is -12 dBFS). The benchmark ran on core 1; both cores are equal.
- **Latency** about 25 .. 45 ms: RX block 5.3 + processing 1.4 + AGC
  look-ahead 5.3 + channel filter group delay (511 taps 21 ms, 175 taps
  7 ms) + DDC / DUC 0.9 + TX buffering. As on the DSP-Box.
- **Memory** (heap, allocated at start, freed at stop, pieces <= 16 KB):
  partition spectra 2 sets x 8 KB (click-free switch), delay line 8 KB,
  design scratch 6 KB, DDC / DUC / AGC 4 KB, NR spectral 11 KB, LMS 1 KB,
  I2S RX DMA 8 KB, panadapter (optional) 4 KB -- about **60 KB**. The
  heap offers 190 .. 200 KB, after the radio in 16 / 32 KB pieces.

## 5 Signal processing: what changes against `dspbox_rx`

| Module | Port | Change |
|---|---|---|
| `rx.c` NCO, Kaiser, DDC (`ddc_process_iq`, `ddc_process`), DUC | as is | input from I2S 24-bit (`>> 8`, scale to counts re 32768 as on the AIC23, so all dB values stay); DUC without the IF mixer (audio only) |
| `chan.c` | design as is, filtering new | `chan_process` -> partitioned overlap-save; a design or a download fills the inactive set's taps, then the design task makes its partition spectra and the audio task switches sets at a block boundary (same rule as now) |
| `agc.c`, `demod.c`, `nr.c` | as is | none |
| squelch, status (`main.c`) | as is | meters to the protocol (section 7) |
| test signal (`main.c`) | internal only | `test` tone with offset, level, noise, keying, I/Q error, fed into the chain; the "line-out loop" variant goes through the DSP image's DAC instead (optional) |
| input modes | as is | `iq`, `qi` (swapped), `if` (real IF on ADC0) |

Tuning window: the ADAU1701's ADC decimation filter ends a little below
24 kHz; the 6 .. 18 kHz window (`SdrTuner`) plus a 3 kHz filter reaches
21 kHz. Step R2 measured the response at 15 .. 23 kHz (section 14): a smooth
slope of at most 2.7 dB, no cutoff, so the window stays.

## 6 SDR front end: the Si5351A

**Option A (recommended): the ESP32 drives the Si5351A on the board's
I2C bus**, the second Uno goes. The Si5351A (address 0x60) sits on the
bus with the DSP (0x34) and the EEPROM (0x50), wired at the I2C header
K1 (SDA, SCL, GND, 3.3 V). The firmware gets `FrontEnd.cpp`, a port of
`SDR-Rx\SDRFrontend` 1.0: LO = 4 x f with 64-bit arithmetic, 1 Hz steps,
drive 0/2/4/6/8 mA, crystal correction in ppb kept in NVS (`adl-rx`),
identity check at start. The firmware also does `SdrTuner`'s split
(dial frequency -> front-end frequency + DSP offset), so app, panel and
pots set just `frequency`.

Points to check for A: the Si5351A module must run on 3.3 V I2C levels
(its pull-ups to 3.3 V, not 5 V); the I2C header is also the USBi's
socket, so in SigmaStudio mode (service) the front end is off the
firmware's hands (the receiver is not running then anyway); the LO
output wiring to the front end stays as it is.

**Option B:** the Uno stays on its own USB port (COM24) and the Windows
application drives it as DSP-Box-Lab does (`FrontEndClient`); panel and
pots then cannot tune beyond the +-21 kHz DSP window.

## 7 Protocol (text, as the other applications)

Application **`receiver`**, id **7**, title "SDR receiver". Parameters
(`params` lists them; ranges as on the DSP-Box):

| Name | Kind | Range | Default | Pot | DSP-Box id |
|---|---|---|---|---|---|
| `mode` | choice | off, am, usb, lsb, cw, fm | usb | | 1 |
| `frequency` | number, Hz | 100 k .. 50 M (front end on the board) | 14074000 | | -- |
| `offset` | number, Hz | -21000 .. 21000 (DSP tuning; follows `frequency`) | 12000 | | 2 |
| `low`, `high` | number, Hz | -6000 .. 6000 (filter edges rel. carrier, -6 dB) | mode preset | | 3, 4 |
| `width` | number, Hz | 100 .. 6000 (edges around the mode's centre) | mode preset | P1 | -- |
| `stopband` | number, dB | 40 .. 100 | 80 | | 5 |
| `pitch` | number, Hz | 300 .. 1000 | 600 | | 6 |
| `agc` | choice | off, fast, medium, slow | medium | | 7 |
| `agc-gain` | number, dB | 0 .. 100 (manual gain when off) | 60 | | 8 |
| `hang`, `decay` | number, s / dB/s | 0 .. 2 / 1 .. 200 | preset | | 9, 12 |
| `fm-deviation`, `deemphasis` | number, Hz / us | 500 .. 5000 / 0 .. 750 | 2500 / 0 | | 11, 13 |
| `squelch` | number, dBFS | -150 .. 0 | -150 | P3 | 14 |
| `nr`, `nr-level` | choice / number, dB | off, lms, spectral / 0 .. 30 | lms / 0 | P2 (level) | 19, 20 |
| `input` | choice | iq, qi, if | iq | | 23 |
| `iq-gain`, `iq-phase` | number, dB / deg | -3 .. 3 / -10 .. 10 | 0 | | 24, 25 |
| `filter` | choice | designed, downloaded | designed | | 18 |
| `test`, `test-offset`, `test-level`, `test-noise`, `test-key` | | as ids 15 .. 17, 21, 22 | off | | 15 .. 22 |
| `lo-drive`, `correction` | choice / integer ppb | off, 2, 4, 6, 8 / -100000 .. 100000 | 8 / NVS | | front end |
| `bass-gain` .. `treble-frequency`, `volume`, `output` | as the equalizer | | | P4 = volume | 10 |

New requests and events:

| Request | Response | Notes |
|---|---|---|
| `meter` | `ok <s-meter dBFS> <agc-gain dB> <fm-offset Hz> <squelch 0/1> <audio dBFS> <cpu %>` | also `! meter ...` five times a second while events are on |
| `rxfilter` | `ok designed\|downloaded <taps> <transition Hz>` | |
| `rxfilter load <taps> real\|complex` | `ok` | starts a download into the inactive set (a design request cancels it) |
| `rxfilter data <v> ...` | `ok <received>` | up to 16 numbers a line, natural order, `%.9g` (exact float); 511 complex taps about 0.2 s at 921600 Bd |
| `rxfilter end <crc32>` | `ok <taps>` / `err` | switches on the next block boundary |
| `rxspectrum [on\|off]` | `ok on\|off` | optional: `! rxspectrum <256 values>` of the I/Q input (+-24 kHz) a few times a second for a panadapter |
| `frontend` | `ok <found 0/1> <frequency> <lo> <drive> <correction>` | option A |

The Analysis application's capture (`diag record`, I2S1) and the receiver
never run together.

## 8 Windows application

Reused from DSP-Box-Lab (ported into `AudioDspLab.Core`, namespaces
renamed, MCNF parts dropped):

- `Filters\ChannelFilter` (Kaiser port of `chan.c`), `FilterSpec`,
  `MaskDesign`, `FilterSweep` (measured response with the internal test
  tone and the S-meter); `FilterDownload` rewritten for `rxfilter` text
  lines (floats, no 24-bit quantisation needed);
- `FrontEnd\SdrTuner` (also mirrored in the firmware), `ImageNull`
  (I/Q null search through `iq-gain` / `iq-phase` and the S-meter);
  `FrontEndClient` only for option B;
- controls `DigitDial` and the S-meter; the Filter page's response plot
  with draggable edges; the Designer page (mask, CSV import / export);
- their tests (Kaiser port against the DSP's numbers, mask design,
  tuner, null search).

New in Audio-DSP-Lab: a **Receiver** application page (mode keys,
frequency dial, filter preview, AGC, NR, squelch, I/Q correction, front
end, meters, Equalizer section as on the radio page) with **Filter** and
**Designer** sub-views as the Analysis page has sub-applications;
`SimulatedBoard` gets the receiver (parameters, meters, filter download)
so the page runs without hardware; `FirmwareTableTests` parse the new
firmware table.

## 9 CYD front panel

A **Receiver** page in `AudioDspLabPanel`: frequency in large digits
(tap a digit, then +/- step it, or drag), mode keys AM USB LSB CW FM,
S-meter bar with dBFS and AGC gain, filter width keys with a small
passband sketch, NR off / LMS / spectral, squelch indicator; the usual
parameter tiles and knob strip below. Optional: panadapter plot from
`! rxspectrum` with the tuned frequency marked. Layout rules as before
(content 182 px, tiles 150 x 40, ASCII fonts).

## 10 Sensitivity: a preamplifier is needed (open again)

*R3 seemed to measure the opposite (about -133 dBm in 2.4 kHz, clipping
above -50 dBm) and the preamplifier was dropped (section 13). Those
levels came from the generator's sync output, not its 100 mVpp sine; the
repeat with the sine (section 14, "R3 levels corrected") finds about
-102 dBm in 2.4 kHz and clipping about 32 dB higher. The decision is open
again. The text below is the proposal as it stood.*

The QRP Labs front end delivers I / Q at microvolt to millivolt level;
the measured requirement on the DSP-Box was about **+30 dB of low-noise
gain** before a line-level codec. The ADAU1701's ADC is about 10 dB
quieter than the AIC23 but still a line input (open-input floor about
-97 .. -100 dBFS RMS on these boards). Without a preamp the receiver
works only with strong signals. Proposed: a stereo low-noise preamp of
+30 dB (matched gain in both channels; residual mismatch is what
`iq-gain` / `iq-phase` correct) between the front end and K2. The
receiver can be built and verified without it (test tone, generator into
K2, strong signals); listening on the band waits for the preamp.

## 11 SigmaStudio steps (yours; about 20 minutes)

A new project `sigmastudio\Receiver`, recipe to become
`sigmastudio\Receiver-Instructions.md`:

1. Open `sigmastudio\Radio\ADAU1701_Radio.dspproj`; **File > Save As**
   `sigmastudio\Receiver\ADAU1701_Receiver.dspproj` (create the folder).
   Work only in the new project.
2. **Hardware Configuration tab:** compare MP6, MP10, MP11 and the serial
   output port with the Analysis project (MP6 = SDATA_OUT0, MP10 =
   OUTPUT_LRCLK, MP11 = OUTPUT_BCLK, port master, 48 kHz). Change nothing
   if they agree; otherwise set them as in the Analysis project, and note
   what you changed. MP0, MP4, MP5 (I2S input) stay.
3. Add two output blocks (*IO > Output*): **`SerialOutL`** set to
   **DIG0** and **`SerialOutR`** set to **DIG1**.
4. Wire them straight from the input cell (`I2SIn`), no gain or filter
   in between; the equalizer path from channels 2 / 3 stays:

   ```
   I2SIn ch 0 (ADC0, I) ------> SerialOutL (DIG0)
   I2SIn ch 1 (ADC1, Q) ------> SerialOutR (DIG1)
   I2SIn ch 2 / 3 (I2S) ------> BassL / BassR -> ...   (as in Radio)
   ```

5. Leave every other cell, name and address as it is. **Link-Compile**
   (no errors), **File > Save**.
6. Image and export as in the Radio recipe, section 4 (service mode,
   Link-Compile-Download, **Write Latest Compilation to E2PROM**,
   **Export System Files** into `sigmastudio\Receiver\Export`, file name
   `ADAU1701_Receiver`; unplug the USBi; leave service). Image and export
   from the same compile.
7. Before leaving service mode: `diag clocks` must read about 48000 Hz on
   the serial output.

I convert the image (`Convert-E2PromImage.ps1 ... -Name
DspImageReceiver`) and check the cell addresses against the Radio's.

## 12 Steps

| Step | Content | Check |
|---|---|---|
| R1 | You: SigmaStudio project and image (section 11); I: `DspImageReceiver.h`, application `receiver` in the firmware table with the equalizer parameters only, I2S1 RX + I2S0 TX pass-through task (I -> audio) | the app runs the image; a generator tone on K2 left is heard on K3 / K4; `diag clocks` 48 kHz; addresses as the Radio's |
| R2 | Chain port `RxChain.cpp` (-O2): DDC, partitioned channel filter with the design task, AGC, demodulators, squelch, DUC; parameters, `meter`, internal test tone; CPU load measured in the task | internal test tone: S-meter reads its level within 0.5 dB; filter response swept with the test tone matches the Kaiser prediction within 1 dB in the passband; CPU <= 30 % at 511 taps + FM; no click when the filter changes; ADC response 15 .. 22 kHz measured |
| R3 | I/Q input from K2 (I left, Q right), I/Q correction; NR (LMS, spectral) | generator I/Q (or the front end) tuned in USB / AM / FM; image rejection after the null >= 50 dB; NR as on the DSP-Box |
| R4 | Front end on the board (option A): Si5351A driver, `frequency` with the tuner split, drive, correction in NVS; `frontend` request | LO = 4 x f checked with a counter / the generator; tuning across a window edge re-centres without losing the station |
| R5 | Windows application: Receiver page, Filter and Designer sub-views, `rxfilter` download, simulator, tests | everything on the page works on the board and on the simulator; a mask filter downloads and measures as designed; tests pass |
| R6 | CYD panel Receiver page (optional panadapter `rxspectrum`) | tune, mode, width, NR from the panel; screenshots |
| R7 | Pots (P1 width, P2 NR level, P3 squelch, P4 volume), power-up option, docs (Manual chapter, Protocol, Building, SigmaStudio recipe), screenshots; release 1.5 | manual walkthrough on the board; release images flashed on both boards |

Each step ends with the firmware flashed on COM10 and checked; COM20
follows at the release. Nothing is committed or pushed without your OK.

## 13 Decisions to confirm

1. Go ahead with the port, **FFT-partitioned channel filter** in place of
   the direct form (section 3).
2. **Option A**: the Si5351A on the board's I2C, the second Uno retired
   (which Si5351A module is it -- does it run on 3.3 V I2C?).
3. A **new SigmaStudio project "Receiver"** (Radio + two serial-out
   blocks). The alternative -- adding the two blocks to the Radio project
   itself, one image for both -- saves an EEPROM rewrite when switching
   between radio and receiver but touches a released image.
4. Pot mapping P1 width, P2 NR level, P3 squelch, P4 volume (tuning on
   panel / app; the pots jitter about 0.7 %, too coarse for SSB tuning).
5. The **preamp** (section 10) as a separate hardware item; the firmware
   steps do not wait for it.

**Confirmed 2026-11-09:** 1 FFT-partitioned channel filter; 2 option A
(open: which Si5351A module, and whether its I2C pull-ups go to 3.3 V --
checked before R4 wires it to K1); 3 new project "Receiver"; 4 pots as
proposed; 5 preamp separate, firmware steps go ahead without it.

**Revised 2026-11-10 (R4):** decision 2 deferred -- for now the Uno on
COM24 keeps driving the Si5351A (option B), the Windows application
tunes it; the move to the board's I2C comes later. R4 therefore becomes
the application's tuning core (`FrontEndClient`, `SdrTuner` centred on
10 kHz, `ReceiverTuner`) instead of firmware; the firmware has no
`frequency` parameter yet.

**Revised 2026-11-10 (R3):** decision 5 dropped -- no preamplifier. The
front end into K2 reaches about -133 dBm in 2.4 kHz and clips above
about -50 dBm (section 14, R3); an attenuator ahead of the front end is
the useful addition, if any.

**Revised again 2026-11-10 (after R6):** the R3 levels were measured on the
generator's sync output. With its sine, the receiver reaches about -102 dBm
in 2.4 kHz (noise figure about 38 dB) and clips at about -18 dBm (section
14, "R3 levels corrected"). Decision 5 is open again: a preamplifier of
about +20 dB would bring the noise-equivalent input to about -122 dBm
with the clipping still near -38 dBm.

**Decided 2026-11-10:** a **10 dB wideband RF amplifier with a high IP3**
ahead of the front end (the user's hardware). Expected: noise-equivalent
input about -112 dBm in 2.4 kHz (its own noise figure is small against
the receiver's 38 dB), clipping at about -28 dBm (S9+45), the S-meter
reading dBm + 19.5 at +10 kHz. To check once fitted: the same carrier
at -50 dBm (about -30 dBFS expected), the noise floor with the
generator's output off, the clipping point.

## 14 Results

**Benchmark (2026-11-09, COM10).** Section 3. Sketch and raw output in
`design\receiver-bench\`.

**R1 done (2026-11-10, COM10).** SigmaStudio project `sigmastudio\Receiver`
= the Radio project plus `SerialOutL` (DIG0) from I2SIn channel 0 and
`SerialOutR` (DIG1) from channel 1. Parameter addresses identical to the
Radio's; registers identical to Radio and Analysis (the serial-output pins
needed no change); the image (864 bytes) adds four instructions, two of
them writing the Analysis image's DIG0/DIG1 addresses. Firmware:
`DspImageReceiver.h`, application `receiver` (0x07, "SDR receiver",
equalizer parameters, P4 volume), `Receiver.cpp` pass-through task (I on
both outputs, I2S0 ring 2 x 256 frames), `Capture.cpp` stream mode
(`diag capture` / `record` answer `busy` meanwhile), `diag rx`. Size
1,252,328 bytes (63 %, +2.5 KB), static RAM +3 KB. On the board:
`diag clocks` 47,988 Hz / 3.07 MHz; one block every 5.33 ms, no gap but
the 2 at boot over 72,000 blocks; open inputs -98.5 / -100 dBFS (RMS
without DC; the DC offset alone reads -36 dBFS); processing 96 us a
block (statistics only; `double` is software on the ESP32 and cost
0.4 ms). A tone on K2 left heard on K3 / K4; P4 drives the volume.

**R2 (2026-11-10, COM10).** `RxChain.cpp` (-O2): the DSP-Box modules
1:1 (DDC with DC trackers and I/Q correction, AGC, demodulators, squelch,
DUC, test signal with noise and keying), the channel filter as
partitioned overlap-save (block 64, FFT 128, up to 8 partitions) with a
design task on core 0 (priority 1) writing the inactive set; the audio
task switches at a block boundary. Heap about 40 KB in pieces of at most
11 KB; firmware 1,266,124 bytes (+14 KB). Parameters 10..31 (`mode`,
`offset`, `low`, `high`, `width`, `stopband`, `pitch`, `agc`,
`agc-gain`, `hang`, `decay`, `fm-deviation`, `deemphasis`, `squelch`,
`input`, `iq-gain`, `iq-phase`, `test`, `test-offset`, `test-level`,
`test-noise`, `test-key`); a mode loads its edges, an AGC speed its hang
and decay, edges and width follow each other (`setDependent`). Requests
`meter`, `rxfilter`, `diag rx [reset]`; `! meter` five times a second.
Checks with the internal test tone:

- S-meter against the test level, 0 .. -100 dBFS: 0.00 dB error.
- USB 300 .. 2700 Hz, 80 dB (203 taps, 300 Hz transition), swept
  -1000 .. 4000 Hz in 100 Hz steps against the same Kaiser design
  computed in Python: passband 0.00 dB, stopband within 1 dB down to
  -139 dB.
- CPU: USB 203 taps 14.8 %; FM with 511 taps (240 Hz, 100 dB, 151 Hz
  transition) 16.5 % average, 17.3 % the longest block.
- 92 filter switches with a 1 kHz tone on the outputs (203 <-> 203 and
  203 <-> 243 taps): no click heard.

- ADC response (loopback cable K3 -> K2; the receiver in mode `off` puts
  its test tone, AGC off at 0 dB, at offset + test-offset on the
  outputs; `diag rx` reads ADC0 / ADC1), relative to 1 kHz, DAC and ADC
  together:

  | kHz | 5 | 10 | 15 | 18 | 20 | 21 | 22 | 23 |
  |---|---:|---:|---:|---:|---:|---:|---:|---:|
  | I (left) dB | -0.5 | -1.3 | -2.0 | -2.4 | -2.6 | -2.6 | -2.6 | -2.7 |
  | Q (right) dB | -0.4 | -1.0 | -1.5 | -1.8 | -1.9 | -1.9 | -1.9 | -2.0 |

  A smooth slope, no cutoff up to 23 kHz: the ADC's decimation filter
  is not the limit, and the 6 .. 18 kHz tuning window (plus 3 kHz of
  filter, 21 kHz) stays. A station at the top of the window reads up to
  2.6 dB low on the S-meter. The channels differ by 0.5 dB at 15 kHz and
  0.7 dB at 21 kHz; `iq-gain` takes out a fixed part, the remaining
  0.2 dB across 15 .. 21 kHz limits the image rejection there (if it is
  the ADCs' and not the DACs'; R3 measures the real one).

R2 done.

**R3, internal part (2026-11-10, COM10).** Noise reduction ported from
`nr.c` (LMS line enhancer; spectral gain with the esp-dsp FFT 256 in
place of the DSP-Box's radix-2), on its own heap block (12 KB);
parameters 32 `nr` (off, lms, spectral; default lms), 33 `nr-level`
(0 .. 30 dB, default 0 = pass), 34 / 35 `test-iq-gain` / `test-iq-phase`
(the test tone's Q errors, as the DSP-Box's 26 / 27); `meter` gains a
seventh field, the spectral NR's average gain. Firmware 1,269,016 bytes.

- CPU, FM with 511 taps and the test tone: NR off 19.6 %, LMS 26.4 %,
  spectral 30.9 % (longest block 31.1 %).
- Image (tone on the mirror, test-offset -25000, S-meter without hang):
  errors 1 dB / 5 deg uncorrected 22.8 dB (theory 22.8); corrected
  exactly: below the S-meter's -150 dBFS floor (> 130 dB); corrected
  0.1 dB off 44.7 dB, 0.5 deg off 47.2 dB (both as theory).
- NR, USB, AGC off at 20 dB, tone -40 dBFS keyed 1 s on / 1 s off, noise
  -50 dBFS: key-down level -24.7 dBFS with every method; the noise
  between keys drops 10.4 dB (LMS 12), 11.3 dB (spectral 12), 21.2 dB
  (spectral 24). A steady tone, by contrast, the spectral method lowers
  by its whole floor (minimum statistics take a carrier that never stops
  for noise) -- as on the DSP-Box, where it "dulls" the sound.

**R3, bench (2026-11-10, COM10).** QRP Labs front end (SDRFrontend 1.0
on the Uno, COM24, crystal correction 325 ppb) with its I/Q on K2, RIGOL
DG4162 at 14.100 MHz, 100 mVpp into 50 ohms (-16 dBm) through 30 dB, the
20 m filter bridged as in the DSP-Box's S4. The Uno resets when a
program opens COM24 with DTR: open it with DTR and RTS low.

- Sense: `input iq` (I left) is right; the carrier at +12 kHz, 0.7 Hz
  from its nominal frequency (the FM tuning indicator).
- At -46 dBm the carrier read -8.5 dBFS and **clipped**: second-order
  products -22 dBc, third-order -26 / -30 dBc, I compressed about 1 dB
  more than Q. With 10 dB more attenuation (-56 dBm, carrier -18 dBFS)
  the second order fell 45 dB to -67 dBc, the third order (+30 kHz,
  folded to -18 kHz) 24 dB to -54 dBc, as distortion does; a product at
  -30 kHz (folded to +18 kHz, -28 dBc) fell dB for dB -- an out-of-band
  product of the front end aliased in: the path is not band-limited at
  24 kHz (section 14, R2: no cutoff up to 23 kHz).
- **12 kHz is fs/4**: the third-order product at +36 kHz folds exactly
  onto the -12 kHz mirror, where no I/Q correction removes it. At -46 dBm
  the null stopped at 29.5 dB with the carrier at +12 kHz but reached
  55.4 dB at +10 kHz.
- Image null at -56 dBm, ±150 Hz filter on the mirror: carrier at +10 kHz
  **66.6 dB** (+0.41 dB, +1.05 deg), at +12 kHz **56.0 dB** (+0.41 dB,
  +1.00 deg; the third-order product at -54 dBc sets the limit there).
  Uncorrected 31.9 dB. The DSP-Box found 0.52 dB / 1.53 deg with its
  codec: the phase is the front end's, the gain also the boards'.
- Noise with the generator off, USB 2.4 kHz, AGC off at 0 dB: -97 ..
  -102 dBFS at offsets of 8 .. 19 kHz (mean -98.6), -93.5 dBFS at 4 kHz
  (noise near the LO). With the carrier at -56 dBm reading -21.4 dBFS
  as USB audio: **S/N 77 dB, noise-equivalent input about -133 dBm in
  2.4 kHz** (10 dB S/N at about -123 dBm); the DSP-Box reached -89 dBm.
  K2 is a plain 22.5 kOhm input to the ADC; the front end into it is
  about 30 dB hotter than into the DSK's LINE IN, and the cause of that
  difference is not established. The absolute dBm rest on the
  generator's setting and the two attenuators.

Consequences: (1) **no preamplifier** -- section 10's +30 dB came from
the DSK's codec; here the problem is the other end: the path clips
above about -50 dBm at the front end, which a strong station on 20 m
(S9+20 .. +40, -53 .. -33 dBm) reaches. An attenuator ahead of the
front end (or the 20 m filter, 28 dB down until it is aligned) is the
safer addition. (2) The tuning should avoid an offset of 12 kHz (fs/4:
third order onto the image) and 16 kHz (second order: 2 x 16 = 32 kHz
folds to -16 kHz) and stay above about 8 kHz (noise near the LO): R4's
tuner split takes 10 kHz as its centre.

R3 done.

**R3 levels corrected (2026-11-10, after R6).** The generator's *sync*
output, not its main output, fed the front end through R3 .. R5: in sine
mode a square wave at the carrier frequency, about 28 dB stronger than
the 100 mVpp sine. Every absolute level above is that much off; ratios
and frequencies stand (image null, filter sweeps, tuning, distortion
orders relative to the carrier, the fs/4 and 16 kHz findings). Repeated
with the main output: **200 mVpp into 50 ohms (-10 dBm) through 40 dB,
-50 dBm at the front end**, AM 1 kHz 80 %, carrier at +10 kHz, I/Q
correction +0.41 dB / +1.05 deg, AGC off at 0 dB, through the panel's
network port while the application held the board. (A first repeat read
15 dB less: a bad connector, found and redone.)

- ADC input, whole band (`diag rx`): -42.1 / -41.8 dBFS RMS.
- Carrier in 100 Hz: **-40.5 dBFS**, so dBm = dBFS - 9.5 at this offset
  (R3's -18 dBFS for "-56 dBm" was the sync output, 28 dB stronger).
  Sidebands -49.1 / -47.8 dBFS, -8.6 / -7.3 dBc (80 % AM: -8.0 dBc):
  the chain is linear there.
- Noise, USB 2.4 kHz, generator connected: -91.3 .. -93.3 dBFS at
  offsets of 16 .. 19 kHz (mean -92.2), -91.9 at -18 kHz, -86.9 at
  -8 kHz, -84.3 at 6 kHz (near the LO); R3 had -98.6 with the generator
  off.
- **S/N 52 dB at -50 dBm: noise-equivalent input about -102 dBm in
  2.4 kHz**, noise figure about 38 dB; 10 dB S/N at about -92 dBm. (With
  R3's floor, generator off, it would be -108 dBm; a floor measured with
  the generator connected but its output off would settle which.) The
  DSP-Box reached -89 dBm: about 13 dB better here, mostly the ADAU1701's
  ADC being quieter than the AIC23 (section 10) -- and the "30 dB hotter
  than into the DSK" of R3 was the sync output.
- Clipping: the carrier clipped at -8.5 dBFS (R3), about **-18 dBm** at
  the front end, not -50 dBm (the front end's own compression is not
  measured).

Consequence: R3's (1) is reversed. The receiver is about 15 dB short of
hearing the 20 m band's own noise (which needs a noise figure of roughly
20 .. 25 dB), and it has some 15 dB of headroom above the strongest
stations (S9+40, -33 dBm). A preamplifier of about +20 dB (noise figure
well below 20 dB) is worth it again: about -122 dBm, clipping near
-38 dBm; the 20 m filter's 28 dB loss (until it is aligned) adds to the
case. Decision 5 is open again (section 13).

**R4 (2026-11-10), front end on the Uno (option B).** `AudioDspLab.Core`
gets `FrontEnd\FrontEndClient` (SDRFrontend 1.0 text protocol, serial
port opened without a DTR pulse, simulated front end), `SdrTuner`
(window 6 .. 18 kHz, re-centre with the carrier at **+10 kHz**) and
`ReceiverTuner` (dial -> front end + the board's `offset`; on a
re-centre the front end first, then the offset). Six tests (49 in all).
Bench check `app\tools\RxTuneCheck COM10 COM24 14100000`, carrier
14.100 MHz at -56 dBm, I/Q correction +0.41 dB / +1 deg:

- The carrier on the dial, FM tuning indicator in 300 Hz: front end at
  14.088 MHz (offset 12 kHz, twice) and 14.090 MHz (10 kHz): +1.0, +0.8,
  +1.0 Hz from the dial (spread 0.3 Hz). LO = 4 x f as reported, the
  dial arithmetic holds wherever the front end sits.
- USB, dial in 100 Hz steps from 14.0970 to 14.0996 MHz with the carrier
  in the passband: the front end moved once, exactly when the offset
  passed 18 kHz (14.0805 -> 14.0886 MHz); the station stayed at its
  audio frequency throughout. Its level was -23.2 dBFS at offsets of
  16.5 .. 18 kHz and -19.2 dBFS at about 10 kHz: **4 dB lower at the top
  of the window**, the slope of the front end's audio output and of the
  ADCs (R2: -2.4 dB at 18 kHz for DAC and ADC together). The AGC hides
  it in the audio; the S-meter shows it, and a re-centre makes it jump.
  A correction of the S-meter (and the AGC detector) over the offset
  would remove it -- left open.

R4 done (option B). The move of the Si5351A to the board's I2C, with
the firmware's `frequency` and `frontend`, remains for later.

**R5 (2026-11-10).** Firmware: `rxfilter load / data / end <crc32>`
(float32 values, CRC-32 as zlib's), parameter 36 `filter`, the default
offset 10 kHz; `rxfilter` reports a design or download not yet taken up
as `designing`. Core: `Receiver\` (`ChannelFilter`, `MaskDesign`,
`CoefficientFile`, `FilterSweep`, `ReceiverRequests`, `ReceiverMeter`,
`ReceiverPageLogic`), `FrontEnd\ImageNull`; the simulator serves the
receiver (its 37 parameters checked against the firmware's tables);
`BoardConnection` resynchronises after a `! ready` that interrupted a
request. Application: a **Receiver** page with Receiver, Filter and
Designer views (mode keys, RF dial through the front end, S-meter and
meters, filter preview, AGC / NR / squelch, input and I/Q correction
with "Null image", the front-end card, the equalizer; the response
chart with draggable edges and "Measure"; the mask designer with
download, CSV and saved designs), ported from DSP-Box-Lab by a
subagent and reviewed. 65 tests. On the board (`RxTuneCheck ... core`,
carrier 14.100 MHz at -56 dBm):

- ImageNull 31.9 -> 72.2 dB (+0.41 dB, +1.12 deg); mode, edges, offset
  and AGC restored.
- FilterSweep of the USB filter against `ChannelFilter`: 0.00 dB in the
  passband, within 0.2 dB down to -100 dB. The test tone gets the I/Q
  errors the correction removes (`test-iq-gain` / `-phase`); without
  that a correction of 0.4 dB read as a filter 0.2 dB low.
- A two-band mask (347 taps) downloaded in 0.44 s and swept 0.00 dB
  against its own response; "design again" returns to the edges.

Not tried by hand yet: dragging the edges on the chart, the CSV
dialogs. The page's hints already name the R7 pots (P1 width, P2 NR
level, P3 squelch); the firmware maps only P4 so far.

**R6 (2026-11-10, CYD on COM6).** A Receiver page in `AudioDspLabPanel`:
an offset dial of five digits (tap a digit, step it with - / +, or drag
along the dial, 14 px a step; the front end is the PC's, option B, so
the panel tunes the offset and the PC's dial follows), an S-meter bar
(-130 .. 0 dBFS) with the squelch's mark, level and AGC gain (FM offset
in FM) and a squelch dot, mode keys Off AM USB LSB CW FM, four width
keys that suit the mode with a passband sketch around the carrier, NR
keys with the CPU load (and the spectral NR's gain); the other
parameters as tiles below, a scroll away. The link reads `! meter` into
its own version count, so only the meter's widgets redraw five times a
second. Home keys 98 x 38 for seven applications; tile names spell AGC,
NR, FM, I/Q; USB command `digit <n>` for screenshots. Panel firmware
1,382,407 bytes. Checked: the page follows mode, width, NR and squelch
set elsewhere; tuning, keys and tiles tried by hand on the CYD. Not
done: the optional panadapter (`rxspectrum`); screenshots for the
manual come with R7.
