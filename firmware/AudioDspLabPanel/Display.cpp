#include "Display.h"
#include "Config.h"
#include <Arduino.h>
#include <SPI.h>
#include <lvgl.h>
// LVGL 9.2 leaves its MIPI panel drivers out of lvgl.h
#include <drivers/display/ili9341/lv_ili9341.h>
#include <drivers/display/st7789/lv_st7789.h>

#define DRAW_BUFFER_LINES             20

static SPIClass displaySpi(HSPI);
static SPIClass touchSpi(VSPI);
// LVGL wants its draw buffers aligned (LV_DRAW_BUF_ALIGN)
alignas(4) static uint8_t drawBuffer1[DISPLAY_WIDTH * DRAW_BUFFER_LINES * 2];
alignas(4) static uint8_t drawBuffer2[DISPLAY_WIDTH * DRAW_BUFFER_LINES * 2];

// --- display ------------------------------------------------------------------------

static void displayWrite(const uint8_t *cmd, size_t cmdSize, const uint8_t *param, size_t paramSize) {
  displaySpi.beginTransaction(SPISettings(TFT_SPI_HZ, MSBFIRST, SPI_MODE0));
  digitalWrite(PIN_TFT_CS, LOW);
  digitalWrite(PIN_TFT_DC, LOW);
  displaySpi.writeBytes(cmd, cmdSize);
  digitalWrite(PIN_TFT_DC, HIGH);
  if (paramSize > 0) displaySpi.writeBytes(param, paramSize);
  digitalWrite(PIN_TFT_CS, HIGH);
  displaySpi.endTransaction();
}

static void sendCommand(lv_display_t *, const uint8_t *cmd, size_t cmdSize, const uint8_t *param, size_t paramSize) {
  displayWrite(cmd, cmdSize, param, paramSize);
}

// LVGL renders RGB565 little-endian; the controller takes the high byte
// first over its 8-bit bus
static void sendColour(lv_display_t *display, const uint8_t *cmd, size_t cmdSize, uint8_t *param, size_t paramSize) {
  lv_draw_sw_rgb565_swap(param, paramSize / 2);
  displayWrite(cmd, cmdSize, param, paramSize);
  lv_display_flush_ready(display);
}

// --- touch ----------------------------------------------------------------------------

static uint16_t touchChannel(uint8_t command) {
  touchSpi.transfer(command);
  return touchSpi.transfer16(0) >> 3;     // 12 bits, left-aligned in 15
}

// a touch's raw position, the median of five readings; false without one
static bool touchRaw(int &rawAcross, int &rawDown, int &pressure) {
  touchSpi.beginTransaction(SPISettings(2000000, MSBFIRST, SPI_MODE0));
  digitalWrite(PIN_TOUCH_CS, LOW);
  const int z1 = touchChannel(0xB1);
  const int z2 = touchChannel(0xC1);
  pressure = z1 + 4095 - z2;
  int xs[5], ys[5];
  for (int i = 0; i < 5; i++) {
    xs[i] = touchChannel(0xD1);
    ys[i] = touchChannel(0x91);
  }
  touchChannel(0xD0);                     // power down, interrupt on
  digitalWrite(PIN_TOUCH_CS, HIGH);
  touchSpi.endTransaction();
  if (pressure < TOUCH_PRESSURE_MIN) return false;
  for (int i = 1; i < 5; i++) {           // insertion sort, for the medians
    for (int j = i; j > 0 && xs[j - 1] > xs[j]; j--) { int t = xs[j]; xs[j] = xs[j - 1]; xs[j - 1] = t; }
    for (int j = i; j > 0 && ys[j - 1] > ys[j]; j--) { int t = ys[j]; ys[j] = ys[j - 1]; ys[j - 1] = t; }
  }
  rawDown = xs[2];                        // the X channel runs top to bottom
  rawAcross = ys[2];                      // the Y channel left to right
  return true;
}

