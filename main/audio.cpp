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
constexpr unsigned sample_rate = 16000;
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
uint64_t requested_seconds = 0;
std::atomic<unsigned> revision{0};
TaskHandle_t player = nullptr;

esp_codec_dev_handle_t open_device() {
  i2c_master_bus_handle_t bus = nullptr;
  if (i2c_master_get_bus_handle(I2C_NUM_0, &bus) != ESP_OK)
    return nullptr;

  i2s_chan_handle_t tx = nullptr;
  i2s_chan_config_t channel = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
  channel.dma_desc_num = dma_buffers;
  channel.dma_frame_num = dma_frames;
  channel.auto_clear_after_cb = true;
  if (i2s_new_channel(&channel, &tx, nullptr) != ESP_OK)
    return nullptr;
  i2s_std_config_t config{};
  config.clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(sample_rate);
  config.slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
      I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO);
  config.gpio_cfg.mclk = GPIO_NUM_14;
  config.gpio_cfg.bclk = GPIO_NUM_15;
  config.gpio_cfg.ws = GPIO_NUM_38;
  config.gpio_cfg.dout = GPIO_NUM_45;
  config.gpio_cfg.din = GPIO_NUM_NC;
  if (i2s_channel_init_std_mode(tx, &config) != ESP_OK) {
    i2s_del_channel(tx);
    return nullptr;
  }

  audio_codec_i2s_cfg_t data_cfg{};
  data_cfg.port = I2S_NUM_0;
  data_cfg.tx_handle = tx;
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
    codec_cfg.codec_mode = ESP_CODEC_DEV_WORK_MODE_DAC;
    codec_cfg.pa_pin = GPIO_NUM_46;
    codec_cfg.use_mclk = true;
    codec_cfg.hw_gain.pa_voltage = 5.0;
    codec_cfg.hw_gain.codec_dac_voltage = 3.3;
    codec = es8311_codec_new(&codec_cfg);
    if (codec) {
      esp_codec_dev_cfg_t device_cfg{};
      device_cfg.dev_type = ESP_CODEC_DEV_TYPE_OUT;
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
    i2s_del_channel(tx);
  }
  return device;
}

void play(esp_codec_dev_handle_t device, const std::vector<int16_t> &pcm,
          unsigned request) {
  esp_codec_dev_sample_info_t format{};
  format.bits_per_sample = 16;
  format.channel = 2;
  format.sample_rate = sample_rate;
  if (esp_codec_dev_open(device, &format) != ESP_CODEC_DEV_OK) {
    ESP_LOGW(tag, "Cannot open audio output");
    return;
  }
  esp_codec_dev_set_out_vol(device, voice_volume);
  int16_t stereo[block_frames * 2];
  // Send enough trailing silence to let the last PCM frames leave DMA.
  const size_t frames = pcm.size() + dma_frames * dma_buffers;
  for (size_t offset = 0; offset < frames && revision.load() == request;) {
    const size_t count = std::min(block_frames, frames - offset);
    for (size_t i = 0; i < count; ++i) {
      const size_t index = offset + i;
      const auto sample = index < pcm.size() ? pcm[index] : 0;
      stereo[2 * i] = stereo[2 * i + 1] = sample;
    }
    if (esp_codec_dev_write(device, stereo, count * 2 * sizeof(int16_t)) !=
        ESP_CODEC_DEV_OK) {
      ESP_LOGW(tag, "Audio output failed");
      break;
    }
    offset += count;
  }
  esp_codec_dev_close(device);
}

void player_task(void *) {
  esp_codec_dev_handle_t device = nullptr;
  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    uint64_t seconds;
    unsigned request;
    {
      std::lock_guard lock(mutex);
      ulTaskNotifyTake(pdTRUE, 0);
      seconds = requested_seconds;
      request = revision.load();
    }
    if (!seconds)
      continue;
    if (!device)
      device = open_device();
    if (device)
      play(device, voice::render(seconds), request);
    else
      ESP_LOGW(tag, "Speaker initialization failed");
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
