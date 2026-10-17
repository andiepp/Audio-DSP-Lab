// ****************************************************************************
// ***                        A D A U 1 7 0 1   A C C E S S                 ***
// ****************************************************************************
//  Reset control, presence, and parameter writes via safeload (5.23 fixed
//  point, up to five parameters per transfer, applied at the next sample).
// ----------------------------------------------------------------------------

#ifndef DSP_H
#define DSP_H

#include <stdint.h>

void dspInit();
void dspHoldReset();
void dspReleaseReset();
bool dspInReset();
bool dspIsPresent();
bool dspWaitForBoot(uint32_t timeoutMs);    // releases reset, polls I2C

// writes 'count' (1..5) consecutive-or-not parameters in one safeload
bool dspSafeload(const uint16_t *addresses, const float *values, int count);
bool dspSafeloadOne(uint16_t address, float value);

// same, raw 28-bit words: for integer parameters such as a switch index,
// which SigmaStudio stores as plain integers, not 5.23 fixed point
bool dspSafeloadRaw(const uint16_t *addresses, const int32_t *words, int count);

int32_t dspToFixed(float value);            // 5.23
bool dspReadParameter(uint16_t address, int32_t &word);   // parameter RAM, 4 bytes as read

// data capture (control port readback, datasheet Tables 27/28): a capture
// register (0x081A or 0x081B) is told which program point to capture by
// SigmaStudio's readback setting (program count and register select, from
// the export); reading it returns the 24-bit value there, 5.19 fixed point,
// sign-extended
bool dspCaptureSelect(uint16_t reg, uint16_t setting);
bool dspCaptureRead(uint16_t reg, int32_t &value);

#endif
