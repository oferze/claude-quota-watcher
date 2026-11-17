#pragma once
#include <windows.h>

#include <cstdint>
#include <functional>
#include <optional>
#include <string>

#include "credentials.h"
#include "usage.h"

enum class AppState { Ok, NoLogin, Expired, AccessDenied, Error, ApiChanged };
enum class DotColor { None, Grey, Red };
DotColor DotFor(AppState s);

struct HttpOutcome {
  bool transportOk = false;
  int status = 0;
  std::string body;
  std::optional<int> retryAfterSec;
};

struct Classified {
  AppState state = AppState::Error;
  std::optional<UsageSnapshot> snapshot;
  int httpStatus = 0;
};

// 200+parsed→Ok; 200 unparseable→ApiChanged; 401→Expired; 403→AccessDenied;
// 429, 5xx, transport failure→Error; 404 / other 4xx→ApiChanged.
Classified ClassifyResponse(const HttpOutcome& r);

enum class PollAction { Skip, MarkExpired, MarkNoLogin, KeepPrevious, Request };
PollAction DecidePoll(const CredResult& c, int64_t nowMs, bool inFlight, int64_t retryNotBeforeMs);

// UI-thread state.
struct AppModel {
  AppState state = AppState::NoLogin;
  std::optional<UsageSnapshot> last;      // last good snapshot (kept across errors)
  std::optional<int64_t> lastUpdatedUtc;  // when `last` was fetched (Unix seconds)
  int lastHttpStatus = 0;
  std::wstring plan;
  int64_t tokenExpiresAtMs = 0;
  int unreadableStreak = 0;      // consecutive polls where the credentials file couldn't be read
  bool credsUnreadable = false;  // streak hit the limit: state is Error because of the file
};

constexpr int kUnreadableLimit = 3;  // ~3 minutes of an unreadable login file → show an error

void ApplyCredResult(AppModel& m, PollAction a, const CredResult& c);
void ApplyClassified(AppModel& m, const Classified& c, int64_t nowUtc);

// hhmm: formats a Unix time as local "HH:MM". An update less than a minute before nowUtc reads
// "now (HH:MM)".
std::wstring StatusLine(const AppModel& m, int64_t nowUtc, const std::function<std::wstring(int64_t)>& hhmm);
// toLocal: converts a Unix time to local SYSTEMTIME.
std::wstring FormatReset(int64_t resetUtc, int64_t nowUtc, const std::function<SYSTEMTIME(int64_t)>& toLocal);
std::wstring FormatPercent(std::optional<double> p);  // "62%", "—"
double BarFraction(std::optional<double> p);          // clamped to [0,1]; nullopt→0

// Current-session usage as a 0..1 fraction for the tray icon (0 when unknown).
double SessionFraction(const AppModel& m);

// Poll cadence: every minute while the user checked the popup in the last 30 minutes, else every 3.
// All times are milliseconds on one monotonic clock.
int64_t PollIntervalMs(int64_t nowMs, int64_t lastCheckMs);
// Whether a 1-minute timer tick should poll (tolerates a timer firing a few seconds early).
bool PollDue(int64_t nowMs, int64_t lastCheckMs, int64_t lastPollMs);

// Clock helpers (real system clock / time zone).
int64_t NowUnixMs();
SYSTEMTIME UnixToLocal(int64_t unixSec);
std::wstring LocalHhmm(int64_t unixSec);

// Where to put a popup of `popup` size next to `anchor` (tray icon rect) inside `workArea`.
POINT PlacePopup(RECT anchor, RECT workArea, SIZE popup);
