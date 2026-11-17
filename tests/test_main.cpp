// Unit tests for Claude Quota Watcher. Run: build.bat test
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "credentials.h"
#include "http.h"
#include "json.h"
#include "popup_view.h"
#include "state.h"
#include "tray.h"
#include "usage.h"

namespace {
struct TestCase { const char* name; void (*fn)(); };
std::vector<TestCase>& Registry() { static std::vector<TestCase> r; return r; }
int g_failures = 0;
struct Registrar { Registrar(const char* n, void (*f)()) { Registry().push_back({n, f}); } };
void Fail(const char* file, int line, const char* expr) {
  ++g_failures;
  std::printf("  FAIL %s:%d: %s\n", file, line, expr);
}
std::string ReadFile(const char* path) {
  std::ifstream f(path, std::ios::binary);
  std::stringstream ss; ss << f.rdbuf();
  return ss.str();
}
}  // namespace

#define TEST(name) \
  static void name(); \
  static Registrar reg_##name(#name, name); \
  static void name()
#define CHECK(cond) do { if (!(cond)) Fail(__FILE__, __LINE__, #cond); } while (0)

// ---------------------------------------------------------------- JSON

TEST(test_json_object_basic) {
  auto v = ParseJson(R"({"a":1,"b":"x","c":null,"d":true})");
  CHECK(v.has_value());
  if (!v) return;
  CHECK(v->get("a") && v->get("a")->num == 1);
  CHECK(v->get("b") && v->get("b")->str == "x");
  CHECK(v->get("c") && v->get("c")->isNull());
  CHECK(v->get("d") && v->get("d")->b == true);
  CHECK(v->get("zz") == nullptr);
}

TEST(test_json_nested_path) {
  auto v = ParseJson(R"({"s":{"m":{"n":"Fable"}}})");
  CHECK(v.has_value());
  if (!v) return;
  CHECK(v->path({"s", "m", "n"}) && v->path({"s", "m", "n"})->str == "Fable");
  CHECK(v->path({"s", "x", "n"}) == nullptr);
}

TEST(test_json_array_numbers) {
  auto v = ParseJson("[1, -2.5, 6.2e1]");
  CHECK(v.has_value());
  if (!v) return;
  CHECK(v->isArray() && v->arr.size() == 3);
  CHECK(v->arr.size() == 3 && v->arr[1].num == -2.5 && v->arr[2].num == 62.0);
}

TEST(test_json_string_escapes) {
  auto v = ParseJson(R"("a\"b\\c\nd\u00e9")");
  CHECK(v.has_value());
  if (!v) return;
  CHECK(v->str == "a\"b\\c\nd\xC3\xA9");
  auto s = ParseJson(R"("\ud83d\ude00")");  // surrogate pair -> U+1F600
  CHECK(s && s->str == "\xF0\x9F\x98\x80");
  CHECK(Utf8ToWide("d\xC3\xA9") == L"d\u00e9");
}

TEST(test_json_invalid) {
  for (const char* bad : {"", "{", "{\"a\":}", "[1,]", "tru", "{} x", "\"abc", "{\"a\" 1}"}) {
    if (ParseJson(bad)) { Fail(__FILE__, __LINE__, bad); }
  }
}

// ---------------------------------------------------------------- usage

TEST(test_iso8601) {
  CHECK(ParseIso8601Utc("2026-11-18T17:40:00.143128+00:00") == 1795023600);
  CHECK(ParseIso8601Utc("2026-11-22T11:00:00+00:00") == 1795345200);
  CHECK(ParseIso8601Utc("2026-11-18T19:40:00+02:00") == 1795023600);
  CHECK(ParseIso8601Utc("2026-11-18T12:10:00-05:30") == 1795023600);
  CHECK(ParseIso8601Utc("2026-11-18T17:40:00Z") == 1795023600);
  CHECK(!ParseIso8601Utc("garbage"));
  CHECK(!ParseIso8601Utc("2026-13-16T17:40:00Z"));
  CHECK(!ParseIso8601Utc(""));
}

TEST(test_usage_fixture_limits) {
  // Real response captured 2026-11-18.
  auto u = ParseUsage(ReadFile("tests/fixtures/usage-2026-11-18.json"));
  CHECK(u.has_value());
  if (!u) return;
  CHECK(u->rows.size() == 3);
  if (u->rows.size() != 3) return;
  CHECK(u->rows[0].label == L"Current session");
  CHECK(u->rows[0].percent == 89.0);
  CHECK(u->rows[0].resetsAtUtc == 1795023599);
  CHECK(u->rows[1].label == L"Weekly · all models");
  CHECK(u->rows[1].percent == 26.0);
  CHECK(u->rows[1].resetsAtUtc == 1795345199);
  CHECK(u->rows[2].label == L"Weekly · Fable");
  CHECK(u->rows[2].percent == 0.0);
  CHECK(u->rows[2].resetsAtUtc == 1795345200);
}

