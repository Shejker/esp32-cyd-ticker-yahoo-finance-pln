// Exercise the real export handlers; no device/network access or persistence writes.
#include <Arduino.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include "../CYDTicker/investment_math.h"
#include <cassert>
#include <cstdio>
#include <memory>

void convertToJson(const Money& money, JsonVariant json) { json.set((double)money); }
WebServer server;
int dataMutex=0; constexpr int portMAX_DELAY=0;
void xSemaphoreTake(int,int) {} void xSemaphoreGive(int) {}
#include "portfolio-models-under-test.h"
TickerState tickerData[MAX_TICKERS]{}; int tickerCount=1;
ManualAsset manualAssets[MAX_MANUAL_ASSETS]{}; int manualCount=1;
#include "backups-under-test.h"

int main() {
  tickerData[0].sym="BTC-USD"; tickerData[0].lotCount=1;
  tickerData[0].lots[0]={parseCalendarDate("2026-10-02"),1e-10f,326656.44f,1};
  manualAssets[0].name="Toyota Bank";
  assert(recordManualValuation(manualAssets[0],parseCalendarDate("2026-10-02"),76171.42,1262.77));
  handleApiTransactions();
  assert(server.status==200);
  assert(server.headers["Content-Disposition"]=="attachment; filename=\"transactions.json\"");
  JsonDocument transactions; assert(!deserializeJson(transactions,server.html));
  assert(transactions.size()==1 && transactions[0]["symbol"]=="BTC-USD");
  assert((float)transactions[0]["qty"].as<double>()==1e-10f);
  assert(transactions[0]["date"]=="2026-10-02");
  handleApiSavingsPPK();
  assert(server.status==200);
  assert(server.headers["Content-Disposition"]=="attachment; filename=\"savings-ppk.json\"");
  JsonDocument accounts; assert(!deserializeJson(accounts,server.html));
  assert(accounts.size()==1 && accounts[0]["name"]=="Toyota Bank");
  assert(accounts[0]["kind"]=="savings");
  // ArduinoJson may parse short decimal tokens into float storage; check the
  // exported decimal text as well as its cent-rounded numerical meaning.
  assert(server.html.find("\"valuePLN\":76171.42")!=std::string::npos);
  assert(server.html.find("\"gainPLN\":1262.77")!=std::string::npos);
  assert(std::llround(accounts[0]["valuePLN"].as<double>()*100)==7617142);
  assert(std::llround(accounts[0]["gainPLN"].as<double>()*100)==126277);
  assert(accounts[0]["history"].size()==1);
  // Empty exports must also download valid JSON files.
  tickerCount=manualCount=0;
  handleApiTransactions(); assert(!deserializeJson(transactions,server.html) && transactions.size()==0);
  handleApiSavingsPPK(); assert(!deserializeJson(accounts,server.html) && accounts.size()==0);
  puts("PASS: real transaction/Savings & PPK exports force JSON downloads with matching filenames and unchanged data.");
}
