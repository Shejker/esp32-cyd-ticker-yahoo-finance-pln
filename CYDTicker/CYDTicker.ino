#include <WiFi.h>
#include <ESPmDNS.h>
#include <WiFiManager.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <WebServer.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>
#include <SPI.h>
#include <time.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include "web_ui.h"

// --- PIN CONFIGURATION ---
#define TOUCH_CS_PIN 33
#define TOUCH_IRQ_PIN 36
#define BL_PIN 21
#define LED_R 4
#define LED_G 16
#define LED_B 17

// --- APP CONSTANTS ---
static constexpr int MAX_TICKERS = 8;
static constexpr int MAX_EXCHANGE_RATES = 8;
static constexpr int MIN_REFRESH = 10;
static constexpr int DEFAULT_REFRESH = 60;
static constexpr int HEADER_H = 26;
static constexpr int FOOTER_H = 18;
static constexpr int TOUCH_DEBOUNCE_MS = 300;
static constexpr int WIFI_CHECK_MS = 30000;
static constexpr int MAX_SPARK_POINTS = 40;
static constexpr int MAX_LOTS = 60;      // transactions (buy/sell lots) kept per ticker
static constexpr int MAX_LOTS_SHOWN = 8; // most recent lots shown in the web UI table

// --- HARDWARE OBJECTS ---
SPIClass touchSPI(HSPI);
XPT2046_Touchscreen touch(TOUCH_CS_PIN, TOUCH_IRQ_PIN);
TFT_eSPI tft = TFT_eSPI();
Preferences prefs;
WebServer server(80);

// --- DATA STRUCTURES ---
struct Quote {
  String sym;
  float price;
  float pct;
  float open;
  String currency;
  bool valid;
  int errors;
  float sparkline[MAX_SPARK_POINTS];
  int sparkCount;
};

struct ExRate {
  String curr;
  float rate;
};

// A single transaction ("lot"): on date ts, quantity changed by qty
// (positive = bought, negative = sold), at pricePLN per unit -- price is
// always stored in PLN (what you actually paid/received) so no historical
// FX-rate approximation is ever needed for cost-basis math.
struct Lot {
  time_t ts;
  float qty;
  float pricePLN;
};

// --- APP STATE ---
String tickers[MAX_TICKERS];
float holdings[MAX_TICKERS]; // cached current quantity, derived from lots[] (not directly editable)
float alertHigh[MAX_TICKERS];
float alertLow[MAX_TICKERS];
Quote quotes[MAX_TICKERS];
ExRate exchangeRates[MAX_EXCHANGE_RATES];
int tickerCount = 0;
int rateCount = 0;
int refreshSec = DEFAULT_REFRESH;
int brightness = 200;
bool darkMode = true;
bool portfolioMode = false;

// --- TRANSACTIONS (COST BASIS) ---
Lot lots[MAX_TICKERS][MAX_LOTS];
int lotCount[MAX_TICKERS];
// One-time migration hint: legacy flat "holdings" value from before this
// feature existed, shown only until you add real transactions for that ticker.
float legacyHint[MAX_TICKERS];

// --- NIGHT MODE ---
bool nightModeEnabled = false;
int nightFrom = 0;
int nightTo = 8;

// --- CHART RANGE ---
String chartRange = "1d"; // Yahoo Finance range parameter

// --- VIEW STATE ---
enum ViewMode { VIEW_GRID, VIEW_DETAIL };
ViewMode viewMode = VIEW_GRID;
int detailIdx = 0;

// --- THREADING & TASK STATE ---
SemaphoreHandle_t dataMutex;
volatile bool fetchPending = true;
volatile bool fetching = false;
unsigned long lastFetchMillis = 0;
time_t lastFetchTime = 0;
unsigned long lastTouchAction = 0;
unsigned long lastWifiCheck = 0;
bool touchWasDown = false;
static int spinFrame = 0;

// --- COLORS ---
static inline uint16_t C_BG() { return darkMode ? TFT_BLACK : 0xEF7D; }
static inline uint16_t C_HEADER() { return darkMode ? 0x1082 : 0x4208; }
static inline uint16_t C_BORDER() { return darkMode ? 0x4208 : 0x8410; }
static inline uint16_t C_LABEL() { return darkMode ? 0xAD75 : 0x4208; }
static inline uint16_t C_PANEL() { return darkMode ? 0x0841 : 0xFFFF; }
static inline uint16_t C_TEXT() { return darkMode ? TFT_WHITE : TFT_BLACK; }
static inline uint16_t C_MUTED() { return darkMode ? 0x528A : 0x8410; }
static inline uint16_t C_UP() { return 0x07E0; }
static inline uint16_t C_DOWN() { return 0xF800; }
static inline uint16_t C_FLAT() { return 0x7BEF; }
static inline uint16_t C_ALERT() { return 0xFFE0; }

// ==========================================
// HARDWARE & UTILS
// ==========================================
void setLED(bool r, bool g, bool b) {
  digitalWrite(LED_R, !r);
  digitalWrite(LED_G, !g);
  digitalWrite(LED_B, !b);
}

void applyBrightness(int val) {
  analogWriteFrequency(5000);
  analogWrite(BL_PIN, val);
}

String displaySym(const String &sym) {
  if (sym == "BTC-USD") return "BTC";
  if (sym == "GC=F" || sym == "XAUUSD=X") return "XAU";
  int dot = sym.indexOf('.');
  return (dot != -1) ? sym.substring(0, dot) : sym;
}

String getCurrencySymbol(const String &curr) {
  if (curr == "EUR") return "EUR ";
  if (curr == "USD") return "$";
  if (curr == "GBP" || curr == "GBp") return "\xc2\xa3";
  if (curr == "PLN") return "PLN ";
  return curr.length() ? curr + " " : "$";
}

String formatPrice(float price, const String &currency) {
  char buf[24];
  if (price >= 10000) sprintf(buf, "%s%.0f", currency.c_str(), price);
  else if (price >= 1000) sprintf(buf, "%s%.1f", currency.c_str(), price);
  else if (price >= 10) sprintf(buf, "%s%.2f", currency.c_str(), price);
  else if (price >= 0.01f) sprintf(buf, "%s%.4f", currency.c_str(), price);
  else sprintf(buf, "%s%.6f", currency.c_str(), price);
  return String(buf);
}

// Returns short display label for current chart range
String rangeLabel() {
  if (chartRange == "1d") return "1D";
  if (chartRange == "5d") return "5D";
  if (chartRange == "1mo") return "1M";
  if (chartRange == "3mo") return "3M";
  if (chartRange == "6mo") return "6M";
  if (chartRange == "ytd") return "YTD";
  if (chartRange == "1y") return "1Y";
  if (chartRange == "3y") return "3Y";
  return "MAX";
}

// Returns Yahoo Finance interval parameter for the selected range
String intervalFor() {
  if (chartRange == "1d") return "15m";
  if (chartRange == "5d") return "30m";
  if (chartRange == "1y") return "1wk";
  if (chartRange == "3y") return "1wk";
  if (chartRange == "max") return "1mo";
  return "1d"; // 1mo, 3mo, 6mo, ytd
}

