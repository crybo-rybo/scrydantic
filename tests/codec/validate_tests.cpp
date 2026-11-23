// validate(): a value the program built is checked against the constraints and
// validators of its type, and reported as decoding its JSON would report it.
// Each constraint kind, member and class validators and the order they run in,
// every shape the walk goes through, the cap on issues, what is left to
// encode(), and the types that are known valid at compile time.

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <meta>
#include <optional>
#include <scrydantic/annotations.hpp>
#include <scrydantic/codec.hpp>
#include <scrydantic/detail/constraints.hpp>
#include <scrydantic/diagnostics.hpp>
#include <scrydantic/error.hpp>
#include <scrydantic/json.hpp>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

namespace {

using scrydantic::check;
using scrydantic::flatten;
using scrydantic::IssueKind;
using scrydantic::items;
using scrydantic::length;
using scrydantic::multiple_of;
using scrydantic::pattern;
using scrydantic::range;
using scrydantic::Status;
using scrydantic::validate;

// ---- Constrained types -------------------------------------------------------

// The issue's example.
struct Ranged {
  [[= range{.min = 0, .max = 10}]] int v;
};

struct Ages {
  [[= range{.min = 0, .max = 150}]] std::int32_t inclusive{};
  [[= range{.exclusive_min = 0, .exclusive_max = 10}]] std::int64_t exclusive{1};
  [[= range{.min = 1}]] std::uint32_t at_least{1};
};

struct Shares {
  [[= range{.exclusive_min = 0.0, .max = 1}]] double share{0.5};
  [[= range{.exclusive_max = 2.5f}]] float below{};
  [[= range{.min = 0.0}]] double positive{};
};

struct Steps {
  [[= multiple_of{5}]] std::int32_t five{};
  [[= multiple_of{0.25}]] double quarter{};
  [[= multiple_of{0.75f}]] float step{};
  [[ = range{.max = 20}, = multiple_of{3} ]] std::uint16_t both{};
};

struct Texts {
  [[= length{.min = 1, .max = 3}]] std::string short_name{"a"};
  [[= pattern{R"(^\d+$)"}]] std::string digits;
};

struct Lists {
  [[= items{.min = 1, .max = 2}]] std::vector<std::int32_t> pair{1};
};

struct Row {
  [[= range{.min = 0}]] std::int32_t age{};
};

struct Many {
  std::vector<Row> rows;
};

// A renamed member is reported under its wire name.
struct Budget {
  [[ = scrydantic::name{"max_tokens"},
     = range{.min = 1, .max = 4096} ]] std::uint32_t limit{1};
};

// A default outside its own constraint, which decoding never checks.
struct Counter {
  [[= range{.min = 1}]] std::int32_t count = 0;
};

// ---- Validators --------------------------------------------------------------

// How many times each counting validator has run.
struct Calls {
  std::size_t nonempty{};
  std::size_t capitalized{};
  std::size_t ordered{};
  std::size_t window{};
  std::size_t never{};
};

Calls calls{};

[[nodiscard]] Status reject(std::string model_message, std::string host_message = {}) {
  return std::unexpected(
      scrydantic::rejection(std::move(model_message), std::move(host_message)));
}

Status is_known_city(const std::string& city) {
  return city == "Oslo" || city == "Lima"
             ? Status{}
             : reject("must be a known city",
                      "names " + city + ", which the gazetteer lacks");
}

Status is_nonempty(const std::string& text) {
  ++calls.nonempty;
  return text.empty() ? reject("must not be empty") : Status{};
}

// Relies on is_nonempty, which runs first: front() of an empty string is
// undefined, so this one must never see one.
Status is_capitalized(const std::string& text) {
  ++calls.capitalized;
  return text.front() >= 'A' && text.front() <= 'Z' ? Status{}
                                                    : reject("must be capitalized");
}

Status is_even(const std::int32_t& value) noexcept {
  return value % 2 == 0 ? Status{} : reject("must be even");
}

Status is_never(const std::int32_t& /*value*/) {
  ++calls.never;
  return reject("is never valid");
}

struct Trip {
  [[= check<&is_known_city>]] std::string from{"Oslo"};
  [[ = length{.max = 4}, = check<&is_known_city> ]] std::string to{"Lima"};
  [[= check<&is_even>]] std::optional<std::int32_t> passengers;
};

struct Title {
  [[ = length{.max = 16}, = check<&is_nonempty>,
     = check<&is_capitalized> ]] std::string text;
};

struct Interval;
Status is_ordered(const Interval& interval);

struct[[= check<&is_ordered>]] Interval {
  [[= range{.min = 0}]] std::int32_t low{};
  std::int32_t high{};
};

Status is_ordered(const Interval& interval) {
  ++calls.ordered;
  return interval.low <= interval.high ? Status{}
                                       : reject("must have low at most high");
}

// ---- Shapes ------------------------------------------------------------------

struct[[= scrydantic::tag{"circle"}]] Disc {
  [[= range{.exclusive_min = 0.0}]] double radius{1.0};
};

struct[[= scrydantic::tag{"label"}]] Label {
  [[= length{.min = 1}]] std::string text{"x"};
};

struct[[= scrydantic::tag{"gap"}]] Gap {
  std::int32_t width{};
};

using Mark = std::variant<Disc, Label, Gap>;

struct Nested {
  Row row;
  std::optional<Row> maybe;
  std::vector<Row> rows;
  std::array<Row, 2> pair;
  std::indirect<Row> boxed;
  std::optional<std::indirect<Row>> maybe_boxed;
  std::map<std::string, Row> by_name;
  Mark mark;
  std::vector<Mark> marks;
};

// Constraints and a validator on members that see through an optional and an
// indirect to the value they bound.
struct Wrapped {
  [[= range{.min = 1}]] std::optional<std::int32_t> maybe{};
  [[= range{.min = 1}]] std::indirect<std::int32_t> boxed{1};
  [[= check<&is_even>]] std::optional<std::indirect<std::int32_t>> both{};
  [[= items{.max = 1}]] std::optional<std::vector<Row>> rows{};
};

// Every value in it is an issue, and each check of one is counted.
struct Noisy {
  [[= check<&is_never>]] std::int32_t value{};
};

struct Crowd {
  std::vector<Noisy> listed;
  std::map<std::string, Noisy> sorted;
  std::unordered_map<std::string, Noisy> hashed;
  Noisy last;
};

// A map that iterates in an order of its own, and one with no order.
struct Ledgers {
  std::map<std::string, Row, std::greater<>> descending;
  std::unordered_map<std::string, Row> unordered;
};

// A flattened class: its members are the enclosing object's, at its path, and
// its validators take the part of the object it is.
struct Window;
Status is_window(const Window& window);

struct[[= check<&is_window>]] Window {
  [[= range{.min = 0}]] std::int32_t offset{};
  [[ = scrydantic::name{"page_size"},
     = range{.min = 1, .max = 100} ]] std::int32_t size{1};
};

Status is_window(const Window& window) {
  ++calls.window;
  return window.offset % window.size == 0 ? Status{}
                                          : reject("must start on a page boundary");
}

struct Listing {
  [[= length{.min = 1}]] std::string query{"q"};
  [[= flatten]] Window window;
};

struct Search {
  std::vector<Listing> listings;
};

// Constrained at every level of a recursive model.
struct Tree {
  [[= range{.min = 0}]] std::int32_t weight{};
  std::vector<Tree> children;
};

// A chain that can nest past what JSON holds.
struct Link {
  [[= range{.min = 0}]] std::int32_t weight{};
  std::optional<std::indirect<Link>> next;
};

// The same chain, linked through a flattened member, which is no object of its
// own in JSON and so no level of nesting.
struct Step;

struct Hop {
  std::optional<std::indirect<Step>> next;
};

struct Step {
  [[= range{.min = 0}]] std::int32_t weight{};
  [[= flatten]] Hop hop;
};

// What encode() refuses, beside a constraint.
enum class Color : std::uint8_t { red, green };

struct Refused {
  Color color{};
  std::string text{};
  double ratio{};
  [[= range{.min = 0}]] std::int32_t count{};
};

// Encodable only: a const member, and text the value borrows.
struct Borrowed {
  [[= range{.min = 1}]] const std::int32_t count;
  std::string_view label;
};

// No constraint and no validator anywhere.
struct Plain {
  std::int32_t count{};
  std::optional<std::string> label;
  std::vector<std::map<std::string, double>> samples;
  std::variant<Gap> gap;
};

// A recursive type with none.
struct Folder {
  std::string name;
  std::vector<Folder> folders;
};

// A type whose only check is deep inside it.
struct Deep {
  std::vector<std::optional<std::map<std::string, std::variant<Gap, Label>>>> layers;
};

// A type whose only check is its class's validator.
struct Span;
Status is_span(const Span& span);

struct[[= check<&is_span>]] Span {
  std::int32_t from{};
  std::int32_t to{};
};

Status is_span(const Span& span) {
  return span.from <= span.to ? Status{} : reject("must not end before it starts");
}

// A type whose only check is a flattened class's validator.
struct Paged {
  std::string query;
  [[= flatten]] Window window;
};

// A pattern is the schema's alone, so a type with nothing else has no check.
struct Digits {
  [[= pattern{"^[0-9]+$"}]] std::string text;
};

// A value of every shape, each part of it valid.
[[nodiscard]] Nested nested() {
  return Nested{
      .row = {.age = 1},
      .maybe = std::nullopt,
      .rows = {{.age = 0}},
      .pair = {Row{.age = 0}, Row{.age = 2}},
      .boxed = std::indirect<Row>(Row{.age = 3}),
      .maybe_boxed = std::nullopt,
      .by_name = {{"ada", {.age = 4}}},
      .mark = Disc{.radius = 1.0},
      .marks = {Label{.text = "a"}, Gap{.width = -1}},
  };
}

// A chain of `length` links, the innermost of which has `weight`.
[[nodiscard]] Link chain(const std::size_t length, const std::int32_t weight) {
  Link first{.weight = weight, .next = std::nullopt};
  for (std::size_t made = 1; made < length; ++made) {
    first = Link{.weight = 0, .next = std::indirect<Link>(std::move(first))};
  }
  return first;
}

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

// The issues of a value that must be invalid. Its JSON must then fail to decode
// with the same Error, issue for issue and word for word: the value has every
// member its JSON has, so the two walks check the same things.
template <typename Type>
[[nodiscard]] std::vector<std::string> violations(const Type& value) {
  const Status status = validate(value);
  REQUIRE_FALSE(status);
  const auto encoded = scrydantic::encode(value);
  REQUIRE(encoded);
  const auto decoded = scrydantic::decode<Type>(*encoded);
  REQUIRE_FALSE(decoded);
  CHECK(status.error().issues == decoded.error().issues);
  CHECK(status.error().message == decoded.error().message);
  CHECK(status.error().model_message == decoded.error().model_message);
  return issue_lines(status.error());
}

// Whether a value is valid, which its JSON then decodes to say as well.
template <typename Type> [[nodiscard]] bool valid(const Type& value) {
  const auto encoded = scrydantic::encode(value);
  REQUIRE(encoded);
  CHECK(scrydantic::decode<Type>(*encoded).has_value() == validate(value).has_value());
  return validate(value).has_value();
}

using Lines = std::vector<std::string>;

} // namespace

