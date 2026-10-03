#include <WiFi.h>
#include <ESPmDNS.h>
#include <WiFiManager.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <WebServer.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>
#include <SPI.h>
#include <time.h>
#include <algorithm>
#include <atomic>
#include <memory>
#include <vector>
#include "investment_math.h"
#include "persistent_store.h"
#include "http_body_reader.h"
#include "firmware_types.h"

void convertToJson(const Money& money, JsonVariant json) { json.set((double)money); }
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include "web_ui.h"
#include "portfolio_ui.h"

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
static constexpr int MAX_LOTS_SHOWN = 60; // newest transactions across all tickers in the web UI
static constexpr int MAX_MANUAL_ASSETS = 4;       // PPK, savings accounts etc. - no market quote
static constexpr int MAX_MANUAL_SNAPSHOTS = 60;   // user-entered valuations per manual asset
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
  time_t asOf;
};

struct ExRate {
  String curr;
  float rate;
  time_t asOf;
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

// A manual asset is deliberately not a ticker. Its value and profit are only
// changed by the owner, so the chart never invents a Yahoo-like price movement.
struct ManualAsset {
  String name;
  bool ppk;
  Money valuePLN;
  Money gainPLN;
  ManualSnapshot history[MAX_MANUAL_SNAPSHOTS];
  int historyCount;
};

String manualVersion(const ManualAsset& asset) {
  time_t last = asset.historyCount ? asset.history[asset.historyCount - 1].ts : 0;
  return asset.name + ":" + String(asset.ppk ? 1 : 0) + ":" + String(asset.historyCount)
    + ":" + String((long long)last) + ":" + String(asset.valuePLN, 2) + ":" + String(asset.gainPLN, 2);
}

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

ManualAsset manualAssets[MAX_MANUAL_ASSETS];
int manualCount = 0;
uint32_t nextLotId = 1;
struct MutationReceipt { String id; uint32_t hash = 0; };
MutationReceipt receipts[32];
int receiptCount = 0;
PersistentStore persistentStore;
std::atomic<bool> redrawPending{true};

AppConfig cfg = {DEFAULT_REFRESH, 200, true, false, false, 0, 8, "1d"};

ExRate exchangeRates[MAX_EXCHANGE_RATES];
int rateCount = 0;

// --- VIEW STATE ---
enum ViewMode { VIEW_GRID, VIEW_DETAIL };
ViewMode viewMode = VIEW_GRID;
int detailIdx = 0;

// --- THREADING & TASK STATE ---
SemaphoreHandle_t dataMutex;
SemaphoreHandle_t prefsMutex;
SemaphoreHandle_t networkMutex;
SemaphoreHandle_t historyMutex;
std::atomic<bool> fetchPending{true};
std::atomic<bool> fetching{false};
std::atomic<unsigned long> lastFetchMillis{0};
std::atomic<time_t> lastFetchTime{0};
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

// --- HARDWARE & UTILS ---
void setLED(bool r, bool g, bool b) {
  digitalWrite(LED_R, !r);
  digitalWrite(LED_G, !g);
  digitalWrite(LED_B, !b);
}

void applyBrightness(int val) {
  // ESP32 Arduino Core 3.x requires the target GPIO as well as frequency.
  analogWriteFrequency(BL_PIN, 5000);
  analogWrite(BL_PIN, val);
}

String getCurrencySymbol(const String &curr) {
  if (curr == "EUR") return "EUR ";
  if (curr == "USD") return "$";
  if (curr == "GBP") return "\xc2\xa3";
  if (curr == "GBp") return "GBp ";
  if (curr == "PLN") return "PLN ";
  return curr.length() ? curr + " " : "$";
}

String formatPrice(float price, const String &currency) {
  char buf[64];
  int decimals = price >= 10000 ? 0 : price >= 1000 ? 1 : price >= 10 ? 2 : price >= 0.01f ? 4 : 6;
  snprintf(buf, sizeof(buf), "%.*f", decimals, price);
  return currency + String(buf);
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

time_t parseDateYMD(const String& date) {
  return parseCalendarDate(date.c_str());
}

String normalizeCurrency(String currency) {
  // Yahoo uses GBp for pence and GBP for pounds: case is significant.
  if (currency != "GBp") currency.toUpperCase();
  return currency;
}

bool validTickerSymbol(const String& symbol) {
  if (symbol.isEmpty() || symbol.length() > 32) return false;
  for (size_t i = 0; i < symbol.length(); ++i) {
    char c = symbol[i];
    if (!isalnum((unsigned char)c) && c != '-' && c != '.' && c != '='
        && c != '^' && c != '_' && c != ':') return false;
  }
  return true;
}

String scriptSafeJson(const JsonDocument& doc) {
  String json;
  serializeJson(doc, json);
  json.replace("<", "\\u003c"); json.replace(">", "\\u003e"); json.replace("&", "\\u0026");
  return json;
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

String htmlEscape(const String &s) {
  String out;
  for (size_t i = 0; i < s.length(); i++) {
    if (s[i] == '&') out += "&amp;";
    else if (s[i] == '<') out += "&lt;";
    else if (s[i] == '>') out += "&gt;";
    else if (s[i] == '\"') out += "&quot;";
    else if (s[i] == '\'') out += "&#39;";
    else out += s[i];
  }
  return out;
}

// Find ticker index by symbol. Caller must hold dataMutex.
int getIndexBySym(const String &sym) {
  for (int i = 0; i < tickerCount; i++) {
    if (tickerData[i].sym == sym) return i;
  }
  return -1;
}

// --- TRANSACTIONS (COST BASIS) ---
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
  tickerData[i].lots[pos].id = 0; // Assigned only when the prospective ledger passes validation.
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

void computeCostBasis(const TickerState& ticker, float& qty, double& cost) {
  double realized;
  calculateLedger(ticker.lots, ticker.lotCount, std::numeric_limits<time_t>::max(), qty, cost, realized);
}

// Recompute t.holdings from its lots. Caller must hold dataMutex.
void refreshHoldings(TickerState &t) {
  float qty; double cost;
  computeCostBasis(t, qty, cost);
  t.holdings = qty;
}

bool computePLFor(const TickerState &ts, double &valueOut, double &costOut, double &plOut, float rateOverride = -1) {
  if (!quoteUsable(ts.quote)) return false;
  float qty; double cost;
  computeCostBasis(ts, qty, cost);
  if (qty <= 0) return false;
  float rate = rateOverride >= 0 ? rateOverride : getRateToPLN(ts.quote.currency);
  if (rate <= 0) return false;
  double pricePLN = (double)ts.quote.price * rate;
  valueOut = pricePLN * qty;
  costOut = cost;
  plOut = valueOut - cost;
  return true;
}

bool computePeriodPLFor(const TickerState &ts, double &valueOut, double &periodPLOut, float rateOverride = -1) {
  if (!quoteUsable(ts.quote) || ts.holdings <= 0) return false;
  float rate = rateOverride >= 0 ? rateOverride : getRateToPLN(ts.quote.currency);
  if (rate <= 0) return false;
  double pricePLN = (double)ts.quote.price * rate;
  valueOut = pricePLN * ts.holdings;
  double pctFrac = (double)ts.quote.pct / 100.0;
  periodPLOut = (pctFrac > -1.0) ? valueOut * pctFrac / (1.0 + pctFrac) : 0.0;
  return true;
}

bool computePeriodPL(int i, double &v, double &p) {
  if (i < 0 || i >= MAX_TICKERS) return false;
  return computePeriodPLFor(tickerData[i], v, p);
}

double manualValuePLN() {
  double total = 0;
  for (int i = 0; i < manualCount; i++) total += manualAssets[i].valuePLN;
  return total;
}

double manualGainPLN() {
  double total = 0;
  for (int i = 0; i < manualCount; i++) total += manualAssets[i].gainPLN;
  return total;
}

// --- PREFERENCES ---
bool loadPersistentState() {
  JsonDocument doc;
  if (!persistentStore.load(doc)) return false;
  std::unique_ptr<TickerState[]> tickers(new TickerState[MAX_TICKERS]{});
  std::unique_ptr<ManualAsset[]> assets(new ManualAsset[MAX_MANUAL_ASSETS]{});
  JsonArray list = doc["tickers"], accounts = doc["accounts"];
  bool valid = doc["schema"] == 2 && list.size() > 0 && list.size() <= MAX_TICKERS
    && accounts.size() <= MAX_MANUAL_ASSETS && doc["nextLotId"].is<uint32_t>() && doc["nextLotId"].as<uint32_t>() > 0;
  int count = 0, manual = 0;
  for (JsonObject row : list) {
    if (!valid) break;
    auto& t = tickers[count++]; t.sym = row["symbol"].as<String>();
    t.alertHigh = row["high"] | 0.0f; t.alertLow = row["low"] | 0.0f;
    valid = validTickerSymbol(t.sym) && row["lots"].size() <= MAX_LOTS;
    for (int i = 0; i < count - 1; ++i) if (tickers[i].sym == t.sym) valid = false;
    JsonArray lots = row["lots"];
    for (JsonArray lot : lots) {
      if (!valid) break;
      auto& l = t.lots[t.lotCount++]; l = {lot[0].as<time_t>(), lot[1].as<float>(), lot[2].as<float>(), lot[3].as<uint32_t>()};
      if (!l.id || l.id >= doc["nextLotId"].as<uint32_t>()) valid = false;
      for (int i = 0; i < count; ++i) for (int k = 0; k < tickers[i].lotCount; ++k)
        if (&tickers[i].lots[k] != &l && tickers[i].lots[k].id == l.id) valid = false;
    }
    valid = valid && validLotSequence(t.lots, t.lotCount); refreshHoldings(t);
    t.quote.sym = t.sym;
  }
  for (JsonObject row : accounts) {
    if (!valid) break;
    auto& a = assets[manual++]; a.name = row["name"].as<String>(); a.ppk = row["ppk"] | false;
    valid = !a.name.isEmpty() && a.name.length() <= 60 && row["history"].size() <= MAX_MANUAL_SNAPSHOTS;
    for (int i = 0; i < manual - 1; ++i) if (assets[i].name == a.name) valid = false;
    JsonArray history = row["history"];
    for (JsonArray h : history) {
      time_t ts = h[0].as<time_t>();
      int64_t value = h[1].as<int64_t>(), gain = h[2].as<int64_t>();
      if (!h[1].is<int64_t>() || !h[2].is<int64_t>() || value < 0 || value > Money::MAX_CENTS
          || gain < -Money::MAX_CENTS || gain > Money::MAX_CENTS || !recordManualValuation(a, ts, Money::fromCents(value), Money::fromCents(gain))) valid = false;
    }
    if (!row["balanceCents"].is<int64_t>() || !row["gainCents"].is<int64_t>()) valid = false;
    Money balance = Money::fromCents(row["balanceCents"].as<int64_t>()), gain = Money::fromCents(row["gainCents"].as<int64_t>());
    if (!std::isfinite((double)balance) || !std::isfinite((double)gain) || balance < 0
        || (a.historyCount && (a.valuePLN.cents != balance.cents || a.gainPLN.cents != gain.cents))) valid = false;
    a.valuePLN = balance; a.gainPLN = gain;
  }
  JsonObject settings = doc["config"];
  String range = settings["range"].as<String>();
  valid = valid && isValidRange(range) && settings["refresh"].as<int>() >= MIN_REFRESH && settings["refresh"].as<int>() <= 3600
    && settings["bright"].as<int>() >= 10 && settings["bright"].as<int>() <= 255
    && settings["from"].as<int>() >= 0 && settings["from"].as<int>() <= 23
    && settings["to"].as<int>() >= 0 && settings["to"].as<int>() <= 23;
  if (!valid) { persistentStore.disable(); Serial.println("Invalid saved state: refusing further writes; legacy backup retained."); return false; }
  cfg = {settings["refresh"], settings["bright"], settings["dark"], settings["portfolio"], settings["night"], settings["from"], settings["to"], range};
  tickerCount = count; manualCount = manual; nextLotId = doc["nextLotId"];
  for (int i = 0; i < count; ++i) tickerData[i] = tickers[i];
  for (int i = 0; i < manual; ++i) manualAssets[i] = assets[i];
  receiptCount = 0;
  JsonArray savedReceipts = doc["receipts"];
  for (JsonObject r : savedReceipts) {
    if (receiptCount == 32) break;
    receipts[receiptCount++] = {r["id"].as<String>(), r["hash"].as<uint32_t>()};
  }
  return true;
}

void loadPrefs() {
  if (loadPersistentState()) return;
  prefs.begin("ticker", true);
  cfg.refreshSec = constrain(prefs.getInt("refresh", DEFAULT_REFRESH), MIN_REFRESH, 3600);
  cfg.brightness = constrain(prefs.getInt("bright", 200), 10, 255);
  cfg.darkMode = prefs.getBool("dark", true);
  cfg.portfolioMode = prefs.getBool("portfolio", false);
  cfg.nightModeEnabled = prefs.getBool("nighten", false);
  cfg.nightFrom = constrain(prefs.getInt("nightfr", 0), 0, 23);
  cfg.nightTo = constrain(prefs.getInt("nightto", 8), 0, 23);
  cfg.chartRange = prefs.getString("range", "1d");
  tickerCount = prefs.getInt("tcount", 0);
  manualCount = prefs.getInt("mcount", 0);
  if (!isValidRange(cfg.chartRange)) cfg.chartRange = "1d";
  if (tickerCount < 0 || tickerCount > MAX_TICKERS) tickerCount = 0;
  if (manualCount < 0 || manualCount > MAX_MANUAL_ASSETS) manualCount = 0;

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
          tickerData[i].lots[tickerData[i].lotCount].id = nextLotId++;
          tickerData[i].lotCount++;
        }
        start = p + 1;
      }
    }

