// arduino-audio-tools first: the Elektor library's macros would break it (not
// used here, but the rule of the radio sketch stands). Its logging is off:
// the serial port carries the protocol only.
#define USE_AUDIO_LOGGING false
#include "AudioTools.h"
#include "AudioTools/AudioCodecs/CodecMP3Helix.h"
#include "AudioTools/Disk/AudioSourceURL.h"

#include "Radio.h"
#include "Config.h"
#include <WiFi.h>
#include <Preferences.h>
#include <string.h>
#include <math.h>

// --- stations ---------------------------------------------------------------------

// the defaults: your stations of 2026-10-16 (slots 0 to 4 from the
// InternetRadio sketch's list)
static const char *const defaultUrls[RADIO_STATION_COUNT] = {
  "http://st01.dlf.de/dlf/01/128/mp3/stream.mp3",
  "http://media-ice.musicradio.com/ClassicFMMP3",
  "http://liveradio.swr.de/sw282p3/swr3/play.mp3",
  "http://icecast.ndr.de/ndr/ndr2/niedersachsen/mp3/128/stream.mp3",
  "http://wdr-wdr2-rheinland.icecast.wdr.de/wdr/wdr2/rheinland/mp3/128/stream.mp3",
  "http://s1-webradio.rockantenne.de/70er-rock/stream/mp3",
  "http://listen.181fm.com/181-70s_128k.mp3",
  "http://liveradio.swr.de/sw282p3/swr1bw/play.mp3"
};
static const char *const defaultNames[RADIO_STATION_COUNT] = {
  "Deutschlandfunk",
  "Classic FM UK",
  "SWR3",
  "NDR 2",
  "WDR 2",
  "Rock Antenne",
  "Oldies",
  "SWR1"
};
static const uint8_t stationCount = RADIO_STATION_COUNT;

#define STATION_STORE                 "aml-radio"

static char stationNames[RADIO_STATION_COUNT][RADIO_NAME_MAX + 1];
static char stationUrls[RADIO_STATION_COUNT][RADIO_URL_LENGTH + 1];
static bool stationsLoaded = false;
static portMUX_TYPE stationLock = portMUX_INITIALIZER_UNLOCKED;

// the URL the player opens: the task's own copy, so an edit never changes
// the text under it
static char playUrl[RADIO_URL_LENGTH + 1];
static const char *playUrls[1] = { playUrl };

static void stationKey(char *key, char field, uint8_t index) {
  key[0] = field;
  key[1] = (char)('0' + index);
  key[2] = '\0';
}

static void copyText(char *to, size_t size, const char *from) {
  portENTER_CRITICAL(&stationLock);
  strncpy(to, from, size - 1);
  to[size - 1] = '\0';
  portEXIT_CRITICAL(&stationLock);
}

// the slots from NVS, a default where a slot was never written
static void loadStations() {
  if (stationsLoaded) return;
  Preferences store;
  bool open = store.begin(STATION_STORE, true);       // false when nothing was ever stored
  for (uint8_t i = 0; i < stationCount; i++) {
    char key[3];
    char text[RADIO_URL_LENGTH + 1];
    stationKey(key, 'n', i);
    size_t length = open && store.isKey(key) ? store.getString(key, text, sizeof(text)) : 0;
    copyText(stationNames[i], sizeof(stationNames[i]), length > 0 ? text : defaultNames[i]);
    stationKey(key, 'u', i);
    length = open && store.isKey(key) ? store.getString(key, text, sizeof(text)) : 0;
    copyText(stationUrls[i], sizeof(stationUrls[i]), length > 0 ? text : defaultUrls[i]);
  }
  if (open) store.end();
  stationsLoaded = true;
}

// the text without trailing zeros; false when a zero sits inside it
static bool trimText(const uint8_t *text, uint8_t &length) {
  while (length > 0 && text[length - 1] == 0) length--;
  for (uint8_t i = 0; i < length; i++) if (text[i] == 0) return false;
  return true;
}