TEST(test_usage_fallback) {
  auto u = ParseUsage(ReadFile("tests/fixtures/usage-no-limits.json"));
  CHECK(u.has_value());
  if (!u) return;
  CHECK(u->rows.size() == 2);
  if (u->rows.size() != 2) return;
  CHECK(u->rows[0].label == L"Current session");
  CHECK(u->rows[0].percent == 89.0);
  CHECK(u->rows[0].resetsAtUtc == 1795023599);
  CHECK(u->rows[1].label == L"Weekly · all models");
  CHECK(u->rows[1].percent == 26.0);
}

TEST(test_usage_fallback_opus) {
  auto u = ParseUsage(R"({"five_hour":{"utilization":5,"resets_at":null},"seven_day":null,)"
                      R"("seven_day_opus":{"utilization":7,"resets_at":"2026-11-22T11:00:00+00:00"}})");
  CHECK(u.has_value());
  if (!u) return;
  CHECK(u->rows.size() == 2);
  if (u->rows.size() != 2) return;
  CHECK(u->rows[0].label == L"Current session");
  CHECK(u->rows[0].percent == 5.0);
  CHECK(!u->rows[0].resetsAtUtc);
  CHECK(u->rows[1].label == L"Weekly · Opus");
  CHECK(u->rows[1].percent == 7.0);
  CHECK(u->rows[1].resetsAtUtc == 1795345200);
}

TEST(test_usage_null_fields_kept) {
  auto u = ParseUsage(R"({"limits":[{"kind":"session","percent":null,"resets_at":null}]})");
  CHECK(u.has_value());
  if (!u) return;
  CHECK(u->rows.size() == 1);
  if (u->rows.empty()) return;
  CHECK(u->rows[0].label == L"Current session");
  CHECK(!u->rows[0].percent);
  CHECK(!u->rows[0].resetsAtUtc);
}

TEST(test_usage_unknown_kind_skipped_scoped_generic) {
  auto u = ParseUsage(
      R"({"limits":[{"kind":"mystery","percent":3},)"
      R"({"kind":"weekly_scoped","percent":4,"scope":{"model":{"display_name":"Sonnet"}}},)"
      R"({"kind":"weekly_scoped","percent":5,"scope":{"model":null}}]})");
  CHECK(u.has_value());
  if (!u) return;
  CHECK(u->rows.size() == 1);
  if (u->rows.empty()) return;
  CHECK(u->rows[0].label == L"Weekly · Sonnet");
  CHECK(u->rows[0].percent == 4.0);
}

TEST(test_usage_unrecognizable) {
  CHECK(!ParseUsage("{}"));
  CHECK(!ParseUsage(R"({"limits":[]})"));
  CHECK(!ParseUsage("not json"));
  CHECK(!ParseUsage("[1,2]"));
}

// ---------------------------------------------------------------- credentials

TEST(test_creds_found) {
  auto r = LoadCredentialsFromText(
      R"({"claudeAiOauth":{"accessToken":"t","expiresAt":1763314377000,"subscriptionType":"team"},"x":1})");
  CHECK(r.status == CredStatus::Found);
  CHECK(r.creds.accessToken == "t");
  CHECK(r.creds.expiresAtMs == 1763314377000LL);
  CHECK(r.creds.plan == L"team");
}

TEST(test_creds_missing_oauth) {
  CHECK(LoadCredentialsFromText(R"({"other":1})").status == CredStatus::NotFound);
}

TEST(test_creds_truncated_is_unreadable) {
  CHECK(LoadCredentialsFromText(R"({"claudeAiOauth":{"accessToken":"t")").status == CredStatus::Unreadable);
  CHECK(LoadCredentialsFromText("").status == CredStatus::Unreadable);
  CHECK(LoadCredentialsFromText(R"({"claudeAiOauth":{"expiresAt":1}})").status == CredStatus::Unreadable);
}

TEST(test_creds_file_absent) {
  CHECK(LoadCredentials(L"C:\\nonexistent\\x.json").status == CredStatus::NotFound);
}

TEST(test_creds_file_found) {
  wchar_t tmp[MAX_PATH];
  GetTempPathW(MAX_PATH, tmp);
  std::wstring path = std::wstring(tmp) + L"cqw-test-creds.json";
  HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
  const char body[] = R"({"claudeAiOauth":{"accessToken":"abc","expiresAt":5}})";
  DWORD written = 0;
  WriteFile(h, body, sizeof(body) - 1, &written, nullptr);
  CloseHandle(h);
  auto r = LoadCredentials(path);
  DeleteFileW(path.c_str());
  CHECK(r.status == CredStatus::Found);
  CHECK(r.creds.accessToken == "abc");
  CHECK(r.creds.expiresAtMs == 5);
}