// Validates a range string from web form
bool isValidRange(const String& r) {
  return r == "1d" || r == "5d" || r == "1mo" || r == "3mo" ||
         r == "6mo" || r == "ytd" || r == "1y" || r == "3y" || r == "max";
}

// Parses a "YYYY-MM-DD" string (from an HTML date input) into a local
// unix timestamp at midday (avoids DST/timezone edge effects).
time_t parseDateYMD(const String &s) {
  if (s.length() < 10) return time(nullptr);
  int y = s.substring(0, 4).toInt();
  int mo = s.substring(5, 7).toInt();
  int d = s.substring(8, 10).toInt();
  if (y < 2000 || mo < 1 || mo > 12 || d < 1 || d > 31) return time(nullptr);
  struct tm tmv = {};
  tmv.tm_year = y - 1900;
  tmv.tm_mon = mo - 1;
  tmv.tm_mday = d;
  tmv.tm_hour = 12;
  tmv.tm_min = 0;
  tmv.tm_sec = 0;
  tmv.tm_isdst = -1;
  time_t t = mktime(&tmv);
  return (t > 0) ? t : time(nullptr);
}

// ==========================================
// TRANSACTIONS (COST BASIS)
// ==========================================

// Inserts a new lot for ticker i, keeping the array sorted ascending by
// date. Returns false if the history for this ticker is full.
bool addLot(int i, time_t ts, float qty, float pricePLN) {
  if (i < 0 || i >= MAX_TICKERS) return false;
  if (lotCount[i] >= MAX_LOTS) return false;
  int n = lotCount[i];
  int pos = n;
  for (int k = 0; k < n; k++) {
    if (ts < lots[i][k].ts) { pos = k; break; }
  }
  for (int k = n; k > pos; k--) lots[i][k] = lots[i][k - 1];
  lots[i][pos].ts = ts;
  lots[i][pos].qty = qty;
  lots[i][pos].pricePLN = pricePLN;
  lotCount[i] = n + 1;
  return true;
}

void deleteLot(int i, int idx) {
  if (i < 0 || i >= MAX_TICKERS) return;
  int n = lotCount[i];
  if (idx < 0 || idx >= n) return;
  for (int k = idx; k < n - 1; k++) lots[i][k] = lots[i][k + 1];
  lotCount[i] = n - 1;
}

// Replays the lot history in date order using the average-cost method to
// get the current quantity held and the remaining cost basis (in PLN,
// since lots already store PLN prices -- no FX conversion needed here).
void computeCostBasis(int i, float &qtyOut, double &costOut) {
  float qty = 0; double cost = 0;
  if (i >= 0 && i < MAX_TICKERS) {
    for (int k = 0; k < lotCount[i]; k++) {
      float d = lots[i][k].qty;
      if (d >= 0) {
        cost += (double)d * lots[i][k].pricePLN;
        qty += d;
      } else {
        float sellQty = -d;
        if (sellQty > qty) sellQty = qty; // guard: can't sell more than held
        if (qty > 0.00001f) {
          double avg = cost / qty;
          cost -= avg * sellQty;
          if (cost < 0) cost = 0;
        }
        qty -= sellQty;
        if (qty < 0) qty = 0;
      }
    }
  }
  qtyOut = qty;
  costOut = cost;
}

// Computes {value, cost basis, P&L}, all in PLN, for a given quote +
// original ticker index (origIdx is needed separately because the web UI
// may pass a locally re-sorted copy of the Quote struct).
bool computePLFor(const Quote &q, int origIdx, double &valueOut, double &costOut, double &plOut) {
  if (!q.valid || origIdx < 0 || origIdx >= MAX_TICKERS) return false;
  float qty; double cost;
  computeCostBasis(origIdx, qty, cost);
  if (qty <= 0.00001f) return false;
  float rate = getRateToPLN(q.currency);
  double pricePLN = (rate > 0) ? (double)q.price * rate : (double)q.price;
  valueOut = pricePLN * qty;
  costOut = cost;
  plOut = valueOut - cost;
  return true;
}

bool computePL(int i, double &v, double &c, double &p) {
  if (i < 0 || i >= MAX_TICKERS) return false;
  return computePLFor(quotes[i], i, v, c, p);
}

// ==========================================
// PREFERENCES
// ==========================================
void loadPrefs() {
  prefs.begin("ticker", true);
  refreshSec = prefs.getInt("refresh", DEFAULT_REFRESH);
  brightness = prefs.getInt("bright", 200);
  darkMode = prefs.getBool("dark", true);
  portfolioMode = prefs.getBool("portfolio", false);
  nightModeEnabled = prefs.getBool("nighten", false);
  nightFrom = prefs.getInt("nightfr", 0);
  nightTo = prefs.getInt("nightto", 8);
  chartRange = prefs.getString("range", "1d");
  tickerCount = prefs.getInt("tcount", 0);
  if (!isValidRange(chartRange)) chartRange = "1d";
  if (tickerCount < 0 || tickerCount > MAX_TICKERS) tickerCount = 0;

  for (int i = 0; i < tickerCount; i++) {
    tickers[i] = prefs.getString(("t" + String(i)).c_str(), "");
    alertHigh[i] = prefs.getFloat( ("ah" + String(i)).c_str(), 0.0f);
    alertLow[i] = prefs.getFloat( ("al" + String(i)).c_str(), 0.0f);
    quotes[i] = Quote{};
    quotes[i].sym = tickers[i];
    quotes[i].currency = "USD";

    // Load transactions, serialized as "ts:qty:price,ts:qty:price,..."
    String lkey = "lt" + String(i);
    bool hasLots = prefs.isKey(lkey.c_str());
    String ls = prefs.getString(lkey.c_str(), "");
    lotCount[i] = 0;
    int start = 0;
    for (int p = 0; p <= (int)ls.length() && lotCount[i] < MAX_LOTS; p++) {
      if (p == (int)ls.length() || ls[p] == ',') {
        String triple = ls.substring(start, p);
        int c1 = triple.indexOf(':');
        int c2 = (c1 > 0) ? triple.indexOf(':', c1 + 1) : -1;
        if (c1 > 0 && c2 > c1) {
          long ts = triple.substring(0, c1).toInt();
          float qty = triple.substring(c1 + 1, c2).toFloat();
          float price = triple.substring(c2 + 1).toFloat();
          lots[i][lotCount[i]].ts = (time_t)ts;
          lots[i][lotCount[i]].qty = qty;
          lots[i][lotCount[i]].pricePLN = price;
          lotCount[i]++;
        }
        start = p + 1;
      }
    }

    // One-time migration hint: only shown if this ticker has never had any
    // transaction saved under the new format yet. We deliberately do NOT
    // fabricate a lot from the old flat value, since we don't know what
    // price you actually paid -- you add the real dated purchases yourself.
    legacyHint[i] = (!hasLots) ? prefs.getFloat(("h" + String(i)).c_str(), 0.0f) : 0.0f;

    float q; double c;
    computeCostBasis(i, q, c);
    holdings[i] = q;
  }
  prefs.end();

  if (tickerCount == 0) {
    const char* def[] = { "ANAV.DE", "WEBN.DE", "BTC-USD", "GC=F" };
    for (int i = 0; i < 4; i++) {
      tickers[i] = def[i];
      holdings[i] = alertHigh[i] = alertLow[i] = 0;
      quotes[i].sym = tickers[i];
      quotes[i].currency = "USD";
      lotCount[i] = 0;
      legacyHint[i] = 0;
    }
    tickerCount = 4;
  }
}

