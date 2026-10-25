#include "Link.h"
#include "Config.h"
#include "Wireless.h"
#include <driver/gpio.h>
#include <esp_rom_gpio.h>
#include <soc/gpio_sig_map.h>

Board board;

enum RequestKind : uint8_t {
  REQ_SYNC, REQ_INFO, REQ_EVENTS, REQ_STATUS, REQ_POTCTL, REQ_PARAMS, REQ_POTS,
  REQ_SET, REQ_APP, REQ_SET_POTCTL, REQ_CHECK,
  REQ_RADIO, REQ_STATIONS, REQ_SWEEP, REQ_POINTS, REQ_STARTUP, REQ_THD, REQ_SPECTRUM,
  REQ_SPECTRUM_STATE, REQ_PEAKS,
  REQ_REMOTE,               // the PC's request: its answer goes back to the PC
  REQ_SECRETS,              // the board's WiFi networks, for the panel to join
  REQ_PANEL,                // the panel's network state, told to the board
};

struct Request {
  String line;
  RequestKind kind;
  int param;                // REQ_SET: the parameter's index
};

static std::vector<Request> queue;
static bool waiting = false;            // a request is in flight
static Request current;
// the THD and the Spectrum result whose spectrum was read last, and a
// spectrum on its way (REQ_SPECTRUM's param: 0 THD's, 1 the Spectrum's)
static int shownResults = -1;
static int shownSpectrumResults = -1;
static bool spectrumAsked = false;
static uint32_t sentAt = 0;
static std::vector<String> dataLines;
static char lineBuffer[LINK_LINE_MAX + 1];
static uint16_t lineLength = 0;
static bool overflow = false;
static uint32_t syncCount = 0;
static String syncWord;
static uint32_t syncSentAt = 0;
static uint32_t heardAt = 0;            // the last line from the board
static bool remoteEvents = false;       // the PC on the network asked for events
static uint32_t reportedNetwork = 0xFFFFFFFF;
static bool txDriven = false;           // the TX line is driven, not let go
static uint32_t lineCheckedAt = 0;
static uint8_t deadChecks = 0;          // checks in a row that found the board's line low

static void changed() {
  board.version++;
}

// --- the line ---------------------------------------------------------------------------

// the TX line to the board's GPIO37: driven by UART2, or let go (high-Z),
// so that nothing feeds a board without power
static void driveTx(bool drive) {
  const gpio_num_t pin = (gpio_num_t)PIN_LINK_TX;
  if (drive) {
    gpio_set_level(pin, 1);             // idle high from the first moment
    gpio_set_direction(pin, GPIO_MODE_OUTPUT);
    esp_rom_gpio_connect_out_signal(PIN_LINK_TX, U2TXD_OUT_IDX, false, false);
  } else {
    esp_rom_gpio_connect_out_signal(PIN_LINK_TX, SIG_GPIO_OUT_IDX, false, false);
    gpio_set_direction(pin, GPIO_MODE_INPUT);
    gpio_set_pull_mode(pin, GPIO_FLOATING);
  }
  txDriven = drive;
  Serial.printf("board line %s: TX %s\n", drive ? "up" : "down", drive ? "driven" : "let go");
}

// the board's TX line: a running UART holds it high at least once a
// character (its stop bit); low all through two characters, it has no power
// (or its firmware has not started the port yet)
static bool boardLineLives() {
  for (int i = 0; i < LINK_LINE_SAMPLES; i++) {
    if (gpio_get_level((gpio_num_t)PIN_LINK_RX) != 0) return true;
    delayMicroseconds(10);
  }
  return false;
}

// --- words ------------------------------------------------------------------------------

// splits a line into words; "quoted text" with \" and \\ is one word
static std::vector<String> words(const char *text) {
  std::vector<String> result;
  const char *p = text;
  while (*p != '\0') {
    while (*p == ' ') p++;
    if (*p == '\0') break;
    String word;
    if (*p == '"') {
      p++;
      while (*p != '\0' && *p != '"') {
        if (*p == '\\' && p[1] != '\0') p++;
        word += *p++;
      }
      if (*p == '"') p++;
    } else {
      while (*p != '\0' && *p != ' ') word += *p++;
    }
    result.push_back(word);
  }
  return result;
}

