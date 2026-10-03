// Compile the actual position API and timeline. External prices and locks are
// fixtures; no writes are made to an ESP32 or a financial account.
#include <Arduino.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include "../CYDTicker/investment_math.h"
#include <cassert>
#include <memory>
#include <vector>
using std::min;
using std::isfinite;

WebServer server;
int dataMutex=0;constexpr int portMAX_DELAY=0;
void xSemaphoreTake(int,int){} void xSemaphoreGive(int){}
#include "portfolio-models-under-test.h"
TickerState tickerData[MAX_TICKERS]{};int tickerCount=1;
ManualAsset manualAssets[MAX_MANUAL_ASSETS]{};int manualCount=1;
std::vector<std::string> requests;
bool failFX=false,missingFirst=false;
int getIndexBySym(const String& sym){for(int i=0;i<tickerCount;++i)if(tickerData[i].sym==sym)return i;return -1;}
float getRateToPLN(const String&){return 4;}
time_t rateStamp=time(nullptr)-86400;
time_t getRateAsOf(const String&){return rateStamp;}
bool quoteUsable(const Quote& q){return q.valid&&freshTimestamp(q.asOf,time(nullptr),7*86400);}
String urlEncode(const String& s){return s;}
bool fetchHistoryCloses(const String& sym,const time_t* dates,int count,float* prices,String& currency,bool& weekly,String& failure,time_t* sources=nullptr){
  requests.push_back(sym.c_str());weekly=false;currency="EUR";
  bool fx=sym=="EURPLN=X";
  for(int i=0;i<count;++i){prices[i]=fx?4:20;if(sources)sources[i]=dates[i]-(fx?2:1)*86400;}
  if(missingFirst&&!fx&&count)prices[0]=NAN;
  if(failFX&&fx){failure="EURPLN=X: HTTP 429";return false;}
  return count>0;
}
#include "portfolio-api-under-test.h"

JsonDocument request(const char* kind,const char* name){
  server.args={{"kind",kind},{"name",name}};requests.clear();HistoryResponse response;buildPortfolioPosition(name,kind,response);server.status=response.status;server.html=response.body.c_str();
  JsonDocument result;assert(!deserializeJson(result,server.html));return result;
}
int main(){
  setenv("TZ","CET-1CEST,M3.5.0/2,M10.5.0/3",1);tzset();
  time_t now=time(nullptr),purchase=now-7*86400;
  tickerData[0].sym="BTC-USD";tickerData[0].lotCount=1;tickerData[0].lots[0]={purchase,2,100};
  tickerData[0].quote.valid=true;tickerData[0].quote.price=10000;
  tickerData[0].quote.asOf=now;
  manualAssets[0].name="Toyota Bank";manualAssets[0].historyCount=1;
  manualAssets[0].history[0]={now-500*86400,76171.42f,1262.77f};
  for(const char* symbol:{"SXR8.DE","VWCE.DE"}){
    auto result=request("benchmark",symbol);
    assert(server.status==200&&result["ok"].as<bool>());
    auto points=result["points"].as<JsonArray>();assert(points.size()>1);
    assert(points[0][0].as<long>()>=historyDayEnd(purchase)); // no savings dates in benchmark history
    assert(points[points.size()-1][1].as<double>()==80); // ETF unit price in PLN, not a held-ticker value
    assert(points[points.size()-1][2].as<double>()==0);
    assert(points[0].size()==5);
    assert(points[0][3].as<time_t>()==points[0][0].as<time_t>()-86400);
    assert(points[0][4].as<time_t>()==points[0][0].as<time_t>()-2*86400);
    assert(requests.size()==2&&requests[0]==symbol&&requests[1]=="EURPLN=X");
  }
  missingFirst=true;auto missing=request("benchmark","SXR8.DE");
  assert(missing["points"][0][1].isNull());missingFirst=false;
  failFX=true;auto failed=request("benchmark","SXR8.DE");
  assert(!failed["ok"].as<bool>()&&failed["error"].as<std::string>()=="EURPLN=X: HTTP 429");
  auto failedPoints=failed["points"].as<JsonArray>();assert(failedPoints[failedPoints.size()-1][1].isNull());
  failFX=false;
  request("benchmark","ARBITRARY");assert(server.status==404&&requests.empty());
  request("ticker","SXR8.DE");assert(server.status==404&&requests.empty());
  auto manual=request("manual","Toyota Bank");assert(server.status==200&&requests.empty());
  auto points=manual["points"].as<JsonArray>();assert(std::abs(points[points.size()-1][1].as<double>()-76171.42)<.01);
  assert(points[0].size()==3); // Manual balances do not pretend to be market quotes.
  auto live=request("ticker","BTC-USD");auto livePoints=live["points"].as<JsonArray>();
  assert(livePoints[livePoints.size()-1][3].as<time_t>()==tickerData[0].quote.asOf);
  assert(livePoints[livePoints.size()-1][4].as<time_t>()==getRateAsOf("EUR"));
  puts("PASS: actual benchmark API allowlist, ticker-only timeline, PLN/FX conversion, price gaps, unavailable FX and unaffected savings history.");
}