static bool validName(const uint8_t *text, uint8_t length) {
  if (length == 0 || length > RADIO_NAME_MAX) return false;
  for (uint8_t i = 0; i < length; ) {
    uint8_t b = text[i];
    uint8_t follow;
    if (b < 0x20 || b == 0x7F) return false;          // no control characters
    if (b < 0x80) follow = 0;
    else if (b >= 0xC2 && b <= 0xDF) follow = 1;
    else if (b >= 0xE0 && b <= 0xEF) follow = 2;
    else if (b >= 0xF0 && b <= 0xF4) follow = 3;
    else return false;
    if (follow > 0 && i + follow >= length) return false;   // cut off at the end
    for (uint8_t k = 1; k <= follow; k++) if ((text[i + k] & 0xC0) != 0x80) return false;
    i += 1 + follow;
  }
  return true;
}

static bool validUrl(const uint8_t *text, uint8_t length) {
  static const char prefix[] = "http://";
  if (length <= sizeof(prefix) - 1 || length > RADIO_URL_LENGTH) return false;
  if (memcmp(text, prefix, sizeof(prefix) - 1) != 0) return false;
  for (uint8_t i = 0; i < length; i++) if (text[i] < 0x21 || text[i] > 0x7E) return false;
  return true;
}

// writes one slot's field to NVS; removes it when it equals the default
static bool storeText(char field, uint8_t index, const char *text, const char *fallback) {
  Preferences store;
  if (!store.begin(STATION_STORE, false)) return false;
  char key[3];
  stationKey(key, field, index);
  bool ok;
  if (strcmp(text, fallback) == 0) ok = !store.isKey(key) || store.remove(key);
  else ok = store.putString(key, text) == strlen(text);
  store.end();
  return ok;
}

// --- WiFi networks ---------------------------------------------------------------

// up to RADIO_WIFI_NETWORKS networks, tried in order; set over the serial
// port and kept in NVS (keys s0/p0, s1/p1). A password never leaves the
// board again.
#define WIFI_STORE                    "adl-wifi"

static char wifiSsids[RADIO_WIFI_NETWORKS][RADIO_SSID_MAX + 1];
static char wifiPasswords[RADIO_WIFI_NETWORKS][RADIO_PASSWORD_MAX + 1];
static bool wifiLoaded = false;
static volatile int8_t wifiSlotConnected = -1;
static volatile bool wifiRetryNow = false;         // a network changed: try again at once

static void loadWifi() {
  if (wifiLoaded) return;
  Preferences store;
  bool open = store.begin(WIFI_STORE, true);
  for (uint8_t i = 0; i < RADIO_WIFI_NETWORKS; i++) {
    char key[3];
    char text[RADIO_PASSWORD_MAX + 1];
    stationKey(key, 's', i);
    size_t length = open && store.isKey(key) ? store.getString(key, text, sizeof(text)) : 0;
    copyText(wifiSsids[i], sizeof(wifiSsids[i]), length > 0 ? text : "");
    stationKey(key, 'p', i);
    length = open && store.isKey(key) ? store.getString(key, text, sizeof(text)) : 0;
    copyText(wifiPasswords[i], sizeof(wifiPasswords[i]), length > 0 ? text : "");
  }
  if (open) store.end();
  wifiLoaded = true;
}

static bool printable(const char *text) {
  for (const char *c = text; *c != '\0'; c++) {
    if ((uint8_t)*c < 0x20 || (uint8_t)*c == 0x7F) return false;
  }
  return true;
}

// --- audio chain ----------------------------------------------------------------

// diagnostic: where the radio task is, and how often it went round
static volatile uint8_t taskStage = RADIO_STAGE_NONE;
static volatile uint32_t taskLoops = 0;

// the ICY stream with a client timeout of our own: the library's 60 s
// (URL_CLIENT_TIMEOUT, fixed for the ESP32) would hold a dead connection
// that long, and ICYStream does not pass setTimeout() on
class RadioStream : public ICYStream {
 public:
  void setClientTimeout(int milliseconds) { url.setTimeout(milliseconds); }

  size_t readBytes(uint8_t *data, size_t length) override {
    uint8_t stage = taskStage;
    taskStage = RADIO_STAGE_READ;
    size_t read = ICYStream::readBytes(data, length);
    taskStage = stage;
    return read;
  }
};

static RadioStream urlStream;         // no credentials: URLStream would block on its own SSID forever
static AudioSourceURL source(urlStream, playUrls, "audio/mp3");
static I2SStream i2s;
static MP3DecoderHelix decoder;

// diagnostic: passes the decoder's PCM on to the I2S and measures it
static volatile uint32_t pcmBytes = 0;
static volatile uint16_t pcmPeak = 0;
static volatile uint32_t mp3Bytes = 0;

