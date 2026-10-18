#include "Display.h"
#include "Config.h"
#include <Arduino.h>
#include <SPI.h>
#include <Preferences.h>
#include <lvgl.h>
// LVGL 9.2 leaves its MIPI panel drivers out of lvgl.h
#include <drivers/display/ili9341/lv_ili9341.h>
#include <drivers/display/st7789/lv_st7789.h>

#define DRAW_BUFFER_LINES             20
#define BACKLIGHT_PWM_HZ              5000
#define PANEL_STORE                   "adl-panel"
#define PANEL_KEY_BRIGHTNESS          "light"

static int brightness = 100;

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

// the window the next pixels go to, as LVGL's driver sets it (column and
// page address set, in the turned coordinates the controller works in)
static uint16_t windowX1, windowX2, windowY1, windowY2;
static bool capturing = false;

static void sendCommand(lv_display_t *, const uint8_t *cmd, size_t cmdSize, const uint8_t *param, size_t paramSize) {
  if (cmdSize == 1 && paramSize == 4 && (cmd[0] == 0x2A || cmd[0] == 0x2B)) {
    const uint16_t from = (param[0] << 8) | param[1];
    const uint16_t to = (param[2] << 8) | param[3];
    if (cmd[0] == 0x2A) { windowX1 = from; windowX2 = to; }
    else { windowY1 = from; windowY2 = to; }
  }
  displayWrite(cmd, cmdSize, param, paramSize);
}

// LVGL renders RGB565 little-endian; the controller takes the high byte
// first over its 8-bit bus. While capturing, the same bytes go to the USB
// port too, after a line naming their window.
static void sendColour(lv_display_t *display, const uint8_t *cmd, size_t cmdSize, uint8_t *param, size_t paramSize) {
  lv_draw_sw_rgb565_swap(param, paramSize / 2);
  displayWrite(cmd, cmdSize, param, paramSize);
  if (capturing) {
    Serial.printf("~area %u %u %u %u\n", windowX1, windowY1, windowX2, windowY2);
    Serial.write(param, paramSize);
  }
  lv_display_flush_ready(display);
}

// the whole screen drawn again, its pixels sent over USB as well
// (tools/Capture-PanelScreen.ps1 makes a PNG of them)
void displayCapture() {
  Serial.flush();
  Serial.printf("~shot %d %d\n", DISPLAY_WIDTH, DISPLAY_HEIGHT);
  capturing = true;
  lv_obj_invalidate(lv_screen_active());
  lv_obj_invalidate(lv_layer_top());
  lv_refr_now(nullptr);
  capturing = false;
  Serial.print("~end\n");
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

// --- backlight ---------------------------------------------------------------------------

static void applyBrightness() {
  ledcWrite(PIN_TFT_BACKLIGHT, (uint32_t)(brightness * 255 / 100));
}

void displaySetBrightness(int percent) {
  brightness = constrain(percent, 10, 100);
  applyBrightness();
  Preferences store;
  if (store.begin(PANEL_STORE, false)) {
    store.putUChar(PANEL_KEY_BRIGHTNESS, (uint8_t)brightness);
    store.end();
  }
}

int displayBrightness() {
  return brightness;
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
  ledcAttach(PIN_TFT_BACKLIGHT, BACKLIGHT_PWM_HZ, 8);
  ledcWrite(PIN_TFT_BACKLIGHT, 0);
  Preferences store;
  if (store.begin(PANEL_STORE, true)) {
    brightness = constrain(store.getUChar(PANEL_KEY_BRIGHTNESS, 100), 10, 100);
    store.end();
  }
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

  applyBrightness();
}

void displayPoll() {
  lv_timer_handler();
}
