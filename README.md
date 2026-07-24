# ESP32 CYD Stock Ticker (Fork by Doman)

A stock and cryptocurrency price tracker for the ESP32 Cheap Yellow Display (CYD / ESP32-2432S028).
Displays live prices, percentage change, and portfolio value on the built-in 2.8" TFT touchscreen.
Configured entirely via a browser, no code changes required.

*Note: This is an enhanced fork created by **Doman**.*

![ESP32 CYD Stock Ticker showing 3 tickers](https://cdn.shopify.com/s/files/1/0870/0021/9940/files/20260617_125927.jpg?v=1781667798)

## Demo

[![ESP32 CYD Stock Ticker Demo](https://img.youtube.com/vi/qng6zG75FMI/maxresdefault.jpg)](https://www.youtube.com/watch?v=qng6zG75FMI)

## What's Changed in this Fork (v3.4)

Compared to the original repository, this version introduces significant refactoring, architectural improvements, and new capabilities:

* **Yahoo Finance API Integration:** Replaced the multi-source setup (Finnhub & CoinGecko) with **Yahoo Finance** as the single unified backend data provider. No external API keys are required anymore!
* **Automatic Currency Conversion to PLN:** Added automatic fetching of exchange rates (e.g., USD to PLN, GBP to PLN) so that all asset prices, holdings, and portfolio valuations are dynamically evaluated and displayed in **PLN**.
* **Market Status & Countdown:** Added intelligent market opening hours detection for European and US exchanges, displaying a precise live countdown until the market reopens (with full support for 24/7 crypto assets).
* **Automated Portfolio Sorting:** In portfolio mode, items are now automatically sorted based on their total PLN value in descending order.
* **Complete English Localization:** Translated the entire user interface, code comments, UI labels, and time formatting strictly to English (including cleaner countdown texts like `in 2h 15m`).
* **Code Cleanup & Optimization:** Removed unused legacy code paths, streamlined variable structures, and optimized FreeRTOS background fetching tasks to make the code much more concise, lightweight, and maintainable.

---

## Features

- Live stock, ETF, crypto, and commodity prices via Yahoo Finance (no API keys needed)
- Automatic currency conversion to PLN
- Touch to drill into detail view for any ticker
- Portfolio mode — track holdings value, day P&L, and automatic sorting by value
- Price alerts with LED flash on breach (based on converted PLN prices)
- Market open/closed status with smart countdown timers
- Dark and light mode
- Full web UI for configuration — change tickers, refresh rate, brightness, holdings and alerts from any browser on your network
- JSON API endpoint at `/api/quotes` for home automation integration
- WiFiManager captive portal — no hardcoded credentials

![ESP32 CYD Stock Ticker showing 8 tickers in 2 column grid](https://cdn.shopify.com/s/files/1/0870/0021/9940/files/20260617_130053.jpg?v=1781667798)

![ESP32 CYD Stock Ticker portfolio mode](https://cdn.shopify.com/s/files/1/0870/0021/9940/files/20260617_130153.jpg?v=1781667798)

![ESP32 CYD Stock Ticker light mode](https://cdn.shopify.com/s/files/1/0870/0021/9940/files/20260617_130115.jpg?v=1781667798)

## Hardware

- [ESP32 CYD (ESP32-2432S028)](https://zaitronics.com.au/collections/esp32/products/esp32-with-2-8-lcd-tft-touch-screen-capacitive-wifi-bluetooth-dev-board) — everything is built in, no wiring required
- USB-C cable and power supply

## Quick Start

1. Flash the firmware
2. Connect to the `StockTicker-Setup` WiFi access point and enter your WiFi credentials
3. Open the displayed IP address in your browser to configure tickers (all assets are evaluated in PLN)

## Supported Ticker Formats

Uses standard Yahoo Finance symbols:
- US Stocks: `AAPL`, `MSFT`
- European Stocks: `ANAV.DE`, `WEBN.DE`
- Cryptocurrencies: `BTC-USD`, `ETH-USD`
- Commodities / Forex: `GC=F` (Gold), `EURPLN=X`

## Dependencies

Install via Arduino Library Manager:

- TFT_eSPI
- XPT2046_Touchscreen
- ArduinoJson
- WiFiManager (tzapu)

Board: `ESP32 Dev Module` via ESP32 Arduino core

## TFT_eSPI Configuration

Copy `User_Setup.h` from the `/config` folder into your TFT_eSPI library folder before compiling.
This configures the correct pins for the CYD.

## License

MIT — free to use, modify, and distribute.

---

Original project built by [Zaitronics](https://zaitronics.com.au) — electronics components and maker supplies, Melbourne AU.
Fork and modifications by **Doman**.