// DecodeOptions: which strings decode() accepts for a boolean, a number, an
// array, or an object when an option is on, that such a value is then held to
// everything a literal in its place is, what is never converted, and that a
// decode without the options is the strict one.

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <scrydantic/annotations.hpp>
#include <scrydantic/codec.hpp>
#include <scrydantic/decode_options.hpp>
#include <scrydantic/error.hpp>
#include <scrydantic/json.hpp>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace {

using scrydantic::check;
using scrydantic::DecodeOptions;
using scrydantic::items;
using scrydantic::length;
using scrydantic::multiple_of;
using scrydantic::range;
using scrydantic::Status;

constexpr DecodeOptions scalars{.coerce_scalars = true};
constexpr DecodeOptions structures{.coerce_structures = true};
constexpr DecodeOptions both{.coerce_scalars = true, .coerce_structures = true};

// ---- Models ------------------------------------------------------------------

enum class Unit : std::uint8_t {
  celsius,
  fahrenheit,
};

// The guide's model.
struct Reading {
  double degrees;
  Unit unit{Unit::celsius};
  std::optional<std::string> label;
};

struct Scalars {
  bool flag{};
  std::int16_t small{};
  std::int32_t whole{};
  std::int64_t wide{};
  std::uint16_t count{};
  std::uint32_t size{};
  std::uint64_t huge{};
  float ratio{};
  double amount{};
  std::optional<std::int32_t> maybe{};
  std::vector<std::int32_t> list{};
  std::array<bool, 2> pair{};
};

struct Boxed {
  std::indirect<double> value;
  std::optional<std::indirect<bool>> flag;
};

struct Untouched {
  std::string text{};
  scrydantic::Json raw{};
  Unit unit{};
  std::optional<std::string> maybe{};
};

[[nodiscard]] Status reject(std::string model_message) {
  return std::unexpected(scrydantic::rejection(std::move(model_message)));
}

Status is_even(const std::int32_t& value) {
  return value % 2 == 0 ? Status{} : reject("must be even");
}

Status is_whole(const double& value) {
  return value == std::floor(value) ? Status{} : reject("must be whole");
}

struct Limited {
  [[= range{.min = 0, .max = 150}]] std::int32_t age{};
  [[= multiple_of{5}]] std::int64_t step{};
  [[= range{.exclusive_min = 0.0, .max = 1}]] double share{};
  [[= multiple_of{0.25}]] double quarter{};
  [[= range{.min = 1}]] std::optional<double> maybe{};
  [[= check<&is_even>]] std::int32_t even{};
  [[ = range{.max = 10.0}, = check<&is_whole> ]] double level{};
  [[= length{.max = 3}]] std::string code{};
  [[= items{.max = 2}]] std::vector<std::int32_t> few{};
};

// Bounds where converting the parsed number to the member's type rounds, so the
// number is checked as the parser read it and again as converted: near 2.5
// floats are 2^-22 apart, and from 2^53 doubles are 2 apart.
struct Narrowed {
  [[= range{.min = 0.5f, .max = 2.5f}]] float inclusive{};
  [[= range{.exclusive_min = 0.5f, .exclusive_max = 2.5f}]] float exclusive{};
  [[= multiple_of{0.75f}]] float step{};
  [[= range{.max = 9007199254740992.0}]] double big{};
  [[= range{.exclusive_max = 9007199254740996.0}]] double bigger{};
  [[= multiple_of{2}]] double even{};
};

struct Address {
  std::string city;
  std::uint32_t zip{};
};

struct[[= scrydantic::tag{"circle"}]] Circle {
  double radius{};
};

struct[[= scrydantic::tag{"square"}]] Square {
  double side{};
};

using Shape = std::variant<Circle, Square>;

struct Span;
Status is_ordered(const Span& span);

struct[[= check<&is_ordered>]] Span {
  std::int32_t from{};
  std::int32_t to{};
};

Status is_ordered(const Span& span) {
  return span.from <= span.to ? Status{} : reject("must not end before it starts");
}

struct Customer {
  Address home;
  std::optional<Address> work{};
  std::vector<std::int32_t> scores{};
  std::array<bool, 2> flags{};
  std::optional<Shape> shape{};
  std::vector<Address> others{};
  std::optional<Span> span{};
  [[= items{.max = 2}]] std::vector<std::string> tags{};
  std::optional<std::indirect<Address>> boxed{};
};

