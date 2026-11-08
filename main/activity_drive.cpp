#include "activity_drive.h"
#include "esp_partition.h"
#include "ff.h"
#include "mbedtls/sha256.h"
#include "tinyusb_msc.h"
#include <cerrno>
#include <cstdio>
#include <sys/stat.h>
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
bool clean_eject = false;
bool history_published = false;
uint64_t history_revision = 0;
ExportHistory export_history;
ClearHistory clear_history;
constexpr char history_path[] = "/usb/history.csv";
constexpr char history_temp[] = "/usb/history.tmp";

void publish_history() {
  history_published = false;
  // Free the previous snapshot's space before writing the replacement.
  if (std::remove(history_path) != 0 && errno != ENOENT) {
    report_error("Cannot replace history.csv");
    return;
  }
  FILE *file = std::fopen(history_temp, "wb");
  if (file) {
    const bool written = export_history(file, history_revision);
    const bool closed = std::fclose(file) == 0;
    if (written && closed && std::rename(history_temp, history_path) == 0) {
      history_published = true;
      return;
    }
  }
  std::remove(history_temp);
  report_error("Cannot export history.csv; use USB history");
}

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
    publish_history();
  } else if (event->id == TINYUSB_MSC_EVENT_MOUNT_COMPLETE) {
    if (event->mount_point == TINYUSB_MSC_STORAGE_MOUNT_USB) {
      exposed = true;
      return;
    }
    if (exposed) {
      import_file();
      struct stat info{};
      if (clean_eject && history_published &&
          stat(history_path, &info) != 0 && errno == ENOENT &&
          clear_history(history_revision))
        publish_history();
    }
    exposed = false;
    history_published = false;
  } else if (event->id == TINYUSB_MSC_EVENT_MOUNT_FAILED ||
             event->id == TINYUSB_MSC_EVENT_FORMAT_FAILED) {
    exposed = false;
    history_published = false;
    report_error("USB drive could not be mounted");
  }
}
} // namespace

esp_err_t init(ReadActivities read, ApplyActivities apply, ReportError report,
               ExportHistory export_csv, ClearHistory clear) {
  export_history = export_csv;
  clear_history = clear;
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

// The storage mount event also fires on cable detach. Only the synchronous
// SCSI eject callback may authorize clearing internal history.
extern "C" bool note4c_vendor_msc_start_stop(uint8_t lun, uint8_t power,
                                            bool start, bool load_eject);
extern "C" bool tud_msc_start_stop_cb(uint8_t lun, uint8_t power,
                                            bool start, bool load_eject) {
  activity_drive::clean_eject = lun == 0 && load_eject && !start;
  const bool result = note4c_vendor_msc_start_stop(lun, power, start, load_eject);
  activity_drive::clean_eject = false;
  return result;
}
