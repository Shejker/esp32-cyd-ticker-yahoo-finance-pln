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
| **Savings & PPK** | — | PPK, savings accounts, and other manually valued PLN positions |
| **Portfolio history** | — | Individual positions, portfolio totals, and colored comparisons from transaction dates |
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
- Savings & PPK for positions without Yahoo quotes — record new contributions/interest or an actual balance; profit is calculated automatically
- Original one-page **Tracker** interface with a separate **Charts** tab for individual positions and portfolio comparisons
- Historical price auto-fill — "Fetch" button pulls that date's closing price and same-day exchange rate for backfilling past transactions
- Edit or delete any individual transaction after the fact
- Downloadable JSON backups of transactions (`/api/transactions`, `transactions.json`) and Savings & PPK (`/api/savings-ppk`, `savings-ppk.json`) — both links download a file; keep the copies outside the device
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

Open the displayed IP in your browser. Amounts are configured in PLN. A quote with missing FX is shown in its native currency; it is never counted as PLN.

**Tracker** — the original single page keeps Live Prices, Tickers, Chart Period, Transactions, Savings & PPK, and Holdings & Alerts together. Display settings (refresh, brightness, theme, portfolio mode and night hours) open under the gear in the upper-right corner. **Total P&L** stays a plain amount. Open the separate **Charts** tab for history; there are no separate settings or transaction pages.

**Live prices table** — price, % change, portfolio value, and P&L per ticker for the selected chart period (both % change and P&L move together when you change the period). Each symbol links to its Yahoo Finance page.
**Force Refresh** — manually trigger a data fetch  
**JSON API** — `/api/quotes` raw data for automation

There are two distinct P&L figures in this app — don't confuse them:

| | Where | Meaning |
|---|---|---|
| **Period P&L** | Live Prices table, device grid & footer | Paper gain/loss from price movement over the selected Chart Period. Changes when you change the period. |
| **Holdings P&L** | Holdings & Alerts / Total P&L | Unrealized market profit/loss vs. your remaining purchase cost (average-cost method), plus reported manual P&L. Independent of Chart Period. History additionally includes realized gains from recorded sales. |

Manual-investment interest/P&L is added to the **Holdings P&L** summary, but is intentionally excluded from Yahoo's period P&L because it has no market price series.

**Transactions (cost basis)** — the source of truth for Holdings & real P&L:

- Transactions are displayed newest first across all tickers, sorted by date only. Up to 60 latest transactions are shown overall, not eight per ticker. Display sorting preserves each transaction's original edit/delete ID and does not reorder saved ledgers
- Log each buy (positive qty) or sell (negative qty) with its real date and price in PLN
- **+ Add Another** stages several transactions at once — fill in as many rows as you need, then save them all together instead of one save cycle per entry
- **Fetch** button auto-fills the price field with that date's closing price, converted using the exchange rate from the *same day* (not today's rate)
- Changing the ticker, date or manually entered price during a fetch discards the old response. Transaction IDs are stable across sorting, edits and deletions
- Saves have persisted request IDs and a state revision: retrying an unchanged request after a lost response does not duplicate a purchase or delete another transaction. Requests from stale pages are rejected; refresh before changing an uncertain transaction
- Every row shows Total PLN (qty × price) so you can see what was actually spent or received
- &#9998; edits a transaction in place; &#10005; deletes it (both update Holdings & real P&L immediately)
- **Transactions JSON (backup)** link (`/api/transactions`) always reflects the current history — save it externally so a device reflash or chip erase doesn't lose your data

**Savings & PPK** — for PPK, a Toyota Bank savings account, or any PLN position whose value is known only when you enter it:

