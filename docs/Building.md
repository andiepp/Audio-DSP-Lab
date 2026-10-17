# Building from source

This guide is for developers: building the firmware and the application, running the tests, making a release, and finding your way in the code.

## Repository layout

| Path | Content |
| --- | --- |
| `firmware/AudioDspLab/` | The ESP32 firmware (Arduino sketch) |
| `firmware/libraries/` | The libraries the firmware needs, as used: arduino-audio-tools (with one local fix) and libhelix |
| `firmware/MidiMonitor/` | A separate diagnostic sketch: prints what arrives on the MIDI input |
| `app/` | The Windows application (.NET 10, WPF), its core library and tests |
| `sigmastudio/` | The DSP programs as SigmaStudio projects, with their recipes |
| `tools/` | Flash tool, release builder, Python client, image converter, PDF builder |
| `docs/` | Manual, installation guide, protocol, this guide |

## The firmware

### Toolchain

- **Arduino IDE 2.3** (or arduino-cli 1.x)
- **ESP32 by Espressif Systems**, version **3.3.10** (Boards Manager). Other 3.3.x versions should work; 2.x does not.
- Board **ESP32 PICO-D4** (`esp32:esp32:pico32`; the same build runs on the PICO-V3 of the ESP32-PICO-KIT-1), partition scheme **Minimal SPIFFS (1.9MB APP with OTA/190KB SPIFFS)**. The default scheme is too small for WiFi and the MP3 decoder, and the firmware keeps its copy of your SigmaStudio program in the SPIFFS partition.
- The libraries in `firmware/libraries`.

### The libraries

The firmware needs **arduino-audio-tools** (version 1.1.0, from github.com/pschatzmann/arduino-audio-tools) and **libhelix** (codec-helix 0.9.4, github.com/pschatzmann/arduino-libhelix), both GPL-3.0. The repository carries the files they need, because arduino-audio-tools has one local fix: in `src/AudioTools/CoreAudio/AudioMetaData/MetaDataICY.h` the metadata limit is 4080 bytes instead of 200 (ICY allows 255 × 16; some stations, such as Classic FM, send blocks longer than 200 bytes, and the old limit corrupts the stream). Re-apply it if you update the library.

**Arduino IDE:** either copy the two folders from `firmware/libraries` into your sketchbook's `libraries` folder, or set the sketchbook location (File → Preferences) to the repository's `firmware` folder.

**arduino-cli:** name the folder with `--libraries`:

```
arduino-cli compile --fqbn esp32:esp32:pico32:PartitionScheme=min_spiffs --libraries firmware/libraries firmware/AudioDspLab
arduino-cli upload  --fqbn esp32:esp32:pico32:PartitionScheme=min_spiffs -p COM5 firmware/AudioDspLab
```

The build takes a few minutes the first time and uses about 61 % of the application partition. Do not add the Elektor_AudioDSP library: its macros clash with arduino-audio-tools, and the firmware has its own DSP, EEPROM and clock drivers.

### Structure

| File | Role |
| --- | --- |
| `AudioDspLab.ino` | `setup()` and `loop()`: drivers up, the applications, the protocol |
| `Config.h` | Pins, versions, protocol settings, the DSP's rates |
| `Board.cpp/.h` | Which ESP32 kit is fitted (PICO-D4 or PICO-V3) and the pin that differs |
| `Protocol.cpp/.h` | The serial protocol: line reader, commands, values, events (docs/Protocol.md) |
| `ParamInfo.cpp/.h` | The protocol's names, kinds and units for every application parameter |
| `Applications.cpp/.h` | The applications: DSP image, parameter table (ranges in scaled integers, what a change does), pot mapping; starting, service mode, power-up |
| `Dsp.cpp/.h` | ADAU1701 over I2C: reset, boot, safeload, readback |
| `Eeprom24.cpp/.h` | The 24C256 EEPROM: compare, write, verify |
| `Mclk.cpp/.h` | The DSP's master clock from the ESP32's I2S peripheral |
| `Pots.cpp/.h` | The four pots, smoothed |
| `DacMux.cpp/.h` | SigmaStudio mode: the pots to the DSP's auxiliary ADC through the ESP32's DACs and the 74HC4053 |
| `StudioBackup.cpp/.h` | The copy of your SigmaStudio program in the SPIFFS partition |
| `Radio.cpp/.h` | Internet radio: WiFi, stream, MP3 decoder, I2S, stations and networks in NVS |
| `Analyzer.cpp/.h` | Response analyzer: sweep, readback, reference |
| `Synth.cpp/.h` | Synthesizer: MIDI, oscillators, filter, envelopes, LFOs, the audio task |
| `DspImage*.h` | The SigmaStudio self-boot images, generated |

