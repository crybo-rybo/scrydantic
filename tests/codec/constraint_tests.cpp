// Constraint annotations: the schema keywords each one writes, the values that
// decoding accepts and rejects at and around each bound, and every declaration
// the compiler rejects, with its diagnostic.

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <meta>
#include <optional>
#include <scrydantic/annotations.hpp>
#include <scrydantic/codec.hpp>
#include <scrydantic/detail/json_number.hpp>
#include <scrydantic/diagnostics.hpp>
#include <scrydantic/error.hpp>
#include <scrydantic/json.hpp>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace {

using scrydantic::items;
using scrydantic::length;
using scrydantic::multiple_of;
using scrydantic::pattern;
using scrydantic::range;

// ---- Constrained types -------------------------------------------------------

struct Ages {
  [[= range{.min = 0, .max = 150}]] std::int32_t inclusive{};
  [[= range{.exclusive_min = 0, .exclusive_max = 10}]] std::int64_t exclusive{};
  [[= range{.min = 1}]] std::uint32_t at_least{};
  [[= range{.max = 10}]] std::int16_t at_most{};
  [[= range{.min = -5, .exclusive_max = 5}]] std::int32_t mixed{};
};

struct Shares {
  [[= range{.exclusive_min = 0.0, .max = 1}]] double share{};
  [[= range{.min = -273.15}]] double celsius{};
  [[= range{.exclusive_max = 0.1f}]] float tiny{};
  [[= range{.min = 1e-7, .max = 1e300}]] double wide{};
  [[= range{.max = 100.0}]] std::int32_t whole{};
};

// Bounds where converting the parsed number to the member's type rounds: near
// 2.5 floats are 2^-22 apart, near 0.5 2^-24 and 2^-25, and from 2^53 doubles are
// 2 apart.
struct Narrowed {
  [[= range{.min = 0.5f, .max = 2.5f}]] float inclusive{};
  [[= range{.exclusive_min = 0.5f, .exclusive_max = 2.5f}]] float exclusive{};
  [[= multiple_of{0.75f}]] float step{};
  [[= range{.max = 9007199254740992.0}]] double big{};
  [[= range{.exclusive_max = 9007199254740996.0}]] double bigger{};
  [[= multiple_of{2}]] double even{};
};

struct Steps {
  [[= multiple_of{5}]] std::int32_t five{};
  [[= multiple_of{0.25}]] double quarter{};
  [[ = range{.max = 20}, = multiple_of{3} ]] std::uint16_t both{};
};

struct Texts {
  [[= length{.min = 1, .max = 3}]] std::string short_name;
  [[= length{.min = 2}]] std::string at_least_two;
  [[= length{.max = 1}]] std::string at_most_one;
  [[= pattern{R"(^\d+$)"}]] std::string digits;
  [[ = pattern{"^[a-z]+$"}, = length{.max = 8} ]] std::string slug;
};

struct Lists {
  [[= items{.min = 1, .max = 2}]] std::vector<std::int32_t> pair;
  [[= items{.max = 1}]] std::vector<std::string> single;
};

struct[[= scrydantic::skip_null]] Optionals {
  [[ = scrydantic::description{"Percent"},
     = range{.min = 0, .max = 100} ]] std::optional<std::int32_t>
      percent;
  [[= items{.min = 1}]] std::optional<std::vector<std::uint16_t>> flags;
  [[= length{.min = 2}]] std::optional<std::string> code;
};

struct Row {
  [[= range{.min = 0}]] std::int32_t age{};
};

struct Table {
  std::vector<Row> rows;
  Row header;
};

struct[[= scrydantic::tag{"circle"}]] Disc {
  [[= range{.exclusive_min = 0.0}]] double radius{};
};

struct[[= scrydantic::tag{"label"}]] Label {
  [[= length{.min = 1}]] std::string text;
};

using Mark = std::variant<Disc, Label>;

struct Canvas {
  std::vector<Mark> marks;
};

// A default outside its own constraint: an absent member keeps it unchecked.
struct Counter {
  [[= range{.min = 1}]] std::int32_t count = 0;
};

// A renamed member is bounded, and reported, under its wire name.
struct Budget {
  [[ = scrydantic::name{"max_tokens"},
     = range{.min = 1, .max = 4096} ]] std::uint32_t limit{};
};

struct Many {
  std::vector<Row> rows;
};

using scrydantic::IssueKind;

// A double as a schema spells a bound, which the encoder spells the same way.
[[nodiscard]] constexpr std::string spelled(const double value) {
  std::string text{};
  scrydantic::detail::append_canonical_double(text, value);
  return text;
}

template <typename Type>
[[nodiscard]] scrydantic::Result<Type> decode_text(const std::string_view text) {
  return scrydantic::decode<Type>(scrydantic::Json{.text = std::string{text}});
}

// A function template because GCC 16 reports -Wshadow for each expansion of a
// `template for` in a function that is not one.
template <typename Enum> [[nodiscard]] std::string enumerator_name(const Enum value) {
  template for (constexpr std::meta::info enumerator :
                std::define_static_array(std::meta::enumerators_of(^^Enum))) {
    if (value == [:enumerator:]) {
      return std::string{std::meta::identifier_of(enumerator)};
    }
  }
  return "an undeclared enumerator";
}