// Nests two deep for each level: an object, and the array of its children.
struct Tree {
  std::vector<Tree> kids{};
};

// ---- Helpers -----------------------------------------------------------------

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

using Lines = std::vector<std::string>;

// The issues of a decode that must fail.
template <typename Type>
[[nodiscard]] Lines rejection(const std::string_view text,
                              const DecodeOptions& options) {
  const auto decoded = scrydantic::decode<Type>(text, options);
  REQUIRE_FALSE(decoded);
  return issue_lines(decoded.error());
}

// The JSON string that holds `text`.
[[nodiscard]] std::string quoted(const std::string_view text) {
  return scrydantic::escape_json_string(text);
}

// Checks that `coerced`, decoded with `options`, is what `strict` is without
// them: the same value, or the same issues and messages.
template <typename Type>
void check_alike(const std::string_view strict, const std::string_view coerced,
                 const DecodeOptions& options) {
  CAPTURE(strict, coerced);
  const auto expected = scrydantic::decode<Type>(strict);
  const auto decoded = scrydantic::decode<Type>(coerced, options);
  REQUIRE(decoded.has_value() == expected.has_value());
  if (expected) {
    // Compared as text, which tells -0 from 0 and needs no operator==.
    CHECK(scrydantic::encode(*decoded)->text == scrydantic::encode(*expected)->text);
  } else {
    CHECK(issue_lines(decoded.error()) == issue_lines(expected.error()));
    CHECK(decoded.error().message == expected.error().message);
    CHECK(decoded.error().model_message == expected.error().model_message);
  }
}

// Checks that the string holding `literal` decodes to a Type, as the root, as
// the literal itself does.
template <typename Type> void check_as_literal(const std::string_view literal) {
  check_alike<Type>(literal, quoted(literal), scalars);
}

// `levels` Trees, each the only child of the one before: text that nests
// 2 * levels deep.
[[nodiscard]] std::string tree_text(const std::size_t levels) {
  std::string text{};
  for (std::size_t level = 1; level < levels; ++level) {
    text += R"({"kids":[)";
  }
  text += R"({"kids":[]})";
  for (std::size_t level = 1; level < levels; ++level) {
    text += "]}";
  }
  return text;
}

// How many Trees stand in a line from `tree` down its first children.
[[nodiscard]] std::size_t levels_of(const Tree& tree) {
  std::size_t levels = 1;
  for (const Tree* node = &tree; !node->kids.empty(); node = &node->kids.front()) {
    ++levels;
  }
  return levels;
}

} // namespace

// ---- Scalars -----------------------------------------------------------------

TEST_CASE("coerce_scalars accepts a string for a number, and nothing else changes") {
  constexpr std::string_view text = R"({"degrees":"21.5","label":null})";
  const auto reading = scrydantic::decode<Reading>(text, scalars);
  REQUIRE(reading);
  CHECK(reading->degrees == 21.5);
  CHECK(reading->unit == Unit::celsius);
  CHECK_FALSE(reading->label);

  // Without the option the input fails as it always has, however the decode
  // is spelled.
  constexpr std::string_view strict = "$.degrees must be a number";
  for (const auto& failed :
       {scrydantic::decode<Reading>(text), scrydantic::decode<Reading>(text, {}),
        scrydantic::decode<Reading>(text, DecodeOptions{}),
        scrydantic::decode<Reading>(text, structures)}) {
    REQUIRE_FALSE(failed);
    CHECK(failed.error().message == strict);
    CHECK(failed.error().model_message == strict);
    CHECK(issue_lines(failed.error()) ==
          Lines{"wrong_type $.degrees must be a number"});
  }

  // With it, a string that spells no number fails in the same words.
  const auto warm =
      scrydantic::decode<Reading>(R"({"degrees":"warm","label":null})", scalars);
  REQUIRE_FALSE(warm);
  CHECK(warm.error().message == strict);
  CHECK(warm.error().model_message == strict);
}

