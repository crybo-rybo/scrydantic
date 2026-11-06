// Tool binding: free functions, namespaces, toolboxes, and typed handlers become
// BoundTools whose handlers decode, call, and encode, with or without a call
// context of the caller's choosing.

#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <scrydantic/detail/binding.hpp>
#include <scrydantic/scrydantic.hpp>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

// A host's call context. Scrydantic only passes it through.
struct Session {
  std::string user{};
};

struct Position {
  std::int32_t x{};
  std::int32_t y{};
};

} // namespace

namespace grid_tools {

[[= scrydantic::tool{"Move one tile east"}]] inline Position step_east(Position from) {
  return Position{.x = from.x + 1, .y = from.y};
}

[[ = scrydantic::tool{"Add two numbers"},
   = scrydantic::name{"sum"} ]] inline std::int32_t add(std::int32_t left,
                                                        std::int32_t right) {
  return left + right;
}

[[= scrydantic::tool{"Refuse every call"}]] inline scrydantic::Status refuse() {
  return std::unexpected(scrydantic::tool_error("not today", "refused a call"));
}

} // namespace grid_tools

namespace session_tools {

[[= scrydantic::tool{"Greet the session's user"}]] inline std::string
    greet(const Session& session, std::string greeting) {
  return greeting + ", " + session.user;
}

} // namespace session_tools

namespace bad_text_tools {

struct Note {
  std::string text{};
};

[[= scrydantic::tool{"Return bytes that are not UTF-8"}]] inline std::string
    raw_bytes() {
  return std::string(1, '\xff');
}

[[= scrydantic::tool{"Return them in a member"}]] inline Note raw_member() {
  return Note{.text = std::string(1, '\xff')};
}

[[= scrydantic::tool{"Return them in a Result"}]] inline scrydantic::Result<std::string>
    raw_result() {
  return std::string(1, '\xff');
}

} // namespace bad_text_tools

namespace {

class Counter {
public:
  [[= scrydantic::tool{"Add to the counter"}]] std::int64_t bump(std::int64_t by) {
    total_ += by;
    return total_;
  }

  [[= scrydantic::tool{"Read the counter for a user"}]] std::string
      read(const Session& session) const {
    return session.user + ":" + std::to_string(total_);
  }

  [[nodiscard]] std::int64_t total() const noexcept { return total_; }

private:
  std::int64_t total_{};
};

[[nodiscard]] scrydantic::Json json(std::string text) {
  return scrydantic::Json{.text = std::move(text)};
}

[[nodiscard]] std::vector<std::string>
names_of(const std::vector<scrydantic::BoundTool<>>& tools) {
  std::vector<std::string> names{};
  for (const auto& tool : tools) {
    names.push_back(tool.name);
  }
  return names;
}

} // namespace

static_assert(scrydantic::Toolbox<Counter, Session>);
// Without a context, read's Session parameter is not a context at all: a lone
// aggregate parameter is the argument object the model supplies.
static_assert(scrydantic::Toolbox<Counter>);
// bump is not const.
static_assert(!scrydantic::Toolbox<const Counter, Session>);

// ---- Diagnostics -------------------------------------------------------------

namespace {

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

struct Area {
  int operator()(const Position& position) const { return position.x * position.y; }
};

struct TakesText {
  int operator()(const std::string& text) const {
    return static_cast<int>(text.size());
  }
};

} // namespace

// Each diagnostic is empty exactly when its concept holds.
static_assert(scrydantic::toolbox_diagnostic<Counter, Session>().empty());
static_assert(mentions(scrydantic::toolbox_diagnostic<const Counter, Session>(),
                       "does not satisfy scrydantic::Toolbox: "));
static_assert(mentions(scrydantic::toolbox_diagnostic<const Counter, Session>(),
                       "bump"));
static_assert(scrydantic::tool_handler_diagnostic<Area, Position>().empty());
static_assert(mentions(scrydantic::tool_handler_diagnostic<TakesText, Position>(),
                       "is not invocable as"));
// An argument type that is not ToolArguments is the reason, as for the concept.
static_assert(!scrydantic::ToolHandlerFor<TakesText, std::string>);
static_assert(mentions(scrydantic::tool_handler_diagnostic<TakesText, std::string>(),
                       "does not satisfy scrydantic::ToolArguments: "));
static_assert(scrydantic::tools_diagnostic<^^grid_tools>().empty());
static_assert(scrydantic::tools_diagnostic<^^session_tools, Session>().empty());
// A function without a tool annotation does not bind, and the text names the
// call the user wrote.
static_assert(mentions(scrydantic::tools_diagnostic<^^json>(), "scrydantic::bind<^^"));
static_assert(mentions(scrydantic::tools_diagnostic<^^json>("mylib::add"),
                       "mylib::add<^^"));