// Each issue as `kind path reason`, so a failed comparison shows all three.
[[nodiscard]] std::vector<std::string> issue_lines(const scrydantic::Error& error) {
  std::vector<std::string> lines{};
  for (const auto& issue : error.issues) {
    lines.push_back(enumerator_name(issue.kind) + " " +
                    scrydantic::json_path(issue.path) + " " + issue.reason);
  }
  return lines;
}

// The issues of a decode that must fail, after checking that its messages are
// one line per issue, the same for the host and the model.
template <typename Type>
[[nodiscard]] std::vector<std::string> rejection(const std::string_view text) {
  const auto decoded = decode_text<Type>(text);
  REQUIRE_FALSE(decoded);
  CHECK(decoded.error().message == decoded.error().model_message);
  return issue_lines(decoded.error());
}

// Index-based rather than string_view::find: under AddressSanitizer GCC 16 cannot
// constant-fold find's null check on an interior pointer of a static string.
[[nodiscard]] consteval bool mentions(const std::string_view text,
                                      const std::string_view fragment) {
  for (std::size_t start = 0; start + fragment.size() <= text.size(); ++start) {
    bool matched = true;
    for (std::size_t index = 0; index < fragment.size() && matched; ++index) {
      matched = text[start + index] == fragment[index];
    }
    if (matched) {
      return true;
    }
  }
  return false;
}

} // namespace

// ---- Schemas -----------------------------------------------------------------

// An integer has one lower and one upper bound: the range's where it sets one,
// and the type's own where it does not.
static_assert(
    scrydantic::schema_v<Ages> ==
    R"({"additionalProperties":false,"properties":{)"
    R"("at_least":{"maximum":4294967295,"minimum":1,"type":"integer"},)"
    R"("at_most":{"maximum":10,"minimum":-32768,"type":"integer"},)"
    R"("exclusive":{"exclusiveMaximum":10,"exclusiveMinimum":0,"type":"integer"},)"
    R"("inclusive":{"maximum":150,"minimum":0,"type":"integer"},)"
    R"("mixed":{"exclusiveMaximum":5,"minimum":-5,"type":"integer"}},)"
    R"("required":[],"type":"object"})");

// A floating-point bound is spelled as the encoder spells the double, a float
// bound as the double it is, and a whole bound of an integer as an integer.
static_assert(scrydantic::schema_v<Shares> ==
              R"({"additionalProperties":false,"properties":{)"
              R"("celsius":{"minimum":-273.15,"type":"number"},)"
              R"("share":{"exclusiveMinimum":0,"maximum":1,"type":"number"},)"
              R"("tiny":{"exclusiveMaximum":0.10000000149011612,"type":"number"},)"
              R"("whole":{"maximum":100,"minimum":-2147483648,"type":"integer"},)"
              R"("wide":{"maximum":1E300,"minimum":1E-7,"type":"number"}},)"
              R"("required":[],"type":"object"})");

static_assert(
    scrydantic::schema_v<Steps> ==
    R"({"additionalProperties":false,"properties":{)"
    R"("both":{"maximum":20,"minimum":0,"multipleOf":3,"type":"integer"},)"
    R"("five":{"maximum":2147483647,"minimum":-2147483648,"multipleOf":5,"type":"integer"},)"
    R"("quarter":{"multipleOf":0.25,"type":"number"}},)"
    R"("required":[],"type":"object"})");

// A pattern is written as a JSON string, escapes and all.
static_assert(
    scrydantic::schema_v<Texts> ==
    R"({"additionalProperties":false,"properties":{)"
    R"("at_least_two":{"minLength":2,"type":"string"},)"
    R"("at_most_one":{"maxLength":1,"type":"string"},)"
    R"("digits":{"pattern":"^\\d+$","type":"string"},)"
    R"("short_name":{"maxLength":3,"minLength":1,"type":"string"},)"
    R"("slug":{"maxLength":8,"pattern":"^[a-z]+$","type":"string"}},)"
    R"("required":["at_least_two","at_most_one","digits","short_name","slug"],)"
    R"("type":"object"})");

static_assert(
    scrydantic::schema_v<Lists> ==
    R"({"additionalProperties":false,"properties":{)"
    R"("pair":{"items":{"maximum":2147483647,"minimum":-2147483648,"type":"integer"},)"
    R"("maxItems":2,"minItems":1,"type":"array"},)"
    R"("single":{"items":{"type":"string"},"maxItems":1,"type":"array"}},)"
    R"("required":["pair","single"],"type":"object"})");

// An optional's constraints bound its engaged value, so they go inside anyOf and
// null stays allowed; a description stays beside anyOf.
static_assert(
    scrydantic::schema_v<Optionals> ==
    R"({"additionalProperties":false,"properties":{)"
    R"("code":{"anyOf":[{"minLength":2,"type":"string"},{"type":"null"}]},)"
    R"("flags":{"anyOf":[{"items":{"maximum":65535,"minimum":0,"type":"integer"},)"
    R"("minItems":1,"type":"array"},)"
    R"({"type":"null"}]},)"
    R"("percent":{"anyOf":[{"maximum":100,"minimum":0,"type":"integer"},)"
    R"({"type":"null"}],"description":"Percent"}},)"
    R"("required":[],"type":"object"})");

