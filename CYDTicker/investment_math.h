#pragma once
#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <limits>
#include <cstdint>
#include <cstdio>

// Monetary account balances are exact integer grosze, including in history.
struct Money {
  int64_t cents = 0;
  static constexpr int64_t MAX_CENTS = 1000000000000000LL;
  Money() = default;
  Money(double pln) : cents(std::isfinite(pln) && std::abs(pln) <= MAX_CENTS / 100.0
    ? std::llround(pln * 100.0) : MAX_CENTS + 1) {}
  operator double() const { return cents >= -MAX_CENTS && cents <= MAX_CENTS ? cents / 100.0 : NAN; }
  static Money fromCents(int64_t n) { Money m; m.cents = n; return m; }
};

inline bool parseMoney(const char* s, Money& out) {
  if (!s || !*s) return false;
  bool negative = *s == '-'; if (*s == '-' || *s == '+') ++s;
  uint64_t digits = 0; int fractional = 0; bool dot = false, any = false;
  while (*s && *s != 'e' && *s != 'E') {
    if (*s == '.' && !dot) { dot = true; ++s; continue; }
    if (*s < '0' || *s > '9' || digits > (UINT64_MAX - (*s - '0')) / 10) return false;
    any = true; digits = digits * 10 + *s++ - '0'; if (dot && ++fractional > 32) return false;
  }
  if (!any) return false;
  int exponent = 0;
  if (*s) {
    ++s; bool minus = *s == '-'; if (*s == '-' || *s == '+') ++s;
    if (!*s) return false;
    while (*s) { if (*s < '0' || *s > '9' || exponent > 32) return false; exponent = exponent * 10 + *s++ - '0'; }
    if (exponent > 32) return false; if (minus) exponent = -exponent;
  }
  int shift = 2 + exponent - fractional;
  while (shift < 0) { if (digits % 10) return false; digits /= 10; ++shift; }
  while (shift > 0) { if (digits > (uint64_t)Money::MAX_CENTS / 10) return false; digits *= 10; --shift; }
  if (digits > (uint64_t)Money::MAX_CENTS) return false;
  out = Money::fromCents(negative ? -(int64_t)digits : (int64_t)digits); return true;
}

inline bool freshTimestamp(time_t stamp, time_t now, time_t maxAge) {
  return stamp > 0 && stamp <= now + 300 && now - stamp <= maxAge;
}

// Nine significant digits round-trip every finite float, including tiny crypto.
inline void formatQuantity(float n, char (&text)[32]) { std::snprintf(text, sizeof(text), "%.9g", (double)n); }

// Backups must preserve every grosz, including at the supported upper limit.
// Printing through double/ArduinoJson can discard cents for very large values.
inline void formatMoney(Money money,char (&text)[32]) {
  if(money.cents < -Money::MAX_CENTS || money.cents > Money::MAX_CENTS){std::snprintf(text,sizeof(text),"null");return;}
  uint64_t magnitude=static_cast<uint64_t>(money.cents<0?-money.cents:money.cents);
  std::snprintf(text,sizeof(text),"%s%llu.%02llu",money.cents<0?"-":"",
    static_cast<unsigned long long>(magnitude/100),static_cast<unsigned long long>(magnitude%100));
}

// Platform-independent ledger rules shared by live prices and history charts.
struct Lot { time_t ts; float qty; float pricePLN; uint32_t id = 0; };
struct ManualSnapshot { time_t ts; Money valuePLN; Money gainPLN; };

struct TransactionRef { int ticker; int lot; time_t ts; };

// Sort display references only: original lot IDs and chronological ledgers stay intact.
template<class Ticker>
int newestTransactions(const Ticker* tickers, int count, TransactionRef* refs, int capacity) {
  int n = 0;
  for (int i = 0; i < count; ++i)
    for (int k = 0; k < tickers[i].lotCount && n < capacity; ++k)
      refs[n++] = {i, k, tickers[i].lots[k].ts};
  std::stable_sort(refs, refs + n, [](const TransactionRef& a, const TransactionRef& b) {
    return a.ts > b.ts;
  });
  return n;
}

inline bool savingsUpdate(Money oldValue, Money oldGain, Money deposit, Money interest,
                          Money& value, Money& gain) {
  if (!std::isfinite((double)oldValue) || !std::isfinite((double)oldGain)
      || !std::isfinite((double)deposit) || !std::isfinite((double)interest)) return false;
  int64_t nextValue = oldValue.cents + deposit.cents + interest.cents;
  int64_t nextGain = oldGain.cents + interest.cents;
  if (interest.cents < 0 || nextValue < 0 || nextValue > Money::MAX_CENTS
      || std::abs(nextGain) > Money::MAX_CENTS) return false;
  value = Money::fromCents(nextValue); gain = Money::fromCents(nextGain);
  return true;
}