// ---- What is decided at compile time ------------------------------------------

static_assert(scrydantic::detail::has_checks_v<Ranged>);
static_assert(scrydantic::detail::has_checks_v<Interval>);
static_assert(scrydantic::detail::has_checks_v<Nested>);
static_assert(scrydantic::detail::has_checks_v<std::vector<Row>>);
static_assert(scrydantic::detail::has_checks_v<Mark>);
static_assert(scrydantic::detail::has_checks_v<Tree>);
static_assert(scrydantic::detail::has_checks_v<Deep>);
static_assert(scrydantic::detail::has_checks_v<Paged>);
static_assert(scrydantic::detail::has_checks_v<Span>);
static_assert(scrydantic::detail::has_checks_v<Noisy>);
static_assert(scrydantic::detail::has_checks_v<Step>);
static_assert(scrydantic::detail::has_checks_v<Borrowed>);

static_assert(!scrydantic::detail::has_checks_v<Plain>);
static_assert(!scrydantic::detail::has_checks_v<Folder>);
static_assert(!scrydantic::detail::has_checks_v<Gap>);
static_assert(!scrydantic::detail::has_checks_v<std::variant<Gap>>);
static_assert(!scrydantic::detail::has_checks_v<std::int32_t>);
static_assert(!scrydantic::detail::has_checks_v<std::string>);
static_assert(!scrydantic::detail::has_checks_v<scrydantic::Json>);
static_assert(!scrydantic::detail::has_checks_v<std::map<std::string, Folder>>);
static_assert(!scrydantic::detail::has_checks_v<Digits>);