TEST_CASE("every source of a decode takes the options") {
  constexpr std::string_view text = R"({"degrees":"21.5","label":"7"})";
  const auto from_text = scrydantic::decode<Reading>(text, scalars);
  const auto from_json =
      scrydantic::decode<Reading>(scrydantic::Json{.text = std::string{text}}, scalars);
  const auto from_view =
      scrydantic::decode<Reading>(*scrydantic::JsonView::parse(text), scalars);
  REQUIRE(from_text);
  REQUIRE(from_json);
  REQUIRE(from_view);
  for (const auto* reading : {&*from_text, &*from_json, &*from_view}) {
    CHECK(reading->degrees == 21.5);
    CHECK(reading->label == "7");
  }

  // The text of the decode itself is JSON, whatever the options.
  const auto bare = scrydantic::decode<double>("21.5 degrees", both);
  REQUIRE_FALSE(bare);
  CHECK(issue_lines(bare.error()) ==
        Lines{"invalid_json $ is not valid JSON: unexpected text after the value at "
              "line 1, column 6"});
}

TEST_CASE("a string that spells a boolean decodes as the boolean") {
  const auto decoded = scrydantic::decode<Scalars>(
      R"({"flag":"true","pair":["false","true"]})", scalars);
  REQUIRE(decoded);
  CHECK(decoded->flag);
  CHECK(decoded->pair == std::array{false, true});

  check_as_literal<bool>("true");
  check_as_literal<bool>("false");
  // A number is not a boolean, in a string or out of one.
  check_as_literal<bool>("1");
  check_as_literal<bool>("0");
  check_as_literal<bool>("null");

  for (const std::string_view text : {"", "True", "TRUE", "yes", "on", "t", " true",
                                      "true ", "truefalse", "\"true\"", "[true]"}) {
    CAPTURE(text);
    CHECK(rejection<bool>(quoted(text), scalars) ==
          Lines{"wrong_type $ must be a boolean"});
  }
}

TEST_CASE("a string that spells an integer decodes as the integer") {
  const auto decoded = scrydantic::decode<Scalars>(
      R"({"count":"65535","huge":"18446744073709551615","list":["1","-2",3],)"
      R"("maybe":"-7","size":"4294967295","small":"-32768","whole":"5",)"
      R"("wide":"-9223372036854775808"})",
      scalars);
  REQUIRE(decoded);
  CHECK(decoded->count == 65535);
  CHECK(decoded->huge == std::numeric_limits<std::uint64_t>::max());
  CHECK(decoded->list == std::vector<std::int32_t>{1, -2, 3});
  CHECK(decoded->maybe == -7);
  CHECK(decoded->size == std::numeric_limits<std::uint32_t>::max());
  CHECK(decoded->small == -32768);
  CHECK(decoded->whole == 5);
  CHECK(decoded->wide == std::numeric_limits<std::int64_t>::min());

  // Whole or not is how the number is spelled, as it is for a literal, and the
  // type's range holds.
  for (const std::string_view literal :
       {"0", "5", "-5", "300", "32767", "32768", "-32768", "-32769", "70000", "5.0",
        "5e0", "1e2", "0.5", "-0", "18446744073709551616", "true", "null"}) {
    check_as_literal<std::int16_t>(literal);
    check_as_literal<std::int32_t>(literal);
    check_as_literal<std::int64_t>(literal);
    check_as_literal<std::uint16_t>(literal);
    check_as_literal<std::uint32_t>(literal);
    check_as_literal<std::uint64_t>(literal);
  }

  CHECK(*scrydantic::decode<std::int16_t>(R"("300")", scalars) == 300);
  CHECK(rejection<std::int16_t>(R"("70000")", scalars) ==
        Lines{"out_of_range $ is outside the integer range"});
  CHECK(rejection<std::uint32_t>(R"("-1")", scalars) ==
        Lines{"out_of_range $ is outside the integer range"});
  CHECK(rejection<std::int32_t>(R"("5.0")", scalars) ==
        Lines{"wrong_type $ must be an integer"});

  // Text that is not one JSON number from its first byte to its last.
  for (const std::string_view text :
       {"", "abc", " 5", "5 ", "\t5", "5\n", "+5", "05", "0x5", "5,", "5 6", "1_000",
        "five", "-", "\"5\"", "[5]", "{}"}) {
    CAPTURE(text);
    CHECK(rejection<std::int32_t>(quoted(text), scalars) ==
          Lines{"wrong_type $ must be an integer"});
    CHECK(rejection<std::uint64_t>(quoted(text), scalars) ==
          Lines{"wrong_type $ must be an integer"});
  }
}

