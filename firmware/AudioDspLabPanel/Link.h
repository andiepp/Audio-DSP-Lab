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

struct Board {
  bool linked = false;
  String firmware;          // "1.0"
  String kit;               // "esp32-pico-kit-1"
  String application;       // "generator", "none", ...
  String state;             // "running", "loading", ...
  String lastError;         // "none", ...
  bool potControl = false;
  uint16_t pots[4] = { 0, 0, 0, 0 };
  std::vector<Param> params;
  uint32_t paramsVersion = 0;   // a new list (another application)
  uint32_t version = 0;         // anything changed
  String message;               // the last error answered, for a toast
};

extern Board board;

void linkBegin();
void linkPoll();

int linkFindParam(const char *name);
void linkSetParam(int index, const String &value);   // coalesced
void linkSelectApplication(const char *name);
void linkSetPotControl(bool on);

#endif
