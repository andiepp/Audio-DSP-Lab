# Third-party notices

Audio-DSP-Lab's own code (the firmware's sketch in `firmware/AudioDspLab`, the panel firmware in `firmware/AudioDspLabPanel`, the application in `app/`, the tools in `tools/`, the SigmaStudio projects in `sigmastudio/` and the documentation) is under the MIT licence in [LICENSE](LICENSE).

The **compiled firmware** also contains the following works. Whoever distributes the firmware binary (`AudioDspLab.bin`, `AudioDspLab-merged.bin`) distributes these works too, under their licences.

## Libraries in the firmware

| Work | Author | Licence | Where |
| --- | --- | --- | --- |
| arduino-audio-tools 1.1.0 | Phil Schatzmann | GNU GPL v3.0 | `firmware/libraries/arduino-audio-tools-main` (licence: `License.txt`) |
| codec-helix (libhelix) 0.9.4, the Arduino wrapper | Phil Schatzmann | GNU GPL v3.0 | `firmware/libraries/codec-helix-main` (licence: `License.txt`) |
| Helix MP3 and AAC decoders, inside codec-helix | RealNetworks, Inc. | RealNetworks Public Source License 1.0 (RPSL), or the RealNetworks Community Source License (RCSL) | `firmware/libraries/codec-helix-main/src/libhelix-mp3`, `.../libhelix-aac` (licences: `RPSL.txt`, `RCSL.txt`, `LICENSE.txt`) |
| Arduino core for the ESP32 3.3.10 | Espressif Systems and contributors | GNU LGPL v2.1 | installed with the Arduino IDE's Boards Manager; github.com/espressif/arduino-esp32 |
| ESP-IDF 5 components (FreeRTOS, lwIP, WiFi, NVS, drivers) | Espressif Systems and contributors | Apache 2.0, MIT and others, per component | linked through the Arduino core; github.com/espressif/esp-idf |

Their source code is in this repository (`firmware/libraries`) or at the addresses above.

**Local change.** arduino-audio-tools carries one change, marked in the file: `src/AudioTools/CoreAudio/AudioMetaData/MetaDataICY.h` accepts ICY metadata blocks up to 4080 bytes instead of 200.

**Note on licence compatibility.** The Free Software Foundation lists the RPSL as incompatible with the GPL. The firmware binary combines GPL-3.0 code (arduino-audio-tools, the libhelix wrapper) with the RPSL-licensed Helix decoder, as the upstream codec-helix library does. If you redistribute the binary, check that this combination is acceptable for your purpose.

## The panel firmware

The front panel's firmware (`AudioDspLabPanel-merged.bin`) contains:

| Work | Author | Licence | Where |
| --- | --- | --- | --- |
| LVGL 9.2.2 | LVGL Kft and contributors | MIT | `firmware/libraries/lvgl` (licence: `LICENCE.txt`); configuration `firmware/libraries/lv_conf.h` |
| Arduino core for the ESP32 3.3.10, ESP-IDF 5 components | Espressif Systems and contributors | LGPL v2.1, Apache 2.0, MIT and others | as for the board's firmware |

**Local change.** The repository's copy of LVGL leaves out the fonts the panel does not use (all but Montserrat 12, 14, 16, 20, 24 and 28) and the ThorVG vector engine (`src/libs/thorvg`); nothing else is changed.

## The application

The Windows application (`AudioDspLab.exe`) is published self-contained and includes:

| Work | Licence |
| --- | --- |
| .NET 10 runtime and WPF | MIT (github.com/dotnet/runtime, github.com/dotnet/wpf) |
| System.IO.Ports | MIT |

## The DSP programs

The SigmaStudio projects were built with Analog Devices SigmaStudio 4.7. Two of them started from Elektor's "Demo1" project for the Audio-DSP-FX board. The ADAU1701 self-boot images in the firmware (`DspImage*.h`) are generated from these projects.

## The board

The Elektor Audio-DSP-FX board (230510) is a design of Elektor. Audio-DSP-Lab is an independent project and is not affiliated with or endorsed by Elektor or Analog Devices. Elektor, SigmaStudio and other names are trademarks of their owners.
