#pragma once
#include <windows.h>

#include <optional>

#include "credentials.h"
#include "state.h"

constexpr UINT WM_APP_POLLRESULT = WM_APP + 1;  // lParam = PollResult* (receiver deletes)

struct PollResult {
  PollAction action = PollAction::Skip;
  CredResult creds;                       // accessToken is always cleared before posting
  std::optional<Classified> classified;   // set only when an HTTP request was made
  std::optional<int> retryAfterSec;
};

// Runs one poll at a time. All methods are called on the UI thread.
class Poller {
 public:
  explicit Poller(HWND notify) : notify_(notify) {}
  // Reads credentials, decides, and either posts a result immediately or runs the HTTP call on a
  // worker thread that posts when done. Skip posts nothing.
  void PollNow(int64_t retryNotBeforeMs);
  // Call after handling a WM_APP_POLLRESULT; clears the in-flight flag for HTTP results.
  void OnResultHandled(const PollResult& r);
  bool InFlight() const { return inFlight_; }

 private:
  HWND notify_;
  bool inFlight_ = false;
};