void savePrefs() {
  prefs.begin("ticker", false);
  prefs.putInt("refresh", refreshSec);
  prefs.putInt("bright", brightness);
  prefs.putBool("dark", darkMode);
  prefs.putBool("portfolio", portfolioMode);
  prefs.putBool("nighten", nightModeEnabled);
  prefs.putInt("nightfr", nightFrom);
  prefs.putInt("nightto", nightTo);
  prefs.putString("range", chartRange);
  prefs.putInt("tcount", tickerCount);

  for (int i = 0; i < tickerCount; i++) {
    prefs.putString(("t" + String(i)).c_str(), tickers[i]);
    prefs.putFloat( ("ah" + String(i)).c_str(), alertHigh[i]);
    prefs.putFloat( ("al" + String(i)).c_str(), alertLow[i]);

    String ls = "";
    for (int k = 0; k < lotCount[i]; k++) {
      if (k) ls += ",";
      ls += String((long)lots[i][k].ts) + ":" + String(lots[i][k].qty, 6)
          + ":" + String(lots[i][k].pricePLN, 2);
    }
    prefs.putString(("lt" + String(i)).c_str(), ls);
  }
  prefs.end();
}

// ==========================================
// MARKET & NETWORK
// ==========================================
void fetchYahoo(int idx) {
  String interval = intervalFor();
  HTTPClient http;
  http.begin("https://query1.finance.yahoo.com/v8/finance/chart/"
    + tickers[idx] + "?interval=" + interval + "&range=" + chartRange);
  http.setTimeout(8000);

  Quote q = quotes[idx];
  q.valid = false;
  q.sparkCount = 0;

  if (http.GET() == 200) {
    JsonDocument doc;
    if (!deserializeJson(doc, http.getString())) {
      JsonObject meta = doc["chart"]["result"][0]["meta"];
      if (!meta.isNull()) {
        float price = meta["regularMarketPrice"] | 0.0f;

        // Collect sparkline with subsampling so full period fits in MAX_SPARK_POINTS
        JsonArray closeArr = doc["chart"]["result"][0]["indicators"]["quote"][0]["close"];
        float firstClose = 0.0f;
        if (!closeArr.isNull()) {
          // Count valid points and find first valid value
          int total = 0;
          for (JsonVariant v : closeArr) {
            if (!v.isNull() && v.as<float>() > 0) {
              if (total == 0) firstClose = v.as<float>();
              total++;
            }
          }
          // Subsample to spread full period across MAX_SPARK_POINTS slots
          int step = max(1, total / MAX_SPARK_POINTS);
          int count = 0;
          for (JsonVariant v : closeArr) {
            if (!v.isNull() && v.as<float>() > 0) {
              if (count % step == 0 && q.sparkCount < MAX_SPARK_POINTS) {
                q.sparkline[q.sparkCount++] = v.as<float>();
              }
              count++;
            }
          }
        }

        if (price > 0) {
          // % change baseline: for 1d use chartPreviousClose; for all other ranges use
          // the first valid data point in the returned series
          float prev = 0.0f;
          if (chartRange == "1d") {
            prev = meta["chartPreviousClose"] | (meta["previousClose"] | 0.0f);
          } else {
            prev = firstClose;
          }
          q.price = price;
          q.open = (prev > 0) ? prev : price;
          q.pct = (prev > 0) ? ((price - prev) / prev * 100.0f) : 0.0f;
          q.currency = String((const char*)(meta["currency"] | "USD"));
          q.currency.toUpperCase();
          q.valid = true;
          q.errors = 0;
        } else q.errors++;
      } else q.errors++;
    } else q.errors++;
  } else q.errors++;
  http.end();

  xSemaphoreTake(dataMutex, portMAX_DELAY);
  quotes[idx] = q;
  xSemaphoreGive(dataMutex);
}

float fetchYahooRate(String curr) {
  if (curr == "PLN" || curr.isEmpty()) return 1.0f;
  String qCurr = (curr == "GBp") ? "GBP" : curr;
  float mult = (curr == "GBp") ? 0.01f : 1.0f;
  HTTPClient http;
  http.begin("https://query1.finance.yahoo.com/v8/finance/chart/"
    + qCurr + "PLN=X?interval=1d&range=1d");
  http.setTimeout(5000);
  float rate = 0.0f;
  if (http.GET() == 200) {
    JsonDocument doc;
    if (!deserializeJson(doc, http.getString())) {
      rate = doc["chart"]["result"][0]["meta"]["regularMarketPrice"] | 0.0f;
    }
  }
  http.end();
  return (rate > 0) ? rate * mult : 0.0f;
}

float getRateToPLN(const String &curr) {
  if (curr == "PLN" || curr.isEmpty()) return 1.0f;
  for (int i = 0; i < rateCount; i++) if (exchangeRates[i].curr == curr) return exchangeRates[i].rate;
  return 0.0f;
}

// Fetches Yahoo Finance daily closes for [period1,period2) and returns the
// close on the last trading day at or before `target` (falls back to the
// earliest available day if `target` predates the whole window -- e.g. a
// date before the instrument was listed).
bool fetchHistoricalClose(const String &sym, time_t period1, time_t period2,
                           time_t target, float &closeOut, String &currencyOut) {
  HTTPClient http;
  http.begin("https://query1.finance.yahoo.com/v8/finance/chart/" + sym
    + "?period1=" + String((long)period1) + "&period2=" + String((long)period2)
    + "&interval=1d");
  http.setTimeout(8000);

  bool ok = false;
  if (http.GET() == 200) {
    JsonDocument doc;
    if (!deserializeJson(doc, http.getString())) {
      JsonObject result = doc["chart"]["result"][0];
      JsonArray ts = result["timestamp"];
      JsonArray closes = result["indicators"]["quote"][0]["close"];
      currencyOut = String((const char*)(result["meta"]["currency"] | "USD"));
      currencyOut.toUpperCase();

      bool haveOnOrBefore = false, haveFallback = false;
      float bestClose = 0, fallbackClose = 0;
      time_t bestTs = 0, fallbackTs = 0;

      size_t n = min(ts.size(), closes.size());
      for (size_t k = 0; k < n; k++) {
        if (closes[k].isNull()) continue;
        float ck = closes[k].as<float>();
        if (ck <= 0) continue;
        time_t tk = (time_t)ts[k].as<long>();
        if (tk <= target) {
          if (!haveOnOrBefore || tk > bestTs) { bestTs = tk; bestClose = ck; haveOnOrBefore = true; }
        } else if (!haveFallback || tk < fallbackTs) {
          fallbackTs = tk; fallbackClose = ck; haveFallback = true;
        }
      }
      if (haveOnOrBefore) { closeOut = bestClose; ok = true; }
      else if (haveFallback) { closeOut = fallbackClose; ok = true; }
    }
  }
  http.end();
  return ok;
}

