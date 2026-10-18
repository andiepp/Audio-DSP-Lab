# The front panel

A touch front panel for the board: an **ESP32-2432S028R** display board (the "CYD", "cheap yellow display": 2.8″, 320 × 240, resistive touch) on a cable to the board's panel port. With it the board works without a PC: choose an application, switch the output, and set every value. The panel and the PC application can work side by side; what one changes, the other shows.

It has the home screen and a settings screen, each application's parameters as tiles with a value editor, the output switch and the knob strip, and pages made for the applications: the equalizer's curve, the radio's station keys, the analyzer's sweep and plot, and the synthesizer's sections with its filter and envelope graphs.

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

(or double-click `Flash-Panel.cmd` and type the port). It writes the panel's single image, `AudioDspLabPanel-merged.bin`, at address 0. The panel keeps one setting of its own, the brightness; flashing resets it to full.

## Using the panel

The panel finds the board by itself: "Connecting to the board" until the board answers, then the home screen. The dot in the top bar is teal while the link stands and red without it; the CYD's LED shows the same, green or red, and amber while a synthesizer note sounds. If the board restarts or the cable comes loose, the panel connects again on its own.

**Home.** The six applications as keys, the running one in teal: tap one to start it and open its page. Below them **Pot control** switches whether the board's pots set values, and the firmware version and the ESP32 kit are shown.

**Settings** (the gear key on the home screen): **Pot control**; **At power-up: last application**, the board's power-up choice (off: the SigmaStudio mode); **Brightness** of the display; and the panel's and the board's versions and the link. Pot control and the power-up choice are read from the board each time the screen opens, so changes made on the PC show here.

**An application's page.** The running application's parameters as tiles, two to a row, each with its value; a coloured dot marks the ones a pot plays, in the knob colours (P1 blue, P2 green, P3 yellow, P4 red). A page longer than the screen scrolls: drag it up or down; the bar at the right shows where you are. **Output** in the top bar switches the output; the house key leads back home. Tap a tile to change its value:

- **Numbers:** **−** and **+** step the value; hold them to repeat, and after a few repeats the steps grow tenfold. **x10** makes the steps ten times larger. The slider covers the whole range, logarithmically for wide ones such as a frequency. **Done** closes the editor.
- **Choices** (waveforms, ranges): a key for each.
- **Switches** change with a tap, without an editor.

The panel sends only the newest value while you hold a key, so it never falls behind.

**The knob strip** at the bottom shows what each pot plays now. Tap a knob there to change that value on the panel: in the radio, for example, the bass, middle and treble are on P1 to P3 and only a tap away.

**Equalizer.** The response of the three bands, ±15 dB from 20 Hz to 20 kHz, a dot at each band's frequency, computed as the board computes its filters. The gains and the volume are the first tiles; the band frequencies follow when you scroll.

**Radio.** The station playing in large letters, the stream title running below it, and the connection (playing, connecting, no network). Eight keys with the stations' names switch the station; the volume is the one tile below them, the bands are on the pots and the knob strip.

**Response.** The plot of the last sweep, the left channel in teal and the right in blue, from 20 Hz to 20 kHz, the 60 dB below its loudest point. **Start sweep** in the plot's corner starts a sweep from the start to the stop frequency (it switches the output on) and turns into **Stop** while it runs; beside it the points measured so far. The sweep's settings are the tiles below.

**Synth.** Keys above the tiles choose a section: **Osc** (the three oscillators and the noise), **Filter** (cutoff, emphasis, the contour, key tracking), **Env** (the loudness envelope), **LFO** (both LFOs, PWM, the filter's LFO), **Perf** (glide, bend, velocity, volume, the MIDI channel), or **All**. **Filter** shows the filter's response at middle C, solid at the cutoff and dashed where the contour takes it at its peak; **Env** the loudness envelope, solid, and the filter's contour, dashed, from the key down through a held stretch to the end of the release.

**Side by side with the PC.** Values the PC sets appear on the panel, and the other way round. Other settings (pot control, stations, WiFi networks) the panel reads when it needs them. WiFi networks and station URLs are entered on the PC.

**WiFi control.** The panel also lets the PC application work over WiFi, so the board needs no USB cable. It joins the networks the board keeps for its radio (network 1 first, then network 2) and takes the application on TCP port 7000, as **audio-dsp-lab.local** or at its address on your network. To set it up, open **WiFi networks …** in the PC application while it is connected by USB: the **Front panel** section shows whether the panel is online and at which address; **Use for connecting** puts **Network: <address>** in the port list. From then on choose that entry under **Board on** and **Connect**. The panel stays usable meanwhile; its LED turns blue while a PC works through it. Changing the networks in the dialog makes the panel join again.

Anyone on your network can reach the board this way, as anyone with the USB cable can; the WiFi passwords themselves never leave the board over the network.

## For developers

- **Source:** `firmware/AudioDspLabPanel`, an Arduino sketch for the ESP32 core 3.3.10, board **ESP32 Dev Module** (`esp32:esp32:esp32`) with the partition scheme **Huge APP** (`PartitionScheme=huge_app`: with WiFi the firmware outgrows the default 1.3 MB), with **LVGL 9.2.2** from `firmware/libraries` (configured by `firmware/libraries/lv_conf.h`). `Display.cpp` drives the display through LVGL's ILI9341/ST7789 driver and reads the XPT2046 touch controller; `Link.cpp` is the protocol client; `Ui.cpp` the screens.
- **Build:** `arduino-cli compile --fqbn esp32:esp32:esp32:PartitionScheme=huge_app --libraries firmware/libraries firmware/AudioDspLabPanel`.
- **Network:** `Wireless.cpp` joins the WiFi and serves TCP port 7000; `Link.cpp` shares the cable between the PC and the screens. LVGL takes its memory from the C heap (`lv_conf.h`): a static pool and WiFi do not fit the ESP32's DRAM together.
- **Log:** the CYD's USB port (115200 baud) logs the link's state and every touch: its raw readings and the screen position they map to. The touch calibration, the raw values at the screen's edges, is in `Config.h`; tap the corners and compare if touches land beside their target on your CYD.
- **The display's orientation and colour order** are set in `displayBegin()`: 270° and RGB order suit the tested CYD.
- **Commands and screenshots:** the USB port takes a command per line: `page home|app|settings`, `section osc|filter|env|lfo|perf|all`, `edit <parameter>`, `close`, and `shot`, which draws the screen again and sends its pixels. `tools/Capture-PanelScreen.ps1 -Port COM18 -Path screen.png -Command 'page settings'` turns them into a PNG (enlarged twice, at 192 dpi); the manual's pictures of the panel are made so.
- **Text:** LVGL's fonts carry ASCII only, so the panel spells accented letters from the board (stream titles, station names) without accents: "ö" as "oe", "é" as "e".
