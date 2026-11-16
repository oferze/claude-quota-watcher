#pragma once
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// Minimal JSON DOM. Strings are UTF-8.
struct JsonValue {
  enum class Type { Null, Bool, Number, String, Array, Object };
  Type type = Type::Null;
  bool b = false;
  double num = 0;
  std::string str;
  std::vector<JsonValue> arr;
  std::vector<std::pair<std::string, JsonValue>> obj;

  // nullptr if this isn't an object or the key is absent.
  const JsonValue* get(std::string_view key) const;
  // Follows nested object keys; nullptr if any step is missing.
  const JsonValue* path(std::initializer_list<std::string_view> keys) const;

  bool isNull() const { return type == Type::Null; }
  bool isNumber() const { return type == Type::Number; }
  bool isString() const { return type == Type::String; }
  bool isObject() const { return type == Type::Object; }
  bool isArray() const { return type == Type::Array; }
};

// nullopt on any syntax error (including trailing garbage).
std::optional<JsonValue> ParseJson(std::string_view text);

std::wstring Utf8ToWide(std::string_view s);