- The table shows balance, net contributions and cumulative profit/loss; these are calculated, not monthly amounts to type manually
- Select an account's name to open its dialog, then choose an **Action**. The table has no action buttons; **+ Add Savings / PPK** is a text link
- When adding an account, choose **PPK** to name it **PPK** automatically; no Name field needs to be filled. **Savings** accounts still require a name
- **Deposit / interest** (savings): enter only this operation's deposit and newly credited net interest. A negative deposit records a withdrawal. Deposits affect capital, interest affects profit
- **Contributions** (PPK): enter only the new employee, employer and government contributions. Optionally include the institution's current valuation. Without it, the recorded balance is the last valuation plus contributions and the last known profit remains unchanged — this is not a live market valuation
- **Update valuation** (PPK) / **Update balance**: enter the actual account balance. Profit/loss is calculated as balance minus recorded net contributions. PPK contributions from employer/government are capital, not fund performance
- For a new account, start at zero. For an existing account with history, provide opening balance and total prior net contributions once. Imported Toyota balances, profit and history already provide this opening basis; existing named PPK accounts are recognized automatically
- Example: an existing PPK balance of **1194.93 PLN** with employee contributions of **669.42 PLN** and employer contributions of **502.07 PLN** is entered as **Opening balance: 1194.93**, **Previous net contributions: 1171.49**. If there were no other contributions, the resulting investment profit is **23.44 PLN**. Use the balance's actual valuation date
- These are not Yahoo tickers and do not receive automatic quotes, percentage moves, or price alerts
- A saved valuation is kept in that asset's history. The manual-series chart uses a step line, so it never pretends that interest accrued smoothly every day
- Choose **Account name and type** in the dialog to rename or change type without losing history. **Remove account** requires confirmation and removes its history. New operations must be chronological; an entirely zero-valued placeholder may start on its first actual contribution date. Same-day operations update one daily chart point
- A stale-page/version check prevents a successful deposit from being applied again after a lost response; refresh and inspect the account before retrying an uncertain save
- Balances, contributions and interest use integer grosze, including saved history. Inputs smaller than one grosz are rejected; market quantities retain their full float precision, including scientific notation for tiny crypto holdings
- Calculated P&L appears in Total P&L and Charts; use **History JSON backup** to export every retained valuation
- Account charts are available only in the **Charts** tab. Choose **Single position**, then the account under **Savings & PPK**. Account history loads before unrelated Yahoo requests; failed tickers do not block a selected account's chart or totals. An account must be saved before it appears on charts

**Charts** — open the separate tab for three views: **Total Portfolio** (every position in its own color; no extra dashed line), **Compare Positions** (toggle individual lines), and **Single Position** (choose BTC, a ticker, PPK, Toyota Bank, or another saved position). Totals remain in the summary cards. Select 1M / 3M / 6M / YTD / 1Y / ALL and point at either chart to inspect a date. YTD starts on January 1 of the latest valuation's calendar year (for example, January 1, 2026 through today); 1Y is the trailing 365 days, not the calendar year. The layout adapts to mobile screens and the tracker's light/dark theme. The position picker displays its selected checkmark on the right and supports arrow keys, Home/End, Enter and Escape.

