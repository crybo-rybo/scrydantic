// Output schemas: a bound tool and its descriptor carry the schema of what a
// successful call returns, a tool that returns no value carries none, and a
// tool set hands whichever it is through.

#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <scrydantic/scrydantic.hpp>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// The guide's forecast example.
namespace weather {

enum class Unit { celsius, fahrenheit };

struct Reading {
  double degrees;
  Unit unit{Unit::celsius};
  std::optional<std::string> label;
};

[[= scrydantic::tool{"Get the forecast for a city"}]] inline Reading
    forecast(std::string city, std::int32_t days) {
  return Reading{.degrees = static_cast<double>(days),
                 .unit = Unit::celsius,
                 .label = std::move(city)};
}

} // namespace weather

namespace result_tools {

struct Caller {
  std::string user{};
};

// A result that contains itself: its schema is the `$ref` form.
struct Folder {
  std::vector<Folder> folders;
  std::string name;
};

// A result that holds a recursive type without being one: the `$defs` form.
struct Drive {
  Folder root;
};

struct Ticket {
  std::string title;
};

[[= scrydantic::tool{"Count"}]] inline std::int32_t count() { return 3; }

[[= scrydantic::tool{"Look a title up"}]] inline scrydantic::Result<Ticket>
    look_up(std::int32_t id) {
  if (id == 0) {
    return std::unexpected(scrydantic::tool_error("no ticket has id 0"));
  }
  return Ticket{.title = "Fix the lamp"};
}

[[= scrydantic::tool{"Do something"}]] inline void act() {}

[[= scrydantic::tool{"Try something"}]] inline scrydantic::Status attempt() {
  return {};
}

[[= scrydantic::tool{"List a folder"}]] inline Folder list(std::string name) {
  return Folder{.folders = {}, .name = std::move(name)};
}

[[= scrydantic::tool{"Mount a drive"}]] inline scrydantic::Result<Drive> mount() {
  return Drive{.root = Folder{.folders = {}, .name = "/"}};
}

[[= scrydantic::tool{"Return a document"}]] inline scrydantic::Json document() {
  return scrydantic::Json{.text = "[1, 2]"};
}

[[= scrydantic::tool{
    "Return a document, or refuse"}]] inline scrydantic::Result<scrydantic::Json>
    fetched() {
  return scrydantic::Json{.text = "null"};
}

[[= scrydantic::tool{
    "Return a title, if there is one"}]] inline std::optional<std::string>
    title() {
  return std::nullopt;
}

// The schema is of the value, whatever cv-qualifiers the return type carries.
[[= scrydantic::tool{"Return a constant ticket"}]] inline const Ticket fixed() {
  return Ticket{.title = "Fixed"};
}

[[= scrydantic::tool{
    "Return a ticket for the caller"}]] inline scrydantic::Result<const Ticket>
    mine(const Caller& caller) {
  return Ticket{.title = caller.user};
}

class Desk {
public:
  [[= scrydantic::tool{"Open a ticket"}]] Ticket open(std::string title) {
    ++opened_;
    return Ticket{.title = std::move(title)};
  }

  [[= scrydantic::tool{"Close every ticket"}]] void close_all() { opened_ = 0; }

  [[= scrydantic::tool{"Count the caller's tickets"}]] scrydantic::Result<std::int64_t>
      count(const Caller&) const {
    return opened_;
  }

  [[= scrydantic::tool{"Check the desk is open"}]] static scrydantic::Status check() {
    return {};
  }

private:
  std::int64_t opened_{};
};

inline constexpr std::string_view ticket_schema =
    R"({"additionalProperties":false,"properties":{"title":{"type":"string"}},)"
    R"("required":["title"],"type":"object"})";

} // namespace result_tools

namespace {

[[nodiscard]] scrydantic::Json arguments(std::string text) {
  return scrydantic::Json{.text = std::move(text)};
}

// The text of a bound tool's output schema, or "none" when it has none: no
// schema is the word, so the two cannot be confused.
template <typename Context>
[[nodiscard]] std::string output_of(const scrydantic::BoundTool<Context>& tool) {
  return tool.output_schema ? tool.output_schema->text : std::string{"none"};
}

} // namespace

// The descriptor's schema is a constant: the very text of schema_v.
static_assert(scrydantic::describe<^^weather>()[0].output_schema ==
              scrydantic::schema_v<weather::Reading>);
static_assert(scrydantic::describe<^^weather::forecast>()[0].output_schema ==
              scrydantic::schema_v<weather::Reading>);

