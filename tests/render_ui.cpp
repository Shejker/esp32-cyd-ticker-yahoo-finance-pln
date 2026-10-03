#include "../CYDTicker/web_ui.h"
#include "../CYDTicker/portfolio_ui.h"
#include <iostream>
#include <cstring>
#include <cassert>

int main(int argc,char** argv) {
  bool light=argc>2&&std::strcmp(argv[2],"light")==0;
  WebServer server;
  if(argc>1&&std::strcmp(argv[1],"history")==0){
    server.sendContent(String("<!DOCTYPE html><html data-theme='")+(light?"light":"dark")+"'>");
    server.sendContent_P(PORTFOLIO_HEAD);
    server.sendContent("</head><body><main>");
    server.sendContent_P(PORTFOLIO_HTML);
    server.sendContent("<script type='application/json' id='portfolio-config'>{\"positions\":[{\"name\":\"BTC-USD\",\"kind\":\"ticker\"},{\"name\":\"PPK\",\"kind\":\"manual\"},{\"name\":\"Toyota Bank\",\"kind\":\"manual\"}],\"cashFlows\":[[1767787200,10000]]}</script>");
    server.sendContent_P(REQUEST_SCRIPT);
    server.sendContent_P(PORTFOLIO_SCRIPT);
  }else{
    TrackerPage page{};page.dark=!light;page.portfolio=true;page.anyRealPL=true;
    page.rows="<tr><td>BTC-USD</td><td>200000 PLN</td><td>+2%</td><td>15000</td><td>300</td></tr>";
    page.holdings="<tr><td>BTC-USD</td><td>0.0750</td><td>133333</td><td>5000</td><td><input class='inp' form='cfgform' name='ahi_BTC-USD' type='number' value='0'></td><td><input class='inp' form='cfgform' name='alo_BTC-USD' type='number' value='0'></td></tr>";
    page.transactions.push_back("<tr><td>BTC-USD</td><td>07.01.2026</td><td>0.075</td><td>133333</td><td>10000</td><td><button class='editlink' data-sym='BTC-USD' data-date='2026-01-07' data-qty='0.075' data-price='133333' data-lot='0' onclick='editLot(this)'>Edit</button><button class='dellink' data-sym='BTC-USD' data-lot='0' onclick='deleteLot(this)'>Delete</button></td></tr>");
    page.manualRows=savingsRow(0,false,"Toyota Bank","2026-10-02",76171.42,1262.77);
    page.manualRows+=savingsRow(1,true,"PPK","2026-10-02",0,0);
    page.tickerOptions="<option value='BTC-USD'>BTC-USD</option><option value='AAPL'>AAPL</option>";
    page.configJson=R"JSON({"tickers":["BTC-USD","AAPL"],"lotCounts":{"BTC-USD":1,"AAPL":0},"range":"1d","manual":[{"id":0,"name":"Toyota Bank","kind":"savings","value":76171.42,"gain":1262.77,"version":"toyota-v1"},{"id":1,"name":"PPK","kind":"ppk","value":0,"gain":0,"version":"ppk-v1"}]})JSON";
    page.totalValue=101171.42;page.periodPL=300;page.holdingPL=7762.77;
    page.refreshSec=60;page.brightness=200;page.minRefresh=10;page.nightFrom=0;page.nightTo=8;
    page.chartRange="1d";page.periodLabel="1D";
    bool stress=argc>1&&std::strcmp(argv[1],"tracker-stress")==0;
    if(stress)for(int i=1;i<60;++i)page.transactions.push_back(page.transactions.front());
    sendRootHtml(server,page);
    assert(server.finished);
    assert(server.flashChunks==6); // savings/backup controls + four complete scripts
    if(stress)assert(server.largestStringChunk<12000); // not the 60-row ledger
  }
  std::cout<<server.html;
}
