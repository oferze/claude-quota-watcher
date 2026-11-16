#include "http.h"

#include <windows.h>
#include <winhttp.h>

#include <cwchar>
#include <string>

namespace {

struct Handle {
  HINTERNET h = nullptr;
  explicit Handle(HINTERNET x) : h(x) {}
  ~Handle() { if (h) WinHttpCloseHandle(h); }
  Handle(const Handle&) = delete;
  Handle& operator=(const Handle&) = delete;
  explicit operator bool() const { return h != nullptr; }
};

std::wstring Widen(const std::string& s) {
  return std::wstring(s.begin(), s.end());  // token and header values are ASCII
}

}  // namespace

HttpOutcome HttpGetUsage(const std::string& accessToken) {
  HttpOutcome out;
  Handle session(WinHttpOpen(L"ClaudeQuotaWatcher/1.0", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                             WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
  if (!session) return out;
  WinHttpSetTimeouts(session.h, 15000, 15000, 15000, 15000);
  DWORD decompress = WINHTTP_DECOMPRESSION_FLAG_ALL;
  WinHttpSetOption(session.h, WINHTTP_OPTION_DECOMPRESSION, &decompress, sizeof(decompress));

  Handle connect(WinHttpConnect(session.h, L"api.anthropic.com", INTERNET_DEFAULT_HTTPS_PORT, 0));
  if (!connect) return out;
  Handle request(WinHttpOpenRequest(connect.h, L"GET", L"/api/oauth/usage", nullptr, WINHTTP_NO_REFERER,
                                    WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE));
  if (!request) return out;

  std::wstring headers = L"Authorization: Bearer " + Widen(accessToken) +
                         L"\r\nanthropic-beta: oauth-2025-04-20\r\nAccept: application/json\r\n";
  BOOL sent = WinHttpSendRequest(request.h, headers.c_str(), static_cast<DWORD>(headers.size()),
                                 WINHTTP_NO_REQUEST_DATA, 0, 0, 0);
  SecureZeroMemory(headers.data(), headers.size() * sizeof(wchar_t));
  if (!sent || !WinHttpReceiveResponse(request.h, nullptr)) return out;

  DWORD status = 0, len = sizeof(status);
  if (!WinHttpQueryHeaders(request.h, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                           WINHTTP_HEADER_NAME_BY_INDEX, &status, &len, WINHTTP_NO_HEADER_INDEX)) {
    return out;
  }
  out.status = static_cast<int>(status);

  wchar_t retry[64];
  DWORD retryLen = sizeof(retry);
  if (WinHttpQueryHeaders(request.h, WINHTTP_QUERY_CUSTOM, L"Retry-After", retry, &retryLen,
                          WINHTTP_NO_HEADER_INDEX)) {
    wchar_t* end = nullptr;
    long secs = std::wcstol(retry, &end, 10);
    if (end != retry && *end == L'\0' && secs >= 0 && secs <= 24 * 3600) out.retryAfterSec = static_cast<int>(secs);
  }

  for (;;) {
    DWORD avail = 0;
    if (!WinHttpQueryDataAvailable(request.h, &avail)) return out;  // transportOk stays false
    if (avail == 0) break;
    if (out.body.size() + avail > (4u << 20)) return out;  // absurdly large: treat as failure
    size_t old = out.body.size();
    out.body.resize(old + avail);
    DWORD read = 0;
    if (!WinHttpReadData(request.h, out.body.data() + old, avail, &read)) return out;
    out.body.resize(old + read);
  }
  out.transportOk = true;
  return out;
}