The loop alone talks to the DSP and the serial port; the radio and the synthesizer run their audio in FreeRTOS tasks on core 0.

### Adding a parameter

1. Add it to the application's table in `Applications.cpp` (id, range and default in scaled integers, the function that applies it) and, if a pot plays it, to the pot table.
2. Add its name, kind, decimals and unit to `ParamInfo.cpp`.
3. Add it to the simulator's table, `app/src/AudioDspLab.Core/Simulation/SimulatedParameters.cs`. The test `FirmwareTableTests` compares the two and fails until they agree.
4. Show it in the application: a `ParameterViewModel` on the application's page, bound in `MainWindow.xaml`.

### Partition and NVS

The firmware stores in NVS: `adl-wifi` (networks), `aml-radio` (stations), `aml-startup` (power-up), `aml-ref` (analyzer reference). The copy of your SigmaStudio program lives in the raw `spiffs` partition. Flashing with the same partition scheme keeps all of it.

## The application

### Toolchain

- **.NET 10 SDK** (Windows; WPF)
- Any editor; Visual Studio 2026 or Rider open `app/AudioDspLab.slnx`.

```
cd app
dotnet build AudioDspLab.slnx
dotnet test AudioDspLab.slnx
dotnet run --project src/AudioDspLab -- --simulator
```

`--simulator` connects to the simulated board at once.

### Structure

| Project | Role |
| --- | --- |
| `src/AudioDspLab.Core` | Protocol text, line transport (serial port), connection with request/response matching and resynchronisation, `BoardClient` (every command as a method, every event as a .NET event), the model, and `SimulatedBoard` |
| `src/AudioDspLab` | The WPF application: `MainViewModel`, one page view model per application, generic `ParameterViewModel`, the controls (digit dial, displays, pot knob, response curve, sweep chart), stored settings |
| `tests/AudioDspLab.Core.Tests` | Protocol text, the client against the simulator, and the firmware/simulator table comparison |
| `tools/Screenshots` | Renders the manual's screenshots from the simulator: `dotnet run --project app/tools/Screenshots` writes `docs/images/screenshots` |

The application binds each page to parameters by their protocol names; ranges, units and pots come from the board's `params`. Writes to one parameter are coalesced (a spinning dial sends only the newest value once the previous write is answered), and the board's events update the pages without polling.

### The simulator

`SimulatedBoard` answers the protocol as firmware 1.0 does, with made-up measurements: a 5 kHz low-pass as the device under test, a radio that "plays" when a network is set, an arpeggio on the synthesizer in demo mode. The tests run against it, and the application offers it as **Simulator (demo)**.

## A release

`tools/Build-Release.ps1` builds everything in one go (Windows PowerShell; needs arduino-cli with the ESP32 core 3.3.10, the .NET 10 SDK and optionally Python with reportlab for the PDFs):

```
powershell -ExecutionPolicy Bypass -File tools\Build-Release.ps1
```

It writes to `dist/`:

- `Audio-DSP-Lab-<version>-windows-x64.zip`: `AudioDspLab.exe` (self-contained, single file), `firmware/` (the binaries, the flash tool), `docs/` (manual and installation guide as PDF and Markdown), licence and notices
- `Audio-DSP-Lab-firmware-<version>.zip`: the firmware alone

The version comes from `FIRMWARE_VERSION_MAJOR/MINOR` in `Config.h`. Attach both zips to a GitHub release with the tag `v<version>`.

### Firmware images

| File | Offset | Content |
| --- | --- | --- |
| `AudioDspLab.bootloader.bin` | 0x1000 | ESP-IDF second-stage boot loader |
| `AudioDspLab.partitions.bin` | 0x8000 | Partition table (Minimal SPIFFS) |
| `boot_app0.bin` | 0xE000 | OTA data: boot the first application partition |
| `AudioDspLab.bin` | 0x10000 | The firmware |
| `AudioDspLab-merged.bin` | 0x0 | All of the above in one 4 MB image (overwrites NVS and SPIFFS) |

## Code style

- Firmware: the existing files' layout (banner headers, a comment per decision that is not obvious from the code), no warnings with `--warnings all`.
- C#: nullable enabled, no warnings, `ConfigureAwait(true)` in view models.
- Every protocol change goes into `docs/Protocol.md`, the firmware, the simulator and `BoardClient` together; the protocol's version (`PROTOCOL_VERSION`, `BoardInfo.SupportedProtocol`) changes when an old application could misunderstand the new firmware.