// Looks up a ticker's closing price on/near a past date and converts it to
// PLN using the FX rate for that SAME date (not today's rate), so backfilled
// transactions get a historically accurate cost basis.
bool fetchHistoricalPricePLN(const String &sym, time_t target, float &pricePLNOut) {
  time_t period1 = target - (7 * 86400);
  time_t period2 = target + 86400;

  float closeNative; String currency;
  if (!fetchHistoricalClose(sym, period1, period2, target, closeNative, currency)) return false;

  if (currency == "PLN") { pricePLNOut = closeNative; return true; }

  String qCurr = (currency == "GBP") ? "GBP" : currency;
  float mult = (currency == "GBp") ? 0.01f : 1.0f; // pence sterling -> pounds, mirrors fetchYahooRate()

  float fxClose; String fxCurrencyOut;
  if (!fetchHistoricalClose(qCurr + "PLN=X", period1, period2, target, fxClose, fxCurrencyOut)) return false;

  pricePLNOut = closeNative * fxClose * mult;
  return true;
}

float getTickerValuePLN(int i) {
  if (!quotes[i].valid || holdings[i] <= 0) return -1.0f;
  float rate = getRateToPLN(quotes[i].currency);
  return (rate > 0 ? quotes[i].price * rate : quotes[i].price) * holdings[i];
}

void sortTickersIfNeeded() {
  if (!portfolioMode) return;
  for (int i = 0; i < tickerCount - 1; i++) {
    for (int j = 0; j < tickerCount - i - 1; j++) {
      float vA = getTickerValuePLN(j), vB = getTickerValuePLN(j + 1);
      if ((vB > vA) || (vA < 0 && vB < 0 && tickers[j + 1] < tickers[j])) {
        std::swap(tickers[j], tickers[j+1]);
        std::swap(holdings[j], holdings[j+1]);
        std::swap(alertHigh[j], alertHigh[j+1]);
        std::swap(alertLow[j], alertLow[j+1]);
        std::swap(quotes[j], quotes[j+1]);
        std::swap(lotCount[j], lotCount[j+1]);
        std::swap(legacyHint[j], legacyHint[j+1]);
        for (int k = 0; k < MAX_LOTS; k++) std::swap(lots[j][k], lots[j+1][k]);
      }
    }
  }
}

void checkAlerts() {
  bool triggered = false;
  xSemaphoreTake(dataMutex, portMAX_DELAY);
  for (int i = 0; i < tickerCount; i++) {
    if (!quotes[i].valid) continue;
    float rate = getRateToPLN(quotes[i].currency);
    float pricePLN = (rate > 0) ? quotes[i].price * rate : quotes[i].price;
    if ((alertHigh[i] > 0 && pricePLN >= alertHigh[i]) ||
        (alertLow[i] > 0 && pricePLN <= alertLow[i])) {
      triggered = true; break;
    }
  }
  xSemaphoreGive(dataMutex);
  if (triggered) {
    for (int f = 0; f < 3; f++) {
      setLED(true, true, false); delay(150);
      setLED(false, false, false); delay(150);
    }
  }
}

void fetchTask(void* param) {
  for (;;) {
    if (fetchPending) {
      fetchPending = false; fetching = true;
      setLED(false, false, true);

      for (int i = 0; i < tickerCount; i++) { fetchYahoo(i); vTaskDelay(pdMS_TO_TICKS(800)); }

      // Collect unique currencies needing conversion
      xSemaphoreTake(dataMutex, portMAX_DELAY);
      for (int i = 0; i < tickerCount; i++) {
        String c = quotes[i].currency;
        if (quotes[i].valid && c != "PLN" && !c.isEmpty()) {
          bool found = false;
          for (int r = 0; r < rateCount; r++) if (exchangeRates[r].curr == c) found = true;
          if (!found && rateCount < MAX_EXCHANGE_RATES) exchangeRates[rateCount++] = {c, 0.0f};
        }
      }
      xSemaphoreGive(dataMutex);

      for (int r = 0; r < rateCount; r++) {
        xSemaphoreTake(dataMutex, portMAX_DELAY); String c = exchangeRates[r].curr; xSemaphoreGive(dataMutex);
        float nr = fetchYahooRate(c);
        if (nr > 0) { xSemaphoreTake(dataMutex, portMAX_DELAY); exchangeRates[r].rate = nr; xSemaphoreGive(dataMutex); }
        vTaskDelay(pdMS_TO_TICKS(800));
      }

      xSemaphoreTake(dataMutex, portMAX_DELAY); sortTickersIfNeeded(); xSemaphoreGive(dataMutex);
      checkAlerts();

      lastFetchMillis = millis(); lastFetchTime = time(nullptr);
      fetching = false; setLED(false, false, false);
    }
    vTaskDelay(pdMS_TO_TICKS(200));
  }
}

// ==========================================
// DRAWING (TFT)
// ==========================================
int gridAreaHeight() { return 240 - HEADER_H - (portfolioMode ? FOOTER_H : 0); }

void drawHeader() {
  tft.fillRect(0, 0, 320, HEADER_H, C_HEADER());
  tft.setTextColor(darkMode ? TFT_WHITE : TFT_BLACK, C_HEADER());
  tft.setTextDatum(ML_DATUM);
  tft.drawString("CYD PORTFOLIO", 8, HEADER_H / 2, 2);

  // WiFi bars
  if (WiFi.status() == WL_CONNECTED) {
    int bars = (WiFi.RSSI() > -50) ? 4 : (WiFi.RSSI() > -65) ? 3 : (WiFi.RSSI() > -80) ? 2 : 1;
    for (int b = 0; b < 4; b++) {
      tft.fillRect(272 + b * 5, 4 + (HEADER_H - 8) - (2 + b * 3) - 2, 3, 2 + b * 3,
        (b < bars) ? (uint16_t)TFT_GREEN : C_MUTED());
    }
  }

  // Last fetch time
  if (lastFetchTime > 0) {
    char buf[16]; struct tm tm; localtime_r(&lastFetchTime, &tm);
    strftime(buf, sizeof(buf), "%H:%M", &tm);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(darkMode ? TFT_WHITE : TFT_BLACK, C_HEADER());
    tft.drawString(buf, 170, HEADER_H / 2, 2);
  }

  // Fetch spinner / done indicator
  const int cx = 308, cy = HEADER_H / 2;
  if (fetching) {
    const int8_t dx[] = { 0, 5, 0, -5 }, dy[] = { -5, 0, 5, 0 };
    for (int d = 0; d < 4; d++)
      tft.fillCircle(cx + dx[d], cy + dy[d], 2,
        ((d + spinFrame) % 4 == 0) ? (uint16_t)TFT_CYAN : C_MUTED());
    spinFrame++;
  } else {
    tft.fillCircle(cx, cy, 3, TFT_GREEN);
  }
}

void drawPortfolioFooter() {
  if (!portfolioMode) return;
  double total = 0, pl = 0; bool anyH = false;
  for (int i = 0; i < tickerCount; i++) {
    double v, c, p;
    if (computePL(i, v, c, p)) { total += v; pl += p; anyH = true; }
  }
  if (!anyH) return;

  tft.fillRect(0, 240 - FOOTER_H, 320, FOOTER_H, C_HEADER());
  tft.setTextColor((pl >= 0) ? C_UP() : C_DOWN(), C_HEADER());
  tft.setTextDatum(MC_DATUM);
  tft.drawString("PLN " + String(total, 2) + " P&L" + (pl >= 0 ? " +" : " ") + String(pl, 2),
    160, 240 - FOOTER_H / 2, 1);
}

