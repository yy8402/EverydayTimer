#include <algorithm>
// NOTE4C native panel sequence follows the MIT-licensed manufacturer's
// reference firmware: LazyYoun/youn-ink-fourcolor-firmware (2bp),
// Copyright (c) 2026 macheng2017. See THIRD_PARTY_NOTICES.md.
// RTC register behavior: NXP PCF8563 data sheet, sections 8.3-8.5.
#include "hardware.h"
#include "fast_bw_reference.h"

#include <cstring>
#include <ctime>
#include <initializer_list>
#include "driver/i2c_master.h"
#include "driver/spi_master.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

namespace {
constexpr char TAG[] = "note4c_hw";
constexpr gpio_num_t POWER_HOLD = GPIO_NUM_17;
constexpr gpio_num_t EPD_POWER = GPIO_NUM_6;
constexpr gpio_num_t EPD_BUSY = GPIO_NUM_8;
constexpr gpio_num_t EPD_RST = GPIO_NUM_9;
constexpr gpio_num_t EPD_DC = GPIO_NUM_10;
constexpr gpio_num_t EPD_CS = GPIO_NUM_11;
constexpr gpio_num_t EPD_SCK = GPIO_NUM_12;
constexpr gpio_num_t EPD_MOSI = GPIO_NUM_13;
constexpr gpio_num_t LED = GPIO_NUM_3;
constexpr int64_t MIN_EPOCH = 946684800LL;
constexpr int64_t MAX_EPOCH = 4102444799LL;
constexpr int I2C_TIMEOUT_MS = 100;
i2c_master_bus_handle_t rtc_bus = nullptr;
i2c_master_dev_handle_t rtc_device = nullptr;
SemaphoreHandle_t epd_mutex = nullptr;
SemaphoreHandle_t rtc_mutex = nullptr;
bool initialized = false;

void delay_ms(unsigned ms) {
    vTaskDelay(pdMS_TO_TICKS(ms) > 0 ? pdMS_TO_TICKS(ms) : 1);
}

bool output(gpio_num_t pin, int level) {
    gpio_hold_dis(pin);
    // Set the output latch before connecting the output to avoid a low pulse.
    if (gpio_set_level(pin, level) != ESP_OK) return false;
    return gpio_set_direction(pin, GPIO_MODE_OUTPUT) == ESP_OK;
}

bool wait_ready(unsigned timeout_ms) {
    const int64_t deadline = esp_timer_get_time() + int64_t(timeout_ms) * 1000;
    while (gpio_get_level(EPD_BUSY) == 0) {
        if (esp_timer_get_time() >= deadline) {
            ESP_LOGE(TAG, "Display BUSY timed out");
            return false;
        }
        delay_ms(20);
    }
    return true;
}

bool wait_refresh_complete() {
    // Do not mistake the small command-to-BUSY delay for completion.
    const int64_t deadline = esp_timer_get_time() + 1000000;
    while (gpio_get_level(EPD_BUSY) != 0) {
        if (esp_timer_get_time() >= deadline) {
            ESP_LOGE(TAG, "Display refresh did not assert BUSY");
            return false;
        }
        delay_ms(1);
    }
    return wait_ready(120000);
}

bool tx(spi_device_handle_t spi, bool data, const uint8_t* p, size_t n) {
    if (gpio_set_level(EPD_DC, data ? 1 : 0) != ESP_OK ||
        gpio_set_level(EPD_CS, 0) != ESP_OK) return false;
    spi_transaction_t t = {};
    t.length = n * 8;
    if (n <= sizeof(t.tx_data)) {
        t.flags = SPI_TRANS_USE_TXDATA;
        std::memcpy(t.tx_data, p, n);
    } else {
        t.tx_buffer = p;
    }
    const esp_err_t err = spi_device_polling_transmit(spi, &t);
    const esp_err_t cs_err = gpio_set_level(EPD_CS, 1);
    if (err != ESP_OK) ESP_LOGE(TAG, "Display SPI: %s", esp_err_to_name(err));
    return err == ESP_OK && cs_err == ESP_OK;
}

bool command(spi_device_handle_t spi, uint8_t c) { return tx(spi, false, &c, 1); }
bool data_byte(spi_device_handle_t spi, uint8_t c) { return tx(spi, true, &c, 1); }

void power_off_panel() {
    gpio_set_level(EPD_POWER, 0);
    gpio_hold_en(EPD_POWER);
    // SPI must be released before these pins are put into safe GPIO states.
    for (gpio_num_t pin : {EPD_RST, EPD_DC, EPD_CS, EPD_SCK, EPD_MOSI}) {
        gpio_reset_pin(pin);
        output(pin, 0);
    }
    gpio_set_pull_mode(EPD_BUSY, GPIO_FLOATING);
    gpio_set_direction(EPD_BUSY, GPIO_MODE_DISABLE);
}

bool rtc_read(uint8_t reg, uint8_t* bytes, size_t len) {
    return i2c_master_transmit_receive(rtc_device, &reg, 1, bytes, len,
                                       I2C_TIMEOUT_MS) == ESP_OK;
}

bool rtc_write(uint8_t reg, const uint8_t* bytes, size_t len) {
    if (len > 7) return false;
    uint8_t packet[8];
    packet[0] = reg;
    std::memcpy(packet + 1, bytes, len);
    return i2c_master_transmit(rtc_device, packet, len + 1, I2C_TIMEOUT_MS) == ESP_OK;
}

int decode_bcd(uint8_t b) {
    if ((b & 15) > 9 || (b >> 4) > 9) return -1;
    return (b >> 4) * 10 + (b & 15);
}

uint8_t encode_bcd(int value) { return uint8_t((value / 10) * 16 + value % 10); }
bool leap_year(int year) { return year % 4 == 0 && (year % 100 != 0 || year % 400 == 0); }
int month_days(int year, int month) {
    constexpr int days[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    return days[month - 1] + ((month == 2 && leap_year(year)) ? 1 : 0);
}

bool read_epoch_locked(int64_t& epoch) {
    uint8_t ctrl = 0;
    uint8_t raw[7] = {};
    if (!rtc_read(0x00, &ctrl, 1) || (ctrl & 0xA0) != 0 ||
        !rtc_read(0x02, raw, sizeof(raw)) || (raw[0] & 0x80) != 0 ||
        (raw[5] & 0x80) != 0) return false;
    const int sec = decode_bcd(raw[0] & 0x7F);
    const int min = decode_bcd(raw[1] & 0x7F);
    const int hour = decode_bcd(raw[2] & 0x3F);
    const int day = decode_bcd(raw[3] & 0x3F);
    const int weekday = raw[4] & 7;
    const int month = decode_bcd(raw[5] & 0x1F);
    const int year2 = decode_bcd(raw[6]);
    if (sec < 0 || sec > 59 || min < 0 || min > 59 || hour < 0 || hour > 23 ||
        weekday > 6 || month < 1 || month > 12 || year2 < 0 || year2 > 99 ||
        day < 1 || day > month_days(2000 + year2, month)) return false;
    const int year = 2000 + year2;
    int64_t days = 0;
    for (int y = 2000; y < year; ++y) days += leap_year(y) ? 366 : 365;
    for (int m = 1; m < month; ++m) days += month_days(year, m);
    days += day - 1;
    epoch = MIN_EPOCH + days * 86400 + hour * 3600 + min * 60 + sec;
    return true;
}
} // namespace

bool hardware_init() {
    if (initialized) return true;
    // The power latch must precede allocation, I2C, logging, or display work.
    if (!output(POWER_HOLD, 1)) return false;
    // V1.0 schematic R45/R46 pull I2C up to the audio rail (AVDD_3V3).
    // Keep this rail powered for RTC communication, even during audio idle.
    // Turning the rail off while I2C is pulled high would backfeed the codec.
    if (!output(GPIO_NUM_46, 0) || !output(GPIO_NUM_42, 1) ||
        !output(GPIO_NUM_21, 0) || !output(LED, 1) || !output(EPD_POWER, 0)) return false;
    delay_ms(10); // Allow AVDD and the shared I2C pullups to settle.
    power_off_panel();

    gpio_config_t buttons = {};
    buttons.pin_bit_mask = (1ULL << BTN_UP) | (1ULL << BTN_DOWN) | (1ULL << BTN_OK);
    buttons.mode = GPIO_MODE_INPUT;
    buttons.pull_up_en = GPIO_PULLUP_ENABLE;
    buttons.pull_down_en = GPIO_PULLDOWN_DISABLE;
    buttons.intr_type = GPIO_INTR_DISABLE;
    if (gpio_config(&buttons) != ESP_OK) return false;
    if (!epd_mutex) epd_mutex = xSemaphoreCreateMutex();
    if (!rtc_mutex) rtc_mutex = xSemaphoreCreateMutex();
    if (!epd_mutex || !rtc_mutex) return false;

    i2c_master_bus_config_t bus = {};
    bus.i2c_port = I2C_NUM_0;
    bus.sda_io_num = GPIO_NUM_47;
    bus.scl_io_num = GPIO_NUM_48;
    bus.clk_source = I2C_CLK_SRC_DEFAULT;
    bus.glitch_ignore_cnt = 7;
    bus.flags.enable_internal_pullup = true;
    if (i2c_new_master_bus(&bus, &rtc_bus) != ESP_OK) return false;
    i2c_device_config_t device = {};
    device.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    device.device_address = 0x51;
    device.scl_speed_hz = 100000;
    if (i2c_master_bus_add_device(rtc_bus, &device, &rtc_device) != ESP_OK) {
        i2c_del_master_bus(rtc_bus);
        rtc_bus = nullptr;
        return false;
    }
    // Do not initialize or overwrite time: preserve a previously set RTC.
    initialized = true;
    return true;
}

void led_set(bool on) { gpio_set_level(LED, on ? 0 : 1); }

bool epd_show(const uint8_t* frame, display_backend::Waveform waveform) {
    using display_backend::Waveform;
    if (!initialized || !frame || xSemaphoreTake(epd_mutex, portMAX_DELAY) != pdTRUE) return false;
    // This buffer tracks the last successful optical update. A failed drive
    // invalidates it, so the next redraw establishes a full baseline.
    static uint8_t displayed[EPD_FRAME_BYTES];
    static bool displayed_valid = false;
    static unsigned fast_updates = 0;
    constexpr unsigned cleanup_interval = 10;
    int left = 100, right = -1, top = 300, bottom = -1;
    if (displayed_valid) {
        for (int y = 0; y < 300; ++y) {
            for (int x = 0; x < 100; ++x) {
                const int offset = y * 100 + x;
                if (frame[offset] == displayed[offset]) continue;
                left = std::min(left, x);
                right = std::max(right, x);
                top = std::min(top, y);
                bottom = y;
            }
        }
        if (right < 0) {
            xSemaphoreGive(epd_mutex);
            return true;
        }
    }
    const bool fast = displayed_valid &&
                      (waveform == Waveform::FastPreferred || fast_updates < cleanup_interval);
    const bool window = fast &&
                        (left != 0 || right != 99 || top != 0 || bottom != 299);
    spi_device_handle_t spi = nullptr;
    bool bus_started = false;
    const int64_t start = esp_timer_get_time();
    auto paint = [&]() -> bool {
        spi_bus_config_t bus = {};
        bus.mosi_io_num = EPD_MOSI;
        bus.miso_io_num = -1;
        bus.sclk_io_num = EPD_SCK;
        bus.quadwp_io_num = -1;
        bus.quadhd_io_num = -1;
        bus.max_transfer_sz = 100;
        if (spi_bus_initialize(SPI3_HOST, &bus, SPI_DMA_CH_AUTO) != ESP_OK) return false;
        bus_started = true;
        spi_device_interface_config_t device = {};
        device.clock_speed_hz = 8000000; // Known reference setting, not guessed 40 MHz.
        device.mode = 0;
        device.spics_io_num = -1; // Explicit CS control for command and data.
        device.queue_size = 1;
        if (spi_bus_add_device(SPI3_HOST, &device, &spi) != ESP_OK) return false;
        gpio_hold_dis(EPD_POWER);
        gpio_set_level(EPD_POWER, 1);
        gpio_set_direction(EPD_BUSY, GPIO_MODE_INPUT);
        gpio_set_pull_mode(EPD_BUSY, GPIO_PULLUP_ONLY);
        gpio_set_level(EPD_CS, 1);
        delay_ms(10);
        gpio_set_level(EPD_RST, 1); delay_ms(10);
        gpio_set_level(EPD_RST, 0); delay_ms(20);
        gpio_set_level(EPD_RST, 1); delay_ms(10);
        if (!wait_ready(10000)) return false;
        if (fast) {
            const auto send = [&](uint8_t reg, std::initializer_list<uint8_t> values) {
                return command(spi, reg) && tx(spi, true, values.begin(), values.size());
            };
            if (!send(0x00, {0x2f, 0x8e}) ||
                !send(0x01, {0x07, FAST_BW_REFERENCE[0], FAST_BW_REFERENCE[1],
                             FAST_BW_REFERENCE[2], FAST_BW_REFERENCE[3],
                             FAST_BW_REFERENCE[4]}) ||
                !send(0x30, {FAST_BW_REFERENCE[6]}) ||
                !send(0x82, {FAST_BW_REFERENCE[5]}) ||
                !send(0x06, {0x0f, 0x8b, 0x9c, 0xaa}) ||
                !send(0xe7, {0x98}) || !send(0x50, {0x37}) ||
                !send(0x61, {0x01, 0x90, 0x01, 0x2c}) ||
                !send(0x62, {0x64, 0x53}) || !send(0x65, {0, 0, 0, 0}))
                return false;
        }
        if (!command(spi, 0xe9) || !data_byte(spi, 0x01)) return false;
        alignas(4) uint8_t row[100];
        if (fast) {
            if (!wait_ready(10000) || !command(spi, 0x20)) return false;
            for (size_t offset = 0; offset < sizeof FAST_BW_REFERENCE; offset += sizeof row) {
                const size_t count = std::min(sizeof row, sizeof FAST_BW_REFERENCE - offset);
                std::memcpy(row, FAST_BW_REFERENCE + offset, count);
                if (!tx(spi, true, row, count)) return false;
            }
        }
        if (window) {
            const int first = left * 4;
            const int last = right * 4 + 3;
            const uint8_t packet[] = {
                uint8_t(first >> 8), uint8_t(first), uint8_t(last >> 8),
                uint8_t(last), uint8_t(top >> 8), uint8_t(top),
                uint8_t(bottom >> 8), uint8_t(bottom), 1,
            };
            delay_ms(10);
            if (!command(spi, 0x83) || !tx(spi, true, packet, sizeof packet))
                return false;
        }
        if (!command(spi, 0x10) || !wait_ready(10000)) return false;
        const int x0 = window ? left : 0;
        const int x1 = window ? right : 99;
        const int y0 = window ? top : 0;
        const int y1 = window ? bottom : 299;
        for (int y = y0; y <= y1; ++y) {
            for (int x = x0; x <= x1; ++x) {
                const int offset = y * 100 + x;
                row[x - x0] = fast
                    ? uint8_t(((displayed[offset] & 0x55) << 1) | (frame[offset] & 0x55))
                    : frame[offset];
            }
            if (!tx(spi, true, row, x1 - x0 + 1)) return false;
            if (((y - y0) & 15) == 15) delay_ms(1);
        }
        if (!command(spi, 0x04)) return false;
        delay_ms(10);
        if (!wait_ready(10000)) return false;
        if (!command(spi, 0x12) || !data_byte(spi, 0x00) ||
            !wait_refresh_complete() || !command(spi, 0x02) || !data_byte(spi, 0x00)) return false;
        delay_ms(10);
        if (!wait_ready(10000)) return false;
        delay_ms(20);
        return command(spi, 0x07) && data_byte(spi, 0xA5);
    };
    bool ok = paint();
    if (spi && spi_bus_remove_device(spi) != ESP_OK) ok = false;
    if (bus_started && spi_bus_free(SPI3_HOST) != ESP_OK) ok = false;
    power_off_panel(); // Also on all failures; never leave panel driven indefinitely.
    displayed_valid = ok;
    if (ok) {
        std::memcpy(displayed, frame, EPD_FRAME_BYTES);
        fast_updates = fast ? std::min(fast_updates + 1, cleanup_interval) : 0;
    }
    xSemaphoreGive(epd_mutex);
    ESP_LOGI(TAG, "%s %s refresh %s in %lld ms", fast ? "Fast" : "Native",
             window ? "windowed" : "full",
             ok ? "complete" : "FAILED",
             static_cast<long long>((esp_timer_get_time() - start) / 1000));
    return ok;
}

bool rtc_get_epoch(int64_t& utc_epoch) {
    if (!initialized || !rtc_device || xSemaphoreTake(rtc_mutex, portMAX_DELAY) != pdTRUE) return false;
    int64_t result = 0;
    const bool ok = read_epoch_locked(result);
    xSemaphoreGive(rtc_mutex);
    if (ok) utc_epoch = result;
    return ok;
}

bool rtc_set_epoch(int64_t utc_epoch) {
    if (!initialized || utc_epoch < MIN_EPOCH || utc_epoch > MAX_EPOCH) return false;
    const time_t input = static_cast<time_t>(utc_epoch);
    tm utc = {};
    if (!gmtime_r(&input, &utc)) return false;
    const uint8_t values[] = {
        encode_bcd(utc.tm_sec), encode_bcd(utc.tm_min), encode_bcd(utc.tm_hour),
        encode_bcd(utc.tm_mday), uint8_t(utc.tm_wday), encode_bcd(utc.tm_mon + 1),
        encode_bcd(utc.tm_year - 100)
    };
    if (xSemaphoreTake(rtc_mutex, portMAX_DELAY) != pdTRUE) return false;
    const uint8_t stop = 0x20;
    const uint8_t run = 0x00;
    // If a burst fails, deliberately leave STOP asserted. An invalid partial
    // time must not be mistaken for valid time on the next read or reboot.
    bool ok = rtc_write(0x00, &stop, 1) && rtc_write(0x02, values, sizeof(values)) &&
              rtc_write(0x00, &run, 1);
    int64_t actual = 0;
    if (ok) ok = read_epoch_locked(actual) && actual >= utc_epoch && actual <= utc_epoch + 1;
    xSemaphoreGive(rtc_mutex);
    return ok;
}
