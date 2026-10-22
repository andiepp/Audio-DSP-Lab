#include "Ui.h"
#include "Config.h"
#include "Display.h"
#include "Link.h"
#include "Curves.h"
#include "Wireless.h"
#include <lvgl.h>
#include <math.h>
#include <algorithm>

// Studio dark, as the PC application's theme
#define COLOUR_GROUND                 0x11151B
#define COLOUR_BAR                    0x161B22
#define COLOUR_CARD                   0x1A2028
#define COLOUR_LINE                   0x2B3440
#define COLOUR_TEXT                   0xE7EBF1
#define COLOUR_MUTED                  0x8B97A8
#define COLOUR_ACCENT                 0x2FC4B2
#define COLOUR_DANGER                 0xFF6B6B
#define TOP_BAR_HEIGHT                30
#define POT_STRIP_HEIGHT              28
#define TOAST_MS                      3000

static const uint32_t potColours[4] = { 0x1E7FD6, 0x1FA35A, 0xF2CC3E, 0xD9453B };

struct Application {
  const char *name;
  const char *title;
};
static const Application applications[] = {
  { "generator", "Generator" }, { "equalizer", "Equalizer" }, { "radio", "Radio" },
  { "analyzer", "Analysis" }, { "synthesizer", "Synth" }, { "sigmastudio", "SigmaStudio" },
};
static const int applicationCount = sizeof(applications) / sizeof(applications[0]);

enum Page : uint8_t { PAGE_HOME, PAGE_APPLICATION, PAGE_SETTINGS };
static Page page = PAGE_HOME;

static lv_obj_t *homeButton;
static lv_obj_t *titleLabel;
static lv_obj_t *linkDot;
static lv_obj_t *outputBox;
static lv_obj_t *outputSwitch;
static lv_obj_t *settingsButton;
// the settings page
static lv_obj_t *settingsPotSwitch;
static lv_obj_t *startupSwitch;
static lv_obj_t *brightnessSlider;
static lv_obj_t *settingsInfo;
static lv_obj_t *content;
static lv_obj_t *potLabels[4];
static lv_obj_t *cover;               // "connecting" while there is no link
static lv_obj_t *coverText;
static lv_obj_t *toast;
static uint32_t toastAt = 0;

// what the content shows now, to rebuild it only when that changes
static Page builtPage = PAGE_HOME;
static uint32_t builtParams = 0xFFFFFFFF;
static String builtApplication;
static uint32_t seenVersion = 0xFFFFFFFF;

// home
static lv_obj_t *appButtons[applicationCount];
static lv_obj_t *potControlSwitch;
static lv_obj_t *infoLabel;
// application page: a row's value label per parameter
static std::vector<lv_obj_t *> valueLabels;
static lv_obj_t *startingLabel;

// the synthesizer's sections: keys that filter its long list
enum Section : uint8_t { SECTION_OSC, SECTION_FILTER, SECTION_ENV, SECTION_LFO, SECTION_PERF, SECTION_ALL };
static const char *const sectionTitles[] = { "Osc", "Filter", "Env", "LFO", "Perf", "All" };
static const int sectionCount = 6;
static Section section = SECTION_OSC;
static Section builtSection = SECTION_ALL;
static lv_obj_t *sectionKeys[sectionCount];
// the radio's header
static uint32_t builtStations = 0xFFFFFFFF;
static lv_obj_t *radioStation;
static lv_obj_t *radioTitle;
static lv_obj_t *radioState;
static std::vector<lv_obj_t *> stationKeys;
// the analysis' header: the plot and its keys (Frequency Response, THD)
#define PLOT_WIDTH                    306
#define PLOT_HEIGHT                   86      // two tile rows below it end at the knob strip
#define PLOT_RANGE_DB                 60
static lv_obj_t *sweepButton;
static lv_obj_t *sweepLabel;
// the THD and Spectrum views in the same plot: its key and the spectrum,
// below the keys; their values take the tiles' place while they measure
#define SPECTRUM_RANGE_DB             140     // 0 dBFS at the top
#define SPECTRUM_TOP                  32      // the spectrum's top edge, below the keys
#define VALUES_HEIGHT                 84      // two tile rows and their gap
enum AnalysisView : uint8_t { VIEW_RESPONSE, VIEW_THD, VIEW_SPECTRUM, VIEW_COUNT };
static AnalysisView view = VIEW_RESPONSE;
static const char *const viewNames[VIEW_COUNT] = { "Resp", "THD", "Spec" };
static lv_obj_t *modeButton;
static lv_obj_t *specLeft;
static lv_obj_t *specRight;
static lv_obj_t *valuesBox;
static lv_obj_t *valueCells[4][3];      // rows: a heading and three values; columns: name, L, R
static std::vector<lv_obj_t *> tiles;
static std::vector<lv_point_precise_t> specLeftPoints;
static std::vector<lv_point_precise_t> specRightPoints;
static uint32_t drawnSpectrum = 0xFFFFFFFF;
static String seenThdState;
static String seenSpectrumState;
static lv_obj_t *plotLeft;
static lv_obj_t *plotRight;
static lv_obj_t *plotTopLabel;
static lv_obj_t *plotBottomLabel;
static std::vector<lv_point_precise_t> leftPoints;
static std::vector<lv_point_precise_t> rightPoints;
static uint32_t drawnPoints = 0xFFFFFFFF;
// the measurement cursor: a line on the point nearest the touch, a dot on
// each channel, and the point's frequency and levels
static lv_obj_t *cursorLine;
static lv_obj_t *cursorDots[2];
static lv_obj_t *cursorLabel;
static int cursorIndex = -1;            // the point, -1 none
static float plotTop = 0;               // the level at the plot's top edge

// the editor
static lv_obj_t *editor;
static int editing = -1;              // the parameter's index
static lv_obj_t *editorValue;
static lv_obj_t *editorSlider;
static lv_obj_t *editorStepLabel;
static std::vector<lv_obj_t *> choiceButtons;
static bool coarse = false;
static int repeats = 0;

// --- text ------------------------------------------------------------------------------

// the fonts carry ASCII only: text from the board (stream titles, station
// names, UTF-8) spelled with it, "Zwischentoene" for "Zwischentöne"
static String readable(const String &text) {
  static const char *const latin[64] = {
    "A", "A", "A", "A", "Ae", "A", "AE", "C", "E", "E", "E", "E", "I", "I", "I", "I",
    "D", "N", "O", "O", "O", "O", "Oe", "x", "O", "U", "U", "U", "Ue", "Y", "Th", "ss",
    "a", "a", "a", "a", "ae", "a", "ae", "c", "e", "e", "e", "e", "i", "i", "i", "i",
    "d", "n", "o", "o", "o", "o", "oe", ":", "o", "u", "u", "u", "ue", "y", "th", "y",
  };
  String result;
  for (unsigned i = 0; i < text.length(); i++) {
    const uint8_t c = (uint8_t)text[i];
    if (c < 0x80) {
      result += (char)c;
    } else if (c == 0xC3 && i + 1 < text.length() && ((uint8_t)text[i + 1] & 0xC0) == 0x80) {
      result += latin[((uint8_t)text[++i]) & 0x3F];         // U+00C0 .. U+00FF
    } else if ((c & 0xC0) == 0xC0) {
      result += '?';                                        // another character: its lead byte
    }                                                       // its continuation bytes: dropped
  }
  return result;
}

static String pretty(const String &name) {
  String text = name;
  text.replace("-", " ");
  if (text.length() > 0) text.setCharAt(0, toupper(text[0]));
  return text;
}

// a choice as words: "inverse-saw" reads "inverse saw", "200-2k" stays
static String choiceText(const String &choice) {
  String text = choice;
  for (unsigned i = 1; i < text.length(); i++) {
    if (text[i] == '-' && isalpha((unsigned char)text[i - 1])) text.setCharAt(i, ' ');
  }
  return text;
}

static const char *applicationTitle(const String &name) {
  for (const Application &app : applications) {
    if (name == app.name) return app.title;
  }
  return name.c_str();
}

static int decimalsOf(const Param &param) {
  return param.kind == KIND_NUMBER ? 2 : 0;
}

static String formatNumber(double value, int decimals) {
  char text[32];
  snprintf(text, sizeof(text), "%.*f", decimals, value);
  String result(text);
  if (decimals > 0) {
    while (result.endsWith("0")) result.remove(result.length() - 1);
    if (result.endsWith(".")) result.remove(result.length() - 1);
  }
  return result;
}

static String shown(const Param &param) {
  String text = param.kind == KIND_CHOICE ? choiceText(param.value) : param.value;
  if (param.unit.length() > 0) text += " " + param.unit;
  return text;
}

// --- styling ------------------------------------------------------------------------------

static void plainBox(lv_obj_t *obj, uint32_t colour) {
  lv_obj_set_style_bg_color(obj, lv_color_hex(colour), 0);
  lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(obj, 0, 0);
  lv_obj_set_style_radius(obj, 0, 0);
  lv_obj_set_style_pad_all(obj, 0, 0);
  lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
}

static lv_obj_t *label(lv_obj_t *parent, const char *text, const lv_font_t *font, uint32_t colour) {
  lv_obj_t *obj = lv_label_create(parent);
  lv_label_set_text(obj, text);
  lv_obj_set_style_text_font(obj, font, 0);
  lv_obj_set_style_text_color(obj, lv_color_hex(colour), 0);
  return obj;
}

