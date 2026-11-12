# Manual

As of 2026-11-11.

Audio-DSP-Lab turns the Elektor Audio-DSP-FX board into seven audio instruments that you run from a Windows PC over USB: a signal generator, an equalizer, an internet radio, an analysis application (frequency response and harmonic distortion), a SigmaStudio mode for your own DSP programs, a Minimoog-style synthesizer, and an SDR receiver backend that takes a real IF or the I/Q output of your front end. This manual covers firmware 1.7 and application 1.7.1, and the optional front panel (panel firmware 1.7.1).

Audio-DSP-Lab lives on GitHub at **[github.com/andiepp/Audio-DSP-Lab](https://github.com/andiepp/Audio-DSP-Lab)**: the source code, the releases (application, firmware and these documents to download) and the documentation, this manual among it.

![Audio-DSP-Lab: the PC over USB, or the optional front panel on its own or as the PC's WiFi link, sets the values; the board does the audio](images/overview.png)

## At a glance

Audio-DSP-Lab has three parts:

- **The firmware** runs on the board's ESP32. It writes each application's DSP program into the ADAU1701's EEPROM, computes filter coefficients, reads the pots and MIDI, streams the radio, makes the generator's signal and the synthesizer's sound, runs the receiver's filters and demodulators, and analyses the spectrum of the line input for the analysis' THD and Spectrum. It talks to the PC over the board's USB port with a simple text protocol.
- **The application** (`AudioDspLab.exe`) is the PC's front panel. It shows one page per application, sets every value, and shows what the board reports, including the changes you make with the board's pots.
- **The front panel** (optional) is a small touch display on a cable to the board, an ESP32 "CYD". It runs the applications without a PC, and it lets the PC application work over WiFi, without a USB cable. See the chapter [The front panel](#the-front-panel); the chapter [Mechanical design](#mechanical-design) shows a printed enclosure for the board and the panel.

The board keeps working without the PC: once an application runs, you can close the window and keep playing. The board can also start an application by itself at power-up.

| Application | What it does | Audio |
| --- | --- | --- |
| Generator | Sine, rectangle, triangle, saw, inverse saw or noise, 20 Hz to 20 kHz, with a width for rectangle and triangle | out K3, K4 |
| Equalizer | Bass, middle, treble and volume on the line input | in K2, out K3, K4 |
| Radio | Eight MP3 internet stations over WiFi, through the equalizer | out K3, K4 |
| Analysis | Frequency Response: sweeps a sine through a device under test and plots its response. THD: its harmonic distortion, THD+N and spectrum. Spectrum: what comes on the line input | out K3, K4, in K2 |
| SigmaStudio | Runs your own SigmaStudio program from the EEPROM | as your program |
| Synthesizer | A monophonic synthesizer played from a MIDI keyboard | MIDI in K5, out K3, K4 |
| Receiver | An SDR receiver backend from the IF on: AM, USB, LSB, CW and FM from a real IF or I/Q on K2, delivered by your front end, with channel filters of up to 511 taps, AGC, S-meter, noise reduction and squelch | IF or I/Q in K2, out K3, K4 |

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

### Analysis

Measures a device under test: a sine leaves on K3/K4, passes the device and returns on K2. The application has three parts, chosen with **Measure** above the plot: **Frequency Response**, **THD** and **Spectrum**. They use the same tone, output level and output switch; Spectrum plays no tone of its own.

#### Frequency Response

The board reads the level of both channels at each frequency of a sweep.

1. Connect K3 straight to K2 with a cable, run a sweep, tick **Replace the reference** and press **Keep as reference**.
2. Put the device between K3 (or K4) and K2 and press **Start sweep**. The curve grows point by point.
3. Choose **Relative to reference** to see the device's own response, without the board's.

![The analysis page, Frequency Response: a sweep of a low-pass](images/screenshots/analyzer.png)

| Control | Range | Default |
| --- | --- | --- |
| Output level | −60 to 0 dBFS (P2) | −20 dBFS |
| Sweep start / stop | 20 Hz to 20 kHz | 20 Hz / 20 kHz |
| Points per decade | 1 to 40 (up to 121 points over 20 Hz to 20 kHz) | 10 |
| Tone between sweeps | 20 Hz to 20 kHz | 1 kHz |

A full sweep at 10 points per decade takes about 10 s; the line under the points per decade gives the estimate. Between sweeps the board plays the tone and the window shows the input levels twice a second. The reference is kept in the board and survives power cycles. Mind the common ground between K3 and K2; the measurement's floor is about −110 dBFS.

#### THD

The board takes the spectrum of what returns on K2 and computes the tone's harmonic distortion: per channel 8192 samples (0.17 s) through an FFT, four captures averaged, a new result about every 1.6 s.

1. Choose **Measure: THD** and press **Start measuring**. The output switches on, and the tone moves to the nearest frequency the FFT holds exactly (a multiple of 5.86 Hz: 1000 Hz plays as 1001.95 Hz), so it leaks into no neighbouring frequencies.
2. Read, per channel: **THD**, the harmonics H2 to H10 below 20 kHz against the tone; **THD+N**, everything from 20 Hz to 20 kHz but the tone; both in % with dB beneath. Below them each harmonic in dBc, and above them the spectrum from 20 Hz to 24 kHz with the tone marked 1 and its harmonics 2 to 10. Hover over the spectrum to read a point.
3. Change the tone and the output level while it measures; **Stop** ends it, and so does a sweep.

![The analysis page, THD: the spectrum with the harmonics marked, THD and THD+N per channel](images/screenshots/thd.png)

What the board measures of itself, a cable from K3 to K2: at an output level of −20 dB (−27 dBFS at the input) THD+N about 0.05 %, the harmonics below the noise; at 0 dB (−7 dBFS) THD about 0.016 % and THD+N about 0.017 %, H2 −81 and H3 −78 dBc. Measure the cable first: a device's figures mean something down to these. A level near the input's full scale gives the lowest THD+N, until the converters' own distortion rises.

#### Spectrum

The spectrum of whatever comes on K2, with no tone of its own: a signal from outside (a preamp, an effect, a phone), the board's own noise floor, mains hum. The measurement is THD's (8192 samples per channel, four captures averaged, a result about every 1.6 s); the output stays as it is, normally off.

1. Choose **Measure: Spectrum** and press **Start measuring**.
2. Read, per channel: the **RMS** from 20 Hz to 20 kHz in dBFS (a full-scale sine reads −3.01; with nothing on K2 it is the noise), and the **strongest peak** with its frequency and level. Below them the five strongest peaks of each channel, strongest first, marked 1 to 5 in the spectrum. A peak's level is a sine's peak in dBFS; peaks closer than about 47 Hz count as one.
3. **Stop** ends it; THD and a sweep end it too.

![The analysis page, Spectrum: the spectrum with its five strongest peaks, the RMS and the strongest peak per channel](images/screenshots/spectrum.png)

The board's own input, nothing on K2: an RMS of about −97.5 dBFS, about 95 dB below a full-scale sine, and mains hum at 50 Hz around −105 dBFS, more or less with the cables and the supply.

### SigmaStudio mode

The DSP runs whatever SigmaStudio last wrote to its EEPROM. The firmware supplies the 48 kHz clock and stays off the I2C bus, so you can download programs and write the EEPROM with the USBi at any time. The pots are always passed to the DSP's auxiliary ADC: P1 to ADC0, P2 to ADC1, P3 to ADC2, P4 to ADC3, every 20 ms.

![The SigmaStudio page: the pots on their way to the DSP's ADC](images/screenshots/sigmastudio.png)

Selecting another application writes its program over yours, but the firmware first keeps a copy of yours in its flash and writes it back when you return to this mode. **Unplug the USBi before you switch.** Confirm every EEPROM write in SigmaStudio. How to build your own programs for the board is described in [SigmaStudio.md](SigmaStudio.md).

### Synthesizer

See the chapter [The synthesizer](#the-synthesizer) below.

### Receiver

The receiver is the backend of an SDR receiver from the IF on: it takes a real IF, or I and Q, from your front end on K2, tunes, filters and demodulates them on the ESP32, and plays the audio through the equalizer's bands and volume on K3 and K4. What it expects at K2 (levels, frequencies, noise) is specified in its chapter. Its page has three views: **Receiver** (the IF dial, meters, AGC, noise reduction, squelch, the input), **Filter** (the channel filter's response, measured on the board) and **Designer** (filters designed on the PC and downloaded). See the chapter [The receiver](#the-receiver) below.

## Pots, stored settings, power-up and service mode

### The pots

The pots set values only while **Pot control** is on, and it is off at every power-up. SigmaStudio mode is the exception: there the pots always go to the DSP's auxiliary ADC.

| Application | P1 | P2 | P3 | P4 |
| --- | --- | --- | --- | --- |
| Generator | Waveform | Frequency | Width | Amplitude |
| Equalizer, Radio | Bass gain | Middle gain | Treble gain | Volume |
| Analysis | – | Output level | – | – |
| SigmaStudio | ADC0 | ADC1 | ADC2 | ADC3 |
| Synthesizer | Oscillator 1 width | Cutoff | Emphasis | Volume |
| Receiver | Filter width | NR level | Squelch | Volume |

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

- With **Last application**, the radio, the synthesizer and the receiver come up sounding: their output is switched on, at the default volume of −20 dB. The generator and the analysis stay off, since they may drive a device under test.
- The receiver comes up with its defaults (USB, IF 10 kHz).
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

## The receiver

The receiver is the back end of a software-defined receiver, from the IF on. The front end is yours: whatever turns the antenna's signal into a real intermediate frequency (IF) on K2's left channel, or into I and Q at audio frequencies on K2's two channels. The ESP32 does the rest: it tunes within the band that K2 carries, filters the channel, keeps the level with an AGC, reads the S-meter, demodulates AM, USB, LSB, CW or FM, reduces the noise, mutes with the squelch, and sends the audio back to the DSP, through the equalizer's bands and volume to K3 and K4. The chain is the author's receiver for a TMS320C6713 DSP board, ported to the ESP32; it takes 15 to 31 % of one of the ESP32's cores.

The receiver is defined at its input: the next section gives what it expects at K2 and how sensitive it is there. The front end is not part of Audio-DSP-Lab, and neither the application nor the board tunes it: the receiver tunes only within the band that K2 carries.

![The receiver page, Receiver view: the mode keys, the IF dial, the S-meter with AGC gain, audio level and CPU load, the filter, AGC, noise reduction and squelch, the input with its I/Q correction, and the equalizer](images/screenshots/receiver.png)

### What the receiver expects at K2

K2 is the board's line input: the left channel goes to ADC0, the right to ADC1, both sampled at 48 kHz, with about 22.5 kΩ input impedance. The receiver takes either

- **a real IF** on the left channel (**Input: IF**); the right channel is not used, and the **IF** dial is set to the front end's IF; or
- **I and Q** at audio frequencies, I on the left and Q on the right (**Input: I/Q**, or **Q/I** for the other way round); the receiver tunes within ±21 kHz of the I/Q centre, the front end's frequency, and the **IF** dial is the station's distance from it.

Measured with a signal generator straight into K2 and no front end: a 10 kHz sine set with the generator's load at High-Z; signal and noise as the audio's RMS in USB with a 2.4 kHz filter, the AGC off at 0 dB and the noise reduction off; the noise with the generator connected but its output off. A level in dBFS is a sine's peak re the ADC's full scale; the S-meter reads a tone's level at the ADC this way, with the IF input too.

| At K2, in 2.4 kHz | IF input (K2 left) | I/Q input (per channel) |
| --- | --- | --- |
| Full scale, 0 dBFS (the ADC clips) | 2.98 Vpp = 1.05 V RMS = +2.7 dBu | The same |
| 1 Vpp at 10 kHz reads | −9.5 dBFS | −9.5 dBFS |
| Noise-equivalent input (S/N 0 dB) | 5.4 µV RMS, −103 dBu, −106 dBFS | 3.7 µV RMS, −106 dBu, −109 dBFS |
| Noise density | About 110 nV/√Hz | About 75 nV/√Hz |
| 10 dB S/N | 17 µV RMS, −93 dBu, −96 dBFS | 12 µV RMS, −96 dBu, −99 dBFS |
| Dynamic range, full scale to noise | About 106 dB | About 109 dB |

With K2 open, `diag rx` reads about −98.5 dBFS (I) and −100 dBFS (Q) RMS over the whole band.

**What a front end should deliver.** As a simple rule, a front end should deliver its signal at K2 at about **1 mV amplitude** (2 mVpp, 0.7 mV RMS; the S-meter reads about −63.5 dBFS). There a signal lies 42 dB (IF) to 46 dB (I/Q) above the receiver's noise in 2.4 kHz and about 63 dB below full scale. Besides:

- **Headroom.** Keep the sum of all strong signals at K2 below full scale: −6 dBFS is about 1.5 Vpp. Above 0 dBFS the ADC clips, strong stations distort and extra carriers appear.
- **Band-limiting.** Nothing on the board limits the band: the ADCs pass up to 24 kHz (−2.7 dB at about 23 kHz). Whatever reaches K2 above that, a signal or a product of the front end, folds back into the band. Filter the front end's output above about 21 kHz.
- **Frequencies with an IF.** Keep the IF and the filter's edges together inside about 0.5 to 22 kHz: USB at the default IF of 10 kHz takes 10.3 to 12.7 kHz.
- **Frequencies with I/Q.** The receiver tunes within ±21 kHz of the I/Q centre, with the filter's edges up to ±5.5 kHz from the carrier. The default of 10 kHz keeps the station clear of two traps: below about 8 kHz a real front end's noise rises near its LO, and at exactly 12 kHz, a quarter of the 48 kHz sample rate, a front end's third-order products fold onto the image, where no I/Q correction removes them (on the author's bench the image null stopped at 56 dB there, against 67 dB at 10 kHz).
- **I/Q balance.** The image rejection is the balance of the front end and the two ADC channels together. The receiver corrects errors of up to ±3 dB and ±10° (**I/Q gain**, **I/Q phase**, **Null image**; see [Input and I/Q correction](#input-and-iq-correction)); with the correction exact, its own image rejection exceeds 130 dB (measured with the internal test signal).

### What the receiver needs

- **A front end** that delivers a real IF or I and Q to K2, as above: a superhet's IF, a direct-conversion I/Q mixer, a quadrature sampling detector. The receiver tunes only within the band that K2 carries; it does not tune the front end.
- **An antenna**, or for a bench check a signal generator into the front end's RF input, through attenuators.

The receiver also runs without a front end: the internal test tone (on the Filter view), or a generator's audio-frequency IF or I and Q on K2, stands in for the band.

### Connecting the front end

| From | | To | Input |
| --- | --- | --- | --- |
| A real IF | → | K2 left (ADC0); the right channel is not used | **IF** |
| I | → | K2 left (ADC0) | **I/Q** |
| Q | → | K2 right (ADC1) | **I/Q** |
| The front end's ground | — | K2's sleeve | |
| Antenna or generator | → | The front end's RF input | |

K2 is a plain line input of about 22.5 kΩ; nothing between the front end and the ADCs is needed. If I and Q are the wrong way round, choose the input **Q/I** instead of swapping the cables: with them swapped, USB and LSB change places and the tuning runs backwards.

### Getting it going

1. Select **Receiver**. It starts in USB with the input I/Q, the IF at 12 kHz, the AGC at medium and the noise reduction LMS at level 0 (it passes the audio unchanged).
2. Choose the **Input**: **I/Q** for I and Q, **IF** for a real IF.
3. Set the **IF** dial (with an IF input, to your front end's IF; with I/Q, to the station's distance from the I/Q centre), then press **Output on**.
4. With the input I/Q and a steady carrier in the channel (a generator, or a steady station), press **Null image** once: it finds the I/Q correction (see [Input and I/Q correction](#input-and-iq-correction)).

### The Receiver view

**Mode.** The keys **Off**, **AM**, **USB**, **LSB**, **CW** and **FM** choose the demodulator. A mode loads its own filter, as a receiver's mode keys do:

| Mode | Filter, Hz re the carrier | What you hear |
| --- | --- | --- |
| Off | −3000 to +3000 | The input through the channel filter, at its own frequency: for tests |
| AM | −3000 to +3000 | The envelope |
| USB | +300 to +2700 | The upper sideband |
| LSB | −2700 to −300 | The lower sideband |
| CW | 500 Hz around the pitch (600 Hz: +350 to +850) | The carrier as a tone at the pitch |
| FM | −5500 to +5500 | Narrow-band FM, with its deviation and de-emphasis |

**The IF dial.** The dial is the receiver's **IF**, −21000 to +21000 Hz, 10000 at the start (in the protocol the parameter `offset`): with the input **IF** the IF, with **I/Q** the station's distance from the I/Q centre. Turn the mouse wheel over a digit, or click its upper or lower half, as on every digit dial.

**S-meter and meters.** The S-meter reads the level in the channel, after the channel filter, in dBFS, on a bar from −130 to 0 dBFS with the squelch's threshold marked; a steady carrier reads its level at K2 (see [What the receiver expects at K2](#what-the-receiver-expects-at-k2)). The line below gives the AGC's gain, the audio's level and the receiver's share of the ESP32's core (**CPU**); the next one whether the squelch is open, in FM the **FM offset** (how far the carrier lies off the dial, a tuning indicator), and with the spectral noise reduction its average gain. The board reports them five times a second. The S-meter is the AGC's detector: it rises at once and falls after the hang time at the decay rate.

**Filter.** The **Width** dial (P1) sets the channel filter's width; the mode places it: USB and LSB from 300 Hz off the carrier, CW around the pitch, the others around the carrier. **CW pitch** (300 to 1000 Hz, shown in CW) moves the CW filter with the tone. Beside them the filter's edges, whether it is the receiver's own design or a download, its taps, and a small picture of its response. The edges themselves are on the Filter view.

The receiver designs its channel filter itself, from the two edges and the stop band: a complex FIR of up to 511 taps at 12 kHz, Kaiser-windowed, with a transition of a quarter of the width (60 to 300 Hz, wider when it would need more than 511 taps). USB at 2.4 kHz and 80 dB takes 203 taps. A new filter is designed in the background and switched in between two blocks of samples, without a click.

**AGC, noise reduction, squelch.**

| Control | Range | Default | Notes |
| --- | --- | --- | --- |
| AGC | Off, fast, medium, slow | Medium | A speed loads its hang and decay: fast 0.1 s and 100 dB/s, medium 0.5 s and 20 dB/s, slow 1.5 s and 10 dB/s |
| Gain | 0 to 90 dB | 60 dB | The AGC's largest gain; with the AGC off, the fixed gain |
| Hang, decay | 0 to 5 s, 1 to 200 dB/s | as the speed | Change them after choosing the speed |
| NR | Off, LMS, spectral | LMS | The noise reduction on the audio |
| NR level | 0 to 30 dB | 0 dB | P2; 0 passes the audio unchanged |
| Squelch | −150 to 0 dBFS | −150 (open) | P3; mutes while the S-meter stays below it |
| Deviation | 100 to 5000 Hz | 2500 Hz | FM only: the deviation that gives −12 dBFS of audio |
| De-emphasis | 0 to 2000 µs | 0 (off) | FM only |

The AGC aims at −12 dBFS of audio and looks ahead by one block (5.3 ms), so a sudden signal does not overshoot. **LMS** is a line enhancer: it keeps what repeats (a CW tone, a voice's harmonics) and lowers the hiss; the level sets how hard it adapts. **Spectral** lowers each part of the spectrum by how far it stands above the noise it has estimated; the level is how far it may lower. Keyed CW at −40 dBFS in noise at −50 dBFS: between the keys the noise fell by about 10 to 12 dB at a level of 12 dB with either method, and by 21 dB with the spectral method at 24 dB. The spectral method takes a carrier that never stops for noise, and lowers it with the noise: on a steady station it dulls the sound, LMS does not.

#### Input and I/Q correction

**Input** chooses what K2 carries: **I/Q** (I left, Q right), **Q/I** (swapped), or **IF**, a real intermediate frequency on the left channel alone, where the dial is set to the IF.

A front end's I and Q are never exactly equal in level and never exactly 90° apart, and the two ADC channels add their own small difference. What is left of it lets a station show also on the mirror side of the I/Q centre, its image: uncorrected, typically some 30 dB below it. **I/Q gain** (−3 to +3 dB) and **I/Q phase** (−10 to +10°) correct front end and ADCs together.

**Null image** finds the two values for you. Tune a steady carrier into the channel, ideally at the default IF of +10 kHz (not at 12 kHz, see above), then press the key. The application tunes the receiver to the carrier's mirror, sets AM with a ±150 Hz filter and the AGC off, and searches phase and gain in turn, three rounds, for the lowest S-meter reading. Afterwards it puts mode, filter, IF and AGC back, also after **Cancel**, and keeps the corrections it found; the line beside the key gives the image rejection before and after and the values. On the author's bench, with a QRP Labs I/Q module, it went from about 32 dB to 66 to 72 dB, with corrections of about +0.41 dB and +1.1°. The corrections are values of the receiver like any other: they go back to 0 at the next start, so keep them in a stored setting (**Settings → Save**).

#### Equalizer

Bass, middle and treble with their frequencies, and the volume (P4), as on the equalizer page; the **Output** key beside the volume. The audio is the same on both channels.

### The Filter view

![The receiver page, Filter view: the channel filter's response with its edges, the stop-band line and a measurement on top; the edges, width, stop band and pitch below; Measure and the test tone](images/screenshots/receiver-filter.png)

The chart draws the channel filter from −6 to +6 kHz around the carrier, computed as the receiver designs it. Drag an edge, the passband between them (both edges move) or the dashed stop-band line: the receiver designs the new filter when you let go. Hover over the chart to read a point. The dials below set the same values by number:

| Control | Range | Default | Notes |
| --- | --- | --- | --- |
| Low edge, high edge | −5500 to +5500 Hz | USB: 300, 2700 | At −6 dB, re the carrier; at least 50 Hz apart. An edge that would pass the other pushes it along |
| Width | 100 to 11000 Hz | 2400 | High minus low, placed by the mode |
| Stop band | 40 to 100 dB | 80 | How deep the filter blocks; more taps for more |
| CW pitch | 300 to 1000 Hz | 600 | CW only |

The lines below give the edges, the width and the centre, and the design the receiver makes of them: its taps, its transition and its delay (half the taps at 12 kHz: 203 taps delay by 8.4 ms).

**Measure** checks the filter on the board. The internal test tone steps across −5900 to +5900 Hz in steps of 50, 100 or 200 Hz (**Step**), and the S-meter reads each step; the curve appears on the chart over the prediction, and the line beneath says how well they agree (on the board: within 0.00 dB in the passband, within 0.2 dB down to −100 dB). While it measures the AGC is off and the S-meter fast; afterwards the test tone and the AGC are as before, also after **Cancel**.

**Test tone** puts the receiver's internal signal in place of the input, for trying the receiver without a front end:

| Control | Range | Default | Notes |
| --- | --- | --- | --- |
| Test tone | Off, on | Off | Replaces the input while on |
| Tone offset | −45000 to +45000 Hz | 1000 | From the IF |
| Tone level | −120 to 0 dBFS | −20 | |
| Noise | −150 to 0 dBFS | −150 (none) | White noise added, RMS |
| Keying | 0 to 5000 ms | 0 (steady) | A period, half of it on, at least 100 ms |

### The Designer view

![The receiver page, Designer view: a mask of two passbands with a notch between them, the response designed from it, and the keys to download, import, export and keep designs](images/screenshots/receiver-designer.png)

The Designer makes filters the receiver's own design cannot: notches, several passbands, slopes. It designs them on the PC and downloads them.

**The mask** is a list of bands, each from and to a frequency (Hz re the carrier, within ±5500 Hz, edges at −6 dB) at a gain (−60 to +20 dB); outside the bands the filter blocks. **Add band** adds one, **✕** removes one; bands must not overlap. The keys beside start from a template: **Notch in USB**, **Two passbands**, **CW 200 Hz**, **Sloped speech**, or **Like the filter in force** (the receiver's own edges and stop band). **Stop band** (20 to 120 dB) and **Transition** (20 to 1000 Hz) set the window and the length, by Kaiser's formula, up to 511 taps; the line beneath gives the taps, whether the filter is real or complex, and its delay. The chart draws the mask and the response designed from it.

**Download** sends the coefficients to the receiver, which switches them in between two blocks; the bar shows the progress (347 taps took 0.44 s). The receiver keeps a downloaded filter until a new edge, width, stop band, pitch or mode makes it design its own again; **Design again** goes back to its own design at once. The state shows at the top right of the page, and every start begins with the receiver's own design.

**Import CSV …** reads coefficients from a file, one tap per line in natural order: `k,re,im` (as **Export CSV …** writes them), `re,im`, or one real value; lines starting with `#` or a letter are skipped, and 1 to 511 taps are taken. **Use the mask** returns from the imported coefficients to the mask.

**Designs kept on this PC.** Type a name and press **Save**; **Load** and **Delete** work on the design chosen in the list. Each design is a JSON file in `%LocalAppData%\Audio-DSP-Lab\Filter designs`.

### The receiver's pots

| Pot | Sets | Range |
| --- | --- | --- |
| P1 | Filter width | 100 Hz to 11 kHz, logarithmic, in 10 Hz steps: about 480 Hz at a third of the travel, 2.3 kHz at two thirds. The mode places and limits it: USB and LSB reach 5.2 kHz |
| P2 | NR level | 0 to 30 dB |
| P3 | Squelch | Off over the first 5 % of the travel, then −130 to −30 dBFS |
| P4 | Volume | −60 to 0 dB |

Tuning stays with the PC application and the front panel: a pot's reading moves by about 0.7 %, too coarse for a sideband signal.

### Levels and sensitivity

The receiver's own figures are those at K2, in [What the receiver expects at K2](#what-the-receiver-expects-at-k2): in 2.4 kHz a noise-equivalent input of about 3.7 µV RMS (−106 dBu) per channel with I/Q and 5.4 µV RMS (−103 dBu) with an IF, a full scale of 2.98 Vpp (+2.7 dBu), about 106 to 109 dB between them. What that makes at the antenna depends on the front end's gain and noise; the simple rule of about 1 mV amplitude at K2 is in [What the receiver expects at K2](#what-the-receiver-expects-at-k2). Besides:

| | Measured |
| --- | --- |
| Load | 15 % of one core (USB, 203 taps) to 31 % (FM, 511 taps, spectral noise reduction) |
| Delay | About 25 to 45 ms, estimated from the blocks, the AGC's look-ahead and the channel filter's delay (8 to 21 ms) |

### Behaviour worth knowing

- **Tuning.** The PC's and the front panel's **IF** dials tune the receiver within ±21 kHz and follow each other; the pots do not tune. The receiver never tunes the front end.
- **Values that follow each other.** A mode loads its filter, an AGC speed its hang and decay, the edges and the width follow each other, and in CW the filter moves with the pitch. The board reports each value it changed this way, so every window and the panel show it.
- **One application at a time.** The receiver uses the DSP's serial output to the ESP32, as the analysis' THD does: `diag capture` and `diag record` answer `busy` while it runs.
- **Mono.** The audio is the same on the left and the right channel.
- **The simulator** runs the receiver too, so the page can be tried without hardware.

## Scripting and the console

Everything the window does goes through the board's text protocol, described in [Protocol.md](Protocol.md). You can use it yourself: in the window's console, from a Python script, or from any serial terminal.

### The console

**Console** (top right of the window) sends requests by hand. Type a request and press Enter; Up and Down bring back earlier requests. The log shows your request (`>`), the board's answer exactly as it was sent (`*` data lines, then `ok` or `err`) and, if **Show events** is ticked, what the board reports by itself (`!`). The log's text can be selected and copied.

The window follows what you do in the console: after a request that changes something (`set`, `app`, `potctl`, a station …), it reads the board again, so the pages show the new values. Requests are not case-sensitive; a text with spaces goes in double quotes.

**Look around.** What runs, and what the running application offers:

```
> info
ok Audio-DSP-Lab 1.7 1 esp32-pico-kit
> status
ok sigmastudio running none
> apps
* generator 1 "Signal generator"
* equalizer 2 "Equalizer"
* radio 3 "Internet radio"
* analyzer 4 "Analysis"
* sigmastudio 5 "SigmaStudio mode"
* synthesizer 6 "Synthesizer"
* receiver 7 "SDR receiver"
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

**A THD measurement.** The same cable, the output at its full level; every result arrives as an event, about every 1.6 s:

```
> set level 0
ok 0
> thd start
ok running 0 - - - - - - -
! value output on
! thd running 1 1001.95 -7.38 -77.28 -76.58 -7.40 -75.72 -75.22
> harmonics
* 2 2003.9 -81.43 -77.09
* 3 3005.85 -78.07 -80.57
* 4 4007.8 -96.99 -93.6
…
ok
> thd stop
ok idle 2 1001.95 -7.38 -77.28 -76.58 -7.40 -75.72 -75.22
```

THD and THD+N are in dB against the tone: −77.28 dB is 0.0137 % (100 × 10^(dB/20)). The tone plays at 1001.95 Hz while THD measures, the nearest frequency the FFT holds exactly.

**A spectrum.** The cable out of K2 and the output off, so the board shows its own input:

```
> set output off
ok off
> spectrum start
ok running 0 - -
! spectrum running 1 -97.53 -98.08
> spectrum peaks
* 1 47.1 -104.56 48.1 -104.78
* 2 159.5 -117.18 251.2 -116.02
* 3 254.6 -115.38 4232.5 -117.44
* 4 4232.5 -116.51 114.9 -115.88
* 5 415.7 -117.46 343.3 -116.13
ok
> spectrum stop
ok idle 3 -97.29 -97.7
```

The RMS from 20 Hz to 20 kHz is −97.5 dBFS on the left; the strongest peak is mains hum near 50 Hz. `spectrum points 64` gives the spectrum itself, 64 points from 20 Hz to 24 kHz.

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

**The receiver.** AM, with the filter the mode loads, and the meters read by hand:

```
> app receiver
ok receiver running
> set mode am
ok am
> get width
ok 6000
> set offset 9000
ok 9000
> meter
ok -41.2 38.5 3 1 -12.3 21.4 0.0
> rxfilter
ok designed 203 300 ready 2
```

`meter` gives the S-meter in dBFS, the AGC's gain, the FM offset, whether the squelch is open, the audio level, the CPU load in % and the spectral noise reduction's gain; with **Show events** ticked the same comes five times a second as `! meter`. `rxfilter` names the filter in force, its taps, its transition, whether a new one is still being designed, and how many filters the receiver has switched in since it started. (The readings are examples.)

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

THD of a device, with `request` for the requests the client has no method of its own for:

```
import time
from adl import Board

with Board("COM5") as board:
    board.select("analyzer")
    board.set("level", -10)
    board.request("thd start")          # switches the output on
    time.sleep(8)                       # four or five results
    state, results, hz, level_l, thd_l, thdn_l, level_r, thd_r, thdn_r = board.request("thd").values
    print(f"{hz} Hz: THD {thd_l} / {thd_r} dB, THD+N {thdn_l} / {thdn_r} dB")
    for order, hertz, left, right in board.request("harmonics").data:
        print(f"H{order} {hertz} Hz: {left} / {right} dBc")
    board.request("thd stop")
    board.set("output", False)
```

### A serial terminal

Any serial terminal works too, PuTTY or Tera Term for example, with their default settings and the port set to 921600 baud:

| | PuTTY | Tera Term |
| --- | --- | --- |
| Port | Session: Connection type **Serial**, Serial line `COM5`, Speed `921600` | Setup → Serial port: `COM5`, speed `921600` |

The board recognises a terminal by its Enter key, which sends CR alone, and from then on serves it as one: it echoes what you type, Backspace erases, and its lines end with CR LF, so each answer starts at the left. The first line you type is echoed when you press Enter; from then on every key shows as you type. Type `info` for a first answer, `help` for the requests, and `events on` to see the board's events.

Opening the port, both terminals switch DTR and RTS on together. The kit's reset circuit ignores that: it resets the ESP32 only when one of the two is on without the other.

## The front panel

The board can have a front panel of its own: an **ESP32-2432S028R** display board, sold as the "CYD" (cheap yellow display), 2.8 inches, 320 × 240 pixels, with a resistive touch screen. It sits on a three-wire cable to the board's panel port and runs every application without a PC: choose an application, switch the output, set any value, start a sweep, measure THD or a spectrum, change the radio station, tune the receiver. The panel and the PC application work side by side; what one changes, the other shows.

The panel does two jobs, as the overview at the start of this manual shows: on its own it is the board's front panel, and on your WiFi it is the PC application's way to the board, without a USB cable (see [WiFi control](#wifi-control)). Everything about the panel is in this chapter; [the front panel guide](Panel.md) has the same with notes for building its firmware.

### What you need

- An **ESP32-2432S028R**. The common version with one micro-USB port has an ILI9341 display, the version with two ports (USB-C and micro-USB) an ST7789; the panel firmware finds out which at start-up and drives either.
- Three wires, and a 10 kΩ resistor (recommended).
- The board with this release's firmware: it has the panel port.

### Wiring

| CYD | | Audio-DSP-FX board |
| --- | --- | --- |
| GPIO22 (TX), on the CN1 or P3 connector | → | **GPIO37** (RX): pin IO37 of the ESP32 kit, not connected on the board |
| GPIO35 (RX), on the P3 connector | ← | **GPIO4** (TX): the pad of R36 ("MIDI out", not fitted), or pin IO4 of the kit |
| GND | — | GND |

Both sides are 3.3 V logic; no level shifter is needed. Put the 10 kΩ resistor between GPIO37 and 3.3 V on the board: GPIO37 has no pull-up of its own, and its line floats while the panel is off or restarting. The panel works without it too.

The panel needs 5 V, from its own USB port or from the board's 5 V if the board's supply is about 9 V or less (the panel draws up to about 250 mA; above 9 V the board's regulator gets hot). From the board, connect its +5 V directly to the CYD's USB 5 V line; the chapter [Mechanical design](#mechanical-design) shows where. A panel on its own supply lets its TX line go while the board is unplugged, so it does not keep the board half powered; a panel still on the firmware of release 1.1 did, and the board then started badly (see [Troubleshooting](#troubleshooting)).

### Flashing

Connect the CYD by USB; it usually shows up as "USB-SERIAL CH340 (COMn)". In the release's `firmware` folder:

```
.\Flash-Panel.cmd COM18
```

(or double-click `Flash-Panel.cmd` and type the port). It writes the panel's single image, `AudioDspLabPanel-merged.bin`, at address 0. The same image serves both CYD versions: at start-up the panel reads its display controller's ID and drives the ILI9341 or the ST7789 it finds. Only a CYD whose display gives no ID (its data line not wired) falls back to the image's own choice: there `.\Flash-Panel.cmd COM18 -ST7789` writes `AudioDspLabPanel-ST7789-merged.bin`, which falls back to the ST7789. The panel keeps one setting of its own, the brightness; flashing resets it to full. Flash the panel and the board from the same release: the panel speaks the board's protocol.

### Home and settings

![Home: the applications, the running one in teal, Pot control, and the knob strip](images/panel/home.png)

The panel connects by itself: "Connecting to the board" until the board answers, then the home screen. It follows the board live: whatever the pots, the PC application or a script changes appears on the panel at once. The seven keys start an application and open its page; the running one is teal. **Pot control** switches whether the board's pots set values.

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

![Analysis, Frequency Response: the last sweep with the cursor on one of its points, Start sweep in the plot's corner](images/panel/analysis.png)

**Analysis.** The key in the plot's corner names the part on show, **Resp** (the Frequency Response), **THD** or **Spec** (the Spectrum); a tap goes to the next. **Frequency Response:** the plot of the last sweep, the left channel teal and the right blue, from 20 Hz to 20 kHz, scaled to the 60 dB below its loudest point. **Start sweep** in the plot's corner starts a sweep and switches the output on; it reads **Stop** while the sweep runs, and the points measured stand beside it. The start and stop frequencies, the points per decade and the level are the tiles below.

![Analysis, THD while it measures: the spectrum below the keys; THD, THD+N and the levels of both channels in the tiles' place](images/panel/analysis-thd.png)

**THD** shows the spectrum of both channels below the keys, all 256 points from 20 Hz to 20 kHz, from 0 to −120 dBFS with a grid line every 20 dB (labelled −20, −60 and −100); each point is the strongest FFT bin in its stretch, so a harmonic stands as a spike of its own. While it measures, the plot grows into the room the tiles leave. **Start** measures continuously and switches the output on. While it measures, its values take the tiles' place: the tone's frequency, and THD and THD+N in % and the level of the left and the right channel; the number of results stands beside the keys. **Stop** brings the tiles back. A measurement started on the PC shows itself here.

![Analysis, Spectrum while it measures a 1.23 kHz tone on K2: the tone at −20 dBFS and its odd harmonics at 3.7, 6.2 and 8.6 kHz; the RMS and the strongest peak of each channel in the tiles' place](images/panel/analysis-spectrum.png)

**Spec** shows the spectrum in the same way. **Start** measures without a tone of its own and leaves the output as it is; while it measures, the tiles give way to the RMS of both channels and each channel's strongest peak with its frequency.

**The cursor.** Touch the plot, or move a finger along it: a line jumps to the nearest measured point, a dot marks it on each channel, and a box beside the line gives the point's frequency and both levels. The cursor stays where you leave it; during the next sweep it shows again as soon as the sweep reaches its point. (In the PC application, the mouse over the plot does the same.)

![The synthesizer's filter section: the response at the cutoff, dashed at the contour's peak](images/panel/synth-filter.png)

**Synth.** Keys choose a section of the synthesizer's 48 parameters: **Osc** (the three oscillators and the noise), **Filter**, **Env** (the loudness envelope), **LFO** (both LFOs, PWM and the filter's LFO), **Perf** (glide, bend, velocity, volume, the MIDI channel), or **All**. **Filter** draws the filter's response at middle C, solid at the cutoff and dashed where the contour takes it at its peak.

![The synthesizer's envelope section: loudness solid, the filter's contour dashed](images/panel/synth-envelope.png)

**Env** draws the loudness envelope, solid, and the filter's contour, dashed, from the key down through a held stretch to the end of the release, at one time scale.

![The receiver: the IF dial with the chosen digit underlined, the S-meter with the squelch's mark, the mode keys, the width keys with the passband sketch, the noise reduction keys with the CPU load](images/panel/receiver.png)

**Receiver.** The dial at the top tunes the receiver's **IF**, with a sign and five digits from 10 kHz down to 1 Hz. Tap a digit to choose it (it is underlined in teal, and its step shows on the left); **−** and **+** step it, and holding them repeats; or drag along the dial, one step for every 14 pixels, to the right to tune up. The panel moves the IF within ±21 kHz, and the PC's dial follows. Below the dial, the S-meter: a bar from −130 to 0 dBFS with the squelch's threshold as a yellow mark, the level and the AGC's gain (in FM the FM offset instead), and a dot beside **Sql** that is teal while the squelch is open and grey while it mutes. Then three rows of keys: the modes **Off**, **AM**, **USB**, **LSB**, **CW** and **FM**; four widths that suit the mode (USB and LSB 1.8k, 2.4k, 2.7k, 3k; CW 200, 300, 500, 1k; FM 6k, 8k, 10k, 11k; AM and Off 4k, 6k, 8k, 10k), with a sketch of the passband around the carrier (the line), ±6 kHz across; and **NR off**, **LMS** and **Spectral**, with the receiver's CPU load beside them (and the spectral noise reduction's gain). The other parameters are tiles below, a scroll away: volume, squelch, NR level and the filter first, the equalizer and the test tone last. The meter's widgets redraw five times a second.

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
| Touches land beside their target | Another CYD's touch calibration | Tap the corners and adjust `Config.h` (the front panel guide, "For developers") |
| Colours swapped, the picture garbled or upside down | The display gave no ID, and the image's fallback is the other controller (the USB log says `display: ... (not detected, build setting)`) | Flash the other image: `Flash-Panel.cmd COMn -ST7789` for the two-USB CYD, without `-ST7789` for the one-USB CYD |
| Two-USB CYD: picture right, but colours inverted (dark looks light, teal looks red) | That batch's ST7789 wants its colours inverted | Build with `DISPLAY_INVERT` set to 1 as well |
| Settings shows "WiFi: none" | No network stored on the board | Enter one in **WiFi networks …** (it serves the radio too) |
| Settings shows "WiFi: failed" | Neither network answers, or a 5 GHz-only network | The panel tries again every 30 s; use a 2.4 GHz network |
| "Network: …" does not connect | The panel's address changed, or the PC is on another network | Read its address in Settings or the WiFi dialog; try audio-dsp-lab.local |

## Mechanical design

The Audio-MiniLab as built: the Elektor board in a two-part 3D-printed box, the pots' shafts coming through its lid with knob caps in the knob colours (P1 blue, P2 green, P3 yellow, P4 red), and the front panel in a wedge-shaped case in front of it, its display tilted towards you. The parts were designed in Onshape; the files to print them are in the repository's `CAD` folder as STL.

![The box on its own, without the front panel: the knobs P1 to P4 on its lid, the jacks and the supply at its sides, SigmaStudio's USBi behind it on its cable](images/mechanical/photo-box.jpg)

![The complete Audio-MiniLab: the box standing in the front panel's case, the panel showing the synthesizer's envelopes](images/mechanical/photo-assembly.jpg)

![The Audio-MiniLab from above: the box with its four knobs, the front panel in its case in front of it, SigmaStudio's USBi on its cable](images/mechanical/photo-top.jpg)

![The Audio-MiniLab on the desk: the display slopes towards you, the jacks come out of the box's sides](images/mechanical/photo-front.jpg)

### The parts

| Part | File in `CAD` | Size (mm) | Printed in | What it does |
| --- | --- | --- | --- | --- |
| Box base | `Audio-Lab-DSP-Base.stl` | 108 × 71.5 × 22.5 | black | Holds the board on four stand-offs at its mounting holes; openings in its walls for the jacks, the USB socket and the two connectors |
| Box lid | `Audio-Lab-DSP-Top.stl` | 108 × 71.5 × 17.5 | black | Closes the box; four holes, each in a recess, for the pots' shafts |
| Panel case | `Audio-Lab-PanelCase.stl` | 111.4 × 136.9 × 39 | blue | A wedge that holds the CYD at about 27°, with a passage for the panel's cable in its back wall and a tray behind it in which the box stands |
| Panel front | `Audio-Lab-PanelFront.stl` | 109.7 × 61.7 × 3 | black | The front plate: the display's window and four holes for the screws that hold the CYD behind it |

![All the parts: the closed box (its panel connector in the end wall, labelled "Panel"), the panel case with the panel's cable and its 5-pin plug, and the front plate with the CYD screwed behind it](images/mechanical/photo-parts.jpg)

### The box

![The box's base: the four stand-offs for the board, the openings in its walls](images/mechanical/dsp-base.png)

![One long side of the base: the half-round openings at the top of the walls take the jacks, together with the lid's](images/mechanical/dsp-base-left.png)

![The base's other long side and an end wall with their openings](images/mechanical/dsp-base-right.png)

![The lid from outside: the four holes for the pots, each in a recess](images/mechanical/dsp-top.png)

![The lid from inside](images/mechanical/dsp-top-inside.png)

The board sits on the four stand-offs of the base, screwed to them through its mounting holes. The lid closes the box from above; the pots' shafts come through its four holes, and the knobs sit on them. The half-round openings at the top of the base's walls and at the lid's edges meet around the jacks K2 to K5.

![The box from above: the knobs P1 to P4 in their colours](images/mechanical/photo-knobs.jpg)

![The empty base: the panel connector (the 5-pin header at the top) and the USBi connector (bottom left)](images/mechanical/photo-box-empty.jpg)

![The board in the base: the wires from the board to the two connectors](images/mechanical/photo-box-board.jpg)

Two connectors in the base are wired to the board, so the front panel's cable and SigmaStudio's USBi plug into the box rather than into the board:

- the **panel connector**, a 5-pin header, for the front panel's cable: the board's panel port, GPIO37, GPIO4 and GND (see the front panel's [Wiring](#wiring)), and the board's +5 V, which supplies the panel;
- the **USBi connector**, a 3.5 mm audio socket, for SigmaStudio's USBi: the board's I2C header K1.

**Note: the USBi is connected through a 3.5 mm audio plug.** Its cable ends in a 3.5 mm audio plug that carries the three lines of the board's I2C header: SCL, SDA and GND. Plug it in only for working in SigmaStudio and pull it out afterwards: while the USBi sits on the I2C bus, the firmware cannot write the EEPROM.

### The front panel's case

![The panel case: the front opens at about 27° for the front plate; its back wall has the passage for the cable, and the tray behind it takes the box](images/mechanical/panel-case.png)

![The panel's front plate from above as it prints: its inside, against the CYD, with the display's window](images/mechanical/panel-front.png)

![The front plate from the bottom as it prints: its outer face, with the four countersunk holes for the CYD's screws](images/mechanical/panel-front-bottom.png)

![The panel case without its front plate, the box standing in its tray: the box's panel connector shows through the passage in the case's back wall; the panel's cable plugs in there](images/mechanical/photo-panel-empty.jpg)

The CYD sits behind the front plate, held by four countersunk screws, its display in the window. The plate closes the case's sloping front; the panel's cable runs through the passage in the case's back wall to the box's panel connector. The box stands in the tray behind the wedge, so the two parts form one unit, as in the photos. To put it together: stand the box in the tray, plug the panel's cable into the box's panel connector through the passage, and lay the front plate with the CYD into the case's sloping front.

**Note: the panel's supply.** The panel takes its +5 V from the board, through the cable from the panel connector. Connect the +5 V directly to the CYD's USB 5 V line: in the picture the red wire is soldered to that line at the diode just below the CYD's micro-USB socket, and the black wire is GND. The board then has to run from about 9 V or less, as the front panel's [Wiring](#wiring) explains.

![The CYD's back: the board's +5 V (red) soldered to the CYD's USB 5 V line at the diode below the micro-USB socket; black is GND](images/mechanical/cyd-5v.jpg)

### Printing

The STL files are in millimetres and lie as they print: the lid and the front plate with their visible face down, so the build plate's surface becomes their outside (the textured finish in the photos comes from a textured plate). The pictures of the parts in this chapter are drawn from the STL files with `tools/Render-Cad.py`; run it again after changing a part.

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
| THD reads a fundamental of a few dozen Hz far below −60 dBFS | Nothing returns on K2: no cable, or the output off | Connect K3 to K2 or the device; press **Output on** |
| THD or Spectrum shows `failed` | The DSP's serial output lost its clocks: the DSP runs another program (service mode) | Leave service mode and select **Analysis** again |
| Spectrum shows a strong line at 50 Hz and its odd multiples | Mains hum: a ground loop between the board, the device and the PC | Power the device and the board from the same outlet; try a USB isolator or a laptop on battery |
| Receiver: only noise from the generator's carrier, or levels some 28 dB off what the generator is set to | The cable is on the generator's sync output, or its main output is off: the sync output gives a square wave whose level does not follow the setting | Take the generator's main output and switch it on |
| Receiver: a station also shows on the other side of the I/Q centre | The image: the I/Q correction is not set (it is 0 after every start) | Tune a steady carrier to an IF near +10 kHz and press **Null image**; keep the result in a stored setting |
| Receiver: USB sounds like LSB, the tuning runs backwards | I and Q swapped on K2 | Choose the input **Q/I**, or swap the cables |
| Receiver: strong stations distort, extra carriers appear | The front end drives the ADCs into clipping: more than 2.98 Vpp (0 dBFS) at K2 | Less gain or an attenuator ahead of K2 or the front end; keep the signals together below about −6 dBFS |
| Receiver: stations or whistles that tune the wrong way, or show where nothing should be | Signals or the front end's products above about 21 kHz at K2 fold back into the band: the board does not limit it | Low-pass the front end's output above about 21 kHz |
| Receiver: the image null stops at about 56 dB | With I/Q, the station sits at an IF of exactly 12 kHz, where the front end's third-order products fall onto the image | Tune so that the station sits near +10 kHz |
| `diag capture` or `diag record` answers `busy` | The receiver holds the DSP's serial output | Select another application first |

## Technical notes

- **Signals from the ESP32.** The generator and the synthesizer compute their signal on the ESP32 at 48 kHz, 16 bits, and send it to the DSP over I2S; the DSP runs the radio's program for them (the equalizer, set flat, and the volume). The generator keeps its phase in 32 bits, so its frequency is exact to the 0.01 Hz set.
- **The DSP's core rate.** The SigmaStudio projects run the ADAU1701's core twice per 48 kHz sample (96 kHz). Every frequency the firmware computes (oscillators, filters) is computed for that rate; frequencies typed into SigmaStudio's own controls for these projects act twice as high.
- **The line input to the ESP32.** The analysis' DSP program sends both line inputs, untouched, to the DSP's serial output (MP6, with its clocks on MP10 and MP11), which the ESP32 receives on its second I2S port. THD and Spectrum take 8192 samples per channel from it, window them (Blackman-Harris) and transform them (an FFT of 5.86 Hz bins); four captures per channel are averaged. The board's own floor: about −97.5 dBFS RMS from 20 Hz to 20 kHz, about 0.016 % THD in a loopback at −7 dBFS.
- **The receiver's path.** The receiver's DSP program (the radio's, plus the serial output) sends ADC0 (I, or the IF) and ADC1 (Q) untouched to the DSP's serial output, which the ESP32 receives on its second I2S port, in blocks of 256 frames (5.33 ms). A task on core 0 runs the chain on each block: a down-converter with DC removal and the I/Q correction to 12 kHz complex, the channel filter as a partitioned FFT convolution (an FFT of 128 points; the direct form of 511 taps would take 40 % of the core), AGC and S-meter, demodulator, noise reduction, squelch, and an interpolation back to 48 kHz. The audio goes back to the DSP on the ESP32's first I2S port, as the generator's does. The ESP32 clocks both directions, so input and output never drift apart.
- **The clock.** With JP1 on ext. MCLK the ESP32 supplies 12.288 MHz (256 × 48 kHz). The radio drives the clock from its stream's rate instead (256 × 44.1 kHz for most stations), and the equalizer's filters are recomputed for it.
- **What the board stores** in the ESP32's flash: the WiFi networks, the stations, the analysis' Frequency Response reference, the power-up choice, and the copy of your SigmaStudio program. Flashing a firmware update with the flash tool keeps them; erasing the flash removes them.
- **The pots** are read by the ESP32 (12-bit, smoothed). The DSP sees them only through the ESP32's DACs in SigmaStudio mode.
- **The ESP32 kit.** The board takes an ESP32-PICO-KIT V4 (PICO-D4) or an ESP32-PICO-KIT-1 (PICO-V3). They differ in the pin that carries I2S audio to the DSP (GPIO18 or GPIO7); the firmware detects the kit and the window shows it beside the firmware version.
