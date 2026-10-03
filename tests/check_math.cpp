#include "../CYDTicker/investment_math.h"
#include <cassert>
#include <cstdio>

struct Asset {
  float valuePLN = 0, gainPLN = 0;
  ManualSnapshot history[3]{};
  int historyCount = 0;
};

int main() {
  setenv("TZ", "CET-1CEST,M3.5.0/2,M10.5.0/3", 1); tzset();
  assert(parseCalendarDate("2024-02-29") > 0);
  for (const char* invalid : {"2026-02-29", "2026-04-31", "2026-13-01", "2026-01-00", "2026-1-01", "2026/01/01", "2026-01-01x", "abcd-ef-gh", "", "1999-01-01"})
    assert(parseCalendarDate(invalid) == 0);
  time_t winter = parseCalendarDate("2026-01-01"), summer = parseCalendarDate("2026-07-01");
  assert(historyDayEnd(winter)-winter == 43199);
  assert(historyDayEnd(summer)-summer == 43199);
  assert(isSameLocalDay(winter, historyDayEnd(winter)));
  assert(!isSameLocalDay(winter, summer));
  float number;
  for (const char* invalid : {"", "NaN", "inf", "12junk", "1,2", "1e99", "1e-99"}) assert(!parseFiniteNumber(invalid, number));
  assert(parseFiniteNumber("-1262.77", number));
  assert(parseFiniteNumber("0.000005", number));
  Lot lots[] = {{1,10,100},{2,10,200},{3,-5,250},{4,-15,100}};
  struct TestTicker { Lot lots[3]; int lotCount; } tickers[] = {
    {{{10,1,100},{30,1,110},{30,1,120}},3},
    {{{20,1,200},{40,1,210}},2}
  };
  TransactionRef refs[6]{};
  assert(newestTransactions(tickers,2,refs,6)==5);
  assert(refs[0].ticker==1&&refs[0].lot==1&&refs[0].ts==40);
  assert(refs[1].ticker==0&&refs[1].lot==1&&refs[1].ts==30);
  assert(refs[2].ticker==0&&refs[2].lot==2&&refs[2].ts==30);
  assert(refs[3].ticker==1&&refs[3].lot==0&&refs[3].ts==20);
  assert(refs[4].ticker==0&&refs[4].lot==0&&refs[4].ts==10);
  assert(tickers[0].lots[0].ts==10&&tickers[1].lots[0].ts==20); // display sorting never changes edit IDs
  assert(newestTransactions(tickers,0,refs,6)==0);
  float qty; double cost, realized;
  assert(validLotSequence(lots,4));
  calculateLedger(lots,4,0,qty,cost,realized); assert(qty==0&&cost==0&&realized==0);
  calculateLedger(lots,4,2,qty,cost,realized); assert(qty==20&&cost==3000&&realized==0);
  calculateLedger(lots,4,3,qty,cost,realized); assert(qty==15&&cost==2250&&realized==500);
  calculateLedger(lots,4,4,qty,cost,realized); assert(qty==0&&cost==0&&realized==-250);
  Lot oversell[]={{1,2,100},{2,-3,120}}, missingBuy[]={{2,-3,120}}, outOfOrder[]={{2,2,100},{1,-1,120}};
  assert(!validLotSequence(oversell,2)); assert(!validLotSequence(missingBuy,1)); assert(!validLotSequence(outOfOrder,2));
  Lot small[]={{1,0.000005f,200000},{2,-0.000005f,300000}};
  assert(validLotSequence(small,2)); calculateLedger(small,2,2,qty,cost,realized);
  assert(qty==0&&cost==0&&std::abs(realized-.5)<.000001);
  Lot emptyCryptoSale[]={{1,-0.00000001f,200000}}, cryptoOversell[]={{1,0.00000001f,200000},{2,-0.00000002f,200000}};
  assert(!validLotSequence(emptyCryptoSale,1)); assert(!validLotSequence(cryptoOversell,2));
  Asset asset;
  auto jan=parseCalendarDate("2026-01-01"), feb=parseCalendarDate("2026-02-01"), mar=parseCalendarDate("2026-03-01"), apr=parseCalendarDate("2026-04-01");
  assert(recordManualValuation(asset,jan,100,0));
  assert(recordManualValuation(asset,mar,1000,50));
  assert(recordManualValuation(asset,feb,500,20));
  assert(asset.valuePLN==1000&&asset.gainPLN==50&&asset.historyCount==3);
  assert(recordManualValuation(asset,feb,600,25));
  assert(asset.valuePLN==1000&&asset.gainPLN==50&&asset.historyCount==3);
  assert(!recordManualValuation(asset,jan-86400,10,0));
  assert(recordManualValuation(asset,apr,1100,100));
  assert(asset.historyCount==3&&asset.history[0].ts==feb&&asset.valuePLN==1100);
  assert(!recordManualValuation(asset,apr,-1,0));
  Money nextValue,nextGain;
  assert(savingsUpdate(76171.42f,1262.77f,5000,250,nextValue,nextGain));
  assert(std::abs(nextValue-81421.42)<.01&&std::abs(nextGain-1512.77)<.001);
  assert(savingsUpdate(nextValue,nextGain,5000,0,nextValue,nextGain));
  assert(std::abs(nextValue-86421.42)<.01&&std::abs(nextGain-1512.77)<.001);
  assert(!savingsUpdate(100,5,-200,0,nextValue,nextGain));
  assert(!savingsUpdate(100,5,0,-10,nextValue,nextGain));
  assert(!savingsUpdate(100,5,NAN,10,nextValue,nextGain));
  assert(ppkUpdate(0,0,669.42f,502.07f,0,false,0,nextValue,nextGain));
  assert(std::abs(nextValue-1171.49)<.001&&nextGain==0);
  assert(ppkUpdate(nextValue,nextGain,0,0,0,true,1180,nextValue,nextGain));
  assert(nextValue==1180&&std::abs(nextGain-8.51)<.001);
  assert(ppkUpdate(nextValue,nextGain,100,50,250,true,1570,nextValue,nextGain));
  assert(nextValue==1570&&std::abs(nextGain+1.49)<.001);
  assert(!ppkUpdate(0,0,-1,0,0,false,0,nextValue,nextGain));
  assert(!ppkUpdate(100,5,0,0,0,true,NAN,nextValue,nextGain));
  nextValue=76171.42;nextGain=1262.77;
  for(int i=0;i<100;++i)assert(savingsUpdate(nextValue,nextGain,0,.01,nextValue,nextGain));
  assert(nextValue.cents==7617242&&nextGain.cents==126377);
  Money parsed;assert(parseMoney("76171.42",parsed)&&parsed.cents==7617142);
  assert(!parseMoney("0.001",parsed)&&!parseMoney("nan",parsed));
  assert(parseMoney("1e3",parsed)&&parsed.cents==100000);
  assert(parseMoney("10000000000000.00",parsed)&&parsed.cents==Money::MAX_CENTS);
  assert(!parseMoney("10000000000000.001",parsed)&&!parseMoney("-1e-3",parsed));
  char tiny[32];formatQuantity(1e-10f,tiny);assert(parseFiniteNumber(tiny,number)&&number==1e-10f);
  assert(freshTimestamp(100,105,7)&&!freshTimestamp(100,108,7)&&!freshTimestamp(1000,100,7));
  Asset sameDay;
  assert(recordManualValuation(sameDay,jan,1171.49f,0));
  assert(recordManualValuation(sameDay,jan,1180,8.51f));
  assert(sameDay.historyCount==1&&sameDay.valuePLN==1180);
  puts("PASS: savings deposits/interest, withdrawals, PPK employer/state contributions, optional valuation, losses and same-day updates.");
  puts("PASS: date/numeric validation, DST, average-cost ledger, partial/full sales, overselling, tiny crypto quantities and manual-history corrections/retention.");
}
