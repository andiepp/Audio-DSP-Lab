// ****************************************************************************
// ***                                                                      ***
// ***               A U D I O - M I N I L A B   M I D I                    ***
// ***                           monitor                                    ***
// ***                                                                      ***
// ****************************************************************************
//  Shows what arrives on the board's MIDI input, to check a cable or a keyboard.
//
//  Board:   Elektor Audio DSP FX (ESP32-PICO-D4 + ADAU1701 + 24C256)
//  Core:    ESP32 by Espressif Systems 3.3.10, board "ESP32 PICO-D4",
//           partition scheme Minimal SPIFFS (as AudioDspLab: same partition
//           table, so the SigmaStudio backup in 'spiffs' and the NVS stay)
//  MIDI in: K5 -> opto-coupler IC3 -> GPIO38, UART1 at 31250 8N1
//
//  A standalone sketch, flashed in place of AudioDspLab and replaced by it
//  again afterwards. It touches GPIO38 (RX) and GPIO4 (TX, idle high)
//  and nothing else; the DSP is not started (no MCLK, no sound).
//
//  USB serial 115200 8N1. Keys (send with Enter or without):
//    r  raw bytes on/off (hex, besides the decoded messages)
//    c  clock and active sensing printed one by one on/off
//    s  statistics (with the GPIO38 pin diagnostic)
//    p  GPIO38 pin diagnostic
//    z  statistics to zero
//    h  this help
// ----------------------------------------------------------------------------

#include <Arduino.h>
#include "soc/gpio_reg.h"

// --- configuration ----------------------------------------------------------
#define USB_BAUD_RATE         115200
#define MIDI_BAUD_RATE        31250
#define PIN_MIDI_IN           38      // input only; from IC3
#define PIN_MIDI_OUT          4       // R36 not fitted: goes nowhere
#define CLOCK_REPORT_MS       1000    // clock summary at most once a second
#define CLOCK_LOST_MS         500     // no clock for this long = stopped
#define SYSEX_SHOWN_BYTES     16

// --- state --------------------------------------------------------------------
static bool showRaw = false;
static bool showEveryClock = false;

static uint8_t runningStatus = 0;     // 0 = none
static uint8_t data[2];
static uint8_t dataCount = 0;
static uint8_t dataNeeded = 0;

static bool inSysEx = false;
static uint32_t sysExLength = 0;
static uint8_t sysExHead[SYSEX_SHOWN_BYTES];

struct Statistics {
  uint32_t bytes;
  uint32_t messages;
  uint32_t clocks;
  uint32_t activeSensing;
  uint32_t strayData;                 // data bytes without a status
  uint32_t uartErrors;
};
static Statistics stats;
static uint32_t uartErrorFlags[6];   // written by the UART event task

// GPIO38 sampled directly, apart from the UART: does the opto-coupler's
// output move at all? Idle is high; every MIDI byte pulls it low.
static uint32_t pinSamples = 0;
static uint32_t pinLowSamples = 0;
static uint32_t pinFallingEdges = 0;
static bool pinWasHigh = true;

// clock summary
static uint32_t clockCountSinceReport = 0;
static uint32_t clockFirstUs = 0;
static uint32_t clockLastUs = 0;
static uint32_t lastClockReportMs = 0;
static bool clockRunning = false;
static uint32_t lastActiveSensingMs = 0;
static bool activeSensingSeen = false;

// --- helpers ------------------------------------------------------------------
static const char *const NOTE_NAMES[12] = {
  "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
};

// MIDI note 60 = C4 (the ARTURIA KeyStep calls it C3 on its own display).
static void printNote(uint8_t note) {
  char text[8];
  snprintf(text, sizeof(text), "%s%d", NOTE_NAMES[note % 12], (int)(note / 12) - 1);
  Serial.printf("%3u %-4s", note, text);
}

static void printTime() {
  uint32_t ms = millis();
  Serial.printf("%7lu.%03lu  ", (unsigned long)(ms / 1000), (unsigned long)(ms % 1000));
}

static uint8_t dataBytesFor(uint8_t status) {
  switch (status & 0xF0) {
    case 0x80: case 0x90: case 0xA0: case 0xB0: case 0xE0: return 2;
    case 0xC0: case 0xD0: return 1;
  }
  switch (status) {
    case 0xF1: case 0xF3: return 1;     // MTC quarter frame, song select
    case 0xF2: return 2;                // song position
  }
  return 0;                             // F6 tune request, undefined F4/F5
}

static const char *controllerName(uint8_t cc) {
  switch (cc) {
    case 0:   return "bank select";
    case 1:   return "modulation";
    case 7:   return "volume";
    case 10:  return "pan";
    case 11:  return "expression";
    case 32:  return "bank select LSB";
    case 64:  return "sustain";
    case 120: return "all sound off";
    case 121: return "reset controllers";
    case 123: return "all notes off";
  }
  return "";
}

