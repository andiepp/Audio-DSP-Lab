// ****************************************************************************
// ***               A U D I O - D S P - L A B   P A N E L                  ***
// ****************************************************************************
//  A touch front panel for the Audio-DSP-Lab board on an ESP32-2432S028R
//  ("CYD"). It speaks the board's protocol over a cable to the board's
//  panel port (docs/Protocol.md), side by side with the PC application.
//
//  Build: Arduino ESP32 core 3.3.10, board "ESP32 Dev Module"
//  (esp32:esp32:esp32), libraries from firmware/libraries (LVGL 9.2.2,
//  configured by firmware/libraries/lv_conf.h).
//
//  The USB port carries a log at 115200: the link's state and every
//  touch's raw and screen position (for the calibration in Config.h).
// ----------------------------------------------------------------------------

#include "Config.h"
#include "Display.h"
#include "Link.h"
#include "Ui.h"

void setup() {
  Serial.begin(LOG_BAUD_RATE);
  Serial.println(PANEL_NAME " " PANEL_VERSION);
  displayBegin();
  Serial.println("display up");
  uiBegin();
  linkBegin();
  Serial.println("link started");
}

void loop() {
  linkPoll();
  uiPoll();
  displayPoll();
  delay(2);
}