static_assert(
    scrydantic::schema_v<Mark> ==
    R"({"anyOf":[{"additionalProperties":false,"properties":{)"
    R"("radius":{"exclusiveMinimum":0,"type":"number"},)"
    R"("type":{"enum":["circle"],"type":"string"}},"required":["type"],"type":"object"},)"
    R"({"additionalProperties":false,"properties":{)"
    R"("text":{"minLength":1,"type":"string"},)"
    R"("type":{"enum":["label"],"type":"string"}},"required":["text","type"],)"
    R"("type":"object"}]})");

// ---- Decoding ----------------------------------------------------------------

TEST_CASE("integer ranges accept their bounds and reject the values past them") {
  CHECK(decode_text<Ages>(R"({"inclusive":0})"));
  CHECK(decode_text<Ages>(R"({"inclusive":1})"));
  CHECK(decode_text<Ages>(R"({"inclusive":149})"));
  CHECK(decode_text<Ages>(R"({"inclusive":150})"));
  CHECK(rejection<Ages>(R"({"inclusive":-1})") ==
        std::vector<std::string>{"violated_constraint $.inclusive must be at least 0"});
  CHECK(
      rejection<Ages>(R"({"inclusive":151})") ==
      std::vector<std::string>{"violated_constraint $.inclusive must be at most 150"});

  CHECK(decode_text<Ages>(R"({"exclusive":1})"));
  CHECK(decode_text<Ages>(R"({"exclusive":9})"));
  CHECK(rejection<Ages>(R"({"exclusive":0})") ==
        std::vector<std::string>{
            "violated_constraint $.exclusive must be greater than 0"});
  CHECK(
      rejection<Ages>(R"({"exclusive":10})") ==
      std::vector<std::string>{"violated_constraint $.exclusive must be less than 10"});

  CHECK(decode_text<Ages>(R"({"at_least":1,"at_most":10,"mixed":-5})"));
  CHECK(decode_text<Ages>(R"({"at_least":4294967295,"at_most":-32768,"mixed":4})"));
  CHECK(rejection<Ages>(R"({"at_least":0,"at_most":11,"mixed":5})") ==
        std::vector<std::string>{"violated_constraint $.at_least must be at least 1",
                                 "violated_constraint $.at_most must be at most 10",
                                 "violated_constraint $.mixed must be less than 5"});

  // The type's own range is still checked first, as its own kind of issue.
  CHECK(
      rejection<Ages>(R"({"at_least":4294967296,"inclusive":2.5})") ==
      std::vector<std::string>{"wrong_type $.inclusive must be an integer",
                               "out_of_range $.at_least is outside the integer range"});
}

TEST_CASE("floating-point ranges compare the decoded value with the exact bound") {
  // The double next to a bound, towards `direction`, as a one-member object.
  const auto next_to = [](const std::string_view key, const double bound,
                          const double direction) {
    return R"({")" + std::string{key} + R"(":)" +
           spelled(std::nextafter(bound, direction)) + "}";
  };
  constexpr double up = std::numeric_limits<double>::infinity();

  CHECK(decode_text<Shares>(R"({"share":1})"));
  CHECK(decode_text<Shares>(next_to("share", 1.0, 0.0)));
  CHECK(decode_text<Shares>(next_to("share", 0.0, 1.0)));
  CHECK(rejection<Shares>(next_to("share", 1.0, up)) ==
        std::vector<std::string>{"violated_constraint $.share must be at most 1"});
  CHECK(rejection<Shares>(R"({"share":0})") ==
        std::vector<std::string>{"violated_constraint $.share must be greater than 0"});
  CHECK(rejection<Shares>(R"({"share":-0.0})") ==
        std::vector<std::string>{"violated_constraint $.share must be greater than 0"});

  CHECK(decode_text<Shares>(R"({"celsius":-273.15})"));
  CHECK(decode_text<Shares>(next_to("celsius", -273.15, up)));
  CHECK(rejection<Shares>(next_to("celsius", -273.15, -up)) ==
        std::vector<std::string>{
            "violated_constraint $.celsius must be at least -273.15"});

  // 0.1 is less than the bound, the float 0.1f, but rounds to it, so the
  // exclusive maximum rejects it in words of its own; the float just below it is
  // accepted.
  CHECK(decode_text<Shares>(R"({"tiny":0.099999994})"));
  CHECK(rejection<Shares>(R"({"tiny":0.1})") ==
        std::vector<std::string>{"violated_constraint $.tiny must be less than "
                                 "0.10000000149011612 even once rounded to single "
                                 "precision"});

  CHECK(decode_text<Shares>(R"({"wide":1E300})"));
  CHECK(decode_text<Shares>(R"({"wide":1E-7})"));
  CHECK(decode_text<Shares>(next_to("wide", 1e300, 0.0)));
  CHECK(decode_text<Shares>(next_to("wide", 1e-7, up)));
  CHECK(rejection<Shares>(next_to("wide", 1e300, up)) ==
        std::vector<std::string>{"violated_constraint $.wide must be at most 1E300"});
  CHECK(rejection<Shares>(next_to("wide", 1e-7, 0.0)) ==
        std::vector<std::string>{"violated_constraint $.wide must be at least 1E-7"});

  CHECK(decode_text<Shares>(R"({"whole":100})"));
  CHECK(rejection<Shares>(R"({"whole":101})") ==
        std::vector<std::string>{"violated_constraint $.whole must be at most 100"});
}

