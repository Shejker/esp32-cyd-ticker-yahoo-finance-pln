#include <Arduino.h>
#include <ArduinoJson.h>
#include <WebServer.h>
#include "../CYDTicker/investment_math.h"
#include <cassert>
#include <cstdio>
WebServer server;int historyMutex=0;constexpr int portMAX_DELAY=0;
void xSemaphoreTake(int,int){}void xSemaphoreGive(int){}
unsigned long clockMillis=100;unsigned long millis(){return clockMillis;}
struct HistoryResponse{int status=200;String body;};
int work=0;
void buildPortfolioPosition(const String&,const String&,HistoryResponse& response){++work;response.body="{\"ok\":true,\"points\":[[1,10,2]]}";}
bool fetchHistoricalPricePLN(const String&,time_t,float& value){++work;value=123.45;return true;}
time_t parseDateYMD(const String& s){return parseCalendarDate(s.c_str());}
#include "jobs-under-test.h"
String jobId(){JsonDocument doc;assert(!deserializeJson(doc,server.html));return doc["job"].as<const char*>();}
int main(){
  historyAvailable=true;server.args={{"kind","ticker"},{"name","BTC-USD"}};handlePortfolioPosition();
  assert(server.status==202&&work==0);String first=jobId();
  server.args={{"id",first}};handleHistoryJob();assert(server.status==202&&work==0);
  enqueueHistory("PPK","manual");String second=jobId();enqueueHistory("Toyota Bank","manual");assert(server.status==503&&work==0);
  assert(processHistoryJobOnce()&&work==1);server.args={{"id",first}};handleHistoryJob();assert(server.status==200&&server.html.find("points")!=std::string::npos);
  handleHistoryJob();assert(server.status==200); // Poll retry after a lost HTTP response.
  enqueueHistory("BTC-USD|2026-01-01","price");String price=jobId();assert(server.status==202);
  while(processHistoryJobOnce()){}
  server.args={{"id",price}};handleHistoryJob();assert(server.status==200&&server.html.find("123.4500")!=std::string::npos);
  server.args={{"id",second}};handleHistoryJob();assert(server.status==200);
  server.args={{"id","missing"}};handleHistoryJob();assert(server.status==404);
  puts("PASS: actual asynchronous history queue, non-blocking admission/poll, capacity handling, poll retries and historical-price jobs.");
}