TEST_CASE("a string that spells a number decodes as a floating-point number") {
  const auto decoded =
      scrydantic::decode<Scalars>(R"({"amount":"21.5","ratio":"-1e-3"})", scalars);
  REQUIRE(decoded);
  CHECK(decoded->amount == 21.5);
  CHECK(decoded->ratio == -1e-3F);

  // An integer is a number too, the sign of a zero is kept, and a float's range
  // holds.
  for (const std::string_view literal :
       {"0", "-0", "5", "-5", "21.5", "1e2", "1E-7", "0.1", "18446744073709551616",
        "9007199254740993", "3.4028235e38", "3.5e38", "-3.5e38", "1e-50", "5e-324",
        "true", "null"}) {
    check_as_literal<float>(literal);
    check_as_literal<double>(literal);
  }

  CHECK(rejection<float>(R"("3.5e38")", scalars) ==
        Lines{"out_of_range $ must be a finite in-range number"});

  // Text that is not one JSON number, and a number that no double holds, which
  // is not JSON text either.
  for (const std::string_view text :
       {"", "abc", " 1.5", "1.5 ", "+1.5", ".5", "5.", "1e", "NaN", "Infinity", "-inf",
        "1,5", "0x1p3", "1e400", "\"1.5\"", "[1.5]"}) {
    CAPTURE(text);
    CHECK(rejection<double>(quoted(text), scalars) ==
          Lines{"wrong_type $ must be a number"});
    CHECK(rejection<float>(quoted(text), scalars) ==
          Lines{"wrong_type $ must be a number"});
  }
}

TEST_CASE("scalars are coerced through an optional and an indirect") {
  const auto decoded =
      scrydantic::decode<Boxed>(R"({"flag":"true","value":"2.5"})", scalars);
  REQUIRE(decoded);
  CHECK(*decoded->value == 2.5);
  REQUIRE(decoded->flag);
  CHECK(**decoded->flag);

  const auto absent =
      scrydantic::decode<Boxed>(R"({"flag":null,"value":"0"})", scalars);
  REQUIRE(absent);
  CHECK_FALSE(absent->flag);

  CHECK(rejection<Boxed>(R"({"flag":"null","value":"x"})", scalars) ==
        Lines{"wrong_type $.value must be a number",
              "wrong_type $.flag must be a boolean"});
}

TEST_CASE("a coerced value meets its member's constraints and validators") {
  const auto decoded = scrydantic::decode<Limited>(
      R"({"age":"150","even":"4","level":"10","maybe":"1","quarter":"0.75",)"
      R"("share":"1","step":"-10"})",
      scalars);
  REQUIRE(decoded);
  CHECK(decoded->age == 150);
  CHECK(decoded->even == 4);
  CHECK(decoded->level == 10.0);
  CHECK(decoded->maybe == 1.0);
  CHECK(decoded->quarter == 0.75);
  CHECK(decoded->share == 1.0);
  CHECK(decoded->step == -10);

  CHECK(rejection<Limited>(
            R"({"age":"151","even":"3","level":"2.5","maybe":"0.5","quarter":"0.3",)"
            R"("share":"0","step":"7"})",
            scalars) ==
        Lines{"violated_constraint $.age must be at most 150",
              "violated_constraint $.step must be a multiple of 5",
              "violated_constraint $.share must be greater than 0",
              "violated_constraint $.quarter must be a multiple of 0.25",
              "violated_constraint $.maybe must be at least 1",
              "failed_check $.even must be even",
              "failed_check $.level must be whole"});
  // A constraint comes before a validator, and a value of the wrong type before
  // both.
  CHECK(rejection<Limited>(R"({"age":"old","even":"3.0","level":"10.5"})", scalars) ==
        Lines{"wrong_type $.age must be an integer",
              "wrong_type $.even must be an integer",
              "violated_constraint $.level must be at most 10"});

  // Each failure is the one the literal has.
  for (const std::string_view literal :
       {"-1", "0", "150", "151", "1e2", "200", "99999999999"}) {
    check_alike<Limited>(std::string{R"({"age":)"} + std::string{literal} + "}",
                         R"({"age":)" + quoted(literal) + "}", scalars);
  }
  for (const std::string_view literal : {"0", "1", "2", "0.5", "1e0", "true"}) {
    check_alike<Limited>(std::string{R"({"maybe":)"} + std::string{literal} + "}",
                         R"({"maybe":)" + quoted(literal) + "}", scalars);
  }
}

