#include "state.h"

#include <algorithm>
#include <cmath>
#include <cwchar>

DotColor DotFor(AppState s) {
  switch (s) {
    case AppState::Ok: return DotColor::None;
    case AppState::NoLogin:
    case AppState::Expired:
    case AppState::AccessDenied: return DotColor::Grey;
    case AppState::Error:
    case AppState::ApiChanged: return DotColor::Red;
  }
  return DotColor::Red;
}

Classified ClassifyResponse(const HttpOutcome& r) {
  Classified c;
  c.httpStatus = r.transportOk ? r.status : 0;
  if (!r.transportOk) {
    c.state = AppState::Error;
  } else if (r.status == 200) {
    c.snapshot = ParseUsage(r.body);
    c.state = c.snapshot ? AppState::Ok : AppState::ApiChanged;
  } else if (r.status == 401) {
    c.state = AppState::Expired;
  } else if (r.status == 403) {
    c.state = AppState::AccessDenied;
  } else if (r.status == 429 || r.status >= 500) {
    c.state = AppState::Error;
  } else {
    c.state = AppState::ApiChanged;  // 404, other 4xx, unexpected 1xx/2xx/3xx
  }
  return c;
}

PollAction DecidePoll(const CredResult& c, int64_t nowMs, bool inFlight, int64_t retryNotBeforeMs) {
  if (inFlight) return PollAction::Skip;
  switch (c.status) {
    case CredStatus::NotFound: return PollAction::MarkNoLogin;
    case CredStatus::Unreadable: return PollAction::KeepPrevious;
    case CredStatus::Found: break;
  }
  if (c.creds.expiresAtMs > 0 && c.creds.expiresAtMs <= nowMs) return PollAction::MarkExpired;  // 0 = unknown
  if (nowMs < retryNotBeforeMs) return PollAction::Skip;
  return PollAction::Request;
}

void ApplyCredResult(AppModel& m, PollAction a, const CredResult& c) {
  if (c.status == CredStatus::Unreadable) {
    if (++m.unreadableStreak >= kUnreadableLimit) {
      m.state = AppState::Error;
      m.credsUnreadable = true;
    }
  } else {
    m.unreadableStreak = 0;
    m.credsUnreadable = false;
  }
  if (c.status == CredStatus::Found) {
    m.plan = c.creds.plan;
    m.tokenExpiresAtMs = c.creds.expiresAtMs;
  }
  switch (a) {
    case PollAction::MarkNoLogin:
      m.state = AppState::NoLogin;
      m.last.reset();
      m.lastUpdatedUtc.reset();
      m.plan.clear();
      m.tokenExpiresAtMs = 0;
      break;
    case PollAction::MarkExpired:
      m.state = AppState::Expired;
      break;
    case PollAction::Skip:
    case PollAction::KeepPrevious:
    case PollAction::Request:
      break;
  }
}

void ApplyClassified(AppModel& m, const Classified& c, int64_t nowUtc) {
  m.state = c.state;
  m.lastHttpStatus = c.httpStatus;
  if (c.state == AppState::Ok && c.snapshot) {
    m.last = c.snapshot;
    m.lastUpdatedUtc = nowUtc;
  }
}

std::wstring StatusLine(const AppModel& m, const std::function<std::wstring(int64_t)>& hhmm) {
  switch (m.state) {
    case AppState::Ok:
      return m.lastUpdatedUtc ? L"Updated " + hhmm(*m.lastUpdatedUtc) : L"Updated";
    case AppState::NoLogin:
      return L"Not connected";
    case AppState::Expired:
      return L"Login expired — open Claude Code to renew";
    case AppState::AccessDenied:
      return L"Access denied — run /login in Claude Code";
    case AppState::Error: {
      const std::wstring what = m.credsUnreadable ? L"Can't read the Claude Code login file" : L"Couldn't reach Claude";
      return m.lastUpdatedUtc ? what + L" — last updated " + hhmm(*m.lastUpdatedUtc) : what;
    }
    case AppState::ApiChanged:
      if (m.lastHttpStatus != 0 && m.lastHttpStatus != 200) {
        return L"Usage API changed (HTTP " + std::to_wstring(m.lastHttpStatus) + L") — app may need an update";
      }
      return L"Usage API changed — app may need an update";
  }
  return {};
}

