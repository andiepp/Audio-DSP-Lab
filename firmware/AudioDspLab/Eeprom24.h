// ****************************************************************************
// ***                  2 4 C 2 5 6   E E P R O M   ( D S P   B O O T )      ***
// ****************************************************************************
//  The ADAU1701 self-boots from this EEPROM. Writes are only allowed while
//  the DSP is held in reset (it is the bus master during self-boot).
// ----------------------------------------------------------------------------

#ifndef EEPROM24_H
#define EEPROM24_H

#include <stdint.h>
#include <stddef.h>

// diagnostic: EEPROM reads that failed since the start, how many of them a
// retry after freeing the bus cured, and the first one: when, where, the
// lines then (bit 0 SDA, bit 1 SCL high) and the clocks that freed SDA.
// linesAtStart: the lines before the firmware's I2C driver took them
struct EepromFaults {
  uint16_t failures;
  uint16_t recovered;
  uint32_t firstMs;
  uint16_t firstAddress;
  uint8_t linesAtFailure;
  uint8_t firstClocks;
  uint8_t linesAtStart;
};

void eepromInit();
const EepromFaults &eepromFaults();
bool eepromMatches(const uint8_t *image, size_t length);   // compare from address 0
bool eepromRead(size_t address, uint8_t *buffer, size_t length);
bool eepromWrite(const uint8_t *image, size_t length);     // write + verify

#endif
