/*
 * ESP32 CYD Stock Ticker — v3.9 (Production Ready)
 * Board: ESP32-2432S028 ("Cheap Yellow Display")
 *
 * Displays live stock, ETF, crypto, and commodity prices in PLN on the built-in 320x240 TFT.
 * Features automated sorting by total PLN portfolio value, web configuration panel,
 * and market status/countdown based on standard market opening hours.
 */

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


// ─── Hardware pins ────────────────────────────────────────────────────────────
#define TOUCH_CS_PIN  33
#define TOUCH_IRQ_PIN 36
#define BL_PIN        21
#define LED_R          4
#define LED_G         16
#define LED_B         17


// ─── Global objects ───────────────────────────────────────────────────────────
SPIClass            touchSPI(HSPI);
XPT2046_Touchscreen touch(TOUCH_CS_PIN, TOUCH_IRQ_PIN);
TFT_eSPI            tft = TFT_eSPI();
Preferences         prefs;
WebServer           server(80);


// ─── Configuration constants ──────────────────────────────────────────────────
#define MAX_TICKERS     8
#define DEFAULT_REFRESH 60
#define MIN_REFRESH     10
#define HEADER_H        26
#define FOOTER_H        18


// ─── User settings (persisted to NVS) ────────────────────────────────────────
String tickers[MAX_TICKERS];
float  holdings[MAX_TICKERS];   
float  alertHigh[MAX_TICKERS];  
float  alertLow[MAX_TICKERS];   
int    tickerCount   = 0;
int    refreshSec    = DEFAULT_REFRESH;
int    brightness    = 200;
bool   darkMode      = true;
bool   portfolioMode = false;


// ─── Quote data ───────────────────────────────────────────────────────────────
struct Quote {
  String sym;
  float  price;
  float  pct;
  float  open;
  String currency;
  bool   valid;
  int    errors;
  bool   isClosed;
  long   timeRemaining;
};

Quote quotes[MAX_TICKERS];

// ─── Exchange Rates (Currency -> PLN) ─────────────────────────────────────────
struct ExRate {
  String curr;
  float  rate;
};
ExRate exchangeRates[8];
int    rateCount = 0;


// ─── Runtime state ────────────────────────────────────────────────────────────
SemaphoreHandle_t dataMutex;
volatile bool     fetchPending = true;
volatile bool     fetching     = false;
unsigned long     lastFetch    = 0;

enum ViewMode { VIEW_GRID, VIEW_DETAIL };
ViewMode viewMode  = VIEW_GRID;
int      detailIdx = 0;

bool          touchWasDown    = false;
unsigned long lastTouchAction = 0;
#define TOUCH_DEBOUNCE_MS 300

unsigned long lastWifiCheck = 0;
#define WIFI_CHECK_MS 30000


// ─── Theme colours ────────────────────────────────────────────────────────────
uint16_t C_BG()     { return darkMode ? (uint16_t)TFT_BLACK : (uint16_t)0xEF7D; }
uint16_t C_HEADER() { return darkMode ? (uint16_t)0x1082    : (uint16_t)0x18C3; }
uint16_t C_BORDER() { return darkMode ? (uint16_t)0x4208    : (uint16_t)0x8410; }
uint16_t C_LABEL()  { return darkMode ? (uint16_t)0xAD75    : (uint16_t)0x4208; }
uint16_t C_PANEL()  { return darkMode ? (uint16_t)0x0841    : (uint16_t)0xFFFF; }
uint16_t C_TEXT()   { return darkMode ? (uint16_t)TFT_WHITE : (uint16_t)TFT_BLACK; }
uint16_t C_MUTED()  { return darkMode ? (uint16_t)0x528A    : (uint16_t)0x8410; }

#define C_UP       0x07E0
#define C_DOWN     0xF800
#define C_FLAT     0x7BEF
#define C_ALERT    0xFFE0


// ─── LED & Display helpers ───────────────────────────────────────────────────
void setLED(bool r, bool g, bool b) {
  digitalWrite(LED_R, !r);
  digitalWrite(LED_G, !g);
  digitalWrite(LED_B, !b);
}

void applyBrightness() {
  analogWriteFrequency(5000);
  analogWrite(BL_PIN, brightness);
}

void setBrightness(int val) {
  brightness = constrain(val, 10, 255);
  applyBrightness();
}

String displaySym(const String &sym) {
  if (sym == "BTC-USD") return "BTC";
  if (sym == "GC=F" || sym == "XAUUSD=X") return "XAU";
  int dotIndex = sym.indexOf('.');
  if (dotIndex != -1) return sym.substring(0, dotIndex);
  return sym;
}

String getCurrencySymbol(const String &curr) {
  if (curr == "EUR") return "EUR ";
  if (curr == "USD") return "$";
  if (curr == "GBP" || curr == "GBp") return "£";
  if (curr == "PLN") return "PLN ";
  return curr.length() > 0 ? curr + " " : "$";
}

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


