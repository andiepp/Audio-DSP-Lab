# The front panel

A touch front panel for the board: an **ESP32-2432S028R** display board (the "CYD", "cheap yellow display": 2.8″, 320 × 240, resistive touch) on a cable to the board's panel port. With it the board works without a PC: choose an application, switch the output, and set every value. The panel and the PC application can work side by side; what one changes, the other shows.

The panel is new and grows in phases. **Phase 1**, this one: the home screen, each application's parameters as a list, a value editor, the output switch and the pot strip. Phase 2 adds pages made for each application (the equalizer's curve, the radio's stations, the analyzer's plot), phase 3 the synthesizer's pages and a settings screen.

## What you need

- An ESP32-2432S028R. The common version with one micro-USB port has an ILI9341 display, which the firmware expects. The version with two ports (USB-C and micro-USB) has an ST7789: set `DISPLAY_ST7789` to 1 in `firmware/AudioDspLabPanel/Config.h` and build it yourself.
- Three wires, and a 10 kΩ resistor (recommended).
- The board with firmware from this release or later: it has the panel port.

## Wiring

| CYD | | Audio-DSP-FX board |
| --- | --- | --- |
| GPIO22 (TX), on the CN1 or P3 connector | → | **GPIO37** (RX): pin IO37 of the ESP32 kit, not connected on the board |
| GPIO35 (RX), on the P3 connector | ← | **GPIO4** (TX): the pad of R36 ("MIDI out", not fitted), or pin IO4 of the kit |
| GND | — | GND |

Both sides are 3.3 V logic; no level shifter is needed. Put the 10 kΩ resistor between GPIO37 and 3.3 V on the board: GPIO37 has no pull-up of its own, and without one its line floats while the panel is off or restarting.

The panel needs 5 V, from its own USB port or from the board's 5 V if the board's supply is about 9 V or less (the panel draws up to about 250 mA; above 9 V the board's regulator gets hot).

## Flashing

Connect the CYD by USB; it usually shows up as "USB-SERIAL CH340 (COMn)". In the release's `firmware` folder:

```
.\Flash-Panel.cmd COM18
```

(or double-click `Flash-Panel.cmd` and type the port). It writes the panel's single image, `AudioDspLabPanel-merged.bin`, at address 0; the panel keeps no settings.

## Using the panel

The panel finds the board by itself: "Connecting to the board" until the board answers, then the home screen. The dot in the top bar is teal while the link stands and red without it; the CYD's LED shows the same, green or red. If the board restarts or the cable comes loose, the panel connects again on its own.

**Home.** The six applications as keys, the running one in teal: tap one to start it and open its page. Below them **Pot control** switches whether the board's pots set values, and the firmware version and the ESP32 kit are shown.

**An application's page.** Every parameter of the running application, as the board describes it, with its value. A coloured dot marks the parameters a pot plays, in the knob colours (P1 blue, P2 green, P3 yellow, P4 red). **Output** in the top bar switches the output; the house key leads back home. Tap a parameter to change it:

- **Numbers:** **−** and **+** step the value; hold them to repeat, and after a few repeats the steps grow tenfold. **x10** makes the steps ten times larger. The slider covers the whole range, logarithmically for wide ones such as a frequency. **Done** closes the editor.
- **Choices** (waveforms, ranges): a key for each.
- **Switches** change with a tap, without an editor.

The panel sends only the newest value while you hold a key, so it never falls behind. The pot strip at the bottom shows what each pot plays now.

**Side by side with the PC.** Values the PC sets appear on the panel, and the other way round. Other settings (pot control, stations, WiFi networks) the panel reads when it needs them. WiFi networks and station URLs are entered on the PC.

## For developers

- **Source:** `firmware/AudioDspLabPanel`, an Arduino sketch for the ESP32 core 3.3.10, board **ESP32 Dev Module** (`esp32:esp32:esp32`), with **LVGL 9.2.2** from `firmware/libraries` (configured by `firmware/libraries/lv_conf.h`). `Display.cpp` drives the display through LVGL's ILI9341/ST7789 driver and reads the XPT2046 touch controller; `Link.cpp` is the protocol client; `Ui.cpp` the screens.
- **Build:** `arduino-cli compile --fqbn esp32:esp32:esp32 --libraries firmware/libraries firmware/AudioDspLabPanel`.
- **Log:** the CYD's USB port (115200 baud) logs the link's state and every touch: its raw readings and the screen position they map to. The touch calibration, the raw values at the screen's edges, is in `Config.h`; tap the corners and compare if touches land beside their target on your CYD.
- **The display's orientation and colour order** are set in `displayBegin()`: 270° and RGB order suit the tested CYD.
