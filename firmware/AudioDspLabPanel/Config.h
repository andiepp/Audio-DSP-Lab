// ****************************************************************************
// ***                  P A N E L   C O N F I G U R A T I O N               ***
// ****************************************************************************
//  The Audio-DSP-Lab front panel on an ESP32-2432S028R ("CYD", 2.8"
//  320x240 display with resistive touch). It talks the board's protocol
//  (docs/Protocol.md) over a cable to the Audio-DSP-FX board's panel
//  port: CYD GPIO22 (TX) to the board's GPIO37, CYD GPIO35 (RX) to the
//  board's GPIO4, and GND.
// ----------------------------------------------------------------------------

#ifndef PANEL_CONFIG_H
#define PANEL_CONFIG_H

#define PANEL_NAME                    "Audio-DSP-Lab panel"
#define PANEL_VERSION                 "1.4"

// display: the one-USB ESP32-2432S028R carries an ILI9341, the two-USB
// (USB-C) version an ST7789; 1 selects the ST7789
#define DISPLAY_ST7789                0
#define DISPLAY_WIDTH                 320
#define DISPLAY_HEIGHT                240
#define PIN_TFT_SCK                   14
#define PIN_TFT_MOSI                  13
#define PIN_TFT_MISO                  12
#define PIN_TFT_CS                    15
#define PIN_TFT_DC                    2
#define PIN_TFT_BACKLIGHT             21
#define TFT_SPI_HZ                    40000000

// touch: XPT2046 on its own SPI bus
#define PIN_TOUCH_CLK                 25
#define PIN_TOUCH_MOSI                32
#define PIN_TOUCH_MISO                39
#define PIN_TOUCH_CS                  33
#define PIN_TOUCH_IRQ                 36
// raw readings at the screen's edges (landscape, as the panel shows it),
// measured from corner taps on the user's CYD: the controller's Y channel
// runs left to right, its X channel top to bottom. Refine with the touch
// log on the USB port.
#define TOUCH_RAW_LEFT                320
#define TOUCH_RAW_RIGHT               3670
#define TOUCH_RAW_TOP                 350
#define TOUCH_RAW_BOTTOM              3880
#define TOUCH_PRESSURE_MIN            400

// RGB LED, active low
#define PIN_LED_RED                   4
#define PIN_LED_GREEN                 16
#define PIN_LED_BLUE                  17

// the link to the board's panel port (UART2)
#define PIN_LINK_RX                   35
#define PIN_LINK_TX                   22
#define LINK_BAUD_RATE                115200
#define LINK_LINE_MAX                 512
#define LINK_RX_BUFFER                16384   // a whole spectrum answer (about 7 KB) while the screen draws
#define LINK_SPECTRUM_POINTS          256     // the board's whole spectrum, 20 Hz to 24 kHz
#define LINK_SPECTRUM_POINTS_TEXT     "256"
#define LINK_SYNC_MS                  400     // between two resync echoes
// the TX line is driven only while the board's own TX line lives: a running
// UART holds it high between characters, a board without power cannot. A
// TX line held at 3.3 V would feed an unplugged board's ESP32 through
// GPIO37, and its next power-up would not be a clean one.
#define LINK_LINE_CHECK_MS            40      // the board's line is looked at this often
#define LINK_LINE_SAMPLES             30      // ... 10 us apart: low all through, longer than two characters
#define LINK_LINE_DEAD_CHECKS         2       // low at this many checks in a row: the board is gone
#define LINK_IDLE_ECHO_MS             3000    // a quiet link is checked this often
#define LINK_ANSWER_MS                2500    // a request unanswered this long loses the link
#define LINK_SLOW_ANSWER_MS           8000    // an application start writes the EEPROM

// the USB port: a log for development (touches, link state)
#define LOG_BAUD_RATE                 115200

#endif
