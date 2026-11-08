#include "wifi_audio.h"
#include "audio.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_random.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include "lwip/tcp.h"
#include "mbedtls/md.h"
#include "nvs.h"

#include <array>
#include <atomic>
#include <cstring>
#include <memory>

namespace {
constexpr uint16_t port = 42574;
constexpr size_t key_length = 64;
using Nonce = std::array<uint8_t, 16>;
using Proof = std::array<uint8_t, 32>;

struct Pairing {
  wifi_config_t wifi{};
  char key[key_length];
};

std::atomic<bool> busy{false};
std::atomic<bool> streaming{false};
std::atomic<bool> voice_pressed{false};
std::atomic<const char *> state{"unpaired"};

esp_netif_t *init_network() {
  if (esp_netif_init() != ESP_OK || esp_event_loop_create_default() != ESP_OK)
    return nullptr;
  auto *station = esp_netif_create_default_wifi_sta();
  if (!station) {
    esp_event_loop_delete_default();
    return nullptr;
  }
  wifi_init_config_t config = WIFI_INIT_CONFIG_DEFAULT();
  config.nvs_enable = 0;
  if (esp_wifi_init(&config) == ESP_OK)
    return station;
  esp_netif_destroy_default_wifi(station);
  esp_event_loop_delete_default();
  return nullptr;
}

bool transfer(int fd, void *buffer, size_t length, bool sending) {
  auto *data = static_cast<uint8_t *>(buffer);
  while (length) {
    const auto bytes = sending ? send(fd, data, length, 0)
                               : recv(fd, data, length, 0);
    if (bytes <= 0)
      return false;
    data += bytes;
    length -= bytes;
  }
  return true;
}

bool proof(const Pairing &pairing, const Nonce &nonce, uint8_t role, Proof &out) {
  std::array<uint8_t, std::tuple_size_v<Nonce> + 1> challenge{};
  challenge[0] = role;
  memcpy(challenge.data() + 1, nonce.data(), nonce.size());
  return mbedtls_md_hmac(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256),
                        reinterpret_cast<const uint8_t *>(pairing.key), sizeof pairing.key,
                        challenge.data(), challenge.size(), out.data()) == 0;
}

bool discover(const Pairing &pairing, Nonce &nonce, sockaddr_in &peer) {
  const int fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (fd < 0)
    return false;
  int enabled = 1;
  timeval timeout{2, 0};
  sockaddr_in broadcast{};
  broadcast.sin_family = AF_INET;
  broadcast.sin_port = htons(port);
  broadcast.sin_addr.s_addr = INADDR_BROADCAST;
  esp_fill_random(nonce.data(), nonce.size());
  Proof expected{}, received{};
  socklen_t length = sizeof peer;
  const bool ok = proof(pairing, nonce, 'M', expected) &&
      setsockopt(fd, SOL_SOCKET, SO_BROADCAST, &enabled, sizeof enabled) == 0 &&
      setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof timeout) == 0 &&
      sendto(fd, nonce.data(), nonce.size(), 0,
             reinterpret_cast<const sockaddr *>(&broadcast), sizeof broadcast) ==
          static_cast<ssize_t>(nonce.size()) &&
      recvfrom(fd, received.data(), received.size(), 0,
               reinterpret_cast<sockaddr *>(&peer), &length) ==
          static_cast<ssize_t>(received.size()) &&
      received == expected;
  close(fd);
  peer.sin_port = htons(port);
  return ok;
}

struct Stream {
  int fd;
  const Pairing &pairing;
  const Nonce &nonce;
  uint64_t frame = 0;
};

bool stream_block(void *context, int16_t *pcm, size_t frames) {
  auto &stream = *static_cast<Stream *>(context);
  const size_t bytes = frames * sizeof(*pcm);
  std::array<uint8_t, 513> packet;
  if (bytes != packet.size() - 1)
    return false;
  packet[0] = voice_pressed.exchange(false) ? 1 : 0;
  memcpy(packet.data() + 1, pcm, bytes);
  if (!transfer(stream.fd, packet.data(), packet.size(), true))
    return false;
  if (packet[0]) {
    Nonce event_nonce = stream.nonce;
    for (unsigned i = 0; i < sizeof stream.frame; ++i)
      event_nonce[i] ^= static_cast<uint8_t>(stream.frame >> (i * 8));
    Proof event{};
    if (!proof(stream.pairing, event_nonce, 'V', event) ||
        !transfer(stream.fd, event.data(), event.size(), true))
      return false;
  }
  ++stream.frame;
  return transfer(stream.fd, pcm, bytes, false);
}