TEST_CASE("a floating-point member's number is checked as parsed, then as stored") {
  // The schema's verdict comes first: these numbers miss a bound or a multiple,
  // though each would round onto a float or double that meets it.
  CHECK(
      rejection<Narrowed>(R"({"inclusive":2.50000001})") ==
      std::vector<std::string>{"violated_constraint $.inclusive must be at most 2.5"});
  CHECK(
      rejection<Narrowed>(R"({"inclusive":0.49999999})") ==
      std::vector<std::string>{"violated_constraint $.inclusive must be at least 0.5"});
  CHECK(rejection<Narrowed>(R"({"step":0.75000001})") ==
        std::vector<std::string>{
            "violated_constraint $.step must be a multiple of 0.75"});
  // 2^53 + 1 is read as the integer it is, not as the 2^53 it would round to.
  CHECK(rejection<Narrowed>(R"({"big":9007199254740993})") ==
        std::vector<std::string>{
            "violated_constraint $.big must be at most 9007199254740992"});
  CHECK(rejection<Narrowed>(R"({"even":9007199254740993})") ==
        std::vector<std::string>{"violated_constraint $.even must be a multiple of 2"});
  CHECK(rejection<Narrowed>(R"({"even":-9007199254740993})") ==
        std::vector<std::string>{"violated_constraint $.even must be a multiple of 2"});

  // These meet the schema, but the value the host would get does not.
  CHECK(
      rejection<Narrowed>(R"({"exclusive":2.4999999})") ==
      std::vector<std::string>{"violated_constraint $.exclusive must be less than 2.5 "
                               "even once rounded to single precision"});
  CHECK(rejection<Narrowed>(R"({"exclusive":0.50000001})") ==
        std::vector<std::string>{"violated_constraint $.exclusive must be greater than "
                                 "0.5 even once rounded to single precision"});
  // 33554433 is 0.75 * 44739244, and the float it rounds to, 33554432, is no
  // multiple of 0.75.
  CHECK(rejection<Narrowed>(R"({"step":33554433})") ==
        std::vector<std::string>{"violated_constraint $.step must be a multiple of "
                                 "0.75 even once rounded to single precision"});
  // 2^53 + 3 rounds, ties to even, to 2^53 + 4.
  CHECK(rejection<Narrowed>(R"({"bigger":9007199254740995})") ==
        std::vector<std::string>{"violated_constraint $.bigger must be less than "
                                 "9007199254740996 even once rounded to double "
                                 "precision"});

  // At the bounds and just within them, both checks pass.
  CHECK(decode_text<Narrowed>(R"({"exclusive":2.4999998,"inclusive":2.5,"step":1.5})"));
  CHECK(
      decode_text<Narrowed>(R"({"exclusive":0.5000001,"inclusive":0.5,"step":-0.75})"));
  CHECK(decode_text<Narrowed>(
      R"({"big":9007199254740992,"bigger":9007199254740994,"even":9007199254740994})"));
}

TEST_CASE("multiple_of accepts exact multiples only") {
  CHECK(decode_text<Steps>(R"({"five":0})"));
  CHECK(decode_text<Steps>(R"({"five":-10})"));
  CHECK(decode_text<Steps>(R"({"five":2147483645})"));
  CHECK(rejection<Steps>(R"({"five":7})") ==
        std::vector<std::string>{"violated_constraint $.five must be a multiple of 5"});
  CHECK(rejection<Steps>(R"({"five":-2147483648})") ==
        std::vector<std::string>{"violated_constraint $.five must be a multiple of 5"});

  CHECK(decode_text<Steps>(R"({"quarter":0.75})"));
  CHECK(decode_text<Steps>(R"({"quarter":-1E300})"));
  CHECK(rejection<Steps>(R"({"quarter":0.3})") ==
        std::vector<std::string>{
            "violated_constraint $.quarter must be a multiple of 0.25"});

  // Every constraint a value misses is reported, in declaration order.
  CHECK(decode_text<Steps>(R"({"both":18})"));
  CHECK(rejection<Steps>(R"({"both":22})") ==
        std::vector<std::string>{"violated_constraint $.both must be at most 20",
                                 "violated_constraint $.both must be a multiple of 3"});
}

TEST_CASE("string lengths count code points, not bytes") {
  // "né!" is three code points in four bytes, and each emoji is one in four.
  CHECK(decode_text<Texts>(R"({"at_least_two":"é!","at_most_one":"😀","digits":"x",)"
                           R"("short_name":"né!","slug":"abc"})"));
  CHECK(decode_text<Texts>(R"({"at_least_two":"😀😀","at_most_one":"","digits":"",)"
                           R"("short_name":"😀😀😀","slug":""})"));
  // An escaped surrogate pair is one code point.
  CHECK(decode_text<Texts>(R"({"at_least_two":"ab","at_most_one":"😀",)"
                           R"("digits":"","short_name":"a","slug":""})"));
  CHECK(rejection<Texts>(R"({"at_least_two":"é","at_most_one":"éé","digits":"",)"
                         R"("short_name":"néé!","slug":"abcdefghi"})") ==
        std::vector<std::string>{
            "violated_constraint $.short_name must have at most 3 characters",
            "violated_constraint $.at_least_two must have at least 2 characters",
            "violated_constraint $.at_most_one must have at most 1 character",
            "violated_constraint $.slug must have at most 8 characters"});
  CHECK(rejection<Texts>(R"({"at_least_two":"ab","at_most_one":"","digits":"",)"
                         R"("short_name":"","slug":""})") ==
        std::vector<std::string>{
            "violated_constraint $.short_name must have at least 1 character"});
}

