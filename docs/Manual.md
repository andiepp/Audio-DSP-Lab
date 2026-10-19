# Manual

As of 2026-10-18.

Audio-DSP-Lab turns the Elektor Audio-DSP-FX board into six audio instruments that you run from a Windows PC over USB: a signal generator, an equalizer, an internet radio, a frequency response analyzer, a SigmaStudio mode for your own DSP programs, and a Minimoog-style synthesizer. This manual covers firmware 1.1 and application 1.1, and the optional front panel (panel firmware 1.1).

![Audio-DSP-Lab: the PC sets values over USB, the board does the audio](images/overview.png)

## At a glance

Audio-DSP-Lab has two parts:

- **The firmware** runs on the board's ESP32. It writes each application's DSP program into the ADAU1701's EEPROM, computes filter coefficients, reads the pots and MIDI, streams the radio and makes the generator's signal and the synthesizer's sound. It talks to the PC over the board's USB port with a simple text protocol.
- **The application** (`AudioDspLab.exe`) is the PC's front panel. It shows one page per application, sets every value, and shows what the board reports, including the changes you make with the board's pots.
- **The front panel** (optional) is a small touch display on a cable to the board, an ESP32 "CYD": it runs the applications without a PC. See [The front panel](#the-front-panel).

The board keeps working without the PC: once an application runs, you can close the window and keep playing. The board can also start an application by itself at power-up.

| Application | What it does | Audio |
| --- | --- | --- |
| Generator | Sine, rectangle, triangle, saw, inverse saw or noise, 20 Hz to 20 kHz, with a width for rectangle and triangle | out K3, K4 |
| Equalizer | Bass, middle, treble and volume on the line input | in K2, out K3, K4 |
| Radio | Eight MP3 internet stations over WiFi, through the equalizer | out K3, K4 |
| Response | Sweeps a sine through a device under test and plots its response | out K3, K4, in K2 |
| SigmaStudio | Runs your own SigmaStudio program from the EEPROM | as your program |
| Synthesizer | A monophonic synthesizer played from a MIDI keyboard | MIDI in K5, out K3, K4 |

Before you start, the board needs the firmware and two jumper settings: see the [installation guide](Installation.md).

## The board

| Part | Function | Notes |
| --- | --- | --- |
| K2 | Line input, left ADC0, right ADC1 | About 22.5 kΩ input impedance |
| K3 | Output DAC0 / DAC1 | Every application's output; about 0.9 Vrms at full scale |
| K4 | Output DAC2 / DAC3 | The same signal as K3 |
| K5 | MIDI input, 3.5 mm TRS type A | DIN pin 4 to ring, pin 5 to tip, pin 2 to sleeve |
| P1 to P4 | Potentiometers | What each sets depends on the application (see [The pots](#the-pots)) |
| USB | Serial port of the ESP32 kit (Silicon Labs CP210x) | The application's connection |
| I2C header | SigmaStudio's USBi | Only for SigmaStudio mode and service mode |
| JP1 | DSP clock | On **ext. MCLK**: the ESP32 supplies the clock |
| JP2 | DSP boot | On **Selfboot** |

The DSP samples at 48 kHz; its core runs twice per sample (96 kHz), and every frequency the firmware computes is computed for that rate. Levels are given in dBFS: 0 dBFS is the DSP's full scale.

## Getting started

1. Connect the board's USB socket (on the ESP32 kit) to the PC.
2. Start **AudioDspLab.exe**.
3. Choose the board's COM port under **Board on** and press **Connect**. The application remembers the port and connects to it by itself next time.
4. Pick an application under **Application**. The badge at the right of that row shows its state: `loading`, then `running`.
5. Set the values, then press **Output on**. Every application starts with its output off.

At power-up the board starts in SigmaStudio mode, unless you set **Power-up** to **Last application** (see [Power-up](#power-up)).

No board at hand? Choose **Simulator (demo)** as the port. The simulator behaves like a board with made-up measurements, so you can try every page.

![The window: application keys, the page of the running application, the pots, and the board's settings on the right](images/screenshots/generator.png)

### The window

| Area | What it shows or does |
| --- | --- |
| Board on (top) | The port, **Connect** and **Disconnect**, the firmware found; **Theme** chooses the look, **Console** talks the protocol by hand, **Manual** opens this document |
| Application | One key per application, and the state badge |
| The page | The running application's values: displays, dials, keys, its **Output** key |
| Potentiometers | P1 to P4, where each stands and what it drives now, and the **Pot control** key |
| Board (right) | Firmware, application, the WiFi networks |
| Power-up | What the board starts when it is switched on |
| Service | Hands the DSP to SigmaStudio and the USBi |
| Settings | Stored settings: save, apply, delete |
| Status line (bottom) | The last action and its result; the dot is green while connected, red after an error |

**Digit dials.** Every numeric value is a digit dial: turn the mouse wheel over a digit, or click its upper or lower half, to step that digit. The board answers every change with the value it took, and the dial shows that value.

**Pot badges.** A badge such as **P2** lights on a display for a moment when the board's pot changed that value, not the window: gold in the Classic theme, in the pot's colour in the Studio themes.

**Themes.** **Theme** in the top bar switches between three looks: **Classic** (coloured blocks on black), **Studio dark** and **Studio light** (quiet cards, one teal accent for what is on or selected). The window rebuilds in the new look at once; the connection, the running application and its values stay as they are, and the choice is remembered for the next start.

**Knob colours.** The pot knobs wear the colours of colour-coded knob caps: **P1 blue, P2 green, P3 yellow, P4 red**; the coloured cap points where the pot stands. The Studio themes add a ring in the pot's colour that fills with the pot's travel. Caps in these colours on the board's pots make the two easy to match.

![The generator page in the Classic theme](images/screenshots/classic/generator.png)

## The applications

Selecting an application writes its DSP program into the EEPROM (about 0.2 s; nothing is written if the program is already there) and boots the DSP. The output is off after every start.

### Generator

A test signal on K3 and K4. The ESP32 makes it, as it makes the synthesizer's sound, and the DSP sets its amplitude.

| Control | Range | Default | Pot |
| --- | --- | --- | --- |
| Waveform | Sine, rectangle, triangle, saw, inverse saw, noise | Sine | P1, six zones |
| Width | 5 to 95 % | 50 % | P3 |
| Frequency | 20 Hz to 20 kHz, 0.01 Hz steps | 1 kHz | P2, logarithmic within the P2 range |
| P2 range | 20–200 Hz, 200 Hz–2 kHz, 2–20 kHz | 200 Hz–2 kHz | – |
| Amplitude | −60 to 0 dBFS | −20 dBFS | P4 |
| Output | Off, on | Off | – |

**Width** shapes the rectangle and the triangle, as the synthesizer's width and symmetry do: the rectangle's high part of the cycle (50 % is a square, 10 % a narrow pulse) and the triangle's rising part (50 % is symmetric; toward 95 % it leans into a saw, toward 5 % into an inverse saw). The saw rises and drops; the inverse saw drops and rises. The other waveforms ignore the width.

Every waveform peaks at the amplitude set; a narrow pulse is scaled so that it does. Rectangle, triangle and the saws are band-limited, so they do not alias at high frequencies; close to 20 kHz all of them come near a sine, because their harmonics lie above the audio band.

Type a frequency into the **F** box (a trailing `k` means kilohertz) and press Enter, or use the keys beside it. Waveform changes fade out and in (5 ms each) and amplitude changes glide, so neither clicks. The line under the amplitude gives the approximate RMS voltage of a sine, from Elektor's 0.9 Vrms at full scale.

### Equalizer

The line input K2 through bass, middle and treble to a volume, the same stereo signal on K3 and K4. The curve at the top is computed from the three filters, not measured.

![The equalizer page](images/screenshots/equalizer.png)

| Control | Range | Default | Pot |
| --- | --- | --- | --- |
| Bass gain / corner | ±12 dB / 50–400 Hz (low shelf) | 0 dB / 200 Hz | P1 (gain) |
| Middle gain / centre | ±12 dB / 300 Hz–5 kHz (peak, Q 0.7) | 0 dB / 1 kHz | P2 (gain) |
| Treble gain / corner | ±12 dB / 500 Hz–8 kHz (high shelf) | 0 dB / 2 kHz | P3 (gain) |
| Volume | −60 to 0 dB | −20 dB | P4 |
| Output | Off, on | Off | – |

### Radio

MP3 internet radio over WiFi, decoded on the ESP32 and played through the equalizer's bands and volume, so the equalizer page and its pots work as above. The radio strip above them shows the station, the stream title, the connection and the stream's sample rate.

![The radio strip above the equalizer](images/screenshots/radio.png)

**WiFi first.** The radio needs a WiFi network. Press **WiFi** (in the radio strip, or **WiFi networks …** on the right), enter the network's name and password and press **Save**. The board keeps up to two networks and tries the first one first. The password goes to the board once and is never shown again. The ESP32 only uses 2.4 GHz networks.

The default stations:

| Key | Station | Key | Station |
| --- | --- | --- | --- |
| 0 | Deutschlandfunk | 4 | WDR 2 |
| 1 | Classic FM UK | 5 | Rock Antenne |
| 2 | SWR3 | 6 | Oldies (181.FM Super 70s) |
| 3 | NDR 2 | 7 | SWR1 |

**Your own stations.** **Edit stations** opens the eight stations' names and URLs. Change a row and press its **Save**: the board keeps the station across power cycles and firmware updates. **Default** gives one row its default back, **Restore all defaults** all of them.

The radio plays MP3 streams over plain HTTP. Streams that are only offered over `https://`, AAC streams and playlist URLs (`.m3u`, `.pls`) do not play. Many stations list a plain `http://` MP3 address on their website or in directories such as radio-browser.info.

| Connection | Meaning |
| --- | --- |
| WiFi … | Connecting to a network |
| Stream … | Opening the station's stream |
| Playing | Sound; the rate shows beside it |
| No network | No network set, or neither network answers (retried every 30 s) |
| Stream error | The station did not deliver (retried every 5 s) |

### Response

Measures a device's frequency response: a sine leaves on K3/K4, passes the device under test and returns on K2, and the board reads the level of both channels at each frequency.

1. Connect K3 straight to K2 with a cable, run a sweep, tick **Replace the reference** and press **Keep as reference**.
2. Put the device between K3 (or K4) and K2 and press **Start sweep**. The curve grows point by point.
3. Choose **Relative to reference** to see the device's own response, without the board's.

![The response page: a sweep of a low-pass](images/screenshots/analyzer.png)

| Control | Range | Default |
| --- | --- | --- |
| Output level | −60 to 0 dBFS (P2) | −20 dBFS |
| Sweep start / stop | 20 Hz to 20 kHz | 20 Hz / 20 kHz |
| Points per decade | 1 to 40 (up to 121 points over 20 Hz to 20 kHz) | 10 |
| Tone between sweeps | 20 Hz to 20 kHz | 1 kHz |

A full sweep at 10 points per decade takes about 10 s; the line under the points per decade gives the estimate. Between sweeps the board plays the tone and the window shows the input levels twice a second. The reference is kept in the board and survives power cycles. Mind the common ground between K3 and K2; the measurement's floor is about −110 dBFS.

### SigmaStudio mode

The DSP runs whatever SigmaStudio last wrote to its EEPROM. The firmware supplies the 48 kHz clock and stays off the I2C bus, so you can download programs and write the EEPROM with the USBi at any time. The pots are always passed to the DSP's auxiliary ADC: P1 to ADC0, P2 to ADC1, P3 to ADC2, P4 to ADC3, every 20 ms.

![The SigmaStudio page: the pots on their way to the DSP's ADC](images/screenshots/sigmastudio.png)

Selecting another application writes its program over yours, but the firmware first keeps a copy of yours in its flash and writes it back when you return to this mode. **Unplug the USBi before you switch.** Confirm every EEPROM write in SigmaStudio. How to build your own programs for the board is described in [SigmaStudio.md](SigmaStudio.md).

### Synthesizer

See the chapter [The synthesizer](#the-synthesizer) below.

## Pots, stored settings, power-up and service mode

### The pots

The pots set values only while **Pot control** is on, and it is off at every power-up. SigmaStudio mode is the exception: there the pots always go to the DSP's auxiliary ADC.

| Application | P1 | P2 | P3 | P4 |
| --- | --- | --- | --- | --- |
| Generator | Waveform | Frequency | Width | Amplitude |
| Equalizer, Radio | Bass gain | Middle gain | Treble gain | Volume |
| Response | – | Output level | – | – |
| SigmaStudio | ADC0 | ADC1 | ADC2 | ADC3 |
| Synthesizer | Oscillator 1 width | Cutoff | Emphasis | Volume |

- Switching Pot control on takes the pots' positions at once: every value a pot drives goes to where its pot stands, and the window shows it.
- After the window writes a value, its pot takes the value back only once you move it by about 2 % of its travel: a pot that merely sits there never overwrites the window's value.
- Once a pot has taken over it follows with a small backlash, so its noise does not make the value flicker; both ends stay reachable.
- With Pot control off, every start uses the defaults. With it on, every start takes the pots' positions.

### Stored settings

The Settings block saves the running application and its values under a name, and applies them again later: double-click a name or press **Apply**. Applying selects the stored application if another runs, and never switches an output on. The radio's stations and WiFi networks are not part of a set: they belong to the board.

Sets are kept on the PC in `%LocalAppData%\Audio-DSP-Lab\Settings`, one text file each (`name=value` lines, the protocol's own words). Copy a file to take a set to another PC.

### Power-up

The **Power-up** key chooses what the board starts when it is switched on. The choice is kept in the board.

| Key shows | At power-up the board starts |
| --- | --- |
| **SigmaStudio mode** (the default) | SigmaStudio mode, whatever ran before |
| **Last application** | The application you selected last, with its defaults |

- With **Last application**, the radio and the synthesizer come up sounding: their output is switched on, at the default volume of −20 dB. The generator and the response analyzer stay off, since they may drive a device under test.
- Service mode is never restored, and Pot control is off at every power-up.
- Before another application's program replaces yours at power-up, the firmware keeps a copy of your SigmaStudio program. If the chosen application cannot start, the board starts SigmaStudio mode instead and the window shows the error.
- **Last application** lets the board play the radio, or wait for the keyboard, as soon as it is powered, with no PC at all: a USB power supply is enough.

### Service mode

Service mode hands the DSP to SigmaStudio while an application's program runs: the firmware releases the EEPROM's write protection and leaves the I2C bus, so SigmaStudio and the USBi can download and tune the application's own SigmaStudio project.

1. Tick **Hand the DSP to SigmaStudio** and press **Enter service**.
2. Work in SigmaStudio with the USBi on the I2C header.
3. **Unplug the USBi**, then press **Leave service**. The firmware writes the application's program back and restarts it; whatever SigmaStudio wrote to the EEPROM meanwhile is replaced.

While the USBi sits on the bus, every EEPROM write of the firmware fails (`eeprom-failed`), even with SigmaStudio closed. For your own programs use SigmaStudio mode instead: it needs no service mode.

## The synthesizer

The synthesizer is one monophonic voice in the spirit of the Minimoog Model D, computed on the ESP32 at 48 kHz and played from any MIDI keyboard on the MIDI input K5. Every default leaves a plain, bright sawtooth: you add what you want from there.

![The synthesizer page](images/screenshots/synthesizer.png)

### The MIDI keyboard

The synthesizer listens to plain MIDI, so any keyboard with a 5-pin DIN MIDI Out can play it.

| The keyboard's control | MIDI message | What it does here | Needed |
| --- | --- | --- | --- |
| Keys | Note on, note off | Plays the voice; the last key held wins | Yes |
| Key velocity | Velocity of the note on | Loudness and brightness, as far as set | No |
| Pitch bend wheel | Pitch bend | Bends all oscillators by up to the bend range | No |
| Mod wheel | Controller 1 | Vibrato, and LFO 1 on the filter | No |

- **The cable:** every standard MIDI Out uses the same pins of the 5-pin DIN socket: pin 4 the current source, pin 5 the data line, pin 2 the shield. The cable to K5 takes pin 4 to the ring, pin 5 to the tip and pin 2 to the sleeve of the 3.5 mm plug (TRS type A).
- **The channel:** the keyboard must send on the channel set as **MIDI channel**, 15 by default.
- **Other outputs:** a keyboard with only USB MIDI needs a USB MIDI host interface with a DIN MIDI Out. A keyboard with a 3.5 mm MIDI Out of type A can use a type A TRS cable straight to K5; a type B output needs a B-to-A adapter.

### Setting up

1. Connect the keyboard's **5-pin DIN MIDI Out** to K5 with the cable described above. Some keyboards (the Arturia KeyStep 37, for instance) also have 3.5 mm jacks for sync and CV: those are not MIDI.
2. Set the keyboard to send on MIDI channel 15, or set **MIDI channel** to the keyboard's channel.
3. Select **Synth**, set **Volume** (about −20 dB to start) and press **Output on**.
4. Play. The **Note** display shows the note sounding (60 = C4), and the LFO header shows the mod wheel's position.

A key sounds within a few milliseconds: the audio path holds about 5 ms.

### How the voice works

The sound runs through the same stages as on a Minimoog: oscillators and noise into a mixer, a 24 dB/octave ladder low-pass filter shaped by its own envelope (the contour), and an amplifier shaped by the loudness envelope. Two LFOs and the keyboard's pitch bend, mod wheel and velocity modulate it; the DSP sets the final volume.

![Synthesizer voice: signal flow and modulation](images/synthesizer-voice.png)

The filter is where most of the character comes from: its cutoff is moved by P2, the contour, the keyboard, velocity and LFO 1 at once.

### Oscillators

| Control | Range | Default | Notes |
| --- | --- | --- | --- |
| Waveform (each) | Sine, saw, rectangle, triangle | Saw | Chosen with the keys |
| Octave (each) | −2 to +2 | 0 | |
| Detune (oscillators 2, 3) | ±700 cents | 0 | Relative to oscillator 1; a few cents give the classic beating |
| Level (each) | 0 to 100 % | 100, 0, 0 | Oscillators 2 and 3 are silent until you raise them |
| Width (each, rectangle) | 5 to 95 % | 50 % | The high part of the cycle: 50 % is a square, 25 % a narrow pulse. P1 plays oscillator 1's |
| Rise (each, triangle) | 10 to 90 % | 50 % | The rising part of the cycle: 50 % is a symmetric triangle; towards either end it leans into a brighter ramp |

Each oscillator shows one shape dial beside its level: **Width** while the rectangle is selected, **Rise** while the triangle is; sine and saw have none. Saw and rectangle are band-limited at their edges and the triangle at its corners, so high notes do not alias.

### Mixer and MIDI

| Control | Range | Default | Notes |
| --- | --- | --- | --- |
| Noise | 0 to 100 % | 0 | White noise into the mixer |
| MIDI channel | 1 to 16 | 15 | Notes on other channels are ignored |

Each source at 100 % plays at −6 dBFS. Above that the sum bends softly toward full scale instead of clipping, as the Minimoog's mixer overdrives: three oscillators at full level sound thicker, not distorted.

### Filter

| Control | Range | Default | Notes |
| --- | --- | --- | --- |
| Cutoff | 20 Hz to 20 kHz | 20 kHz (open) | P2, logarithmic |
| Emphasis | 0 to 100 % | 0 | P3. Grows nasal, then whistles; at 100 % the filter rings on its own |
| Contour amount | 0 to 100 % | 0 | How far the contour opens the filter: 100 % = +5 octaves |
| Contour attack | 1 ms to 10 s | 5 ms | |
| Contour decay | 1 ms to 10 s | 300 ms | Also the contour's release |
| Contour sustain | 0 to 100 % | 50 % | |
| Keyboard tracking | 0 to 100 % | 0 | At 100 % the cutoff follows the keyboard one octave per octave, around C4 |

The contour starts with the first key of a phrase, together with the loudness envelope.

**The filter graph** beside the dials draws the filter's response as the cutoff, the emphasis and the contour amount set it, from 20 Hz to 20 kHz between +24 and −48 dB. The solid curve is the filter at the cutoff set, at middle C with the contour at rest; the dashed curve is the filter where the contour takes it at its peak. Emphasis raises the peak at the cutoff and lowers the passband a little (the firmware makes up part of that loss). Keyboard tracking, velocity and LFO 1 move the cutoff while you play and are not drawn.

### Envelope

| Control | Range | Default |
| --- | --- | --- |
| Attack | 1 ms to 10 s | 5 ms |
| Decay | 1 ms to 10 s | 200 ms |
| Sustain | 0 to 100 % | 100 % |
| Release | 1 ms to 10 s | 100 ms |

The attack rises in a straight line; decay and release fall exponentially and come within 1 % of their target in the time set. The envelope is **single trigger**, as on the Minimoog: a key pressed while another is held changes only the pitch, so legato phrases do not restart it.

**The envelope graph** beside the dials draws the loudness envelope (filled) and the filter's contour (dashed) against time: from the key down until both have reached their sustain, a stretch with the key held, then the releases after the key up, at the same time scale. The labels give the time to the sustain and the longer release.

### LFOs and the mod wheel

| Control | Range | Default | Notes |
| --- | --- | --- | --- |
| LFO 1 waveform / rate | Sine, triangle, square / 0.1 to 20 Hz | Triangle / 5 Hz | |
| LFO 1 depth (vibrato) | 0 to 1200 cents | 50 cents | Pitch, scaled by the mod wheel |
| LFO 1 filter | 0 to 100 % | 0 | Cutoff, scaled by the mod wheel: 100 % = ±2 octaves |
| LFO 2 waveform / rate | Sine, triangle, square / 0.1 to 20 Hz | Sine / 4 Hz | |
| LFO 2 depth (tremolo) | 0 to 100 % | 0 | Loudness, always on |
| LFO 2 PWM | 0 to 100 % | 0 | Moves every rectangle's width, always on: 100 % = ±45 points |
| Width (each LFO, square) | 5 to 95 % | 50 % | The square's high part: short pulses give a chopped tremolo |
| Symmetry (each LFO, triangle) | 0 to 100 % | 50 % | The rise's part: 0 % is a falling ramp, 100 % a rising one |

**PWM** (pulse-width modulation) gives a rectangle the slow, chorus-like movement of the classic polysynths: try oscillator 1 as a rectangle at 50 %, LFO 2 at about 0.8 Hz and PWM at 60 %. The mod wheel is at 0 after every start, so there is no vibrato until you touch it.

### Performance

| Control | Range | Default | Notes |
| --- | --- | --- | --- |
| Glide | 0 to 5 s | 0 (off) | Slides to each new note, within 1 % of the interval in the time set |
| Bend range | 0 to 12 semitones | 2 | How far the pitch bend wheel bends, either way |
| Velocity to loudness | 0 to 100 % | 0 | At 100 % the loudness follows the velocity |
| Velocity to brightness | 0 to 100 % | 0 | A hard key opens the filter: 100 % = +3 octaves at full velocity |

### Starting points

Start from the defaults, then change only what the row lists.

| Sound | Settings |
| --- | --- |
| Fat lead | Oscillator 2 saw, level 100 %, detune +7 cents; oscillator 3 saw, level 100 %, detune −7 cents; cutoff 3 kHz; glide 80 ms |
| Bass pluck | Oscillator 3 rectangle (width 50 %), octave −1, level 100 %; cutoff 150 Hz, emphasis 40 %, contour amount 80 %, contour sustain 0 %; decay 400 ms, sustain 0 % |
| Slow wah | Cutoff 300 Hz, emphasis 60 %, contour amount 70 %, contour attack 1.5 s, contour decay 2 s |
| Wobble | Cutoff 500 Hz, emphasis 50 %, LFO 1 filter 60 %, LFO 1 rate 3 Hz, LFO 1 depth 0; push the mod wheel up |
| Expressive keys | Velocity to loudness 60 %, velocity to brightness 40 %, cutoff 1 kHz |
| Moving pulse | Oscillator 1 rectangle, width 50 %; LFO 2 rate 0.8 Hz, PWM 60 % |
| Hollow reed | Oscillator 1 rectangle, width 15 %; cutoff 2 kHz, emphasis 30 % |

Save a sound you like with **Settings → Save**.

### Behaviour worth knowing

- **Last-note priority:** a new key takes over; releasing it returns to a key still held, at once and without a new attack.
- **Silence:** MIDI controllers 120 (all sound off) and 123 (all notes off) silence the voice; changing the MIDI channel does too.
- **Load:** the voice takes about 4 % of the ESP32's audio time when silent and about 40 % at peak with two oscillators through the filter.

## Scripting and the console

Everything the window does goes through the board's text protocol, described in [Protocol.md](Protocol.md). You can use it yourself: in the window's console, from a Python script, or from any serial terminal.

### The console

**Console** (top right of the window) sends requests by hand. Type a request and press Enter; Up and Down bring back earlier requests. The log shows your request (`>`), the board's answer exactly as it was sent (`*` data lines, then `ok` or `err`) and, if **Show events** is ticked, what the board reports by itself (`!`). The log's text can be selected and copied.

The window follows what you do in the console: after a request that changes something (`set`, `app`, `potctl`, a station …), it reads the board again, so the pages show the new values. Requests are not case-sensitive; a text with spaces goes in double quotes.

**Look around.** What runs, and what the running application offers:

```
> info
ok Audio-DSP-Lab 1.1 1 esp32-pico-kit
> status
ok sigmastudio running none
> apps
* generator 1 "Signal generator"
* equalizer 2 "Equalizer"
* radio 3 "Internet radio"
* analyzer 4 "Response analyzer"
* sigmastudio 5 "SigmaStudio mode"
* synthesizer 6 "Synthesizer"
ok
```

**A test tone.** Start the generator and set it up; every `ok` carries the value the board took:

```
> app generator
ok generator running
> set waveform sine
ok sine
> set frequency 440.126
ok 440.13
> set amplitude -12.5
ok -12.5
> set output on
ok on
> get
* waveform sine
* frequency 440.13
* amplitude -12.5
* output on
* p2-range 200-2k
* width 50
ok
```

**What a parameter accepts.** `params` lists the running application's parameters: name, kind, value, minimum, maximum, default, unit, choices and the pot that plays it. A value outside the range is refused with the range:

```
> params
* waveform choice sine 0 5 sine - sine,rectangle,triangle,saw,inverse-saw,noise P1
* frequency number 440.13 20 20000 1000 Hz - P2
* amplitude number -12.5 -60 0 -20 dBFS - P4
* output switch on 0 1 off - - -
* p2-range choice 200-2k 0 2 200-2k - 20-200,200-2k,2k-20k -
* width integer 50 5 95 50 % - P3
ok
> set frequency 30000
err out-of-range frequency 20..20000 Hz
> set waveform 2
ok triangle
```

**The pots.** Switching Pot control on takes the pots' positions at once, and each value that moves is reported (here P1 already stands on triangle). After that, turning a pot is reported as an event: where the pots stand (0 to 4095) and the value it changed:

```
> potctl on
ok on
! value frequency 1021.43
! value amplitude -11.65
! value width 20
! pots 1500 2900 700 3600         (P4 turned)
! value amplitude -7.25
```

**The radio.** Enter a WiFi network once (the board keeps it), choose a station, give a station slot your own name and stream:

```
> wifi 0 "My network" "my-wifi-password"
ok
> app radio
ok radio running
! radio connecting-wifi 44100
! radio playing 48000
! title "Deutschlandfunk - Nachrichten"
> set station 2
ok 2
> station 7 name "Jazz"
ok
> station 7 url "http://example.com/jazz.mp3"
ok
```

(The title, and the stream address in the last line, are examples.)

**A frequency response.** Connect K3 to K2 with a cable, sweep 100 Hz to 10 kHz with 5 points per decade, and watch the points arrive:

```
> app analyzer
ok analyzer running
> set start 100
ok 100
> set stop 10000
ok 10000
> set points-per-decade 5
ok 5
> sweep start
ok running 0 11 0
! point 0 100 -27.47 -27.49 - -
! point 1 158.49 -27.46 -27.48 - -
…
! sweep done 11 11 0
> reference keep
ok
```

`points` lists the last sweep again; after `reference keep` every later point also carries its levels relative to the reference.

**A synthesizer sound.** The "fat lead" from the starting points above, typed in:

```
> app synthesizer
ok synthesizer running
> set osc2-level 100
ok 100
> set osc2-detune 7
ok 7
> set osc3-level 100
ok 100
> set osc3-detune -7
ok -7
> set cutoff 3000
ok 3000
> set glide 80
ok 80
> set output on
ok on
```

### Python

[tools/adl.py](../tools/adl.py) is a small Python client for the same protocol. It needs Python 3 and pyserial (`pip install pyserial`). Close the Audio-DSP-Lab window first, or press **Disconnect**: only one program can own the port.

From the command line, each quoted argument is one request:

```
python adl.py --list
python adl.py COM5 info "app generator" "set frequency 1000" "set output on"
python adl.py COM5
```

The last line starts an interactive session, like the console.

In your own scripts, `Board` sends requests and returns their answers. Three steps of a test tone:

```
import time
from adl import Board

with Board("COM5") as board:
    board.select("generator")
    board.set("amplitude", -20)
    board.set("output", True)
    for hertz in (100, 1000, 10000):
        board.set("frequency", hertz)
        time.sleep(2)
    board.set("output", False)
```

A frequency response saved as a CSV file for a spreadsheet:

```
from adl import Board

with Board("COM5") as board:
    board.select("analyzer")
    points = board.sweep(start=20, stop=20000, points_per_decade=10)
    board.set("output", False)          # a sweep switches the output on
    with open("response.csv", "w") as file:
        file.write("hz;left;right\n")
        for point in points:
            file.write(f"{point['hz']};{point['left']};{point['right']}\n")
```

### A serial terminal

Any serial terminal works too: 921600 baud, 8N1, **DTR and RTS off** (asserting them resets the ESP32), line end LF. Type `events on` to see the board's events.

## The front panel

The board can have a front panel of its own: an **ESP32-2432S028R** display board, sold as the "CYD" (cheap yellow display), 2.8 inches, 320 × 240 pixels, with a resistive touch screen. It sits on a three-wire cable to the board's panel port and runs every application without a PC: choose an application, switch the output, set any value, start a sweep, change the radio station. The panel and the PC application work side by side; what one changes, the other shows.

Wiring, power and flashing are in [the front panel guide](Panel.md). In short: CYD GPIO22 to the board's GPIO37, CYD GPIO35 to the board's GPIO4, GND to GND, and `Flash-Panel.cmd` writes the panel firmware.

### Home and settings

![Home: the six applications, the running one in teal, Pot control, and the knob strip](images/panel/home.png)

The panel connects by itself: "Connecting to the board" until the board answers, then the home screen. It follows the board live: whatever the pots, the PC application or a script changes appears on the panel at once. The six keys start an application and open its page; the running one is teal. **Pot control** switches whether the board's pots set values.

Every screen has the same frame. The top bar shows the page's name, a dot that is teal while the link stands (red without it), and, on an application's page, the **Output** switch and the house key back home. The **knob strip** at the bottom names what each pot plays now, in the knob colours: P1 blue, P2 green, P3 yellow, P4 red. Tap a knob there to set that value on the panel. The CYD's LED is green while the link stands, red without it, and amber while a synthesizer note sounds.

![Settings: pot control, the power-up choice, brightness, versions and link](images/panel/settings.png)

The gear key on the home screen opens the **settings**: Pot control; **At power-up: last application**, the board's power-up choice (off: the SigmaStudio mode, see [Power-up](#power-up)); the display's **Brightness**, which the panel keeps; and the versions of the panel and the board's firmware with the state of the link.

### Values

![The generator's page: a tile per parameter, a dot for the pot that plays it](images/panel/generator.png)

An application's page shows its parameters as tiles, two to a row, each with its value; a coloured dot marks the ones a pot plays. A page longer than the screen scrolls when you drag it; the bar at the right edge shows where you are. A tap on a tile changes its value: a switch toggles at once, a choice opens a key for each choice, and a number opens the editor.

![The editor: minus and plus, the step, x10 and the slider](images/panel/editor.png)

In the editor **−** and **+** step the value; hold them to repeat, and after a few repeats the steps grow tenfold. The step shown below the value suits the value: a percent of it for wide ranges such as a frequency, a fixed step otherwise; **x10** makes it ten times larger. The slider covers the whole range, logarithmically for frequencies. **Done** closes the editor. While you hold a key, the panel sends only the newest value, so it never falls behind.

### The applications' pages

![The equalizer: the response of the three bands, the gains and the volume](images/panel/equalizer.png)

**Equalizer.** The response of the three bands, ±15 dB from 20 Hz to 20 kHz, a dot at each band's frequency, computed as the board computes its filters. The gains and the volume come first; the band frequencies follow when you scroll.

![The radio: the station, its stream title, the station keys and the volume](images/panel/radio.png)

**Radio.** The station playing, its stream title running below it, and the connection: playing with its sample rate, connecting, or no network. The eight station keys carry the stations' names; the volume is the tile below them, and bass, middle and treble are on the pots and in the knob strip. Station names and URLs, and WiFi networks, are entered in the PC application. The panel's font has no accents: "Zwischentöne" reads "Zwischentoene".

![Response: the last sweep with the cursor on one of its points, Start sweep in the plot's corner](images/panel/response.png)

**Response.** The plot of the last sweep, the left channel teal and the right blue, from 20 Hz to 20 kHz, scaled to the 60 dB below its loudest point. **Start sweep** in the plot's corner starts a sweep and switches the output on; it reads **Stop** while the sweep runs, and the points measured stand beside it. The start and stop frequencies, the points per decade and the level are the tiles below.

**The cursor.** Touch the plot, or move a finger along it: a line jumps to the nearest measured point, a dot marks it on each channel, and a box beside the line gives the point's frequency and both levels. The cursor stays where you leave it; during the next sweep it shows again as soon as the sweep reaches its point. (In the PC application, the mouse over the plot does the same.)

![The synthesizer's filter section: the response at the cutoff, dashed at the contour's peak](images/panel/synth-filter.png)

**Synth.** Keys choose a section of the synthesizer's 48 parameters: **Osc** (the three oscillators and the noise), **Filter**, **Env** (the loudness envelope), **LFO** (both LFOs, PWM and the filter's LFO), **Perf** (glide, bend, velocity, volume, the MIDI channel), or **All**. **Filter** draws the filter's response at middle C, solid at the cutoff and dashed where the contour takes it at its peak.

![The synthesizer's envelope section: loudness solid, the filter's contour dashed](images/panel/synth-envelope.png)

**Env** draws the loudness envelope, solid, and the filter's contour, dashed, from the key down through a held stretch to the end of the release, at one time scale.

**SigmaStudio.** The page says that your program runs; the knob strip shows the pots going to the DSP's ADC inputs.

### WiFi control

**WiFi control.** The panel also lets the PC application work over WiFi, so the board needs no USB cable. It joins the networks the board keeps for its radio (network 1 first, then network 2) and takes the application on TCP port 7000, as **audio-dsp-lab.local** or at its address on your network. To set it up, open **WiFi networks …** in the PC application while it is connected by USB: the **Front panel** section shows whether the panel is online and at which address; **Use for connecting** puts **Network: <address>** in the port list. From then on choose that entry under **Board on** and **Connect**. The panel stays usable meanwhile; its LED turns blue while a PC works through it. Changing the networks in the dialog makes the panel join again.

Anyone on your network can reach the board this way, as anyone with the USB cable can; the WiFi passwords themselves never leave the board over the network.

### When the panel does not connect

| Symptom | Likely cause | Fix |
| --- | --- | --- |
| "Connecting to the board" stays | The board's firmware has no panel port (older than this release) | Flash the board's firmware from this release |
| | TX and RX swapped, or GND missing | CYD GPIO22 to the board's GPIO37, CYD GPIO35 to GPIO4, GND to GND |
| The link drops now and then | GPIO37 floats while the panel restarts | Fit the 10 kΩ pull-up from GPIO37 to 3.3 V |
| Touches land beside their target | Another CYD's touch calibration | Tap the corners and adjust `Config.h` (see the front panel guide) |
| Colours swapped or the picture upside down | The two-USB CYD with its ST7789 display | Build the panel firmware with `DISPLAY_ST7789` set to 1 |
| Settings shows "WiFi: none" | No network stored on the board | Enter one in **WiFi networks …** (it serves the radio too) |
| Settings shows "WiFi: failed" | Neither network answers, or a 5 GHz-only network | The panel tries again every 30 s; use a 2.4 GHz network |
| "Network: …" does not connect | The panel's address changed, or the PC is on another network | Read its address in Settings or the WiFi dialog; try audio-dsp-lab.local |

## Troubleshooting

| Symptom | Likely cause | Fix |
| --- | --- | --- |
| The port is not in the list | The CP210x driver is missing, or a charge-only USB cable | Install the driver (see the installation guide), use a data cable, press ↻ |
| "COMn is in use by another program" | Arduino's serial monitor, a terminal or another Audio-DSP-Lab holds the port | Close it, then Connect |
| "No Audio-DSP-Lab board answers" | The board runs other firmware, or JP1 is not on ext. MCLK | Flash the firmware; check the jumpers |
| No sound after selecting an application | The output is off after every start | Press **Output on**; check volume or amplitude |
| The application fails with `eeprom-failed` | SigmaStudio's USBi is still on the I2C header | Unplug the USBi and select again |
| After plugging the board in it does not answer, or the SigmaStudio mode shows `eeprom-failed` | The front panel has its own supply and kept the board's ESP32 half powered through its cable while the board was unplugged | Press the ESP32 kit's EN button (or `reset`); power the panel from the board, or unplug the panel first (see the front panel chapter) |
| The application fails with `dsp-not-responding` | JP1 on the crystal instead of ext. MCLK, JP2 not on Selfboot | Set the jumpers as in the installation guide |
| After a power cycle the board is in SigmaStudio mode | **Power-up** shows SigmaStudio mode, or the last application could not start | Select the application again, or set **Power-up** to **Last application** |
| Radio shows "No network" | No WiFi network set, a 5 GHz network, or out of reach | **WiFi**: enter a 2.4 GHz network |
| A station of your own stays at "Stream error" | The URL is `https://` only, AAC, a playlist, or no longer served | Find the station's plain `http://` MP3 stream |
| Synthesizer silent, Note stays `---` | No MIDI arrives: wrong cable or plug, or a jack that is not MIDI | Use the keyboard's DIN MIDI Out and a type A cable (pin 4 ring, pin 5 tip) |
| Notes arrive but the synthesizer stays silent | The keyboard sends on another channel, or the output is off | Set **MIDI channel**; press **Output on** |
| Vibrato or LFO 1 filter does nothing | The mod wheel is at 0 after every start | Push the mod wheel up |
| Your SigmaStudio program is gone after using an application | The copy failed (USBi on the bus) or an EEPROM write did not land | Return to SigmaStudio mode (the copy is written back); confirm writes in SigmaStudio |
| "The board restarted" in the status line | A power dip, a reset, or a crash | The window reads everything again; if it repeats, use a better USB port or supply |

## Technical notes

- **Signals from the ESP32.** The generator and the synthesizer compute their signal on the ESP32 at 48 kHz, 16 bits, and send it to the DSP over I2S; the DSP runs the radio's program for them (the equalizer, set flat, and the volume). The generator keeps its phase in 32 bits, so its frequency is exact to the 0.01 Hz set.
- **The DSP's core rate.** The SigmaStudio projects run the ADAU1701's core twice per 48 kHz sample (96 kHz). Every frequency the firmware computes (oscillators, filters) is computed for that rate; frequencies typed into SigmaStudio's own controls for these projects act twice as high.
- **The clock.** With JP1 on ext. MCLK the ESP32 supplies 12.288 MHz (256 × 48 kHz). The radio drives the clock from its stream's rate instead (256 × 44.1 kHz for most stations), and the equalizer's filters are recomputed for it.
- **What the board stores** in the ESP32's flash: the WiFi networks, the stations, the analyzer's reference, the power-up choice, and the copy of your SigmaStudio program. Flashing a firmware update with the flash tool keeps them; erasing the flash removes them.
- **The pots** are read by the ESP32 (12-bit, smoothed). The DSP sees them only through the ESP32's DACs in SigmaStudio mode.
- **The ESP32 kit.** The board takes an ESP32-PICO-KIT V4 (PICO-D4) or an ESP32-PICO-KIT-1 (PICO-V3). They differ in the pin that carries I2S audio to the DSP (GPIO18 or GPIO7); the firmware detects the kit and the window shows it beside the firmware version.