void drawQuoteGrid(int idx, Quote &q) {
  int cols = (tickerCount <= 4) ? 1 : 2;
  int cellW = 320 / cols;
  int cellH = gridAreaHeight() / ((tickerCount + cols - 1) / cols);
  int x = (idx % cols) * cellW, y = HEADER_H + (idx / cols) * cellH;

  tft.fillRect(x + 1, y + 1, cellW - 2, cellH - 2, C_PANEL());
  tft.drawRect(x, y, cellW, cellH, C_BORDER());

  if (!q.valid) {
    tft.setTextColor(C_MUTED(), C_PANEL()); tft.setTextDatum(MC_DATUM);
    tft.drawString(displaySym(q.sym) + " (err)", x + cellW / 2, y + cellH / 2, 2); return;
  }

  float r = getRateToPLN(q.currency); bool cv = (r > 0 && q.currency != "PLN");
  float dP = cv ? q.price * r : q.price;
  uint16_t cPct = q.pct > 0.05f ? C_UP() : q.pct < -0.05f ? C_DOWN() : C_FLAT();

  int lineY = (portfolioMode && holdings[idx] > 0) ? y + cellH / 3 - 2 : y + cellH / 2;
  tft.setTextDatum(ML_DATUM); tft.setTextColor(C_LABEL(), C_PANEL());
  tft.drawString(displaySym(q.sym), x + 6, lineY, 2);
  tft.setTextDatum(MR_DATUM); tft.setTextColor(cPct, C_PANEL());
  tft.drawString((q.pct >= 0 ? "+" : "") + String(q.pct, 2) + "%", x + cellW - 6, lineY, 2);
  tft.setTextDatum(MC_DATUM); tft.setTextColor(C_TEXT(), C_PANEL());
  tft.drawString(formatPrice(dP, cv ? "PLN " : getCurrencySymbol(q.currency)),
    x + cellW / 2, lineY, 2);

  if (portfolioMode && holdings[idx] > 0 && cellH >= 40) {
    double v, c, pl;
    if (computePL(idx, v, c, pl)) {
      tft.setTextColor(pl >= 0 ? C_UP() : C_DOWN(), C_PANEL());
      tft.drawString("V:" + String(v, 0) + " P&L:" + (pl >= 0 ? "+" : "") + String(pl, 0),
        x + cellW / 2, y + (cellH * 2 / 3) + 4, 1);
    }
  }

  bool br = (alertHigh[idx] > 0 && dP >= alertHigh[idx]) || (alertLow[idx] > 0 && dP <= alertLow[idx]);
  if (br) tft.fillCircle(x + cellW - 5, y + 5, 3, C_ALERT());
  else if (alertHigh[idx] > 0 || alertLow[idx] > 0) tft.drawCircle(x + cellW - 5, y + 5, 3, C_ALERT());
}

void drawDetailView(int idx) {
  tft.fillRect(0, HEADER_H, 320, 240 - HEADER_H, C_BG());
  Quote &q = quotes[idx];

  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(C_LABEL());
  tft.drawString(q.sym, 160, HEADER_H + 15, 2);

  tft.setTextColor(C_TEXT());
  tft.drawString(formatPrice(q.price, getCurrencySymbol(q.currency)), 160, HEADER_H + 40, 4);

  uint16_t cPct = q.pct > 0 ? C_UP() : (q.pct < 0 ? C_DOWN() : C_FLAT());
  tft.setTextColor(cPct);
  tft.drawString((q.pct >= 0 ? "+" : "") + String(q.pct, 2) + "% (" + rangeLabel() + ")",
    160, HEADER_H + 65, 2);

  if (q.sparkCount > 1) {
    int chartX = 30, chartY = HEADER_H + 85, chartW = 260, chartH = 65;
    tft.drawRect(chartX - 2, chartY - 2, chartW + 4, chartH + 4, C_BORDER());
    float minP = q.sparkline[0], maxP = q.sparkline[0];
    for (int i = 1; i < q.sparkCount; i++) {
      if (q.sparkline[i] < minP) minP = q.sparkline[i];
      if (q.sparkline[i] > maxP) maxP = q.sparkline[i];
    }
    if (maxP > minP) {
      for (int i = 0; i < q.sparkCount - 1; i++) {
        int x1 = chartX + (i * chartW) / (q.sparkCount - 1);
        int y1 = chartY + chartH - (int)(((q.sparkline[i] - minP) * chartH) / (maxP - minP));
        int x2 = chartX + ((i + 1) * chartW) / (q.sparkCount - 1);
        int y2 = chartY + chartH - (int)(((q.sparkline[i + 1] - minP) * chartH) / (maxP - minP));
        tft.drawLine(x1, y1, x2, y2, cPct);
        tft.drawLine(x1, y1 + 1, x2, y2 + 1, cPct);
      }
    } else {
      tft.setTextColor(C_MUTED());
      tft.drawString("FLAT", 160, chartY + chartH / 2, 2);
    }
  } else {
    tft.setTextColor(C_MUTED());
    tft.drawString("No chart data", 160, HEADER_H + 110, 2);
  }

  tft.setTextDatum(MR_DATUM); tft.setTextColor(C_MUTED());
  tft.drawString("TAP TO GO BACK", 310, 240 - 20, 2);
}

void drawAll() {
  xSemaphoreTake(dataMutex, portMAX_DELAY);
  if (detailIdx >= tickerCount) { detailIdx = 0; viewMode = VIEW_GRID; }

  static int prevTickerCount = -1;
  static ViewMode prevViewMode = VIEW_GRID;

  drawHeader();

  if (viewMode == VIEW_GRID) {
    if (tickerCount != prevTickerCount || prevViewMode != VIEW_GRID) {
      tft.fillRect(0, HEADER_H, 320, 240 - HEADER_H, C_BG());
    }
    for (int i = 0; i < tickerCount; i++) drawQuoteGrid(i, quotes[i]);
    if (portfolioMode) drawPortfolioFooter();
  } else {
    drawDetailView(detailIdx);
  }

  prevTickerCount = tickerCount;
  prevViewMode = viewMode;
  xSemaphoreGive(dataMutex);
}

void handleTouch() {
  bool isDown = touch.tirqTouched() && touch.touched();
  if (isDown && !touchWasDown) {
    touchWasDown = true;
    if (millis() - lastTouchAction < TOUCH_DEBOUNCE_MS) return;
    lastTouchAction = millis();

    TS_Point p = touch.getPoint();
    int tx = map(p.x, 200, 3800, 0, 320), ty = map(p.y, 200, 3800, 0, 240);

    if (viewMode == VIEW_GRID) {
      if (tickerCount == 0) return;
      int cols = (tickerCount <= 4) ? 1 : 2;
      int idx = ((ty - HEADER_H) / (gridAreaHeight() / ((tickerCount + cols - 1) / cols))) * cols
        + (tx / (320 / cols));
      if (idx >= 0 && idx < tickerCount) { detailIdx = idx; viewMode = VIEW_DETAIL; drawAll(); }
    } else {
      viewMode = VIEW_GRID; drawAll();
    }
  }
  if (!isDown) touchWasDown = false;
}