// ---- Constraints -------------------------------------------------------------

TEST_CASE("validate reports a value past its range as decoding does") {
  const Status status = validate(Ranged{.v = 99});
  REQUIRE_FALSE(status);
  CHECK(status.error().issues ==
        std::vector<scrydantic::Issue>{{.kind = IssueKind::violated_constraint,
                                        .path = {"v"},
                                        .reason = "must be at most 10"}});
  CHECK(status.error().message == "$.v must be at most 10");
  CHECK(status.error().model_message == "$.v must be at most 10");

  CHECK(validate(Ranged{.v = 0}));
  CHECK(validate(Ranged{.v = 10}));
  CHECK(violations(Ranged{.v = -1}) ==
        Lines{"violated_constraint $.v must be at least 0"});
}

TEST_CASE("validate checks integer ranges at and around each bound") {
  CHECK(valid(Ages{.inclusive = 0, .exclusive = 1, .at_least = 1}));
  CHECK(valid(Ages{.inclusive = 150, .exclusive = 9, .at_least = 4000000000U}));
  CHECK(violations(Ages{.inclusive = 151, .exclusive = 0, .at_least = 0}) ==
        Lines{"violated_constraint $.inclusive must be at most 150",
              "violated_constraint $.exclusive must be greater than 0",
              "violated_constraint $.at_least must be at least 1"});
  CHECK(violations(Ages{.inclusive = -1, .exclusive = 10}) ==
        Lines{"violated_constraint $.inclusive must be at least 0",
              "violated_constraint $.exclusive must be less than 10"});
}

