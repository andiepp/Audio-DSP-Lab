#include "ParamInfo.h"
#include "Applications.h"
#include <string.h>
#include <strings.h>

#define WAVEFORMS_GENERATOR           "sine,square,triangle,noise"
#define WAVEFORMS_SYNTH               "sine,saw,rectangle,triangle"
#define WAVEFORMS_LFO                 "sine,triangle,square"

// application 0x01 - signal generator
static const ParamInfo generatorInfo[] = {
  { 1, "waveform",          KIND_CHOICE,  0, "",     WAVEFORMS_GENERATOR },
  { 2, "frequency",         KIND_NUMBER,  2, "Hz",   nullptr },
  { 3, "amplitude",         KIND_NUMBER,  2, "dBFS", nullptr },
  { 4, "output",            KIND_SWITCH,  0, "",     nullptr },
  { 5, "p1-range",          KIND_CHOICE,  0, "",     "20-200,200-2k,2k-20k" },
};

// application 0x02 - equalizer; the radio (0x03) adds its station
static const ParamInfo equalizerInfo[] = {
  { 1, "bass-gain",         KIND_NUMBER,  2, "dB",   nullptr },
  { 2, "bass-frequency",    KIND_NUMBER,  2, "Hz",   nullptr },
  { 3, "middle-gain",       KIND_NUMBER,  2, "dB",   nullptr },
  { 4, "middle-frequency",  KIND_NUMBER,  2, "Hz",   nullptr },
  { 5, "treble-gain",       KIND_NUMBER,  2, "dB",   nullptr },
  { 6, "treble-frequency",  KIND_NUMBER,  2, "Hz",   nullptr },
  { 7, "volume",            KIND_NUMBER,  2, "dB",   nullptr },
  { 8, "output",            KIND_SWITCH,  0, "",     nullptr },
  { 9, "station",           KIND_INTEGER, 0, "",     nullptr },
};

// application 0x04 - response analyzer
static const ParamInfo analyzerInfo[] = {
  { 1, "level",             KIND_NUMBER,  2, "dBFS", nullptr },
  { 2, "output",            KIND_SWITCH,  0, "",     nullptr },
  { 3, "start",             KIND_NUMBER,  2, "Hz",   nullptr },
  { 4, "stop",              KIND_NUMBER,  2, "Hz",   nullptr },
  { 5, "points-per-decade", KIND_INTEGER, 0, "",     nullptr },
  { 6, "tone",              KIND_NUMBER,  2, "Hz",   nullptr },
};

