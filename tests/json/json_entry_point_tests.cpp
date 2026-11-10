// What the JSON core accepts, seen from outside it: the public JsonView and the
// text operations in <scrydantic/detail/json_text.hpp> that the reflected codec
// calls are thin layers over the core's document (src/json/document.hpp), which
// json_fixture_tests.cpp holds to the golden fixtures. So every entry point must
// give the document's verdict and write the document's canonical text, checked
// here on a table of adversarial documents named by shape and a handful of
// representative ones.

#include "json/document.hpp"

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <limits>
#include <scrydantic/detail/json_text.hpp>
#include <scrydantic/error.hpp>
#include <scrydantic/json.hpp>
#include <string>
#include <string_view>

namespace {

using scrydantic::JsonView;
namespace json = scrydantic::detail::json;
namespace core = scrydantic::detail;

// Every entry point's verdict on `input`, each checked against the document's.
[[nodiscard]] bool core_accepts(const std::string_view input) {
  const auto document = json::parse(input);
  const auto viewed = JsonView::parse(scrydantic::Json{.text = std::string{input}});
  std::string appended{};
  const bool canonicalized =
      core::append_canonical_json(appended, input) == core::json_validity::valid;
  REQUIRE(scrydantic::is_valid_json(input) == document.has_value());
  REQUIRE((core::check_json(input) == core::json_validity::valid) ==
          document.has_value());
  REQUIRE(viewed.has_value() == document.has_value());
  REQUIRE(canonicalized == document.has_value());
  if (!document) {
    // The whole error: the document's rejection, for the host and the model.
    const auto description = json::describe(input, document.error());
    CHECK(core::describe_invalid_json(input) == description);
    CHECK(viewed.error().message == "JSON text is not valid: " + description);
    CHECK(viewed.error().model_message == viewed.error().message);
    CHECK(viewed.error().issues.empty());
    CHECK(appended.empty());
    return false;
  }
  CHECK(core::describe_invalid_json(input).empty());
  const auto written = json::write(*document);
  CHECK(viewed->to_json().text == written);
  CHECK(appended == written);
  return true;
}

struct Adversarial {
  std::string_view name{};
  std::string_view text{};
  bool accepted{};
};

} // namespace

TEST_CASE("the JSON core rejects every shape of incomplete document") {
  static constexpr Adversarial cases[] = {
      // Truncation: the buffer stops mid-value.
      {"truncated object", R"({"a":1)", false},
      {"truncated nested object", R"({"error":{"type":"not_found_error")", false},
      {"truncated array", R"([1,2)", false},
      {"key with no value", R"({"a")", false},
      {"key with colon and no value", R"({"a":)", false},
      {"truncated string", R"("abc)", false},
      {"truncated literal", "tru", false},
      {"truncated fraction", "1.", false},
      {"truncated exponent", "1e", false},
      {"truncated escape", R"("ab\)", false},
      {"truncated unicode escape", R"("\u12)", false},
      {"unclosed nesting", "[[[[1]]]", false},
      {"truncated deep value", R"({"a":[1,{"b":"x)", false},
      {"open brace only", "{", false},
      // Nothing at all.
      {"empty input", "", false},
      {"whitespace only", "   ", false},
      {"newline only", "\n\t\r ", false},
      // Trailing bytes after a complete value.
      {"trailing garbage", R"({"a":1} x)", false},
      {"second document", R"({"a":1}{"b":2})", false},
      {"number then junk", "123abc", false},
      {"literal then junk", "trueX", false},
      {"trailing comment", R"({"a":1} // note)", false},
      {"embedded comment", "{\"a\":1 /*note*/}", false},
      {"trailing NUL byte", std::string_view{"{\"a\":1}\0", 8}, false},
      // Malformed JSON the read itself rejects.
      {"array trailing comma", "[1,2,]", false},
      {"object trailing comma", R"({"a":1,})", false},
      {"close brace only", "}", false},
      {"colon only", ":", false},
      {"NaN literal", "NaN", false},
      {"leading zero", "01", false},
      {"leading plus", "+1", false},
      {"single quoted string", "'a'", false},
      {"unescaped control character", std::string_view{"\"a\x01\x62\"", 5}, false},
      // Invalid Unicode, in a value or a key.
      {"invalid UTF-8 in a string", "\"\xff\xfe\"", false},
      {"invalid UTF-8 in a key", "{\"\xff\":1}", false},
      {"lone surrogate escape", R"("\ud800")", false},
      // Complete documents, including the ones that differ from the above by one byte.
      {"complete object", R"({"a":1})", true},
      {"complete nested object", R"({"error":{"type":"not_found_error"}})", true},
      {"complete array", "[1,2]", true},
      {"closed nesting", "[[[[1]]]]", true},
      {"trailing space", R"({"a":1} )", true},
      {"trailing newline", "{\"a\":1}\n", true},
      {"leading whitespace", "  {\"a\":1}", true},
      {"bare number", "123", true},
      // Numbers the grammar allows but a double cannot hold.
      {"overflowing number", "1e400", false},
      {"overflowing negative number", "-1e400", false},
      {"nonzero number that rounds to zero", "1e-400", false},
      {"number below half the smallest subnormal", "2e-324", false},
      {"smallest subnormal", "5e-324", true},
      {"zero with a huge negative exponent", "0e-400", true},
      {"bare number with space", "123 ", true},
      {"bare true", "true", true},
      {"bare null", "null", true},
      {"bare string", R"("abc")", true},
      {"empty object", "{}", true},
      {"empty array", "[]", true},
  };

  for (const auto& adversarial : cases) {
    INFO(adversarial.name);
    CHECK(core_accepts(adversarial.text) == adversarial.accepted);
  }
}