TEST_CASE("a pattern is the schema's alone and decoding does not check it") {
  const auto decoded = decode_text<Texts>(R"({"at_least_two":"ab","at_most_one":"",)"
                                          R"("digits":"not digits","short_name":"a",)"
                                          R"("slug":"UPPER"})");
  REQUIRE(decoded);
  CHECK(decoded->digits == "not digits");
  CHECK(decoded->slug == "UPPER");
}

TEST_CASE("item counts bound vectors") {
  CHECK(decode_text<Lists>(R"({"pair":[1],"single":[]})"));
  CHECK(decode_text<Lists>(R"({"pair":[1,2],"single":["s"]})"));
  CHECK(rejection<Lists>(R"({"pair":[],"single":["a","b"]})") ==
        std::vector<std::string>{
            "violated_constraint $.pair must have at least 1 item",
            "violated_constraint $.single must have at most 1 item"});
  CHECK(
      rejection<Lists>(R"({"pair":[1,2,3],"single":[]})") ==
      std::vector<std::string>{"violated_constraint $.pair must have at most 2 items"});

  // A vector whose elements fail has no value to count.
  CHECK(rejection<Lists>(R"({"pair":[1,"x",3],"single":[]})") ==
        std::vector<std::string>{"wrong_type $.pair[1] must be an integer"});
}

TEST_CASE("an optional member's constraints bound its engaged value") {
  CHECK(decode_text<Optionals>("{}"));
  CHECK(decode_text<Optionals>(R"({"code":null,"flags":null,"percent":null})"));
  CHECK(decode_text<Optionals>(R"({"code":"ab","flags":[1],"percent":100})"));
  CHECK(rejection<Optionals>(R"({"code":"a","flags":[],"percent":101})") ==
        std::vector<std::string>{
            "violated_constraint $.percent must be at most 100",
            "violated_constraint $.flags must have at least 1 item",
            "violated_constraint $.code must have at least 2 characters"});
}

TEST_CASE("constraints apply inside vectors, nested objects, and variants") {
  CHECK(
      rejection<Table>(R"({"header":{"age":-1},"rows":[{"age":1},{"age":-2},{}]})") ==
      std::vector<std::string>{"violated_constraint $.rows[1].age must be at least 0",
                               "violated_constraint $.header.age must be at least 0"});
  CHECK(rejection<Canvas>(
            R"({"marks":[{"radius":0,"type":"circle"},)"
            R"({"text":"","type":"label"},{"radius":2,"type":"circle"}]})") ==
        std::vector<std::string>{
            "violated_constraint $.marks[0].radius must be greater than 0",
            "violated_constraint $.marks[1].text must have at least 1 character"});
}

TEST_CASE("constraint issues sit among other issues in visiting order") {
  CHECK(rejection<Ages>(R"({"at_most":"x","extra":1,"inclusive":-1,"mixed":9})") ==
        std::vector<std::string>{R"(unknown_member $ contains unknown member "extra")",
                                 "violated_constraint $.inclusive must be at least 0",
                                 "wrong_type $.at_most must be an integer",
                                 "violated_constraint $.mixed must be less than 5"});
}

TEST_CASE("constraint issues count toward max_issues") {
  std::string text{R"({"rows":[)"};
  for (std::size_t index = 0; index <= scrydantic::max_issues; ++index) {
    text.append(index == 0 ? "" : ",").append(R"({"age":-1})");
  }
  text.append("]}");
  const auto decoded = decode_text<Many>(text);
  REQUIRE_FALSE(decoded);
  CHECK(decoded.error().issues.size() == scrydantic::max_issues);
  CHECK(
      decoded.error().issues.back().path ==
      std::vector<scrydantic::PathSegment>{"rows", scrydantic::max_issues - 1, "age"});
  CHECK(decoded.error().message.ends_with(
      "$.rows[63].age must be at least 0\n"
      "further issues past the first 64 were not reported"));
}

TEST_CASE("constraints check decoded input, not the program's own values") {
  // An absent member keeps its default, which is not checked.
  const auto absent = decode_text<Counter>("{}");
  REQUIRE(absent);
  CHECK(absent->count == 0);
  CHECK(rejection<Counter>(R"({"count":0})") ==
        std::vector<std::string>{"violated_constraint $.count must be at least 1"});

  // Encoding writes the program's values as they are, verbatim or canonical.
  constexpr std::string_view written =
      R"({"at_least":0,"at_most":0,"exclusive":0,"inclusive":1000,"mixed":0})";
  CHECK(scrydantic::encode(Ages{.inclusive = 1000})->text == written);
  CHECK(scrydantic::encode_verbatim(Ages{.inclusive = 1000})->text == written);
}

