#pragma once
#include <Arduino.h>

#define FAVICON_LINK "<link rel='icon' type='image/svg+xml' href='/favicon.svg'>"

String buildRedirectPage(bool dark, const char* emoji, const char* message) {
  const char* rbg = dark ? "#0a0a0f" : "#f0f0f5";
  const char* rfg = dark ? "#00cc44" : "#1a1a2e";
  return String(F("<!DOCTYPE html><html><head><meta charset='UTF-8'>"))
    + F("<meta http-equiv='refresh' content='2;url=/'>" FAVICON_LINK "<style>")
    + "body{font-family:'SF Mono',monospace;background:" + rbg + ";color:" + rfg
    + F(";padding:40px;text-align:center;display:flex;align-items:center;")
    + F("justify-content:center;height:100vh;margin:0;flex-direction:column}")
    + F("h1{font-size:48px;margin-bottom:16px}p{font-size:20px;opacity:0.8}")
    + F("</style></head><body>")
    + "<h1>" + emoji + "</h1><p>" + message + "</p>"
    + F("<p style='position:fixed;bottom:24px;left:0;right:0;font-size:13px;opacity:0.4;text-align:center'>Redirecting...</p>")
    + F("</body></html>");
}

String buildRootHtml(
  bool dark, bool portfolio,
  const String& rows, const String& holdRows, const String& txRows, const String& tickerOptions,
  const String& tickerList,
  double totalVal, double totalPL, bool anyMissing,
  double totalRealPL, bool anyRealPl,
  int refreshSec, int brightness, int minRefresh, int defaultRefresh,
  bool nightMode, int nightFrom, int nightTo,
  const String& chartRange, const String& rangeLabel)
{
  const char* bg = dark ? "#0a0a0f" : "#f0f0f5";
  const char* card = dark ? "#13131a" : "#ffffff";
  const char* bord = dark ? "#222230" : "#d0d0e0";
  const char* text = dark ? "#c8ccd4" : "#1a1a2e";
  const char* muted = dark ? "#555" : "#888";
  const char* inp = dark ? "#1c1c28" : "#f8f8ff";
  const char* ibord = dark ? "#2a2a40" : "#b0b0cc";
  const char* hint = dark ? "#444" : "#999";
  const char* rmbg = dark ? "#2a0a0a" : "#fee2e2";
  const char* rmclr = dark ? "#ff6666" : "#b91c1c";

  String dmChk = dark ? " checked" : "";
  String pmChk = portfolio ? " checked" : "";
  String nmChk = nightMode ? " checked" : "";

  String h = F("<!DOCTYPE html><html><head>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<meta charset='UTF-8'><title>Portfolio Tracker</title>"
    FAVICON_LINK
    "<style>*{box-sizing:border-box;margin:0;padding:0}");
  h += "body{font-family:'SF Mono','Fira Mono',monospace;background:" + String(bg)
    + ";color:" + text + ";padding:20px 14px;max-width:900px;margin:0 auto;overflow-x:hidden}";
  h += "h1{font-size:10px;letter-spacing:4px;text-transform:uppercase;color:" + String(muted) + ";margin-bottom:3px}";
  h += "h2{font-size:22px;font-weight:700;margin-bottom:18px}";
  h += "h3{font-size:10px;letter-spacing:3px;text-transform:uppercase;color:" + String(muted) + ";margin-bottom:10px}";
  h += ".card{background:" + String(card) + ";border:1px solid " + bord + ";border-radius:10px;padding:16px;margin-bottom:14px}";
  h += "label{display:block;font-size:11px;color:" + String(muted) + ";margin:10px 0 3px}";
  h += ".inp{width:100%;padding:7px 9px;background:" + String(inp)
    + ";border:1px solid " + ibord + ";color:" + text + ";border-radius:6px;font-family:inherit;font-size:12px;outline:none}";
  h += ".inp:focus{border-color:#0af}";
  h += "input[type=checkbox]{width:16px;height:16px;accent-color:#0080ff}";
  h += ".hint{font-size:10px;color:" + String(hint) + ";margin-top:4px;line-height:1.5}";
  h += ".hint2{font-size:9px;color:" + String(hint) + ";opacity:.8}";
  h += ".row{display:flex;align-items:center;gap:8px;margin-top:10px}.row label{margin:0}";
  h += "button{margin-top:14px;width:100%;padding:12px;background:#0080ff;color:#fff;border:none;"
    "border-radius:8px;font-size:14px;font-weight:600;font-family:inherit;cursor:pointer}";
  h += "button:hover{background:#0062cc}";
  h += ".rm{margin:0;width:26px;padding:5px 0;font-size:11px;background:" + String(rmbg)
    + ";color:" + rmclr + ";border:1px solid " + rmclr + ";border-radius:5px;flex-shrink:0}";
  h += ".rm:hover{background:" + String(rmclr) + ";color:#fff}";
  h += ".addbtn{margin-top:6px;width:auto;padding:7px 14px;font-size:11px;"
    "background:transparent;color:#0af;border:1px solid #0af;border-radius:5px}";
  h += ".addbtn:hover{background:#0af;color:#000}";
  h += ".trow{display:flex;gap:6px;margin-bottom:5px;align-items:center}";
  h += ".trow .inp{flex:1;width:auto;min-width:0}";
  h += ".tidx{font-size:10px;color:" + String(muted) + ";min-width:12px;text-align:right}";
  h += ".rg{display:flex;flex-wrap:wrap;gap:6px;margin-top:8px}";
  h += ".rb{margin:0;width:auto;padding:6px 11px;font-size:11px;font-weight:600;"
    "background:transparent;color:" + String(muted) + ";border:1px solid " + bord + ";border-radius:5px}";
  h += ".rb:hover{border-color:#0af;color:#0af}";
  h += ".ra{background:#0080ff!important;color:#fff!important;border-color:#0080ff!important}";
  h += ".tbl-wrap{overflow-x:auto;-webkit-overflow-scrolling:touch;max-width:100%}";
  h += "table{border-collapse:collapse;font-size:11px;width:100%}";
  h += "th{font-size:9px;letter-spacing:2px;text-transform:uppercase;color:" + String(muted)
    + ";text-align:left;padding:5px 2px;border-bottom:1px solid " + bord + ";white-space:nowrap}";
  h += "td{padding:5px 2px;border-bottom:1px solid " + String(bord) + "}";
  h += ".tnowrap{white-space:nowrap}";
  h += ".chg{font-size:10px;white-space:nowrap}";
  h += ".dellink{color:" + String(rmclr) + ";text-decoration:none;font-size:13px;padding:2px 6px}";
  h += ".editlink{color:#0af;text-decoration:none;font-size:13px;padding:2px 6px}";
  h += ".meta{font-size:11px;color:" + String(muted) + ";margin-top:6px}";
  h += ".meta strong{color:" + String(text) + "}";
  h += ".pl-pos{color:#00cc44;font-weight:bold}.pl-neg{color:#ff4444;font-weight:bold}";
  h += ".night-range{display:flex;gap:12px;margin-top:8px}.night-range>div{flex:1}";
  h += ".txform{display:flex;flex-wrap:wrap;gap:6px;margin-top:10px}";
  h += ".txform>*{flex:1 1 46%;min-width:120px}";
  h += ".pricerow{display:flex;gap:6px}";
  h += ".pricerow .inp{flex:1;min-width:0}";
  h += ".fetchbtn{flex:0 0 auto;margin:0;width:auto;padding:7px 12px;font-size:11px;white-space:nowrap;"
    "background:transparent;color:#0af;border:1px solid #0af;border-radius:6px}";
  h += ".fetchbtn:hover{background:#0af;color:#000}";
  h += ".rmtext{flex:0 0 auto;margin:0;width:auto;padding:7px 12px;font-size:11px;white-space:nowrap;"
    "background:transparent;color:" + String(rmclr) + ";border:1px solid " + rmclr + ";border-radius:6px}";
  h += ".rmtext:hover{background:" + String(rmclr) + ";color:#fff}";
  h += ".trow-group{border-top:1px solid " + String(bord) + ";padding-top:8px;margin-top:8px}";
  h += ".trow-group:first-child{border-top:none;padding-top:0;margin-top:0}";
  h += "a{color:#0af;text-decoration:none}";
  h += "</style></head><body>";

  h += "<h1>ESP32 \xc2\xb7 CYD</h1><h2>Portfolio Tracker</h2>";

  h += "<div class='card'><h3>Live Prices</h3><div class='tbl-wrap'>";
  h += "<table><thead><tr>"
    "<th>Symbol</th><th>Price (PLN)</th><th>Change (" + rangeLabel + ")</th>"
    "<th>Value (PLN)</th><th>P&amp;L (" + rangeLabel + ")</th>"
    "</tr></thead><tbody>" + rows + "</tbody></table></div>";

  if (portfolio && totalVal > 0) {
    h += "<div class='meta'>Portfolio (PLN): <strong>" + String(totalVal, 2) + "</strong>"
      + " &nbsp; P&amp;L (" + rangeLabel + "): "
      + "<span class='" + (totalPL >= 0 ? "pl-pos" : "pl-neg") + "'>"
      + (totalPL >= 0 ? "+" : "") + String(totalPL, 2) + "</span>";
    if (anyMissing) h += "<br><span style='color:#e6a23c'>* Missing rate.</span>";
    h += "<br><span class='hint2'>Paper gain/loss from price movement over the selected period. For your real cost-basis P&amp;L (what you actually paid), see Holdings &amp; Alerts below.</span>";
    h += "</div>";
  }
  h += "<div class='meta'><a href='/refresh'>Force Refresh</a> &nbsp;|&nbsp; "
    "<a href='/api/quotes' target='_blank'>JSON API</a></div></div>";

  // Settings form. Closed early (right after the Display card) so the
  // separate /addlot form below it isn't nested inside this one -- HTML
  // forms can't nest. Holdings & Alerts renders after that point but its
  // inputs still submit here via the HTML5 form='cfgform' attribute.
  h += "<form id='cfgform' method='POST' action='/save'>";

  h += "<div class='card'><h3>Tickers</h3>"
    "<label>Up to 8 symbols &mdash; all converted and displayed in PLN</label>"
    "<div id='tl'></div>"
    "<button type='button' class='addbtn' onclick='addT()'>+ Add Ticker</button>"
    "<input type='hidden' name='tickers' id='th'></div>";

  h += "<div class='card'><h3>Chart Period</h3>"
    "<label>Period for % change, sparkline, and the Live Prices P&amp;L column (real cost-basis P&amp;L in Holdings &amp; Alerts is unaffected)</label>"
    "<div class='rg' id='rg'></div>"
    "<input type='hidden' name='range' id='ri' value='" + chartRange + "'>"
    "<div class='hint'>Save &amp; Apply to reload data.</div></div>";

  h += "<div class='card'><h3>Display</h3>";
  h += "<label>Refresh Interval (seconds)</label>"
    "<input class='inp' type='number' name='refresh' min='" + String(minRefresh) + "' max='3600' value='" + refreshSec + "'>";
  h += "<div class='hint'>Min " + String(minRefresh) + "s &nbsp;\xc2\xb7&nbsp; Default " + defaultRefresh + "s</div>";
  h += "<label>Backlight (10-255)</label>"
    "<input class='inp' type='number' name='bright' min='10' max='255' value='" + String(brightness) + "'>";
  h += "<div class='row'><input type='checkbox' name='darkmode' id='dm' value='1'" + dmChk + ">"
    "<label for='dm'>Dark Mode</label></div>";
  h += "<div class='row'><input type='checkbox' name='portfolio' id='pm' value='1'" + pmChk + ">"
    "<label for='pm'>Portfolio Mode (Value &amp; P&amp;L &amp; Sort)</label></div>";
  h += "<div class='row'><input type='checkbox' name='nighten' id='nm' value='1'" + nmChk + ">"
    "<label for='nm'>Night Mode</label></div>";
  h += "<div class='night-range'>"
    "<div><label>From (hour 0-23)</label><input class='inp' type='number' name='nightfr' min='0' max='23' value='" + String(nightFrom) + "'></div>"
    "<div><label>To (hour 0-23)</label><input class='inp' type='number' name='nightto' min='0' max='23' value='" + String(nightTo) + "'></div>"
    "</div>";
  h += "<div class='hint'>e.g. 0 &rarr; 8 (midnight wrap supported)</div></div>";

  h += "</form>";

  // Transactions card -- kept outside the settings form since HTML forms
  // cannot be nested. Rows are added/removed client-side; on submit each
  // row is posted individually via fetch() to /addlot (or /editlot when
  // editing), so several transactions can be entered and saved together.
  h += "<div class='card'><h3>Transactions (cost basis)</h3><div class='tbl-wrap'>"
    "<table><thead><tr><th>Symbol</th><th>Date</th><th>Qty</th><th>Price PLN/unit</th><th>Total PLN</th><th></th></tr></thead>"
    "<tbody>" + txRows + "</tbody></table></div>";
  h += "<div id='txRows'></div>"
    "<button type='button' class='addbtn' id='txAddBtn' onclick='addTxRow()'>+ Add Another</button>"
    "<input type='hidden' id='txLotIdx' value=''>"
    "<input type='hidden' id='txLotOrigSym' value=''>"
    "<div class='row' style='width:100%;gap:8px;margin-top:10px'>"
    "<button type='button' id='txSubmitBtn' style='margin-top:0;flex:1' onclick='submitTx()'>+ Add Transaction</button>"
    "<button type='button' id='txCancelBtn' class='fetchbtn' style='display:none' onclick='cancelEdit()'>Cancel</button>"
    "</div>"
    "<span class='hint2' id='fetchStatus'></span>";
  h += "<template id='txRowTpl'><div class='trow-group' data-idx='__IDX__'>"
    "<div class='txform'>"
    "<select class='inp' id='txSym__IDX__'>" + tickerOptions + "</select>"
    "<input class='inp' type='date' id='txDate__IDX__' required>"
    "<input class='inp' type='number' id='txQty__IDX__' step='any' placeholder='+1.5 buy / -0.5 sell' required>"
    "<div class='pricerow'>"
    "<input class='inp' type='number' id='txPrice__IDX__' step='any' min='0' placeholder='price PLN/unit' required>"
    "<button type='button' class='fetchbtn' onclick='fetchHistPrice(__IDX__)'>&#8635; Fetch</button>"
    "</div></div>"
    "<div class='row' style='justify-content:space-between;margin-top:4px'>"
    "<span class='hint2' id='txRowStatus__IDX__'></span>"
    "<button type='button' class='rmtext' id='txRmBtn__IDX__' onclick='removeTxRow(__IDX__)'>&#10005; Remove</button>"
    "</div></div></template>";
  h += "<div class='hint'>Positive qty = buy, negative = sell. Pick any date, including past purchases you still need to backfill. "
    "Your real cost-basis P&amp;L (average-cost method, from these prices) is shown in Holdings &amp; Alerts below, independent of the chart period. "
    "The Fetch button looks up that date's closing price and same-day exchange rate; double-check it against your broker statement before saving. "
    "Use + Add Another to enter several transactions before saving them together. Use &#9998; on a row to edit it (fills this form in edit mode) or &#10005; to delete it.</div>"
    "<div class='hint'><a href='/api/transactions' target='_blank'>Transactions JSON (backup)</a> &mdash; always reflects what's currently saved. "
    "Save this externally so a device reflash/erase doesn't lose your history.</div></div>";

  // Holdings & Alerts -- qty/avg cost are read-only, derived from the
  // transactions above. Its inputs carry form='cfgform' (set in the .ino
  // row builder) so they still submit with the settings form above, even
  // though this card sits outside that <form> tag.
  h += "<div class='card'><h3>Holdings &amp; Alerts</h3><div class='tbl-wrap'>"
    "<table><thead><tr><th>Symbol</th><th>Qty (calc.)</th><th>Avg Cost (PLN)</th><th>P&amp;L (PLN)</th><th>Alert High</th><th>Alert Low</th></tr></thead>"
    "<tbody>" + holdRows + "</tbody></table></div>";
  if (anyRealPl) {
    h += "<div class='meta'>Total P&amp;L (all holdings): ";
    h += "<span class='";
    h += (totalRealPL >= 0 ? "pl-pos" : "pl-neg");
    h += "'>";
    h += (totalRealPL >= 0 ? "+" : "");
    h += String(totalRealPL, 2);
    h += " PLN</span></div>";
  }
  h += "<div class='hint'>Alert threshold in PLN, 0 = disabled. Qty, avg cost &amp; P&amp;L are calculated from your Transactions above, not editable here. "
    "P&amp;L here is real cost-basis (what you actually paid vs current value) and does not change with Chart Period.</div></div>";

  h += "<button type='submit' form='cfgform'>&#9654; Save &amp; Apply</button>";

  h += "<div style='height:28px'></div>";

  h += "<script>";
  h += "var T='" + tickerList + "'.split(',').filter(Boolean);";
  h += "function render(){"
    "var el=document.getElementById('tl');el.innerHTML='';"
    "T.forEach(function(t,i){"
    "var d=document.createElement('div');d.className='trow';"
    "d.innerHTML='<span class=\"tidx\">'+(i+1)+'</span>"
    "<input class=\"inp\" type=\"text\" value=\"'+t+'\" placeholder=\"e.g. AAPL\" "
    "oninput=\"upd('+i+',this.value)\">"
    "<button type=\"button\" class=\"rm\" onclick=\"del('+i+')\">&#10005;</button>';"
    "el.appendChild(d);"
    "});"
    "}";
  h += "function upd(i,v){T[i]=v.toUpperCase();document.querySelectorAll('#tl input')[i].value=T[i];}";
  h += "function addT(){if(T.length>=8)return;T.push('');render();document.querySelectorAll('#tl input')[T.length-1].focus();}";
  h += "function del(i){T.splice(i,1);render();}";
  h += "document.getElementById('cfgform').addEventListener('submit',function(){"
    "document.getElementById('th').value=T.map(function(t){return t.trim().toUpperCase();}).filter(Boolean).join(',');"
    "});";
  h += "render();";

  h += "var RANGES=[{v:'1d',l:'1D'},{v:'5d',l:'5D'},{v:'1mo',l:'1M'},"
    "{v:'3mo',l:'3M'},{v:'6mo',l:'6M'},{v:'ytd',l:'YTD'},"
    "{v:'1y',l:'1Y'},{v:'3y',l:'3Y'},{v:'max',l:'MAX'}];";
  h += "var curR='" + chartRange + "';";
  h += "(function(){"
    "var rg=document.getElementById('rg');"
    "RANGES.forEach(function(r){"
    "var b=document.createElement('button');"
    "b.type='button';b.className='rb'+(r.v===curR?' ra':'');b.textContent=r.l;"
    "b.onclick=function(){"
    "document.querySelectorAll('.rb').forEach(function(x){x.classList.remove('ra');});"
    "b.classList.add('ra');"
    "document.getElementById('ri').value=r.v;"
    "};"
    "rg.appendChild(b);"
    "});"
    "})();";
  h += "var txSeq=0;"
    "function tplHtml(idx){var t=document.getElementById('txRowTpl').innerHTML;return t.split('__IDX__').join(idx);}"
    "function addTxRow(foc){"
    "var idx=txSeq++;"
    "var wrap=document.createElement('div');wrap.innerHTML=tplHtml(idx);"
    "document.getElementById('txRows').appendChild(wrap.firstElementChild);"
    "updateRemoveButtons();"
    "if(foc!==false){var el=document.getElementById('txSym'+idx);if(el)el.focus();}"
    "return idx;"
    "}"
    "function removeTxRow(idx){"
    "var el=document.querySelector('.trow-group[data-idx=\"'+idx+'\"]');"
    "if(el)el.remove();"
    "if(!document.querySelector('.trow-group'))addTxRow(false);"
    "updateRemoveButtons();"
    "}"
    "function updateRemoveButtons(){"
    "var groups=document.querySelectorAll('.trow-group');"
    "groups.forEach(function(g){"
    "var idx=g.getAttribute('data-idx');"
    "var btn=document.getElementById('txRmBtn'+idx);"
    "if(btn)btn.style.display=(groups.length>1)?'':'none';"
    "});"
    "}";

  h += "function fetchHistPrice(idx){"
    "var sym=document.getElementById('txSym'+idx).value;"
    "var date=document.getElementById('txDate'+idx).value;"
    "var priceEl=document.getElementById('txPrice'+idx);"
    "var statusEl=document.getElementById('txRowStatus'+idx);"
    "if(!date){statusEl.textContent='Pick a date first.';return;}"
    "statusEl.textContent='Fetching...';"
    "fetch('/api/histprice?lt='+encodeURIComponent(sym)+'&ld='+encodeURIComponent(date))"
    ".then(function(r){return r.json();})"
    ".then(function(d){"
    "if(d.ok){priceEl.value=d.pricePLN.toFixed(2);statusEl.textContent='Loaded closing price. Double-check before saving.';}"
    "else{statusEl.textContent='No data for that date -- enter manually.';}"
    "})"
    ".catch(function(){statusEl.textContent='Fetch failed -- enter manually.';});"
    "}";

  h += "function submitTx(){"
    "var editing=document.getElementById('txLotIdx').value!=='';"
    "var groups=document.querySelectorAll('.trow-group');"
    "var rows=[];var incomplete=false;"
    "groups.forEach(function(g){"
    "var idx=g.getAttribute('data-idx');"
    "var sym=document.getElementById('txSym'+idx).value;"
    "var date=document.getElementById('txDate'+idx).value;"
    "var qty=document.getElementById('txQty'+idx).value;"
    "var price=document.getElementById('txPrice'+idx).value;"
    "if(!date&&!qty&&price==='')return;"
    "if(!date||!qty||price===''){incomplete=true;return;}"
    "rows.push({sym:sym,date:date,qty:qty,price:price});"
    "});"
    "var statusEl=document.getElementById('fetchStatus');"
    "if(incomplete){statusEl.textContent='Some rows are missing fields -- fill them in or remove the row.';return;}"
    "if(rows.length===0){statusEl.textContent='Nothing to save -- fill in at least one transaction.';return;}"
    "statusEl.textContent='Saving '+rows.length+' transaction'+(rows.length>1?'s':'')+'...';"
    "var endpoint=editing?'/editlot':'/addlot';"
    "var lotIdx=document.getElementById('txLotIdx').value;"
    "var origSym=document.getElementById('txLotOrigSym').value;"
    "var chain=Promise.resolve();var okCount=0,failCount=0;"
    "rows.forEach(function(r){"
    "chain=chain.then(function(){"
    "var body='lt='+encodeURIComponent(r.sym)+'&ld='+encodeURIComponent(r.date)"
    "+'&lq='+encodeURIComponent(r.qty)+'&lp='+encodeURIComponent(r.price);"
    "if(editing)body+='&lk='+encodeURIComponent(lotIdx)+'&lo='+encodeURIComponent(origSym);"
    "return fetch(endpoint,{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:body})"
    ".then(function(resp){return resp.text();})"
    ".then(function(txt){if(txt.indexOf('Could not')!==-1)failCount++;else okCount++;})"
    ".catch(function(){failCount++;});"
    "});"
    "});"
    "chain.then(function(){"
    "statusEl.textContent=okCount+' saved'+(failCount?(', '+failCount+' failed'):'')+'. Reloading...';"
    "setTimeout(function(){window.location.href='/';},600);"
    "});"
    "}";

  h += "function editLot(sym,date,qty,price,lotIdx){"
    "document.getElementById('txRows').innerHTML='';"
    "var idx=addTxRow(false);"
    "document.getElementById('txSym'+idx).value=sym;"
    "document.getElementById('txDate'+idx).value=date;"
    "document.getElementById('txQty'+idx).value=qty;"
    "document.getElementById('txPrice'+idx).value=price;"
    "document.getElementById('txLotIdx').value=lotIdx;"
    "document.getElementById('txLotOrigSym').value=sym;"
    "document.getElementById('txAddBtn').style.display='none';"
    "updateRemoveButtons();"
    "document.getElementById('txSubmitBtn').textContent='\\u2713 Update Transaction';"
    "document.getElementById('txCancelBtn').style.display='inline-block';"
    "document.getElementById('fetchStatus').textContent='Editing existing transaction -- change values and Update, or Cancel.';"
    "document.getElementById('txRows').scrollIntoView({behavior:'smooth',block:'center'});"
    "}";
  h += "function cancelEdit(){"
    "document.getElementById('txLotIdx').value='';"
    "document.getElementById('txLotOrigSym').value='';"
    "document.getElementById('txRows').innerHTML='';"
    "addTxRow(false);"
    "document.getElementById('txAddBtn').style.display='';"
    "document.getElementById('txSubmitBtn').textContent='+ Add Transaction';"
    "document.getElementById('txCancelBtn').style.display='none';"
    "document.getElementById('fetchStatus').textContent='';"
    "}";
  h += "addTxRow(false);";

  h += "</script>";

  h += "</body></html>";
  return h;
}
