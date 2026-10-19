# Building from source

This guide is for developers: building the firmware and the application, running the tests, making a release, and finding your way in the code.

## Repository layout

| Path | Content |
| --- | --- |
| `firmware/AudioDspLab/` | The ESP32 firmware (Arduino sketch) |
| `firmware/AudioDspLabPanel/` | The front panel's firmware for the ESP32-2432S028R display board ([Panel.md](Panel.md)) |
| `firmware/libraries/` | The libraries the firmwares need, as used: arduino-audio-tools (with one local fix), libhelix, and LVGL with its configuration `lv_conf.h` |
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

The build takes a few minutes the first time and uses about 63 % of the application partition. Do not add the Elektor_AudioDSP library: its macros clash with arduino-audio-tools, and the firmware has its own DSP, EEPROM and clock drivers.

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
| `Mclk.cpp/.h` | The DSP's master clock from the ESP32's I2S peripheral (see [The DSP's clock](#the-dsps-clock)) |
| `Pots.cpp/.h` | The four pots, smoothed |
| `DacMux.cpp/.h` | SigmaStudio mode: the pots to the DSP's auxiliary ADC through the ESP32's DACs and the 74HC4053 |
| `StudioBackup.cpp/.h` | The copy of your SigmaStudio program in the SPIFFS partition |
| `Radio.cpp/.h` | Internet radio: WiFi, stream, MP3 decoder, I2S, stations and networks in NVS |
| `Analyzer.cpp/.h` | Analysis, Frequency Response: sweep, readback, reference |
| `Distortion.cpp/.h` | Analysis, THD and Spectrum: capture, window, FFT (esp-dsp), THD, THD+N, harmonics, RMS, peaks, spectrum, in a task of its own |
| `Capture.cpp/.h` | The DSP's serial output on the ESP32's second I2S port; `diag capture`, `diag record`, `diag clocks` |
| `Synth.cpp/.h` | Synthesizer: MIDI, oscillators, filter, envelopes, LFOs, the audio task |
| `DspImage*.h` | The SigmaStudio self-boot images, generated |

The loop alone talks to the DSP and the serial port; the radio and the synthesizer run their audio in FreeRTOS tasks on core 0, and THD and Spectrum their capture and FFT in a task of their own (the serial output and its memory only).

### The DSP's clock

The ESP32 makes the DSP's 12.288 MHz MCLK on GPIO0 with an I2S port clocked by its audio PLL (APLL). On the ESP32 that pin carries the APLL itself, and the I2S driver sets the APLL to twice the port's own MCLK, its divider cannot go below 2. So `Mclk.cpp`, `AudioOut.cpp` and the radio ask for `DSP_MCLK_I2S_MULTIPLE` = 128 × fs to put 256 × fs on the pin. With 256 the DSP ran on 24.576 MHz, twice its rating, and its converters made sporadic errors. `diag clocks` shows the result without an oscilloscope: the DSP's serial output must run at about 48000 Hz. Do not probe GPIO0 with the pulse counter: claiming the pin as an input takes it off the clock output.

### Memory

THD and Spectrum need about 82 KB of heap while they run, in one block of 32 KB and three of 16 KB; after the radio has played, the heap offers about 193 KB in smaller pieces (two blocks of 32 KB, one of 16 KB). `diag heap` and `diag blocks` show what is there.

### Adding a parameter

1. Add it to the application's table in `Applications.cpp` (id, range and default in scaled integers, the function that applies it) and, if a pot plays it, to the pot table.
2. Add its name, kind, decimals and unit to `ParamInfo.cpp`.
3. Add it to the simulator's table, `app/src/AudioDspLab.Core/Simulation/SimulatedParameters.cs`. The test `FirmwareTableTests` compares the two and fails until they agree.
4. Show it in the application: a `ParameterViewModel` on the application's page, bound in `MainWindow.xaml`.

### The panel firmware

`firmware/AudioDspLabPanel` builds for the board **ESP32 Dev Module** with the partition scheme **Huge APP** (`esp32:esp32:esp32:PartitionScheme=huge_app`; with WiFi it outgrows the default 1.3 MB) with **LVGL 9.2.2** from `firmware/libraries/lvgl`, configured by `firmware/libraries/lv_conf.h` (LVGL finds it as `../../lv_conf.h` from its `src` folder). The repository carries LVGL's sources without the fonts the panel does not use (it keeps Montserrat 12 to 28) and without its ThorVG vector engine.

```
arduino-cli compile --fqbn esp32:esp32:esp32:PartitionScheme=huge_app --libraries firmware/libraries firmware/AudioDspLabPanel
arduino-cli upload  --fqbn esp32:esp32:esp32:PartitionScheme=huge_app -p COM18 firmware/AudioDspLabPanel
```

It uses about 43 % of the 3 MB application partition; LVGL takes its memory from the C heap, as WiFi needs the DRAM a static pool would take. LVGL 9.2's `lvgl.h` leaves its MIPI display drivers out, so `Display.cpp` includes the ILI9341 and ST7789 headers itself; LVGL's draw buffers must be 4-byte aligned. The panel speaks the protocol of [Protocol.md](Protocol.md) on its UART2 (RX GPIO35, TX GPIO22, 115200) to the board's panel port.

### Partition and NVS

The firmware stores in NVS: `adl-wifi` (networks), `aml-radio` (stations), `aml-startup` (power-up), `aml-ref` (the Frequency Response reference). The copy of your SigmaStudio program lives in the raw `spiffs` partition. Flashing with the same partition scheme keeps all of it.

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
| `src/AudioDspLab` | The WPF application: `MainViewModel`, one page view model per application, generic `ParameterViewModel`, the controls (digit dial, displays, pot knob, response curve, sweep chart, spectrum chart), stored settings, the themes |
| `tests/AudioDspLab.Core.Tests` | Protocol text, the client against the simulator, and the firmware/simulator table comparison |
| `tools/Screenshots` | Renders the manual's screenshots from the simulator in every theme: `dotnet run --project app/tools/Screenshots` writes Studio dark, the manual's theme, to `docs/images/screenshots` and Classic and Studio light to its `classic` and `studio-light` folders |

The application binds each page to parameters by their protocol names; ranges, units and pots come from the board's `params`. Writes to one parameter are coalesced (a spinning dial sends only the newest value once the previous write is answered), and the board's events update the pages without polling.

### Themes

`src/AudioDspLab/Themes` holds one colour dictionary per theme, `Classic.xaml`, `StudioDark.xaml` and `StudioLight.xaml`, all with the same keys; the Studio themes share their styles in `StudioStyles.xaml`, Classic keeps its styles in its own file. `ThemeResources` loads a theme into the application's resources and gives the controls that draw themselves (pot knob, digit dial, charts) their colours. Switching the theme rebuilds the main window over the same view model, so the connection stays. A new theme is one more colour dictionary with the same keys and its name in `ThemeResources.Names`; the pot colours are `AudioLabPot1` to `AudioLabPot4`.

### The simulator

`SimulatedBoard` answers the protocol as firmware 1.1 does, with made-up measurements: a 5 kHz low-pass as the device under test, harmonics, mains hum and noise for THD and Spectrum, a radio that "plays" when a network is set, an arpeggio on the synthesizer in demo mode. The tests run against it, and the application offers it as **Simulator (demo)**.

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
