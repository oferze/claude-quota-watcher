#include "usage.h"

#include "json.h"

namespace {

bool Digits(std::string_view s, size_t pos, size_t n, int& out) {
  if (pos + n > s.size()) return false;
  int v = 0;
  for (size_t i = 0; i < n; ++i) {
    char c = s[pos + i];
    if (c < '0' || c > '9') return false;
    v = v * 10 + (c - '0');
  }
  out = v;
  return true;
}

// Days since 1970-01-01 for a proleptic Gregorian date (H. Hinnant's algorithm).
int64_t DaysFromCivil(int64_t y, unsigned m, unsigned d) {
  y -= m <= 2;
  const int64_t era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = static_cast<unsigned>(y - era * 400);
  const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + static_cast<int64_t>(doe) - 719468;
}

std::optional<double> NumberField(const JsonValue& o, std::string_view key) {
  const JsonValue* v = o.get(key);
  if (v && v->isNumber()) return v->num;
  return std::nullopt;
}

std::optional<int64_t> ResetField(const JsonValue& o) {
  const JsonValue* v = o.get("resets_at");
  if (v && v->isString()) return ParseIso8601Utc(v->str);
  return std::nullopt;
}

void ParseLimits(const JsonValue& limits, UsageSnapshot& out) {
  for (const JsonValue& l : limits.arr) {
    if (!l.isObject()) continue;
    const JsonValue* kind = l.get("kind");
    if (!kind || !kind->isString()) continue;
    std::wstring label;
    if (kind->str == "session") {
      label = L"Current session";
    } else if (kind->str == "weekly_all") {
      label = L"Weekly · all models";
    } else if (kind->str == "weekly_scoped") {
      const JsonValue* name = l.path({"scope", "model", "display_name"});
      if (!name || !name->isString() || name->str.empty()) continue;
      label = L"Weekly · " + Utf8ToWide(name->str);
    } else {
      continue;
    }
    out.rows.push_back({label, NumberField(l, "percent"), ResetField(l)});
  }
}

void ParseLegacy(const JsonValue& root, UsageSnapshot& out) {
  static const struct { const char* key; const wchar_t* label; } kFields[] = {
      {"five_hour", L"Current session"},
      {"seven_day", L"Weekly · all models"},
      {"seven_day_opus", L"Weekly · Opus"},
      {"seven_day_sonnet", L"Weekly · Sonnet"},
  };
  for (const auto& f : kFields) {
    const JsonValue* o = root.get(f.key);
    if (!o || !o->isObject()) continue;
    out.rows.push_back({f.label, NumberField(*o, "utilization"), ResetField(*o)});
  }
}

}  // namespace

std::optional<int64_t> ParseIso8601Utc(std::string_view s) {
  // YYYY-MM-DDTHH:MM:SS[.fraction](Z|±HH:MM)
  int y, mo, d, h, mi, se;
  if (s.size() < 20 || !Digits(s, 0, 4, y) || s[4] != '-' || !Digits(s, 5, 2, mo) || s[7] != '-' ||
      !Digits(s, 8, 2, d) || (s[10] != 'T' && s[10] != 't' && s[10] != ' ') || !Digits(s, 11, 2, h) ||
      s[13] != ':' || !Digits(s, 14, 2, mi) || s[16] != ':' || !Digits(s, 17, 2, se)) {
    return std::nullopt;
  }
  if (mo < 1 || mo > 12 || d < 1 || d > 31 || h > 23 || mi > 59 || se > 60) return std::nullopt;
  size_t pos = 19;
  if (pos < s.size() && s[pos] == '.') {
    ++pos;
    size_t start = pos;
    while (pos < s.size() && s[pos] >= '0' && s[pos] <= '9') ++pos;
    if (pos == start) return std::nullopt;
  }
  int offsetSec = 0;
  if (pos < s.size() && (s[pos] == 'Z' || s[pos] == 'z')) {
    ++pos;
  } else if (pos < s.size() && (s[pos] == '+' || s[pos] == '-')) {
    int oh, om;
    if (!Digits(s, pos + 1, 2, oh) || pos + 3 >= s.size() || s[pos + 3] != ':' || !Digits(s, pos + 4, 2, om)) {
      return std::nullopt;
    }
    offsetSec = (oh * 3600 + om * 60) * (s[pos] == '-' ? -1 : 1);
    pos += 6;
  } else {
    return std::nullopt;
  }
  if (pos != s.size()) return std::nullopt;
  int64_t days = DaysFromCivil(y, static_cast<unsigned>(mo), static_cast<unsigned>(d));
  return days * 86400 + h * 3600 + mi * 60 + se - offsetSec;
}

std::optional<UsageSnapshot> ParseUsage(std::string_view body) {
  auto root = ParseJson(body);
  if (!root || !root->isObject()) return std::nullopt;
  UsageSnapshot snap;
  const JsonValue* limits = root->get("limits");
  if (limits && limits->isArray()) ParseLimits(*limits, snap);
  if (snap.rows.empty()) ParseLegacy(*root, snap);
  if (snap.rows.empty()) return std::nullopt;
  return snap;
}
