// Default arguments on the parameters of a bound function. A parameter with one
// is an optional member of the argument object, and a call whose input leaves
// it out leaves the argument out too, so the function's own default applies.

#include <catch2/catch_test_macros.hpp>
#include <concepts>
#include <cstdint>
#include <memory>
#include <optional>
#include <scrydantic/scrydantic.hpp>
#include <string>
#include <string_view>
#include <utility>

namespace {

// A host's call context. Scrydantic only passes it through.
struct Session {
  std::string user{};
};

struct Margin {
  std::int32_t width{2};
};

} // namespace

namespace sums {

[[= scrydantic::tool{"Add"}]] inline int add(int a, int b = 3) { return a + b; }

// Several trailing defaults: a call may end after any of them.
[[= scrydantic::tool{"Join words"}]] inline std::string join(std::string first,
                                                             std::string second = "b",
                                                             std::string third = "c",
                                                             std::string fourth = "d") {
  return first + second + third + fourth;
}

// Every parameter has a default, so the input may be `{}`.
[[= scrydantic::tool{"Count from a start"}]] inline std::int32_t
    count(std::int32_t start = 10, std::int32_t step = 1) {
  return start + step;
}

// A default that is not null, on an optional: `null` and a missing member are
// different arguments.
[[= scrydantic::tool{"Limit a listing"}]] inline std::string
    limited(std::string query, std::optional<std::int32_t> limit = 20) {
  return query + ":" + (limit ? std::to_string(*limit) : "all");
}

// The usual optional parameter: it may be left out, and it may be null.
[[= scrydantic::tool{"Tag a listing"}]] inline std::string
    tagged(std::string query, std::optional<std::string> tag = std::nullopt) {
  return query + ":" + tag.value_or("-");
}

// Without a default, an optional parameter must be there, though it may be null.
[[= scrydantic::tool{"Tag a listing, always"}]] inline std::string
    always_tagged(std::string query, std::optional<std::string> tag) {
  return query + ":" + tag.value_or("-");
}

// A defaulted parameter is annotated as any other, and is taken as any other:
// by const reference, by rvalue reference, or as an aggregate.
[[= scrydantic::tool{"Pad a line"}]] inline std::string
    pad(const std::string& line,
        [[= scrydantic::description{"What to pad with"}]]
            [[= scrydantic::length{.min = 1, .max = 1}]] const std::string& fill = "*",
        [[= scrydantic::name{"margin"}]] Margin&& edge = Margin{},
        [[= scrydantic::range{.min = 0, .max = 4}]] std::int32_t repeat = 1) {
  std::string padding{};
  for (std::int32_t index = 0; index < edge.width * repeat; ++index) {
    padding += fill;
  }
  return padding + line + padding;
}

// The values after a context are defaulted as any others.
[[= scrydantic::tool{"Greet the caller"}]] inline std::string
    greet(const Session& session, std::string greeting = "hello",
          std::int32_t times = 1) {
  std::string text{};
  for (std::int32_t index = 0; index < times; ++index) {
    text += greeting + " " + session.user + ";";
  }
  return text;
}

// The default is on a later declaration than the one that makes it a tool.
[[= scrydantic::tool{"Scale a value"}]] std::int32_t scale(std::int32_t value,
                                                           std::int32_t factor);
std::int32_t scale(std::int32_t value, std::int32_t factor = 2);
inline std::int32_t scale(std::int32_t value, std::int32_t factor) {
  return value * factor;
}

// A lone aggregate parameter is the argument object, which every call is
// given, so its default argument is never used.
[[= scrydantic::tool{"Measure a margin"}]] inline std::int32_t
    measure(Margin margin = Margin{.width = 7}) {
  return margin.width;
}

} // namespace sums