TEST_CASE("a coerced number is checked as the parser read it, then as converted") {
  const auto check_member = [](const std::string_view key,
                               const std::string_view literal) {
    const std::string member = quoted(key) + ":";
    check_alike<Narrowed>("{" + member + std::string{literal} + "}",
                          "{" + member + quoted(literal) + "}", scalars);
  };
  for (const std::string_view literal :
       {"0.5", "2.5", "0.49999999", "2.50000001", "2.5000001", "0.4999999", "3", "0"}) {
    check_member("inclusive", literal);
    check_member("exclusive", literal);
  }
  for (const std::string_view literal : {"0.75", "1.5", "0.7500000001", "1", "3"}) {
    check_member("step", literal);
  }
  for (const std::string_view literal :
       {"9007199254740992", "9007199254740993", "9007199254740994", "9007199254740995",
        "9007199254740996", "9007199254740997", "9.007199254740993e15", "-1", "2", "3",
        "2.0000000000000001", "18446744073709551614", "18446744073709551615"}) {
    check_member("big", literal);
    check_member("bigger", literal);
    check_member("even", literal);
  }

  // A whole number is compared as the integer it is, not as the double it
  // rounds to: 2^53 + 1 rounds onto the bound and is still above it.
  CHECK(rejection<Narrowed>(R"({"big":"9007199254740993"})", scalars) ==
        Lines{"violated_constraint $.big must be at most 9007199254740992"});
  CHECK(rejection<Narrowed>(R"({"even":"9007199254740993"})", scalars) ==
        Lines{"violated_constraint $.even must be a multiple of 2"});
}

// ---- What is never converted -------------------------------------------------

TEST_CASE("a string is never reinterpreted where a string is wanted") {
  const auto decoded = scrydantic::decode<Untouched>(
      R"({"maybe":"null","raw":"[1, 2]","text":"5","unit":"fahrenheit"})", both);
  REQUIRE(decoded);
  CHECK(decoded->maybe == "null");
  CHECK(decoded->raw.text == R"("[1, 2]")");
  CHECK(decoded->text == "5");
  CHECK(decoded->unit == Unit::fahrenheit);

  for (const std::string_view text : {"true", "5", "1.5", "null", "", "[1]", "{}"}) {
    CAPTURE(text);
    const auto kept = scrydantic::decode<std::string>(quoted(text), both);
    REQUIRE(kept);
    CHECK(*kept == text);
  }

  // A constraint on a string counts the string.
  CHECK(rejection<Limited>(R"({"code":"12345"})", both) ==
        Lines{"violated_constraint $.code must have at most 3 characters"});
}

TEST_CASE("nothing but a string is coerced, and never to a string or to null") {
  // A number or a boolean is no string, and an enumerator is a name.
  CHECK(rejection<Untouched>(R"({"maybe":true,"text":5,"unit":0})", both) ==
        Lines{"wrong_type $.text must be a string",
              "wrong_type $.unit must be an enumerator name",
              "wrong_type $.maybe must be a string"});
  CHECK(rejection<Untouched>(R"({"unit":"0"})", both) ==
        Lines{"unknown_enumerator $.unit is not a declared enumerator; must be one of: "
              "celsius, fahrenheit"});

  // A number is no boolean, and a boolean no number.
  CHECK(rejection<Scalars>(R"({"amount":true,"flag":1,"whole":false})", both) ==
        Lines{"wrong_type $.flag must be a boolean",
              "wrong_type $.whole must be an integer",
              "wrong_type $.amount must be a number"});
  CHECK(rejection<Scalars>(R"({"amount":"true","flag":"1","whole":"false"})", both) ==
        Lines{"wrong_type $.flag must be a boolean",
              "wrong_type $.whole must be an integer",
              "wrong_type $.amount must be a number"});

  // A null is spelled as one.
  CHECK(rejection<Scalars>(R"({"maybe":"null"})", both) ==
        Lines{"wrong_type $.maybe must be an integer"});
  CHECK(rejection<Scalars>(R"({"maybe":""})", both) ==
        Lines{"wrong_type $.maybe must be an integer"});
  CHECK(rejection<Customer>(R"({"home":{"city":"Oslo"},"work":"null"})", both) ==
        Lines{"wrong_type $.work must be an object"});
  CHECK(rejection<Customer>(R"({"home":{"city":"Oslo"},"work":""})", both) ==
        Lines{"wrong_type $.work must be an object"});
  CHECK(rejection<Scalars>(R"({"whole":null})", both) ==
        Lines{"wrong_type $.whole must be an integer"});
}

