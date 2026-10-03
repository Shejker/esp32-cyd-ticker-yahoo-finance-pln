#pragma once
#include <ArduinoJson.h>
#include <LittleFS.h>
#include "backup_json_reader.h"

// One bounded, temporary upload. It is never used as a financial snapshot.
static constexpr size_t MAX_BACKUP_BYTES = 65536;
static const char RESTORE_FILE[] = "/portfolio-restore-upload.json";
struct RestoreUpload {
  File file;
  String token, kind, base, error;
  size_t bytes = 0;
  uint32_t crc = ~0U;
  unsigned long touched = 0;
  bool complete = false, receiving = false;
};
RestoreUpload restoreUpload;

void clearRestoreUpload() {
  restoreUpload.file.close();
  LittleFS.remove(RESTORE_FILE);
  restoreUpload = RestoreUpload{};
}

void restoreReply(bool ok, const String& message, JsonDocument* details = nullptr) {
  JsonDocument empty;
  JsonDocument& doc = details ? *details : empty;
  doc["ok"] = ok; doc["revision"] = persistentStore.revision();
  if (!ok) doc["error"] = message;
  String json; serializeJson(doc, json);
  server.send(ok ? 200 : 400, "application/json", json);
}

void handleRestoreUpload() {
  HTTPUpload& upload = server.upload();
  if (upload.status == UPLOAD_FILE_START) {
    // A second file in the same multipart request is not a second backup.
    if (restoreUpload.receiving) { clearRestoreUpload(); restoreUpload.receiving = true; restoreUpload.error = "Upload exactly one JSON backup."; return; }
    clearRestoreUpload(); restoreUpload.receiving = true;
    restoreUpload.kind = server.arg("kind"); restoreUpload.base = server.arg("base");
    if (restoreUpload.kind != "transactions" && restoreUpload.kind != "savings-ppk") restoreUpload.error = "Choose the backup type.";
    else if (!persistentStore.writable()) restoreUpload.error = "Storage is unavailable. Back up existing data before repairing storage.";
    else if (restoreUpload.base != String(persistentStore.revision())) restoreUpload.error = "Saved data has changed. Refresh before restoring.";
    if (!restoreUpload.error.isEmpty()) return;
    restoreUpload.token = String(esp_random()) + "-" + String(esp_random());
    restoreUpload.file = LittleFS.open(RESTORE_FILE, "w");
    if (!restoreUpload.file) restoreUpload.error = "Could not open the temporary backup file.";
  } else if (upload.status == UPLOAD_FILE_WRITE && restoreUpload.error.isEmpty()) {
    if (!restoreUpload.file || upload.currentSize > MAX_BACKUP_BYTES - restoreUpload.bytes) restoreUpload.error = "Backup is too large (maximum 64 KiB).";
    else if (restoreUpload.file.write(upload.buf, upload.currentSize) != upload.currentSize) restoreUpload.error = "Could not write the temporary backup file.";
    else { restoreUpload.bytes += upload.currentSize; restoreUpload.crc = crcBytes(restoreUpload.crc, upload.buf, upload.currentSize); }
  } else if (upload.status == UPLOAD_FILE_END) {
    restoreUpload.file.flush(); restoreUpload.file.close();
    restoreUpload.complete = restoreUpload.error.isEmpty() && restoreUpload.bytes > 0 && upload.totalSize == restoreUpload.bytes;
    if (!restoreUpload.complete && restoreUpload.error.isEmpty()) restoreUpload.error = "The upload was incomplete or empty.";
  } else if (upload.status == UPLOAD_FILE_ABORTED) {
    clearRestoreUpload(); restoreUpload.error = "Backup upload was interrupted.";
  }
  restoreUpload.touched = millis();
}

int backupNonSpace(File& file) {
  int c;
  do { c = file.read(); } while (c == ' ' || c == '\n' || c == '\r' || c == '\t');
  return c;
}
bool backupMoney(JsonVariantConst value, Money& money) {
  const char* text=backupNumericToken(value);
  return text && parseMoney(text,money);
}