TEST_CASE("a namespace binds its tools in declaration order") {
  auto tools = scrydantic::bind<^^grid_tools>();

  REQUIRE(names_of(tools) == std::vector<std::string>{"step_east", "sum", "refuse"});
  CHECK(tools[0].description == "Move one tile east");
  CHECK(tools[0].input_schema.text == scrydantic::input_schema_v<Position>);
  CHECK(tools[1].input_schema.text ==
        R"({"additionalProperties":false,"properties":{"left":{"maximum":2147483647,)"
        R"("minimum":-2147483648,"type":"integer"},"right":{"maximum":2147483647,)"
        R"("minimum":-2147483648,"type":"integer"}},"required":["left","right"],)"
        R"("type":"object"})");
  CHECK(
      tools[2].input_schema.text ==
      R"({"additionalProperties":false,"properties":{},"required":[],"type":"object"})");

  const auto stepped = tools[0].invoke(json(R"({"x":1,"y":2})"));
  REQUIRE(stepped);
  CHECK(stepped->text == R"({"x":2,"y":2})");

  const auto sum = tools[1].invoke(json(R"({"left":2,"right":40})"));
  REQUIRE(sum);
  CHECK(sum->text == "42");
}

TEST_CASE("bound handlers report decode and handler failures as Errors") {
  auto tools = scrydantic::bind<^^grid_tools>();
  REQUIRE(tools.size() == 3);

  const auto missing = tools[1].invoke(json(R"({"left":2})"));
  REQUIRE_FALSE(missing);
  CHECK(missing.error().message == "$.right is a required member");
  CHECK(missing.error().model_message == "$.right is a required member");

  const auto malformed = tools[1].invoke(json("{"));
  REQUIRE_FALSE(malformed);
  CHECK(malformed.error().message == "tool arguments are not valid JSON");
  CHECK(malformed.error().model_message == "tool arguments are not valid JSON");

  const auto refused = tools[2].invoke(json("{}"));
  REQUIRE_FALSE(refused);
  CHECK(refused.error().message == "refused a call");
  CHECK(refused.error().model_message == "not today");
}

TEST_CASE("bound handlers give the model every issue in their arguments") {
  auto tools = scrydantic::bind<^^grid_tools>();
  REQUIRE(tools.size() == 3);

  const auto sum = tools[1].invoke(json(R"({"extra":true,"left":"two"})"));
  REQUIRE_FALSE(sum);
  constexpr std::string_view listed = "$ contains unknown member \"extra\"\n"
                                      "$.left must be an integer\n"
                                      "$.right is a required member";
  CHECK(sum.error().model_message == listed);
  CHECK(sum.error().message == listed);
  CHECK(sum.error().issues == std::vector<scrydantic::Issue>{
                                  {.kind = scrydantic::IssueKind::unknown_member,
                                   .reason = R"(contains unknown member "extra")"},
                                  {.kind = scrydantic::IssueKind::wrong_type,
                                   .path = {"left"},
                                   .reason = "must be an integer"},
                                  {.kind = scrydantic::IssueKind::missing_member,
                                   .path = {"right"},
                                   .reason = "is a required member"},
                              });

  // A typed handler's argument object reports the same way.
  auto handler = scrydantic::bind_handler<Position>([](Position) { return 0; });
  const auto stepped = handler(json(R"({"x":1.5,"y":null})"));
  REQUIRE_FALSE(stepped);
  CHECK(stepped.error().model_message ==
        "$.x must be an integer\n$.y must be an integer");

  // Arguments that are not JSON are one issue; the handler's own failures,
  // whether refusals or results that cannot be JSON, are none.
  const auto malformed = tools[1].invoke(json("{"));
  REQUIRE_FALSE(malformed);
  CHECK(malformed.error().issues ==
        std::vector<scrydantic::Issue>{{.kind = scrydantic::IssueKind::invalid_json,
                                        .reason = "is not valid JSON"}});
  const auto refused = tools[2].invoke(json("{}"));
  REQUIRE_FALSE(refused);
  CHECK(refused.error().issues.empty());
  const auto unencodable =
      scrydantic::bind<^^bad_text_tools>().front().invoke(json("{}"));
  REQUIRE_FALSE(unencodable);
  CHECK(unencodable.error().issues.empty());
}