TEST(test_creds_path_env) {
  SetEnvironmentVariableW(L"CLAUDE_CONFIG_DIR", L"D:\\cfg");
  CHECK(CredentialsPath() == L"D:\\cfg\\.credentials.json");
  SetEnvironmentVariableW(L"CLAUDE_CONFIG_DIR", nullptr);
  wchar_t prof[MAX_PATH];
  GetEnvironmentVariableW(L"USERPROFILE", prof, MAX_PATH);
  CHECK(CredentialsPath() == std::wstring(prof) + L"\\.claude\\.credentials.json");
}

// ---------------------------------------------------------------- state

namespace {
HttpOutcome Http(int status, std::string body = "", bool transportOk = true) {
  HttpOutcome r;
  r.transportOk = transportOk;
  r.status = status;
  r.body = std::move(body);
  return r;
}
CredResult Creds(CredStatus s, int64_t expiresAtMs = 0) {
  CredResult r;
  r.status = s;
  r.creds.accessToken = s == CredStatus::Found ? "t" : "";
  r.creds.expiresAtMs = expiresAtMs;
  r.creds.plan = L"team";
  return r;
}
std::wstring Hhmm(int64_t) { return L"15:52"; }
SYSTEMTIME At(WORD dow, WORD h, WORD m) {
  SYSTEMTIME st{};
  st.wYear = 2026; st.wMonth = 11; st.wDay = 18;
  st.wDayOfWeek = dow; st.wHour = h; st.wMinute = m;
  return st;
}
bool Inside(POINT p, SIZE s, RECT work) {
  return p.x >= work.left + 8 && p.y >= work.top + 8 && p.x + s.cx <= work.right - 8 &&
         p.y + s.cy <= work.bottom - 8;
}
}  // namespace

TEST(test_classify_matrix) {
  auto ok = ClassifyResponse(Http(200, ReadFile("tests/fixtures/usage-2026-11-18.json")));
  CHECK(ok.state == AppState::Ok);
  CHECK(ok.snapshot && ok.snapshot->rows.size() == 3);
  CHECK(ok.httpStatus == 200);
  CHECK(ClassifyResponse(Http(200, "{}")).state == AppState::ApiChanged);
  CHECK(ClassifyResponse(Http(401)).state == AppState::Expired);
  CHECK(ClassifyResponse(Http(403)).state == AppState::AccessDenied);
  CHECK(ClassifyResponse(Http(429)).state == AppState::Error);
  CHECK(ClassifyResponse(Http(503)).state == AppState::Error);
  CHECK(ClassifyResponse(Http(0, "", false)).state == AppState::Error);
  CHECK(ClassifyResponse(Http(404)).state == AppState::ApiChanged);
  CHECK(ClassifyResponse(Http(404)).httpStatus == 404);
  CHECK(ClassifyResponse(Http(400)).state == AppState::ApiChanged);
  CHECK(!ClassifyResponse(Http(503)).snapshot);
}

TEST(test_decide_poll) {
  const int64_t now = 1'000'000;
  CHECK(DecidePoll(Creds(CredStatus::Found, now + 1), now, false, 0) == PollAction::Request);
  CHECK(DecidePoll(Creds(CredStatus::Found, now - 1), now, false, 0) == PollAction::MarkExpired);
  CHECK(DecidePoll(Creds(CredStatus::NotFound), now, false, 0) == PollAction::MarkNoLogin);
  CHECK(DecidePoll(Creds(CredStatus::Unreadable), now, false, 0) == PollAction::KeepPrevious);
  CHECK(DecidePoll(Creds(CredStatus::Found, now + 1), now, false, now + 5000) == PollAction::Skip);
}

TEST(test_decide_poll_in_flight) {
  const int64_t now = 1'000'000;
  CHECK(DecidePoll(Creds(CredStatus::Found, now + 1), now, true, 0) == PollAction::Skip);
  CHECK(DecidePoll(Creds(CredStatus::NotFound), now, true, 0) == PollAction::Skip);
}

TEST(test_apply_keeps_last_snapshot) {
  AppModel m;
  ApplyCredResult(m, PollAction::Request, Creds(CredStatus::Found, 99));
  CHECK(m.plan == L"team");
  CHECK(m.tokenExpiresAtMs == 99);
  ApplyClassified(m, ClassifyResponse(Http(200, ReadFile("tests/fixtures/usage-2026-11-18.json"))), 1000);
  CHECK(m.state == AppState::Ok);
  CHECK(m.last && m.last->rows.size() == 3);
  CHECK(m.lastUpdatedUtc == 1000);
  ApplyClassified(m, ClassifyResponse(Http(503)), 1060);
  CHECK(m.state == AppState::Error);
  CHECK(m.last && m.last->rows.size() == 3);
  CHECK(m.lastUpdatedUtc == 1000);
  CHECK(StatusLine(m, 5000, Hhmm) == L"Couldn't reach Claude — last updated 15:52");
}

