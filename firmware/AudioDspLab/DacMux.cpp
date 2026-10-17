#include "DacMux.h"
#include "Config.h"
#include "Pots.h"
#include <Arduino.h>
#include <driver/dac_oneshot.h>

static dac_oneshot_handle_t dacX = nullptr;           // GPIO25
static dac_oneshot_handle_t dacY = nullptr;           // GPIO26
static bool running = false;
static bool selectHigh = false;
static uint32_t switchedAt = 0;
static uint8_t written[4];                            // ADC0..ADC3

// pot 0..4095 -> DAC 0..255
static uint8_t toDac(uint8_t pot) {
  return (uint8_t)(potRaw(pot) >> 4);
}

// one position: the select lines first, then both DACs (Elektor's order)
static void writePosition(bool high) {
  digitalWrite(PIN_DAC_MUX0, high ? HIGH : LOW);
  digitalWrite(PIN_DAC_MUX1, high ? HIGH : LOW);
  // LOW: X0 = ADC2 (P3), Y0 = ADC0 (P1); HIGH: X1 = ADC1 (P2), Y1 = ADC3 (P4)
  uint8_t inputX = high ? 1 : 2;
  uint8_t inputY = high ? 3 : 0;
  written[inputX] = toDac(inputX);
  written[inputY] = toDac(inputY);
  dac_oneshot_output_voltage(dacX, written[inputX]);
  dac_oneshot_output_voltage(dacY, written[inputY]);
  switchedAt = millis();
}

bool dacMuxBegin() {
  if (running) return true;
  dac_oneshot_config_t configX = { .chan_id = DAC_CHAN_0 };
  dac_oneshot_config_t configY = { .chan_id = DAC_CHAN_1 };
  if (dac_oneshot_new_channel(&configX, &dacX) != ESP_OK) return false;
  if (dac_oneshot_new_channel(&configY, &dacY) != ESP_OK) {
    dac_oneshot_del_channel(dacX);
    dacX = nullptr;
    return false;
  }
  pinMode(PIN_DAC_MUX0, OUTPUT);
  pinMode(PIN_DAC_MUX1, OUTPUT);
  running = true;
  // both positions once, so every input holds a value at once
  writePosition(true);
  delay(DAC_MUX_HOLD_MS);
  selectHigh = false;
  writePosition(false);
  return true;
}

void dacMuxEnd() {
  if (!running) return;
  running = false;
  dac_oneshot_del_channel(dacX);
  dac_oneshot_del_channel(dacY);
  dacX = dacY = nullptr;
  pinMode(PIN_DAC_MUX0, INPUT);
  pinMode(PIN_DAC_MUX1, INPUT);
}

void dacMuxPoll() {
  if (!running || millis() - switchedAt < DAC_MUX_HOLD_MS) return;
  selectHigh = !selectHigh;
  writePosition(selectHigh);
}

bool dacMuxRunning() {
  return running;
}

uint8_t dacMuxValue(uint8_t input) {
  return input < 4 ? written[input] : 0;
}
