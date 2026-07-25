#include <WiFi.h>
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

// --- PIN CONFIGURATION ---
#define TOUCH_CS_PIN  33
#define TOUCH_IRQ_PIN 36
#define BL_PIN        21
#define LED_R          4
#define LED_G         16
#define LED_B         17

// --- APP CONSTANTS ---
static constexpr int   MAX_TICKERS       = 8;
static constexpr int   MAX_EXCHANGE_RATES= 8;
static constexpr int   MIN_REFRESH       = 10;
static constexpr int   DEFAULT_REFRESH   = 60;
static constexpr int   HEADER_H          = 26;
static constexpr int   FOOTER_H          = 18;
static constexpr int   TOUCH_DEBOUNCE_MS = 300;
static constexpr int   WIFI_CHECK_MS     = 30000;
static constexpr int   MAX_SPARK_POINTS  = 40;

// --- HARDWARE OBJECTS ---
SPIClass            touchSPI(HSPI);
XPT2046_Touchscreen touch(TOUCH_CS_PIN, TOUCH_IRQ_PIN);
TFT_eSPI            tft = TFT_eSPI();
Preferences         prefs;
WebServer           server(80);

// --- DATA STRUCTURES ---
struct Quote {
  String sym;
  float  price;
  float  pct;
  float  open;
  String currency;
  bool   valid;
  int    errors;
  bool   isClosed;
  long   timeRemaining; // Minutes to open
  
  // Sparkline data
  float  sparkline[MAX_SPARK_POINTS];
  int    sparkCount;
};

struct ExRate {
  String curr;
  float  rate;
};

// --- APP STATE (GLOBALS) ---
String tickers[MAX_TICKERS];
float  holdings[MAX_TICKERS];
float  alertHigh[MAX_TICKERS];
float  alertLow[MAX_TICKERS];
Quote  quotes[MAX_TICKERS];
ExRate exchangeRates[MAX_EXCHANGE_RATES];

int  tickerCount   = 0;
int  rateCount     = 0;
int  refreshSec    = DEFAULT_REFRESH;
int  brightness    = 200;
bool darkMode      = true;
bool portfolioMode = false;

enum ViewMode { VIEW_GRID, VIEW_DETAIL };
ViewMode viewMode  = VIEW_GRID;
int      detailIdx = 0;

// --- THREADING & TASK STATE ---
SemaphoreHandle_t dataMutex;
volatile bool     fetchPending = true;
volatile bool     fetching     = false;
unsigned long     lastFetchMillis = 0;
time_t            lastFetchTime   = 0;
unsigned long     lastTouchAction = 0;
unsigned long     lastWifiCheck   = 0;
bool              touchWasDown    = false;
static int        spinFrame       = 0;

// --- COLORS (MACROS) ---
static inline uint16_t C_BG()     { return darkMode ? TFT_BLACK   : 0xEF7D; }
static inline uint16_t C_HEADER() { return darkMode ? 0x1082      : 0x18C3; }
static inline uint16_t C_BORDER() { return darkMode ? 0x4208      : 0x8410; }
static inline uint16_t C_LABEL()  { return darkMode ? 0xAD75      : 0x4208; }
static inline uint16_t C_PANEL()  { return darkMode ? 0x0841      : 0xFFFF; }
static inline uint16_t C_TEXT()   { return darkMode ? TFT_WHITE   : TFT_BLACK; }
static inline uint16_t C_MUTED()  { return darkMode ? 0x528A      : 0x8410; }
static inline uint16_t C_UP()     { return 0x07E0; }
static inline uint16_t C_DOWN()   { return 0xF800; }
static inline uint16_t C_FLAT()   { return 0x7BEF; }
static inline uint16_t C_ALERT()  { return 0xFFE0; }

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
  if (curr == "GBP" || curr == "GBp") return "£";
  if (curr == "PLN") return "PLN ";
  return curr.length() ? curr + " " : "$";
}

String formatPrice(float price, const String &currency) {
  char buf[24];
  if      (price >= 10000) sprintf(buf, "%s%.0f", currency.c_str(), price);
  else if (price >= 1000)  sprintf(buf, "%s%.1f", currency.c_str(), price);
  else if (price >= 10)    sprintf(buf, "%s%.2f", currency.c_str(), price);
  else if (price >= 0.01f) sprintf(buf, "%s%.4f", currency.c_str(), price);
  else                     sprintf(buf, "%s%.6f", currency.c_str(), price);
  return String(buf);
}

