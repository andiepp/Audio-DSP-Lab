// ****************************************************************************
// ***                               L I N K                                ***
// ****************************************************************************
//  The board's protocol over the cable, as a host: it finds the board
//  with "echo" (noise and restarts on either side need a resync), reads
//  what runs and its parameters, follows the events, and writes values,
//  one request in flight and only the newest value of each parameter.
//  The screens read the model and call the actions below.
// ----------------------------------------------------------------------------

#ifndef PANEL_LINK_H
#define PANEL_LINK_H

#include <Arduino.h>
#include <vector>

enum ParamKind : uint8_t { KIND_NUMBER, KIND_INTEGER, KIND_CHOICE, KIND_SWITCH };

struct Param {
  String name;
  ParamKind kind;
  String value;             // as the board writes it: "440.13", "triangle", "on"
  double minimum;
  double maximum;
  String unit;              // "" for none
  std::vector<String> choices;
  int8_t pot;               // 0..3 = P1..P4, -1 none
  // writes
  String wanted;            // the newest value asked for
  bool dirty;               // wanted not yet sent
  bool inFlight;            // a set is on its way
};

struct SweepPoint {
  float hertz;
  float left;               // dBFS
  float right;
};

struct Board {
  bool linked = false;
  String firmware;          // "1.0"
  String kit;               // "esp32-pico-kit-1"
  String application;       // "generator", "none", ...
  String state;             // "running", "loading", ...
  String lastError;         // "none", ...
  bool potControl = false;
  bool startupRestores = false; // power-up: the application selected last, or the SigmaStudio mode
  bool gate = false;            // a synthesizer note sounds
  uint16_t pots[4] = { 0, 0, 0, 0 };
  std::vector<Param> params;
  uint32_t paramsVersion = 0;   // a new list (another application)
  uint32_t version = 0;         // anything changed
  String message;               // the last error answered, for a toast
  // the radio
  String radioState;            // "playing", "connecting-wifi", ...
  uint32_t radioRate = 0;
  String title;                 // the stream title
  std::vector<String> stations; // the eight slots' names
  uint32_t stationsVersion = 0;
  // the analysis: its Frequency Response
  String sweepState;            // "idle", "running", "done", ...
  int sweepMeasured = 0;
  int sweepTotal = 0;
  std::vector<SweepPoint> points;
  uint32_t pointsVersion = 0;
  // its THD: the latest result (NAN for '-') and its spectrum
  String thdState;              // "idle", "running", "failed"
  int thdResults = 0;
  float thdHertz = NAN;
  float thdLevel[2] = { NAN, NAN };   // the fundamental's peak, dBFS
  float thd[2] = { NAN, NAN };        // dB
  float thdn[2] = { NAN, NAN };
  std::vector<SweepPoint> spectrum;   // 64 points: per point the strongest bin, dBFS
  uint32_t spectrumVersion = 0;
};

extern Board board;

void linkBegin();
void linkPoll();

int linkFindParam(const char *name);
void linkSetParam(int index, const String &value);   // coalesced
void linkSelectApplication(const char *name);
void linkSetPotControl(bool on);
void linkSweep(bool start);                          // starts or stops a sweep
void linkDistortion(bool start);                     // starts or stops the THD measurement
void linkReadSettings();                             // pot control and the power-up choice, read again
void linkSetStartup(bool restores);

// the PC on the network (Network.cpp): its lines in, and whether it is there
void linkRemoteLine(const String &line);
void linkRemoteConnected(bool connected);

#endif
