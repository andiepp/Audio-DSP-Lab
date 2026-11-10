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

## 10 Sensitivity: a preamplifier is needed (superseded)

*R3 measured the opposite on this board (section 14): about -133 dBm in
2.4 kHz through K2, clipping above about -50 dBm. The preamplifier is
dropped (section 13). The text below is the proposal as it stood.*

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

**Revised 2026-11-10 (R3):** decision 5 dropped -- no preamplifier. The
front end into K2 reaches about -133 dBm in 2.4 kHz and clips above
about -50 dBm (section 14, R3); an attenuator ahead of the front end is
the useful addition, if any.

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