TEST_CASE("a bound tool carries the schema of what it returns") {
  const auto tools = scrydantic::bind<^^weather>();
  REQUIRE(tools.size() == 1);
  REQUIRE(tools[0].output_schema.has_value());
  CHECK(tools[0].output_schema->text == scrydantic::schema_v<weather::Reading>);
  CHECK(tools[0].output_schema->text ==
        R"({"additionalProperties":false,"properties":{"degrees":{"type":"number"},)"
        R"("label":{"anyOf":[{"type":"string"},{"type":"null"}]},)"
        R"("unit":{"default":"celsius","enum":["celsius","fahrenheit"],"type":"string"}},)"
        R"("required":["degrees","label"],"type":"object"})");

  // The schema describes what a call answers with.
  const auto reading = tools[0].invoke(arguments(R"({"city":"Oslo","days":2})"));
  REQUIRE(reading);
  CHECK(reading->text == R"({"degrees":2,"label":"Oslo","unit":"celsius"})");
  CHECK(scrydantic::decode<weather::Reading>(*reading));
}

TEST_CASE("an output schema is of the value a function returns, alone or in a Result") {
  using result_tools::Caller;
  const auto tools = scrydantic::bind<^^result_tools, Caller>();
  REQUIRE(tools.size() == 11);

  SECTION("a value") {
    CHECK(tools[0].name == "count");
    CHECK(output_of(tools[0]) == scrydantic::schema_v<std::int32_t>);
    CHECK(output_of(tools[0]) ==
          R"({"maximum":2147483647,"minimum":-2147483648,"type":"integer"})");
  }
  SECTION("a Result of a value") {
    CHECK(tools[1].name == "look_up");
    CHECK(output_of(tools[1]) == result_tools::ticket_schema);
    // A refusal is not a result, so the schema says nothing of it.
    CHECK_FALSE(tools[1].invoke(Caller{}, arguments(R"({"id":0})")));
  }
  SECTION("void and Status have none") {
    CHECK(tools[2].name == "act");
    CHECK_FALSE(tools[2].output_schema.has_value());
    CHECK(tools[3].name == "attempt");
    CHECK_FALSE(tools[3].output_schema.has_value());
    // What such a tool answers with is an acknowledgment.
    CHECK(tools[2].invoke(Caller{}, arguments("{}"))->text == "{}");
    CHECK(tools[3].invoke(Caller{}, arguments("{}"))->text == "{}");
  }
  SECTION("a recursive result") {
    CHECK(tools[4].name == "list");
    CHECK(output_of(tools[4]) == scrydantic::schema_v<result_tools::Folder>);
    CHECK(output_of(tools[4]) ==
          R"({"additionalProperties":false,"properties":{"folders":{"items":{)"
          R"("$ref":"#"},"type":"array"},"name":{"type":"string"}},)"
          R"("required":["folders","name"],"type":"object"})");
    CHECK(tools[5].name == "mount");
    CHECK(output_of(tools[5]) == scrydantic::schema_v<result_tools::Drive>);
    CHECK(output_of(tools[5]) ==
          R"({"$defs":{"result_tools.Folder":{"additionalProperties":false,)"
          R"("properties":{"folders":{"items":{"$ref":"#/$defs/result_tools.Folder"},)"
          R"("type":"array"},"name":{"type":"string"}},"required":["folders","name"],)"
          R"("type":"object"}},"additionalProperties":false,"properties":{"root":{)"
          R"("$ref":"#/$defs/result_tools.Folder"}},"required":["root"],)"
          R"("type":"object"})");
  }
  SECTION("a Json is any value, which is a schema, not the lack of one") {
    CHECK(tools[6].name == "document");
    REQUIRE(tools[6].output_schema.has_value());
    CHECK(tools[6].output_schema->text == "{}");
    CHECK(tools[7].name == "fetched");
    CHECK(output_of(tools[7]) == "{}");
  }
  SECTION("an optional value") {
    CHECK(tools[8].name == "title");
    CHECK(output_of(tools[8]) == R"({"anyOf":[{"type":"string"},{"type":"null"}]})");
  }
  SECTION("a const value, alone or in a Result") {
    CHECK(tools[9].name == "fixed");
    CHECK(output_of(tools[9]) == result_tools::ticket_schema);
    CHECK(tools[10].name == "mine");
    CHECK(output_of(tools[10]) == result_tools::ticket_schema);
  }
}

TEST_CASE("a context is no part of a tool's output schema") {
  using result_tools::Caller;
  const auto tools = scrydantic::bind<^^result_tools::mine, Caller>();
  REQUIRE(tools.size() == 1);
  CHECK(output_of(tools[0]) == result_tools::ticket_schema);
  CHECK(
      tools[0].input_schema.text ==
      R"({"additionalProperties":false,"properties":{},"required":[],"type":"object"})");

  const auto mine = tools[0].invoke(Caller{.user = "ada"}, arguments("{}"));
  REQUIRE(mine);
  CHECK(mine->text == R"({"title":"ada"})");
}