TEST_CASE("validate checks a floating-point value once, as the value it is") {
  CHECK(valid(Shares{.share = 1.0, .below = 2.25F}));
  CHECK(violations(Shares{.share = 0.0}) ==
        Lines{"violated_constraint $.share must be greater than 0"});
  CHECK(violations(Shares{.share = 1.5, .positive = -0.5}) ==
        Lines{"violated_constraint $.share must be at most 1",
              "violated_constraint $.positive must be at least 0"});

  // A float that is its exclusive bound. Decoding reaches it only from a number
  // that rounds onto the bound, and says so; the value itself is just on it.
  const Status on_bound = validate(Shares{.below = 2.5F});
  REQUIRE_FALSE(on_bound);
  CHECK(issue_lines(on_bound.error()) ==
        Lines{"violated_constraint $.below must be less than 2.5"});
  const auto rounded =
      scrydantic::decode<Shares>(R"({"below":2.4999999999,"positive":0,"share":0.5})");
  REQUIRE_FALSE(rounded);
  CHECK(rounded.error().model_message ==
        "$.below must be less than 2.5 even once rounded to single precision");
  CHECK(validate(Shares{.below = std::nextafter(2.5F, 0.0F)}));
}

TEST_CASE("a NaN is within no range, and an infinity within those it is inside") {
  constexpr double nan = std::numeric_limits<double>::quiet_NaN();
  constexpr double infinity = std::numeric_limits<double>::infinity();
  const Status not_a_number = validate(Shares{.share = nan, .positive = nan});
  REQUIRE_FALSE(not_a_number);
  CHECK(issue_lines(not_a_number.error()) ==
        Lines{"violated_constraint $.share must be greater than 0",
              "violated_constraint $.positive must be at least 0"});
  // An infinity above a lower bound meets it, and is encode()'s to refuse.
  CHECK(validate(Shares{.positive = infinity}));
  CHECK_FALSE(scrydantic::encode(Shares{.positive = infinity}));
  const Status below = validate(Shares{.positive = -infinity});
  REQUIRE_FALSE(below);
  CHECK(below.error().message == "$.positive must be at least 0");

  const Status steps = validate(Steps{.quarter = infinity, .step = nan});
  REQUIRE_FALSE(steps);
  CHECK(issue_lines(steps.error()) ==
        Lines{"violated_constraint $.quarter must be a multiple of 0.25",
              "violated_constraint $.step must be a multiple of 0.75"});
}

TEST_CASE("validate checks multiples exactly") {
  CHECK(valid(Steps{.five = -15, .quarter = 2.75, .step = 2.25F, .both = 18}));
  CHECK(violations(Steps{.five = 7, .quarter = 0.3, .step = 1.0F, .both = 10}) ==
        Lines{"violated_constraint $.five must be a multiple of 5",
              "violated_constraint $.quarter must be a multiple of 0.25",
              "violated_constraint $.step must be a multiple of 0.75",
              "violated_constraint $.both must be a multiple of 3"});
  // Each constraint of a member that the value misses is reported.
  CHECK(violations(Steps{.both = 22}) ==
        Lines{"violated_constraint $.both must be at most 20",
              "violated_constraint $.both must be a multiple of 3"});
}

TEST_CASE("validate counts a string's code points and leaves its pattern alone") {
  CHECK(valid(Texts{.short_name = "\xC3\xA9\xC3\xA9\xC3\xA9", .digits = "not digits"}));
  CHECK(violations(Texts{.short_name = "", .digits = "1"}) ==
        Lines{"violated_constraint $.short_name must have at least 1 character"});
  CHECK(violations(Texts{.short_name = "abcd", .digits = "1"}) ==
        Lines{"violated_constraint $.short_name must have at most 3 characters"});
}

