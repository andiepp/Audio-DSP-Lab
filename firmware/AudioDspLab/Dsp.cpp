#include "Dsp.h"
#include "Config.h"
#include <Arduino.h>
#include <Wire.h>
#include <math.h>

#define DSP_CORE_REGISTER             0x081C
#define DSP_SAFELOAD_DATA0            0x0810
#define DSP_SAFELOAD_ADDRESS0         0x0815
#define DSP_CORE_IST                  0x0020    // initiate safeload transfer

void dspInit() {
  pinMode(PIN_DSP_RESET, OUTPUT);
  digitalWrite(PIN_DSP_RESET, LOW);
}

void dspHoldReset() {
  digitalWrite(PIN_DSP_RESET, LOW);
}

void dspReleaseReset() {
  digitalWrite(PIN_DSP_RESET, HIGH);
}

bool dspInReset() {
  return digitalRead(PIN_DSP_RESET) == LOW;
}

bool dspIsPresent() {
  Wire.beginTransmission(DSP_I2C_ADDRESS);
  return Wire.endTransmission() == 0;
}

bool dspWaitForBoot(uint32_t timeoutMs) {
  dspReleaseReset();
  uint32_t start = millis();
  delay(50);
  while (millis() - start < timeoutMs) {
    if (dspIsPresent()) return true;
    delay(50);
  }
  return dspIsPresent();
}

static void writeRegister(uint16_t address, const uint8_t *data, size_t length) {
  Wire.beginTransmission(DSP_I2C_ADDRESS);
  Wire.write(address >> 8);
  Wire.write(address & 0xFF);
  Wire.write(data, length);
  Wire.endTransmission();
}

static bool readCoreRegister(uint16_t &value) {
  Wire.beginTransmission(DSP_I2C_ADDRESS);
  Wire.write(DSP_CORE_REGISTER >> 8);
  Wire.write(DSP_CORE_REGISTER & 0xFF);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom((uint8_t)DSP_I2C_ADDRESS, (uint8_t)2) != 2) return false;
  value = (Wire.read() << 8) | Wire.read();
  return true;
}

// one parameter RAM word as the DSP holds it (the 28-bit 5.23 value in the
// low bits of 4 bytes); a diagnostic read, never part of a control path
bool dspReadParameter(uint16_t address, int32_t &word) {
  if (dspInReset()) return false;
  Wire.beginTransmission(DSP_I2C_ADDRESS);
  Wire.write(address >> 8);
  Wire.write(address & 0xFF);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom((uint8_t)DSP_I2C_ADDRESS, (uint8_t)4) != 4) return false;
  uint32_t raw = 0;
  for (int i = 0; i < 4; i++) raw = (raw << 8) | (uint8_t)Wire.read();
  word = (int32_t)raw;
  return true;
}

bool dspCaptureSelect(uint16_t reg, uint16_t setting) {
  if (dspInReset()) return false;
  Wire.beginTransmission(DSP_I2C_ADDRESS);
  Wire.write(reg >> 8);
  Wire.write(reg & 0xFF);
  Wire.write(setting >> 8);
  Wire.write(setting & 0xFF);
  return Wire.endTransmission() == 0;
}

bool dspCaptureRead(uint16_t reg, int32_t &value) {
  if (dspInReset()) return false;
  Wire.beginTransmission(DSP_I2C_ADDRESS);
  Wire.write(reg >> 8);
  Wire.write(reg & 0xFF);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom((uint8_t)DSP_I2C_ADDRESS, (uint8_t)3) != 3) return false;
  uint32_t raw = 0;
  for (int i = 0; i < 3; i++) raw = (raw << 8) | (uint8_t)Wire.read();
  value = (raw & 0x800000) ? (int32_t)(raw | 0xFF000000) : (int32_t)raw;
  return true;
}

int32_t dspToFixed(float value) {
  if (value > 15.999999f) value = 15.999999f;
  if (value < -16.0f) value = -16.0f;
  return (int32_t)lroundf(value * 8388608.0f);
}

// one parameter word in the 5-byte safeload data format
static void toSafeload(int32_t word, uint8_t *buffer) {
  buffer[0] = 0x00;
  buffer[1] = (word >> 24) & 0xFF;
  buffer[2] = (word >> 16) & 0xFF;
  buffer[3] = (word >> 8) & 0xFF;
  buffer[4] = word & 0xFF;
}

bool dspSafeloadRaw(const uint16_t *addresses, const int32_t *words, int count) {
  if (count < 1 || count > 5 || dspInReset()) return false;
  uint16_t core;
  if (!readCoreRegister(core)) return false;
  for (int i = 0; i < count; i++) {
    uint8_t data[5];
    uint8_t address[2] = { (uint8_t)(addresses[i] >> 8), (uint8_t)(addresses[i] & 0xFF) };
    toSafeload(words[i], data);
    writeRegister(DSP_SAFELOAD_DATA0 + i, data, sizeof(data));
    writeRegister(DSP_SAFELOAD_ADDRESS0 + i, address, sizeof(address));
  }
  uint16_t start = core | DSP_CORE_IST;
  uint8_t control[2] = { (uint8_t)(start >> 8), (uint8_t)(start & 0xFF) };
  writeRegister(DSP_CORE_REGISTER, control, sizeof(control));
  delayMicroseconds(100);   // transfer happens at the next sample (~21 us)
  return true;
}

bool dspSafeload(const uint16_t *addresses, const float *values, int count) {
  if (count < 1 || count > 5) return false;
  int32_t words[5];
  for (int i = 0; i < count; i++) words[i] = dspToFixed(values[i]);
  return dspSafeloadRaw(addresses, words, count);
}

bool dspSafeloadOne(uint16_t address, float value) {
  return dspSafeload(&address, &value, 1);
}