TEST_CASE("the JSON core bounds malformed non-null-terminated input") {
  // A lone brace in a buffer with no terminator after it: the read must stop at
  // the view's end rather than look for a NUL.
  constexpr std::array input{'{'};
  const auto text = std::string_view{input.data(), input.size()};
  CHECK_FALSE(scrydantic::is_valid_json(text));
  CHECK_FALSE(core_accepts(text));
}

TEST_CASE("the JSON core keeps the last of duplicate object keys") {
  // src/json/document.hpp: the last occurrence wins, at every nesting level.
  auto document = JsonView::parse(scrydantic::Json{.text = R"({"a":1,"a":2})"});
  REQUIRE(document);
  REQUIRE(document->size() == 1);
  CHECK(document->find("a")->unsigned_integer() == 2U);

  auto nested = JsonView::parse(scrydantic::Json{.text = R"({"o":{"k":1,"k":2}})"});
  REQUIRE(nested);
  REQUIRE(nested->find("o")->size() == 1);
  CHECK(nested->find("o")->find("k")->unsigned_integer() == 2U);
}

TEST_CASE("every entry point writes the core's canonical text") {
  // One input per canonical-form rule; core_accepts compares every entry point's
  // output with the document writer's.
  for (const std::string_view input : {
           R"( { "b" : 1 , "a" : [ 2 , { "d" : 3 , "c" : 4 } ] , "a" : null } )",
           "[1.0,-0,1e2,0.00001,1.5e16,18446744073709551615,-9223372036854775808]",
           R"({"\u0041\u00e9\ud83d\ude00":"\/\u001f\b\t"})",
           R"("\u0000")",
           "true",
       }) {
    INFO(input);
    CHECK(core_accepts(input));
  }
}

TEST_CASE("canonical numbers are spelled positionally or with an uppercase E") {
  CHECK(scrydantic::format_json_number(1.0) == "1");
  CHECK(scrydantic::format_json_number(-0.0) == "-0");
  CHECK(scrydantic::format_json_number(1234.5) == "1234.5");
  CHECK(scrydantic::format_json_number(0.0001) == "0.0001");
  CHECK(scrydantic::format_json_number(1e-7) == "1E-7");
  CHECK(scrydantic::format_json_number(1e15) == "1000000000000000");
  CHECK(scrydantic::format_json_number(1e16) == "1E16");
  CHECK(scrydantic::format_json_number(1.5e300) == "1.5E300");
  // JSON has no spelling for a non-finite number.
  CHECK(scrydantic::format_json_number(std::numeric_limits<double>::infinity()) ==
        "null");
  CHECK(scrydantic::format_json_number(-std::numeric_limits<double>::infinity()) ==
        "null");
  CHECK(scrydantic::format_json_number(std::numeric_limits<double>::quiet_NaN()) ==
        "null");

  // The codec's in-place form appends the same text.
  std::string text{"["};
  core::append_canonical_json_number(text, 1e-7);
  CHECK(text == "[1E-7");
}

