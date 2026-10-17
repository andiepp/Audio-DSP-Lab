#include "Eeprom24.h"
#include "Config.h"
#include <Arduino.h>
#include <Wire.h>

#define READ_CHUNK                    32
#define WRITE_TIMEOUT_MS              20      // tWR is 5 ms; ack polling

void eepromInit() {
  pinMode(PIN_EEPROM_WP, OUTPUT);
  digitalWrite(PIN_EEPROM_WP, HIGH);          // protected
}

static bool readChunk(uint16_t address, uint8_t *buffer, size_t length) {
  Wire.beginTransmission(EEPROM_I2C_ADDRESS);
  Wire.write(address >> 8);
  Wire.write(address & 0xFF);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom((uint8_t)EEPROM_I2C_ADDRESS, (uint8_t)length) != length) return false;
  for (size_t i = 0; i < length; i++) buffer[i] = Wire.read();
  return true;
}

bool eepromRead(size_t address, uint8_t *buffer, size_t length) {
  if (address + length > EEPROM_SIZE) return false;
  for (size_t offset = 0; offset < length; offset += READ_CHUNK) {
    size_t n = min((size_t)READ_CHUNK, length - offset);
    if (!readChunk(address + offset, buffer + offset, n)) return false;
  }
  return true;
}

bool eepromMatches(const uint8_t *image, size_t length) {
  uint8_t buffer[READ_CHUNK];
  for (size_t offset = 0; offset < length; offset += READ_CHUNK) {
    size_t n = min((size_t)READ_CHUNK, length - offset);
    if (!readChunk(offset, buffer, n)) return false;
    if (memcmp(buffer, image + offset, n) != 0) return false;
  }
  return true;
}

static bool waitWriteComplete() {
  uint32_t start = millis();
  while (millis() - start < WRITE_TIMEOUT_MS) {
    Wire.beginTransmission(EEPROM_I2C_ADDRESS);
    if (Wire.endTransmission() == 0) return true;   // device acks again
    delayMicroseconds(500);
  }
  return false;
}

bool eepromWrite(const uint8_t *image, size_t length) {
  if (length == 0 || length > EEPROM_SIZE) return false;
  bool ok = true;
  digitalWrite(PIN_EEPROM_WP, LOW);
  for (size_t offset = 0; offset < length && ok; ) {
    // never cross a page boundary
    size_t n = min((size_t)EEPROM_PAGE_SIZE - (offset % EEPROM_PAGE_SIZE), length - offset);
    Wire.beginTransmission(EEPROM_I2C_ADDRESS);
    Wire.write(offset >> 8);
    Wire.write(offset & 0xFF);
    Wire.write(image + offset, n);
    ok = Wire.endTransmission() == 0 && waitWriteComplete();
    offset += n;
  }
  digitalWrite(PIN_EEPROM_WP, HIGH);
  return ok && eepromMatches(image, length);
}
