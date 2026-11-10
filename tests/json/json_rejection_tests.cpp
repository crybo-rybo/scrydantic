// Where and why the JSON core rejects a text (src/json/document.hpp): every
// fault's phrase, the byte each one points at, and how that byte's line and
// column are counted. The parser and the validator must agree on all of it.

#include "json/document.hpp"

#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>

namespace {

namespace json = scrydantic::detail::json;

// The parser's rejection of `text` as describe() spells it, after checking that
// the validator rejects it at the same byte for the same reason.
[[nodiscard]] std::string rejection(const std::string_view text) {
  const auto parsed = json::parse(text);
  REQUIRE_FALSE(parsed);
  const auto diagnosed = json::diagnose(text);
  REQUIRE(diagnosed);
  CHECK(*diagnosed == parsed.error());
  CHECK(parsed.error().offset <= text.size());
  return json::describe(text, parsed.error());
}

struct Case {
  std::string_view text{};
  std::string_view rejection{};
};

void check_cases(const std::initializer_list<Case> cases) {
  for (const auto& [text, expected] : cases) {
    INFO(text);
    CHECK(rejection(text) == expected);
  }
}

} // namespace

TEST_CASE("json rejections name an empty text, a truncated one, and a trailing one") {
  check_cases({
      {"", "empty input at line 1, column 1"},
      {" \n\t", "empty input at line 2, column 2"},
      {"[", "unexpected end of input at line 1, column 2"},
      {"[1,", "unexpected end of input at line 1, column 4"},
      {R"({"a")", "unexpected end of input at line 1, column 5"},
      {R"({"a":)", "unexpected end of input at line 1, column 6"},
      {R"({"a":1 )", "unexpected end of input at line 1, column 8"},
      {"[tru", "unexpected end of input at line 1, column 5"},
      {"-", "unexpected end of input at line 1, column 2"},
      {"1.", "unexpected end of input at line 1, column 3"},
      {"1e+", "unexpected end of input at line 1, column 4"},
      {"{} x", "unexpected text after the value at line 1, column 4"},
      {"[1]]", "unexpected text after the value at line 1, column 4"},
      // A leading zero ends the number, so the digit after it is left over.
      {"01", "unexpected text after the value at line 1, column 2"},
  });
}

TEST_CASE("json rejections say what the grammar expected") {
  check_cases({
      {"[,1]", "expected a value at line 1, column 2"},
      {"[1,]", "expected a value at line 1, column 4"},
      {"}", "expected a value at line 1, column 1"},
      {"+1", "expected a value at line 1, column 1"},
      {".5", "expected a value at line 1, column 1"},
      {"[nul]", "expected a value at line 1, column 2"},
      {"True", "expected a value at line 1, column 1"},
      {"\xEF\xBB\xBF{}", "expected a value at line 1, column 1"},
      {"{1:2}", "expected a string key at line 1, column 2"},
      {R"({"a":1,})", "expected a string key at line 1, column 8"},
      {R"({"a" 1})", "expected ':' at line 1, column 6"},
      {"[1 2]", "expected ',' or ']' at line 1, column 4"},
      {"[1}", "expected ',' or ']' at line 1, column 3"},
      {R"({"a":1 "b":2})", "expected ',' or '}' at line 1, column 8"},
      {R"({"a":1])", "expected ',' or '}' at line 1, column 7"},
      {"-x", "expected a digit at line 1, column 2"},
      {"1.x", "expected a digit at line 1, column 3"},
      {"1.e5", "expected a digit at line 1, column 3"},
      {"1e+x", "expected a digit at line 1, column 4"},
  });
}

TEST_CASE("json rejections point at the start of a number out of range") {
  check_cases({
      {"1e400", "number out of range at line 1, column 1"},
      {"[0, -1e-400]", "number out of range at line 1, column 5"},
  });
}

TEST_CASE("json rejections point at the opening quote of an unterminated string") {
  // Wherever in the string the text ends, an escape or a UTF-8 sequence
  // included, the string is what was left open.
  check_cases({
      {R"("abc)", "unterminated string at line 1, column 1"},
      {R"([1, "a\)", "unterminated string at line 1, column 5"},
      {R"("\u12)", "unterminated string at line 1, column 1"},
      {R"("\uD83D)", "unterminated string at line 1, column 1"},
      {R"("\uD83D\)", "unterminated string at line 1, column 1"},
      {R"("\uD83D\uDE0)", "unterminated string at line 1, column 1"},
      {"\"\xC3", "unterminated string at line 1, column 1"},
      {"\"\xF0\x9F\x98", "unterminated string at line 1, column 1"},
  });
}

