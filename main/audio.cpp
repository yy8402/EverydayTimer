#include "audio.h"
#include "wav.h"

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/i2s_std.h"
#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <dirent.h>
#include <memory>
#include <mutex>
#include <strings.h>

namespace {
constexpr char tag[] = "timer_audio";
constexpr size_t dma_frames = 128;
constexpr size_t dma_buffers = 4;
constexpr size_t block_frames = 256;
constexpr int64_t repeat_interval_us = 3000000;

struct Sound {
  std::string filename;
  std::vector<int16_t> pcm;
};
using Clip = std::shared_ptr<const Sound>;
using Catalog = std::array<Clip, 100>;
Catalog sounds;
unsigned selected = 0;
int64_t repeat_at = 0;
std::mutex mutex;
Clip requested;
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
  config.clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(wav::sample_rate);
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

void play(esp_codec_dev_handle_t device, const Sound &sound, unsigned request) {
  esp_codec_dev_sample_info_t format{};
  format.bits_per_sample = 16;
  format.channel = 2;
  format.sample_rate = wav::sample_rate;
  if (esp_codec_dev_open(device, &format) != ESP_CODEC_DEV_OK) {
    ESP_LOGW(tag, "Cannot open audio output");
    return;
  }
  esp_codec_dev_set_out_vol(device, 55);
  int16_t stereo[block_frames * 2];
  // Send enough trailing silence to let the last PCM frames leave DMA.
  const size_t frames = sound.pcm.size() + dma_frames * dma_buffers;
  for (size_t offset = 0; offset < frames && revision.load() == request;) {
    const size_t count = std::min(block_frames, frames - offset);
    for (size_t i = 0; i < count; ++i) {
      const size_t index = offset + i;
      const auto sample = index < sound.pcm.size() ? sound.pcm[index] : 0;
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
    Clip clip;
    unsigned request;
    {
      std::lock_guard lock(mutex);
      // A newer notification may have arrived before taking the lock.
      ulTaskNotifyTake(pdTRUE, 0);
      clip = std::move(requested);
      request = revision.load();
    }
    if (!clip)
      continue;
    if (!device)
      device = open_device();
    if (device)
      play(device, *clip, request);
    else
      ESP_LOGW(tag, "Speaker initialization failed");
  }
}

void enqueue_selection() {
  if (!player)
    return;
  requested = sounds[selected];
  ++revision;
  xTaskNotifyGive(player);
}

void play_and_schedule_repeat() {
  repeat_at = selected ? esp_timer_get_time() + repeat_interval_us : 0;
  enqueue_selection();
}

unsigned slot_for(const std::string &name) {
  if (name.size() < 6 || name[0] < '0' || name[0] > '9' ||
      name[1] < '0' || name[1] > '9' ||
      (name[2] != '-' && name[2] != '.') ||
      strcasecmp(name.c_str() + name.size() - 4, ".wav") != 0 ||
      !std::all_of(name.begin(), name.end(),
                   [](unsigned char c) { return c >= 32 && c <= 126; }))
    return 0;
  return 10 * (name[0] - '0') + name[1] - '0';
}
} // namespace

void audio::init() {
  if (xTaskCreate(player_task, "audio", 4096, nullptr, 2, &player) != pdPASS)
    ESP_LOGW(tag, "Cannot create speaker task");
}

void audio::load(const char *directory) {
  Catalog loaded;
  if (DIR *dir = opendir(directory)) {
    while (const auto *entry = readdir(dir)) {
      std::string name = entry->d_name;
      const unsigned slot = slot_for(name);
      // Resolve duplicate numbers in filename order.
      if (!slot || (loaded[slot] && loaded[slot]->filename <= name))
        continue;
      const std::string path = std::string(directory) + "/" + name;
      std::unique_ptr<FILE, decltype(&std::fclose)> file(
          std::fopen(path.c_str(), "rb"), std::fclose);
      if (!file)
        continue;
      auto pcm = wav::read_pcm(file.get());
      if (pcm.empty()) {
        ESP_LOGW(tag, "Skipping invalid WAV: %s", name.c_str());
        continue;
      }
      loaded[slot] = std::make_shared<Sound>(Sound{std::move(name), std::move(pcm)});
    }
    closedir(dir);
  }
  std::lock_guard lock(mutex);
  sounds.swap(loaded);
  if (selected && !sounds[selected]) {
    selected = 0;
    if (repeat_at) {
      repeat_at = 0;
      enqueue_selection();
    }
  }
}

std::string audio::selection() {
  std::lock_guard lock(mutex);
  return sounds[selected] ? sounds[selected]->filename : "00 Off";
}

void audio::cycle(int direction) {
  std::lock_guard lock(mutex);
  const int count = sounds.size();
  do {
    selected = (int(selected) + direction + count) % count;
  } while (selected && !sounds[selected]);
  play_and_schedule_repeat();
}

void audio::start_run() {
  std::lock_guard lock(mutex);
  play_and_schedule_repeat();
}

void audio::stop_run() {
  std::lock_guard lock(mutex);
  repeat_at = 0;
  enqueue_selection();
}

void audio::poll() {
  std::lock_guard lock(mutex);
  const int64_t now = esp_timer_get_time();
  if (repeat_at && now >= repeat_at) {
    repeat_at = now + repeat_interval_us;
    enqueue_selection();
  }
}
