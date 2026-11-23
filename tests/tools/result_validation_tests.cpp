// Tool results: a handler checks what its tool returns against the constraints
// and validators of the result's type, which its output schema publishes, before
// it encodes it. A result that fails is the tool's own fault, reported to the
// host alone. Every kind of handler checks, and a result type with nothing to
// check has no check.

#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <map>
#include <memory>
#include <scrydantic/detail/constraints.hpp>
#include <scrydantic/scrydantic.hpp>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// The issue's example.
namespace ranged_tools {

struct Ranged {
  [[= scrydantic::range{.min = 0, .max = 10}]] int v;
};

[[= scrydantic::tool{"Out of range"}]] inline Ranged bad() { return Ranged{.v = 99}; }

} // namespace ranged_tools

namespace checked_results {

using ranged_tools::Ranged;

inline scrydantic::Status is_known_city(const std::string& city) {
  if (city == "Oslo" || city == "Lima") {
    return {};
  }
  return std::unexpected(
      scrydantic::rejection("must be a known city", "has no row in cities"));
}

struct Booking;
scrydantic::Status has_room(const Booking& booking);

struct[[= scrydantic::check<&has_room>]] Booking {
  [[= scrydantic::check<&is_known_city>]] std::string city;
  [[= scrydantic::range{.min = 1, .max = 8}]] std::int32_t guests{};
  [[= scrydantic::length{.max = 5}]] std::string note{};
};

inline scrydantic::Status has_room(const Booking& booking) {
  if (booking.city == "Lima" && booking.guests > 4) {
    return std::unexpected(scrydantic::Error{.message = "hotel 17 is full"});
  }
  return {};
}

struct Query {
  std::string city;
  std::int32_t guests{};
};

[[= scrydantic::tool{"Echo a value"}]] inline Ranged echo(int v) {
  return Ranged{.v = v};
}

[[= scrydantic::tool{"Book a stay"}]] inline Booking book(Query query) {
  return Booking{.city = std::move(query.city), .guests = query.guests, .note = ""};
}

// A Result's value is checked as a plain return is; its error is the tool's own.
[[= scrydantic::tool{"Find a stay"}]] inline scrydantic::Result<Booking>
    find(std::string city, std::int32_t guests) {
  if (city.empty()) {
    return std::unexpected(scrydantic::tool_error("name a city"));
  }
  return Booking{.city = std::move(city), .guests = guests, .note = "found it"};
}

[[= scrydantic::tool{"List some values"}]] inline std::vector<Ranged>
    list(std::vector<int> values) {
  std::vector<Ranged> listed{};
  for (const int value : values) {
    listed.push_back(Ranged{.v = value});
  }
  return listed;
}

[[= scrydantic::tool{"File a value under a key"}]] inline std::map<std::string, Ranged>
    keyed(std::string key, int v) {
  return {{std::move(key), Ranged{.v = v}}};
}

// What neither a constraint nor encoding allows: the constraint is found first.
struct Reading {
  [[= scrydantic::range{.min = 0.0}]] double level{};
  double drift{};
};

[[= scrydantic::tool{"Take a reading"}]] inline Reading measure(double level) {
  return Reading{.level = level, .drift = std::numeric_limits<double>::quiet_NaN()};
}

class Desk {
public:
  [[= scrydantic::tool{"Count the stays booked"}]] Ranged booked() {
    return Ranged{.v = ++count_};
  }

private:
  int count_{9};
};

} // namespace checked_results

namespace caller_results {

struct Caller {
  std::string user{};
};

[[= scrydantic::tool{"Echo a value to its caller"}]] inline ranged_tools::Ranged
    echo_to(const Caller& caller, int v) {
  return ranged_tools::Ranged{.v = caller.user == "ada" ? v : -1};
}

} // namespace caller_results

namespace unchecked_results {

// No constraint and no validator anywhere in it.
struct Point {
  std::int32_t x{};
  std::int32_t y{};
};

[[= scrydantic::tool{"Mirror a point"}]] inline Point mirror(Point point) {
  return Point{.x = -point.x, .y = -point.y};
}

// A pattern is the schema's alone, here as everywhere.
struct Code {
  [[= scrydantic::pattern{"^[A-Z]+$"}]] std::string text;
};

[[= scrydantic::tool{"Make a code"}]] inline Code code() { return Code{.text = "x1"}; }

} // namespace unchecked_results

