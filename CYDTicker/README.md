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
| **Portfolio** | — | Holdings, P&L, auto-sort by value |
| **Cost basis** | — | Dated buy/sell transaction log, average-cost P&L, historical price auto-fill |
| **Chart period** | — | 1D / 5D / 1M / 3M / 6M / YTD / 1Y / 3Y / MAX |
| **Touch** | — | Tap ticker → detail view with sparkline |
| **Theme** | — | Dark / light mode |
| **Night mode** | — | Auto-dim display on schedule |
| **API** | — | JSON endpoint at `/api/quotes` |
| **Architecture** | Single-threaded | FreeRTOS tasks + mutex |

---

## Features

- Live prices for stocks, ETFs, crypto, commodities, and forex via Yahoo Finance
- Automatic currency conversion to PLN for all assets
- Configurable chart period — % change, sparkline, and the Live Prices P&L column all use the selected timeframe (1D through MAX)
- Touch any ticker to open a detail view with sparkline chart; sparkline covers the full selected period
- Portfolio mode — track holdings value, period P&L, and sort by PLN value
- Transaction-based cost basis — log dated buy/sell lots in real PLN prices; real P&L (average-cost method, in Holdings & Alerts) is independent of the chart period
- Historical price auto-fill — "Fetch" button pulls that date's closing price and same-day exchange rate for backfilling past transactions
- Edit or delete any individual transaction after the fact
- Transaction history export as JSON at `/api/transactions` — a live backup that survives a device reflash
- Price alerts with RGB LED flash (based on converted PLN price)
- Night mode — automatically dims the display to brightness 25 on a configurable schedule
- Dark and light mode
- Full web UI — configure everything from any browser, mobile-friendly
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
2. Connect to the `PortfolioTracker-Setup` WiFi access point and enter your WiFi credentials
3. Open the IP address shown on the display in your browser and configure tickers

> After connecting to WiFi, the device is also reachable at **http://portfolio-tracker.local** (works on macOS, iOS, Windows 10/11).

## Web UI

Open the displayed IP in your browser. All values are shown and configured in PLN.

**Live Prices table** — price, % change, portfolio value, and P&L per ticker for the selected chart period (both % change and P&L move together when you change the period). Each symbol links to its Yahoo Finance page.  
**Force Refresh** — manually trigger a data fetch  
**JSON API** — `/api/quotes` raw data for automation

There are two distinct P&L figures in this app — don't confuse them:

| | Where | Meaning |
|---|---|---|
| **Period P&L** | Live Prices table, device grid & footer | Paper gain/loss from price movement over the selected Chart Period. Changes when you change the period. |
| **Real P&L** | Holdings & Alerts | Actual profit/loss vs. what you paid, from your Transactions (average-cost method). Independent of Chart Period. A total across all holdings is shown below the table. |

**Transactions (cost basis)** — the source of truth for Holdings & real P&L:

- Log each buy (positive qty) or sell (negative qty) with its real date and price in PLN
- **Fetch** button auto-fills the price field with that date's closing price, converted using the exchange rate from the *same day* (not today's rate)
- Every row shows Total PLN (qty × price) so you can see what was actually spent or received
- &#9998; edits a transaction in place; &#10005; deletes it (both update Holdings & real P&L immediately)
- **Transactions JSON (backup)** link (`/api/transactions`) always reflects the current history — save it externally so a device reflash or chip erase doesn't lose your data

**Settings:**

| Setting | Description |
|---|---|
| Tickers | Add/remove individual ticker fields dynamically, up to 8 |
| Chart Period | 1D / 5D / 1M / 3M / 6M / YTD / 1Y / 3Y / MAX — changes % change, sparkline, and Live Prices' Period P&L (real P&L in Holdings & Alerts is unaffected) |
| Refresh interval | Fetch frequency in seconds (min 10s) |
| Backlight | Brightness 10–255 |
| Dark mode | Toggle dark/light theme |
| Portfolio mode | Enable holdings tracking, P&L, and sort by value |
| Night mode | Auto-dim to brightness 25 between configurable hours (e.g. 00:00 → 08:00, midnight wrap supported) |
| Holdings & Alerts | Quantity, avg. cost & real P&L (read-only, derived from Transactions) plus alert thresholds per ticker (PLN) |

## Chart Periods

Chart Period affects the % change figure, the sparkline, and the Period P&L shown in Live Prices / the device grid & footer. It does **not** affect real P&L (Holdings & Alerts), which always comes from your actual Transactions.

| Period | % change / Period P&L baseline | Sparkline interval |
|---|---|---|
| 1D | Previous close (Yahoo `chartPreviousClose`) | 15 min |
| 5D | First data point in series | 30 min |
| 1M / 3M / 6M / YTD | First data point in series | 1 day |
| 1Y / 3Y | First data point in series | 1 week |
| MAX | First data point in series | 1 month |

Sparklines are subsampled to fit the display regardless of period length.

## Price Alerts

Set alert thresholds (PLN) per ticker in the Holdings & Alerts section. When a price crosses a threshold, the RGB LED flashes yellow three times. The alert dot is visible on each grid cell:

- **Filled yellow dot** — alert currently triggered
- **Outlined yellow dot** — alert set but not yet triggered

All thresholds are evaluated in PLN after currency conversion.

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
| `/api/quotes` | GET | All ticker data as JSON — `sym`, `pricePLN`, `pct`, `range`, `nativePrice`, `nativeCurrency`, `heldQty`, `costBasisPLN`, `plPLN`, `lots[]` (`ts`, `qty`, `pricePLN`), `valid` |
| `/api/transactions` | GET | Full transaction history as JSON (`symbol`, `date`, `qty`, `pricePLN`, `totalPLN`) — a live backup |
| `/api/histprice` | GET | Closing price (PLN) for a ticker on/near a given date; params `lt` (symbol), `ld` (date) |
| `/refresh` | GET | Triggers an immediate data fetch |
| `/favicon.svg` | GET | Device favicon |

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