// The wire names of constrained types are the codec's own.
static_assert(scrydantic::keys_v<Budget>.size() == 1 &&
              scrydantic::keys_v<Budget>[0] == "max_tokens");
static_assert(scrydantic::tags_v<Mark>.size() == 2 &&
              scrydantic::tags_v<Mark>[0] == "circle" &&
              scrydantic::tags_v<Mark>[1] == "label");
static_assert(scrydantic::schema_v<Budget> ==
              R"({"additionalProperties":false,"properties":{"max_tokens":)"
              R"({"maximum":4096,"minimum":1,"type":"integer"}},"required":[],)"
              R"("type":"object"})");

TEST_CASE("a renamed member's constraint issue is at its wire name") {
  const auto decoded = decode_text<Budget>(R"({"max_tokens":0})");
  REQUIRE_FALSE(decoded);
  CHECK(decoded.error().issues ==
        std::vector<scrydantic::Issue>{{.kind = IssueKind::violated_constraint,
                                        .path = {"max_tokens"},
                                        .reason = "must be at least 1"}});
  CHECK(decoded.error().model_message == "$.max_tokens must be at least 1");
  const auto largest = decode_text<Budget>(R"({"max_tokens":4096})");
  REQUIRE(largest);
  CHECK(largest->limit == 4096U);
}

// ---- Indirect and recursive models -------------------------------------------

// A named namespace, so the recursive types' definition names are stable.
namespace constrained {

// Constraints see through std::indirect, as the codec and the schemas do.
struct Boxed {
  [[= range{.min = 0, .max = 10}]] std::indirect<std::int32_t> level;
  [[= length{.min = 2}]] std::optional<std::indirect<std::string>> code;
  [[= range{.exclusive_max = 2.5f}]] std::indirect<float> gain;
};

// A recursive aggregate whose element schema is a "$ref", with a constraint on
// the vector of itself and one on a leaf.
struct Outline {
  [[= items{.max = 2}]] std::vector<Outline> children;
  [[= length{.min = 1}]] std::string title;
};

// Outline as a definition rather than the root.
struct Book {
  Outline contents;
  [[= range{.min = 1}]] std::int32_t edition{};
};

} // namespace constrained

// The keywords go to the owned value's schema, inside an optional's anyOf.
static_assert(scrydantic::schema_v<constrained::Boxed> ==
              R"({"additionalProperties":false,"properties":{)"
              R"("code":{"anyOf":[{"minLength":2,"type":"string"},{"type":"null"}]},)"
              R"("gain":{"exclusiveMaximum":2.5,"type":"number"},)"
              R"("level":{"maximum":10,"minimum":0,"type":"integer"}},)"
              R"("required":["code","gain","level"],"type":"object"})");

// An item bound sits beside "items", whatever its element's schema: here the
// root's own "#".
static_assert(scrydantic::schema_v<constrained::Outline> ==
              R"({"additionalProperties":false,"properties":{)"
              R"("children":{"items":{"$ref":"#"},"maxItems":2,"type":"array"},)"
              R"("title":{"minLength":1,"type":"string"}},)"
              R"("required":["children","title"],"type":"object"})");

// As a definition, the type carries the same keywords.
static_assert(
    scrydantic::schema_v<constrained::Book> ==
    R"({"$defs":{"constrained.Outline":{"additionalProperties":false,"properties":{)"
    R"("children":{"items":{"$ref":"#/$defs/constrained.Outline"},"maxItems":2,)"
    R"("type":"array"},"title":{"minLength":1,"type":"string"}},)"
    R"("required":["children","title"],"type":"object"}},)"
    R"("additionalProperties":false,"properties":{)"
    R"("contents":{"$ref":"#/$defs/constrained.Outline"},)"
    R"("edition":{"maximum":2147483647,"minimum":1,"type":"integer"}},)"
    R"("required":["contents"],"type":"object"})");

TEST_CASE("constraints check the value an indirect owns") {
  CHECK(decode_text<constrained::Boxed>(R"({"code":null,"gain":2,"level":10})"));
  CHECK(decode_text<constrained::Boxed>(R"({"code":"ab","gain":-1,"level":0})"));
  CHECK(rejection<constrained::Boxed>(R"({"code":"a","gain":2.4999999,"level":11})") ==
        std::vector<std::string>{
            "violated_constraint $.level must be at most 10",
            "violated_constraint $.code must have at least 2 characters",
            "violated_constraint $.gain must be less than 2.5 even once rounded to "
            "single precision"});
}

TEST_CASE("constraints apply at every level of a recursive model") {
  CHECK(decode_text<constrained::Outline>(
      R"({"children":[{"children":[],"title":"b"}],"title":"a"})"));
  CHECK(rejection<constrained::Outline>(
            R"({"children":[{"children":[],"title":""},{"children":[)"
            R"({"children":[],"title":"x"},{"children":[],"title":"y"},)"
            R"({"children":[],"title":"z"}],"title":"b"}],"title":"a"})") ==
        std::vector<std::string>{
            "violated_constraint $.children[0].title must have at least 1 character",
            "violated_constraint $.children[1].children must have at most 2 items"});
  CHECK(rejection<constrained::Book>(
            R"({"contents":{"children":[],"title":""},"edition":0})") ==
        std::vector<std::string>{
            "violated_constraint $.contents.title must have at least 1 character",
            "violated_constraint $.edition must be at least 1"});
}

