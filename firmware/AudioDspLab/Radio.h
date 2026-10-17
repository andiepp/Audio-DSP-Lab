// ****************************************************************************
// ***                       I N T E R N E T   R A D I O                    ***
// ****************************************************************************
//  Application 0x03: MP3 internet radio over WiFi, decoded on the
//  ESP32 (arduino-audio-tools, libhelix) and sent to the DSP over I2S.
//
//  While the radio runs, its I2S channel drives the DSP's MCLK at 256 x the
//  stream's sample rate, so the DSP's rate follows the stream. Streaming,
//  WiFi and decoding run in a task of their own; the task never touches
//  I2C. Everything it learns (sample rate, title, state) is read by the
//  firmware's loop, which alone talks to the DSP.
//
//  Stations: eight slots, each a name and an
//  http:// URL, kept in NVS (namespace "aml-radio") and edited over the serial port;
//  a slot never written holds its default. Only the firmware's loop edits them;
//  the task copies the URL it plays under a lock.
//
//  WiFi: up to two networks (SSID and password), set over the serial port
//  and kept in NVS (namespace "adl-wifi"); the radio tries them in order.
//  Without one the radio reports "no network".
// ----------------------------------------------------------------------------

#ifndef RADIO_H
#define RADIO_H

#include <stdint.h>
#include <stddef.h>

#define RADIO_DEFAULT_SAMPLE_RATE     44100   // the I2S rate before a stream says otherwise
#define RADIO_TEXT_LENGTH             64      // one text field on the wire, zero padded
#define RADIO_STATION_COUNT           8
#define RADIO_NAME_MAX                63      // bytes of UTF-8, so a name fits a text field
#define RADIO_URL_LENGTH              200     // a URL's field on the wire, zero padded
#define RADIO_STATION_ALL             0xFF    // radioRestoreStations: every slot
#define RADIO_WIFI_NETWORKS           2
#define RADIO_SSID_MAX                32
#define RADIO_PASSWORD_MAX            63

enum RadioEdit : uint8_t {
  RADIO_EDIT_OK            = 0,
  RADIO_EDIT_INVALID       = 1,    // empty, too long, not UTF-8 or not http://
  RADIO_EDIT_STORE_FAILED  = 2,    // NVS refused the write
};

enum RadioState : uint8_t {
  RADIO_IDLE              = 0,    // the radio is not the running application
  RADIO_CONNECTING_WIFI   = 1,
  RADIO_CONNECTING_STREAM = 2,
  RADIO_PLAYING           = 3,
  RADIO_NO_NETWORK        = 4,    // no credentials, or neither network answered
  RADIO_STREAM_ERROR      = 5,    // the station did not deliver; retried
};

// takes the DSP clock (I2S at the default rate) and starts the radio task
bool radioBegin(uint8_t station);
// stops the task, the stream, WiFi and the I2S channel; idempotent
void radioEnd();
// asks the task to play another station (index into the list)
void radioSelect(uint8_t station);

uint8_t radioStationCount();
const char *radioStationName(uint8_t index);   // nullptr when out of range
const char *radioStationUrl(uint8_t index);    // nullptr when out of range
// a slot's name or URL, as raw bytes (trailing zeros ignored); a changed URL
// of the station playing restarts its stream
RadioEdit radioSetStationName(uint8_t index, const uint8_t *text, uint8_t length);
RadioEdit radioSetStationUrl(uint8_t index, const uint8_t *text, uint8_t length);
// back to the default of one slot, or of all with RADIO_STATION_ALL
RadioEdit radioRestoreStations(uint8_t index);
RadioState radioState();

// WiFi networks: the SSID of a slot ("" when empty, nullptr out of range),
// whether it has a password (the password itself is never read back)
const char *radioWifiSsid(uint8_t slot);
bool radioWifiHasPassword(uint8_t slot);
// sets a slot (an empty SSID clears it); the radio tries again at once
RadioEdit radioSetWifi(uint8_t slot, const char *ssid, const char *password);
// the slot the radio is connected through, -1 when not connected
int8_t radioWifiConnected();
void radioWifiAddress(char *out, size_t length);
uint32_t radioSampleRate();
// copies the stream title (from the ICY metadata), zero padded
void radioStreamTitle(char *out, size_t length);
// true once per change of the stream's sample rate, with the new rate
bool radioTakeRateChange(uint32_t &rate);
// diagnostic: a 1 kHz sine written to the I2S instead of the stream
void radioTestTone(bool on);
// diagnostic: where the radio task is
enum RadioStage : uint8_t {
  RADIO_STAGE_NONE      = 0,    // no task
  RADIO_STAGE_WIFI      = 1,    // connecting or waiting for WiFi
  RADIO_STAGE_CONNECT   = 2,    // opening the stream
  RADIO_STAGE_COPY      = 3,    // reading and decoding
  RADIO_STAGE_I2S_WRITE = 4,    // writing PCM to the I2S
  RADIO_STAGE_IDLE      = 5,    // between copies
  RADIO_STAGE_TEST_TONE = 6,
  RADIO_STAGE_READ      = 7,    // reading from the connection
};
// diagnostic: PCM bytes the decoder handed to the I2S since the last call,
// their peak magnitude (16-bit), MP3 bytes read, the task's stage, its
// loop count and its FreeRTOS state (eTaskState, 0xFF: no task); counts
// reset by the call
void radioTakePcmMeter(uint32_t &bytes, uint16_t &peak, uint32_t &streamBytes,
                       uint8_t &stage, uint32_t &loops, uint8_t &taskState);

#endif
