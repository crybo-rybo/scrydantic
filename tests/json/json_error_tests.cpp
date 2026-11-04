// The error vocabulary of error.hpp as a C++23 consumer sees it: how a path is
// spelled, and that failures which are not decode failures carry no issues.

#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <limits>
#include <scrydantic/error.hpp>
#include <scrydantic/json.hpp>
#include <string>
#include <vector>

namespace {

using Path = std::vector<scrydantic::PathSegment>;

} // namespace

TEST_CASE("json_path spells identifier keys as members and indexes as elements") {
  CHECK(scrydantic::json_path(Path{}) == "$");
  CHECK(scrydantic::json_path(Path{"shapes", 0UZ, "radius"}) == "$.shapes[0].radius");
  CHECK(scrydantic::json_path(Path{0UZ, 12UZ}) == "$[0][12]");
  CHECK(scrydantic::json_path(Path{"_private", "Camel9", "x_1"}) ==
        "$._private.Camel9.x_1");
  CHECK(scrydantic::json_path(Path{std::numeric_limits<std::size_t>::max()}) ==
        "$[18446744073709551615]");
}

TEST_CASE("json_path quotes every other key as a JSON string literal") {
  const auto spelled = [](const std::string& key) {
    return scrydantic::json_path(Path{key});
  };
  // Keys that dot shorthand would misread as more steps, as an index, or as the
  // root, and the empty key.
  CHECK(spelled("a.b") == R"($["a.b"])");
  CHECK(spelled("[0]") == R"($["[0]"])");
  CHECK(spelled("0") == R"($["0"])");
  CHECK(spelled("1st") == R"($["1st"])");
  CHECK(spelled("$") == R"($["$"])");
  CHECK(spelled("") == R"($[""])");
  CHECK(spelled("content-type") == R"($["content-type"])");
  CHECK(spelled("a b") == R"($["a b"])");
  // The literal is escaped as the canonical writer escapes strings, so a quote
  // or bracket inside a key cannot end it.
  CHECK(spelled("\"") == R"($["\""])");
  CHECK(spelled("\"]") == R"($["\"]"])");
  CHECK(spelled("\\") == R"($["\\"])");
  CHECK(spelled("line\nbreak") == R"($["line\nbreak"])");
  CHECK(spelled(std::string(1, '\x01')) == R"($["\u0001"])");
  // Non-ASCII letters are quoted too, and pass through unescaped.
  CHECK(spelled("caf\xc3\xa9") == "$[\"caf\xc3\xa9\"]");
  for (const std::string key : {"a.b", "\"]", "\\", "\x1f", "\x7f", "caf\xc3\xa9"}) {
    CHECK(spelled(key) == "$[" + scrydantic::escape_json_string(key) + "]");
  }
  CHECK(scrydantic::json_path(Path{"a.b", 1UZ, "c"}) == R"($["a.b"][1].c)");
}

TEST_CASE("failures other than decoding carry no issues") {
  const auto parsed = scrydantic::JsonView::parse(scrydantic::Json{.text = "{"});
  REQUIRE_FALSE(parsed);
  CHECK(parsed.error().issues.empty());
  CHECK(scrydantic::tool_error("not today").issues.empty());
}