static std::vector<String> splitCommas(const String &text) {
  std::vector<String> result;
  int start = 0;
  while (start <= (int)text.length()) {
    int comma = text.indexOf(',', start);
    if (comma < 0) comma = text.length();
    if (comma > start) result.push_back(text.substring(start, comma));
    start = comma + 1;
  }
  return result;
}

static String plain(const String &word) {
  return word == "-" ? String() : word;
}

// --- requests --------------------------------------------------------------------------

static void enqueue(const String &line, RequestKind kind, int param = -1) {
  queue.push_back({ line, kind, param });
}

static void send(const Request &request) {
  current = request;
  waiting = true;
  sentAt = millis();
  dataLines.clear();
  Serial2.print(request.line);
  Serial2.print('\n');
}

static void loseLink(const char *why) {
  if (board.linked) Serial.printf("link lost: %s\n", why);
  board.linked = false;
  queue.clear();
  waiting = false;
  for (Param &param : board.params) param.inFlight = false;
  syncSentAt = 0;
  changed();
}

// once the board answered the resync: what runs, with events from now on
static void linked() {
  board.linked = true;
  heardAt = millis();
  Serial.println("link up");
  enqueue("info", REQ_INFO);
  enqueue("events on", REQ_EVENTS);
  enqueue("status", REQ_STATUS);
  enqueue("potctl", REQ_POTCTL);
  enqueue("params", REQ_PARAMS);
  enqueue("pots", REQ_POTS);
  enqueue("wifi secrets", REQ_SECRETS);
  reportedNetwork = 0xFFFFFFFF;           // the board hears of the network again
  changed();
}

static void readParams() {
  board.params.clear();
  for (const String &line : dataLines) {
    // name kind value min max default unit choices pot
    std::vector<String> w = words(line.c_str());
    if (w.size() < 9) continue;
    Param param;
    param.name = w[0];
    param.kind = w[1] == "number" ? KIND_NUMBER : w[1] == "choice" ? KIND_CHOICE : w[1] == "switch" ? KIND_SWITCH : KIND_INTEGER;
    param.value = w[2];
    param.minimum = w[3].toDouble();
    param.maximum = w[4].toDouble();
    param.unit = plain(w[6]);
    param.choices = splitCommas(plain(w[7]));
    param.pot = w[8].length() == 2 && w[8][0] == 'P' ? w[8][1] - '1' : -1;
    param.dirty = false;
    param.inFlight = false;
    board.params.push_back(param);
  }
  board.paramsVersion++;
  // what the application's own page needs besides its parameters
  if (board.application == "radio") {
    enqueue("stations", REQ_STATIONS);
    enqueue("radio", REQ_RADIO);
  } else if (board.application == "analyzer") {
    enqueue("sweep", REQ_SWEEP);
    enqueue("points", REQ_POINTS);
    shownResults = -1;
    shownSpectrumResults = -1;
    enqueue("thd", REQ_THD);
    enqueue("spectrum", REQ_SPECTRUM_STATE);
  }
}

// <state> <measured> <total> <reference-points>, from w[from] on
static void readSweep(const std::vector<String> &w, size_t from) {
  if (w.size() < from + 3) return;
  board.sweepState = w[from];
  board.sweepMeasured = w[from + 1].toInt();
  board.sweepTotal = w[from + 2].toInt();
}

// <index> <hz> <left> <right> ..., from w[from] on; index 0 starts anew
static void readPoint(const std::vector<String> &w, size_t from) {
  if (w.size() < from + 4) return;
  const int index = w[from].toInt();
  if (index == 0) board.points.clear();
  const SweepPoint point = { w[from + 1].toFloat(), w[from + 2].toFloat(), w[from + 3].toFloat() };
  if (index < (int)board.points.size()) board.points[index] = point;
  else if (index == (int)board.points.size()) board.points.push_back(point);
  board.pointsVersion++;
}

// a number, or NAN for '-'
static float number(const String &word) {
  return word == "-" ? NAN : word.toFloat();
}

