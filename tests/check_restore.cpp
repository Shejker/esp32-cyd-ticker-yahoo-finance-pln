#include <Arduino.h>
#include <WebServer.h>
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
WebServer server;
struct SerialMock {void println(const char*){}} Serial;
int dataMutex=0,prefsMutex=0;constexpr int portMAX_DELAY=0;
constexpr int MIN_REFRESH=10,DEFAULT_REFRESH=60;
template<class T>T constrain(T value,T minimum,T maximum){return std::min(maximum,std::max(minimum,value));}
struct PreferencesMock {
  void begin(const char*,bool){}void end(){}
  bool isKey(const char*){return false;}
  String getString(const char*,const char* fallback){return fallback;}
  int getInt(const char*,int fallback){return fallback;}
  float getFloat(const char*,float fallback){return fallback;}
  bool getBool(const char*,bool fallback){return fallback;}
} prefs;
void xSemaphoreTake(int,int){}void xSemaphoreGive(int){}
void sendMutationResult(bool ok,const String& error,const char* = "/"){
  JsonDocument doc;doc["ok"]=ok;if(!ok)doc["error"]=error;String data;serializeJson(doc,data);server.send(ok?200:400,"application/json",data);
}
time_t parseDateYMD(const String& date){return parseCalendarDate(date.c_str());}
#include "mutations-under-test.h"
unsigned long clockMs=1;
unsigned long millis(){return clockMs;}
uint32_t esp_random(){static uint32_t next=123456789;return ++next;}
#include "../CYDTicker/backup_restore.h"
#include "backups-under-test.h"