// A function that is no tool, bound by hand with arguments_t and apply().
namespace ledger {

inline std::int32_t total(std::int32_t amount, std::int32_t tax = 5,
                          std::optional<std::int32_t> tip = std::nullopt) {
  return amount + tax + tip.value_or(0);
}

inline std::int32_t balance = 0;

// What a function returns is its caller's, whichever call is made.
inline std::int32_t& deposit(std::int32_t amount = 1) { return balance += amount; }

} // namespace ledger

namespace {

class Counter {
public:
  [[= scrydantic::tool{"Advance the counter"}]] std::int32_t
      advance(std::int32_t by = 1) {
    return value_ += by;
  }

  [[= scrydantic::tool{"Read the counter"}]] std::string
      read(this const Counter& self, std::string unit, bool spaced = false) {
    return std::to_string(self.value_) + (spaced ? " " : "") + unit;
  }

private:
  std::int32_t value_{};
};

[[nodiscard]] scrydantic::Json json(const std::string_view text) {
  return scrydantic::Json{.text = std::string{text}};
}

[[nodiscard]] std::string
model_message(const scrydantic::Result<scrydantic::Json>& result) {
  REQUIRE_FALSE(result.has_value());
  return result.error().model_message;
}

constexpr std::string_view int32_schema =
    R"({"maximum":2147483647,"minimum":-2147483648,"type":"integer"})";

} // namespace

TEST_CASE("a parameter with a default argument is optional and keeps its default") {
  const auto tools = scrydantic::bind<^^sums::add>();
  REQUIRE(tools.size() == 1);
  CHECK(tools[0].input_schema.text ==
        R"({"additionalProperties":false,"properties":{"a":)" +
            std::string{int32_schema} + R"(,"b":)" + std::string{int32_schema} +
            R"(},"required":["a"],"type":"object"})");
  CHECK(tools[0].invoke(json(R"({"a":2})"))->text == "5");
  CHECK(tools[0].invoke(json(R"({"a":2,"b":5})"))->text == "7");

  // The member is the parameter's type: it may be left out, but not be null.
  CHECK(model_message(tools[0].invoke(json(R"({"a":2,"b":null})"))) ==
        "$.b must be an integer");
  CHECK(model_message(tools[0].invoke(json(R"({"b":5})"))) ==
        "$.a is a required member");
}

TEST_CASE("a call ends after any of several trailing defaults") {
  const auto tools = scrydantic::bind<^^sums::join>();
  REQUIRE(tools.size() == 1);
  const auto& join = tools[0].invoke;
  CHECK(tools[0].input_schema.text ==
        R"({"additionalProperties":false,"properties":{)"
        R"("first":{"type":"string"},"fourth":{"type":"string"},)"
        R"("second":{"type":"string"},"third":{"type":"string"}},)"
        R"("required":["first"],"type":"object"})");
  CHECK(join(json(R"({"first":"1"})"))->text == R"("1bcd")");
  CHECK(join(json(R"({"first":"1","second":"2"})"))->text == R"("12cd")");
  CHECK(join(json(R"({"first":"1","second":"2","third":"3"})"))->text == R"("123d")");
  CHECK(join(json(R"({"first":"1","second":"2","third":"3","fourth":"4"})"))->text ==
        R"("1234")");
}

TEST_CASE("a later default cannot be given without the ones before it") {
  const auto tools = scrydantic::bind<^^sums::join>();
  const auto& join = tools[0].invoke;

  const auto skipped = join(json(R"({"first":"1","third":"3"})"));
  CHECK(model_message(skipped) == "$.second is required when \"third\" is given");
  REQUIRE(skipped.error().issues.size() == 1);
  CHECK(skipped.error().issues[0].kind == scrydantic::IssueKind::missing_member);
  CHECK(scrydantic::json_path(skipped.error().issues[0].path) == "$.second");

  // Each member left out before one that is given is its own issue, in
  // declaration order, among the other issues of the input.
  CHECK(model_message(join(json(R"({"first":1,"fourth":"4"})"))) ==
        "$.first must be a string\n"
        "$.second is required when \"fourth\" is given\n"
        "$.third is required when \"fourth\" is given");
  CHECK(model_message(join(json(R"({"first":"1","second":"2","fourth":"4"})"))) ==
        "$.third is required when \"fourth\" is given");
}

