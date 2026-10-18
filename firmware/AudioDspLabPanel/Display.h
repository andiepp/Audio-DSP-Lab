// ****************************************************************************
// ***                D I S P L A Y   A N D   T O U C H                     ***
// ****************************************************************************
//  The display through LVGL's ILI9341/ST7789 driver on the CYD's display
//  SPI bus, the XPT2046 touch controller on its own bus as LVGL's pointer,
//  and the RGB LED.
// ----------------------------------------------------------------------------

#ifndef PANEL_DISPLAY_H
#define PANEL_DISPLAY_H

#include <stdint.h>

void displayBegin();      // LVGL, the display, the touch, the LED
void displayPoll();       // LVGL's timers: call often
void displayCapture();    // the screen's pixels over USB (a screenshot)

// the backlight, 10 .. 100 %, kept in the panel's flash
void displaySetBrightness(int percent);
int displayBrightness();

enum LedColour : uint8_t { LED_OFF, LED_GREEN, LED_RED, LED_AMBER, LED_BLUE };
void ledSet(LedColour colour);

#endif
