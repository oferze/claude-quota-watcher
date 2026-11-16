#include "credentials.h"

#include <windows.h>

#include <vector>

#include "json.h"

std::wstring CredentialsPath() {
  wchar_t buf[MAX_PATH * 2];
  DWORD n = GetEnvironmentVariableW(L"CLAUDE_CONFIG_DIR", buf, static_cast<DWORD>(std::size(buf)));
  std::wstring dir;
  if (n > 0 && n < std::size(buf)) {
    dir.assign(buf, n);
  } else {
    n = GetEnvironmentVariableW(L"USERPROFILE", buf, static_cast<DWORD>(std::size(buf)));
    if (n > 0 && n < std::size(buf)) dir.assign(buf, n);
    dir += L"\\.claude";
  }
  while (!dir.empty() && (dir.back() == L'\\' || dir.back() == L'/')) dir.pop_back();
  return dir + L"\\.credentials.json";
}

CredResult LoadCredentialsFromText(std::string_view text) {
  CredResult r;
  auto root = ParseJson(text);
  if (!root || !root->isObject()) {
    r.status = CredStatus::Unreadable;
    return r;
  }
  const JsonValue* oauth = root->get("claudeAiOauth");
  if (!oauth || !oauth->isObject()) {
    r.status = CredStatus::NotFound;
    return r;
  }
  const JsonValue* token = oauth->get("accessToken");
  if (!token || !token->isString() || token->str.empty()) {
    r.status = CredStatus::Unreadable;
    return r;
  }
  r.status = CredStatus::Found;
  r.creds.accessToken = token->str;
  if (const JsonValue* e = oauth->get("expiresAt"); e && e->isNumber()) {
    r.creds.expiresAtMs = static_cast<int64_t>(e->num);
  }
  if (const JsonValue* p = oauth->get("subscriptionType"); p && p->isString()) {
    r.creds.plan = Utf8ToWide(p->str);
  }
  return r;
}

CredResult LoadCredentials(const std::wstring& path) {
  HANDLE h = INVALID_HANDLE_VALUE;
  for (int attempt = 0; attempt < 2; ++attempt) {
    h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                    nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h != INVALID_HANDLE_VALUE) break;
    DWORD err = GetLastError();
    if (err == ERROR_FILE_NOT_FOUND || err == ERROR_PATH_NOT_FOUND) return {CredStatus::NotFound, {}};
    if (err != ERROR_SHARING_VIOLATION || attempt == 1) return {CredStatus::Unreadable, {}};
    Sleep(100);
  }
  std::string text;
  LARGE_INTEGER size{};
  if (GetFileSizeEx(h, &size) && size.QuadPart > 0 && size.QuadPart < (1 << 20)) {
    text.resize(static_cast<size_t>(size.QuadPart));
    DWORD read = 0;
    if (!ReadFile(h, text.data(), static_cast<DWORD>(text.size()), &read, nullptr)) read = 0;
    text.resize(read);
  }
  CloseHandle(h);
  CredResult r = LoadCredentialsFromText(text);
  SecureZeroMemory(text.data(), text.size());
  return r;
}