TEST_CASE("a function whose every parameter has a default takes an empty object") {
  const auto tools = scrydantic::bind<^^sums::count>();
  REQUIRE(tools.size() == 1);
  CHECK(tools[0].input_schema.text ==
        R"({"additionalProperties":false,"properties":{"start":)" +
            std::string{int32_schema} + R"(,"step":)" + std::string{int32_schema} +
            R"(},"required":[],"type":"object"})");
  CHECK(tools[0].invoke(json("{}"))->text == "11");
  CHECK(tools[0].invoke(json(R"({"start":1})"))->text == "2");
  CHECK(tools[0].invoke(json(R"({"start":1,"step":4})"))->text == "5");
  CHECK(model_message(tools[0].invoke(json(R"({"step":4})"))) ==
        "$.start is required when \"step\" is given");
}

TEST_CASE("a defaulted optional may be left out and may be null") {
  const auto tagged = scrydantic::bind<^^sums::tagged>();
  REQUIRE(tagged.size() == 1);
  CHECK(tagged[0].input_schema.text ==
        R"({"additionalProperties":false,"properties":{"query":{"type":"string"},)"
        R"("tag":{"anyOf":[{"type":"string"},{"type":"null"}]}},)"
        R"("required":["query"],"type":"object"})");
  CHECK(tagged[0].invoke(json(R"({"query":"q"})"))->text == R"("q:-")");
  CHECK(tagged[0].invoke(json(R"({"query":"q","tag":null})"))->text == R"("q:-")");
  CHECK(tagged[0].invoke(json(R"({"query":"q","tag":"new"})"))->text == R"("q:new")");

  // Left out, the argument is the function's default; null, it is no value.
  const auto limited = scrydantic::bind<^^sums::limited>();
  CHECK(limited[0].invoke(json(R"({"query":"q"})"))->text == R"("q:20")");
  CHECK(limited[0].invoke(json(R"({"query":"q","limit":null})"))->text == R"("q:all")");
  CHECK(limited[0].invoke(json(R"({"query":"q","limit":3})"))->text == R"("q:3")");

  // Without a default argument the member is required, and may still be null.
  const auto always = scrydantic::bind<^^sums::always_tagged>();
  CHECK(always[0].input_schema.text ==
        R"({"additionalProperties":false,"properties":{"query":{"type":"string"},)"
        R"("tag":{"anyOf":[{"type":"string"},{"type":"null"}]}},)"
        R"("required":["query","tag"],"type":"object"})");
  CHECK(model_message(always[0].invoke(json(R"({"query":"q"})"))) ==
        "$.tag is a required member");
  CHECK(always[0].invoke(json(R"({"query":"q","tag":null})"))->text == R"("q:-")");
}

TEST_CASE("a defaulted parameter keeps its annotations and its passing convention") {
  const auto tools = scrydantic::bind<^^sums::pad>();
  REQUIRE(tools.size() == 1);
  const auto& pad = tools[0].invoke;
  CHECK(
      tools[0].input_schema.text ==
      R"({"additionalProperties":false,"properties":{)"
      R"("fill":{"description":"What to pad with","maxLength":1,"minLength":1,"type":"string"},)"
      R"("line":{"type":"string"},)"
      R"("margin":{"additionalProperties":false,"properties":{"width":{"default":2,"maximum":2147483647,"minimum":-2147483648,"type":"integer"}},"required":[],"type":"object"},)"
      R"("repeat":{"maximum":4,"minimum":0,"type":"integer"}},)"
      R"("required":["line"],"type":"object"})");
  CHECK(pad(json(R"({"line":"x"})"))->text == R"("**x**")");
  CHECK(pad(json(R"({"line":"x","fill":"-"})"))->text == R"("--x--")");
  CHECK(pad(json(R"({"line":"x","fill":"-","margin":{"width":1}})"))->text ==
        R"("-x-")");
  CHECK(pad(json(R"({"line":"x","fill":"-","margin":{},"repeat":2})"))->text ==
        R"("----x----")");

  // A value that is given is held to the parameter's constraints.
  CHECK(
      model_message(pad(json(R"({"line":"x","fill":"--","margin":{},"repeat":9})"))) ==
      "$.fill must have at most 1 character\n$.repeat must be at most 4");
  CHECK(model_message(pad(json(R"({"line":"x","fill":"-","margin":null})"))) ==
        "$.margin must be an object");
}

