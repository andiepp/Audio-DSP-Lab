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

void eepromInit();
bool eepromMatches(const uint8_t *image, size_t length);   // compare from address 0
bool eepromRead(size_t address, uint8_t *buffer, size_t length);
bool eepromWrite(const uint8_t *image, size_t length);     // write + verify

#endif
