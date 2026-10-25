// ****************************************************************************
// ***                                                                      ***
// ***                     A U D I O - D S P - L A B                        ***
// ***                         configuration                                ***
// ***                                                                      ***
// ****************************************************************************
//  Board:   Elektor Audio DSP FX v2.2 (ESP32-PICO-D4 + ADAU1701 + 24C256)
//  Core:    ESP32 by Espressif Systems 3.3.10, board "ESP32 PICO-D4",
//           partition scheme "Minimal SPIFFS"
//  JP1:     on 'ext. MCLK' (the firmware supplies 12.288 MHz on GPIO0)
//  JP2:     on 'Selfboot'
//  Kit:     ESP32-PICO-KIT V4 (PICO-D4) or ESP32-PICO-KIT-1 (PICO-V3), Board.h
// ----------------------------------------------------------------------------

#ifndef CONFIG_H
#define CONFIG_H

#include <stdint.h>

// --- firmware ---------------------------------------------------------------
#define FIRMWARE_NAME                 "Audio-DSP-Lab"
#define FIRMWARE_VERSION_MAJOR        1
#define FIRMWARE_VERSION_MINOR        4
#define PROTOCOL_VERSION              1

// --- serial protocol (docs/Protocol.md) ---------------------------------------
#define PROTOCOL_BAUD_RATE            921600
#define PROTOCOL_PANEL_BAUD_RATE      115200  // the panel port: a cable, not USB
#define PROTOCOL_LINE_MAX             320     // one request, without its line end
#define PROTOCOL_RX_BUFFER            1024
#define PROTOCOL_TX_BUFFER            2048
#define PROTOCOL_VALUE_EVENT_MS       50      // pot-driven values: at most 20 events/s each
#define PROTOCOL_POT_EVENT_MS         100     // pot positions: at most 10 events/s
#define PROTOCOL_POT_EVENT_COUNTS     80      // a pot must move this far to be reported (2 %; the
                                              // scaled readings jitter by up to about 65 counts)
#define PROTOCOL_VOICE_EVENT_MS       50      // synthesizer voice: at most 20 events/s

// --- pins -------------------------------------------------------------------
#define PIN_LED                       2
#define PIN_DSP_RESET                 12      // active low
#define PIN_EEPROM_WP                 21      // high = write protected
#define PIN_I2C_SDA                   9
#define PIN_I2C_SCL                   10
#define PIN_MCLK                      0       // I2S MCLK -> JP1 -> MCLKI
#define PIN_I2S_BCLK                  27      // -> MP5 INPUT_BCLK (radio only)
#define PIN_I2S_LRCLK                 14      // -> MP4 INPUT_LRCLK (radio only)
// I2S data -> MP0 SDATA_IN0 (radio, synthesizer): GPIO18 or GPIO7 by kit, Board.h
#define PIN_SOUT_DATA                 32      // <- MP6 SDATA_OUT0: the DSP's serial output (Capture.cpp)
#define PIN_SOUT_LRCLK                13      // <- MP10 OUTPUT_LRCLK
#define PIN_SOUT_BCLK                 19      // <- MP11 OUTPUT_BCLK
#define PIN_POT1                      35      // P1
#define PIN_POT2                      34      // P2
#define PIN_POT3                      39      // P3
#define PIN_POT4                      36      // P4
#define PIN_DAC_MUX0                  5       // 74HC4053 select X: DAC X (GPIO25) to ADC2 or ADC1
#define PIN_DAC_MUX1                  15      // 74HC4053 select Y: DAC Y (GPIO26) to ADC0 or ADC3
#define PIN_MIDI_IN                   38      // K5 -> opto-coupler IC3 (synthesizer only)
#define PIN_MIDI_OUT                  4       // R36 not fitted: leads nowhere (now the panel port's TX)
#define PIN_PANEL_RX                  37      // panel port (UART2) RX: module pin 24, input-only, not connected on the board
#define PIN_PANEL_TX                  4       // panel port (UART2) TX: the R36 pad (MIDI out, not fitted)

// --- I2C devices --------------------------------------------------------------
#define I2C_CLOCK_HZ                  400000
#define DSP_I2C_ADDRESS               0x34    // ADAU1701, ADDR0/1 low
#define EEPROM_I2C_ADDRESS            0x50    // 24C256, A0..A2 low
#define EEPROM_PAGE_SIZE              64
#define EEPROM_SIZE                   32768

// --- audio clock --------------------------------------------------------------
#define DSP_SAMPLE_RATE               48000   // MCLK = 256 x fs = 12.288 MHz
// The ESP32 drives MCLK on GPIO0 from an I2S channel clocked by the APLL.
// On the ESP32 that pin carries the APLL itself, and the driver sets the
// APLL to twice the I2S port's own MCLK (its divider cannot go below 2):
// so the port asks for 128 x fs to put 256 x fs on the pin. With 256 the
// DSP ran on 24.576 MHz, twice its rating, and its converters made
// sporadic single-sample errors (found with the serial output capture;
// on the board's crystal X1 they vanished).
#define DSP_MCLK_I2S_MULTIPLE         128
// The rate the DSP core runs its program at. The SigmaStudio projects carry
// Elektor Demo1's core register (program length 01: 512 instructions), so
// the core runs twice per sample. Every DSP frequency is computed for this
// rate, not for DSP_SAMPLE_RATE.
#define DSP_CORE_RATE                 96000
// The DSP's serial output (MP6, MP10, MP11; Capture.cpp): the images make
// its port a master at the sample rate, 64 BCLK a frame (diag clocks)
#define DSP_SERIAL_OUT_RATE           48000

// --- timing -------------------------------------------------------------------
#define DSP_BOOT_TIMEOUT_MS           1500    // self-boot from EEPROM
#define POT_SAMPLE_PERIOD_MS          20

#endif

// ============================================================================
// ==================================  E O F  =================================
// ============================================================================