// ─── Preferences (NVS) ───────────────────────────────────────────────────────
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
  }
  prefs.end();

  if (tickerCount == 0) {
    tickers[0] = "ANAV.DE"; holdings[0] = 0; alertHigh[0] = 0; alertLow[0] = 0;
    tickers[1] = "WEBN.DE"; holdings[1] = 0; alertHigh[1] = 0; alertLow[1] = 0;
    tickers[2] = "BTC-USD"; holdings[2] = 0; alertHigh[2] = 0; alertLow[2] = 0;
    tickers[3] = "GC=F";    holdings[3] = 0; alertHigh[3] = 0; alertLow[3] = 0;
    tickerCount = 4;
  }

  for (int i = 0; i < tickerCount; i++) {
    quotes[i]               = Quote{};
    quotes[i].sym           = tickers[i];
    quotes[i].currency      = "USD";
    quotes[i].isClosed      = false;
    quotes[i].timeRemaining = 0;
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


// ─── API: Yahoo Finance & Static Hours Check ──────────────────────────────────
void updateMarketStatus(Quote &q) {
  // Cryptocurrencies are always open
  if (q.sym.indexOf("BTC") != -1 || q.sym.indexOf("ETH") != -1 || q.sym.indexOf("-USD") != -1) {
    q.isClosed = false;
    q.timeRemaining = 0;
    return;
  }

  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) return;

  int wday = timeinfo.tm_wday; // 0 = Sunday, 6 = Saturday
  int hour = timeinfo.tm_hour;
  int min  = timeinfo.tm_min;
  int totalMins = hour * 60 + min;

  // Weekend check
  if (wday == 0 || wday == 6) {
    q.isClosed = true;
    int daysUntilMon = (wday == 6) ? 2 : 1;
    long minsToOpen = (daysUntilMon * 24 * 60) - totalMins + (9 * 60);
    q.timeRemaining = minsToOpen * 60;
    return;
  }

  // European exchanges (~ 9:00 - 17:30 local time)
  if (q.sym.endsWith(".DE") || q.sym.endsWith(".PA") || q.sym.endsWith(".L") || q.sym.endsWith(".AS")) {
    if (totalMins < 9 * 60) {
      q.isClosed = true;
      q.timeRemaining = ((9 * 60) - totalMins) * 60;
    } else if (totalMins > 17 * 60 + 30) {
      q.isClosed = true;
      long minsToOpen = (24 * 60 - totalMins) + (9 * 60);
      q.timeRemaining = minsToOpen * 60;
    } else {
      q.isClosed = false;
      q.timeRemaining = ((17 * 60 + 30) - totalMins) * 60;
    }
    return;
  }

  // Default US exchanges (NYSE / NASDAQ): 9:30 - 16:00 ET = 15:30 - 22:00 CET
  if (totalMins < 15 * 60 + 30) {
    q.isClosed = true;
    q.timeRemaining = ((15 * 60 + 30) - totalMins) * 60;
  } else if (totalMins > 22 * 60) {
    q.isClosed = true;
    long minsToOpen = (24 * 60 - totalMins) + (15 * 60 + 30);
    q.timeRemaining = minsToOpen * 60;
  } else {
    q.isClosed = false;
    q.timeRemaining = ((22 * 60) - totalMins) * 60;
  }
}

void fetchYahoo(int idx) {
  String sym = tickers[idx];
  String url = "https://query1.finance.yahoo.com/v8/finance/chart/" + sym + "?interval=1d&range=1d";

  HTTPClient http;
  http.begin(url);
  http.setTimeout(8000);
  http.setUserAgent("Mozilla/5.0");

  Quote q;
  q.sym      = sym;
  q.valid    = false;
  q.errors   = quotes[idx].errors;
  q.currency = "USD";

  if (http.GET() == 200) {
    JsonDocument doc;
    if (!deserializeJson(doc, http.getString())) {
      JsonObject meta = doc["chart"]["result"][0]["meta"];
      if (!meta.isNull()) {
        float price     = meta["regularMarketPrice"] | 0.0f;
        float prevClose = meta["chartPreviousClose"] | (meta["previousClose"] | 0.0f);
        const char* curr = meta["currency"] | "USD";

        if (price > 0) {
          q.price    = price;
          q.open     = (prevClose > 0) ? prevClose : price;
          q.pct      = (prevClose > 0) ? ((price - prevClose) / prevClose * 100.0f) : 0.0f;
          q.currency = String(curr);
          q.currency.toUpperCase();
          
          updateMarketStatus(q);

          q.valid    = true;
          q.errors   = 0;
        } else {
          q.errors++;
        }
      } else {
        q.errors++;
      }
    } else {
      q.errors++;
    }
  } else {
    q.errors++;
  }
  
  http.end();

  xSemaphoreTake(dataMutex, portMAX_DELAY);
  quotes[idx] = q;
  xSemaphoreGive(dataMutex);
}

