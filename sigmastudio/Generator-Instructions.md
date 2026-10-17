# Generator application — building the SigmaStudio project

The result is application 0x01 of
the Audio-DSP-Lab firmware: four waveforms, 20 Hz to 20 kHz, one amplitude, on K3
and K4. The ESP32 sets every value over I2C; the project only has to
provide the blocks, with the cell names below, because the firmware finds
the parameters by those names in the export.

Allow about 30 minutes. Steps 1-5 need no board.

## 1. Start from the Demo1 project

Demo1 already has the right hardware configuration (ADAU1701, USBi,
E2PROM) and two of the four sources.

1. Open Elektor's Demo1 project
   `ADAU1701_Demo1_Generator.dspproj` (from the examples of Elektor's Audio DSP library).
2. **File > Save As**
   `sigmastudio\Generator\ADAU1701_Generator.dspproj`
   (create the folder `Generator`).
3. Hardware Configuration tab: leave it as it is.

## 2. Check the toolbox for triangle and noise

In the Schematic tab, open the toolbox tree **Sources** (and its
sub-folders such as *Oscillators* and *Noise*). Look for:

- a **triangle** source,
- a **white noise** source.

If either is missing, use the closest source the toolbox offers and keep
the cell names below.

## 3. Sources — four cells with exact names

| Cell name | Block | Setting in its GUI |
|---|---|---|
| `Sine` | already present (Tone Synthesis lookup/sine) | 1000 Hz, on |
| `Square` | already present (Square Wave) | 1000 Hz, on |
| `Triangle` | triangle source from step 2 | 1000 Hz, on |
| `Noise` | white noise source from step 2 | on |

Rename a cell by double-clicking its name under the block. The 1000 Hz
settings are only defaults; they let the export show how each block
scales frequency, which the firmware needs.

## 4. Waveform selector and amplitude

1. Select the existing **Switch** block (Mono Switch Nx1) and give it
   **four** inputs: right-click > *Add Input* (or grow it) until it shows
   four. Rename it to **`Waveform`**.
2. Wire: `Sine` -> input 1, `Square` -> input 2, `Triangle` -> input 3,
   `Noise` -> input 4. Select input 1 (sine) in its GUI.
3. Delete **VolumeLeft** and **VolumeRight**.
4. Add one volume block for the amplitude, cell name **`Amplitude`**:
   - preferably a **slewing** single volume (*Volume Controls >
     Adjustable Gain*, a "single ... slew" block), so amplitude changes
     do not click;
   - otherwise the plain **Single Volume** that Demo1 used.
   Set it to **-20 dB**.
5. Wire `Waveform` output -> `Amplitude` input.

## 5. Four outputs

1. Keep the two output blocks and rename them **`OutK3L`** (DAC0) and
   **`OutK3R`** (DAC1).
2. Add two more output blocks (*IO > Output*), set them to **DAC2** and
   **DAC3**, and name them **`OutK4L`** and **`OutK4R`**.
3. Wire the `Amplitude` output to **all four** outputs (one output pin may
   feed several inputs).
4. **Link-Compile** (no board needed). It must finish without errors.
5. **File > Save**.

The schematic is now:

```
Sine     --+
Square   --+--> Waveform (4 in) --> Amplitude --+--> OutK3L (DAC0)
Triangle --+                                     +--> OutK3R (DAC1)
Noise    --+                                     +--> OutK4L (DAC2)
                                                 +--> OutK4R (DAC3)
```

## 6. Try it on the board (USBi)

The board must run the Audio-DSP-Lab firmware; the USBi on the I2C header.
Turn your amplifier or headphones down first.

1. In the Audio-DSP-Lab application tick **Hand the DSP to SigmaStudio**
   and press **Enter service** (or send `service on` in the console).
2. SigmaStudio: **Link-Compile-Download**.
3. Listen on **K3 and K4** while you change things in the GUIs: each
   waveform via `Waveform`, frequency 20 Hz, 1 kHz and 20 kHz on the
   source in use, amplitude via `Amplitude`.
4. Put the GUIs back to the defaults of steps 3 and 4 (1000 Hz, sine,
   -20 dB).

## 7. Produce the image and the export

Still in service mode:

1. Right-click the **IC 1** block in the Hardware Configuration tab >
   **Write Latest Compilation to E2PROM**. This creates
   `ADAU1701_Generator_IC 2\E2Prom.bin` and `E2Prom.Hex` next to the
   project — the image the firmware will carry.
2. **Action > Export System Files** into
   `sigmastudio\Generator\Export`
   (file name `ADAU1701_Generator`). Check the folder in the dialog:
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
.\tools\Convert-E2PromImage.ps1 -Source "sigmastudio\Generator\ADAU1701_Generator_IC 2\E2Prom.bin" `
    -Name DspImageGenerator -Destination firmware\AudioDspLab\DspImageGenerator.h
```

Remember that the DSP core runs at four times the sample rate (192 kHz):
frequencies the firmware writes are computed for that rate.

## If something is unclear

- A block is named differently in the toolbox than here: use the closest
  match and note which one you took.
- The `Waveform` switch cannot get four inputs: use two 2-input switches
  in a row, named `WaveformA` and `WaveformB`, and adapt the firmware's waveform switch to them.
