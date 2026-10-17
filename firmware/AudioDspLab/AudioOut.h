// ****************************************************************************
// ***                         A U D I O   O U T                            ***
// ****************************************************************************
//  The ESP32's I2S output to the DSP (MP0 data, MP4 LRCLK, MP5 BCLK) at
//  48 kHz, 16 bits, stereo, with the DSP's 12.288 MHz MCLK from the APLL:
//  the synthesizer and the generator send what they compute through it.
//  One user at a time.
// ----------------------------------------------------------------------------

#ifndef AUDIO_OUT_H
#define AUDIO_OUT_H

#include <stddef.h>
#include <stdint.h>

#define AUDIO_OUT_RATE                48000

bool audioOutStart();
void audioOutStop();
// blocks until the DMA takes the frames (interleaved left, right)
void audioOutWrite(const int16_t *frames, size_t bytes);

#endif
