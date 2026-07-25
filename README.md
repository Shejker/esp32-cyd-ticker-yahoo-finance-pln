# ESP32 CYD Stock Ticker (Fork by Doman)

A stock and cryptocurrency price tracker for the ESP32 Cheap Yellow Display (CYD / ESP32-2432S028).  
Displays live prices, percentage change, and portfolio value on the built-in 2.8" TFT touchscreen.  
Configured entirely via a browser, no code changes required.

*Note: This is an enhanced fork created by **Doman**.*

![ESP32 CYD Stock Ticker showing 3 tickers](https://cdn.shopify.com/s/files/1/0870/0021/9940/files/20260617_125927.jpg?v=1781667798)

## Demo

[![ESP32 CYD Stock Ticker Demo](https://img.youtube.com/vi/qng6zG75FMI/maxresdefault.jpg)](https://www.youtube.com/watch?v=qng6zG75FMI)

---

## What's New in this Fork (v3.4)

This version introduces significant enhancements, full web configuration, and a refined user experience:

### 🔹 Full Web Configuration Interface
- **Complete browser-based setup** – change tickers, holdings, alerts, and display settings from any device on your network.
- **Live price table** with automatic sorting in portfolio mode.
- **JSON API endpoint** (`/api/quotes`) for home automation and external integrations.

### 🔹 Intelligent Market Status & Countdowns
- Automatic detection of European and US market opening hours.
- **Precise countdown timers** showing time until market opens (e.g., `2d 13h 30m`).
- 24/7 support for cryptocurrencies.

### 🔹 Enhanced Portfolio Management
- **Automatic sorting** of assets by total PLN value (descending).
- **Color-coded Day P&L** – green for positive, red for negative.
- Holdings and alert configuration directly from the web UI.

### 🔹 Polish & Refinement
- **Full English localization** – cleaner, consistent UI labels and time formatting.
- **Improved redirect pages** with clear feedback (`✅ Settings saved!`, `🔄 Refreshed!`).
- **Wider, centered layout** optimized for desktop and mobile browsers.
- **WiFi connection screens** with clear status and IP display.

### 🔹 Code Quality & Performance
- **Thread-safe data handling** using FreeRTOS mutexes.
- **Optimized background fetching** – no UI lag during updates.
- **Cleaner, more maintainable code** with removed legacy dependencies.

---

## Features

- Live stock, ETF, crypto, and commodity prices via Yahoo Finance (no API keys needed)
- Automatic currency conversion to PLN
- Touch to drill into detail view for any ticker
- Portfolio mode — track holdings value, day P&L, and automatic sorting by value
- Price alerts with LED flash on breach (based on converted PLN prices)
- Market open/closed status with smart countdown timers (days/hours/minutes)
- Dark and light mode
- **Full web UI for configuration** — change tickers, refresh rate, brightness, holdings and alerts from any browser
- **JSON API endpoint** at `/api/quotes` for home automation integration
- WiFiManager captive portal — no hardcoded credentials

![ESP32 CYD Stock Ticker showing 8 tickers in 2 column grid](https://cdn.shopify.com/s/files/1/0870/0021/9940/files/20260617_130053.jpg?v=1781667798)

![ESP32 CYD Stock Ticker portfolio mode](https://cdn.shopify.com/s/files/1/0870/0021/9940/files/20260617_130153.jpg?v=1781667798)

![ESP32 CYD Stock Ticker light mode](https://cdn.shopify.com/s/files/1/0870/0021/9940/files/20260617_130115.jpg?v=1781667798)

---

## Hardware

- [ESP32 CYD (ESP32-2432S028)](https://zaitronics.com.au/collections/esp32/products/esp32-with-2-8-lcd-tft-touch-screen-capacitive-wifi-bluetooth-dev-board) — everything is built in, no wiring required
- USB-C cable and power supply

## Quick Start

1. Flash the firmware
2. Connect to the `StockTicker-Setup` WiFi access point and enter your WiFi credentials
3. Open the displayed IP address in your browser to configure tickers (all assets are evaluated in PLN)

## Web Configuration

Once connected, open the displayed IP address in your browser:

### Main Page
- **Live Prices** — real-time table with price, change, value, and day P&L
- **Portfolio Summary** — total portfolio value and day P&L (color-coded)
- **Force Refresh** — manually trigger data update
- **JSON API** — raw data endpoint for automation

### Settings
- **Tickers** — comma-separated list of Yahoo Finance symbols (up to 8)
- **Refresh Interval** — fetch frequency in seconds (minimum 10s)
- **Backlight** — brightness control (10–255)
- **Dark Mode** — toggle dark/light theme
- **Portfolio Mode** — enable holdings, P&L tracking, and automatic sorting
- **Holdings & Alerts** — set shares/units and alert thresholds (PLN)

## Supported Ticker Formats

Uses standard Yahoo Finance symbols:
- US Stocks: `AAPL`, `MSFT`
- European Stocks: `ANAV.DE`, `WEBN.DE`, `ASML.AS`
- Cryptocurrencies: `BTC-USD`, `ETH-USD`
- Commodities / Forex: `GC=F` (Gold), `EURPLN=X` (EUR to PLN)

## API Endpoint

| Endpoint | Method | Description |
|----------|--------|-------------|
| `/api/quotes` | GET | Returns all ticker data as JSON (pricePLN, pct, nativePrice, isClosed, etc.) |
| `/refresh` | GET | Triggers an immediate data refresh |

## Dependencies

Install via Arduino Library Manager:

- **TFT_eSPI** — display driver
- **XPT2046_Touchscreen** — touch controller
- **ArduinoJson** — JSON parsing
- **WiFiManager** — WiFi configuration (tzapu)

Board: `ESP32 Dev Module` via ESP32 Arduino core

## TFT_eSPI Configuration

Copy `User_Setup.h` from the `/config` folder into your TFT_eSPI library folder before compiling.  
This configures the correct pins for the CYD.

## Troubleshooting

### No data showing
- Check that ticker symbols are valid on Yahoo Finance
- Ensure the device has internet access
- Try pressing **Force Refresh** in the web UI

### Web UI not accessible
- Verify the IP address displayed on the screen
- Ensure your computer is on the same network
- Check firewall settings

### Touch not responding
- Calibrate touch by pressing the four corners during boot (future version)

## License

MIT — free to use, modify, and distribute.

---

Original project built by [Zaitronics](https://zaitronics.com.au) — electronics components and maker supplies, Melbourne AU.  
Fork and modifications by **Doman**.

---