static void readTouch(lv_indev_t *, lv_indev_data_t *data) {
  static bool wasPressed = false;
  int rawAcross, rawDown, pressure;
  if (!touchRaw(rawAcross, rawDown, pressure)) {
    data->state = LV_INDEV_STATE_RELEASED;
    wasPressed = false;
    return;
  }
  const int x = constrain(map(rawAcross, TOUCH_RAW_LEFT, TOUCH_RAW_RIGHT, 0, DISPLAY_WIDTH - 1), 0, DISPLAY_WIDTH - 1);
  const int y = constrain(map(rawDown, TOUCH_RAW_TOP, TOUCH_RAW_BOTTOM, 0, DISPLAY_HEIGHT - 1), 0, DISPLAY_HEIGHT - 1);
  if (!wasPressed) Serial.printf("touch raw %d %d pressure %d -> %d %d\n", rawAcross, rawDown, pressure, x, y);
  wasPressed = true;
  // LVGL turns a pointer's point with the display, by its native portrait
  // size (270: (a, b) becomes (b, 239 - a)), so it gets the point the
  // turn takes to (x, y)
  data->point.x = DISPLAY_HEIGHT - 1 - y;
  data->point.y = x;
  data->state = LV_INDEV_STATE_PRESSED;
}

// --- LED --------------------------------------------------------------------------------

void ledSet(LedColour colour) {
  const bool red = colour == LED_RED || colour == LED_AMBER;
  const bool green = colour == LED_GREEN || colour == LED_AMBER;
  const bool blue = colour == LED_BLUE;
  digitalWrite(PIN_LED_RED, red ? LOW : HIGH);
  digitalWrite(PIN_LED_GREEN, green ? LOW : HIGH);
  digitalWrite(PIN_LED_BLUE, blue ? LOW : HIGH);
}

// --- public ------------------------------------------------------------------------------

static uint32_t tick() {
  return millis();
}

static void logLine(lv_log_level_t, const char *text) {
  Serial.print(text);
}

void displayBegin() {
  pinMode(PIN_LED_RED, OUTPUT);
  pinMode(PIN_LED_GREEN, OUTPUT);
  pinMode(PIN_LED_BLUE, OUTPUT);
  ledSet(LED_OFF);

  pinMode(PIN_TFT_CS, OUTPUT);
  pinMode(PIN_TFT_DC, OUTPUT);
  digitalWrite(PIN_TFT_CS, HIGH);
  pinMode(PIN_TFT_BACKLIGHT, OUTPUT);
  digitalWrite(PIN_TFT_BACKLIGHT, LOW);
  displaySpi.begin(PIN_TFT_SCK, PIN_TFT_MISO, PIN_TFT_MOSI, PIN_TFT_CS);

  pinMode(PIN_TOUCH_CS, OUTPUT);
  digitalWrite(PIN_TOUCH_CS, HIGH);
  pinMode(PIN_TOUCH_IRQ, INPUT);
  touchSpi.begin(PIN_TOUCH_CLK, PIN_TOUCH_MISO, PIN_TOUCH_MOSI, PIN_TOUCH_CS);

  lv_init();
  lv_tick_set_cb(tick);
  lv_log_register_print_cb(logLine);
  // the controller's native orientation is portrait; LVGL turns it
#if DISPLAY_ST7789
  lv_display_t *display = lv_st7789_create(DISPLAY_HEIGHT, DISPLAY_WIDTH, LV_LCD_FLAG_NONE, sendCommand, sendColour);
  lv_lcd_generic_mipi_set_invert(display, true);
#else
  lv_display_t *display = lv_ili9341_create(DISPLAY_HEIGHT, DISPLAY_WIDTH, LV_LCD_FLAG_NONE, sendCommand, sendColour);   // RGB order on the CYD
#endif
  // the buffers first: turning the display redraws it
  lv_display_set_buffers(display, drawBuffer1, drawBuffer2, sizeof(drawBuffer1), LV_DISPLAY_RENDER_MODE_PARTIAL);
  lv_display_set_rotation(display, LV_DISPLAY_ROTATION_270);   // 90 shows it upside down on the CYD

  lv_indev_t *touch = lv_indev_create();
  lv_indev_set_type(touch, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(touch, readTouch);

  digitalWrite(PIN_TFT_BACKLIGHT, HIGH);
}

void displayPoll() {
  lv_timer_handler();
}