JsonDocument reply(){JsonDocument doc;assert(!deserializeJson(doc,server.html));return doc;}
void uploadBody(const std::string& json,bool abort=false){
  auto& upload=server.incoming;upload.status=UPLOAD_FILE_START;handleRestoreUpload();
  for(size_t i=0;i<json.size();i+=137){upload.status=UPLOAD_FILE_WRITE;upload.currentSize=std::min<size_t>(137,json.size()-i);std::memcpy(upload.buf,json.data()+i,upload.currentSize);handleRestoreUpload();}
  upload.status=abort?UPLOAD_FILE_ABORTED:UPLOAD_FILE_END;upload.totalSize=json.size();handleRestoreUpload();
}
JsonDocument preview(const std::string& json,const char* kind="transactions",bool abort=false){
  server.args={{"kind",kind},{"base",String(persistentStore.revision())}};uploadBody(json,abort);handleRestorePreview();return reply();
}
void commit(const JsonDocument& doc,const char* rid="restore-000000001"){
  server.args={{"format","json"},{"restoreKind",doc["kind"].as<String>()},{"restoreToken",doc["token"].as<String>()},{"base",String(doc["revision"].as<uint32_t>())},{"rid",rid},{"confirm","replace"}};handleRestoreCommit();
}
int main(){
  setenv("TZ","CET-1CEST,M3.5.0/2,M10.5.0/3",1);tzset();assert(persistentStore.begin());
  tickerCount=1;tickerData[0].sym="BTC-USD";tickerData[0].alertHigh=123;
  tickerData[0].lots[0]={parseCalendarDate("2026-01-01"),0.5f,200000,1};tickerData[0].lotCount=1;nextLotId=2;refreshHoldings(tickerData[0]);
  manualCount=1;manualAssets[0].name="Toyota Bank";assert(recordManualValuation(manualAssets[0],parseCalendarDate("2026-01-01"),76171.42,1262.77));assert(savePrefs());
  uint32_t originalRevision=persistentStore.revision();
  const std::string transactions=R"JSON([
    {"symbol":"BTC-USD","date":"2026-02-01","qty":-0.25,"pricePLN":250000},
    {"symbol":"BTC-USD","date":"2026-01-01","qty":0.5,"pricePLN":200000},
    {"symbol":"WEBN.DE","date":"2026-01-01","qty":1e-10,"pricePLN":48.06}
  ])JSON";
  auto summary=preview(transactions);assert(summary["ok"]&&summary["count"]==3&&summary["replacedCount"]==1&&summary["names"].size()==2);
  assert(persistentStore.revision()==originalRevision&&tickerCount==1&&tickerData[0].lotCount==1&&manualAssets[0].valuePLN.cents==7617142);
  // Explicit confirmation, token and revision are all required.
  server.args={{"format","json"},{"restoreKind","transactions"},{"restoreToken",summary["token"].as<String>()},{"base",String(originalRevision)},{"rid","restore-unconfirmed"}};handleRestoreCommit();assert(!reply()["ok"]&&tickerCount==1);
  LittleFS.failRename=true;commit(summary);assert(!reply()["ok"]&&persistentStore.revision()==originalRevision&&tickerCount==1&&tickerData[0].lotCount==1&&nextLotId==2);LittleFS.failRename=false;
  commit(summary);if(!reply()["ok"])std::fprintf(stderr,"Restore response: %s\n",server.html.c_str());
  assert(reply()["ok"]);assert(tickerCount==2);assert(tickerData[0].lotCount==2);assert(tickerData[0].holdings==0.25f);assert(tickerData[0].alertHigh==123);assert(tickerData[1].lots[0].qty==1e-10f);assert(manualAssets[0].valuePLN.cents==7617142);
  auto committedArgs=server.args;uint32_t committedRevision=persistentStore.revision();
  handleRestoreCommit();assert(reply()["ok"]&&persistentStore.revision()==committedRevision); // lost response retry after temp file deletion
  assert(loadPersistentState());server.args=committedArgs;handleRestoreCommit();assert(reply()["ok"]&&persistentStore.revision()==committedRevision); // after reboot too
  server.args["restoreKind"]="savings-ppk";handleRestoreCommit();assert(!reply()["ok"]); // reused ID with another payload
  // Actual exports round-trip, including small quantities and large balances.
  handleApiTransactions();std::string exportedTransactions=server.html;
  summary=preview(exportedTransactions);assert(summary["ok"]);commit(summary,"restore-roundtrip-tx");assert(reply()["ok"]&&tickerData[1].lots[0].qty==1e-10f);
  manualAssets[0]=ManualAsset{};manualAssets[0].name="Toyota Bank";assert(recordManualValuation(manualAssets[0],parseCalendarDate("2026-01-01"),1234567.89,1262.77));assert(savePrefs());
  handleApiSavingsPPK();std::string exportedSavings=server.html;
  summary=preview(exportedSavings,"savings-ppk");assert(summary["ok"]&&summary["valuations"]==1);
  commit(summary,"restore-roundtrip-ppk");assert(reply()["ok"]&&manualAssets[0].valuePLN.cents==123456789&&manualAssets[0].gainPLN.cents==126277&&tickerData[0].lotCount==2);
  uint32_t revision=persistentStore.revision();
  for(const std::string& invalid:{std::string("{}"),std::string("[{},]"),std::string("["),std::string("[] trailing"),std::string("[42]"),std::string("[{\"symbol\":\"BTC-USD\",\"date\":\"2026-01-01\",\"qty\":-1,\"pricePLN\":1}]"),std::string("[{\"symbol\":\"BTC-USD\",\"date\":\"2026-02-30\",\"qty\":1,\"pricePLN\":1}]"),std::string("[{\"symbol\":\"BTC-USD\",\"date\":\"2099-01-01\",\"qty\":1,\"pricePLN\":1}]")}){
    assert(!preview(invalid)["ok"]);assert(persistentStore.revision()==revision&&tickerData[0].lotCount==2&&manualAssets[0].valuePLN.cents==123456789);
  }
  for(const char* qty:{"\"1\"","\"N:1\"","true","null","01","1.","1e","1e999","1e-999","0","-0"}){
    std::string invalid="[{\"symbol\":\"BTC-USD\",\"date\":\"2026-01-01\",\"pricePLN\":1,\"qty\":";invalid+=qty;invalid+="}]";
    assert(!preview(invalid)["ok"]);
  }
  assert(!preview(exportedSavings)["ok"]);assert(!preview(exportedTransactions,"savings-ppk")["ok"]);
  assert(!preview(transactions,"transactions",true)["ok"]);
  assert(!preview(std::string(65537,' '))["ok"]);
  server.args={{"kind","transactions"},{"base",String(revision)}};uploadBody(transactions);uploadBody(transactions);handleRestorePreview();assert(!reply()["ok"]); // multiple files
  summary=preview(transactions);assert(summary["ok"]);clockMs+=600001;commit(summary,"restore-expired-001");assert(!reply()["ok"]);
  summary=preview(transactions);assert(summary["ok"]);assert(savePrefs());commit(summary,"restore-stale-00001");assert(!reply()["ok"]);
  summary=preview(transactions);assert(summary["ok"]);File corrupt=LittleFS.open(RESTORE_FILE,"r+");uint8_t byte='x';assert(corrupt.write(&byte,1)==1);corrupt.close();commit(summary,"restore-corrupt-001");assert(!reply()["ok"]);
  // Empty arrays clear only the explicitly selected category.
  summary=preview("[]","savings-ppk");assert(summary["ok"]&&summary["count"]==0);commit(summary,"restore-empty-ppk01");assert(reply()["ok"]&&manualCount==0&&tickerData[0].lotCount==2);
  summary=preview("[]");assert(summary["ok"]);commit(summary,"restore-empty-tx001");assert(reply()["ok"]&&tickerCount==2&&tickerData[0].lotCount==0&&tickerData[1].lotCount==0&&loadPersistentState());
  // Strict cents, chronology, duplicate accounts and inconsistent last values.
  const char* ppk=R"JSON([{"name":"PPK","kind":"ppk","valuePLN":1194.93,"gainPLN":23.44,"history":[{"ts":1767265200,"valuePLN":1194.93,"gainPLN":23.44}]}])JSON";
  summary=preview(ppk,"savings-ppk");assert(summary["ok"]);commit(summary,"restore-ppk-valid01");assert(reply()["ok"]&&manualAssets[0].ppk&&manualAssets[0].gainPLN.cents==2344);
  for(const char* invalid:{R"JSON([{"name":"PPK","valuePLN":1.001,"gainPLN":0,"history":[]}])JSON",R"JSON([{"name":"PPK","kind":"other","valuePLN":1,"gainPLN":0,"history":[]}])JSON",R"JSON([{"name":"PPK","valuePLN":1,"gainPLN":0,"history":[]},{"name":"PPK","valuePLN":1,"gainPLN":0,"history":[]}])JSON",R"JSON([{"name":"PPK","valuePLN":1,"gainPLN":0,"history":[{"ts":1767265200,"valuePLN":2,"gainPLN":0}]}])JSON"})assert(!preview(invalid,"savings-ppk")["ok"]);
  manualAssets[0]=ManualAsset{};manualAssets[0].name="Savings \"quoted\" \\ bank";
  assert(recordManualValuation(manualAssets[0],parseCalendarDate("2026-01-01"),Money::fromCents(Money::MAX_CENTS-1),Money::fromCents(-Money::MAX_CENTS+1)));assert(savePrefs());
  handleApiSavingsPPK();summary=preview(server.html,"savings-ppk");assert(summary["ok"]);commit(summary,"restore-max-money01");assert(reply()["ok"]&&manualAssets[0].valuePLN.cents==Money::MAX_CENTS-1&&manualAssets[0].gainPLN.cents==-Money::MAX_CENTS+1&&manualAssets[0].name=="Savings \"quoted\" \\ bank");
  // Maximum exported capacity is parsed one row at a time without truncation.
  tickerCount=MAX_TICKERS;manualCount=MAX_MANUAL_ASSETS;
  for(int i=0;i<tickerCount;++i){tickerData[i]=TickerState{};tickerData[i].sym="TEST"+String(i);tickerData[i].lotCount=MAX_LOTS;
    for(int k=0;k<MAX_LOTS;++k)tickerData[i].lots[k]={parseCalendarDate("2026-01-01")+k*86400,1e-10f,326656.44f,nextLotId++};refreshHoldings(tickerData[i]);}
  for(int i=0;i<manualCount;++i){manualAssets[i]=ManualAsset{};manualAssets[i].name="Savings "+String(i);
    for(int k=0;k<MAX_MANUAL_SNAPSHOTS;++k)assert(recordManualValuation(manualAssets[i],parseCalendarDate("2026-01-01")+k*86400,Money::fromCents(123456789+k),Money::fromCents(126277+k)));}
  assert(savePrefs());handleApiTransactions();exportedTransactions=server.html;assert(exportedTransactions.size()<MAX_BACKUP_BYTES);
  summary=preview(exportedTransactions);assert(summary["ok"]&&summary["count"]==480);commit(summary,"restore-max-tx00001");assert(reply()["ok"]&&loadPersistentState()&&tickerData[7].lotCount==60);
  handleApiSavingsPPK();exportedSavings=server.html;summary=preview(exportedSavings,"savings-ppk");assert(summary["ok"]&&summary["valuations"]==240);commit(summary,"restore-max-ppk0001");assert(reply()["ok"]&&loadPersistentState()&&manualAssets[3].historyCount==60&&manualAssets[3].valuePLN.cents==123456848);
  puts("PASS: actual streaming restore preview/confirmation, export round-trip, category isolation, validation, maximum capacity, CRC, expiry, stale data, rollback and idempotent/reboot retries.");
}
