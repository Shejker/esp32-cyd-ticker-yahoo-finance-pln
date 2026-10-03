#pragma once
#include <Arduino.h>
#include "browser_requests.h"

// Browser draws charts locally; the ESP32 streams one position at a time.
static const char PORTFOLIO_HEAD[] PROGMEM = R"PORTFOLIO(
<head><meta charset="UTF-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Portfolio Explorer</title><link rel="icon" href="/favicon.svg">
<style>
:root{color-scheme:dark;--bg:#0a0a0f;--panel:#13131a;--border:#222230;--text:#c8ccd4;--muted:#888;--grid:#2b3040;--accent:#0080ff;--positive:#00cc44;--negative:#ff4444}
html[data-theme=light]{color-scheme:light;--bg:#f0f0f5;--panel:#fff;--border:#d0d0e0;--text:#1a1a2e;--muted:#777;--grid:#dce0e8;--accent:#0080ff;--positive:#008833;--negative:#d32f2f}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--text);font-family:'SF Mono','Fira Mono',monospace}
main{max-width:900px;margin:auto;padding:20px 14px 32px}a{color:#0af;text-decoration:none}
.page-tabs{display:flex;gap:8px;margin-bottom:18px}.page-tabs a{font-size:12px;border:1px solid var(--border);border-radius:6px;padding:8px 14px}.page-tabs a[aria-current=page]{background:#0080ff;color:#fff;border-color:#0080ff}
header{margin:18px 0}h1{font-size:22px;margin:0 0 8px}p{color:var(--muted);line-height:1.55;font-size:12px;margin:6px 0}
.eyebrow{font-size:10px;font-weight:700;text-transform:uppercase;letter-spacing:2px;color:var(--muted)}.toolbar,.card,.stat{border:1px solid var(--border);background:var(--panel);border-radius:10px}
.toolbar{padding:14px;display:flex;flex-wrap:wrap;align-items:center;gap:12px;justify-content:space-between}
.tabs,.periods{display:flex;flex-wrap:wrap;gap:5px}button,select{font:inherit;font-size:12px;border-radius:8px;border:1px solid var(--border);background:transparent;color:var(--text);padding:9px 12px;cursor:pointer}button.active{background:var(--accent);color:var(--bg);border-color:var(--accent)}button:hover{border-color:var(--accent)}button:focus-visible,select:focus-visible{outline:2px solid var(--accent);outline-offset:3px}select{background:var(--panel);min-width:150px}.controls{display:flex;align-items:center;gap:10px}
#single-wrap[hidden]{display:none}.status{font-size:12px;margin:12px 2px;color:var(--muted)}.warning{color:var(--negative)}#retry{padding:4px 8px;margin-left:8px}
.position-picker{position:relative}#position{min-width:150px;display:flex;justify-content:space-between;gap:18px;align-items:center;background:var(--panel)}#position::after{content:'⌄';color:var(--muted)}#position-list{position:absolute;top:calc(100% + 6px);left:0;min-width:100%;width:max-content;max-width:calc(100vw - 40px);max-height:320px;overflow:auto;z-index:3;background:var(--panel);border:1px solid var(--border);border-radius:8px;padding:5px;box-shadow:0 8px 24px #0004}#position-list[hidden]{display:none}.position-group-label{font-size:10px;color:var(--muted);padding:9px 8px 5px}.position-option{display:flex;align-items:center;text-align:left;gap:18px;width:100%;border:0;border-radius:4px;padding:8px}.position-option:hover,.position-option:focus-visible{background:var(--border)}.position-check{margin-left:auto;visibility:hidden}.position-option[aria-selected=true] .position-check{visibility:visible}#benchmark-status{font-size:11px}#benchmark-retry{padding:4px 8px;margin-top:8px}#benchmark-table td small{display:block;font-size:10px;color:var(--muted);margin-top:4px}
.stats{display:grid;grid-template-columns:repeat(4,minmax(0,1fr));gap:12px;margin:16px 0}.stat{padding:18px}.stat strong{font-size:24px;display:block;margin:9px 0 5px;font-variant-numeric:tabular-nums;letter-spacing:-.5px}.stat small{font-size:11px;color:var(--muted)}.positive{color:var(--positive)}.negative{color:var(--negative)}
.card{padding:20px;margin-top:16px}.chart-head{display:flex;justify-content:space-between;align-items:start;gap:12px}h2{font-size:17px;letter-spacing:-.3px;margin:0 0 5px}.badge{font-size:11px;padding:5px 9px;border-radius:6px;border:1px solid var(--border);white-space:nowrap;color:var(--muted)}
.legend{display:flex;flex-wrap:wrap;gap:7px;margin:16px 0 8px}.legend button{padding:6px 9px;display:flex;align-items:center;gap:7px}.legend button.off{opacity:.45}.swatch{height:9px;width:9px;border-radius:50%;display:inline-block}.chart{position:relative;height:310px;margin-top:12px}canvas{width:100%;height:100%;display:block;touch-action:pan-y}
.tooltip{position:absolute;pointer-events:none;display:none;top:10px;right:12px;background:var(--panel);border:1px solid var(--border);box-shadow:0 8px 30px #0003;border-radius:9px;padding:10px;font-size:11px;max-width:260px;z-index:1}.tooltip-row{display:flex;justify-content:space-between;gap:16px;margin-top:5px}.tooltip time{color:var(--muted)}.empty{padding:80px 16px;text-align:center;color:var(--muted)}
.table-wrap{overflow-x:auto}table{border-collapse:collapse;width:100%;font-size:12px;font-variant-numeric:tabular-nums}th,td{padding:12px 8px;border-bottom:1px solid var(--border);text-align:left;white-space:nowrap}th{font-size:10px;color:var(--muted);font-weight:600}td button{border:0;padding:0;font-weight:600;display:flex;align-items:center;gap:8px}
.note{margin-top:18px;font-size:12px}#details{font-size:11px}.subtitle{font-size:12px}
.price-age{display:block;font-size:10px;font-weight:normal;color:var(--muted);margin-top:3px;line-height:1.4}
#benchmark-card .chart-head{flex-wrap:wrap;align-items:center}#benchmark-card .chart-head>div:first-child{flex:1 1 300px}#benchmark-card .periods{flex:0 0 auto}
@media(max-width:720px){.stats{grid-template-columns:repeat(2,minmax(0,1fr));gap:8px}.stat{padding:12px}.stat strong{font-size:clamp(12px,3.6vw,18px)}.card{padding:14px}.chart{height:280px}.toolbar{gap:14px}.controls{width:100%;justify-content:space-between;flex-wrap:wrap}}
</style>
)PORTFOLIO";

static const char PORTFOLIO_HTML[] PROGMEM = R"PORTFOLIO(
<nav class="page-tabs" aria-label="Pages"><a href="/">Tracker</a><a href="/portfolio" aria-current="page">Charts</a></nav>
<header><h1>Portfolio Charts</h1><p>Select a position or compare your portfolio. Values and profit are shown separately in PLN.</p></header>
<div class="toolbar">
<div class="tabs" aria-label="View"><button data-mode="total" class="active" aria-pressed="true">Total Portfolio</button><button data-mode="compare" aria-pressed="false">Compare Positions</button><button data-mode="single" aria-pressed="false">Single Position</button></div>
<div class="controls"><div id="single-wrap" class="position-picker" hidden><button id="position" type="button" aria-label="Position" aria-haspopup="listbox" aria-expanded="false" aria-controls="position-list">Select Position</button><div id="position-list" role="listbox" aria-label="Position" hidden></div></div>
<div class="periods" aria-label="Period"><button data-range="30">1M</button><button data-range="90">3M</button><button data-range="180">6M</button><button data-range="ytd" title="January 1 to the latest valuation">YTD</button><button data-range="365">1Y</button><button data-range="all" class="active" aria-pressed="true">ALL</button></div></div>
</div>
<div class="status" role="status" aria-live="polite"><span id="status">Loading saved positions…</span><button id="retry" hidden>Retry failed positions</button></div>
<div class="stats">
<div class="stat"><div class="eyebrow">Current Value</div><strong id="value">—</strong><small id="scope">All positions</small></div>
<div class="stat"><div class="eyebrow">Cumulative P&amp;L</div><strong id="gain">—</strong><small>Profit from recorded transactions</small></div>
<div class="stat"><div class="eyebrow">P&amp;L Change</div><strong id="change">—</strong><small id="period-label">Over the visible period</small></div>
<div class="stat"><div class="eyebrow">Net Cash Contributed</div><strong id="capital">—</strong><small>Contributions minus withdrawals</small></div>
</div>
<section class="card"><div class="chart-head"><div><h2 id="value-title">Portfolio Value</h2><p class="subtitle">Deposits and withdrawals change this balance.</p></div><span class="badge">PLN</span></div><div id="legend" class="legend"></div><div class="chart"><canvas id="value-chart" role="img" aria-label="History of position values in PLN"></canvas><div class="tooltip" id="value-tip"></div></div></section>
<section class="card"><div class="chart-head"><div><h2 id="gain-title">Profit &amp; Loss</h2><p class="subtitle">Cumulative profit including realized sales. Manual entries use the P&amp;L you recorded.</p></div><span class="badge">PLN · P&amp;L</span></div><div class="chart"><canvas id="gain-chart" role="img" aria-label="History of position profit and loss in PLN"></canvas><div class="tooltip" id="gain-tip"></div></div></section>
<section class="card" id="benchmark-card"><div class="chart-head"><div><h2>Tickers vs. Benchmarks</h2><p class="subtitle">Your tickers only, including crypto and gold. Savings and PPK are excluded.</p></div><div class="periods" aria-label="Benchmark Period"><button type="button" data-range="30">1M</button><button type="button" data-range="90">3M</button><button type="button" data-range="180">6M</button><button type="button" data-range="ytd" title="January 1 to the latest valuation">YTD</button><button type="button" data-range="365">1Y</button><button type="button" data-range="all" class="active" aria-pressed="true">ALL</button></div></div><p id="benchmark-period"></p><div class="table-wrap"><table id="benchmark-table"><thead><tr><th>Portfolio / ETF</th><th>Value · PLN</th><th id="benchmark-gain-heading">P&amp;L · PLN</th><th id="benchmark-return-heading">Return · %</th><th>Portfolio Lead · PLN</th></tr></thead><tbody id="benchmarks"></tbody></table></div><p id="benchmark-status" role="status" aria-live="polite">Loading benchmark prices…</p><button id="benchmark-retry" hidden>Retry Benchmarks</button><p class="note">Uses the same period as the charts. For shorter periods, each ETF starts with your tickers' opening value and follows the same purchases and sale withdrawals. ALL starts with the first recorded purchase. P&amp;L excludes added capital; Return = period P&amp;L / (opening value + purchases), not an annualized or time-weighted return. Fractional ETF units and PLN exchange rates are used; period boundaries use available samples. No extra fees or taxes are assumed; cash dividends on your tickers are excluded. Positive Portfolio Lead means your tickers did better.</p></section>
<section class="card"><div class="chart-head"><div><h2>Position Breakdown</h2><p class="subtitle">Select a name to inspect it. Values below use the latest available date.</p></div></div><div class="table-wrap"><table><thead><tr><th>Position</th><th>Value · PLN</th><th>P&amp;L · PLN</th><th>Net Cash · PLN</th><th>History From</th></tr></thead><tbody id="breakdown"></tbody></table></div></section>
<p class="note">Market history uses recorded buy/sell quantities, closing prices and historical PLN exchange rates. P&amp;L includes realized gains from recorded sales and unrealized gains on remaining holdings. Fees count only if included in your PLN unit price; cash dividends are not tracked automatically. Manual valuations stay constant until your next entry; missing market data appears as a gap.</p>
<p id="details" class="note"></p>
</main>
)PORTFOLIO";

static const char PORTFOLIO_SCRIPT[] PROGMEM = R"PORTFOLIO(
<script>
(function(){
'use strict';
var config=JSON.parse(document.getElementById('portfolio-config').textContent);
var palette=['#45b6fe','#a78bfa','#f8b84e','#4dd6a1','#f778a1','#57d5da','#e2d06b','#abdc72','#fb9062','#91a1f8','#d789df','#66c5a2'];
var colorOrder=config.positions.map(function(p){return p.kind+'|'+p.name;}).sort();
var positions=config.positions.map(function(p,i){p.id=i;p.color=palette[colorOrder.indexOf(p.kind+'|'+p.name)%palette.length];p.data=null;p.error='';p.hidden=false;return p;});
var state={mode:'total',range:'all',single:positions.length?positions[0].id:0,busy:false};
var query=new URLSearchParams(location.search),requested=positions.find(function(p){return p.name===query.get('name')&&p.kind===query.get('kind');});
if(requested){state.mode='single';state.single=requested.id;}
var charts={};var el=function(id){return document.getElementById(id);};
var benchmarks=[{name:'S&P 500',symbol:'SXR8.DE',data:null,error:''},{name:'FTSE All-World',symbol:'VWCE.DE',data:null,error:''}],benchmarkBusy=false;
var money=new Intl.NumberFormat('pl-PL',{minimumFractionDigits:2,maximumFractionDigits:2});
var shortMoney=new Intl.NumberFormat('en',{notation:'compact',maximumFractionDigits:1});
var dateFmt=new Intl.DateTimeFormat('en-GB',{timeZone:'UTC',day:'2-digit',month:'short',year:'2-digit'});
var warsaw=new Intl.DateTimeFormat('en-CA',{timeZone:'Europe/Warsaw',year:'numeric',month:'2-digit',day:'2-digit'});
function day(ts){var d=warsaw.formatToParts(new Date(ts*1000)),parts={};d.forEach(function(p){parts[p.type]=p.value;});return Date.UTC(Number(parts.year),Number(parts.month)-1,Number(parts.day));}
function fmt(n){return n===null||!Number.isFinite(n)?'—':money.format(n);}
function signed(n){return n===null||!Number.isFinite(n)?'—':(n>0?'+':'')+fmt(n);}
function finite(n){return typeof n==='number'&&Number.isFinite(n);}
var cashFlows=(config.cashFlows||[]).map(function(f){return [day(f[0]),f[1]];}).sort(function(a,b){return a[0]-b[0];});
// Benchmark math: pure functions also exercised by the native test runner.
function benchmarkCash(flows,until){
 var invested=0,withdrawn=0;flows.forEach(function(f){if(f[0]>until)return;if(!finite(f[1]))throw new Error('Invalid transaction amount.');if(f[1]>0)invested+=f[1];else withdrawn-=f[1];});
 return {invested:invested,withdrawn:withdrawn,net:invested-withdrawn};
}
function simulateBenchmark(flows,points,until){
 var cash=benchmarkCash(flows,until),daily=new Map(),units=0;
 flows.forEach(function(f){if(f[0]<=until)daily.set(f[0],(daily.get(f[0])||0)+f[1]);});
 Array.from(daily.entries()).sort(function(a,b){return a[0]-b[0];}).forEach(function(f){
  if(f[1]===0)return;var q=at(points,f[0]);if(!q||!finite(q[1])||q[1]<=0)throw new Error('Missing ETF or FX price for a transaction date.');
  units+=f[1]/q[1];if(units < -1e-8)throw new Error('ETF value cannot cover a recorded sale withdrawal.');units=Math.max(0,units);
 });
 var last=at(points,until);if(units>0&&(!last||!finite(last[1])||last[1]<=0))throw new Error('Missing current ETF or FX price.');
 var value=units>0?units*last[1]:0,gain=value-cash.net;
 return {value:value,gain:gain,returnPct:cash.invested>0?gain/cash.invested*100:null};
}
function periodCash(flows,from,until){return benchmarkCash(flows.filter(function(f){return f[0]>from;}),until);}
function simulateBenchmarkPeriod(flows,points,from,until,opening){
 if(!finite(opening)||opening<0||until<from)throw new Error('Invalid opening portfolio value or period.');
 var cash=periodCash(flows,from,until),periodFlows=flows.filter(function(f){return f[0]>from&&f[0]<=until;}),seed=[];
 if(opening>0)seed.push([from,opening]);
 var result=simulateBenchmark(seed.concat(periodFlows),points,until),capital=opening+cash.invested;
 return {value:result.value,gain:result.value-opening-cash.net,returnPct:capital>0?(result.value-opening-cash.net)/capital*100:null};
}
// Benchmark math end.
function tickerTotalsAt(tickers,t){
 var value=0,gain=0;
 for(var i=0;i<tickers.length;i++){
  var p=tickers[i];if(!p.data)return null;
  if(p.data.start&&day(p.data.start)>t)continue;
  var q=at(p.data.points,t);if(!q||!finite(q[1])||!finite(q[2]))return null;
  value+=q[1];gain+=q[2];
 }
 return {value:value,gain:gain};
}
function gainClass(node,n){node.classList.remove('positive','negative');if(finite(n))node.classList.add(n>=0?'positive':'negative');}
function selected(){return positions.filter(function(p){return state.mode==='single'?p.id===state.single:state.mode==='total'||!p.hidden;});}
function lastPoint(p){return p.data&&p.data.points.length?p.data.points[p.data.points.length-1]:null;}
function activeSeries(){
 return selected().filter(function(p){return p.data;}).map(function(p){return {name:p.name,color:p.color,step:p.kind==='manual',points:p.data.points,start:p.data.start?day(p.data.start):Infinity};});
}
function totalSeries(){
 var chosen=selected();if(!chosen.length||chosen.some(function(p){return !p.data;}))return null;
 var times=Array.from(new Set(chosen.flatMap(function(p){return p.data.points.map(function(q){return q[0];});}))).sort(function(a,b){return a-b;});
 var cursors=chosen.map(function(){return -1;});
 var points=times.map(function(t){var value=0,gain=0,valid=true,priceDate=0,fxDate=0;chosen.forEach(function(p,i){while(cursors[i]+1<p.data.points.length&&p.data.points[cursors[i]+1][0]<=t)cursors[i]++;var q=cursors[i]>=0?p.data.points[cursors[i]]:null;if(q){if(!finite(q[1])||!finite(q[2]))valid=false;else{value+=q[1];gain+=q[2];if(q[3]>0)priceDate=priceDate?Math.min(priceDate,q[3]):q[3];if(q[4]>0)fxDate=fxDate?Math.min(fxDate,q[4]):q[4];}}});return [t,valid?value:null,valid?gain:null,priceDate,fxDate];});
 return {name:state.mode==='single'?chosen[0].name:state.mode==='compare'?'Selected total':'Total portfolio',color:state.mode==='single'?chosen[0].color:getComputedStyle(document.documentElement).getPropertyValue('--text').trim(),step:chosen.every(function(p){return p.kind==='manual';}),points:points,start:Math.min.apply(null,chosen.map(function(p){return p.data.start?day(p.data.start):Infinity;})),total:state.mode!=='single'};
}
function rangeStart(until,range){
 // Timeline dates are Warsaw calendar days encoded as UTC midnight.
 return range==='ytd'?Date.UTC(new Date(until).getUTCFullYear(),0,1):until-Number(range)*86400000;
}
function timeWindow(series){
 var points=series.flatMap(function(s){return s.points;}),now=day(Date.now()/1000);
 var last=points.length?Math.max.apply(null,points.map(function(p){return p[0];})):now;
 var start=state.range==='all'?Math.min.apply(null,series.map(function(s){return s.start;})):rangeStart(last,state.range);
 if(!Number.isFinite(start))start=last;
 return {from:start,to:last};
}
function at(points,t){var last=null;for(var i=0;i<points.length&&points[i][0]<=t;i++)last=points[i];return last;}
function renderSummary(total,window){
 var last=total&&total.points.length?total.points[total.points.length-1]:null;
 el('value').textContent=last&&finite(last[1])?fmt(last[1])+' PLN':'—';el('gain').textContent=last&&finite(last[2])?signed(last[2])+' PLN':'—';gainClass(el('gain'),last?last[2]:null);
 el('capital').textContent=last&&finite(last[1])&&finite(last[2])?fmt(last[1]-last[2])+' PLN':'—';
 var first=total?at(total.points,window.from):null;
 if(!first&&total&&total.points.length)first=total.points[0];
 var change=first&&last&&finite(first[2])&&finite(last[2])?last[2]-first[2]:null;
 el('change').textContent=finite(change)?signed(change)+' PLN':'—';gainClass(el('change'),change);
 el('scope').textContent=state.mode==='single'?(selected()[0]?selected()[0].name:'No position'):state.mode==='compare'?'Selected positions':'All positions';
 el('period-label').textContent=dateFmt.format(new Date(window.from))+' — '+dateFmt.format(new Date(window.to));
}
function renderLegend(){
 el('legend').replaceChildren();
 positions.forEach(function(p){if(state.mode==='single'&&p.id!==state.single)return;var b=document.createElement('button');b.type='button';b.className=p.hidden&&state.mode==='compare'?'off':'';b.setAttribute('aria-pressed',String(!(p.hidden&&state.mode==='compare')));var dot=document.createElement('span');dot.className='swatch';dot.style.background=p.color;b.appendChild(dot);b.appendChild(document.createTextNode(p.name));b.onclick=function(){if(state.mode==='compare'){p.hidden=!p.hidden;render();}else{state.mode='single';state.single=p.id;render();}};el('legend').appendChild(b);});
}
function renderTable(){
 el('breakdown').replaceChildren();
 positions.forEach(function(p){var q=lastPoint(p),tr=document.createElement('tr'),name=document.createElement('td'),b=document.createElement('button'),dot=document.createElement('span');dot.className='swatch';dot.style.background=p.color;b.appendChild(dot);b.appendChild(document.createTextNode(p.name));b.onclick=function(){state.mode='single';state.single=p.id;render();window.scrollTo({top:0,behavior:'smooth'});};name.appendChild(b);tr.appendChild(name);
 [q?fmt(q[1]):'—',q?signed(q[2]):'—',q&&finite(q[1])&&finite(q[2])?fmt(q[1]-q[2]):'—',p.data&&p.data.start?dateFmt.format(new Date(day(p.data.start))):'—'].forEach(function(v,i){var td=document.createElement('td');td.textContent=v;if(i===1&&q)gainClass(td,q[2]);tr.appendChild(td);});el('breakdown').appendChild(tr);});
}
function renderBenchmarks(){
 var tickers=positions.filter(function(p){return p.kind==='ticker';}),ends=[];
 tickers.forEach(function(p){var q=lastPoint(p);if(q)ends.push(q[0]);});
 benchmarks.forEach(function(b){if(b.data&&b.data.points.length)ends.push(b.data.points[b.data.points.length-1][0]);});
 var until=ends.length?Math.min.apply(null,ends):day(Date.now()/1000),cash=benchmarkCash(cashFlows,until);
 el('benchmarks').replaceChildren();
 if(!tickers.length||!cash.invested){el('benchmark-period').textContent='';el('benchmark-status').textContent='Record ticker purchases in the tracker to compare performance.';el('benchmark-retry').hidden=true;return;}
 var from=state.range==='all'?cashFlows[0][0]:Math.max(cashFlows[0][0],rangeStart(until,state.range)),limited=from>cashFlows[0][0];
 var opening=limited?tickerTotalsAt(tickers,from):{value:0,gain:0},period=limited?periodCash(cashFlows,from,until):cash;
 el('benchmark-period').textContent=dateFmt.format(new Date(from))+' — '+dateFmt.format(new Date(until))+(limited?' · Opening value: '+(opening?fmt(opening.value):'—')+' PLN · Purchases: ':' · Total purchases: ')+fmt(period.invested)+' PLN';
 el('benchmark-gain-heading').textContent=limited?'Period P&L · PLN':'P&L · PLN';el('benchmark-return-heading').textContent=limited?'Period Return · %':'Return · %';
 var end=tickerTotalsAt(tickers,until),portfolio=null,errors=[];
 if(end&&opening){var capital=opening.value+period.invested,gain=end.value-opening.value-period.net;portfolio={value:end.value,gain:gain,returnPct:capital>0?gain/capital*100:null};}
 else errors.push('Ticker history incomplete at a period boundary; portfolio result is unavailable.');
 var rows=[{name:'My Tickers',symbol:'Savings & PPK excluded',result:portfolio}];
 benchmarks.forEach(function(b){var result=null;b.failed=false;if(b.data&&opening){try{result=limited?simulateBenchmarkPeriod(cashFlows,b.data.points,from,until,opening.value):simulateBenchmark(cashFlows,b.data.points,until);}catch(error){b.failed=true;errors.push(b.name+': '+error.message);}}else if(b.error)errors.push(b.name+': '+b.error);rows.push({name:b.name,symbol:b.symbol,result:result});});
 rows.forEach(function(row,i){var tr=document.createElement('tr'),label=document.createElement('td'),name=document.createElement('strong'),symbol=document.createElement('small');name.textContent=row.name;symbol.textContent=row.symbol;label.append(name,symbol);tr.appendChild(label);var r=row.result,lead=i>0&&r&&portfolio?portfolio.gain-r.gain:null;
  [r?fmt(r.value):'—',r?signed(r.gain):'—',r&&finite(r.returnPct)?signed(r.returnPct)+'%':'—',finite(lead)?signed(lead):'—'].forEach(function(value,k){var cell=document.createElement('td');cell.textContent=value;if(k===1&&r)gainClass(cell,r.gain);if(k===2&&r)gainClass(cell,r.returnPct);if(k===3)gainClass(cell,lead);tr.appendChild(cell);});el('benchmarks').appendChild(tr);
 });
 var weekly=benchmarks.some(function(b){return b.data&&b.data.weekly;});
 el('benchmark-status').textContent=benchmarkBusy?'Loading benchmark prices…':errors.length?errors.join(' '):benchmarks.some(function(b){return !b.data;})?'Waiting for benchmark prices…':'Same cash flows compared in PLN.';
 if(weekly)el('benchmark-status').textContent+=' Older benchmark prices use weekly closes; transaction-date estimates are approximate.';
 el('benchmark-status').classList.toggle('warning',!benchmarkBusy&&!!errors.length);el('benchmark-retry').hidden=!benchmarks.some(function(b){return b.error||b.failed;});el('benchmark-retry').disabled=benchmarkBusy||state.busy;
}
function closePicker(focus){el('position-list').hidden=true;el('position').setAttribute('aria-expanded','false');if(focus)el('position').focus();}
function openPicker(){el('position-list').hidden=false;el('position').setAttribute('aria-expanded','true');var option=el('position-list').querySelector('[aria-selected=true]')||el('position-list').querySelector('[role=option]');if(option)option.focus();}
function renderPicker(){
 var chosen=positions.find(function(p){return p.id===state.single;});el('position').textContent=chosen?chosen.name:'Select Position';
 el('position-list').querySelectorAll('[role=option]').forEach(function(option){option.setAttribute('aria-selected',String(Number(option.dataset.position)===state.single));});
 if(state.mode!=='single')closePicker(false);
}
function render(){
 el('single-wrap').hidden=state.mode!=='single';renderPicker();
 document.querySelectorAll('[data-mode]').forEach(function(b){var a=b.dataset.mode===state.mode;b.classList.toggle('active',a);b.setAttribute('aria-pressed',String(a));});
 document.querySelectorAll('[data-range]').forEach(function(b){var a=b.dataset.range===state.range;b.classList.toggle('active',a);b.setAttribute('aria-pressed',String(a));});
 var series=activeSeries(),total=totalSeries();
 if(state.mode==='single'){series=total?[total]:series;}
 var window=timeWindow(series);renderSummary(total,window);renderLegend();renderTable();
 el('value-title').textContent=state.mode==='single'?(selected()[0]?selected()[0].name:'Position')+' · Value':state.mode==='compare'?'Compare Position Values':'Portfolio & Position Values';
 el('gain-title').textContent=state.mode==='single'?(selected()[0]?selected()[0].name:'Position')+' · Profit & Loss':'Profit & Loss by Position';
 draw('value-chart','value-tip',series,window,1);draw('gain-chart','gain-tip',series,window,2);
 el('details').textContent=positions.some(function(p){return p.data&&p.data.weekly;})?'Older market histories use weekly closes. The displayed amount of P&L is not an annualized or time-weighted return.':'The displayed amount of P&L is not an annualized or time-weighted return.';
 renderLoadStatus();
 renderBenchmarks();
}
function draw(id,tipId,series,window,metric){
 var canvas=el(id),rect=canvas.getBoundingClientRect(),dpr=windowDeviceRatio(),w=rect.width,h=rect.height;
 canvas.width=Math.round(w*dpr);canvas.height=Math.round(h*dpr);var ctx=canvas.getContext('2d');ctx.scale(dpr,dpr);
 var styles=getComputedStyle(document.documentElement),text=styles.getPropertyValue('--muted'),grid=styles.getPropertyValue('--grid');
 var left=w<450?55:78,right=14,top=14,bottom=32,cw=w-left-right,ch=h-top-bottom;
 var filtered=series.map(function(s){var points=s.points.filter(function(p){return p[0]>=window.from&&p[0]<=window.to&&p[0]>=s.start;});var previous=at(s.points,window.from);if(previous&&window.from>=s.start&&(!points.length||points[0][0]>window.from))points.unshift([window.from].concat(previous.slice(1)));return Object.assign({},s,{visible:points});});
 var values=filtered.flatMap(function(s){return s.visible.map(function(p){return p[metric];}).filter(finite);});
 ctx.font='11px system-ui';ctx.fillStyle=text;
 if(!values.length){ctx.textAlign='center';ctx.fillText(state.busy?'Loading history…':'No data for the selected positions and period.',w/2,h/2);charts[id]=null;return;}
 var lo=Math.min.apply(null,values),hi=Math.max.apply(null,values);
 if(metric===2){lo=Math.min(0,lo);hi=Math.max(0,hi);}var pad=Math.max(1,(hi-lo)*.08);lo-=pad;hi+=pad;
 var x=function(t){return left+(t-window.from)/Math.max(86400000,window.to-window.from)*cw;},y=function(v){return top+(hi-v)/(hi-lo)*ch;};
 ctx.textAlign='right';for(var i=0;i<=4;i++){var yy=top+i*ch/4,v=hi-i*(hi-lo)/4;ctx.strokeStyle=grid;ctx.lineWidth=1;ctx.beginPath();ctx.moveTo(left,yy);ctx.lineTo(w-right,yy);ctx.stroke();ctx.fillStyle=text;ctx.fillText(shortMoney.format(v),left-9,yy+4);}
 ctx.textAlign='left';ctx.fillText(dateFmt.format(new Date(window.from)),left,h-6);ctx.textAlign='right';ctx.fillText(dateFmt.format(new Date(window.to)),w-right,h-6);
 filtered.forEach(function(s){
 ctx.strokeStyle=s.color;ctx.fillStyle=s.color;ctx.lineWidth=2;ctx.beginPath();var previous=null;
 s.visible.forEach(function(p){if(!finite(p[metric])){previous=null;return;}var xx=x(p[0]),yy=y(p[metric]);if(!previous)ctx.moveTo(xx,yy);else{if(s.step)ctx.lineTo(xx,y(previous[metric]));ctx.lineTo(xx,yy);}previous=p;});ctx.stroke();
 // Draw dots even for a single valuation, and mark the current endpoint.
 var valid=s.visible.filter(function(p){return finite(p[metric]);});if(valid.length){var endpoint=valid[valid.length-1];ctx.beginPath();ctx.arc(x(endpoint[0]),y(endpoint[metric]),3.4,0,Math.PI*2);ctx.fill();}
 });
 charts[id]={series:filtered,window:window,x:x,y:y,metric:metric,left:left,cw:cw,tipId:tipId};
}
function windowDeviceRatio(){return Math.min(2,window.devicePixelRatio||1);}
function priceAge(point,t,total){
 var notes=[];if(!finite(point[1]))return '';
 if(point[3]>0&&day(point[3])<t)notes.push((total?'Oldest price from ':'Price from ')+dateFmt.format(new Date(day(point[3]))));
 if(point[4]>0&&day(point[4])<t)notes.push((total?'Oldest FX from ':'FX from ')+dateFmt.format(new Date(day(point[4]))));
 return notes.join(' · ');
}
function pointer(id,event){
 var c=charts[id];if(!c)return;var canvas=el(id),rect=canvas.getBoundingClientRect(),px=Math.max(c.left,Math.min(c.left+c.cw,event.clientX-rect.left));
 var t=c.window.from+(px-c.left)/c.cw*(c.window.to-c.window.from),times=c.series.flatMap(function(s){return s.visible.map(function(p){return p[0];});});if(!times.length)return;
 var nearest=times.reduce(function(a,b){return Math.abs(b-t)<Math.abs(a-t)?b:a;});var tip=el(c.tipId);tip.replaceChildren();var date=document.createElement('time');date.textContent=dateFmt.format(new Date(nearest));tip.appendChild(date);
 c.series.forEach(function(s){var p=at(s.points,nearest);if(!p||nearest<s.start)return;var row=document.createElement('div');row.className='tooltip-row';var label=document.createElement('span');label.style.color=s.color;label.textContent=s.name;var age=priceAge(p,nearest,s.total);if(age){var detail=document.createElement('small');detail.className='price-age';detail.textContent=age;label.appendChild(detail);}var val=document.createElement('strong');val.textContent=(c.metric===2?signed(p[c.metric]):fmt(p[c.metric]))+' PLN';row.append(label,val);tip.appendChild(row);});tip.style.display='block';
}
['value-chart','gain-chart'].forEach(function(id){el(id).addEventListener('pointermove',function(e){pointer(id,e);});el(id).addEventListener('pointerleave',function(){if(charts[id])el(charts[id].tipId).style.display='none';});});
document.querySelectorAll('[data-mode]').forEach(function(b){b.onclick=function(){state.mode=b.dataset.mode;render();};});
document.querySelectorAll('[data-range]').forEach(function(b){b.onclick=function(){state.range=b.dataset.range;render();};});
var groups={};positions.forEach(function(p){if(!groups[p.kind]){var group=document.createElement('div'),label=document.createElement('div');group.className='position-group';group.dataset.kind=p.kind;group.setAttribute('role','group');group.setAttribute('aria-label',p.kind==='manual'?'Savings & PPK':'Tickers');label.className='position-group-label';label.textContent=group.getAttribute('aria-label');group.appendChild(label);el('position-list').appendChild(group);groups[p.kind]=group;}var o=document.createElement('button'),name=document.createElement('span'),check=document.createElement('span');o.type='button';o.className='position-option';o.setAttribute('role','option');o.setAttribute('aria-selected',String(p.id===state.single));o.tabIndex=-1;o.dataset.position=p.id;name.textContent=p.name;check.className='position-check';check.setAttribute('aria-hidden','true');check.textContent='✓';o.append(name,check);o.onclick=function(){state.single=p.id;closePicker(true);render();};groups[p.kind].appendChild(o);});
el('position').onclick=function(){if(el('position-list').hidden)openPicker();else closePicker(false);};
el('position').onkeydown=function(event){if(event.key==='ArrowDown'||event.key==='ArrowUp'){event.preventDefault();openPicker();}};
el('position-list').onkeydown=function(event){var options=Array.from(this.querySelectorAll('[role=option]')),index=options.indexOf(document.activeElement);if(event.key==='Escape'){event.preventDefault();closePicker(true);}else if(['ArrowDown','ArrowUp','Home','End'].includes(event.key)){event.preventDefault();var next=event.key==='Home'?0:event.key==='End'?options.length-1:(index+(event.key==='ArrowDown'?1:-1)+options.length)%options.length;if(options[next])options[next].focus();}else if(event.key==='Tab')closePicker(false);};
document.addEventListener('click',function(event){if(!el('single-wrap').contains(event.target))closePicker(false);});
var resizeTimer;window.addEventListener('resize',function(){clearTimeout(resizeTimer);resizeTimer=setTimeout(render,100);});
function renderLoadStatus(){
 if(state.busy)return;
 var failed=positions.filter(function(p){return p.error;}),selectedFailed=selected().filter(function(p){return p.error;});
 el('status').textContent=selectedFailed.length?'Could not load '+selectedFailed.map(function(p){return p.name+' ('+p.error+')';}).join('; ')+'. Totals remain unavailable until all selected positions load.':failed.length?'Selected history loaded. Some other positions failed; you can retry them.':positions.length?'History loaded · '+positions.length+' positions. Select a line label or a position name to explore.':'Add a transaction or a savings/PPK valuation in the tracker to start.';
 el('status').classList.toggle('warning',!!selectedFailed.length);el('retry').hidden=!failed.length;el('retry').disabled=benchmarkBusy;
}
function normalizePoints(data){var byDay=new Map();data.points.forEach(function(q){byDay.set(day(q[0]),[day(q[0]),q[1],q[2],q[3]||0,q[4]||0]);});data.points=Array.from(byDay.values()).sort(function(a,b){return a[0]-b[0];});return data;}
async function loadBenchmarks(){
 if(benchmarkBusy||state.busy||!cashFlows.length)return;benchmarkBusy=true;render();
 for(var i=0;i<benchmarks.length;i++){var b=benchmarks[i];if(b.data&&!b.error&&!b.failed)continue;
  try{var result=await requestJson('/api/portfolio-position?kind=benchmark&name='+encodeURIComponent(b.symbol)),r=result.response,data=result.data;if(!r.ok)throw new Error('HTTP '+r.status);if(!data.ok)throw new Error(data.error||'ETF or FX prices unavailable');b.data=normalizePoints(data);b.error='';}
  catch(error){b.data=null;b.error=error.message;}render();
 }
 benchmarkBusy=false;render();
}
async function load(){
 if(state.busy||benchmarkBusy)return;state.busy=true;el('retry').hidden=true;render();
 // Saved account history needs no Yahoo requests. Show it first, and give a
 // directly selected position priority so unrelated tickers cannot delay it.
 var order=positions.slice().sort(function(a,b){var rank=function(p){return state.mode==='single'&&p.id===state.single?0:p.kind==='manual'?1:2;};return rank(a)-rank(b)||a.id-b.id;});
 for(var i=0;i<order.length;i++){
 var p=order[i];if(p.data&&!p.error)continue;el('status').textContent='Loading '+p.name+' · '+(i+1)+' / '+positions.length;
 try{var result=await requestJson('/api/portfolio-position?kind='+encodeURIComponent(p.kind)+'&name='+encodeURIComponent(p.name)),r=result.response,data=result.data;if(!r.ok)throw new Error('HTTP '+r.status);if(!data.ok)throw new Error(data.error||'Historical price or FX data unavailable');
 p.data=normalizePoints(data);p.error='';}
 catch(error){p.data=null;p.error=error.message;}render();
 }
 state.busy=false;render();await loadBenchmarks();
}
el('retry').onclick=load;el('benchmark-retry').onclick=loadBenchmarks;load();
})();
</script></body></html>
)PORTFOLIO";
