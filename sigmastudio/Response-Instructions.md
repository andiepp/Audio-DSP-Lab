# Response application — building the SigmaStudio project

The result is application 0x04 of
the Audio-DSP-Lab firmware, the response analyzer: a sine on K3 and K4, and on
each channel of the line input K2 a level the firmware reads back. The ESP32
sets the sine, computes every filter and reads the levels over I2C; the
project only has to provide the blocks, with the cell names below,
because the firmware finds them by those names in the export.

Allow about 40 minutes. Steps 1-5 need no board; step 6 needs a stereo
cable from K3 to K2.

## 1. Start from the Generator project

1. Open
   `sigmastudio\Generator\ADAU1701_Generator.dspproj`.
2. **File > Save As**
   `sigmastudio\Response\ADAU1701_Response.dspproj`
   (create the folder `Response`). From here on work only in the new
   project; the Generator project must stay as it is.
3. Hardware Configuration tab: leave it as it is.

## 2. The sine and its outputs

1. Delete `Square`, `Triangle`, `Noise` and `Waveform`.
2. Keep `Sine`, `Amplitude` and the four outputs `OutK3L`, `OutK3R`,
   `OutK4L`, `OutK4R`, with their names.
3. Wire `Sine` directly to `Amplitude`; `Amplitude` stays wired to all
   four outputs.
4. `Sine` at 1000 Hz, `Amplitude` at -20 dB.

## 3. The line input

Add an input block (*IO > Input*), name it **`LineIn`**: channel 0 is
**ADC0** (left), channel 1 is **ADC1** (right), as in the Equalizer
project.

## 4. Per channel: band-pass, absolute value, smoothing, readback

Eight blocks, four per channel, with exact names:

| Cell names | Block | Setting in its GUI |
|---|---|---|
| `BandL`, `BandR` | *Filters > Second Order > Double Precision >* **General (2nd order)**, one channel | **band-pass**, 1000 Hz, Q 5 (or the closest the GUI offers) |
| `AbsL`, `AbsR` | the **absolute value** block (*Basic DSP > Arithmetic Operations > Abs*, or similar) | — |
| `SmoothL`, `SmoothR` | **General (2nd order)** again, double precision, one channel | **low-pass**, 10 Hz, Q 0.707 (Butterworth) |
| `LevelL`, `LevelR` | the **readback** block (*DSP Readback*; the toolbox may list it under *Basic DSP*, *Level Detectors* or *IO*) | — |

Wire:

```
Sine --> Amplitude --+--> OutK3L (DAC0)      LineIn ch 0 (ADC0) --> BandL --> AbsL --> SmoothL --> LevelL
                     +--> OutK3R (DAC1)
                     +--> OutK4L (DAC2)      LineIn ch 1 (ADC1) --> BandR --> AbsR --> SmoothR --> LevelR
                     +--> OutK4R (DAC3)
```

The band-pass lets only the measured frequency through; the absolute
value and the low-pass turn the sine into a steady level; the readback
hands that level to the ESP32. The firmware recomputes both filters for
every frequency of a sweep, so the GUI settings only have to be what the
table says: the export shows how SigmaStudio stores a band-pass and a
low-pass, which the firmware's formulas are checked against.

Note down:
- the type names the General (2nd order) GUI offers for band-pass and
  low-pass, and whether it asks for Q, bandwidth or slope;
- the exact name and toolbox folder of the absolute-value and readback
  blocks you took;
- the readback block's display format setting (decimal, dB, ...), if
  it has one.

Then **Link-Compile** (no board needed); it must finish without errors.
**File > Save**.

## 5. Nothing to change in the readback

Leave the readback blocks as they come. SigmaStudio compiles each one
into a setting for the DSP's data capture register; the export carries
it, and the firmware uses it.

## 6. Try it on the board (USBi), with a loopback cable

The board must run the Audio-DSP-Lab firmware; the USBi on the I2C header; a
**stereo cable from K3 to K2** (output to line input). Nothing else on
K3 or K4; turn any amplifier down.

1. In the Audio-DSP-Lab application tick **Hand the DSP to SigmaStudio**
   and press **Enter service** (or send `service on` in the console).
2. SigmaStudio: **Link-Compile-Download**.
3. Watch the two readback blocks: each shows a value that SigmaStudio
   reads from the DSP. Note both values with the sine at 1000 Hz and
   `Amplitude` at -20 dB.
4. Set `Amplitude` to -40 dB, note both values; back to -20 dB.
5. Pull the cable out of K2, note both values (the noise floor); plug it
   back in.

The frequencies in SigmaStudio's GUIs play twice as high on this
board (the DSP core runs at twice the sample rate); that
is expected and does not matter here.

## 7. Produce the image and the export

Still in service mode:

1. Right-click the **IC 1** block in the Hardware Configuration tab >
   **Write Latest Compilation to E2PROM**. This creates
   `ADAU1701_Response_IC 2\E2Prom.bin` and `E2Prom.Hex` next to the
   project — the image the firmware will carry.
