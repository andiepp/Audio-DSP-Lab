# SigmaStudio

Audio-DSP-Lab works with Analog Devices **SigmaStudio 4.7** in two ways: you can run **your own programs** on the board in SigmaStudio mode, and the five built-in applications are SigmaStudio projects you can study and change.

## Your own programs: SigmaStudio mode

In SigmaStudio mode the DSP runs whatever program its EEPROM holds, and the firmware keeps out of the way:

- it supplies the DSP's 48 kHz master clock (JP1 on ext. MCLK),
- it stays off the I2C bus, so SigmaStudio and the **USBi** can download programs and write the EEPROM at any time,
- it passes the four pots to the DSP's auxiliary ADC inputs every 20 ms: P1 to ADC0 (MP9), P2 to ADC1 (MP2), P3 to ADC2 (MP3), P4 to ADC3 (MP8).

### Workflow

1. Select **SigmaStudio** in the application (the board also starts in this mode at power-up, unless **Power-up** is set to **Last application**).
2. Connect the USBi to the board's I2C header.
3. In SigmaStudio: **Link-Compile-Download** to try a program; the DSP runs it at once.
4. To keep it, write it to the EEPROM: right-click the E2PROM in the Hardware Configuration and choose **Write Latest Compilation to E2PROM**. Confirm that the write succeeded.
5. **Unplug the USBi** before you select another application.

At the next power-up in SigmaStudio mode the DSP boots your program from the EEPROM by itself.

### Your program is safe

Selecting another application writes that application's program into the EEPROM. Before it does, the firmware copies your program into the ESP32's flash, and when you return to SigmaStudio mode it writes the copy back. The SigmaStudio page shows the copy's length and checksum. If the copy cannot be made (for example because the USBi is still on the bus), the firmware refuses the switch rather than lose your program.

### Your project's settings

| Setting | Value |
| --- | --- |
| IC | ADAU1701, I2C address 0x34 (0x68 in SigmaStudio's 8-bit notation) |
| EEPROM | 24C256 (32 KB) at 0x50 (0xA0) |
| Clock | MCLK 12.288 MHz = 256 × 48 kHz from the ESP32; PLL mode 256 × fs |
| Inputs | ADC0/ADC1 = K2 left/right; serial input 0/1 = I2S from the ESP32 (only the radio and the synthesizer use it) |
| Outputs | DAC0/DAC1 = K3, DAC2/DAC3 = K4 |
| Pots | Auxiliary ADC 0 to 3 (see above), read with SigmaStudio's Auxiliary ADC input blocks |

The project `sigmastudio/SigmaStudio/ADAU1701_SigmaStudio.dspproj` is a small working example (a VCO whose pitch follows P1). Start your own projects from it or from Elektor's Demo1: their hardware configuration fits the board.

**About the core rate.** The repository's projects keep Elektor Demo1's core setting, which runs the program four times per 48 kHz sample (192 kHz). In such projects the frequencies you type into SigmaStudio's block GUIs act four times higher than shown. A project of your own with the standard core setting behaves normally.

## Service mode

Service mode is for working on an application's own project while that application runs: the firmware releases the EEPROM's write protection and leaves the I2C bus. Enter it from the application (**Service** on the right), work in SigmaStudio, unplug the USBi and press **Leave service**: the firmware writes the application's own program back. Whatever SigmaStudio wrote to the EEPROM meanwhile is replaced, so for your own programs use SigmaStudio mode.

## The built-in applications' projects

Each application's DSP program is a SigmaStudio project in `sigmastudio/`. The ESP32 sets every value through the DSP's safeload registers, by the parameter addresses SigmaStudio exported; the projects only provide the blocks.

| Folder | Application | What the project contains | Recipe |
| --- | --- | --- | --- |
| `Generator` | Generator | Sine, square, triangle, noise sources; a 4-input switch; a slewed gain; outputs K3, K4 | [Generator-Instructions.md](../sigmastudio/Generator-Instructions.md) |
| `Equalizer` | Equalizer | Line input; three general biquads per channel (bass, middle, treble); slewed volume | [Equalizer-Instructions.md](../sigmastudio/Equalizer-Instructions.md) |
| `Radio` | Radio, Synthesizer | The equalizer with its input moved to the I2S serial input | [Radio-Instructions.md](../sigmastudio/Radio-Instructions.md) |
| `Response` | Response | The generator's sine and gain; per input channel a band-pass, absolute value, smoothing low-pass and a readback | [Response-Instructions.md](../sigmastudio/Response-Instructions.md) |
| `SigmaStudio` | (example) | A VCO controlled by P1 | – |

Each folder holds the `.dspproj`, the self-boot image (`ADAU1701_<name>_IC 2/E2Prom.bin`) and SigmaStudio's export (`Export/`), all from the same compile. The firmware carries the images as C arrays (`firmware/AudioDspLab/DspImage*.h`, made with `tools/Convert-E2PromImage.ps1`) and the parameter addresses in `firmware/AudioDspLab/Applications.cpp`.

The project files are kept byte for byte as SigmaStudio wrote them (`.gitattributes` marks them binary): SigmaStudio's exports carry trailing whitespace and Windows line ends.

### Changing a built-in application

1. Open the project, change it, **Link-Compile-Download** to try it (service mode, or SigmaStudio mode).
2. Write the EEPROM image (**Write Latest Compilation to E2PROM**) and **Export System Files** into the project's `Export` folder, from the same compile.
3. Compare the exported parameter addresses (`Export/*_IC_1_PARAM.h`) with those in `Applications.cpp`, and update them where they moved.
4. Convert the image: `.\tools\Convert-E2PromImage.ps1 -Source "sigmastudio\<name>\ADAU1701_<name>_IC 2\E2Prom.bin" -Name DspImage<Name> -Destination firmware\AudioDspLab\DspImage<Name>.h`.
5. Build and flash the firmware ([Building.md](Building.md)). Selecting the application writes the new image into the EEPROM.