float fetchYahooRate(String curr) {
  if (curr == "PLN" || curr.length() == 0) return 1.0f;
  
  String qCurr = curr;
  float multiplier = 1.0f;
  if (curr == "GBp") { qCurr = "GBP"; multiplier = 0.01f; }

  String sym = qCurr + "PLN=X";
  String url = "https://query1.finance.yahoo.com/v8/finance/chart/" + sym + "?interval=1d&range=1d";

  HTTPClient http;
  http.begin(url);
  http.setTimeout(5000);
  http.setUserAgent("Mozilla/5.0");

  float rate = 0.0f;
  if (http.GET() == 200) {
    JsonDocument doc;
    if (!deserializeJson(doc, http.getString())) {
      rate = doc["chart"]["result"][0]["meta"]["regularMarketPrice"] | 0.0f;
    }
  }
  http.end();

  if (rate > 0) return rate * multiplier;
  return 0.0f;
}

float getRateToPLN(String curr) {
  if (curr == "PLN" || curr.length() == 0) return 1.0f;
  for (int i = 0; i < rateCount; i++) {
    if (exchangeRates[i].curr == curr) return exchangeRates[i].rate;
  }
  return 0.0f;
}


// ─── Sorting logic (Portfolio Mode) ──────────────────────────────────────────
float getTickerValuePLN(int i) {
  if (!quotes[i].valid || holdings[i] <= 0) return -1.0f;
  float rate = getRateToPLN(quotes[i].currency);
  float pricePLN = (rate > 0) ? quotes[i].price * rate : quotes[i].price;
  return pricePLN * holdings[i];
}

void sortTickersIfNeeded() {
  if (!portfolioMode) return;
  
  for (int i = 0; i < tickerCount - 1; i++) {
    for (int j = 0; j < tickerCount - i - 1; j++) {
      float valA = getTickerValuePLN(j);
      float valB = getTickerValuePLN(j + 1);
      
      bool swapNeeded = false;
      if (valB > valA) {
        swapNeeded = true;
      } else if (valA < 0 && valB < 0) {
        if (tickers[j + 1] < tickers[j]) swapNeeded = true;
      }

      if (swapNeeded) {
        String tempT = tickers[j]; tickers[j] = tickers[j + 1]; tickers[j + 1] = tempT;
        float tempH = holdings[j]; holdings[j] = holdings[j + 1]; holdings[j + 1] = tempH;
        float tempAH = alertHigh[j]; alertHigh[j] = alertHigh[j + 1]; alertHigh[j + 1] = tempAH;
        float tempAL = alertLow[j]; alertLow[j] = alertLow[j + 1]; alertLow[j + 1] = tempAL;
        Quote tempQ = quotes[j]; quotes[j] = quotes[j + 1]; quotes[j + 1] = tempQ;
      }
    }
  }
}


// ─── Price alerts ─────────────────────────────────────────────────────────────
void checkAlerts() {
  bool any = false;
  xSemaphoreTake(dataMutex, portMAX_DELAY);
  for (int i = 0; i < tickerCount; i++) {
    if (!quotes[i].valid) continue;
    float rate = getRateToPLN(quotes[i].currency);
    float currentPrice = (rate > 0) ? quotes[i].price * rate : quotes[i].price;
    
    if (alertHigh[i] > 0 && currentPrice >= alertHigh[i]) any = true;
    if (alertLow[i]  > 0 && currentPrice <= alertLow[i])  any = true;
  }
  xSemaphoreGive(dataMutex);
  
  if (any) {
    for (int f = 0; f < 3; f++) {
      digitalWrite(LED_R, LOW); digitalWrite(LED_G, LOW);
      delay(150);
      digitalWrite(LED_R, HIGH); digitalWrite(LED_G, HIGH);
      delay(150);
    }
  }
}


// ─── Fetch task (Core 0) ──────────────────────────────────────────────────────
void fetchTask(void* param) {
  for (;;) {
    if (fetchPending) {
      fetchPending = false;
      fetching     = true;
      setLED(false, false, true);

      for (int i = 0; i < tickerCount; i++) {
        fetchYahoo(i);
        vTaskDelay(pdMS_TO_TICKS(800));
      }

      xSemaphoreTake(dataMutex, portMAX_DELAY);
      for (int i = 0; i < tickerCount; i++) {
        String c = quotes[i].currency;
        if (quotes[i].valid && c != "PLN" && c.length() > 0) {
          bool found = false;
          for (int r = 0; r < rateCount; r++) {
            if (exchangeRates[r].curr == c) { found = true; break; }
          }
          if (!found && rateCount < 8) {
            exchangeRates[rateCount].curr = c;
            exchangeRates[rateCount].rate = 0.0f;
            rateCount++;
          }
        }
      }
      xSemaphoreGive(dataMutex);

      for (int r = 0; r < rateCount; r++) {
        String cToFetch;
        xSemaphoreTake(dataMutex, portMAX_DELAY);
        cToFetch = exchangeRates[r].curr;
        xSemaphoreGive(dataMutex);

        float newRate = fetchYahooRate(cToFetch);
        if (newRate > 0) {
          xSemaphoreTake(dataMutex, portMAX_DELAY);
          exchangeRates[r].rate = newRate;
          xSemaphoreGive(dataMutex);
        }
        vTaskDelay(pdMS_TO_TICKS(800));
      }

      xSemaphoreTake(dataMutex, portMAX_DELAY);
      sortTickersIfNeeded();
      xSemaphoreGive(dataMutex);

      checkAlerts();
      lastFetch = millis();
      fetching  = false;
      setLED(false, false, false);
    }
    vTaskDelay(pdMS_TO_TICKS(200));
  }
}