// --- decoded output -------------------------------------------------------------
static void printChannelMessage(uint8_t status, const uint8_t *d) {
  uint8_t channel = (status & 0x0F) + 1;
  printTime();
  Serial.printf("ch %2u  ", channel);
  switch (status & 0xF0) {
    case 0x80:
      Serial.print("Note off     ");
      printNote(d[0]);
      Serial.printf(" vel %3u\n", d[1]);
      break;
    case 0x90:
      if (d[1] == 0) {
        Serial.print("Note off     ");
        printNote(d[0]);
        Serial.print(" (note on, vel 0)\n");
      } else {
        Serial.print("Note on      ");
        printNote(d[0]);
        Serial.printf(" vel %3u\n", d[1]);
      }
      break;
    case 0xA0:
      Serial.print("Poly AT      ");
      printNote(d[0]);
      Serial.printf(" pressure %3u\n", d[1]);
      break;
    case 0xB0:
      Serial.printf("Control  %3u = %3u  %s\n", d[0], d[1], controllerName(d[0]));
      break;
    case 0xC0:
      Serial.printf("Program  %3u\n", d[0]);
      break;
    case 0xD0:
      Serial.printf("Channel AT   pressure %3u\n", d[0]);
      break;
    case 0xE0: {
      int bend = (int)(((uint16_t)d[1] << 7) | d[0]) - 8192;
      Serial.printf("Pitch bend   %+5d\n", bend);
      break;
    }
  }
}

static void printSystemCommon(uint8_t status, const uint8_t *d) {
  printTime();
  switch (status) {
    case 0xF1: Serial.printf("MTC quarter frame  %02X\n", d[0]); break;
    case 0xF2: Serial.printf("Song position  %u sixteenths\n", ((uint16_t)d[1] << 7) | d[0]); break;
    case 0xF3: Serial.printf("Song select  %u\n", d[0]); break;
    case 0xF6: Serial.print("Tune request\n"); break;
    default:   Serial.printf("Undefined system common %02X\n", status); break;
  }
}

static void endSysEx(bool terminated) {
  printTime();
  Serial.printf("SysEx  %lu bytes%s:", (unsigned long)sysExLength,
                terminated ? "" : " (cut short)");
  uint32_t shown = sysExLength < SYSEX_SHOWN_BYTES ? sysExLength : SYSEX_SHOWN_BYTES;
  for (uint32_t i = 0; i < shown; i++) Serial.printf(" %02X", sysExHead[i]);
  if (sysExLength > shown) Serial.print(" ...");
  Serial.print("\n");
  inSysEx = false;
  stats.messages++;
}

// --- clock -------------------------------------------------------------------------
static void onClock() {
  uint32_t now = micros();
  stats.clocks++;
  if (showEveryClock) {
    printTime();
    Serial.print("Clock\n");
  }
  if (!clockRunning) {
    clockRunning = true;
    clockCountSinceReport = 0;
    clockFirstUs = now;
    lastClockReportMs = millis();
    printTime();
    Serial.print("Clock running\n");
  } else {
    clockCountSinceReport++;
  }
  clockLastUs = now;
}

static void reportClock() {
  uint32_t nowMs = millis();
  if (clockRunning && (micros() - clockLastUs) > CLOCK_LOST_MS * 1000UL) {
    clockRunning = false;
    printTime();
    Serial.print("Clock stopped\n");
  }
  if (clockRunning && nowMs - lastClockReportMs >= CLOCK_REPORT_MS && clockCountSinceReport > 0) {
    // 24 clocks per quarter note
    float intervalUs = (float)(clockLastUs - clockFirstUs) / (float)clockCountSinceReport;
    float bpm = 60.0e6f / (intervalUs * 24.0f);
    if (!showEveryClock) {
      printTime();
      Serial.printf("Clock  %.1f BPM  (%lu clocks)\n", bpm, (unsigned long)clockCountSinceReport);
    }
    clockCountSinceReport = 0;
    clockFirstUs = clockLastUs;
    lastClockReportMs = nowMs;
  }
  if (activeSensingSeen && nowMs - lastActiveSensingMs > 300) {
    activeSensingSeen = false;
    printTime();
    Serial.print("Active sensing stopped\n");
  }
}

// --- realtime ------------------------------------------------------------------------
static void onRealtime(uint8_t status) {
  stats.messages++;
  switch (status) {
    case 0xF8: onClock(); return;
    case 0xFE:
      stats.activeSensing++;
      lastActiveSensingMs = millis();
      if (showEveryClock) {
        printTime();
        Serial.print("Active sensing\n");
      } else if (!activeSensingSeen) {
        printTime();
        Serial.print("Active sensing present\n");
      }
      activeSensingSeen = true;
      return;
  }
  printTime();
  switch (status) {
    case 0xFA: Serial.print("Start\n"); break;
    case 0xFB: Serial.print("Continue\n"); break;
    case 0xFC: Serial.print("Stop\n"); break;
    case 0xFF: Serial.print("System reset\n"); break;
    default:   Serial.printf("Undefined realtime %02X\n", status); break;
  }
}