// ---- Rejections --------------------------------------------------------------

namespace {

struct MisappliedLength {
  [[= length{.max = 3}]] std::int32_t count{};
};

struct MisappliedRange {
  [[= range{.min = 0}]] std::optional<std::string> name;
};

// An indirect is seen through, to the type it owns.
struct MisappliedIndirect {
  [[= length{.max = 3}]] std::optional<std::indirect<std::int32_t>> count;
};

struct FixedItems {
  [[= items{.max = 2}]] std::array<bool, 2> flags{};
};

struct Crossed {
  [[= range{.min = 5, .max = 4}]] std::int32_t value{};
};

struct CrossedExclusive {
  [[= range{.exclusive_min = 0.5, .max = 0.25}]] double value{};
};

struct EmptyIntegers {
  [[= range{.exclusive_min = 5, .exclusive_max = 6}]] std::int32_t value{};
};

struct EmptyPoint {
  [[= range{.min = 1.5, .exclusive_max = 1.5}]] double value{};
};

struct EmptyEdge {
  [[= range{.exclusive_min = 65535}]] std::uint16_t value{};
};

struct EmptyDoubles {
  [[= range{.exclusive_min = 1.0, .exclusive_max = 1.0000000000000002}]] double value{};
};

struct NegativeUnsigned {
  [[= range{.min = -1}]] std::uint32_t value{};
};

struct TooWide {
  [[= range{.max = 32768}]] std::int16_t value{};
};

struct Fractional {
  [[= range{.max = 1.5}]] std::int32_t value{};
};

struct NotAFloat {
  [[= range{.max = 0.1}]] float value{};
};

struct NotADouble {
  [[= range{.max = 9007199254740993}]] double value{};
};

struct Infinite {
  [[= range{.max = std::numeric_limits<double>::infinity()}]] double value{};
};

struct Unbounded {
  [[= range{}]] std::int32_t value{};
};

struct DoubleLower {
  [[= range{.min = 0, .exclusive_min = 0}]] std::int32_t value{};
};

struct ZeroFactor {
  [[= multiple_of{0}]] std::int32_t value{};
};

struct NegativeFactor {
  [[= multiple_of{-0.5}]] double value{};
};

struct DecimalFactor {
  [[= multiple_of{0.1}]] double value{};
};

struct FractionalFactor {
  [[= multiple_of{0.5}]] std::int32_t value{};
};

struct NoFactor {
  [[= multiple_of{}]] std::int32_t value{};
};

struct CrossedLength {
  [[= length{.min = 5, .max = 4}]] std::string text;
};

struct UnboundedItems {
  [[= items{}]] std::vector<std::int32_t> values;
};

struct TwoRanges {
  [[ = range{.min = 0}, = range{.max = 9} ]] std::int32_t value{};
};

struct TwoPatterns {
  [[ = pattern{"a"}, = pattern{"b"} ]] std::string text;
};

struct[[= range{.min = 0}]] RangedClass {
  std::int32_t value{};
};

// A rejection inside a nested member names the whole path.
struct HoldsCrossed {
  std::vector<Crossed> values;
};

} // namespace

static_assert(mentions(scrydantic::supported_value_diagnostic<MisappliedLength>(),
                       "MisappliedLength::count: carries scrydantic::length, which "
                       "applies to std::string members, not to int"));
static_assert(
    mentions(scrydantic::supported_value_diagnostic<MisappliedRange>(),
             "MisappliedRange::name: carries scrydantic::range, which applies "
             "to integer and floating-point members, not to std::optional<"));
static_assert(
    mentions(scrydantic::supported_value_diagnostic<MisappliedIndirect>(),
             "MisappliedIndirect::count: carries scrydantic::length, which applies "
             "to std::string members, not to std::optional<std::indirect<int"));
static_assert(mentions(scrydantic::supported_value_diagnostic<FixedItems>(),
                       "FixedItems::flags: carries scrydantic::items, which does not "
                       "apply to std::array<bool, 2>, whose size is fixed"));
static_assert(mentions(scrydantic::supported_value_diagnostic<Crossed>(),
                       "Crossed::value: carries a scrydantic::range whose min, 5, "
                       "exceeds its max, 4"));
static_assert(mentions(scrydantic::supported_value_diagnostic<CrossedExclusive>(),
                       "CrossedExclusive::value: carries a scrydantic::range whose "
                       "exclusive_min, 0.5, exceeds its max, 0.25"));
static_assert(
    mentions(scrydantic::supported_value_diagnostic<EmptyIntegers>(),
             "EmptyIntegers::value: carries a scrydantic::range that no value "
             "of int satisfies"));
static_assert(
    mentions(scrydantic::supported_value_diagnostic<EmptyPoint>(),
             "EmptyPoint::value: carries a scrydantic::range that no value of "
             "double satisfies"));
static_assert(mentions(scrydantic::supported_value_diagnostic<EmptyEdge>(),
                       "EmptyEdge::value: carries a scrydantic::range that no value of "
                       "short unsigned int satisfies"));
static_assert(mentions(scrydantic::supported_value_diagnostic<EmptyDoubles>(),
                       "EmptyDoubles::value: carries a scrydantic::range that no value "
                       "of double satisfies"));
