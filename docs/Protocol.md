# Audio-DSP-Lab serial protocol

Protocol version 1, firmware 1.1.

The board speaks a line-based text protocol on its USB serial port. You can use it from the Audio-DSP-Lab application, from a serial terminal, or from your own scripts. [`tools/adl.py`](../tools/adl.py) is a small Python client.

## Contents

- [Port settings](#port-settings)
- [The panel port](#the-panel-port)
- [Lines](#lines)
- [Values](#values)
- [Errors](#errors)
- [Commands](#commands): [general](#general), [applications](#applications-and-parameters), [potentiometers and settings](#potentiometers-power-up-and-service-mode), [radio](#radio), [response analyzer](#response-analyzer), [synthesizer](#synthesizer), [SigmaStudio mode](#sigmastudio-mode), [diagnostics](#diagnostics)
- [Events](#events)
- [Parameters per application](#parameters-per-application)
- [A session](#a-session)

## Port settings

| Setting | Value |
| --- | --- |
| Baud rate | 921600 |
| Format | 8 data bits, no parity, 1 stop bit, no flow control |
| DTR, RTS | **both deasserted**: on the ESP32-PICO-KIT they drive the reset and boot circuit, so asserting them resets the board |
| Encoding | UTF-8 |
| Line end | LF (`\n`); a CR before it is ignored |

Only one program can own the port at a time: the application, a terminal or a script.

After a reset the ESP32's boot ROM prints a few lines at 115200 baud, which look like garbage at 921600. Then the firmware sends `! ready ...`. A host ignores every line that does not start with `ok`, `err`, `* ` or `! `.

## The panel port

A second port speaks the same protocol over a cable, for a front panel such as an ESP32 "CYD" display board: the board keeps running without a PC, and the panel and the PC application can work side by side.

| Setting | Value |
| --- | --- |
| Board side | UART2: RX on **GPIO37** (module pin 24, not connected on the board), TX on **GPIO4** (the pad of R36, "MIDI out", not fitted) |
| Wiring | panel TX to GPIO37, panel RX to GPIO4, and GND; 3.3 V logic on both sides |
| Baud rate | 115200, 8N1, no flow control |
| Pull-up | 10 kΩ from GPIO37 to 3.3 V recommended: GPIO37 is input-only and has none of its own, so the line floats while the panel is off or restarting |

Both ports are equal. Each has its own requests, its own `events on` and its own responses; the board answers each request on the port it came from, one request at a time. Events go to every port with events on, and a value one port sets reaches the other as a `! value` event, so both show the same. The port that set it has its `ok` and gets no echo. `! ready` goes to both ports.

Settings other than parameters (pot control, the power-up choice, stations, WiFi) are not reported across: a host that shows them reads them again when it needs them.

A panel that starts or restarts while the board runs may leave noise in front of its first request; it starts with `echo <word>`, as the PC application does, and repeats it until the answer comes back.

The synthesizer's MIDI port receives on GPIO38 only; GPIO4 belongs to the panel port.

### Over WiFi, through the panel

The front panel (docs/Panel.md) can pass a PC's requests on: it joins the board's WiFi networks and takes one PC on **TCP port 7000** (announced as `audio-dsp-lab.local`, mDNS service `_adl._tcp`). The lines are the same as on USB. The panel shares its port: the PC's requests wait their turn with the panel's own, each answer goes back to whoever asked, and events reach the PC if it sent `events on` (the panel answers that itself and keeps its own events). Values the panel sets reach the PC as `! value`, and values the PC sets reach the panel's screens. A second PC that connects takes the place of the first.

Two requests are for the panel's cable alone and are refused over the network: `wifi secrets` and `panel` with arguments.

| Request | Response | Notes |
| --- | --- | --- |
| `wifi secrets` | `* <slot> "<ssid>" "<password>"` for slots 0 and 1, then `ok` | **panel port only**: the panel joins the board's networks with them; refused on USB and over the network |
| `panel` | `ok <state> <address> <clients>` | the panel's network as it reported it: state `none`, `connecting`, `online`, `failed`; address or `-`; clients 0 or 1 |
| `panel <state> <address> <clients>` | as `panel` | panel port only: the panel reports its network |

## Lines

| Direction | Form | Meaning |
| --- | --- | --- |
| host → board | `<command> [arguments]` | a request, at most 320 bytes |
| board → host | `* <values>` | a data line of the response to the current request (zero or more) |
| board → host | `ok [values]` | the request succeeded; ends the response |
| board → host | `err <code> <message>` | the request failed; ends the response |
| board → host | `! <event> [values]` | an [event](#events), only between responses |

The board handles one request at a time, in the order they arrive, and answers each with exactly one response. A host can send its next request once it has read `ok` or `err`. An empty line asks nothing and gets no answer.

Arguments are separated by spaces. A text that contains spaces goes in double quotes; inside quotes, write `\"` for a quote and `\\` for a backslash. The board quotes every text it sends the same way.

Commands, application names, parameter names and words such as `on` are not case-sensitive.

**Resynchronising.** A host that gave up waiting for a response cannot tell a late response from the next one. It sends `echo <word>` with a word of its own and discards every line until `ok <word>` comes back.

## Values

| Kind | Written as | Example |
| --- | --- | --- |
| number | decimal with a point, at most 2 decimals; trailing zeros left out | `1000`, `-20`, `1234.5` |
| integer | whole number | `15` |
| switch | `on` or `off` (`1`/`0`, `true`/`false` are accepted) | `on` |
| choice | one of the parameter's names, or its index | `rectangle` or `1` |
| text | `"quoted"` | `"Deutschlandfunk"` |
| no value | `-` | |

The board keeps numbers in hundredths. A request with more decimals is rounded, and the `ok` line of a `set` reports the value the board really took.

## Errors

| Code | Meaning |
| --- | --- |
| `unknown-command` | no such command |
| `bad-argument` | an argument is missing, extra or malformed |
| `out-of-range` | the value lies outside what the parameter accepts; for `set`, the line names the range |
| `unknown-parameter` | the running application has no such parameter |
| `line-too-long` | the request was longer than 320 bytes |
| `unknown-application` | no such application |
| `eeprom-failed` | the DSP's EEPROM could not be written or read (is SigmaStudio's USBi still on the I2C header?) |
| `dsp-not-responding` | the DSP did not boot or does not answer |
| `service-mode` | refused while the DSP belongs to SigmaStudio |
| `dsp-write-failed` | writing a value into the DSP failed |
| `wrong-application` | the command needs another application to be running |
| `no-sweep` | there is no complete sweep to keep as the reference |
| `store-failed` | the ESP32's flash (NVS) refused the write |
| `no-clock` | no clock arrives on the DSP's serial output (MP10, MP11) |

## Commands

### General

| Request | Response | Notes |
| --- | --- | --- |
| `help` | `* <usage>` per command, `ok` | |
| `ping` | `ok` | |
| `echo <word>` | `ok <word>` | for resynchronising |
| `info` | `ok Audio-DSP-Lab <firmware> <protocol> <kit>` | e.g. `ok Audio-DSP-Lab 1.1 1 esp32-pico-kit`; kit `esp32-pico-kit` (PICO-D4) or `esp32-pico-kit-1` (PICO-V3) |
| `events [on\|off]` | `ok on\|off` | [events](#events) are off after every reset |
| `status` | `ok <application> <state> <last-error>` | state: `idle`, `loading`, `running`, `failed`, `service`; last error: an error code or `none` |
| `led [on\|off]` | `ok on\|off` | the ESP32 kit's LED |
| `reset` | `ok`, then the board restarts | |

### Applications and parameters

| Request | Response | Notes |
| --- | --- | --- |
| `apps` | `* <name> <id> "<title>"` per application, `ok` | |
| `app` | as `status` | |
| `app <name>` | `ok <name> <state>` | writes the application's DSP program into the EEPROM (if it is not there yet), boots the DSP and sets every parameter to its default. Takes 0.1 to 3 s. Outputs are off after the start. |
| `params` | `* <name> <kind> <value> <min> <max> <default> <unit> <choices> <pot>` per parameter, `ok` | kind: `number`, `integer`, `switch`, `choice`; unit `-` for none; choices comma-separated, `-` for none; pot `P1` to `P4` or `-` |
| `get` | `* <name> <value>` per parameter, `ok` | |
| `get <name>` | `ok <value>` | |
| `set <name> <value>` | `ok <value>` | the value as the board took it |

Applications:

| Name | Id | Application |
| --- | --- | --- |
| `generator` | 1 | signal generator: sine, rectangle, triangle, saw, inverse saw, noise on K3 and K4 |
| `equalizer` | 2 | three-band equalizer from the line input K2 to K3 and K4 |
| `radio` | 3 | internet radio over WiFi, through the equalizer |
| `analyzer` | 4 | frequency response analyzer: sine out on K3/K4, measured on K2 |
| `sigmastudio` | 5 | your own SigmaStudio program from the EEPROM |
| `synthesizer` | 6 | monophonic synthesizer played from MIDI on K5 |

### Potentiometers, power-up and service mode

| Request | Response | Notes |
| --- | --- | --- |
| `pots` | `ok <p1> <p2> <p3> <p4>` | 0 to 4095 from stop to stop (the readings scaled to the pots' real travel) |
| `potctl [on\|off]` | `ok on\|off` | whether the pots set parameters; off after every reset. Turning it on takes the pots' positions at once: each pot-driven parameter goes to where its pot stands (a `! value` event for each that moves). After a `set`, the parameter's pot takes over again once it is turned by about 2 %. |
| `startup [on\|off]` | `ok on\|off <application>` | off: every power-up starts the SigmaStudio mode; on: it starts the application selected last (radio and synthesizer with their output on) |
| `service [on\|off]` | `ok on\|off` | on: the DSP keeps running, the EEPROM is writable and the firmware leaves the I2C bus to SigmaStudio and the USBi; off: the application is written back and restarted |

### Radio

| Request | Response | Notes |
| --- | --- | --- |
| `radio` | `ok <state> <sample-rate> "<title>"` | state: `idle`, `connecting-wifi`, `connecting-stream`, `playing`, `no-network`, `stream-error` |
| `stations` | `* <slot> "<name>" "<url>"` for slots 0 to 7, `ok` | |
| `station <slot> name "<text>"` | `ok` | 1 to 63 bytes of UTF-8 |
| `station <slot> url "<url>"` | `ok` | `http://` only (no `https://`), MP3 streams, up to 200 characters |
| `station <slot> default` | `ok` | the slot's default name and URL |
| `station all default` | `ok` | every slot |
| `wifi` | `* <slot> "<ssid>" <has-password>` for slots 0 and 1, then `ok <connected-slot> <ip>` | has-password: `yes`/`no`; `- -` while not connected. Passwords are never sent back. |
| `wifi <slot> "<ssid>" "<password>"` | `ok` | SSID 1 to 32 bytes; password 8 to 63 characters (WPA2), or left out for an open network |
| `wifi <slot> clear` | `ok` | |

The station that plays is the radio's parameter `station`. The radio tries network 0 first, then network 1. Stations and networks are kept in the ESP32's flash.

### Response analyzer

| Request | Response | Notes |
| --- | --- | --- |
| `sweep` | `ok <state> <measured> <total> <reference-points>` | state: `idle`, `running`, `done`, `stopped`, `failed` |
| `sweep start` | as `sweep` | sweeps from `start` to `stop` with `points-per-decade`; switches the output on |
| `sweep stop` | as `sweep` | keeps the points measured so far |
| `points [<from>]` | `* <index> <hz> <left> <right> <rel-left> <rel-right>` per point, `ok` | levels in dBFS; relative levels in dB against the reference, `-` without one |
| `reference` | `* <index> <hz> <left> <right>` per point, `ok` | |
| `reference keep` | `ok` | the last complete sweep becomes the reference (kept in flash) |
| `level` | `ok <left> <right>` | the input levels at the tone playing now, in dBFS |

### Synthesizer

| Request | Response | Notes |
| --- | --- | --- |
| `voice` | `ok <note> <gate> <held> <channel> <mod-strip> <stage>` | note: MIDI number (60 = C4) or `-`; gate `on`/`off`; held: keys held; mod strip 0 to 127; stage `idle`, `attack`, `decay`, `sustain`, `release` |
| `midi` | `ok <note-messages> <other-channels> <bytes> <uart-errors>` | counts since the synthesizer started |
| `load` | `ok <average> <peak>` | the audio task's share of its time budget in %, since the last `load` |

### SigmaStudio mode

| Request | Response | Notes |
| --- | --- | --- |
| `aux` | `ok <adc0> <adc1> <adc2> <adc3> on\|off` | the 8-bit values the ESP32's DACs last gave the DSP's auxiliary ADC inputs, and whether forwarding runs |
| `backup` | `ok yes\|no <length> <crc32>` | the copy of your SigmaStudio program the firmware keeps in its flash |

### Diagnostics

| Request | Response |
| --- | --- |
| `diag boot` | `ok <reason> <code> <lines> <failures> <recovered> <first-ms> <first-address> <first-lines> <clocks>`: the last start and the EEPROM reads since: the reset reason (`power-on`, `external`, `software`, `panic`, `watchdog`, `brownout`, `other`) and the chip's own code (`0x01` a clean power-on; `0x07` the boot watchdog restarted a boot that hung, as an unclean power-up gives it); the I2C lines before the firmware took them (bit 0 SDA, bit 1 SCL, high); EEPROM reads that failed, and how many of them a retry after freeing the bus cured; the first failure: ms after the start, EEPROM address, the lines then, and the clocks that freed SDA |
| `diag dsp <address>` | `ok <word> <hex>`: one word of the DSP's parameter RAM |
| `diag dspset <address> <word>` | `ok`: safeload one raw word |
| `diag eeprom <address> <count>` | `ok <hex bytes>`: up to 64 bytes of the EEPROM |
| `diag scan` | `ok <result> <length> firmware-image\|own-program <crc32> <stopped-at>`: the EEPROM program as the backup sees it |
| `diag tone on\|off` | `ok`: the radio plays a 1 kHz test tone instead of the stream |
| `diag meter` | `ok <pcm-bytes> <peak> <mp3-bytes> <stage> <loops> <task-state>`: the radio task since the last call |
| `diag raw` | `ok <left> <right>`: the analyzer's raw capture words (5.19) |
| `diag heap` | `ok <free> <largest-block> <lowest-free>`: the ESP32's heap in bytes: free now, the largest block one allocation can get, the lowest free since the start |
| `diag clocks` | `ok <lrclk-hz> <bclk-hz> <data-edges> <data-high>`: the DSP's serial output clocks (MP10 on GPIO13, MP11 on GPIO19), counted with the ESP32's pulse counter (the program images give about 48000 and 3072000; the DSP's sample rate shows its MCLK), the rising edges a second on the data pin (MP6 on GPIO32; 0 while the program sends nothing there) and the share of readings that found it high, in % |
| `diag capture [<frames>]` | `ok <rate> <frames> <rms-L> <peak-L> <hz-L> <rms-R> <peak-R> <hz-R> <first-word> <dc-L> <dc-R>`: the DSP's serial output (MP6 on GPIO32) as the ESP32's second I2S port receives it, 9728 frames (0.2 s) or 256 to 48000: the frame rate timed by the ESP32's clock, per channel the RMS without the DC and the peak from it in dBFS (a full-scale sine: -3.01 RMS, 0 peak; -200 for silence) and the frequency of the crossings of the DC, the first left slot as received, and per channel the DC as a fraction of full scale; `err no-clock` without clocks. It uses no I2C, so it also works in service mode |
| `diag record [<frames>]` | `* <frame> ...` 16 frames a line, each the left and right sample as 6 hex digits (24 bits, two's complement), then `ok <rate> <frames>`: 4096 contiguous frames (or 256 to 4096, rounded up to a multiple of 256) of the serial output |

Addresses and words may be written in hexadecimal with `0x`.

## Events

Events come only after `events on`, except `! ready`, which every start sends. They tell the host what changed without being asked, so a host does not need to poll.

| Event | When |
| --- | --- |
| `! ready Audio-DSP-Lab <firmware> <protocol>` | the board has started (power-up, reset, `reset`) |
| `! status <application> <state> <last-error>` | the application, its state or its last error changed |
| `! value <name> <value>` | a parameter changed without a `set` on this port: a pot turned it, the generator's frequency followed a new P2 range, or the other port set it. At most 20 per second for each parameter. |
| `! pots <p1> <p2> <p3> <p4>` | a pot moved by 80 counts (2 %) or more; at most 10 per second |
| `! radio <state> <sample-rate>` | the radio's connection or sample rate changed |
| `! wifi` | the WiFi networks were changed (the panel reads them again) |
| `! panel <state> <address> <clients>` | the front panel's network changed |
| `! title "<text>"` | the stream title changed |
| `! point <index> <hz> <left> <right> <rel-left> <rel-right>` | the analyzer measured a point |
| `! sweep <state> <measured> <total> <reference-points>` | the sweep progressed or ended, or a reference was kept |
| `! voice <note> <gate> <held> <channel> <mod-strip> <stage>` | the synthesizer's voice changed; at most 20 per second |

## Parameters per application

`params` is the authoritative list; this table gives the firmware 1.1 ranges.

### generator

| Name | Kind | Range | Default | Pot |
| --- | --- | --- | --- | --- |
| `waveform` | choice | `sine`, `rectangle`, `triangle`, `saw`, `inverse-saw`, `noise` | `sine` | P1 |
| `frequency` | number, Hz | 20 to 20000 | 1000 | P2 |
| `amplitude` | number, dBFS | -60 to 0 | -20 | P4 |
| `output` | switch | | off | |
| `p2-range` | choice | `20-200`, `200-2k`, `2k-20k`: the decade P2 sweeps | `200-2k` | |
| `width` | integer, % | 5 to 95: the rectangle's high part, the triangle's rise | 50 | P3 |

### equalizer and radio

| Name | Kind | Range | Default | Pot |
| --- | --- | --- | --- | --- |
| `bass-gain` | number, dB | -12 to 12 | 0 | P1 |
| `bass-frequency` | number, Hz | 50 to 400 | 200 | |
| `middle-gain` | number, dB | -12 to 12 | 0 | P2 |
| `middle-frequency` | number, Hz | 300 to 5000 | 1000 | |
| `treble-gain` | number, dB | -12 to 12 | 0 | P3 |
| `treble-frequency` | number, Hz | 500 to 8000 | 2000 | |
| `volume` | number, dB | -60 to 0 | -20 | P4 |
| `output` | switch | | off | |
| `station` | integer (radio only) | 0 to 7 | 0 | |

### analyzer

| Name | Kind | Range | Default | Pot |
| --- | --- | --- | --- | --- |
| `level` | number, dBFS | -60 to 0 | -20 | P2 |
| `output` | switch | | off | |
| `start` | number, Hz | 20 to 20000 | 20 | |
| `stop` | number, Hz | 20 to 20000 | 20000 | |
| `points-per-decade` | integer | 1 to 40 | 10 | |
| `tone` | number, Hz | 20 to 20000 | 1000 | |

### synthesizer

| Name | Kind | Range | Default | Pot |
| --- | --- | --- | --- | --- |
| `osc1-waveform`, `osc2-waveform`, `osc3-waveform` | choice | `sine`, `saw`, `rectangle`, `triangle` | `saw` | |
| `osc1-octave`, `osc2-octave`, `osc3-octave` | integer | -2 to 2 | 0 | |
| `osc1-level` | integer, % | 0 to 100 | 100 | |
| `osc2-level`, `osc3-level` | integer, % | 0 to 100 | 0 | |
| `osc2-detune`, `osc3-detune` | integer, cents | -700 to 700 | 0 | |
| `osc1-width` | integer, % | 5 to 95 | 50 | P1 |
| `osc2-width`, `osc3-width` | integer, % | 5 to 95 | 50 | |
| `osc1-symmetry` … `osc3-symmetry` | integer, % | 10 to 90 | 50 | |
| `noise-level` | integer, % | 0 to 100 | 0 | |
| `volume` | number, dB | -60 to 0 | -20 | P4 |
| `output` | switch | | off | |
| `channel` | integer | MIDI channel 1 to 16 | 15 | |
| `lfo1-rate`, `lfo2-rate` | number, Hz | 0.1 to 20 | 5, 4 | |
| `lfo1-waveform`, `lfo2-waveform` | choice | `sine`, `triangle`, `square` | `triangle`, `sine` | |
| `lfo1-depth` | integer, cents | vibrato 0 to 1200 at full mod strip | 50 | |
| `lfo2-depth` | integer, % | tremolo 0 to 100 | 0 | |
| `lfo1-width`, `lfo2-width` | integer, % | 5 to 95 | 50 | |
| `lfo1-symmetry`, `lfo2-symmetry` | integer, % | 0 to 100 | 50 | |
| `pwm-depth` | integer, % | 0 to 100 (100 = ±45 points of width from LFO 2) | 0 | |
| `attack` | integer, ms | 1 to 10000 | 5 | |
| `decay` | integer, ms | 1 to 10000 | 200 | |
| `sustain` | integer, % | 0 to 100 | 100 | |
| `release` | integer, ms | 1 to 10000 | 100 | |
| `cutoff` | integer, Hz | 20 to 20000 | 20000 | P2 |
| `emphasis` | integer, % | 0 to 100 (100 rings) | 0 | P3 |
| `contour-amount` | integer, % | 0 to 100 (100 = +5 octaves) | 0 | |
| `contour-attack` | integer, ms | 1 to 10000 | 5 | |
| `contour-decay` | integer, ms | 1 to 10000 (also the release) | 300 | |
| `contour-sustain` | integer, % | 0 to 100 | 50 | |
| `key-tracking` | integer, % | 0 to 100 | 0 | |
| `filter-lfo` | integer, % | 0 to 100 (LFO 1 into the cutoff, 100 = ±2 octaves) | 0 | |
| `glide` | integer, ms | 0 to 5000, 0 off | 0 | |
| `bend-range` | integer, semitones | 0 to 12 | 2 | |
| `velocity-loudness` | integer, % | 0 to 100 | 0 | |
| `velocity-brightness` | integer, % | 0 to 100 (100 = +3 octaves) | 0 | |

The SigmaStudio mode has no parameters.

## A session

```
> info
< ok Audio-DSP-Lab 1.1 1 esp32-pico-kit
> events on
< ok on
> app generator
< ok generator running
< ! status generator running none
> set frequency 440
< ok 440
> set amplitude -12.5
< ok -12.5
> set output on
< ok on
> potctl on
< ok on
< ! value waveform triangle       (the pots' positions, taken at once)
< ! value frequency 1021.43
< ! value amplitude -11.65
< ! value width 20
< ! pots 1500 2900 700 3600       (P4 turned)
< ! value amplitude -7.25
> set frequency 30000
< err out-of-range frequency 20..20000 Hz
```
