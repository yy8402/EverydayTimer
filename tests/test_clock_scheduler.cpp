#include "clock_view.h"
#include "refresh_scheduler.h"
#include <cassert>
#include <cstdio>
#include <string>
int checks=0;
#define CHECK(x) do{++checks;if(!(x)){fprintf(stderr,"Clock test failed line %d: %s\n",__LINE__,#x);return 1;}}while(0)
int main(){using namespace timer_app;using display_backend::Update;
 IdleScreen idle(10);
 CHECK(!idle.update(300000009, false));
 CHECK(idle.update(300000010, false));
 CHECK(idle.visible());
 CHECK(!idle.update(300000011, false));
 CHECK(idle.update(300000012, true));
 CHECK(!idle.visible());
 CHECK(!idle.update(900000000, true));
 CHECK(!idle.update(1199999999, false));
 CHECK(idle.update(1200000000, false));

 CHECK(format_clock(1704067200,true,480).date_time=="2024-01-01 08:00");
 CHECK(format_clock(1704067200,true,-720).date_time=="2023-12-31 12:00");
 CHECK(format_clock(1704067200,true,345).date_time=="2024-01-01 05:45");
 CHECK(format_clock(1704067200,true,-210).date_time=="2023-12-31 20:30");
 CHECK(format_clock(1704067200,true,0).zone=="UTC+00:00");
 CHECK(format_clock(1704067200,true,480).zone=="UTC+08:00");
 CHECK(format_clock(1704067200,true,-210).zone=="UTC-03:30");
 CHECK(format_clock(1709164800,true,480).date_time=="2024-02-29 08:00");
 CHECK(format_clock(1709249400,true,60).date_time=="2024-03-01 00:30");
 CHECK(format_clock(4102444799,true,840).date_time=="2100-01-01 13:59");
 CHECK(format_clock(0,true,480).date_time=="SYNC CLOCK VIA USB");
 CHECK(format_clock(1704067200,false,480).date_time=="SYNC CLOCK VIA USB");
 CHECK(format_clock(4102444800,true,480).date_time=="SYNC CLOCK VIA USB");
 CHECK(format_clock(1704067200,true,841).date_time=="CHECK TIMEZONE VIA USB");
 CHECK(!valid_utc_offset(-721));CHECK(valid_utc_offset(-720));CHECK(valid_utc_offset(840));CHECK(!valid_utc_offset(841));
 constexpr int64_t epoch=1704067200;
 for(bool running:{false,true}){RefreshScheduler s;int updates=0;CHECK(s.poll(0,epoch,true,running,false)==Update::None);
  for(int ms=5;ms<=180000;ms+=5){auto u=s.poll(int64_t(ms)*1000,epoch+ms/1000,true,running,false);bool boundary=running && ms%60000==0;CHECK((u!=Update::None)==boundary);if(u!=Update::None){updates++;CHECK(u==Update::ClockAndNumber);}}
  CHECK(updates==(running?3:0));
 }
 {RefreshScheduler s;CHECK(s.poll(0,epoch+17,true,true,false)==Update::None);CHECK(s.poll(42000000,epoch+59,true,true,false)==Update::None);CHECK(s.poll(43000000,epoch+60,true,true,false)==Update::ClockAndNumber);CHECK(s.poll(60000000,epoch+77,true,true,false)==Update::None);CHECK(s.poll(103000000,epoch+120,true,true,false)==Update::ClockAndNumber);}
 {RefreshScheduler s;int nums=0,both=0;CHECK(s.poll(0,epoch,true,true,true)==Update::None);for(int second=1;second<=180;second++){auto u=s.poll(int64_t(second)*1000000,epoch+second,true,true,true);if(second%60==0){CHECK(u==Update::ClockAndNumber);both++;}else{CHECK(u==Update::ElapsedNumberOnly);nums++;}}CHECK(nums==177);CHECK(both==3);}
 {RefreshScheduler s;CHECK(s.poll(0,epoch,true,false,false)==Update::None);CHECK(s.poll(10000,epoch+10800,true,false,false)==Update::None);for(int i=0;i<100;i++)CHECK(s.poll(10000+i,epoch+10800,true,false,false)==Update::None);CHECK(s.poll(20000,epoch+60,true,false,false)==Update::None);CHECK(s.poll(30000,0,false,false,false)==Update::None);CHECK(s.poll(600000000,0,false,false,false)==Update::None);CHECK(s.poll(700000000,epoch,true,false,false)==Update::None);}
 printf("%d clock/scheduler assertions passed\n",checks);
}
