// ****************************************************************************
// ***         T H E   O P E R A T O R ' S   E E P R O M   P R O G R A M     ***
// ****************************************************************************
//  In the SigmaStudio mode the EEPROM holds your own program.
//  Selecting another application writes that application's image over it,
//  so the firmware keeps a copy:
//
//  - leaving the mode: the EEPROM's self-boot image, read up to its end
//    message, is copied to the ESP32's flash (the 'spiffs' data partition,
//    used raw, a header with length and CRC) unless it is one of the
//    firmware's own images or the copy already holds it;
//  - entering the mode: if the EEPROM holds one of the firmware's own images
//    and a copy exists, the copy is written back before the DSP boots. A
//    program of your found there is left as it is.
//
//  Both need the bus: the USBi off the header, as for any EEPROM write of
//  the firmware.
// ----------------------------------------------------------------------------

#ifndef STUDIO_BACKUP_H
#define STUDIO_BACKUP_H

#include <stdint.h>
#include <stddef.h>

// tells whether an image, read up to its end message, is one of the firmware's
// own (whose arrays may carry padding after the end message)
typedef bool (*NodeImagePredicate)(const uint8_t *image, size_t length);

enum BackupResult : uint8_t {
  BACKUP_NONE      = 0,   // nothing to do
  BACKUP_DONE      = 1,   // copied (leaving) or written back (entering)
  BACKUP_FAILED    = 2,   // the EEPROM or the flash could not be read or written
};

BackupResult studioBackupSave(NodeImagePredicate isNodeImage);
BackupResult studioBackupRestore(NodeImagePredicate isNodeImage);

// diagnostic: whether a copy exists, its length and CRC-32
bool studioBackupInfo(uint16_t &length, uint32_t &crc);

// diagnostic: what the EEPROM holds as the backup sees it: 0 an image,
// 1 no image, 2 a read failed; its length, whether it is one of the firmware's
// images, its CRC-32, and the position where the scan stopped
uint8_t studioBackupScan(NodeImagePredicate isNodeImage, uint16_t &length, bool &nodeImage,
                         uint32_t &crc, uint16_t &stoppedAt);

#endif
