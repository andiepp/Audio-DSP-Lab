#include "Protocol.h"
#include "Config.h"
#include "Errors.h"
#include "ParamInfo.h"
#include "Board.h"
#include "Applications.h"
#include "Pots.h"
#include "Dsp.h"
#include "Radio.h"
#include "Analyzer.h"
#include "DacMux.h"
#include "StudioBackup.h"
#include "Eeprom24.h"
#include "Synth.h"
#include "Capture.h"
#include <Arduino.h>
#include <esp_system.h>
#include <esp_rom_sys.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define APP_RADIO                     0x03
#define MAX_TOKENS                    8
#define OUTPUT_LINE_MAX               384
#define EVENT_CHECK_MS                20

// ----------------------------------------------------------------------------
//  Ports
// ----------------------------------------------------------------------------

// The USB port and the panel port (a second UART for a front panel such as
// a CYD display) speak the same protocol, each with its own request line,
// its own "events on" and its own queue of value events. A response goes
// to the port that asked; an event goes to every port with events on.
struct Port {
  HardwareSerial &serial;
  char line[PROTOCOL_LINE_MAX + 1];
  uint16_t lineLength;
  bool lineOverflow;
  bool eventsOn;
  uint64_t pendingValues;                    // bit = parameter index
  uint32_t valuesSentAt;
};

static Port ports[] = {
  { Serial, {}, 0, false, false, 0, 0 },
  { Serial2, {}, 0, false, false, 0, 0 },
};
static const uint8_t portCount = sizeof(ports) / sizeof(ports[0]);
static Port *out = &ports[0];               // the port whose request is answered
static bool broadcasting = false;           // events: to every port with events on

// ----------------------------------------------------------------------------
//  Output
// ----------------------------------------------------------------------------

static void writeTo(Port &port, const char *prefix, const char *text) {
  port.serial.print(prefix);
  port.serial.print(text);
  port.serial.print('\n');
}

static void writeLine(const char *prefix, const char *format, va_list args) {
  char text[OUTPUT_LINE_MAX];
  vsnprintf(text, sizeof(text), format, args);
  if (!broadcasting) {
    writeTo(*out, prefix, text);
    return;
  }
  for (uint8_t i = 0; i < portCount; i++) {
    if (ports[i].eventsOn) writeTo(ports[i], prefix, text);
  }
}

static void ok() {
  out->serial.print("ok\n");
}

static void okf(const char *format, ...) {
  va_list args;
  va_start(args, format);
  writeLine("ok ", format, args);
  va_end(args);
}

static void dataf(const char *format, ...) {
  va_list args;
  va_start(args, format);
  writeLine("* ", format, args);
  va_end(args);
}

static void eventf(const char *format, ...) {
  va_list args;
  va_start(args, format);
  writeLine("! ", format, args);
  va_end(args);
}

static const char *errorWord(uint8_t code) {
  switch (code) {
    case ERR_NONE:                return "none";
    case ERR_UNKNOWN_COMMAND:     return "unknown-command";
    case ERR_BAD_ARGUMENT:        return "bad-argument";
    case ERR_OUT_OF_RANGE:        return "out-of-range";
    case ERR_UNKNOWN_PARAMETER:   return "unknown-parameter";
    case ERR_LINE_TOO_LONG:       return "line-too-long";
    case ERR_UNKNOWN_APPLICATION: return "unknown-application";
    case ERR_EEPROM_FAILED:       return "eeprom-failed";
    case ERR_DSP_NOT_RESPONDING:  return "dsp-not-responding";
    case ERR_SERVICE_MODE:        return "service-mode";
    case ERR_DSP_WRITE_FAILED:    return "dsp-write-failed";
    case ERR_WRONG_APPLICATION:   return "wrong-application";
    case ERR_NO_SWEEP:            return "no-sweep";
    case ERR_STORE_FAILED:        return "store-failed";
    case ERR_NO_CLOCK:            return "no-clock";
    default:                      return "failed";
  }
}

static const char *errorText(uint8_t code) {
  switch (code) {
    case ERR_UNKNOWN_COMMAND:     return "no such command (try help)";
    case ERR_BAD_ARGUMENT:        return "missing or malformed argument";
    case ERR_OUT_OF_RANGE:        return "value out of range";
    case ERR_UNKNOWN_PARAMETER:   return "the running application has no such parameter";
    case ERR_LINE_TOO_LONG:       return "request too long";
    case ERR_UNKNOWN_APPLICATION: return "no such application";
    case ERR_EEPROM_FAILED:       return "EEPROM access failed (USBi still on the header?)";
    case ERR_DSP_NOT_RESPONDING:  return "the DSP does not answer";
    case ERR_SERVICE_MODE:        return "the DSP belongs to SigmaStudio now";
    case ERR_DSP_WRITE_FAILED:    return "writing to the DSP failed";
    case ERR_WRONG_APPLICATION:   return "not available in the running application";
    case ERR_NO_SWEEP:            return "no complete sweep to keep";
    case ERR_STORE_FAILED:        return "storing in flash failed";
    case ERR_NO_CLOCK:            return "no clock on the DSP's serial output (MP10, MP11)";
    default:                      return "failed";
  }
}

static void fail(uint8_t code) {
  out->serial.printf("err %s %s\n", errorWord(code), errorText(code));
}

// ok, or the error the module reported
static void result(uint8_t code) {
  if (code == ERR_NONE) ok();
  else fail(code);
}

// a text in double quotes; quote and backslash escaped, control characters
// written as spaces
static void quote(char *out, size_t size, const char *text) {
  size_t n = 0;
  if (size < 3) return;
  out[n++] = '"';
  for (const char *c = text; *c != '\0' && n + 3 < size; c++) {
    if (*c == '"' || *c == '\\') {
      out[n++] = '\\';
      out[n++] = *c;
    } else if ((uint8_t)*c < 0x20 || (uint8_t)*c == 0x7F) {
      out[n++] = ' ';
    } else {
      out[n++] = *c;
    }
  }
  out[n++] = '"';
  out[n] = '\0';
}

// ----------------------------------------------------------------------------
//  Values
// ----------------------------------------------------------------------------

static const int32_t powersOfTen[] = { 1, 10, 100, 1000 };