// ==========================================
// MEMORY MANAGEMENT (PREFERENCES)
// ==========================================
void loadPrefs() {
  prefs.begin("ticker", true);
  refreshSec    = prefs.getInt("refresh",    DEFAULT_REFRESH);
  brightness    = prefs.getInt("bright",     200);
  darkMode      = prefs.getBool("dark",      true);
  portfolioMode = prefs.getBool("portfolio", false);
  tickerCount   = prefs.getInt("tcount",     0);

  for (int i = 0; i < tickerCount; i++) {
    tickers[i]   = prefs.getString(("t"  + String(i)).c_str(), "");
    holdings[i]  = prefs.getFloat( ("h"  + String(i)).c_str(), 0.0f);
    alertHigh[i] = prefs.getFloat( ("ah" + String(i)).c_str(), 0.0f);
    alertLow[i]  = prefs.getFloat( ("al" + String(i)).c_str(), 0.0f);
    
    quotes[i] = Quote{};
    quotes[i].sym = tickers[i];
    quotes[i].currency = "USD";
  }
  prefs.end();

  // Defaults for first boot
  if (tickerCount == 0) {
    const char* def[] = { "ANAV.DE", "WEBN.DE", "BTC-USD", "GC=F" };
    for (int i = 0; i < 4; i++) {
      tickers[i] = def[i];
      holdings[i] = alertHigh[i] = alertLow[i] = 0;
      quotes[i].sym = tickers[i];
      quotes[i].currency = "USD";
    }
    tickerCount = 4;
  }
}

void savePrefs() {
  prefs.begin("ticker", false);
  prefs.putInt("refresh",    refreshSec);
  prefs.putInt("bright",     brightness);
  prefs.putBool("dark",      darkMode);
  prefs.putBool("portfolio", portfolioMode);
  prefs.putInt("tcount",     tickerCount);
  
  for (int i = 0; i < tickerCount; i++) {
    prefs.putString(("t"  + String(i)).c_str(), tickers[i]);
    prefs.putFloat( ("h"  + String(i)).c_str(), holdings[i]);
    prefs.putFloat( ("ah" + String(i)).c_str(), alertHigh[i]);
    prefs.putFloat( ("al" + String(i)).c_str(), alertLow[i]);
  }
  prefs.end();
}

// ==========================================
// MARKET & NETWORK LOGIC
// ==========================================
void updateMarketStatus(Quote &q) {
  // Crypto always open
  if (q.sym.indexOf("BTC") != -1 || q.sym.indexOf("ETH") != -1 || q.sym.indexOf("-USD") != -1) {
    q.isClosed = false; q.timeRemaining = 0; return;
  }

  struct tm tm;
  if (!getLocalTime(&tm)) return;
  int wday = tm.tm_wday, mins = tm.tm_hour * 60 + tm.tm_min;

  bool euMarket = (q.sym.endsWith(".DE") || q.sym.endsWith(".PA") || q.sym.endsWith(".L") || q.sym.endsWith(".WA"));
  int openTime  = euMarket ? (9 * 60) : (15 * 60 + 30);
  int closeTime = euMarket ? (17 * 60 + 30) : (22 * 60);

  q.isClosed = false;
  int daysToOpen = 0;

  if (wday == 0) { // Sunday
    q.isClosed = true; daysToOpen = 1;
  } else if (wday == 6) { // Saturday
    q.isClosed = true; daysToOpen = 2;
  } else if (mins < openTime) { // Before open
    q.isClosed = true; daysToOpen = 0;
  } else if (mins > closeTime) { // After close
    q.isClosed = true; 
    daysToOpen = (wday == 5) ? 3 : 1; // Friday jumps to Monday
  }

  if (q.isClosed) {
    q.timeRemaining = (daysToOpen * 24 * 60) - mins + openTime; // minutes
  } else {
    q.timeRemaining = 0;
  }
}

void fetchYahoo(int idx) {
  HTTPClient http;
  http.begin("https://query1.finance.yahoo.com/v8/finance/chart/" + tickers[idx] + "?interval=15m&range=1d");
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
        float prev  = meta["chartPreviousClose"] | (meta["previousClose"] | 0.0f);
        if (price > 0) {
          q.price    = price;
          q.open     = (prev > 0) ? prev : price;
          q.pct      = (prev > 0) ? ((price - prev) / prev * 100.0f) : 0.0f;
          q.currency = String((const char*)(meta["currency"] | "USD"));
          q.currency.toUpperCase();
          updateMarketStatus(q);
          
          // Parse sparkline
          JsonArray closeArr = doc["chart"]["result"][0]["indicators"]["quote"][0]["close"];
          if (!closeArr.isNull()) {
            for (JsonVariant v : closeArr) {
              if (q.sparkCount < MAX_SPARK_POINTS) {
                if (!v.isNull()) {
                  q.sparkline[q.sparkCount++] = v.as<float>();
                }
              }
            }
          }
          
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
  http.begin("https://query1.finance.yahoo.com/v8/finance/chart/" + qCurr + "PLN=X?interval=1d&range=1d");
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
      }
    }
  }
}

void checkAlerts() {
  bool alertTriggered = false;
  xSemaphoreTake(dataMutex, portMAX_DELAY);
  for (int i = 0; i < tickerCount; i++) {
    if (!quotes[i].valid) continue;
    float rate = getRateToPLN(quotes[i].currency);
    float pricePLN = (rate > 0) ? quotes[i].price * rate : quotes[i].price;
    if ((alertHigh[i] > 0 && pricePLN >= alertHigh[i]) || (alertLow[i] > 0 && pricePLN <= alertLow[i])) {
      alertTriggered = true; break;
    }
  }
  xSemaphoreGive(dataMutex);

  if (alertTriggered) {
    for (int f = 0; f < 3; f++) {
      setLED(true, true, false); delay(150);
      setLED(false, false, false); delay(150);
    }
  }
}