TEST_CASE("each option coerces only what it names") {
  constexpr std::string_view text = R"({"list":"[1,2]","whole":"5"})";
  CHECK(rejection<Scalars>(text, {}) == Lines{"wrong_type $.whole must be an integer",
                                              "wrong_type $.list must be an array"});
  CHECK(rejection<Scalars>(text, scalars) ==
        Lines{"wrong_type $.list must be an array"});
  CHECK(rejection<Scalars>(text, structures) ==
        Lines{"wrong_type $.whole must be an integer"});
  const auto decoded = scrydantic::decode<Scalars>(text, both);
  REQUIRE(decoded);
  CHECK(decoded->list == std::vector<std::int32_t>{1, 2});
  CHECK(decoded->whole == 5);

  // The text of a structure holds strings of its own, which the same options
  // govern.
  constexpr std::string_view nested = R"({"list":"[\"1\",2]"})";
  CHECK(rejection<Scalars>(nested, structures) ==
        Lines{"wrong_type $.list[0] must be an integer"});
  const auto inner = scrydantic::decode<Scalars>(nested, both);
  REQUIRE(inner);
  CHECK(inner->list == std::vector<std::int32_t>{1, 2});
}

// ---- Structures --------------------------------------------------------------

TEST_CASE("coerce_structures accepts a string for an object or an array") {
  const auto decoded = scrydantic::decode<Customer>(
      R"({"boxed":"{\"city\":\"Rome\",\"zip\":7}",)"
      R"("flags":"[true,false]",)"
      R"("home":"{\"city\":\"Oslo\",\"zip\":150}",)"
      R"("others":"[{\"city\":\"Lima\"},{\"city\":\"Bern\",\"zip\":3000}]",)"
      R"("scores":" [1, 2,\n 3] ",)"
      R"("shape":"{\"type\":\"square\",\"side\":2}",)"
      R"("span":"{\"from\":1,\"to\":2}",)"
      R"("tags":"[\"a\",\"[b]\"]",)"
      R"("work":"{\"zip\":1,\"city\":\"Lima\"}"})",
      structures);
  REQUIRE(decoded);
  REQUIRE(decoded->boxed);
  CHECK((*decoded->boxed)->city == "Rome");
  CHECK(decoded->flags == std::array{true, false});
  CHECK(decoded->home.city == "Oslo");
  CHECK(decoded->home.zip == 150);
  REQUIRE(decoded->others.size() == 2);
  CHECK(decoded->others[1].zip == 3000);
  CHECK(decoded->scores == std::vector<std::int32_t>{1, 2, 3});
  REQUIRE(decoded->shape);
  REQUIRE(std::holds_alternative<Square>(*decoded->shape));
  CHECK(std::get<Square>(*decoded->shape).side == 2.0);
  REQUIRE(decoded->span);
  CHECK(decoded->span->to == 2);
  // A string in the text is a string where one is wanted.
  CHECK(decoded->tags == std::vector<std::string>{"a", "[b]"});
  REQUIRE(decoded->work);
  CHECK(decoded->work->city == "Lima");

  // The root is a value like any other.
  const auto list =
      scrydantic::decode<std::vector<std::int32_t>>(R"("[1,2]")", structures);
  REQUIRE(list);
  CHECK(*list == std::vector<std::int32_t>{1, 2});
  const auto shape = scrydantic::decode<Shape>(
      R"("{\"radius\":1.5,\"type\":\"circle\"}")", structures);
  REQUIRE(shape);
  CHECK(std::get<Circle>(*shape).radius == 1.5);

  // The same input is not accepted without the option.
  CHECK(rejection<std::vector<std::int32_t>>(R"("[1,2]")", scalars) ==
        Lines{"wrong_type $ must be an array"});
  CHECK(rejection<Shape>(R"("{\"radius\":1.5,\"type\":\"circle\"}")", {}) ==
        Lines{"wrong_type $ must be an object"});
}

TEST_CASE("an issue inside a coerced structure keeps the outer path") {
  CHECK(rejection<Customer>(R"({"flags":"[true]",)"
                            R"("home":"{\"city\":7,\"street\":\"x\",\"zip\":-1}",)"
                            R"("others":"[{\"city\":\"Lima\"},{\"zip\":1}]",)"
                            R"("scores":"[1,\"two\",3.5]",)"
                            R"("shape":"{\"type\":\"hexagon\"}",)"
                            R"("span":"{\"from\":2,\"to\":1}",)"
                            R"("tags":"[\"a\",\"b\",\"c\"]",)"
                            R"("work":"{\"zip\":1}"})",
                            structures) ==
        Lines{"unknown_member $.home contains unknown member \"street\"",
              "wrong_type $.home.city must be a string",
              "out_of_range $.home.zip is outside the integer range",
              "missing_member $.work.city is a required member",
              "wrong_type $.scores[1] must be an integer",
              "wrong_type $.scores[2] must be an integer",
              "wrong_size $.flags must be an array of the declared fixed size",
              "unknown_tag $.shape.type must be one of: circle, square",
              "missing_member $.others[1].city is a required member",
              "failed_check $.span must not end before it starts",
              "violated_constraint $.tags must have at most 2 items"});

  CHECK(rejection<Customer>(R"({"home":"{\"city\":\"Oslo\"}","shape":"{\"side\":1}"})",
                            structures) ==
        Lines{"missing_tag $.shape.type is a required member; must be one of: circle, "
              "square"});
}

TEST_CASE("a string that does not hold the structure is a value of the wrong type") {
  // Not JSON, JSON of another kind, and a string that holds the text: each
  // string is read once.
  for (const std::string_view text :
       {"", "Oslo", "{", "{\"city\":\"Oslo\"", "[1,2]", "5", "null", "true",
        "\"{\\\"city\\\":\\\"Oslo\\\"}\"", "{\"city\":\"Oslo\"} {}"}) {
    CAPTURE(text);
    CHECK(rejection<Address>(quoted(text), both) ==
          Lines{"wrong_type $ must be an object"});
    CHECK(rejection<Shape>(quoted(text), both) ==
          Lines{"wrong_type $ must be an object"});
  }
  for (const std::string_view text :
       {"", "1,2", "[1,2", "{}", "5", "null", "\"[1,2]\"", "[1,2],"}) {
    CAPTURE(text);
    CHECK(rejection<std::vector<std::int32_t>>(quoted(text), both) ==
          Lines{"wrong_type $ must be an array"});
    CHECK((rejection<std::array<std::int32_t, 2>>(quoted(text), both)) ==
          Lines{"wrong_type $ must be an array of the declared fixed size"});
  }

  // A number, a boolean, and a null are as wrong as they were.
  CHECK(rejection<Customer>(R"({"home":5,"scores":true,"shape":[]})", both) ==
        Lines{"wrong_type $.home must be an object",
              "wrong_type $.scores must be an array",
              "wrong_type $.shape must be an object"});
}

TEST_CASE("a structure inside a coerced structure may be a string too") {
  const auto decoded = scrydantic::decode<Customer>(
      R"({"home":"{\"city\":\"Oslo\"}","others":"[\"{\\\"city\\\":\\\"Lima\\\"}\"]"})",
      structures);
  REQUIRE(decoded);
  REQUIRE(decoded->others.size() == 1);
  CHECK(decoded->others[0].city == "Lima");

  CHECK(
      rejection<Customer>(
          R"({"home":"{\"city\":\"Oslo\"}","others":"[\"{\\\"zip\\\":\\\"1\\\"}\"]"})",
          structures) == Lines{"missing_member $.others[0].city is a required member",
                               "wrong_type $.others[0].zip must be an integer"});
}

