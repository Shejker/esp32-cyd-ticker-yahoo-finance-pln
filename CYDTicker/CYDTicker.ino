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
static constexpr time_t NTP_SYNC_MIN_EPOCH = 1700000000; // ~2023-11-14
static constexpr int TOUCH_MIN = 200;
static constexpr int TOUCH_MAX = 3800;

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
// (positive = bought, negative = sold), at pricePLN per unit
struct Lot {
  time_t ts;
  float qty;
  float pricePLN;
};

// Unified state for a single ticker to prevent fragmented parallel arrays
struct TickerState {
  String sym;
  float holdings;
  float alertHigh;
  float alertLow;
  Quote quote;
  Lot lots[MAX_LOTS];
  int lotCount;
  float legacyHint;
};

struct AppConfig {
  int refreshSec;
  int brightness;
  bool darkMode;
  bool portfolioMode;
  bool nightModeEnabled;
  int nightFrom;
  int nightTo;
  String chartRange;
};

// --- APP STATE ---
TickerState tickerData[MAX_TICKERS];
int tickerCount = 0;

AppConfig cfg = {DEFAULT_REFRESH, 200, true, false, false, 0, 8, "1d"};

ExRate exchangeRates[MAX_EXCHANGE_RATES];
int rateCount = 0;

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
static inline uint16_t C_BG() { return cfg.darkMode ? TFT_BLACK : 0xEF7D; }
static inline uint16_t C_HEADER() { return cfg.darkMode ? 0x1082 : 0x4208; }
static inline uint16_t C_BORDER() { return cfg.darkMode ? 0x4208 : 0x8410; }
static inline uint16_t C_LABEL() { return cfg.darkMode ? 0xAD75 : 0x4208; }
static inline uint16_t C_PANEL() { return cfg.darkMode ? 0x0841 : 0xFFFF; }
static inline uint16_t C_TEXT() { return cfg.darkMode ? TFT_WHITE : TFT_BLACK; }
static inline uint16_t C_MUTED() { return cfg.darkMode ? 0x528A : 0x8410; }
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

String rangeLabel(const String& range) {
  if (range == "1d") return "1D";
  if (range == "5d") return "5D";
  if (range == "1mo") return "1M";
  if (range == "3mo") return "3M";
  if (range == "6mo") return "6M";
  if (range == "ytd") return "YTD";
  if (range == "1y") return "1Y";
  if (range == "3y") return "3Y";
  return "MAX";
}

String intervalFor(const String& range) {
  if (range == "1d") return "15m";
  if (range == "5d") return "30m";
  if (range == "1y") return "1wk";
  if (range == "3y") return "1wk";
  if (range == "max") return "1mo";
  return "1d"; // 1mo, 3mo, 6mo, ytd
}

bool isValidRange(const String& r) {
  return r == "1d" || r == "5d" || r == "1mo" || r == "3mo" ||
         r == "6mo" || r == "ytd" || r == "1y" || r == "3y" || r == "max";
}

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

String urlEncode(const String &s) {
  String out;
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    if (isalnum((unsigned char)c) || c == '-' || c == '.' || c == '_' || c == '~') {
      out += c;
    } else {
      char buf[4];
      snprintf(buf, sizeof(buf), "%%%02X", (unsigned char)c);
      out += buf;
    }
  }
  return out;
}

// Protected helper to find ticker index by symbol
int getIndexBySym(const String &sym) {
  for (int i = 0; i < tickerCount; i++) {
    if (tickerData[i].sym == sym) return i;
  }
  return -1;
}

// ==========================================
// TRANSACTIONS (COST BASIS)
// ==========================================
bool addLot(int i, time_t ts, float qty, float pricePLN) {
  if (i < 0 || i >= MAX_TICKERS) return false;
  if (tickerData[i].lotCount >= MAX_LOTS) return false;
  int n = tickerData[i].lotCount;
  int pos = n;
  for (int k = 0; k < n; k++) {
    if (ts < tickerData[i].lots[k].ts) { pos = k; break; }
  }
  for (int k = n; k > pos; k--) tickerData[i].lots[k] = tickerData[i].lots[k - 1];
  tickerData[i].lots[pos].ts = ts;
  tickerData[i].lots[pos].qty = qty;
  tickerData[i].lots[pos].pricePLN = pricePLN;
  tickerData[i].lotCount = n + 1;
  return true;
}

void deleteLot(int i, int idx) {
  if (i < 0 || i >= MAX_TICKERS) return;
  int n = tickerData[i].lotCount;
  if (idx < 0 || idx >= n) return;
  for (int k = idx; k < n - 1; k++) tickerData[i].lots[k] = tickerData[i].lots[k + 1];
  tickerData[i].lotCount = n - 1;
}