TEST_CASE("validate counts a vector's items") {
  CHECK(valid(Lists{.pair = {1, 2}}));
  CHECK(violations(Lists{.pair = {}}) ==
        Lines{"violated_constraint $.pair must have at least 1 item"});
  CHECK(violations(Lists{.pair = {1, 2, 3}}) ==
        Lines{"violated_constraint $.pair must have at most 2 items"});
}

TEST_CASE("validate reports a renamed member under its wire name") {
  CHECK(violations(Budget{.limit = 0}) ==
        Lines{"violated_constraint $.max_tokens must be at least 1"});
}

TEST_CASE("validate checks a member that holds its default") {
  // Decoding leaves an absent member at its default without a check, since no
  // input set it. To validate(), the default is the value the member has.
  REQUIRE(scrydantic::decode<Counter>("{}"));
  CHECK(violations(Counter{}) ==
        Lines{"violated_constraint $.count must be at least 1"});
  CHECK(validate(Counter{.count = 1}));
}

// ---- Validators --------------------------------------------------------------

TEST_CASE("validate runs member validators, and tells the host and the model apart") {
  CHECK(valid(Trip{.passengers = 2}));
  const Trip trip{.from = "Rome", .to = "Paris", .passengers = 3};
  CHECK(violations(trip) ==
        Lines{"failed_check $.from must be a known city",
              "violated_constraint $.to must have at most 4 characters",
              "failed_check $.passengers must be even"});
  const Status status = validate(trip);
  REQUIRE_FALSE(status);
  CHECK(status.error().model_message == "$.from must be a known city\n"
                                        "$.to must have at most 4 characters\n"
                                        "$.passengers must be even");
  CHECK(status.error().message == "$.from names Rome, which the gazetteer lacks\n"
                                  "$.to must have at most 4 characters\n"
                                  "$.passengers must be even");
  // A disengaged optional has no value for its validator.
  CHECK(validate(Trip{.passengers = std::nullopt}));
}

TEST_CASE(
    "a member's validators run in order, after its constraints, until one fails") {
  calls = {};
  CHECK(violations(Title{.text = "a title that is too long"}) ==
        Lines{"violated_constraint $.text must have at most 16 characters"});
  // Counted twice from here on: violations() decodes the value's JSON as well.
  CHECK(calls.nonempty == 0);
  CHECK(calls.capitalized == 0);

  CHECK(violations(Title{.text = ""}) ==
        Lines{"failed_check $.text must not be empty"});
  CHECK(calls.nonempty == 2);
  CHECK(calls.capitalized == 0);

  CHECK(violations(Title{.text = "lower"}) ==
        Lines{"failed_check $.text must be capitalized"});
  CHECK(valid(Title{.text = "Upper"}));
}

TEST_CASE("a class validator runs once every member of the object is valid") {
  calls = {};
  CHECK(valid(Interval{.low = 1, .high = 2}));
  CHECK(calls.ordered == 3);

  calls = {};
  CHECK(violations(Interval{.low = 3, .high = 2}) ==
        Lines{"failed_check $ must have low at most high"});
  CHECK(calls.ordered == 2);

  // A member that misses its constraint keeps the class validator from running.
  calls = {};
  CHECK(violations(Interval{.low = -1, .high = -2}) ==
        Lines{"violated_constraint $.low must be at least 0"});
  CHECK(calls.ordered == 0);

  // A class validator is reason enough to walk to an object, wherever it is.
  CHECK(valid(Span{.from = 1, .to = 1}));
  CHECK(violations(std::vector<Span>{{.from = 1, .to = 2}, {.from = 2, .to = 1}}) ==
        Lines{"failed_check $[1] must not end before it starts"});
}

// ---- Shapes ------------------------------------------------------------------