std::wstring FormatReset(int64_t resetUtc, int64_t nowUtc, const std::function<SYSTEMTIME(int64_t)>& toLocal) {
  const int64_t diff = resetUtc - nowUtc;
  if (diff <= 0) return L"Resets now";
  const SYSTEMTIME st = toLocal(resetUtc);
  wchar_t buf[64];
  if (diff < 86400) {
    const int64_t mins = (diff + 59) / 60;
    const int64_t h = mins / 60, m = mins % 60;
    wchar_t rel[32];
    if (h == 0) swprintf_s(rel, L"%lldm", m);
    else if (m == 0) swprintf_s(rel, L"%lldh", h);
    else swprintf_s(rel, L"%lldh %lldm", h, m);
    swprintf_s(buf, L"Resets %02u:%02u (in %s)", st.wHour, st.wMinute, rel);
  } else {
    static const wchar_t* kDays[] = {L"Sun", L"Mon", L"Tue", L"Wed", L"Thu", L"Fri", L"Sat"};
    swprintf_s(buf, L"Resets %s %02u:%02u", kDays[st.wDayOfWeek % 7], st.wHour, st.wMinute);
  }
  return buf;
}

std::wstring FormatPercent(std::optional<double> p) {
  if (!p) return L"—";
  return std::to_wstring(static_cast<long long>(std::llround(*p))) + L"%";
}

double BarFraction(std::optional<double> p) {
  if (!p) return 0.0;
  return std::clamp(*p / 100.0, 0.0, 1.0);
}

POINT PlacePopup(RECT anchor, RECT work, SIZE popup) {
  constexpr LONG kMargin = 8;
  const LONG cx = (anchor.left + anchor.right) / 2;
  const LONG cy = (anchor.top + anchor.bottom) / 2;
  POINT p{cx - popup.cx / 2, cy - popup.cy / 2};
  if (anchor.top >= work.bottom) {          // taskbar at bottom
    p.y = work.bottom - kMargin - popup.cy;
  } else if (anchor.bottom <= work.top) {   // taskbar at top
    p.y = work.top + kMargin;
  } else if (anchor.right <= work.left) {   // taskbar at left
    p.x = work.left + kMargin;
  } else if (anchor.left >= work.right) {   // taskbar at right
    p.x = work.right - kMargin - popup.cx;
  } else {                                   // icon inside the work area: above it, else below
    p.y = anchor.top - kMargin - popup.cy;
    if (p.y < work.top + kMargin) p.y = anchor.bottom + kMargin;
  }
  const LONG maxX = work.right - kMargin - popup.cx;
  const LONG maxY = work.bottom - kMargin - popup.cy;
  p.x = std::max(work.left + kMargin, std::min(p.x, maxX));
  p.y = std::max(work.top + kMargin, std::min(p.y, maxY));
  return p;
}

namespace {
constexpr int64_t kUnixEpochAsFileTime = 116444736000000000LL;  // 100 ns ticks from 1601 to 1970
}

int64_t NowUnixMs() {
  FILETIME ft;
  GetSystemTimeAsFileTime(&ft);
  ULARGE_INTEGER u;
  u.LowPart = ft.dwLowDateTime;
  u.HighPart = ft.dwHighDateTime;
  return (static_cast<int64_t>(u.QuadPart) - kUnixEpochAsFileTime) / 10000;
}

SYSTEMTIME UnixToLocal(int64_t unixSec) {
  ULARGE_INTEGER u;
  u.QuadPart = static_cast<ULONGLONG>(unixSec * 10000000LL + kUnixEpochAsFileTime);
  FILETIME ft{u.LowPart, u.HighPart};
  SYSTEMTIME utc{}, local{};
  FileTimeToSystemTime(&ft, &utc);
  if (!SystemTimeToTzSpecificLocalTime(nullptr, &utc, &local)) return utc;
  return local;
}

std::wstring LocalHhmm(int64_t unixSec) {
  SYSTEMTIME st = UnixToLocal(unixSec);
  wchar_t buf[8];
  swprintf_s(buf, L"%02u:%02u", st.wHour, st.wMinute);
  return buf;
}

double SessionFraction(const AppModel& m) {
  if (!m.last) return 0.0;
  for (const LimitRow& r : m.last->rows) {
    if (r.label == L"Current session") return BarFraction(r.percent);
  }
  return 0.0;
}