// ==========================================
// WEB SERVER
// ==========================================
void handleFavicon() {
  server.send(200, "image/svg+xml",
    "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 32 32'>"
    "<rect x='4' y='20' width='4.5' height='8' rx='1' fill='#334155'/>"
    "<rect x='11' y='15' width='4.5' height='13' rx='1' fill='#475569'/>"
    "<rect x='18' y='10' width='4.5' height='18' rx='1' fill='#64748B'/>"
    "<rect x='25' y='4' width='4.5' height='24' rx='1' fill='#10B981'/>"
    "</svg>");
}

void handleRoot() {
  xSemaphoreTake(dataMutex, portMAX_DELAY);

  String localTickers[MAX_TICKERS];
  float localAlertHigh[MAX_TICKERS];
  float localAlertLow[MAX_TICKERS];
  Quote localQuotes[MAX_TICKERS];
  int localOrigIdx[MAX_TICKERS]; // tracks the original (unsorted) ticker index
  int localCount = tickerCount;

  for (int i = 0; i < localCount; i++) {
    localTickers[i] = tickers[i];
    localAlertHigh[i] = alertHigh[i];
    localAlertLow[i] = alertLow[i];
    localQuotes[i] = quotes[i];
    localOrigIdx[i] = i;
  }

  // Sort for portfolio view
  if (portfolioMode) {
    for (int i = 0; i < localCount - 1; i++) {
      for (int j = 0; j < localCount - i - 1; j++) {
        auto valOf = [&](int k) -> float {
          if (!localQuotes[k].valid || holdings[localOrigIdx[k]] <= 0) return -1.0f;
          float r = getRateToPLN(localQuotes[k].currency);
          return (r > 0 ? localQuotes[k].price * r : localQuotes[k].price) * holdings[localOrigIdx[k]];
        };
        float vA = valOf(j), vB = valOf(j + 1);
        if ((vB > vA) || (vA < 0 && vB < 0 && localTickers[j + 1] < localTickers[j])) {
          std::swap(localTickers[j], localTickers[j+1]);
          std::swap(localAlertHigh[j], localAlertHigh[j+1]);
          std::swap(localAlertLow[j], localAlertLow[j+1]);
          std::swap(localQuotes[j], localQuotes[j+1]);
          std::swap(localOrigIdx[j], localOrigIdx[j+1]);
        }
      }
    }
  }

  String tickerList = "";
  for (int i = 0; i < localCount; i++) { if (i) tickerList += ","; tickerList += localTickers[i]; }

  String rows = "";
  double totalVal = 0, totalPL = 0;
  bool anyMissing = false;

  for (int i = 0; i < localCount; i++) {
    String rawSym = localQuotes[i].sym;
    String symLink = "<a href='https://finance.yahoo.com/quote/" + rawSym
      + "/' target='_blank'>" + rawSym + "</a>";

    float rate = getRateToPLN(localQuotes[i].currency);
    bool conv = (rate > 0 && localQuotes[i].currency != "PLN");
    if (!conv && localQuotes[i].currency != "PLN" && localQuotes[i].valid) anyMissing = true;

    float dPrice = conv ? localQuotes[i].price * rate : localQuotes[i].price;

    String cSym = (!conv && localQuotes[i].valid && localQuotes[i].currency != "PLN")
      ? getCurrencySymbol(localQuotes[i].currency) : "";
    String price = localQuotes[i].valid ? cSym + String(dPrice, 2) : "--";
    String pct = localQuotes[i].valid
      ? (localQuotes[i].pct >= 0 ? "+" : "") + String(localQuotes[i].pct, 2) + "%" : "--";
    String clr = localQuotes[i].valid ? (localQuotes[i].pct >= 0 ? "#00cc44" : "#ff4444") : "#888";
    String arrow = localQuotes[i].valid
      ? (localQuotes[i].pct > 0.05f ? "&#9650;"
         : localQuotes[i].pct < -0.05f ? "&#9660;" : "&mdash;") : "";

    String valStr = "", plStr = "";
    double v, c, p;
    if (computePLFor(localQuotes[i], localOrigIdx[i], v, c, p)) {
      totalVal += v; totalPL += p;
      valStr = String(v, 2);
      plStr = (p >= 0 ? "+" : "") + String(p, 2);
    }

    rows += "<tr>"
      + String("<td>") + symLink + "</td>"
      + "<td class='tnowrap' style='font-weight:700'>" + price + "</td>"
      + "<td class='chg' style='color:" + clr + "'>" + arrow + " " + pct + "</td>"
      + "<td class='tnowrap'>" + valStr + "</td>"
      + "<td class='tnowrap' style='color:" + clr + "'>" + plStr + "</td>"
      + "</tr>";
  }

  // Holdings & Alerts: quantity + avg cost are read-only (derived from the
  // transactions above); only the alert thresholds stay editable here.
  String holdRows = "";
  for (int i = 0; i < localCount; i++) {
    int orig = localOrigIdx[i];
    float qty; double cost;
    computeCostBasis(orig, qty, cost);
    double avgCost = (qty > 0.00001) ? (cost / qty) : 0.0;

    holdRows += "<tr><td>" + localTickers[i] + "</td>"
      + "<td class='tnowrap'>" + String(qty, 4) + "</td>"
      + "<td class='tnowrap'>" + (qty > 0.00001 ? String(avgCost, 2) : "&mdash;") + "</td>"
      + "<td><input class='inp' type='number' name='ahi" + i + "' form='cfgform' value='" + String(localAlertHigh[i], 2)
      + "' step='any' min='0' placeholder='0=off'></td>"
      + "<td><input class='inp' type='number' name='alo" + i + "' form='cfgform' value='" + String(localAlertLow[i], 2)
      + "' step='any' min='0' placeholder='0=off'></td></tr>";
  }

  // Transactions card: dropdown options + recent lots per ticker
  String tickerOptions = "";
  for (int i = 0; i < localCount; i++) {
    tickerOptions += "<option value='" + String(localOrigIdx[i]) + "'>" + localTickers[i] + "</option>";
  }

  String txRows = "";
  for (int i = 0; i < localCount; i++) {
    int orig = localOrigIdx[i];
    int n = lotCount[orig];
    int shown = min(n, MAX_LOTS_SHOWN);
    for (int k = n - shown; k < n; k++) {
      time_t ts = lots[orig][k].ts;
      struct tm tmk; localtime_r(&ts, &tmk);
      char buf[12]; strftime(buf, sizeof(buf), "%d.%m.%Y", &tmk);
      String qtyStr = (lots[orig][k].qty >= 0 ? "+" : "") + String(lots[orig][k].qty, 4);
      txRows += "<tr><td>" + localTickers[i] + "</td><td class='tnowrap'>" + String(buf) + "</td>"
        + "<td class='tnowrap'>" + qtyStr + "</td>"
        + "<td class='tnowrap'>" + String(lots[orig][k].pricePLN, 2) + "</td>"
        + "<td><a class='dellink' href='/dellot?lt=" + String(orig) + "&lk=" + String(k)
        + "' onclick=\"return confirm('Delete this transaction?')\">&#10005;</a></td></tr>";
    }
    if (n > shown) {
      txRows += "<tr><td colspan='5' class='hint2'>...and " + String(n - shown)
        + " older (full list at /api/quotes)</td></tr>";
    }
    if (n == 0 && legacyHint[orig] > 0) {
      txRows += "<tr><td colspan='5' class='hint2' style='color:#e6a23c'>Legacy record detected: "
        + String(legacyHint[orig], 4) + " units (no purchase price) &mdash; add real transactions below.</td></tr>";
    }
  }

  xSemaphoreGive(dataMutex);

  server.send(200, "text/html; charset=utf-8",
    buildRootHtml(darkMode, portfolioMode,
      rows, holdRows, txRows, tickerOptions, tickerList,
      totalVal, totalPL, anyMissing,
      refreshSec, brightness, MIN_REFRESH, DEFAULT_REFRESH,
      nightModeEnabled, nightFrom, nightTo,
      chartRange, rangeLabel()));
}