// a scaled integer as decimal text, trailing zeros dropped: 100050 with two
// decimals is "1000.5"
static void formatFixed(char *out, size_t size, int32_t value, uint8_t decimals) {
  if (decimals == 0 || decimals > 3) {
    snprintf(out, size, "%ld", (long)value);
    return;
  }
  const int32_t scale = powersOfTen[decimals];
  const bool negative = value < 0;
  const uint32_t magnitude = negative ? (uint32_t)(-(int64_t)value) : (uint32_t)value;
  uint32_t whole = magnitude / scale;
  uint32_t fraction = magnitude % scale;
  uint8_t digits = decimals;
  while (digits > 0 && fraction % 10 == 0) {
    fraction /= 10;
    digits--;
  }
  if (digits == 0) snprintf(out, size, "%s%lu", negative ? "-" : "", (unsigned long)whole);
  else snprintf(out, size, "%s%lu.%0*lu", negative ? "-" : "", (unsigned long)whole, digits, (unsigned long)fraction);
}

// decimal text -> scaled integer; more decimals than the scale holds are
// rounded half away from zero
static bool parseFixed(const char *text, uint8_t decimals, int32_t &value) {
  const char *c = text;
  bool negative = false;
  if (*c == '+' || *c == '-') negative = *c++ == '-';
  int64_t number = 0;
  uint8_t digitsSeen = 0;
  while (*c >= '0' && *c <= '9') {
    number = number * 10 + (*c++ - '0');
    digitsSeen++;
    if (number > 100000000000LL) return false;
  }
  uint8_t fractionDigits = 0;
  bool roundUp = false;
  if (*c == '.') {
    c++;
    while (*c >= '0' && *c <= '9') {
      if (fractionDigits < decimals) {
        number = number * 10 + (*c - '0');
        fractionDigits++;
      } else if (fractionDigits == decimals) {
        roundUp = *c >= '5';
        fractionDigits++;                    // later digits do not matter
      }
      c++;
      digitsSeen++;
    }
  }
  if (*c != '\0' || digitsSeen == 0) return false;
  for (uint8_t i = fractionDigits; i < decimals; i++) number *= 10;
  if (roundUp) number++;
  if (negative) number = -number;
  if (number < INT32_MIN || number > INT32_MAX) return false;
  value = (int32_t)number;
  return true;
}

static bool parseLong(const char *text, long minimum, long maximum, long &value) {
  if (text == nullptr || *text == '\0') return false;
  char *end;
  long number = strtol(text, &end, 0);       // decimal, or hexadecimal with 0x
  if (*end != '\0' || number < minimum || number > maximum) return false;
  value = number;
  return true;
}

static bool parseSwitch(const char *text, bool &on) {
  if (strcasecmp(text, "on") == 0 || strcmp(text, "1") == 0 || strcasecmp(text, "true") == 0) {
    on = true;
    return true;
  }
  if (strcasecmp(text, "off") == 0 || strcmp(text, "0") == 0 || strcasecmp(text, "false") == 0) {
    on = false;
    return true;
  }
  return false;
}

static const char *onOff(bool on) {
  return on ? "on" : "off";
}

// the index-th name of a comma separated list; false past its end
static bool choiceName(const char *choices, int32_t index, char *out, size_t size) {
  if (choices == nullptr || index < 0) return false;
  const char *start = choices;
  for (int32_t i = 0; i < index; i++) {
    start = strchr(start, ',');
    if (start == nullptr) return false;
    start++;
  }
  const char *end = strchr(start, ',');
  size_t length = end != nullptr ? (size_t)(end - start) : strlen(start);
  if (length + 1 > size) length = size - 1;
  memcpy(out, start, length);
  out[length] = '\0';
  return true;
}

static int32_t choiceCount(const char *choices) {
  if (choices == nullptr) return 0;
  int32_t count = 1;
  for (const char *c = choices; *c != '\0'; c++) if (*c == ',') count++;
  return count;
}

static void formatValue(char *out, size_t size, const ParamInfo *info, int32_t value) {
  if (info == nullptr) {
    snprintf(out, size, "%ld", (long)value);
    return;
  }
  switch (info->kind) {
    case KIND_NUMBER:
      formatFixed(out, size, value, info->decimals);
      return;
    case KIND_SWITCH:
      snprintf(out, size, "%s", onOff(value != 0));
      return;
    case KIND_CHOICE:
      if (choiceName(info->choices, value, out, size)) return;
      break;
    default:
      break;
  }
  snprintf(out, size, "%ld", (long)value);
}

static bool parseValue(const char *text, const ParamInfo *info, int32_t &value) {
  if (info == nullptr || info->kind == KIND_INTEGER) {
    long number;
    if (!parseLong(text, INT32_MIN, INT32_MAX, number)) return false;
    value = (int32_t)number;
    return true;
  }
  if (info->kind == KIND_NUMBER) return parseFixed(text, info->decimals, value);
  if (info->kind == KIND_SWITCH) {
    bool on;
    if (!parseSwitch(text, on)) return false;
    value = on ? 1 : 0;
    return true;
  }
  // a choice: by name, or by its index
  const int32_t count = choiceCount(info->choices);
  for (int32_t i = 0; i < count; i++) {
    char name[32];
    if (choiceName(info->choices, i, name, sizeof(name)) && strcasecmp(name, text) == 0) {
      value = i;
      return true;
    }
  }
  long number;
  if (!parseLong(text, 0, count - 1, number)) return false;
  value = (int32_t)number;
  return true;
}

static const char *kindWord(ParamKind kind) {
  switch (kind) {
    case KIND_NUMBER:  return "number";
    case KIND_SWITCH:  return "switch";
    case KIND_CHOICE:  return "choice";
    default:           return "integer";
  }
}

static const char *stateWord(AppState state) {
  switch (state) {
    case APP_STATE_LOADING: return "loading";
    case APP_STATE_RUNNING: return "running";
    case APP_STATE_FAILED:  return "failed";
    case APP_STATE_SERVICE: return "service";
    default:                return "idle";
  }
}

static const char *radioWord(RadioState state) {
  switch (state) {
    case RADIO_CONNECTING_WIFI:   return "connecting-wifi";
    case RADIO_CONNECTING_STREAM: return "connecting-stream";
    case RADIO_PLAYING:           return "playing";
    case RADIO_NO_NETWORK:        return "no-network";
    case RADIO_STREAM_ERROR:      return "stream-error";
    default:                      return "idle";
  }
}

static const char *sweepWord(SweepState state) {
  switch (state) {
    case SWEEP_RUNNING: return "running";
    case SWEEP_DONE:    return "done";
    case SWEEP_STOPPED: return "stopped";
    case SWEEP_FAILED:  return "failed";
    default:            return "idle";
  }
}

static const char *stageWord(uint8_t stage) {
  switch (stage) {
    case SYNTH_ATTACK:  return "attack";
    case SYNTH_DECAY:   return "decay";
    case SYNTH_SUSTAIN: return "sustain";
    case SYNTH_RELEASE: return "release";
    default:            return "idle";
  }
}

