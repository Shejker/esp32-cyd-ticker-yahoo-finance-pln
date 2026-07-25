#pragma once
#include <Arduino.h>

// ==========================================
// HTML LAYER
// ==========================================

String buildRedirectPage(bool dark, const char* emoji, const char* message) {
  const char* rbg = dark ? "#0a0a0f" : "#f0f0f5";
  const char* rfg = dark ? "#00cc44" : "#1a1a2e";
  return String("<!DOCTYPE html><html><head><meta charset='UTF-8'>")
    + "<meta http-equiv='refresh' content='2;url=/'>"
    + "<style>"
    + "body{font-family:'SF Mono',monospace;background:" + rbg + ";color:" + rfg
    + ";padding:40px;text-align:center;display:flex;align-items:center;justify-content:center;height:100vh;margin:0;flex-direction:column}"
    + "h1{font-size:48px;margin-bottom:16px}"
    + "p{font-size:20px;opacity:0.8}"
    + "</style>"
    + "</head><body>"
    + "<h1>" + emoji + "</h1>"
    + "<p>" + message + "</p>"
    + "<p style='position:fixed;bottom:24px;left:0;right:0;font-size:13px;opacity:0.4;text-align:center'>Redirecting...</p>"
    + "</body></html>";
}

String buildRootHtml(
  bool dark, bool portfolio,
  const String& rows, const String& holdRows, const String& tickerList,
  double totalVal, double totalPL, bool anyMissing,
  int refreshSec, int brightness, int minRefresh, int defaultRefresh)
{
  const char* bg    = dark ? "#0a0a0f" : "#f0f0f5";
  const char* card  = dark ? "#13131a" : "#ffffff";
  const char* bord  = dark ? "#222230" : "#d0d0e0";
  const char* text  = dark ? "#c8ccd4" : "#1a1a2e";
  const char* muted = dark ? "#555"    : "#888";
  const char* inp   = dark ? "#1c1c28" : "#f8f8ff";
  const char* ibord = dark ? "#2a2a40" : "#b0b0cc";
  const char* hint  = dark ? "#444"    : "#999";
  String dmChk = dark      ? " checked" : "";
  String pmChk = portfolio  ? " checked" : "";

  return String(
    "<!DOCTYPE html><html><head>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<meta charset='UTF-8'><title>Portfolio Tracker</title>"
    "<style>"
    "*{box-sizing:border-box;margin:0;padding:0}")
    + "body{font-family:'SF Mono','Fira Mono',monospace;background:" + bg + ";color:" + text + ";padding:20px 14px;max-width:900px;margin:0 auto}"
    + "h1{font-size:10px;letter-spacing:4px;text-transform:uppercase;color:" + muted + ";margin-bottom:3px}"
    + "h2{font-size:22px;font-weight:700;margin-bottom:18px}"
    + "h3{font-size:10px;letter-spacing:3px;text-transform:uppercase;color:" + muted + ";margin-bottom:10px}"
    + ".card{background:" + card + ";border:1px solid " + bord + ";border-radius:10px;padding:16px;margin-bottom:14px;width:100%}"
    + "label{display:block;font-size:11px;color:" + muted + ";margin:10px 0 3px}"
    + "input[type=text],input[type=password],input[type=number]{width:100%;padding:9px 11px;background:" + inp + ";border:1px solid " + ibord + ";color:" + text + ";border-radius:6px;font-family:inherit;font-size:13px;outline:none}"
    + "input:focus{border-color:#0af}"
    + ".hint{font-size:10px;color:" + hint + ";margin-top:3px}"
    + ".row{display:flex;align-items:center;gap:8px;margin-top:10px}"
    + ".row label{margin:0}"
    + "input[type=checkbox]{width:16px;height:16px;accent-color:#0080ff}"
    + "button{margin-top:14px;width:100%;padding:12px;background:#0080ff;color:#fff;border:none;border-radius:8px;font-size:14px;font-weight:600;font-family:inherit;cursor:pointer}"
    + "button:hover{background:#0062cc}"
    + "table{width:100%;border-collapse:collapse;font-size:13px}"
    + "th{font-size:9px;letter-spacing:2px;text-transform:uppercase;color:" + muted + ";text-align:left;padding:5px 0;border-bottom:1px solid " + bord + "}"
    + "td{padding:6px 4px;border-bottom:1px solid " + bord + "}"
    + ".meta{font-size:11px;color:" + muted + ";margin-top:6px}"
    + ".meta strong{color:" + text + "}"
    + ".meta .pl-positive{color:#00cc44;font-weight:bold}"
    + ".meta .pl-negative{color:#ff4444;font-weight:bold}"
    + "a{color:#0af;text-decoration:none}"
    + "</style></head><body>"
    + "<h1>ESP32 \xc2\xb7 CYD</h1><h2>Portfolio Tracker</h2>"

    + "<div class='card'><h3>Live Prices</h3>"
    + "<table><thead><tr><th>Symbol</th><th>Price (PLN)</th><th>Change</th><th>Value (PLN)</th><th>Day P&L</th></tr></thead>"
    + "<tbody>" + rows + "</tbody></table>"
    + (portfolio && totalVal > 0
        ? String("<div class='meta'>") + (anyMissing ? "Total*: " : "Portfolio: PLN ")
          + "<strong>" + String(totalVal, 2) + "</strong>"
          + " &nbsp; Day P&L: <span class='" + (totalPL >= 0 ? "pl-positive" : "pl-negative") + "'>"
          + (totalPL >= 0 ? "+" : "") + String(totalPL, 2) + "</span>"
          + (anyMissing ? "<br><span style='color:#e6a23c'>* Missing currency rate. Some values may be in native currency.</span>" : "")
          + "</div>"
        : String(""))
    + "<div class='meta'>"
    + "<a href='/refresh'>Force Refresh</a>"
    + " &nbsp;|&nbsp; <a href='/api/quotes' target='_blank'>JSON API</a></div>"
    + "</div>"

    + "<form method='POST' action='/save'>"

    + "<div class='card'><h3>Tickers</h3>"
    + "<label>Symbols (comma-separated, up to 8)</label>"
    + "<input type='text' name='tickers' value='" + tickerList + "' placeholder='ANAV.DE,WEBN.DE,BTC-USD,GC=F'>"
    + "<div class='hint'><b>Note:</b> All assets are automatically evaluated and displayed in PLN.</div>"
    + "</div>"

    + "<div class='card'><h3>Display</h3>"
    + "<label>Refresh Interval (seconds)</label>"
    + "<input type='number' name='refresh' min='" + minRefresh + "' max='3600' value='" + refreshSec + "'>"
    + "<div class='hint'>Minimum " + minRefresh + "s &nbsp;\xc2\xb7&nbsp; Default " + defaultRefresh + "s</div>"
    + "<label>Backlight (10-255)</label>"
    + "<input type='number' name='bright' min='10' max='255' value='" + brightness + "'>"
    + "<div class='row'>"
    + "<input type='checkbox' name='darkmode' id='dm' value='1'" + dmChk + ">"
    + "<label for='dm'>Dark Mode</label>"
    + "</div>"
    + "<div class='row'>"
    + "<input type='checkbox' name='portfolio' id='pm' value='1'" + pmChk + ">"
    + "<label for='pm'>Portfolio Mode (Value &amp; P&amp;L &amp; Sort)</label>"
    + "</div>"
    + "</div>"

    + "<div class='card'><h3>Holdings &amp; Alerts (PLN)</h3>"
    + "<table><thead><tr><th>Symbol</th><th>Shares/Units</th><th>Alert High (PLN)</th><th>Alert Low (PLN)</th></tr></thead>"
    + "<tbody>" + holdRows + "</tbody></table>"
    + "<div class='hint'>Set 0 to disable. Alerts now trigger based on the converted PLN price.</div>"
    + "</div>"

    + "<button type='submit'>&#9654; Save &amp; Apply</button>"
    + "</form>"
    + "<div style='height:28px'></div></body></html>";
}
