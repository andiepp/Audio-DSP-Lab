// ****************************************************************************
// ***              P O T S   T O   T H E   D S P ' S   A U X   A D C       ***
// ****************************************************************************
//  The pots are wired to the ESP32 only. The DSP's four auxiliary ADC
//  inputs see them through the ESP32's two DACs and a 74HC4053
//  multiplexer, whose outputs hold their voltage between refreshes
//  (Elektor's ESP32_dac_mux does the same):
//
//    select LOW:   DAC X (GPIO25) -> X0 -> MP3 = ADC2    DAC Y (GPIO26) -> Y0 -> MP9 = ADC0
//    select HIGH:  DAC X (GPIO25) -> X1 -> MP2 = ADC1    DAC Y (GPIO26) -> Y1 -> MP8 = ADC3
//
//  P1 -> ADC0, P2 -> ADC1, P3 -> ADC2, P4 -> ADC3, as Elektor maps them.
//  The firmware alternates the two positions continuously; each input is
//  refreshed every 2 x DAC_MUX_HOLD_MS. The new ESP-IDF DAC driver, as the
//  firmware's other drivers (the legacy one would abort at boot beside them).
// ----------------------------------------------------------------------------

#ifndef DAC_MUX_H
#define DAC_MUX_H

#include <stdint.h>

#define DAC_MUX_HOLD_MS               10      // per position: each input every 20 ms

bool dacMuxBegin();                         // DACs on, the first position written
void dacMuxEnd();                           // DACs off, select lines released
void dacMuxPoll();                          // the next position when its time came
bool dacMuxRunning();

// diagnostic: the last 8-bit value written for ADC0..ADC3
uint8_t dacMuxValue(uint8_t input);

#endif
