#include "AudioOut.h"
#include "Config.h"
#include "Board.h"
#include <Arduino.h>
#include <driver/i2s_std.h>

// 4 x 64 frames queued: about 5 ms from a key to the DSP (the radio's
// audio-tools default queue holds close to 100 ms, too long to play)
#define AUDIO_OUT_DMA_BUFFERS         4
#define AUDIO_OUT_DMA_FRAMES          64

static i2s_chan_handle_t txChannel = NULL;

bool audioOutStart() {
  i2s_chan_config_t channelConfig = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
  channelConfig.auto_clear = true;          // silence, not the last block, if the task lags
  channelConfig.dma_desc_num = AUDIO_OUT_DMA_BUFFERS;
  channelConfig.dma_frame_num = AUDIO_OUT_DMA_FRAMES;
  if (i2s_new_channel(&channelConfig, &txChannel, NULL) != ESP_OK) {
    txChannel = NULL;
    return false;
  }

  i2s_std_config_t config = {
    .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(AUDIO_OUT_RATE),
    .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
    .gpio_cfg = {
      .mclk = (gpio_num_t)PIN_MCLK,
      .bclk = (gpio_num_t)PIN_I2S_BCLK,
      .ws   = (gpio_num_t)PIN_I2S_LRCLK,
      .dout = (gpio_num_t)boardI2sDataPin(),
      .din  = I2S_GPIO_UNUSED,
      .invert_flags = { .mclk_inv = false, .bclk_inv = false, .ws_inv = false },
    },
  };
  config.clk_cfg.clk_src = I2S_CLK_SRC_APLL;                 // exact 12.288 MHz
  // 256 x fs on the pin (ADAU1701 PLL mode 256 x fs): see Config.h
  config.clk_cfg.mclk_multiple = (i2s_mclk_multiple_t)DSP_MCLK_I2S_MULTIPLE;

  if (i2s_channel_init_std_mode(txChannel, &config) != ESP_OK ||
      i2s_channel_enable(txChannel) != ESP_OK) {
    i2s_del_channel(txChannel);
    txChannel = NULL;
    return false;
  }
  return true;
}

void audioOutStop() {
  if (txChannel == NULL) return;
  i2s_channel_disable(txChannel);
  i2s_del_channel(txChannel);
  txChannel = NULL;
}

void audioOutWrite(const int16_t *frames, size_t bytes) {
  if (txChannel == NULL) return;
  size_t written;
  i2s_channel_write(txChannel, frames, bytes, &written, pdMS_TO_TICKS(100));
}