static const char *appWord(uint8_t id) {
  const char *token = appToken(id);
  return token != nullptr ? token : "none";
}

// 0.01 units, "-" for no value
static void formatLevel(char *out, size_t size, int32_t value) {
  if (value == ANALYZER_NO_VALUE) snprintf(out, size, "-");
  else formatFixed(out, size, value, 2);
}

// ----------------------------------------------------------------------------
//  Requests
// ----------------------------------------------------------------------------

static char *tokens[MAX_TOKENS];
static uint8_t tokenCount = 0;

// splits the line in place: words separated by spaces, or "quoted text"
// with \" and \\ inside; false on an open quote or too many words
static bool tokenize(char *text) {
  tokenCount = 0;
  char *read = text;
  while (true) {
    while (*read == ' ' || *read == '\t') read++;
    if (*read == '\0') return true;
    if (tokenCount == MAX_TOKENS) return false;
    char *write = read;
    tokens[tokenCount++] = write;
    if (*read == '"') {
      read++;
      while (*read != '"') {
        if (*read == '\0') return false;
        if (*read == '\\' && (read[1] == '"' || read[1] == '\\')) read++;
        *write++ = *read++;
      }
      read++;                                 // the closing quote
      if (*read != '\0' && *read != ' ' && *read != '\t') return false;
    } else {
      while (*read != '\0' && *read != ' ' && *read != '\t') *write++ = *read++;
    }
    bool end = *read == '\0';
    *write = '\0';
    if (end) return true;
    read++;
  }
}

static const char *arg(uint8_t index) {
  return index < tokenCount ? tokens[index] : nullptr;
}

static bool is(uint8_t index, const char *word) {
  return index < tokenCount && strcasecmp(tokens[index], word) == 0;
}

// "<command>" alone queries; "<command> on|off" sets
static bool switchArgument(bool &on) {
  if (tokenCount != 2) return false;
  return parseSwitch(tokens[1], on);
}

static bool runningApp(uint8_t id) {
  if (appRunning(id)) return true;
  fail(ERR_WRONG_APPLICATION);
  return false;
}

// --- general ------------------------------------------------------------------

static void cmdHelp();

static void cmdPing() {
  ok();
}

// "echo <word>" -> "ok <word>": a host that lost track of the responses
// sends a word of its own and reads until it comes back
static void cmdEcho() {
  if (tokenCount != 2) return fail(ERR_BAD_ARGUMENT);
  okf("%s", tokens[1]);
}

static void cmdInfo() {
  okf("%s %d.%d %d %s", FIRMWARE_NAME, FIRMWARE_VERSION_MAJOR, FIRMWARE_VERSION_MINOR, PROTOCOL_VERSION, boardKit());
}

static void syncEvents();
static void reportToOtherPorts(uint8_t paramId);

static void cmdEvents() {
  if (tokenCount > 1) {
    bool on;
    if (!switchArgument(on)) return fail(ERR_BAD_ARGUMENT);
    if (on && !out->eventsOn) syncEvents();
    out->eventsOn = on;
  }
  okf("%s", onOff(out->eventsOn));
}

static void cmdStatus() {
  okf("%s %s %s", appWord(appActive()), stateWord(appState()), errorWord(appLastError()));
}

static void cmdApps() {
  for (uint8_t i = 0; i < appCount(); i++) {
    const AppDef *app = appAt(i);
    char title[64];
    quote(title, sizeof(title), app->name);
    dataf("%s %u %s", appWord(app->id), app->id, title);
  }
  ok();
}

static void cmdApp() {
  if (tokenCount == 1) return cmdStatus();
  if (tokenCount != 2) return fail(ERR_BAD_ARGUMENT);
  long number;
  uint8_t id = appFromToken(tokens[1]);
  if (id == APP_NONE && parseLong(tokens[1], 1, 255, number)) id = (uint8_t)number;
  uint8_t error = appSelect(id);
  if (error != ERR_NONE) return fail(error);
  okf("%s %s", appWord(appActive()), stateWord(appState()));
}

static void cmdService() {
  if (tokenCount > 1) {
    bool on;
    if (!switchArgument(on)) return fail(ERR_BAD_ARGUMENT);
    uint8_t error = appServiceMode(on);
    if (error != ERR_NONE) return fail(error);
  }
  okf("%s", onOff(appInServiceMode()));
}

static void cmdPotControl() {
  if (tokenCount > 1) {
    bool on;
    if (!switchArgument(on)) return fail(ERR_BAD_ARGUMENT);
    appPotControl(on);
  }
  okf("%s", onOff(appPotControlEnabled()));
}

static void cmdStartup() {
  if (tokenCount > 1) {
    bool on;
    if (!switchArgument(on)) return fail(ERR_BAD_ARGUMENT);
    uint8_t error = appStartupSet(on);
    if (error != ERR_NONE) return fail(error);
  }
  bool restores;
  uint8_t id;
  appStartupGet(restores, id);
  okf("%s %s", onOff(restores), appWord(id));
}

static void cmdLed() {
  if (tokenCount > 1) {
    bool on;
    if (!switchArgument(on)) return fail(ERR_BAD_ARGUMENT);
    digitalWrite(PIN_LED, on ? HIGH : LOW);
  }
  okf("%s", onOff(digitalRead(PIN_LED) == HIGH));
}

static void cmdPots() {
  okf("%u %u %u %u", potRaw(0), potRaw(1), potRaw(2), potRaw(3));
}

static void cmdReset() {
  ok();
  out->serial.flush();
  delay(50);
  ESP.restart();
}

// --- parameters -----------------------------------------------------------------

static const ParamDef *findDef(uint8_t paramId) {
  for (uint8_t i = 0; i < appParamCount(); i++) {
    const ParamDef *def = appParamAt(i);
    if (def->id == paramId) return def;
  }
  return nullptr;
}

static void cmdParams() {
  const uint8_t app = appActive();
  for (uint8_t i = 0; i < appParamCount(); i++) {
    const ParamDef *def = appParamAt(i);
    const ParamInfo *info = paramInfo(app, def->id);
    int32_t current = def->initial;
    appParamGet(def->id, current);
    char name[24], value[40], minimum[24], maximum[24], initial[40];
    if (info != nullptr) snprintf(name, sizeof(name), "%s", info->name);
    else snprintf(name, sizeof(name), "p%u", def->id);
    formatValue(value, sizeof(value), info, current);
    formatValue(initial, sizeof(initial), info, def->initial);
    const uint8_t decimals = info != nullptr && info->kind == KIND_NUMBER ? info->decimals : 0;
    formatFixed(minimum, sizeof(minimum), def->minimum, decimals);
    formatFixed(maximum, sizeof(maximum), def->maximum, decimals);
    const char *unit = info != nullptr && info->unit[0] != '\0' ? info->unit : "-";
    const char *choices = info != nullptr && info->kind == KIND_CHOICE ? info->choices : "-";
    const int8_t pot = appParamPot(def->id);
    char potName[8];
    if (pot < 0) snprintf(potName, sizeof(potName), "-");
    else snprintf(potName, sizeof(potName), "P%d", pot + 1);
    dataf("%s %s %s %s %s %s %s %s %s", name, kindWord(info != nullptr ? info->kind : KIND_INTEGER),
          value, minimum, maximum, initial, unit, choices, potName);
  }
  ok();
}