TEST_CASE("a toolbox's member tools carry their output schemas") {
  using result_tools::Caller;
  using result_tools::Desk;
  const auto tools = scrydantic::bind<Caller>(std::make_shared<Desk>());
  REQUIRE(tools.size() == 4);

  CHECK(tools[0].name == "open");
  CHECK(output_of(tools[0]) == result_tools::ticket_schema);
  CHECK(tools[1].name == "close_all");
  CHECK(output_of(tools[1]) == "none");
  CHECK(tools[2].name == "count");
  CHECK(output_of(tools[2]) == scrydantic::schema_v<std::int64_t>);
  CHECK(tools[3].name == "check");
  CHECK(output_of(tools[3]) == "none");

  // A toolbox bound without a context has the same results.
  const auto plain = scrydantic::bind(std::make_shared<Desk>());
  REQUIRE(plain.size() == 4);
  CHECK(output_of(plain[0]) == result_tools::ticket_schema);
  CHECK(output_of(plain[2]) == scrydantic::schema_v<std::int64_t>);
}

TEST_CASE("descriptors hold the output schemas that binding gives the tools") {
  using result_tools::Caller;
  using result_tools::Desk;

  static constexpr auto described = scrydantic::describe<^^result_tools, Caller>();
  static_assert(described.size() == 11);
  static_assert(described[0].output_schema == scrydantic::schema_v<std::int32_t>);
  static_assert(described[1].output_schema == result_tools::ticket_schema);
  // Empty when the tool returns no value.
  static_assert(described[2].output_schema.empty());
  static_assert(described[3].output_schema.empty());
  static_assert(described[4].output_schema ==
                scrydantic::schema_v<result_tools::Folder>);
  static_assert(described[5].output_schema ==
                scrydantic::schema_v<result_tools::Drive>);
  static_assert(described[6].output_schema == "{}");
  static_assert(described[10].output_schema == result_tools::ticket_schema);

  static constexpr auto desk = scrydantic::describe<Desk, Caller>();
  static_assert(desk.size() == 4);
  static_assert(desk[0].output_schema == result_tools::ticket_schema);
  static_assert(desk[1].output_schema.empty());
  static_assert(desk[2].output_schema == scrydantic::schema_v<std::int64_t>);
  static_assert(desk[3].output_schema.empty());

  const auto bound = scrydantic::bind<^^result_tools, Caller>();
  REQUIRE(bound.size() == described.size());
  for (std::size_t index = 0; index < described.size(); ++index) {
    INFO(described[index].name);
    CHECK(bound[index].output_schema.has_value() ==
          !described[index].output_schema.empty());
    if (bound[index].output_schema) {
      CHECK(bound[index].output_schema->text == described[index].output_schema);
    }
  }
}

TEST_CASE("a tool set hands output schemas through unchanged") {
  auto made = scrydantic::ToolSet<result_tools::Caller>::make(
      scrydantic::bind<^^result_tools, result_tools::Caller>());
  REQUIRE(made);

  const auto* const look_up = made->find("look_up");
  REQUIRE(look_up != nullptr);
  CHECK(output_of(*look_up) == result_tools::ticket_schema);
  const auto* const act = made->find("act");
  REQUIRE(act != nullptr);
  CHECK_FALSE(act->output_schema.has_value());
  CHECK(output_of(made->tools()[6]) == "{}");

  const auto released = made->release();
  REQUIRE(released.size() == 11);
  CHECK(output_of(released[1]) == result_tools::ticket_schema);
  CHECK(output_of(released[2]) == "none");
}

TEST_CASE("a tool built by hand may leave its output schema empty or supply one") {
  using result_tools::Ticket;
  auto next_id = std::make_shared<std::atomic<std::int64_t>>(1);

  std::vector<scrydantic::BoundTool<>> tools{};
  // bind_handler() returns the handler alone, so the host fills in the rest.
  tools.push_back(scrydantic::BoundTool<>{
      .name = "open_ticket",
      .description = "Open a ticket",
      .input_schema =
          scrydantic::Json{.text = std::string{scrydantic::input_schema_v<Ticket>}},
      .output_schema =
          scrydantic::Json{.text = std::string{scrydantic::schema_v<std::int64_t>}},
      .invoke = scrydantic::bind_handler<Ticket>(
          [next_id](Ticket) { return next_id->fetch_add(1); }),
  });
  tools.push_back(scrydantic::BoundTool<>{
      .name = "peek_ticket",
      .invoke = scrydantic::bind_handler<Ticket>(
          [next_id](Ticket) { return next_id->load(); }),
  });

  const auto set = scrydantic::ToolSet<>::make(std::move(tools));
  REQUIRE(set);
  CHECK(output_of(*set->find("open_ticket")) ==
        R"({"maximum":9223372036854775807,"minimum":-9223372036854775808,)"
        R"("type":"integer"})");
  CHECK_FALSE(set->find("peek_ticket")->output_schema.has_value());

  const auto opened = set->invoke("open_ticket", arguments(R"({"title":"Fix"})"));
  REQUIRE(opened);
  CHECK(opened->text == "1");
  const auto peeked = set->invoke("peek_ticket", arguments(R"({"title":"Fix"})"));
  REQUIRE(peeked);
  CHECK(peeked->text == "2");
}