class PcmMeter : public AudioStream {
 public:
  explicit PcmMeter(AudioStream &output) : output(output) {}

  size_t write(const uint8_t *data, size_t length) override {
    const int16_t *samples = (const int16_t *)data;
    uint16_t peak = pcmPeak;
    for (size_t i = 0; i < length / 2; i++) {
      int magnitude = abs((int)samples[i]);
      if (magnitude > peak) peak = (uint16_t)min(magnitude, 32767);
    }
    pcmPeak = peak;
    pcmBytes += length;
    uint8_t stage = taskStage;
    taskStage = RADIO_STAGE_I2S_WRITE;
    size_t written = output.write(data, length);
    taskStage = stage;
    return written;
  }

  int availableForWrite() override { return output.availableForWrite(); }

  void setAudioInfo(AudioInfo info) override {
    AudioStream::setAudioInfo(info);
    output.setAudioInfo(info);
  }

  AudioInfo audioInfo() override { return output.audioInfo(); }

 private:
  AudioStream &output;
};

static PcmMeter meter(i2s);
static AudioPlayer player(source, meter, decoder);

#define RADIO_TASK_STACK              8192
// core 0, beside WiFi: the firmware's loop never yields core 1, so the radio
// would only get what time slicing leaves it there
#define RADIO_TASK_CORE               0
#define RADIO_TASK_PRIORITY           2
#define RADIO_WIFI_TIMEOUT_MS         10000
#define RADIO_WIFI_RETRY_MS           30000
#define RADIO_STREAM_TIMEOUT_MS       8000    // no audio for this long = stream error
#define RADIO_STREAM_RETRY_MS         5000
#define RADIO_HTTP_TIMEOUT_MS         5000    // a connect or read may not block longer

static TaskHandle_t task = nullptr;
static volatile bool stopRequested = false;
static volatile int stationRequest = -1;
static volatile bool restartRequested = false;   // the playing station's URL changed
static volatile RadioState state = RADIO_IDLE;
static volatile uint32_t sampleRate = RADIO_DEFAULT_SAMPLE_RATE;
static volatile uint32_t reportedRate = RADIO_DEFAULT_SAMPLE_RATE;
static volatile bool rateChanged = false;
static uint8_t station = 0;
static char title[RADIO_TEXT_LENGTH];
static portMUX_TYPE titleLock = portMUX_INITIALIZER_UNLOCKED;
static bool i2sRunning = false;
static volatile bool testTone = false;

static void setTitle(const char *text, int length) {
  portENTER_CRITICAL(&titleLock);
  memset(title, 0, sizeof(title));
  if (text != nullptr && length > 0) {
    memcpy(title, text, min(length, (int)sizeof(title) - 1));
  }
  portEXIT_CRITICAL(&titleLock);
}

// some stations repeat empty titles every few seconds; keep the last real one
static void metadataCallback(MetaDataType type, const char *text, int length) {
  if (type == Title && length > 0 && text[0] != '\0') setTitle(text, length);
}

static bool startI2s(uint32_t rate) {
  auto config = i2s.defaultConfig(TX_MODE);
  config.pin_bck = PIN_I2S_BCLK;
  config.pin_ws = PIN_I2S_LRCLK;
  config.pin_data = PIN_I2S_DATA;
  config.pin_mck = PIN_MCLK;
  config.i2s_format = I2S_STD_FORMAT;       // ADAU1701 serial input mode 0 = I2S
  config.use_apll = true;
  config.mclk_multiple = 256;               // ADAU1701 PLL mode 256 x fs
  config.sample_rate = rate;
  config.channels = 2;
  config.bits_per_sample = 16;
  i2sRunning = i2s.begin(config);
  return i2sRunning;
}

// waits in small steps so a stop request is honoured within milliseconds
static bool waitFor(uint32_t milliseconds, bool (*done)()) {
  uint32_t start = millis();
  while (millis() - start < milliseconds) {
    if (stopRequested) return false;
    if (done != nullptr && done()) return true;
    vTaskDelay(pdMS_TO_TICKS(50));
  }
  return done != nullptr && done();
}

static bool wifiConnected() {
  return WiFi.status() == WL_CONNECTED;
}