    tickerData[i].legacyHint = (!hasLots) ? prefs.getFloat(("h" + String(i)).c_str(), 0.0f) : 0.0f;

    refreshHoldings(tickerData[i]);
  }

  for (int i = 0; i < manualCount; i++) {
    manualAssets[i].name = prefs.getString(("mn" + String(i)).c_str(), "Manual asset");
    String upperName = manualAssets[i].name; upperName.toUpperCase();
    manualAssets[i].ppk = prefs.getBool(("mp" + String(i)).c_str(), upperName.indexOf("PPK") >= 0);
    manualAssets[i].valuePLN = prefs.getFloat(("mv" + String(i)).c_str(), 0.0f);
    manualAssets[i].gainPLN = prefs.getFloat(("mg" + String(i)).c_str(), 0.0f);
    manualAssets[i].historyCount = 0;
    String hs = prefs.getString(("mh" + String(i)).c_str(), "");
    int start = 0;
    for (int p = 0; p <= (int)hs.length() && manualAssets[i].historyCount < MAX_MANUAL_SNAPSHOTS; p++) {
      if (p == (int)hs.length() || hs[p] == ',') {
        String triple = hs.substring(start, p);
        int c1 = triple.indexOf(':');
        int c2 = (c1 > 0) ? triple.indexOf(':', c1 + 1) : -1;
        if (c1 > 0 && c2 > c1) {
          int n = manualAssets[i].historyCount++;
          manualAssets[i].history[n].ts = (time_t)triple.substring(0, c1).toInt();
          if (!parseMoney(triple.substring(c1 + 1, c2).c_str(), manualAssets[i].history[n].valuePLN)
              || !parseMoney(triple.substring(c2 + 1).c_str(), manualAssets[i].history[n].gainPLN)) persistentStore.disable();
        }
        start = p + 1;
      }
    }
    if (manualAssets[i].historyCount) {
      const auto& last = manualAssets[i].history[manualAssets[i].historyCount - 1];
      manualAssets[i].valuePLN = last.valuePLN; manualAssets[i].gainPLN = last.gainPLN;
    }
  }

  // The obsolete aggregate snapshot key 'ph' is deliberately left untouched.
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

bool savePrefs() {
  // Only HTTP mutations write financial state. Serialize under dataMutex so
  // quote sorting cannot reorder the ledger, without allocating a second copy
  // of all holdings/history alongside the mutation rollback snapshot.
  xSemaphoreTake(prefsMutex, portMAX_DELAY);
  xSemaphoreTake(dataMutex, portMAX_DELAY);
  JsonDocument doc;
  doc["schema"] = 2; doc["nextLotId"] = nextLotId;
  JsonObject settings = doc["config"].to<JsonObject>();
  settings["refresh"] = cfg.refreshSec; settings["bright"] = cfg.brightness;
  settings["dark"] = cfg.darkMode; settings["portfolio"] = cfg.portfolioMode;
  settings["night"] = cfg.nightModeEnabled; settings["from"] = cfg.nightFrom;
  settings["to"] = cfg.nightTo; settings["range"] = cfg.chartRange;

  for (int i = 0; i < tickerCount; i++) {
    JsonObject ticker = doc["tickers"].add<JsonObject>();
    ticker["symbol"] = tickerData[i].sym; ticker["high"] = tickerData[i].alertHigh; ticker["low"] = tickerData[i].alertLow;
    for (int k = 0; k < tickerData[i].lotCount; k++) {
      const Lot& l = tickerData[i].lots[k]; char qty[32], price[32]; formatQuantity(l.qty, qty); formatQuantity(l.pricePLN, price);
      JsonArray lot = ticker["lots"].add<JsonArray>();
      lot.add(l.ts); lot.add(serialized(String(qty))); lot.add(serialized(String(price))); lot.add(l.id);
    }
  }

  for (int i = 0; i < manualCount; i++) {
    JsonObject account = doc["accounts"].add<JsonObject>();
    account["name"] = manualAssets[i].name; account["ppk"] = manualAssets[i].ppk;
    account["balanceCents"] = manualAssets[i].valuePLN.cents; account["gainCents"] = manualAssets[i].gainPLN.cents;
    for (int k = 0; k < manualAssets[i].historyCount; k++) {
      const ManualSnapshot& h = manualAssets[i].history[k];
      JsonArray row = account["history"].add<JsonArray>(); row.add(h.ts); row.add(h.valuePLN.cents); row.add(h.gainPLN.cents);
    }
  }
  for (int i = 0; i < receiptCount; ++i) {
    JsonObject r = doc["receipts"].add<JsonObject>(); r["id"] = receipts[i].id; r["hash"] = receipts[i].hash;
  }
  xSemaphoreGive(dataMutex);
  bool saved = !doc.overflowed() && persistentStore.save(doc);
  xSemaphoreGive(prefsMutex);
  return saved;
}

// Every mutation restores its pre-request financial state if persistence fails.
struct MutationGuard {
  std::unique_ptr<TickerState[]> tickers{new TickerState[MAX_TICKERS]{}};
  std::unique_ptr<ManualAsset[]> accounts{new ManualAsset[MAX_MANUAL_ASSETS]{}};
  AppConfig config; int count, manual, rc; uint32_t next;
  MutationReceipt oldReceipts[32];
  MutationGuard() {
    xSemaphoreTake(dataMutex, portMAX_DELAY);
    count = tickerCount; manual = manualCount; config = cfg; next = nextLotId; rc = receiptCount;
    for (int i = 0; i < count; ++i) tickers[i] = tickerData[i];
    for (int i = 0; i < manual; ++i) accounts[i] = manualAssets[i];
    for (int i = 0; i < rc; ++i) oldReceipts[i] = receipts[i];
    xSemaphoreGive(dataMutex);
  }
  bool commit(bool& ok, String& message) {
    if (!ok) return false;
    if (savePrefs()) { redrawPending = true; return true; }
    xSemaphoreTake(dataMutex, portMAX_DELAY);
    // Keep any newer network quotes for retained symbols while rolling back money.
    for (int i = 0; i < count; ++i) { int current = getIndexBySym(tickers[i].sym); if (current >= 0) tickers[i].quote = tickerData[current].quote; }
    for (int i = 0; i < count; ++i) tickerData[i] = tickers[i];
    for (int i = 0; i < manual; ++i) manualAssets[i] = accounts[i];
    tickerCount = count; manualCount = manual; cfg = config; nextLotId = next; receiptCount = rc;
    for (int i = 0; i < rc; ++i) receipts[i] = oldReceipts[i];
    xSemaphoreGive(dataMutex);
    ok = false; message = "Could not persist this operation. No financial changes were applied. Download backups and check filesystem space/configuration.";
    return false;
  }
};

// --- MARKET & NETWORK ---
static constexpr time_t MAX_QUOTE_AGE = 7 * 86400;
bool quoteUsable(const Quote& quote) { return quote.valid && freshTimestamp(quote.asOf, time(nullptr), MAX_QUOTE_AGE); }

struct NetworkLock {
  explicit NetworkLock(bool liveQuote = false) {
    for (;;) {
      if (!liveQuote && (fetching.load() || fetchPending.load())) { vTaskDelay(pdMS_TO_TICKS(50)); continue; }
      if (!xSemaphoreTake(networkMutex, pdMS_TO_TICKS(50))) continue;
      // A price refresh may have arrived while history waited for the socket.
      if (!liveQuote && (fetching.load() || fetchPending.load())) { xSemaphoreGive(networkMutex); continue; }
      break;
    }
  }
  ~NetworkLock() { xSemaphoreGive(networkMutex); }
};
void prepareYahooRequest(HTTPClient& http, WiFiClientSecure& client, const String& url, int timeout) {
  // Preserve the existing Yahoo TLS policy (begin(url) also used insecure TLS),
  // but bound the handshake instead of the core's 120-second default.
  client.setInsecure(); client.setHandshakeTimeout(10);
  http.begin(client, url); http.setTimeout(timeout); http.setConnectTimeout(5000);
  http.setUserAgent("Mozilla/5.0 (compatible; PortfolioTracker/1.0)");
  const char* headers[] = {"Transfer-Encoding"}; http.collectHeaders(headers, 1);
}
struct HttpSource {
  NetworkClient& stream;
  uint8_t buffer[512];
  size_t cursor = 0, count = 0;
  uint32_t began, budget;
  explicit HttpSource(NetworkClient& source, uint32_t totalTimeout = 20000) : stream(source), began(millis()), budget(totalTimeout) {}
  int read() {
    if ((uint32_t)(millis() - began) >= budget) return -1;
    if (cursor < count) return buffer[cursor++];
    const uint32_t started = millis();
    do {
      int available = stream.available();
      if (available > 0) {
        int received = stream.read(buffer, min((size_t)available, sizeof(buffer)));
        if (received > 0) {
          cursor = 1; count = (size_t)received;
          return buffer[0];
        }
      }
      // ESP32 TLS read/readBytes may return -1/0 between packets, even with a
      // connected socket. Wait for data instead of reporting a false JSON EOF.
      // Consume buffered bytes before testing whether the peer has closed.
      if (!stream.connected()) return -1;
      delay(2);
    } while ((uint32_t)(millis() - started) < stream.getTimeout() && (uint32_t)(millis() - began) < budget);
    return -1;
  }
};
DeserializationError readYahooJson(HTTPClient& http, JsonDocument& doc, const JsonDocument& filter) {
  HttpSource source{http.getStream()};
  HttpBodyReader<HttpSource> body(source, http.header("Transfer-Encoding").equalsIgnoreCase("chunked"));
  return deserializeJson(doc, body, DeserializationOption::Filter(filter));
}