static const ParamInfo *namedParam(const char *name, uint8_t &paramId) {
  const uint8_t app = appActive();
  const ParamInfo *info = paramInfoByName(app, name);
  if (info != nullptr) {
    paramId = info->id;
    return info;
  }
  // p<id> reaches a parameter the name table lacks
  long number;
  if ((name[0] == 'p' || name[0] == 'P') && parseLong(name + 1, 1, 255, number)) {
    paramId = (uint8_t)number;
    return paramInfo(app, paramId);
  }
  paramId = 0;
  return nullptr;
}

static void cmdGet() {
  const uint8_t app = appActive();
  if (tokenCount == 1) {
    for (uint8_t i = 0; i < appParamCount(); i++) {
      const ParamDef *def = appParamAt(i);
      const ParamInfo *info = paramInfo(app, def->id);
      int32_t current;
      if (appParamGet(def->id, current) != ERR_NONE) continue;
      char value[40];
      formatValue(value, sizeof(value), info, current);
      if (info != nullptr) dataf("%s %s", info->name, value);
      else dataf("p%u %s", def->id, value);
    }
    return ok();
  }
  if (tokenCount != 2) return fail(ERR_BAD_ARGUMENT);
  uint8_t paramId;
  const ParamInfo *info = namedParam(tokens[1], paramId);
  int32_t current;
  if (paramId == 0 || appParamGet(paramId, current) != ERR_NONE) return fail(ERR_UNKNOWN_PARAMETER);
  char value[40];
  formatValue(value, sizeof(value), info, current);
  okf("%s", value);
}

static void cmdSet() {
  if (tokenCount != 3) return fail(ERR_BAD_ARGUMENT);
  uint8_t paramId;
  const ParamInfo *info = namedParam(tokens[1], paramId);
  const ParamDef *def = paramId != 0 ? findDef(paramId) : nullptr;
  if (def == nullptr) return fail(ERR_UNKNOWN_PARAMETER);
  int32_t requested;
  if (!parseValue(tokens[2], info, requested)) return fail(ERR_BAD_ARGUMENT);
  uint8_t error = appParamSet(paramId, requested);
  if (error == ERR_OUT_OF_RANGE) {
    const uint8_t decimals = info != nullptr && info->kind == KIND_NUMBER ? info->decimals : 0;
    char minimum[24], maximum[24];
    formatFixed(minimum, sizeof(minimum), def->minimum, decimals);
    formatFixed(maximum, sizeof(maximum), def->maximum, decimals);
    out->serial.printf("err out-of-range %s %s..%s %s\n", tokens[1], minimum, maximum,
                  info != nullptr ? info->unit : "");
    return;
  }
  if (error != ERR_NONE) return fail(error);
  reportToOtherPorts(paramId);
  int32_t current = requested;
  appParamGet(paramId, current);
  char value[40];
  formatValue(value, sizeof(value), info, current);
  okf("%s", value);
}

// --- radio ---------------------------------------------------------------------------

static void cmdStations() {
  for (uint8_t i = 0; i < radioStationCount(); i++) {
    char name[2 * RADIO_TEXT_LENGTH + 4], url[2 * RADIO_URL_LENGTH + 4];
    quote(name, sizeof(name), radioStationName(i));
    quote(url, sizeof(url), radioStationUrl(i));
    dataf("%u %s %s", i, name, url);
  }
  ok();
}

static uint8_t editResult(RadioEdit edit) {
  if (edit == RADIO_EDIT_INVALID) return ERR_OUT_OF_RANGE;
  return edit == RADIO_EDIT_OK ? ERR_NONE : ERR_STORE_FAILED;
}

// station <slot> name "<text>" | url "<url>" | default; station all default
static void cmdStation() {
  if (tokenCount == 3 && is(1, "all") && is(2, "default")) {
    return result(editResult(radioRestoreStations(RADIO_STATION_ALL)));
  }
  long slot;
  if (tokenCount < 3 || !parseLong(tokens[1], 0, radioStationCount() - 1, slot)) return fail(ERR_BAD_ARGUMENT);
  if (tokenCount == 3 && is(2, "default")) return result(editResult(radioRestoreStations((uint8_t)slot)));
  if (tokenCount != 4) return fail(ERR_BAD_ARGUMENT);
  const uint8_t length = (uint8_t)min(strlen(tokens[3]), (size_t)255);
  if (is(2, "name")) return result(editResult(radioSetStationName((uint8_t)slot, (const uint8_t *)tokens[3], length)));
  if (is(2, "url")) return result(editResult(radioSetStationUrl((uint8_t)slot, (const uint8_t *)tokens[3], length)));
  fail(ERR_BAD_ARGUMENT);
}

static void cmdRadio() {
  char title[RADIO_TEXT_LENGTH], quoted[2 * RADIO_TEXT_LENGTH + 4];
  radioStreamTitle(title, sizeof(title));
  title[sizeof(title) - 1] = '\0';
  quote(quoted, sizeof(quoted), title);
  okf("%s %lu %s", radioWord(radioState()), (unsigned long)radioSampleRate(), quoted);
}

// wifi | wifi <slot> "<ssid>" ["<password>"] | wifi <slot> clear
static bool wifiChanged = false;           // "! wifi" goes out with the next events

// the networks with their passwords, for the front panel to join the same
// WiFi: on the panel port only, a cable inside the box; never on USB
static void wifiSecrets() {
  if (out != &ports[1]) return fail(ERR_BAD_ARGUMENT);
  for (uint8_t i = 0; i < RADIO_WIFI_NETWORKS; i++) {
    char ssid[2 * RADIO_SSID_MAX + 4];
    char password[2 * RADIO_PASSWORD_MAX + 4];
    quote(ssid, sizeof(ssid), radioWifiSsid(i));
    quote(password, sizeof(password), radioWifiPassword(i));
    dataf("%u %s %s", i, ssid, password);
  }
  ok();
}

