#include "duration.h"
#include "ui.h"
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <climits>
#include <string>
#include <vector>
// These tests use English labels. CJK shape verification uses the real binary
// asset in the separately generated Chinese renderer preview.
extern const uint8_t test_cjk_stub[96] asm("_binary_cjk_bin_start")={0};
int checks=0;
#define CHECK(x) do{++checks;if(!(x)){fprintf(stderr,"Failed line %d: %s\n",__LINE__,#x);std::abort();}}while(0)
int main(){
 struct Case{int64_t ms;const char* text;};
 const Case cases[]={
 {-1,"0 s"},{0,"0 s"},{1,"0 s"},{99,"0 s"},{100,"0 s"},{999,"0 s"},{1000,"1 s"},
 {32700,"32 s"},{59900,"59 s"},{59999,"59 s"},{60000,"1:00"},{60099,"1:00"},{60100,"1:00"},
 {60500,"1:00"},{119999,"1:59"},{120000,"2:00"},{1825000,"30:25"},{3599999,"59:59"},
 {3600000,"1:00:00"},{3600100,"1:00:00"},{3723400,"1:02:03"},{36000000,"10:00:00"},
 {86400000,"24:00:00"},{359999999,"99:59:59"},{360000000,"100:00:00"},
 {INT64_MAX,"2562047788015:12:55"}};
 for(const auto& c:cases){auto out=timer_app::format_duration(c.ms);CHECK(out.with_unit()==c.text);CHECK(duration_layout_width(c.ms)>0);CHECK(duration_layout_width(c.ms)<=360);
  std::vector<uint8_t>guarded(30032,0xa5);timer_app::View v;v.name="Activity 1";v.total=3;v.storage_ok=true;v.clock_valid=true;v.utc_epoch=1793683800;v.elapsed_ms=c.ms;v.best_ms=std::max<int64_t>(0,c.ms);render_ui(guarded.data()+16,v);
  CHECK(std::all_of(guarded.begin(),guarded.begin()+16,[](uint8_t b){return b==0xa5;}));CHECK(std::all_of(guarded.end()-16,guarded.end(),[](uint8_t b){return b==0xa5;}));
  bool has_number=false,within_margin=true;for(int y=134;y<214;y++)for(int x=0;x<400;x++){auto b=guarded[16+y*100+x/4];int color=(b>>(6-2*(x%4)))&3;if(color==0){has_number=true;if(x<20||x>=380)within_margin=false;}}
  CHECK(has_number);CHECK(within_margin);
 }
 // Validate adaptive fitting over every hour/minute transition for 100 hours.
 for(int64_t minutes=1;minutes<=6000;minutes++)for(int64_t delta:{-1,0,1,99,100})CHECK(duration_layout_width(minutes*60000+delta)<=360);
 printf("%d duration/renderer assertions passed\n",checks);
}