// ─── UI Rendering ─────────────────────────────────────────────────────────────
int gridAreaHeight() {
  return 240 - HEADER_H - (portfolioMode ? FOOTER_H : 0);
}

static int spinFrame = 0;

void drawHeaderTitle() {
  tft.fillRect(0, 0, 150, HEADER_H, C_HEADER());
  tft.setTextColor(TFT_WHITE, C_HEADER());
  tft.setTextDatum(ML_DATUM);
  tft.drawString("STOCK TICKER", 8, HEADER_H / 2, 2);
}

void drawHeaderSpinner() {
  tft.fillRect(296, 2, 22, HEADER_H - 4, C_HEADER());
  if (fetching) {
    const int cx = 308, cy = HEADER_H / 2, r = 5;
    const int8_t dx[] = { 0,  r,  0, -r };
    const int8_t dy[] = {-r,  0,  r,  0 };
    for (int d = 0; d < 4; d++) {
      uint16_t c = ((d + spinFrame) % 4 == 0) ? (uint16_t)TFT_CYAN : C_MUTED();
      tft.fillCircle(cx + dx[d], cy + dy[d], 2, c);
    }
    spinFrame++;
  }
}

void drawHeader() {
  tft.fillRect(0, 0, 320, HEADER_H, C_HEADER());
  drawHeaderTitle();
  drawHeaderSpinner();
}

void drawQuoteGrid(int idx, Quote &q) {
  int cols  = (tickerCount <= 4) ? 1 : 2;
  int rows  = (tickerCount + cols - 1) / cols;
  int cellW = 320 / cols;
  int cellH = gridAreaHeight() / rows;
  int x     = (idx % cols) * cellW;
  int y     = HEADER_H + (idx / cols) * cellH;

  tft.fillRect(x + 1, y + 1, cellW - 2, cellH - 2, C_PANEL());
  tft.drawRect(x, y, cellW, cellH, C_BORDER());

  if (!q.valid) {
    int midY = y + cellH / 2;
    tft.setTextColor(C_MUTED(), C_PANEL());
    tft.setTextDatum(ML_DATUM);
    tft.drawString(displaySym(q.sym), x + 6, midY, 2);
    tft.setTextDatum(MR_DATUM);
    char eb[12];
    sprintf(eb, q.errors ? "err:%d" : "--", q.errors);
    tft.drawString(eb, x + cellW - 6, midY, 2);
    return;
  }

  float rate = getRateToPLN(q.currency);
  bool  conv = (rate > 0 && q.currency != "PLN");
  float dPrice = conv ? q.price * rate : q.price;
  float dOpen  = conv ? q.open * rate  : q.open;
  String cSym  = conv ? "PLN " : getCurrencySymbol(q.currency);

  uint16_t pctColor = q.isClosed ? C_MUTED() : ((q.pct > 0.05f) ? C_UP : (q.pct < -0.05f) ? C_DOWN : C_FLAT);

  char priceBuf[24];
  if      (dPrice >= 10000) sprintf(priceBuf, "%s%.0f",  cSym.c_str(), dPrice);
  else if (dPrice >= 1000)  sprintf(priceBuf, "%s%.1f",  cSym.c_str(), dPrice);
  else if (dPrice >= 10)    sprintf(priceBuf, "%s%.2f",  cSym.c_str(), dPrice);
  else if (dPrice >= 0.01f) sprintf(priceBuf, "%s%.4f",  cSym.c_str(), dPrice);
  else                      sprintf(priceBuf, "%s%.6f",  cSym.c_str(), dPrice);

  char pctBuf[12];
  sprintf(pctBuf, "%+.2f%%", q.pct);

  int fnt = 2;
  String symStr = displaySym(q.sym);

  int lineY1 = portfolioMode && holdings[idx] > 0 ? y + cellH / 3 - 2 : y + cellH / 2;

  tft.setTextColor(q.isClosed ? C_MUTED() : C_LABEL(), C_PANEL());
  tft.setTextDatum(ML_DATUM);
  tft.drawString(symStr, x + 6, lineY1, fnt);

  tft.setTextColor(pctColor, C_PANEL());
  tft.setTextDatum(MR_DATUM);
  tft.drawString(pctBuf, x + cellW - 6, lineY1, fnt);

  tft.setTextColor(q.isClosed ? C_MUTED() : C_TEXT(), C_PANEL());
  tft.setTextDatum(MC_DATUM);
  tft.drawString(priceBuf, x + cellW / 2, lineY1, fnt);

  if (portfolioMode && holdings[idx] > 0 && cellH >= 40) {
    float val   = dPrice * holdings[idx];
    float dayPL = (dPrice - dOpen) * holdings[idx];
    char portBuf[32];
    
    if (q.isClosed) {
      if (val >= 1000) sprintf(portBuf, "CLOSED P/L:%+.0f", dayPL);
      else             sprintf(portBuf, "CLOSED P/L:%+.2f", dayPL);
    } else {
      if (val >= 1000) sprintf(portBuf, "W:%.0f  P/L:%+.0f", val, dayPL);
      else             sprintf(portBuf, "W:%.2f  P/L:%+.2f", val, dayPL);
    }
    
    uint16_t plColor = q.isClosed ? C_MUTED() : (dayPL >= 0 ? (uint16_t)C_UP : (uint16_t)C_DOWN);
    tft.setTextColor(plColor, C_PANEL());
    tft.setTextDatum(MC_DATUM);
    tft.drawString(portBuf, x + cellW / 2, y + (cellH * 2 / 3) + 4, 1);
  }

  bool breached = (alertHigh[idx] > 0 && dPrice >= alertHigh[idx])
               || (alertLow[idx]  > 0 && dPrice <= alertLow[idx]);
  bool alertSet = alertHigh[idx] > 0 || alertLow[idx] > 0;
  if      (breached) tft.fillCircle(x + cellW - 5, y + 5, 3, C_ALERT);
  else if (alertSet) tft.drawCircle(x + cellW - 5, y + 5, 3, C_ALERT);
}

