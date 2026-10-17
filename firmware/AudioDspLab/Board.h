// ****************************************************************************
// ***                        T H E   E S P 3 2   K I T                     ***
// ****************************************************************************
//  The Elektor board takes either of two Espressif kits, whose headers
//  route two pins differently (Elektor_AudioDSP.h, ESP32_MODULE):
//
//    kit                   chip             DSP MP0 (I2S data)   DSP MP1
//    ESP32-PICO-KIT V4     ESP32-PICO-D4    GPIO18               GPIO23
//    ESP32-PICO-KIT-1      ESP32-PICO-V3    GPIO7                GPIO8
//
//  Every other pin is the same. The firmware tells the kits apart by the
//  chip: the PICO-V3 is the PICO-D4 package at chip revision 3 (or the
//  PICO-V3-02 package), as esptool reports it.
// ----------------------------------------------------------------------------

#ifndef BOARD_H
#define BOARD_H

#include <stdint.h>

// the GPIO that carries I2S data to the DSP's MP0 (SDATA_IN0)
uint8_t boardI2sDataPin();
// "esp32-pico-kit" or "esp32-pico-kit-1", for the protocol's info
const char *boardKit();

#endif

// ============================================================================
// ==================================  E O F  =================================
// ============================================================================
