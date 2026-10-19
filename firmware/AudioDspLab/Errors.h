// ****************************************************************************
// ***                          E R R O R   C O D E S                       ***
// ****************************************************************************
//  Every module reports failures with one of these codes; the protocol
//  turns them into the words of an "err" line (Protocol.cpp, see
//  docs/Protocol.md).
// ----------------------------------------------------------------------------

#ifndef ERRORS_H
#define ERRORS_H

#include <stdint.h>

enum ErrorCode : uint8_t {
  ERR_NONE                  = 0,
  ERR_UNKNOWN_COMMAND       = 1,    // no such command
  ERR_BAD_ARGUMENT          = 2,    // missing, extra or malformed argument
  ERR_OUT_OF_RANGE          = 3,    // a value outside what the target accepts
  ERR_UNKNOWN_PARAMETER     = 4,    // the running application has no such parameter
  ERR_LINE_TOO_LONG         = 5,    // a request longer than PROTOCOL_LINE_MAX
  ERR_UNKNOWN_APPLICATION   = 0x10,
  ERR_EEPROM_FAILED         = 0x11, // EEPROM write or read failed (a USBi on the header?)
  ERR_DSP_NOT_RESPONDING    = 0x12, // the DSP did not boot or answer
  ERR_SERVICE_MODE          = 0x14, // refused while the DSP belongs to SigmaStudio
  ERR_DSP_WRITE_FAILED      = 0x15,
  ERR_WRONG_APPLICATION     = 0x16, // the request needs another application running
  ERR_NO_SWEEP              = 0x17, // no complete sweep to keep as the reference
  ERR_STORE_FAILED          = 0x18, // NVS refused a write
  ERR_NO_CLOCK              = 0x19, // no clock on the DSP's serial output
  ERR_BUSY                  = 0x1A, // the serial output capture is in use (the THD measurement)
  ERR_NO_MEMORY             = 0x1B, // not enough free heap for the measurement
};

#endif

// ============================================================================
// ==================================  E O F  =================================
// ============================================================================
