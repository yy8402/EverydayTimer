#include "audio.h"
#include "voice.h"

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/i2s_std.h"
#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"

#include <algorithm>
#include <atomic>
#include <mutex>

namespace {
constexpr char tag[] = "timer_audio";
constexpr size_t dma_frames = 128;
constexpr size_t dma_buffers = 4;
constexpr size_t block_frames = 256;
constexpr int voice_volume = 72;
using audio::sample_rate;
constexpr int64_t second_us = 1000000;
struct Interval {
  uint16_t seconds;
  const char *label;
};
constexpr Interval intervals[] = {
    {0, "Voice Off"}, {30, "Voice 30 sec"}, {60, "Voice 1 min"},
    {180, "Voice 3 min"}, {300, "Voice 5 min"}, {600, "Voice 10 min"}};
constexpr int interval_count = sizeof intervals / sizeof *intervals;
constexpr int default_interval = 2;
constexpr char interval_key[] = "interval";
int selected = default_interval;
nvs_handle_t settings = 0;
int64_t run_started = 0;
int64_t announce_at = 0;

std::mutex mutex;
std::mutex codec_mutex;
uint64_t requested_seconds = 0;
std::atomic<unsigned> revision{0};
unsigned handled_revision = 0;  // Protected by codec_mutex.
TaskHandle_t player = nullptr;

esp_codec_dev_handle_t open_device() {
  i2c_master_bus_handle_t bus = nullptr;
  if (i2c_master_get_bus_handle(I2C_NUM_0, &bus) != ESP_OK)
    return nullptr;

  i2s_chan_handle_t tx = nullptr, rx = nullptr;
  i2s_chan_config_t channel = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
  channel.dma_desc_num = dma_buffers;
  channel.dma_frame_num = dma_frames;
  channel.auto_clear_after_cb = true;
  if (i2s_new_channel(&channel, &tx, &rx) != ESP_OK)
    return nullptr;
  i2s_std_config_t config{};
  config.clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(sample_rate);
  config.slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
      I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO);
  config.gpio_cfg.mclk = GPIO_NUM_14;
  config.gpio_cfg.bclk = GPIO_NUM_15;
  config.gpio_cfg.ws = GPIO_NUM_38;
  config.gpio_cfg.dout = GPIO_NUM_45;
  config.gpio_cfg.din = GPIO_NUM_16;
  if (i2s_channel_init_std_mode(tx, &config) != ESP_OK ||
      i2s_channel_init_std_mode(rx, &config) != ESP_OK) {
    i2s_del_channel(rx);
    i2s_del_channel(tx);
    return nullptr;
  }

  audio_codec_i2s_cfg_t data_cfg{};
  data_cfg.port = I2S_NUM_0;
  data_cfg.tx_handle = tx;
  data_cfg.rx_handle = rx;
  audio_codec_i2c_cfg_t control_cfg{};
  control_cfg.port = I2C_NUM_0;
  control_cfg.addr = ES8311_CODEC_DEFAULT_ADDR;
  control_cfg.bus_handle = bus;
  const auto *data = audio_codec_new_i2s_data(&data_cfg);
  const auto *control = audio_codec_new_i2c_ctrl(&control_cfg);
  const auto *gpio = audio_codec_new_gpio();
  const audio_codec_if_t *codec = nullptr;
  esp_codec_dev_handle_t device = nullptr;
  if (data && control && gpio) {
    es8311_codec_cfg_t codec_cfg{};
    codec_cfg.ctrl_if = control;
    codec_cfg.gpio_if = gpio;
    codec_cfg.codec_mode = ESP_CODEC_DEV_WORK_MODE_BOTH;
    codec_cfg.pa_pin = GPIO_NUM_46;
    codec_cfg.use_mclk = true;
    codec_cfg.hw_gain.pa_voltage = 5.0;
    codec_cfg.hw_gain.codec_dac_voltage = 3.3;
    codec = es8311_codec_new(&codec_cfg);
    if (codec) {
      esp_codec_dev_cfg_t device_cfg{};
      device_cfg.dev_type = ESP_CODEC_DEV_TYPE_IN_OUT;
      device_cfg.codec_if = codec;
      device_cfg.data_if = data;
      device = esp_codec_dev_new(&device_cfg);
    }
  }
  if (!device) {
    if (codec)
      audio_codec_delete_codec_if(codec);
    if (gpio)
      audio_codec_delete_gpio_if(gpio);
    if (control)
      audio_codec_delete_ctrl_if(control);
    if (data)
      audio_codec_delete_data_if(data);
    i2s_del_channel(rx);
    i2s_del_channel(tx);
  }
  return device;
}

esp_codec_dev_handle_t acquire_device() {
  // Called with codec_mutex held. Retry initialization after a transient failure.
  static esp_codec_dev_handle_t device = nullptr;
  if (!device)
    device = open_device();
  if (!device)
    return nullptr;
  esp_codec_dev_sample_info_t format{};
  format.bits_per_sample = 16;
  format.channel = 2;
  format.sample_rate = sample_rate;
  if (esp_codec_dev_open(device, &format) != ESP_CODEC_DEV_OK)
    return nullptr;
  return device;
}

