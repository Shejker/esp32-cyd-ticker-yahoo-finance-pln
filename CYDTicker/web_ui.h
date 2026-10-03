#pragma once
#include <Arduino.h>
#include "browser_requests.h"
#include <WebServer.h>
#include <vector>
#include "tracker_script.h"
#include "savings_ui.h"
#include "backup_ui.h"

struct TrackerPage {
  bool dark, portfolio, anyMissing, anyRealPL;
  String rows, holdings;
  std::vector<String> transactions;
  String manualRows, tickerOptions, configJson;
  double totalValue, periodPL, holdingPL;
  int refreshSec, brightness, minRefresh;
  bool nightMode;
  int nightFrom, nightTo;
  String chartRange, periodLabel;
};

#define FAVICON_LINK "<link rel='icon' type='image/svg+xml' href='/favicon.svg'>"

String buildRedirectPage(bool dark, const char* emoji, const char* message, const char* destination = "/") {
  const char* rbg = dark ? "#0a0a0f" : "#f0f0f5";
  const char* rfg = dark ? "#00cc44" : "#1a1a2e";
  return String(F("<!DOCTYPE html><html><head><meta charset='UTF-8'>"))
    + F("<meta http-equiv='refresh' content='2;url=") + destination + F("'>" FAVICON_LINK "<style>")
    + "body{font-family:'SF Mono',monospace;background:" + rbg + ";color:" + rfg
    + F(";padding:40px;text-align:center;display:flex;align-items:center;")
    + F("justify-content:center;height:100vh;margin:0;flex-direction:column}")
    + F("h1{font-size:48px;margin-bottom:16px}p{font-size:20px;opacity:0.8}")
    + F("</style></head><body>")
    + "<h1>" + emoji + "</h1><p>" + message + "</p>"
    + F("<p style='position:fixed;bottom:24px;left:0;right:0;font-size:13px;opacity:0.4;text-align:center'>Redirecting...</p>")
    + F("</body></html>");
}