void computeCostBasis(const TickerState& ts, float &qtyOut, double &costOut) {
  float qty = 0; double cost = 0;
  for (int k = 0; k < ts.lotCount; k++) {
    float d = ts.lots[k].qty;
    if (d >= 0) {
      cost += (double)d * ts.lots[k].pricePLN;
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
  qtyOut = qty;
  costOut = cost;
}

bool computePLFor(const TickerState &ts, double &valueOut, double &costOut, double &plOut) {
  if (!ts.quote.valid) return false;
  float qty; double cost;
  computeCostBasis(ts, qty, cost);
  if (qty <= 0.00001f) return false;
  float rate = getRateToPLN(ts.quote.currency);
  double pricePLN = (rate > 0) ? (double)ts.quote.price * rate : (double)ts.quote.price;
  valueOut = pricePLN * qty;
  costOut = cost;
  plOut = valueOut - cost;
  return true;
}

bool computePeriodPLFor(const TickerState &ts, double &valueOut, double &periodPLOut) {
  if (!ts.quote.valid || ts.holdings <= 0.00001f) return false;
  float rate = getRateToPLN(ts.quote.currency);
  double pricePLN = (rate > 0) ? (double)ts.quote.price * rate : (double)ts.quote.price;
  valueOut = pricePLN * ts.holdings;
  double pctFrac = (double)ts.quote.pct / 100.0;
  periodPLOut = (pctFrac > -1.0) ? valueOut * pctFrac / (1.0 + pctFrac) : 0.0;
  return true;
}

bool computePeriodPL(int i, double &v, double &p) {
  if (i < 0 || i >= MAX_TICKERS) return false;
  return computePeriodPLFor(tickerData[i], v, p);
}

// ==========================================
// PREFERENCES
// ==========================================
void loadPrefs() {
  prefs.begin("ticker", true);
  cfg.refreshSec = prefs.getInt("refresh", DEFAULT_REFRESH);
  cfg.brightness = prefs.getInt("bright", 200);
  cfg.darkMode = prefs.getBool("dark", true);
  cfg.portfolioMode = prefs.getBool("portfolio", false);
  cfg.nightModeEnabled = prefs.getBool("nighten", false);
  cfg.nightFrom = prefs.getInt("nightfr", 0);
  cfg.nightTo = prefs.getInt("nightto", 8);
  cfg.chartRange = prefs.getString("range", "1d");
  tickerCount = prefs.getInt("tcount", 0);
  if (!isValidRange(cfg.chartRange)) cfg.chartRange = "1d";
  if (tickerCount < 0 || tickerCount > MAX_TICKERS) tickerCount = 0;

  for (int i = 0; i < tickerCount; i++) {
    tickerData[i].sym = prefs.getString(("t" + String(i)).c_str(), "");
    tickerData[i].alertHigh = prefs.getFloat( ("ah" + String(i)).c_str(), 0.0f);
    tickerData[i].alertLow = prefs.getFloat( ("al" + String(i)).c_str(), 0.0f);
    tickerData[i].quote = Quote{};
    tickerData[i].quote.sym = tickerData[i].sym;
    tickerData[i].quote.currency = "USD";

    String lkey = "lt" + String(i);
    bool hasLots = prefs.isKey(lkey.c_str());
    String ls = prefs.getString(lkey.c_str(), "");
    tickerData[i].lotCount = 0;
    int start = 0;
    for (int p = 0; p <= (int)ls.length() && tickerData[i].lotCount < MAX_LOTS; p++) {
      if (p == (int)ls.length() || ls[p] == ',') {
        String triple = ls.substring(start, p);
        int c1 = triple.indexOf(':');
        int c2 = (c1 > 0) ? triple.indexOf(':', c1 + 1) : -1;
        if (c1 > 0 && c2 > c1) {
          long ts = triple.substring(0, c1).toInt();
          float qty = triple.substring(c1 + 1, c2).toFloat();
          float price = triple.substring(c2 + 1).toFloat();
          tickerData[i].lots[tickerData[i].lotCount].ts = (time_t)ts;
          tickerData[i].lots[tickerData[i].lotCount].qty = qty;
          tickerData[i].lots[tickerData[i].lotCount].pricePLN = price;
          tickerData[i].lotCount++;
        }
        start = p + 1;
      }
    }

    tickerData[i].legacyHint = (!hasLots) ? prefs.getFloat(("h" + String(i)).c_str(), 0.0f) : 0.0f;

    float q; double c;
    computeCostBasis(tickerData[i], q, c);
    tickerData[i].holdings = q;
  }
  prefs.end();

  if (tickerCount == 0) {
    const char* def[] = { "AAPL", "MSFT", "BTC-USD", "GC=F" };
    for (int i = 0; i < 4; i++) {
      tickerData[i].sym = def[i];
      tickerData[i].holdings = tickerData[i].alertHigh = tickerData[i].alertLow = 0;
      tickerData[i].quote.sym = tickerData[i].sym;
      tickerData[i].quote.currency = "USD";
      tickerData[i].lotCount = 0;
      tickerData[i].legacyHint = 0;
    }
    tickerCount = 4;
  }
}

void savePrefs() {
  TickerState* localData = new TickerState[MAX_TICKERS];
  AppConfig localCfg;
  int localCount;

  xSemaphoreTake(dataMutex, portMAX_DELAY);
  localCount = tickerCount;
  localCfg = cfg;
  for (int i = 0; i < localCount; i++) localData[i] = tickerData[i];
  xSemaphoreGive(dataMutex);

  prefs.begin("ticker", false);
  prefs.putInt("refresh", localCfg.refreshSec);
  prefs.putInt("bright", localCfg.brightness);
  prefs.putBool("dark", localCfg.darkMode);
  prefs.putBool("portfolio", localCfg.portfolioMode);
  prefs.putBool("nighten", localCfg.nightModeEnabled);
  prefs.putInt("nightfr", localCfg.nightFrom);
  prefs.putInt("nightto", localCfg.nightTo);
  prefs.putString("range", localCfg.chartRange);
  prefs.putInt("tcount", localCount);

  for (int i = 0; i < localCount; i++) {
    prefs.putString(("t" + String(i)).c_str(), localData[i].sym);
    prefs.putFloat( ("ah" + String(i)).c_str(), localData[i].alertHigh);
    prefs.putFloat( ("al" + String(i)).c_str(), localData[i].alertLow);

    String ls = "";
    for (int k = 0; k < localData[i].lotCount; k++) {
      if (k) ls += ",";
      ls += String((long)localData[i].lots[k].ts) + ":" + String(localData[i].lots[k].qty, 6)
          + ":" + String(localData[i].lots[k].pricePLN, 2);
    }
    prefs.putString(("lt" + String(i)).c_str(), ls);
  }
  prefs.end();
  delete[] localData;
}

// ==========================================
// MARKET & NETWORK
// ==========================================
void fetchYahoo(int idx, const String& sym, const String& chartRange) {
  String interval = intervalFor(chartRange);
  HTTPClient http;
  http.begin("https://query1.finance.yahoo.com/v8/finance/chart/"
    + sym + "?interval=" + interval + "&range=" + chartRange);
  http.setTimeout(8000);

  Quote q;
  q.sym = sym;
  q.currency = "USD";
  q.valid = false;
  q.sparkCount = 0;
  q.errors = 0;

  if (http.GET() == 200) {
    JsonDocument doc;
    if (!deserializeJson(doc, http.getString())) {
      JsonObject meta = doc["chart"]["result"][0]["meta"];
      if (!meta.isNull()) {
        float price = meta["regularMarketPrice"] | 0.0f;

        JsonArray closeArr = doc["chart"]["result"][0]["indicators"]["quote"][0]["close"];
        float firstClose = 0.0f;
        if (!closeArr.isNull()) {
          int total = 0;
          for (JsonVariant v : closeArr) {
            if (!v.isNull() && v.as<float>() > 0) {
              if (total == 0) firstClose = v.as<float>();
              total++;
            }
          }
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
          float prev = 0.0f;
          if (chartRange == "1d") {
            prev = meta["chartPreviousClose"] | (meta["previousClose"] | 0.0f);
          } else {
            prev = firstClose;
          }
          q.price = price;
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
  if (idx < tickerCount && tickerData[idx].sym == sym) {
      tickerData[idx].quote = q;
  } else {
      int real_idx = getIndexBySym(sym);
      if (real_idx >= 0) tickerData[real_idx].quote = q;
  }
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

bool fetchHistoricalPricePLN(const String &sym, time_t target, float &pricePLNOut) {
  time_t period1 = target - (7 * 86400);
  time_t period2 = target + 86400;

  float closeNative; String currency;
  if (!fetchHistoricalClose(sym, period1, period2, target, closeNative, currency)) return false;

  if (currency == "PLN") { pricePLNOut = closeNative; return true; }

  String qCurr = (currency == "GBP") ? "GBP" : currency;
  float mult = (currency == "GBp") ? 0.01f : 1.0f;

  float fxClose; String fxCurrencyOut;
  if (!fetchHistoricalClose(qCurr + "PLN=X", period1, period2, target, fxClose, fxCurrencyOut)) return false;

  pricePLNOut = closeNative * fxClose * mult;
  return true;
}

float getTickerValuePLN(int i) {
  if (!tickerData[i].quote.valid || tickerData[i].holdings <= 0) return -1.0f;
  float rate = getRateToPLN(tickerData[i].quote.currency);
  return (rate > 0 ? tickerData[i].quote.price * rate : tickerData[i].quote.price) * tickerData[i].holdings;
}

void sortTickersIfNeeded() {
  if (!cfg.portfolioMode) return;
  for (int i = 0; i < tickerCount - 1; i++) {
    for (int j = 0; j < tickerCount - i - 1; j++) {
      float vA = getTickerValuePLN(j), vB = getTickerValuePLN(j + 1);
      if ((vB > vA) || (vA < 0 && vB < 0 && tickerData[j + 1].sym < tickerData[j].sym)) {
        std::swap(tickerData[j], tickerData[j+1]);
      }
    }
  }
}

void checkAlerts() {
  bool triggered = false;
  xSemaphoreTake(dataMutex, portMAX_DELAY);
  for (int i = 0; i < tickerCount; i++) {
    if (!tickerData[i].quote.valid) continue;
    float rate = getRateToPLN(tickerData[i].quote.currency);
    float pricePLN = (rate > 0) ? tickerData[i].quote.price * rate : tickerData[i].quote.price;
    if ((tickerData[i].alertHigh > 0 && pricePLN >= tickerData[i].alertHigh) ||
        (tickerData[i].alertLow > 0 && pricePLN <= tickerData[i].alertLow)) {
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

      for (int i = 0; i < tickerCount; i++) {
        String sym; String chartRange;
        xSemaphoreTake(dataMutex, portMAX_DELAY);
        if (i < tickerCount) { sym = tickerData[i].sym; chartRange = cfg.chartRange; }
        xSemaphoreGive(dataMutex);

        if (sym.length()) fetchYahoo(i, sym, chartRange);
        vTaskDelay(pdMS_TO_TICKS(800));
      }

      xSemaphoreTake(dataMutex, portMAX_DELAY);
      for (int i = 0; i < tickerCount; i++) {
        String c = tickerData[i].quote.currency;
        if (tickerData[i].quote.valid && c != "PLN" && !c.isEmpty()) {
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
int gridAreaHeight() { return 240 - HEADER_H - (cfg.portfolioMode ? FOOTER_H : 0); }

void drawHeader(bool spinnerOnly = false) {
  const int cx = 308, cy = HEADER_H / 2;

  if (!spinnerOnly) {
    tft.fillRect(0, 0, 320, HEADER_H, C_HEADER());
    tft.setTextColor(cfg.darkMode ? TFT_WHITE : TFT_BLACK, C_HEADER());
    tft.setTextDatum(ML_DATUM);
    tft.drawString("CYD PORTFOLIO", 8, HEADER_H / 2, 2);

    if (WiFi.status() == WL_CONNECTED) {
      int bars = (WiFi.RSSI() > -50) ? 4 : (WiFi.RSSI() > -65) ? 3 : (WiFi.RSSI() > -80) ? 2 : 1;
      for (int b = 0; b < 4; b++) {
        tft.fillRect(272 + b * 5, 4 + (HEADER_H - 8) - (2 + b * 3) - 2, 3, 2 + b * 3,
          (b < bars) ? (uint16_t)TFT_GREEN : C_MUTED());
      }
    }

    if (lastFetchTime > 0) {
      char buf[16]; struct tm tm; localtime_r(&lastFetchTime, &tm);
      strftime(buf, sizeof(buf), "%H:%M", &tm);
      tft.setTextDatum(MC_DATUM);
      tft.setTextColor(cfg.darkMode ? TFT_WHITE : TFT_BLACK, C_HEADER());
      tft.drawString(buf, 170, HEADER_H / 2, 2);
    }
  }

  // Fetch spinner updates only its local area
  tft.fillRect(cx - 6, cy - 6, 13, 13, C_HEADER());
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
  if (!cfg.portfolioMode) return;
  double total = 0, pl = 0;
  bool anyHeld = false, anyValid = false;
  for (int i = 0; i < tickerCount; i++) {
    if (tickerData[i].holdings > 0) anyHeld = true;
    double v, p;
    if (computePeriodPL(i, v, p)) { total += v; pl += p; anyValid = true; }
  }
  if (!anyHeld) return;

  tft.fillRect(0, 240 - FOOTER_H, 320, FOOTER_H, C_HEADER());
  tft.setTextDatum(MC_DATUM);

  if (!anyValid) {
    tft.setTextColor(C_MUTED(), C_HEADER());
    tft.drawString("Refreshing...", 160, 240 - FOOTER_H / 2, 1);
    return;
  }

  tft.setTextColor((pl >= 0) ? C_UP() : C_DOWN(), C_HEADER());
  tft.drawString("PLN:" + String(total, 2) + " P&L(" + rangeLabel(cfg.chartRange) + "):" + (pl >= 0 ? "+" : "") + String(pl, 2),
    160, 240 - FOOTER_H / 2, 1);
}

void drawQuoteGrid(int idx, Quote &q) {
  int cols = (tickerCount <= 4) ? 1 : 2;
  int cellW = 320 / cols;
  int cellH = gridAreaHeight() / ((tickerCount + cols - 1) / cols);
  int x = (idx % cols) * cellW, y = HEADER_H + (idx / cols) * cellH;

  tft.fillRect(x + 1, y + 1, cellW - 2, cellH - 2, C_PANEL());
  tft.drawRect(x, y, cellW, cellH, C_BORDER());

  int mainFont = (cols == 2) ? 1 : 2;

  if (!q.valid) {
    tft.setTextColor(C_MUTED(), C_PANEL()); tft.setTextDatum(MC_DATUM);
    tft.drawString(q.sym + " (err)", x + cellW / 2, y + cellH / 2, mainFont); return;
  }

  float r = getRateToPLN(q.currency); bool cv = (r > 0 && q.currency != "PLN");
  float dP = cv ? q.price * r : q.price;
  uint16_t cPct = q.pct > 0.0f ? C_UP() : q.pct < 0.0f ? C_DOWN() : C_FLAT();

  bool showPL = cfg.portfolioMode && tickerData[idx].holdings > 0 && cellH >= 40;

  if (cols == 1) {
      int y1 = y + cellH / 3;
      int y2 = y + cellH * 2 / 3;

      tft.setTextDatum(ML_DATUM);
      tft.setTextColor(C_LABEL(), C_PANEL());
      tft.drawString(q.sym, x + 4, y1, mainFont);

      String priceStr = formatPrice(dP, cv ? "PLN " : getCurrencySymbol(q.currency));
      String pctStr = (q.pct >= 0 ? "+" : "") + String(q.pct, 2) + "%";

      tft.setTextDatum(MC_DATUM);
      tft.setTextColor(C_TEXT(), C_PANEL());
      tft.drawString(priceStr, x + cellW / 2, y1, mainFont);

      tft.setTextDatum(MR_DATUM);
      tft.setTextColor(cPct, C_PANEL());
      tft.drawString(pctStr, x + cellW - 12, y1, mainFont);

      if (showPL) {
          double v, pl;
          if (computePeriodPL(idx, v, pl)) {
              tft.setTextDatum(MC_DATUM);
              tft.setTextColor(pl >= 0 ? C_UP() : C_DOWN(), C_PANEL());
              tft.drawString(
                  "V:" + String((long)round(v)) + " P&L(" + rangeLabel(cfg.chartRange) + "):" +
                  (pl >= 0 ? "+" : "") + String(pl, 2),
                  x + cellW / 2, y2, 1);
          }
      }

  } else {
      int lines = showPL ? 3 : 2;
      int rowH = max(cellH / (lines + 1), tft.fontHeight(mainFont) + 4);
      int y1 = y + rowH;
      int y2 = y + rowH * 2;

      tft.setTextDatum(MC_DATUM);
      tft.setTextColor(C_LABEL(), C_PANEL());
      tft.drawString(q.sym, x + cellW / 2, y1, mainFont);

      String priceStr = formatPrice(dP, cv ? "PLN " : getCurrencySymbol(q.currency));
      String pctStr = (q.pct >= 0 ? "+" : "") + String(q.pct, 2) + "%";

      int gap = 6;
      int wPrice = tft.textWidth(priceStr, mainFont);
      int wPct = tft.textWidth(pctStr, mainFont);
      int totalW = wPrice + gap + wPct;
      int startX = x + (cellW - totalW) / 2;

      tft.setTextDatum(ML_DATUM);
      tft.setTextColor(C_TEXT(), C_PANEL());
      tft.drawString(priceStr, startX, y2, mainFont);

      tft.setTextColor(cPct, C_PANEL());
      tft.drawString(pctStr, startX + wPrice + gap, y2, mainFont);

      if (showPL) {
          double v, pl;
          if (computePeriodPL(idx, v, pl)) {
              int y3 = y + rowH * 3;
              String vStr = "V:" + String((long)round(v));
              String plStr = "P&L(" + rangeLabel(cfg.chartRange) + "):" + (pl >= 0 ? "+" : "") + String(pl, 2);
              uint16_t plCol = pl >= 0 ? C_UP() : C_DOWN();

              tft.setTextDatum(MC_DATUM);
              tft.setTextColor(plCol, C_PANEL());
              tft.drawString(vStr + "  " + plStr, x + cellW / 2, y3, 1);
          }
      }
  }

  bool br = (tickerData[idx].alertHigh > 0 && dP >= tickerData[idx].alertHigh) ||
            (tickerData[idx].alertLow > 0 && dP <= tickerData[idx].alertLow);

  if (br)
      tft.fillCircle(x + cellW - 5, y + 5, 3, C_ALERT());
  else if (tickerData[idx].alertHigh > 0 || tickerData[idx].alertLow > 0)
      tft.drawCircle(x + cellW - 5, y + 5, 3, C_ALERT());
}

void drawEmptyGridCell(int idx) {
  int cols = (tickerCount <= 4) ? 1 : 2;
  int cellW = 320 / cols;
  int cellH = gridAreaHeight() / ((tickerCount + cols - 1) / cols);
  int x = (idx % cols) * cellW, y = HEADER_H + (idx / cols) * cellH;
  tft.fillRect(x + 1, y + 1, cellW - 2, cellH - 2, C_PANEL());
  tft.drawRect(x, y, cellW, cellH, C_BORDER());
}

void drawDetailView(int idx) {
  tft.fillRect(0, HEADER_H, 320, 240 - HEADER_H, C_BG());
  Quote &q = tickerData[idx].quote;

  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(C_LABEL());
  tft.drawString(q.sym, 160, HEADER_H + 15, 2);

  tft.setTextColor(C_TEXT());
  tft.drawString(formatPrice(q.price, getCurrencySymbol(q.currency)), 160, HEADER_H + 40, 4);

  uint16_t cPct = q.pct > 0 ? C_UP() : (q.pct < 0 ? C_DOWN() : C_FLAT());
  tft.setTextColor(cPct);
  tft.drawString((q.pct >= 0 ? "+" : "") + String(q.pct, 2) + "% (" + rangeLabel(cfg.chartRange) + ")",
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
  static bool prevPortfolioMode = cfg.portfolioMode;

  drawHeader(false);

  if (viewMode == VIEW_GRID) {
    if (tickerCount != prevTickerCount || prevViewMode != VIEW_GRID || cfg.portfolioMode != prevPortfolioMode) {
      tft.fillRect(0, HEADER_H, 320, 240 - HEADER_H, C_BG());
    }
    for (int i = 0; i < tickerCount; i++) drawQuoteGrid(i, tickerData[i].quote);
    if (tickerCount > 0) {
      int cols = (tickerCount <= 4) ? 1 : 2;
      int totalCells = cols * ((tickerCount + cols - 1) / cols);
      for (int i = tickerCount; i < totalCells; i++) drawEmptyGridCell(i);
    }
    if (cfg.portfolioMode) drawPortfolioFooter();
  } else {
    drawDetailView(detailIdx);
  }

  prevTickerCount = tickerCount;
  prevViewMode = viewMode;
  prevPortfolioMode = cfg.portfolioMode;
  xSemaphoreGive(dataMutex);
}

void handleTouch() {
  bool isDown = touch.tirqTouched() && touch.touched();
  if (isDown && !touchWasDown) {
    touchWasDown = true;
    if (millis() - lastTouchAction < TOUCH_DEBOUNCE_MS) return;
    lastTouchAction = millis();

    TS_Point p = touch.getPoint();
    int tx = map(p.x, TOUCH_MIN, TOUCH_MAX, 0, 320), ty = map(p.y, TOUCH_MIN, TOUCH_MAX, 0, 240);

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

  TickerState* localData = new TickerState[MAX_TICKERS];
  int localCount = tickerCount;
  AppConfig localCfg = cfg;

  for (int i = 0; i < localCount; i++) {
    localData[i] = tickerData[i];
  }
  xSemaphoreGive(dataMutex);

  if (localCfg.portfolioMode) {
    for (int i = 0; i < localCount - 1; i++) {
      for (int j = 0; j < localCount - i - 1; j++) {
        auto valOf = [&](int k) -> float {
          if (!localData[k].quote.valid || localData[k].holdings <= 0) return -1.0f;
          float r = getRateToPLN(localData[k].quote.currency);
          return (r > 0 ? localData[k].quote.price * r : localData[k].quote.price) * localData[k].holdings;
        };
        float vA = valOf(j), vB = valOf(j + 1);
        if ((vB > vA) || (vA < 0 && vB < 0 && localData[j + 1].sym < localData[j].sym)) {
          std::swap(localData[j], localData[j+1]);
        }
      }
    }
  }

  String tickerList = "";
  for (int i = 0; i < localCount; i++) { if (i) tickerList += ","; tickerList += localData[i].sym; }

  String rows = "";
  double totalVal = 0, totalPL = 0;
  bool anyMissing = false;

  for (int i = 0; i < localCount; i++) {
    String rawSym = localData[i].quote.sym;
    String symLink = "<a href='https://finance.yahoo.com/quote/" + rawSym
      + "/' target='_blank'>" + rawSym + "</a>";

    float rate = getRateToPLN(localData[i].quote.currency);
    bool conv = (rate > 0 && localData[i].quote.currency != "PLN");
    if (!conv && localData[i].quote.currency != "PLN" && localData[i].quote.valid) anyMissing = true;

    float dPrice = conv ? localData[i].quote.price * rate : localData[i].quote.price;

    String cSym = (!conv && localData[i].quote.valid && localData[i].quote.currency != "PLN")
      ? getCurrencySymbol(localData[i].quote.currency) : "";
    String price = localData[i].quote.valid ? cSym + String(dPrice, 2) : "--";
    String pct = localData[i].quote.valid
      ? (localData[i].quote.pct >= 0 ? "+" : "") + String(localData[i].quote.pct, 2) + "%" : "--";
    String clr = localData[i].quote.valid ? (localData[i].quote.pct >= 0 ? "#00cc44" : "#ff4444") : "#888";
    String arrow = localData[i].quote.valid
      ? (localData[i].quote.pct > 0.0f ? "&#9650;"
         : localData[i].quote.pct < 0.0f ? "&#9660;" : "&mdash;") : "";

    String valStr = "", plStr = "", plClr = clr;
    double v, p;
    if (computePeriodPLFor(localData[i], v, p)) {
      totalVal += v; totalPL += p;
      valStr = String(v, 2);
      plStr = (p >= 0 ? "+" : "") + String(p, 2);
      plClr = (p >= 0) ? "#00cc44" : "#ff4444";
    }

    rows += "<tr>"
      + String("<td>") + symLink + "</td>"
      + "<td class='tnowrap' style='font-weight:700'>" + price + "</td>"
      + "<td class='chg' style='color:" + clr + "'>" + arrow + " " + pct + "</td>"
      + "<td class='tnowrap'>" + valStr + "</td>"
      + "<td class='tnowrap' style='color:" + plClr + "'>" + plStr + "</td>"
      + "</tr>";
  }

  String holdRows = "";
  double totalRealPL = 0;
  bool anyRealPl = false;
  for (int i = 0; i < localCount; i++) {
    float qty; double cost;
    computeCostBasis(localData[i], qty, cost);
    double avgCost = (qty > 0.00001) ? (cost / qty) : 0.0;

    double plVal, plCost, pl;
    bool havePl = computePLFor(localData[i], plVal, plCost, pl);
    String plStr = havePl ? (pl >= 0 ? "+" : "") + String(pl, 2) : "&mdash;";
    String plClr = havePl ? (pl >= 0 ? "#00cc44" : "#ff4444") : "inherit";
    if (havePl) { totalRealPL += pl; anyRealPl = true; }

    holdRows += "<tr><td>" + localData[i].sym + "</td>"
      + "<td class='tnowrap'>" + String(qty, 4) + "</td>"
      + "<td class='tnowrap'>" + (qty > 0.00001 ? String(avgCost, 2) : "&mdash;") + "</td>"
      + "<td class='tnowrap' style='color:" + plClr + "'>" + plStr + "</td>"
      + "<td><input class='inp' type='number' name='ahi" + i + "' form='cfgform' value='" + String(localData[i].alertHigh, 2)
      + "' step='any' min='0' placeholder='0=off'></td>"
      + "<td><input class='inp' type='number' name='alo" + i + "' form='cfgform' value='" + String(localData[i].alertLow, 2)
      + "' step='any' min='0' placeholder='0=off'></td></tr>";
  }

  String tickerOptions = "";
  for (int i = 0; i < localCount; i++) {
    tickerOptions += "<option value='" + localData[i].sym + "'>" + localData[i].sym + "</option>";
  }

  String txRows = "";
  for (int i = 0; i < localCount; i++) {
    int n = localData[i].lotCount;
    int shown = min(n, MAX_LOTS_SHOWN);
    for (int k = n - shown; k < n; k++) {
      time_t ts = localData[i].lots[k].ts;
      struct tm tmk; localtime_r(&ts, &tmk);
      char buf[12]; strftime(buf, sizeof(buf), "%d.%m.%Y", &tmk);
      char isoBuf[11]; strftime(isoBuf, sizeof(isoBuf), "%Y-%m-%d", &tmk);
      String qtyStr = (localData[i].lots[k].qty >= 0 ? "+" : "") + String(localData[i].lots[k].qty, 4);
      double total = localData[i].lots[k].qty * localData[i].lots[k].pricePLN;
      String totalStr = (total >= 0 ? "+" : "") + String(total, 2);
      String encSym = urlEncode(localData[i].sym);
      txRows += "<tr><td>" + localData[i].sym + "</td><td class='tnowrap'>" + String(buf) + "</td>"
        + "<td class='tnowrap'>" + qtyStr + "</td>"
        + "<td class='tnowrap'>" + String(localData[i].lots[k].pricePLN, 2) + "</td>"
        + "<td class='tnowrap'>" + totalStr + "</td>"
        + "<td class='tnowrap'>"
        + "<a class='editlink' href='#' onclick=\"editLot('" + localData[i].sym + "','" + isoBuf + "',"
        + String(localData[i].lots[k].qty, 6) + "," + String(localData[i].lots[k].pricePLN, 2) + "," + String(k)
        + ");return false;\">&#9998;</a> "
        + "<a class='dellink' href='/dellot?lt=" + encSym + "&lk=" + String(k)
        + "' onclick=\"return confirm('Delete this transaction?')\">&#10005;</a></td></tr>";
    }
    if (n > shown) {
      txRows += "<tr><td colspan='6' class='hint2'>...and " + String(n - shown)
        + " older (full list at /api/quotes)</td></tr>";
    }
    if (n == 0 && localData[i].legacyHint > 0) {
      txRows += "<tr><td colspan='6' class='hint2' style='color:#e6a23c'>Legacy record detected: "
        + String(localData[i].legacyHint, 4) + " units (no purchase price) &mdash; add real transactions below.</td></tr>";
    }
  }

  sendRootHtml(server, localCfg.darkMode, localCfg.portfolioMode,
    rows, holdRows, txRows, tickerOptions, tickerList,
    totalVal, totalPL, anyMissing,
    totalRealPL, anyRealPl,
    localCfg.refreshSec, localCfg.brightness, MIN_REFRESH, DEFAULT_REFRESH,
    localCfg.nightModeEnabled, localCfg.nightFrom, localCfg.nightTo,
    localCfg.chartRange, rangeLabel(localCfg.chartRange));

  delete[] localData;
}

void handleSave() {
  if (server.hasArg("tickers")) {
    String raw = server.arg("tickers");

    TickerState* oldData = new TickerState[MAX_TICKERS];
    int oldCount = tickerCount;
    for (int k = 0; k < oldCount; k++) oldData[k] = tickerData[k];

    xSemaphoreTake(dataMutex, portMAX_DELAY);
    tickerCount = 0;
    int start = 0;
    for (int i = 0; i <= (int)raw.length(); i++) {
      if (i == (int)raw.length() || raw[i] == ',') {
        String t = raw.substring(start, i); t.trim();
        if (t.length() && tickerCount < MAX_TICKERS) {
          t.toUpperCase();
          tickerData[tickerCount].sym = t;
          tickerData[tickerCount].quote = Quote{};
          tickerData[tickerCount].quote.sym = t;
          tickerData[tickerCount].quote.currency = "USD";

          int match = -1;
          for (int k = 0; k < oldCount; k++) if (oldData[k].sym == t) { match = k; break; }
          if (match >= 0) {
            tickerData[tickerCount].alertHigh = oldData[match].alertHigh;
            tickerData[tickerCount].alertLow = oldData[match].alertLow;
            tickerData[tickerCount].lotCount = oldData[match].lotCount;
            for (int m = 0; m < oldData[match].lotCount; m++) tickerData[tickerCount].lots[m] = oldData[match].lots[m];
            tickerData[tickerCount].legacyHint = oldData[match].legacyHint;
          } else {
            tickerData[tickerCount].alertHigh = 0;
            tickerData[tickerCount].alertLow = 0;
            tickerData[tickerCount].lotCount = 0;
            tickerData[tickerCount].legacyHint = 0;
          }
          float q; double c; computeCostBasis(tickerData[tickerCount], q, c);
          tickerData[tickerCount].holdings = q;
          tickerCount++;
        }
        start = i + 1;
      }
    }
    xSemaphoreGive(dataMutex);
    delete[] oldData;
  }

  xSemaphoreTake(dataMutex, portMAX_DELAY);
  for (int i = 0; i < tickerCount; i++) {
    if (server.hasArg("ahi" + String(i))) tickerData[i].alertHigh = server.arg("ahi" + String(i)).toFloat();
    if (server.hasArg("alo" + String(i))) tickerData[i].alertLow = server.arg("alo" + String(i)).toFloat();
  }
  xSemaphoreGive(dataMutex);

  if (server.hasArg("refresh")) {
    cfg.refreshSec = server.arg("refresh").toInt();
    if (cfg.refreshSec < MIN_REFRESH) cfg.refreshSec = MIN_REFRESH;
  }
  if (server.hasArg("bright")) {
    int val = server.arg("bright").toInt();
    if (val >= 10 && val <= 255) cfg.brightness = val;
    applyBrightness(cfg.brightness);
  }
  cfg.darkMode = server.hasArg("darkmode");
  cfg.portfolioMode = server.hasArg("portfolio");
  cfg.nightModeEnabled = server.hasArg("nighten");
  if (server.hasArg("nightfr")) { int v = server.arg("nightfr").toInt(); if (v >= 0 && v <= 23) cfg.nightFrom = v; }
  if (server.hasArg("nightto")) { int v = server.arg("nightto").toInt(); if (v >= 0 && v <= 23) cfg.nightTo = v; }
  if (server.hasArg("range")) {
    String r = server.arg("range");
    if (isValidRange(r)) cfg.chartRange = r;
  }

  savePrefs();
  fetchPending = true;
  drawAll();

  server.send(200, "text/html; charset=utf-8", buildRedirectPage(cfg.darkMode, "\xe2\x9c\x85", "Settings saved!"));
}

void handleAddLot() {
  String sym = server.hasArg("lt") ? server.arg("lt") : "";
  String dateStr = server.hasArg("ld") ? server.arg("ld") : "";
  float qty = server.hasArg("lq") ? server.arg("lq").toFloat() : 0;
  float price = server.hasArg("lp") ? server.arg("lp").toFloat() : -1;

  bool ok = false;
  if (sym.length() && dateStr.length() >= 10 && qty != 0 && price >= 0) {
    time_t ts = parseDateYMD(dateStr);
    xSemaphoreTake(dataMutex, portMAX_DELAY);
    int t = getIndexBySym(sym);
    if (t >= 0) {
      ok = addLot(t, ts, qty, price);
      if (ok) { float q; double c; computeCostBasis(tickerData[t], q, c); tickerData[t].holdings = q; }
    }
    xSemaphoreGive(dataMutex);
    if (ok) savePrefs();
  }

  server.send(200, "text/html; charset=utf-8",
    buildRedirectPage(cfg.darkMode, ok ? "\xe2\x9c\x85" : "\xe2\x9a\xa0\xef\xb8\x8f",
      ok ? "Transaction added!" : "Could not add (check fields / history full)"));
}

void handleDelLot() {
  String sym = server.hasArg("lt") ? server.arg("lt") : "";
  int k = server.hasArg("lk") ? server.arg("lk").toInt() : -1;

  bool ok = false;
  if (sym.length() && k >= 0) {
    xSemaphoreTake(dataMutex, portMAX_DELAY);
    int t = getIndexBySym(sym);
    if (t >= 0 && k < tickerData[t].lotCount) {
      deleteLot(t, k);
      float q; double c; computeCostBasis(tickerData[t], q, c); tickerData[t].holdings = q;
      ok = true;
    }
    xSemaphoreGive(dataMutex);
    if (ok) savePrefs();
  }
  server.send(200, "text/html; charset=utf-8",
    buildRedirectPage(cfg.darkMode, ok ? "\xf0\x9f\x97\x91" : "\xe2\x9a\xa0\xef\xb8\x8f",
      ok ? "Transaction deleted" : "Transaction not found"));
}

void handleEditLot() {
  String origSym = server.hasArg("lo") ? server.arg("lo") : (server.hasArg("lt") ? server.arg("lt") : "");
  String sym = server.hasArg("lt") ? server.arg("lt") : "";
  int k = server.hasArg("lk") ? server.arg("lk").toInt() : -1;
  String dateStr = server.hasArg("ld") ? server.arg("ld") : "";
  float qty = server.hasArg("lq") ? server.arg("lq").toFloat() : 0;
  float price = server.hasArg("lp") ? server.arg("lp").toFloat() : -1;

  bool ok = false;
  if (origSym.length() && sym.length() && k >= 0 && dateStr.length() >= 10 && qty != 0 && price >= 0) {
    time_t ts = parseDateYMD(dateStr);
    xSemaphoreTake(dataMutex, portMAX_DELAY);
    int origT = getIndexBySym(origSym);
    int newT = getIndexBySym(sym);

    if (origT >= 0 && newT >= 0 && k < tickerData[origT].lotCount) {
      Lot backup = tickerData[origT].lots[k];
      deleteLot(origT, k);
      ok = addLot(newT, ts, qty, price);
      if (!ok) addLot(origT, backup.ts, backup.qty, backup.pricePLN);

      float q; double c;
      computeCostBasis(tickerData[origT], q, c); tickerData[origT].holdings = q;
      if (newT != origT) { computeCostBasis(tickerData[newT], q, c); tickerData[newT].holdings = q; }
    }
    xSemaphoreGive(dataMutex);
    if (ok) savePrefs();
  }

  server.send(200, "text/html; charset=utf-8",
    buildRedirectPage(cfg.darkMode, ok ? "\xe2\x9c\x85" : "\xe2\x9a\xa0\xef\xb8\x8f",
      ok ? "Transaction updated!" : "Could not update (check fields / target history full)"));
}

void handleHistPrice() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  String sym = server.hasArg("lt") ? server.arg("lt") : "";
  String dateStr = server.hasArg("ld") ? server.arg("ld") : "";

  xSemaphoreTake(dataMutex, portMAX_DELAY);
  int found = getIndexBySym(sym);
  xSemaphoreGive(dataMutex);

  if (sym.isEmpty() || found < 0 || dateStr.length() < 10) {
    server.send(200, "application/json", "{\"ok\":false}");
    return;
  }

  time_t target = parseDateYMD(dateStr);
  float pricePLN = 0;
  bool ok = fetchHistoricalPricePLN(sym, target, pricePLN);

  String json = ok
    ? "{\"ok\":true,\"pricePLN\":" + String(pricePLN, 4) + "}"
    : "{\"ok\":false}";
  server.send(200, "application/json", json);
}

void handleApiQuotes() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "application/json", "");
  
  xSemaphoreTake(dataMutex, portMAX_DELAY);
  server.sendContent("[");
  for (int i = 0; i < tickerCount; i++) {
    float rate = getRateToPLN(tickerData[i].quote.currency);
    bool conv = (rate > 0 && tickerData[i].quote.currency != "PLN");
    float dPrice = conv ? tickerData[i].quote.price * rate : tickerData[i].quote.price;
    float qty; double cost;
    computeCostBasis(tickerData[i], qty, cost);
    double valuePLN = (double)dPrice * qty;
    double plPLN = valuePLN - cost;

    String chunk = (i > 0) ? ",{" : "{";
    chunk += "\"sym\":\"" + tickerData[i].sym + "\","
      + "\"pricePLN\":" + String(dPrice, 4) + ","
      + "\"pct\":" + String(tickerData[i].quote.pct, 2) + ","
      + "\"range\":\"" + cfg.chartRange + "\","
      + "\"nativePrice\":" + String(tickerData[i].quote.price, 4) + ","
      + "\"nativeCurrency\":\"" + tickerData[i].quote.currency + "\","
      + "\"heldQty\":" + String(qty, 6) + ","
      + "\"costBasisPLN\":" + String(cost, 2) + ","
      + "\"plPLN\":" + String(plPLN, 4) + ","
      + "\"valid\":" + String(tickerData[i].quote.valid ? "true" : "false") + ","
      + "\"lots\":[";
    server.sendContent(chunk);

    for (int k = 0; k < tickerData[i].lotCount; k++) {
      String lJson = (k > 0) ? ",{" : "{";
      lJson += "\"ts\":" + String((long)tickerData[i].lots[k].ts)
        + ",\"qty\":" + String(tickerData[i].lots[k].qty, 6)
        + ",\"pricePLN\":" + String(tickerData[i].lots[k].pricePLN, 2) + "}";
      server.sendContent(lJson);
    }
    server.sendContent("]}");
  }
  server.sendContent("]");
  xSemaphoreGive(dataMutex);
  server.sendContent("");
}

void handleForceRefresh() {
  fetchPending = true;
  server.send(200, "text/html; charset=utf-8",
    buildRedirectPage(cfg.darkMode, "\xf0\x9f\x94\x84", "Refreshing..."));
}

void handleApiTransactions() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "application/json", "");
  server.sendContent("[\n");
  
  xSemaphoreTake(dataMutex, portMAX_DELAY);
  bool first = true;
  for (int i = 0; i < tickerCount; i++) {
    for (int k = 0; k < tickerData[i].lotCount; k++) {
      struct tm tmk; time_t ts = tickerData[i].lots[k].ts; localtime_r(&ts, &tmk);
      char buf[11]; strftime(buf, sizeof(buf), "%Y-%m-%d", &tmk);
      double total = (double)tickerData[i].lots[k].qty * tickerData[i].lots[k].pricePLN;

      String chunk = first ? "  {" : ",\n  {";
      first = false;
      chunk += "\"symbol\": \"" + tickerData[i].sym + "\""
        + ", \"date\": \"" + String(buf) + "\""
        + ", \"qty\": " + String(tickerData[i].lots[k].qty, 6)
        + ", \"pricePLN\": " + String(tickerData[i].lots[k].pricePLN, 2)
        + ", \"totalPLN\": " + String(total, 2) + "}";
      server.sendContent(chunk);
    }
  }
  xSemaphoreGive(dataMutex);
  server.sendContent("\n]\n");
  server.sendContent("");
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

  loadPrefs(); applyBrightness(cfg.brightness);
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

  uint32_t syncStart = millis();
  while (time(nullptr) < NTP_SYNC_MIN_EPOCH && millis() - syncStart < 8000) delay(200);

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
  server.on("/editlot", HTTP_POST, handleEditLot);
  server.on("/refresh", HTTP_GET, handleForceRefresh);
  server.on("/api/quotes", HTTP_GET, handleApiQuotes);
  server.on("/api/transactions", HTTP_GET, handleApiTransactions);
  server.begin();
  MDNS.begin("portfolio-tracker");

  setLED(false, false, false);
  xTaskCreatePinnedToCore(fetchTask, "fetch", 8192, NULL, 1, NULL, 0);
  lastTouchAction = millis();
}

void loop() {
  server.handleClient();
  handleTouch();

  if (!fetching && (millis() - lastFetchMillis) >= (unsigned long)cfg.refreshSec * 1000UL)
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
      xSemaphoreTake(dataMutex, portMAX_DELAY); drawHeader(true); xSemaphoreGive(dataMutex);
    }
    if (lastFetch && !fetching) drawAll();
    lastFetch = fetching;
  }

  // Night mode auto-brightness
  {
    static int lastNightBright = -1;
    int target = cfg.brightness;
    if (cfg.nightModeEnabled) {
      struct tm tmn;
      if (getLocalTime(&tmn)) {
        int h = tmn.tm_hour;
        bool inNight = (cfg.nightFrom < cfg.nightTo)
          ? (h >= cfg.nightFrom && h < cfg.nightTo)
          : (h >= cfg.nightFrom || h < cfg.nightTo);
        if (inNight) target = 25;
      }
    }
    if (target != lastNightBright) { applyBrightness(target); lastNightBright = target; }
  }
}