void handleSave() {
  if (server.hasArg("tickers")) {
    String raw = server.arg("tickers");

    // Snapshot old per-ticker data BY SYMBOL (not by index) before rebuilding,
    // so alerts and -- crucially -- transaction history survive reordering,
    // adding or removing tickers in the list above.
    static String oldSym[MAX_TICKERS];
    static float oldAH[MAX_TICKERS], oldAL[MAX_TICKERS];
    static int oldLotCount[MAX_TICKERS];
    static Lot oldLots[MAX_TICKERS][MAX_LOTS];
    static float oldLegacyHint[MAX_TICKERS];
    int oldCount = tickerCount;
    for (int k = 0; k < oldCount; k++) {
      oldSym[k] = tickers[k];
      oldAH[k] = alertHigh[k];
      oldAL[k] = alertLow[k];
      oldLotCount[k] = lotCount[k];
      for (int m = 0; m < lotCount[k]; m++) oldLots[k][m] = lots[k][m];
      oldLegacyHint[k] = legacyHint[k];
    }

    xSemaphoreTake(dataMutex, portMAX_DELAY);
    tickerCount = 0;
    int start = 0;
    for (int i = 0; i <= (int)raw.length(); i++) {
      if (i == (int)raw.length() || raw[i] == ',') {
        String t = raw.substring(start, i); t.trim();
        if (t.length() && tickerCount < MAX_TICKERS) {
          t.toUpperCase();
          tickers[tickerCount] = t;
          quotes[tickerCount] = Quote{};
          quotes[tickerCount].sym = t;
          quotes[tickerCount].currency = "USD";

          int match = -1;
          for (int k = 0; k < oldCount; k++) if (oldSym[k] == t) { match = k; break; }
          if (match >= 0) {
            alertHigh[tickerCount] = oldAH[match];
            alertLow[tickerCount] = oldAL[match];
            lotCount[tickerCount] = oldLotCount[match];
            for (int m = 0; m < oldLotCount[match]; m++) lots[tickerCount][m] = oldLots[match][m];
            legacyHint[tickerCount] = oldLegacyHint[match];
          } else {
            alertHigh[tickerCount] = 0;
            alertLow[tickerCount] = 0;
            lotCount[tickerCount] = 0;
            legacyHint[tickerCount] = 0;
          }
          float q; double c; computeCostBasis(tickerCount, q, c);
          holdings[tickerCount] = q;
          tickerCount++;
        }
        start = i + 1;
      }
    }
    xSemaphoreGive(dataMutex);
  }

  for (int i = 0; i < tickerCount; i++) {
    if (server.hasArg("ahi" + String(i))) alertHigh[i] = server.arg("ahi" + String(i)).toFloat();
    if (server.hasArg("alo" + String(i))) alertLow[i] = server.arg("alo" + String(i)).toFloat();
  }

  if (server.hasArg("refresh")) {
    refreshSec = server.arg("refresh").toInt();
    if (refreshSec < MIN_REFRESH) refreshSec = MIN_REFRESH;
  }
  if (server.hasArg("bright")) {
    int val = server.arg("bright").toInt();
    if (val >= 10 && val <= 255) brightness = val;
    applyBrightness(brightness);
  }
  darkMode = server.hasArg("darkmode");
  portfolioMode = server.hasArg("portfolio");
  nightModeEnabled = server.hasArg("nighten");
  if (server.hasArg("nightfr")) { int v = server.arg("nightfr").toInt(); if (v >= 0 && v <= 23) nightFrom = v; }
  if (server.hasArg("nightto")) { int v = server.arg("nightto").toInt(); if (v >= 0 && v <= 23) nightTo = v; }
  if (server.hasArg("range")) {
    String r = server.arg("range");
    if (isValidRange(r)) chartRange = r;
  }

  savePrefs();
  fetchPending = true;
  drawAll();

  server.send(200, "text/html; charset=utf-8",
    buildRedirectPage(darkMode, "\xe2\x9c\x85", "Settings saved!"));
}

// Adds one dated transaction (buy or sell) for a ticker. Params:
//   lt = ticker index, ld = date "YYYY-MM-DD", lq = qty (+/-), lp = price PLN/unit
void handleAddLot() {
  int t = server.hasArg("lt") ? server.arg("lt").toInt() : -1;
  String dateStr = server.hasArg("ld") ? server.arg("ld") : "";
  float qty = server.hasArg("lq") ? server.arg("lq").toFloat() : 0;
  float price = server.hasArg("lp") ? server.arg("lp").toFloat() : -1;

  bool ok = (t >= 0 && t < tickerCount && dateStr.length() >= 10 && qty != 0 && price >= 0);
  if (ok) {
    time_t ts = parseDateYMD(dateStr);
    xSemaphoreTake(dataMutex, portMAX_DELAY);
    ok = addLot(t, ts, qty, price);
    if (ok) { float q; double c; computeCostBasis(t, q, c); holdings[t] = q; }
    xSemaphoreGive(dataMutex);
    if (ok) savePrefs();
  }

  server.send(200, "text/html; charset=utf-8",
    buildRedirectPage(darkMode, ok ? "\xe2\x9c\x85" : "\xe2\x9a\xa0\xef\xb8\x8f",
      ok ? "Transaction added!" : "Could not add (check fields / history full)"));
}

// Deletes one transaction. Params: lt = ticker index, lk = lot index
void handleDelLot() {
  int t = server.hasArg("lt") ? server.arg("lt").toInt() : -1;
  int k = server.hasArg("lk") ? server.arg("lk").toInt() : -1;
  bool ok = (t >= 0 && t < tickerCount && k >= 0 && k < lotCount[t]);
  if (ok) {
    xSemaphoreTake(dataMutex, portMAX_DELAY);
    deleteLot(t, k);
    float q; double c; computeCostBasis(t, q, c); holdings[t] = q;
    xSemaphoreGive(dataMutex);
    savePrefs();
  }
  server.send(200, "text/html; charset=utf-8",
    buildRedirectPage(darkMode, ok ? "\xf0\x9f\x97\x91" : "\xe2\x9a\xa0\xef\xb8\x8f",
      ok ? "Transaction deleted" : "Not found"));
}

