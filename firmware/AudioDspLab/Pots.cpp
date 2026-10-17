#include "Pots.h"
#include "Config.h"
#include <Arduino.h>

static const uint8_t potPins[POT_COUNT] = { PIN_POT1, PIN_POT2, PIN_POT3, PIN_POT4 };
static float filtered[POT_COUNT];
static uint32_t lastSample = 0;

void potsInit() {
  analogReadResolution(12);
  for (uint8_t i = 0; i < POT_COUNT; i++) {
    analogSetPinAttenuation(potPins[i], ADC_11db);
    filtered[i] = analogRead(potPins[i]);
  }
  lastSample = millis();
}

bool potsPoll() {
  if (millis() - lastSample < POT_SAMPLE_PERIOD_MS) return false;
  lastSample = millis();
  for (uint8_t i = 0; i < POT_COUNT; i++) {
    filtered[i] += 0.25f * (analogRead(potPins[i]) - filtered[i]);
  }
  return true;
}

uint16_t potRaw(uint8_t index) {
  if (index >= POT_COUNT) return 0;
  return (uint16_t)(filtered[index] + 0.5f);
}
