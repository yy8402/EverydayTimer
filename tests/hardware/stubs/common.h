#pragma once
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <string>
using esp_err_t = int;
constexpr esp_err_t ESP_OK = 0;
inline const char* esp_err_to_name(int e) { return e ? "ERR" : "OK"; }
using gpio_num_t=int;
constexpr int GPIO_NUM_0=0, GPIO_NUM_3=3, GPIO_NUM_6=6, GPIO_NUM_8=8,
 GPIO_NUM_9=9, GPIO_NUM_10=10, GPIO_NUM_11=11, GPIO_NUM_12=12, GPIO_NUM_13=13,
 GPIO_NUM_17=17, GPIO_NUM_18=18, GPIO_NUM_21=21, GPIO_NUM_39=39,
 GPIO_NUM_42=42, GPIO_NUM_46=46, GPIO_NUM_47=47, GPIO_NUM_48=48;
constexpr int GPIO_MODE_OUTPUT=1,GPIO_MODE_INPUT=2,GPIO_MODE_DISABLE=0,
 GPIO_PULLUP_ENABLE=1,GPIO_PULLDOWN_DISABLE=0,GPIO_INTR_DISABLE=0,
 GPIO_FLOATING=0,GPIO_PULLUP_ONLY=1;
struct gpio_config_t { uint64_t pin_bit_mask; int mode,pull_up_en,pull_down_en,intr_type; };
inline int levels[50]={}, directions[50]={};
inline bool touched[50]={};
inline int i2c_transmit_calls=0, i2c_fail_on_call=0;
inline int64_t now_us=0;
inline int busy_left=0, display_mode=0, last_command=-1, bus_count=0, device_count=0;
inline bool spi_fail=false, i2c_fail=false;
inline uint8_t registers_[16]={};
inline int gpio_hold_dis(int){return 0;}
inline int gpio_hold_en(int){return 0;}
inline int gpio_reset_pin(int p){touched[p]=true;levels[p]=0;directions[p]=0;return 0;}
inline int gpio_set_level(int p,int v){touched[p]=true;levels[p]=v;return 0;}
inline int gpio_set_direction(int p,int d){touched[p]=true;directions[p]=d;return 0;}
inline int gpio_set_pull_mode(int,int){return 0;}
inline int gpio_config(const gpio_config_t*c){for(int p=0;p<50;++p)if(c->pin_bit_mask&(1ULL<<p)){touched[p]=true;directions[p]=c->mode;}return 0;}
inline int gpio_get_level(int p){ if(p!=8)return levels[p]; if(display_mode==2)return 0; if(busy_left>0){--busy_left;return 0;} return 1; }
inline int64_t esp_timer_get_time(){now_us+=1000;return now_us;}
#define ESP_LOGE(...) ((void)0)
#define ESP_LOGI(...) ((void)0)
#define pdMS_TO_TICKS(x) (x)
constexpr int pdTRUE=1, portMAX_DELAY=-1;
inline void vTaskDelay(int n){now_us+=int64_t(n)*1000;}
using SemaphoreHandle_t=void*;
inline void* xSemaphoreCreateMutex(){return reinterpret_cast<void*>(1);}
inline int xSemaphoreTake(void*,int){return 1;}
inline void xSemaphoreGive(void*){}
using i2c_master_bus_handle_t=void*;
using i2c_master_dev_handle_t=void*;
constexpr int I2C_NUM_0=0,I2C_CLK_SRC_DEFAULT=0,I2C_ADDR_BIT_LEN_7=7;
struct i2c_master_bus_config_t { int i2c_port,sda_io_num,scl_io_num,clk_source,glitch_ignore_cnt; struct { bool enable_internal_pullup; } flags; };
struct i2c_device_config_t { int dev_addr_length,device_address,scl_speed_hz; };
inline int i2c_new_master_bus(const i2c_master_bus_config_t*,void**p){*p=reinterpret_cast<void*>(1);return 0;}
inline int i2c_del_master_bus(void*){return 0;}
inline int i2c_master_bus_add_device(void*,const i2c_device_config_t*,void**p){*p=reinterpret_cast<void*>(1);return 0;}
inline int i2c_master_transmit_receive(void*,const uint8_t*p,size_t,uint8_t*out,size_t n,int){if(i2c_fail)return 1;std::memcpy(out,registers_+*p,n);return 0;}
inline int i2c_master_transmit(void*,const uint8_t*p,size_t n,int){++i2c_transmit_calls;if(i2c_fail || (i2c_fail_on_call>0 && i2c_transmit_calls==i2c_fail_on_call))return 1;std::memcpy(registers_+p[0],p+1,n-1);return 0;}
using spi_device_handle_t=void*;
constexpr int SPI3_HOST=3,SPI_DMA_CH_AUTO=1,SPI_TRANS_USE_TXDATA=1;
struct spi_bus_config_t { int mosi_io_num,miso_io_num,sclk_io_num,quadwp_io_num,quadhd_io_num,max_transfer_sz; };
struct spi_device_interface_config_t { int clock_speed_hz,mode,spics_io_num,queue_size; };
struct spi_transaction_t { size_t length; int flags; uint8_t tx_data[4]; const void* tx_buffer; };
inline int spi_bus_initialize(int,const spi_bus_config_t*,int){++bus_count;return 0;}
inline int spi_bus_add_device(int,const spi_device_interface_config_t*,void**p){++device_count;*p=reinterpret_cast<void*>(1);return 0;}
inline int spi_bus_remove_device(void*){--device_count;return 0;}
inline int spi_bus_free(int){--bus_count;return 0;}
inline int spi_device_polling_transmit(void*,spi_transaction_t*t){if(spi_fail)return 1;auto p=(t->flags&1)?t->tx_data:static_cast<const uint8_t*>(t->tx_buffer);if(!levels[10])last_command=*p;else if(last_command==0x12 && display_mode!=3)busy_left=10;return 0;}
