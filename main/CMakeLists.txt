idf_component_register(SRCS "main.cpp" "audio.cpp" "wav.cpp" "activity_drive.cpp" "core.cpp" "hardware.cpp" "ui.cpp" "duration.cpp" "clock_view.cpp" "refresh_scheduler.cpp" "display_backend.cpp"
 EMBED_FILES "cjk.bin"
 INCLUDE_DIRS "." REQUIRES esp_driver_gpio esp_driver_spi esp_driver_i2c esp_driver_i2s fatfs wear_levelling mbedtls esp_timer json esp_partition esp_psram littlefs)
