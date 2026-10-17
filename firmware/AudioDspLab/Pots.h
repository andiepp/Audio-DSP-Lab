// ****************************************************************************
// ***                       P O T E N T I O M E T E R S                    ***
// ****************************************************************************
//  P1..P4 read by the ESP32 ADC, smoothed and scaled to the pots' real
//  travel (0..4095 from stop to stop; see Pots.cpp). Mapping to parameters
//  and take-over live in the application manager.
// ----------------------------------------------------------------------------

#ifndef POTS_H
#define POTS_H

#include <stdint.h>

#define POT_COUNT                     4

void potsInit();
bool potsPoll();                    // true when a new sample set was taken
uint16_t potRaw(uint8_t index);     // smoothed and scaled, 0..4095 over the pot's travel

#endif