void drawDetail(int idx, Quote &q) {
  tft.fillRect(0, HEADER_H, 320, 240 - HEADER_H, C_BG());

  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(C_MUTED(), C_BG());
  tft.drawString("TAP TO GO BACK", 10, HEADER_H + 4, 1);

  if (!q.valid) {
    tft.setTextColor(C_MUTED(), C_BG());
    tft.setTextDatum(MC_DATUM);
    tft.drawString(displaySym(q.sym) + " — no data", 160, 130, 2);
    return;
  }

  float rate = getRateToPLN(q.currency);
  bool  conv = (rate > 0 && q.currency != "PLN");
  float dPrice = conv ? q.price * rate : q.price;
  float dOpen  = conv ? q.open * rate  : q.open;
  String cSym  = conv ? "PLN " : getCurrencySymbol(q.currency);

  uint16_t pctColor = q.isClosed ? C_MUTED() : ((q.pct > 0.05f) ? C_UP : (q.pct < -0.05f) ? C_DOWN : C_FLAT);

  tft.setTextColor(C_LABEL(), C_BG());
  tft.setTextDatum(TL_DATUM);
  tft.drawString(displaySym(q.sym), 10, 42, 4);

  if (q.isClosed) {
    tft.setTextColor(C_MUTED(), C_BG());
    String closedStr = "CLOSED";
    if (q.timeRemaining > 0) {
      closedStr += " (opens " + formatCountdown(q.timeRemaining) + ")";
    }
    tft.drawString(closedStr, 10, 68, 2);
  }

  char buf[32];
  if      (dPrice >= 10000) sprintf(buf, "%s%.0f",  cSym.c_str(), dPrice);
  else if (dPrice >= 1000)  sprintf(buf, "%s%.2f",  cSym.c_str(), dPrice);
  else if (dPrice >= 10)    sprintf(buf, "%s%.2f",  cSym.c_str(), dPrice);
  else if (dPrice >= 0.01f) sprintf(buf, "%s%.4f",  cSym.c_str(), dPrice);
  else                      sprintf(buf, "%s%.6f",  cSym.c_str(), dPrice);
  
  tft.setTextColor(C_TEXT(), C_BG());
  tft.setTextDatum(TL_DATUM);
  tft.drawString(buf, 10, 84, 4);

  if (conv) {
    char nBuf[32];
    String origSym = getCurrencySymbol(q.currency);
    sprintf(nBuf, "Native: %s%.2f", origSym.c_str(), q.price);
    tft.setTextColor(C_MUTED(), C_BG());
    tft.drawString(nBuf, 10, 122, 1);
  }

  sprintf(buf, "%+.2f%%", q.pct);
  tft.setTextColor(pctColor, C_BG());
  tft.setTextDatum(TR_DATUM);
  tft.drawString(buf, 312, 42, 4);

  tft.setTextColor(C_MUTED(), C_BG());
  tft.setTextDatum(TL_DATUM);
  sprintf(buf, "Open: %s%.2f", cSym.c_str(), dOpen);
  tft.drawString(buf, 10, 148, 2);

  if (portfolioMode && holdings[idx] > 0) {
    float val   = dPrice * holdings[idx];
    float dayPL = (dPrice - dOpen) * holdings[idx];
    char plBuf[48];
    sprintf(plBuf, "Held: %s%.2f  Day: %+.2f", cSym.c_str(), val, dayPL);
    tft.setTextColor(dayPL >= 0 ? (uint16_t)C_UP : (uint16_t)C_DOWN, C_BG());
    tft.drawString(plBuf, 10, 172, 1);
  }

  if (alertHigh[idx] > 0 || alertLow[idx] > 0) {
    String aStr = "Alert: ";
    char ab[28];
    if (alertHigh[idx] > 0) { sprintf(ab, "H>%s%.2f ", cSym.c_str(), alertHigh[idx]); aStr += ab; }
    if (alertLow[idx]  > 0) { sprintf(ab, "L<%s%.2f",  cSym.c_str(), alertLow[idx]);  aStr += ab; }
    bool br = (alertHigh[idx] > 0 && dPrice >= alertHigh[idx])
           || (alertLow[idx]  > 0 && dPrice <= alertLow[idx]);
    tft.setTextColor(br ? (uint16_t)C_ALERT : C_MUTED(), C_BG());
    tft.drawString(aStr, 10, 194, 1);
  }
}

