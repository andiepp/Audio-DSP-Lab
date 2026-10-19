#include "Capture.h"
#include "Config.h"
#include <Arduino.h>
#include <driver/i2s_std.h>
#include <driver/pulse_cnt.h>
#include <math.h>

#define CAPTURE_CLOCK_TIMEOUT_MS      100     // no full chunk this long: no clock
#define CAPTURE_HYSTERESIS            64      // counts of 2^-23 a zero crossing must clear (-102 dBFS)

static int32_t chunk[2 * CAPTURE_CHUNK_FRAMES];

struct Stats {
  int32_t peak;
  double squares;
  uint32_t crossings;       // upward, past the hysteresis
  int8_t side;              // -1 below, 1 above, 0 not yet known
};

static void take(Stats &s, int32_t x) {
  const int32_t magnitude = x < 0 ? -x : x;
  if (magnitude > s.peak) s.peak = magnitude;
  s.squares += (double)x * x;
  if (x > CAPTURE_HYSTERESIS) {
    if (s.side < 0) s.crossings++;
    s.side = 1;
  } else if (x < -CAPTURE_HYSTERESIS) {
    s.side = -1;
  }
}

static float decibels(double counts) {
  return counts > 0 ? (float)(20.0 * log10(counts / 8388608.0)) : -200.0f;
}

static void finish(const Stats &s, uint32_t frames, float rate, CaptureChannel &out) {
  out.rmsDb = decibels(sqrt(s.squares / frames));
  out.peakDb = decibels(s.peak);
  out.hertz = s.crossings * rate / frames;
}

// rising edges on a pin over a window, with the pulse counter (it counts
// to 32767, so the window keeps below that)
static float countEdges(int pin, uint32_t windowUs) {
  pcnt_unit_config_t unitConfig = {};
  unitConfig.low_limit = -32768;
  unitConfig.high_limit = 32767;
  pcnt_unit_handle_t unit = NULL;
  if (pcnt_new_unit(&unitConfig, &unit) != ESP_OK) return -1;
  pcnt_chan_config_t channelConfig = {};
  channelConfig.edge_gpio_num = pin;
  channelConfig.level_gpio_num = -1;
  pcnt_channel_handle_t channel = NULL;
  float hertz = -1;
  if (pcnt_new_channel(unit, &channelConfig, &channel) == ESP_OK) {
    pcnt_channel_set_edge_action(channel, PCNT_CHANNEL_EDGE_ACTION_INCREASE, PCNT_CHANNEL_EDGE_ACTION_HOLD);
    pcnt_unit_enable(unit);
    pcnt_unit_clear_count(unit);
    const uint32_t startedAt = micros();
    pcnt_unit_start(unit);
    while (micros() - startedAt < windowUs) {}
    pcnt_unit_stop(unit);
    const uint32_t elapsed = micros() - startedAt;
    int count = 0;
    pcnt_unit_get_count(unit, &count);
    hertz = count * 1e6f / elapsed;
    pcnt_unit_disable(unit);
    pcnt_del_channel(channel);
  }
  pcnt_del_unit(unit);
  return hertz;
}

void captureClocks(float &lrclkHertz, float &bclkHertz) {
  lrclkHertz = countEdges(PIN_SOUT_LRCLK, 50000);     // up to 192 kHz: 9600 edges
  bclkHertz = countEdges(PIN_SOUT_BCLK, 2000);        // up to 12.3 MHz: 24576 edges
}

// one chunk, exactly a DMA buffer, so that every read starts on a frame
static bool readChunk(i2s_chan_handle_t rx, uint32_t timeoutMs) {
  size_t got = 0;
  return i2s_channel_read(rx, chunk, sizeof(chunk), &got, timeoutMs) == ESP_OK && got == sizeof(chunk);
}

bool captureMeasure(uint32_t frames, CaptureResult &result) {
  result = CaptureResult();
  i2s_chan_handle_t rx = NULL;
  i2s_chan_config_t channelConfig = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_1, I2S_ROLE_SLAVE);
  channelConfig.dma_desc_num = 4;
  channelConfig.dma_frame_num = CAPTURE_CHUNK_FRAMES;
  if (i2s_new_channel(&channelConfig, NULL, &rx) != ESP_OK) return false;

  i2s_std_config_t config = {
    .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(DSP_SERIAL_OUT_RATE),
    .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_STEREO),
    .gpio_cfg = {
      .mclk = I2S_GPIO_UNUSED,
      .bclk = (gpio_num_t)PIN_SOUT_BCLK,     // inputs: the DSP drives both clocks
      .ws   = (gpio_num_t)PIN_SOUT_LRCLK,
      .dout = I2S_GPIO_UNUSED,
      .din  = (gpio_num_t)PIN_SOUT_DATA,
      .invert_flags = { .mclk_inv = false, .bclk_inv = false, .ws_inv = false },
    },
  };
  // not the APLL: it makes the DSP's MCLK on the first port. A slave's
  // own clock only samples the incoming BCLK and must run at 8 times it
  // (512 x 96 kHz = 49.152 MHz); at 1024 x 48 kHz, 4 times the 96 kHz
  // port's BCLK, it miscounted the frames (89.6 kHz measured)
  config.clk_cfg.clk_src = I2S_CLK_SRC_PLL_160M;
  config.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_512;

  bool clocked = false;
  if (i2s_channel_init_std_mode(rx, &config) == ESP_OK && i2s_channel_enable(rx) == ESP_OK) {
    clocked = readChunk(rx, CAPTURE_CLOCK_TIMEOUT_MS);
    // what the DMA buffered meanwhile goes; the timing starts with a chunk
    // that has just come in
    while (clocked && readChunk(rx, 0)) {}
    clocked = clocked && readChunk(rx, CAPTURE_CLOCK_TIMEOUT_MS);
    const uint32_t startedAt = micros();
    uint32_t lastAt = startedAt;
    const uint32_t chunks = (frames + CAPTURE_CHUNK_FRAMES - 1) / CAPTURE_CHUNK_FRAMES;
    uint32_t done = 0;
    Stats left = {}, right = {};
    while (clocked && done < chunks) {
      if (!readChunk(rx, CAPTURE_CLOCK_TIMEOUT_MS)) {
        clocked = false;
        break;
      }
      lastAt = micros();
      if (done == 0) result.firstWord = (uint32_t)chunk[0];
      // 24 bits, MSB first, at the top of each 32-bit slot
      for (int i = 0; i < CAPTURE_CHUNK_FRAMES; i++) {
        take(left, chunk[2 * i] >> 8);
        take(right, chunk[2 * i + 1] >> 8);
      }
      done++;
    }
    if (clocked && lastAt != startedAt) {
      result.frames = done * CAPTURE_CHUNK_FRAMES;
      result.rate = result.frames * 1e6f / (lastAt - startedAt);
      finish(left, result.frames, result.rate, result.left);
      finish(right, result.frames, result.rate, result.right);
    }
    i2s_channel_disable(rx);
  }
  i2s_del_channel(rx);
  return clocked && result.frames > 0;
}
