#include "Link.h"
#include "Config.h"

Board board;

enum RequestKind : uint8_t {
  REQ_SYNC, REQ_INFO, REQ_EVENTS, REQ_STATUS, REQ_POTCTL, REQ_PARAMS, REQ_POTS,
  REQ_SET, REQ_APP, REQ_SET_POTCTL, REQ_CHECK,
  REQ_RADIO, REQ_STATIONS, REQ_SWEEP, REQ_POINTS, REQ_STARTUP,
};

struct Request {
  String line;
  RequestKind kind;
  int param;                // REQ_SET: the parameter's index
};

static std::vector<Request> queue;
static bool waiting = false;            // a request is in flight
static Request current;
static uint32_t sentAt = 0;
static std::vector<String> dataLines;
static char lineBuffer[LINK_LINE_MAX + 1];
static uint16_t lineLength = 0;
static bool overflow = false;
static uint32_t syncCount = 0;
static String syncWord;
static uint32_t syncSentAt = 0;
static uint32_t heardAt = 0;            // the last line from the board

static void changed() {
  board.version++;
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

static void answered(bool ok, const std::vector<String> &w) {
  waiting = false;
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
      }
      break;
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
    default:
      break;
  }
  changed();
}

// --- events -----------------------------------------------------------------------------

static void event(const std::vector<String> &w) {
  if (w.size() < 2) return;
  const String &name = w[1];
  if (name == "ready") {
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
  Serial2.setRxBufferSize(4096);
  Serial2.begin(LINK_BAUD_RATE, SERIAL_8N1, PIN_LINK_RX, PIN_LINK_TX);
}

void linkPoll() {
  readLines();
  const uint32_t now = millis();
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
    const uint32_t limit = current.kind == REQ_APP ? LINK_SLOW_ANSWER_MS : LINK_ANSWER_MS;
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
  if (!queue.empty()) {
    Request next = queue.front();
    queue.erase(queue.begin());
    send(next);
    return;
  }
  if (now - heardAt > LINK_IDLE_ECHO_MS) send({ "echo check", REQ_CHECK, -1 });
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
