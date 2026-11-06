#include <initializer_list>
#include "display_backend.h"
#include <cassert>
#include <cstdint>
#include <cstdio>
int calls=0,checks=0;bool answer=true;
#define CHECK(x) do{++checks;assert(x);}while(0)
bool epd_show(const uint8_t*, display_backend::Waveform){++calls;return answer;}
int main(){using namespace display_backend;uint8_t f[30000]{};CHECK(capabilities().number_region_partial);CHECK(capabilities().clock_region_partial);CHECK(running_snapshot_seconds()==1);CHECK(show(f,Update::None));CHECK(calls==0);for(auto mode:{Update::Full,Update::ElapsedNumberOnly,Update::ClockOnly,Update::ClockAndNumber,static_cast<Update>(unsigned(Update::Full)|unsigned(Update::ClockAndNumber))}){int before=calls;CHECK(show(f,mode));CHECK(calls==before+1);}answer=false;CHECK(!show(f,Update::ClockAndNumber));CHECK(NUMBER_REGION.x%4==0);CHECK(NUMBER_REGION.width%4==0);CHECK(CLOCK_REGION.x%4==0);CHECK(CLOCK_REGION.width%4==0);CHECK(CLOCK_REGION.y+CLOCK_REGION.height<=NUMBER_REGION.y);printf("%d display-backend assertions passed\n",checks);}