static bool connectWifi() {
  loadWifi();
  wifiSlotConnected = -1;
  for (uint8_t i = 0; i < RADIO_WIFI_NETWORKS; i++) {
    char ssid[RADIO_SSID_MAX + 1];
    char password[RADIO_PASSWORD_MAX + 1];
    copyText(ssid, sizeof(ssid), wifiSsids[i]);
    copyText(password, sizeof(password), wifiPasswords[i]);
    if (ssid[0] == '\0') continue;
    state = RADIO_CONNECTING_WIFI;
    WiFi.disconnect();
    WiFi.begin(ssid, password[0] != '\0' ? password : nullptr);
    if (waitFor(RADIO_WIFI_TIMEOUT_MS, wifiConnected)) {
      wifiSlotConnected = (int8_t)i;
      return true;
    }
    if (stopRequested) return false;
  }
  state = RADIO_NO_NETWORK;
  return false;
}

static void play(uint8_t index) {
  station = index;
  setTitle(nullptr, 0);
  state = RADIO_CONNECTING_STREAM;
  taskStage = RADIO_STAGE_CONNECT;
  copyText(playUrl, sizeof(playUrl), stationUrls[index]);
  player.begin(0, true);
  player.setAutoNext(false);                // begin() takes the source's setting again
}

// ends the playing and closes the connection: the player's end() leaves the
// socket open, and a socket that outlives WiFi crashes lwip when it is
// closed later (its buffers belong to the interface that is gone)
static void stopStream() {
  player.end();
  urlStream.end();
}

// one block of a 1 kHz sine at a quarter of full scale, 16-bit stereo
static void writeTestTone() {
  static float phase = 0;
  int16_t frames[256 * 2];
  const float step = 2.0f * (float)M_PI * 1000.0f / (float)sampleRate;
  for (int i = 0; i < 256; i++) {
    int16_t sample = (int16_t)(8192.0f * sinf(phase));
    frames[2 * i] = sample;
    frames[2 * i + 1] = sample;
    phase += step;
    if (phase > 2.0f * (float)M_PI) phase -= 2.0f * (float)M_PI;
  }
  i2s.write((const uint8_t *)frames, sizeof(frames));
}

static void radioTask(void *) {
  urlStream.setClientTimeout(RADIO_HTTP_TIMEOUT_MS);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  player.setVolume(1.0);                    // the volume is the DSP's
  player.setMetadataCallback(metadataCallback);

  bool streaming = false;
  uint32_t lastAudio = 0;
  uint32_t retryAt = 0;

  while (!stopRequested) {
    taskLoops = taskLoops + 1;
    if (testTone) {
      if (streaming) {
        stopStream();
        streaming = false;
      }
      taskStage = RADIO_STAGE_TEST_TONE;
      writeTestTone();
      continue;
    }

    if (wifiRetryNow) {                     // a network was set or cleared
      wifiRetryNow = false;
      retryAt = 0;
      if (wifiConnected()) WiFi.disconnect();
    }

    if (!wifiConnected()) {
      wifiSlotConnected = -1;
      if (streaming) {
        stopStream();
        streaming = false;
      }
      taskStage = RADIO_STAGE_WIFI;
      if (millis() >= retryAt && !connectWifi()) {
        retryAt = millis() + RADIO_WIFI_RETRY_MS;
      }
      if (!wifiConnected()) {
        vTaskDelay(pdMS_TO_TICKS(100));
        continue;
      }
    }

    // a request for the station already playing changes nothing
    int request = stationRequest;
    if (request >= 0) {
      stationRequest = -1;
      if (!streaming || request != station) {
        station = (uint8_t)request;
        streaming = false;
      }
    }
    if (restartRequested) {
      restartRequested = false;
      streaming = false;
    }

    if (!streaming) {
      if (state == RADIO_STREAM_ERROR && millis() < retryAt) {
        vTaskDelay(pdMS_TO_TICKS(100));
        continue;
      }
      play(station);
      streaming = true;
      lastAudio = millis();
    }

    taskStage = RADIO_STAGE_COPY;
    size_t copied = player.copy();
    taskStage = RADIO_STAGE_IDLE;
    mp3Bytes += copied;
    if (copied > 0) {
      lastAudio = millis();
      state = RADIO_PLAYING;
    } else if (millis() - lastAudio > RADIO_STREAM_TIMEOUT_MS) {
      stopStream();
      streaming = false;
      state = RADIO_STREAM_ERROR;
      retryAt = millis() + RADIO_STREAM_RETRY_MS;
    }

    // the DSP's rate follows MCLK = 256 x the stream's rate
    uint32_t rate = i2s.audioInfo().sample_rate;
    if (rate != 0 && rate != sampleRate) {
      sampleRate = rate;
      reportedRate = rate;
      rateChanged = true;
    }

    if (copied == 0) vTaskDelay(1);          // let the firmware's loop in while waiting for data
  }

  stopStream();                             // before WiFi goes
  wifiSlotConnected = -1;
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  taskStage = RADIO_STAGE_NONE;
  task = nullptr;
  vTaskDelete(nullptr);
}