static_assert(
    mentions(scrydantic::supported_value_diagnostic<NegativeUnsigned>(),
             "NegativeUnsigned::value: carries a scrydantic::range whose min, "
             "-1, is outside the range of unsigned int"));
static_assert(
    mentions(scrydantic::supported_value_diagnostic<TooWide>(),
             "TooWide::value: carries a scrydantic::range whose max, 32768, is "
             "outside the range of short int"));
static_assert(mentions(scrydantic::supported_value_diagnostic<Fractional>(),
                       "Fractional::value: carries a scrydantic::range whose max, 1.5, "
                       "is not an integer"));
static_assert(
    mentions(scrydantic::supported_value_diagnostic<NotAFloat>(),
             "NotAFloat::value: carries a scrydantic::range whose max, 0.1, is "
             "not exactly a float"));
static_assert(mentions(scrydantic::supported_value_diagnostic<NotADouble>(),
                       "NotADouble::value: carries a scrydantic::range whose max, "
                       "9007199254740993, is not exactly a double"));
static_assert(mentions(scrydantic::supported_value_diagnostic<Infinite>(),
                       "Infinite::value: carries a scrydantic::range whose max, "
                       "infinity, is not finite"));
static_assert(
    mentions(scrydantic::supported_value_diagnostic<Unbounded>(),
             "Unbounded::value: carries a scrydantic::range that sets no bound"));
static_assert(mentions(scrydantic::supported_value_diagnostic<DoubleLower>(),
                       "DoubleLower::value: carries a scrydantic::range that sets both "
                       "min and exclusive_min"));
static_assert(mentions(scrydantic::supported_value_diagnostic<ZeroFactor>(),
                       "ZeroFactor::value: carries a scrydantic::multiple_of whose "
                       "value, 0, is not positive"));
static_assert(mentions(scrydantic::supported_value_diagnostic<NegativeFactor>(),
                       "NegativeFactor::value: carries a scrydantic::multiple_of whose "
                       "value, -0.5, is not positive"));
static_assert(mentions(scrydantic::supported_value_diagnostic<DecimalFactor>(),
                       "DecimalFactor::value: carries a scrydantic::multiple_of whose "
                       "value, 0.1, is not exactly that decimal in binary"));
static_assert(mentions(scrydantic::supported_value_diagnostic<FractionalFactor>(),
                       "FractionalFactor::value: carries a scrydantic::multiple_of "
                       "whose value, 0.5, is not an integer"));
static_assert(
    mentions(scrydantic::supported_value_diagnostic<NoFactor>(),
             "NoFactor::value: carries a scrydantic::multiple_of that sets no "
             "value"));
static_assert(
    mentions(scrydantic::supported_value_diagnostic<CrossedLength>(),
             "CrossedLength::text: carries a scrydantic::length whose min, 5, "
             "exceeds its max, 4"));
static_assert(mentions(scrydantic::supported_value_diagnostic<UnboundedItems>(),
                       "UnboundedItems::values: carries a scrydantic::items that sets "
                       "no bound"));
static_assert(mentions(scrydantic::supported_value_diagnostic<TwoRanges>(),
                       "TwoRanges::value: has more than one scrydantic::range "
                       "annotation"));
static_assert(mentions(scrydantic::supported_value_diagnostic<TwoPatterns>(),
                       "TwoPatterns::text: has more than one scrydantic::pattern "
                       "annotation"));
static_assert(mentions(scrydantic::supported_value_diagnostic<RangedClass>(),
                       "RangedClass carries scrydantic::range, which applies to data "
                       "members"));
static_assert(
    mentions(scrydantic::supported_value_diagnostic<HoldsCrossed>(),
             "HoldsCrossed::values[].value: carries a scrydantic::range whose "
             "min, 5, exceeds its max, 4"));
// Every family checks constraints, encoding included, though it does not apply them.
static_assert(mentions(scrydantic::encodable_diagnostic<Crossed>(),
                       "does not satisfy scrydantic::Encodable: "));
static_assert(mentions(scrydantic::encodable_diagnostic<Crossed>(),
                       "Crossed::value: carries a scrydantic::range whose min, 5, "
                       "exceeds its max, 4"));
static_assert(!scrydantic::Decodable<Crossed>);
// Each family's public diagnostic gives the constraint's problem as its concept
// rejects it, and none for the constrained types these tests decode.
static_assert(!scrydantic::Encodable<Crossed>);
static_assert(
    mentions(scrydantic::decodable_diagnostic<Crossed>(),
             "does not satisfy scrydantic::Decodable: {anonymous}::Crossed::value: "
             "carries a scrydantic::range whose min, 5, exceeds its max, 4"));
static_assert(!scrydantic::ToolArguments<Crossed>);
static_assert(
    mentions(scrydantic::tool_arguments_diagnostic<Crossed>(),
             "does not satisfy scrydantic::ToolArguments: {anonymous}::Crossed::value: "
             "carries a scrydantic::range whose min, 5, exceeds its max, 4"));
static_assert(scrydantic::Decodable<Budget> &&
              scrydantic::decodable_diagnostic<Budget>().empty());
static_assert(scrydantic::ToolArguments<Budget> &&
              scrydantic::tool_arguments_diagnostic<Budget>().empty());
