# Changelog

## Unreleased

- A second protocol port for a front panel (an ESP32 CYD display board): UART2 on GPIO37 (RX) and GPIO4 (TX) at 115200 baud. The PC application and the panel can work side by side; a value one sets reaches the other as an event. The synthesizer's MIDI port no longer claims GPIO4.
- A front panel on an ESP32-2432S028R ("CYD") display board: the home screen, each application's parameters as tiles with a value editor, the output switch, and a knob strip in the knob colours whose knobs open the value their pot plays; the equalizer's curve, station keys with names and the stream title for the radio, Start/Stop, progress and a plot for the analyzer's sweep, section keys with the filter's and the envelopes' graphs for the synthesizer, and a settings screen (pot control, power-up, brightness). It connects and reconnects by itself (docs/Panel.md). `Flash-Panel.cmd` flashes it; the release carries its image.

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