bool radioBegin(uint8_t index) {
  if (task != nullptr) radioEnd();
  loadStations();
  loadWifi();
  station = index < stationCount ? index : 0;
  stationRequest = -1;
  stopRequested = false;
  sampleRate = RADIO_DEFAULT_SAMPLE_RATE;
  reportedRate = RADIO_DEFAULT_SAMPLE_RATE;
  rateChanged = false;
  setTitle(nullptr, 0);
  if (!startI2s(RADIO_DEFAULT_SAMPLE_RATE)) return false;
  state = RADIO_CONNECTING_WIFI;
  return xTaskCreatePinnedToCore(radioTask, "radio", RADIO_TASK_STACK, nullptr,
                                 RADIO_TASK_PRIORITY, &task, RADIO_TASK_CORE) == pdPASS;
}

void radioEnd() {
  if (task != nullptr) {
    stopRequested = true;
    for (int i = 0; i < 100 && task != nullptr; i++) delay(20);   // the task ends itself
  }

  if (i2sRunning) {
    i2s.end();
    i2sRunning = false;
  }

  state = RADIO_IDLE;
  setTitle(nullptr, 0);
}

void radioSelect(uint8_t index) {
  if (index < stationCount) stationRequest = index;
}

uint8_t radioStationCount() {
  return stationCount;
}

const char *radioStationName(uint8_t index) {
  loadStations();
  return index < stationCount ? stationNames[index] : nullptr;
}

const char *radioStationUrl(uint8_t index) {
  loadStations();
  return index < stationCount ? stationUrls[index] : nullptr;
}

RadioEdit radioSetStationName(uint8_t index, const uint8_t *text, uint8_t length) {
  loadStations();
  if (index >= stationCount || !trimText(text, length) || !validName(text, length)) return RADIO_EDIT_INVALID;
  char name[RADIO_NAME_MAX + 1];
  memcpy(name, text, length);
  name[length] = '\0';
  if (strcmp(name, stationNames[index]) == 0) return RADIO_EDIT_OK;
  if (!storeText('n', index, name, defaultNames[index])) return RADIO_EDIT_STORE_FAILED;
  copyText(stationNames[index], sizeof(stationNames[index]), name);
  return RADIO_EDIT_OK;
}

// a new URL for the station playing takes effect at once
static void restartIfPlaying(uint8_t index) {
  if (task != nullptr && index == station) restartRequested = true;
}

RadioEdit radioSetStationUrl(uint8_t index, const uint8_t *text, uint8_t length) {
  loadStations();
  if (index >= stationCount || !trimText(text, length) || !validUrl(text, length)) return RADIO_EDIT_INVALID;
  char url[RADIO_URL_LENGTH + 1];
  memcpy(url, text, length);
  url[length] = '\0';
  if (strcmp(url, stationUrls[index]) == 0) return RADIO_EDIT_OK;
  if (!storeText('u', index, url, defaultUrls[index])) return RADIO_EDIT_STORE_FAILED;
  copyText(stationUrls[index], sizeof(stationUrls[index]), url);
  restartIfPlaying(index);
  return RADIO_EDIT_OK;
}

RadioEdit radioRestoreStations(uint8_t index) {
  loadStations();
  if (index >= stationCount && index != RADIO_STATION_ALL) return RADIO_EDIT_INVALID;
  for (uint8_t i = 0; i < stationCount; i++) {
    if (index != RADIO_STATION_ALL && i != index) continue;
    if (!storeText('n', i, defaultNames[i], defaultNames[i])) return RADIO_EDIT_STORE_FAILED;
    if (!storeText('u', i, defaultUrls[i], defaultUrls[i])) return RADIO_EDIT_STORE_FAILED;
    copyText(stationNames[i], sizeof(stationNames[i]), defaultNames[i]);
    if (strcmp(stationUrls[i], defaultUrls[i]) != 0) {
      copyText(stationUrls[i], sizeof(stationUrls[i]), defaultUrls[i]);
      restartIfPlaying(i);
    }
  }
  return RADIO_EDIT_OK;
}