// Main background fetch task
void fetchTask(void* param) {
  for (;;) {
    if (fetchPending) {
      fetchPending = false; fetching = true;
      setLED(false, false, true);

      for (int i = 0; i < tickerCount; i++) { fetchYahoo(i); vTaskDelay(pdMS_TO_TICKS(800)); }

      xSemaphoreTake(dataMutex, portMAX_DELAY);
      for (int i = 0; i < tickerCount; i++) {
        String c = quotes[i].currency;
        if (quotes[i].valid && c != "PLN" && !c.isEmpty()) {
          bool found = false;
          for (int r = 0; r < rateCount; r++) if (exchangeRates[r].curr == c) found = true;
          if (!found && rateCount < MAX_EXCHANGE_RATES) {
            exchangeRates[rateCount++] = {c, 0.0f};
          }
        }
      }
      xSemaphoreGive(dataMutex);

      for (int r = 0; r < rateCount; r++) {
        xSemaphoreTake(dataMutex, portMAX_DELAY); String c = exchangeRates[r].curr; xSemaphoreGive(dataMutex);
        float nr = fetchYahooRate(c);
        if (nr > 0) {
          xSemaphoreTake(dataMutex, portMAX_DELAY); exchangeRates[r].rate = nr; xSemaphoreGive(dataMutex);
        }
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
// UI & DRAWING
// ==========================================
int gridAreaHeight() { return 240 - HEADER_H - (portfolioMode ? FOOTER_H : 0); }

void drawHeader() {
  tft.fillRect(0, 0, 320, HEADER_H, C_HEADER());
  tft.setTextColor(TFT_WHITE, C_HEADER());
  tft.setTextDatum(ML_DATUM);
  tft.drawString("CYD PORTFOLIO", 8, HEADER_H / 2, 2);

  // WiFi Indicator
  if (WiFi.status() == WL_CONNECTED) {
    int bars = (WiFi.RSSI() > -50) ? 4 : (WiFi.RSSI() > -65) ? 3 : (WiFi.RSSI() > -80) ? 2 : 1;
    for (int b = 0; b < 4; b++) {
      tft.fillRect(272 + b * 5 + (bars == 4 ? 0 : 2), 4 + (HEADER_H - 8) - (2 + b * 3) - 2, 3, 2 + b * 3, (b < bars) ? TFT_GREEN : C_MUTED());
    }
  }

  // Last Update Time
  if (lastFetchTime > 0) {
    char buf[16]; struct tm tm; localtime_r(&lastFetchTime, &tm);
    strftime(buf, sizeof(buf), "%H:%M", &tm);
    tft.setTextDatum(MC_DATUM);
    tft.drawString(buf, 170, HEADER_H / 2, 2);
  }

  // Spinner
  const int cx = 308, cy = HEADER_H / 2;
  if (fetching) {
    const int8_t dx[] = { 0, 5, 0, -5 }, dy[] = { -5, 0, 5, 0 };
    for (int d = 0; d < 4; d++) tft.fillCircle(cx + dx[d], cy + dy[d], 2, ((d + spinFrame) % 4 == 0) ? (uint16_t)TFT_CYAN : C_MUTED());
    spinFrame++;
  } else tft.fillCircle(cx, cy, 3, TFT_GREEN);
}

void drawPortfolioFooter() {
  if (!portfolioMode) return;
  float total = 0, dayPL = 0; bool anyH = false;
  for (int i = 0; i < tickerCount; i++) {
    if (quotes[i].valid && holdings[i] > 0) {
      float r = getRateToPLN(quotes[i].currency);
      float p = (r > 0) ? quotes[i].price * r : quotes[i].price;
      float o = (r > 0) ? quotes[i].open * r : quotes[i].open;
      total += p * holdings[i];
      if (!quotes[i].isClosed) dayPL += (p - o) * holdings[i];
      anyH = true;
    }
  }
  if (!anyH) return;
  
  tft.fillRect(0, 240 - FOOTER_H, 320, FOOTER_H, C_HEADER());
  
  // Color based on dayPL
  uint16_t plColor = (dayPL >= 0) ? C_UP() : C_DOWN();
  tft.setTextColor(plColor, C_HEADER());
  tft.setTextDatum(MC_DATUM);
  tft.drawString("Portfolio PLN " + String(total, 2) + "  Day " + (dayPL>=0?"+":"") + String(dayPL, 2), 160, 240 - FOOTER_H / 2, 1);
}

void drawQuoteGrid(int idx, Quote &q) {
  int cols = (tickerCount <= 4) ? 1 : 2;
  int cellW = 320 / cols, cellH = gridAreaHeight() / ((tickerCount + cols - 1) / cols);
  int x = (idx % cols) * cellW, y = HEADER_H + (idx / cols) * cellH;

  tft.fillRect(x + 1, y + 1, cellW - 2, cellH - 2, C_PANEL());
  tft.drawRect(x, y, cellW, cellH, C_BORDER());

  if (!q.valid) {
    tft.setTextColor(C_MUTED(), C_PANEL()); tft.setTextDatum(MC_DATUM);
    tft.drawString(displaySym(q.sym) + " (err)", x + cellW/2, y + cellH/2, 2); return;
  }

  float r = getRateToPLN(q.currency); bool cv = (r > 0 && q.currency != "PLN");
  float dP = cv ? q.price * r : q.price, dO = cv ? q.open * r : q.open;
  uint16_t cPct = q.isClosed ? C_MUTED() : (q.pct > 0.05f ? C_UP() : q.pct < -0.05f ? C_DOWN() : C_FLAT());
  int lineY = (portfolioMode && holdings[idx] > 0) ? y + cellH / 3 - 2 : y + cellH / 2;

  tft.setTextDatum(ML_DATUM); tft.setTextColor(q.isClosed ? C_MUTED() : C_LABEL(), C_PANEL());
  tft.drawString(displaySym(q.sym), x + 6, lineY, 2);
  
  tft.setTextDatum(MR_DATUM); tft.setTextColor(cPct, C_PANEL());
  tft.drawString((q.pct>=0?"+":"") + String(q.pct, 2) + "%", x + cellW - 6, lineY, 2);

  tft.setTextDatum(MC_DATUM); tft.setTextColor(q.isClosed ? C_MUTED() : C_TEXT(), C_PANEL());
  tft.drawString(formatPrice(dP, cv ? "PLN " : getCurrencySymbol(q.currency)), x + cellW / 2, lineY, 2);

  if (portfolioMode && holdings[idx] > 0 && cellH >= 40) {
    float pl = (dP - dO) * holdings[idx];
    tft.setTextColor(q.isClosed ? C_MUTED() : (pl >= 0 ? C_UP() : C_DOWN()), C_PANEL());
    
    String bText = "";
    if (q.isClosed) {
      bText = "CLOSED";
    } else {
      bText = "V:" + String(dP*holdings[idx],0) + " P/L:" + (pl>=0?"+":"") + String(pl,0);
    }
    tft.drawString(bText, x + cellW / 2, y + (cellH * 2 / 3) + 4, 1);
  } else if (q.isClosed && cellH >= 40) {
    tft.setTextColor(C_MUTED(), C_PANEL());
    tft.drawString("CLOSED", x + cellW / 2, y + (cellH * 2 / 3) + 4, 1);
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
  
  tft.setTextColor(q.isClosed ? C_MUTED() : C_TEXT());
  tft.drawString(formatPrice(q.price, getCurrencySymbol(q.currency)), 160, HEADER_H + 40, 4);
  
  uint16_t cPct = q.isClosed ? C_MUTED() : (q.pct > 0 ? C_UP() : (q.pct < 0 ? C_DOWN() : C_FLAT()));
  tft.setTextColor(cPct);
  tft.drawString((q.pct >= 0 ? "+" : "") + String(q.pct, 2) + "%", 160, HEADER_H + 65, 2);

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
        int y1 = chartY + chartH - ((q.sparkline[i] - minP) * chartH) / (maxP - minP);
        int x2 = chartX + ((i + 1) * chartW) / (q.sparkCount - 1);
        int y2 = chartY + chartH - ((q.sparkline[i + 1] - minP) * chartH) / (maxP - minP);
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

  if (q.isClosed) {
    tft.setTextColor(C_MUTED());
    // Format time remaining with days if needed
    long totalMinutes = q.timeRemaining; // minutes
    long days = totalMinutes / (24 * 60);
    long hours = (totalMinutes % (24 * 60)) / 60;
    long mins = totalMinutes % 60;
    String timeStr;
    if (days > 0) {
      timeStr = String(days) + "d " + String(hours) + "h " + String(mins) + "m";
    } else if (hours > 0) {
      timeStr = String(hours) + "h " + String(mins) + "m";
    } else {
      timeStr = String(mins) + "m";
    }
    tft.drawString("Opens in: " + timeStr, 160, HEADER_H + 165, 2);
  }

  tft.setTextDatum(MR_DATUM);
  tft.setTextColor(C_MUTED());
  tft.drawString("TAP TO GO BACK", 310, 240 - 20, 2);
}

void drawAll() {
  xSemaphoreTake(dataMutex, portMAX_DELAY);
  drawHeader();
  if (viewMode == VIEW_GRID) {
    tft.fillRect(0, HEADER_H, 320, gridAreaHeight(), C_BG());
    for (int i = 0; i < tickerCount; i++) drawQuoteGrid(i, quotes[i]);
    if (portfolioMode) drawPortfolioFooter();
  } else {
    drawDetailView(detailIdx);
  }
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
      int cols = (tickerCount <= 4) ? 1 : 2;
      int idx = ((ty - HEADER_H) / (gridAreaHeight() / ((tickerCount + cols - 1) / cols))) * cols + (tx / (320 / cols));
      if (idx >= 0 && idx < tickerCount) { detailIdx = idx; viewMode = VIEW_DETAIL; drawAll(); }
    } else { 
      viewMode = VIEW_GRID; 
      drawAll(); 
    }
  }
  if (!isDown) touchWasDown = false;
}

// ==========================================
// WEB SERVER LOGIC
// ==========================================

String formatCountdown(long seconds) {
  if (seconds <= 0) return "soon";
  long hours = seconds / 3600;
  long mins = (seconds % 3600) / 60;
  if (hours > 24) {
    long days = hours / 24;
    return "in " + String(days) + "d " + String(hours % 24) + "h";
  }
  if (hours > 0) {
    return "in " + String(hours) + "h " + String(mins) + "m";
  }
  return "in " + String(mins) + "m";
}

void handleRoot() {
  // --- Synchronize data under mutex first ---
  xSemaphoreTake(dataMutex, portMAX_DELAY);
  
  // Make a local copy of all relevant data to avoid mixing indices
  String localTickers[MAX_TICKERS];
  float localHoldings[MAX_TICKERS];
  float localAlertHigh[MAX_TICKERS];
  float localAlertLow[MAX_TICKERS];
  Quote localQuotes[MAX_TICKERS];
  int localTickerCount = tickerCount;
  
  for (int i = 0; i < localTickerCount; i++) {
    localTickers[i] = tickers[i];
    localHoldings[i] = holdings[i];
    localAlertHigh[i] = alertHigh[i];
    localAlertLow[i] = alertLow[i];
    localQuotes[i] = quotes[i];
  }
  
  // Now sort the local copies if portfolio mode is enabled
  if (portfolioMode) {
    for (int i = 0; i < localTickerCount - 1; i++) {
      for (int j = 0; j < localTickerCount - i - 1; j++) {
        float vA = (localQuotes[j].valid && localHoldings[j] > 0) 
          ? (getRateToPLN(localQuotes[j].currency) > 0 ? localQuotes[j].price * getRateToPLN(localQuotes[j].currency) : localQuotes[j].price) * localHoldings[j]
          : -1.0f;
        float vB = (localQuotes[j+1].valid && localHoldings[j+1] > 0)
          ? (getRateToPLN(localQuotes[j+1].currency) > 0 ? localQuotes[j+1].price * getRateToPLN(localQuotes[j+1].currency) : localQuotes[j+1].price) * localHoldings[j+1]
          : -1.0f;
        
        if ((vB > vA) || (vA < 0 && vB < 0 && localTickers[j + 1] < localTickers[j])) {
          std::swap(localTickers[j], localTickers[j+1]);
          std::swap(localHoldings[j], localHoldings[j+1]);
          std::swap(localAlertHigh[j], localAlertHigh[j+1]);
          std::swap(localAlertLow[j], localAlertLow[j+1]);
          std::swap(localQuotes[j], localQuotes[j+1]);
        }
      }
    }
  }
  
  // Build ticker list string from local data
  String tickerList = "";
  for (int i = 0; i < localTickerCount; i++) {
    if (i) tickerList += ",";
    tickerList += localTickers[i];
  }

  // Build rows from local data
  String rows = "";
  float totalVal = 0, totalPL = 0;
  bool anyMissing = false;

  for (int i = 0; i < localTickerCount; i++) {
    String sym = localQuotes[i].sym;
    
    float rate = getRateToPLN(localQuotes[i].currency);
    bool conv = (rate > 0 && localQuotes[i].currency != "PLN");
    if (!conv && localQuotes[i].currency != "PLN" && localQuotes[i].valid) anyMissing = true;
    
    float dPrice = conv ? localQuotes[i].price * rate : localQuotes[i].price;
    float dOpen  = conv ? localQuotes[i].open * rate : localQuotes[i].open;
    String cSym  = conv ? "PLN " : getCurrencySymbol(localQuotes[i].currency);
    
    String price = localQuotes[i].valid ? cSym + String(dPrice, 2) : "--";
    String pct   = localQuotes[i].valid
                   ? (localQuotes[i].pct >= 0 ? "+" : "") + String(localQuotes[i].pct, 2) + "%" : "--";
    String clr   = localQuotes[i].valid ? (localQuotes[i].pct >= 0 ? "#00cc44" : "#ff4444") : "#888";
    String arrow = localQuotes[i].valid
                   ? (localQuotes[i].pct > 0.05f ? "&#9650;" : localQuotes[i].pct < -0.05f ? "&#9660;" : "&mdash;")
                   : "";
                   
    String valStr = "", plStr = "";
    if (localQuotes[i].valid && localHoldings[i] > 0) {
      float v = dPrice * localHoldings[i];
      float d = (dPrice - dOpen) * localHoldings[i];
      totalVal += v; totalPL += d;
      valStr = cSym + String(v, 2);
      plStr  = (d >= 0 ? "+" : "") + String(d, 2);
    }
    
    String statusInfo = "";
    if (localQuotes[i].isClosed) {
      statusInfo = " <span style='font-size:10px;color:#888'>(Closed";
      if (localQuotes[i].timeRemaining > 0) {
        statusInfo += " - opens " + formatCountdown(localQuotes[i].timeRemaining * 60);
      }
      statusInfo += ")</span>";
    }

    rows += "<tr>"
          + String("<td>") + sym + statusInfo + "</td>"
          + "<td style='font-weight:700'>" + price + "</td>"
          + "<td style='color:" + clr + "'>" + arrow + " " + pct + "</td>"
          + "<td>" + valStr + "</td>"
          + "<td style='color:" + clr + "'>" + plStr + "</td>"
          + "</tr>";
  }

  // Build holdings rows
  String holdRows = "";
  for (int i = 0; i < localTickerCount; i++) {
    holdRows += "<tr><td>" + localTickers[i] + "</td>"
      + "<td><input type='number' name='hold" + i + "' value='" + String(localHoldings[i], 4)
      + "' step='any' min='0' style='width:90px'></td>"
      + "<td><input type='number' name='ahi"  + i + "' value='" + String(localAlertHigh[i], 2)
      + "' step='any' min='0' placeholder='0=off' style='width:90px'></td>"
      + "<td><input type='number' name='alo"  + i + "' value='" + String(localAlertLow[i], 2)
      + "' step='any' min='0' placeholder='0=off' style='width:90px'></td></tr>";
  }

  // Update global tickerCount if changed
  tickerCount = localTickerCount;
  xSemaphoreGive(dataMutex);

  // --- Generate HTML ---
  const char* bg    = darkMode ? "#0a0a0f" : "#f0f0f5";
  const char* card  = darkMode ? "#13131a" : "#ffffff";
  const char* bord  = darkMode ? "#222230" : "#d0d0e0";
  const char* text  = darkMode ? "#c8ccd4" : "#1a1a2e";
  const char* muted = darkMode ? "#555"    : "#888";
  const char* inp   = darkMode ? "#1c1c28" : "#f8f8ff";
  const char* ibord = darkMode ? "#2a2a40" : "#b0b0cc";
  const char* hint  = darkMode ? "#444"    : "#999";
  String dmChk = darkMode      ? " checked" : "";
  String pmChk = portfolioMode ? " checked" : "";

  // Color for portfolio footer in HTML
  String plColor = (totalPL >= 0) ? "#00cc44" : "#ff4444";

  String html =
    "<!DOCTYPE html><html><head>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<meta charset='UTF-8'><title>Portfolio Tracker</title>"
    "<style>"
      "*{box-sizing:border-box;margin:0;padding:0}"
      "body{font-family:'SF Mono','Fira Mono',monospace;background:" + String(bg) + ";color:" + String(text) + ";padding:20px 14px;max-width:900px;margin:0 auto}"
      "h1{font-size:10px;letter-spacing:4px;text-transform:uppercase;color:" + String(muted) + ";margin-bottom:3px}"
      "h2{font-size:22px;font-weight:700;margin-bottom:18px}"
      "h3{font-size:10px;letter-spacing:3px;text-transform:uppercase;color:" + String(muted) + ";margin-bottom:10px}"
      ".card{background:" + String(card) + ";border:1px solid " + String(bord) + ";border-radius:10px;padding:16px;margin-bottom:14px;width:100%}"
      "label{display:block;font-size:11px;color:" + String(muted) + ";margin:10px 0 3px}"
      "input[type=text],input[type=password],input[type=number]{width:100%;padding:9px 11px;background:" + String(inp) + ";border:1px solid " + String(ibord) + ";color:" + String(text) + ";border-radius:6px;font-family:inherit;font-size:13px;outline:none}"
      "input:focus{border-color:#0af}"
      ".hint{font-size:10px;color:" + String(hint) + ";margin-top:3px}"
      ".row{display:flex;align-items:center;gap:8px;margin-top:10px}"
      ".row label{margin:0}"
      "input[type=checkbox]{width:16px;height:16px;accent-color:#0080ff}"
      "button{margin-top:14px;width:100%;padding:12px;background:#0080ff;color:#fff;border:none;border-radius:8px;font-size:14px;font-weight:600;font-family:inherit;cursor:pointer}"
      "button:hover{background:#0062cc}"
      "table{width:100%;border-collapse:collapse;font-size:13px}"
      "th{font-size:9px;letter-spacing:2px;text-transform:uppercase;color:" + String(muted) + ";text-align:left;padding:5px 0;border-bottom:1px solid " + String(bord) + "}"
      "td{padding:6px 4px;border-bottom:1px solid " + String(bord) + "}"
      ".meta{font-size:11px;color:" + String(muted) + ";margin-top:6px}"
      ".meta strong{color:" + String(text) + "}"
      ".meta .pl-positive{color:#00cc44;font-weight:bold}"
      ".meta .pl-negative{color:#ff4444;font-weight:bold}"
      "a{color:#0af;text-decoration:none}"
    "</style></head><body>"
    "<h1>ESP32 · CYD</h1><h2>Portfolio Tracker</h2>"

    "<div class='card'><h3>Live Prices</h3>"
    "<table><thead><tr><th>Symbol</th><th>Price (PLN)</th><th>Change</th><th>Value (PLN)</th><th>Day P&L</th></tr></thead>"
    "<tbody>" + rows + "</tbody></table>"
    + (portfolioMode && totalVal > 0
        ? "<div class='meta'>" + String(anyMissing ? "Total*: " : "Portfolio: PLN ") 
          + "<strong>" + String(totalVal, 2) + "</strong>"
          + " &nbsp; Day P&L: <span class='" + String(totalPL >= 0 ? "pl-positive" : "pl-negative") + "'>"
          + (totalPL >= 0 ? "+" : "") + String(totalPL, 2) + "</span>"
          + (anyMissing ? "<br><span style='color:#e6a23c'>* Missing currency rate. Some values may be in native currency.</span>" : "")
          + "</div>"
        : "")
    + "<div class='meta'>"
    + "<a href='/refresh'>Force Refresh</a>"
    + " &nbsp;|&nbsp; <a href='/api/quotes' target='_blank'>JSON API</a></div>"
    "</div>"

    "<form method='POST' action='/save'>"

    "<div class='card'><h3>Tickers</h3>"
      "<label>Symbols (comma-separated, up to 8)</label>"
      "<input type='text' name='tickers' value='" + tickerList + "' placeholder='ANAV.DE,WEBN.DE,BTC-USD,GC=F'>"
      "<div class='hint'><b>Note:</b> All assets are automatically evaluated and displayed in PLN.</div>"
    "</div>"

    "<div class='card'><h3>Display</h3>"
      "<label>Refresh interval (seconds)</label>"
      "<input type='number' name='refresh' min='" + String(MIN_REFRESH) + "' max='3600' value='" + String(refreshSec) + "'>"
      "<div class='hint'>Minimum " + String(MIN_REFRESH) + "s &nbsp;·&nbsp; Default " + String(DEFAULT_REFRESH) + "s</div>"
      "<label>Backlight (10–255)</label>"
      "<input type='number' name='bright' min='10' max='255' value='" + String(brightness) + "'>"
      "<div class='row'>"
        "<input type='checkbox' name='darkmode' id='dm' value='1'" + dmChk + ">"
        "<label for='dm'>Dark mode</label>"
      "</div>"
      "<div class='row'>"
        "<input type='checkbox' name='portfolio' id='pm' value='1'" + pmChk + ">"
        "<label for='pm'>Portfolio mode (value &amp; P&amp;L &amp; Sort)</label>"
      "</div>"
    "</div>"

    "<div class='card'><h3>Holdings &amp; Alerts (PLN)</h3>"
      "<table><thead><tr><th>Symbol</th><th>Shares/Units</th><th>Alert High (PLN)</th><th>Alert Low (PLN)</th></tr></thead>"
      "<tbody>" + holdRows + "</tbody></table>"
      "<div class='hint'>Set 0 to disable. Alerts now trigger based on the converted PLN price.</div>"
    "</div>"

    "<button type='submit'>&#9654; Save &amp; Apply</button>"
    "</form>"
    "<div style='height:28px'></div></body></html>";

  server.send(200, "text/html; charset=utf-8", html);
}

void handleSave() {
  // --- Parse and save data ---
  if (server.hasArg("tickers")) {
    String raw = server.arg("tickers");
    xSemaphoreTake(dataMutex, portMAX_DELAY);
    tickerCount = 0;
    int start   = 0;
    for (int i = 0; i <= (int)raw.length(); i++) {
      if (i == raw.length() || raw[i] == ',') {
        String t = raw.substring(start, i);
        t.trim();
        if (t.length() && tickerCount < MAX_TICKERS) {
          t.toUpperCase();
          tickers[tickerCount]   = t;
          holdings[tickerCount]  = 0;
          alertHigh[tickerCount] = 0;
          alertLow[tickerCount]  = 0;
          quotes[tickerCount]    = Quote{};
          quotes[tickerCount].sym      = t;
          quotes[tickerCount].currency = "USD";
          quotes[tickerCount].isClosed = false;
          quotes[tickerCount].timeRemaining = 0;
          tickerCount++;
        }
        start = i + 1;
      }
    }
    xSemaphoreGive(dataMutex);
  }

  for (int i = 0; i < tickerCount; i++) {
    if (server.hasArg("hold" + String(i))) holdings[i]  = server.arg("hold" + String(i)).toFloat();
    if (server.hasArg("ahi"  + String(i))) alertHigh[i] = server.arg("ahi"  + String(i)).toFloat();
    if (server.hasArg("alo"  + String(i))) alertLow[i]  = server.arg("alo"  + String(i)).toFloat();
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

  darkMode      = server.hasArg("darkmode");
  portfolioMode = server.hasArg("portfolio");

  savePrefs();
  fetchPending = true;
  drawAll();

  // --- Send nice redirect page ---
  const char* rbg = darkMode ? "#0a0a0f" : "#f0f0f5";
  const char* rfg = darkMode ? "#00cc44" : "#1a1a2e";
  server.send(200, "text/html; charset=utf-8",
    String("<!DOCTYPE html><html><head><meta charset='UTF-8'>")
    + "<meta http-equiv='refresh' content='2;url=/'>"
    + "<style>"
    + "body{font-family:'SF Mono',monospace;background:" + rbg + ";color:" + rfg
    + ";padding:40px;text-align:center;display:flex;align-items:center;justify-content:center;height:100vh;margin:0;flex-direction:column}"
    + "h1{font-size:48px;margin-bottom:16px}"
    + "p{font-size:20px;opacity:0.8}"
    + "</style>"
    + "</head><body>"
    + "<h1>✅</h1>"
    + "<p>Settings saved!</p>"
    + "<p style='font-size:14px;margin-top:12px;opacity:0.5'>Redirecting...</p>"
    + "</body></html>");
}

void handleApiQuotes() {
  String json = "[";
  xSemaphoreTake(dataMutex, portMAX_DELAY);
  for (int i = 0; i < tickerCount; i++) {
    float rate = getRateToPLN(quotes[i].currency);
    bool conv = (rate > 0 && quotes[i].currency != "PLN");
    float dPrice = conv ? quotes[i].price * rate : quotes[i].price;

    if (i) json += ",";
    json += "{\"sym\":\""           + quotes[i].sym + "\","
          + "\"pricePLN\":"         + String(dPrice, 4) + ","
          + "\"pct\":"              + String(quotes[i].pct, 2)   + ","
          + "\"nativePrice\":"      + String(quotes[i].price, 4) + ","
          + "\"nativeCurrency\":\"" + quotes[i].currency         + "\","
          + "\"valid\":"            + (quotes[i].valid ? "true" : "false") + ","
          + "\"isClosed\":"         + (quotes[i].isClosed ? "true" : "false") + "}";
  }
  xSemaphoreGive(dataMutex);
  json += "]";
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(200, "application/json", json);
}

void handleForceRefresh() {
  fetchPending = true;
  const char* rbg = darkMode ? "#0a0a0f" : "#f0f0f5";
  const char* rfg = darkMode ? "#00cc44" : "#1a1a2e";
  server.send(200, "text/html; charset=utf-8",
    String("<!DOCTYPE html><html><head><meta charset='UTF-8'>")
    + "<meta http-equiv='refresh' content='2;url=/'>"
    + "<style>"
    + "body{font-family:'SF Mono',monospace;background:" + rbg + ";color:" + rfg
    + ";padding:40px;text-align:center;display:flex;align-items:center;justify-content:center;height:100vh;margin:0;flex-direction:column}"
    + "h1{font-size:48px;margin-bottom:16px}"
    + "p{font-size:20px;opacity:0.8}"
    + "</style>"
    + "</head><body>"
    + "<h1>🔄</h1>"
    + "<p>Refreshed!</p>"
    + "<p style='font-size:14px;margin-top:12px;opacity:0.5'>Redirecting...</p>"
    + "</body></html>");
}

// ==========================================
// MAIN INITIALIZATION
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

  // --- WiFi start ---
  tft.fillScreen(C_BG());
  tft.setTextColor(TFT_CYAN, C_BG());
  tft.setTextDatum(MC_DATUM);
  tft.drawString("Connecting WiFi...", 160, 120, 2);

  WiFiManager wm;
  wm.setConfigPortalTimeout(120);
  wm.setAPCallback([](WiFiManager*) {
    tft.fillScreen(C_BG());
    tft.setTextColor(TFT_YELLOW, C_BG());
    tft.setTextDatum(MC_DATUM);
    tft.drawString("WiFi: PortfolioTracker-Setup", 160, 110, 2);
    tft.drawString("192.168.4.1", 160, 130, 2);
    setLED(false, true, false);
  });

  if (!wm.autoConnect("PortfolioTracker-Setup")) {
    tft.fillScreen(TFT_RED);
    tft.setTextColor(TFT_WHITE, TFT_RED);
    tft.setTextDatum(MC_DATUM);
    tft.drawString("WiFi failed! Restarting...", 160, 120, 2);
    delay(3000);
    ESP.restart();
  }

  setLED(false, true, false);

  tft.fillScreen(C_BG());
  tft.setTextColor(TFT_CYAN, C_BG());
  tft.setTextDatum(MC_DATUM);
  tft.drawString("Syncing time...", 160, 120, 2);
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  setenv("TZ", "CET-1CEST,M3.5.0/2,M10.5.0/3", 1); tzset();

  tft.fillScreen(C_BG());
  tft.setTextColor(TFT_GREEN, C_BG());
  tft.setTextDatum(MC_DATUM);
  tft.drawString("http://" + WiFi.localIP().toString(), 160, 120, 2);
  delay(2000);

  // --- Web server ---
  server.on("/",           HTTP_GET,  handleRoot);
  server.on("/save",       HTTP_POST, handleSave);
  server.on("/refresh",    HTTP_GET,  handleForceRefresh);
  server.on("/api/quotes", HTTP_GET,  handleApiQuotes);
  server.begin();

  setLED(false, false, false);

  xTaskCreatePinnedToCore(fetchTask, "fetch", 8192, NULL, 1, NULL, 0);
  lastTouchAction = millis();
}

void loop() {
  server.handleClient();
  handleTouch();

  if (!fetching && (millis() - lastFetchMillis) >= (unsigned long)refreshSec * 1000UL) fetchPending = true;

  if (millis() - lastWifiCheck > WIFI_CHECK_MS) {
    lastWifiCheck = millis();
    if (WiFi.status() != WL_CONNECTED) WiFi.reconnect();
  }

  static unsigned long lastTick = 0;
  static bool lastFetching = false;
  if (millis() - lastTick > 250) {
    lastTick = millis();
    if (fetching || lastFetching) {
      xSemaphoreTake(dataMutex, portMAX_DELAY);
      drawHeader(); 
      xSemaphoreGive(dataMutex);
    }
    if (lastFetching && !fetching) drawAll();
    lastFetching = fetching;
  }
}