void fetchYahoo(int idx, const String& sym, const String& chartRange) {
  NetworkLock network(true);
  String interval = intervalFor(chartRange);
  WiFiClientSecure client;
  HTTPClient http;
  prepareYahooRequest(http, client, "https://query1.finance.yahoo.com/v8/finance/chart/"
    + urlEncode(sym) + "?interval=" + interval + "&range=" + chartRange, 8000);
  // Keep the previously working HTTP/1.1 body handling (including chunked responses).
  Quote q{};
  q.sym = sym;
  q.currency = "USD";
  q.valid = false;
  q.sparkCount = 0;
  q.errors = 0;

  int httpStatus = http.GET();
  if (httpStatus == 200) {
    JsonDocument filter;
    filter["chart"]["result"][0]["meta"]["regularMarketPrice"] = true;
    filter["chart"]["result"][0]["meta"]["regularMarketTime"] = true;
    filter["chart"]["result"][0]["meta"]["currency"] = true;
    filter["chart"]["result"][0]["meta"]["chartPreviousClose"] = true;
    filter["chart"]["result"][0]["meta"]["previousClose"] = true;
    filter["chart"]["result"][0]["indicators"]["quote"][0]["close"] = true;
    JsonDocument doc;
    DeserializationError error = readYahooJson(http, doc, filter);
    if (!error) {
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
          int samples = min(total, MAX_SPARK_POINTS), count = 0;
          for (JsonVariant v : closeArr) {
            if (!v.isNull() && v.as<float>() > 0) {
              int target = samples > 1 ? q.sparkCount * (total - 1) / (samples - 1) : 0;
              if (q.sparkCount < samples && count == target) q.sparkline[q.sparkCount++] = v.as<float>();
              count++;
            }
          }
        }

        q.asOf = meta["regularMarketTime"] | (time_t)0;
        if (isfinite(price) && price > 0 && freshTimestamp(q.asOf, time(nullptr), MAX_QUOTE_AGE)) {
          float prev = 0.0f;
          if (chartRange == "1d") {
            prev = meta["chartPreviousClose"] | (meta["previousClose"] | 0.0f);
          } else {
            prev = firstClose;
          }
          q.price = price;
          q.pct = (prev > 0) ? ((price - prev) / prev * 100.0f) : 0.0f;
          q.currency = normalizeCurrency(String((const char*)(meta["currency"] | "USD")));
          q.valid = true;
          q.errors = 0;
        } else q.errors++;
      } else q.errors++;
    } else {
      Serial.printf("Quote %s: JSON %s\n", sym.c_str(), error.c_str());
      q.errors++;
    }
  } else {
    Serial.printf("Quote %s: HTTP %d\n", sym.c_str(), httpStatus);
    q.errors++;
  }
  http.end();

  xSemaphoreTake(dataMutex, portMAX_DELAY);
  if (cfg.chartRange != chartRange) {
      // Settings changed while this request was in flight; the pending refresh
      // will supply data for the new period instead of publishing stale data.
  } else if (idx < tickerCount && tickerData[idx].sym == sym) {
      if (q.valid || !tickerData[idx].quote.valid) tickerData[idx].quote = q;
      else tickerData[idx].quote.errors++;
  } else {
      int real_idx = getIndexBySym(sym);
      if (real_idx >= 0) {
        if (q.valid || !tickerData[real_idx].quote.valid) tickerData[real_idx].quote = q;
        else tickerData[real_idx].quote.errors++;
      }
  }
  xSemaphoreGive(dataMutex);
  redrawPending = true;
}

float fetchYahooRate(String curr, time_t& asOf) {
  if (curr == "PLN") return 1.0f;
  if (curr.isEmpty()) return 0.0f;
  String qCurr = (curr == "GBp") ? "GBP" : curr;
  float mult = (curr == "GBp") ? 0.01f : 1.0f;
  NetworkLock network(true);
  WiFiClientSecure client;
  HTTPClient http;
  prepareYahooRequest(http, client, "https://query1.finance.yahoo.com/v8/finance/chart/"
    + qCurr + "PLN=X?interval=1d&range=1d", 5000);
  float rate = 0.0f;
  if (http.GET() == 200) {
    JsonDocument doc;
    JsonDocument filter; filter["chart"]["result"][0]["meta"]["regularMarketPrice"] = true;
    filter["chart"]["result"][0]["meta"]["regularMarketTime"] = true;
    if (!readYahooJson(http, doc, filter)) {
      rate = doc["chart"]["result"][0]["meta"]["regularMarketPrice"] | 0.0f;
      asOf = doc["chart"]["result"][0]["meta"]["regularMarketTime"] | (time_t)0;
    }
  }
  http.end();
  return isfinite(rate) && rate > 0 && freshTimestamp(asOf, time(nullptr), MAX_QUOTE_AGE) ? rate * mult : 0.0f;
}

float getRateToPLN(const String &curr) {
  if (curr == "PLN") return 1.0f;
  if (curr.isEmpty()) return 0.0f;
  for (int i = 0; i < rateCount; i++) if (exchangeRates[i].curr == curr && freshTimestamp(exchangeRates[i].asOf, time(nullptr), MAX_QUOTE_AGE)) return exchangeRates[i].rate;
  return 0.0f;
}

time_t getRateAsOf(const String& curr) {
  if (curr == "PLN") return 0;
  for (int i = 0; i < rateCount; ++i) if (exchangeRates[i].curr == curr) return exchangeRates[i].asOf;
  return 0;
}

bool fetchHistoricalClose(const String &sym, time_t period1, time_t period2,
                           time_t target, float &closeOut, String &currencyOut) {
  NetworkLock network;
  WiFiClientSecure client;
  HTTPClient http;
  prepareYahooRequest(http, client, "https://query1.finance.yahoo.com/v8/finance/chart/" + urlEncode(sym)
    + "?period1=" + String((long long)period1) + "&period2=" + String((long long)period2)
    + "&interval=1d", 8000);

  bool ok = false;
  if (http.GET() == 200) {
    JsonDocument doc;
    JsonDocument filter; filter["chart"]["result"][0]["timestamp"] = true;
    filter["chart"]["result"][0]["meta"]["currency"] = true;
    filter["chart"]["result"][0]["indicators"]["quote"][0]["close"] = true;
    if (!readYahooJson(http, doc, filter)) {
      JsonObject result = doc["chart"]["result"][0];
      JsonArray ts = result["timestamp"];
      JsonArray closes = result["indicators"]["quote"][0]["close"];
      currencyOut = normalizeCurrency(String((const char*)(result["meta"]["currency"] | "")));

      bool haveOnOrBefore = false;
      float bestClose = 0;
      time_t bestTs = 0;

      size_t n = min(ts.size(), closes.size());
      for (size_t k = 0; k < n; k++) {
        if (closes[k].isNull()) continue;
        float ck = closes[k].as<float>();
        if (!isfinite(ck) || ck <= 0) continue;
        time_t tk = ts[k].as<time_t>();
        if (tk <= target) {
          if (!haveOnOrBefore || tk > bestTs) { bestTs = tk; bestClose = ck; haveOnOrBefore = true; }
        }
      }
      if (haveOnOrBefore && target - bestTs <= 7 * 86400 && currencyOut.length()) { closeOut = bestClose; ok = true; }
    }
  }
  http.end();
  return ok;
}

bool fetchHistoricalPricePLN(const String &sym, time_t target, float &pricePLNOut) {
  target = historyDayEnd(target);
  time_t period1 = target - (7 * 86400);
  time_t period2 = target + 86400;

  float closeNative; String currency;
  if (!fetchHistoricalClose(sym, period1, period2, target, closeNative, currency)) return false;

  if (currency == "PLN") { pricePLNOut = closeNative; return true; }

  String qCurr = (currency == "GBp") ? "GBP" : currency;
  float mult = (currency == "GBp") ? 0.01f : 1.0f;

  float fxClose; String fxCurrencyOut;
  if (!fetchHistoricalClose(qCurr + "PLN=X", period1, period2, target, fxClose, fxCurrencyOut)) return false;

  pricePLNOut = closeNative * fxClose * mult;
  return isfinite(pricePLNOut) && pricePLNOut > 0;
}

// Portfolio charts share one timeline so summing positions never depends on
// browser interpolation. Include exact transaction/valuation days as well as
// evenly spaced samples. Data stays on the heap, outside ESP32 task stacks.
static constexpr int MAX_HISTORY_POINTS = 1024;

int makeHistoryTimeline(time_t* dates, time_t now, bool tickersOnly) {
  int count = 0;
  time_t first = 0;
  xSemaphoreTake(dataMutex, portMAX_DELAY);
  for (int i = 0; i < tickerCount; i++) {
    for (int k = 0; k < tickerData[i].lotCount; k++) {
      time_t ts = tickerData[i].lots[k].ts;
      if (ts > 0 && (ts <= now || isSameLocalDay(ts, now)) && count < MAX_HISTORY_POINTS - 181) {
        dates[count++] = min(historyDayEnd(ts), now);
        if (!first || ts < first) first = ts;
      }
    }
  }
  for (int i = 0; !tickersOnly && i < manualCount; i++) {
    for (int k = 0; k < manualAssets[i].historyCount; k++) {
      time_t ts = manualAssets[i].history[k].ts;
      if (ts > 0 && (ts <= now || isSameLocalDay(ts, now)) && count < MAX_HISTORY_POINTS - 181) {
        dates[count++] = min(historyDayEnd(ts), now);
        if (!first || ts < first) first = ts;
      }
    }
  }
  xSemaphoreGive(dataMutex);
  if (!first) return 0;
  time_t start = min(historyDayEnd(first), now);
  for (int i = 0; i < 180; i++) {
    time_t ts = start + (time_t)((double)(now - start) * i / 179);
    dates[count++] = min(historyDayEnd(ts), now);
  }
  std::sort(dates, dates + count);
  int unique = 0;
  for (int i = 0; i < count; i++) {
    if (!unique || dates[i] != dates[unique - 1]) dates[unique++] = dates[i];
  }
  return unique;
}

struct HistoryBar { time_t available; float close; bool spot = false; };
struct FxHistoryCache {
  String symbol, currency; bool weekly = false;
  time_t from = 0, to = 0; unsigned long fetched = 0;
  std::vector<HistoryBar> bars;
};
FxHistoryCache fxHistoryCache[2];

bool sampleHistory(const std::vector<HistoryBar>& bars, const time_t* dates, int count, bool weekly, float* prices, time_t* sources = nullptr) {
  size_t cursor = 0; float last = NAN, spot = NAN; time_t stamp = 0, spotTime = 0; bool any = false;
  for (int i = 0; i < count; ++i) {
    while (cursor < bars.size() && bars[cursor].available <= dates[i]) {
      if (bars[cursor].spot) { spot = bars[cursor].close; spotTime = bars[cursor].available; }
      // Empty candles do not replace a known price or renew its timestamp.
      // Carry only within the age limit measured from the last real quote.
      if (isfinite(bars[cursor].close) && bars[cursor].close > 0) {
        last = bars[cursor].close; stamp = bars[cursor].available;
      }
      ++cursor;
    }
    prices[i] = stamp && dates[i] - stamp <= (weekly ? 14 : 7) * 86400 && isfinite(last) && last > 0 ? last : NAN;
    // A trailing null placeholder must not hide a known, fresh last-traded
    // price. Only the current endpoint gets this fallback; older gaps remain.
    if (i == count - 1 && !isfinite(prices[i]) && isfinite(spot) && spot > 0
        && dates[i] - spotTime <= 7 * 86400 && freshTimestamp(spotTime, time(nullptr), 7 * 86400)) { prices[i] = spot; stamp = spotTime; }
    if (sources) sources[i] = isfinite(prices[i]) ? stamp : 0;
    any = any || isfinite(prices[i]);
  }
  return any;
}