bool play(const int16_t *pcm, size_t count, const unsigned *request = nullptr) {
  if (request && revision.load() != *request)
    return true;
  auto device = acquire_device();
  if (!device)
    return false;
  bool ok = esp_codec_dev_set_out_vol(device, voice_volume) == ESP_CODEC_DEV_OK &&
            esp_codec_dev_set_out_mute(device, false) == ESP_CODEC_DEV_OK;
  int16_t stereo[block_frames * 2];
  // Flush the last PCM frames from DMA before closing the codec.
  const size_t frames = count + dma_frames * dma_buffers;
  for (size_t offset = 0; ok && offset < frames &&
                          (!request || revision.load() == *request);) {
    const size_t block = std::min(block_frames, frames - offset);
    for (size_t i = 0; i < block; ++i) {
      const size_t index = offset + i;
      const auto sample = index < count ? pcm[index] : 0;
      stereo[2 * i] = stereo[2 * i + 1] = sample;
    }
    ok = esp_codec_dev_write(device, stereo, block * 2 * sizeof(int16_t)) ==
         ESP_CODEC_DEV_OK;
    offset += block;
  }
  return esp_codec_dev_close(device) == ESP_CODEC_DEV_OK && ok;
}

bool take_announcement(uint64_t &seconds, unsigned &request) {
  std::lock_guard lock(mutex);
  request = revision.load();
  if (request == handled_revision)
    return false;
  handled_revision = request;
  seconds = requested_seconds;
  return true;
}

void player_task(void *) {
  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    std::lock_guard lock(codec_mutex);
    uint64_t seconds;
    unsigned request;
    if (take_announcement(seconds, request) && seconds) {
      const auto pcm = voice::render(seconds);
      if (!play(pcm.data(), pcm.size(), &request))
        ESP_LOGW(tag, "Timer announcement failed");
    }
  }
}

void enqueue(uint64_t seconds) {
  if (!player)
    return;
  std::lock_guard lock(mutex);
  requested_seconds = seconds;
  ++revision;
  xTaskNotifyGive(player);
}

void schedule(int64_t now) {
  const int64_t interval = intervals[selected].seconds * second_us;
  announce_at = interval ? run_started + ((now - run_started) / interval + 1) * interval : 0;
}
} // namespace

bool audio::stream(Exchange exchange, void *context) {
  std::lock_guard lock(codec_mutex);
  auto device = acquire_device();
  if (!device)
    return false;
  bool ok = esp_codec_dev_set_in_gain(device, 30) == ESP_CODEC_DEV_OK &&
            esp_codec_dev_set_out_vol(device, voice_volume) == ESP_CODEC_DEV_OK &&
            esp_codec_dev_set_out_mute(device, false) == ESP_CODEC_DEV_OK;
  int16_t stereo[block_frames * 2];
  int16_t pcm[block_frames];
  unsigned quiet_blocks = 6;
  std::vector<int16_t> announcement;
  size_t announcement_offset = 0;
  while (ok) {
    uint64_t seconds;
    unsigned request;
    if (take_announcement(seconds, request)) {
      announcement = seconds ? voice::render(seconds) : std::vector<int16_t>{};
      announcement_offset = 0;
    }
    ok = esp_codec_dev_read(device, stereo, sizeof stereo) == ESP_CODEC_DEV_OK;
    if (!ok)
      break;
    for (size_t i = 0; i < block_frames; ++i)
      pcm[i] = quiet_blocks ? 0 : stereo[2 * i];
    if (quiet_blocks)
      --quiet_blocks;
    if (!exchange(context, pcm, block_frames))
      break;
    for (size_t i = 0; i < block_frames; ++i) {
      int sample = pcm[i];
      if (announcement_offset < announcement.size())
        sample += announcement[announcement_offset++];
      if (sample < -96 || sample > 96)
        quiet_blocks = 12;  // Suppress speaker leakage and the decay after playback.
      stereo[2 * i] = stereo[2 * i + 1] = std::clamp(sample, -32768, 32767);
    }
    ok = esp_codec_dev_write(device, stereo, sizeof stereo) == ESP_CODEC_DEV_OK;
  }
  return esp_codec_dev_close(device) == ESP_CODEC_DEV_OK && ok;
}

void audio::init() {
  if (nvs_flash_init() == ESP_OK &&
      nvs_open("timer_audio", NVS_READWRITE, &settings) == ESP_OK) {
    uint16_t seconds;
    if (nvs_get_u16(settings, interval_key, &seconds) == ESP_OK) {
      for (int i = 0; i < interval_count; ++i) {
        if (intervals[i].seconds == seconds) {
          selected = i;
          break;
        }
      }
    }
  } else {
    ESP_LOGE(tag, "Cannot open announcement settings");
  }
  if (xTaskCreate(player_task, "audio", 4096, nullptr, 2, &player) != pdPASS)
    ESP_LOGE(tag, "Cannot create speaker task");
}

const char *audio::selection() {
  return intervals[selected].label;
}

bool audio::adjust_interval(int direction) {
  const int next = std::clamp(selected + direction, 0, interval_count - 1);
  if (next == selected)
    return true;
  if (!settings ||
      nvs_set_u16(settings, interval_key, intervals[next].seconds) != ESP_OK ||
      nvs_commit(settings) != ESP_OK)
    return false;
  selected = next;
  schedule(esp_timer_get_time());
  enqueue(0);
  return true;
}

void audio::start_run(int64_t started) {
  run_started = started;
  schedule(esp_timer_get_time());
}

void audio::stop_run() {
  announce_at = 0;
  enqueue(0);
}

void audio::poll() {
  if (!announce_at)
    return;
  const int64_t now = esp_timer_get_time();
  if (now >= announce_at) {
    const int64_t interval = intervals[selected].seconds;
    const auto boundary = (now - run_started) / (interval * second_us);
    enqueue(boundary * interval);
    schedule(now);
  }
}
