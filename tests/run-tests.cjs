// Native tests have no npm dependencies. Browser tests additionally use Playwright.
const fs=require('node:fs');
const os=require('node:os');
const path=require('node:path');
const assert=require('node:assert/strict');
const {execFileSync}=require('node:child_process');
const root=path.resolve(__dirname,'..');
const output=fs.mkdtempSync(path.join(os.tmpdir(),'cyd-tests-'));
const compiler=process.env.CXX||'clang++';
function compile(file,name,extra=[]){const target=path.join(output,name);execFileSync(compiler,['-std=c++17','-Wall','-Wextra','-pedantic',...extra,path.join(__dirname,file),'-o',target],{stdio:'inherit'});return target;}
const wifiManagerSource=process.env.WIFIMANAGER_SOURCE||path.join(os.homedir(),'Documents/Arduino/libraries/WiFiManager/WiFiManager.cpp');
if(fs.existsSync(wifiManagerSource)){
  const source=fs.readFileSync(wifiManagerSource,'utf8'),begin=source.indexOf('    const uint32_t heapFree = info.total_free_bytes;');
  assert.ok(begin>=0,'Apply patches/wifimanager-2.0.17-heap-debug.patch to WiFiManager before testing');
  const end=source.indexOf('    #endif',begin);
  assert.ok(end>begin);
  fs.writeFileSync(path.join(output,'wifimanager-debug-under-test.h'),source.slice(begin,end));
  const wifiDebug=compile('check_wifimanager_debug.cpp','check_wifimanager_debug',['-Wformat=2','-Werror=format','-I',output]);
  execFileSync(wifiDebug,[],{stdio:'inherit'});
}else console.log('SKIP: WiFiManager diagnostics (set WIFIMANAGER_SOURCE to the patched library source).');
const math=compile('check_math.cpp','check_math');
execFileSync(math,[],{stdio:'inherit'});
// Exercise the actual firmware models and allocations, not copied fixture types.
// A new position must never inherit random quote flags or ledger/history counts.
const firmware=fs.readFileSync(path.join(root,'CYDTicker/CYDTicker.ino'),'utf8');
assert.match(firmware,/server\.on\("\/api\/savings-ppk", HTTP_GET, handleApiSavingsPPK\)/);
assert.match(firmware,/server\.on\("\/api\/manual-investments", HTTP_GET, handleApiSavingsPPK\)/,'Retain the legacy API alias');
const allocations=[...firmware.matchAll(/new (TickerState|ManualAsset)\[([A-Z_]+)\](\{\})?/g)];
assert.ok(allocations.length>0);
for(const allocation of allocations)assert.equal(allocation[3],'{}','Value-initialize '+allocation[0]);
const constants=[...firmware.matchAll(/static constexpr int MAX_[A-Z_]+ = [^;]+;/g)].map(m=>m[0]).join('\n');
const models=firmware.slice(firmware.indexOf('struct Quote {'),firmware.indexOf('// --- APP STATE ---'));
const initializationTest=path.join(output,'check_initialization.cpp');
fs.writeFileSync(initializationTest,`#include <Arduino.h>
#include <memory>
#include <cassert>
#include "investment_math.h"
${constants}
${models}
int main(){
  std::unique_ptr<TickerState[]> tickers(${allocations.find(m=>m[1]==='TickerState')[0]});
  for(int i=0;i<MAX_TICKERS;++i){assert(!tickers[i].quote.valid);assert(tickers[i].quote.errors==0);assert(tickers[i].quote.sparkCount==0);assert(tickers[i].lotCount==0);assert(tickers[i].holdings==0);assert(tickers[i].alertHigh==0);assert(tickers[i].alertLow==0);}
  std::unique_ptr<ManualAsset[]> manual(${allocations.find(m=>m[1]==='ManualAsset')[0]});
  for(int i=0;i<MAX_MANUAL_ASSETS;++i){assert(manual[i].historyCount==0);assert(manual[i].valuePLN==0);assert(manual[i].gainPLN==0);}
}`);
const initialization=path.join(output,'check_initialization');
execFileSync(compiler,['-std=c++17','-I',path.join(__dirname,'stubs'),'-I',path.join(root,'CYDTicker'),initializationTest,'-o',initialization],{stdio:'inherit'});
execFileSync(initialization,[],{stdio:'inherit'});
console.log('PASS: new ticker/manual firmware state is initialized; every array allocation is guarded.');
const reads=firmware.slice(firmware.indexOf('bool readNumber('),firmware.indexOf('String lotVersion('))+firmware.slice(firmware.indexOf('bool readInteger('),firmware.indexOf('void handleSave()'));
const actions=firmware.slice(firmware.indexOf('void handleManualAction()'),firmware.indexOf('// Validate a prospective ledger'));
fs.writeFileSync(path.join(output,'manual-action-under-test.h'),constants+'\n'+models+'\nconstexpr time_t NTP_SYNC_MIN_EPOCH=1700000000;\nManualAsset manualAssets[MAX_MANUAL_ASSETS]{};int manualCount=0;\n'+reads+actions);
const manualCheck=compile('check_manual_action.cpp','check_manual_action',['-I',path.join(__dirname,'stubs'),'-I',output]);
execFileSync(manualCheck,[],{stdio:'inherit'});
const jsonInclude=process.env.ARDUINOJSON_INCLUDE||path.join(os.homedir(),'Documents/Arduino/libraries/ArduinoJson/src');
if(fs.existsSync(path.join(jsonInclude,'ArduinoJson.h'))){
  const storage=compile('check_storage.cpp','check_storage',['-I',path.join(__dirname,'stubs'),'-I',jsonInclude]);execFileSync(storage,[],{stdio:'inherit'});
  fs.writeFileSync(path.join(output,'yahoo-reader-under-test.h'),firmware.slice(firmware.indexOf('struct HttpSource {'),firmware.indexOf('void fetchYahoo(')));
  const transport=compile('check_yahoo_transport.cpp','check_yahoo_transport',['-I',path.join(__dirname,'stubs'),'-I',output,'-I',jsonInclude]);execFileSync(transport,[],{stdio:'inherit'});
  const globals=`TickerState tickerData[MAX_TICKERS]{};int tickerCount=0;ManualAsset manualAssets[MAX_MANUAL_ASSETS]{};int manualCount=0;uint32_t nextLotId=1;struct MutationReceipt{String id;uint32_t hash=0;};MutationReceipt receipts[32];int receiptCount=0;PersistentStore persistentStore;std::atomic<bool> redrawPending{false},fetchPending{false};AppConfig cfg={60,200,true,false,false,0,8,"1d"};constexpr time_t NTP_SYNC_MIN_EPOCH=1700000000;`;
  const part=(from,to)=>firmware.slice(firmware.indexOf(from),firmware.indexOf(to));
  fs.writeFileSync(path.join(output,'startup-models-under-test.h'),constants+'\n'+models);
  fs.writeFileSync(path.join(output,'startup-fetch-under-test.h'),part('String intervalFor(','bool isValidRange(')+part('struct NetworkLock {','struct HttpSource {')+part('void fetchYahoo(','float getRateToPLN(')+part('void pruneQuoteRates()','// --- DRAWING (TFT) ---'));
  const startup=compile('check_startup_fetch.cpp','check_startup_fetch',['-I',path.join(__dirname,'stubs'),'-I',output,'-I',jsonInclude]);execFileSync(startup,[],{stdio:'inherit'});
  fs.writeFileSync(path.join(output,'mutations-under-test.h'),constants+'\n'+models+'\n'+globals+'\n'+part('bool isValidRange(','time_t parseDateYMD(')+part('bool validTickerSymbol(','String scriptSafeJson(')+part('int getIndexBySym(','bool computePLFor(')+part('bool loadPersistentState()','// --- MARKET & NETWORK ---')+part('bool readNumber(','void handleHistPrice()'));
  const mutations=compile('check_mutations.cpp','check_mutations',['-I',path.join(__dirname,'stubs'),'-I',output,'-I',jsonInclude]);execFileSync(mutations,[],{stdio:'inherit'});
  fs.writeFileSync(path.join(output,'jobs-under-test.h'),part('struct HistoryJob {','void historyTask('));
  const jobs=compile('check_jobs.cpp','check_jobs',['-I',path.join(__dirname,'stubs'),'-I',output,'-I',jsonInclude]);execFileSync(jobs,[],{stdio:'inherit'});
  const normalize=firmware.slice(firmware.indexOf('String normalizeCurrency('),firmware.indexOf('bool validTickerSymbol('));
  const fetchHistory=firmware.slice(firmware.indexOf('struct HistoryBar {'),firmware.indexOf('void tickerProfitAt('));
  fs.writeFileSync(path.join(output,'history-under-test.h'),normalize+fetchHistory);
  const historyCheck=compile('check_history.cpp','check_history',['-I',path.join(__dirname,'stubs'),'-I',output,'-I',jsonInclude]);
  execFileSync(historyCheck,[],{stdio:'inherit'});
  fs.writeFileSync(path.join(output,'portfolio-models-under-test.h'),constants+'\n'+models+'\nconstexpr time_t NTP_SYNC_MIN_EPOCH=1700000000;');
  fs.writeFileSync(path.join(output,'backups-under-test.h'),part('void handleApiTransactions()','#include "backup_restore.h"'));
  const backups=compile('check_backups.cpp','check_backups',['-I',path.join(__dirname,'stubs'),'-I',output,'-I',jsonInclude]);execFileSync(backups,[],{stdio:'inherit'});
  const restore=compile('check_restore.cpp','check_restore',['-I',path.join(__dirname,'stubs'),'-I',output,'-I',jsonInclude]);execFileSync(restore,[],{stdio:'inherit'});
  const timeline=firmware.slice(firmware.indexOf('int makeHistoryTimeline('),firmware.indexOf('struct HistoryBar {'));
  const positionAPI=firmware.slice(firmware.indexOf('void tickerProfitAt('),firmware.indexOf('struct HistoryJob {'));
  fs.writeFileSync(path.join(output,'portfolio-api-under-test.h'),timeline+positionAPI);
  const portfolioCheck=compile('check_portfolio_position.cpp','check_portfolio_position',['-I',path.join(__dirname,'stubs'),'-I',output,'-I',jsonInclude]);
  execFileSync(portfolioCheck,[],{stdio:'inherit'});
}else console.log('SKIP: historical JSON tests (set ARDUINOJSON_INCLUDE to your ArduinoJson src folder).');
const renderer=compile('render_ui.cpp','render_ui',['-I',path.join(__dirname,'stubs')]);
const render=(view,theme='dark')=>execFileSync(renderer,[view,theme],{encoding:'utf8'});
for(const view of ['tracker','history'])for(const theme of ['dark','light']){
  const html=render(view,theme);
  assert.match(html,/aria-label=['"]Pages['"]/);
  assert.doesNotMatch(html,/[ąćęłńóśźżĄĆĘŁŃÓŚŹŻ]/,'English UI must not contain Polish interface text');
  if(view==='tracker'){
    assert.doesNotMatch(html,/href=['"]\/api\/manual-investments|manual-investments\.json/);
    assert.match(html,/class='backup-link' href='\/api\/transactions' download='transactions\.json'/);
    assert.match(html,/class='backup-link' href='\/api\/savings-ppk' download='savings-ppk\.json'/);
  }
  for(const script of html.matchAll(/<script>([\s\S]*?)<\/script>/g))new Function(script[1]);
}
console.log('PASS: native rendering of both pages and JavaScript syntax.');
const stressHtml=render('tracker-stress');
assert.equal([...stressHtml.matchAll(/aria-label='Edit transaction'|onclick='editLot\(this\)'/g)].length,60);
assert.match(stressHtml,/<\/script><\/body><\/html>/);
assert.equal([...stressHtml.matchAll(/<script>/g)].length,4);
assert.doesNotMatch(firmware,/sendContent\(FPSTR\(/,'Flash assets must not be copied into a temporary RAM String');
console.log('PASS: 60 transaction rows and all root scripts stream completely without a ledger-sized RAM chunk; saving stays disabled until initialization.');
const historyScript=[...render('history').matchAll(/<script>([\s\S]*?)<\/script>/g)].find(m=>m[1].includes('function benchmarkCash('))[1];
const rangeStart=new Function(historyScript.slice(historyScript.indexOf('function rangeStart('),historyScript.indexOf('function timeWindow('))+'return rangeStart;')();
assert.equal(rangeStart(Date.UTC(2026,9,3),'ytd'),Date.UTC(2026,0,1));
assert.equal(rangeStart(Date.UTC(2026,0,1),'ytd'),Date.UTC(2026,0,1));
assert.equal(rangeStart(Date.UTC(2027,0,1),'ytd'),Date.UTC(2027,0,1));
assert.equal(rangeStart(Date.UTC(2024,1,29),'ytd'),Date.UTC(2024,0,1));
assert.equal(rangeStart(Date.UTC(2026,9,3),'365'),Date.UTC(2025,9,3));
console.log('PASS: YTD starts on January 1, rolls over by calendar year and differs from trailing 1Y.');
const pureMath=historyScript.slice(historyScript.indexOf('function benchmarkCash('),historyScript.indexOf('// Benchmark math end.'));
const benchmarkMath=new Function('finite','at',pureMath+'return {benchmarkCash,simulateBenchmark,periodCash,simulateBenchmarkPeriod};')(
  n=>typeof n==='number'&&Number.isFinite(n),
  (points,t)=>points.filter(q=>q[0]<=t).at(-1)||null
);
const result=benchmarkMath.simulateBenchmark([[1,100],[2,100],[3,-50]],[[1,10],[2,20],[3,25],[4,30]],4);
assert.deepEqual(result,{value:390,gain:240,returnPct:120});
assert.deepEqual(benchmarkMath.simulateBenchmark([[1,100],[1,-50],[9,1000]],[[1,10],[2,20]],2),{value:100,gain:50,returnPct:50});
assert.throws(()=>benchmarkMath.simulateBenchmark([[1,100],[2,-200]],[[1,10],[2,10]],2),/cannot cover/);
assert.throws(()=>benchmarkMath.simulateBenchmark([[1,100]],[[2,10]],2),/Missing/);
assert.throws(()=>benchmarkMath.simulateBenchmark([[1,100]],[[1,null],[2,10]],2),/Missing/);
assert.throws(()=>benchmarkMath.simulateBenchmark([[1,100]],[[1,10],[2,null]],2),/Missing current/);
assert.deepEqual(benchmarkMath.simulateBenchmark([],[],2),{value:0,gain:0,returnPct:null});
console.log('PASS: benchmark same-date contributions/withdrawals, future filtering, no lookahead, missing prices and insufficient withdrawal coverage.');
const ranged=benchmarkMath.simulateBenchmarkPeriod([[1,999],[2,100],[3,-50],[4,200],[9,1000]],[[1,10],[2,20],[3,25],[4,30],[5,40]],2,5,200);
assert.ok(Math.abs(ranged.value-586.6666666666666)<1e-9);
assert.ok(Math.abs(ranged.gain-236.6666666666666)<1e-9);
assert.ok(Math.abs(ranged.returnPct-59.16666666666666)<1e-9);
assert.deepEqual(benchmarkMath.periodCash([[1,999],[2,100],[3,-50],[4,200],[9,1000]],2,5),{invested:200,withdrawn:50,net:150});
assert.deepEqual(benchmarkMath.simulateBenchmarkPeriod([[3,200]],[[2,10],[3,10],[4,10]],2,4,500),{value:700,gain:0,returnPct:0});
assert.deepEqual(benchmarkMath.simulateBenchmarkPeriod([],[[2,10],[4,8]],2,4,500),{value:400,gain:-100,returnPct:-20});
assert.deepEqual(benchmarkMath.simulateBenchmarkPeriod([[3,-100]],[[2,10],[3,10],[4,50]],2,4,100),{value:0,gain:0,returnPct:0});
assert.deepEqual(benchmarkMath.simulateBenchmarkPeriod([],[],2,4,0),{value:0,gain:0,returnPct:null});
assert.throws(()=>benchmarkMath.simulateBenchmarkPeriod([[3,-700]],[[2,10],[3,10]],2,4,500),/cannot cover/);
assert.throws(()=>benchmarkMath.simulateBenchmarkPeriod([],[[3,10]],2,4,500),/Missing/);
assert.throws(()=>benchmarkMath.simulateBenchmarkPeriod([],[[2,null],[3,10]],2,4,500),/Missing/);
console.log('PASS: period benchmarks seed equal opening capital, exclude boundary/prior/future cash flows and handle deposits, withdrawals, losses and missing data.');
if(!process.argv.includes('--ui'))process.exit(0);

async function browserTests(){
  const {chromium}=require(process.env.PLAYWRIGHT_MODULE||'playwright');
  const options={headless:true};if(process.env.CHROME_BINARY)options.executablePath=process.env.CHROME_BINARY;
  const browser=await chromium.launch(options);
  // Downloads need real HTTP responses; Chrome cancels route.fulfill downloads.
  const downloadServer=require('node:http').createServer((request,response)=>{
    const filename=request.url==='/api/transactions'?'transactions.json':request.url==='/api/savings-ppk'?'savings-ppk.json':null;
    if(!filename){response.writeHead(404);response.end();return;}
    response.writeHead(200,{'Content-Type':'application/json','Content-Disposition':'attachment; filename="'+filename+'"'});
    response.end(JSON.stringify([{backup:filename}]));
  });
  await new Promise(resolve=>downloadServer.listen(0,'127.0.0.1',resolve));
  const testOrigin='http://127.0.0.1:'+downloadServer.address().port;
  try{
    const page=await browser.newPage({viewport:{width:1280,height:1000},acceptDownloads:true}),errors=[];
    page.on('pageerror',e=>errors.push(e.message));page.on('dialog',dialog=>dialog.accept());
    const ts=date=>Date.parse(date+'T12:00:00Z')/1000;
    const dates=['2026-01-07','2026-05-01','2026-10-02'].map(ts);
    const history={
      'BTC-USD':{ok:true,start:dates[0],points:dates.map((t,i)=>[t,[10000,12000,15000][i],[0,2000,5000][i],t-(i?2:0)*86400,t-(i?1:0)*86400])},
      'PPK':{ok:true,start:dates[1],points:dates.map((t,i)=>[t,[0,8000,10000][i],[0,500,1500][i]])},
      'Toyota Bank':{ok:true,start:dates[0],points:dates.map((t,i)=>[t,[100,60458.17,76171.42][i],[0,458.17,1262.77][i]])},
      'SXR8.DE':{ok:true,start:dates[0],points:dates.map((t,i)=>[t,[100,110,120][i],0])},
      'VWCE.DE':{ok:true,start:dates[0],points:dates.map((t,i)=>[t,[100,105,115][i],0])}
    };
    let failToyota=false,failBTC=false,failBenchmark=false,failSecond=false,theme='dark',posts=[],historyRequests=[];
    let restorePreviewFails=false,dropRestoreResponse=false,previewPosts=[],restorePosts=[];
    await page.route(testOrigin+'/**',async route=>{
      const request=route.request(),url=new URL(request.url());
      if(['/api/transactions','/api/savings-ppk'].includes(url.pathname)){
        await route.continue();return;
      }
      if(url.pathname==='/favicon.svg'){await route.fulfill({contentType:'image/svg+xml',body:"<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 32 32'><path fill='#48d8a0' d='M4 20h5v8H4zm8-6h5v14h-5zm8-10h5v24h-5z'/></svg>"});return;}
      if(url.pathname==='/api/portfolio-position'){
        const name=url.searchParams.get('name');historyRequests.push(name);await route.fulfill({contentType:'application/json',body:JSON.stringify((failToyota&&name==='Toyota Bank')||(failBTC&&name==='BTC-USD')||(failBenchmark&&name==='SXR8.DE')?{ok:false,error:'EURPLN=X: HTTP 429',points:[]}:history[name])});return;
      }
      if(url.pathname==='/api/restore-preview'){
        previewPosts.push({kind:url.searchParams.get('kind'),body:request.postData()});
        const kind=url.searchParams.get('kind');
        await route.fulfill({status:restorePreviewFails?400:200,contentType:'application/json',body:JSON.stringify(restorePreviewFails?{ok:false,error:'Invalid backup entry.'}:{ok:true,revision:0,token:'preview-token-123',kind:kind,count:kind==='transactions'?3:2,replacedCount:kind==='transactions'?1:2,valuations:21,names:kind==='transactions'?['BTC-USD','AAPL']:['Toyota Bank','PPK']})});return;
      }
      if(url.pathname==='/api/restore'){
        restorePosts.push(new URLSearchParams(request.postData()));
        if(dropRestoreResponse){dropRestoreResponse=false;await route.abort('failed');return;}
        await route.fulfill({contentType:'application/json',body:'{"ok":true,"revision":1}'});return;
      }
      if(request.method()==='POST'){
        posts.push({url:url.pathname,body:new URLSearchParams(request.postData())});
        const fail=failSecond&&url.pathname==='/addlot'&&posts.filter(p=>p.url==='/addlot').length===2;
        await route.fulfill({status:fail?400:200,contentType:'application/json',body:JSON.stringify(fail?{ok:false,error:'Not enough purchased units'}:{ok:true})});return;
      }
      await route.fulfill({contentType:'text/html',body:render(url.pathname==='/portfolio'?'history':'tracker',theme)});
    });
    await page.goto(testOrigin+'/');
    assert.equal(await page.locator('[data-tracker-save]:disabled').count(),0);
    const incomplete=await browser.newPage();
    await incomplete.setContent(render('tracker').split("<script type='application/json'")[0]);
    assert.equal(await incomplete.locator('[data-tracker-save]:disabled').count(),2,'An incomplete page must not submit an empty ticker list');
    await incomplete.close();
    async function checkLeftAlignedData(){
      const fields=await page.locator('th,td,.inp').evaluateAll(elements=>elements.map(element=>({
        tag:element.tagName,id:element.id,align:getComputedStyle(element).textAlign,direction:getComputedStyle(element).direction
      })));
      assert.ok(fields.length>0);
      for(const field of fields)assert.ok(field.align==='left'||(field.align==='start'&&field.direction==='ltr'),JSON.stringify(field));
    }
    async function checkBackupAlignment(){
      const layout=await page.locator('.backup-link').evaluateAll(links=>links.map(link=>{
        const css=getComputedStyle(link),parent=getComputedStyle(link.parentElement);
        const before=link.previousSibling,text=before.textContent.trimEnd(),left=document.createRange(),right=document.createRange();
        left.setStart(before,text.length-1);left.setEnd(before,text.length);right.selectNodeContents(link);
        const a=left.getBoundingClientRect(),b=right.getClientRects()[0],lineHeight=parseFloat(parent.lineHeight);
        const lines=(b.top-a.top)/lineHeight;
        return {inline:css.display==='inline',sameFont:css.fontSize===parent.fontSize&&css.fontFamily===parent.fontFamily,baseline:css.verticalAlign==='baseline',aligned:Math.abs(lines-Math.round(lines))<0.04,sameHeight:Math.abs(a.height-b.height)<0.5};
      }));
      assert.equal(layout.length,2);
      for(const link of layout)assert.deepEqual(link,{inline:true,sameFont:true,baseline:true,aligned:true,sameHeight:true});
    }
    await checkBackupAlignment();
    assert.deepEqual(await page.locator('.backup-link').allTextContents(),['Download Backup','Download Backup']);
    for(const [href,filename] of [['/api/transactions','transactions.json'],['/api/savings-ppk','savings-ppk.json']]){
      const link=page.locator('.backup-link[href="'+href+'"]');
      assert.equal(await link.getAttribute('target'),null);
      const downloadEvent=page.waitForEvent('download');await link.click();const download=await downloadEvent;
      assert.equal(download.suggestedFilename(),filename);
      assert.deepEqual(JSON.parse(fs.readFileSync(await download.path(),'utf8')),[{backup:filename}]);
      assert.equal(page.url(),testOrigin+'/');assert.equal(page.context().pages().length,1);
    }
    console.log('PASS: both backup links download correctly named JSON files without navigation or a new tab.');
    await page.locator('#settingsOpen').click();
    assert.equal(await page.locator('#restorePanel').getAttribute('open'),null);
    assert.equal(await page.locator('#restoreKind').isVisible(),false);
    assert.ok(await page.locator('.backup-controls').evaluate(section=>section.getBoundingClientRect().height)<100,'Collapsed backups must stay compact');
    await page.locator('#settingsDialog').screenshot({path:path.join(output,'settings-backups-compact.png')});
    async function openRestorePanel(){if(!await page.locator('#restorePanel').evaluate(panel=>panel.open))await page.locator('#restoreToggle').click();}
    async function checkRestoreConfirmation(){
      const layout=await page.locator('.restore-confirm').evaluate(label=>{
        const input=label.querySelector('input'),span=label.querySelector('span');
        const box=input.getBoundingClientRect(),text=span.getBoundingClientRect(),button=document.getElementById('restoreApply').getBoundingClientRect();
        const lineHeight=parseFloat(getComputedStyle(label).lineHeight);
        return {topAligned:Math.abs(box.top+box.height/2-(text.top+lineHeight/2))<2,
          sameColor:getComputedStyle(label).color===getComputedStyle(document.getElementById('settingsDialog')).color,
          gap:button.top-label.getBoundingClientRect().bottom,checkboxWidth:box.width};
      });
      assert.equal(layout.topAligned,true,'Checkbox must align with the first text line, even when wrapped');
      assert.equal(layout.sameColor,true,'Confirmation must not inherit the low-contrast generic label color');
      assert.equal(layout.checkboxWidth,16);assert.ok(layout.gap>=10,'Keep space before the destructive action');
    }
    await openRestorePanel();
    assert.equal(await page.locator('#restoreKind').evaluate(select=>getComputedStyle(select).fontSize),await page.locator('[name="bright"]').evaluate(input=>getComputedStyle(input).fontSize));
    const backupFile={name:'transactions.json',mimeType:'application/json',buffer:Buffer.from('[{"symbol":"BTC-USD","date":"2026-01-01","qty":1,"pricePLN":100}]')};
    await page.locator('#restoreFile').setInputFiles(backupFile);
    await page.locator('#restorePreview').click();await page.waitForFunction(()=>!document.getElementById('restoreDetails').hidden);
    assert.match(await page.locator('#restoreSummary').textContent(),/Replace 1 saved transactions with 3 transactions/);
    assert.match(await page.locator('#restoreNames').textContent(),/BTC-USD, AAPL/);
    assert.equal(await page.locator('#restoreApply').isDisabled(),true);
    assert.equal(restorePosts.length,0,'Preview must not restore financial data');
    assert.match(previewPosts[0].body,/name="backup"; filename="transactions.json"/);
    await page.locator('#restoreConfirm').check();dropRestoreResponse=true;
    await page.locator('#restoreApply').click();await page.waitForFunction(()=>document.getElementById('restoreStatus').textContent.includes('response was lost'));
    assert.equal(await page.locator('#restoreFile').isDisabled(),true);assert.equal(await page.locator('#restoreKind').isDisabled(),true);
    assert.equal(await page.locator('#restoreApply').textContent(),'Retry Restore');
    await page.locator('#restoreToggle').click();assert.equal(await page.locator('#restorePanel').evaluate(panel=>panel.open),true,'Keep uncertain restore feedback visible');
    await page.locator('#restoreApply').click();await page.waitForFunction(()=>!document.getElementById('settingsDialog').open);
    assert.equal(restorePosts.length,2);assert.equal(restorePosts[0].toString(),restorePosts[1].toString());
    assert.equal(restorePosts[0].get('confirm'),'replace');assert.ok(restorePosts[0].get('rid').length>=16);
    await page.locator('#settingsOpen').click();await openRestorePanel();await page.locator('#restoreKind').selectOption('savings-ppk');
    await page.locator('#restoreFile').setInputFiles({name:'savings-ppk.json',mimeType:'application/json',buffer:Buffer.from('[]')});
    restorePreviewFails=true;await page.locator('#restorePreview').click();await page.waitForFunction(()=>document.getElementById('restoreStatus').textContent.includes('Invalid backup entry'));
    assert.equal(await page.locator('#restoreDetails').isVisible(),false);assert.equal(restorePosts.length,2);
    restorePreviewFails=false;await page.locator('#restorePreview').click();await page.waitForFunction(()=>!document.getElementById('restoreDetails').hidden);
    assert.match(await page.locator('#restoreSummary').textContent(),/Replace 2 saved accounts with 2 accounts and 21 valuations/);
    assert.match(await page.locator('#restoreConfirmLabel').textContent(),/Savings & PPK accounts and history/);
    await checkRestoreConfirmation();
    await page.locator('#restoreConfirmLabel').click();assert.equal(await page.locator('#restoreConfirm').isChecked(),true,'Confirmation text toggles the checkbox');
    await page.locator('#restoreConfirmLabel').click();assert.equal(await page.locator('#restoreApply').isDisabled(),true);
    await page.locator('#restoreDetails').screenshot({path:path.join(output,'restore-confirmation-desktop.png')});
    await page.locator('#restoreKind').selectOption('transactions');assert.equal(await page.locator('#restoreDetails').isVisible(),false);
    await page.locator('#restoreToggle').click();assert.equal(await page.locator('#restoreFile').isVisible(),false);
    await page.locator('#settingsClose').click();
    console.log('PASS: restore file upload/preview, explicit category replacement, invalid backup feedback and unchanged retries after a lost response.');
    assert.equal(await page.locator('#transactions').isVisible(),true);
    assert.equal(await page.locator('#cfgform').isVisible(),true);
    assert.equal(await page.locator('#manualAccounts').isVisible(),true);
    assert.equal(await page.locator('#manualAccounts button').count(),0);
    assert.equal(await page.locator('#manualRows tr').first().locator('td').count(),4);
    assert.deepEqual(await page.locator('#manualRows a').allTextContents(),['Toyota Bank','PPK']);
    assert.equal(await page.locator('#manualAdd').textContent(),'+ Add Savings / PPK');
    assert.equal(await page.locator('#manualRows td').evaluateAll(cells=>cells.every(cell=>getComputedStyle(cell).verticalAlign==='top')),true);
    const amountAlignment=await page.locator('#manualRows tr').first().evaluate(row=>{
      const nameRange=document.createRange(),amountRange=document.createRange();
      nameRange.selectNodeContents(row.querySelector('.account-name'));amountRange.selectNodeContents(row.cells[1]);
      return {delta:Math.abs(nameRange.getBoundingClientRect().top-amountRange.getBoundingClientRect().top),align:getComputedStyle(row.cells[1]).textAlign};
    });
    assert.ok(amountAlignment.delta<1);assert.equal(amountAlignment.align,'left');
    await checkLeftAlignedData();
    await page.locator('#manualAccounts').screenshot({path:path.join(output,'accounts-desktop.png')});
    assert.equal(await page.locator('[data-page]').count(),0);
    assert.equal(await page.locator('.page-tabs a').count(),2);
    assert.deepEqual(await page.locator('h3:visible').allTextContents(),['Live Prices','Tickers','Chart Period','Transactions (Cost Basis)','Savings & PPK','Holdings & Alerts']);
    assert.equal(await page.locator('#settingsDialog').isVisible(),false);
    await page.locator('#settingsOpen').click();assert.equal(await page.locator('#settingsDialog').isVisible(),true);
    await page.locator('[name=bright]').fill('180');
    assert.equal(await page.evaluate(()=>document.querySelector('[name=bright]').form.id),'cfgform');
    await page.locator('#settingsClose').click();assert.equal(await page.locator('#settingsDialog').isVisible(),false);
    assert.equal(await page.evaluate(()=>new FormData(document.getElementById('cfgform')).get('bright')),'180');
    await page.locator('#settingsOpen').click();await page.keyboard.press('Escape');assert.equal(await page.locator('#settingsDialog').isVisible(),false);
    assert.equal(await page.locator('.portfolio-link').count(),0);
    await page.screenshot({path:path.join(output,'tracker.png'),fullPage:true});
    await page.locator('#tickerAdd').click();
    assert.equal(await page.locator('#tl input').count(),3);
    // Untrusted text never becomes markup, even during a rerender.
    await page.locator('#tl input').last().fill('\"><img src=x onerror=alert(1)>');
    await page.locator('#tickerAdd').click();assert.equal(await page.locator('#tl img').count(),0);
    const openAccount=async(id,action)=>{await page.locator('[data-account-id="'+id+'"]').click();if(action)await page.locator('#manualAction').selectOption(action);};
    await openAccount(0);
    await page.locator('#manualDeposit').fill('5000');await page.locator('#manualInterest').fill('250');
    const preview=()=>page.locator('#manualPreview').textContent().then(s=>s.replace(/[\s\u00a0\u202f]/g,''));
    assert.match(await preview(),/81421,42PLN/);assert.match(await preview(),/1512,77PLN/);
    await page.screenshot({path:path.join(output,'savings-desktop.png')});
    await page.locator('#manualSubmit').click();await page.waitForFunction(()=>!document.getElementById('manualDialog').open);
    const savingsPost=posts.find(p=>p.url==='/manual-action');
    assert.equal(savingsPost.body.get('deposit'),'5000');assert.equal(savingsPost.body.get('interest'),'250');
    assert.equal(savingsPost.body.get('version'),'toyota-v1');assert.equal(savingsPost.body.get('id'),'0');
    await openAccount(1);
    await page.locator('#manualEmployee').fill('669.42');await page.locator('#manualEmployer').fill('502.07');
    assert.match(await preview(),/1171,49PLN/);assert.match(await preview(),/profit\/loss0,00PLN/);
    await page.locator('#manualValue').fill('1180');assert.match(await preview(),/profit\/loss8,51PLN/);
    await page.locator('#manualSubmit').click();await page.waitForFunction(()=>!document.getElementById('manualDialog').open);
    const ppkPost=posts.filter(p=>p.url==='/manual-action').at(-1);
    assert.equal(ppkPost.body.get('op'),'ppk-deposit');assert.equal(ppkPost.body.get('value'),'1180');
    assert.equal(ppkPost.body.get('employee'),'669.42');assert.equal(ppkPost.body.get('employer'),'502.07');
    await openAccount(1,'valuation');
    assert.equal(await page.locator('#manualPPK').isVisible(),false);
    await page.locator('#manualValue').fill('1200');await page.locator('#manualSubmit').click();
    assert.match(await page.locator('#manualStatus').textContent(),/Record PPK contributions first/);
    await page.locator('#manualClose').click();
    await openAccount(0,'valuation');
    await page.locator('#manualValue').fill('76000');assert.match(await preview(),/profit\/loss1091,35PLN/);
    await page.locator('#manualClose').click();
    await openAccount(0,'edit');
    await page.locator('#manualName').fill('Toyota renamed');
    await page.locator('#manualSubmit').click();await page.waitForFunction(()=>!document.getElementById('manualDialog').open);
    assert.equal(posts.filter(p=>p.url==='/manual-action').at(-1).body.get('id'),'0');
    await openAccount(1,'edit');
    assert.equal(await page.locator('#manualName').isVisible(),true);
    assert.equal(await page.locator('#manualName').inputValue(),'PPK');
    await page.locator('#manualClose').click();
    await openAccount(0,'delete');
    assert.equal(await page.locator('#manualDate').isVisible(),false);
    assert.equal(await page.locator('#manualDeleteNotice').isVisible(),true);
    await page.locator('#manualSubmit').click();await page.waitForFunction(()=>!document.getElementById('manualDialog').open);
    const deletePost=posts.filter(p=>p.url==='/manual-action').at(-1);
    assert.equal(deletePost.body.get('op'),'delete');assert.equal(deletePost.body.get('id'),'0');assert.equal(deletePost.body.get('version'),'toyota-v1');
    await page.locator('#manualAdd').click();
    assert.equal(await page.locator('#manualTitle').textContent(),'Add Savings / PPK');
    assert.equal(await page.locator('#manualKind option[value=savings]').textContent(),'Savings');
    assert.equal(await page.locator('#manualActionPicker').isVisible(),false);
    assert.equal(await page.locator('#manualName').isVisible(),true);
    assert.equal(await page.locator('#manualActionForm').evaluate(form=>form.checkValidity()),false);
    await page.locator('#manualName').fill('Savings test');await page.locator('#manualKind').selectOption('ppk');
    assert.equal(await page.locator('#manualName').isVisible(),false);
    assert.equal(await page.locator('#manualName').isDisabled(),true);
    assert.equal(await page.locator('#manualName').inputValue(),'PPK');
    await page.locator('#manualKind').selectOption('savings');
    assert.equal(await page.locator('#manualName').isVisible(),true);
    assert.equal(await page.locator('#manualName').inputValue(),'Savings test');
    await page.locator('#manualKind').selectOption('ppk');
    await page.locator('#manualOpeningValue').fill('1194.93');await page.locator('#manualCapital').fill('1171.49');
    assert.match(await preview(),/profit\/loss23,44PLN/);
    assert.equal(await page.locator('#manualActionForm').evaluate(form=>form.checkValidity()),true);
    await page.screenshot({path:path.join(output,'add-ppk-desktop.png')});
    await page.locator('#manualSubmit').click();await page.waitForFunction(()=>!document.getElementById('manualDialog').open);
    assert.equal(posts.filter(p=>p.url==='/manual-action').at(-1).body.get('op'),'create');
    assert.equal(posts.filter(p=>p.url==='/manual-action').at(-1).body.get('kind'),'ppk');
    assert.equal(posts.filter(p=>p.url==='/manual-action').at(-1).body.get('name'),'PPK');
    assert.equal(posts.filter(p=>p.url==='/manual-action').at(-1).body.get('value'),'1194.93');
    await page.screenshot({path:path.join(output,'tracker-desktop.png'),fullPage:true});
    await page.setViewportSize({width:390,height:844});assert.equal(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth),true);
    await checkBackupAlignment();
    assert.equal(await page.locator('#manualRows tr').first().locator('td').nth(1).evaluate(cell=>getComputedStyle(cell).textAlign),'left');
    await checkLeftAlignedData();
    await page.locator('#manualAccounts').screenshot({path:path.join(output,'accounts-mobile.png')});
    await page.screenshot({path:path.join(output,'tracker-mobile.png'),fullPage:true});
    await page.locator('#settingsOpen').click();await openRestorePanel();await page.locator('#restoreKind').selectOption('savings-ppk');
    await page.locator('#restoreFile').setInputFiles({name:'savings-ppk.json',mimeType:'application/json',buffer:Buffer.from('[]')});
    await page.locator('#restorePreview').click();await page.waitForFunction(()=>!document.getElementById('restoreDetails').hidden);
    await page.locator('.backup-controls').scrollIntoViewIfNeeded();
    await checkRestoreConfirmation();
    assert.equal(await page.locator('#settingsDialog').evaluate(dialog=>dialog.scrollWidth<=dialog.clientWidth),true);
    await page.locator('#settingsDialog').screenshot({path:path.join(output,'restore-mobile.png')});
    await page.locator('#settingsClose').click();
    await openAccount(1);
    await page.locator('#manualEmployee').fill('669.42');await page.locator('#manualEmployer').fill('502.07');await page.locator('#manualValue').fill('1180');
    assert.equal(await page.evaluate(()=>document.getElementById('manualDialog').getBoundingClientRect().width<=innerWidth),true);
    await page.screenshot({path:path.join(output,'ppk-mobile.png')});await page.keyboard.press('Escape');
    await page.locator('#settingsOpen').click();
    assert.equal(await page.evaluate(()=>document.getElementById('settingsDialog').getBoundingClientRect().width<=innerWidth),true);
    await page.screenshot({path:path.join(output,'settings-mobile.png')});await page.keyboard.press('Escape');
    await page.locator('#transactions .editlink').click();assert.equal(await page.locator('#txDate1').inputValue(),'2026-01-07');
    assert.equal(await page.locator('#txAddBtn').isVisible(),false);await page.locator('#txCancelBtn').click();
    assert.equal(await page.locator('#txAddBtn').isVisible(),true);
    // One successful and one failed write: keep only the failed row, do not
    // reload and do not post the successful row a second time when retrying.
    for(const input of [['txDate2','2026-01-07'],['txQty2','1'],['txPrice2','100']])await page.locator('#'+input[0]).fill(input[1]);
    await page.locator('#txAddBtn').click();
    for(const input of [['txDate3','2026-01-08'],['txQty3','-2'],['txPrice3','110']])await page.locator('#'+input[0]).fill(input[1]);
    failSecond=true;await page.locator('#txSubmitBtn').click();
    await page.waitForFunction(()=>document.getElementById('fetchStatus').textContent.includes('1 saved, 1 failed'));
    assert.equal(await page.locator('.trow-group').count(),1);assert.equal(await page.locator('#txQty3').inputValue(),'-2');
    failSecond=false;await page.locator('#txQty3').fill('-1');await page.locator('#txSubmitBtn').click();
    await page.waitForFunction(()=>document.querySelectorAll('.trow-group').length===1&&document.getElementById('fetchStatus').textContent==='');
    assert.equal(posts.filter(p=>p.url==='/addlot').length,3);
    assert.equal(posts.filter(p=>p.url==='/addlot')[2].body.get('lq'),'-1');
    await page.locator('.dellink').click();await page.waitForFunction(()=>document.getElementById('fetchStatus').textContent==='');
    assert.equal(posts.at(-1).url,'/dellot');
    historyRequests=[];await page.locator('.page-tabs a').filter({hasText:'Charts'}).click();
    await page.waitForFunction(()=>document.getElementById('status').textContent.includes('History loaded'));
    assert.deepEqual(new Set(historyRequests.slice(0,2)),new Set(['Toyota Bank','PPK']));
    await page.locator('[data-mode=single]').click();
    await page.locator('#position').click();
    const toyotaOption=page.locator('#position-list [role=option]').filter({hasText:'Toyota Bank'});
    const selectedOption=page.locator('#position-list [aria-selected=true]');
    assert.equal(await selectedOption.locator('.position-check').evaluate(check=>getComputedStyle(check).visibility),'visible');
    assert.equal(await selectedOption.evaluate(option=>{const name=option.children[0].getBoundingClientRect(),tick=option.children[1].getBoundingClientRect();return tick.left>name.right;}),true);
    await page.screenshot({path:path.join(output,'position-picker.png')});await toyotaOption.click();
    assert.equal(await page.locator('[data-mode=single]').getAttribute('aria-pressed'),'true');
    assert.match(await page.locator('#value-title').textContent(),/Toyota Bank/);
    await page.locator('[data-mode=total]').click();
    assert.equal(await page.locator('#legend .badge').count(),0);
    assert.equal(await page.locator('#legend button').count(),3);
    assert.equal(await page.locator('#position-list .position-group[data-kind=manual] [role=option]').count(),2);
    const normalized=id=>page.locator('#'+id).textContent().then(s=>s.replace(/[\s\u00a0\u202f]/g,''));
    assert.equal(await normalized('value'),'101171,42PLN');assert.equal(await normalized('gain'),'+7762,77PLN');assert.equal(await normalized('capital'),'93408,65PLN');
    await page.waitForFunction(()=>document.getElementById('benchmark-status').textContent==='Same cash flows compared in PLN.');
    const benchmarkRows=()=>page.locator('#benchmarks tr').evaluateAll(rows=>rows.map(row=>Array.from(row.cells).slice(1).map(cell=>cell.textContent.replace(/[\s\u00a0\u202f]/g,''))));
    assert.deepEqual(await benchmarkRows(),[['15000,00','+5000,00','+50,00%','—'],['12000,00','+2000,00','+20,00%','+3000,00'],['11500,00','+1500,00','+15,00%','+3500,00']]);
    const callsBeforePeriod=historyRequests.length;
    await page.locator('.toolbar [data-range="ytd"]').click();
    assert.equal(await page.locator('#benchmark-card [data-range="ytd"]').getAttribute('aria-pressed'),'true');
    assert.match(await page.locator('#period-label').textContent(),/^01 Jan 26/);
    // This fixture's first purchase is Jan 7: no pre-purchase money is invented.
    assert.match(await page.locator('#benchmark-period').textContent(),/^07 Jan 26/);
    assert.deepEqual(await benchmarkRows(),[['15000,00','+5000,00','+50,00%','—'],['12000,00','+2000,00','+20,00%','+3000,00'],['11500,00','+1500,00','+15,00%','+3500,00']]);
    await page.locator('#benchmark-card [data-range="ytd"]').click();
    assert.equal(await page.locator('.toolbar [data-range="ytd"]').getAttribute('aria-pressed'),'true');
    assert.equal(historyRequests.length,callsBeforePeriod);
    await page.locator('.toolbar [data-range="90"]').click();
    assert.equal(await page.locator('#benchmark-card [data-range="90"]').getAttribute('aria-pressed'),'true');
    assert.match(await page.locator('#benchmark-period').textContent(),/Opening value/);
    assert.equal(await page.locator('#benchmark-gain-heading').textContent(),'Period P&L · PLN');
    assert.deepEqual(await benchmarkRows(),[['15000,00','+3000,00','+25,00%','—'],['13090,91','+1090,91','+9,09%','+1909,09'],['13142,86','+1142,86','+9,52%','+1857,14']]);
    await checkLeftAlignedData();
    await page.locator('#benchmark-card').screenshot({path:path.join(output,'benchmarks-period-mobile.png')});
    await page.locator('#benchmark-card [data-range="180"]').click();
    assert.equal(await page.locator('.toolbar [data-range="180"]').getAttribute('aria-pressed'),'true');
    assert.equal(await page.locator('[data-range="180"].active').count(),2);
    assert.equal(historyRequests.length,callsBeforePeriod,'Period changes must not issue extra Yahoo requests');
    await page.locator('#benchmark-card [data-range="all"]').click();
    assert.equal(await page.locator('.toolbar [data-range="all"]').getAttribute('aria-pressed'),'true');
    assert.deepEqual(await benchmarkRows(),[['15000,00','+5000,00','+50,00%','—'],['12000,00','+2000,00','+20,00%','+3000,00'],['11500,00','+1500,00','+15,00%','+3500,00']]);
    console.log('PASS: shared chart/benchmark period controls stay synchronized and recalculate period returns without extra requests; ALL retains lifetime results.');
    assert.equal(await page.locator('#value-title').textContent(),'Portfolio & Position Values');
    assert.equal(await page.locator('#gain-title').textContent(),'Profit & Loss by Position');
    await page.locator('[data-mode=compare]').click();await page.locator('#legend button').filter({hasText:'Toyota Bank'}).click();
    assert.equal(await normalized('value'),'25000,00PLN');assert.equal(await normalized('gain'),'+6500,00PLN');
    assert.equal((await benchmarkRows())[0][0],'15000,00');
    await page.goto(testOrigin+'/portfolio?kind=manual&name=PPK');
    await page.waitForFunction(()=>document.getElementById('status').textContent.includes('History loaded'));
    assert.equal(await page.locator('[data-mode=single]').getAttribute('aria-pressed'),'true');assert.equal(await normalized('value'),'10000,00PLN');
    await page.locator('#position').focus();await page.keyboard.press('ArrowDown');await page.keyboard.press('Home');await page.keyboard.press('Enter');
    assert.match(await page.locator('#value-title').textContent(),/BTC-USD/);
    const chartWidth=await page.locator('#value-chart').evaluate(canvas=>canvas.getBoundingClientRect().width);
    await page.locator('#value-chart').hover({position:{x:chartWidth-16,y:80}});
    await page.waitForFunction(()=>document.getElementById('value-tip').textContent.includes('Price from 30 Sept 26'));
    assert.match(await page.locator('#value-tip .price-age').textContent(),/FX from 01 Oct 26/);
    await page.screenshot({path:path.join(output,'carried-price-tooltip.png')});
    await page.locator('#gain-chart').hover({position:{x:chartWidth-16,y:80}});
    await page.waitForFunction(()=>document.getElementById('gain-tip').textContent.includes('Price from 30 Sept 26'));
    assert.match(await page.locator('#gain-tip .price-age').textContent(),/FX from 01 Oct 26/);
    await page.locator('#position').click();await page.keyboard.press('Escape');assert.equal(await page.locator('#position-list').isVisible(),false);
    await page.locator('#position').click();await page.locator('#position-list [role=option]').filter({hasText:'PPK'}).click();
    await page.locator('.toolbar [data-range="30"]').click();await page.locator('#value-chart').hover();
    await page.waitForFunction(()=>document.getElementById('value-tip').style.display==='block');assert.match(await page.locator('#value-tip').textContent(),/PPK/);
    assert.equal(await page.locator('#value-tip .price-age').count(),0,'PPK valuations must not show a market-price freshness warning');
    assert.equal(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth),true);
    await page.screenshot({path:path.join(output,'history-mobile.png'),fullPage:true});
    await page.setViewportSize({width:1280,height:1000});await page.locator('[data-mode=total]').click();await page.locator('.toolbar [data-range=all]').click();
    await page.locator('#benchmark-card [data-range="90"]').click();
    await checkLeftAlignedData();
    await page.locator('#benchmark-card').screenshot({path:path.join(output,'benchmarks-period-desktop.png')});
    await page.locator('.toolbar [data-range=all]').click();
    await page.screenshot({path:path.join(output,'history-desktop.png'),fullPage:true});
    failBenchmark=true;await page.goto(testOrigin+'/portfolio');
    await page.waitForFunction(()=>document.getElementById('benchmark-status').textContent.includes('HTTP 429'));
    assert.equal(await page.locator('#value').textContent(),'101 171,42 PLN');
    assert.equal((await benchmarkRows())[1][0],'—');failBenchmark=false;await page.locator('#benchmark-retry').click();
    await page.waitForFunction(()=>document.getElementById('benchmark-status').textContent==='Same cash flows compared in PLN.');
    failBTC=true;historyRequests=[];await page.goto(testOrigin+'/portfolio?kind=manual&name=PPK');
    await page.waitForFunction(()=>document.getElementById('status').textContent.includes('Selected history loaded'));
    assert.equal(historyRequests[0],'PPK');assert.equal(await normalized('value'),'10000,00PLN');assert.equal(await normalized('gain'),'+1500,00PLN');
    assert.equal(await page.evaluate(()=>document.getElementById('status').classList.contains('warning')),false);
    await page.locator('[data-mode=total]').click();assert.equal(await page.locator('#value').textContent(),'—');
    assert.match(await page.locator('#status').textContent(),/Could not load BTC-USD/);failBTC=false;
    failToyota=true;await page.goto(testOrigin+'/portfolio');await page.waitForFunction(()=>document.getElementById('status').textContent.includes('Could not load'));
    assert.match(await page.locator('#status').textContent(),/EURPLN=X: HTTP 429/);
    assert.equal(await page.locator('#value').textContent(),'—');failToyota=false;await page.locator('#retry').click();
    await page.waitForFunction(()=>document.getElementById('status').textContent.includes('History loaded'));assert.equal(await normalized('gain'),'+7762,77PLN');
    history['BTC-USD'].points[2]=[dates[2],null,null];await page.reload();await page.waitForFunction(()=>document.getElementById('status').textContent.includes('History loaded'));
    assert.equal(await page.locator('#value').textContent(),'—');
    theme='light';await page.locator('.page-tabs a').filter({hasText:'Tracker'}).click();
    assert.equal(await page.evaluate(()=>getComputedStyle(document.body).backgroundColor),'rgb(240, 240, 245)');
    await page.screenshot({path:path.join(output,'tracker-light.png'),fullPage:true});
    assert.deepEqual(errors,[]);
    // Actual browser: asynchronous job polling, changed-input race and a POST
    // that was committed by the server but whose response was lost.
    const audit=await browser.newPage();let held=null,releasePoll,stored=new Map(),attempts=[],lose=true;
    const pollArrived=()=>new Promise(resolve=>{releasePoll=resolve;});
    await audit.route('http://audit.test/**',async route=>{
      const request=route.request(),url=new URL(request.url());
      if(url.pathname==='/api/histprice'){await route.fulfill({status:202,contentType:'application/json',body:'{"job":"audit-job"}'});return;}
      if(url.pathname==='/api/history-job'){held=route;if(releasePoll)releasePoll();return;}
      if(request.method()==='POST'){
        const body=new URLSearchParams(request.postData());attempts.push(body);const id=body.get('rid');if(!stored.has(id))stored.set(id,body);
        if(lose){lose=false;await route.abort('failed');return;}
        await route.fulfill({contentType:'application/json',body:JSON.stringify({ok:true,revision:stored.size})});return;
      }
      await route.fulfill({contentType:'text/html',body:render('tracker')});
    });
    await audit.goto('http://audit.test/');await audit.locator('#txDate0').fill('2026-01-07');
    let arrived=pollArrived();await audit.locator('#txFetch0').click();await arrived;
    await audit.locator('#txSym0').selectOption('AAPL');await audit.locator('#txDate0').fill('2026-05-01');
    await held.fulfill({contentType:'application/json',body:'{"ok":true,"pricePLN":200000}'});
    await audit.waitForFunction(()=>document.getElementById('txRowStatus0').textContent.includes('discarded'));
    assert.equal(await audit.locator('#txPrice0').inputValue(),'');
    arrived=pollArrived();await audit.locator('#txFetch0').click();await arrived;
    await audit.locator('#txPrice0').fill('123.45');await held.fulfill({contentType:'application/json',body:'{"ok":true,"pricePLN":200000}'});
    await audit.waitForFunction(()=>!document.getElementById('txFetch0').disabled);assert.equal(await audit.locator('#txPrice0').inputValue(),'123.45');
    await audit.locator('#txQty0').fill('1');await audit.locator('#txSubmitBtn').click();
    await audit.waitForFunction(()=>document.getElementById('fetchStatus').textContent.includes('lost'));
    assert.equal(stored.size,1);assert.equal(attempts.length,1);assert.match(attempts[0].get('rid'),/^[a-f0-9]{32}$/);
    await audit.locator('#txSubmitBtn').click();await audit.waitForFunction(()=>document.getElementById('fetchStatus').textContent==='');
    assert.equal(stored.size,1);assert.equal(attempts.length,2);assert.equal(attempts[0].get('rid'),attempts[1].get('rid'));
    await audit.close();
    console.log('PASS: browser Fetch races/manual-price preservation, asynchronous polling and unchanged request IDs after a committed-but-lost POST response.');
    console.log('PASS: navigation, root forms, escaping, manual IDs, transaction edits/deletes, partial-save retry, chart totals, deep links, filters, tooltips, missing data, dark/light theme and mobile layout.');
    console.log('Screenshots and compiled host-test binaries:',output);
  }finally{await browser.close();await new Promise(resolve=>downloadServer.close(resolve));}
}
browserTests().catch(error=>{console.error(error);process.exitCode=1;});