2. **Action > Export System Files** into
   `sigmastudio\Response\Export`
   (file name `ADAU1701_Response`). Check the folder in the dialog.
3. **File > Save**, then close SigmaStudio.
4. **Unplug the USBi from the board's I2C header.** A USBi left on the
   bus makes every EEPROM write of the firmware fail with error 0x11.
5. Press **Leave service** (or send `service off`)

Image and export must come from the **same compile**: after any change
in the schematic, repeat steps 1-5 together, because addresses can move.

To use the project in the firmware, take the cells' parameter addresses
from `Export/*_IC_1_PARAM.h` into `firmware/AudioDspLab/Applications.cpp`,
and convert the image with

```
.\tools\Convert-E2PromImage.ps1 -Source "sigmastudio\Response\ADAU1701_Response_IC 2\E2Prom.bin" `
    -Name DspImageResponse -Destination firmware\AudioDspLab\DspImageResponse.h
```

Remember that the DSP core runs at twice the sample rate (96 kHz):
frequencies the firmware writes are computed for that rate.

## 8. The serial output, for the distortion measurement

The distortion measurement takes the line input's samples to the ESP32
and computes their spectrum there. The DSP sends them on its serial
output: MP6 (SDATA_OUT0) to ESP32 GPIO32, its clocks MP10 (OUTPUT_LRCLK)
and MP11 (OUTPUT_BCLK) to GPIO13 and GPIO19. The project's register
settings already make that port a clock master; the ESP32 measured
48 kHz and 64 bit clocks a frame (`diag clocks`). The only thing the
project lacks is something on the port, so this step adds two output
blocks and changes nothing else. Allow about 20 minutes. The board needs
firmware with `diag capture` (after 1.1), the USBi on the I2C header, and
the stereo cable from K3 to K2.

1. Open `sigmastudio\Response\ADAU1701_Response.dspproj`.
2. **Hardware Configuration tab: change nothing.** Its register and
   pin settings are what the ESP32 relies on.
3. Add two output blocks (*IO > Output*; each carries one channel):
   **`SerialOutL`** with its list set to **DIG0** and **`SerialOutR`**
   set to **DIG1**. DIG0 and DIG1 are the left and right slot of
   SDATA_OUT0 on MP6; DIG2 and up go to pins the ESP32 does not hear.
4. Wire them in parallel with what is there; nothing existing changes:

   ```
   LineIn ch 0 (ADC0) --+--> BandL --> ...       (as before)
                        +--> SerialOutL (DIG0)
   LineIn ch 1 (ADC1) --+--> BandR --> ...       (as before)
                        +--> SerialOutR (DIG1)
   ```

   Straight from the input: no gain, no filter in between.
5. **Link-Compile** (no board needed); it must finish without errors.
   **File > Save**.

Note down the exact names the output block offers for its channels, and
which two you took.

### Try it on the board

1. In the Audio-DSP-Lab application tick **Hand the DSP to SigmaStudio**
   and press **Enter service** (or send `service on` in the console).
2. SigmaStudio: **Link-Compile-Download**. `Sine` at 1000 Hz and
   `Amplitude` at -20 dB, as in step 6.
3. In the application's console send `diag capture`. It needs no I2C, so
   it works while SigmaStudio holds the DSP. The answer is

   ```
   ok <rate> <frames> <rms-L> <peak-L> <hz-L> <rms-R> <peak-R> <hz-R> <first-word>
   ```

   with the levels in dBFS (a full-scale sine is -3.01 dB RMS, 0 dB
   peak). Note the whole line.
4. Set `Amplitude` to -40 dB, send `diag capture` again, note it; back
   to -20 dB.
5. Pull the cable out of K2, `diag capture`, note it (the floor); plug
   it back in.
6. Swap the cable's channels if you can (or pull only one side) to see
   which reading follows which input.

Expected: a rate near 48000; on both channels a peak near -27 dBFS
(the loopback from K3 to K2 loses about 7 dB), 20 dB lower at -40 dB;
and the same frequency on both, 2000 Hz: SigmaStudio's frequencies play
twice as high on this board (see step 6). The levels are measured from
the input's DC offset (about -0.016 of full scale), which `diag capture`
reports in its last two fields.

To check the path alone, wire `SerialOutR` to the sine (after
`Amplitude`) instead of LineIn ch 1 for a moment: `diag record` then
shows a perfect sine at exactly -20.00 dBFS on the right. Wire it back
before step 7.

If `diag capture` answers `err no-clock`, the port lost its clocks: check
that nothing in the Hardware Configuration tab changed. If it answers
with -200 dB on both channels, the samples did not reach the port: check
the output block's channel choice.

### Image and export

When the readings look as expected, do **step 7** again (image and
export from the same compile) and send the noted lines and names along.
The firmware then takes over the new image and its addresses, which may
have moved.

## If something is unclear

- A block is named differently in the toolbox than here: use the
  closest match and note which one you took.
- The General (2nd order) GUI offers no band-pass or no low-pass: look for its type list in SigmaStudio's help before going on.
- No readback block can be found: check the toolbox tree in SigmaStudio's help.
