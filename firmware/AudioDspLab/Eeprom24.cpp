#include "Eeprom24.h"
#include "Config.h"
#include <Arduino.h>
#include <Wire.h>
#include <driver/gpio.h>

#define READ_CHUNK                    32
#define WRITE_TIMEOUT_MS              20      // tWR is 5 ms; ack polling
#define FREE_BUS_CLOCKS               9       // a byte and its acknowledge at most

static const uint16_t retryDelayMs[] = { 0, 5, 50 };   // after freeing the bus

static EepromFaults faults;

// bit 0 SDA, bit 1 SCL, as the pads read them
static uint8_t lineLevels() {
  return (gpio_get_level((gpio_num_t)PIN_I2C_SDA) ? 1 : 0) | (gpio_get_level((gpio_num_t)PIN_I2C_SCL) ? 2 : 0);
}

void eepromInit() {
  pinMode(PIN_EEPROM_WP, OUTPUT);
  digitalWrite(PIN_EEPROM_WP, HIGH);          // protected
  pinMode(PIN_I2C_SDA, INPUT);
  pinMode(PIN_I2C_SCL, INPUT);
  faults.linesAtStart = lineLevels();
}

// a device stopped in the middle of a byte it sends (by a reset or a
// power-up during a transfer) holds SDA low until SCL clocks the byte out;
// the I2C driver cannot start a transfer until then. Clocks until SDA is
// released, a START and a STOP, then the driver afresh. Returns the clocks
static uint8_t freeBus() {
  Wire.end();
  pinMode(PIN_I2C_SDA, INPUT);
  gpio_set_level((gpio_num_t)PIN_I2C_SCL, 1);  // released as it turns into an output
  pinMode(PIN_I2C_SCL, OUTPUT_OPEN_DRAIN);
  digitalWrite(PIN_I2C_SCL, HIGH);
  delayMicroseconds(5);
  uint8_t clocks = 0;
  while (clocks < FREE_BUS_CLOCKS && digitalRead(PIN_I2C_SDA) == LOW) {
    digitalWrite(PIN_I2C_SCL, LOW);
    delayMicroseconds(5);
    digitalWrite(PIN_I2C_SCL, HIGH);
    delayMicroseconds(5);
    clocks++;
  }
  pinMode(PIN_I2C_SDA, OUTPUT_OPEN_DRAIN);
  digitalWrite(PIN_I2C_SDA, LOW);             // START
  delayMicroseconds(5);
  digitalWrite(PIN_I2C_SDA, HIGH);            // STOP
  delayMicroseconds(5);
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL, I2C_CLOCK_HZ);
  return clocks;
}

static bool readChunkOnce(uint16_t address, uint8_t *buffer, size_t length) {
  Wire.beginTransmission(EEPROM_I2C_ADDRESS);
  Wire.write(address >> 8);
  Wire.write(address & 0xFF);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom((uint8_t)EEPROM_I2C_ADDRESS, (uint8_t)length) != length) return false;
  for (size_t i = 0; i < length; i++) buffer[i] = Wire.read();
  return true;
}

// a failed read is tried again after the bus is freed; the first failure
// since the start is kept for 'diag boot'
static bool readChunk(uint16_t address, uint8_t *buffer, size_t length) {
  if (readChunkOnce(address, buffer, length)) return true;
  faults.failures++;
  bool first = faults.failures == 1;
  if (first) {
    faults.firstMs = millis();
    faults.firstAddress = address;
    faults.linesAtFailure = lineLevels();
  }
  for (uint16_t wait : retryDelayMs) {
    delay(wait);
    uint8_t clocks = freeBus();
    if (first && clocks > faults.firstClocks) faults.firstClocks = clocks;
    if (readChunkOnce(address, buffer, length)) {
      faults.recovered++;
      return true;
    }
  }
  return false;
}

bool eepromRead(size_t address, uint8_t *buffer, size_t length) {
  if (address + length > EEPROM_SIZE) return false;
  for (size_t offset = 0; offset < length; offset += READ_CHUNK) {
    size_t n = min((size_t)READ_CHUNK, length - offset);
    if (!readChunk(address + offset, buffer + offset, n)) return false;
  }
  return true;
}

const EepromFaults &eepromFaults() {
  return faults;
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
