// Real quote publication, FX scheduling and network priority, with deterministic
// HTTP/time/FreeRTOS fixtures. No physical ESP32 or Yahoo writes are involved.
#include <Arduino.h>
#include <ArduinoJson.h>
#include "../CYDTicker/investment_math.h"
#include <algorithm>
#include <atomic>
#include <cassert>
#include <cstdarg>
#include <cstdio>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>
using std::min;
using std::isfinite;
#include "startup-models-under-test.h"

TickerState tickerData[MAX_TICKERS]{};
ExRate exchangeRates[MAX_EXCHANGE_RATES]{};
int tickerCount=0,rateCount=0;
AppConfig cfg={60,200,true,false,false,0,8,"1d"};
std::atomic<bool> fetchPending{true},fetching{false},redrawPending{false};
std::atomic<unsigned long> lastFetchMillis{0};
std::atomic<time_t> lastFetchTime{0};
constexpr time_t NTP_SYNC_MIN_EPOCH=1700000000,MAX_QUOTE_AGE=7*86400;
constexpr int networkMutex=1,dataMutex=2,portMAX_DELAY=-1,WL_CONNECTED=3;
#define pdMS_TO_TICKS(ms) (ms)
uint32_t clockMs=100;
time_t clockTime=1791020000;
time_t testTime(time_t* out) {if(out)*out=clockTime;return clockTime;}
#define time testTime
unsigned long millis(){return clockMs;}
struct FakeWiFi{bool online=true;int status(){return online?WL_CONNECTED:0;}} WiFi;
struct FakeSerial{void printf(const char*,...) {}} Serial;
bool networkHeld=false,arrivalWhileWaiting=false;
int acquireCalls=0,releases=0,priorityWaits=0,clearPriorityAfter=0,sorts=0,alerts=0;
struct StopTask{};
std::vector<std::set<std::string>> frames;
std::vector<std::string> requests;
int xSemaphoreTake(int mutex,int){
  if(mutex!=networkMutex)return 1;
  assert(!networkHeld);networkHeld=true;++acquireCalls;
  if(arrivalWhileWaiting){arrivalWhileWaiting=false;fetchPending=true;}
  return 1;
}
void xSemaphoreGive(int mutex){if(mutex==networkMutex){assert(networkHeld);networkHeld=false;++releases;}}
void vTaskDelay(unsigned long ms){
  clockMs+=(uint32_t)ms;
  if(ms==50){++priorityWaits;if(clearPriorityAfter&&priorityWaits>=clearPriorityAfter){fetchPending=false;fetching=false;}}
  else if(ms==800){
    assert(fetching&&redrawPending);redrawPending=false;
    std::set<std::string> ready;
    for(int i=0;i<rateCount;++i)if(exchangeRates[i].rate>0)ready.insert(exchangeRates[i].curr.c_str());
    frames.push_back(ready);
  }else if(ms==200)throw StopTask{}; // One real refresh cycle, then stop its infinite loop.
}
int getIndexBySym(const String& name){for(int i=0;i<tickerCount;++i)if(tickerData[i].sym==name)return i;return -1;}
bool quoteUsable(const Quote& quote){return quote.valid&&freshTimestamp(quote.asOf,clockTime,MAX_QUOTE_AGE);}
String normalizeCurrency(String currency){if(currency!="GBp")currency.toUpperCase();return currency;}
String urlEncode(const String& value){return value;}
void setLED(bool,bool,bool){}
void sortTickersIfNeeded(){++sorts;}
void checkAlerts(){++alerts;}
struct WiFiClientSecure{
  bool insecure=false;unsigned long handshake=120;
  void setInsecure(){insecure=true;}
  void setHandshakeTimeout(unsigned long seconds){handshake=seconds;}
};
struct Fixture{String currency;float price=100;int status=200;};
std::map<std::string,Fixture> fixtures;
struct HTTPClient{
  WiFiClientSecure* client=nullptr;
  std::string symbol;
  int timeout=0,connectTimeout=0;
  void begin(WiFiClientSecure& source,const String& url){
    client=&source;std::string text=url.c_str();size_t start=text.find("/chart/")+7;symbol=text.substr(start,text.find('?',start)-start);
  }
  void setTimeout(int value){timeout=value;}
  void setConnectTimeout(int value){connectTimeout=value;}
  void setUserAgent(const char*){}
  void collectHeaders(const char**,int){}
  int GET(){
    assert(networkHeld&&client&&client->insecure&&client->handshake==10);
    assert(connectTimeout==5000 && (timeout==5000||timeout==8000));
    assert(fetching);requests.push_back(symbol);clockMs+=100;
    return fixtures.at(symbol).status;
  }
  void end(){}
};
DeserializationError readYahooJson(HTTPClient& http,JsonDocument& doc,const JsonDocument&){
  Fixture f=fixtures.at(http.symbol);auto meta=doc["chart"]["result"][0]["meta"];
  meta["currency"]=f.currency;meta["regularMarketPrice"]=f.price;meta["regularMarketTime"]=clockTime;
  meta["chartPreviousClose"]=f.price*.99f;
  doc["chart"]["result"][0]["indicators"]["quote"][0]["close"].add(f.price);
  return DeserializationError::Ok;
}
#include "startup-fetch-under-test.h"

