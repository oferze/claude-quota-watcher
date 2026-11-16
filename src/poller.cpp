#include "poller.h"

#include <memory>
#include <thread>

#include "http.h"

namespace {
void Post(HWND hwnd, std::unique_ptr<PollResult> r) {
  if (PostMessageW(hwnd, WM_APP_POLLRESULT, 0, reinterpret_cast<LPARAM>(r.get()))) r.release();
}
}  // namespace

void Poller::PollNow(int64_t retryNotBeforeMs) {
  CredResult creds = LoadCredentials(CredentialsPath());
  PollAction action = DecidePoll(creds, NowUnixMs(), inFlight_, retryNotBeforeMs);
  if (action == PollAction::Skip) return;

  std::string token;
  token.swap(creds.creds.accessToken);
  auto result = std::make_unique<PollResult>();
  result->action = action;
  result->creds = creds;

  if (action != PollAction::Request) {
    SecureZeroMemory(token.data(), token.size());
    Post(notify_, std::move(result));
    return;
  }

  inFlight_ = true;
  HWND hwnd = notify_;
  std::thread([hwnd, token = std::move(token), result = std::move(result)]() mutable {
    HttpOutcome r = HttpGetUsage(token);
    SecureZeroMemory(token.data(), token.size());
    result->classified = ClassifyResponse(r);
    result->retryAfterSec = r.retryAfterSec;
    Post(hwnd, std::move(result));
  }).detach();
}

void Poller::OnResultHandled(const PollResult& r) {
  if (r.classified) inFlight_ = false;
}