TEST_CASE("a formatted number is the spelling the canonical writer gives it") {
  for (const double value :
       {0.1, -2.5, 1e21, 123456789.125, 5e-324, 1.7976931348623157e308}) {
    const auto text = scrydantic::format_json_number(value);
    INFO(text);
    const auto parsed = JsonView::parse(scrydantic::Json{.text = text});
    REQUIRE(parsed);
    CHECK(parsed->number() == value);
    CHECK(parsed->to_json().text == text);
  }
}

TEST_CASE("appending canonical JSON leaves the output alone on a rejection") {
  using core::json_validity;
  std::string output{"prefix:"};
  CHECK(core::append_canonical_json(output, R"( { "b" : 1.0 , "a" : [ ] } )") ==
        json_validity::valid);
  CHECK(output == R"(prefix:{"a":[],"b":1})");
  CHECK(core::append_canonical_json(output, R"({"a":1} 2)") == json_validity::invalid);
  CHECK(core::append_canonical_json(output, "") == json_validity::invalid);
  CHECK(core::append_canonical_json(output, "[[0]]", core::json_max_depth - 1) ==
        json_validity::too_deep);
  CHECK(output == R"(prefix:{"a":[],"b":1})");
}

namespace {

[[nodiscard]] std::string nested_arrays(const std::size_t depth) {
  return std::string(depth, '[') + "0" + std::string(depth, ']');
}

} // namespace

