// ****************************************************************************
// ***                   A P P L I C A T I O N   M A N A G E R              ***
// ****************************************************************************
//  An application = ADAU1701 EEPROM image + parameter table + pot mapping.
//  Selecting one holds the DSP in reset, writes and verifies the image,
//  lets the DSP self-boot and applies every parameter's default. Leaving
//  service mode restarts the application with the values it had, except
//  those marked resetOnStart (the generator's output), which take their
//  default.
//
//  The SigmaStudio mode (0x05) is the one application
//  without an image: the DSP boots whatever the EEPROM holds, the firmware
//  stays off the I2C bus, keeps the EEPROM writable and forwards the pots
//  to the DSP's auxiliary ADC (DacMux.h). The firmware starts it at every
//  power-up; another application lasts until the next power cycle. The
//  your SigmaStudio program survives the other applications in a copy in the
//  ESP32's flash (StudioBackup.h).
//
//  The startup option, kept in NVS: on, a
//  power-up starts the application selected last instead, with its
//  defaults (the radio's and the synthesizer's output on), and falls back
//  to the SigmaStudio mode if it cannot start.
//  Service mode is never restored.
//
//  The firmware owns every parameter value; writes from the PC and, while pot control
//  is on, potentiometers change it. Pot control is off at every power-up.
//  With it on, every start gives a pot-driven parameter its pot's position;
//  after a write from the PC a pot takes control again only once it moved more
//  than POT_PICKUP_THRESHOLD. Turning pot control on does the same: the
//  pots take over once moved, nothing jumps.
// ----------------------------------------------------------------------------

#ifndef APPLICATIONS_H
#define APPLICATIONS_H

#include <stdint.h>
#include <stddef.h>

#define APP_NONE                      0x00
#define APP_RESPONSE                  0x04
#define APP_SIGMASTUDIO               0x05
#define APP_SYNTH                     0x06

// response analyzer (0x04) parameters
#define RESP_LEVEL                    1       // 0.01 dBFS, -60 .. 0, the output's level
#define RESP_OUTPUT                   2       // 0 off, 1 on
#define RESP_START                    3       // 0.01 Hz, 20 Hz .. 20 kHz, the sweep's first point
#define RESP_STOP                     4       // 0.01 Hz, the sweep's last point
#define RESP_POINTS_PER_DECADE        5       // 1 .. 40
#define RESP_TONE                     6       // 0.01 Hz, the tone between sweeps

enum AppState : uint8_t {
  APP_STATE_IDLE    = 0,    // no application selected
  APP_STATE_LOADING = 1,
  APP_STATE_RUNNING = 2,
  APP_STATE_FAILED  = 3,
  APP_STATE_SERVICE = 4,    // DSP open for SigmaStudio/USBi, firmware off the bus
};

struct ParamDef {
  uint8_t id;
  int32_t minimum;          // scaled integer units, see each application
  int32_t maximum;
  int32_t initial;
  bool (*apply)(int32_t value);
  bool resetOnStart;        // takes its default even when a start resumes
};

struct PotMap {
  uint8_t pot;              // 0..3 = P1..P4
  uint8_t paramId;
  int32_t (*map)(uint16_t raw);
};

struct AppDef {
  uint8_t id;
  const char *name;
  const uint8_t *image;
  size_t imageLength;
  const ParamDef *params;
  uint8_t paramCount;
  const PotMap *pots;
  uint8_t potCount;
  bool (*begin)();          // takes the DSP clock before the DSP boots; nullptr: the firmware's 48 kHz
  void (*end)();            // releases what begin took; must be idempotent
};

void appsInit();                            // the SigmaStudio mode, or the last selection
void appsPoll();                            // pots -> parameters

uint8_t appActive();
AppState appState();
uint8_t appLastError();
uint8_t appSelect(uint8_t id);              // returns an error code (Errors.h)
uint8_t appParamGet(uint8_t paramId, int32_t &value);
uint8_t appParamSet(uint8_t paramId, int32_t value);
uint8_t appServiceMode(bool enable);
bool appInServiceMode();
void appPotControl(bool enable);
bool appPotControlEnabled();
uint8_t appStartupSet(bool restores);       // the startup option; ERR_STORE_FAILED
void appStartupGet(bool &restores, uint8_t &id);
bool appRunning(uint8_t id);                // the application runs (not loading, failed or in service)
// the applications the firmware knows, in their order
uint8_t appCount();
const AppDef *appAt(uint8_t index);
// the running application's parameters (nullptr past the last one)
uint8_t appParamCount();
// the pot (0..3 = P1..P4) driving a parameter of the running application, -1 for none
int8_t appParamPot(uint8_t paramId);
const ParamDef *appParamAt(uint8_t index);
// told of every change of a value that did not come from appParamSet:
// a pot, or the generator's frequency following its P1 range
typedef void (*ParamListener)(uint8_t paramId, int32_t value);
void appOnParamChanged(ParamListener listener);
// diagnostic: the backup's view of the EEPROM (StudioBackup.h)
uint8_t appStudioScan(uint16_t &length, bool &nodeImage, uint32_t &crc, uint16_t &stoppedAt);

#endif