TEST_CASE("bound handlers refuse a result that is not valid JSON") {
  constexpr std::string_view reason =
      "the value holds a string that is not valid UTF-8, so it cannot be JSON";
  const std::string invalid(1, '\xff');
  // Invalid UTF-8 is the one thing a supported value can hold that the writer
  // passes through, so the public encoder is the reference for the verdict.
  REQUIRE_FALSE(scrydantic::encode(invalid));
  REQUIRE(scrydantic::encode(invalid).error().message == reason);

  const auto check_refused = [&](const scrydantic::Result<scrydantic::Json>& result) {
    REQUIRE_FALSE(result);
    CHECK(result.error().message == reason);
    // It describes the handler's own value, so the model gets nothing from it.
    CHECK(result.error().model_message.empty());
  };

  SECTION("an annotated function") {
    auto tools = scrydantic::bind<^^bad_text_tools>();
    REQUIRE(tools.size() == 3);
    check_refused(tools[0].invoke(json("{}")));
    check_refused(tools[1].invoke(json("{}")));
    check_refused(tools[2].invoke(json("{}")));
  }
  SECTION("a typed handler returning a value") {
    auto handler =
        scrydantic::bind_handler<Position>([&](Position) { return invalid; });
    check_refused(handler(json(R"({"x":0,"y":0})")));
  }
  SECTION("a typed handler returning a Result") {
    auto handler = scrydantic::bind_handler<Position>(
        [&](Position) -> scrydantic::Result<std::string> { return invalid; });
    check_refused(handler(json(R"({"x":0,"y":0})")));
  }
  SECTION("a typed handler with a context") {
    auto handler = scrydantic::bind_handler<Position, Session>(
        [&](const Session&, Position) { return invalid; });
    check_refused(handler(Session{}, json(R"({"x":0,"y":0})")));
  }
}

TEST_CASE("bound handlers pass well-formed multi-byte text through") {
  auto handler = scrydantic::bind_handler<Position>(
      [](Position) { return std::string{"caf\xc3\xa9"}; });
  const auto result = handler(json(R"({"x":0,"y":0})"));
  REQUIRE(result);
  CHECK(result->text == "\"caf\xc3\xa9\"");
}

TEST_CASE("one function binds alone") {
  auto tools = scrydantic::bind<^^grid_tools::add>();
  REQUIRE(tools.size() == 1);
  CHECK(tools.front().name == "sum");
  CHECK(tools.front().description == "Add two numbers");
}

TEST_CASE("a context reaches tools that take it first and is not an argument") {
  auto tools = scrydantic::bind<^^session_tools, Session>();
  REQUIRE(tools.size() == 1);
  // The context parameter is not part of the argument object.
  CHECK(tools.front().input_schema.text ==
        R"({"additionalProperties":false,"properties":{"greeting":{"type":"string"}},)"
        R"("required":["greeting"],"type":"object"})");

  const Session session{.user = "ada"};
  const auto greeted = tools.front().invoke(session, json(R"({"greeting":"hello"})"));
  REQUIRE(greeted);
  CHECK(greeted->text == R"("hello, ada")");
}

TEST_CASE("a shared toolbox binds its member tools to the object") {
  auto counter = std::make_shared<Counter>();
  auto tools = scrydantic::bind<Session>(counter);

  REQUIRE(tools.size() == 2);
  CHECK(tools[0].name == "bump");
  CHECK(tools[1].name == "read");

  const Session session{.user = "bo"};
  REQUIRE(tools[0].invoke(session, json(R"({"by":5})")));
  REQUIRE(tools[0].invoke(session, json(R"({"by":2})")));
  CHECK(counter->total() == 7);

  const auto read = tools[1].invoke(session, json("{}"));
  REQUIRE(read);
  CHECK(read->text == R"("bo:7")");

  // The handlers share the toolbox, so it outlives the host's pointer.
  counter.reset();
  const auto again = tools[1].invoke(session, json("{}"));
  REQUIRE(again);
  CHECK(again->text == R"("bo:7")");

  CHECK(scrydantic::bind<Session>(std::shared_ptr<Counter>{}).empty());
}

TEST_CASE("typed handlers bind with and without a context") {
  auto plain = scrydantic::bind_handler<Position>(
      [](Position position) { return position.x * position.y; });
  const auto product = plain(json(R"({"x":6,"y":7})"));
  REQUIRE(product);
  CHECK(product->text == "42");

  auto acknowledging = scrydantic::bind_handler<Position, Session>(
      [](const Session& session, Position) -> scrydantic::Status {
        if (session.user.empty()) {
          return std::unexpected(scrydantic::tool_error("sign in first"));
        }
        return {};
      });
  const auto accepted = acknowledging(Session{.user = "cy"}, json(R"({"x":0,"y":0})"));
  REQUIRE(accepted);
  CHECK(accepted->text == "{}");
  const auto rejected = acknowledging(Session{}, json(R"({"x":0,"y":0})"));
  REQUIRE_FALSE(rejected);
  CHECK(rejected.error().model_message == "sign in first");
  CHECK(rejected.error().message == "sign in first");
}

