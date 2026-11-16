#pragma once
#include <cstdint>
#include <string>
#include <string_view>

// Reads Claude Code's login (never writes it, never refreshes it).
enum class CredStatus {
  Found,
  NotFound,    // file absent, or no claudeAiOauth section
  Unreadable,  // file exists but is partial/invalid JSON or lacks an access token (e.g. mid-rewrite)
};

struct Credentials {
  std::string accessToken;
  int64_t expiresAtMs = 0;  // Unix epoch milliseconds
  std::wstring plan;        // subscriptionType, e.g. L"team"
};

struct CredResult {
  CredStatus status = CredStatus::NotFound;
  Credentials creds;
};

// %CLAUDE_CONFIG_DIR%\.credentials.json if set, else %USERPROFILE%\.claude\.credentials.json
std::wstring CredentialsPath();
CredResult LoadCredentialsFromText(std::string_view text);
CredResult LoadCredentials(const std::wstring& path);