void drawPortfolioFooter() {
  if (!portfolioMode) return;
  float total = 0, dayPL = 0;
  bool  anyH  = false;
  bool  anyMissing = false;

  for (int i = 0; i < tickerCount; i++) {
    if (quotes[i].valid && holdings[i] > 0) {
      float rate = getRateToPLN(quotes[i].currency);
      if (rate <= 0 && quotes[i].currency != "PLN") anyMissing = true;
      
      float p = (rate > 0) ? quotes[i].price * rate : quotes[i].price;
      float o = (rate > 0) ? quotes[i].open * rate  : quotes[i].open;
      
      total += p * holdings[i];
      dayPL += (p - o) * holdings[i];
      anyH   = true;
    }
  }
  if (!anyH) return;

  tft.fillRect(0, 240 - FOOTER_H, 320, FOOTER_H, C_HEADER());
  char buf[64];
  if (anyMissing) {
    sprintf(buf, "Total*  %.2f  Day %+.2f", total, dayPL);
  } else {
    sprintf(buf, "Portfolio PLN %.2f  Day %+.2f", total, dayPL);
  }
  tft.setTextColor(dayPL >= 0 ? (uint16_t)C_UP : (uint16_t)C_DOWN, C_HEADER());
  tft.setTextDatum(MC_DATUM);
  tft.drawString(buf, 160, 240 - FOOTER_H / 2, 1);
}

void drawAll() {
  xSemaphoreTake(dataMutex, portMAX_DELAY);
  drawHeader();
  if (viewMode == VIEW_GRID) {
    tft.fillRect(0, HEADER_H, 320, gridAreaHeight(), C_BG());
    for (int i = 0; i < tickerCount; i++) drawQuoteGrid(i, quotes[i]);
    if (portfolioMode) drawPortfolioFooter();
  } else {
    drawDetail(detailIdx, quotes[detailIdx]);
  }
  xSemaphoreGive(dataMutex);
}


// ─── Touch Input ─────────────────────────────────────────────────────────────
void handleTouch() {
  bool isDown = touch.tirqTouched() && touch.touched();

  if (isDown && !touchWasDown) {
    touchWasDown = true;
    unsigned long now = millis();
    if (now - lastTouchAction < TOUCH_DEBOUNCE_MS) return;
    lastTouchAction = now;

    TS_Point p = touch.getPoint();
    int tx = map(p.x, 200, 3800, 0, 320);
    int ty = map(p.y, 200, 3800, 0, 240);

    if (viewMode == VIEW_GRID) {
      int cols  = (tickerCount <= 4) ? 1 : 2;
      int rows  = (tickerCount + cols - 1) / cols;
      int cellW = 320 / cols;
      int cellH = gridAreaHeight() / rows;
      int col   = tx / cellW;
      int row   = (ty - HEADER_H) / cellH;
      int idx   = row * cols + col;
      if (idx >= 0 && idx < tickerCount) {
        detailIdx = idx;
        viewMode  = VIEW_DETAIL;
        drawAll();
      }
    } else {
      viewMode = VIEW_GRID;
      drawAll();
    }
  }
  if (!isDown) touchWasDown = false;
}