TEST_CASE("validate walks optionals, vectors, arrays, indirects, maps, and variants") {
  CHECK(valid(nested()));

  const Nested broken{
      .row = {.age = -1},
      .maybe = Row{.age = -2},
      .rows = {{.age = 0}, {.age = -3}},
      .pair = {Row{.age = -4}, Row{.age = 0}},
      .boxed = std::indirect<Row>(Row{.age = -5}),
      .maybe_boxed = std::indirect<Row>(Row{.age = -6}),
      .by_name = {{"zed", {.age = -8}}, {"a b", {.age = -7}}, {"ok", {.age = 0}}},
      .mark = Disc{.radius = 0.0},
      .marks = {Gap{}, Label{.text = ""}, Disc{.radius = -1.0}},
  };
  CHECK(violations(broken) ==
        Lines{"violated_constraint $.row.age must be at least 0",
              "violated_constraint $.maybe.age must be at least 0",
              "violated_constraint $.rows[1].age must be at least 0",
              "violated_constraint $.pair[0].age must be at least 0",
              "violated_constraint $.boxed.age must be at least 0",
              "violated_constraint $.maybe_boxed.age must be at least 0",
              R"(violated_constraint $.by_name["a b"].age must be at least 0)",
              "violated_constraint $.by_name.zed.age must be at least 0",
              "violated_constraint $.mark.radius must be greater than 0",
              "violated_constraint $.marks[1].text must have at least 1 character",
              "violated_constraint $.marks[2].radius must be greater than 0"});
}

TEST_CASE("validate checks a root that is not an object") {
  CHECK(violations(std::vector<Row>{{.age = 0}, {.age = -1}}) ==
        Lines{"violated_constraint $[1].age must be at least 0"});
  CHECK(violations(std::optional<Row>{Row{.age = -1}}) ==
        Lines{"violated_constraint $.age must be at least 0"});
  CHECK(validate(std::optional<Row>{}));
  CHECK(violations(Mark{Label{.text = ""}}) ==
        Lines{"violated_constraint $.text must have at least 1 character"});
  CHECK(violations(std::map<std::string, Row>{{"", {.age = -1}}}) ==
        Lines{R"(violated_constraint $[""].age must be at least 0)"});
}

TEST_CASE("a member's constraints see through its optional and its indirect") {
  CHECK(valid(Wrapped{}));
  CHECK(valid(Wrapped{.maybe = 1,
                      .boxed = std::indirect<std::int32_t>(2),
                      .both = std::indirect<std::int32_t>(4),
                      .rows = std::vector<Row>{{.age = 0}}}));
  CHECK(violations(Wrapped{.maybe = 0,
                           .boxed = std::indirect<std::int32_t>(0),
                           .both = std::indirect<std::int32_t>(3),
                           .rows = std::vector<Row>{{.age = 0}, {.age = 1}}}) ==
        Lines{"violated_constraint $.maybe must be at least 1",
              "violated_constraint $.boxed must be at least 1",
              "failed_check $.both must be even",
              "violated_constraint $.rows must have at most 1 item"});
  // What a member holds is checked before the member is: an invalid row is the
  // issue, and the count of rows is not looked at.
  CHECK(violations(Wrapped{.rows = std::vector<Row>{{.age = -1}, {.age = 1}}}) ==
        Lines{"violated_constraint $.rows[0].age must be at least 0"});
}

TEST_CASE(
    "validate visits a map's entries in key order, whatever order it iterates in") {
  Ledgers ledgers{};
  for (const std::string_view key : {"b", "a", "d", "c"}) {
    ledgers.descending.emplace(key, Row{.age = -1});
    ledgers.unordered.emplace(key, Row{.age = -1});
  }
  CHECK(violations(ledgers) ==
        Lines{"violated_constraint $.descending.a.age must be at least 0",
              "violated_constraint $.descending.b.age must be at least 0",
              "violated_constraint $.descending.c.age must be at least 0",
              "violated_constraint $.descending.d.age must be at least 0",
              "violated_constraint $.unordered.a.age must be at least 0",
              "violated_constraint $.unordered.b.age must be at least 0",
              "violated_constraint $.unordered.c.age must be at least 0",
              "violated_constraint $.unordered.d.age must be at least 0"});
}

TEST_CASE("a flattened member's fields are checked at the enclosing object's path") {
  calls = {};
  CHECK(valid(Listing{.window = {.offset = 20, .size = 10}}));
  CHECK(calls.window == 3);

  CHECK(violations(Listing{.query = "", .window = {.offset = -1, .size = 0}}) ==
        Lines{"violated_constraint $.query must have at least 1 character",
              "violated_constraint $.offset must be at least 0",
              "violated_constraint $.page_size must be at least 1"});

  // The flattened class's validator takes the part of the object it is, once
  // its members are valid, and whatever the enclosing object's other members are.
  calls = {};
  CHECK(violations(Listing{.query = "", .window = {.offset = 5, .size = 10}}) ==
        Lines{"violated_constraint $.query must have at least 1 character",
              "failed_check $ must start on a page boundary"});
  CHECK(calls.window == 2);

  CHECK(violations(Search{.listings = {Listing{}, Listing{.window = {.size = 101}}}}) ==
        Lines{"violated_constraint $.listings[1].page_size must be at most 100"});
  CHECK(violations(Paged{.query = "q", .window = {.offset = 1, .size = 2}}) ==
        Lines{"failed_check $ must start on a page boundary"});
}