TEST_CASE("values after a context parameter keep their defaults") {
  const auto tools = scrydantic::bind<^^sums::greet, Session>();
  REQUIRE(tools.size() == 1);
  CHECK(tools[0].input_schema.text ==
        R"({"additionalProperties":false,"properties":{"greeting":{"type":"string"},)"
        R"("times":)" +
            std::string{int32_schema} + R"(},"required":[],"type":"object"})");
  const Session session{.user = "ada"};
  CHECK(tools[0].invoke(session, json("{}"))->text == R"("hello ada;")");
  CHECK(tools[0].invoke(session, json(R"({"greeting":"hi"})"))->text == R"("hi ada;")");
  CHECK(tools[0].invoke(session, json(R"({"greeting":"hi","times":2})"))->text ==
        R"("hi ada;hi ada;")");
  CHECK(model_message(tools[0].invoke(session, json(R"({"times":2})"))) ==
        "$.greeting is required when \"times\" is given");
}

TEST_CASE("a toolbox member function keeps its default arguments") {
  const auto tools = scrydantic::bind(std::make_shared<Counter>());
  REQUIRE(tools.size() == 2);
  CHECK(tools[0].input_schema.text ==
        R"({"additionalProperties":false,"properties":{"by":)" +
            std::string{int32_schema} + R"(},"required":[],"type":"object"})");
  CHECK(tools[0].invoke(json("{}"))->text == "1");
  CHECK(tools[0].invoke(json(R"({"by":4})"))->text == "5");

  // After an explicit object parameter too.
  CHECK(tools[1].input_schema.text ==
        R"({"additionalProperties":false,"properties":{"spaced":{"type":"boolean"},)"
        R"("unit":{"type":"string"}},"required":["unit"],"type":"object"})");
  CHECK(tools[1].invoke(json(R"({"unit":"m"})"))->text == R"("5m")");
  CHECK(tools[1].invoke(json(R"({"unit":"m","spaced":true})"))->text == R"("5 m")");
}

TEST_CASE("a default argument on any declaration before the binding counts") {
  const auto tools = scrydantic::bind<^^sums::scale>();
  REQUIRE(tools.size() == 1);
  CHECK(tools[0].invoke(json(R"({"value":4})"))->text == "8");
  CHECK(tools[0].invoke(json(R"({"value":4,"factor":3})"))->text == "12");
}

TEST_CASE("a lone aggregate parameter is the argument object, default or not") {
  const auto tools = scrydantic::bind<^^sums::measure>();
  REQUIRE(tools.size() == 1);
  CHECK(tools[0].input_schema.text == scrydantic::input_schema_v<Margin>);
  // The object is always given, here as `{}`, whose member has its own default.
  CHECK(tools[0].invoke(json("{}"))->text == "2");
  CHECK(tools[0].invoke(json(R"({"width":4})"))->text == "4");
}