// Filter the response to limit JSON memory. Each sample uses the most recent
// close on or before its date. Weekly bars are dated at their opening, so
// conservatively expose their close only after that week has ended.
bool fetchHistoryCloses(const String& sym, const time_t* dates, int count,
                        float* prices, String& currency, bool& weekly, String& failure, time_t* sources = nullptr) {
  failure = "";
  for (int i = 0; i < count; i++) { prices[i] = NAN; if (sources) sources[i] = 0; }
  if (!count) { failure = "No dated transactions, or device clock is not synchronized."; return false; }
  weekly = dates[count - 1] - dates[0] > 730L * 86400L;
  time_t from = dates[0] - 14 * 86400, to = historyDayEnd(dates[count-1]) + 1;
  bool fx = sym.endsWith("PLN=X");
  if (fx) for (auto& cached : fxHistoryCache) {
    if (cached.symbol == sym && cached.weekly == weekly && from >= cached.from && to <= cached.to
        && millis() - cached.fetched < 300000 && sampleHistory(cached.bars, dates, count, weekly, prices, sources)) {
      currency = cached.currency; return true;
    }
  }
  NetworkLock network;
  WiFiClientSecure client;
  HTTPClient http;
  prepareYahooRequest(http, client, "https://query1.finance.yahoo.com/v8/finance/chart/" + urlEncode(sym)
    + "?period1=" + String((long long)from)
    + "&period2=" + String((long long)to)
    + "&interval=" + String(weekly ? "1wk" : "1d"), 10000);
  bool ok = false;
  int status = http.GET();
  if (status == 200) {
    JsonDocument filter;
    filter["chart"]["result"][0]["timestamp"] = true;
    filter["chart"]["result"][0]["meta"]["currency"] = true;
    filter["chart"]["result"][0]["meta"]["regularMarketPrice"] = true;
    filter["chart"]["result"][0]["meta"]["regularMarketTime"] = true;
    filter["chart"]["result"][0]["indicators"]["quote"][0]["close"] = true;
    JsonDocument doc;
    // The buffered TLS source waits for delayed packets; HttpBodyReader removes
    // chunk framing before the filtered JSON parser sees the body.
    DeserializationError error = readYahooJson(http, doc, filter);
    if (!error) {
      JsonObject result = doc["chart"]["result"][0];
      JsonArray timestamps = result["timestamp"];
      JsonArray closes = result["indicators"]["quote"][0]["close"];
      currency = normalizeCurrency(String((const char*)(result["meta"]["currency"] | "")));
      size_t n = min(timestamps.size(), closes.size()); std::vector<HistoryBar> bars;
      if (n <= 2048) {
        bars.reserve(n + 1);
        for (size_t i = 0; i < n; ++i) bars.push_back({timestamps[i].as<time_t>() + (weekly ? 7 * 86400 : 0), closes[i].isNull() ? NAN : closes[i].as<float>()});
        // Yahoo sometimes leaves the latest daily close null (e.g. after the
        // session/weekend) while meta contains the last traded price. ETF/FX
        // benchmarks are fetched independently of holdings, so use that quote
        // at its real timestamp, never before it and never after it is stale.
        float spot = result["meta"]["regularMarketPrice"] | 0.0f;
        time_t spotTime = result["meta"]["regularMarketTime"] | (time_t)0;
        if (isfinite(spot) && spot > 0 && spotTime <= dates[count - 1]
            && freshTimestamp(spotTime, time(nullptr), 7 * 86400)) {
          bars.push_back({spotTime, spot, true});
          // A not-yet-completed weekly bar can follow the current quote.
          std::stable_sort(bars.begin(), bars.end(), [](const HistoryBar& a, const HistoryBar& b) { return a.available < b.available; });
        }
        ok = sampleHistory(bars, dates, count, weekly, prices, sources);
        if (ok && fx && !currency.isEmpty()) {
          auto& cached = fxHistoryCache[fxHistoryCache[0].symbol == sym ? 0 : fxHistoryCache[1].symbol == sym ? 1 : fxHistoryCache[0].fetched <= fxHistoryCache[1].fetched ? 0 : 1];
          cached.symbol = sym; cached.currency = currency; cached.weekly = weekly; cached.from = from; cached.to = to; cached.fetched = millis(); cached.bars = std::move(bars);
        }
      }
      if (!ok) failure = sym + ": Yahoo returned no usable historical closes.";
      else if (currency.isEmpty()) { ok = false; failure = sym + ": Yahoo returned no currency."; }
    } else failure = sym + ": JSON " + error.c_str();
  } else failure = sym + ": HTTP " + String(status);
  if (!ok) Serial.printf("History: %s\n", failure.c_str());
  http.end();
  return ok;
}

void tickerProfitAt(const TickerState& ticker, time_t at,
                    float& qty, double& cost, double& realized) {
  calculateLedger(ticker.lots, ticker.lotCount, at, qty, cost, realized);
}

struct HistoryResponse {
  int status = 200; String body;
  void setContentLength(int) {}
  void send(int code, const char*, const String& text) { status = code; body = text; }
  void sendContent(const String& text) { body += text; }
};

void buildPortfolioPosition(const String& name, const String& kind, HistoryResponse& response) {
  bool manual = kind == "manual";
  bool benchmark = kind == "benchmark";
  TickerState* ticker = new TickerState{};
  ManualAsset* asset = new ManualAsset{};
  bool found = false;
  float currentRate = 0;
  time_t currentRateAsOf = 0;
  time_t now = time(nullptr);
  xSemaphoreTake(dataMutex, portMAX_DELAY);
  if (manual) {
    for (int i = 0; i < manualCount; i++) if (manualAssets[i].name == name) {
      *asset = manualAssets[i]; found = true; break;
    }
  } else if (benchmark) {
    found = name == "SXR8.DE" || name == "VWCE.DE";
    ticker->sym = name;
  } else {
    int i = getIndexBySym(name);
    if (i >= 0) {
      *ticker = tickerData[i]; found = true;
      currentRate = getRateToPLN(ticker->quote.currency);
      currentRateAsOf = getRateAsOf(ticker->quote.currency);
    }
  }
  xSemaphoreGive(dataMutex);
  if (!found) {
    delete ticker; delete asset;
    response.send(404, "application/json", "{\"error\":\"Position not found\"}");
    return;
  }
  time_t* dates = new time_t[MAX_HISTORY_POINTS];
  int count = now >= NTP_SYNC_MIN_EPOCH ? makeHistoryTimeline(dates, now, benchmark) : 0;
  float* prices = new float[count];
  float* rates = new float[count];
  time_t* priceSources = new time_t[count]{};
  time_t* fxSources = new time_t[count]{};
  bool weekly = false, fxWeekly = false, pricesOK = true;
  String currency, failure;
  if (!manual) {
    pricesOK = fetchHistoryCloses(ticker->sym, dates, count, prices, currency, weekly, failure, priceSources);
    if (pricesOK && currency == "PLN") {
      for (int i = 0; i < count; i++) rates[i] = 1;
    } else if (pricesOK && currency.length()) {
      String fxCurrency;
      String fxSym = (currency == "GBp" ? String("GBP") : currency) + "PLN=X";
      pricesOK = fetchHistoryCloses(fxSym, dates, count, rates, fxCurrency, fxWeekly, failure, fxSources);
      if (currency == "GBp") for (int i = 0; i < count; i++) rates[i] *= 0.01f;
    } else pricesOK = false;
  }
  response.setContentLength(CONTENT_LENGTH_UNKNOWN);
  response.send(200, "application/json", "");
  time_t start = manual ? (asset->historyCount ? asset->history[0].ts : 0)
    : (ticker->lotCount ? ticker->lots[0].ts : 0);
  response.sendContent(String("{\"ok\":") + (pricesOK ? "true" : "false")
    + ",\"weekly\":" + ((weekly || fxWeekly) ? "true" : "false")
    + ",\"start\":" + String((long long)start));
  if (!pricesOK) {
    JsonDocument errorDoc;
    errorDoc["error"] = failure;
    String errorJson;
    serializeJson(errorDoc, errorJson);
    response.sendContent("," + errorJson.substring(1, errorJson.length() - 1));
  }
  response.sendContent(",\"points\":[");
  int cursor = 0;
  const ManualSnapshot* latest = nullptr;
  for (int i = 0; i < count; i++) {
    double value = 0, gain = 0;
    bool valid = true;
    bool usesMarketPrice = false;
    if (manual) {
      while (cursor < asset->historyCount && asset->history[cursor].ts <= historyDayEnd(dates[i]))
        latest = &asset->history[cursor++];
      if (latest) { value = latest->valuePLN; gain = latest->gainPLN; }
    } else if (benchmark) {
      usesMarketPrice = true;
      valid = pricesOK && isfinite(prices[i]) && prices[i] > 0 && isfinite(rates[i]) && rates[i] > 0;
      if (valid) value = (double)prices[i] * rates[i];
    } else {
      float qty; double cost, realized;
      tickerProfitAt(*ticker, historyDayEnd(dates[i]), qty, cost, realized);
      gain = realized;
      if (qty > 0) {
        usesMarketPrice = true;
        valid = pricesOK && isfinite(prices[i]) && isfinite(rates[i]);
        if (valid) { value = (double)prices[i] * rates[i] * qty; gain += value - cost; }
        // Today's individual quote keeps the summary aligned with live prices.
        if (i == count - 1 && quoteUsable(ticker->quote) && currentRate > 0) {
          value = (double)ticker->quote.price * currentRate * qty;
          gain = realized + value - cost; valid = true;
          priceSources[i] = ticker->quote.asOf; fxSources[i] = currentRateAsOf;
        }
      }
    }
    String point = i ? ",[" : "[";
    point += String((long long)dates[i]) + ",";
    point += valid ? String(value, 2) + "," + String(gain, 2) : "null,null";
    if (!manual) point += "," + String((long long)(valid && usesMarketPrice ? priceSources[i] : 0)) + "," + String((long long)(valid && usesMarketPrice ? fxSources[i] : 0));
    point += "]";
    response.sendContent(point);
  }
  response.sendContent("]}");
  delete[] dates; delete[] prices; delete[] rates;
  delete[] priceSources; delete[] fxSources;
  delete ticker; delete asset;
}

struct HistoryJob {
  String id, name, kind, result;
  int status = 200;
  bool pending = false, running = false, ready = false, delivered = false;
  unsigned long touched = 0;
};
HistoryJob historyJobs[2];
uint32_t historyJobSequence = 0;
bool historyAvailable = false;

void enqueueHistory(const String& name, const String& kind) {
  if (!historyAvailable) { server.send(503, "application/json", "{\"ok\":false,\"error\":\"History worker is unavailable. Restart the device.\"}"); return; }
  xSemaphoreTake(historyMutex, portMAX_DELAY);
  int slot = -1;
  for (int i = 0; i < 2; ++i) {
    auto& j = historyJobs[i];
    if (j.id.isEmpty() || (j.ready && (j.delivered || millis() - j.touched > 90000))) { slot = i; break; }
  }
  if (slot < 0) { xSemaphoreGive(historyMutex); server.send(503, "application/json", "{\"ok\":false,\"error\":\"History worker is busy. Retry shortly.\"}"); return; }
  auto& job = historyJobs[slot]; job = HistoryJob{};
  job.id = String(++historyJobSequence); job.name = name; job.kind = kind; job.pending = true; job.touched = millis();
  String body = "{\"job\":\"" + job.id + "\"}";
  xSemaphoreGive(historyMutex); server.send(202, "application/json", body);
}

void handlePortfolioPosition() {
  String kind = server.arg("kind"), name = server.arg("name");
  if ((kind != "ticker" && kind != "manual" && kind != "benchmark") || name.isEmpty() || name.length() > 60) {
    server.send(400, "application/json", "{\"ok\":false,\"error\":\"Invalid history request.\"}"); return;
  }
  enqueueHistory(name, kind);
}

void handleHistoryJob() {
  String id = server.arg("id"), body; int status = 404;
  xSemaphoreTake(historyMutex, portMAX_DELAY);
  for (auto& j : historyJobs) if (j.id == id) {
    j.touched = millis(); status = j.ready ? j.status : 202;
    body = j.ready ? j.result : "{\"job\":\"" + j.id + "\"}"; if (j.ready) j.delivered = true; break;
  }
  xSemaphoreGive(historyMutex);
  server.send(status, "application/json", status == 404 ? String("{\"ok\":false,\"error\":\"History job expired. Retry the request.\"}") : body);
}

bool processHistoryJobOnce() {
  static int next = 0;
  int slot = -1; String name, kind;
  xSemaphoreTake(historyMutex, portMAX_DELAY);
  for (auto& j : historyJobs) if (j.ready && millis() - j.touched > 90000) j = HistoryJob{};
  for (int offset = 0; offset < 2; ++offset) {
    int i = (next + offset) % 2;
    if (!historyJobs[i].pending) continue;
    slot = i; auto& j = historyJobs[i]; j.pending = false; j.running = true;
    name = j.name; kind = j.kind; next = (i + 1) % 2; break;
  }
  xSemaphoreGive(historyMutex);
  if (slot < 0) return false;
  HistoryResponse response;
  if (kind == "price") {
    int separator = name.indexOf('|'); float price = 0;
    bool ok = separator > 0 && fetchHistoricalPricePLN(name.substring(0, separator), parseDateYMD(name.substring(separator+1)), price);
    response.body = ok ? "{\"ok\":true,\"pricePLN\":" + String(price, 4) + "}" : "{\"ok\":false}";
  } else buildPortfolioPosition(name, kind, response);
  xSemaphoreTake(historyMutex, portMAX_DELAY);
  auto& j = historyJobs[slot]; j.result = std::move(response.body); j.status = response.status;
  j.running = false; j.ready = true; j.touched = millis();
  xSemaphoreGive(historyMutex);
  return true;
}

void historyTask(void*) {
  for (;;) { processHistoryJobOnce(); vTaskDelay(pdMS_TO_TICKS(50)); }
}

// Portfolio value of a single ticker in PLN, or -1 if not held / no valid quote.
float valuePLN(const TickerState &t) {
  if (!quoteUsable(t.quote) || t.holdings <= 0) return -1.0f;
  float rate = getRateToPLN(t.quote.currency);
  return rate > 0 ? t.quote.price * rate * t.holdings : -1.0f;
}

// Descending sort by portfolio value; unheld tickers sink to the bottom (alphabetically).
// Shared by the live ticker array (sortTickersIfNeeded) and web UI row ordering (handleRoot).
void sortByValuePLN(TickerState *arr, int n) {
  for (int i = 0; i < n - 1; i++) {
    for (int j = 0; j < n - i - 1; j++) {
      float vA = valuePLN(arr[j]), vB = valuePLN(arr[j + 1]);
      if ((vB > vA) || (vA < 0 && vB < 0 && arr[j + 1].sym < arr[j].sym)) {
        std::swap(arr[j], arr[j + 1]);
      }
    }
  }
}