TEST_CASE("text is checked as it stands inside open arrays and objects") {
  using core::json_validity;
  constexpr auto limit = core::json_max_depth;
  CHECK(core::check_json("0", limit) == json_validity::valid);
  CHECK(core::check_json(R"("[")", limit) == json_validity::valid);
  CHECK(core::check_json("[]", limit) == json_validity::too_deep);
  CHECK(core::check_json("{}", limit) == json_validity::too_deep);
  CHECK(core::check_json("[]", limit - 1) == json_validity::valid);
  CHECK(core::check_json(R"({"a":[]})", limit - 1) == json_validity::too_deep);

  for (const std::size_t depth : {0UZ, 1UZ, 17UZ, limit - 1, limit}) {
    CAPTURE(depth);
    CHECK(core::check_json(nested_arrays(limit - depth), depth) ==
          json_validity::valid);
    CHECK(core::check_json(nested_arrays(limit - depth + 1), depth) ==
          json_validity::too_deep);
    // Text that breaks a rule before it nests too deep is invalid, not too deep.
    CHECK(core::check_json("1 " + nested_arrays(limit + 1), depth) ==
          json_validity::invalid);
  }
  // Standing at depth zero is the plain check.
  CHECK(core::check_json(nested_arrays(limit + 1)) == json_validity::too_deep);
  CHECK_FALSE(scrydantic::is_valid_json(nested_arrays(limit + 1)));
}

TEST_CASE("canonical JSON is measured as it is written") {
  using core::json_validity;
  constexpr auto limit = core::json_max_depth;
  for (const std::size_t depth : {0UZ, 1UZ, 17UZ, limit - 1, limit}) {
    CAPTURE(depth);
    std::string output{};
    CHECK(core::append_canonical_json(output, nested_arrays(limit - depth), depth) ==
          json_validity::valid);
    CHECK(output == nested_arrays(limit - depth));
    output.clear();
    // One level more is too deep where it stands, unless it is too deep alone,
    // which is not JSON at all.
    CHECK(
        core::append_canonical_json(output, nested_arrays(limit - depth + 1), depth) ==
        (depth == 0 ? json_validity::invalid : json_validity::too_deep));
    CHECK(output.empty());
  }

  // The earlier `x` nests as deep as the parser allows. Canonical text keeps
  // only the later one, so it fits where the text itself would not.
  const auto duplicate = R"({"x":)" + nested_arrays(limit - 1) + R"(,"x":[0]})";
  REQUIRE(core::check_json(duplicate) == json_validity::valid);
  CHECK(core::check_json(duplicate, 1) == json_validity::too_deep);
  std::string output{};
  CHECK(core::append_canonical_json(output, duplicate, limit - 2) ==
        json_validity::valid);
  CHECK(output == R"({"x":[0]})");
  CHECK(core::append_canonical_json(output, duplicate, limit - 1) ==
        json_validity::too_deep);
  CHECK(output == R"({"x":[0]})");
}

TEST_CASE("a document knows how deep it nests") {
  const auto nesting = [](const std::string_view text) {
    const auto document = json::parse(text);
    REQUIRE(document);
    return json::nesting(*document);
  };
  CHECK(nesting("0") == 0);
  CHECK(nesting(R"("[[]]")") == 0);
  CHECK(nesting("[]") == 1);
  CHECK(nesting("{}") == 1);
  CHECK(nesting(R"([1,{"a":[2,[3]],"b":{}},[]])") == 4);
  CHECK(nesting(R"({"a":[[[0]]],"a":0})") == 1);
  CHECK(nesting(nested_arrays(core::json_max_depth)) == core::json_max_depth);
}

namespace {

// Whether the parser accepts `bytes` as the content of a string. The bytes are
// never a quote, a backslash, or a control character here.
[[nodiscard]] bool parser_accepts_string(const std::string_view bytes) {
  return scrydantic::is_valid_json('"' + std::string{bytes} + '"');
}

// Every writer of a string must refuse exactly what the parser refuses in one.
void check_utf8_verdicts(const std::string_view bytes) {
  const bool accepted = parser_accepts_string(bytes);
  std::string output{"prefix:"};
  const bool appended = core::append_utf8_json_string(output, bytes);
  if (core::is_utf8(bytes) != accepted || appended != accepted ||
      output != (accepted ? "prefix:" + scrydantic::escape_json_string(bytes)
                          : std::string{"prefix:"})) {
    CAPTURE(bytes, accepted, appended, output);
    FAIL_CHECK("a string writer disagrees with the parser about UTF-8");
  }
}

} // namespace

TEST_CASE("a string is written only when the parser would read it back") {
  std::string output{};
  REQUIRE(core::append_utf8_json_string(output,
                                        "a\"b\\c\n\x01 caf\xc3\xa9 \xf0\x9f\x98\x80"));
  CHECK(output == "\"a\\\"b\\\\c\\n\\u0001 caf\xc3\xa9 \xf0\x9f\x98\x80\"");
  const auto read_back = JsonView::parse(output);
  REQUIRE(read_back);
  CHECK(read_back->string() == "a\"b\\c\n\x01 caf\xc3\xa9 \xf0\x9f\x98\x80");

  // Every lead byte with every second byte, and with each boundary of Unicode's
  // Table 3-7 as its second, third, and fourth bytes.
  constexpr std::string_view bounds{"\x7f\x80\x8f\x90\x9f\xa0\xbf\xc0\xff", 9};
  for (int lead = 0x80; lead <= 0xFF; ++lead) {
    const std::string first(1, static_cast<char>(lead));
    check_utf8_verdicts(first);
    for (int second = 0x20; second <= 0xFF; ++second) {
      if (second != '"' && second != '\\') {
        check_utf8_verdicts(first + static_cast<char>(second));
      }
    }
    for (const char second : bounds) {
      for (const char third : bounds) {
        check_utf8_verdicts(first + second + third);
        for (const char fourth : bounds) {
          check_utf8_verdicts(first + second + third + fourth);
        }
      }
    }
  }

  // The word-at-a-time scan must not skip a sequence at any offset, nor read a
  // sequence that the text ends inside as whole.
  for (const std::string_view sequence :
       {"\xc3\xa9", "\xe2\x82\xac", "\xf0\x9f\x98\x80", "\xff", "\xc3", "\xe2\x82",
        "\xf0\x9f\x98", "\xed\xa0\x80", "\xc0\x80"}) {
    for (std::size_t before = 0; before <= 17; ++before) {
      for (std::size_t after = 0; after <= 9; ++after) {
        check_utf8_verdicts(std::string(before, 'a') + std::string{sequence} +
                            std::string(after, 'b'));
      }
    }
  }
}