// ---- Results -----------------------------------------------------------------

namespace {

// Copyable but not movable: the destructor suppresses the implicit move
// constructor, and the class is still an aggregate. Each destruction is counted,
// so a test sees every copy that encoding a result makes.
struct CopyOnlyResult {
  static inline int destructions = 0;
  std::vector<int> values;

  // NOLINTNEXTLINE(cppcoreguidelines-special-member-functions): the point.
  ~CopyOnlyResult() { ++destructions; }
};

} // namespace

TEST_CASE("a handler's result is encoded where it lands, without a copy") {
  CopyOnlyResult::destructions = 0;
  const auto run = [](auto body) {
    auto handler = scrydantic::bind_handler<Position>(std::move(body));
    return handler(json(R"({"x":0,"y":0})"));
  };

  scrydantic::Result<scrydantic::Json> result{};
  SECTION("a bare value") {
    result = run([](Position) { return CopyOnlyResult{.values = {1, 2, 3}}; });
  }
  SECTION("a Result") {
    result = run([](Position) -> scrydantic::Result<CopyOnlyResult> {
      return scrydantic::Result<CopyOnlyResult>{std::in_place,
                                                std::vector<int>{1, 2, 3}};
    });
  }
  REQUIRE(result);
  CHECK(result->text == R"({"values":[1,2,3]})");
  // The one value the handler returned, and no copy of it.
  CHECK(CopyOnlyResult::destructions == 1);
}

TEST_CASE("encoding a named result reads it in place") {
  CopyOnlyResult::destructions = 0;
  SECTION("a bare value") {
    const CopyOnlyResult value{.values = {1, 2, 3}};
    const auto encoded = scrydantic::detail::encode_handler_result(value);
    REQUIRE(encoded);
    CHECK(encoded->text == R"({"values":[1,2,3]})");
    CHECK(CopyOnlyResult::destructions == 0);
  }
  SECTION("a Result passed as an rvalue") {
    scrydantic::Result<CopyOnlyResult> value{std::in_place, std::vector<int>{1, 2, 3}};
    const auto encoded = scrydantic::detail::encode_handler_result(std::move(value));
    REQUIRE(encoded);
    CHECK(encoded->text == R"({"values":[1,2,3]})");
    CHECK(CopyOnlyResult::destructions == 0);
  }
  CHECK(CopyOnlyResult::destructions == 1);
}

TEST_CASE("encoding a named failed Result leaves the caller's error in place") {
  // Mutable, so that moving from the caller's error, rather than copying it,
  // would empty the strings checked below.
  scrydantic::Result<CopyOnlyResult> value = std::unexpected(scrydantic::Error{
      .message = "application rejected arguments",
      .model_message = "rejected",
  });
  const auto encoded = scrydantic::detail::encode_handler_result(value);
  REQUIRE_FALSE(encoded);
  CHECK(encoded.error().message == "application rejected arguments");
  CHECK(encoded.error().model_message == "rejected");
  REQUIRE_FALSE(value);
  CHECK(value.error().message == "application rejected arguments");
  CHECK(value.error().model_message == "rejected");
}

TEST_CASE("descriptors describe tools at compile time without handlers") {
  static constexpr auto grid = scrydantic::describe<^^grid_tools>();
  static_assert(grid.size() == 3);
  static_assert(grid[1].name == "sum");
  static_assert(grid[1].description == "Add two numbers");

  static constexpr auto counter = scrydantic::describe<Counter, Session>();
  static_assert(counter.size() == 2);
  static_assert(counter[1].input_schema ==
                R"({"additionalProperties":false,"properties":{},"required":[],)"
                R"("type":"object"})");
  // Without the context, read takes a Session from the model instead.
  static constexpr auto contextless = scrydantic::describe<Counter>();
  static_assert(contextless[1].input_schema == scrydantic::input_schema_v<Session>);

  const auto bound = scrydantic::bind<^^grid_tools>();
  REQUIRE(bound.size() == grid.size());
  for (std::size_t index = 0; index < grid.size(); ++index) {
    CHECK(bound[index].name == grid[index].name);
    CHECK(bound[index].description == grid[index].description);
    CHECK(bound[index].input_schema.text == grid[index].input_schema);
  }
}
