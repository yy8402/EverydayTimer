#include <cassert>
#include <cstdio>
#include "../../main/hardware.cpp"
int main(){
 assert(hardware_init()); assert(hardware_init());
 assert(levels[17]==1 && levels[42]==1 && levels[46]==0 && levels[21]==0);
 assert(levels[3]==1 && directions[3]==GPIO_MODE_OUTPUT);
 assert(!touched[19] && !touched[20]); // Native USB pins must remain untouched.
 led_set(true); assert(levels[3]==0); led_set(false);assert(levels[3]==1);
 int64_t t=-1;
 assert(!rtc_get_epoch(t) && t==-1);
 assert(!rtc_set_epoch(946684799));assert(!rtc_set_epoch(4102444800LL));
 size_t dates=0;
 for(int64_t v=946684800;v<=4102444799LL;v+=86400){
  assert(rtc_set_epoch(v));assert(rtc_get_epoch(t));assert(t==v);++dates;
 }
 for(int64_t v : {946684800LL,951782400LL,2147483647LL,2147483648LL,4102444799LL}){
  assert(rtc_set_epoch(v));assert(rtc_get_epoch(t));assert(t==v);
 }
 assert(rtc_set_epoch(1704067200)); // 2024-01-01
 const uint8_t sec=registers_[2];registers_[2]=0x80|sec;assert(!rtc_get_epoch(t));registers_[2]=sec;
 registers_[0]=0x20;assert(!rtc_get_epoch(t));registers_[0]=0x80;assert(!rtc_get_epoch(t));registers_[0]=0;
 registers_[2]=0x6A;assert(!rtc_get_epoch(t));registers_[2]=0;
 registers_[3]=0x60;assert(!rtc_get_epoch(t));registers_[3]=0;
 registers_[4]=0x24;assert(!rtc_get_epoch(t));registers_[4]=0;
 registers_[7]=0x04;registers_[5]=0x31;assert(!rtc_get_epoch(t));
 registers_[7]=0x02;registers_[5]=0x29;registers_[8]=0x23;assert(!rtc_get_epoch(t));
 registers_[8]=0x24;assert(rtc_get_epoch(t));
 registers_[7]|=0x80;assert(!rtc_get_epoch(t));registers_[7]&=0x7f;
 registers_[6]=7;assert(!rtc_get_epoch(t));registers_[6]=0;
 i2c_fail=true;assert(!rtc_get_epoch(t));assert(!rtc_set_epoch(1704067200));i2c_fail=false;
 assert(rtc_set_epoch(1704067200));
 i2c_transmit_calls=0;i2c_fail_on_call=2; // Fail the seven-byte write after STOP.
 assert(!rtc_set_epoch(1704067200));assert(registers_[0]&0x20);assert(!rtc_get_epoch(t));
 i2c_fail_on_call=0;assert(rtc_set_epoch(1704067200));
 i2c_transmit_calls=0;i2c_fail_on_call=3; // Fail the resume after a successful burst.
 assert(!rtc_set_epoch(1704067200));assert(registers_[0]&0x20);assert(!rtc_get_epoch(t));
 i2c_fail_on_call=0;assert(rtc_set_epoch(1704067200));
 uint8_t frame[30000]={};assert(epd_show(frame));
 assert(bus_count==0&&device_count==0&&levels[6]==0&&levels[13]==0);
 display_mode=2;assert(epd_show(frame)); // No bus transaction for an identical frame.
 frame[0]=1;assert(!epd_show(frame));assert(bus_count==0&&device_count==0&&levels[6]==0);
 display_mode=3;assert(!epd_show(frame));assert(bus_count==0&&device_count==0&&levels[6]==0);
 display_mode=0;spi_fail=true;assert(!epd_show(frame));assert(bus_count==0&&device_count==0&&levels[6]==0);
 spi_fail=false;assert(epd_show(frame));
 std::printf("PASS: %zu daily UTC roundtrips 2000-2099, 2038 boundary, leap days, invalid BCD/ranges/VL/STOP/century, I2C failure, EPD success/BUSY timeout/no assertion/SPI failure/recovery and power cleanup\n",dates);
}