void sortTickersIfNeeded() {
  if (cfg.portfolioMode) sortByValuePLN(tickerData, tickerCount);
}

void checkAlerts() {
  bool triggered = false;
  xSemaphoreTake(dataMutex, portMAX_DELAY);
  for (int i = 0; i < tickerCount; i++) {
    if (!quoteUsable(tickerData[i].quote)) continue;
    float rate = getRateToPLN(tickerData[i].quote.currency);
    if (rate <= 0) continue;
    float pricePLN = tickerData[i].quote.price * rate;
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

// Called with dataMutex held. Drop unused FX slots without throwing away
// working cached rates for retained currencies or publishing partial totals.
void pruneQuoteRates() {
  int retained = 0;
  for (int r = 0; r < rateCount; ++r) {
    bool used = false;
    for (int i = 0; i < tickerCount; ++i)
      if (tickerData[i].quote.valid && tickerData[i].quote.currency == exchangeRates[r].curr) used = true;
    if (used) exchangeRates[retained++] = exchangeRates[r];
  }
  rateCount = retained;
}

void refreshQuoteRate(const String& symbol, String* attempted, int& attemptedCount) {
  String currency;
  xSemaphoreTake(dataMutex, portMAX_DELAY);
  pruneQuoteRates();
  int index = getIndexBySym(symbol);
  if (index >= 0 && quoteUsable(tickerData[index].quote)) currency = tickerData[index].quote.currency;
  xSemaphoreGive(dataMutex);
  if (currency.isEmpty() || currency == "PLN") return;
  for (int i = 0; i < attemptedCount; ++i) if (attempted[i] == currency) return;
  if (attemptedCount == MAX_EXCHANGE_RATES) return;
  attempted[attemptedCount++] = currency;
  uint32_t began = millis(); time_t stamp = 0;
  float rate = fetchYahooRate(currency, stamp);
  if (rate > 0) {
    xSemaphoreTake(dataMutex, portMAX_DELAY);
    int slot = 0; while (slot < rateCount && exchangeRates[slot].curr != currency) ++slot;
    if (slot < MAX_EXCHANGE_RATES) {
      exchangeRates[slot] = {currency, rate, stamp};
      if (slot == rateCount) ++rateCount;
    }
    xSemaphoreGive(dataMutex);
  }
  Serial.printf("FX %s: %lu ms%s\n", currency.c_str(), (unsigned long)(millis() - began), rate > 0 ? "" : " (failed; retained cache)");
  redrawPending = true;
}

void fetchTask(void*) {
  for (;;) {
    // Do not spend the initial refresh on unavailable Wi-Fi or an unsynced
    // clock (which would reject every quote as a future timestamp).
    if (WiFi.status() != WL_CONNECTED || time(nullptr) < NTP_SYNC_MIN_EPOCH) { vTaskDelay(pdMS_TO_TICKS(200)); continue; }
    if (fetchPending.load()) {
      fetching = true;
      fetchPending.exchange(false); // Keep priority asserted while claiming the pending cycle.
      setLED(false, false, true);
      uint32_t cycleBegan = millis();

      String symbols[MAX_TICKERS], chartRange, attemptedRates[MAX_EXCHANGE_RATES];
      int attemptedCount = 0;
      xSemaphoreTake(dataMutex, portMAX_DELAY);
      int count = tickerCount;
      chartRange = cfg.chartRange;
      for (int i = 0; i < count; ++i) symbols[i] = tickerData[i].sym;
      xSemaphoreGive(dataMutex);
      for (int i = 0; i < count; ++i) {
        uint32_t began = millis();
        fetchYahoo(i, symbols[i], chartRange);
        Serial.printf("Quote %s: %lu ms\n", symbols[i].c_str(), (unsigned long)(millis() - began));
        // Make the first successful position usable in PLN immediately;
        // currencies are refreshed only once per cycle, including failures.
        refreshQuoteRate(symbols[i], attemptedRates, attemptedCount);
        redrawPending = true;
        vTaskDelay(pdMS_TO_TICKS(800));
      }

      xSemaphoreTake(dataMutex, portMAX_DELAY); pruneQuoteRates(); sortTickersIfNeeded(); xSemaphoreGive(dataMutex);
      checkAlerts();

      lastFetchMillis = millis(); lastFetchTime = time(nullptr);
      fetching = false; setLED(false, false, false);
      redrawPending = true;
      Serial.printf("Quote refresh: %lu ms\n", (unsigned long)(millis() - cycleBegan));
    }
    vTaskDelay(pdMS_TO_TICKS(200));
  }
}

// --- DRAWING (TFT) ---
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
      char buf[16]; struct tm tm; time_t fetchedAt = lastFetchTime.load(); localtime_r(&fetchedAt, &tm);
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
  bool anyHeld = false, anyMissing = false;
  for (int i = 0; i < tickerCount; i++) {
    if (tickerData[i].holdings > 0) anyHeld = true;
    double v, p;
    if (computePeriodPL(i, v, p)) { total += v; pl += p; }
    else if (tickerData[i].holdings > 0) anyMissing = true;
  }
  if (manualCount > 0) {
    total += manualValuePLN();
    anyHeld = true;
  }
  tft.fillRect(0, 240 - FOOTER_H, 320, FOOTER_H, C_HEADER());
  tft.setTextDatum(MC_DATUM);
  if (!anyHeld) {
    tft.setTextColor(C_MUTED(), C_HEADER());
    tft.drawString("No holdings", 160, 240 - FOOTER_H / 2, 1);
    return;
  }

  if (anyMissing) {
    tft.setTextColor(C_MUTED(), C_HEADER());
    tft.drawString("Quotes/FX missing - see web UI", 160, 240 - FOOTER_H / 2, 1);
    return;
  }

  tft.setTextColor((pl >= 0) ? C_UP() : C_DOWN(), C_HEADER());
  tft.drawString("PLN:" + String(total, 2) + " M P&L(" + rangeLabel(cfg.chartRange) + "):" + (pl >= 0 ? "+" : "") + String(pl, 2),
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

  if (!quoteUsable(q)) {
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

  bool br = r > 0 && ((tickerData[idx].alertHigh > 0 && dP >= tickerData[idx].alertHigh) ||
            (tickerData[idx].alertLow > 0 && dP <= tickerData[idx].alertLow));

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
    int tx = constrain(map(p.x, TOUCH_MIN, TOUCH_MAX, 0, 320), 0, 319);
    int ty = constrain(map(p.y, TOUCH_MIN, TOUCH_MAX, 0, 240), 0, 239);
    if (ty < HEADER_H) return;

    if (viewMode == VIEW_GRID) {
      if (ty >= HEADER_H + gridAreaHeight()) return;
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

// --- WEB SERVER ---
void handleFavicon() {
  server.send(200, "image/svg+xml",
    "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 32 32'>"
    "<rect x='4' y='20' width='4.5' height='8' rx='1' fill='#334155'/>"
    "<rect x='11' y='15' width='4.5' height='13' rx='1' fill='#475569'/>"
    "<rect x='18' y='10' width='4.5' height='18' rx='1' fill='#64748B'/>"
    "<rect x='25' y='4' width='4.5' height='24' rx='1' fill='#10B981'/>"
    "</svg>");
}

void handlePortfolio() {
  JsonDocument doc;
  JsonArray positions = doc["positions"].to<JsonArray>();
  bool dark;
  xSemaphoreTake(dataMutex, portMAX_DELAY);
  dark = cfg.darkMode;
  for (int i = 0; i < tickerCount; i++) {
    if (tickerData[i].lotCount == 0) continue;
    JsonObject position = positions.add<JsonObject>();
    position["name"] = tickerData[i].sym;
    position["kind"] = "ticker";
    for (int k = 0; k < tickerData[i].lotCount; ++k) {
      const Lot& lot = tickerData[i].lots[k];
      JsonArray flow = doc["cashFlows"].add<JsonArray>();
      flow.add((int64_t)lot.ts); flow.add((double)lot.qty * lot.pricePLN);
    }
  }
  for (int i = 0; i < manualCount; i++) {
    JsonObject position = positions.add<JsonObject>();
    position["name"] = manualAssets[i].name;
    position["kind"] = "manual";
  }
  xSemaphoreGive(dataMutex);
  String data = scriptSafeJson(doc);
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "text/html; charset=utf-8", "");
  server.sendContent(String("<!DOCTYPE html><html lang='en' data-theme='")
    + (dark ? "dark" : "light") + "'>");
  server.sendContent_P(PORTFOLIO_HEAD);
  server.sendContent("</head><body><main>");
  server.sendContent_P(PORTFOLIO_HTML);
  server.sendContent("<script type='application/json' id='portfolio-config'>" + data + "</script>");
  server.sendContent_P(REQUEST_SCRIPT);
  server.sendContent_P(PORTFOLIO_SCRIPT);
  server.sendContent("");
}

void handleRoot() {
  xSemaphoreTake(dataMutex, portMAX_DELAY);

  std::unique_ptr<TickerState[]> localData(new TickerState[MAX_TICKERS]{});
  std::unique_ptr<ManualAsset[]> localManual(new ManualAsset[MAX_MANUAL_ASSETS]{});
  ExRate localRates[MAX_EXCHANGE_RATES];
  int localRateCount = rateCount;
  for (int i = 0; i < localRateCount; ++i) localRates[i] = exchangeRates[i];
  int localCount = tickerCount;
  int localManualCount = manualCount;
  AppConfig localCfg = cfg;

  for (int i = 0; i < localCount; i++) {
    localData[i] = tickerData[i];
    localData[i].quote.valid = quoteUsable(localData[i].quote);
  }
  for (int i = 0; i < localManualCount; i++) localManual[i] = manualAssets[i];
  xSemaphoreGive(dataMutex);

  // Sort the snapshot while rates cannot change underneath the comparator.
  if (localCfg.portfolioMode) {
    xSemaphoreTake(dataMutex, portMAX_DELAY);
    sortByValuePLN(localData.get(), localCount);
    xSemaphoreGive(dataMutex);
  }

  JsonDocument config;
  JsonArray names = config["tickers"].to<JsonArray>();
  for (int i = 0; i < localCount; ++i) {
    names.add(localData[i].sym);
    config["lotCounts"][localData[i].sym] = localData[i].lotCount;
  }
  config["range"] = localCfg.chartRange;
  config["revision"] = persistentStore.revision();
  config["storageWritable"] = persistentStore.writable();
  config["storageRecovered"] = persistentStore.recovered();

  String rows = "";
  time_t nowForRates = time(nullptr);
  double totalVal = 0, totalPL = 0;
  bool anyMissing = false;

  for (int i = 0; i < localCount; i++) {
    String rawSym = localData[i].sym;
    String symLink = "<a href='https://finance.yahoo.com/quote/" + urlEncode(rawSym)
      + "/' target='_blank' rel='noopener'>" + htmlEscape(rawSym) + "</a>";

    float rate = localData[i].quote.currency == "PLN" ? 1.0f : 0.0f;
    for (int r = 0; r < localRateCount; ++r)
      if (localRates[r].curr == localData[i].quote.currency && freshTimestamp(localRates[r].asOf, nowForRates, MAX_QUOTE_AGE)) rate = localRates[r].rate;
    bool conv = (rate > 0 && localData[i].quote.currency != "PLN");
    if (localData[i].holdings > 0 && (!localData[i].quote.valid || rate <= 0)) anyMissing = true;

    float dPrice = conv ? localData[i].quote.price * rate : localData[i].quote.price;

    String cSym = (!conv && localData[i].quote.valid && localData[i].quote.currency != "PLN")
      ? getCurrencySymbol(localData[i].quote.currency) : "";
    String price = localData[i].quote.valid ? cSym + String(dPrice, 2) : "--";
    if (localData[i].quote.valid && localData[i].quote.errors > 0) price += " <span class='hint2'>(cached)</span>";
    String pct = localData[i].quote.valid
      ? (localData[i].quote.pct >= 0 ? "+" : "") + String(localData[i].quote.pct, 2) + "%" : "--";
    String clr = localData[i].quote.valid ? (localData[i].quote.pct >= 0 ? "#00cc44" : "#ff4444") : "#888";
    String arrow = localData[i].quote.valid
      ? (localData[i].quote.pct > 0.0f ? "&#9650;"
         : localData[i].quote.pct < 0.0f ? "&#9660;" : "&mdash;") : "";

    String valStr = "", plStr = "", plClr = clr;
    double v, p;
    if (computePeriodPLFor(localData[i], v, p, rate)) {
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

  double manualVal = 0, manualGain = 0;
  String manualRows = "";
  char todayBuf[11] = "";
  time_t now = time(nullptr); struct tm todayTm; localtime_r(&now, &todayTm);
  strftime(todayBuf, sizeof(todayBuf), "%Y-%m-%d", &todayTm);
  for (int i = 0; i < localManualCount; i++) {
    manualVal += localManual[i].valuePLN;
    manualGain += localManual[i].gainPLN;
    String snapDate = String(todayBuf);
    if (localManual[i].historyCount > 0) {
      struct tm snapTm; time_t ts = localManual[i].history[localManual[i].historyCount - 1].ts;
      localtime_r(&ts, &snapTm); char snapBuf[11]; strftime(snapBuf, sizeof(snapBuf), "%Y-%m-%d", &snapTm);
      snapDate = String(snapBuf);
    }
    manualRows += savingsRow(i, localManual[i].ppk, htmlEscape(localManual[i].name), snapDate,
      localManual[i].valuePLN, localManual[i].gainPLN);
    JsonObject account = config["manual"].add<JsonObject>();
    account["id"] = i; account["name"] = localManual[i].name;
    account["kind"] = localManual[i].ppk ? "ppk" : "savings";
    account["value"] = localManual[i].valuePLN; account["gain"] = localManual[i].gainPLN;
    account["version"] = manualVersion(localManual[i]);
  }

  String holdRows = "";
  double totalRealPL = 0;
  bool anyRealPl = false;
  for (int i = 0; i < localCount; i++) {
    float qty; double cost;
    computeCostBasis(localData[i], qty, cost);
    double avgCost = (qty > 0) ? (cost / qty) : 0.0;

    double plVal, plCost, pl;
    float rate = localData[i].quote.currency == "PLN" ? 1.0f : 0.0f;
    for (int r = 0; r < localRateCount; ++r)
      if (localRates[r].curr == localData[i].quote.currency && freshTimestamp(localRates[r].asOf, nowForRates, MAX_QUOTE_AGE)) rate = localRates[r].rate;
    bool havePl = computePLFor(localData[i], plVal, plCost, pl, rate);
    String plStr = havePl ? (pl >= 0 ? "+" : "") + String(pl, 2) : "&mdash;";
    String plClr = havePl ? (pl >= 0 ? "#00cc44" : "#ff4444") : "inherit";
    if (havePl) { totalRealPL += pl; anyRealPl = true; }

    String field = htmlEscape(localData[i].sym);
    holdRows += "<tr><td>" + field + "</td>"
      + "<td class='tnowrap'>" + String(qty, 4) + "</td>"
      + "<td class='tnowrap'>" + (qty > 0 ? String(avgCost, 2) : "&mdash;") + "</td>"
      + "<td class='tnowrap' style='color:" + plClr + "'>" + plStr + "</td>"
      + "<td><input class='inp' type='number' name='ahi_" + field
      + "' form='cfgform' value='" + String(localData[i].alertHigh, 2) + "' step='any' min='0'></td>"
      + "<td><input class='inp' type='number' name='alo_" + field
      + "' form='cfgform' value='" + String(localData[i].alertLow, 2) + "' step='any' min='0'></td></tr>";
  }
  totalRealPL += manualGain;
  if (localManualCount > 0) anyRealPl = true;
  totalVal += manualVal;

  String tickerOptions = "";
  for (int i = 0; i < localCount; i++) {
    tickerOptions += "<option value='" + htmlEscape(localData[i].sym) + "'>" + htmlEscape(localData[i].sym) + "</option>";
  }

  std::vector<String> txRows;
  std::unique_ptr<TransactionRef[]> txOrder(new TransactionRef[MAX_TICKERS * MAX_LOTS]);
  int transactionCount = newestTransactions(localData.get(), localCount, txOrder.get(), MAX_TICKERS * MAX_LOTS);
  int shown = min(transactionCount, MAX_LOTS_SHOWN);
  txRows.reserve(shown + MAX_TICKERS + 1);
  for (int row = 0; row < shown; ++row) {
      int i = txOrder[row].ticker, k = txOrder[row].lot;
      time_t ts = localData[i].lots[k].ts;
      struct tm tmk; localtime_r(&ts, &tmk);
      char buf[12]; strftime(buf, sizeof(buf), "%d.%m.%Y", &tmk);
      char isoBuf[11]; strftime(isoBuf, sizeof(isoBuf), "%Y-%m-%d", &tmk);
      String qtyStr = (localData[i].lots[k].qty >= 0 ? "+" : "") + String(localData[i].lots[k].qty, 8);
      char exactQty[32], exactPrice[32]; formatQuantity(localData[i].lots[k].qty, exactQty); formatQuantity(localData[i].lots[k].pricePLN, exactPrice);
      String version = htmlEscape(lotVersion(localData[i].lots[k]));
      double total = localData[i].lots[k].qty * localData[i].lots[k].pricePLN;
      String totalStr = (total >= 0 ? "+" : "") + String(total, 2);
      txRows.push_back("<tr><td>" + htmlEscape(localData[i].sym) + "</td><td class='tnowrap'>" + String(buf) + "</td>"
        + "<td class='tnowrap'>" + qtyStr + "</td>"
        + "<td class='tnowrap'>" + String(localData[i].lots[k].pricePLN, 2) + "</td>"
        + "<td class='tnowrap'>" + totalStr + "</td>"
        + "<td class='tnowrap'>"
        + "<button type='button' class='editlink' data-sym='" + htmlEscape(localData[i].sym)
        + "' data-date='" + isoBuf + "' data-qty='" + exactQty
        + "' data-price='" + exactPrice + "' data-lot='" + String(localData[i].lots[k].id) + "' data-version='" + version
        + "' onclick='editLot(this)' aria-label='Edit transaction'>&#9998;</button> "
        + "<button type='button' class='dellink' data-sym='" + htmlEscape(localData[i].sym)
        + "' data-lot='" + String(localData[i].lots[k].id) + "' data-version='" + version + "' onclick='deleteLot(this)' aria-label='Delete transaction'>&#10005;</button></td></tr>");
  }
  if (transactionCount > shown) {
    txRows.push_back("<tr><td colspan='6' class='hint2'>...and " + String(transactionCount - shown)
      + " older (<a href='/api/transactions' download='transactions.json'>full transaction backup</a>)</td></tr>");
  }
  for (int i = 0; i < localCount; ++i) {
    int n = localData[i].lotCount;
    if (n == 0 && localData[i].legacyHint > 0) {
      txRows.push_back("<tr><td colspan='6' class='hint2' style='color:#e6a23c'>Legacy record detected: "
        + String(localData[i].legacyHint, 4) + " units (no purchase price) &mdash; add real transactions below.</td></tr>");
    }
  }

  TrackerPage page{
    localCfg.darkMode, localCfg.portfolioMode, anyMissing, anyRealPl,
    rows, holdRows, std::move(txRows), manualRows, tickerOptions, scriptSafeJson(config),
    totalVal, totalPL, totalRealPL,
    localCfg.refreshSec, localCfg.brightness, MIN_REFRESH,
    localCfg.nightModeEnabled, localCfg.nightFrom, localCfg.nightTo,
    localCfg.chartRange, rangeLabel(localCfg.chartRange)
  };
  // Release financial snapshots before rendering the response.
  localData.reset(); localManual.reset(); txOrder.reset();
  sendRootHtml(server, page);

}

void sendMutationResult(bool ok, const String& message, const char* destination = "/") {
  if (server.arg("format") == "json") {
    JsonDocument doc;
    doc["ok"] = ok;
    doc["revision"] = persistentStore.revision();
    if (!ok) doc["error"] = message;
    String json;
    serializeJson(doc, json);
    server.send(ok ? 200 : 400, "application/json", json);
  } else {
    server.send(ok ? 200 : 400, "text/html; charset=utf-8",
      buildRedirectPage(cfg.darkMode, ok ? "\xe2\x9c\x85" : "\xe2\x9a\xa0",
        htmlEscape(message).c_str(), destination));
  }
}

bool readNumber(const String& key, float& value) {
  return server.hasArg(key) && parseFiniteNumber(server.arg(key).c_str(), value);
}

bool readMoneyNumber(const String& key, Money& value) {
  return server.hasArg(key) && parseMoney(server.arg(key).c_str(), value);
}

String lotVersion(const Lot& l) {
  char qty[32], price[32]; formatQuantity(l.qty, qty); formatQuantity(l.pricePLN, price);
  return String(l.id) + ":" + String((long long)l.ts) + ":" + qty + ":" + price;
}

int findLot(const TickerState& t, const String& id) {
  for (int i = 0; i < t.lotCount; ++i) if (String(t.lots[i].id) == id) return i;
  return -1;
}

uint32_t requestHash(const char* op) {
  uint32_t hash = crcBytes(~0U, (const uint8_t*)op, strlen(op));
  const char* keys[] = {"lt", "ld", "lq", "lp", "lk", "lo", "lv"};
  for (const char* key : keys) { String value = server.arg(key); hash = crcBytes(hash, (const uint8_t*)value.c_str(), value.length()); const uint8_t separator = 0; hash = crcBytes(hash, &separator, 1); }
  if (strcmp(op,"restore") == 0) {
    const char* restoreKeys[] = {"restoreKind","restoreToken"};
    for (const char* key : restoreKeys) { String value=server.arg(key); hash=crcBytes(hash,(const uint8_t*)value.c_str(),value.length()); const uint8_t separator=0; hash=crcBytes(hash,&separator,1); }
  }
  return ~hash;
}

bool beginTransactionRequest(const char* op) {
  String id = server.arg("rid");
  bool valid = id.length() >= 16 && id.length() <= 64;
  for (size_t i = 0; i < id.length(); ++i) if (!isalnum((unsigned char)id[i]) && id[i] != '-') valid = false;
  if (!valid) { sendMutationResult(false, "Refresh the page before saving: a transaction request ID is required."); return false; }
  uint32_t hash = requestHash(op);
  for (int i = 0; i < receiptCount; ++i) if (receipts[i].id == id) {
    sendMutationResult(receipts[i].hash == hash, "This request ID was already used for a different transaction. Refresh the page."); return false;
  }
  if (server.arg("base") != String(persistentStore.revision())) {
    sendMutationResult(false, "Saved data has changed, or this old request can no longer be safely retried. Refresh and check the transaction list."); return false;
  }
  return true;
}

void rememberTransaction(const char* op) {
  if (receiptCount == 32) { for (int i = 1; i < 32; ++i) receipts[i-1] = receipts[i]; --receiptCount; }
  receipts[receiptCount++] = {server.arg("rid"), requestHash(op)};
}

bool readInteger(const String& key, int& value, int minimum, int maximum) {
  float number;
  if (!readNumber(key, number) || number < minimum || number > maximum || floorf(number) != number) return false;
  value = (int)number;
  return true;
}

void handleSave() {
  MutationGuard mutation;
  std::unique_ptr<TickerState[]> updated(new TickerState[MAX_TICKERS]{});
  AppConfig nextConfig;
  int nextCount = 0;
  bool ok = true;
  String message = "Settings saved!";
  xSemaphoreTake(dataMutex, portMAX_DELAY);
  nextConfig = cfg;
  String raw = server.arg("tickers");
  int start = 0;
  for (int p = 0; p <= (int)raw.length(); ++p) {
    if (p != (int)raw.length() && raw[p] != ',') continue;
    String sym = raw.substring(start, p); sym.trim(); sym.toUpperCase(); start = p+1;
    if (sym.isEmpty()) continue;
    if (!validTickerSymbol(sym) || nextCount == MAX_TICKERS) { ok = false; break; }
    for (int i = 0; i < nextCount; ++i) if (updated[i].sym == sym) ok = false;
    if (!ok) break;
    int old = getIndexBySym(sym);
    if (old >= 0) updated[nextCount] = tickerData[old];
    else { updated[nextCount].sym = sym; updated[nextCount].quote.sym = sym; updated[nextCount].quote.currency = "USD"; }
    float high = updated[nextCount].alertHigh, low = updated[nextCount].alertLow;
    if (server.hasArg("ahi_" + sym) && (!readNumber("ahi_" + sym, high) || high < 0)) ok = false;
    if (server.hasArg("alo_" + sym) && (!readNumber("alo_" + sym, low) || low < 0)) ok = false;
    updated[nextCount].alertHigh = high; updated[nextCount].alertLow = low;
    ++nextCount;
  }
  if (!server.hasArg("tickers") || nextCount == 0 || !ok) {
    ok = false; message = "Use 1–8 unique ticker symbols and nonnegative alert prices.";
  }
  for (int i = 0; ok && i < tickerCount; ++i) {
    bool retained = false;
    for (int k = 0; k < nextCount; ++k) if (updated[k].sym == tickerData[i].sym) retained = true;
    if (!retained && tickerData[i].lotCount > 0 && server.arg("confirmremove") != "1") {
      ok = false; message = "Removing a ticker with transactions requires confirmation. Download a backup first.";
    }
  }
  if (ok && (!readInteger("refresh", nextConfig.refreshSec, MIN_REFRESH, 3600)
      || !readInteger("bright", nextConfig.brightness, 10, 255)
      || !readInteger("nightfr", nextConfig.nightFrom, 0, 23)
      || !readInteger("nightto", nextConfig.nightTo, 0, 23)
      || !isValidRange(server.arg("range")))) {
    ok = false; message = "Invalid display settings. Check refresh, brightness, night hours and chart period.";
  }
  if (ok) {
    nextConfig.darkMode = server.hasArg("darkmode");
    nextConfig.portfolioMode = server.hasArg("portfolio");
    nextConfig.nightModeEnabled = server.hasArg("nighten");
    nextConfig.chartRange = server.arg("range");
    for (int i = 0; i < nextCount; ++i) {
      if (cfg.chartRange != nextConfig.chartRange) updated[i].quote.valid = false;
      tickerData[i] = updated[i];
    }
    tickerCount = nextCount; cfg = nextConfig;
  }
  xSemaphoreGive(dataMutex);
  if (mutation.commit(ok, message)) fetchPending = true;
  sendMutationResult(ok, message, "/");
}

void handleSaveManual() {
  MutationGuard mutation;
  int requested;
  if (!readInteger("mcount", requested, 0, MAX_MANUAL_ASSETS)) {
    sendMutationResult(false, "Invalid manual position count.", "/"); return;
  }
  std::unique_ptr<ManualAsset[]> updated(new ManualAsset[MAX_MANUAL_ASSETS]{});
  bool used[MAX_MANUAL_ASSETS] = {};
  int count = 0;
  bool ok = true;
  String message = "Manual valuations saved!";
  time_t now = time(nullptr);
  xSemaphoreTake(dataMutex, portMAX_DELAY);
  for (int i = 0; ok && i < requested; ++i) {
    String name = server.arg("mname" + String(i)); name.trim();
    Money value, gain;
    time_t ts = parseDateYMD(server.arg("mdate" + String(i)));
    if (name.isEmpty() || name.length() > 60 || !readMoneyNumber("mvalue" + String(i), value)
        || !readMoneyNumber("mgain" + String(i), gain) || value < 0 || ts <= 0
        || now < NTP_SYNC_MIN_EPOCH || ts > historyDayEnd(now)) {
      ok = false; message = "Check the name, amounts and valuation date. Future dates are not supported."; break;
    }
    for (int k = 0; k < count; ++k) if (updated[k].name == name) ok = false;
    int old = -1;
    if (server.hasArg("mid" + String(i))) {
      if (!readInteger("mid" + String(i), old, -1, manualCount-1)) ok = false;
    } else {
      for (int k = 0; k < manualCount; ++k) if (manualAssets[k].name == name) old = k;
    }
    if (!ok || (old >= 0 && used[old])) {
      ok = false; message = "Manual names must be unique and each saved position can appear only once."; break;
    }
    if (old >= 0) { updated[count] = manualAssets[old]; used[old] = true; }
    updated[count].name = name;
    if (old < 0) { String upperName = name; upperName.toUpperCase(); updated[count].ppk = upperName.indexOf("PPK") >= 0; }
    if (!recordManualValuation(updated[count], ts, value, gain)) {
      ok = false; message = "That date is older than this position's retained history. Download a backup first."; break;
    }
    ++count;
  }
  if (ok) {
    for (int i = 0; i < count; ++i) manualAssets[i] = updated[i];
    for (int i = count; i < manualCount; ++i) manualAssets[i] = ManualAsset{};
    manualCount = count;
  }
  xSemaphoreGive(dataMutex);
  mutation.commit(ok, message);
  sendMutationResult(ok, message, "/");
}

// Operate on one account, without resubmitting (or deleting) other accounts.
void handleManualAction() {
  MutationGuard mutation;
  String op = server.arg("op"), name = server.arg("name"); name.trim();
  if (op == "create" && server.arg("kind") == "ppk" && name.isEmpty()) name = "PPK";
  time_t ts = parseDateYMD(server.arg("date")), now = time(nullptr);
  bool dated = op != "edit" && op != "delete";
  bool ok = !dated || (ts > 0 && now >= NTP_SYNC_MIN_EPOCH && ts <= historyDayEnd(now));
  String message = "Check the date and amounts. Future dates are not supported.";
  std::unique_ptr<ManualAsset> next(new ManualAsset{});
  xSemaphoreTake(dataMutex, portMAX_DELAY);
  int id = -1;
  if (op == "create") {
    Money value, capital;
    ok = ok && manualCount < MAX_MANUAL_ASSETS && !name.isEmpty() && name.length() <= 60
      && (server.arg("kind") == "ppk" || server.arg("kind") == "savings")
      && readMoneyNumber("value", value) && value >= 0
      && readMoneyNumber("capital", capital) && capital >= 0
      && (server.arg("kind") != "ppk" || value == 0 || capital > 0);
    for (int i = 0; i < manualCount; ++i) if (manualAssets[i].name == name) ok = false;
    if (ok) {
      next->name = name; next->ppk = server.arg("kind") == "ppk";
      ok = recordManualValuation(*next, ts, value, Money::fromCents(value.cents - capital.cents));
      if (ok) manualAssets[manualCount++] = *next;
    } else message = "Check the opening balance and contributions. Names must be unique; the limit is four accounts.";
  } else {
    ok = ok && readInteger("id", id, 0, manualCount - 1);
    if (ok && server.arg("version") != manualVersion(manualAssets[id])) {
      ok = false; message = "This account has changed. Refresh and check whether the operation was already saved before retrying.";
    }
    if (ok) {
      *next = manualAssets[id];
      time_t last = next->historyCount ? next->history[next->historyCount - 1].ts : 0;
      if (dated && ts < last && !isSameLocalDay(ts, last)) {
        // An empty account created as a zero placeholder may receive its first
        // real contribution with the original date. Never discard real values.
        bool empty = next->valuePLN == 0 && next->gainPLN == 0;
        for (int i = 0; i < next->historyCount; ++i)
          if (next->history[i].valuePLN != 0 || next->history[i].gainPLN != 0) empty = false;
        if (empty) next->historyCount = 0;
        else { ok = false; message = "Record contributions and valuations chronologically. This date is earlier than the last saved update."; }
      }
    }
    if (ok && op == "delete") {
      for (int i = id + 1; i < manualCount; ++i) manualAssets[i - 1] = manualAssets[i];
      manualAssets[--manualCount] = ManualAsset{};
    } else if (ok && op == "edit") {
      ok = !name.isEmpty() && name.length() <= 60
        && (server.arg("kind") == "ppk" || server.arg("kind") == "savings");
      for (int i = 0; i < manualCount; ++i) if (i != id && manualAssets[i].name == name) ok = false;
      if (ok) { next->name = name; next->ppk = server.arg("kind") == "ppk"; manualAssets[id] = *next; }
      else message = "Enter a unique name and choose the account type.";
    } else if (ok) {
      Money value, gain;
      if (op == "savings" && !next->ppk) {
        Money deposit, interest;
        ok = readMoneyNumber("deposit", deposit) && readMoneyNumber("interest", interest)
          && (deposit != 0 || interest != 0)
          && savingsUpdate(next->valuePLN, next->gainPLN, deposit, interest, value, gain);
      } else if (op == "ppk-deposit" && next->ppk) {
        Money employee, employer, state, reported;
        bool haveValuation = !server.arg("value").isEmpty();
        ok = readMoneyNumber("employee", employee) && readMoneyNumber("employer", employer) && readMoneyNumber("state", state)
          && (employee > 0 || employer > 0 || state > 0)
          && (!haveValuation || readMoneyNumber("value", reported))
          && ppkUpdate(next->valuePLN, next->gainPLN, employee, employer, state, haveValuation, reported, value, gain);
      } else if (op == "valuation") {
        Money reported;
        ok = readMoneyNumber("value", reported)
          && ppkUpdate(next->valuePLN, next->gainPLN, 0, 0, 0, true, reported, value, gain);
        if (ok && next->ppk && next->valuePLN == 0 && next->gainPLN == 0 && reported > 0) {
          ok = false; message = "Record PPK contributions first. Otherwise the entire account balance would incorrectly count as profit.";
        }
      } else ok = false;
      if (ok) ok = recordManualValuation(*next, ts, value, gain);
      if (ok) manualAssets[id] = *next;
    }
  }
  xSemaphoreGive(dataMutex);
  mutation.commit(ok, message);
  sendMutationResult(ok, message);
}

// Validate a prospective ledger before changing saved state, including edits
// and deletes that would leave later sales without enough purchased units.
void handleAddLot() {
  if (!beginTransactionRequest("add")) return;
  MutationGuard mutation;
  String sym = server.arg("lt");
  float qty, price;
  time_t ts = parseDateYMD(server.arg("ld")), now = time(nullptr);
  bool ok = ts > 0 && now >= NTP_SYNC_MIN_EPOCH && ts <= historyDayEnd(now)
    && readNumber("lq", qty) && qty != 0 && readNumber("lp", price) && price >= 0;
  xSemaphoreTake(dataMutex, portMAX_DELAY);
  int t = getIndexBySym(sym);
  if (ok && t >= 0 && tickerData[t].lotCount < MAX_LOTS && nextLotId < UINT32_MAX) {
    ok = addLot(t, ts, qty, price);
    if (ok && !validLotSequence(tickerData[t].lots, tickerData[t].lotCount)) {
      // addLot is stable for same-day entries; find the new entry's insertion point.
      int pos = tickerData[t].lotCount-1;
      for (int i = 0; i < tickerData[t].lotCount; ++i) if (tickerData[t].lots[i].ts > ts) { pos = i-1; break; }
      deleteLot(t, pos); ok = false;
    }
    if (ok) {
      int pos = tickerData[t].lotCount - 1;
      for (int i = 0; i < tickerData[t].lotCount; ++i) if (tickerData[t].lots[i].ts > ts) { pos = i - 1; break; }
      tickerData[t].lots[pos].id = nextLotId++; refreshHoldings(tickerData[t]);
    }
  } else ok = false;
  xSemaphoreGive(dataMutex);
  String message = ok ? "Transaction added!" : "Check date and amounts, available quantity and the 60-entry history limit.";
  if (ok) rememberTransaction("add");
  mutation.commit(ok, message);
  sendMutationResult(ok, message);
}

void handleDelLot() {
  if (!beginTransactionRequest("delete")) return;
  MutationGuard mutation;
  String sym = server.arg("lt");
  bool ok = true;
  xSemaphoreTake(dataMutex, portMAX_DELAY);
  int t = getIndexBySym(sym);
  int k = t >= 0 ? findLot(tickerData[t], server.arg("lk")) : -1;
  if (t >= 0 && k >= 0 && server.arg("lv") == lotVersion(tickerData[t].lots[k])) {
    Lot backup = tickerData[t].lots[k];
    deleteLot(t, k);
    ok = validLotSequence(tickerData[t].lots, tickerData[t].lotCount);
    if (!ok) {
      // Restore at the same index; same-day transaction order must not change.
      for (int i = tickerData[t].lotCount; i > k; --i) tickerData[t].lots[i] = tickerData[t].lots[i-1];
      tickerData[t].lots[k] = backup; ++tickerData[t].lotCount;
    } else refreshHoldings(tickerData[t]);
  } else ok = false;
  xSemaphoreGive(dataMutex);
  String message = ok ? "Transaction deleted." : "Transaction changed/not found, or deleting it would invalidate a later sale. Refresh the page.";
  if (ok) rememberTransaction("delete");
  mutation.commit(ok, message);
  sendMutationResult(ok, message);
}

void handleEditLot() {
  if (!beginTransactionRequest("edit")) return;
  MutationGuard mutation;
  String origSym = server.hasArg("lo") ? server.arg("lo") : server.arg("lt"), sym = server.arg("lt");
  float qty, price;
  time_t ts = parseDateYMD(server.arg("ld")), now = time(nullptr);
  bool ok = ts > 0
    && now >= NTP_SYNC_MIN_EPOCH && ts <= historyDayEnd(now)
    && readNumber("lq", qty) && qty != 0 && readNumber("lp", price) && price >= 0;
  std::unique_ptr<TickerState> original(new TickerState{}), target(new TickerState{});
  xSemaphoreTake(dataMutex, portMAX_DELAY);
  int old = getIndexBySym(origSym), next = getIndexBySym(sym);
  int k = old >= 0 ? findLot(tickerData[old], server.arg("lk")) : -1;
  if (ok && old >= 0 && next >= 0 && k >= 0 && server.arg("lv") == lotVersion(tickerData[old].lots[k])) {
    *original = tickerData[old]; *target = tickerData[next];
    Lot backup = tickerData[old].lots[k];
    deleteLot(old, k);
    // An unchanged date and symbol retain the transaction's same-day order.
    if (old == next && ts == backup.ts) {
      for (int i = tickerData[old].lotCount; i > k; --i) tickerData[old].lots[i] = tickerData[old].lots[i-1];
      tickerData[old].lots[k] = {ts, qty, price, backup.id}; ++tickerData[old].lotCount;
    } else {
      ok = addLot(next, ts, qty, price);
      if (ok) { int pos = tickerData[next].lotCount - 1; for (int i = 0; i < tickerData[next].lotCount; ++i) if (tickerData[next].lots[i].ts > ts) { pos = i-1; break; } tickerData[next].lots[pos].id = backup.id; }
    }
    if (ok) ok = validLotSequence(tickerData[old].lots, tickerData[old].lotCount)
      && validLotSequence(tickerData[next].lots, tickerData[next].lotCount);
    if (!ok) { tickerData[old] = *original; if (next != old) tickerData[next] = *target; }
    else { refreshHoldings(tickerData[old]); if (next != old) refreshHoldings(tickerData[next]); }
  } else ok = false;
  xSemaphoreGive(dataMutex);
  String message = ok ? "Transaction updated!" : "Transaction changed/not found, or invalid date, amount, sale or target history limit. Refresh the page.";
  if (ok) rememberTransaction("edit");
  mutation.commit(ok, message);
  sendMutationResult(ok, message);
}

void handleHistPrice() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  String sym = server.hasArg("lt") ? server.arg("lt") : "";
  String dateStr = server.hasArg("ld") ? server.arg("ld") : "";

  xSemaphoreTake(dataMutex, portMAX_DELAY);
  int found = getIndexBySym(sym);
  xSemaphoreGive(dataMutex);

  time_t target = parseDateYMD(dateStr);
  if (sym.isEmpty() || found < 0 || target <= 0 || target > historyDayEnd(time(nullptr))) {
    server.send(200, "application/json", "{\"ok\":false}");
    return;
  }

  enqueueHistory(sym + "|" + dateStr, "price");
}

void handleApiQuotes() {
  std::unique_ptr<TickerState[]> copy(new TickerState[MAX_TICKERS]{});
  float rates[MAX_TICKERS];
  String range;
  xSemaphoreTake(dataMutex, portMAX_DELAY);
  int count = tickerCount;
  range = cfg.chartRange;
  for (int i = 0; i < count; ++i) {
    copy[i] = tickerData[i]; rates[i] = getRateToPLN(copy[i].quote.currency);
  }
  xSemaphoreGive(dataMutex);
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "application/json", "");
  server.sendContent("[");
  for (int i = 0; i < count; ++i) {
    const TickerState& ticker = copy[i];
    JsonDocument doc;
    float qty; double cost;
    computeCostBasis(ticker, qty, cost);
    bool valid = quoteUsable(ticker.quote) && rates[i] > 0;
    doc["sym"] = ticker.sym; doc["range"] = range;
    doc["nativeCurrency"] = ticker.quote.currency;
    if (quoteUsable(ticker.quote)) { doc["nativePrice"] = ticker.quote.price; doc["pct"] = ticker.quote.pct; }
    else { doc["nativePrice"] = nullptr; doc["pct"] = nullptr; }
    if (valid) {
      double price = (double)ticker.quote.price*rates[i];
      doc["pricePLN"] = price; doc["plPLN"] = qty > 0 ? price*qty-cost : 0;
    } else { doc["pricePLN"] = nullptr; doc["plPLN"] = nullptr; }
    doc["heldQty"] = qty; doc["costBasisPLN"] = cost;
    doc["errors"] = ticker.quote.errors;
    doc["valid"] = valid; doc["quoteValid"] = quoteUsable(ticker.quote); doc["fxValid"] = rates[i] > 0;
    doc["quoteAsOf"] = ticker.quote.asOf; doc["cached"] = ticker.quote.errors > 0;
    JsonArray lots = doc["lots"].to<JsonArray>();
    for (int k = 0; k < ticker.lotCount; ++k) {
      JsonObject lot = lots.add<JsonObject>();
      char qtyText[32],priceText[32];formatQuantity(ticker.lots[k].qty,qtyText);formatQuantity(ticker.lots[k].pricePLN,priceText);
      lot["ts"] = ticker.lots[k].ts; lot["qty"] = serialized(String(qtyText)); lot["pricePLN"] = serialized(String(priceText));
      lot["id"] = ticker.lots[k].id;
    }
    String json; serializeJson(doc, json);
    if (i) server.sendContent(",");
    server.sendContent(json);
  }
  server.sendContent("]"); server.sendContent("");
}

void handleForceRefresh() {
  fetchPending = true;
  server.send(200, "text/html; charset=utf-8",
    buildRedirectPage(cfg.darkMode, "\xf0\x9f\x94\x84", "Refreshing..."));
}

void handleApiTransactions() {
  std::unique_ptr<TickerState[]> copy(new TickerState[MAX_TICKERS]{});
  xSemaphoreTake(dataMutex, portMAX_DELAY);
  int count = tickerCount;
  for (int i = 0; i < count; ++i) copy[i] = tickerData[i];
  xSemaphoreGive(dataMutex);
  server.sendHeader("Content-Disposition", "attachment; filename=\"transactions.json\"");
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "application/json", "");
  server.sendContent("[\n");
  bool first = true;
  for (int i = 0; i < count; ++i) {
    for (int k = 0; k < copy[i].lotCount; ++k) {
      const Lot& lot = copy[i].lots[k];
      tm date{}; localtime_r(&lot.ts, &date);
      char dateText[11]; strftime(dateText, sizeof(dateText), "%Y-%m-%d", &date);
      JsonDocument doc;
      doc["symbol"] = copy[i].sym; doc["date"] = dateText;
      char qtyText[32],priceText[32];formatQuantity(lot.qty,qtyText);formatQuantity(lot.pricePLN,priceText);
      doc["qty"] = serialized(String(qtyText)); doc["pricePLN"] = serialized(String(priceText));
      doc["totalPLN"] = (double)lot.qty*lot.pricePLN;
      String json; serializeJson(doc, json);
      if (!first) server.sendContent(",\n");
      server.sendContent(json); first = false;
    }
  }
  server.sendContent("\n]"); server.sendContent("");
}

void handleApiSavingsPPK() {
  std::unique_ptr<ManualAsset[]> copy(new ManualAsset[MAX_MANUAL_ASSETS]{});
  xSemaphoreTake(dataMutex, portMAX_DELAY);
  int count = manualCount;
  for (int i = 0; i < count; ++i) copy[i] = manualAssets[i];
  xSemaphoreGive(dataMutex);
  server.sendHeader("Content-Disposition", "attachment; filename=\"savings-ppk.json\"");
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "application/json", "");
  server.sendContent("[");
  for (int i = 0; i < count; ++i) {
    JsonDocument doc;
    doc["name"] = copy[i].name; doc["kind"] = copy[i].ppk ? "ppk" : "savings";
    char valueText[32],gainText[32];formatMoney(copy[i].valuePLN,valueText);formatMoney(copy[i].gainPLN,gainText);
    doc["valuePLN"] = serialized(String(valueText)); doc["gainPLN"] = serialized(String(gainText));
    JsonArray history = doc["history"].to<JsonArray>();
    for (int k = 0; k < copy[i].historyCount; ++k) {
      JsonObject row = history.add<JsonObject>();
      formatMoney(copy[i].history[k].valuePLN,valueText);formatMoney(copy[i].history[k].gainPLN,gainText);
      row["ts"] = copy[i].history[k].ts; row["valuePLN"] = serialized(String(valueText)); row["gainPLN"] = serialized(String(gainText));
    }
    String json; serializeJson(doc, json);
    if (i) server.sendContent(",");
    server.sendContent(json);
  }
  server.sendContent("]"); server.sendContent("");
}

#include "backup_restore.h"

// --- SETUP & LOOP ---
void setup() {
  Serial.begin(115200); delay(300);
  pinMode(LED_R, OUTPUT); pinMode(LED_G, OUTPUT); pinMode(LED_B, OUTPUT);
  pinMode(BL_PIN, OUTPUT); digitalWrite(BL_PIN, HIGH);

  tft.init(); tft.setRotation(3); tft.fillScreen(TFT_BLACK);
  touchSPI.begin(25, 39, 32, TOUCH_CS_PIN);
  touch.begin(touchSPI); touch.setRotation(3);

  persistentStore.begin();
  loadPrefs(); applyBrightness(cfg.brightness);
  dataMutex = xSemaphoreCreateMutex();
  prefsMutex = xSemaphoreCreateMutex();
  networkMutex = xSemaphoreCreateMutex();
  historyMutex = xSemaphoreCreateMutex();
  if (!dataMutex || !prefsMutex || !networkMutex || !historyMutex) { Serial.println("Mutex allocation failed"); delay(1000); ESP.restart(); }

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
  server.on("/portfolio", HTTP_GET, handlePortfolio);
  server.on("/favicon.svg",HTTP_GET, handleFavicon);
  server.on("/save", HTTP_POST, handleSave);
  server.on("/savemanual", HTTP_POST, handleSaveManual);
  server.on("/manual-action", HTTP_POST, handleManualAction);
  server.on("/api/portfolio-position", HTTP_GET, handlePortfolioPosition);
  server.on("/api/history-job", HTTP_GET, handleHistoryJob);
  server.on("/addlot", HTTP_POST, handleAddLot);
  server.on("/api/histprice", HTTP_GET, handleHistPrice);
  server.on("/dellot", HTTP_POST, handleDelLot);
  server.on("/editlot", HTTP_POST, handleEditLot);
  server.on("/refresh", HTTP_GET, handleForceRefresh);
  server.on("/api/quotes", HTTP_GET, handleApiQuotes);
  server.on("/api/transactions", HTTP_GET, handleApiTransactions);
  server.on("/api/savings-ppk", HTTP_GET, handleApiSavingsPPK);
  server.on("/api/manual-investments", HTTP_GET, handleApiSavingsPPK); // Legacy bookmarks/API clients.
  server.on("/api/restore-preview", HTTP_POST, handleRestorePreview, handleRestoreUpload);
  server.on("/api/restore", HTTP_POST, handleRestoreCommit);
  server.begin();
  MDNS.begin("portfolio-tracker");

  setLED(false, false, false);
  xTaskCreatePinnedToCore(fetchTask, "fetch", 8192, NULL, 1, NULL, 0);
  historyJobSequence = esp_random();
  historyAvailable = xTaskCreatePinnedToCore(historyTask, "history", 8192, NULL, 1, NULL, 0) == pdPASS;
  lastTouchAction = millis();
}

void loop() {
  server.handleClient();
  handleTouch();
  if (redrawPending.exchange(false)) drawAll();

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
      time_t localNow = time(nullptr);
      if (localNow >= NTP_SYNC_MIN_EPOCH) {
        localtime_r(&localNow, &tmn);
        int h = tmn.tm_hour;
        bool inNight = cfg.nightFrom != cfg.nightTo && ((cfg.nightFrom < cfg.nightTo)
          ? (h >= cfg.nightFrom && h < cfg.nightTo)
          : (h >= cfg.nightFrom || h < cfg.nightTo));
        if (inNight) target = 25;
      }
    }
    if (target != lastNightBright) { applyBrightness(target); lastNightBright = target; }
  }
}