TEST(test_apply_unreadable_keeps_state) {
  AppModel m;
  ApplyClassified(m, ClassifyResponse(Http(200, ReadFile("tests/fixtures/usage-2026-11-18.json"))), 1000);
  ApplyCredResult(m, PollAction::KeepPrevious, Creds(CredStatus::Unreadable));
  CHECK(m.state == AppState::Ok);
  CHECK(m.last.has_value());
}

TEST(test_apply_no_login_and_expired) {
  AppModel m;
  ApplyClassified(m, ClassifyResponse(Http(200, ReadFile("tests/fixtures/usage-2026-11-18.json"))), 1000);
  ApplyCredResult(m, PollAction::MarkExpired, Creds(CredStatus::Found, 5));
  CHECK(m.state == AppState::Expired);
  CHECK(m.last.has_value());  // stale numbers still shown with the expired notice
  ApplyCredResult(m, PollAction::MarkNoLogin, Creds(CredStatus::NotFound));
  CHECK(m.state == AppState::NoLogin);
  CHECK(!m.last);  // logged out: don't show someone's stale numbers
}

TEST(test_status_lines) {
  AppModel m;
  m.state = AppState::Ok; m.lastUpdatedUtc = 1;
  CHECK(StatusLine(m, 5000, Hhmm) == L"Updated 15:52");
  m.state = AppState::NoLogin;
  CHECK(StatusLine(m, 5000, Hhmm) == L"Not connected");
  m.state = AppState::Expired;
  CHECK(StatusLine(m, 5000, Hhmm) == L"Login expired — open Claude Code to renew");
  m.state = AppState::AccessDenied;
  CHECK(StatusLine(m, 5000, Hhmm) == L"Access denied — run /login in Claude Code");
  m.state = AppState::Error;
  CHECK(StatusLine(m, 5000, Hhmm) == L"Couldn't reach Claude — last updated 15:52");
  m.lastUpdatedUtc.reset();
  CHECK(StatusLine(m, 5000, Hhmm) == L"Couldn't reach Claude");
  m.state = AppState::ApiChanged; m.lastHttpStatus = 404;
  CHECK(StatusLine(m, 5000, Hhmm) == L"Usage API changed (HTTP 404) — app may need an update");
  m.lastHttpStatus = 200;
  CHECK(StatusLine(m, 5000, Hhmm) == L"Usage API changed — app may need an update");
}

TEST(test_status_line_now) {
  AppModel m;
  m.state = AppState::Ok; m.lastUpdatedUtc = 1000;
  CHECK(StatusLine(m, 1000, Hhmm) == L"Updated now (15:52)");
  CHECK(StatusLine(m, 1059, Hhmm) == L"Updated now (15:52)");
  CHECK(StatusLine(m, 1060, Hhmm) == L"Updated 15:52");  // a full minute ago
  CHECK(StatusLine(m, 990, Hhmm) == L"Updated now (15:52)");  // clock stepped back: still "now"
  m.state = AppState::Error;
  CHECK(StatusLine(m, 1010, Hhmm) == L"Couldn't reach Claude — last updated now (15:52)");
}

TEST(test_format_reset_soon) {
  const int64_t now = 1'800'000'000;
  auto at1740 = [](int64_t) { return At(1, 17, 40); };
  CHECK(FormatReset(now + 4800, now, at1740) == L"Resets 17:40 (in 1h 20m)");
  CHECK(FormatReset(now + 300, now, at1740) == L"Resets 17:40 (in 5m)");
  CHECK(FormatReset(now + 7200, now, at1740) == L"Resets 17:40 (in 2h)");
  CHECK(FormatReset(now + 30, now, at1740) == L"Resets 17:40 (in 1m)");
}

TEST(test_format_reset_far) {
  const int64_t now = 1'800'000'000;
  CHECK(FormatReset(now + 5 * 86400, now, [](int64_t) { return At(0, 13, 0); }) == L"Resets Sun 13:00");
  CHECK(FormatReset(now + 2 * 86400, now, [](int64_t) { return At(3, 9, 5); }) == L"Resets Wed 09:05");
}

TEST(test_format_reset_past) {
  const int64_t now = 1'800'000'000;
  CHECK(FormatReset(now - 60, now, [](int64_t) { return At(1, 17, 40); }) == L"Resets now");
  CHECK(FormatReset(now, now, [](int64_t) { return At(1, 17, 40); }) == L"Resets now");
}