**Tickers vs. Benchmarks** — a separate table in Charts compares **My Tickers** with [iShares Core S&P 500 UCITS ETF (SXR8.DE)](https://www.ishares.com/uk/individual/en/products/253743/ishares-core-sp-500-ucits-etf) and [Vanguard FTSE All-World UCITS ETF (VWCE.DE)](https://www.vanguard.co.uk/professional/product/etf/equity/9679/ftse-all-world-ucits-etf-usd-accumulating). Both are accumulating share classes, so the ETF share price includes reinvested fund distributions.

Both benchmark symbols are fixed in the firmware/UI and load automatically; do not add them to your holdings. ETF and PLN FX history are fetched independently. A fresh last-traded price is used for the current endpoint when Yahoo leaves the latest candle empty. Short empty-candle gaps use bounded carry-forward; long gaps remain unavailable.

- Includes all recorded ticker positions, including BTC and gold; excludes Savings and PPK. The 1M / 3M / 6M / YTD / 1Y / ALL controls beside the table and above the charts are synchronized. Position selection does not exclude tickers from the benchmark comparison
- **ALL** compares from the first recorded purchase. Shorter periods start both ETFs with your tickers' market value at the beginning of the selected range, including positions acquired earlier. Each ETF then receives the same dated PLN purchase amounts and sale withdrawals during that period, using ETF prices and same-date PLN FX rates. Transactions on the opening day are already included in the opening balance and are not applied twice. Same-day cash flows are netted before calculating fractional ETF units
- **Value** is the remaining position value. **Period P&L** = ending value − opening value − period purchases + sale withdrawals. **Period Return** = period P&L / (opening value + period purchases); neither annualized nor time-weighted. With ALL, opening value is zero and Return uses total purchases, preserving the lifetime comparison. **Portfolio Lead** = your tickers' period P&L minus the ETF's period P&L; positive means your tickers outperformed
- Quotes use the common latest sampled valuation day; ticker endpoints may include their latest live quote while ETF endpoints use available historical closes. This is a daily approximation, not a synchronized intraday comparison. Histories spanning more than two years use weekly closes and show an approximation notice
- Fractional ETF units are allowed. No extra commissions or taxes are assumed; any fees already included in recorded transaction prices remain in the cash flows. Cash dividends on your own tickers and unrecorded corporate actions are not tracked
- Missing ETF/FX prices, incomplete ticker history, or a hypothetical ETF balance too small to fund a recorded sale withdrawal show unavailable results, never invented zero returns. ETF errors can be retried separately and do not block existing position charts

The value chart includes deposits and withdrawals. The P&L chart shows cumulative profit; it includes realized gains from recorded ticker sales and unrealized gains on remaining units, and uses the cumulative P&L entered for manual investments. Summary cards show current value, cumulative P&L, the change in P&L over the visible period, and net cash contributions. History P&L can differ from the tracker's holdings P&L, which shows only unrealized market P&L plus manual P&L. Trading fees are included only if incorporated into the recorded PLN unit price; cash dividends are not tracked automatically; P&L is an amount, not a time-weighted or annualized return.

Market history uses historical closes and historical PLN exchange rates, with the quantity held on each date. A shared timeline includes up to 180 evenly spaced samples plus the exact transaction and manual-valuation days. Histories longer than two years use weekly closes. Manual histories stay constant between entered valuations. Today's market endpoint uses the latest valid quote. Data loads one position at a time with a progress indicator; failed positions can be retried, and missing historical prices create gaps instead of artificial zero values. Imported Toyota history remains saved; the one-time import form and endpoint have been removed.

**Settings:**

| Setting | Description |
|---|---|
| Tickers | Add/remove individual ticker fields dynamically, up to 8 |
| Chart Period | 1D / 5D / 1M / 3M / 6M / YTD / 1Y / 3Y / MAX — changes % change, sparkline, and Live Prices' Period P&L (real P&L in Holdings & Alerts is unaffected) |
| Refresh interval | Fetch frequency in seconds (min 10s) |
| Backlight | Brightness 10–255 |
| Dark mode | Toggle dark/light theme |
| Portfolio mode | Enable holdings tracking, P&L, and sort by value |
| Night mode | Auto-dim to brightness 25 between configurable hours (e.g. 00:00 → 08:00, midnight wrap supported). Equal hours disable dimming. |
| Holdings & Alerts | Quantity, average cost, unrealized P&L and editable alert thresholds on the original tracker page |

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

After boot, live prices start as soon as Wi-Fi and the device clock are ready. Live refreshes take priority over queued history/benchmark network requests; an already-running request finishes before yielding. Each ticker is redrawn when its quote arrives, with its PLN rate fetched immediately when needed (one attempt per currency per cycle). Working cached quotes/rates survive a failed refresh; unavailable positions still keep portfolio totals unavailable. Yahoo TLS handshakes use a 10-second timeout, TCP connects 5 seconds, and JSON bodies a 20-second total read budget in addition to the per-read timeout. Serial output at 115200 baud reports each quote/FX duration and the total refresh duration. These are separate stages, not a guaranteed ten-second limit for the entire refresh. The original display layout is unchanged.

## Price Alerts

Set alert thresholds (PLN) per ticker in **Holdings & Alerts**. When a price crosses a threshold, the RGB LED flashes yellow three times. The alert dot is visible on each grid cell:

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
| `/api/quotes` | GET | All ticker data as JSON — `sym`, `pricePLN`, `pct`, `range`, `nativePrice`, `nativeCurrency`, `heldQty`, `costBasisPLN`, `plPLN`, `lots[]` (`ts`, `qty`, `pricePLN`), `valid`, `quoteValid`, `fxValid`. Unavailable PLN values are `null`, not native-currency amounts |
| `/api/transactions` | GET | Full transaction history as JSON (`symbol`, `date`, `qty`, `pricePLN`, `totalPLN`) — download to keep a backup outside the device |
| `/api/savings-ppk` | GET | Savings & PPK JSON download with all retained valuations (`name`, `valuePLN`, `gainPLN`, `history[]`) |
| `/api/manual-investments` | GET | Legacy alias for `/api/savings-ppk`; retained for existing bookmarks/API clients |
| `/api/restore-preview` | POST | Multipart upload of exactly one JSON backup (max 64 KiB); query parameters `kind=transactions` or `kind=savings-ppk` and current `base` revision. Streams to a temporary file, validates it, and returns counts, names and a preview token without changing financial data. |
| `/api/restore` | POST | Confirm a validated preview using `restoreKind`, `restoreToken`, `base`, unique `rid`, `confirm=replace` and `format=json`. Replaces only that category; verified A/B persistence and unchanged retry IDs protect failed/lost-response writes. Preview expires after 10 minutes. |
| `/manual-action` | POST | One-account create, savings deposit/interest, PPK contributions, valuation, rename/type change or deletion. Existing accounts require `id` and `version`; browser sends `format=json`. |
| `/api/histprice` | GET | Closing price (PLN) for a ticker on/near a given date; params `lt` (symbol), `ld` (date) |
| `/api/history-job` | GET | Poll an asynchronous history request using `id`. History/price requests initially return HTTP 202 with `job`; poll until HTTP 200 or an error. The browser handles this automatically |
| `/portfolio` | GET | Interactive portfolio explorer: totals, comparisons, and individual positions |
| `/api/portfolio-position` | GET | Position/benchmark history; params `kind` (`ticker`, `manual` or `benchmark`) and `name`. Returns `ok`, `start`, `weekly`, and `points`. Market points are `[timestamp, valuePLN, cumulativeGainPLN, priceAsOf, fxAsOf]`; manual points retain the first three fields. Source timestamps are Unix seconds, or `0` when inapplicable/unavailable; missing values are `null`. A failed request includes `error` with the symbol/FX pair and HTTP/JSON failure. |
| `/refresh` | GET | Triggers an immediate data fetch |
| `/favicon.svg` | GET | Device favicon |

## Dependencies

Install via Arduino Library Manager:

- **TFT_eSPI** — display driver
- **XPT2046_Touchscreen** — touch controller
- **ArduinoJson** — JSON parsing
- **WiFiManager** — WiFi setup (tzapu)

The local WiFiManager 2.0.17 installation has a small ESP32 heap-debug fix, kept in `patches/wifimanager-2.0.17-heap-debug.patch`. It corrects unsigned printf formats, prevents truncating the largest free block to 16 bits, and guards the fragmentation calculation against division by zero and intermediate overflow. WiFi connection and portal behavior are unchanged. Arduino Library Manager updates may overwrite this local fix; if still needed, reapply the patch to `WiFiManager.cpp` in that library's directory. The host test runner checks the installed source (override its location with `WIFIMANAGER_SOURCE`).

Board: **ESP32 Dev Module**, ESP32 Arduino core **3.x**, **4 MB flash**, partition scheme **Huge APP (3 MB No OTA)**. Tested locally with core 3.3.12. The default 1.25 MB application partition is too small. Start with an upload speed of **115200** if a faster upload fails.

For updates, leave **Erase All Flash Before Sketch Upload = Disabled** to preserve saved transactions, valuations and WiFi configuration. Download both JSON backups before changing partitions or erasing flash; the firmware does not automatically restore files stored on your computer.

### Durable Storage and Upgrade Compatibility

Financial data and settings now use two CRC-verified A/B snapshots in the **existing LittleFS filesystem partition**. This avoids the 20 KB NVS limit without changing the partition scheme. A new snapshot is written and verified before an atomic rename; a write/space/rename failure returns an error and rolls back the operation in RAM. On restart, the newest valid snapshot is selected, with the previous valid snapshot as fallback.

On the first upgrade, existing NVS transactions, settings and Toyota/PPK valuations are read automatically. Legacy keys are never deleted or overwritten; the first successful save creates the new snapshot. Legacy manual-history decimal amounts are parsed directly into grosze. Quote caches are not persisted.

Only a verified completely blank filesystem partition can be initialized automatically. An existing/unmountable filesystem is **not** reformatted. If storage is unavailable, the tracker shows a read-only recovery warning and financial writes fail; download both JSON backups before any recovery or partition changes. Do not downgrade to old firmware after adding new operations: old firmware only sees the retained, older NVS copy.

## TFT_eSPI Setup

Copy `User_Setup.h` from the repo root into your TFT_eSPI library folder before compiling. This configures the correct pins for the CYD. The existing display and touchscreen configuration is unchanged.

## Validation and maintenance

- `CYDTicker.ino`: device/network/storage integration
- `investment_math.h`: shared cost-basis calculation, strict date/numeric validation and manual-history rules
- `web_ui.h`, `tracker_script.h`: tracker presentation and browser interactions
- `savings_ui.h`: savings/PPK summaries and operation dialogs with calculated previews
- `portfolio_ui.h`: history charts and position comparisons
- `persistent_store.h`: verified A/B snapshots, safe filesystem initialization and recovery
- `http_body_reader.h`: bounded chunked-HTTP decoding for streaming JSON
- `browser_requests.h`: browser request deadlines and asynchronous history polling
- `backup_ui.h`, `backup_restore.h`, `backup_json_reader.h`: settings-only restore controls, bounded file upload, exact numeric-token parsing, validated preview and atomic category replacement

The legacy NVS namespace and keys remain available as an untouched upgrade backup. The former daily aggregate snapshot recorder is no longer used: history is reconstructed from transactions and valuations, without periodic flash writes. Snapshots are written only for explicit user changes. Existing imported Toyota valuations are retained.

History is fetched by a separate worker so Yahoo requests do not block the HTTP panel or touchscreen. Concurrent Yahoo connections are serialized to limit TLS memory use. Responses are parsed as streams, including HTTP chunk framing, rather than buffered in full. A small TLS read buffer waits for delayed packets until the configured timeout; temporary gaps are not treated as the end of the JSON response. Historical FX bars have a bounded two-entry, five-minute cache shared by ticker and benchmark requests.

Live quote/FX timestamps must be no older than seven days. Historical daily prices are carried forward for at most seven days (weekends/holidays), weekly closes for at most fourteen days after the completed bar becomes available. Empty candles do not reset a known price or renew its timestamp. Longer gaps and periods before the first usable quote remain unavailable. Chart tooltips label carried quotes as “Price from …” and “FX from …”, using their original source dates. Missing/stale prices are never converted into a flat ninety-day market series.

Run host tests (Node.js plus a C++17 compiler):

```sh
node tests/run-tests.cjs
```

For interaction tests, make Playwright available to Node and run `node tests/run-tests.cjs --ui`. Set `PLAYWRIGHT_MODULE` to its module path if installed outside this project, and optionally `CHROME_BINARY` to an installed Chrome executable. Browser tests use mock data, not a live ESP32 or Yahoo connection; they check both pages, mobile layout, chart calculations and transaction retry behavior.

Historical-fetch, mutation, storage and history-job tests compile the actual firmware implementation with the real ArduinoJson library and host fixtures. They cover lost responses, stale transactions, failed persistence/rename, CRC recovery, restart round-trips, maximum history, cent-exact arithmetic, tiny quantities, chunked streaming, delayed TLS packets, transport disconnects/timeouts, bounded historical gaps and FX-cache reuse. Set `ARDUINOJSON_INCLUDE` to its `src` folder if it is not installed in `Documents/Arduino/libraries/ArduinoJson/src`; these additional tests are skipped when the library is unavailable.

Invalid dates/numbers, duplicate positions, future entries and overselling are rejected before saving. Editing or deleting a purchase is rejected if it would invalidate a later sale. Removing a ticker with saved transactions requires explicit confirmation. Partial transaction-save failures retain unsaved rows and do not retry successful ones.

Limits: eight market positions, four manual positions, 60 transactions per ticker and 60 dated valuations per manual position. Manual history keeps the newest 60 valuations; export a backup before its oldest entry ages out. History is sampled, not a tick-by-tick record. Corporate actions and cash dividends are not automatically accounted for; check affected positions against broker statements.

### Restore JSON Backups

Open **Settings → Backups → Restore Backup**, choose **Transactions** or **Savings & PPK**, select the matching exported JSON file, and click **Preview Backup**. Review the replacement counts and names, tick the confirmation, then click **Restore Backup**. Download your current backups first. The restore form stays collapsed until needed. On mobile, the file picker can select a backup saved in Downloads/Files.

Restoration replaces the entire selected category, never appends duplicates. Transaction restoration keeps the current watchlist and alerts, adds missing symbols (up to eight total), sorts each ledger chronologically while preserving same-day order, and validates that sales are covered. If current and restored symbols exceed eight, remove unused watchlist symbols before previewing. Savings & PPK restoration replaces only those accounts and their retained valuation history; transactions and display/WiFi settings are untouched. An empty array clears the selected category only, after explicit confirmation.

The server validates required fields, types, strict monetary precision, dates, chronology, limits and consistency between account balances and the last valuations. Legacy Savings & PPK exports without `kind` infer PPK from the account name. Uploads use a fixed temporary file and row-by-row JSON parsing, not a full upload copy in RAM. Truncated, oversized, interrupted or corrupt uploads cannot commit. Tokens expire after ten minutes and are invalidated by newer uploads; stale previews are rejected after another financial/settings change. Failed persistence restores the previous state. If a restore response is lost, **Retry Restore** repeats the same request ID, not a new operation; you can also refresh and inspect the data. Temporary previews do not survive a device restart.

The web panel has no authentication or HTTPS. Keep it on a trusted local network and do not expose its port to the internet.

---

## License

MIT — free to use, modify, and distribute.

Original project by [MaWe88](https://github.com/MaWe88/esp32-cyd-ticker).  
Fork and modifications by Doman.
