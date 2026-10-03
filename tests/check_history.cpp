// Compiles the firmware's actual fetchHistoryCloses with decoded HTTP fixtures
// and the real ArduinoJson library. No ESP32 or external requests are used.
#include <Arduino.h>
#include <ArduinoJson.h>
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <iostream>
#include <iterator>
#include <vector>
#include "../CYDTicker/investment_math.h"
using std::min;
using std::isfinite;

struct FakeSerial { template<class... Args> void printf(const char*, Args...) {} } Serial;
static int responseStatus = 200;
static std::string responseBody;
static int fetchCount=0;
unsigned long millis(){return 100;}
struct NetworkLock{~NetworkLock(){}};
struct WiFiClientSecure {};
struct HTTPClient {
  void begin(const String&) {}
  void setTimeout(int) {}
  void setUserAgent(const String&) {}
  int GET() { ++fetchCount;return responseStatus; }
  std::string getString() { return responseBody; }
  void end() {}
};
String urlEncode(const String& value) { return value; }
void prepareYahooRequest(HTTPClient&,WiFiClientSecure&,const String&,int){}
DeserializationError readYahooJson(HTTPClient& http,JsonDocument& doc,const JsonDocument& filter){return deserializeJson(doc,http.getString(),DeserializationOption::Filter(filter));}
#include "history-under-test.h"

