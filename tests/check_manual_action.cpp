#include <Arduino.h>
#include "../CYDTicker/investment_math.h"
#include <cassert>
#include <map>
#include <memory>
#include <cstdio>
using std::floorf;

struct Request {
  std::map<std::string,String> args;
  String arg(const String& key)const{auto it=args.find(key.c_str());return it==args.end()?String():it->second;}
  bool hasArg(const String& key)const{return args.count(key.c_str())!=0;}
} server;
bool result=false;String resultMessage;int saves=0;
int dataMutex=0;constexpr int portMAX_DELAY=0;
void xSemaphoreTake(int,int){} void xSemaphoreGive(int){}
bool savePrefs(){++saves;return true;} void drawAll(){}
struct MutationGuard{bool commit(bool& ok,String&){if(ok)savePrefs();return ok;}};
void sendMutationResult(bool ok,const String& message){result=ok;resultMessage=message;}
time_t parseDateYMD(const String& date){return parseCalendarDate(date.c_str());}
#include "manual-action-under-test.h"

void request(std::initializer_list<std::pair<const std::string,String>> args){server.args=args;handleManualAction();}
int main(){
  setenv("TZ","CET-1CEST,M3.5.0/2,M10.5.0/3",1);tzset();
  manualCount=1;manualAssets[0].name="Toyota Bank";
  assert(recordManualValuation(manualAssets[0],parseCalendarDate("2026-01-01"),76171.42f,1262.77f));
  String original=manualVersion(manualAssets[0]);
  request({{"op","savings"},{"id","0"},{"version",original},{"date","2026-01-02"},{"deposit","5000"},{"interest","250"}});
  assert(result&&saves==1);assert(std::abs(manualAssets[0].valuePLN-81421.42)<.01);
  assert(std::abs(manualAssets[0].gainPLN-1512.77)<.001&&manualAssets[0].historyCount==2);
  // Simulate a lost response and retry: the same deposit must not be doubled.
  handleManualAction();assert(!result&&saves==1&&manualAssets[0].historyCount==2);
  assert(std::abs(manualAssets[0].valuePLN-81421.42)<.01);
  request({{"op","savings"},{"id","0"},{"version",manualVersion(manualAssets[0])},{"date","2026-01-02"},{"deposit","5000"},{"interest","0"}});
  assert(result&&manualAssets[0].historyCount==2&&std::abs(manualAssets[0].valuePLN-86421.42)<.01);
  request({{"op","savings"},{"id","0"},{"version",manualVersion(manualAssets[0])},{"date","2026-01-01"},{"deposit","5000"},{"interest","0"}});
  assert(!result&&manualAssets[0].historyCount==2&&std::abs(manualAssets[0].valuePLN-86421.42)<.01);
  request({{"op","create"},{"kind","ppk"},{"date","2026-01-01"},{"value","0"},{"capital","0"}});
  assert(result&&manualCount==2&&manualAssets[1].ppk&&manualAssets[1].name=="PPK");
  request({{"op","create"},{"kind","ppk"},{"date","2026-01-01"},{"value","0"},{"capital","0"}});
  assert(!result&&manualCount==2); // The automatic name cannot create a duplicate.
  request({{"op","create"},{"kind","savings"},{"date","2026-01-01"},{"value","0"},{"capital","0"}});
  assert(!result&&manualCount==2); // Savings still needs an explicit name.
  request({{"op","valuation"},{"id","1"},{"version",manualVersion(manualAssets[1])},{"date","2026-01-02"},{"value","1180"}});
  assert(!result&&manualAssets[1].valuePLN==0);
  request({{"op","ppk-deposit"},{"id","1"},{"version",manualVersion(manualAssets[1])},{"date","2026-01-02"},{"employee","669.42"},{"employer","502.07"},{"state","0"}});
  assert(result&&std::abs(manualAssets[1].valuePLN-1171.49)<.001&&manualAssets[1].gainPLN==0);
  request({{"op","valuation"},{"id","1"},{"version",manualVersion(manualAssets[1])},{"date","2026-01-02"},{"value","1180"}});
  assert(result&&manualAssets[1].valuePLN==1180&&std::abs(manualAssets[1].gainPLN-8.51)<.001);
  assert(manualAssets[1].historyCount==2);
  request({{"op","ppk-deposit"},{"id","1"},{"version",manualVersion(manualAssets[1])},{"date","2026-01-03"},{"employee","100"},{"employer","50"},{"state","250"},{"value","1570"}});
  assert(result&&manualAssets[1].valuePLN==1570&&std::abs(manualAssets[1].gainPLN+1.49)<.001);
  request({{"op","edit"},{"id","0"},{"version",manualVersion(manualAssets[0])},{"name","Renamed Toyota"},{"kind","savings"}});
  assert(result&&manualAssets[0].name=="Renamed Toyota"&&manualAssets[0].historyCount==2);
  assert(std::abs(manualAssets[0].history[0].valuePLN-76171.42)<.01);
  request({{"op","edit"},{"id","0"},{"version",manualVersion(manualAssets[0])},{"name","PPK"},{"kind","savings"}});
  assert(!result&&manualAssets[0].name=="Renamed Toyota");
  request({{"op","savings"},{"id","0"},{"version",manualVersion(manualAssets[0])},{"date","2099-01-02"},{"deposit","5"},{"interest","0"}});
  assert(!result);
  request({{"op","delete"},{"id","0"},{"version",original}});assert(!result&&manualCount==2);
  request({{"op","delete"},{"id","0"},{"version",manualVersion(manualAssets[0])}});
  assert(result&&manualCount==1&&manualAssets[0].name=="PPK"&&manualAssets[0].ppk&&manualAssets[0].historyCount==3);
  request({{"op","create"},{"name","Bad PPK"},{"kind","ppk"},{"date","2026-01-01"},{"value","1180"},{"capital","0"}});
  assert(!result&&manualCount==1);
  request({{"op","create"},{"name","Empty PPK"},{"kind","ppk"},{"date","2026-01-05"},{"value","0"},{"capital","0"}});
  assert(result);
  request({{"op","ppk-deposit"},{"id","1"},{"version",manualVersion(manualAssets[1])},{"date","2026-01-02"},{"employee","10"},{"employer","5"},{"state","0"}});
  assert(result&&manualAssets[1].valuePLN==15&&manualAssets[1].gainPLN==0);
  assert(manualAssets[1].historyCount==1&&manualAssets[1].history[0].ts==parseCalendarDate("2026-01-02"));
  request({{"op","create"},{"name","Existing PPK"},{"kind","ppk"},{"date","2026-09-11"},{"value","1194.93"},{"capital","1171.49"}});
  assert(result&&manualCount==3&&manualAssets[2].ppk);
  assert(std::abs(manualAssets[2].gainPLN-23.44)<.001&&std::abs(manualAssets[2].valuePLN-1194.93)<.001);
  puts("PASS: actual account handler, existing history, deposits, PPK contribution/valuation/loss, rename, chronology, stale retries and targeted deletion.");
}