static void cmdWifi() {
  if (tokenCount == 2 && is(1, "secrets")) return wifiSecrets();
  if (tokenCount == 1) {
    for (uint8_t i = 0; i < RADIO_WIFI_NETWORKS; i++) {
      char ssid[2 * RADIO_SSID_MAX + 4];
      quote(ssid, sizeof(ssid), radioWifiSsid(i));
      dataf("%u %s %s", i, ssid, radioWifiHasPassword(i) ? "yes" : "no");
    }
    const int8_t connected = radioWifiConnected();
    char address[20];
    radioWifiAddress(address, sizeof(address));
    if (connected < 0) return okf("- -");
    return okf("%d %s", connected, address);
  }
  long slot;
  if (!parseLong(arg(1), 0, RADIO_WIFI_NETWORKS - 1, slot)) return fail(ERR_BAD_ARGUMENT);
  uint8_t error;
  if (tokenCount == 3 && is(2, "clear")) {
    error = editResult(radioSetWifi((uint8_t)slot, "", ""));
  } else {
    if (tokenCount != 3 && tokenCount != 4) return fail(ERR_BAD_ARGUMENT);
    if (tokens[2][0] == '\0') return fail(ERR_BAD_ARGUMENT);
    error = editResult(radioSetWifi((uint8_t)slot, tokens[2], tokenCount == 4 ? tokens[3] : ""));
  }
  if (error == ERR_NONE) wifiChanged = true;
  result(error);
}

// --- the front panel's network --------------------------------------------------------

// what the panel reports of its own network: set from the panel port,
// read from any (the PC application shows it)
static char panelState[16] = "none";
static char panelAddress[48] = "-";
static uint8_t panelClients = 0;
static bool panelChanged = false;

static void cmdPanel() {
  if (tokenCount == 4) {
    if (out != &ports[1]) return fail(ERR_BAD_ARGUMENT);
    long clients;
    if (!parseLong(arg(3), 0, 9, clients)) return fail(ERR_BAD_ARGUMENT);
    strncpy(panelState, tokens[1], sizeof(panelState) - 1);
    strncpy(panelAddress, tokens[2], sizeof(panelAddress) - 1);
    panelClients = (uint8_t)clients;
    panelChanged = true;
  } else if (tokenCount != 1) {
    return fail(ERR_BAD_ARGUMENT);
  }
  okf("%s %s %u", panelState, panelAddress, panelClients);
}

// --- response analyzer ---------------------------------------------------------------

static void sweepStatus() {
  okf("%s %u %u %u", sweepWord(analyzerState()), analyzerPointCount(), analyzerPointTotal(),
      analyzerReferenceCount());
}

static void cmdSweep() {
  if (tokenCount == 1) return sweepStatus();
  if (tokenCount != 2) return fail(ERR_BAD_ARGUMENT);
  if (!runningApp(APP_RESPONSE)) return;
  if (is(1, "start")) {
    int32_t start, stop, perDecade;
    appParamGet(RESP_START, start);
    appParamGet(RESP_STOP, stop);
    appParamGet(RESP_POINTS_PER_DECADE, perDecade);
    if (stop <= start) return fail(ERR_OUT_OF_RANGE);
    uint8_t error = appParamSet(RESP_OUTPUT, 1);
    if (error == ERR_NONE) error = analyzerStartSweep(start, stop, perDecade);
    if (error != ERR_NONE) return fail(error);
    return sweepStatus();
  }
  if (is(1, "stop")) {
    analyzerStopSweep();
    return sweepStatus();
  }
  fail(ERR_BAD_ARGUMENT);
}

static void pointLine(bool event, uint16_t index, const AnalyzerPoint &point) {
  int32_t relativeLeft, relativeRight;
  analyzerRelative(point, relativeLeft, relativeRight);
  char f[16], l[16], r[16], rl[16], rr[16];
  formatFixed(f, sizeof(f), point.centiHz, 2);
  formatLevel(l, sizeof(l), point.levelLeft);
  formatLevel(r, sizeof(r), point.levelRight);
  formatLevel(rl, sizeof(rl), relativeLeft);
  formatLevel(rr, sizeof(rr), relativeRight);
  if (event) eventf("point %u %s %s %s %s %s", index, f, l, r, rl, rr);
  else dataf("%u %s %s %s %s %s", index, f, l, r, rl, rr);
}

static void cmdPoints() {
  long from = 0;
  if (tokenCount == 2 && !parseLong(tokens[1], 0, ANALYZER_MAX_POINTS, from)) return fail(ERR_BAD_ARGUMENT);
  if (tokenCount > 2) return fail(ERR_BAD_ARGUMENT);
  AnalyzerPoint point;
  for (uint16_t i = (uint16_t)from; i < analyzerPointCount() && analyzerPoint(i, point); i++) pointLine(false, i, point);
  ok();
}

static void cmdReference() {
  if (tokenCount == 2 && is(1, "keep")) {
    if (!runningApp(APP_RESPONSE)) return;
    return result(analyzerKeepReference());
  }
  if (tokenCount != 1) return fail(ERR_BAD_ARGUMENT);
  AnalyzerPoint point;
  for (uint16_t i = 0; i < analyzerReferenceCount() && analyzerReferencePoint(i, point); i++) {
    char f[16], l[16], r[16];
    formatFixed(f, sizeof(f), point.centiHz, 2);
    formatLevel(l, sizeof(l), point.levelLeft);
    formatLevel(r, sizeof(r), point.levelRight);
    dataf("%u %s %s %s", i, f, l, r);
  }
  ok();
}

static void cmdLevel() {
  if (!runningApp(APP_RESPONSE)) return;
  int32_t left, right;
  if (!analyzerMeasure(left, right)) return fail(ERR_DSP_NOT_RESPONDING);
  char l[16], r[16];
  formatLevel(l, sizeof(l), left);
  formatLevel(r, sizeof(r), right);
  okf("%s %s", l, r);
}

// --- synthesizer ------------------------------------------------------------------

static void voiceText(char *out, size_t size) {
  SynthStatus status;
  synthStatus(status);
  uint8_t modStrip, stage;
  synthModulation(modStrip, stage);
  char note[8];
  if (status.note == 0xFF) snprintf(note, sizeof(note), "-");
  else snprintf(note, sizeof(note), "%u", status.note);
  snprintf(out, size, "%s %s %u %u %u %s", note, onOff(status.gate), status.held, status.channel, modStrip,
           stageWord(stage));
}

static void cmdVoice() {
  if (!runningApp(APP_SYNTH)) return;
  char text[64];
  voiceText(text, sizeof(text));
  okf("%s", text);
}

static void cmdMidi() {
  if (!runningApp(APP_SYNTH)) return;
  SynthStatus status;
  synthStatus(status);
  okf("%lu %lu %lu %lu", (unsigned long)status.noteMessages, (unsigned long)status.otherChannel,
      (unsigned long)status.bytes, (unsigned long)status.uartErrors);
}

