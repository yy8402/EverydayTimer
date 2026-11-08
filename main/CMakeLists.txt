idf_component_register(SRCS "main.cpp" "audio.cpp" "wifi_audio.cpp" "voice.cpp" "activity_drive.cpp" "core.cpp" "hardware.cpp" "ui.cpp" "cjk_font.cpp" "duration.cpp" "clock_view.cpp" "refresh_scheduler.cpp" "display_backend.cpp"
 EMBED_FILES "cjk.z" "voice.bin"
 INCLUDE_DIRS "." REQUIRES esp_driver_gpio esp_driver_spi esp_driver_i2c esp_driver_i2s fatfs wear_levelling mbedtls esp_timer json esp_partition esp_psram nvs_flash littlefs esp_wifi esp_event esp_netif lwip esp_rom)

# Keep the vendor eject implementation while adding a distinguishable hook
# around it. TinyUSB's weak callback shares a translation unit with its caller.
idf_component_get_property(tinyusb_driver espressif__esp_tinyusb COMPONENT_LIB)
target_compile_definitions(${tinyusb_driver} PRIVATE
  tud_msc_start_stop_cb=note4c_vendor_msc_start_stop)