RadioState radioState() {
  return state;
}

uint32_t radioSampleRate() {
  return reportedRate;
}

void radioStreamTitle(char *out, size_t length) {
  portENTER_CRITICAL(&titleLock);
  memset(out, 0, length);
  memcpy(out, title, min(length, sizeof(title)));
  portEXIT_CRITICAL(&titleLock);
}

void radioTestTone(bool on) {
  testTone = on;
}

void radioTakePcmMeter(uint32_t &bytes, uint16_t &peak, uint32_t &streamBytes,
                       uint8_t &stage, uint32_t &loops, uint8_t &taskState) {
  TaskHandle_t handle = task;
  taskState = handle != nullptr ? (uint8_t)eTaskGetState(handle) : 0xFF;
  bytes = pcmBytes;
  peak = pcmPeak;
  streamBytes = mp3Bytes;
  stage = taskStage;
  loops = taskLoops;
  pcmBytes = 0;
  pcmPeak = 0;
  mp3Bytes = 0;
  taskLoops = 0;
}

bool radioTakeRateChange(uint32_t &rate) {
  if (!rateChanged) return false;
  rateChanged = false;
  rate = reportedRate;
  return true;
}

// --- WiFi networks (public) -------------------------------------------------------

const char *radioWifiSsid(uint8_t slot) {
  if (slot >= RADIO_WIFI_NETWORKS) return nullptr;
  loadWifi();
  return wifiSsids[slot];
}

bool radioWifiHasPassword(uint8_t slot) {
  if (slot >= RADIO_WIFI_NETWORKS) return false;
  loadWifi();
  return wifiPasswords[slot][0] != '\0';
}

// an SSID of 1..32 bytes; a password empty (open network) or 8..63
// printable characters (WPA2); an empty SSID clears the slot
RadioEdit radioSetWifi(uint8_t slot, const char *ssid, const char *password) {
  if (slot >= RADIO_WIFI_NETWORKS || ssid == nullptr || password == nullptr) return RADIO_EDIT_INVALID;
  size_t ssidLength = strlen(ssid);
  size_t passwordLength = strlen(password);
  if (ssidLength > RADIO_SSID_MAX || !printable(ssid)) return RADIO_EDIT_INVALID;
  if (ssidLength == 0 && passwordLength != 0) return RADIO_EDIT_INVALID;
  if (passwordLength != 0 && (passwordLength < 8 || passwordLength > RADIO_PASSWORD_MAX || !printable(password))) {
    return RADIO_EDIT_INVALID;
  }
  loadWifi();
  Preferences store;
  if (!store.begin(WIFI_STORE, false)) return RADIO_EDIT_STORE_FAILED;
  char ssidKey[3];
  char passwordKey[3];
  stationKey(ssidKey, 's', slot);
  stationKey(passwordKey, 'p', slot);
  bool ok;
  if (ssidLength == 0) {
    ok = (!store.isKey(ssidKey) || store.remove(ssidKey)) && (!store.isKey(passwordKey) || store.remove(passwordKey));
  } else {
    ok = store.putString(ssidKey, ssid) == ssidLength;
    if (ok) ok = passwordLength == 0 ? (!store.isKey(passwordKey) || store.remove(passwordKey))
                                     : store.putString(passwordKey, password) == passwordLength;
  }
  store.end();
  if (!ok) return RADIO_EDIT_STORE_FAILED;
  copyText(wifiSsids[slot], sizeof(wifiSsids[slot]), ssid);
  copyText(wifiPasswords[slot], sizeof(wifiPasswords[slot]), password);
  wifiRetryNow = true;
  return RADIO_EDIT_OK;
}

int8_t radioWifiConnected() {
  return task != nullptr && WiFi.status() == WL_CONNECTED ? wifiSlotConnected : -1;
}

void radioWifiAddress(char *out, size_t length) {
  if (radioWifiConnected() < 0) {
    copyText(out, length, "");
    return;
  }
  copyText(out, length, WiFi.localIP().toString().c_str());
}