static void cmdLoad() {
  if (!runningApp(APP_SYNTH)) return;
  uint16_t average, peak;
  synthLoad(average, peak);
  char a[12], p[12];
  formatFixed(a, sizeof(a), average, 1);
  formatFixed(p, sizeof(p), peak, 1);
  okf("%s %s", a, p);
}

// --- SigmaStudio mode -----------------------------------------------------------------

static void cmdAux() {
  okf("%u %u %u %u %s", dacMuxValue(0), dacMuxValue(1), dacMuxValue(2), dacMuxValue(3), onOff(dacMuxRunning()));
}

static void cmdBackup() {
  uint16_t length;
  uint32_t crc;
  bool exists = studioBackupInfo(length, crc);
  okf("%s %u %08lX", exists ? "yes" : "no", exists ? length : 0, (unsigned long)(exists ? crc : 0));
}

// --- diagnostics -----------------------------------------------------------------

static const char *resetReason() {
  switch (esp_reset_reason()) {
    case ESP_RST_POWERON:   return "power-on";
    case ESP_RST_EXT:       return "external";
    case ESP_RST_SW:        return "software";
    case ESP_RST_PANIC:     return "panic";
    case ESP_RST_INT_WDT:
    case ESP_RST_TASK_WDT:
    case ESP_RST_WDT:       return "watchdog";
    case ESP_RST_BROWNOUT:  return "brownout";
    default:                return "other";
  }
}

static void cmdDiag() {
  long address, number;
  // the heap: free now, the largest block one allocation can get, the
  // lowest free since the start (all in bytes)
  if (is(1, "heap") && tokenCount == 2) {
    return okf("%u %u %u", (unsigned)ESP.getFreeHeap(),
               (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT), (unsigned)ESP.getMinFreeHeap());
  }
  // the start and the EEPROM reads since: the reset reason and the chip's
  // own code for it (0x07: the boot watchdog restarted a boot that hung, as
  // an unclean power-up gives it), the I2C lines before the driver took
  // them, failed reads, those a retry cured, and the first failure (ms after
  // the start, address, lines, clocks that freed SDA)
  if (is(1, "boot") && tokenCount == 2) {
    const EepromFaults &f = eepromFaults();
    return okf("%s 0x%02X %u %u %u %lu %u %u %u", resetReason(), (unsigned)esp_rom_get_reset_reason(0),
               f.linesAtStart, f.failures, f.recovered, (unsigned long)f.firstMs, f.firstAddress,
               f.linesAtFailure, f.firstClocks);
  }
  if (is(1, "dsp") && tokenCount == 3) {
    if (appInServiceMode() || appRunning(APP_SIGMASTUDIO)) return fail(ERR_SERVICE_MODE);
    if (!parseLong(tokens[2], 0, 0x3FF, address)) return fail(ERR_BAD_ARGUMENT);
    int32_t word;
    if (!dspReadParameter((uint16_t)address, word)) return fail(ERR_DSP_NOT_RESPONDING);
    return okf("%ld 0x%08lX", (long)word, (unsigned long)word);
  }
  if (is(1, "dspset") && tokenCount == 4) {
    if (appInServiceMode() || appRunning(APP_SIGMASTUDIO)) return fail(ERR_SERVICE_MODE);
    if (!parseLong(tokens[2], 0, 0x3FF, address)) return fail(ERR_BAD_ARGUMENT);
    char *end;
    const int32_t word = (int32_t)strtoul(tokens[3], &end, 0);
    if (*end != '\0') return fail(ERR_BAD_ARGUMENT);
    const uint16_t at = (uint16_t)address;
    return result(dspSafeloadRaw(&at, &word, 1) ? ERR_NONE : ERR_DSP_WRITE_FAILED);
  }
  if (is(1, "eeprom") && tokenCount == 4) {
    if (appInServiceMode()) return fail(ERR_SERVICE_MODE);
    if (!parseLong(tokens[2], 0, EEPROM_SIZE - 1, address) || !parseLong(tokens[3], 1, 64, number)) {
      return fail(ERR_BAD_ARGUMENT);
    }
    uint8_t bytes[64];
    if (!eepromRead((size_t)address, bytes, (size_t)number)) return fail(ERR_EEPROM_FAILED);
    char hex[3 * 64 + 1];
    for (long i = 0; i < number; i++) snprintf(hex + 3 * i, 4, "%02X ", bytes[i]);
    hex[3 * number - 1] = '\0';
    return okf("%s", hex);
  }
  if (is(1, "scan") && tokenCount == 2) {
    if (appInServiceMode()) return fail(ERR_SERVICE_MODE);
    uint16_t length, stoppedAt;
    bool nodeImage;
    uint32_t crc;
    uint8_t scan = appStudioScan(length, nodeImage, crc, stoppedAt);
    return okf("%u %u %s %08lX %u", scan, length, nodeImage ? "firmware-image" : "own-program",
               (unsigned long)crc, stoppedAt);
  }
  if (is(1, "tone") && tokenCount == 3) {
    bool on;
    if (!parseSwitch(tokens[2], on)) return fail(ERR_BAD_ARGUMENT);
    if (!runningApp(APP_RADIO)) return;
    radioTestTone(on);
    return ok();
  }
  if (is(1, "meter") && tokenCount == 2) {
    uint32_t bytes, streamBytes, loops;
    uint16_t peak;
    uint8_t stage, taskState;
    radioTakePcmMeter(bytes, peak, streamBytes, stage, loops, taskState);
    return okf("%lu %u %lu %u %lu %u", (unsigned long)bytes, peak, (unsigned long)streamBytes, stage,
               (unsigned long)loops, taskState);
  }
  // the DSP's serial output, as the ESP32's second I2S port receives it:
  // the rate timed against the ESP32's clock, the frames taken, per channel
  // RMS and peak in dBFS and the frequency of its zero crossings, and the
  // first left slot as it came (no I2C: works in service mode too)
  if (is(1, "capture") && (tokenCount == 2 || tokenCount == 3)) {
    long frames = CAPTURE_DEFAULT_FRAMES;
    if (tokenCount == 3 && !parseLong(tokens[2], CAPTURE_MIN_FRAMES, CAPTURE_MAX_FRAMES, frames)) {
      return fail(ERR_BAD_ARGUMENT);
    }
    CaptureResult r;
    if (!captureMeasure((uint32_t)frames, r)) return fail(ERR_NO_CLOCK);
    return okf("%.1f %lu %.2f %.2f %.1f %.2f %.2f %.1f 0x%08lX", r.rate, (unsigned long)r.frames,
               r.left.rmsDb, r.left.peakDb, r.left.hertz, r.right.rmsDb, r.right.peakDb, r.right.hertz,
               (unsigned long)r.firstWord);
  }
  // the serial output's LRCLK and BCLK, counted on their pins (Hz)
  if (is(1, "clocks") && tokenCount == 2) {
    float lrclk, bclk;
    captureClocks(lrclk, bclk);
    return okf("%.0f %.0f", lrclk, bclk);
  }
  if (is(1, "raw") && tokenCount == 2) {
    if (!runningApp(APP_RESPONSE)) return;
    int32_t left, right;
    if (!analyzerReadRaw(left, right)) return fail(ERR_DSP_NOT_RESPONDING);
    return okf("%ld %ld", (long)left, (long)right);
  }
  fail(ERR_BAD_ARGUMENT);
}

