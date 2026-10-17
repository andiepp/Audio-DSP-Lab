#include "Board.h"
#include <Arduino.h>
#include "soc/efuse_reg.h"

#define PIN_MP0_PICO_D4               18
#define PIN_MP0_PICO_V3               7

static bool isPicoV3() {
  const uint32_t package = REG_GET_FIELD(EFUSE_BLK0_RDATA3_REG, EFUSE_RD_CHIP_PACKAGE) & 0x7;
  if (package == EFUSE_RD_CHIP_VER_PKG_ESP32PICOV302) return true;
  return package == EFUSE_RD_CHIP_VER_PKG_ESP32PICOD4 && ESP.getChipRevision() / 100 >= 3;
}

uint8_t boardI2sDataPin() {
  return isPicoV3() ? PIN_MP0_PICO_V3 : PIN_MP0_PICO_D4;
}

const char *boardKit() {
  return isPicoV3() ? "esp32-pico-kit-1" : "esp32-pico-kit";
}
