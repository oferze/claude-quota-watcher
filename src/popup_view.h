#pragma once
#include <windows.h>

#include <functional>
#include <string>
#include <vector>

#include "state.h"

// What the popup shows, independent of drawing.
struct PopupRowView {
  std::wstring label;
  std::wstring percent;  // "62%" or "—"
  std::wstring reset;    // "Resets 17:40 (in 1h 20m)" or empty when unknown
  double fraction = 0;   // bar fill, 0..1
};

struct PopupView {
  std::wstring plan;                // "Team" (capitalized), may be empty
  std::vector<PopupRowView> rows;   // empty when there is nothing to show
  std::wstring status;              // StatusLine()
  bool statusIsError = false;       // Error / ApiChanged → shown in red
  bool showSettingsLink = false;    // NoLogin → "Settings…" link instead of stats
};

// What the Settings window shows.
struct SettingsView {
  std::wstring login;         // "Found" / "Not found" / "Expired"
  std::wstring plan;          // "Team" or "—"
  std::wstring tokenExpires;  // local date-time or "—"
  std::wstring path;          // credentials file path
  std::wstring lastResult;    // StatusLine()
  bool showInstructions = false;
};

constexpr wchar_t kLoginInstructions[] =
    L"Run claude in a terminal and type /login (or just use Claude Code — it renews the login automatically).";

// dateTime: formats a Unix time as a local date-time string.
SettingsView BuildSettingsView(const AppModel& m, const std::wstring& credentialsPath,
                               const std::function<std::wstring(int64_t)>& hhmm,
                               const std::function<std::wstring(int64_t)>& dateTime);

PopupView BuildPopupView(const AppModel& m, int64_t nowUtc, const std::function<std::wstring(int64_t)>& hhmm,
                         const std::function<SYSTEMTIME(int64_t)>& toLocal);