// ─── Web UI ──────────────────────────────────────────────────────────────────
void handleRoot() {
  String tickerList = "";
  for (int i = 0; i < tickerCount; i++) {
    if (i) tickerList += ",";
    tickerList += tickers[i];
  }

  String rows = "";
  xSemaphoreTake(dataMutex, portMAX_DELAY);
  
  sortTickersIfNeeded();

  float totalVal = 0, totalPL = 0;
  bool anyMissing = false;

  for (int i = 0; i < tickerCount; i++) {
    String sym   = quotes[i].sym;
    
    float rate = getRateToPLN(quotes[i].currency);
    bool conv = (rate > 0 && quotes[i].currency != "PLN");
    if (!conv && quotes[i].currency != "PLN" && quotes[i].valid) anyMissing = true;
    
    float dPrice = conv ? quotes[i].price * rate : quotes[i].price;
    float dOpen  = conv ? quotes[i].open * rate : quotes[i].open;
    String cSym  = conv ? "PLN " : getCurrencySymbol(quotes[i].currency);
    
    String price = quotes[i].valid ? cSym + String(dPrice, 2) : "--";
    String pct   = quotes[i].valid
                   ? (quotes[i].pct >= 0 ? "+" : "") + String(quotes[i].pct, 2) + "%" : "--";
    String clr   = quotes[i].valid ? (quotes[i].pct >= 0 ? "#00cc44" : "#ff4444") : "#888";
    String arrow = quotes[i].valid
                   ? (quotes[i].pct > 0.05f ? "&#9650;" : quotes[i].pct < -0.05f ? "&#9660;" : "&mdash;")
                   : "";
                   
    String valStr = "", plStr = "";
    if (quotes[i].valid && holdings[i] > 0) {
      float v = dPrice * holdings[i];
      float d = (dPrice - dOpen) * holdings[i];
      totalVal += v; totalPL += d;
      valStr = cSym + String(v, 2);
      plStr  = (d >= 0 ? "+" : "") + String(d, 2);
    }
    
    String statusInfo = "";
    if (quotes[i].isClosed) {
      statusInfo = " <span style='font-size:10px;color:#888'>(Closed";
      if (quotes[i].timeRemaining > 0) {
        statusInfo += " - opens " + formatCountdown(quotes[i].timeRemaining);
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
  xSemaphoreGive(dataMutex);

  String holdRows = "";
  for (int i = 0; i < tickerCount; i++) {
    holdRows += "<tr><td>" + tickers[i] + "</td>"
      + "<td><input type='number' name='hold" + i + "' value='" + String(holdings[i], 4)
      + "' step='any' min='0' style='width:90px'></td>"
      + "<td><input type='number' name='ahi"  + i + "' value='" + String(alertHigh[i], 2)
      + "' step='any' min='0' placeholder='0=off' style='width:90px'></td>"
      + "<td><input type='number' name='alo"  + i + "' value='" + String(alertLow[i], 2)
      + "' step='any' min='0' placeholder='0=off' style='width:90px'></td></tr>";
  }

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

  String html =
    "<!DOCTYPE html><html><head>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<meta charset='UTF-8'><title>Stock Ticker</title>"
    "<style>"
      "*{box-sizing:border-box;margin:0;padding:0}"
      "body{font-family:'SF Mono','Fira Mono',monospace;background:" + String(bg) + ";color:" + String(text) + ";padding:20px 14px}"
      "h1{font-size:10px;letter-spacing:4px;text-transform:uppercase;color:" + String(muted) + ";margin-bottom:3px}"
      "h2{font-size:22px;font-weight:700;margin-bottom:18px}"
      "h3{font-size:10px;letter-spacing:3px;text-transform:uppercase;color:" + String(muted) + ";margin-bottom:10px}"
      ".card{background:" + String(card) + ";border:1px solid " + String(bord) + ";border-radius:10px;padding:16px;margin-bottom:14px;max-width:500px}"
      "label{display:block;font-size:11px;color:" + String(muted) + ";margin:10px 0 3px}"
      "input[type=text],input[type=password],input[type=number]{width:100%;padding:9px 11px;background:" + String(inp) + ";border:1px solid " + String(ibord) + ";color:" + String(text) + ";border-radius:6px;font-family:inherit;font-size:13px;outline:none}"
      "input:focus{border-color:#0af}"
      ".hint{font-size:10px;color:" + String(hint) + ";margin-top:3px}"
      ".row{display:flex;align-items:center;gap:8px;margin-top:10px}"
      ".row label{margin:0}"
      "input[type=checkbox]{width:16px;height:16px;accent-color:#0080ff}"
      "button{margin-top:14px;width:100%;max-width:500px;padding:12px;background:#0080ff;color:#fff;border:none;border-radius:8px;font-size:14px;font-weight:600;font-family:inherit;cursor:pointer}"
      "button:hover{background:#0062cc}"
      "table{width:100%;border-collapse:collapse;font-size:13px}"
      "th{font-size:9px;letter-spacing:2px;text-transform:uppercase;color:" + String(muted) + ";text-align:left;padding:5px 0;border-bottom:1px solid " + String(bord) + "}"
      "td{padding:6px 4px;border-bottom:1px solid " + String(bord) + "}"
      ".meta{font-size:10px;color:" + String(muted) + ";margin-top:6px}"
      "a{color:#0af;text-decoration:none}"
    "</style></head><body>"
    "<h1>ESP32 · CYD</h1><h2>Stock Ticker</h2>"

    "<div class='card'><h3>Live Prices</h3>"
    "<table><thead><tr><th>Symbol</th><th>Price (PLN)</th><th>Change</th><th>Value (PLN)</th><th>Day P&L</th></tr></thead>"
    "<tbody>" + rows + "</tbody></table>"
    + (portfolioMode && totalVal > 0
        ? "<div class='meta'>" + String(anyMissing ? "Total*: " : "Portfolio: PLN ") + String(totalVal, 2)
          + " &nbsp; Day P&L: " + (totalPL >= 0 ? "+" : "") + String(totalPL, 2)
          + (anyMissing ? "<br><span style='color:#e6a23c'>* Missing currency rate. Some values may be in native currency.</span>" : "")
          + "</div>"
        : "")
    + "<div class='meta'>"
    + "<a href='/refresh'>Force refresh</a>"
    + " &nbsp;|&nbsp; <a href='/api/quotes'>JSON API</a></div>"
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
  if (server.hasArg("bright")) setBrightness(server.arg("bright").toInt());

  darkMode      = server.hasArg("darkmode");
  portfolioMode = server.hasArg("portfolio");

  savePrefs();
  fetchPending = true;
  drawAll();

  const char* rbg = darkMode ? "#0a0a0f" : "#f0f0f5";
  const char* rfg = darkMode ? "#00cc44" : "#1a1a2e";
  server.send(200, "text/html; charset=utf-8",
    String("<!DOCTYPE html><html><head><meta charset='UTF-8'>")
    + "<meta http-equiv='refresh' content='2;url=/'>"
    + "<style>body{font-family:monospace;background:" + rbg + ";color:" + rfg
    + ";padding:40px;text-align:center}</style>"
    + "</head><body><p>&#10003; Saved.</p></body></html>");
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
  server.send(200, "text/plain", "ok");
}


// ─── Setup ────────────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(300);

  pinMode(LED_R, OUTPUT); pinMode(LED_G, OUTPUT); pinMode(LED_B, OUTPUT);
  setLED(true, false, false);
  pinMode(BL_PIN, OUTPUT);
  digitalWrite(BL_PIN, HIGH);

  tft.init();
  tft.setRotation(3);
  tft.fillScreen(TFT_BLACK);

  touchSPI.begin(25, 39, 32, TOUCH_CS_PIN);
  touch.begin(touchSPI);
  touch.setRotation(3);

  loadPrefs();
  applyBrightness();

  tft.fillScreen(C_BG());
  tft.setTextColor(TFT_CYAN, C_BG());
  tft.setTextDatum(MC_DATUM);
  tft.drawString("Connecting WiFi...", 160, 120, 2);

  dataMutex = xSemaphoreCreateMutex();

  WiFiManager wm;
  wm.setConfigPortalTimeout(120);
  wm.setAPCallback([](WiFiManager*) {
    tft.fillScreen(C_BG());
    tft.setTextColor(TFT_YELLOW, C_BG());
    tft.setTextDatum(MC_DATUM);
    tft.drawString("WiFi: StockTicker-Setup", 160, 110, 2);
    tft.drawString("192.168.4.1", 160, 130, 2);
    setLED(false, true, false);
  });

  if (!wm.autoConnect("StockTicker-Setup")) {
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
  configTime(3600, 3600, "pool.ntp.org", "time.nist.gov");

  tft.fillScreen(C_BG());
  tft.setTextColor(TFT_GREEN, C_BG());
  tft.setTextDatum(MC_DATUM);
  tft.drawString("http://" + WiFi.localIP().toString(), 160, 120, 2);
  delay(2000);

  server.on("/",           HTTP_GET,  handleRoot);
  server.on("/save",       HTTP_POST, handleSave);
  server.on("/refresh",    HTTP_GET,  handleForceRefresh);
  server.on("/api/quotes", HTTP_GET,  handleApiQuotes);
  server.begin();

  setLED(false, false, false);

  xTaskCreatePinnedToCore(fetchTask, "fetch", 8192, NULL, 1, NULL, 0);
  fetchPending = true;
}


// ─── Main Loop ────────────────────────────────────────────────────────────────
void loop() {
  server.handleClient();
  handleTouch();

  if (!fetching && (millis() - lastFetch) >= (unsigned long)refreshSec * 1000UL)
    fetchPending = true;

  if (millis() - lastWifiCheck > WIFI_CHECK_MS) {
    lastWifiCheck = millis();
    if (WiFi.status() != WL_CONNECTED) {
      setLED(true, false, false);
      WiFi.reconnect();
    }
  }

  static unsigned long lastTick     = 0;
  static bool          lastFetching = false;

  if (millis() - lastTick > 250) {
    lastTick = millis();

    if (fetching || lastFetching) {
      xSemaphoreTake(dataMutex, portMAX_DELAY);
      drawHeaderSpinner();
      xSemaphoreGive(dataMutex);
    }

    if (lastFetching && !fetching) {
      drawAll();
    }

    lastFetching = fetching;
  }
}