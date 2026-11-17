#include "popup_view.h"

#include <cwctype>

PopupView BuildPopupView(const AppModel& m, int64_t nowUtc, const std::function<std::wstring(int64_t)>& hhmm,
                         const std::function<SYSTEMTIME(int64_t)>& toLocal) {
  PopupView v;
  v.plan = m.plan;
  if (!v.plan.empty()) v.plan[0] = static_cast<wchar_t>(std::towupper(v.plan[0]));
  v.status = StatusLine(m, nowUtc, hhmm);
  v.statusIsError = m.state == AppState::Error || m.state == AppState::ApiChanged;
  v.showSettingsLink = m.state == AppState::NoLogin;
  if (m.last && m.state != AppState::NoLogin) {
    for (const LimitRow& r : m.last->rows) {
      PopupRowView row;
      row.label = r.label;
      row.percent = FormatPercent(r.percent);
      row.fraction = BarFraction(r.percent);
      if (r.resetsAtUtc) row.reset = FormatReset(*r.resetsAtUtc, nowUtc, toLocal);
      v.rows.push_back(std::move(row));
    }
  }
  return v;
}

SettingsView BuildSettingsView(const AppModel& m, const std::wstring& credentialsPath, int64_t nowUtc,
                               const std::function<std::wstring(int64_t)>& hhmm,
                               const std::function<std::wstring(int64_t)>& dateTime) {
  SettingsView v;
  switch (m.state) {
    case AppState::NoLogin: v.login = L"Not found"; break;
    case AppState::Expired: v.login = L"Expired"; break;
    default: v.login = L"Found"; break;
  }
  v.plan = m.plan.empty() ? L"—" : m.plan;
  if (!m.plan.empty()) v.plan[0] = static_cast<wchar_t>(std::towupper(v.plan[0]));
  v.tokenExpires = m.tokenExpiresAtMs > 0 ? dateTime(m.tokenExpiresAtMs / 1000) : L"—";
  v.path = credentialsPath;
  v.lastResult = StatusLine(m, nowUtc, hhmm);
  v.showInstructions =
      m.state == AppState::NoLogin || m.state == AppState::Expired || m.state == AppState::AccessDenied;
  return v;
}
