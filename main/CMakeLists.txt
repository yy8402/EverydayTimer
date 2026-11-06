idf_component_register(SRCS "main.cpp" "core.cpp" "hardware.cpp" "ui.cpp" "duration.cpp" "clock_view.cpp" "refresh_scheduler.cpp" "display_backend.cpp"
 EMBED_FILES "cjk.bin"
 INCLUDE_DIRS "." REQUIRES esp_driver_gpio esp_driver_spi esp_driver_i2c esp_driver_usb_serial_jtag esp_timer json esp_partition esp_psram littlefs)