const char *connect_bridge(const Pairing &pairing) {
  Nonce nonce{};
  sockaddr_in peer{};
  state = "discovering Mac";
  if (!discover(pairing, nonce, peer))
    return "waiting for Mac bridge";
  int fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (fd < 0)
    return "socket allocation failed";
  timeval timeout{3, 0};
  int enabled = 1;
  char command = 0;
  Proof response{};
  const bool connected =
      setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof timeout) == 0 &&
      setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof timeout) == 0 &&
      setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &enabled, sizeof enabled) == 0 &&
      connect(fd, reinterpret_cast<const sockaddr *>(&peer), sizeof peer) == 0 &&
      proof(pairing, nonce, 'B', response) &&
      transfer(fd, nonce.data(), nonce.size(), true) &&
      transfer(fd, response.data(), response.size(), true) &&
      transfer(fd, &command, 1, false) && command == 'V';
  const char *outcome = "Mac connection failed";
  if (connected) {
    voice_pressed = false;
    streaming = true;
    state = "streaming";
    Stream stream{fd, pairing, nonce};
    outcome = audio::stream(stream_block, &stream) ? "stream ended" : "stream codec failed";
    streaming = false;
  }
  close(fd);
  return outcome;
}

void worker(void *arg) {
  std::unique_ptr<Pairing> pairing(static_cast<Pairing *>(arg));
  static esp_netif_t *station = init_network();
  if (!station || esp_wifi_set_storage(WIFI_STORAGE_RAM) != ESP_OK ||
      esp_wifi_set_mode(WIFI_MODE_STA) != ESP_OK ||
      esp_wifi_set_config(WIFI_IF_STA, &pairing->wifi) != ESP_OK ||
      esp_wifi_start() != ESP_OK) {
    state = "Wi-Fi setup failed";
    pairing.reset();
    busy = false;
    vTaskDelete(nullptr);
    return;
  }
  esp_wifi_set_ps(WIFI_PS_NONE);
  for (;;) {
    state = "joining Wi-Fi";
    esp_wifi_connect();
    for (;;) {
      esp_netif_ip_info_t ip{};
      wifi_ap_record_t ap{};
      if (esp_netif_get_ip_info(station, &ip) == ESP_OK && ip.ip.addr &&
          esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
        state = connect_bridge(*pairing);
        vTaskDelay(pdMS_TO_TICKS(750));
      } else {
        break;
      }
    }
    vTaskDelay(pdMS_TO_TICKS(2500));
  }
}

bool launch(const Pairing &pairing) {
  auto *pending = new (std::nothrow) Pairing(pairing);
  if (pending && xTaskCreate(worker, "wifi_audio", 6144, pending, 2, nullptr) == pdPASS)
    return true;
  delete pending;
  state = "Wi-Fi task allocation failed";
  busy = false;
  return false;
}

const char *field(const cJSON *request, const char *name) {
  return cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(request, name));
}
esp_err_t save_pairing(const Pairing &pairing) {
  nvs_handle_t nvs;
  auto result = nvs_open("wifi_audio", NVS_READWRITE, &nvs);
  if (result == ESP_OK) {
    result = nvs_set_blob(nvs, "pairing", &pairing, sizeof pairing);
    if (result == ESP_OK)
      result = nvs_commit(nvs);
    nvs_close(nvs);
  }
  return result;
}

bool load_pairing(Pairing &pairing) {
  nvs_handle_t nvs;
  if (nvs_open("wifi_audio", NVS_READONLY, &nvs) != ESP_OK)
    return false;
  size_t size = sizeof pairing;
  const auto result = nvs_get_blob(nvs, "pairing", &pairing, &size);
  nvs_close(nvs);
  return result == ESP_OK && size == sizeof pairing;
}

bool set_credentials(Pairing &pairing, const cJSON *request) {
  const char *ssid = field(request, "ssid");
  const char *password = field(request, "password");
  if (!ssid || !*ssid || strlen(ssid) > sizeof pairing.wifi.sta.ssid ||
      !password || strlen(password) > sizeof pairing.wifi.sta.password)
    return false;
  memset(pairing.wifi.sta.ssid, 0, sizeof pairing.wifi.sta.ssid);
  memset(pairing.wifi.sta.password, 0, sizeof pairing.wifi.sta.password);
  memcpy(pairing.wifi.sta.ssid, ssid, strlen(ssid));
  memcpy(pairing.wifi.sta.password, password, strlen(password));
  return true;
}
} // namespace

void wifi_audio::init() {
  Pairing pairing{};
  if (load_pairing(pairing) && !busy.exchange(true))
    launch(pairing);
}

void wifi_audio::voice_button() {
  if (streaming)
    voice_pressed = true;
}

const char *wifi_audio::status() {
  return state.load();
}

const char *wifi_audio::pair(const cJSON *request) {
  const char *key = field(request, "key");
  Pairing pairing{};
  if (!key || strlen(key) != key_length || !set_credentials(pairing, request))
    return "Invalid Wi-Fi audio pairing";
  if (busy.exchange(true))
    return "Wi-Fi audio already paired";

  memcpy(pairing.key, key, sizeof pairing.key);

  const auto result = save_pairing(pairing);
  if (result != ESP_OK) {
    busy = false;
    return "Could not save Wi-Fi pairing";
  }
  return launch(pairing) ? nullptr : status();
}

const char *wifi_audio::configure(const cJSON *request) {
  Pairing pairing{};
  if (!load_pairing(pairing))
    return "Pair the device before changing Wi-Fi settings";
  if (!set_credentials(pairing, request))
    return "Invalid Wi-Fi credentials";
  return save_pairing(pairing) == ESP_OK ? nullptr : "Could not save Wi-Fi credentials";
}