TEST_CASE("validate checks every level of a recursive value") {
  const Tree tree{
      .weight = 1,
      .children = {{.weight = -1, .children = {{.weight = -2, .children = {}}}},
                   {.weight = 0, .children = {{.weight = -3, .children = {}}}}}};
  // A member's issue comes before those inside the members after it.
  CHECK(
      violations(tree) ==
      Lines{"violated_constraint $.children[0].weight must be at least 0",
            "violated_constraint $.children[0].children[0].weight must be at least 0",
            "violated_constraint $.children[1].children[0].weight must be at least 0"});
}

// ---- Limits ------------------------------------------------------------------

TEST_CASE("validate stops past max_issues, as decoding does") {
  Many many{};
  many.rows.assign(scrydantic::max_issues + 1, Row{.age = -1});
  const Status status = validate(many);
  REQUIRE_FALSE(status);
  CHECK(status.error().issues.size() == scrydantic::max_issues);
  CHECK(
      status.error().issues.back().path ==
      std::vector<scrydantic::PathSegment>{"rows", scrydantic::max_issues - 1, "age"});
  CHECK(status.error().message.ends_with(
      "$.rows[63].age must be at least 0\n"
      "further issues past the first 64 were not reported"));
  CHECK(violations(many).size() == scrydantic::max_issues);

  // Exactly max_issues are all reported, and nothing says that more were not.
  many.rows.pop_back();
  CHECK(violations(many).size() == scrydantic::max_issues);
  CHECK(validate(many).error().message.ends_with("$.rows[63].age must be at least 0"));
}

TEST_CASE("validate stops walking once its report is full") {
  // The report fills in the vector, in the map, in the unordered map, or at
  // the last member. The issue past the last one it keeps is what stops the
  // walk, and nothing after that one is visited.
  for (const std::size_t listed : {200UZ, 60UZ, 50UZ, 44UZ}) {
    Crowd crowd{};
    crowd.listed.assign(listed, Noisy{});
    for (std::size_t made = 0; made < 10; ++made) {
      crowd.sorted.emplace(std::to_string(made), Noisy{});
      crowd.hashed.emplace(std::to_string(made), Noisy{});
    }
    calls = {};
    const Status status = validate(crowd);
    REQUIRE_FALSE(status);
    CHECK(status.error().issues.size() == scrydantic::max_issues);
    CHECK(calls.never == scrydantic::max_issues + 1);
  }

  // One issue fewer is every issue, and every value is visited to find them.
  Crowd crowd{};
  crowd.listed.assign(scrydantic::max_issues - 1, Noisy{});
  calls = {};
  const Status status = validate(crowd);
  REQUIRE_FALSE(status);
  CHECK(calls.never == scrydantic::max_issues);
  CHECK(status.error().message.ends_with("$.last.value is never valid"));
}

TEST_CASE("validate walks no deeper than JSON nests") {
  // max_json_depth objects nest in JSON, and the innermost is checked.
  const Link deepest = chain(scrydantic::max_json_depth, -1);
  REQUIRE(scrydantic::encode(deepest));
  const Status status = validate(deepest);
  REQUIRE_FALSE(status);
  REQUIRE(status.error().issues.size() == 1);
  CHECK(status.error().issues[0].path.size() == scrydantic::max_json_depth);
  CHECK(status.error().issues[0].reason == "must be at least 0");

  // One more is in no JSON: encode() refuses the value, and validate() leaves
  // it to, so that a value nested without bound does not recurse without one.
  const Link too_deep = chain(scrydantic::max_json_depth + 1, -1);
  CHECK(validate(too_deep));
  const auto encoded = scrydantic::encode(too_deep);
  REQUIRE_FALSE(encoded);
  CHECK(encoded.error().message.ends_with(
      "is an array or object nested more than 256 deep, so the value cannot be JSON"));

  // A flattened member is no level of its own: max_json_depth steps nest too.
  Step step{.weight = -1, .hop = {.next = std::nullopt}};
  for (std::size_t made = 1; made < scrydantic::max_json_depth; ++made) {
    step = Step{.weight = 0, .hop = {.next = std::indirect<Step>(std::move(step))}};
  }
  REQUIRE(scrydantic::encode(step));
  CHECK(violations(step).size() == 1);
}