bool validateRestoreFile(TickerState* tickers, int& count, ManualAsset* accounts, int& manual,
                         uint32_t& nextId, JsonDocument& summary, String& error) {
  auto fail = [&](const char* message) { error = message; return false; };
  if (!restoreUpload.complete || millis() - restoreUpload.touched > 600000) return fail("Backup preview expired. Upload the file again.");
  File file = LittleFS.open(RESTORE_FILE, "r");
  if (!file || file.size() != restoreUpload.bytes) return fail("The temporary backup file is incomplete.");
  uint32_t crc = ~0U; uint8_t buffer[256]; size_t left = file.size();
  while (left) { size_t n = file.read(buffer, std::min(left, sizeof(buffer))); if (!n) return fail("Could not read the temporary backup."); crc = crcBytes(crc, buffer, n); left -= n; }
  if (crc != restoreUpload.crc || !file.seek(0)) return fail("The temporary backup failed its integrity check.");
  time_t now = time(nullptr);
  if (now < NTP_SYNC_MIN_EPOCH) return fail("Wait for the device clock to synchronize before restoring.");
  time_t earliest = parseCalendarDate("2000-01-01"), latest = historyDayEnd(now);
  bool transactions = restoreUpload.kind == "transactions";
  int replaced = 0, imported = 0, valuations = 0;
  xSemaphoreTake(dataMutex, portMAX_DELAY);
  count = tickerCount; manual = 0; nextId = nextLotId;
  if (transactions) {
    for (int i = 0; i < count; ++i) { tickers[i] = tickerData[i]; replaced += tickers[i].lotCount; tickers[i].lotCount = 0; tickers[i].holdings = 0; tickers[i].legacyHint = 0; }
  } else replaced = manualCount;
  xSemaphoreGive(dataMutex);
  if (backupNonSpace(file) != '[') return fail("Expected a JSON backup array.");
  int c = backupNonSpace(file);
  if (c != ']') for (;;) {
    if (c != '{') return fail("Every backup entry must be an object; trailing commas are not allowed.");
    JsonDocument row; BackupRowReader reader{file,c};
    if (deserializeJson(row, reader, DeserializationOption::NestingLimit(6)) || reader.invalid || !row.is<JsonObject>()) return fail("Invalid JSON backup entry.");
    if (transactions) {
      String symbol,dateText;float qty=0,price=0;
      if (!backupText(row["S:symbol"],symbol) || !backupText(row["S:date"],dateText)
          || !parseFiniteNumber(backupNumericToken(row["S:qty"]),qty) || !parseFiniteNumber(backupNumericToken(row["S:pricePLN"]),price)) return fail("Transaction entries require symbol, date, numeric qty and numeric pricePLN.");
      if (!validTickerSymbol(symbol)) return fail("Invalid ticker symbol in backup.");
      time_t date = parseCalendarDate(dateText.c_str());
      if (date < earliest || date > latest || !std::isfinite(qty) || !std::isfinite(price) || qty == 0 || price < 0) return fail("Invalid transaction date, quantity or price.");
      int index = 0; while (index < count && tickers[index].sym != symbol) ++index;
      if (index == count) {
        if (count == MAX_TICKERS) return fail("Restored and current tickers exceed the eight-ticker limit. Remove an unused ticker first.");
        tickers[index].sym = symbol; tickers[index].quote.sym = symbol; tickers[index].quote.currency = "USD"; ++count;
      }
      auto& ticker = tickers[index];
      if (ticker.lotCount == MAX_LOTS || nextId == UINT32_MAX) return fail("Transaction capacity or ID limit exceeded.");
      ticker.lots[ticker.lotCount++] = {date,qty,price,nextId++}; ++imported;
    } else {
      if (manual == MAX_MANUAL_ASSETS || !row["S:history"].is<JsonArray>()) return fail("Savings & PPK entries require a name and history; maximum four accounts.");
      auto& account = accounts[manual];
      if(!backupText(row["S:name"],account.name))return fail("Account names must be text.");
      String trimmed = account.name; trimmed.trim();
      if (trimmed.isEmpty() || account.name.length() > 60) return fail("Invalid account name in backup.");
      for (int i = 0; i < manual; ++i) if (accounts[i].name == account.name) return fail("Duplicate account names in backup.");
      if (row["S:kind"].isNull()) { String upper = account.name; upper.toUpperCase(); account.ppk = upper.indexOf("PPK") >= 0; }
      else { String kind; if(!backupText(row["S:kind"],kind) || (kind != "ppk" && kind != "savings")) return fail("Unknown account type in backup."); account.ppk = kind == "ppk"; }
      Money value, gain;
      if (!backupMoney(row["S:valuePLN"],value) || !backupMoney(row["S:gainPLN"],gain) || value < 0) return fail("Account amounts must be finite PLN values with at most two decimal places.");
      JsonArray history = row["S:history"];
      if (history.size() > MAX_MANUAL_SNAPSHOTS) return fail("Account history exceeds sixty valuations.");
      time_t previous = 0;
      for (JsonVariant entry : history) {
        int64_t stamp=0;Money v,g;
        if (!entry.is<JsonObject>() || !backupInteger(entry["S:ts"],stamp)) return fail("Invalid account valuation entry.");
        if (stamp < earliest || stamp > latest || stamp <= previous || isSameLocalDay(previous,(time_t)stamp)
            || !backupMoney(entry["S:valuePLN"],v) || !backupMoney(entry["S:gainPLN"],g) || v < 0) return fail("Invalid valuation date, amount, chronology or duplicate day.");
        account.history[account.historyCount++] = {(time_t)stamp,v,g}; previous = (time_t)stamp; ++valuations;
      }
      if (account.historyCount) { const auto& last = account.history[account.historyCount-1]; if (last.valuePLN.cents != value.cents || last.gainPLN.cents != gain.cents) return fail("Account balance does not match its last valuation."); }
      account.valuePLN = value; account.gainPLN = gain; ++manual;
    }
    c = backupNonSpace(file);
    if (c == ']') break;
    if (c != ',') return fail("Invalid JSON array separator.");
    c = backupNonSpace(file);
  }
  if (backupNonSpace(file) != -1) return fail("Unexpected content after the backup array.");
  if (transactions) for (int i = 0; i < count; ++i) {
    auto& ticker = tickers[i];
    std::stable_sort(ticker.lots,ticker.lots+ticker.lotCount,[](const Lot& a,const Lot& b){return a.ts < b.ts;});
    if (!validLotSequence(ticker.lots,ticker.lotCount)) return fail("A sale would exceed the purchased quantity. The backup cannot be restored.");
    refreshHoldings(ticker); summary["names"].add(ticker.sym);
  }
  else for (int i = 0; i < manual; ++i) summary["names"].add(accounts[i].name);
  summary["kind"] = restoreUpload.kind; summary["count"] = transactions ? imported : manual;
  summary["replacedCount"] = replaced; summary["valuations"] = valuations;
  return true;
}