TEST_CASE("a coerced value is read while the ones around it are still being read") {
  // An array in a string, of objects that are each in a string and hold a
  // number in a string: the decode comes back to the array's text after each
  // object, and to the document after the array.
  std::string others{};
  for (std::size_t index = 0; index < 5; ++index) {
    const std::string number = std::to_string(index);
    others += index == 0 ? "[" : ",";
    others += quoted(R"({"city":"city )" + number + R"(","zip":")" + number + R"("})");
  }
  others += "]";
  const auto decoded = scrydantic::decode<Customer>(
      R"({"home":)" + quoted(R"({"city":"Oslo","zip":"7"})") + R"(,"others":)" +
          quoted(others) + R"(,"scores":["1","2"],"tags":["a"],"work":)" +
          quoted(R"({"city":"Lima","zip":"8"})") + "}",
      both);
  REQUIRE(decoded);
  CHECK(decoded->home.city == "Oslo");
  CHECK(decoded->home.zip == 7);
  REQUIRE(decoded->work);
  CHECK(decoded->work->city == "Lima");
  CHECK(decoded->work->zip == 8);
  CHECK(decoded->scores == std::vector<std::int32_t>{1, 2});
  REQUIRE(decoded->others.size() == 5);
  for (std::size_t index = 0; index < 5; ++index) {
    CHECK(decoded->others[index].city == "city " + std::to_string(index));
    CHECK(decoded->others[index].zip == index);
  }
  CHECK(decoded->tags == std::vector<std::string>{"a"});
}

