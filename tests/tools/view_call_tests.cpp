// Calling a tool with arguments the host has already parsed: a view of part of
// a larger document goes to the handler as it is, and answers what the text of
// those arguments answers, without being written out and parsed again.

#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <scrydantic/scrydantic.hpp>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

// A host's call context. Scrydantic only passes it through.
struct Session {
  std::string user{};
};

struct Point {
  std::int32_t x;
  std::int32_t y;
};

} // namespace

namespace trail_tools {

[[= scrydantic::tool{"Walk some steps"}]] inline std::int32_t walk(std::int32_t steps) {
  return steps * 2;
}

[[= scrydantic::tool{"Mark a point"}]] inline std::string mark(std::string label,
                                                               std::vector<Point> at) {
  return label + ":" + std::to_string(at.size());
}

[[= scrydantic::tool{"Rest for a while"}]] inline scrydantic::Status
    rest(std::int32_t minutes) {
  if (minutes > 60) {
    return std::unexpected(scrydantic::tool_error("that is too long", "rest refused"));
  }
  return {};
}

} // namespace trail_tools

namespace hut_tools {

[[= scrydantic::tool{"Sign the hut's book"}]] inline std::string
    sign(const Session& session, std::string note) {
  return session.user + ": " + note;
}

} // namespace hut_tools