TEST_CASE("json rejections point at the bad byte or escape inside a string") {
  check_cases({
      {"\"a\nb\"", "unescaped control character at line 1, column 3"},
      {"\"\t\"", "unescaped control character at line 1, column 2"},
      {R"("a\qb")", "invalid escape at line 1, column 3"},
      {R"("\u12G4")", "invalid escape at line 1, column 2"},
      // Too few digits before the closing quote: the quote is no digit.
      {R"("\u1")", "invalid escape at line 1, column 2"},
      // A surrogate pair is one token, so a broken one points at its start.
      {R"("\uDC00")", "invalid escape at line 1, column 2"},
      {R"("\uD800")", "invalid escape at line 1, column 2"},
      {R"("\uD800\n")", "invalid escape at line 1, column 2"},
      {R"("x\uD800A")", "invalid escape at line 1, column 3"},
      {"\"\xFF\"", "invalid UTF-8 at line 1, column 2"},
      {"\"\x80\"", "invalid UTF-8 at line 1, column 2"},
      {"\"\xC0\xAF\"", "invalid UTF-8 at line 1, column 2"},
      {"\"\xED\xA0\x80\"", "invalid UTF-8 at line 1, column 2"},
      {"\"\xE2\x28\xA1\"", "invalid UTF-8 at line 1, column 2"},
      // A cut-short sequence whose bytes are already wrong is not cut short.
      {"\"\xE2(", "invalid UTF-8 at line 1, column 2"},
  });
}

TEST_CASE("json rejections point at the container that nests too deep") {
  CHECK(rejection(std::string(257, '[')) ==
        "nesting deeper than 256 at line 1, column 257");
  CHECK(rejection("{\"a\":" + std::string(256, '[')) ==
        "nesting deeper than 256 at line 1, column 261");
  // Nesting within the limit, then a fault of another kind.
  CHECK(rejection(std::string(256, '[') + "\"\xFF\"") ==
        "invalid UTF-8 at line 1, column 258");
}

TEST_CASE("json rejection lines end at each line feed") {
  check_cases({
      {"{\n  \"a\": 1,\n  \"b\" 2\n}", "expected ':' at line 3, column 7"},
      // A carriage return is a column of the line it ends, so CRLF text has
      // the lines and columns of LF text.
      {"[\r\n  1\r\n  2]", "expected ',' or ']' at line 3, column 3"},
      {"\n\n\n", "empty input at line 4, column 1"},
      {"[1,\n", "unexpected end of input at line 2, column 1"},
      // A tab is one column.
      {"[\t1\t2]", "expected ',' or ']' at line 1, column 5"},
  });
}

TEST_CASE("json rejection columns count code points, not bytes") {
  // e-acute is two bytes, the euro sign three, and U+1F600 four: one column each.
  check_cases({
      {"[\"\xC3\xA9\xE2\x82\xAC\xF0\x9F\x98\x80\" x]",
       "expected ',' or ']' at line 1, column 8"},
      {"[\"\xC3\xA9\",\n \"\xE2\x82\xAC\xF0\x9F\x98\x80\" x]",
       "expected ',' or ']' at line 2, column 7"},
      {"{\"\xC3\xA9\":\"\xC3\xA9\xFF\"}", "invalid UTF-8 at line 1, column 8"},
  });
  CHECK(json::locate("\xC3\xA9x", 0) == json::Position{.line = 1, .column = 1});
  CHECK(json::locate("\xC3\xA9x", 2) == json::Position{.line = 1, .column = 2});
  CHECK(json::locate("\xC3\xA9x", 3) == json::Position{.line = 1, .column = 3});
  CHECK(json::locate("a\n\xF0\x9F\x98\x80", 6) ==
        json::Position{.line = 2, .column = 2});
}

TEST_CASE("json fault phrases are fixed") {
  CHECK(json::reason(json::Fault::empty_input) == "empty input");
  CHECK(json::reason(json::Fault::end_of_input) == "unexpected end of input");
  CHECK(json::reason(json::Fault::text_after_value) ==
        "unexpected text after the value");
  CHECK(json::reason(json::Fault::expected_value) == "expected a value");
  CHECK(json::reason(json::Fault::expected_key) == "expected a string key");
  CHECK(json::reason(json::Fault::expected_colon) == "expected ':'");
  CHECK(json::reason(json::Fault::expected_comma_or_bracket) == "expected ',' or ']'");
  CHECK(json::reason(json::Fault::expected_comma_or_brace) == "expected ',' or '}'");
  CHECK(json::reason(json::Fault::expected_digit) == "expected a digit");
  CHECK(json::reason(json::Fault::number_out_of_range) == "number out of range");
  CHECK(json::reason(json::Fault::unterminated_string) == "unterminated string");
  CHECK(json::reason(json::Fault::control_character) == "unescaped control character");
  CHECK(json::reason(json::Fault::invalid_escape) == "invalid escape");
  CHECK(json::reason(json::Fault::invalid_utf8) == "invalid UTF-8");
  CHECK(json::reason(json::Fault::too_deep) == "nesting deeper than 256");
}

TEST_CASE("json validation stays a yes or no over the same rejection") {
  CHECK(json::diagnose(R"({"a":[1,2]})") == std::nullopt);
  CHECK(json::diagnose("[1 2]") ==
        json::Rejection{.fault = json::Fault::expected_comma_or_bracket, .offset = 3});
  CHECK(json::check(std::string(257, '[')) == json::Validity::too_deep);
  CHECK(json::check("[1 2]") == json::Validity::invalid);
}
