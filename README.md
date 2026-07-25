# ESP32 CYD Stock Ticker — PLN Fork

A live stock, ETF, crypto, and commodity price tracker for the [ESP32 Cheap Yellow Display](https://zaitronics.com.au/collections/esp32/products/esp32-with-2-8-lcd-tft-touch-screen-capacitive-wifi-bluetooth-dev-board) (ESP32-2432S028).  
Displays prices and portfolio value on the built-in 2.8" touchscreen. Fully configured from a browser — no code changes needed.

> Fork of [MaWe88/esp32-cyd-ticker](https://github.com/MaWe88/esp32-cyd-ticker) with significant rewrites. See [Changes from original](#changes-from-original) below.

![ESP32 CYD Stock Ticker showing 3 tickers](https://cdn.shopify.com/s/files/1/0870/0021/9940/files/20260617_125927.jpg?v=1781667798)

## Demo

[![ESP32 CYD Stock Ticker Demo](https://img.youtube.com/vi/qng6zG75FMI/maxresdefault.jpg)](https://www.youtube.com/watch?v=qng6zG75FMI)

---

## Changes from Original

| | Original (MaWe88) | This fork |
|---|---|---|
| **Data source** | CoinGecko + Finnhub (API keys required) | Yahoo Finance (no API keys) |
| **Currency** | USD | Automatic conversion to PLN |
| **Tickers** | Up to 5 crypto + 10 stocks | Up to 8, any mix |
| **Portfolio** | — | Holdings, day P&L, auto-sort by value |
| **Market status** | — | Open/closed detection + countdown timer |
| **Touch** | — | Tap ticker → detail view |
| **Theme** | — | Dark / light mode |
| **API** | — | JSON endpoint at `/api/quotes` |
| **Architecture** | Single-threaded | FreeRTOS tasks + mutex |

---

## Features

- Live prices for stocks, ETFs, crypto, commodities, and forex via Yahoo Finance
- Automatic currency conversion to PLN for all assets
- Touch any ticker to open a detail view with sparkline
- Portfolio mode — track holdings value, day P&L, and sort by PLN value
- Price alerts with RGB LED flash (based on converted PLN price)
- Market open/closed status with countdown timers (e.g. `1d 13h 30m`)
- Dark and light mode
- Full web UI — change tickers, refresh rate, brightness, holdings, and alerts from any browser
- JSON API at `/api/quotes` for home automation
- WiFiManager captive portal — no hardcoded credentials

![ESP32 CYD Stock Ticker showing 8 tickers in 2 column grid](https://cdn.shopify.com/s/files/1/0870/0021/9940/files/20260617_130053.jpg?v=1781667798)

![ESP32 CYD Stock Ticker portfolio mode](https://cdn.shopify.com/s/files/1/0870/0021/9940/files/20260617_130153.jpg?v=1781667798)

![ESP32 CYD Stock Ticker light mode](https://cdn.shopify.com/s/files/1/0870/0021/9940/files/20260617_130115.jpg?v=1781667798)

---

## Hardware

- [ESP32 CYD (ESP32-2432S028)](https://zaitronics.com.au/collections/esp32/products/esp32-with-2-8-lcd-tft-touch-screen-capacitive-wifi-bluetooth-dev-board) — everything built in, no wiring required
- USB-C cable and power supply

## Quick Start

1. Flash the firmware via Arduino IDE
2. Connect to the `StockTicker-Setup` WiFi access point and enter your WiFi credentials
3. Open the IP address shown on the display in your browser and configure tickers

> After connecting to WiFi, the device is also reachable at **http://portfolio-tracker.local** (works on macOS, iOS, Windows 10/11).

## Web UI

Open the displayed IP in your browser. All values are shown and configured in PLN.

**Live Prices table** — price, % change, portfolio value, day P&L per ticker  
**Force Refresh** — manually trigger a data fetch  
**JSON API** — `/api/quotes` raw data for automation

**Settings:**

| Setting | Description |
|---|---|
| Tickers | Comma-separated Yahoo Finance symbols, up to 8 |
| Refresh interval | Fetch frequency in seconds (min 10s) |
| Backlight | Brightness 10–255 |
| Dark mode | Toggle dark/light theme |
| Portfolio mode | Enable holdings tracking, day P&L, and sort by value |
| Holdings & Alerts | Shares/units and alert thresholds per ticker (PLN) |

## Ticker Symbols

Standard Yahoo Finance format:

| Type | Examples |
|---|---|
| US stocks | `AAPL`, `MSFT`, `NVDA` |
| European stocks | `ANAV.DE`, `WEBN.DE`, `ASML.AS` |
| Crypto | `BTC-USD`, `ETH-USD` |
| Commodities | `GC=F` (Gold), `CL=F` (Oil) |
| Forex | `EURPLN=X`, `USDPLN=X` |

## API

| Endpoint | Method | Description |
|---|---|---|
| `/api/quotes` | GET | All ticker data as JSON — `pricePLN`, `pct`, `nativePrice`, `nativeCurrency`, `isClosed` |
| `/refresh` | GET | Triggers an immediate data fetch |

## Dependencies

Install via Arduino Library Manager:

- **TFT_eSPI** — display driver
- **XPT2046_Touchscreen** — touch controller
- **ArduinoJson** — JSON parsing
- **WiFiManager** — WiFi setup (tzapu)

Board: `ESP32 Dev Module` via ESP32 Arduino core

## TFT_eSPI Setup

Copy `User_Setup.h` from the repo root into your TFT_eSPI library folder before compiling. This configures the correct pins for the CYD.

---

## License

MIT — free to use, modify, and distribute.

Original project by [MaWe88](https://github.com/MaWe88/esp32-cyd-ticker).  
Fork and modifications by Doman.
