# Receiver application — building the SigmaStudio project

The result is application 0x07 of
the Audio-DSP-Lab firmware, the SDR receiver: I and Q from a front end on
the line input K2 go untouched to the DSP's serial output for the ESP32,
which tunes, filters and demodulates them and sends the audio back over
I2S, through the equalizer's bands and volume to K3 and K4. The DSP
project is the Radio project plus two output blocks on the serial
output, fed straight from the line input; everything else, cell names
and addresses included, stays as it is, so the firmware's equalizer code
serves the receiver too.

Allow about 20 minutes. Steps 1-4 need no board.

## 1. Start from the Radio project

1. Open
   `sigmastudio\Radio\ADAU1701_Radio.dspproj`.
2. **File > Save As**
   `sigmastudio\Receiver\ADAU1701_Receiver.dspproj`
   (create the folder `Receiver`). From here on work only in the new
   project; the Radio project must stay as it is.

## 2. Check the serial output's pins

Hardware Configuration tab: compare these settings with the Analysis
project (`sigmastudio\Analysis\ADAU1701_Analysis.dspproj`), whose serial
output the ESP32 already receives:

| Setting | Must be |
| --- | --- |
| MP6 | SDATA_OUT0 |
| MP10 | OUTPUT_LRCLK |
| MP11 | OUTPUT_BCLK |
| Serial output port | master, 48 kHz |

If they agree, change nothing; otherwise set them as in the Analysis
project and note what you changed. MP0 (data), MP4 (LRCLK) and MP5
(BCLK), the I2S input from the ESP32, stay as they are. In the project as
built they agreed: its registers are those of the Radio and the Analysis
projects.

## 3. The serial output

1. Add two output blocks (*IO > Output*; each carries one channel):
   **`SerialOutL`** with its list set to **DIG0** and **`SerialOutR`**
   set to **DIG1**. DIG0 and DIG1 are the left and right slot of
   SDATA_OUT0 on MP6.
2. Wire them straight from the input cell `I2SIn`, with no gain or filter
   in between: channel 0 (ADC0, I) to `SerialOutL`, channel 1 (ADC1, Q)
   to `SerialOutR`. The Radio project leaves these two channels unused.
3. Leave every other cell, name and setting as it is: the path from
   channels 2 and 3 through `BassL/R`, `MiddleL/R`, `TrebleL/R`,
   `VolumeL/R` to the four outputs.

```
I2SIn ch 0 (ADC0, I) -------------------------------------------> SerialOutL (DIG0)
I2SIn ch 1 (ADC1, Q) -------------------------------------------> SerialOutR (DIG1)
I2SIn ch 2 (I2S L) --> BassL --> MiddleL --> TrebleL --> VolumeL --+--> OutK3L (DAC0)
                                                                    +--> OutK4L (DAC2)
I2SIn ch 3 (I2S R) --> BassR --> MiddleR --> TrebleR --> VolumeR --+--> OutK3R (DAC1)
                                                                    +--> OutK4R (DAC3)
```

## 4. Compile

1. **Link-Compile** (no board needed). It must finish without errors.
2. **File > Save**.

## 5. Produce the image and the export

The board must run the Audio-DSP-Lab firmware; the USBi on the I2C header.

1. In the Audio-DSP-Lab application tick **Hand the DSP to SigmaStudio**
   and press **Enter service** (or send `service on` in the console).
2. SigmaStudio: **Link-Compile-Download**.
3. In the Audio-DSP-Lab console, `diag clocks` must read about 48000 Hz
   (and about 3072000 Hz) on the serial output.
4. Right-click the **IC 1** block in the Hardware Configuration tab >
   **Write Latest Compilation to E2PROM**. This creates
   `ADAU1701_Receiver_IC 2\E2Prom.bin` and `E2Prom.Hex` next to the
   project.
5. **Action > Export System Files** into
   `sigmastudio\Receiver\Export`
   (file name `ADAU1701_Receiver`). Check the folder in the dialog.
6. **File > Save**, then close SigmaStudio.
7. **Unplug the USBi from the board's I2C header.**
8. Press **Leave service** (or send `service off`).

Image and export must come from the **same compile**.

## 6. Into the firmware

The parameter addresses in `Export/*_IC_1_PARAM.h` must be the Radio's:
compare them with the Radio project's export. As built they are
identical, and the image (864 bytes) has four instructions more than the
Radio's, two of them writing the serial output's slots. Convert the image
with

```
.\tools\Convert-E2PromImage.ps1 -Source "sigmastudio\Receiver\ADAU1701_Receiver_IC 2\E2Prom.bin" `
    -Name DspImageReceiver -Destination firmware\AudioDspLab\DspImageReceiver.h
```

Remember that the DSP core runs at twice the sample rate (96 kHz):
frequencies the firmware writes are computed for that rate.
The receiver's cells and addresses must stay the Radio's, and so the
equalizer's: the firmware uses the same code for all three.

With the new firmware, `diag rx` counts the receiver's blocks without
gaps and reads the levels of I (K2 left) and Q (K2 right): about
-98 to -100 dBFS with nothing on K2.
