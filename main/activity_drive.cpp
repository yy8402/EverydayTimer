#include "activity_drive.h"
#include "esp_partition.h"
#include "ff.h"
#include "mbedtls/sha256.h"
#include "tinyusb_msc.h"
#include <cstdio>
#include <utility>

namespace activity_drive {
namespace {
constexpr char path[] = "/usb/activities.txt";
constexpr size_t max_file_size = 4096;
ReadActivities read_activities;
ApplyActivities apply_activities;
ReportError report_error;
std::string original;
bool exposed = false;

void publish() {
  original = read_activities();
  FILE *file = std::fopen(path, "wb");
  if (!file) {
    report_error("Cannot write activities.txt");
    return;
  }
  const bool written =
      std::fwrite(original.data(), 1, original.size(), file) == original.size();
  if (std::fclose(file) != 0 || !written)
    report_error("Cannot write activities.txt");
}

void import_file() {
  FILE *file = std::fopen(path, "rb");
  if (!file) {
    report_error("Missing activities.txt");
    return;
  }
  std::string edited(max_file_size + 1, '\0');
  const size_t bytes = std::fread(edited.data(), 1, edited.size(), file);
  const bool read_ok = !std::ferror(file);
  std::fclose(file);
  if (!read_ok || bytes > max_file_size) {
    report_error("Cannot read activities.txt");
    return;
  }
  edited.resize(bytes);
  if (edited != original)
    apply_activities(original, std::move(edited));
}

void storage_event(tinyusb_msc_storage_handle_t, tinyusb_msc_event_t *event,
                   void *) {
  if (event->id == TINYUSB_MSC_EVENT_MOUNT_START &&
      event->mount_point == TINYUSB_MSC_STORAGE_MOUNT_APP) {
    publish();
  } else if (event->id == TINYUSB_MSC_EVENT_MOUNT_COMPLETE) {
    if (event->mount_point == TINYUSB_MSC_STORAGE_MOUNT_USB) {
      exposed = true;
      return;
    }
    if (exposed)
      import_file();
    exposed = false;
  } else if (event->id == TINYUSB_MSC_EVENT_MOUNT_FAILED ||
             event->id == TINYUSB_MSC_EVENT_FORMAT_FAILED) {
    exposed = false;
    report_error("USB drive could not be mounted");
  }
}
} // namespace

esp_err_t init(ReadActivities read, ApplyActivities apply, ReportError report) {
  read_activities = read;
  apply_activities = apply;
  report_error = report;
  const auto *partition = esp_partition_find_first(
      ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_FAT, "activities");
  if (!partition)
    return ESP_ERR_NOT_FOUND;
  tinyusb_msc_storage_config_t storage{};
  auto result = wl_mount(partition, &storage.medium.wl_handle);
  if (result != ESP_OK)
    return result;
  tinyusb_msc_driver_config_t driver{};
  driver.callback = storage_event;
  result = tinyusb_msc_install_driver(&driver);
  if (result != ESP_OK) {
    wl_unmount(storage.medium.wl_handle);
    return result;
  }
  storage.fat_fs.base_path = const_cast<char *>("/usb");
  storage.fat_fs.config.max_files = 1;
  storage.fat_fs.format_flags = FM_FAT | FM_SFD;
  storage.mount_point = TINYUSB_MSC_STORAGE_MOUNT_APP;
  result = tinyusb_msc_new_storage_spiflash(&storage, nullptr);
  if (result != ESP_OK) {
    tinyusb_msc_uninstall_driver();
    wl_unmount(storage.medium.wl_handle);
    return result;
  }
  if (f_setlabel("0:NOTE4C") != FR_OK)
    report_error("USB drive label could not be set");
  return ESP_OK;
}

std::string id_for_name(const std::string &name) {
  unsigned char digest[32];
  mbedtls_sha256(reinterpret_cast<const unsigned char *>(name.data()),
                 name.size(), digest, 0);
  constexpr char hex[] = "0123456789abcdef";
  std::string id = "txt-";
  for (size_t i = 0; i < 14; ++i) {
    id += hex[digest[i] >> 4];
    id += hex[digest[i] & 0xf];
  }
  return id;
}
} // namespace activity_drive