static lv_obj_t *button(lv_obj_t *parent, const char *text, int width, int height) {
  lv_obj_t *obj = lv_button_create(parent);
  lv_obj_set_size(obj, width, height);
  lv_obj_set_style_bg_color(obj, lv_color_hex(COLOUR_CARD), 0);
  lv_obj_set_style_border_color(obj, lv_color_hex(COLOUR_LINE), 0);
  lv_obj_set_style_border_width(obj, 1, 0);
  lv_obj_set_style_shadow_width(obj, 0, 0);
  lv_obj_set_style_radius(obj, 6, 0);
  lv_obj_t *text_ = label(obj, text, &lv_font_montserrat_14, COLOUR_TEXT);
  lv_obj_center(text_);
  return obj;
}

static void markSelected(lv_obj_t *obj, bool selected) {
  lv_obj_set_style_bg_color(obj, lv_color_hex(selected ? COLOUR_ACCENT : COLOUR_CARD), 0);
  lv_obj_t *text = lv_obj_get_child(obj, 0);
  if (text != nullptr) lv_obj_set_style_text_color(text, lv_color_hex(selected ? COLOUR_GROUND : COLOUR_TEXT), 0);
}

// --- the editor ----------------------------------------------------------------------------

static bool logarithmic(const Param &param) {
  return param.minimum > 0 && param.maximum / param.minimum >= 100;
}

// 1, 2 or 5 times a power of ten, at most x
static double niceStep(double x) {
  if (x <= 0) return 1;
  const double power = pow(10, floor(log10(x)));
  const double m = x / power;
  return (m >= 5 ? 5 : m >= 2 ? 2 : 1) * power;
}

static double stepOf(const Param &param, double value) {
  double step = logarithmic(param) ? niceStep(fabs(value) / 100) : niceStep((param.maximum - param.minimum) / 200);
  if (param.kind == KIND_INTEGER) step = fmax(1, round(step));
  else step = fmax(step, 0.01);
  return coarse || repeats > 8 ? step * 10 : step;
}

static int32_t toSlider(const Param &param, double value) {
  const double share = logarithmic(param)
    ? log(value / param.minimum) / log(param.maximum / param.minimum)
    : (value - param.minimum) / (param.maximum - param.minimum);
  return (int32_t)lround(constrain(share, 0.0, 1.0) * 1000);
}

static double fromSlider(const Param &param, int32_t position) {
  const double share = position / 1000.0;
  const double value = logarithmic(param)
    ? param.minimum * pow(param.maximum / param.minimum, share)
    : param.minimum + share * (param.maximum - param.minimum);
  return param.kind == KIND_INTEGER ? round(value) : value;
}

static void editorShow() {
  if (editor == nullptr || editing < 0 || editing >= (int)board.params.size()) return;
  const Param &param = board.params[editing];
  if (editorValue != nullptr) lv_label_set_text(editorValue, shown(param).c_str());
  if (editorSlider != nullptr && !lv_obj_has_state(editorSlider, LV_STATE_PRESSED)) {
    lv_slider_set_value(editorSlider, toSlider(param, param.value.toDouble()), LV_ANIM_OFF);
  }
  if (editorStepLabel != nullptr) {
    const double step = stepOf(param, param.value.toDouble());
    lv_label_set_text(editorStepLabel, (String("step ") + formatNumber(step, decimalsOf(param))).c_str());
  }
  for (size_t i = 0; i < choiceButtons.size(); i++) markSelected(choiceButtons[i], param.value == param.choices[i]);
}

static void editorClose() {
  if (editor != nullptr) lv_obj_delete(editor);
  editor = nullptr;
  editing = -1;
  editorValue = editorSlider = editorStepLabel = nullptr;
  choiceButtons.clear();
}

static void writeNumber(double value) {
  const Param &param = board.params[editing];
  value = constrain(value, param.minimum, param.maximum);
  linkSetParam(editing, formatNumber(value, decimalsOf(param)));
  editorShow();
}

static void onStep(lv_event_t *e) {
  if (editing < 0) return;
  const lv_event_code_t code = lv_event_get_code(e);
  if (code == LV_EVENT_LONG_PRESSED_REPEAT) repeats++;
  else if (code == LV_EVENT_PRESSED) repeats = 0;
  if (code == LV_EVENT_PRESSED || code == LV_EVENT_RELEASED) {
    editorShow();
    return;
  }
  const int direction = (int)(intptr_t)lv_event_get_user_data(e);
  const Param &param = board.params[editing];
  const double value = param.value.toDouble();
  const double step = stepOf(param, value);
  // onto the step's grid, then one step on
  double next = direction > 0 ? floor(value / step + 1e-9) * step + step : ceil(value / step - 1e-9) * step - step;
  writeNumber(next);
}

static void onSlider(lv_event_t *e) {
  if (editing < 0) return;
  writeNumber(fromSlider(board.params[editing], lv_slider_get_value((lv_obj_t *)lv_event_get_target(e))));
}

static void onCoarse(lv_event_t *e) {
  coarse = lv_obj_has_state((lv_obj_t *)lv_event_get_target(e), LV_STATE_CHECKED);
  editorShow();
}

static void onChoice(lv_event_t *e) {
  if (editing < 0) return;
  const int index = (int)(intptr_t)lv_event_get_user_data(e);
  linkSetParam(editing, board.params[editing].choices[index]);
  editorShow();
}

static void onEditorDone(lv_event_t *) {
  editorClose();
}

