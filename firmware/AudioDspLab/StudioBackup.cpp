#include "StudioBackup.h"
#include "Eeprom24.h"
#include "Config.h"
#include <Arduino.h>
#include <esp_partition.h>
#include <esp_rom_crc.h>
#include <string.h>

#define BACKUP_MAGIC                  0x534C4D41UL    // "AMLS"
#define BACKUP_MAX_LENGTH             16384           // a full self-boot image is 9248 bytes and some
#define SECTOR_SIZE                   4096

struct BackupHeader {
  uint32_t magic;
  uint32_t length;
  uint32_t crc;
};

static const esp_partition_t *partition() {
  return esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS, nullptr);
}

// what the EEPROM was found to hold
enum ImageScan : uint8_t { SCAN_IMAGE, SCAN_NO_IMAGE, SCAN_READ_FAILED };

// the self-boot image's length: its messages up to and including the end
// (datasheet, Table 19). A read that fails is not taken for an EEPROM
// without an image: that would let another image overwrite the program
static size_t lastPosition = 0;                       // diagnostic: where the last scan stopped

static ImageScan imageLength(size_t &length) {
  size_t position = 0;
  uint8_t head[3];
  length = 0;
  while (position < BACKUP_MAX_LENGTH) {
    lastPosition = position;
    if (!eepromRead(position, head, 1)) return SCAN_READ_FAILED;
    switch (head[0]) {
      case 0x00:                                       // end
      case 0x06:                                       // end and wait for writeback
        length = position + 1;
        return SCAN_IMAGE;
      case 0x01:                                       // write: 2 bytes length, then the bytes
        if (!eepromRead(position + 1, head + 1, 2)) return SCAN_READ_FAILED;
        position += 3 + ((size_t)head[1] << 8 | head[2]);
        break;
      case 0x02:                                       // delay: 2 bytes
        position += 3;
        break;
      case 0x03:                                       // no-op
      case 0x04:                                       // multiple writeback
      case 0x05:                                       // writeback on falling edge
        position += 1;
        break;
      default:
        return SCAN_NO_IMAGE;                          // not an image
    }
  }
  return SCAN_NO_IMAGE;
}

// the EEPROM's image into a new buffer (free it); nullptr unless SCAN_IMAGE
static uint8_t *readImage(size_t &length, ImageScan &scan) {
  scan = imageLength(length);
  if (scan != SCAN_IMAGE) return nullptr;
  uint8_t *image = (uint8_t *)malloc(length);
  if (image == nullptr || !eepromRead(0, image, length)) {
    free(image);
    scan = SCAN_READ_FAILED;
    return nullptr;
  }
  return image;
}

static bool readHeader(const esp_partition_t *store, BackupHeader &header) {
  return store != nullptr
      && esp_partition_read(store, 0, &header, sizeof(header)) == ESP_OK
      && header.magic == BACKUP_MAGIC
      && header.length > 0 && header.length <= BACKUP_MAX_LENGTH;
}

BackupResult studioBackupSave(NodeImagePredicate isNodeImage) {
  size_t length;
  ImageScan scan;
  uint8_t *image = readImage(length, scan);
  if (scan == SCAN_READ_FAILED) return BACKUP_FAILED;  // unknown: the switch is refused
  if (image == nullptr) return BACKUP_NONE;            // nothing that boots: nothing to keep
  BackupResult result = BACKUP_NONE;
  const esp_partition_t *store = partition();
  BackupHeader header;
  uint32_t crc = esp_rom_crc32_le(0, image, length);
  if (isNodeImage(image, length)) {
    result = BACKUP_NONE;
  } else if (readHeader(store, header) && header.length == length && header.crc == crc) {
    result = BACKUP_NONE;                              // the copy holds it already
  } else if (store == nullptr) {
    result = BACKUP_FAILED;
  } else {
    header = { BACKUP_MAGIC, (uint32_t)length, crc };
    size_t span = (sizeof(header) + length + SECTOR_SIZE - 1) / SECTOR_SIZE * SECTOR_SIZE;
    bool written = esp_partition_erase_range(store, 0, span) == ESP_OK
                && esp_partition_write(store, sizeof(header), image, length) == ESP_OK
                && esp_partition_write(store, 0, &header, sizeof(header)) == ESP_OK;
    result = written ? BACKUP_DONE : BACKUP_FAILED;
  }
  free(image);
  return result;
}

BackupResult studioBackupRestore(NodeImagePredicate isNodeImage) {
  const esp_partition_t *store = partition();
  BackupHeader header;
  if (!readHeader(store, header)) return BACKUP_NONE;  // no copy
  size_t length;
  ImageScan scan;
  uint8_t *current = readImage(length, scan);
  bool nodeImage = current != nullptr && isNodeImage(current, length);
  free(current);
  if (scan == SCAN_READ_FAILED) return BACKUP_FAILED;
  if (!nodeImage) return BACKUP_NONE;                  // your SigmaStudio program, or none: left as it is
  uint8_t *copy = (uint8_t *)malloc(header.length);
  if (copy == nullptr) return BACKUP_FAILED;
  bool valid = esp_partition_read(store, sizeof(header), copy, header.length) == ESP_OK
            && esp_rom_crc32_le(0, copy, header.length) == header.crc;
  // a copy of one of the firmware's own images is no program of your
  // (an early version could take one for it): never written back
  if (!valid || isNodeImage(copy, header.length)) {
    free(copy);
    return valid ? BACKUP_NONE : BACKUP_FAILED;
  }
  bool restored = eepromWrite(copy, header.length);
  free(copy);
  return restored ? BACKUP_DONE : BACKUP_FAILED;
}

uint8_t studioBackupScan(NodeImagePredicate isNodeImage, uint16_t &length, bool &nodeImage,
                         uint32_t &crc, uint16_t &stoppedAt) {
  size_t found;
  ImageScan scan;
  uint8_t *image = readImage(found, scan);
  length = (uint16_t)found;
  nodeImage = image != nullptr && isNodeImage(image, found);
  crc = image != nullptr ? esp_rom_crc32_le(0, image, found) : 0;
  stoppedAt = (uint16_t)lastPosition;
  free(image);
  return scan;
}

bool studioBackupInfo(uint16_t &length, uint32_t &crc) {
  BackupHeader header;
  if (!readHeader(partition(), header)) {
    length = 0;
    crc = 0;
    return false;
  }
  length = (uint16_t)header.length;
  crc = header.crc;
  return true;
}