// application 0x06 - synthesizer
static const ParamInfo synthInfo[] = {
  { 1,  "osc1-waveform",       KIND_CHOICE,  0, "",          WAVEFORMS_SYNTH },
  { 2,  "volume",              KIND_NUMBER,  2, "dB",        nullptr },
  { 3,  "output",              KIND_SWITCH,  0, "",          nullptr },
  { 4,  "channel",             KIND_INTEGER, 0, "",          nullptr },
  { 5,  "osc1-octave",         KIND_INTEGER, 0, "",          nullptr },
  { 6,  "osc1-level",          KIND_INTEGER, 0, "%",         nullptr },
  { 7,  "osc2-waveform",       KIND_CHOICE,  0, "",          WAVEFORMS_SYNTH },
  { 8,  "osc2-octave",         KIND_INTEGER, 0, "",          nullptr },
  { 9,  "osc2-detune",         KIND_INTEGER, 0, "cents",     nullptr },
  { 10, "osc2-level",          KIND_INTEGER, 0, "%",         nullptr },
  { 11, "osc3-waveform",       KIND_CHOICE,  0, "",          WAVEFORMS_SYNTH },
  { 12, "osc3-octave",         KIND_INTEGER, 0, "",          nullptr },
  { 13, "osc3-detune",         KIND_INTEGER, 0, "cents",     nullptr },
  { 14, "osc3-level",          KIND_INTEGER, 0, "%",         nullptr },
  { 15, "noise-level",         KIND_INTEGER, 0, "%",         nullptr },
  { 16, "lfo1-rate",           KIND_NUMBER,  2, "Hz",        nullptr },
  { 17, "lfo1-waveform",       KIND_CHOICE,  0, "",          WAVEFORMS_LFO },
  { 18, "lfo1-depth",          KIND_INTEGER, 0, "cents",     nullptr },
  { 19, "lfo2-rate",           KIND_NUMBER,  2, "Hz",        nullptr },
  { 20, "lfo2-waveform",       KIND_CHOICE,  0, "",          WAVEFORMS_LFO },
  { 21, "lfo2-depth",          KIND_INTEGER, 0, "%",         nullptr },
  { 22, "attack",              KIND_INTEGER, 0, "ms",        nullptr },
  { 23, "decay",               KIND_INTEGER, 0, "ms",        nullptr },
  { 24, "sustain",             KIND_INTEGER, 0, "%",         nullptr },
  { 25, "release",             KIND_INTEGER, 0, "ms",        nullptr },
  { 26, "cutoff",              KIND_INTEGER, 0, "Hz",        nullptr },
  { 27, "emphasis",            KIND_INTEGER, 0, "%",         nullptr },
  { 28, "contour-amount",      KIND_INTEGER, 0, "%",         nullptr },
  { 29, "contour-attack",      KIND_INTEGER, 0, "ms",        nullptr },
  { 30, "contour-decay",       KIND_INTEGER, 0, "ms",        nullptr },
  { 31, "contour-sustain",     KIND_INTEGER, 0, "%",         nullptr },
  { 32, "key-tracking",        KIND_INTEGER, 0, "%",         nullptr },
  { 33, "glide",               KIND_INTEGER, 0, "ms",        nullptr },
  { 34, "bend-range",          KIND_INTEGER, 0, "semitones", nullptr },
  { 35, "velocity-loudness",   KIND_INTEGER, 0, "%",         nullptr },
  { 36, "velocity-brightness", KIND_INTEGER, 0, "%",         nullptr },
  { 37, "filter-lfo",          KIND_INTEGER, 0, "%",         nullptr },
  { 38, "osc1-width",          KIND_INTEGER, 0, "%",         nullptr },
  { 39, "osc2-width",          KIND_INTEGER, 0, "%",         nullptr },
  { 40, "osc3-width",          KIND_INTEGER, 0, "%",         nullptr },
  { 41, "osc1-symmetry",       KIND_INTEGER, 0, "%",         nullptr },
  { 42, "osc2-symmetry",       KIND_INTEGER, 0, "%",         nullptr },
  { 43, "osc3-symmetry",       KIND_INTEGER, 0, "%",         nullptr },
  { 44, "lfo1-width",          KIND_INTEGER, 0, "%",         nullptr },
  { 45, "lfo2-width",          KIND_INTEGER, 0, "%",         nullptr },
  { 46, "lfo1-symmetry",       KIND_INTEGER, 0, "%",         nullptr },
  { 47, "lfo2-symmetry",       KIND_INTEGER, 0, "%",         nullptr },
  { 48, "pwm-depth",           KIND_INTEGER, 0, "%",         nullptr },
};

#define COUNT(table) (sizeof(table) / sizeof(table[0]))

struct AppTable {
  uint8_t appId;
  const char *token;
  const ParamInfo *info;
  uint8_t count;
};

static const AppTable tables[] = {
  { 0x01,            "generator",   generatorInfo, COUNT(generatorInfo) },
  { 0x02,            "equalizer",   equalizerInfo, COUNT(equalizerInfo) - 1 },  // without the station
  { 0x03,            "radio",       equalizerInfo, COUNT(equalizerInfo) },
  { APP_RESPONSE,    "analyzer",    analyzerInfo,  COUNT(analyzerInfo) },
  { APP_SIGMASTUDIO, "sigmastudio", nullptr,       0 },
  { APP_SYNTH,       "synthesizer", synthInfo,     COUNT(synthInfo) },
};

static const AppTable *tableOf(uint8_t appId) {
  for (const AppTable &table : tables) {
    if (table.appId == appId) return &table;
  }
  return nullptr;
}

const char *appToken(uint8_t appId) {
  const AppTable *table = tableOf(appId);
  return table != nullptr ? table->token : nullptr;
}

uint8_t appFromToken(const char *token) {
  for (const AppTable &table : tables) {
    if (strcasecmp(table.token, token) == 0) return table.appId;
  }
  return APP_NONE;
}

const ParamInfo *paramInfo(uint8_t appId, uint8_t paramId) {
  const AppTable *table = tableOf(appId);
  if (table == nullptr) return nullptr;
  for (uint8_t i = 0; i < table->count; i++) {
    if (table->info[i].id == paramId) return &table->info[i];
  }
  return nullptr;
}

const ParamInfo *paramInfoByName(uint8_t appId, const char *name) {
  const AppTable *table = tableOf(appId);
  if (table == nullptr) return nullptr;
  for (uint8_t i = 0; i < table->count; i++) {
    if (strcasecmp(table->info[i].name, name) == 0) return &table->info[i];
  }
  return nullptr;
}