TEST_CASE("describe() agrees with bind() about default arguments") {
  static constexpr auto described = scrydantic::describe<^^sums>();
  const auto bound = scrydantic::bind<^^sums>();
  REQUIRE(described.size() == bound.size());
  for (std::size_t index = 0; index < bound.size(); ++index) {
    CHECK(described[index].name == bound[index].name);
    CHECK(described[index].input_schema == bound[index].input_schema.text);
  }

  static constexpr auto counter = scrydantic::describe<Counter>();
  const auto tools = scrydantic::bind(std::make_shared<Counter>());
  REQUIRE(counter.size() == tools.size());
  CHECK(counter[0].input_schema == tools[0].input_schema.text);
  CHECK(counter[1].input_schema == tools[1].input_schema.text);
}

TEST_CASE("the argument object holds a defaulted argument as an optional") {
  using Arguments = scrydantic::arguments_t<^^ledger::total>;
  static_assert(std::same_as<decltype(Arguments::amount), std::int32_t>);
  static_assert(std::same_as<decltype(Arguments::tax), std::optional<std::int32_t>>);
  static_assert(std::same_as<decltype(Arguments::tip),
                             std::optional<std::optional<std::int32_t>>>);
  static constexpr std::string_view schema = scrydantic::input_schema_v<Arguments>;
  CHECK(schema == R"({"additionalProperties":false,"properties":{"amount":)" +
                      std::string{int32_schema} + R"(,"tax":)" +
                      std::string{int32_schema} + R"(,"tip":{"anyOf":[)" +
                      std::string{int32_schema} +
                      R"(,{"type":"null"}]}},"required":["amount"],"type":"object"})");

  // Decoding engages exactly the members the input gives.
  auto none = scrydantic::decode<Arguments>(R"({"amount":100})");
  REQUIRE(none.has_value());
  CHECK_FALSE(none->tax.has_value());
  CHECK_FALSE(none->tip.has_value());
  CHECK(scrydantic::apply<^^ledger::total>(std::move(*none)) == 105);

  auto null_tip = scrydantic::decode<Arguments>(R"({"amount":100,"tax":0,"tip":null})");
  REQUIRE(null_tip.has_value());
  CHECK(null_tip->tax == 0);
  REQUIRE(null_tip->tip.has_value());
  CHECK_FALSE(null_tip->tip->has_value());
  // Encoding writes what was given, and leaves out what was left out.
  CHECK(scrydantic::encode(*null_tip)->text == R"({"amount":100,"tax":0,"tip":null})");
  CHECK(scrydantic::apply<^^ledger::total>(std::move(*null_tip)) == 100);

  // An object built by hand leaves an argument out by leaving it disengaged.
  Arguments by_hand{};
  by_hand.amount = 1;
  CHECK(scrydantic::encode(by_hand)->text == R"({"amount":1})");
  by_hand.tax = 2;
  CHECK(scrydantic::encode(by_hand)->text == R"({"amount":1,"tax":2})");
  CHECK(scrydantic::apply<^^ledger::total>(Arguments{by_hand}) == 3);
  by_hand.tip = 3;
  CHECK(scrydantic::apply<^^ledger::total>(Arguments{by_hand}) == 6);

  const auto skipped = scrydantic::decode<Arguments>(R"({"amount":100,"tip":7})");
  REQUIRE_FALSE(skipped.has_value());
  CHECK(skipped.error().model_message == "$.tax is required when \"tip\" is given");
}

TEST_CASE("apply() returns what the function returns, with or without a default") {
  using Arguments = scrydantic::arguments_t<^^ledger::deposit>;
  ledger::balance = 0;
  static_assert(
      std::same_as<decltype(scrydantic::apply<^^ledger::deposit>(Arguments{})),
                   std::int32_t&>);
  CHECK(&scrydantic::apply<^^ledger::deposit>(Arguments{}) == &ledger::balance);
  CHECK(ledger::balance == 1);
  CHECK(scrydantic::apply<^^ledger::deposit>(Arguments{.amount = 4}) == 5);
  CHECK(ledger::balance == 5);
}