void sendRootHtml(WebServer& server, const TrackerPage& page) {
  const bool dark = page.dark, portfolio = page.portfolio, anyMissing = page.anyMissing, anyRealPl = page.anyRealPL;
  const String& rows = page.rows;
  const String& holdRows = page.holdings;
  const String& tickerOptions = page.tickerOptions;
  const String& chartRange = page.chartRange;
  const String& rangeLabel = page.periodLabel;
  const double totalVal = page.totalValue, totalPL = page.periodPL, totalRealPL = page.holdingPL;
  const int refreshSec = page.refreshSec, brightness = page.brightness, minRefresh = page.minRefresh, defaultRefresh = 60;
  const bool nightMode = page.nightMode;
  const int nightFrom = page.nightFrom, nightTo = page.nightTo;
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "text/html; charset=utf-8", "");

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

  String h = F("<!DOCTYPE html><html><head><meta name='viewport' content='width=device-width,initial-scale=1'><meta charset='UTF-8'><title>Portfolio Tracker</title>" FAVICON_LINK "<style>*{box-sizing:border-box;margin:0;padding:0}");
  h += "body{font-family:'SF Mono','Fira Mono',monospace;background:" + String(bg) + ";color:" + text + ";padding:20px 14px;max-width:900px;margin:0 auto;overflow-x:hidden}";
  h += "h1{font-size:10px;letter-spacing:4px;text-transform:uppercase;color:" + String(muted) + ";margin-bottom:3px}h2{font-size:22px;font-weight:700;margin-bottom:18px}h3{font-size:10px;letter-spacing:3px;text-transform:uppercase;color:" + String(muted) + ";margin-bottom:10px}";
  h += ".card{background:" + String(card) + ";border:1px solid " + bord + ";border-radius:10px;padding:16px;margin-bottom:14px}";
  h += "label{display:block;font-size:11px;color:" + String(muted) + ";margin:10px 0 3px}";
  h += ".inp{width:100%;padding:7px 9px;background:" + String(inp) + ";border:1px solid " + ibord + ";color:" + text + ";border-radius:6px;font-family:inherit;font-size:12px;text-align:left;outline:none}.inp:focus{border-color:#0af}";
  h += "input[type=checkbox]{width:16px;height:16px;accent-color:#0080ff}";
  h += ".hint{font-size:10px;color:" + String(hint) + ";margin-top:4px;line-height:1.5}.hint2{font-size:9px;color:" + String(hint) + ";opacity:.8}";
  h += ".backup-note{margin-top:8px}.backup-link{display:inline;font:inherit;line-height:inherit;vertical-align:baseline;white-space:nowrap}.backup-link:hover{text-decoration:underline}.backup-link:focus-visible{outline:1px solid #0af;outline-offset:3px}";
  h += ".backup-controls{margin-top:16px;padding-top:12px;border-top:1px solid " + String(bord) + "}.backup-controls .hint{color:inherit;opacity:.8;overflow-wrap:anywhere}.backup-downloads{font-size:11px;line-height:1.6}.backup-controls summary{margin-top:8px;font-size:11px;color:#0af;cursor:pointer}.backup-controls summary:hover{text-decoration:underline}.backup-controls summary[aria-disabled=true]{opacity:.6}.restore-fields{display:grid;grid-template-columns:minmax(120px,.8fr) minmax(0,1.2fr);gap:10px}.restore-fields>div{min-width:0}.backup-controls .row label{line-height:1.5}.backup-controls input[type=checkbox]{flex-shrink:0}.backup-controls input[type=file]{font-size:11px;padding:5px;min-width:0}.backup-controls input[type=file]::file-selector-button{font:inherit;color:inherit;background:transparent;border:1px solid " + String(ibord) + ";border-radius:4px;padding:3px 6px;margin-right:6px}#restoreSummary{margin-top:10px}#restoreStatus{margin-top:8px;line-height:1.5}#restoreStatus:empty{display:none}@media(max-width:480px){.restore-fields{grid-template-columns:minmax(0,1fr);gap:0}}";
  h += ".row{display:flex;align-items:center;gap:8px;margin-top:10px}.row label{margin:0}";
  h += ".backup-controls .restore-confirm{display:flex;align-items:flex-start;gap:8px;margin:12px 0 10px;font-size:11px;line-height:1.6;color:inherit;cursor:pointer}.restore-confirm input[type=checkbox]{flex:0 0 16px;margin:1px 0 0}.restore-confirm span{min-width:0;opacity:.9;overflow-wrap:anywhere}#restoreApply:disabled{opacity:.45;cursor:not-allowed}";
  h += "button{margin-top:14px;width:100%;padding:12px;background:#0080ff;color:#fff;border:none;border-radius:8px;font-size:14px;font-weight:600;font-family:inherit;cursor:pointer}button:hover{background:#0062cc}";
  h += ".rm{margin:0;width:26px;padding:5px 0;font-size:11px;background:" + String(rmbg) + ";color:" + rmclr + ";border:1px solid " + rmclr + ";border-radius:5px;flex-shrink:0}.rm:hover{background:" + String(rmclr) + ";color:#fff}";
  h += ".addbtn{margin-top:6px;width:auto;padding:7px 14px;font-size:11px;background:transparent;color:#0af;border:1px solid #0af;border-radius:5px}.addbtn:hover{background:#0af;color:#000}";
  h += ".trow{display:flex;gap:6px;margin-bottom:5px;align-items:center}.trow .inp{flex:1;width:auto;min-width:0}.tidx{font-size:10px;color:" + String(muted) + ";min-width:12px;text-align:right}";
  h += ".rg{display:flex;flex-wrap:wrap;gap:6px;margin-top:8px}";
  h += ".rb{margin:0;width:auto;padding:6px 11px;font-size:11px;font-weight:600;background:transparent;color:" + String(muted) + ";border:1px solid " + bord + ";border-radius:5px}.rb:hover{border-color:#0af;color:#0af}.ra{background:#0080ff!important;color:#fff!important;border-color:#0080ff!important}";
  h += ".tbl-wrap{overflow-x:auto;-webkit-overflow-scrolling:touch;max-width:100%}";
  h += "table{border-collapse:collapse;font-size:11px;width:100%}";
  h += "th{font-size:9px;letter-spacing:2px;text-transform:uppercase;color:" + String(muted) + ";text-align:left;padding:5px 2px;border-bottom:1px solid " + bord + ";white-space:nowrap}td{text-align:left;padding:5px 2px;border-bottom:1px solid " + String(bord) + "}";
  h += ".tnowrap{white-space:nowrap}.chg{font-size:10px;white-space:nowrap}.dellink{color:" + String(rmclr) + ";text-decoration:none;font-size:13px;padding:2px 6px}.editlink{color:#0af;text-decoration:none;font-size:13px;padding:2px 6px}";
  h += ".meta{font-size:11px;color:" + String(muted) + ";margin-top:6px}.meta strong{color:" + String(text) + "}";
  h += ".pl-pos{color:#00cc44;font-weight:bold}.pl-neg{color:#ff4444;font-weight:bold}";
  h += ".night-range{display:flex;gap:12px;margin-top:8px}.night-range>div{flex:1}";
  h += ".txform{display:flex;flex-wrap:wrap;gap:6px;margin-top:10px}.txform>*{flex:1 1 46%;min-width:120px}";
  h += ".pricerow{display:flex;gap:6px}.pricerow .inp{flex:1;min-width:0}";
  h += ".fetchbtn{flex:0 0 auto;margin:0;width:auto;padding:7px 12px;font-size:11px;white-space:nowrap;background:transparent;color:#0af;border:1px solid #0af;border-radius:6px}.fetchbtn:hover{background:#0af;color:#000}";
  h += ".rmtext{flex:0 0 auto;margin:0;width:auto;padding:7px 12px;font-size:11px;white-space:nowrap;background:transparent;color:" + String(rmclr) + ";border:1px solid " + rmclr + ";border-radius:6px}.rmtext:hover{background:" + String(rmclr) + ";color:#fff}";
  h += ".trow-group{border-top:1px solid " + String(bord) + ";padding-top:8px;margin-top:8px}.trow-group:first-child{border-top:none;padding-top:0;margin-top:0}";
  h += "a{color:#0af;text-decoration:none}.page-tabs{display:flex;gap:8px;margin-bottom:18px}.page-tabs a{font-size:12px;border:1px solid " + String(bord) + ";border-radius:6px;padding:8px 14px}.page-tabs a[aria-current=page]{background:#0080ff;color:#fff;border-color:#0080ff}.manual-date{max-width:128px}[hidden]{display:none!important}.editlink,.dellink{width:auto;margin:0;background:none;border:none;font-weight:normal}.editlink:hover,.dellink:hover{background:none}.editlink:disabled,.dellink:disabled{opacity:.5}";
  h += ".settings-button{margin:0 0 0 auto;width:36px;padding:0;font-size:22px;background:transparent;color:#0af;border:1px solid " + String(bord) + ";border-radius:6px}.settings-button:hover{background:transparent;border-color:#0af}dialog{margin:auto;width:calc(100% - 28px);max-width:460px;max-height:90vh;overflow:auto;padding:20px;background:" + String(card) + ";color:" + text + ";border:1px solid " + bord + ";border-radius:10px}dialog::backdrop{background:#0009}.settings-heading{display:flex;justify-content:space-between;align-items:center;margin-bottom:18px}.settings-heading h3{margin:0}.settings-close{width:auto;margin:0;padding:4px 8px;background:transparent;color:inherit;font-size:18px}.settings-close:hover{background:transparent;color:#0af}";
  h += ".account-name{color:inherit;font-weight:bold}.account-name:hover{color:#0af}.account-name:focus-visible{outline:1px solid #0af;outline-offset:3px}.account-add{display:inline-block;margin-top:10px;font-size:11px}.account-kind{display:block;font-size:9px;opacity:.6;margin-top:3px}#manualAccounts th,#manualAccounts td{padding:6px}#manualPreview{margin-top:14px;line-height:1.7;color:inherit;font-size:12px}#manualStatus{color:#e6a23c}#manualTitle{letter-spacing:1px;line-height:1.6;color:inherit}#manualDialog .hint{color:inherit;opacity:.7;font-size:11px}#manualDialog label{color:inherit;opacity:.8;font-size:12px}";
  h += "#manualAccounts th,#manualAccounts td{padding:8px 6px;vertical-align:top;line-height:1.4}#manualAccounts th:first-child,#manualAccounts td:first-child{padding-left:0}#manualAccounts th:last-child,#manualAccounts td:last-child{padding-right:0}#manualAccounts th:nth-child(n+2),#manualAccounts td:nth-child(n+2){font-variant-numeric:tabular-nums}.account-name{display:block}.account-kind{white-space:nowrap}";
  h += "@media(max-width:600px){#manualAccounts table,#manualAccounts tbody{display:block}#manualAccounts thead{display:none}#manualAccounts .manual-row{display:grid;grid-template-columns:repeat(3,minmax(0,1fr));border-bottom:1px solid " + String(bord) + ";padding-bottom:8px;margin-bottom:8px}#manualAccounts td{display:block;border:0;padding:5px 2px}#manualAccounts td:first-child{grid-column:1/-1}#manualAccounts td:nth-child(2)::before{content:'Balance'}#manualAccounts td:nth-child(3)::before{content:'Net contributions'}#manualAccounts td:nth-child(4)::before{content:'Profit / loss'}#manualAccounts td:nth-child(n+2):nth-child(-n+4)::before{display:block;font-size:9px;opacity:.6;margin-bottom:3px}}";
  h += "@media(max-width:600px){#manualAccounts td{padding:5px 0}}";
  h += "</style></head><body>";
  server.sendContent(h);

  h = F("<h1>ESP32 &middot; CYD</h1><h2>Portfolio Tracker</h2><nav class='page-tabs' aria-label='Pages'><a href='/' aria-current='page'>Tracker</a><a href='/portfolio'>Charts</a><button type='button' class='settings-button' id='settingsOpen' aria-label='Settings' title='Settings'>&#9881;&#65038;</button></nav><div class='card'><h3>Live Prices</h3><div class='tbl-wrap'>");
  h += F("<table><thead><tr><th>Symbol</th><th>Price (PLN)</th><th>Change (");
  h += rangeLabel;
  h += F(")</th><th>Value (PLN)</th><th>P&amp;L (");
  h += rangeLabel;
  h += F(")</th></tr></thead><tbody>");
  h += rows;
  h += F("</tbody></table></div>");
  server.sendContent(h);

  if (portfolio && totalVal > 0) {
    h = F("<div class='meta'>Portfolio (PLN): <strong>");
    h += anyMissing ? String("&mdash;") : String(totalVal, 2);
    h += F("</strong> &nbsp; P&amp;L (");
    h += rangeLabel;
    h += F("): <span class='");
    h += (totalPL >= 0 ? "pl-pos" : "pl-neg");
    h += F("'>");
    h += (totalPL >= 0 ? "+" : "");
    h += anyMissing ? String("&mdash;") : String(totalPL, 2);
    h += F("</span>");
    if (anyMissing) h += F("<br><span style='color:#e6a23c'>* Some holdings have no quote or PLN exchange rate; totals are unavailable.</span>");
    h += F("<br><span class='hint2'>Paper gain/loss from price movement over the selected period. For your real cost-basis P&amp;L (what you actually paid), see Holdings &amp; Alerts below.</span></div>");
    server.sendContent(h);
  }

  h = F("<div class='meta'><a href='/refresh'>Force Refresh</a> &nbsp;|&nbsp; <a href='/api/quotes' target='_blank'>JSON API</a></div></div><form id='cfgform' method='POST' action='/save'><input type='hidden' name='confirmremove' id='confirmRemove' value='0'><div class='card'><h3>Tickers</h3><label>Up to 8 symbols &mdash; all converted and displayed in PLN</label><div id='tl'></div><button type='button' class='addbtn' id='tickerAdd' onclick='addT()'>+ Add Ticker</button><input type='hidden' name='tickers' id='th'></div>");
  h += F("<div class='card'><h3>Chart Period</h3><label>Period for % change, sparkline, and the Live Prices P&amp;L column (real cost-basis P&amp;L in Holdings &amp; Alerts is unaffected)</label><div class='rg' id='rg'></div><input type='hidden' name='range' id='ri' value='");
  h += chartRange;
  h += F("'><div class='hint'>Save &amp; Apply to reload data.</div></div>");
  
  h += F("<dialog id='settingsDialog' aria-labelledby='settingsTitle'><div class='settings-heading'><h3 id='settingsTitle'>Settings</h3><button type='button' class='settings-close' id='settingsClose' aria-label='Close settings'>&#10005;</button></div><div class='settings-content'><h3>Display</h3><label>Refresh Interval (seconds)</label><input class='inp' type='number' name='refresh' min='");
  h += String(minRefresh);
  h += F("' max='3600' value='");
  h += refreshSec;
  h += F("'><div class='hint'>Min ");
  h += String(minRefresh);
  h += F("s &nbsp;&middot;&nbsp; Default ");
  h += defaultRefresh;
  h += F("s</div><label>Backlight (10-255)</label><input class='inp' type='number' name='bright' min='10' max='255' value='");
  h += String(brightness);
  h += F("'>");
  server.sendContent(h);

  h = F("<div class='row'><input type='checkbox' name='darkmode' id='dm' value='1'");
  if (dark) h += " checked";
  h += F("><label for='dm'>Dark Mode</label></div><div class='row'><input type='checkbox' name='portfolio' id='pm' value='1'");
  if (portfolio) h += " checked";
  h += F("><label for='pm'>Portfolio Mode (Value &amp; P&amp;L &amp; Sort)</label></div><div class='row'><input type='checkbox' name='nighten' id='nm' value='1'");
  if (nightMode) h += " checked";
  h += F("><label for='nm'>Night Mode</label></div>");
  server.sendContent(h);

  h = F("<div class='night-range'><div><label>From (hour 0-23)</label><input class='inp' type='number' name='nightfr' min='0' max='23' value='");
  h += String(nightFrom);
  h += F("'></div><div><label>To (hour 0-23)</label><input class='inp' type='number' name='nightto' min='0' max='23' value='");
  h += String(nightTo);
  h += F("'></div></div><div class='hint'>e.g. 0 &rarr; 8 (midnight wrap supported)</div></div><button type='submit' data-tracker-save disabled>Save &amp; Apply</button>");
  server.sendContent(h);
  server.sendContent_P(BACKUP_CONTROLS);
  h = F("</dialog></form>");
  server.sendContent(h);

  h = F("<div class='card' id='transactions'><h3>Transactions (Cost Basis)</h3><div class='tbl-wrap'><table><thead><tr><th>Symbol</th><th>Date</th><th>Qty</th><th>Price PLN/Unit</th><th>Total PLN</th><th></th></tr></thead><tbody>");
  server.sendContent(h);
  // Never duplicate the complete ledger in one large HTML String.
  for (const String& row : page.transactions) server.sendContent(row);
  h = "";
  h += F("</tbody></table></div><div id='txRows'></div><button type='button' class='addbtn' id='txAddBtn' onclick='addTxRow()'>+ Add Another</button><input type='hidden' id='txLotIdx' value=''><input type='hidden' id='txLotOrigSym' value=''><div class='row' style='width:100%;gap:8px;margin-top:10px'><button type='button' id='txSubmitBtn' style='margin-top:0;flex:1' onclick='submitTx()'>+ Add Transaction</button><button type='button' id='txCancelBtn' class='fetchbtn' hidden onclick='cancelEdit()'>Cancel</button></div><span class='hint2' id='fetchStatus'></span>");
  server.sendContent(h);

  h = F("<template id='txRowTpl'><div class='trow-group' data-idx='__IDX__'><div class='txform'><select class='inp' id='txSym__IDX__'>");
  h += tickerOptions;
  h += F("</select><input class='inp' type='date' id='txDate__IDX__' required><input class='inp' type='number' id='txQty__IDX__' step='any' placeholder='+1.5 buy / -0.5 sell' required><div class='pricerow'><input class='inp' type='number' id='txPrice__IDX__' step='any' min='0' placeholder='price PLN/unit' required><button type='button' class='fetchbtn' id='txFetch__IDX__' onclick='fetchHistPrice(__IDX__)'>&#8635; Fetch</button></div></div><div class='row' style='justify-content:space-between;margin-top:4px'><span class='hint2' id='txRowStatus__IDX__'></span><button type='button' class='rmtext' id='txRmBtn__IDX__' onclick='removeTxRow(__IDX__)'>&#10005; Remove</button></div></div></template>");
  h += F("<div class='hint'>Positive qty = buy, negative = sell. Pick any date, including past purchases you still need to backfill. Your real cost-basis P&amp;L (average-cost method, from these prices) is shown in Holdings &amp; Alerts below, independent of the chart period. The Fetch button looks up that date's closing price and same-day exchange rate; double-check it against your broker statement before saving. Use + Add Another to enter several transactions before saving them together. Use &#9998; on a row to edit it (fills this form in edit mode) or &#10005; to delete it.</div><div class='hint backup-note'>Keep a copy of your saved transactions outside the device before reflashing or erasing it. <a class='backup-link' href='/api/transactions' download='transactions.json' aria-label='Download Transactions Backup'>Download Backup</a></div></div>");
  server.sendContent(h);

  h = F("<div class='card' id='manualAccounts'><h3>Savings &amp; PPK</h3><div class='hint'>Select an account name to record deposits, interest or a valuation.</div><div class='tbl-wrap'><table><thead><tr><th>Name</th><th>Balance PLN</th><th>Net Contributions PLN</th><th>Profit / Loss PLN</th></tr></thead><tbody id='manualRows'>");
  h += page.manualRows;
  h += F("</tbody></table></div><a href='#add-savings' class='account-add' id='manualAdd'>+ Add Savings / PPK</a><p class='hint' id='manualGlobalStatus' role='status'></p><div class='hint backup-note'>Net contributions = deposits minus withdrawals. Profit/loss excludes employee, employer and government contributions. <a class='backup-link' href='/api/savings-ppk' download='savings-ppk.json' aria-label='Download Savings &amp; PPK Backup'>Download Backup</a></div></div>");
  server.sendContent(h);
  server.sendContent_P(SAVINGS_DIALOG);

  h = F("<div class='card'><h3>Holdings &amp; Alerts</h3><div class='tbl-wrap'><table><thead><tr><th>Symbol</th><th>Qty (calc.)</th><th>Avg Cost (PLN)</th><th>P&amp;L (PLN)</th><th>Alert High</th><th>Alert Low</th></tr></thead><tbody>");
  h += holdRows;
  h += F("</tbody></table></div>");
  server.sendContent(h);

  if (anyRealPl) {
    h = F("<div class='meta'>Total P&amp;L (all holdings): <span class='");
    h += (totalRealPL >= 0 ? "pl-pos" : "pl-neg");
    h += F("'>");
    h += anyMissing ? String("&mdash;") : String(totalRealPL >= 0 ? "+" : "") + String(totalRealPL, 2);
    h += F(" PLN</span></div>");
    server.sendContent(h);
  }

  h = F("<div class='hint'>Alert threshold in PLN, 0 = disabled. Qty, avg cost &amp; P&amp;L are calculated from your Transactions above, not editable here. P&amp;L here is real cost-basis (what you actually paid vs current value) and does not change with Chart Period.</div></div><button type='submit' form='cfgform' data-tracker-save disabled>&#9654; Save &amp; Apply</button><div style='height:28px'></div>");
  server.sendContent(h);

  server.sendContent("<script type='application/json' id='tracker-config'>" + page.configJson + "</script>");
  // A failed flash-to-String allocation sends an empty chunk and ends the page.
  // Stream flash assets directly, without a large temporary RAM allocation.
  server.sendContent_P(REQUEST_SCRIPT);
  server.sendContent_P(SAVINGS_SCRIPT);
  server.sendContent_P(TRACKER_SCRIPT);
  server.sendContent_P(RESTORE_SCRIPT);
  server.sendContent("");
}
