#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

struct LimitRow {
  std::wstring label;
  std::optional<double> percent;       // 0–100 scale, may exceed 100
  std::optional<int64_t> resetsAtUtc;  // Unix seconds
};

struct UsageSnapshot {
  std::vector<LimitRow> rows;
};

// ISO-8601 like "2026-11-18T17:40:00.143128+00:00" or "...Z" -> Unix seconds (fraction dropped).
std::optional<int64_t> ParseIso8601Utc(std::string_view s);

// Parses the /api/oauth/usage body. nullopt = invalid JSON or no recognizable rows.
std::optional<UsageSnapshot> ParseUsage(std::string_view body);
