#include "Pots.h"
#include "Config.h"
#include <Arduino.h>
#include <Preferences.h>

// The pots' travel as the ADC sees it. On the Elektor board the wipers stop
// well below the ADC's full scale: measured on a stock board (2026-10-17),
// fully right read 3230..3370 of 4095 on all four pots. Readings are scaled
// so that POT_TOP_DEFAULT and above is the full 4095; ends stay reachable
// despite the noise. A board whose pots go clearly higher (above
// POT_LEARN_ABOVE) raises that pot's top to its highest reading less
// POT_LEARN_MARGIN, kept in NVS, so its full travel is used too.
#define POT_TOP_DEFAULT               3200
#define POT_LEARN_ABOVE               3600
#define POT_LEARN_MARGIN              64
#define POT_SAVE_STEP                 64      // NVS is written when a top rose this far
#define POT_STORE                     "adl-pots"

static const uint8_t potPins[POT_COUNT] = { PIN_POT1, PIN_POT2, PIN_POT3, PIN_POT4 };
static float filtered[POT_COUNT];
static uint16_t top[POT_COUNT];
static uint16_t savedTop[POT_COUNT];
static uint32_t lastSample = 0;

static void topKey(char *key, uint8_t index) {
  key[0] = 't';
  key[1] = (char)('0' + index);
  key[2] = '\0';
}

static void loadTops() {
  Preferences store;
  bool open = store.begin(POT_STORE, true);           // false when nothing was ever stored
  for (uint8_t i = 0; i < POT_COUNT; i++) {
    char key[3];
    topKey(key, i);
    top[i] = open ? store.getUShort(key, POT_TOP_DEFAULT) : POT_TOP_DEFAULT;
    if (top[i] < POT_TOP_DEFAULT || top[i] > 4095) top[i] = POT_TOP_DEFAULT;
    savedTop[i] = top[i];
  }
  if (open) store.end();
}

static void saveTop(uint8_t index) {
  Preferences store;
  if (!store.begin(POT_STORE, false)) return;
  char key[3];
  topKey(key, index);
  if (store.putUShort(key, top[index]) == sizeof(uint16_t)) savedTop[index] = top[index];
  store.end();
}

void potsInit() {
  analogReadResolution(12);
  loadTops();
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
    if (filtered[i] > POT_LEARN_ABOVE && filtered[i] - POT_LEARN_MARGIN > top[i]) {
      top[i] = (uint16_t)min(4095.0f, filtered[i] - POT_LEARN_MARGIN);
      if (top[i] - savedTop[i] >= POT_SAVE_STEP) saveTop(i);
    }
  }
  return true;
}

uint16_t potRaw(uint8_t index) {
  if (index >= POT_COUNT) return 0;
  const float scaled = filtered[index] * 4095.0f / top[index];
  if (scaled <= 0.0f) return 0;
  if (scaled >= 4095.0f) return 4095;
  return (uint16_t)(scaled + 0.5f);
}