// <state> <results> <hz> <level-L> <thd-L> <thdn-L> <level-R> <thd-R> <thdn-R>,
// from w[from] on; a result not yet drawn brings its spectrum along
static void readThd(const std::vector<String> &w, size_t from) {
  if (w.size() < from + 2) return;
  board.thdState = w[from];
  board.thdResults = w[from + 1].toInt();
  if (w.size() >= from + 9) {
    board.thdHertz = number(w[from + 2]);
    for (int c = 0; c < 2; c++) {
      board.thdLevel[c] = number(w[from + 3 + 3 * c]);
      board.thd[c] = number(w[from + 4 + 3 * c]);
      board.thdn[c] = number(w[from + 5 + 3 * c]);
    }
  }
  if (board.thdResults == 0) {
    board.spectrum.clear();
    board.spectrumVersion++;
    shownResults = 0;
  } else if (board.thdResults != shownResults && !spectrumAsked) {
    spectrumAsked = true;
    enqueue("spectrum points " LINK_SPECTRUM_POINTS_TEXT, REQ_SPECTRUM, 0);
  }
}

// <state> <results> <rms-L> <rms-R>, from w[from] on; a result not yet
// drawn brings its spectrum and peaks along
static void readSpectrumState(const std::vector<String> &w, size_t from) {
  if (w.size() < from + 2) return;
  board.spectrumState = w[from];
  board.spectrumResults = w[from + 1].toInt();
  if (w.size() >= from + 4) {
    board.rms[0] = number(w[from + 2]);
    board.rms[1] = number(w[from + 3]);
  }
  if (board.spectrumResults == 0) {
    shownSpectrumResults = 0;
  } else if (board.spectrumResults != shownSpectrumResults && !spectrumAsked) {
    spectrumAsked = true;
    enqueue("spectrum points " LINK_SPECTRUM_POINTS_TEXT, REQ_SPECTRUM, 1);
    enqueue("spectrum peaks", REQ_PEAKS);
  }
}