struct Command {
  const char *name;
  void (*run)();
  const char *usage;
};

static const Command commands[] = {
  { "help",      cmdHelp,       "help" },
  { "ping",      cmdPing,       "ping" },
  { "echo",      cmdEcho,       "echo <word>" },
  { "info",      cmdInfo,       "info" },
  { "events",    cmdEvents,     "events [on|off]" },
  { "status",    cmdStatus,     "status" },
  { "apps",      cmdApps,       "apps" },
  { "app",       cmdApp,        "app [<application>]" },
  { "params",    cmdParams,     "params" },
  { "get",       cmdGet,        "get [<parameter>]" },
  { "set",       cmdSet,        "set <parameter> <value>" },
  { "pots",      cmdPots,       "pots" },
  { "potctl",    cmdPotControl, "potctl [on|off]" },
  { "startup",   cmdStartup,    "startup [on|off]" },
  { "service",   cmdService,    "service [on|off]" },
  { "led",       cmdLed,        "led [on|off]" },
  { "stations",  cmdStations,   "stations" },
  { "station",   cmdStation,    "station <slot> name|url \"<text>\" | station <slot>|all default" },
  { "radio",     cmdRadio,      "radio" },
  { "wifi",      cmdWifi,       "wifi [<slot> \"<ssid>\" [\"<password>\"] | <slot> clear]" },
  { "panel",     cmdPanel,      "panel: the front panel's network state, address, clients" },
  { "sweep",     cmdSweep,      "sweep [start|stop]" },
  { "points",    cmdPoints,     "points [<from>]" },
  { "reference", cmdReference,  "reference [keep]" },
  { "level",     cmdLevel,      "level" },
  { "voice",     cmdVoice,      "voice" },
  { "midi",      cmdMidi,       "midi" },
  { "load",      cmdLoad,       "load" },
  { "aux",       cmdAux,        "aux" },
  { "backup",    cmdBackup,     "backup" },
  { "diag",      cmdDiag,       "diag boot|dsp|dspset|eeprom|scan|tone|meter|raw|heap|capture|clocks ..." },
  { "reset",     cmdReset,      "reset" },
};

static void cmdHelp() {
  for (const Command &command : commands) dataf("%s", command.usage);
  ok();
}

static void dispatch() {
  if (!tokenize(out->line)) return fail(ERR_BAD_ARGUMENT);
  if (tokenCount == 0) return;              // an empty line asks nothing
  for (const Command &command : commands) {
    if (strcasecmp(command.name, tokens[0]) == 0) {
      command.run();
      return;
    }
  }
  fail(ERR_UNKNOWN_COMMAND);
}

// ----------------------------------------------------------------------------
//  Events
// ----------------------------------------------------------------------------

static uint32_t eventsCheckedAt = 0;
static uint8_t seenApp = 0xFF;
static uint8_t seenState = 0xFF;
static uint8_t seenError = 0xFF;
static uint16_t seenPots[POT_COUNT];
static uint32_t potsSentAt = 0;
static uint8_t seenRadioState = 0xFF;
static uint32_t seenRate = 0;
static char seenTitle[RADIO_TEXT_LENGTH];
static uint8_t seenSweepState = 0xFF;
static uint16_t seenPoints = 0;
static uint16_t seenReference = 0;
static char seenVoice[64];
static uint32_t voiceSentAt = 0;

static uint64_t paramBit(uint8_t paramId) {
  for (uint8_t i = 0; i < appParamCount() && i < 64; i++) {
    if (appParamAt(i)->id == paramId) return 1ULL << i;
  }
  return 0;
}

// a pot changed a value: every port with events on hears of it
static void onParamChanged(uint8_t paramId, int32_t) {
  const uint64_t bit = paramBit(paramId);
  for (uint8_t i = 0; i < portCount; i++) {
    if (ports[i].eventsOn) ports[i].pendingValues |= bit;
  }
}

// a port set a value: the other ports hear of it; the one that set it has
// its answer, and an echo could undo a newer value it is about to send
static void reportToOtherPorts(uint8_t paramId) {
  const uint64_t bit = paramBit(paramId);
  for (uint8_t i = 0; i < portCount; i++) {
    if (&ports[i] != out && ports[i].eventsOn) ports[i].pendingValues |= bit;
  }
}

// the trackers take what is there now: "events on" reports changes, not
// the state the host is about to read anyway. While another port already
// follows events, the trackers stay as they are, so it misses nothing.
static void syncEvents() {
  out->pendingValues = 0;
  for (uint8_t i = 0; i < portCount; i++) {
    if (ports[i].eventsOn) return;
  }
  seenApp = appActive();
  seenState = appState();
  seenError = appLastError();
  for (uint8_t i = 0; i < POT_COUNT; i++) seenPots[i] = potRaw(i);
  seenRadioState = radioState();
  seenRate = radioSampleRate();
  radioStreamTitle(seenTitle, sizeof(seenTitle));
  seenSweepState = analyzerState();
  seenPoints = analyzerPointCount();
  seenReference = analyzerReferenceCount();
  seenVoice[0] = '\0';
  if (appRunning(APP_SYNTH)) voiceText(seenVoice, sizeof(seenVoice));
}

static void statusEvents() {
  const uint8_t app = appActive();
  const uint8_t state = appState();
  const uint8_t error = appLastError();
  if (app == seenApp && state == seenState && error == seenError) return;
  if (app != seenApp) {
    for (uint8_t i = 0; i < portCount; i++) ports[i].pendingValues = 0;   // other parameters now
    seenPoints = 0;
    seenTitle[0] = '\0';
    seenVoice[0] = '\0';
  }
  seenApp = app;
  seenState = state;
  seenError = error;
  eventf("status %s %s %s", appWord(app), stateWord((AppState)state), errorWord(error));
}

