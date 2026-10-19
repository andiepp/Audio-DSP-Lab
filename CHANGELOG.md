# Changelog

## Unreleased

- `diag heap` reports the ESP32's free heap, its largest free block and the lowest free since the start.
- A failed EEPROM read is tried again after the firmware frees the I2C bus (clocks out a byte a device was left sending, then a STOP); `diag boot` reports the reset reason, the bus lines at the start and the EEPROM reads that failed since.
- Docs: a front panel with its own supply half-powers the board's ESP32 through its cable while the board is unplugged, and the next power-up is not clean (the board silent until EN, or `eeprom-failed` in the SigmaStudio mode). Switch both together.

## 1.1 — front panel and WiFi control

Firmware 1.1, protocol 1 (new requests and events only), application 1.1, panel firmware 1.1.

- **A front panel** on an ESP32-2432S028R ("CYD") display board, on a three-wire cable to the board (docs/Panel.md): the home screen and a settings screen (pot control, power-up, brightness); each application's parameters as tiles with a value editor; the output switch; a knob strip in the knob colours whose knobs open the value their pot plays; the equalizer's curve; station keys with names and the stream title for the radio; Start/Stop, progress, a plot and a measurement cursor for the analyzer; section keys with the filter's and the envelopes' graphs for the synthesizer. It connects and reconnects by itself and follows changes made by the pots, the PC application and scripts live. `Flash-Panel.cmd` flashes it; the release carries its image.
- **WiFi control through the panel:** the panel joins the board's WiFi networks and takes the PC application on TCP port 7000 (`audio-dsp-lab.local`). The application's port list offers "Network: <address>", set up in the WiFi dialog's new Front panel section.
- **The board's panel port:** a second protocol port on UART2 (GPIO37 RX, GPIO4 TX, 115200 baud); a value one port sets reaches the other as an event. New: `wifi secrets` (panel port only), the `panel` request, and the `! wifi` and `! panel` events. The synthesizer's MIDI port no longer claims GPIO4.
- **The manual** has a chapter on the front panel, with screenshots taken from the panel itself (`tools/Capture-PanelScreen.ps1`).

## 1.0 — first public release

Firmware 1.0, protocol 1, application 1.0.

- Six applications for the Elektor Audio-DSP-FX board: generator, equalizer, internet radio, response analyzer, SigmaStudio mode, synthesizer.
- The generator makes its signal on the ESP32: sine, rectangle, triangle, saw, inverse saw and noise, band-limited, with a width for the rectangle and the triangle; its pots follow the other applications (P1 waveform, P2 frequency, P3 width, P4 amplitude).
- A line-based text protocol on the USB serial port (921600 baud) with self-describing parameters and events instead of polling.
- WiFi networks for the radio are entered from the PC and kept on the board; no credentials are compiled into the firmware.
- Windows application with one page per application, the board's pots shown live, stored settings, a protocol console and a built-in board simulator.
- The synthesizer page draws the filter's response and the two envelopes beside their dials, like the equalizer's curve.
- Three themes for the window: Classic, Studio dark and Studio light; the pot knobs in the knob-cap colours P1 blue, P2 green, P3 yellow, P4 red.
- Python client `tools/adl.py`; flash tool and release builder.

Audio-DSP-Lab grew out of the author's Audio-MiniLab, an instrument of a private lab automation system; this release is the standalone version, with its own protocol and PC application.