static void answered(bool ok, const std::vector<String> &w) {
  waiting = false;
  if (current.kind == REQ_REMOTE) {
    // the PC has its answer; the panel follows what it changed
    if (ok && w.size() >= 2) {
      std::vector<String> asked = words(current.line.c_str());
      if (asked.size() >= 3 && asked[0] == "set") {
        const int index = linkFindParam(asked[1].c_str());
        if (index >= 0 && !board.params[index].dirty && !board.params[index].inFlight) board.params[index].value = w[1];
      } else if (asked.size() >= 2 && asked[0] == "potctl") {
        board.potControl = w[1] == "on";
      } else if (asked.size() >= 2 && asked[0] == "startup") {
        board.startupRestores = w[1] == "on";
      }
      changed();
    }
    return;
  }
  if (!ok) {
    // err <code> <message>: shown, and the value read again where one was set
    board.message = w.size() >= 2 ? w[1] : String("failed");
    Serial.printf("err to '%s': %s\n", current.line.c_str(), board.message.c_str());
    if (current.kind == REQ_SET && current.param < (int)board.params.size()) {
      board.params[current.param].inFlight = false;
      enqueue("params", REQ_PARAMS);
    }
    changed();
    return;
  }
  const String first = w.size() >= 2 ? w[1] : String();
  switch (current.kind) {
    case REQ_SYNC:
    case REQ_CHECK:
      break;
    case REQ_INFO:
      // ok Audio-DSP-Lab <firmware> <protocol> <kit>
      if (w.size() >= 3) board.firmware = w[2];
      if (w.size() >= 5) board.kit = w[4];
      break;
    case REQ_STATUS:
      if (w.size() >= 4) {
        board.application = w[1];
        board.state = w[2];
        board.lastError = w[3];
      }
      break;
    case REQ_POTCTL:
    case REQ_SET_POTCTL:
      board.potControl = first == "on";
      break;
    case REQ_PARAMS:
      readParams();
      break;
    case REQ_POTS:
      for (int i = 0; i < 4 && i + 1 < (int)w.size(); i++) board.pots[i] = w[i + 1].toInt();
      break;
    case REQ_SET:
      if (current.param < (int)board.params.size()) {
        Param &param = board.params[current.param];
        param.inFlight = false;
        if (!param.dirty) param.value = first;
        // the board tells only the other port; the PC hears it from the panel
        if (remoteEvents) networkSend((String("! value ") + param.name + " " + first).c_str());
      }
      break;
    case REQ_SECRETS: {
      // * <slot> "<ssid>" "<password>"
      String ssids[2], passwords[2];
      for (const String &data : dataLines) {
        std::vector<String> d = words(data.c_str());
        const int slot = d.size() >= 3 ? d[0].toInt() : -1;
        if (slot == 0 || slot == 1) {
          ssids[slot] = d[1];
          passwords[slot] = d[2];
        }
      }
      networkConfigure(ssids, passwords);
      break;
    }
    case REQ_APP:
      // ok <app> <state>: the new application's parameters
      if (w.size() >= 3) {
        board.application = w[1];
        board.state = w[2];
      }
      enqueue("params", REQ_PARAMS);
      break;
    case REQ_RADIO:
      // ok <state> <sample-rate> "<title>"
      if (w.size() >= 3) {
        board.radioState = w[1];
        board.radioRate = w[2].toInt();
      }
      board.title = w.size() >= 4 ? w[3] : String();
      break;
    case REQ_STATIONS:
      // * <slot> "<name>" "<url>"
      board.stations.assign(8, String());
      for (const String &line : dataLines) {
        std::vector<String> d = words(line.c_str());
        const int slot = d.size() >= 2 ? d[0].toInt() : -1;
        if (slot >= 0 && slot < 8) board.stations[slot] = d[1];
      }
      board.stationsVersion++;
      break;
    case REQ_SWEEP:
      readSweep(w, 1);
      break;
    case REQ_STARTUP:
      // ok on|off <application>
      board.startupRestores = first == "on";
      break;
    case REQ_POINTS:
      // * <index> <hz> <left> <right> <rel-left> <rel-right>
      board.points.clear();
      for (const String &line : dataLines) readPoint(words(line.c_str()), 0);
      board.pointsVersion++;
      break;
    case REQ_THD:
      readThd(w, 1);
      break;
    case REQ_SPECTRUM_STATE:
      readSpectrumState(w, 1);
      break;
    case REQ_PEAKS:
      // * <rank> <hz-L> <dB-L> <hz-R> <dB-R>: the strongest is the first
      board.peakHertz[0] = board.peakHertz[1] = board.peakDb[0] = board.peakDb[1] = NAN;
      if (!dataLines.empty()) {
        std::vector<String> d = words(dataLines[0].c_str());
        if (d.size() >= 5) {
          board.peakHertz[0] = number(d[1]);
          board.peakDb[0] = number(d[2]);
          board.peakHertz[1] = number(d[3]);
          board.peakDb[1] = number(d[4]);
        }
      }
      break;
    case REQ_SPECTRUM: {
      // * <index> <hz> <dB-L> <dB-R>
      spectrumAsked = false;
      if (current.param == 1) shownSpectrumResults = board.spectrumResults;
      else shownResults = board.thdResults;
      // every point in its place, or the spectrum shown stays (a line lost on the way)
      std::vector<SweepPoint> spectrum;
      for (const String &line : dataLines) {
        std::vector<String> d = words(line.c_str());
        if (d.size() < 4 || d[0].toInt() != (int)spectrum.size()) break;
        spectrum.push_back({ d[1].toFloat(), d[2].toFloat(), d[3].toFloat() });
      }
      if (spectrum.size() != LINK_SPECTRUM_POINTS) {
        Serial.printf("spectrum incomplete: %u of %u points\n", (unsigned)spectrum.size(), (unsigned)dataLines.size());
        break;
      }
      board.spectrum.swap(spectrum);
      board.spectrumVersion++;
      break;
    }
    default:
      break;
  }
  changed();
}

// --- events -----------------------------------------------------------------------------

