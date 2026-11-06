#include "core.h"
#include "cJSON.h"
#include <cassert>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <unistd.h>
using timer_app::Core;
static int checks=0;
#define CHECK(x) do{++checks;if(!(x)){std::cerr<<"Failed line "<<__LINE__<<": "<<#x<<"\n";std::abort();}}while(0)
std::string dir(){char t[]="/tmp/note4c-test-XXXXXX";return mkdtemp(t);}
std::string read(const std::string&p){std::ifstream f(p);return {std::istreambuf_iterator<char>(f),{}};}
void write(const std::string&p,const std::string&s){std::ofstream f(p);f<<s;}
int main(){auto settingsroot=dir();{Core c(settingsroot);CHECK(c.init());CHECK(c.utc_offset_minutes()==480);CHECK(c.set_utc_offset_minutes(-330));CHECK(c.utc_offset_minutes()==-330);CHECK(!c.set_utc_offset_minutes(-721));CHECK(!c.set_utc_offset_minutes(841));CHECK(c.utc_offset_minutes()==-330);CHECK(!c.has_utc_marker());}{Core c(settingsroot);CHECK(c.init());CHECK(c.utc_offset_minutes()==-330);CHECK(c.view(0,false).utc_offset_minutes==-330);}write(settingsroot+"/settings.json","{\"schema_version\":1,\"utc_offset_minutes\":9999}");{Core c(settingsroot);CHECK(!c.init());CHECK(read(settingsroot+"/settings.json").find("9999")!=std::string::npos);}std::filesystem::remove_all(settingsroot);auto utc_marker_root=dir();{Core c(utc_marker_root);CHECK(c.init());CHECK(!c.has_utc_marker());CHECK(c.save_utc_marker());CHECK(c.has_utc_marker());}{Core c(utc_marker_root);CHECK(c.init());CHECK(c.has_utc_marker());}write(utc_marker_root+"/clock.json","{\"schema_version\":1,\"basis\":\"LOCAL\"}");{Core c(utc_marker_root);CHECK(c.init());CHECK(!c.has_utc_marker());}std::filesystem::remove_all(utc_marker_root);constexpr int64_t date=1793682000;auto root=dir();
 {
 Core c(root);CHECK(c.init());CHECK(c.view(0,true).total==3);CHECK(!c.start(1000,0));CHECK(c.start(1000000,date));CHECK(!c.select(1));CHECK(c.view(2500000,true).elapsed_ms==1500);CHECK(c.stop(4000000,date+3));CHECK(c.view(0,true).best_ms==3000);CHECK(c.record_count()==1);
 CHECK(c.start(9000000,date+10));CHECK(c.stop(11000000,date+12));CHECK(c.view(0,true).best_ms==2000);CHECK(c.start(12000000,date+13));CHECK(c.stop(16000000,date+17));CHECK(c.view(0,true).best_ms==2000);CHECK(c.view(0,true).elapsed_ms==4000);
 cJSON*d=cJSON_Parse("{\"schema_version\":1,\"actions\":[{\"id\":\"activity-1\",\"name\":\"穿衣服\"},{\"id\":\"new\",\"name\":\"New\"}]}");CHECK(c.set_actions(d));cJSON_Delete(d);CHECK(c.view(0,true).best_ms==2000);CHECK(read(root+"/history.json").find("Activity 1")!=std::string::npos);CHECK(c.select(1));CHECK(c.view(0,true).best_ms==-1);CHECK(c.select(1));CHECK(c.view(0,true).name=="穿衣服");CHECK(c.start(50000000,date+20));
 }
 {Core c(root);CHECK(c.init());CHECK(c.record_count()==4);CHECK(c.view(0,true).best_ms==2000);CHECK(read(root+"/history.json").find("interrupted")!=std::string::npos);CHECK(!std::filesystem::exists(root+"/pending.json"));}
 auto valid=read(root+"/history.json");write(root+"/history.json",valid.substr(0,valid.size()-3)+",{\"seq\":5,\"action");
 {Core c(root);CHECK(c.init());CHECK(c.record_count()==4);CHECK(read(root+"/history.json")==valid);CHECK(c.start(10000,date+30));CHECK(c.stop(1110000,date+31));CHECK(c.view(0,true).best_ms==1100);}
 {Core c(root);CHECK(c.init());CHECK(c.record_count()==5);cJSON*d=cJSON_Parse(read(root+"/history.json").c_str());CHECK(d);cJSON_Delete(d);}
 auto bad=dir();write(bad+"/actions.json","this is invalid");{Core c(bad);CHECK(!c.init());CHECK(read(bad+"/actions.json")=="this is invalid");}
 {std::vector<timer_app::Action>a;std::string why;for(auto s:{"{\"schema_version\":1,\"actions\":[]}","{\"schema_version\":1,\"actions\":[{\"id\":\"A\",\"name\":\"Bad\"}]}","{\"schema_version\":1,\"actions\":[{\"id\":\"a\",\"name\":\"💪\"}]}","{\"schema_version\":1,\"actions\":[{\"id\":\"a\",\"name\":\"A\"},{\"id\":\"a\",\"name\":\"B\"}]}"}){cJSON*d=cJSON_Parse(s);CHECK(!timer_app::validate_actions(d,a,why));cJSON_Delete(d);}}

 // Wall-clock adjustments must never alter elapsed time or shortest duration.
 auto clockroot=dir();
 {Core c(clockroot);CHECK(c.init());CHECK(c.start(1000000,date));CHECK(c.stop(2234567,date-3600));CHECK(c.view(0,true).elapsed_ms==1234);CHECK(c.view(0,true).best_ms==1234);}
 // Completed write-ahead records are recovered exactly once, even if history
 // was committed before reset but pending unlink was not.
 auto recoverroot=dir();
 const std::string record="{\"seq\":1,\"action_id\":\"activity-1\",\"action_name\":\"Activity 1\",\"started_at\":\"2026-11-03T00:00:00Z\",\"ended_at\":\"2026-11-03T00:00:01Z\",\"duration_ms\":1234,\"status\":\"completed\"}";
 {Core c(recoverroot);CHECK(c.init());}
 const auto empty=read(recoverroot+"/history.json");
 for(size_t cut=0;cut<=record.size()+1;++cut){
   write(recoverroot+"/history.json",empty.substr(0,empty.size()-3)+(record+"\n").substr(0,cut));
   write(recoverroot+"/pending.json",record+"\n");
   Core c(recoverroot);CHECK(c.init());CHECK(c.record_count()==1);CHECK(c.view(0,true).best_ms==1234);
   cJSON*d=cJSON_Parse(read(recoverroot+"/history.json").c_str());CHECK(d);cJSON_Delete(d);
 }
 write(recoverroot+"/pending.json",record+"\n");
 {Core c(recoverroot);CHECK(c.init());CHECK(c.record_count()==1);CHECK(!std::filesystem::exists(recoverroot+"/pending.json"));}
 // Removing and re-adding a stable ID retains all original statistics.
 {Core c(recoverroot);CHECK(c.init());
  auto *d=cJSON_Parse("{\"schema_version\":1,\"actions\":[{\"id\":\"other\",\"name\":\"Other\"}]}");CHECK(c.set_actions(d));cJSON_Delete(d);CHECK(c.view(0,true).best_ms==-1);
  d=cJSON_Parse("{\"schema_version\":1,\"actions\":[{\"id\":\"activity-1\",\"name\":\"Renamed\"}]}");CHECK(c.set_actions(d));cJSON_Delete(d);CHECK(c.view(0,true).best_ms==1234);CHECK(c.record_count()==1);
 }

 // Corrupt committed final rows with intact footer must never be truncated.
 const auto committed=read(recoverroot+"/history.json");
 auto damaged=committed;
 damaged.replace(damaged.find("completed"),9,"corrupted");
 write(recoverroot+"/history.json",damaged);
 {Core c(recoverroot);CHECK(!c.init());CHECK(read(recoverroot+"/history.json")==damaged);}
 write(recoverroot+"/history.json",committed);
 // Malformed write-ahead entries stay intact for manual recovery, with the
 // existing committed history untouched, on every restart.
 for(const auto &pending: {std::string("{\"seq\":2}"),std::string("{}"),std::string("not JSON"),std::string("{\"seq\":2,\"action_id\":\"activity-1\",\"action_name\":\"Activity 1\",\"started_at\":\"2026-11-03T00:00:00Z\",\"status\":\"completed\",\"duration_ms\":-1,\"ended_at\":\"2026-11-03T00:00:01Z\"}")}){
  write(recoverroot+"/pending.json",pending);
  for(int restart=0;restart<2;restart++){Core c(recoverroot);CHECK(!c.init());CHECK(read(recoverroot+"/pending.json")==pending);CHECK(read(recoverroot+"/history.json")==committed);}
 }
 auto textroot = dir();
 {
   Core c(textroot);
   auto new_id = +[](const std::string &) { return std::string("test-new-id"); };
   CHECK(c.init());
   CHECK(c.start(1000000, date));
   CHECK(!c.import_activities("拉伸\n", new_id));
   CHECK(c.stop(2300000, date + 2));
   const auto history = read(textroot + "/history.json");
   CHECK(c.import_activities("\xef\xbb\xbf拉伸\r\nActivity 1\r\n", new_id));
   CHECK(c.activities_text() == "拉伸\nActivity 1\n");
   CHECK(c.view(0, true).name == "拉伸");
   CHECK(c.select(1));
   CHECK(c.view(0, true).best_ms == 1300);
   CHECK(!c.import_activities("拉伸\n拉伸\n", new_id));
   CHECK(!c.import_activities("💪\n", new_id));
   CHECK(!c.import_activities(std::string("A\0B", 3), new_id));
   CHECK(!c.import_activities("\n", new_id));
   CHECK(c.activities_text() == "拉伸\nActivity 1\n");
   CHECK(read(textroot + "/history.json") == history);
 }
 {
   Core c(textroot);
   CHECK(c.init());
   CHECK(c.activities_text() == "拉伸\nActivity 1\n");
 }
 std::filesystem::remove_all(textroot);
 std::filesystem::remove_all(clockroot);std::filesystem::remove_all(recoverroot);
 std::filesystem::remove_all(root);std::filesystem::remove_all(bad);std::cout<<checks<<" core assertions passed\n";
}