TEST_CASE("issues inside a coerced structure count toward max_issues") {
  std::string elements{};
  for (std::size_t index = 0; index < 2 * scrydantic::max_issues; ++index) {
    elements += index == 0 ? "null" : ",null";
  }
  const auto decoded = scrydantic::decode<Scalars>(
      R"({"list":")" + ("[" + elements + "]\"}"), structures);
  REQUIRE_FALSE(decoded);
  REQUIRE(decoded.error().issues.size() == scrydantic::max_issues);
  CHECK(issue_lines(decoded.error()).back() ==
        "wrong_type $.list[63] must be an integer");
  CHECK(decoded.error().message.ends_with(
      "\nfurther issues past the first 64 were not reported"));
}

// ---- The nesting limit -------------------------------------------------------

TEST_CASE("the text of a coerced structure counts toward the nesting limit") {
  static_assert(scrydantic::max_json_depth == 256);

  // At the root nothing is open around the string, so its text may nest as deep
  // as any text.
  const auto root = scrydantic::decode<Tree>(quoted(tree_text(128)), structures);
  REQUIRE(root);
  CHECK(levels_of(*root) == 128);

  // Inside one array, one level less.
  const auto element = scrydantic::decode<std::vector<Tree>>(
      "[" + quoted(tree_text(127)) + "]", structures);
  REQUIRE(element);
  CHECK(levels_of(element->front()) == 127);
  CHECK(rejection<std::vector<Tree>>("[" + quoted(tree_text(128)) + "]", structures) ==
        Lines{"wrong_type $[0] must be an object"});

  // Inside an object and its array, exactly the limit and one past it.
  const auto child = scrydantic::decode<Tree>(
      R"({"kids":[)" + quoted(tree_text(127)) + "]}", structures);
  REQUIRE(child);
  CHECK(levels_of(*child) == 128);
  CHECK(rejection<Tree>(
            R"({"kids":[{"kids":)" + quoted("[" + tree_text(127) + "]") + "}]}",
            structures) == Lines{"wrong_type $.kids[0].kids must be an array"});
  const auto grandchild = scrydantic::decode<Tree>(
      R"({"kids":[{"kids":)" + quoted("[" + tree_text(126) + "]") + "}]}", structures);
  REQUIRE(grandchild);
  CHECK(levels_of(*grandchild) == 128);
}

TEST_CASE("text inside text adds to the same nesting limit") {
  // A Tree whose children are a string, whose only Tree has children that are a
  // string again: the levels of all three texts add up.
  const auto chained = [](const std::size_t levels) {
    const std::string inner = "[" + tree_text(levels) + "]";
    const std::string middle = R"([{"kids":)" + quoted(inner) + "}]";
    return R"({"kids":)" + quoted(middle) + "}";
  };
  // One object, one array and its object, then an array of `levels` Trees:
  // 4 + 2 * levels deep.
  const auto within = scrydantic::decode<Tree>(chained(126), structures);
  REQUIRE(within);
  CHECK(levels_of(*within) == 128);
  CHECK(rejection<Tree>(chained(127), structures) ==
        Lines{"wrong_type $.kids[0].kids must be an array"});
}

TEST_CASE("coerce_structures reads a map from a string that spells an object") {
  using Counts = std::map<std::string, int>;
  const scrydantic::DecodeOptions options{.coerce_structures = true};

  const auto counts = scrydantic::decode<Counts>(R"("{\"a\":1,\"b\":2}")", options);
  REQUIRE(counts);
  CHECK(*counts == Counts{{"a", 1}, {"b", 2}});

  const auto inner = scrydantic::decode<Counts>(R"("{\"a\":\"x\"}")", options);
  REQUIRE_FALSE(inner);
  CHECK(inner.error().model_message == "$.a must be an integer");

  const auto strict = scrydantic::decode<Counts>(R"("{\"a\":1}")");
  REQUIRE_FALSE(strict);
  CHECK(strict.error().model_message == "$ must be an object");
}