namespace {

class Tally {
public:
  [[= scrydantic::tool{"Add to the tally"}]] std::int64_t add(std::int64_t by) {
    total_ += by;
    return total_;
  }

private:
  std::int64_t total_{};
};

using scrydantic::Json;
using scrydantic::JsonView;
using scrydantic::Result;

[[nodiscard]] Json json(std::string text) { return Json{.text = std::move(text)}; }

// What a host holds after it has parsed a whole request once: the view of the
// request's `arguments` member. The view shares the document, so it outlives
// the request's root.
[[nodiscard]] JsonView arguments_of(const std::string_view request) {
  const auto parsed = JsonView::parse(request);
  REQUIRE(parsed);
  const auto call = parsed->find("call");
  REQUIRE(call);
  const auto arguments = call->find("arguments");
  REQUIRE(arguments);
  return *arguments;
}

// A request that calls a tool with `arguments`, spelled as a sender might.
[[nodiscard]] std::string request_with(const std::string_view arguments) {
  return R"({ "id": 7, "call": { "name": "tool", "arguments": )" +
         std::string{arguments} + R"( }, "trace": [1, 2] })";
}

// Whether two calls answered alike: the same result, or the same failure.
[[nodiscard]] bool same_answer(const Result<Json>& left, const Result<Json>& right) {
  if (left.has_value() != right.has_value()) {
    return false;
  }
  if (left) {
    return left->text == right->text;
  }
  return left.error().message == right.error().message &&
         left.error().model_message == right.error().model_message &&
         left.error().issues == right.error().issues;
}

// The tool of `tools` named `name`.
template <typename Context>
[[nodiscard]] const scrydantic::BoundTool<Context>&
named(const std::vector<scrydantic::BoundTool<Context>>& tools,
      const std::string_view name) {
  for (const auto& tool : tools) {
    if (tool.name == name) {
      return tool;
    }
  }
  FAIL("no tool is named " << name);
  return tools.front();
}

// ---- The two forms of a call --------------------------------------------------

// A handler is called through a const ToolFunction with a view as it is with
// text, preceded by the context when it has one, and answers a Result<Json>.
static_assert(std::is_invocable_r_v<Result<Json>, const scrydantic::ToolFunction<>&,
                                    const JsonView&>);
static_assert(
    std::is_invocable_r_v<Result<Json>, const scrydantic::ToolFunction<>&, Json>);
static_assert(
    std::is_invocable_r_v<Result<Json>, const scrydantic::ToolFunction<Session>&,
                          const Session&, const JsonView&>);
static_assert(
    std::is_invocable_r_v<Result<Json>, const scrydantic::ToolFunction<Session>&,
                          const Session&, Json>);
static_assert(
    !std::is_invocable_v<const scrydantic::ToolFunction<Session>&, const JsonView&>);
static_assert(!std::is_invocable_v<const scrydantic::ToolFunction<>&, const Session&,
                                   const JsonView&>);

// A handler stays move-only, and is made from a callable of either form.
static_assert(std::is_nothrow_move_constructible_v<scrydantic::ToolFunction<>>);
static_assert(std::is_nothrow_move_assignable_v<scrydantic::ToolFunction<>>);
static_assert(!std::is_copy_constructible_v<scrydantic::ToolFunction<>>);
static_assert(!std::is_copy_assignable_v<scrydantic::ToolFunction<Session>>);
static_assert(std::is_convertible_v<Result<Json> (*)(const JsonView&),
                                    scrydantic::ToolFunction<>>);
static_assert(
    std::is_convertible_v<Result<Json> (*)(Json), scrydantic::ToolFunction<>>);
static_assert(
    std::is_convertible_v<Result<Json> (*)(const Json&), scrydantic::ToolFunction<>>);
static_assert(std::is_convertible_v<Result<Json> (*)(const Session&, const JsonView&),
                                    scrydantic::ToolFunction<Session>>);
static_assert(std::is_convertible_v<Result<Json> (*)(const Session&, Json),
                                    scrydantic::ToolFunction<Session>>);
// Not from a callable of the other context's form, nor from one that takes
// neither text nor a view.
static_assert(!std::is_convertible_v<Result<Json> (*)(const JsonView&),
                                     scrydantic::ToolFunction<Session>>);
static_assert(!std::is_convertible_v<Result<Json> (*)(const Session&, Json),
                                     scrydantic::ToolFunction<>>);
static_assert(
    !std::is_convertible_v<Result<Json> (*)(std::string), scrydantic::ToolFunction<>>);

TEST_CASE("a tool called with a parsed sub-view answers as the text call does") {
  const auto tools = scrydantic::bind<^^trail_tools>();
  const auto& mark = named(tools, "mark");
  const std::string_view arguments =
      R"({ "label": "cairn", "at": [ {"x": 1, "y": 2}, {"y": 4, "x": 3} ] })";

  // Parsed once, as part of the request that carries it.
  const JsonView view = arguments_of(request_with(arguments));
  const auto from_view = mark.invoke(view);
  REQUIRE(from_view);
  CHECK(from_view->text == R"("cairn:2")");
  CHECK(same_answer(from_view, mark.invoke(json(std::string{arguments}))));

  // The view is only read, so it can be passed again, and to another tool.
  CHECK(same_answer(mark.invoke(view), from_view));
  const auto elsewhere = named(tools, "walk").invoke(view);
  REQUIRE_FALSE(elsewhere);
  CHECK(same_answer(elsewhere,
                    named(tools, "walk").invoke(json(std::string{arguments}))));
}

TEST_CASE("a view call fails as the text call does") {
  const auto tools = scrydantic::bind<^^trail_tools>();
  const auto& mark = named(tools, "mark");
  const auto& rest = named(tools, "rest");

  SECTION("arguments that do not decode") {
    const std::string_view arguments =
        R"({"label":7,"at":[{"x":1,"y":2},{"x":"east"}],"color":"red"})";
    const auto from_view = mark.invoke(arguments_of(request_with(arguments)));
    REQUIRE_FALSE(from_view);
    CHECK(from_view.error().issues.size() == 4);
    CHECK(from_view.error().model_message == "$ contains unknown member \"color\"\n"
                                             "$.label must be a string\n"
                                             "$.at[1].x must be an integer\n"
                                             "$.at[1].y is a required member");
    CHECK(same_answer(from_view, mark.invoke(json(std::string{arguments}))));
  }

  SECTION("a tool that refuses") {
    const auto from_view = rest.invoke(arguments_of(request_with(R"({"minutes":90})")));
    REQUIRE_FALSE(from_view);
    CHECK(from_view.error().model_message == "that is too long");
    CHECK(from_view.error().message == "rest refused");
    CHECK(same_answer(from_view, rest.invoke(json(R"({"minutes":90})"))));
  }

  SECTION("a tool that acknowledges") {
    const auto from_view = rest.invoke(arguments_of(request_with(R"({"minutes":5})")));
    REQUIRE(from_view);
    CHECK(from_view->text == "{}");
  }
}

TEST_CASE("a view that is not an object fails through the argument object's decode") {
  const auto tools = scrydantic::bind<^^trail_tools>();
  const auto& walk = named(tools, "walk");

  for (const std::string_view arguments :
       {"[3]", R"("steps")", "3", "true", "null", R"([{"steps":3}])"}) {
    CAPTURE(arguments);
    const auto from_view = walk.invoke(arguments_of(request_with(arguments)));
    REQUIRE_FALSE(from_view);
    REQUIRE(from_view.error().issues.size() == 1);
    CHECK(from_view.error().issues[0].kind == scrydantic::IssueKind::wrong_type);
    CHECK(from_view.error().issues[0].path.empty());
    CHECK(from_view.error().model_message == "$ must be an object");
    CHECK(same_answer(from_view, walk.invoke(json(std::string{arguments}))));
  }

  // A default-constructed view shows a null.
  const auto from_empty = walk.invoke(JsonView{});
  REQUIRE_FALSE(from_empty);
  CHECK(same_answer(from_empty, walk.invoke(json("null"))));
}

TEST_CASE("text that is not JSON is the text call's failure alone") {
  const auto tools = scrydantic::bind<^^trail_tools>();
  const auto& walk = named(tools, "walk");

  const auto from_text = walk.invoke(json(R"({"steps" 3})"));
  REQUIRE_FALSE(from_text);
  CHECK(from_text.error().message ==
        "tool arguments are not valid JSON: expected ':' at line 1, column 10");
  CHECK(from_text.error().model_message == from_text.error().message);
  REQUIRE(from_text.error().issues.size() == 1);
  CHECK(from_text.error().issues[0].kind == scrydantic::IssueKind::invalid_json);

  // The same characters inside a request the host parsed are a string, which a
  // view shows as one: it is the wrong type, not text that failed to parse.
  const auto from_view = walk.invoke(arguments_of(request_with(R"("{\"steps\" 3}")")));
  REQUIRE_FALSE(from_view);
  CHECK(from_view.error().model_message == "$ must be an object");
}

// The host's parser read `1e2` as a number with an exponent, which is not an
// integer, and the decoder reads that very node. Written out, the number is
// spelled `100`, which a second parse would read as an integer: the path a host
// had to take before a handler read a view.
TEST_CASE("a view call decodes the document the host parsed, not a copy of it") {
  const auto tools = scrydantic::bind<^^trail_tools>();
  const auto& walk = named(tools, "walk");
  const std::string_view arguments = R"({"steps":1e2})";
  const JsonView view = arguments_of(request_with(arguments));

  const auto from_view = walk.invoke(view);
  REQUIRE_FALSE(from_view);
  CHECK(from_view.error().model_message == "$.steps must be an integer");
  CHECK(same_answer(from_view, walk.invoke(json(std::string{arguments}))));

  const Json rewritten = view.to_json();
  CHECK(rewritten.text == R"({"steps":100})");
  const auto reparsed = walk.invoke(rewritten);
  REQUIRE(reparsed);
  CHECK(reparsed->text == "200");
}

// A view is an overload beside text, so a call that spells a Json in braces
// still names the text.
TEST_CASE("arguments spelled in braces are text") {
  const auto set = scrydantic::ToolSet<>::make(scrydantic::bind<^^trail_tools>());
  REQUIRE(set);
  const auto direct = set->find("walk")->invoke({.text = R"({"steps":1})"});
  REQUIRE(direct);
  CHECK(direct->text == "2");
  const auto by_name = set->invoke("walk", {R"({"steps":2})"});
  REQUIRE(by_name);
  CHECK(by_name->text == "4");
}

// ---- Contexts, toolboxes, and handlers -----------------------------------------

TEST_CASE("a tool with a context takes it before the view") {
  const auto tools = scrydantic::bind<^^hut_tools, Session>();
  REQUIRE(tools.size() == 1);
  const Session session{.user = "ada"};
  const std::string_view arguments = R"({"note":"fine weather"})";

  const auto from_view =
      tools[0].invoke(session, arguments_of(request_with(arguments)));
  REQUIRE(from_view);
  CHECK(from_view->text == R"("ada: fine weather")");
  CHECK(same_answer(from_view, tools[0].invoke(session, json(std::string{arguments}))));

  const auto rejected = tools[0].invoke(session, arguments_of(request_with("{}")));
  REQUIRE_FALSE(rejected);
  CHECK(rejected.error().model_message == "$.note is a required member");
}

TEST_CASE("a toolbox member is called with a view on the one object") {
  const auto tools = scrydantic::bind(std::make_shared<Tally>());
  REQUIRE(tools.size() == 1);
  const JsonView view = arguments_of(request_with(R"({"by":5})"));

  const auto first = tools[0].invoke(view);
  REQUIRE(first);
  CHECK(first->text == "5");
  const auto second = tools[0].invoke(json(R"({"by":5})"));
  REQUIRE(second);
  CHECK(second->text == "10");
  const auto third = tools[0].invoke(view);
  REQUIRE(third);
  CHECK(third->text == "15");
}

TEST_CASE("a bind_handler() handler is called with a view or with text") {
  const auto distance = scrydantic::bind_handler<Point>(
      [](const Point point) { return point.x + point.y; });
  const auto from_view = distance(arguments_of(request_with(R"({"y":40,"x":2})")));
  REQUIRE(from_view);
  CHECK(from_view->text == "42");
  CHECK(same_answer(from_view, distance(json(R"({"y":40,"x":2})"))));

  const auto rejected = distance(arguments_of(request_with(R"({"x":2})")));
  REQUIRE_FALSE(rejected);
  CHECK(same_answer(rejected, distance(json(R"({"x":2})"))));

  const auto located = scrydantic::bind_handler<Point, Session>(
      [](const Session& session, const Point point) {
        return session.user + "@" + std::to_string(point.x);
      });
  const auto with_context =
      located(Session{.user = "cy"}, arguments_of(request_with(R"({"x":9,"y":0})")));
  REQUIRE(with_context);
  CHECK(with_context->text == R"("cy@9")");
}

// ---- Tool sets ---------------------------------------------------------------

TEST_CASE("a tool set calls the named tool with a view") {
  const auto set = scrydantic::ToolSet<>::make(scrydantic::bind<^^trail_tools>());
  REQUIRE(set);
  const JsonView view = arguments_of(request_with(R"({"steps":21})"));

  const auto walked = set->invoke("walk", view);
  REQUIRE(walked);
  CHECK(walked->text == "42");
  CHECK(same_answer(walked, set->invoke("walk", json(R"({"steps":21})"))));

  const auto rejected = set->invoke("mark", view);
  REQUIRE_FALSE(rejected);
  CHECK(same_answer(rejected, set->invoke("mark", json(R"({"steps":21})"))));

  const auto unknown = set->invoke("fly", view);
  REQUIRE_FALSE(unknown);
  CHECK(unknown.error().model_message ==
        R"(unknown tool "fly"; known tools: walk, mark, rest)");
  CHECK(same_answer(unknown, set->invoke("fly", json("{}"))));
}

TEST_CASE("a tool set with a context calls the named tool with a view") {
  const auto set =
      scrydantic::ToolSet<Session>::make(scrydantic::bind<^^hut_tools, Session>());
  REQUIRE(set);
  const Session session{.user = "bo"};
  const JsonView view = arguments_of(request_with(R"({"note":"snow"})"));

  const auto signed_book = set->invoke("sign", session, view);
  REQUIRE(signed_book);
  CHECK(signed_book->text == R"("bo: snow")");
  CHECK(same_answer(signed_book,
                    set->invoke("sign", session, json(R"({"note":"snow"})"))));

  const auto unknown = set->invoke("erase", session, view);
  REQUIRE_FALSE(unknown);
  CHECK(unknown.error().model_message == R"(unknown tool "erase"; known tools: sign)");
}

// ---- Handlers written by hand --------------------------------------------------

TEST_CASE("a handler written to read a view is given the view, or the parsed text") {
  // Answers the kind and size of what it was given, which for a view of part of
  // a document is that part.
  scrydantic::BoundTool<> tool{.name = "shape"};
  tool.invoke = [](const JsonView& arguments) -> Result<Json> {
    return Json{.text = std::to_string(arguments.size()) +
                        (arguments.kind() == scrydantic::JsonKind::object ? "" : "?")};
  };
  REQUIRE(tool.invoke);

  const auto from_view =
      tool.invoke(arguments_of(request_with(R"({"a":1,"b":2,"c":3})")));
  REQUIRE(from_view);
  CHECK(from_view->text == "3");
  const auto from_text = tool.invoke(json(R"({"a":1,"b":2})"));
  REQUIRE(from_text);
  CHECK(from_text->text == "2");

  // The handler parses a text call's text, so text that is not JSON never
  // reaches the callable.
  const auto invalid = tool.invoke(json("{"));
  REQUIRE_FALSE(invalid);
  CHECK(
      invalid.error().message ==
      "tool arguments are not valid JSON: unexpected end of input at line 1, column 2");

  // With a context.
  scrydantic::BoundTool<Session> greeting{
      .name = "greet",
      .invoke = [](const Session& session, const JsonView& arguments) -> Result<Json> {
        return Json{.text = scrydantic::escape_json_string(
                        session.user + std::to_string(arguments.size()))};
      },
  };
  const auto greeted = greeting.invoke(Session{.user = "di"}, json("[1,2]"));
  REQUIRE(greeted);
  CHECK(greeted->text == R"("di2")");
}

TEST_CASE("a handler written to read text is given text, as passed or as the view "
          "spells it") {
  // Answers the text it was given, as a JSON string.
  scrydantic::BoundTool<> tool{.name = "echo"};
  tool.invoke = [](const Json& arguments) -> Result<Json> {
    return Json{.text = scrydantic::escape_json_string(arguments.text)};
  };
  REQUIRE(tool.invoke);

  // Text reaches it untouched: neither parsed nor checked.
  const auto from_text = tool.invoke(json(R"({ "b": 2, "a": 1.50 })"));
  REQUIRE(from_text);
  CHECK(from_text->text == R"("{ \"b\": 2, \"a\": 1.50 }")");
  const auto unchecked = tool.invoke(json("{"));
  REQUIRE(unchecked);
  CHECK(unchecked->text == R"("{")");

  // A view reaches it as the view's canonical text.
  const auto from_view =
      tool.invoke(arguments_of(request_with(R"({ "b": 2, "a": 1.50 })")));
  REQUIRE(from_view);
  CHECK(from_view->text == R"("{\"a\":1.5,\"b\":2}")");

  // With a context, and through a set.
  std::vector<scrydantic::BoundTool<Session>> tools{};
  tools.push_back(scrydantic::BoundTool<Session>{
      .name = "echo",
      .invoke = [](const Session& session, Json arguments) -> Result<Json> {
        return Json{.text =
                        scrydantic::escape_json_string(session.user + arguments.text)};
      },
  });
  const auto set = scrydantic::ToolSet<Session>::make(std::move(tools));
  REQUIRE(set);
  const auto echoed = set->invoke("echo", Session{.user = "ed"},
                                  arguments_of(request_with("[ 1 , 2 ]")));
  REQUIRE(echoed);
  CHECK(echoed->text == R"("ed[1,2]")");
}

TEST_CASE("an empty handler tests as empty, and a moved one keeps its form") {
  scrydantic::ToolFunction<> empty{};
  CHECK_FALSE(empty);
  CHECK(empty == nullptr);
  const scrydantic::ToolFunction<Session> null{nullptr};
  CHECK_FALSE(null);

  auto tools = scrydantic::bind<^^trail_tools>();
  scrydantic::ToolFunction<> walk = std::move(tools[0].invoke);
  REQUIRE(walk);
  CHECK(walk != nullptr);
  const auto walked = walk(arguments_of(request_with(R"({"steps":4})")));
  REQUIRE(walked);
  CHECK(walked->text == "8");

  empty = std::move(walk);
  REQUIRE(empty);
  const auto again = empty(json(R"({"steps":5})"));
  REQUIRE(again);
  CHECK(again->text == "10");

  empty = nullptr;
  CHECK_FALSE(empty);
}

} // namespace
