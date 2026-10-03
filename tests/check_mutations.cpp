#include <Arduino.h>
#include <ArduinoJson.h>
#include "../CYDTicker/investment_math.h"
#include "../CYDTicker/persistent_store.h"
#include <atomic>
#include <cassert>
#include <cstdio>
#include <memory>
#include <map>
using std::floorf;
void convertToJson(const Money& m,JsonVariant json){json.set((double)m);}
void convertFromJson(JsonVariantConst json,String& s){s=json.as<const char*>();}
struct Request {
  std::map<std::string,String> args;
  String arg(const String& key)const{auto i=args.find(key.c_str());return i==args.end()?String():i->second;}
  bool hasArg(const String& key)const{return args.count(key.c_str());}
} server;
struct SerialMock {void println(const char*){}} Serial;
int dataMutex=0,prefsMutex=0;constexpr int portMAX_DELAY=0;
constexpr int MIN_REFRESH=10;
constexpr int DEFAULT_REFRESH=60;
template<class T>T constrain(T value,T minimum,T maximum){return std::min(maximum,std::max(minimum,value));}
struct PreferencesMock {
  std::map<std::string,String> data;
  void begin(const char*,bool){}void end(){}
  bool isKey(const char* key){return data.count(key);}
  String getString(const char* key,const char* fallback){return data.count(key)?data[key]:String(fallback);}
  int getInt(const char* key,int fallback){return data.count(key)?data[key].toInt():fallback;}
  float getFloat(const char* key,float fallback){return data.count(key)?data[key].toFloat():fallback;}
  bool getBool(const char* key,bool fallback){return data.count(key)?data[key]=="1":fallback;}
} prefs;
void xSemaphoreTake(int,int){} void xSemaphoreGive(int){}
bool result=false;String error;
void sendMutationResult(bool ok,const String& message,const char* = "/"){result=ok;error=message;}
time_t parseDateYMD(const String& date){return parseCalendarDate(date.c_str());}
#include "mutations-under-test.h"