static void editorOpen(int index) {
  editorClose();
  editing = index;
  coarse = false;
  const Param &param = board.params[index];

  editor = lv_obj_create(lv_layer_top());
  plainBox(editor, 0x000000);
  lv_obj_set_size(editor, DISPLAY_WIDTH, DISPLAY_HEIGHT);
  lv_obj_set_style_bg_opa(editor, LV_OPA_60, 0);
  lv_obj_add_flag(editor, LV_OBJ_FLAG_CLICKABLE);

  lv_obj_t *panel = lv_obj_create(editor);
  lv_obj_set_size(panel, 304, 224);
  lv_obj_center(panel);
  lv_obj_set_style_bg_color(panel, lv_color_hex(COLOUR_CARD), 0);
  lv_obj_set_style_border_color(panel, lv_color_hex(COLOUR_LINE), 0);
  lv_obj_set_style_radius(panel, 10, 0);
  lv_obj_set_style_pad_all(panel, 8, 0);
  lv_obj_remove_flag(panel, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *title = label(panel, pretty(param.name).c_str(), &lv_font_montserrat_16, COLOUR_MUTED);
  lv_obj_align(title, LV_ALIGN_TOP_LEFT, 0, 0);
  lv_obj_t *done = button(panel, "Done", 70, 30);
  lv_obj_align(done, LV_ALIGN_TOP_RIGHT, 0, -4);
  lv_obj_add_event_cb(done, onEditorDone, LV_EVENT_CLICKED, nullptr);

  if (param.kind == KIND_CHOICE) {
    lv_obj_t *grid = lv_obj_create(panel);
    lv_obj_set_size(grid, 288, 172);
    lv_obj_align(grid, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_opa(grid, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(grid, 0, 0);
    lv_obj_set_style_pad_all(grid, 2, 0);
    lv_obj_set_style_pad_gap(grid, 6, 0);
    lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
    for (size_t i = 0; i < param.choices.size(); i++) {
      lv_obj_t *key = button(grid, choiceText(param.choices[i]).c_str(), 88, 38);
      lv_obj_add_event_cb(key, onChoice, LV_EVENT_CLICKED, (void *)(intptr_t)i);
      choiceButtons.push_back(key);
    }
  } else {
    editorValue = label(panel, "", &lv_font_montserrat_28, COLOUR_TEXT);
    lv_obj_align(editorValue, LV_ALIGN_TOP_MID, 0, 34);

    lv_obj_t *minus = button(panel, LV_SYMBOL_MINUS, 84, 48);
    lv_obj_align(minus, LV_ALIGN_LEFT_MID, 0, 18);
    lv_obj_t *plus = button(panel, LV_SYMBOL_PLUS, 84, 48);
    lv_obj_align(plus, LV_ALIGN_RIGHT_MID, 0, 18);
    for (lv_obj_t *key : { minus, plus }) {
      void *direction = (void *)(intptr_t)(key == plus ? 1 : -1);
      lv_obj_add_event_cb(key, onStep, LV_EVENT_SHORT_CLICKED, direction);
      lv_obj_add_event_cb(key, onStep, LV_EVENT_LONG_PRESSED_REPEAT, direction);
      lv_obj_add_event_cb(key, onStep, LV_EVENT_PRESSED, direction);
      lv_obj_add_event_cb(key, onStep, LV_EVENT_RELEASED, direction);
    }
    editorStepLabel = label(panel, "", &lv_font_montserrat_12, COLOUR_MUTED);
    lv_obj_align(editorStepLabel, LV_ALIGN_CENTER, 0, 8);
    lv_obj_t *coarseKey = button(panel, "x10", 56, 28);
    lv_obj_align(coarseKey, LV_ALIGN_CENTER, 0, 34);
    lv_obj_add_flag(coarseKey, LV_OBJ_FLAG_CHECKABLE);
    lv_obj_set_style_bg_color(coarseKey, lv_color_hex(COLOUR_ACCENT), LV_STATE_CHECKED);
    lv_obj_add_event_cb(coarseKey, onCoarse, LV_EVENT_VALUE_CHANGED, nullptr);

    editorSlider = lv_slider_create(panel);
    lv_obj_set_width(editorSlider, 260);
    lv_obj_align(editorSlider, LV_ALIGN_BOTTOM_MID, 0, -10);
    lv_slider_set_range(editorSlider, 0, 1000);
    lv_obj_add_event_cb(editorSlider, onSlider, LV_EVENT_VALUE_CHANGED, nullptr);
  }
  editorShow();
}

// --- pages -------------------------------------------------------------------------------

static void onHome(lv_event_t *) {
  editorClose();
  page = PAGE_HOME;
}

static void onApplication(lv_event_t *e) {
  const int index = (int)(intptr_t)lv_event_get_user_data(e);
  if (board.application != applications[index].name) linkSelectApplication(applications[index].name);
  page = PAGE_APPLICATION;
}

static void onPotControl(lv_event_t *e) {
  linkSetPotControl(lv_obj_has_state((lv_obj_t *)lv_event_get_target(e), LV_STATE_CHECKED));
}

static void onOutput(lv_event_t *e) {
  const int index = linkFindParam("output");
  if (index >= 0) linkSetParam(index, lv_obj_has_state((lv_obj_t *)lv_event_get_target(e), LV_STATE_CHECKED) ? "on" : "off");
}

static void onRow(lv_event_t *e) {
  const int index = (int)(intptr_t)lv_event_get_user_data(e);
  if (index >= (int)board.params.size()) return;
  const Param &param = board.params[index];
  if (param.kind == KIND_SWITCH) linkSetParam(index, param.value == "on" ? "off" : "on");
  else editorOpen(index);
}

// a knob in the strip: edits the parameter its pot plays now
static void onPot(lv_event_t *e) {
  const int pot = (int)(intptr_t)lv_event_get_user_data(e);
  if (!board.linked || board.state != "running") return;
  for (size_t i = 0; i < board.params.size(); i++) {
    if (board.params[i].pot != pot) continue;
    if (board.params[i].kind == KIND_SWITCH) linkSetParam(i, board.params[i].value == "on" ? "off" : "on");
    else editorOpen(i);
    return;
  }
}

// --- settings ------------------------------------------------------------------------------

static void setChecked(lv_obj_t *obj, bool checked);   // with the refresh, below

static void onSettings(lv_event_t *) {
  linkReadSettings();
  page = PAGE_SETTINGS;
}

static void onStartup(lv_event_t *e) {
  linkSetStartup(lv_obj_has_state((lv_obj_t *)lv_event_get_target(e), LV_STATE_CHECKED));
}

static void onBrightness(lv_event_t *e) {
  displaySetBrightness(lv_slider_get_value((lv_obj_t *)lv_event_get_target(e)));
}

// a line of the settings page: its text on the left, a control on the right
static lv_obj_t *settingLine(const char *text, const char *hint) {
  lv_obj_t *line = lv_obj_create(content);
  plainBox(line, COLOUR_CARD);
  lv_obj_set_size(line, lv_pct(100), hint != nullptr ? 44 : 34);
  lv_obj_set_style_radius(line, 6, 0);
  lv_obj_t *name = label(line, text, &lv_font_montserrat_14, COLOUR_TEXT);
  lv_obj_align(name, hint != nullptr ? LV_ALIGN_TOP_LEFT : LV_ALIGN_LEFT_MID, 8, hint != nullptr ? 5 : 0);
  if (hint != nullptr) {
    lv_obj_t *small = label(line, hint, &lv_font_montserrat_12, COLOUR_MUTED);
    lv_obj_align(small, LV_ALIGN_BOTTOM_LEFT, 8, -5);
  }
  return line;
}

static lv_obj_t *settingSwitch(lv_obj_t *line, lv_event_cb_t handler) {
  lv_obj_t *control = lv_switch_create(line);
  lv_obj_set_size(control, 50, 26);
  lv_obj_align(control, LV_ALIGN_RIGHT_MID, -8, 0);
  lv_obj_add_event_cb(control, handler, LV_EVENT_VALUE_CHANGED, nullptr);
  return control;
}

static void buildSettings() {
  lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_gap(content, 4, 0);
  settingsPotSwitch = settingSwitch(settingLine("Pot control", "The board's pots set values"), onPotControl);
  startupSwitch = settingSwitch(settingLine("At power-up: last application", "Off: the SigmaStudio mode"), onStartup);
  lv_obj_t *line = settingLine("Brightness", nullptr);
  brightnessSlider = lv_slider_create(line);
  lv_obj_set_width(brightnessSlider, 150);
  lv_obj_align(brightnessSlider, LV_ALIGN_RIGHT_MID, -14, 0);
  lv_slider_set_range(brightnessSlider, 10, 100);
  lv_slider_set_value(brightnessSlider, displayBrightness(), LV_ANIM_OFF);
  lv_obj_add_event_cb(brightnessSlider, onBrightness, LV_EVENT_VALUE_CHANGED, nullptr);
  settingsInfo = label(content, "", &lv_font_montserrat_12, COLOUR_MUTED);
}

static void refreshSettings() {
  if (settingsInfo == nullptr) return;
  setChecked(settingsPotSwitch, board.potControl);
  setChecked(startupSwitch, board.startupRestores);
  // two lines: the page has room for no more
  String info = String("Panel " PANEL_VERSION ", board firmware ") + board.firmware +
                (board.kit == "esp32-pico-kit-1" ? " on ESP32-PICO-KIT-1" : board.kit.length() > 0 ? " on ESP32-PICO-KIT V4" : "") +
                "\nLink: " + (board.linked ? "connected" : "not connected") + ", WiFi: " + networkState() +
                (strcmp(networkState(), "online") == 0
                   ? String(" ") + networkAddress() + (networkClientConnected() ? ", PC" : "") : String());
  lv_label_set_text(settingsInfo, info.c_str());
}

static void buildHome() {
  lv_obj_set_flex_flow(content, LV_FLEX_FLOW_ROW_WRAP);
  lv_obj_set_style_pad_gap(content, 6, 0);
  for (int i = 0; i < applicationCount; i++) {
    appButtons[i] = button(content, applications[i].title, 98, 56);
    lv_obj_add_event_cb(appButtons[i], onApplication, LV_EVENT_CLICKED, (void *)(intptr_t)i);
  }
  lv_obj_t *row = lv_obj_create(content);
  plainBox(row, COLOUR_GROUND);
  lv_obj_set_size(row, 306, 34);
  lv_obj_t *text = label(row, "Pot control", &lv_font_montserrat_14, COLOUR_TEXT);
  lv_obj_align(text, LV_ALIGN_LEFT_MID, 4, 0);
  potControlSwitch = lv_switch_create(row);
  lv_obj_set_size(potControlSwitch, 50, 26);
  lv_obj_align(potControlSwitch, LV_ALIGN_LEFT_MID, 104, 0);
  lv_obj_add_event_cb(potControlSwitch, onPotControl, LV_EVENT_VALUE_CHANGED, nullptr);
  infoLabel = label(row, "", &lv_font_montserrat_12, COLOUR_MUTED);
  lv_obj_align(infoLabel, LV_ALIGN_RIGHT_MID, -4, 0);
}

// --- the radio's header -------------------------------------------------------------------

static void onStation(lv_event_t *e) {
  const int index = linkFindParam("station");
  if (index >= 0) linkSetParam(index, String((int)(intptr_t)lv_event_get_user_data(e)));
}

static lv_obj_t *column(lv_obj_t *parent, int gap) {
  lv_obj_t *box = lv_obj_create(parent);
  lv_obj_set_size(box, lv_pct(100), LV_SIZE_CONTENT);
  lv_obj_set_style_bg_opa(box, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(box, 0, 0);
  lv_obj_set_style_pad_all(box, 0, 0);
  lv_obj_set_style_pad_gap(box, gap, 0);
  lv_obj_remove_flag(box, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_flex_flow(box, LV_FLEX_FLOW_COLUMN);
  return box;
}

static lv_obj_t *row(lv_obj_t *parent, int gap) {
  lv_obj_t *box = column(parent, gap);
  lv_obj_set_flex_flow(box, LV_FLEX_FLOW_ROW_WRAP);
  return box;
}

static void buildRadioHeader() {
  lv_obj_t *box = column(content, 2);
  radioStation = label(box, "", &lv_font_montserrat_20, COLOUR_TEXT);
  radioTitle = label(box, "", &lv_font_montserrat_14, COLOUR_TEXT);
  lv_label_set_long_mode(radioTitle, LV_LABEL_LONG_SCROLL_CIRCULAR);
  lv_obj_set_width(radioTitle, lv_pct(100));
  radioState = label(box, "", &lv_font_montserrat_12, COLOUR_MUTED);
  lv_obj_t *keys = row(box, 4);
  stationKeys.clear();
  for (int slot = 0; slot < 8; slot++) {
    const String name = slot < (int)board.stations.size() && board.stations[slot].length() > 0
                          ? readable(board.stations[slot]) : String(slot + 1);
    lv_obj_t *key = button(keys, name.c_str(), 73, 34);
    lv_obj_t *text = lv_obj_get_child(key, 0);
    lv_obj_set_style_text_font(text, &lv_font_montserrat_12, 0);
    lv_label_set_long_mode(text, LV_LABEL_LONG_DOT);
    lv_obj_set_width(text, 66);
    lv_obj_set_style_text_align(text, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_add_event_cb(key, onStation, LV_EVENT_CLICKED, (void *)(intptr_t)slot);
    stationKeys.push_back(key);
  }
}

static const char *radioWords(const String &state) {
  if (state == "playing") return "Playing";
  if (state == "connecting-wifi") return "Connecting to WiFi";
  if (state == "connecting-stream") return "Connecting to the station";
  if (state == "no-network") return "No WiFi network (enter one on the PC)";
  if (state == "stream-error") return "The station does not answer";
  return "Idle";
}

static void refreshRadio() {
  if (radioStation == nullptr) return;
  const int index = linkFindParam("station");
  const int slot = index >= 0 ? board.params[index].value.toInt() : -1;
  const String name = slot >= 0 && slot < (int)board.stations.size() ? board.stations[slot] : String();
  lv_label_set_text(radioStation, name.length() > 0 ? readable(name).c_str() : "Radio");
  lv_label_set_text(radioTitle, readable(board.title).c_str());
  String state = radioWords(board.radioState);
  if (board.radioState == "playing" && board.radioRate > 0) state += String(", ") + (board.radioRate / 1000.0f) + " kHz";
  lv_label_set_text(radioState, state.c_str());
  for (size_t i = 0; i < stationKeys.size(); i++) markSelected(stationKeys[i], (int)i == slot);
}

// --- the analyzer's header -----------------------------------------------------------------

static void onSweep(lv_event_t *) {
  if (view == VIEW_THD) linkDistortion(board.thdState != "running");
  else if (view == VIEW_SPECTRUM) linkSpectrum(board.spectrumState != "running");
  else linkSweep(board.sweepState != "running");
}

static void refreshAnalyzer();

static void onMode(lv_event_t *) {
  view = (AnalysisView)((view + 1) % VIEW_COUNT);
  drawnPoints = drawnSpectrum = 0xFFFFFFFF;
  refreshAnalyzer();
}

static lv_obj_t *gridLine(lv_obj_t *parent, int x, int y, int width, int height) {
  lv_obj_t *line = lv_obj_create(parent);
  plainBox(line, 0x232B35);
  lv_obj_set_size(line, width, height);
  lv_obj_set_pos(line, x, y);
  return line;
}

static int plotX(float hertz) {
  return (int)lroundf((log10f(fmaxf(hertz, 20)) - log10f(20)) / 3.0f * (PLOT_WIDTH - 1));
}

static int plotY(float level) {
  return (int)lroundf(constrain((plotTop - level) / PLOT_RANGE_DB, 0.0f, 1.0f) * (PLOT_HEIGHT - 1));
}

static String hertzText(float hertz) {
  if (isnan(hertz)) return "--";
  char text[24];
  if (hertz >= 1000) snprintf(text, sizeof(text), "%.2f kHz", hertz / 1000);
  else snprintf(text, sizeof(text), "%.0f Hz", hertz);
  return String(text);
}

// the cursor where cursorIndex says, or hidden while there is no such point
static void drawCursor() {
  if (cursorLine == nullptr) return;
  if (cursorIndex < 0 || cursorIndex >= (int)board.points.size()) {
    lv_obj_add_flag(cursorLine, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(cursorLabel, LV_OBJ_FLAG_HIDDEN);
    for (lv_obj_t *dot : cursorDots) lv_obj_add_flag(dot, LV_OBJ_FLAG_HIDDEN);
    return;
  }
  const SweepPoint &point = board.points[cursorIndex];
  const int x = plotX(point.hertz);
  lv_obj_set_pos(cursorLine, x, 0);
  lv_obj_remove_flag(cursorLine, LV_OBJ_FLAG_HIDDEN);
  const float levels[2] = { point.left, point.right };
  for (int channel = 0; channel < 2; channel++) {
    lv_obj_set_pos(cursorDots[channel], x - 3, plotY(levels[channel]) - 3);
    lv_obj_remove_flag(cursorDots[channel], LV_OBJ_FLAG_HIDDEN);
  }
  char text[64];
  snprintf(text, sizeof(text), "%s\nL %.1f  R %.1f dB", hertzText(point.hertz).c_str(), point.left, point.right);
  lv_label_set_text(cursorLabel, text);
  lv_obj_remove_flag(cursorLabel, LV_OBJ_FLAG_HIDDEN);
  // beside the line, on the side with room
  lv_obj_update_layout(cursorLabel);
  const int width = lv_obj_get_width(cursorLabel);
  lv_obj_set_pos(cursorLabel, x + 6 + width < PLOT_WIDTH ? x + 6 : x - 6 - width, 32);
}

// a touch on the plot, or a finger moving on it: the cursor to the nearest point
static void onPlot(lv_event_t *e) {
  if (view != VIEW_RESPONSE || board.points.empty()) return;
  lv_point_t touch;
  lv_indev_get_point(lv_indev_active(), &touch);
  lv_area_t area;
  lv_obj_get_coords((lv_obj_t *)lv_event_get_current_target(e), &area);
  const int x = touch.x - area.x1;
  int nearest = 0;
  for (size_t i = 1; i < board.points.size(); i++) {
    if (abs(plotX(board.points[i].hertz) - x) < abs(plotX(board.points[nearest].hertz) - x)) nearest = (int)i;
  }
  cursorIndex = nearest;
  drawCursor();
}

static void buildAnalyzerHeader() {
  lv_obj_t *plot = lv_obj_create(content);
  plainBox(plot, 0x141920);
  lv_obj_set_size(plot, PLOT_WIDTH, PLOT_HEIGHT);
  // the plot takes touches for its cursor; a finger moving on it does not scroll the page
  lv_obj_add_flag(plot, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_remove_flag(plot, LV_OBJ_FLAG_SCROLL_CHAIN);
  lv_obj_add_event_cb(plot, onPlot, LV_EVENT_PRESSED, nullptr);
  lv_obj_add_event_cb(plot, onPlot, LV_EVENT_PRESSING, nullptr);
  for (float hertz : { 100.0f, 1000.0f, 10000.0f }) gridLine(plot, plotX(hertz), 0, 1, PLOT_HEIGHT);
  for (int i = 1; i < 3; i++) gridLine(plot, 0, i * PLOT_HEIGHT / 3, PLOT_WIDTH, 1);
  const char *const marks[] = { "100", "1k", "10k" };
  const float hertz[] = { 100, 1000, 10000 };
  for (int i = 0; i < 3; i++) {
    lv_obj_t *mark = label(plot, marks[i], &lv_font_montserrat_12, COLOUR_MUTED);
    lv_obj_set_pos(mark, plotX(hertz[i]) + 3, PLOT_HEIGHT - 16);
  }
  plotTopLabel = label(plot, "", &lv_font_montserrat_12, COLOUR_MUTED);
  lv_obj_set_pos(plotTopLabel, 3, 1);
  plotBottomLabel = label(plot, "", &lv_font_montserrat_12, COLOUR_MUTED);
  lv_obj_set_pos(plotBottomLabel, 3, PLOT_HEIGHT - 30);
  plotRight = lv_line_create(plot);
  lv_obj_set_style_line_color(plotRight, lv_color_hex(0x7AA7FF), 0);
  lv_obj_set_style_line_width(plotRight, 1, 0);
  plotLeft = lv_line_create(plot);
  lv_obj_set_style_line_color(plotLeft, lv_color_hex(COLOUR_ACCENT), 0);
  lv_obj_set_style_line_width(plotLeft, 2, 0);

  cursorLine = gridLine(plot, 0, 0, 1, PLOT_HEIGHT);
  lv_obj_set_style_bg_color(cursorLine, lv_color_hex(COLOUR_TEXT), 0);
  const uint32_t dotColours[2] = { COLOUR_ACCENT, 0x7AA7FF };
  for (int channel = 0; channel < 2; channel++) {
    cursorDots[channel] = lv_obj_create(plot);
    plainBox(cursorDots[channel], dotColours[channel]);
    lv_obj_set_size(cursorDots[channel], 7, 7);
    lv_obj_set_style_radius(cursorDots[channel], LV_RADIUS_CIRCLE, 0);
  }
  cursorLabel = label(plot, "", &lv_font_montserrat_12, COLOUR_TEXT);
  lv_obj_set_style_bg_color(cursorLabel, lv_color_hex(0x0E1116), 0);
  lv_obj_set_style_bg_opa(cursorLabel, LV_OPA_80, 0);
  lv_obj_set_style_pad_hor(cursorLabel, 4, 0);
  lv_obj_set_style_pad_ver(cursorLabel, 2, 0);
  lv_obj_set_style_radius(cursorLabel, 4, 0);
  for (lv_obj_t *part : { cursorLine, cursorDots[0], cursorDots[1], cursorLabel }) {
    lv_obj_remove_flag(part, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(part, LV_OBJ_FLAG_HIDDEN);
  }

  // the THD and Spectrum views: the spectrum, 0 to -140 dBFS below the keys
  specRight = lv_line_create(plot);
  lv_obj_set_style_line_color(specRight, lv_color_hex(0x7AA7FF), 0);
  lv_obj_set_style_line_width(specRight, 1, 0);
  specLeft = lv_line_create(plot);
  lv_obj_set_style_line_color(specLeft, lv_color_hex(COLOUR_ACCENT), 0);
  lv_obj_set_style_line_width(specLeft, 1, 0);
  for (lv_obj_t *part : { specLeft, specRight }) lv_obj_remove_flag(part, LV_OBJ_FLAG_CLICKABLE);

  // the keys and the progress in the plot's top corner: the tiles get the room
  sweepButton = button(plot, "Start sweep", 96, 28);
  lv_obj_set_style_text_font(lv_obj_get_child(sweepButton, 0), &lv_font_montserrat_12, 0);
  lv_obj_align(sweepButton, LV_ALIGN_TOP_RIGHT, -2, 2);
  lv_obj_add_event_cb(sweepButton, onSweep, LV_EVENT_CLICKED, nullptr);
  modeButton = button(plot, "Resp", 50, 28);
  lv_obj_set_style_text_font(lv_obj_get_child(modeButton, 0), &lv_font_montserrat_12, 0);
  lv_obj_align(modeButton, LV_ALIGN_TOP_RIGHT, -102, 2);
  lv_obj_add_event_cb(modeButton, onMode, LV_EVENT_CLICKED, nullptr);
  sweepLabel = label(plot, "", &lv_font_montserrat_12, COLOUR_TEXT);
  lv_obj_align(sweepLabel, LV_ALIGN_TOP_RIGHT, -158, 8);
  drawnPoints = drawnSpectrum = 0xFFFFFFFF;

  // the THD's and the Spectrum's values: a table in the tiles' place, shown while they measure
  valuesBox = lv_obj_create(content);
  plainBox(valuesBox, COLOUR_CARD);
  lv_obj_set_size(valuesBox, PLOT_WIDTH, VALUES_HEIGHT);
  lv_obj_set_style_radius(valuesBox, 6, 0);
  lv_obj_add_flag(valuesBox, LV_OBJ_FLAG_HIDDEN);
  const uint32_t headingColours[3] = { COLOUR_MUTED, COLOUR_ACCENT, 0x7AA7FF };
  for (int row = 0; row < 4; row++) {
    for (int column = 0; column < 3; column++) {
      const bool value = row > 0 && column > 0;
      lv_obj_t *cell = label(valuesBox, "", value ? &lv_font_montserrat_16 : &lv_font_montserrat_14,
                             value ? COLOUR_TEXT : row == 0 ? headingColours[column] : COLOUR_MUTED);
      static const int columnX[3] = { 8, 90, 198 };          // room for "-100.0 dBFS"
      lv_obj_set_pos(cell, columnX[column], row == 0 ? 3 : row * 20);
      valueCells[row][column] = cell;
    }
  }
}

static void setValues(const String cells[4][3]) {
  for (int row = 0; row < 4; row++) {
    for (int column = 0; column < 3; column++) lv_label_set_text(valueCells[row][column], cells[row][column].c_str());
  }
}

static String levelText(float decibel) {
  if (isnan(decibel)) return "--";
  char text[24];
  snprintf(text, sizeof(text), "%.1f dBFS", decibel);
  return String(text);
}

// a ratio in dB as a percentage with three significant digits: 0.0123, 0.123, 1.23
static String percentText(float decibel) {
  if (isnan(decibel)) return "--";
  const float percent = 100.0f * powf(10.0f, decibel / 20.0f);
  const int decimals = constrain(2 - (int)floorf(log10f(fmaxf(percent, 1e-6f))), 0, 5);
  char text[16];
  snprintf(text, sizeof(text), "%.*f", decimals, percent);
  return String(text);
}

static void drawSpectrum();

static void refreshDistortion() {
  const bool running = board.thdState == "running";
  lv_label_set_text(lv_obj_get_child(sweepButton, 0), running ? "Stop" : "Start");
  markSelected(sweepButton, running);
  String status;
  if (board.thdState == "failed") status = "failed";
  else if (board.thdResults > 0) status = String("#") + board.thdResults + (running ? "" : " stopped");
  else if (running) status = "measuring";
  lv_label_set_text(sweepLabel, status.c_str());
  const bool shown = board.thdResults > 0;
  String cells[4][3];
  cells[0][0] = shown ? hertzText(board.thdHertz) : String("Tone");      // the tone's frequency
  cells[1][0] = "THD";
  cells[2][0] = "THD+N";
  cells[3][0] = "Level";
  for (int channel = 0; channel < 2; channel++) {
    cells[0][channel + 1] = channel == 0 ? "L" : "R";
    cells[1][channel + 1] = shown ? percentText(board.thd[channel]) + " %" : String("--");
    cells[2][channel + 1] = shown ? percentText(board.thdn[channel]) + " %" : String("--");
    cells[3][channel + 1] = shown ? levelText(board.thdLevel[channel]) : String("--");
  }
  setValues(cells);
  drawSpectrum();
}

// the Spectrum's values: the RMS and the strongest peak of each channel
static void refreshSpectrumView() {
  const bool running = board.spectrumState == "running";
  lv_label_set_text(lv_obj_get_child(sweepButton, 0), running ? "Stop" : "Start");
  markSelected(sweepButton, running);
  String status;
  if (board.spectrumState == "failed") status = "failed";
  else if (board.spectrumResults > 0) status = String("#") + board.spectrumResults + (running ? "" : " stopped");
  else if (running) status = "measuring";
  lv_label_set_text(sweepLabel, status.c_str());
  const bool shown = board.spectrumResults > 0;
  String cells[4][3];
  cells[0][0] = "Input";
  cells[1][0] = "RMS";
  cells[2][0] = "Peak at";
  cells[3][0] = "Peak";
  for (int channel = 0; channel < 2; channel++) {
    cells[0][channel + 1] = channel == 0 ? "L" : "R";
    cells[1][channel + 1] = shown ? levelText(board.rms[channel]) : String("--");
    cells[2][channel + 1] = shown ? hertzText(board.peakHertz[channel]) : String("--");
    cells[3][channel + 1] = shown ? levelText(board.peakDb[channel]) : String("--");
  }
  setValues(cells);
  drawSpectrum();
}

static void drawSpectrum() {
  if (drawnSpectrum == board.spectrumVersion) return;
  drawnSpectrum = board.spectrumVersion;
  specLeftPoints.clear();
  specRightPoints.clear();
  for (const SweepPoint &point : board.spectrum) {
    const int x = plotX(fminf(point.hertz, 20000));
    const auto y = [](float level) {
      return SPECTRUM_TOP + (int32_t)lroundf(constrain(-level / SPECTRUM_RANGE_DB, 0.0f, 1.0f) * (PLOT_HEIGHT - 1 - SPECTRUM_TOP));
    };
    specLeftPoints.push_back({ x, y(point.left) });
    specRightPoints.push_back({ x, y(point.right) });
  }
  lv_line_set_points(specLeft, specLeftPoints.data(), specLeftPoints.size());
  lv_line_set_points(specRight, specRightPoints.data(), specRightPoints.size());
}

static void refreshAnalyzer() {
  if (sweepButton == nullptr) return;
  // a measurement started elsewhere (the PC) shows itself
  if (board.thdState != seenThdState) {
    if (board.thdState == "running" && view != VIEW_THD) {
      view = VIEW_THD;
      drawnSpectrum = 0xFFFFFFFF;
    }
    seenThdState = board.thdState;
  }
  if (board.spectrumState != seenSpectrumState) {
    if (board.spectrumState == "running" && view != VIEW_SPECTRUM) {
      view = VIEW_SPECTRUM;
      drawnSpectrum = 0xFFFFFFFF;
    }
    seenSpectrumState = board.spectrumState;
  }
  const bool spectral = view != VIEW_RESPONSE;
  // the key names the view on show; a tap changes it
  lv_label_set_text(lv_obj_get_child(modeButton, 0), viewNames[view]);
  for (lv_obj_t *part : { plotLeft, plotRight }) {
    if (spectral) lv_obj_add_flag(part, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_remove_flag(part, LV_OBJ_FLAG_HIDDEN);
  }
  for (lv_obj_t *part : { specLeft, specRight }) {
    if (spectral) lv_obj_remove_flag(part, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(part, LV_OBJ_FLAG_HIDDEN);
  }
  // while THD or Spectrum measures, its values take the tiles' place
  const bool measuring = (view == VIEW_THD && board.thdState == "running")
                         || (view == VIEW_SPECTRUM && board.spectrumState == "running");
  if (measuring == lv_obj_has_flag(valuesBox, LV_OBJ_FLAG_HIDDEN)) {
    lv_obj_update_flag(valuesBox, LV_OBJ_FLAG_HIDDEN, !measuring);
    for (lv_obj_t *tile : tiles) lv_obj_update_flag(tile, LV_OBJ_FLAG_HIDDEN, measuring);
    lv_obj_scroll_to_y(content, 0, LV_ANIM_OFF);           // the tiles may have been scrolled
  }
  if (spectral) {
    // the scale is 0 to -140 dBFS, below the keys
    lv_obj_add_flag(plotTopLabel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(plotBottomLabel, LV_OBJ_FLAG_HIDDEN);
    cursorIndex = -1;
    drawCursor();
    if (view == VIEW_THD) refreshDistortion();
    else refreshSpectrumView();
    return;
  }
  lv_obj_remove_flag(plotTopLabel, LV_OBJ_FLAG_HIDDEN);
  lv_obj_remove_flag(plotBottomLabel, LV_OBJ_FLAG_HIDDEN);
  const bool running = board.sweepState == "running";
  lv_label_set_text(lv_obj_get_child(sweepButton, 0), running ? "Stop" : "Start sweep");
  markSelected(sweepButton, running);
  String progress = board.sweepTotal > 0 ? String(board.sweepMeasured) + " / " + board.sweepTotal : String();
  if (board.sweepState.length() > 0 && board.sweepState != "running") progress += String("  ") + board.sweepState;
  lv_label_set_text(sweepLabel, progress.c_str());
  if (drawnPoints == board.pointsVersion) return;
  drawnPoints = board.pointsVersion;
  // the 60 dB below the loudest point, its top on a 10 dB line
  float loudest = -200;
  for (const SweepPoint &point : board.points) loudest = fmaxf(loudest, fmaxf(point.left, point.right));
  const float top = board.points.empty() ? 0 : ceilf(loudest / 10) * 10;
  const float bottom = top - PLOT_RANGE_DB;
  plotTop = top;
  lv_label_set_text(plotTopLabel, (String((int)top) + " dB").c_str());
  lv_label_set_text(plotBottomLabel, (String((int)bottom) + " dB").c_str());
  leftPoints.clear();
  rightPoints.clear();
  for (const SweepPoint &point : board.points) {
    const int x = plotX(point.hertz);
    const auto y = [&](float level) {
      return (int32_t)lroundf(constrain((top - level) / PLOT_RANGE_DB, 0.0f, 1.0f) * (PLOT_HEIGHT - 1));
    };
    leftPoints.push_back({ x, y(point.left) });
    rightPoints.push_back({ x, y(point.right) });
  }
  lv_line_set_points(plotLeft, leftPoints.data(), leftPoints.size());
  lv_line_set_points(plotRight, rightPoints.data(), rightPoints.size());
  drawCursor();
}

// --- curves: the equalizer's response, the synthesizer's filter and envelopes ----------------

// heights that end the tile rows below at the knob strip: the content is
// 182 high with 6 of padding, a tile row 40 and 4 of gap
#define CURVE_HEIGHT_EQUALIZER        86      // two rows of tiles below
#define CURVE_HEIGHT_SYNTH            92      // the section keys above, one row below
#define CURVE_POINTS                  64

struct CurvePlot {
  lv_obj_t *box = nullptr;
  int height = CURVE_HEIGHT_SYNTH;
  lv_obj_t *lines[2] = { nullptr, nullptr };
  std::vector<lv_point_precise_t> points[2];
  lv_obj_t *dots[3] = { nullptr, nullptr, nullptr };
};

enum CurveKind : uint8_t { CURVE_NONE, CURVE_EQUALIZER, CURVE_FILTER, CURVE_ENVELOPE };
static CurvePlot curve;
static CurveKind curveKind = CURVE_NONE;

static float paramNumber(const char *name, float fallback) {
  const int index = linkFindParam(name);
  return index >= 0 ? board.params[index].value.toFloat() : fallback;
}

static int curveY(float level, float top, float bottom) {
  return (int)lroundf(constrain((top - level) / (top - bottom), 0.0f, 1.0f) * (curve.height - 1));
}

// a frame with a line per series (the second dashed); grid lines at 100 Hz,
// 1 kHz and 10 kHz for frequency curves, and at the given level
static void buildCurve(CurveKind kind, int height, bool frequencyGrid, float zeroShare, const char *top, const char *bottom) {
  curveKind = kind;
  curve = CurvePlot();
  curve.height = height;
  curve.box = lv_obj_create(content);
  plainBox(curve.box, 0x141920);
  lv_obj_set_size(curve.box, PLOT_WIDTH, height);
  if (frequencyGrid) {
    for (float hertz : { 100.0f, 1000.0f, 10000.0f }) gridLine(curve.box, plotX(hertz), 0, 1, height);
  }
  if (zeroShare >= 0) {
    lv_obj_t *zero = gridLine(curve.box, 0, (int)(zeroShare * (height - 1)), PLOT_WIDTH, 1);
    lv_obj_set_style_bg_color(zero, lv_color_hex(0x4A5566), 0);
  }
  lv_obj_t *topLabel = label(curve.box, top, &lv_font_montserrat_12, COLOUR_MUTED);
  lv_obj_set_pos(topLabel, 3, 1);
  lv_obj_t *bottomLabel = label(curve.box, bottom, &lv_font_montserrat_12, COLOUR_MUTED);
  lv_obj_align(bottomLabel, LV_ALIGN_BOTTOM_LEFT, 3, -1);
  const uint32_t colours[2] = { COLOUR_ACCENT, 0x7AA7FF };
  for (int i = 1; i >= 0; i--) {
    curve.lines[i] = lv_line_create(curve.box);
    lv_obj_set_style_line_color(curve.lines[i], lv_color_hex(colours[i]), 0);
    lv_obj_set_style_line_width(curve.lines[i], i == 0 ? 2 : 1, 0);
    if (i == 1) {
      lv_obj_set_style_line_dash_width(curve.lines[i], 4, 0);
      lv_obj_set_style_line_dash_gap(curve.lines[i], 3, 0);
    }
  }
}

static void setCurve(int series, std::vector<lv_point_precise_t> &&points) {
  curve.points[series] = std::move(points);
  lv_line_set_points(curve.lines[series], curve.points[series].data(), curve.points[series].size());
}

// a frequency curve from a function, CURVE_POINTS along 20 Hz .. 20 kHz
template <typename Gain>
static std::vector<lv_point_precise_t> frequencyCurve(float top, float bottom, Gain gain) {
  std::vector<lv_point_precise_t> points;
  for (int i = 0; i < CURVE_POINTS; i++) {
    const float hertz = 20.0f * powf(1000.0f, i / (float)(CURVE_POINTS - 1));
    points.push_back({ plotX(hertz), curveY(gain(hertz), top, bottom) });
  }
  return points;
}

#define EQ_RANGE_DB                   15.0f
#define FILTER_TOP_DB                 24.0f
#define FILTER_BOTTOM_DB              -48.0f

static void buildEqualizerCurve() {
  buildCurve(CURVE_EQUALIZER, CURVE_HEIGHT_EQUALIZER, true, 0.5f, "+15", "-15");
  for (lv_obj_t *&dot : curve.dots) {
    dot = lv_obj_create(curve.box);
    plainBox(dot, COLOUR_TEXT);
    lv_obj_set_size(dot, 6, 6);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
  }
}

static void buildFilterCurve() {
  buildCurve(CURVE_FILTER, CURVE_HEIGHT_SYNTH, true, FILTER_TOP_DB / (FILTER_TOP_DB - FILTER_BOTTOM_DB), "+24", "-48");
}

static void buildEnvelopeCurve() {
  buildCurve(CURVE_ENVELOPE, CURVE_HEIGHT_SYNTH, false, -1, "", "");
}

static void refreshCurve() {
  if (curve.box == nullptr) return;
  if (curveKind == CURVE_EQUALIZER) {
    const float gains[3] = { paramNumber("bass-gain", 0), paramNumber("middle-gain", 0), paramNumber("treble-gain", 0) };
    const float centres[3] = { paramNumber("bass-frequency", 200), paramNumber("middle-frequency", 1000),
                               paramNumber("treble-frequency", 2000) };
    setCurve(0, frequencyCurve(EQ_RANGE_DB, -EQ_RANGE_DB, [&](float hertz) { return equalizerGainDb(hertz, gains, centres); }));
    for (int band = 0; band < 3; band++) {
      const float level = equalizerGainDb(centres[band], gains, centres);
      lv_obj_set_pos(curve.dots[band], plotX(centres[band]) - 3, curveY(level, EQ_RANGE_DB, -EQ_RANGE_DB) - 3);
    }
  } else if (curveKind == CURVE_FILTER) {
    const float cutoff = paramNumber("cutoff", 20000);
    const float emphasis = paramNumber("emphasis", 0);
    const float amount = paramNumber("contour-amount", 0);
    setCurve(0, frequencyCurve(FILTER_TOP_DB, FILTER_BOTTOM_DB, [&](float hertz) { return ladderGainDb(hertz, cutoff, emphasis); }));
    const float peak = contourPeakHz(cutoff, amount);
    setCurve(1, amount > 0 ? frequencyCurve(FILTER_TOP_DB, FILTER_BOTTOM_DB,
                                            [&](float hertz) { return ladderGainDb(hertz, peak, emphasis); })
                           : std::vector<lv_point_precise_t>());
  } else if (curveKind == CURVE_ENVELOPE) {
    // from the key down to the sustain, a held stretch, then the releases,
    // the times at one scale
    const float attack = paramNumber("attack", 5), decay = paramNumber("decay", 200);
    const float sustain = paramNumber("sustain", 100) / 100, release = paramNumber("release", 100);
    const float contourAttack = paramNumber("contour-attack", 5), contourDecay = paramNumber("contour-decay", 300);
    const float contourSustain = paramNumber("contour-sustain", 50) / 100;
    const float heldSpan = fmaxf(attack + decay, contourAttack + contourDecay);
    const float releaseSpan = fmaxf(release, contourDecay);
    const float held = PLOT_WIDTH * 0.2f;
    const float scale = (PLOT_WIDTH - 1 - held) / (heldSpan + releaseSpan);
    const auto envelope = [&](float a, float d, float s, float r) {
      std::vector<lv_point_precise_t> points;
      const float keyUp = heldSpan * scale + held;
      for (int x = 0; x <= (int)(heldSpan * scale); x += 3) {
        points.push_back({ x, curveY(heldLevel(x / scale, a, d, s), 1, 0) });
      }
      const float level = heldLevel(heldSpan, a, d, s);
      points.push_back({ (int32_t)keyUp, curveY(level, 1, 0) });
      for (int x = (int)keyUp + 3; x < PLOT_WIDTH; x += 3) {
        points.push_back({ x, curveY(releasedLevel((x - keyUp) / scale, level, r), 1, 0) });
      }
      return points;
    };
    setCurve(0, envelope(attack, decay, sustain, release));
    setCurve(1, envelope(contourAttack, contourDecay, contourSustain, contourDecay));
  }
}

// --- the synthesizer's sections ------------------------------------------------------------

static Section sectionOf(const String &name) {
  if (name.startsWith("osc") || name == "noise-level") return SECTION_OSC;
  if (name == "cutoff" || name == "emphasis" || name.startsWith("contour") || name == "key-tracking") return SECTION_FILTER;
  if (name == "attack" || name == "decay" || name == "sustain" || name == "release") return SECTION_ENV;
  if (name.startsWith("lfo") || name == "pwm-depth" || name == "filter-lfo") return SECTION_LFO;
  return SECTION_PERF;
}

// what a parameter belongs with: "osc2-detune" with "osc2", the rest alone
static String groupOf(const String &name) {
  const int dash = name.indexOf('-');
  return (name.startsWith("osc") || name.startsWith("lfo")) && dash > 0 ? name.substring(0, dash) : name;
}

static void onSection(lv_event_t *e) {
  section = (Section)(intptr_t)lv_event_get_user_data(e);
}

static void buildSectionKeys() {
  lv_obj_t *keys = row(content, 4);
  for (int i = 0; i < sectionCount; i++) {
    sectionKeys[i] = button(keys, sectionTitles[i], 48, 32);
    lv_obj_set_style_text_font(lv_obj_get_child(sectionKeys[i], 0), &lv_font_montserrat_12, 0);
    lv_obj_add_event_cb(sectionKeys[i], onSection, LV_EVENT_CLICKED, (void *)(intptr_t)i);
  }
}

// --- the application's page ------------------------------------------------------------------

// a parameter as a tile, two to a row: its name above, its value below
#define TILE_WIDTH                    150
#define TILE_HEIGHT                   40

static void buildRow(size_t i) {
  const Param &param = board.params[i];
  lv_obj_t *tile = lv_obj_create(content);
  lv_obj_set_size(tile, TILE_WIDTH, TILE_HEIGHT);
  lv_obj_set_style_bg_color(tile, lv_color_hex(COLOUR_CARD), 0);
  lv_obj_set_style_border_width(tile, 0, 0);
  lv_obj_set_style_radius(tile, 6, 0);
  lv_obj_set_style_pad_hor(tile, 6, 0);
  lv_obj_set_style_pad_ver(tile, 2, 0);
  lv_obj_remove_flag(tile, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(tile, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_bg_color(tile, lv_color_hex(COLOUR_LINE), LV_STATE_PRESSED);
  lv_obj_add_event_cb(tile, onRow, LV_EVENT_CLICKED, (void *)(intptr_t)i);
  int nameX = 0;
  if (param.pot >= 0 && param.pot < 4) {
    lv_obj_t *dot = lv_obj_create(tile);
    plainBox(dot, potColours[param.pot]);
    lv_obj_set_size(dot, 8, 8);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_align(dot, LV_ALIGN_TOP_LEFT, 0, 4);
    nameX = 12;
  }
  lv_obj_t *name = label(tile, pretty(param.name).c_str(), &lv_font_montserrat_12, COLOUR_MUTED);
  lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
  lv_obj_set_width(name, TILE_WIDTH - 12 - nameX);
  lv_obj_align(name, LV_ALIGN_TOP_LEFT, nameX, 0);
  valueLabels[i] = label(tile, "", &lv_font_montserrat_16, COLOUR_TEXT);
  lv_obj_align(valueLabels[i], LV_ALIGN_BOTTOM_RIGHT, 0, 0);
  tiles.push_back(tile);
}

static void buildApplication() {
  // headers take a whole line, the tiles two to a line
  lv_obj_set_flex_flow(content, LV_FLEX_FLOW_ROW_WRAP);
  lv_obj_set_style_pad_gap(content, 4, 0);
  valueLabels.assign(board.params.size(), nullptr);
  startingLabel = nullptr;
  if (board.state != "running" || board.params.empty()) {
    const char *text = board.application == "sigmastudio" ? "Your SigmaStudio program runs.\nThe pots go to the DSP's ADC."
                                                          : "Starting ...";
    startingLabel = label(content, text, &lv_font_montserrat_16, COLOUR_MUTED);
    return;
  }
  const bool synth = board.application == "synthesizer";
  if (board.application == "radio") buildRadioHeader();
  else if (board.application == "analyzer") buildAnalyzerHeader();
  else if (board.application == "equalizer") buildEqualizerCurve();
  else if (synth) buildSectionKeys();
  if (synth && section == SECTION_FILTER) buildFilterCurve();
  if (synth && section == SECTION_ENV) buildEnvelopeCurve();

  // the rows: on the synthesizer only the section's, a group's together
  std::vector<int> rows;
  for (size_t i = 0; i < board.params.size(); i++) {
    const String &name = board.params[i].name;
    if (name == "output") continue;                     // the switch in the top bar
    // the radio: the volume beside its station keys; the bands are on the pots
    if (board.application == "radio" && name != "volume") continue;
    if (synth && section != SECTION_ALL && sectionOf(name) != section) continue;
    rows.push_back((int)i);
  }
  if (board.application == "equalizer") {
    std::stable_sort(rows.begin(), rows.end(), [](int a, int b) {
      return board.params[a].name.endsWith("-frequency") < board.params[b].name.endsWith("-frequency");
    });
  }
  if (synth) {
    const auto rank = [](int index) {
      const String group = groupOf(board.params[index].name);
      for (size_t i = 0; i < board.params.size(); i++) {
        if (groupOf(board.params[i].name) == group) return (int)i;
      }
      return index;
    };
    std::stable_sort(rows.begin(), rows.end(), [&](int a, int b) { return rank(a) < rank(b); });
  }
  for (int i : rows) buildRow(i);
}

static void rebuild() {
  editorClose();
  lv_obj_clean(content);
  potControlSwitch = infoLabel = startingLabel = nullptr;
  radioStation = radioTitle = radioState = nullptr;
  sweepButton = sweepLabel = plotLeft = plotRight = plotTopLabel = plotBottomLabel = nullptr;
  modeButton = specLeft = specRight = valuesBox = nullptr;
  tiles.clear();
  cursorLine = cursorLabel = cursorDots[0] = cursorDots[1] = nullptr;
  for (lv_obj_t *&key : sectionKeys) key = nullptr;
  curve = CurvePlot();
  curveKind = CURVE_NONE;
  stationKeys.clear();
  valueLabels.clear();
  settingsPotSwitch = startupSwitch = brightnessSlider = settingsInfo = nullptr;
  if (page == PAGE_HOME) buildHome();
  else if (page == PAGE_SETTINGS) buildSettings();
  else buildApplication();
  builtPage = page;
  builtParams = board.paramsVersion;
  builtApplication = board.application + board.state;
  builtSection = section;
  builtStations = board.stationsVersion;
}

// --- refresh ------------------------------------------------------------------------------

static void setChecked(lv_obj_t *obj, bool checked) {
  if (obj == nullptr || lv_obj_has_state(obj, LV_STATE_PRESSED)) return;
  if (checked) lv_obj_add_state(obj, LV_STATE_CHECKED);
  else lv_obj_remove_state(obj, LV_STATE_CHECKED);
}

static void refresh() {
  // the cover while there is no board
  if (board.linked) {
    lv_obj_add_flag(cover, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_remove_flag(cover, LV_OBJ_FLAG_HIDDEN);
    editorClose();
  }
  lv_obj_set_style_bg_color(linkDot, lv_color_hex(board.linked ? COLOUR_ACCENT : COLOUR_DANGER), 0);
  // the LED: red without a link, amber while a synthesizer note sounds
  // and blue while a PC works through the panel's network
  ledSet(!board.linked ? LED_RED
         : board.gate && board.application == "synthesizer" ? LED_AMBER
         : networkClientConnected() ? LED_BLUE : LED_GREEN);

  // the top bar
  if (page == PAGE_HOME) {
    lv_obj_add_flag(homeButton, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(settingsButton, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(titleLabel, "Audio-DSP-Lab");
  } else {
    lv_obj_remove_flag(homeButton, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(settingsButton, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(titleLabel, page == PAGE_SETTINGS ? "Settings" : applicationTitle(board.application));
  }
  const int output = linkFindParam("output");
  if (page == PAGE_APPLICATION && output >= 0) {
    lv_obj_remove_flag(outputBox, LV_OBJ_FLAG_HIDDEN);
    setChecked(outputSwitch, board.params[output].value == "on");
  } else {
    lv_obj_add_flag(outputBox, LV_OBJ_FLAG_HIDDEN);
  }

  // the content
  if (page == PAGE_HOME) {
    for (int i = 0; i < applicationCount; i++) markSelected(appButtons[i], board.application == applications[i].name);
    setChecked(potControlSwitch, board.potControl);
    if (infoLabel != nullptr) {
      String info = board.firmware.length() > 0 ? String("fw ") + board.firmware : String();
      if (board.kit == "esp32-pico-kit-1") info += ", KIT-1";
      else if (board.kit.length() > 0) info += ", KIT V4";
      lv_label_set_text(infoLabel, info.c_str());
    }
  } else if (page == PAGE_SETTINGS) {
    refreshSettings();
  } else {
    for (size_t i = 0; i < valueLabels.size() && i < board.params.size(); i++) {
      if (valueLabels[i] != nullptr) lv_label_set_text(valueLabels[i], shown(board.params[i]).c_str());
    }
    refreshRadio();
    refreshAnalyzer();
    refreshCurve();
    for (int i = 0; i < sectionCount; i++) {
      if (sectionKeys[i] != nullptr) markSelected(sectionKeys[i], i == section);
    }
  }
  editorShow();

  // the pots: what each plays now
  for (int pot = 0; pot < 4; pot++) {
    String role = "-";
    if (board.application == "sigmastudio") {
      role = String("ADC") + pot;
    } else {
      for (const Param &param : board.params) {
        if (param.pot == pot) role = pretty(param.name);
      }
    }
    if (role.length() > 9) role = role.substring(0, 8) + ".";
    lv_label_set_text(potLabels[pot], role.c_str());
  }

  // an error the board answered
  if (board.message.length() > 0) {
    lv_label_set_text(toast, (String("Board: ") + board.message).c_str());
    lv_obj_remove_flag(toast, LV_OBJ_FLAG_HIDDEN);
    toastAt = millis();
    board.message = "";
  }
}

// --- public --------------------------------------------------------------------------------

void uiBegin() {
  lv_display_t *display = lv_display_get_default();
  lv_theme_t *theme = lv_theme_default_init(display, lv_color_hex(COLOUR_ACCENT), lv_color_hex(0x7AA7FF), true,
                                            &lv_font_montserrat_14);
  lv_display_set_theme(display, theme);
  lv_obj_t *screen = lv_screen_active();
  lv_obj_set_style_bg_color(screen, lv_color_hex(COLOUR_GROUND), 0);
  lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

  // the top bar: home, title, link, output
  lv_obj_t *bar = lv_obj_create(screen);
  plainBox(bar, COLOUR_BAR);
  lv_obj_set_size(bar, DISPLAY_WIDTH, TOP_BAR_HEIGHT);
  lv_obj_align(bar, LV_ALIGN_TOP_LEFT, 0, 0);
  homeButton = button(bar, LV_SYMBOL_HOME, 40, 26);
  lv_obj_align(homeButton, LV_ALIGN_LEFT_MID, 2, 0);
  lv_obj_add_event_cb(homeButton, onHome, LV_EVENT_CLICKED, nullptr);
  titleLabel = label(bar, "", &lv_font_montserrat_16, COLOUR_TEXT);
  lv_obj_align(titleLabel, LV_ALIGN_LEFT_MID, 48, 0);
  linkDot = lv_obj_create(bar);
  plainBox(linkDot, COLOUR_DANGER);
  lv_obj_set_size(linkDot, 10, 10);
  lv_obj_set_style_radius(linkDot, LV_RADIUS_CIRCLE, 0);
  lv_obj_align(linkDot, LV_ALIGN_RIGHT_MID, -112, 0);
  outputBox = lv_obj_create(bar);
  plainBox(outputBox, COLOUR_BAR);
  lv_obj_set_size(outputBox, 100, TOP_BAR_HEIGHT);
  lv_obj_align(outputBox, LV_ALIGN_RIGHT_MID, -4, 0);
  lv_obj_t *outputText = label(outputBox, "Output", &lv_font_montserrat_14, COLOUR_MUTED);
  lv_obj_align(outputText, LV_ALIGN_LEFT_MID, 0, 0);
  outputSwitch = lv_switch_create(outputBox);
  lv_obj_set_size(outputSwitch, 44, 22);
  lv_obj_align(outputSwitch, LV_ALIGN_RIGHT_MID, 0, 0);
  lv_obj_add_event_cb(outputSwitch, onOutput, LV_EVENT_VALUE_CHANGED, nullptr);
  settingsButton = button(bar, LV_SYMBOL_SETTINGS, 40, 26);
  lv_obj_align(settingsButton, LV_ALIGN_RIGHT_MID, -2, 0);
  lv_obj_add_event_cb(settingsButton, onSettings, LV_EVENT_CLICKED, nullptr);

  // the page
  content = lv_obj_create(screen);
  lv_obj_set_size(content, DISPLAY_WIDTH, DISPLAY_HEIGHT - TOP_BAR_HEIGHT - POT_STRIP_HEIGHT);
  lv_obj_align(content, LV_ALIGN_TOP_LEFT, 0, TOP_BAR_HEIGHT);
  lv_obj_set_style_bg_opa(content, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(content, 0, 0);
  lv_obj_set_style_radius(content, 0, 0);
  lv_obj_set_style_pad_all(content, 6, 0);
  lv_obj_set_scroll_dir(content, LV_DIR_VER);
  // a visible bar: on a resistive screen a list that goes on is not obvious
  lv_obj_set_scrollbar_mode(content, LV_SCROLLBAR_MODE_ACTIVE);
  lv_obj_set_style_bg_color(content, lv_color_hex(COLOUR_ACCENT), LV_PART_SCROLLBAR);
  lv_obj_set_style_width(content, 4, LV_PART_SCROLLBAR);

  // the pot strip
  lv_obj_t *strip = lv_obj_create(screen);
  plainBox(strip, COLOUR_BAR);
  lv_obj_set_size(strip, DISPLAY_WIDTH, POT_STRIP_HEIGHT);
  lv_obj_align(strip, LV_ALIGN_BOTTOM_LEFT, 0, 0);
  // each knob a key: a tap edits what the pot plays
  for (int pot = 0; pot < 4; pot++) {
    lv_obj_t *cell = lv_obj_create(strip);
    plainBox(cell, COLOUR_BAR);
    lv_obj_set_size(cell, 80, POT_STRIP_HEIGHT);
    lv_obj_set_pos(cell, pot * 80, 0);
    lv_obj_add_flag(cell, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(cell, lv_color_hex(COLOUR_LINE), LV_STATE_PRESSED);
    lv_obj_add_event_cb(cell, onPot, LV_EVENT_CLICKED, (void *)(intptr_t)pot);
    lv_obj_t *dot = lv_obj_create(cell);
    plainBox(dot, potColours[pot]);
    lv_obj_set_size(dot, 10, 10);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_align(dot, LV_ALIGN_LEFT_MID, 3, 0);
    lv_obj_remove_flag(dot, LV_OBJ_FLAG_CLICKABLE);
    potLabels[pot] = label(cell, "-", &lv_font_montserrat_12, COLOUR_MUTED);
    lv_obj_align(potLabels[pot], LV_ALIGN_LEFT_MID, 15, 0);
  }

  // an error's toast above the strip
  toast = label(screen, "", &lv_font_montserrat_14, COLOUR_DANGER);
  lv_obj_set_style_bg_color(toast, lv_color_hex(COLOUR_CARD), 0);
  lv_obj_set_style_bg_opa(toast, LV_OPA_COVER, 0);
  lv_obj_set_style_pad_all(toast, 6, 0);
  lv_obj_set_style_radius(toast, 6, 0);
  lv_obj_align(toast, LV_ALIGN_BOTTOM_MID, 0, -POT_STRIP_HEIGHT - 4);
  lv_obj_add_flag(toast, LV_OBJ_FLAG_HIDDEN);

  // the cover while connecting
  cover = lv_obj_create(screen);
  plainBox(cover, COLOUR_GROUND);
  lv_obj_set_size(cover, DISPLAY_WIDTH, DISPLAY_HEIGHT);
  lv_obj_t *name = label(cover, "Audio-DSP-Lab", &lv_font_montserrat_28, COLOUR_TEXT);
  lv_obj_align(name, LV_ALIGN_CENTER, 0, -40);
  coverText = label(cover, "Connecting to the board ...", &lv_font_montserrat_16, COLOUR_MUTED);
  lv_obj_align(coverText, LV_ALIGN_CENTER, 0, 4);
  lv_obj_t *hint = label(cover, "Cable: CYD 22 to GPIO37, CYD 35 to GPIO4, GND", &lv_font_montserrat_12, COLOUR_MUTED);
  lv_obj_align(hint, LV_ALIGN_CENTER, 0, 40);
  lv_obj_t *version = label(cover, PANEL_NAME " " PANEL_VERSION, &lv_font_montserrat_12, COLOUR_MUTED);
  lv_obj_align(version, LV_ALIGN_BOTTOM_MID, 0, -8);

  rebuild();
  refresh();
}

void uiPoll() {
  static uint32_t polledAt = 0;
  const uint32_t now = millis();
  if (now - polledAt < 40) return;          // 25 refreshes a second at most
  polledAt = now;
  if (toastAt != 0 && now - toastAt > TOAST_MS) {
    lv_obj_add_flag(toast, LV_OBJ_FLAG_HIDDEN);
    toastAt = 0;
  }
  if (page != builtPage || board.paramsVersion != builtParams ||
      (page == PAGE_APPLICATION && (board.application + board.state != builtApplication || section != builtSection ||
                                    (board.application == "radio" && board.stationsVersion != builtStations)))) {
    rebuild();
    seenVersion = board.version - 1;
  }
  if (board.version != seenVersion) {
    seenVersion = board.version;
    refresh();
  }
}

// commands on the USB port, for screenshots and tests: "page home|app|settings",
// "section osc|filter|env|lfo|perf|all", "edit <parameter>", "close",
// "cursor <point>" (the Frequency Response plot's cursor on that point, -1 none),
// "view response|thd|spectrum" (the analysis' view)
void uiCommand(const String &line) {
  if (line.startsWith("page ")) {
    const String name = line.substring(5);
    editorClose();
    page = name == "settings" ? PAGE_SETTINGS : name == "app" ? PAGE_APPLICATION : PAGE_HOME;
    if (page == PAGE_SETTINGS) linkReadSettings();
  } else if (line.startsWith("section ")) {
    const String name = line.substring(8);
    for (int i = 0; i < sectionCount; i++) {
      String title = sectionTitles[i];
      title.toLowerCase();
      if (title == name) section = (Section)i;
    }
  } else if (line.startsWith("edit ")) {
    uiPoll();
    const int index = linkFindParam(line.substring(5).c_str());
    if (index >= 0) editorOpen(index);
  } else if (line == "close") {
    editorClose();
  } else if (line.startsWith("cursor ")) {
    cursorIndex = line.substring(7).toInt();
    drawCursor();
  } else if (line == "view thd" || line == "view response" || line == "view spectrum") {
    view = line == "view thd" ? VIEW_THD : line == "view spectrum" ? VIEW_SPECTRUM : VIEW_RESPONSE;
    drawnPoints = drawnSpectrum = 0xFFFFFFFF;
    refreshAnalyzer();
  } else {
    Serial.printf("? %s\n", line.c_str());
    return;
  }
  Serial.printf("ok %s\n", line.c_str());
}