// each port's own queue, to that port alone
static void valueEvents(Port &port, uint32_t now) {
  if (!port.eventsOn || port.pendingValues == 0 || now - port.valuesSentAt < PROTOCOL_VALUE_EVENT_MS) return;
  port.valuesSentAt = now;
  const uint64_t pending = port.pendingValues;
  port.pendingValues = 0;
  Port *asked = out;
  out = &port;
  broadcasting = false;
  const uint8_t app = appActive();
  for (uint8_t i = 0; i < appParamCount() && i < 64; i++) {
    if ((pending & (1ULL << i)) == 0) continue;
    const ParamDef *def = appParamAt(i);
    const ParamInfo *info = paramInfo(app, def->id);
    int32_t current;
    if (appParamGet(def->id, current) != ERR_NONE) continue;
    char value[40];
    formatValue(value, sizeof(value), info, current);
    if (info != nullptr) eventf("value %s %s", info->name, value);
    else eventf("value p%u %s", def->id, value);
  }
  out = asked;
  broadcasting = true;
}

static void potEvents(uint32_t now) {
  if (now - potsSentAt < PROTOCOL_POT_EVENT_MS) return;
  bool moved = false;
  for (uint8_t i = 0; i < POT_COUNT; i++) {
    if (abs((int)potRaw(i) - (int)seenPots[i]) >= PROTOCOL_POT_EVENT_COUNTS) moved = true;
  }
  if (!moved) return;
  potsSentAt = now;
  for (uint8_t i = 0; i < POT_COUNT; i++) seenPots[i] = potRaw(i);
  eventf("pots %u %u %u %u", seenPots[0], seenPots[1], seenPots[2], seenPots[3]);
}

static void radioEvents() {
  if (!appRunning(APP_RADIO)) return;
  const uint8_t state = radioState();
  const uint32_t rate = radioSampleRate();
  if (state != seenRadioState || rate != seenRate) {
    seenRadioState = state;
    seenRate = rate;
    eventf("radio %s %lu", radioWord((RadioState)state), (unsigned long)rate);
  }
  char title[RADIO_TEXT_LENGTH];
  radioStreamTitle(title, sizeof(title));
  title[sizeof(title) - 1] = '\0';
  if (strcmp(title, seenTitle) != 0) {
    memcpy(seenTitle, title, sizeof(seenTitle));
    char quoted[2 * RADIO_TEXT_LENGTH + 4];
    quote(quoted, sizeof(quoted), title);
    eventf("title %s", quoted);
  }
}

static void sweepEvents() {
  if (!appRunning(APP_RESPONSE)) return;
  const uint16_t count = analyzerPointCount();
  if (count < seenPoints) seenPoints = 0;  // a new sweep began
  AnalyzerPoint point;
  bool changed = count != seenPoints;
  for (uint16_t i = seenPoints; i < count && analyzerPoint(i, point); i++) pointLine(true, i, point);
  seenPoints = count;
  const uint8_t state = analyzerState();
  const uint16_t reference = analyzerReferenceCount();
  if (!changed && state == seenSweepState && reference == seenReference) return;
  seenSweepState = state;
  seenReference = reference;
  eventf("sweep %s %u %u %u", sweepWord((SweepState)state), count, analyzerPointTotal(), reference);
}

static void voiceEvents(uint32_t now) {
  if (!appRunning(APP_SYNTH) || now - voiceSentAt < PROTOCOL_VOICE_EVENT_MS) return;
  char voice[64];
  voiceText(voice, sizeof(voice));
  if (strcmp(voice, seenVoice) == 0) return;
  voiceSentAt = now;
  memcpy(seenVoice, voice, sizeof(seenVoice));
  eventf("voice %s", voice);
}

static void events() {
  const uint32_t now = millis();
  if (now - eventsCheckedAt < EVENT_CHECK_MS) return;
  eventsCheckedAt = now;
  broadcasting = true;
  statusEvents();
  if (wifiChanged) {
    wifiChanged = false;
    eventf("wifi");
  }
  if (panelChanged) {
    panelChanged = false;
    eventf("panel %s %s %u", panelState, panelAddress, panelClients);
  }
  if (!appInServiceMode()) {
    for (uint8_t i = 0; i < portCount; i++) valueEvents(ports[i], now);
    potEvents(now);
    radioEvents();
    sweepEvents();
    voiceEvents(now);
  }
  broadcasting = false;
}

// ----------------------------------------------------------------------------
//  Public
// ----------------------------------------------------------------------------

void protocolInit() {
  pinMode(PIN_LED, OUTPUT);
  digitalWrite(PIN_LED, LOW);
  Serial.setRxBufferSize(PROTOCOL_RX_BUFFER);
  Serial.setTxBufferSize(PROTOCOL_TX_BUFFER);
  Serial.begin(PROTOCOL_BAUD_RATE);
  // the panel port: GPIO37 is input-only and has no pull-up of its own (the
  // panel's idle line, or 10k to 3.3 V, holds it high); GPIO4 led nowhere
  Serial2.setRxBufferSize(PROTOCOL_RX_BUFFER);
  Serial2.setTxBufferSize(PROTOCOL_TX_BUFFER);
  Serial2.begin(PROTOCOL_PANEL_BAUD_RATE, SERIAL_8N1, PIN_PANEL_RX, PIN_PANEL_TX);
  appOnParamChanged(onParamChanged);
}

void protocolReady() {
  // a fresh line first: the ESP32's boot ROM may have left text on the port
  for (uint8_t i = 0; i < portCount; i++) {
    ports[i].serial.print('\n');
    out = &ports[i];
    eventf("ready %s %d.%d %d", FIRMWARE_NAME, FIRMWARE_VERSION_MAJOR, FIRMWARE_VERSION_MINOR, PROTOCOL_VERSION);
  }
  out = &ports[0];
}

static void readRequests(Port &port) {
  out = &port;
  while (port.serial.available() > 0) {
    const int c = port.serial.read();
    if (c == '\r') continue;
    if (c == '\n') {
      if (port.lineOverflow) {
        fail(ERR_LINE_TOO_LONG);
      } else {
        port.line[port.lineLength] = '\0';
        dispatch();
      }
      port.lineLength = 0;
      port.lineOverflow = false;
      continue;
    }
    if (port.lineLength < PROTOCOL_LINE_MAX) port.line[port.lineLength++] = (char)c;
    else port.lineOverflow = true;
  }
}

void protocolPoll() {
  for (uint8_t i = 0; i < portCount; i++) readRequests(ports[i]);
  out = &ports[0];
  for (uint8_t i = 0; i < portCount; i++) {
    if (ports[i].eventsOn) {
      events();
      break;
    }
  }
}
