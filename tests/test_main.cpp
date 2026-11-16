// Unit tests for Claude Quota Watcher. Run: build.bat test
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "json.h"
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

// ---------------------------------------------------------------- main

int main(int argc, char** argv) {
  (void)argc; (void)argv;
  int passed = 0, failed = 0;
  for (auto& t : Registry()) {
    int before = g_failures;
    t.fn();
    if (g_failures == before) { ++passed; } else { ++failed; std::printf("FAILED %s\n", t.name); }
  }
  std::printf("%d passed, %d failed\n", passed, failed);
  return failed ? 1 : 0;
}