static void event(const std::vector<String> &w) {
  if (w.size() < 2) return;
  const String &name = w[1];
  if (name == "wifi") {
    // the networks changed on the board: join what it keeps now
    enqueue("wifi secrets", REQ_SECRETS);
  } else if (name == "ready") {
    // the board started again: its events are off, its state new
    loseLink("the board restarted");
  } else if (name == "status" && w.size() >= 5) {
    const bool another = w[2] != board.application;
    if (another) board.gate = false;
    board.application = w[2];
    board.state = w[3];
    board.lastError = w[4];
    if (another) enqueue("params", REQ_PARAMS);
    changed();
  } else if (name == "value" && w.size() >= 4) {
    const int index = linkFindParam(w[2].c_str());
    // a value of our own on its way wins over what the board reports
    if (index >= 0 && !board.params[index].dirty && !board.params[index].inFlight) {
      board.params[index].value = w[3];
      changed();
    }
  } else if (name == "pots" && w.size() >= 6) {
    for (int i = 0; i < 4; i++) board.pots[i] = w[i + 2].toInt();
    changed();
  } else if (name == "radio" && w.size() >= 4) {
    board.radioState = w[2];
    board.radioRate = w[3].toInt();
    changed();
  } else if (name == "title") {
    board.title = w.size() >= 3 ? w[2] : String();
    changed();
  } else if (name == "voice" && w.size() >= 4) {
    // ! voice <note> <gate> ...
    board.gate = w[3] == "on";
    changed();
  } else if (name == "sweep") {
    readSweep(w, 2);
    changed();
  } else if (name == "point") {
    readPoint(w, 2);
    changed();
  } else if (name == "thd") {
    readThd(w, 2);
    changed();
  } else if (name == "spectrum") {
    readSpectrumState(w, 2);
    changed();
  }
}

// --- lines -------------------------------------------------------------------------------

static void line(const char *text) {
  heardAt = millis();
  if (!board.linked) {
    Serial.printf("sync heard: %s\n", text);
    // only the answer to the newest resync counts
    if (strncmp(text, "ok ", 3) == 0 && syncWord == (text + 3)) {
      waiting = false;
      linked();
    }
    return;
  }
  // the PC on the network: the answers to its requests, and the events it asked for
  const bool forPc = waiting && current.kind == REQ_REMOTE && strncmp(text, "! ", 2) != 0;
  if (forPc || (strncmp(text, "! ", 2) == 0 && (remoteEvents || strncmp(text, "! ready", 7) == 0))) {
    networkSend(text);
  }
  if (strncmp(text, "! ", 2) == 0) {
    event(words(text));
  } else if (strncmp(text, "* ", 2) == 0) {
    if (waiting) dataLines.push_back(String(text + 2));
  } else if (strcmp(text, "ok") == 0 || strncmp(text, "ok ", 3) == 0) {
    if (waiting) answered(true, words(text));
  } else if (strncmp(text, "err ", 4) == 0) {
    if (waiting) answered(false, words(text));
  }
}

static void readLines() {
  while (Serial2.available() > 0) {
    const int c = Serial2.read();
    if (c == '\r') continue;
    if (c == '\n') {
      if (!overflow) {
        lineBuffer[lineLength] = '\0';
        line(lineBuffer);
      }
      lineLength = 0;
      overflow = false;
      continue;
    }
    if (lineLength < LINK_LINE_MAX) lineBuffer[lineLength++] = (char)c;
    else overflow = true;
  }
}

// --- public -------------------------------------------------------------------------------

void linkBegin() {
  Serial2.setRxBufferSize(LINK_RX_BUFFER);
  Serial2.begin(LINK_BAUD_RATE, SERIAL_8N1, PIN_LINK_RX, PIN_LINK_TX);
  driveTx(false);                       // until the board's line lives
}

void linkPoll() {
  readLines();
  const uint32_t now = millis();
  if (now - lineCheckedAt >= LINK_LINE_CHECK_MS) {
    lineCheckedAt = now;
    if (boardLineLives()) {
      deadChecks = 0;
      if (!txDriven) driveTx(true);
    } else if (deadChecks < LINK_LINE_DEAD_CHECKS && ++deadChecks == LINK_LINE_DEAD_CHECKS) {
      if (board.linked) loseLink("the board's line went low");
      if (txDriven) driveTx(false);
    }
  }
  if (!txDriven) return;
  if (!board.linked) {
    // a fresh line end first: whatever noise came before ends there
    if (now - syncSentAt >= LINK_SYNC_MS) {
      syncSentAt = now;
      syncWord = String("panel") + String(++syncCount);
      Serial2.print("\necho ");
      Serial2.print(syncWord);
      Serial2.print('\n');
    }
    return;
  }
  if (waiting) {
    const uint32_t limit = current.kind == REQ_APP || current.kind == REQ_REMOTE ? LINK_SLOW_ANSWER_MS : LINK_ANSWER_MS;
    if (now - sentAt > limit) loseLink("no answer");
    return;
  }
  // the newest value of each parameter written, one request at a time
  for (size_t i = 0; i < board.params.size(); i++) {
    Param &param = board.params[i];
    if (param.dirty && !param.inFlight) {
      param.dirty = false;
      param.inFlight = true;
      send({ String("set ") + param.name + " " + param.wanted, REQ_SET, (int)i });
      return;
    }
  }
  // the board hears of the panel's network when it changes
  if (networkVersion() != reportedNetwork) {
    reportedNetwork = networkVersion();
    send({ String("panel ") + networkState() + " " + networkAddress() + " " + (networkClientConnected() ? 1 : 0),
           REQ_PANEL, -1 });
    return;
  }
  if (!queue.empty()) {
    Request next = queue.front();
    queue.erase(queue.begin());
    send(next);
    return;
  }
  if (now - heardAt > LINK_IDLE_ECHO_MS) send({ "echo check", REQ_CHECK, -1 });
}