void add(const char* rid,const char* quantity="1",const char* date="2026-01-01"){
  server.args={{"rid",rid},{"base",String(persistentStore.revision())},{"lt","BTC-USD"},{"ld",date},{"lq",quantity},{"lp","1000"}};handleAddLot();
}
int main(){
  setenv("TZ","CET-1CEST,M3.5.0/2,M10.5.0/3",1);tzset();assert(persistentStore.begin());
  tickerCount=1;tickerData[0].sym="BTC-USD";
  add("request-00000001");assert(result&&tickerData[0].lotCount==1&&tickerData[0].holdings==1);
  auto firstRequest=server.args;handleAddLot();assert(result&&tickerData[0].lotCount==1);
  server.args["lq"]="2";handleAddLot();assert(!result&&tickerData[0].holdings==1);
  add("request-00000002","2","2026-01-02");assert(result);
  add("request-00000003","3","2026-01-03");assert(result);
  Lot target=tickerData[0].lots[1];uint32_t otherId=tickerData[0].lots[2].id;
  server.args={{"rid","delete-000000001"},{"base",String(persistentStore.revision())},{"lt","BTC-USD"},{"lk",String(target.id)},{"lv",lotVersion(target)}};
  handleDelLot();assert(result&&tickerData[0].holdings==4);handleDelLot();assert(result&&tickerData[0].holdings==4&&tickerData[0].lots[1].id==otherId);
  auto before=tickerData[0];uint32_t revision=persistentStore.revision(),next=nextLotId;
  LittleFS.failWrite=true;add("request-00000004","5");assert(!result&&tickerData[0].lotCount==before.lotCount&&tickerData[0].holdings==before.holdings&&nextLotId==next&&persistentStore.revision()==revision);LittleFS.failWrite=false;
  handleAddLot();assert(result&&tickerData[0].holdings==9); // Failed persistence left no false receipt.
  auto id=tickerData[0].lots[0].id;String originalVersion=lotVersion(tickerData[0].lots[0]);
  server.args={{"rid","edit-00000000001"},{"base",String(persistentStore.revision())},{"lt","BTC-USD"},{"lo","BTC-USD"},{"lk",String(id)},{"lv",originalVersion},{"ld","2026-01-01"},{"lq","2"},{"lp","1000"}};
  handleEditLot();assert(result&&tickerData[0].lots[0].id==id);auto edited=tickerData[0].holdings;
  server.args["rid"]="edit-00000000002";server.args["base"]=String(persistentStore.revision());handleEditLot();assert(!result&&tickerData[0].holdings==edited);
  server.args=firstRequest;server.args["rid"]="request-old-page";handleAddLot();assert(!result&&tickerData[0].holdings==edited);
  add("request-00000005","0.0000000001");assert(result);int lots=tickerData[0].lotCount;
  manualCount=1;manualAssets[0].name="Toyota Bank";assert(recordManualValuation(manualAssets[0],parseCalendarDate("2026-01-01"),76171.42,1262.77));assert(savePrefs());
  String manualOriginal=manualVersion(manualAssets[0]);
  server.args={{"op","savings"},{"id","0"},{"version",manualOriginal},{"date","2026-01-02"},{"deposit","5000"},{"interest","250"}};
  LittleFS.failRename=true;handleManualAction();assert(!result&&manualAssets[0].valuePLN.cents==7617142);LittleFS.failRename=false;
  handleManualAction();assert(result&&manualAssets[0].valuePLN.cents==8142142);
  // Reload the actual new persisted state; receipts and tiny quantities survive.
  tickerCount=0;manualCount=0;receiptCount=0;assert(loadPersistentState());
  assert(tickerCount==1&&tickerData[0].lotCount==lots&&manualAssets[0].valuePLN.cents==8142142&&manualAssets[0].gainPLN.cents==151277);
  bool tiny=false;for(int k=0;k<lots;++k)tiny=tiny||tickerData[0].lots[k].qty==1e-10f;assert(tiny);
  server.args=firstRequest;handleAddLot();assert(result&&tickerData[0].lotCount==lots);
  // A settings failure must restore the old settings and all financial data.
  server.args={{"tickers","BTC-USD"},{"refresh","90"},{"bright","180"},{"nightfr","0"},{"nightto","8"},{"range","1d"}};
  LittleFS.failWrite=true;handleSave();assert(!result&&cfg.refreshSec==60&&tickerData[0].lotCount==lots&&manualAssets[0].valuePLN.cents==8142142);LittleFS.failWrite=false;
  // Exercise the actual legacy NVS reader, not a hand-written migration fixture.
  LittleFS.files.clear();persistentStore=PersistentStore{};assert(persistentStore.begin());
  tickerCount=manualCount=receiptCount=0;nextLotId=1;
  prefs.data={{"tcount","1"},{"t0","BTC-USD"},{"lt0","1767265200:1e-10:200000"},{"mcount","2"},
    {"mn0","Toyota Bank"},{"mv0","1234567.89"},{"mg0","1262.77"},{"mh0","1767265200:1234567.89:1262.77"},
    {"mn1","PPK"},{"mp1","1"},{"mv1","1194.93"},{"mg1","23.44"},{"mh1","1767265200:1194.93:23.44"}};
  auto originalNvs=prefs.data;loadPrefs();
  assert(tickerCount==1&&tickerData[0].lots[0].qty==1e-10f&&manualCount==2);
  assert(manualAssets[0].valuePLN.cents==123456789&&manualAssets[0].gainPLN.cents==126277&&manualAssets[1].ppk&&manualAssets[1].valuePLN.cents==119493);
  assert(savePrefs()&&prefs.data==originalNvs);assert(loadPersistentState()&&manualAssets[0].valuePLN.cents==123456789);
  // Full advertised capacity goes through the real serializer and reader.
  tickerCount=MAX_TICKERS;manualCount=MAX_MANUAL_ASSETS;
  for(int i=0;i<tickerCount;++i){tickerData[i]=TickerState{};tickerData[i].sym="TEST"+String(i);tickerData[i].lotCount=MAX_LOTS;
    for(int k=0;k<MAX_LOTS;++k)tickerData[i].lots[k]={parseCalendarDate("2026-01-01")+k*86400,1e-10f,326656.44f,nextLotId++};refreshHoldings(tickerData[i]);}
  for(int i=0;i<manualCount;++i){manualAssets[i]=ManualAsset{};manualAssets[i].name="Savings "+String(i);
    for(int k=0;k<MAX_MANUAL_SNAPSHOTS;++k)assert(recordManualValuation(manualAssets[i],parseCalendarDate("2026-01-01")+k*86400,Money::fromCents(123456789+k),Money::fromCents(126277+k)));}
  assert(savePrefs()&&loadPersistentState()&&tickerCount==8&&manualCount==4&&tickerData[7].lotCount==60&&manualAssets[3].historyCount==60);
  assert(prefs.data==originalNvs);
  puts("PASS: actual transaction IDs, duplicate/lost-response retries, stale edits/pages, real persistence rollback, settings rollback, integer money and reboot round-trip of IDs/receipts/tiny quantities.");
}