// ---- What is left to encode() ------------------------------------------------

TEST_CASE("validate leaves what encode() refuses to encode()") {
  constexpr auto undeclared = static_cast<Color>(7);
  const Refused refused{.color = undeclared,
                        .text = "\xC3",
                        .ratio = std::numeric_limits<double>::quiet_NaN(),
                        .count = 1};
  CHECK(validate(refused));
  CHECK_FALSE(scrydantic::encode(refused));

  // The constraints of such a value are still checked.
  const Status status = validate(Refused{.color = undeclared, .count = -1});
  REQUIRE_FALSE(status);
  CHECK(status.error().message == "$.count must be at least 0");
}

TEST_CASE("validate passes over an indirect and a variant that hold no value") {
  Wrapped wrapped{.both = std::indirect<std::int32_t>(3)};
  const auto taken = std::move(wrapped.boxed);
  const auto also_taken = std::move(*wrapped.both);
  REQUIRE(wrapped.boxed.valueless_after_move());
  REQUIRE(wrapped.both->valueless_after_move());
  CHECK(validate(wrapped));
  CHECK_FALSE(scrydantic::encode(wrapped));
  CHECK(*taken == 1);
  CHECK(*also_taken == 3);

  Nested holder = nested();
  holder.boxed = std::indirect<Row>(Row{.age = -1});
  REQUIRE_FALSE(validate(holder));
  const auto row = std::move(holder.boxed);
  CHECK(validate(holder));
  CHECK(row->age == -1);

  struct ThrowingText {
    // NOLINTNEXTLINE(google-explicit-constructor): the conversion is the point.
    operator std::string() const { throw 1; }
  };
  Mark mark{Label{.text = ""}};
  REQUIRE_FALSE(validate(mark));
  try {
    mark.emplace<Label>(ThrowingText{});
  } catch (const int) {
  }
  REQUIRE(mark.valueless_by_exception());
  CHECK(validate(mark));
  CHECK_FALSE(scrydantic::encode(mark));
}

// ---- Types -------------------------------------------------------------------

TEST_CASE("validate takes what encode() takes") {
  // A value that decoding could not fill, since it only reads.
  const Status status = validate(Borrowed{.count = 0, .label = "borrowed"});
  REQUIRE_FALSE(status);
  CHECK(issue_lines(status.error()) ==
        Lines{"violated_constraint $.count must be at least 1"});
  CHECK(validate(Borrowed{.count = 1, .label = "borrowed"}));
}

TEST_CASE("a type with no constraint and no validator is always valid") {
  CHECK(validate(Plain{}));
  CHECK(
      validate(Plain{.count = -1, .label = "", .samples = {{{"x", -1.0}}}, .gap = {}}));
  CHECK(validate(
      Folder{.name = "/", .folders = {Folder{.name = "home", .folders = {}}}}));
  CHECK(validate(std::int32_t{-1}));
  CHECK(validate(std::string{"\xC3"}));
  CHECK(validate(scrydantic::Json{.text = "not JSON"}));

  // The one check in a type is found however deep it is.
  Deep deep{};
  deep.layers.emplace_back(std::in_place);
  deep.layers.back()->emplace("gap", Gap{.width = -1});
  CHECK(valid(deep));
  deep.layers.back()->emplace("label", Label{.text = ""});
  CHECK(
      violations(deep) ==
      Lines{
          "violated_constraint $.layers[0].label.text must have at least 1 character"});
}

namespace kind_union {

struct Step {
  [[= scrydantic::range{.min = 1, .max = 9}]] int size{1};
};

struct Plan {
  std::variant<std::monostate, Step, std::vector<Step>, std::string> steps;
};

} // namespace kind_union

TEST_CASE("validate walks whichever alternative a variant told apart by kind holds") {
  using kind_union::Plan;
  using kind_union::Step;

  CHECK(scrydantic::validate(Plan{}));
  CHECK(scrydantic::validate(Plan{.steps = std::string{"none"}}));
  CHECK(scrydantic::validate(Plan{.steps = Step{.size = 9}}));

  const auto one = scrydantic::validate(Plan{.steps = Step{.size = 10}});
  REQUIRE_FALSE(one);
  CHECK(one.error().message == "$.steps.size must be at most 9");

  const auto many = scrydantic::validate(
      Plan{.steps = std::vector{Step{.size = 2}, Step{.size = 0}}});
  REQUIRE_FALSE(many);
  CHECK(many.error().message == "$.steps[1].size must be at least 1");
}
