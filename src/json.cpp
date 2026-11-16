#include "json.h"

#include <windows.h>

#include <charconv>
#include <cstdint>

namespace {

class Parser {
 public:
  explicit Parser(std::string_view s) : s_(s) {}

  std::optional<JsonValue> ParseDocument() {
    JsonValue v;
    SkipWs();
    if (!ParseValue(v, 0)) return std::nullopt;
    SkipWs();
    if (pos_ != s_.size()) return std::nullopt;
    return v;
  }

 private:
  static constexpr int kMaxDepth = 64;

  void SkipWs() {
    while (pos_ < s_.size() &&
           (s_[pos_] == ' ' || s_[pos_] == '\t' || s_[pos_] == '\n' || s_[pos_] == '\r')) {
      ++pos_;
    }
  }

  bool Literal(std::string_view lit) {
    if (s_.substr(pos_, lit.size()) != lit) return false;
    pos_ += lit.size();
    return true;
  }

  bool ParseValue(JsonValue& out, int depth) {
    if (depth > kMaxDepth || pos_ >= s_.size()) return false;
    char c = s_[pos_];
    switch (c) {
      case '{': return ParseObject(out, depth);
      case '[': return ParseArray(out, depth);
      case '"': out.type = JsonValue::Type::String; return ParseString(out.str);
      case 't': out.type = JsonValue::Type::Bool; out.b = true; return Literal("true");
      case 'f': out.type = JsonValue::Type::Bool; out.b = false; return Literal("false");
      case 'n': out.type = JsonValue::Type::Null; return Literal("null");
      default: return ParseNumber(out);
    }
  }

  bool ParseObject(JsonValue& out, int depth) {
    out.type = JsonValue::Type::Object;
    ++pos_;  // '{'
    SkipWs();
    if (pos_ < s_.size() && s_[pos_] == '}') { ++pos_; return true; }
    for (;;) {
      SkipWs();
      if (pos_ >= s_.size() || s_[pos_] != '"') return false;
      std::string key;
      if (!ParseString(key)) return false;
      SkipWs();
      if (pos_ >= s_.size() || s_[pos_] != ':') return false;
      ++pos_;
      SkipWs();
      JsonValue v;
      if (!ParseValue(v, depth + 1)) return false;
      out.obj.emplace_back(std::move(key), std::move(v));
      SkipWs();
      if (pos_ >= s_.size()) return false;
      if (s_[pos_] == ',') { ++pos_; continue; }
      if (s_[pos_] == '}') { ++pos_; return true; }
      return false;
    }
  }

  bool ParseArray(JsonValue& out, int depth) {
    out.type = JsonValue::Type::Array;
    ++pos_;  // '['
    SkipWs();
    if (pos_ < s_.size() && s_[pos_] == ']') { ++pos_; return true; }
    for (;;) {
      SkipWs();
      JsonValue v;
      if (!ParseValue(v, depth + 1)) return false;
      out.arr.push_back(std::move(v));
      SkipWs();
      if (pos_ >= s_.size()) return false;
      if (s_[pos_] == ',') { ++pos_; continue; }
      if (s_[pos_] == ']') { ++pos_; return true; }
      return false;
    }
  }

  static void AppendUtf8(std::string& out, uint32_t cp) {
    if (cp < 0x80) {
      out += static_cast<char>(cp);
    } else if (cp < 0x800) {
      out += static_cast<char>(0xC0 | (cp >> 6));
      out += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
      out += static_cast<char>(0xE0 | (cp >> 12));
      out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
      out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
      out += static_cast<char>(0xF0 | (cp >> 18));
      out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
      out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
      out += static_cast<char>(0x80 | (cp & 0x3F));
    }
  }

  bool Hex4(uint32_t& out) {
    if (pos_ + 4 > s_.size()) return false;
    uint32_t v = 0;
    for (int i = 0; i < 4; ++i) {
      char c = s_[pos_++];
      v <<= 4;
      if (c >= '0' && c <= '9') v |= static_cast<uint32_t>(c - '0');
      else if (c >= 'a' && c <= 'f') v |= static_cast<uint32_t>(c - 'a' + 10);
      else if (c >= 'A' && c <= 'F') v |= static_cast<uint32_t>(c - 'A' + 10);
      else return false;
    }
    out = v;
    return true;
  }

  bool ParseString(std::string& out) {
    ++pos_;  // opening quote
    while (pos_ < s_.size()) {
      char c = s_[pos_++];
      if (c == '"') return true;
      if (static_cast<unsigned char>(c) < 0x20) return false;
      if (c != '\\') { out += c; continue; }
      if (pos_ >= s_.size()) return false;
      char e = s_[pos_++];
      switch (e) {
        case '"': out += '"'; break;
        case '\\': out += '\\'; break;
        case '/': out += '/'; break;
        case 'b': out += '\b'; break;
        case 'f': out += '\f'; break;
        case 'n': out += '\n'; break;
        case 'r': out += '\r'; break;
        case 't': out += '\t'; break;
        case 'u': {
          uint32_t cp;
          if (!Hex4(cp)) return false;
          if (cp >= 0xD800 && cp <= 0xDBFF) {
            uint32_t lo;
            if (!Literal("\\u") || !Hex4(lo) || lo < 0xDC00 || lo > 0xDFFF) return false;
            cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
          } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
            return false;
          }
          AppendUtf8(out, cp);
          break;
        }
        default: return false;
      }
    }
    return false;  // unterminated
  }

  bool ParseNumber(JsonValue& out) {
    size_t start = pos_;
    if (pos_ < s_.size() && s_[pos_] == '-') ++pos_;
    size_t digits = pos_;
    while (pos_ < s_.size() && s_[pos_] >= '0' && s_[pos_] <= '9') ++pos_;
    if (pos_ == digits) return false;
    if (pos_ < s_.size() && s_[pos_] == '.') {
      ++pos_;
      size_t frac = pos_;
      while (pos_ < s_.size() && s_[pos_] >= '0' && s_[pos_] <= '9') ++pos_;
      if (pos_ == frac) return false;
    }
    if (pos_ < s_.size() && (s_[pos_] == 'e' || s_[pos_] == 'E')) {
      ++pos_;
      if (pos_ < s_.size() && (s_[pos_] == '+' || s_[pos_] == '-')) ++pos_;
      size_t exp = pos_;
      while (pos_ < s_.size() && s_[pos_] >= '0' && s_[pos_] <= '9') ++pos_;
      if (pos_ == exp) return false;
    }
    double v = 0;
    auto res = std::from_chars(s_.data() + start, s_.data() + pos_, v);
    if (res.ec != std::errc() || res.ptr != s_.data() + pos_) return false;
    out.type = JsonValue::Type::Number;
    out.num = v;
    return true;
  }

  std::string_view s_;
  size_t pos_ = 0;
};

}  // namespace

const JsonValue* JsonValue::get(std::string_view key) const {
  if (type != Type::Object) return nullptr;
  for (const auto& kv : obj) {
    if (kv.first == key) return &kv.second;
  }
  return nullptr;
}

const JsonValue* JsonValue::path(std::initializer_list<std::string_view> keys) const {
  const JsonValue* cur = this;
  for (auto k : keys) {
    cur = cur->get(k);
    if (!cur) return nullptr;
  }
  return cur;
}

std::optional<JsonValue> ParseJson(std::string_view text) {
  return Parser(text).ParseDocument();
}

std::wstring Utf8ToWide(std::string_view s) {
  if (s.empty()) return {};
  int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
  if (n <= 0) return {};
  std::wstring w(static_cast<size_t>(n), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
  return w;
}
