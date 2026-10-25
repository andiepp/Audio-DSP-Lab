#include "Wireless.h"
#include "Link.h"
#include <WiFi.h>
#include <ESPmDNS.h>

#define NETWORK_JOIN_MS               15000   // a network that has not answered by then: the other one
#define NETWORK_RETRY_MS              30000   // both failed: try again after this
#define NETWORK_LINE_MAX              512

static String ssids[2];
static String passwords[2];
static bool configured = false;
static int trying = -1;                 // the slot being joined, -1 none
static uint32_t triedAt = 0;
static const char *state = "none";
static uint32_t version = 0;
static bool serving = false;

static WiFiServer server(NETWORK_PORT);
static WiFiClient client;
static bool hadClient = false;
static String clientLine;

static void setState(const char *next) {
  if (strcmp(state, next) != 0) {
    state = next;
    version++;
    board.version++;              // the screens show it
    Serial.printf("network: %s %s\n", state, networkAddress().c_str());
  }
}

static bool hasNetwork(int slot) {
  return slot >= 0 && slot < 2 && ssids[slot].length() > 0;
}

static void join(int slot) {
  trying = slot;
  triedAt = millis();
  WiFi.disconnect();
  WiFi.begin(ssids[slot].c_str(), passwords[slot].length() > 0 ? passwords[slot].c_str() : nullptr);
  setState("connecting");
}

static void startServing() {
  if (serving) return;
  if (MDNS.begin(NETWORK_HOST_NAME)) MDNS.addService("adl", "tcp", NETWORK_PORT);
  server.begin();
  server.setNoDelay(true);
  serving = true;
}

// --- the PC ---------------------------------------------------------------------------

static void serveClient() {
  if (!serving) return;
  if (server.hasClient()) {
    // a new PC takes the place of the one before
    if (client && client.connected()) client.stop();
    client = server.accept();
    client.setNoDelay(true);
    clientLine = "";
    linkRemoteConnected(true);
  }
  const bool connected = client && client.connected();
  if (connected != hadClient) {
    hadClient = connected;
    version++;
    board.version++;              // the screens show it
    if (!connected) linkRemoteConnected(false);
  }
  if (!connected) return;
  while (client.available() > 0) {
    const char c = (char)client.read();
    if (c == 0x0D || c == 0x0A) {                         // CR, LF or CR LF: a terminal's Enter sends CR alone
      if (clientLine.length() > 0) linkRemoteLine(clientLine);
      clientLine = "";
    } else if (clientLine.length() < NETWORK_LINE_MAX) {
      clientLine += c;
    }
  }
}

// --- public ---------------------------------------------------------------------------

void networkBegin() {
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(NETWORK_HOST_NAME);
  WiFi.setAutoReconnect(true);
}

void networkConfigure(const String newSsids[2], const String newPasswords[2]) {
  bool changed = !configured;
  for (int i = 0; i < 2; i++) {
    if (ssids[i] != newSsids[i] || passwords[i] != newPasswords[i]) changed = true;
    ssids[i] = newSsids[i];
    passwords[i] = newPasswords[i];
  }
  configured = true;
  if (!changed) return;
  // other networks: join again, the first one first
  trying = -1;
  triedAt = 0;
  if (!hasNetwork(0) && !hasNetwork(1)) {
    WiFi.disconnect();
    setState("none");
  }
}

void networkPoll() {
  if (!configured) return;
  const uint32_t now = millis();
  if (WiFi.status() == WL_CONNECTED) {
    if (strcmp(state, "online") != 0) {
      trying = -1;
      setState("online");
      version++;
    board.version++;              // the screens show it
      startServing();
    }
    serveClient();
    return;
  }
  if (strcmp(state, "online") == 0) setState("connecting");   // lost: auto-reconnect works on it
  if (!hasNetwork(0) && !hasNetwork(1)) return;
  if (trying < 0) {
    if (triedAt != 0 && now - triedAt < NETWORK_RETRY_MS) return;
    join(hasNetwork(0) ? 0 : 1);
  } else if (now - triedAt > NETWORK_JOIN_MS) {
    const int next = trying + 1;
    if (hasNetwork(next)) {
      join(next);
    } else {
      trying = -1;
      triedAt = now;
      setState("failed");
    }
  }
}

const char *networkState() {
  return state;
}

String networkAddress() {
  return WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : String("-");
}

bool networkClientConnected() {
  return hadClient;
}

uint32_t networkVersion() {
  return version;
}

void networkSend(const char *line) {
  if (!hadClient || !client.connected()) return;
  client.print(line);
  client.print('\n');
}