namespace {

using ranged_tools::Ranged;
using scrydantic::Json;

[[nodiscard]] Json json(const std::string_view text) {
  return Json{.text = std::string{text}};
}

// A result that failed its own type: the host's text and nothing else.
void check_invalid(const scrydantic::Result<Json>& result,
                   const std::string_view message) {
  REQUIRE_FALSE(result);
  CHECK(result.error().message == message);
  CHECK(result.error().model_message.empty());
  CHECK(result.error().issues.empty());
}

[[nodiscard]] const scrydantic::BoundTool<>&
named(const std::vector<scrydantic::BoundTool<>>& tools, const std::string_view name) {
  for (const auto& tool : tools) {
    if (tool.name == name) {
      return tool;
    }
  }
  FAIL("no tool is named " << name);
  return tools.front();
}

} // namespace

static_assert(scrydantic::detail::has_checks_v<Ranged>);
static_assert(scrydantic::detail::has_checks_v<checked_results::Booking>);
static_assert(!scrydantic::detail::has_checks_v<unchecked_results::Point>);
static_assert(!scrydantic::detail::has_checks_v<unchecked_results::Code>);

TEST_CASE("a tool whose result misses its constraint fails on the host's side") {
  auto tools = scrydantic::bind<^^ranged_tools>();
  REQUIRE(tools.size() == 1);
  // The schema that the result is held to is the one the tool publishes.
  REQUIRE(tools[0].output_schema);
  CHECK(tools[0].output_schema->text ==
        R"({"additionalProperties":false,"properties":{)"
        R"("v":{"maximum":10,"minimum":0,"type":"integer"}},)"
        R"("required":["v"],"type":"object"})");
  check_invalid(tools[0].invoke(json("{}")), "result $.v must be at most 10");
}

TEST_CASE("a tool whose result meets its constraints answers as before") {
  const auto tools = scrydantic::bind<^^checked_results>();
  const auto echoed = named(tools, "echo").invoke(json(R"({"v":10})"));
  REQUIRE(echoed);
  CHECK(echoed->text == R"({"v":10})");
  check_invalid(named(tools, "echo").invoke(json(R"({"v":11})")),
                "result $.v must be at most 10");
  check_invalid(named(tools, "echo").invoke(json(R"({"v":-1})")),
                "result $.v must be at least 0");

  const auto booked =
      named(tools, "book").invoke(json(R"({"city":"Oslo","guests":2})"));
  REQUIRE(booked);
  CHECK(booked->text == R"({"city":"Oslo","guests":2,"note":""})");
}

TEST_CASE("a result's validators run, and the host is given their own text") {
  const auto tools = scrydantic::bind<^^checked_results>();
  const auto& book = named(tools, "book");
  // One line for each issue, each with the reason the host is given: a
  // validator's `message` where it has one.
  check_invalid(book.invoke(json(R"({"city":"Rome","guests":9})")),
                "result $.city has no row in cities\n"
                "result $.guests must be at most 8");
  // The class's validator runs once the members are valid, as in a decode.
  check_invalid(book.invoke(json(R"({"city":"Lima","guests":5})")),
                "result $ hotel 17 is full");
}

TEST_CASE("the value of a Result is checked, and its error is passed on") {
  const auto tools = scrydantic::bind<^^checked_results>();
  const auto& find = named(tools, "find");
  check_invalid(find.invoke(json(R"({"city":"Oslo","guests":2})")),
                "result $.note must have at most 5 characters");

  const auto refused = find.invoke(json(R"({"city":"","guests":2})"));
  REQUIRE_FALSE(refused);
  CHECK(refused.error().model_message == "name a city");
  CHECK(refused.error().message == "name a city");
}

TEST_CASE("a result is checked wherever its type holds a constraint") {
  const auto tools = scrydantic::bind<^^checked_results>();
  const auto listed = named(tools, "list").invoke(json(R"({"values":[1,2]})"));
  REQUIRE(listed);
  CHECK(listed->text == R"([{"v":1},{"v":2}])");
  check_invalid(named(tools, "list").invoke(json(R"({"values":[1,20,3,-4]})")),
                "result $[1].v must be at most 10\n"
                "result $[3].v must be at least 0");
  check_invalid(named(tools, "keyed").invoke(json(R"({"key":"a b","v":11})")),
                R"(result $["a b"].v must be at most 10)");
}

