#include "Mclk.h"
#include "Config.h"
#include <driver/i2s_std.h>

static i2s_chan_handle_t txChannel = NULL;

bool mclkStart(uint32_t sampleRate) {
  if (txChannel != NULL) mclkStop();

  i2s_chan_config_t channelConfig = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
  channelConfig.auto_clear = true;          // clocks run without writing data
  channelConfig.dma_desc_num = 2;
  channelConfig.dma_frame_num = 64;
  if (i2s_new_channel(&channelConfig, &txChannel, NULL) != ESP_OK) {
    txChannel = NULL;
    return false;
  }

  i2s_std_config_t config = {
    .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(sampleRate),
    .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_STEREO),
    .gpio_cfg = {
      .mclk = (gpio_num_t)PIN_MCLK,
      .bclk = I2S_GPIO_UNUSED,
      .ws   = I2S_GPIO_UNUSED,
      .dout = I2S_GPIO_UNUSED,
      .din  = I2S_GPIO_UNUSED,
      .invert_flags = { .mclk_inv = false, .bclk_inv = false, .ws_inv = false },
    },
  };
  config.clk_cfg.clk_src = I2S_CLK_SRC_APLL;                 // exact 12.288 MHz
  config.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_256;      // ADAU1701 PLL 256 x fs

  if (i2s_channel_init_std_mode(txChannel, &config) != ESP_OK ||
      i2s_channel_enable(txChannel) != ESP_OK) {
    i2s_del_channel(txChannel);
    txChannel = NULL;
    return false;
  }
  return true;
}

void mclkStop() {
  if (txChannel == NULL) return;
  i2s_channel_disable(txChannel);
  i2s_del_channel(txChannel);
  txChannel = NULL;
}

bool mclkRunning() {
  return txChannel != NULL;
}
