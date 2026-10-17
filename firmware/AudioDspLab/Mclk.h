// ****************************************************************************
// ***                       D S P   M A S T E R   C L O C K                ***
// ****************************************************************************
//  The ADAU1701 is clocked by the ESP32 (JP1 on 'ext. MCLK'): MCLK = 256 x fs
//  on GPIO0 from an I2S TX channel (new IDF driver, APLL). Only MCLK is
//  routed; BCLK, LRCLK and data stay free for an application that streams
//  audio, which stops this clock and runs its own I2S at 256 x fs instead.
// ----------------------------------------------------------------------------

#ifndef MCLK_H
#define MCLK_H

#include <stdint.h>

bool mclkStart(uint32_t sampleRate);
void mclkStop();
bool mclkRunning();

#endif