TEST_CASE("a result past max_issues says that the rest were not reported") {
  std::string values{"["};
  for (std::size_t made = 0; made <= scrydantic::max_issues; ++made) {
    values.append(made == 0 ? "-1" : ",-1");
  }
  values.push_back(']');
  const auto tools = scrydantic::bind<^^checked_results>();
  const auto listed = named(tools, "list").invoke(json(R"({"values":)" + values + "}"));
  REQUIRE_FALSE(listed);
  CHECK(listed.error().message.starts_with("result $[0].v must be at least 0\n"
                                           "result $[1].v must be at least 0\n"));
  CHECK(listed.error().message.ends_with(
      "result $[63].v must be at least 0\n"
      "further issues past the first 64 were not reported"));
  CHECK(listed.error().issues.empty());
}

TEST_CASE("a result is checked before it is encoded") {
  const auto tools = scrydantic::bind<^^checked_results>();
  const auto& measure = named(tools, "measure");
  check_invalid(measure.invoke(json(R"({"level":-1})")),
                "result $.level must be at least 0");
  // A valid result that cannot be JSON fails as it did, in encoding.
  check_invalid(measure.invoke(json(R"({"level":1})")), "$.drift must be finite");
}

TEST_CASE("every kind of handler checks its result") {
  using caller_results::Caller;

  SECTION("a tool that takes a context") {
    const auto tools = scrydantic::bind<^^caller_results::echo_to, Caller>();
    REQUIRE(tools.size() == 1);
    const auto echoed = tools[0].invoke(Caller{.user = "ada"}, json(R"({"v":3})"));
    REQUIRE(echoed);
    CHECK(echoed->text == R"({"v":3})");
    check_invalid(tools[0].invoke(Caller{.user = "eve"}, json(R"({"v":3})")),
                  "result $.v must be at least 0");
  }
  SECTION("a toolbox's member function") {
    const auto tools = scrydantic::bind(std::make_shared<checked_results::Desk>());
    REQUIRE(tools.size() == 1);
    const auto tenth = tools[0].invoke(json("{}"));
    REQUIRE(tenth);
    CHECK(tenth->text == R"({"v":10})");
    check_invalid(tools[0].invoke(json("{}")), "result $.v must be at most 10");
  }
  SECTION("a typed handler returning a value") {
    const auto handler = scrydantic::bind_handler<Ranged>(
        [](Ranged value) { return Ranged{value.v + 1}; });
    const auto raised = handler(json(R"({"v":9})"));
    REQUIRE(raised);
    CHECK(raised->text == R"({"v":10})");
    check_invalid(handler(json(R"({"v":10})")), "result $.v must be at most 10");
  }
  SECTION("a typed handler returning a Result") {
    const auto handler = scrydantic::bind_handler<Ranged>(
        [](Ranged value) -> scrydantic::Result<Ranged> { return Ranged{value.v + 1}; });
    check_invalid(handler(json(R"({"v":10})")), "result $.v must be at most 10");
  }
  SECTION("a typed handler with a context") {
    const auto handler = scrydantic::bind_handler<Ranged, Caller>(
        [](const Caller&, Ranged value) { return Ranged{value.v - 1}; });
    check_invalid(handler(Caller{}, json(R"({"v":0})")),
                  "result $.v must be at least 0");
  }
  SECTION("a tool set") {
    const auto set = scrydantic::ToolSet<>::make(scrydantic::bind<^^ranged_tools>());
    REQUIRE(set);
    check_invalid(set->invoke("bad", json("{}")), "result $.v must be at most 10");
  }
}

TEST_CASE("a result type with nothing to check is encoded as it is") {
  const auto tools = scrydantic::bind<^^unchecked_results>();
  const auto mirrored = named(tools, "mirror").invoke(json(R"({"x":1,"y":-2})"));
  REQUIRE(mirrored);
  CHECK(mirrored->text == R"({"x":-1,"y":2})");
  const auto code = named(tools, "code").invoke(json("{}"));
  REQUIRE(code);
  CHECK(code->text == R"({"text":"x1"})");
}
