# Radio application — building the SigmaStudio project

The result is application 0x03 of
the Audio-DSP-Lab firmware: MP3 internet radio decoded on the ESP32, sent to the
DSP over I2S, through the equalizer's bands and volume to K3 and K4. The
DSP project is the Equalizer project with its input moved from the
analog line input to the I2S input; everything else, cell names and
addresses included, stays as it is.

Allow about 15 minutes. Steps 1-3 need no board.

## 1. Start from the Equalizer project

1. Open
   `sigmastudio\Equalizer\ADAU1701_Equalizer.dspproj`.
2. **File > Save As**
   `sigmastudio\Radio\ADAU1701_Radio.dspproj`
   (create the folder `Radio`). From here on work only in the new
   project; the Equalizer project must stay as it is.
3. Hardware Configuration tab: leave it as it is. Its serial input
   (I2S) and multipurpose pins (MP0 data, MP4 LRCLK, MP5 BCLK) are
   already those of your working InternetRadio project.

## 2. Move the input to I2S

1. Delete the two wires from `LineIn` channel 0 to `BassL` and from
   channel 1 to `BassR`.
2. Wire `LineIn` **channel 2** (I2S left) to `BassL` and **channel 3**
   (I2S right) to `BassR`.
3. Rename `LineIn` to **`I2SIn`**.
4. Leave every other cell, name and setting as it is: `BassL/R`,
   `MiddleL/R`, `TrebleL/R`, `VolumeL/R`, the four outputs.

```
I2SIn ch 2 (I2S L) --> BassL --> MiddleL --> TrebleL --> VolumeL --+--> OutK3L (DAC0)
                                                                    +--> OutK4L (DAC2)
I2SIn ch 3 (I2S R) --> BassR --> MiddleR --> TrebleR --> VolumeR --+--> OutK3R (DAC1)
                                                                    +--> OutK4R (DAC3)
```

## 3. Compile

1. **Link-Compile** (no board needed). It must finish without errors.
2. **File > Save**.

There is nothing to try on the board in SigmaStudio this time: the I2S
input only carries sound once the ESP32 streams, which the new firmware
does.

## 4. Produce the image and the export

The board must run the Audio-DSP-Lab firmware; the USBi on the I2C header.

1. In the Audio-DSP-Lab application tick **Hand the DSP to SigmaStudio**
   and press **Enter service** (or send `service on` in the console).
2. SigmaStudio: **Link-Compile-Download**.
3. Right-click the **IC 1** block in the Hardware Configuration tab >
   **Write Latest Compilation to E2PROM**. This creates
   `ADAU1701_Radio_IC 2\E2Prom.bin` and `E2Prom.Hex` next to the
   project.
4. **Action > Export System Files** into
   `sigmastudio\Radio\Export`
   (file name `ADAU1701_Radio`). Check the folder in the dialog.
5. **File > Save**, then close SigmaStudio.
6. **Unplug the USBi from the board's I2C header.**
7. Press **Leave service** (or send `service off`)

Image and export must come from the **same compile**.

## 5. WiFi

Nothing to do in SigmaStudio: the radio's WiFi networks are entered in the
Audio-DSP-Lab application (**WiFi networks …**) and kept on the board.

To use the project in the firmware, take the cells' parameter addresses
from `Export/*_IC_1_PARAM.h` into `firmware/AudioDspLab/Applications.cpp`,
and convert the image with

```
.\tools\Convert-E2PromImage.ps1 -Source "sigmastudio\Radio\ADAU1701_Radio_IC 2\E2Prom.bin" `
    -Name DspImageRadio -Destination firmware\AudioDspLab\DspImageRadio.h
```

Remember that the DSP core runs at four times the sample rate (192 kHz):
frequencies the firmware writes are computed for that rate.
The radio's cells and addresses must stay the equalizer's: the firmware
uses the same code for both.
