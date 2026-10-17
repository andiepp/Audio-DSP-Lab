# Changelog

## 1.0 — first public release

Firmware 1.0, protocol 1, application 1.0.

- Six applications for the Elektor Audio-DSP-FX board: generator, equalizer, internet radio, response analyzer, SigmaStudio mode, synthesizer.
- The generator makes its signal on the ESP32: sine, rectangle, triangle, saw, inverse saw and noise, band-limited, with a width for the rectangle and the triangle; its pots follow the other applications (P1 waveform, P2 frequency, P3 width, P4 amplitude).
- A line-based text protocol on the USB serial port (921600 baud) with self-describing parameters and events instead of polling.
- WiFi networks for the radio are entered from the PC and kept on the board; no credentials are compiled into the firmware.
- Windows application with one page per application, the board's pots shown live, stored settings, a protocol console and a built-in board simulator.
- Three themes for the window: Classic, Studio dark and Studio light; the pot knobs in the knob-cap colours P1 blue, P2 green, P3 yellow, P4 red.
- Python client `tools/adl.py`; flash tool and release builder.

Audio-DSP-Lab grew out of the author's Audio-MiniLab, an instrument of a private lab automation system; this release is the standalone version, with its own protocol and PC application.