void handleRestorePreview() {
  String error = restoreUpload.error;
  if (!restoreUpload.receiving) error = "Choose and upload a JSON backup first.";
  if (server.arg("kind") != restoreUpload.kind || server.arg("base") != String(persistentStore.revision())) error = "Saved data or backup selection changed. Refresh and upload again.";
  JsonDocument summary; int count=0,manual=0; uint32_t next=0;
  bool transactions = restoreUpload.kind == "transactions";
  std::unique_ptr<TickerState[]> tickers(transactions ? new TickerState[MAX_TICKERS]{} : nullptr);
  std::unique_ptr<ManualAsset[]> accounts(transactions ? nullptr : new ManualAsset[MAX_MANUAL_ASSETS]{});
  bool ok = error.isEmpty() && validateRestoreFile(tickers.get(),count,accounts.get(),manual,next,summary,error);
  if (ok) { summary["token"] = restoreUpload.token; restoreUpload.touched = millis(); restoreUpload.receiving = false; }
  else clearRestoreUpload();
  restoreReply(ok,error,&summary);
}

void handleRestoreCommit() {
  // Receipts make a committed-but-lost response safe to retry unchanged.
  if (!beginTransactionRequest("restore")) return;
  String error;
  bool transactions = restoreUpload.kind == "transactions";
  if (!persistentStore.writable()) error = "Storage is unavailable; nothing was restored.";
  else if (server.arg("confirm") != "replace" || server.arg("restoreToken") != restoreUpload.token
      || server.arg("restoreKind") != restoreUpload.kind || restoreUpload.base != String(persistentStore.revision())) error = "Preview this backup again and explicitly confirm replacement.";
  JsonDocument summary; int count=0,manual=0; uint32_t next=0;
  std::unique_ptr<TickerState[]> tickers(transactions ? new TickerState[MAX_TICKERS]{} : nullptr);
  std::unique_ptr<ManualAsset[]> accounts(transactions ? nullptr : new ManualAsset[MAX_MANUAL_ASSETS]{});
  if (!error.isEmpty() || !validateRestoreFile(tickers.get(),count,accounts.get(),manual,next,summary,error)) { restoreReply(false,error); return; }
  MutationGuard mutation;
  xSemaphoreTake(dataMutex,portMAX_DELAY);
  if (transactions) { for (int i=0;i<count;++i) { int old=getIndexBySym(tickers[i].sym); if(old>=0)tickers[i].quote=tickerData[old].quote; tickerData[i]=tickers[i]; } tickerCount=count; nextLotId=next; }
  else { for(int i=0;i<manual;++i)manualAssets[i]=accounts[i]; manualCount=manual; }
  rememberTransaction("restore");
  xSemaphoreGive(dataMutex);
  tickers.reset(); accounts.reset();
  bool ok = true;
  if (mutation.commit(ok,error)) { fetchPending = true; clearRestoreUpload(); }
  restoreReply(ok,error);
}
