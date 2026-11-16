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

PopupView BuildPopupView(const AppModel& m, int64_t nowUtc, const std::function<std::wstring(int64_t)>& hhmm,
                         const std::function<SYSTEMTIME(int64_t)>& toLocal);
