// ****************************************************************************
// ***                                                                      ***
// ***                     A U D I O - D S P - L A B                        ***
// ***                                                                      ***
// ****************************************************************************
//  HEADER:
//  - Target:       ESP32 PICO-D4 on the Elektor Audio DSP FX board v2.2
//  - Repository:   https://github.com/andiepp/Audio-DSP-Lab
//  - Licence:      MIT for this firmware's own code; the compiled firmware
//                  includes GPL-3.0 libraries (THIRD-PARTY-NOTICES.md)
//  DESCRIPTION:
//  - Six audio applications on the Elektor board, controlled from a PC over
//    the board's USB serial port with a simple text protocol
//    (docs/Protocol.md): signal generator, equalizer, internet radio,
//    frequency response analyzer, SigmaStudio mode and a monophonic
//    synthesizer played over MIDI.
//  - An application is a DSP program (written to the DSP's EEPROM when it
//    is selected), its parameters and what the four pots do. Every
//    power-up starts the SigmaStudio mode (the DSP boots the EEPROM, the
//    firmware stays off the I2C bus and passes the pots to the DSP's aux
//    ADC), or, with the startup option on, the application selected last.
//  - The firmware supplies the DSP clock (JP1 on 'ext. MCLK').
//  See Config.h (pins, settings), Protocol.cpp (commands), Applications.cpp
//  (applications and their parameters).
// ----------------------------------------------------------------------------

#include <Wire.h>
#include "Config.h"
#include "Protocol.h"
#include "Applications.h"
#include "Dsp.h"
#include "Eeprom24.h"
#include "Mclk.h"
#include "Pots.h"

void setup() {
  dspInit();                          // DSP held in reset first
  eepromInit();
  protocolInit();                     // requests wait in the buffer until ready
  pinMode(PIN_SELFBOOT, INPUT);

  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL, I2C_CLOCK_HZ);
  mclkStart(DSP_SAMPLE_RATE);         // the DSP needs its clock before reset ends
  potsInit();
  appsInit();                         // the SigmaStudio mode or the last selection
  protocolReady();
}

void loop() {
  protocolPoll();
  if (!appInServiceMode()) appsPoll();
}

// ============================================================================
// ==================================  E O F  =================================
// ============================================================================