TEST(test_bar_fraction_clamps) {
  CHECK(BarFraction(62.0) == 0.62);
  CHECK(BarFraction(150.0) == 1.0);
  CHECK(BarFraction(-5.0) == 0.0);
  CHECK(BarFraction(std::nullopt) == 0.0);
  CHECK(FormatPercent(150.0) == L"150%");
  CHECK(FormatPercent(62.4) == L"62%");
  CHECK(FormatPercent(0.0) == L"0%");
  CHECK(FormatPercent(std::nullopt) == L"—");
}

TEST(test_place_popup_bottom_taskbar) {
  RECT work{0, 0, 1920, 1040};
  SIZE s{300, 280};
  POINT p = PlacePopup(RECT{1800, 1050, 1816, 1066}, work, s);
  CHECK(Inside(p, s, work));
  CHECK(p.y + s.cy == 1040 - 8);
  CHECK(p.x + s.cx <= 1920 - 8);
}

TEST(test_place_popup_top_taskbar) {
  RECT work{0, 40, 1920, 1080};
  SIZE s{300, 280};
  POINT p = PlacePopup(RECT{1800, 10, 1816, 26}, work, s);
  CHECK(Inside(p, s, work));
  CHECK(p.y == 40 + 8);
}

TEST(test_place_popup_left_taskbar) {
  RECT work{60, 0, 1920, 1080};
  SIZE s{300, 280};
  POINT p = PlacePopup(RECT{20, 1000, 36, 1016}, work, s);
  CHECK(Inside(p, s, work));
  CHECK(p.x == 60 + 8);
}

TEST(test_place_popup_right_taskbar) {
  RECT work{0, 0, 1860, 1080};
  SIZE s{300, 280};
  POINT p = PlacePopup(RECT{1880, 500, 1896, 516}, work, s);
  CHECK(Inside(p, s, work));
  CHECK(p.x + s.cx == 1860 - 8);
}

TEST(test_place_popup_anchor_inside_work_area) {
  // Overflow flyout / auto-hidden taskbar: icon sits inside the work area -> popup above it.
  RECT work{0, 0, 1920, 1080};
  SIZE s{300, 280};
  POINT p = PlacePopup(RECT{1700, 900, 1716, 916}, work, s);
  CHECK(Inside(p, s, work));
  CHECK(p.y + s.cy <= 900);
}

TEST(test_time_helpers) {
  long long nowMs = static_cast<long long>(std::time(nullptr)) * 1000;
  CHECK(std::llabs(NowUnixMs() - nowMs) < 5000);
  // 2026-11-18T17:40:00Z: local conversion must agree with the OS's own UTC->local conversion.
  SYSTEMTIME utc{};
  utc.wYear = 2026; utc.wMonth = 11; utc.wDay = 18; utc.wHour = 17; utc.wMinute = 40;
  SYSTEMTIME expect{};
  SystemTimeToTzSpecificLocalTime(nullptr, &utc, &expect);
  SYSTEMTIME got = UnixToLocal(1795023600);
  CHECK(got.wYear == expect.wYear && got.wMonth == expect.wMonth && got.wDay == expect.wDay);
  CHECK(got.wHour == expect.wHour && got.wMinute == expect.wMinute && got.wDayOfWeek == expect.wDayOfWeek);
  wchar_t buf[8];
  swprintf_s(buf, L"%02u:%02u", expect.wHour, expect.wMinute);
  CHECK(LocalHhmm(1795023600) == buf);
}

namespace {
// Returns ARGB pixels of an icon's color bitmap (top-down), or empty on failure.
std::vector<uint32_t> IconPixels(HICON icon, int size) {
  std::vector<uint32_t> px;
  ICONINFO ii{};
  if (!icon || !GetIconInfo(icon, &ii)) return px;
  BITMAPINFO bi{};
  bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
  bi.bmiHeader.biWidth = size;
  bi.bmiHeader.biHeight = -size;
  bi.bmiHeader.biPlanes = 1;
  bi.bmiHeader.biBitCount = 32;
  bi.bmiHeader.biCompression = BI_RGB;
  px.resize(static_cast<size_t>(size) * size);
  HDC dc = GetDC(nullptr);
  if (!GetDIBits(dc, ii.hbmColor, 0, size, px.data(), &bi, DIB_RGB_COLORS)) px.clear();
  ReleaseDC(nullptr, dc);
  DeleteObject(ii.hbmColor);
  DeleteObject(ii.hbmMask);
  return px;
}
}  // namespace