int main(int argc,char** argv) {
  if(argc>1){
    responseBody=std::string(std::istreambuf_iterator<char>(std::cin),{});
    time_t realDates[]={parseCalendarDate("2025-12-12"),time(nullptr)};
    float realPrices[2];String realCurrency,realFailure;bool realWeekly=false;
    bool ok=fetchHistoryCloses(argv[1],realDates,2,realPrices,realCurrency,realWeekly,realFailure);
    if(!ok||!std::isfinite(realPrices[1]))fprintf(stderr,"Current price unavailable for %s: %s\n",argv[1],realFailure.c_str());
    assert(ok&&std::isfinite(realPrices[1])&&realPrices[1]>0);
    printf("PASS: real Yahoo history for %s has a usable independent endpoint: %.6f %s.\n",argv[1],realPrices[1],realCurrency.c_str());return 0;
  }
  time_t dates[] = {1767225600,1767312000,1767398400};
  float prices[3]; String currency, failure; bool weekly=false;
  time_t sources[3]{};
  const char* fixtures[] = {"EUR","USD","GBp","PLN"};
  for(const char* native:fixtures) {
    responseBody=std::string("{\"chart\":{\"result\":[{\"meta\":{\"currency\":\"")+native+"\"},\"timestamp\":[1767312000,1767355200,1767398400],\"indicators\":{\"quote\":[{\"close\":[10,null,12]}]}}],\"error\":null}}";
    assert(fetchHistoryCloses("BTC-USD",dates,3,prices,currency,weekly,failure,sources));
    assert(currency==native&&failure.isEmpty()&&!weekly);
    assert(std::isnan(prices[0])&&prices[1]==10&&prices[2]==12);
    assert(sources[0]==0&&sources[1]==1767312000&&sources[2]==1767398400);
  }
  // Weekly closes are unavailable before the week ends (no lookahead).
  time_t oldDates[]={1767225600,1767398400,1767225600+800L*86400};
  assert(!fetchHistoryCloses("WEBN.DE",oldDates,3,prices,currency,weekly,failure));
  assert(weekly&&std::isnan(prices[1])&&std::isnan(prices[2]));
  time_t longGap[]={1767312000,1767312000+90L*86400};
  assert(fetchHistoryCloses("WEBN.DE",longGap,2,prices,currency,weekly,failure));
  assert(prices[0]==10&&std::isnan(prices[1]));
  int before=fetchCount;
  assert(fetchHistoryCloses("EURPLN=X",dates,3,prices,currency,weekly,failure));
  assert(fetchHistoryCloses("EURPLN=X",dates,3,prices,currency,weekly,failure));
  assert(fetchCount==before+1);
  responseStatus=429;
  assert(!fetchHistoryCloses("ANAV.DE",dates,3,prices,currency,weekly,failure));
  assert(failure=="ANAV.DE: HTTP 429"&&std::isnan(prices[2]));
  responseStatus=200; responseBody="not JSON";
  assert(!fetchHistoryCloses("XAUT-USD",dates,3,prices,currency,weekly,failure));
  assert(failure=="XAUT-USD: JSON InvalidInput");
  responseBody="{\"chart\":{\"result\":null}}";
  assert(!fetchHistoryCloses("BTC-USD",dates,3,prices,currency,weekly,failure));
  assert(!failure.isEmpty());
  assert(!fetchHistoryCloses("BTC-USD",dates,0,prices,currency,weekly,failure));
  assert(!failure.isEmpty());
  // ETF benchmarks are not holdings. Their latest Yahoo daily candle (and
  // FX candle) may be null, even though independent meta has a fresh quote.
  time_t now=time(nullptr),currentDates[]={now-2*86400,now-1200,now};
  auto currentFixture=[&](time_t asOf){return std::string("{\"chart\":{\"result\":[{\"meta\":{\"currency\":\"EUR\",\"regularMarketPrice\":12,\"regularMarketTime\":")+std::to_string(asOf)+"},\"timestamp\":["+std::to_string(now-3*86400)+","+std::to_string(now-3600)+","+std::to_string(now-100)+"],\"indicators\":{\"quote\":[{\"close\":[10,null,null]}]}}]}}";};
  responseBody=currentFixture(now-600);
  for(const char* symbol:{"SXR8.DE","VWCE.DE"}){
    assert(fetchHistoryCloses(symbol,currentDates,3,prices,currency,weekly,failure));
    assert(prices[0]==10&&prices[1]==10&&prices[2]==12);
  }
  // Fresh spot metadata is retained in the shared FX cache as well.
  before=fetchCount;assert(fetchHistoryCloses("EURPLN=X",currentDates,3,prices,currency,weekly,failure));
  assert(fetchHistoryCloses("EURPLN=X",currentDates,3,prices,currency,weekly,failure));
  assert(prices[2]==12&&fetchCount==before+1);
  responseBody=currentFixture(now-9*86400);
  assert(fetchHistoryCloses("SXR8.DE",currentDates,3,prices,currency,weekly,failure));assert(prices[2]==10);
  responseBody=currentFixture(now+100); // No future price may enter the history.
  assert(fetchHistoryCloses("VWCE.DE",currentDates,3,prices,currency,weekly,failure));assert(prices[2]==10);
  std::vector<HistoryBar> gaps={{now-10*86400,10},{now-5*86400,NAN},{now-86400,NAN}};
  time_t gapDates[]={now-4*86400,now-3*86400,now};
  assert(sampleHistory(gaps,gapDates,3,false,prices,sources));
  assert(prices[0]==10&&prices[1]==10&&std::isnan(prices[2]));
  assert(sources[0]==now-10*86400&&sources[1]==sources[0]&&sources[2]==0);
  std::vector<HistoryBar> empty={{now-86400,NAN}};
  assert(!sampleHistory(empty,gapDates,3,false,prices,sources));
  std::vector<HistoryBar> weeklyGaps={{now-16*86400,20},{now-5*86400,NAN}};
  assert(sampleHistory(weeklyGaps,gapDates,3,true,prices,sources));
  assert(prices[0]==20&&prices[1]==20&&std::isnan(prices[2]));
  puts("PASS: actual historical fetch/JSON filter, null closes, FX currencies, GBp, weekly no-lookahead and HTTP/JSON errors.");
  puts("PASS: bounded short-gap carry retains original quote dates, expires after 7/14 days and never invents an initial or future price; benchmark/FX spot metadata stays independent.");
}