void reset(){
  for(auto& t:tickerData)t=TickerState{};for(auto& r:exchangeRates)r=ExRate{};
  cfg.chartRange="1d";tickerCount=rateCount=0;fetchPending=true;fetching=redrawPending=false;
  clockMs=100;clockTime=1791020000;WiFi.online=true;
  networkHeld=arrivalWhileWaiting=false;acquireCalls=releases=priorityWaits=clearPriorityAfter=sorts=alerts=0;
  lastFetchMillis=0;lastFetchTime=0;frames.clear();requests.clear();fixtures.clear();
}
void ticker(const char* symbol,const char* currency){
  auto& t=tickerData[tickerCount++];t.sym=symbol;t.quote.sym=symbol;
  fixtures[symbol]={currency,100,200};
}
void cycle(){try{fetchTask(nullptr);assert(false);}catch(const StopTask&){}assert(!networkHeld);}
int main(){
  reset();fetchPending=false;
  {NetworkLock history;assert(networkHeld&&acquireCalls==1);}assert(!networkHeld&&releases==1);
  reset();fetchPending=true;clearPriorityAfter=3;
  {NetworkLock history;assert(priorityWaits==3&&networkHeld);}assert(!networkHeld);
  reset();fetchPending=false;fetching=true;clearPriorityAfter=2;
  {NetworkLock history;assert(priorityWaits==2&&networkHeld);}assert(!networkHeld);
  reset();fetchPending=false;arrivalWhileWaiting=true;clearPriorityAfter=2;
  {NetworkLock history;assert(acquireCalls==2&&releases==1&&priorityWaits==2);}assert(releases==2);
  reset();fetchPending=true;fetching=true;
  {NetworkLock live(true);assert(networkHeld&&priorityWaits==0);}assert(!networkHeld);

  reset();ticker("WEBN.DE","EUR");ticker("BTC-USD","USD");ticker("ANAV.DE","EUR");ticker("LOCAL","PLN");
  fixtures["EURPLN=X"]={"PLN",4.25f,200};fixtures["USDPLN=X"]={"PLN",3.85f,200};
  cycle();
  assert((requests==std::vector<std::string>{"WEBN.DE","EURPLN=X","BTC-USD","USDPLN=X","ANAV.DE","LOCAL"}));
  assert(frames.size()==4&&frames[0].count("EUR")&&!frames[0].count("USD"));
  assert(frames[1].count("USD")&&rateCount==2&&sorts==1&&alerts==1);
  assert(!fetching&&!fetchPending&&redrawPending&&lastFetchMillis==clockMs-200&&lastFetchTime==clockTime);
  for(int i=0;i<tickerCount;++i)assert(tickerData[i].quote.valid&&tickerData[i].quote.errors==0);

  reset();ticker("WEBN.DE","EUR");ticker("ANAV.DE","EUR");
  fixtures["EURPLN=X"]={"PLN",0,503};
  tickerData[0].quote.currency="EUR";tickerData[0].quote.valid=true;tickerData[0].quote.price=88;tickerData[0].quote.asOf=clockTime;
  fixtures["WEBN.DE"].status=503;
  exchangeRates[rateCount++]={"EUR",4.1f,clockTime};exchangeRates[rateCount++]={"JPY",.02f,clockTime};
  cycle();
  assert((requests==std::vector<std::string>{"WEBN.DE","EURPLN=X","ANAV.DE"}));
  assert(tickerData[0].quote.valid&&tickerData[0].quote.price==88&&tickerData[0].quote.errors==1);
  assert(rateCount==1&&exchangeRates[0].curr=="EUR"&&exchangeRates[0].rate==4.1f);
  assert(frames[0].count("EUR")&&redrawPending); // No zero/invalid replacement of a working cache.

  reset();ticker("PENCE","GBp");ticker("POUNDS","GBP");fixtures["GBPPLN=X"]={"PLN",5,200};
  cycle();assert(rateCount==2&&exchangeRates[0].curr=="GBp"&&std::abs(exchangeRates[0].rate-.05f)<1e-6);
  assert(exchangeRates[1].curr=="GBP"&&exchangeRates[1].rate==5);

  reset();
  const char* currencies[]={"EUR","USD","CHF","JPY","GBP","AUD","CAD","SEK"};
  for(int i=0;i<MAX_TICKERS;++i){
    String symbol="T"+String(i),old="OLD"+String(i);ticker(symbol.c_str(),currencies[i]);
    tickerData[i].quote.valid=true;tickerData[i].quote.currency=old;tickerData[i].quote.asOf=clockTime;
    exchangeRates[rateCount++]={old,.5f,clockTime};fixtures[std::string(currencies[i])+"PLN=X"]={"PLN",2,200};
  }
  cycle();assert(rateCount==MAX_EXCHANGE_RATES&&requests.size()==16);
  for(int i=0;i<rateCount;++i)assert(exchangeRates[i].curr==currencies[i]&&exchangeRates[i].rate==2);

  reset();ticker("ANAV.DE","EUR");WiFi.online=false;cycle();
  assert(fetchPending&&!fetching&&requests.empty()&&lastFetchTime==0);
  WiFi.online=true;clockTime=0;cycle();assert(fetchPending&&requests.empty());
  clockTime=1791020000;fixtures["EURPLN=X"]={"PLN",4.25f,200};cycle();
  assert(!fetchPending&&tickerData[0].quote.valid); // Retry immediately when Wi-Fi and clock are ready.
  puts("PASS: real live-quote priority, 10-second TLS setup, Wi-Fi/NTP startup gating, progressive quote/PLN publication, FX deduplication, failure cache retention and timing state.");
}