TEST(test_tray_icon_usage_fill) {
  // Session usage greys the orange body from the left: 0% all orange, 50% left half grey, 100% all grey.
  const int n = 32;
  const int left = n / 2 * n + 3, right = n / 2 * n + (n - 4);  // orange body pixels near each edge
  auto rgb = [](uint32_t v) { return v & 0xFFFFFF; };
  struct Case { double fraction; uint32_t left, right; } cases[] = {
      {0.0, 0xD97757, 0xD97757}, {0.5, 0xA0A0A0, 0xD97757}, {1.0, 0xA0A0A0, 0xA0A0A0}};
  for (const Case& c : cases) {
    HICON icon = MakeTrayIcon(DotColor::None, n, c.fraction);
    auto px = IconPixels(icon, n);
    DestroyIcon(icon);
    CHECK(px.size() == n * n);
    if (px.size() != n * n) continue;
    CHECK(rgb(px[left]) == c.left);
    CHECK(rgb(px[right]) == c.right);
  }
  // The used/unused boundary follows the fraction: at 25%, column 6 (center 6.5/32 ≈ 20%) is grey and
  // column 10 (≈ 33%) is orange. Row 4 crosses the orange body above the white gauge ring.
  HICON q = MakeTrayIcon(DotColor::None, n, 0.25);
  auto qp = IconPixels(q, n);
  DestroyIcon(q);
  CHECK(qp.size() == n * n);
  if (qp.size() != n * n) return;
  CHECK(rgb(qp[4 * n + 6]) == 0xA0A0A0);
  CHECK(rgb(qp[4 * n + 10]) == 0xD97757);
}

TEST(test_session_fraction) {
  AppModel m;
  CHECK(SessionFraction(m) == 0.0);  // no data yet: full orange
  ApplyClassified(m, ClassifyResponse(Http(200, ReadFile("tests/fixtures/usage-2026-11-18.json"))), 1000);
  CHECK(SessionFraction(m) == 0.89);  // the "Current session" row, not the weekly ones
  m.last->rows[0].percent = 150.0;
  CHECK(SessionFraction(m) == 1.0);
  m.last->rows.erase(m.last->rows.begin());  // no session row → 0
  CHECK(SessionFraction(m) == 0.0);
}

TEST(test_poll_interval_by_last_check) {
  const int64_t min = 60 * 1000;
  const int64_t now = 1000 * min;
  CHECK(PollIntervalMs(now, now) == 1 * min);              // just hovered
  CHECK(PollIntervalMs(now, now - 29 * min) == 1 * min);   // checked 29 min ago
  CHECK(PollIntervalMs(now, now - 30 * min) == 3 * min);   // 30 min without a check
  CHECK(PollIntervalMs(now, now - 300 * min) == 3 * min);
}

TEST(test_poll_due) {
  const int64_t min = 60 * 1000;
  const int64_t now = 1000 * min;
  const int64_t recent = now - 5 * min, stale = now - 45 * min;
  CHECK(PollDue(now, recent, now - 1 * min));     // fast cadence, a minute since the last poll
  CHECK(PollDue(now, recent, now - 58 * 1000));   // a 1-minute timer that fires a little early still polls
  CHECK(!PollDue(now, recent, now - 30 * 1000));
  CHECK(!PollDue(now, stale, now - 1 * min));     // slow cadence: skip the 1- and 2-minute ticks
  CHECK(!PollDue(now, stale, now - 2 * min));
  CHECK(PollDue(now, stale, now - 3 * min));
  CHECK(PollDue(now, stale, now - 3 * min + 2000));  // early timer at the 3rd tick
}

TEST(test_tray_icon_pixels) {
  const int n = 32;
  HICON plain = MakeTrayIcon(DotColor::None, n);
  HICON red = MakeTrayIcon(DotColor::Red, n);
  HICON grey = MakeTrayIcon(DotColor::Grey, n);
  auto p = IconPixels(plain, n), r = IconPixels(red, n), g = IconPixels(grey, n);
  CHECK(p.size() == n * n && r.size() == n * n && g.size() == n * n);
  if (p.size() != n * n || r.size() != n * n || g.size() != n * n) return;
  auto alpha = [](uint32_t v) { return v >> 24; };
  auto rgb = [](uint32_t v) { return v & 0xFFFFFF; };
  CHECK(alpha(p[0]) == 0);                       // transparent corner
  CHECK(alpha(p[n / 2 * n + 3]) == 255);         // opaque body (left edge of the disc, on orange)
  CHECK(rgb(p[n / 2 * n + 3]) == 0xD97757);      // Claude orange
  const int dot = 25 * n + 25;                   // center of the status dot
  CHECK(rgb(r[dot]) == 0xE0302A);
  CHECK(rgb(g[dot]) == 0x808080);
  CHECK(rgb(p[dot]) != 0xE0302A && rgb(p[dot]) != 0x808080);
  DestroyIcon(plain); DestroyIcon(red); DestroyIcon(grey);
}