// --- parser ----------------------------------------------------------------------------
static void onByte(uint8_t b) {
  stats.bytes++;

  if (b >= 0xF8) {                    // realtime: anywhere, changes nothing
    onRealtime(b);
    return;
  }

  if (b & 0x80) {                     // status byte
    if (inSysEx) endSysEx(b == 0xF7);
    if (b == 0xF7) return;            // EOX handled above (or stray)
    if (b == 0xF0) {
      inSysEx = true;
      sysExLength = 0;
      runningStatus = 0;
      return;
    }
    dataCount = 0;
    dataNeeded = dataBytesFor(b);
    if (b >= 0xF0) {                  // system common: clears running status
      runningStatus = 0;
      if (dataNeeded == 0) {
        stats.messages++;
        printSystemCommon(b, data);
      } else {
        runningStatus = b;            // held only until its data is complete
      }
      return;
    }
    runningStatus = b;
    return;
  }

  // data byte
  if (inSysEx) {
    if (sysExLength < SYSEX_SHOWN_BYTES) sysExHead[sysExLength] = b;
    sysExLength++;
    return;
  }
  if (runningStatus == 0) {
    stats.strayData++;
    return;
  }
  data[dataCount++] = b;
  if (dataCount < dataNeeded) return;
  dataCount = 0;
  stats.messages++;
  if (runningStatus >= 0xF0) {
    printSystemCommon(runningStatus, data);
    runningStatus = 0;
  } else {
    printChannelMessage(runningStatus, data);
  }
}

// --- GPIO38 pin diagnostic ----------------------------------------------------------------
static inline bool midiPinHigh() {
  return (REG_READ(GPIO_IN1_REG) >> (PIN_MIDI_IN - 32)) & 1;
}

static void samplePin() {
  bool high = midiPinHigh();
  pinSamples++;
  if (!high) {
    pinLowSamples++;
    if (pinWasHigh) pinFallingEdges++;
  }
  pinWasHigh = high;
}

static void printPin() {
  Serial.printf("GPIO38 now %s  samples %lu  low %lu  falling edges %lu\n",
                midiPinHigh() ? "HIGH" : "LOW", (unsigned long)pinSamples,
                (unsigned long)pinLowSamples, (unsigned long)pinFallingEdges);
}

// --- UART errors --------------------------------------------------------------------------
static void onUartError(hardwareSerial_error_t error) {
  if ((unsigned)error < 6) __atomic_fetch_add(&uartErrorFlags[error], 1, __ATOMIC_RELAXED);
}

static void reportUartErrors() {
  static const char *const NAMES[6] = {
    "none", "break", "buffer full", "FIFO overflow", "framing", "parity"
  };
  for (int i = 1; i < 6; i++) {
    uint32_t n = __atomic_exchange_n(&uartErrorFlags[i], 0, __ATOMIC_RELAXED);
    if (n == 0) continue;
    stats.uartErrors += n;
    printTime();
    Serial.printf("UART %s error x%lu\n", NAMES[i], (unsigned long)n);
  }
}

// --- keys -------------------------------------------------------------------------------------
static void printHelp() {
  Serial.print("\nAudio-DSP-Lab MIDI monitor\n"
               "MIDI in K5 -> GPIO38, 31250 8N1. Note 60 = C4.\n"
               "Keys: r raw bytes, c every clock, s statistics, p pin, z zero, h help\n");
  printPin();
  Serial.print("\n");
}

static void printStatistics() {
  Serial.printf("\nbytes %lu  messages %lu  clocks %lu  active sensing %lu  "
                "stray data %lu  UART errors %lu\n",
                (unsigned long)stats.bytes, (unsigned long)stats.messages,
                (unsigned long)stats.clocks, (unsigned long)stats.activeSensing,
                (unsigned long)stats.strayData, (unsigned long)stats.uartErrors);
  printPin();
  Serial.print("\n");
}

static void handleKeys() {
  while (Serial.available()) {
    switch (Serial.read()) {
      case 'r': showRaw = !showRaw;
                Serial.printf("raw bytes %s\n", showRaw ? "on" : "off"); break;
      case 'c': showEveryClock = !showEveryClock;
                Serial.printf("every clock %s\n", showEveryClock ? "on" : "off"); break;
      case 's': printStatistics(); break;
      case 'p': printPin(); break;
      case 'z': memset(&stats, 0, sizeof(stats));
                pinSamples = pinLowSamples = pinFallingEdges = 0;
                Serial.print("statistics zero\n"); break;
      case 'h': case '?': printHelp(); break;
    }
  }
}

// --- Arduino --------------------------------------------------------------------------------------
void setup() {
  Serial.begin(USB_BAUD_RATE);
  Serial1.setRxBufferSize(1024);
  Serial1.begin(MIDI_BAUD_RATE, SERIAL_8N1, PIN_MIDI_IN, PIN_MIDI_OUT);
  Serial1.onReceiveError(onUartError);
  delay(200);
  printHelp();
}

void loop() {
  samplePin();
  while (Serial1.available()) {
    uint8_t b = (uint8_t)Serial1.read();
    if (showRaw && !(b == 0xF8 && !showEveryClock) && !(b == 0xFE && !showEveryClock)) {
      printTime();
      Serial.printf("raw %02X\n", b);
    }
    onByte(b);
  }
  reportUartErrors();
  reportClock();
  handleKeys();
}

// ============================================================================
// ==================================  E O F  =================================
// ============================================================================