inline bool ppkUpdate(Money oldValue, Money oldGain, Money employee, Money employer,
                      Money state, bool haveValuation, Money reportedValue,
                      Money& value, Money& gain) {
  if (!std::isfinite((double)oldValue) || !std::isfinite((double)oldGain)
      || !std::isfinite((double)employee) || !std::isfinite((double)employer)
      || !std::isfinite((double)state) || (haveValuation && !std::isfinite((double)reportedValue))) return false;
  if (employee.cents < 0 || employer.cents < 0 || state.cents < 0) return false;
  int64_t contributions = employee.cents + employer.cents + state.cents;
  int64_t nextValue = haveValuation ? reportedValue.cents : oldValue.cents + contributions;
  int64_t nextGain = haveValuation ? nextValue - (oldValue.cents - oldGain.cents + contributions) : oldGain.cents;
  if (nextValue < 0 || nextValue > Money::MAX_CENTS || std::abs(nextGain) > Money::MAX_CENTS) return false;
  value = Money::fromCents(nextValue); gain = Money::fromCents(nextGain);
  return true;
}

inline bool isSameLocalDay(time_t a, time_t b) {
  if (a <= 0 || b <= 0) return false;
  tm x{}, y{};
  localtime_r(&a, &x); localtime_r(&b, &y);
  return x.tm_year == y.tm_year && x.tm_yday == y.tm_yday;
}

inline time_t historyDayEnd(time_t ts) {
  tm local{};
  localtime_r(&ts, &local);
  local.tm_hour = 23; local.tm_min = 59; local.tm_sec = 59;
  local.tm_isdst = -1;
  return mktime(&local);
}

// Reject malformed/normalized dates instead of silently recording them today.
inline time_t parseCalendarDate(const char* s) {
  if (!s || std::strlen(s) != 10 || s[4] != '-' || s[7] != '-') return 0;
  for (int i = 0; i < 10; ++i)
    if (i != 4 && i != 7 && (s[i] < '0' || s[i] > '9')) return 0;
  int y = (s[0]-'0')*1000+(s[1]-'0')*100+(s[2]-'0')*10+s[3]-'0';
  int m = (s[5]-'0')*10+s[6]-'0', d = (s[8]-'0')*10+s[9]-'0';
  if (y < 2000 || m < 1 || m > 12 || d < 1 || d > 31) return 0;
  tm local{};
  local.tm_year = y-1900; local.tm_mon = m-1; local.tm_mday = d;
  local.tm_hour = 12; local.tm_isdst = -1;
  time_t ts = mktime(&local);
  return ts > 0 && local.tm_year == y-1900 && local.tm_mon == m-1
    && local.tm_mday == d ? ts : 0;
}

inline bool parseFiniteNumber(const char* s, float& out) {
  if (!s || !*s) return false;
  char* end = nullptr;
  errno = 0;
  float value = std::strtof(s, &end);
  if (end == s || *end || errno == ERANGE || !std::isfinite(value)) return false;
  out = value;
  return true;
}

inline void calculateLedger(const Lot* lots, int count, time_t at,
                            float& qty, double& cost, double& realized) {
  qty = 0; cost = 0; realized = 0;
  for (int k = 0; k < count && lots[k].ts <= at; ++k) {
    const Lot& lot = lots[k];
    if (lot.qty >= 0) {
      qty += lot.qty;
      cost += static_cast<double>(lot.qty) * lot.pricePLN;
    } else {
      float sold = std::min(-lot.qty, qty);
      double average = qty > 0 ? cost / qty : 0;
      realized += sold * (static_cast<double>(lot.pricePLN) - average);
      cost = std::max(0.0, cost - average * sold);
      qty = std::max(0.0f, qty - sold);
    }
  }
}

inline bool validLotSequence(const Lot* lots, int count) {
  double qty = 0;
  time_t previous = 0;
  for (int i = 0; i < count; ++i) {
    if (lots[i].ts <= 0 || lots[i].ts < previous || !std::isfinite(lots[i].qty)
        || !std::isfinite(lots[i].pricePLN) || lots[i].qty == 0 || lots[i].pricePLN < 0) return false;
    double before = qty;
    qty += lots[i].qty;
    // A relative tolerance absorbs float rounding but never permits a small
    // crypto sale from an empty position merely because its units are tiny.
    double tolerance = std::numeric_limits<float>::epsilon()
      * std::max(std::abs(before), std::abs(static_cast<double>(lots[i].qty))) * 2;
    if (qty < -tolerance) return false;
    if (qty < 0) qty = 0;
    previous = lots[i].ts;
  }
  return true;
}

template<class Asset>
bool recordManualValuation(Asset& asset, time_t ts, Money value, Money gain) {
  if (ts <= 0 || !std::isfinite(value) || !std::isfinite(gain) || value < 0) return false;
  int n = asset.historyCount;
  const int capacity = sizeof(asset.history) / sizeof(asset.history[0]);
  int pos = 0;
  while (pos < n && asset.history[pos].ts < ts) ++pos;
  for (int i = 0; i < n; ++i) {
    if (isSameLocalDay(asset.history[i].ts, ts)) {
      asset.history[i] = {ts, value, gain};
      asset.valuePLN = asset.history[n-1].valuePLN;
      asset.gainPLN = asset.history[n-1].gainPLN;
      return true;
    }
  }
  if (n == capacity) {
    if (pos == 0) return false; // do not displace newer history with an older entry
    for (int i = 1; i < n; ++i) asset.history[i-1] = asset.history[i];
    --n; --pos;
  }
  for (int i = n; i > pos; --i) asset.history[i] = asset.history[i-1];
  asset.history[pos] = {ts, value, gain};
  asset.historyCount = n+1;
  asset.valuePLN = asset.history[n].valuePLN;
  asset.gainPLN = asset.history[n].gainPLN;
  return true;
}