TEST(test_popup_view_ok) {
  AppModel m;
  ApplyCredResult(m, PollAction::Request, Creds(CredStatus::Found, 99));
  ApplyClassified(m, ClassifyResponse(Http(200, ReadFile("tests/fixtures/usage-2026-11-18.json"))), 1795022800);
  auto v = BuildPopupView(m, 1795022800, Hhmm, [](int64_t) { return At(1, 17, 39); });
  CHECK(v.plan == L"Team");
  CHECK(v.rows.size() == 3);
  if (v.rows.size() != 3) return;
  CHECK(v.rows[0].label == L"Current session");
  CHECK(v.rows[0].percent == L"89%");
  CHECK(v.rows[0].fraction == 0.89);
  CHECK(v.rows[0].reset == L"Resets 17:39 (in 14m)");
  CHECK(v.rows[2].label == L"Weekly · Fable");
  CHECK(v.status == L"Updated now (15:52)");  // just fetched
  CHECK(!v.statusIsError);
  CHECK(!v.showSettingsLink);
}

TEST(test_popup_view_error_keeps_rows) {
  AppModel m;
  ApplyClassified(m, ClassifyResponse(Http(200, ReadFile("tests/fixtures/usage-2026-11-18.json"))), 1795022800);
  ApplyClassified(m, ClassifyResponse(Http(404)), 1795022860);
  auto v = BuildPopupView(m, 1795022860, Hhmm, [](int64_t) { return At(1, 17, 39); });
  CHECK(v.rows.size() == 3);
  CHECK(v.statusIsError);
  CHECK(v.status == L"Usage API changed (HTTP 404) — app may need an update");
}

TEST(test_popup_view_no_login) {
  AppModel m;  // initial state: NoLogin
  auto v = BuildPopupView(m, 1795022800, Hhmm, [](int64_t) { return At(1, 17, 39); });
  CHECK(v.rows.empty());
  CHECK(v.showSettingsLink);
  CHECK(v.status == L"Not connected");
  CHECK(!v.statusIsError);
  CHECK(v.plan.empty());
}

TEST(test_popup_view_missing_values) {
  AppModel m;
  ApplyClassified(m, ClassifyResponse(Http(200, R"({"limits":[{"kind":"session","percent":null,"resets_at":null}]})")), 5);
  auto v = BuildPopupView(m, 5, Hhmm, [](int64_t) { return At(1, 17, 39); });
  CHECK(v.rows.size() == 1);
  if (v.rows.empty()) return;
  CHECK(v.rows[0].percent == L"—");
  CHECK(v.rows[0].reset.empty());
  CHECK(v.rows[0].fraction == 0.0);
}

namespace {
std::wstring FakeDateTime(int64_t unixSec) { return L"DT" + std::to_wstring(unixSec); }
}

TEST(test_settings_view_found) {
  AppModel m;
  ApplyCredResult(m, PollAction::Request, Creds(CredStatus::Found, 1795024800123LL));
  ApplyClassified(m, ClassifyResponse(Http(200, ReadFile("tests/fixtures/usage-2026-11-18.json"))), 1000);
  auto v = BuildSettingsView(m, L"C:\\u\\.claude\\.credentials.json", 1030, Hhmm, FakeDateTime);
  CHECK(v.login == L"Found");
  CHECK(v.plan == L"Team");
  CHECK(v.tokenExpires == L"DT1795024800");
  CHECK(v.path == L"C:\\u\\.claude\\.credentials.json");
  CHECK(v.lastResult == L"Updated now (15:52)");  // updated 30 s ago
  CHECK(!v.showInstructions);
}

TEST(test_settings_view_not_found) {
  AppModel m;
  ApplyCredResult(m, PollAction::MarkNoLogin, Creds(CredStatus::NotFound));
  auto v = BuildSettingsView(m, L"p", 5000, Hhmm, FakeDateTime);
  CHECK(v.login == L"Not found");
  CHECK(v.plan == L"—");
  CHECK(v.tokenExpires == L"—");
  CHECK(v.lastResult == L"Not connected");
  CHECK(v.showInstructions);
}

TEST(test_settings_view_expired_and_denied) {
  AppModel m;
  ApplyCredResult(m, PollAction::MarkExpired, Creds(CredStatus::Found, 5000));
  auto v = BuildSettingsView(m, L"p", 5000, Hhmm, FakeDateTime);
  CHECK(v.login == L"Expired");
  CHECK(v.tokenExpires == L"DT5");
  CHECK(v.showInstructions);
  m.state = AppState::AccessDenied;
  v = BuildSettingsView(m, L"p", 5000, Hhmm, FakeDateTime);
  CHECK(v.login == L"Found");
  CHECK(v.lastResult == L"Access denied — run /login in Claude Code");
  CHECK(v.showInstructions);
  m.state = AppState::Error;
  CHECK(!BuildSettingsView(m, L"p", 5000, Hhmm, FakeDateTime).showInstructions);
}

