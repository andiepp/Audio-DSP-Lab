# Equalizer application — building the SigmaStudio project

The result is application 0x02 of
the Audio-DSP-Lab firmware: the analog line input on K2 through bass, middle and
treble to a volume, on K3 and K4. The ESP32 computes and sets every
filter and the volume over I2C; the project only has to provide the
blocks, with the cell names below, because the firmware finds the
parameters by those names in the export.

Allow about 30 minutes. Steps 1-5 need no board.

## 1. Start from the Generator project

The Generator project already has the right hardware configuration
(ADAU1701, USBi, E2PROM), the slewing gain block and the four outputs.

1. Open
   `sigmastudio\Generator\ADAU1701_Generator.dspproj`.
2. **File > Save As**
   `sigmastudio\Equalizer\ADAU1701_Equalizer.dspproj`
   (create the folder `Equalizer`). From here on work only in the new
   project; the Generator project must stay as it is.
3. Hardware Configuration tab: leave it as it is.

## 2. Clear the sources

In the Schematic tab delete `Sine`, `Square`, `Triangle`, `Noise` and
`Waveform`. Keep `Amplitude` (it is renamed below) and the four outputs
`OutK3L`, `OutK3R`, `OutK4L`, `OutK4R`.

## 3. The line input

Add an input block (*IO > Input*). Its first two channels are the
analog inputs: channel 0 is **ADC0** (left), channel 1 is **ADC1**
(right); channels 2 and 3 are the I2S input, which stay unused. Name the
cell **`LineIn`**.

## 4. Six filters with exact names

Add six filters of the type the internet radio uses: *Filters > Second
Order > Double Precision >* **General (2nd order)**, one channel each.

| Cell name | Filter type in its GUI | Frequency | Gain | Q / slope |
|---|---|---|---|---|
| `BassL`, `BassR` | low shelf (shelving low) | 100 Hz | +6 dB | slope 1 (or the default) |
| `MiddleL`, `MiddleR` | peaking (parametric EQ) | 1000 Hz | +6 dB | Q 0.7 |
| `TrebleL`, `TrebleR` | high shelf (shelving high) | 10000 Hz | +6 dB | slope 1 (or the default) |

The +6 dB settings are deliberate: from the export the firmware's
coefficient formulas are compared with SigmaStudio's own, so the
settings must not be flat. Note down exactly which type names, and
whether "Q", "bandwidth" or "slope", the GUI offers.

## 5. Volume, wiring, outputs

1. Rename `Amplitude` to **`VolumeL`** and set it to **-20 dB**.
2. Add a second block of the same kind (*Volume Controls > Adjustable
   Gain*, the same "Gain (RC Slew)" block), name it **`VolumeR`**, set
   it to **-20 dB**.
3. Wire:

```
LineIn ch 0 (ADC0) --> BassL --> MiddleL --> TrebleL --> VolumeL --+--> OutK3L (DAC0)
                                                                    +--> OutK4L (DAC2)
LineIn ch 1 (ADC1) --> BassR --> MiddleR --> TrebleR --> VolumeR --+--> OutK3R (DAC1)
                                                                    +--> OutK4R (DAC3)
```

4. **Link-Compile** (no board needed). It must finish without errors.
5. **File > Save**.

## 6. Try it on the board (USBi)

The board must run the Audio-DSP-Lab firmware; the USBi on the I2C header; a
line-level source (player, phone with a line adapter) on **K2**.
Turn your amplifier or headphones down first.

1. In the Audio-DSP-Lab application tick **Hand the DSP to SigmaStudio**
   and press **Enter service** (or send `service on` in the console).
2. SigmaStudio: **Link-Compile-Download**.
3. Listen on **K3 and K4**: the source with bass, middle and treble
   raised 6 dB. Change the gains in the filter GUIs and the volume to
   hear that each acts; then put them back to the settings of steps 4
   and 5 (+6 dB, 100 Hz / 1 kHz / 10 kHz, -20 dB).

## 7. Produce the image and the export

Still in service mode:

1. Right-click the **IC 1** block in the Hardware Configuration tab >
   **Write Latest Compilation to E2PROM**. This creates
   `ADAU1701_Equalizer_IC 2\E2Prom.bin` and `E2Prom.Hex` next to the
   project — the image the firmware will carry.
2. **Action > Export System Files** into
   `sigmastudio\Equalizer\Export`
   (file name `ADAU1701_Equalizer`). Check the folder in the dialog:
   SigmaStudio may offer the project folder instead, and the export must
   not be split between two places.
3. **File > Save**, then close SigmaStudio.
4. **Unplug the USBi from the board's I2C header.** A USBi left on the
   bus — even with SigmaStudio closed — makes every EEPROM write of the
   firmware fail with error 0x11.
5. Press **Leave service** (or send `service off`) — the firmware restores its own
   application to the EEPROM; the SigmaStudio write of step 1 is only
   needed for the file.

Image and export must come from the **same compile**: after any change in
the schematic, repeat steps 1-5 together, because addresses can move.

To use the project in the firmware, take the cells' parameter addresses
from `Export/*_IC_1_PARAM.h` into `firmware/AudioDspLab/Applications.cpp`,
and convert the image with

```
.\tools\Convert-E2PromImage.ps1 -Source "sigmastudio\Equalizer\ADAU1701_Equalizer_IC 2\E2Prom.bin" `
    -Name DspImageEqualizer -Destination firmware\AudioDspLab\DspImageEqualizer.h
```

Remember that the DSP core runs at twice the sample rate (96 kHz):
frequencies the firmware writes are computed for that rate.

## If something is unclear

- A block is named differently in the toolbox than here: use the closest
  match and note which one you took.
- The General (2nd order) filter offers no low or high shelf: look for its type list in SigmaStudio's help before going on.
- The input block shows no ADC channels: check it in SigmaStudio's help.