// Returns the JSON-encoded closing price (in PLN) for a ticker on/near a
// past date. Called from the transaction form's "Fetch" button so the user
// doesn't have to look up historical prices by hand. Params:
//   lt = ticker index, ld = date "YYYY-MM-DD"
void handleHistPrice() {
  int t = server.hasArg("lt") ? server.arg("lt").toInt() : -1;
  String dateStr = server.hasArg("ld") ? server.arg("ld") : "";

  if (t < 0 || t >= tickerCount || dateStr.length() < 10) {
    server.send(200, "application/json", "{\"ok\":false}");
    return;
  }

  time_t target = parseDateYMD(dateStr);
  String sym = tickers[t];
  float pricePLN = 0;
  bool ok = fetchHistoricalPricePLN(sym, target, pricePLN);

  String json = ok
    ? "{\"ok\":true,\"pricePLN\":" + String(pricePLN, 4) + "}"
    : "{\"ok\":false}";
  server.send(200, "application/json", json);
}

void handleApiQuotes() {
  String json = "[";
  xSemaphoreTake(dataMutex, portMAX_DELAY);
  for (int i = 0; i < tickerCount; i++) {
    float rate = getRateToPLN(quotes[i].currency);
    bool conv = (rate > 0 && quotes[i].currency != "PLN");
    float dPrice = conv ? quotes[i].price * rate : quotes[i].price;
    float qty; double cost;
    computeCostBasis(i, qty, cost);
    double valuePLN = (double)dPrice * qty;
    double plPLN = valuePLN - cost;

    String lotsJson = "[";
    for (int k = 0; k < lotCount[i]; k++) {
      if (k) lotsJson += ",";
      lotsJson += "{\"ts\":" + String((long)lots[i][k].ts)
        + ",\"qty\":" + String(lots[i][k].qty, 6)
        + ",\"pricePLN\":" + String(lots[i][k].pricePLN, 2) + "}";
    }
    lotsJson += "]";

    if (i) json += ",";
    json += "{\"sym\":\"" + quotes[i].sym + "\","
      + "\"pricePLN\":" + String(dPrice, 4) + ","
      + "\"pct\":" + String(quotes[i].pct, 2) + ","
      + "\"range\":\"" + chartRange + "\","
      + "\"nativePrice\":" + String(quotes[i].price, 4) + ","
      + "\"nativeCurrency\":\"" + quotes[i].currency + "\","
      + "\"heldQty\":" + String(qty, 6) + ","
      + "\"costBasisPLN\":" + String(cost, 2) + ","
      + "\"plPLN\":" + String(plPLN, 4) + ","
      + "\"lots\":" + lotsJson + ","
      + "\"valid\":" + (quotes[i].valid ? "true" : "false") + "}";
  }
  xSemaphoreGive(dataMutex);
  json += "]";
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(200, "application/json", json);
}

void handleForceRefresh() {
  fetchPending = true;
  server.send(200, "text/html; charset=utf-8",
    buildRedirectPage(darkMode, "\xf0\x9f\x94\x84", "Refreshing..."));
}

// ==========================================
// SETUP & LOOP
// ==========================================
void setup() {
  Serial.begin(115200); delay(300);
  pinMode(LED_R, OUTPUT); pinMode(LED_G, OUTPUT); pinMode(LED_B, OUTPUT);
  pinMode(BL_PIN, OUTPUT); digitalWrite(BL_PIN, HIGH);

  tft.init(); tft.setRotation(3); tft.fillScreen(TFT_BLACK);
  touchSPI.begin(25, 39, 32, TOUCH_CS_PIN);
  touch.begin(touchSPI); touch.setRotation(3);

  loadPrefs(); applyBrightness(brightness);
  dataMutex = xSemaphoreCreateMutex();

  tft.fillScreen(C_BG()); tft.setTextColor(TFT_CYAN, C_BG());
  tft.setTextDatum(MC_DATUM); tft.drawString("Connecting WiFi...", 160, 120, 2);

  WiFiManager wm;
  wm.setConfigPortalTimeout(120);
  wm.setAPCallback([](WiFiManager*) {
    tft.fillScreen(C_BG()); tft.setTextColor(TFT_YELLOW, C_BG());
    tft.setTextDatum(MC_DATUM);
    tft.drawString("WiFi: PortfolioTracker-Setup", 160, 110, 2);
    tft.drawString("192.168.4.1", 160, 130, 2);
    setLED(false, true, false);
  });

  if (!wm.autoConnect("PortfolioTracker-Setup")) {
    tft.fillScreen(TFT_RED); tft.setTextColor(TFT_WHITE, TFT_RED);
    tft.setTextDatum(MC_DATUM); tft.drawString("WiFi failed! Restarting...", 160, 120, 2);
    delay(3000); ESP.restart();
  }

  setLED(false, true, false);
  tft.fillScreen(C_BG()); tft.setTextColor(TFT_CYAN, C_BG());
  tft.setTextDatum(MC_DATUM); tft.drawString("Syncing time...", 160, 120, 2);
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  setenv("TZ", "CET-1CEST,M3.5.0/2,M10.5.0/3", 1); tzset();

  tft.fillScreen(C_BG()); tft.setTextColor(TFT_GREEN, C_BG());
  tft.setTextDatum(MC_DATUM);
  tft.drawString("http://" + WiFi.localIP().toString() + "/", 160, 110, 2);
  tft.drawString("http://portfolio-tracker.local/", 160, 130, 2);
  delay(2000);

  server.on("/", HTTP_GET, handleRoot);
  server.on("/favicon.svg",HTTP_GET, handleFavicon);
  server.on("/save", HTTP_POST, handleSave);
  server.on("/addlot", HTTP_POST, handleAddLot);
  server.on("/api/histprice", HTTP_GET, handleHistPrice);
  server.on("/dellot", HTTP_GET, handleDelLot);
  server.on("/refresh", HTTP_GET, handleForceRefresh);
  server.on("/api/quotes", HTTP_GET, handleApiQuotes);
  server.begin();
  MDNS.begin("portfolio-tracker");

  setLED(false, false, false);
  xTaskCreatePinnedToCore(fetchTask, "fetch", 8192, NULL, 1, NULL, 0);
  lastTouchAction = millis();
}

void loop() {
  server.handleClient();
  handleTouch();

  if (!fetching && (millis() - lastFetchMillis) >= (unsigned long)refreshSec * 1000UL)
    fetchPending = true;

  if (millis() - lastWifiCheck > WIFI_CHECK_MS) {
    lastWifiCheck = millis();
    if (WiFi.status() != WL_CONNECTED) WiFi.reconnect();
  }

  static unsigned long lastTick = 0;
  static bool lastFetch = false;
  if (millis() - lastTick > 250) {
    lastTick = millis();
    if (fetching || lastFetch) {
      xSemaphoreTake(dataMutex, portMAX_DELAY); drawHeader(); xSemaphoreGive(dataMutex);
    }
    if (lastFetch && !fetching) drawAll();
    lastFetch = fetching;
  }

  // Night mode auto-brightness
  {
    static int lastNightBright = -1;
    int target = brightness;
    if (nightModeEnabled) {
      struct tm tmn;
      if (getLocalTime(&tmn)) {
        int h = tmn.tm_hour;
        bool inNight = (nightFrom < nightTo)
          ? (h >= nightFrom && h < nightTo)
          : (h >= nightFrom || h < nightTo);
        if (inNight) target = 25;
      }
    }
    if (target != lastNightBright) { applyBrightness(target); lastNightBright = target; }
  }
}