void linkRemoteLine(const String &text) {
  if (!board.linked) {
    networkSend("err failed the panel has no link to the board");
    return;
  }
  // events are the panel's to give: its own stay on, whatever the PC wants
  std::vector<String> w = words(text.c_str());
  if (w.size() >= 1 && w[0].equalsIgnoreCase("events")) {
    if (w.size() == 2) remoteEvents = w[1].equalsIgnoreCase("on");
    networkSend(remoteEvents ? "ok on" : "ok off");
    return;
  }
  // what the board gives the panel's cable alone stays on it: the WiFi
  // passwords, and the panel's own network register
  // (the board takes any case: "WIFI Secrets" is the same request)
  if (w.size() >= 2 && ((w[0].equalsIgnoreCase("wifi") && w[1].equalsIgnoreCase("secrets")) ||
                        w[0].equalsIgnoreCase("panel"))) {
    networkSend("err bad-argument not over the network");
    return;
  }
  enqueue(text, REQ_REMOTE);
}

void linkRemoteConnected(bool connected) {
  remoteEvents = false;
  if (!connected) {
    // what the PC asked and did not get answered is nobody's now
    for (size_t i = queue.size(); i-- > 0;) {
      if (queue[i].kind == REQ_REMOTE) queue.erase(queue.begin() + i);
    }
  }
  Serial.printf("network: PC %s\n", connected ? "connected" : "gone");
}

int linkFindParam(const char *name) {
  for (size_t i = 0; i < board.params.size(); i++) {
    if (board.params[i].name == name) return (int)i;
  }
  return -1;
}

void linkSetParam(int index, const String &value) {
  if (index < 0 || index >= (int)board.params.size()) return;
  Param &param = board.params[index];
  param.wanted = value;
  param.dirty = true;
  param.value = value;          // shown at once; the board's answer corrects it
  changed();
}

void linkSelectApplication(const char *name) {
  if (!board.linked) return;
  enqueue(String("app ") + name, REQ_APP);
  board.state = "loading";
  changed();
}

void linkSweep(bool start) {
  if (!board.linked) return;
  if (start) {
    board.points.clear();
    board.pointsVersion++;
  }
  enqueue(start ? "sweep start" : "sweep stop", REQ_SWEEP);
  changed();
}

void linkDistortion(bool start) {
  if (!board.linked) return;
  enqueue(start ? "thd start" : "thd stop", REQ_THD);
  changed();
}

void linkSpectrum(bool start) {
  if (!board.linked) return;
  enqueue(start ? "spectrum start" : "spectrum stop", REQ_SPECTRUM_STATE);
  changed();
}

void linkReadSettings() {
  if (!board.linked) return;
  enqueue("potctl", REQ_POTCTL);
  enqueue("startup", REQ_STARTUP);
}

void linkSetStartup(bool restores) {
  if (!board.linked) return;
  enqueue(restores ? "startup on" : "startup off", REQ_STARTUP);
  board.startupRestores = restores;
  changed();
}

void linkSetPotControl(bool on) {
  if (!board.linked) return;
  enqueue(on ? "potctl on" : "potctl off", REQ_SET_POTCTL);
  board.potControl = on;
  changed();
}