TEST(test_unreadable_streak_becomes_error) {
  AppModel m;
  ApplyClassified(m, ClassifyResponse(Http(200, ReadFile("tests/fixtures/usage-2026-11-18.json"))), 1000);
  ApplyCredResult(m, PollAction::KeepPrevious, Creds(CredStatus::Unreadable));
  ApplyCredResult(m, PollAction::KeepPrevious, Creds(CredStatus::Unreadable));
  CHECK(m.state == AppState::Ok);  // a brief rewrite is tolerated
  ApplyCredResult(m, PollAction::KeepPrevious, Creds(CredStatus::Unreadable));
  CHECK(m.state == AppState::Error);  // 3 minutes unreadable: say so
  CHECK(m.last.has_value());
  CHECK(StatusLine(m, 5000, Hhmm) == L"Can't read the Claude Code login file — last updated 15:52");
  // Readable again: back to normal on the next result.
  ApplyCredResult(m, PollAction::Request, Creds(CredStatus::Found, 99));
  ApplyClassified(m, ClassifyResponse(Http(200, ReadFile("tests/fixtures/usage-2026-11-18.json"))), 1060);
  CHECK(m.state == AppState::Ok);
  CHECK(StatusLine(m, 5000, Hhmm) == L"Updated 15:52");
  ApplyCredResult(m, PollAction::KeepPrevious, Creds(CredStatus::Unreadable));
  CHECK(m.state == AppState::Ok);  // streak restarted
}

TEST(test_decide_poll_unknown_expiry) {
  // No expiresAt in the file → unknown, not expired: ask the API (a 401 still catches a dead token).
  CHECK(DecidePoll(Creds(CredStatus::Found, 0), 1'000'000, false, 0) == PollAction::Request);
}

TEST(test_dot_colors) {
  CHECK(DotFor(AppState::Ok) == DotColor::None);
  CHECK(DotFor(AppState::NoLogin) == DotColor::Grey);
  CHECK(DotFor(AppState::Expired) == DotColor::Grey);
  CHECK(DotFor(AppState::AccessDenied) == DotColor::Grey);
  CHECK(DotFor(AppState::Error) == DotColor::Red);
  CHECK(DotFor(AppState::ApiChanged) == DotColor::Red);
}

// ---------------------------------------------------------------- main

namespace {
const char* StateName(AppState s) {
  switch (s) {
    case AppState::Ok: return "Ok";
    case AppState::NoLogin: return "NoLogin";
    case AppState::Expired: return "Expired";
    case AppState::AccessDenied: return "AccessDenied";
    case AppState::Error: return "Error";
    case AppState::ApiChanged: return "ApiChanged";
  }
  return "?";
}
std::string Narrow(const std::wstring& w) {
  int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
  std::string s(static_cast<size_t>(n), '\0');
  WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), s.data(), n, nullptr, nullptr);
  return s;
}

// Calls the real endpoint with the real login. Prints status and parsed rows only — never the token or body.
int RunLive() {
  SetConsoleOutputCP(CP_UTF8);
  CredResult c = LoadCredentials(CredentialsPath());
  if (c.status != CredStatus::Found) {
    std::printf("credentials: %s\n", c.status == CredStatus::NotFound ? "NotFound" : "Unreadable");
    return 1;
  }
  HttpOutcome r = HttpGetUsage(c.creds.accessToken);
  Classified k = ClassifyResponse(r);
  std::printf("HTTP %d%s\n", r.status, r.transportOk ? "" : " (transport failure)");
  if (r.retryAfterSec) std::printf("Retry-After %d\n", *r.retryAfterSec);
  std::printf("%s\n", StateName(k.state));
  if (k.snapshot) {
    for (const auto& row : k.snapshot->rows) {
      std::printf("%s | %s | %lld\n", Narrow(row.label).c_str(), Narrow(FormatPercent(row.percent)).c_str(),
                  row.resetsAtUtc ? static_cast<long long>(*row.resetsAtUtc) : -1LL);
    }
  }
  return k.state == AppState::Ok ? 0 : 1;
}
}  // namespace

int main(int argc, char** argv) {
  if (argc > 1 && std::strcmp(argv[1], "--live") == 0) return RunLive();
  int passed = 0, failed = 0;
  for (auto& t : Registry()) {
    int before = g_failures;
    t.fn();
    if (g_failures == before) { ++passed; } else { ++failed; std::printf("FAILED %s\n", t.name); }
  }
  std::printf("%d passed, %d failed\n", passed, failed);
  return failed ? 1 : 0;
}
